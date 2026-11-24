#include "Checkpoint/GinkgoCheckpointSubsystem.h"

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "Algo/StableSort.h"
#include "Checkpoint/GinkgoCheckpoint.h"
#include "Checkpoint/GinkgoCheckpointEnemySpawn.h"
#include "Checkpoint/GinkgoCheckpointSaveNode.h"
#include "Character/GinkgoCharacter.h"
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/StreamableManager.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PlatformFeatures.h"
#include "SaveGame/GinkgoSaveGameLibrary.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "SaveGameSystem.h"
#include "UI/GinkgoTitleSubsystem.h"
#include "Weapon/GinkgoWeaponComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoCheckpoint, Log, All);

void UGinkgoCheckpointSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGinkgoSaveGameSubsystem>();
	GetGameInstance()->GetSubsystem<UGinkgoSaveGameSubsystem>()->GetRegistry().Register(MakeShared<FGinkgoCheckpointSaveNode>());
	FWorldDelegates::LevelAddedToWorld.AddUObject(this, &ThisClass::HandleLevelAdded);
	FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &ThisClass::HandleWorldBeginTearDown);
}

void UGinkgoCheckpointSubsystem::Deinitialize()
{
	CancelAllPendingEnemySpawns();
	FallbackStreamableManager.Reset();
	FWorldDelegates::OnWorldBeginTearDown.RemoveAll(this);
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	if (auto* Save = GetGameInstance()->GetSubsystem<UGinkgoSaveGameSubsystem>()) { Save->GetRegistry().Unregister(FGinkgoCheckpointSaveNode::NodeGuid); }
	Super::Deinitialize();
}

UGinkgoCheckpointSubsystem* UGinkgoCheckpointSubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UGinkgoCheckpointSubsystem>() : nullptr;
}

FString UGinkgoCheckpointSubsystem::MakeStableId(const AActor& Actor, const FName ExplicitId)
{
	return UWorld::RemovePIEPrefix(ExplicitId.IsNone() ? Actor.GetPathName()
		: Actor.GetWorld()->GetOutermost()->GetName() + TEXT(":") + ExplicitId.ToString());
}

bool UGinkgoCheckpointSubsystem::Activate(AGinkgoCheckpoint& Checkpoint, AGinkgoCharacter* Player)
{
	auto* Controller = Player ? Cast<AGinkgoPlayerController>(Player->GetController()) : nullptr;
	auto* Save = UGinkgoSaveGameSubsystem::Get(this);
	if (!Checkpoint.HasAuthority() || !Controller || !Player->HasAuthority() || Player->GetDamageable()->IsDead()
		|| Player->GetWorld() != Checkpoint.GetWorld() || !Save || Save->IsLoadingSaveGame()
		|| bPlayerRestorePending || Checkpoint.Announcement.LocationName.IsEmpty())
	{
		return false;
	}
	const FString Id = MakeStableId(Checkpoint, Checkpoint.CheckpointId);
	if (!Checkpoint.bRepeatable && State.ReachedCheckpoints.Contains(Id)) { return false; }
	const FGinkgoCheckpointState Previous = State;
	State.bHasCheckpoint = true;
	State.CheckpointId = Id;
	State.MapPackage = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	State.LocationName = Checkpoint.Announcement.LocationName;
	State.RespawnTransform = Checkpoint.GetRespawnTransform();
	State.ReachedCheckpoints.AddUnique(Id);
	State.Health = Player->GetDamageable()->GetHealth();
	State.MaxHealth = Player->GetDamageable()->GetMaxHealth();
	State.Armor = Player->GetDamageable()->GetArmor();
	State.bHasFlashbang = Player->GetFlashbang()->HasItem();
	State.Weapons = Player->GetWeapons()->CaptureSaveState();
	if (const UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(this))
	{
		State.Difficulty = Title->GetDifficulty();
		State.bHasSavedDifficulty = true;
	}
	FGinkgoCheckpointAnnouncement Announcement = Checkpoint.Announcement;
	Announcement.bSaved = Save->Save(SaveSlotName);
	if (!Announcement.bSaved) { State = Previous; }
	Controller->ClientAnnounceCheckpoint(Announcement);
	UE_LOG(LogGinkgoCheckpoint, Log, TEXT("[Checkpoint] %s 地点=%s 存档=%s 已击杀=%d，标注开始前已完成保存尝试"),
		*Id, *Announcement.LocationName.ToString(), Announcement.bSaved ? TEXT("成功") : *Save->GetLastError(), State.DefeatedEnemies.Num());
	if (Announcement.bSaved)
	{
		TArray<AGinkgoCheckpointEnemySpawn*> Markers;
		for (TActorIterator<AGinkgoCheckpointEnemySpawn> It(Checkpoint.GetWorld()); It; ++It)
		{
			if (It->Checkpoint == &Checkpoint && It->CanSpawnForProgress(*this))
			{
				Markers.Add(*It);
			}
		}
		EnqueueEnemySpawnsSorted(Markers, Player->GetActorLocation());
		Checkpoint.OnEnemyRefreshRequested(Player);
	}
	return Announcement.bSaved;
}

bool UGinkgoCheckpointSubsystem::RestartFromCheckpoint()
{
	auto* Save = UGinkgoSaveGameSubsystem::Get(this);
	return GetWorld() && GetWorld()->GetNetMode() != NM_Client && Save && Save->Load(SaveSlotName, true);
}

bool UGinkgoCheckpointSubsystem::HasAvailableCheckpoint() const
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return false;
	}
	const UGinkgoSaveGameSubsystem* Save = GetGameInstance() ? GetGameInstance()->GetSubsystem<UGinkgoSaveGameSubsystem>() : nullptr;
	if (!Save || Save->IsLoadingSaveGame())
	{
		return false;
	}
	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	return SaveSystem && SaveSystem->DoesSaveGameExist(*SaveSlotName, 0);
}

void UGinkgoCheckpointSubsystem::ClearCheckpointForNewGame()
{
	CancelAllPendingEnemySpawns();
	State = FGinkgoCheckpointState();
	bPlayerRestorePending = false;
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return;
	}
	if (ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem(); SaveSystem && SaveSystem->DoesSaveGameExist(*SaveSlotName, 0))
	{
		SaveSystem->DeleteGame(false, *SaveSlotName, 0);
		UE_LOG(LogGinkgoCheckpoint, Log, TEXT("[Checkpoint] 开始新游戏：已清除旧检查点存档槽 %s"), *SaveSlotName);
	}
}

bool UGinkgoCheckpointSubsystem::GetRespawnTransform(FTransform& OutTransform) const
{
	if (!State.bHasCheckpoint || !GetWorld() || UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()) != State.MapPackage) { return false; }
	OutTransform = State.RespawnTransform;
	return true;
}

void UGinkgoCheckpointSubsystem::RecordEnemyDeath(const AGinkgoAICharacter& Enemy)
{
	if (!Enemy.HasAuthority()) { return; }
	// 运行时刷出的敌人必须由生成脚本提供 ID；自动编号不能跨地图重载当作稳定身份。
	if (Enemy.PersistentEnemyId.IsNone() && !UGinkgoSaveGameLibrary::WasObjectLoaded(&Enemy)) { return; }
	State.DefeatedEnemies.AddUnique(MakeStableId(Enemy, Enemy.PersistentEnemyId));
}

bool UGinkgoCheckpointSubsystem::IsEnemyDefeated(const AGinkgoAICharacter& Enemy) const
{
	return State.DefeatedEnemies.Contains(MakeStableId(Enemy, Enemy.PersistentEnemyId));
}

void UGinkgoCheckpointSubsystem::RemoveDefeatedEnemy(AGinkgoAICharacter& Enemy)
{
	if (!Enemy.HasAuthority() || !IsEnemyDefeated(Enemy)) { return; }
	const auto* LoadingSave = UGinkgoSaveGameSubsystem::Get(this);
	if (LoadingSave && LoadingSave->IsLoadingSaveGame() && !bApplyingLoadedState) { return; }
	if (auto* AI = Cast<AGinkgoAIController>(Enemy.GetController())) { AI->StopAI(); }
	const FSoftObjectPath Path(&Enemy);
	Enemy.Destroy();
	// 这些销毁由稳定敌人 ID 管理，不能再由只支持持久关卡名字的 Core.DestroyedActors 重复解释。
	if (auto* Save = UGinkgoSaveGameSubsystem::Get(this)) { Save->GetDestroyedLevelActors().Remove(Path); }
}

void UGinkgoCheckpointSubsystem::HandleLevelAdded(ULevel* Level, UWorld* World)
{
	if (World != GetWorld() || !Level || World->GetNetMode() == NM_Client) { return; }
	const TArray<TObjectPtr<AActor>> Actors = Level->Actors;
	for (AActor* Actor : Actors)
	{
		if (auto* Enemy = Cast<AGinkgoAICharacter>(Actor)) { RemoveDefeatedEnemy(*Enemy); }
	}
}

void UGinkgoCheckpointSubsystem::ApplyLoadedState()
{
	TGuardValue<bool> ApplyingGuard(bApplyingLoadedState, true);
	CancelAllPendingEnemySpawns();
	bPlayerRestorePending = State.bHasCheckpoint;
	if (State.bHasSavedDifficulty)
	{
		if (UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(this))
		{
			Title->SetDifficulty(State.Difficulty);
		}
	}
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client) { return; }
	for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It) { RemoveDefeatedEnemy(**It); }

	TArray<AGinkgoCheckpointEnemySpawn*> Markers;
	for (TActorIterator<AGinkgoCheckpointEnemySpawn> It(World); It; ++It)
	{
		// 读档立即清除旧敌人，避免旧状态敌人在异步重建完成前继续活动或射击玩家。
		It->DestroySpawnedEnemy();
		if (It->CanSpawnForProgress(*this))
		{
			Markers.Add(*It);
		}
	}

	FVector PriorityOrigin = FVector::ZeroVector;
	if (State.bHasCheckpoint)
	{
		PriorityOrigin = State.RespawnTransform.GetLocation();
	}
	else if (const APlayerController* FirstPC = World->GetFirstPlayerController())
	{
		if (const APawn* Pawn = FirstPC->GetPawn())
		{
			PriorityOrigin = Pawn->GetActorLocation();
		}
	}
	EnqueueEnemySpawnsSorted(Markers, PriorityOrigin);

	if (APlayerController* PC = World->GetFirstPlayerController()) { RestorePendingPlayer(*PC); }
}

void UGinkgoCheckpointSubsystem::HandleWorldBeginTearDown(UWorld* World)
{
	if (World && (World == PendingSpawnWorld.Get() || World == GetWorld()))
	{
		CancelAllPendingEnemySpawns();
	}
}

void UGinkgoCheckpointSubsystem::EnqueueEnemySpawn(AGinkgoCheckpointEnemySpawn& Marker, const bool bReload)
{
	UWorld* World = Marker.GetWorld();
	if (!World || World->GetNetMode() == NM_Client) { return; }
	if (bReload)
	{
		CancelPendingEnemySpawn(Marker);
		Marker.DestroySpawnedEnemy();
	}
	if (!Marker.CanSpawnForProgress(*this)) { return; }
	if (PendingSpawnWorld.IsValid() && PendingSpawnWorld.Get() != World)
	{
		CancelAllPendingEnemySpawns();
	}
	PendingSpawnWorld = World;

	const bool bAlreadyQueued = PendingEnemySpawns.ContainsByPredicate([&Marker](const FPendingEnemySpawn& Entry)
	{
		return Entry.Marker.Get() == &Marker;
	});
	if (bAlreadyQueued) { return; }

	FPendingEnemySpawn& Added = PendingEnemySpawns.AddDefaulted_GetRef();
	Added.Marker = &Marker;
	Added.Generation = EnemySpawnQueueGeneration;

	PumpEnemySpawnPreloads();
	if (!PendingEnemySpawns.IsEmpty())
	{
		EnsureEnemySpawnTicker();
	}
}

void UGinkgoCheckpointSubsystem::EnqueueEnemySpawnsSorted(TArray<AGinkgoCheckpointEnemySpawn*>& Markers, const FVector& PriorityOrigin)
{
	if (Markers.IsEmpty()) { return; }

	UWorld* World = Markers[0] ? Markers[0]->GetWorld() : GetWorld();
	if (!World || World->GetNetMode() == NM_Client) { return; }
	if (PendingSpawnWorld.IsValid() && PendingSpawnWorld.Get() != World)
	{
		CancelAllPendingEnemySpawns();
	}
	PendingSpawnWorld = World;

	Algo::StableSort(Markers, [&PriorityOrigin](const AGinkgoCheckpointEnemySpawn* A, const AGinkgoCheckpointEnemySpawn* B)
	{
		return FVector::DistSquared(A->GetActorLocation(), PriorityOrigin) < FVector::DistSquared(B->GetActorLocation(), PriorityOrigin);
	});

	int32 EnqueuedCount = 0;
	for (AGinkgoCheckpointEnemySpawn* Marker : Markers)
	{
		if (!Marker || !Marker->CanSpawnForProgress(*this)) { continue; }
		const bool bAlreadyQueued = PendingEnemySpawns.ContainsByPredicate([Marker](const FPendingEnemySpawn& Entry)
		{
			return Entry.Marker.Get() == Marker;
		});
		if (bAlreadyQueued) { continue; }

		FPendingEnemySpawn& Added = PendingEnemySpawns.AddDefaulted_GetRef();
		Added.Marker = Marker;
		Added.Generation = EnemySpawnQueueGeneration;
		++EnqueuedCount;
	}

	if (EnqueuedCount > 0)
	{
		UE_LOG(LogGinkgoCheckpoint, Log, TEXT("[Checkpoint] 已入队 %d 名待异步分帧生成的敌人（总待生成=%d，每帧上限=%d，并发预载上限=%d）"),
			EnqueuedCount, PendingEnemySpawns.Num(), FMath::Max(1, MaxEnemySpawnsPerFrame), FMath::Max(1, MaxConcurrentEnemyPreloads));
	}

	PumpEnemySpawnPreloads();
	if (!PendingEnemySpawns.IsEmpty())
	{
		EnsureEnemySpawnTicker();
	}
}

void UGinkgoCheckpointSubsystem::CancelPendingEnemySpawn(const AGinkgoCheckpointEnemySpawn& Marker)
{
	for (int32 Index = PendingEnemySpawns.Num() - 1; Index >= 0; --Index)
	{
		if (PendingEnemySpawns[Index].Marker.Get() == &Marker)
		{
			ReleasePendingSpawnHandles(PendingEnemySpawns[Index], /*bCancel*/ true);
			PendingEnemySpawns.RemoveAt(Index);
		}
	}
	if (PendingEnemySpawns.IsEmpty())
	{
		StopEnemySpawnTicker();
	}
}

void UGinkgoCheckpointSubsystem::CancelAllPendingEnemySpawns()
{
	++EnemySpawnQueueGeneration;
	for (FPendingEnemySpawn& Entry : PendingEnemySpawns)
	{
		ReleasePendingSpawnHandles(Entry, /*bCancel*/ true);
	}
	PendingEnemySpawns.Reset();
	PendingSpawnWorld.Reset();
	StopEnemySpawnTicker();
}

void UGinkgoCheckpointSubsystem::ReleasePendingSpawnHandles(FPendingEnemySpawn& Entry, const bool bCancel)
{
	for (TSharedPtr<FStreamableHandle>& Handle : Entry.StreamableHandles)
	{
		if (!Handle.IsValid()) { continue; }
		if (bCancel)
		{
			Handle->CancelHandle();
		}
		else
		{
			Handle->ReleaseHandle();
		}
	}
	Entry.StreamableHandles.Reset();
	Entry.ActiveHandle.Reset();
}

FStreamableManager& UGinkgoCheckpointSubsystem::GetStreamableManager()
{
	if (UAssetManager::IsInitialized())
	{
		return UAssetManager::GetStreamableManager();
	}
	if (!FallbackStreamableManager.IsValid())
	{
		FallbackStreamableManager = MakeShared<FStreamableManager>();
	}
	return *FallbackStreamableManager;
}

bool UGinkgoCheckpointSubsystem::AdvanceEnemySpawnPreload(FPendingEnemySpawn& Entry)
{
	constexpr int32 MaxPreloadHops = 3;
	TGuardValue<bool> ReentrancyGuard(bAdvancingEnemyPreload, true);

	AGinkgoCheckpointEnemySpawn* Marker = Entry.Marker.Get();
	if (!Marker || Marker->IsActorBeingDestroyed() || !Marker->CanSpawnForProgress(*this))
	{
		return false;
	}

	while (Entry.Phase != FPendingEnemySpawn::EPhase::ReadyToSpawn)
	{
		if (Entry.Phase == FPendingEnemySpawn::EPhase::Preloading)
		{
			if (Entry.ActiveHandle.IsValid() && !Entry.ActiveHandle->HasLoadCompleted())
			{
				return true;
			}
			Entry.ActiveHandle.Reset();
			++Entry.PreloadHop;
		}

		if (Entry.PreloadHop >= MaxPreloadHops)
		{
			Entry.Phase = FPendingEnemySpawn::EPhase::ReadyToSpawn;
			return true;
		}

		TArray<FSoftObjectPath> MissingPaths;
		Marker->CollectUnloadedAssets(Entry.AttemptedPaths, MissingPaths);
		if (MissingPaths.IsEmpty())
		{
			Entry.Phase = FPendingEnemySpawn::EPhase::ReadyToSpawn;
			return true;
		}

		for (const FSoftObjectPath& Path : MissingPaths)
		{
			Entry.AttemptedPaths.Add(Path);
		}

		const FStreamableDelegate OnLoaded = FStreamableDelegate::CreateUObject(
			this, &ThisClass::HandleEnemyPreloadCompleted, Entry.Marker, Entry.Generation);
		TSharedPtr<FStreamableHandle> Handle = GetStreamableManager().RequestAsyncLoad(
			MoveTemp(MissingPaths), OnLoaded, FStreamableManager::DefaultAsyncLoadPriority);
		if (!Handle.IsValid())
		{
			++Entry.PreloadHop;
			continue;
		}

		Entry.StreamableHandles.Add(Handle);
		Entry.ActiveHandle = Handle;
		Entry.Phase = FPendingEnemySpawn::EPhase::Preloading;
		if (!Handle->HasLoadCompleted())
		{
			return true;
		}
	}
	return true;
}

void UGinkgoCheckpointSubsystem::HandleEnemyPreloadCompleted(
	const TWeakObjectPtr<AGinkgoCheckpointEnemySpawn> WeakMarker, const uint64 ExpectedGeneration)
{
	if (bAdvancingEnemyPreload || ExpectedGeneration != EnemySpawnQueueGeneration || !WeakMarker.IsValid()) { return; }

	const int32 Index = PendingEnemySpawns.IndexOfByPredicate([&WeakMarker](const FPendingEnemySpawn& Entry)
	{
		return Entry.Marker == WeakMarker;
	});
	if (Index == INDEX_NONE) { return; }

	// 异步加载回调仅推进级联预载跳数，不直接 SpawnActor，落地仍严格遵循每帧预算。
	if (!AdvanceEnemySpawnPreload(PendingEnemySpawns[Index]))
	{
		ReleasePendingSpawnHandles(PendingEnemySpawns[Index], /*bCancel*/ true);
		PendingEnemySpawns.RemoveAt(Index);
		if (PendingEnemySpawns.IsEmpty())
		{
			StopEnemySpawnTicker();
		}
	}
}

void UGinkgoCheckpointSubsystem::PumpEnemySpawnPreloads()
{
	const int32 PreloadLimit = FMath::Max(1, MaxConcurrentEnemyPreloads);
	int32 ActivePreloads = 0;

	for (int32 Index = 0; Index < PendingEnemySpawns.Num(); )
	{
		FPendingEnemySpawn& Entry = PendingEnemySpawns[Index];
		AGinkgoCheckpointEnemySpawn* Marker = Entry.Marker.Get();
		if (!Marker || Marker->IsActorBeingDestroyed() || !Marker->CanSpawnForProgress(*this))
		{
			ReleasePendingSpawnHandles(Entry, /*bCancel*/ true);
			PendingEnemySpawns.RemoveAt(Index);
			continue;
		}

		if (Entry.Phase == FPendingEnemySpawn::EPhase::ReadyToSpawn)
		{
			++Index;
			continue;
		}

		if (Entry.Phase == FPendingEnemySpawn::EPhase::Preloading || ActivePreloads < PreloadLimit)
		{
			if (!AdvanceEnemySpawnPreload(Entry))
			{
				ReleasePendingSpawnHandles(Entry, /*bCancel*/ true);
				PendingEnemySpawns.RemoveAt(Index);
				continue;
			}
			if (Entry.Phase == FPendingEnemySpawn::EPhase::Preloading)
			{
				++ActivePreloads;
			}
		}

		++Index;
	}
}

void UGinkgoCheckpointSubsystem::ProcessPendingEnemySpawns()
{
	if (PendingEnemySpawns.IsEmpty())
	{
		StopEnemySpawnTicker();
		return;
	}

	UWorld* World = GetWorld();
	if (!World || World->bIsTearingDown || World->GetNetMode() == NM_Client
		|| (PendingSpawnWorld.IsValid() && PendingSpawnWorld.Get() != World))
	{
		CancelAllPendingEnemySpawns();
		return;
	}

	if (World->IsPaused()) { return; }
	if (const auto* Save = UGinkgoSaveGameSubsystem::Get(this); Save && Save->IsLoadingSaveGame() && !bApplyingLoadedState)
	{
		return;
	}

	PumpEnemySpawnPreloads();

	const int32 SpawnLimit = FMath::Max(1, MaxEnemySpawnsPerFrame);
	const double BudgetSeconds = FMath::Max(0.5, static_cast<double>(MaxEnemySpawnFrameBudgetMs)) * 0.001;
	const double StartSeconds = FPlatformTime::Seconds();
	int32 SpawnedThisFrame = 0;

	while (SpawnedThisFrame < SpawnLimit)
	{
		const int32 ReadyIndex = PendingEnemySpawns.IndexOfByPredicate([](const FPendingEnemySpawn& Entry)
		{
			return Entry.Phase == FPendingEnemySpawn::EPhase::ReadyToSpawn;
		});
		if (ReadyIndex == INDEX_NONE) { break; }

		FPendingEnemySpawn ReadyEntry = MoveTemp(PendingEnemySpawns[ReadyIndex]);
		PendingEnemySpawns.RemoveAt(ReadyIndex);

		if (AGinkgoCheckpointEnemySpawn* Marker = ReadyEntry.Marker.Get())
		{
			Marker->ApplyProgress(*this, /*bReload*/ false);
		}
		ReleasePendingSpawnHandles(ReadyEntry, /*bCancel*/ false);
		++SpawnedThisFrame;

		if (SpawnedThisFrame < SpawnLimit && (FPlatformTime::Seconds() - StartSeconds) >= BudgetSeconds)
		{
			break;
		}
	}

	// 腾出队首位置后立刻预热后续出生位，让后台 I/O 与前台逐帧落地无缝衔接。
	if (!PendingEnemySpawns.IsEmpty())
	{
		PumpEnemySpawnPreloads();
	}
	if (PendingEnemySpawns.IsEmpty())
	{
		StopEnemySpawnTicker();
	}
}

void UGinkgoCheckpointSubsystem::EnsureEnemySpawnTicker()
{
	if (!EnemySpawnTickerHandle.IsValid())
	{
		EnemySpawnTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::TickEnemySpawnQueue));
	}
}

void UGinkgoCheckpointSubsystem::StopEnemySpawnTicker()
{
	if (EnemySpawnTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(EnemySpawnTickerHandle);
		EnemySpawnTickerHandle.Reset();
	}
}

bool UGinkgoCheckpointSubsystem::TickEnemySpawnQueue(const float DeltaTime)
{
	ProcessPendingEnemySpawns();
	if (PendingEnemySpawns.IsEmpty())
	{
		EnemySpawnTickerHandle.Reset();
		return false;
	}
	return true;
}

void UGinkgoCheckpointSubsystem::RestorePendingPlayer(AController& Controller)
{
	FTransform Transform;
	auto* Player = Cast<AGinkgoCharacter>(Controller.GetPawn());
	if (!bPlayerRestorePending || !Player || !Player->HasActorBegunPlay() || !GetRespawnTransform(Transform)) { return; }
	Player->GetCharacterMovement()->StopMovementImmediately();
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Player->SetActorLocationAndRotation(Transform.GetLocation(), Transform.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
	Controller.SetControlRotation(Transform.Rotator());
	const bool bVitals = Player->GetDamageable()->RestoreSaveState(State.Health, State.MaxHealth, State.Armor);
	Player->GetFlashbang()->RestoreInventory(State.bHasFlashbang);
	const bool bWeapons = Player->GetWeapons()->RestoreSaveState(State.Weapons);
	bPlayerRestorePending = false;
	UE_LOG(LogGinkgoCheckpoint, Log, TEXT("[Checkpoint] 恢复玩家 生命=%d 武器=%d 复活点=%s"), bVitals, bWeapons, *Transform.GetLocation().ToString());
}

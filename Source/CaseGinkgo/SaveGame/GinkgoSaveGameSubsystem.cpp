#include "SaveGame/GinkgoSaveGameSubsystem.h"

#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "SaveGame/GinkgoSaveGameLibrary.h"
#include "SaveGame/GinkgoSaveGameObject.h"
#include "SaveGame/GinkgoSaveGameSerializer.h"
#include "SaveGame/SaveNode/Core/GinkgoCoreActorsSaveNode.h"
#include "SaveGame/SaveNode/Core/GinkgoCoreDestroyedActorsSaveNode.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Selection.h"
#endif

void UGinkgoSaveGameSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &ThisClass::OnWorldInitialized);
	FWorldDelegates::OnWorldInitializedActors.AddUObject(this, &ThisClass::OnActorsInitialized);
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::OnWorldCleanup);

	// 框架内置的节点。各系统在自己的 Initialize 里注册自己的节点
	NodeRegistry.Register(MakeShared<FGinkgoCoreActorsSaveNode>());
	NodeRegistry.Register(MakeShared<FGinkgoCoreDestroyedActorsSaveNode>());

	OnWorldInitialized(GetWorld(), UWorld::InitializationValues());
}

void UGinkgoSaveGameSubsystem::Deinitialize()
{
	Super::Deinitialize();

	FWorldDelegates::OnPostWorldInitialization.RemoveAll(this);
	FWorldDelegates::OnWorldInitializedActors.RemoveAll(this);
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);

	NodeRegistry.Unregister(FGinkgoCoreDestroyedActorsSaveNode::NodeGuid);
	NodeRegistry.Unregister(FGinkgoCoreActorsSaveNode::NodeGuid);
}

bool UGinkgoSaveGameSubsystem::Save(const FString& SlotName)
{
	LastError.Reset();
	if (IsLoadingSaveGame())
	{
		LastError = TEXT("正在读取存档。");
		return false;
	}

	OnBeforeSaveGame.Broadcast();

	TGinkgoSaveGameSerializer<false> BinarySerializer(this);
	const bool bBinarySuccess = BinarySerializer.Save(SlotName);
	if (!bBinarySuccess)
	{
		LastError = TEXT("存档写入失败，详情见日志。");
	}

#if !UE_BUILD_SHIPPING && WITH_TEXT_ARCHIVE_SUPPORT
	// 开发期多写一份 JSON，看存档里有什么
	TGinkgoSaveGameSerializer<false, true> TextSerializer(this);
	TextSerializer.Save(SlotName);
#endif

	return bBinarySuccess;
}

bool UGinkgoSaveGameSubsystem::Load(const FString& SlotName, const bool bReloadMap)
{
	LastError.Reset();
	if (IsLoadingSaveGame())
	{
		LastError = TEXT("正在读取存档。");
		return false;
	}

#if WITH_EDITOR
	// 编辑器选中着被读档销毁的 Actor 时会崩
	if (GEditor)
	{
		GEditor->GetSelectedActors()->DeselectAll();
		GEditor->RedrawLevelEditingViewports();
	}
#endif

	const TSharedRef<TGinkgoSaveGameSerializer<true>> BinarySerializer = MakeShared<TGinkgoSaveGameSerializer<true>>(this);
	CurrentSerializer = BinarySerializer.ToSharedPtr();
	const bool bStarted = BinarySerializer->Load(SlotName, bReloadMap);
	if (!bStarted)
	{
		CurrentSerializer.Reset();
		if (LastError.IsEmpty())
		{
			LastError = TEXT("未找到可用存档，或存档数据无效。");
		}
	}
	return bStarted;
}

bool UGinkgoSaveGameSubsystem::IsLoadingSaveGame() const
{
	return CurrentSerializer.IsValid();
}

void UGinkgoSaveGameSubsystem::OnWorldInitialized(UWorld* InWorld, const UWorld::InitializationValues)
{
	if (!IsValid(InWorld) || GetWorld() != InWorld || HookedWorld == InWorld)
	{
		return;
	}

	HookedWorld = InWorld;
	InWorld->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::OnActorPreSpawn));
	InWorld->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateUObject(this, &ThisClass::OnActorDestroyed));
}

void UGinkgoSaveGameSubsystem::OnActorsInitialized(const FActorsInitializedParams& Params)
{
	if (!IsValid(Params.World) || GetWorld() != Params.World)
	{
		return;
	}

	for (TActorIterator<AActor> It(Params.World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && Actor->Implements<UGinkgoSaveGameObject>())
		{
			SaveGameActors.Add(Actor);
		}
	}
}

void UGinkgoSaveGameSubsystem::OnWorldCleanup(UWorld* InWorld, bool, bool)
{
	if (!IsValid(InWorld) || GetWorld() != InWorld)
	{
		return;
	}

	SaveGameActors.Reset();
	DestroyedLevelActors.Reset();
}

void UGinkgoSaveGameSubsystem::OnActorPreSpawn(AActor* InActor)
{
	if (IsValid(InActor) && InActor->Implements<UGinkgoSaveGameObject>())
	{
		SaveGameActors.Add(InActor);
	}
}

void UGinkgoSaveGameSubsystem::OnActorDestroyed(AActor* InActor)
{
	SaveGameActors.Remove(InActor);

	if (UGinkgoSaveGameLibrary::WasObjectLoaded(InActor))
	{
		DestroyedLevelActors.Add(InActor);
	}
}

void UGinkgoSaveGameSubsystem::OnLoadComplete()
{
	CurrentSerializer = nullptr;

	// 全部节点走完 PostLoadGame 之后才广播，订阅者可以放心查任何节点的状态
	OnSaveGameLoaded.Broadcast();
}

UGinkgoSaveGameSubsystem* UGinkgoSaveGameSubsystem::Get(UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UGinkgoSaveGameSubsystem>() : nullptr;
}

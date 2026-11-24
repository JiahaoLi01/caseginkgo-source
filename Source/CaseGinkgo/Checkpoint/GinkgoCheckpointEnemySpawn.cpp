#include "Checkpoint/GinkgoCheckpointEnemySpawn.h"

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoEnemyArchetype.h"
#include "Checkpoint/GinkgoCheckpoint.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Presentation/GinkgoDialogueComponent.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "Weapon/GinkgoModularWeaponData.h"
#include "Weapon/GinkgoWeaponAttachment.h"
#include "Weapon/GinkgoWeaponDefinition.h"

AGinkgoCheckpointEnemySpawn::AGinkgoCheckpointEnemySpawn()
{
	EnemyClass = AGinkgoAICharacter::StaticClass();
	Points.Reset();
#if WITH_EDITORONLY_DATA
	if (auto* Facing = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("SpawnFacing")))
	{
		Facing->SetupAttachment(GetRootComponent());
		Facing->SetRelativeLocation(FVector(0., 0., 75.));
		Facing->SetArrowColor(FLinearColor(1.f, .35f, .1f));
		Facing->SetHiddenInGame(true);
	}
#endif
}

void AGinkgoCheckpointEnemySpawn::PostLoad()
{
	Super::PostLoad();
	BehaviorOverrides.MigrateLegacy();
}

void AGinkgoCheckpointEnemySpawn::BeginPlay()
{
	Super::BeginPlay();
	auto* Progress = UGinkgoCheckpointSubsystem::Get(this);
	const auto* Save = UGinkgoSaveGameSubsystem::Get(this);
	if (Progress && Save && !Save->IsLoadingSaveGame())
	{
		Progress->EnqueueEnemySpawn(*this);
	}
}

void AGinkgoCheckpointEnemySpawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (auto* Progress = UGinkgoCheckpointSubsystem::Get(this))
	{
		Progress->CancelPendingEnemySpawn(*this);
	}
	// 子关卡可以在 GC 前重新显示。卸载时明确收回旧实例，重新进入时再按当前进度生成。
	if (EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::RemovedFromWorld)
	{
		DestroySpawnedEnemy();
	}
	else
	{
		SpawnedEnemy.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AGinkgoCheckpointEnemySpawn::Destroyed()
{
	// 尚未 BeginPlay 就被销毁的出生位不会走 EndPlay，需在此同步取消未落地的异步生成请求。
	if (auto* Progress = UGinkgoCheckpointSubsystem::Get(this))
	{
		Progress->CancelPendingEnemySpawn(*this);
	}
	Super::Destroyed();
}

void AGinkgoCheckpointEnemySpawn::DestroySpawnedEnemy()
{
	if (SpawnedEnemy.IsValid())
	{
		SpawnedEnemy->Destroy();
	}
	SpawnedEnemy.Reset();
}

bool AGinkgoCheckpointEnemySpawn::CanSpawnForProgress(const UGinkgoCheckpointSubsystem& Progress, FName* OutEnemyId) const
{
	// 关卡标记不复制，在客户端也可能具有本地 Authority，必须以世界的网络模式为准。
	if (GetNetMode() == NM_Client || IsActorBeingDestroyed() || !GetWorld()) { return false; }
	if (SpawnedEnemy.IsValid() || !Checkpoint || !EnemyClass) { return false; }
	const FGinkgoCheckpointState& State = Progress.GetState();
	if (!State.ReachedCheckpoints.Contains(Progress.MakeStableId(*Checkpoint, Checkpoint->CheckpointId))) { return false; }

	// 生成顺序、Actor Label 和 PIE 实例号都不参与存档身份，同一出生位跨次加载仍对应同一人。
	const FName EnemyId(*FString::Printf(TEXT("Encounter/%s"), *Progress.MakeStableId(*this)));
	if (State.DefeatedEnemies.Contains(Progress.MakeStableId(*this, EnemyId))) { return false; }

	if (OutEnemyId)
	{
		*OutEnemyId = EnemyId;
	}
	return true;
}

void AGinkgoCheckpointEnemySpawn::CollectUnloadedAssets(const TSet<FSoftObjectPath>& AlreadyAttempted, TArray<FSoftObjectPath>& OutMissingPaths) const
{
	if (!EnemyClass) { return; }

	const auto AddIfMissing = [&AlreadyAttempted, &OutMissingPaths](const FSoftObjectPath& Path)
	{
		if (Path.IsNull() || AlreadyAttempted.Contains(Path) || Path.ResolveObject() != nullptr)
		{
			return;
		}
		OutMissingPaths.AddUnique(Path);
	};

	const bool bNeedsVisualAssets = GetNetMode() != NM_DedicatedServer;

	// 基础角色网格、动画蓝图与全局命中音效
	if (const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get())
	{
		AddIfMissing(Settings->CharacterMesh.ToSoftObjectPath());
		AddIfMissing(Settings->CharacterAnimClass.ToSoftObjectPath());
		const FGinkgoHitSounds& HitSounds = Settings->HitSounds;
		for (const EGinkgoHitSound Kind : {EGinkgoHitSound::Normal, EGinkgoHitSound::Armor, EGinkgoHitSound::ArmorBreak, EGinkgoHitSound::Head, EGinkgoHitSound::Kill, EGinkgoHitSound::HeadKill})
		{
			if (const FGinkgoHitSound* Item = HitSounds.Get(Kind))
			{
				AddIfMissing(Item->Sound.ToSoftObjectPath());
			}
		}
	}

	const AGinkgoAICharacter* Defaults = EnemyClass.GetDefaultObject();
	if (!Defaults) { return; }

	// 第一跳：敌人类型、AI 配置、起始武器覆盖与台词表
	const TSoftObjectPtr<UGinkgoEnemyArchetype>& EffectiveEnemyType = !EnemyType.IsNull() ? EnemyType : Defaults->EnemyType;
	const TSoftObjectPtr<UGinkgoAIConfig>& EffectiveDefinition = !DefinitionOverride.IsNull() ? DefinitionOverride : Defaults->Definition;
	const TSoftObjectPtr<UGinkgoWeaponDefinition>& EffectiveWeaponOverride = !WeaponOverride.IsNull() ? WeaponOverride : Defaults->StartingWeaponOverride;

	AddIfMissing(EffectiveEnemyType.ToSoftObjectPath());
	AddIfMissing(EffectiveDefinition.ToSoftObjectPath());
	AddIfMissing(EffectiveWeaponOverride.ToSoftObjectPath());
	if (Defaults->Dialogue)
	{
		AddIfMissing(Defaults->Dialogue->DialogueSet.ToSoftObjectPath());
	}

	// 第二跳：从已驻留的敌人类型与 AI 配置中发现外观模型与默认武器定义
	if (bNeedsVisualAssets)
	{
		if (const UGinkgoEnemyArchetype* Archetype = EffectiveEnemyType.Get())
		{
			AddIfMissing(Archetype->Model.ToSoftObjectPath());
		}
	}

	const UGinkgoWeaponDefinition* ResolvedWeapon = EffectiveWeaponOverride.Get();
	if (!ResolvedWeapon && EffectiveWeaponOverride.IsNull())
	{
		if (const UGinkgoAIConfig* Config = EffectiveDefinition.Get())
		{
			AddIfMissing(Config->Weapon.ToSoftObjectPath());
			ResolvedWeapon = Config->Weapon.Get();
		}
	}

	// 第三跳：从已驻留的武器定义中收集枪械网格、开火特效、弹种资产与音频，避免首帧同步卡顿
	if (ResolvedWeapon)
	{
		AddIfMissing(ResolvedWeapon->SkeletalMesh.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->FireAttenuation.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->MuzzleEffect.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->MuzzleFlashOnly.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->MuzzleWisp.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->MuzzleHeat.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->Rounds.LiveRound.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->Rounds.SpentCase.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->Rounds.EjectMaterial.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->Rounds.HitSoundHard.ToSoftObjectPath());
		AddIfMissing(ResolvedWeapon->Rounds.HitSoundSoft.ToSoftObjectPath());

		if (bNeedsVisualAssets && ResolvedWeapon->Modularity)
		{
			AddIfMissing(ResolvedWeapon->Modularity->Object.ToSoftObjectPath());
		}

		for (const TPair<FGameplayTag, FGinkgoWeaponCue>& Cue : ResolvedWeapon->Cues)
		{
			AddIfMissing(Cue.Value.Sound.ToSoftObjectPath());
		}
		for (const FGinkgoWeaponAttachmentMount& Mount : ResolvedWeapon->Attachments)
		{
			if (!Mount.Part) { continue; }
			AddIfMissing(Mount.Part->FireSound.ToSoftObjectPath());
			if (bNeedsVisualAssets && Mount.bDefault)
			{
				AddIfMissing(Mount.Part->Mesh.ToSoftObjectPath());
				AddIfMissing(Mount.Part->ModuleMesh.ToSoftObjectPath());
			}
		}
		if (ResolvedWeapon->AmmoType.IsValid())
		{
			for (const FGameplayTag& Choice : UGinkgoAmmoDefinition::GetChoices(ResolvedWeapon->AmmoType))
			{
				if (const UGinkgoAmmoDefinition* ChoiceAmmo = UGinkgoAmmoDefinition::Find(Choice))
				{
					AddIfMissing(ChoiceAmmo->ProjectileMesh.ToSoftObjectPath());
					AddIfMissing(ChoiceAmmo->LiveRoundMesh.ToSoftObjectPath());
					AddIfMissing(ChoiceAmmo->RoundMaterial.ToSoftObjectPath());
					AddIfMissing(ChoiceAmmo->FireSound.ToSoftObjectPath());
					AddIfMissing(ChoiceAmmo->Trail.System.ToSoftObjectPath());
				}
			}
		}
	}
}

void AGinkgoCheckpointEnemySpawn::ApplyProgress(const UGinkgoCheckpointSubsystem& Progress, const bool bReload)
{
	if (GetNetMode() == NM_Client) { return; }
	if (bReload)
	{
		DestroySpawnedEnemy();
	}
	FName EnemyId = NAME_None;
	if (!CanSpawnForProgress(Progress, &EnemyId)) { return; }

	const auto* Defaults = EnemyClass.GetDefaultObject();
	const FTransform Transform(FRotator(0., GetActorRotation().Yaw, 0.),
		GetActorLocation() + FVector::UpVector * Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	FActorSpawnParameters Params;
	Params.OverrideLevel = GetLevel();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
	Params.bDeferConstruction = true;
	auto* Enemy = GetWorld()->SpawnActor<AGinkgoAICharacter>(EnemyClass, Transform, Params);
	if (!Enemy)
	{
		UE_LOG(LogTemp, Warning, TEXT("敌人出生位 %s 生成失败，请检查胶囊周围的空间。"), *GetPathName());
		return;
	}
	SpawnedEnemy = Enemy;
	Enemy->PersistentEnemyId = EnemyId;
	Enemy->PatrolRoute = this;
	if (!EnemyType.IsNull()) { Enemy->EnemyType = EnemyType; }
	if (!DefinitionOverride.IsNull()) { Enemy->Definition = DefinitionOverride; }
	if (!WeaponOverride.IsNull()) { Enemy->StartingWeaponOverride = WeaponOverride; }
	BehaviorOverrides.ApplyTo(Enemy->BehaviorOverrides);
	Enemy->FinishSpawning(Enemy->GetActorTransform());
}

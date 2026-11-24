#include "AI/GinkgoAICharacter.h"

#include "AI/GinkgoAICharacterMovementComponent.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoEnemyArchetype.h"
#include "Character/GinkgoCharacterAudioComponent.h"
#include "Character/GinkgoCharacterModel.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "AI/GinkgoAIController.h"
#include "Camera/GinkgoCameraContextComponent.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Framework/GinkgoEventHub.h"
#include "Presentation/GinkgoDialogueComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Net/UnrealNetwork.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

AGinkgoAICharacter::AGinkgoAICharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGinkgoAICharacterMovementComponent>(CharacterMovementComponentName))
{
	AIControllerClass = AGinkgoAIController::StaticClass();
	CreateDefaultSubobject<UGinkgoEventHub>(TEXT("EventHub"));
	Dialogue = CreateDefaultSubobject<UGinkgoDialogueComponent>(TEXT("Dialogue"));
	Dialogue->DialogueSet = TSoftObjectPtr<UGinkgoDialogueSet>(FSoftObjectPath(TEXT("/Game/AI/Dialogue/DA_EnemyDialogue.DA_EnemyDialogue")));
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	EnemyType = TSoftObjectPtr<UGinkgoEnemyArchetype>(FSoftObjectPath(TEXT("/Game/AI/Enemies/DA_Enemy_Unarmored.DA_Enemy_Unarmored")));
	Definition = TSoftObjectPtr<UGinkgoAIConfig>(FSoftObjectPath(TEXT("/Game/AI/DA_Enemy.DA_Enemy")));
	GetDamageable()->SetFullBodyArmor(false);
	GetDamageable()->SetIsReplicated(false);
	GetDamageable()->SetHealthRegenEnabled(false);
	GetCharacterAudio()->SetLowHealthBreathEnabled(false);
	// 离屏也要推进蒙太奇通知与枪口骨骼，否则 AI 换弹会停在卸弹匣、弹道使用过期姿势。
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

void AGinkgoAICharacter::PostLoad()
{
	Super::PostLoad();
	BehaviorOverrides.MigrateLegacy();
}

void AGinkgoAICharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (const auto* Type = EnemyType.LoadSynchronous()) { GetDamageable()->InitializeVitals(Type->MaxHealth, Type->Armor); }
	}
	if (UGinkgoCameraContextComponent* Context = UGinkgoCameraContextComponent::Find(this)) Context->SetCollectEvents(false);
	GetDamageable()->OnDied.AddUObject(this, &ThisClass::HandleDied);
	if (auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(this)) { Checkpoints->RemoveDefeatedEnemy(*this); }
}

const UGinkgoCharacterModel* AGinkgoAICharacter::ResolveCharacterModel() const
{
	const auto* Type = EnemyType.LoadSynchronous();
	return Type ? Type->Model.LoadSynchronous() : nullptr;
}

void AGinkgoAICharacter::OnRep_EnemyType()
{
	if (HasActorBegunPlay()) { ApplyCharacterModel(); }
}

void AGinkgoAICharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bCoverEyes);
	DOREPLIFETIME(ThisClass, EnemyType);
	DOREPLIFETIME(AGinkgoAICharacter, TacticalPeek);
	DOREPLIFETIME_CONDITION(ThisClass, ReplicatedHoldPose, COND_SimulatedOnly);
}

void AGinkgoAICharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	if (!IsHeldWeaponInHand())
	{
		ReplicatedHoldPose = {};
		return;
	}
	const FGinkgoHoldState& Hold = GetWeapons()->GetHoldState();
	ReplicatedHoldPose.Slot = GetWeapons()->GetHeldSlot();
	ReplicatedHoldPose.Stance = Hold.Stance;
	ReplicatedHoldPose.Raise = FMath::RoundToInt(Hold.RaiseProgress * 255.f);
	ReplicatedHoldPose.Ads = FMath::RoundToInt(Hold.AdsProgress * 255.f);
	ReplicatedHoldPose.Sprint = FMath::RoundToInt(Hold.SprintProgress * 255.f);
	ReplicatedHoldPose.bBothHandsOnGun = Hold.bBothHandsOnGun;
}

void AGinkgoAICharacter::ApplyCarriedWeapons()
{
	Super::ApplyCarriedWeapons();
	// Actor 与武器组件分别复制，任一方先到都可能；网格建好后用最近一次权威快照重建挂点。
	if (GetLocalRole() == ROLE_SimulatedProxy) { OnRep_HoldPose(); }
}

void AGinkgoAICharacter::OnRep_HoldPose()
{
	GetWeapons()->ApplyReplicatedHoldPose(ReplicatedHoldPose);
}

void AGinkgoAICharacter::SetTacticalPeek(const float Amount)
{
	if (HasAuthority()) TacticalPeek = FMath::Clamp(Amount, -1.f, 1.f);
}

FVector AGinkgoAICharacter::GetPawnViewLocation() const
{
	// 眼位跟随实际动画，不能将视线瞬移到墙外。武器改用已知任务瞄准点，子弹仍受真实枪口遮挡。
	return TacticalPeek != 0.f && GetMesh() && GetMesh()->DoesSocketExist(TEXT("head"))
		? GetMesh()->GetSocketLocation(TEXT("head")) : Super::GetPawnViewLocation();
}

bool AGinkgoAICharacter::GetTacticalAimPoint(FVector& OutPoint) const
{
	const AGinkgoAIController* AI = Cast<AGinkgoAIController>(GetController());
	return AI && AI->GetTacticalAimPoint(OutPoint);
}

void AGinkgoAICharacter::InitializeStartingEquipment()
{
	const UGinkgoAIConfig* Config = Definition.LoadSynchronous();
	UGinkgoWeaponDefinition* Weapon = StartingWeaponOverride.LoadSynchronous();
	if (!Weapon && Config) { Weapon = Config->Weapon.LoadSynchronous(); }
	if (!Weapon) return;
	const int32 Slot = GetWeapons()->FindFreeSlot(Weapon->SlotKind);
	if (Slot == INDEX_NONE) return;
	GetWeapons()->SetCarried(Slot, Weapon);
	if (Config) { GetWeapons()->SetReserveRounds(Weapon->AmmoType, Config->ReserveRounds); }
	GetWeapons()->EquipStartingWeapon(Slot);
}

void AGinkgoAICharacter::HandleDied(UGinkgoDamageableComponent& Component, const FGinkgoDamageEvent& Event)
{
	// 单独标记死亡入口，避免把同帧测试准备、资源加载或编辑器刷新误算为死亡开销。
	TRACE_CPUPROFILER_EVENT_SCOPE(Ginkgo_AI_HandleDied);

	// 掉落复用按 G 的机构快照、配件转移、贴墙修正与拾取物生成，不在 AI 上再造一份武器状态。
	// StopAI 会停止导航与武器动作，DisableMovement 会清速度；先从当前挂点松手，再交给布娃娃。
	// 只有权威端生成掉落物，空槽由 DropWeapon 忽略；伤害组件只在首次死亡时广播 OnDied。
	if (HasAuthority())
	{
		if (auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(this)) { Checkpoints->RecordEnemyDeath(*this); }
		for (int32 Slot = 0; Slot < UGinkgoWeaponComponent::SlotCount; ++Slot)
		{
			GetWeapons()->DropWeapon(Slot, /*bTossFromHand*/ false);
		}
	}
	if (AGinkgoAIController* AI = Cast<AGinkgoAIController>(GetController()))
	{
		AI->StopAI();
	}
	EnterDeathRagdoll();
	if (Event.Ammo && !Event.Bone.IsNone())
	{
		GetMesh()->AddImpulse(Event.Direction * Event.Ammo->KillPush * Event.PenetrationScale, Event.Bone, true);
	}
}

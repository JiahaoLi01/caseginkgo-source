#include "GinkgoTraversalComponent.h"

#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/CapsuleComponent.h"
#include "GinkgoTraversalAction.h"
#include "GinkgoTraversalDetection.h"
#include "GinkgoMotionWarpingComponent.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/GinkgoMovementConfig.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

void UGinkgoTraversalComponent::BeginPlay()
{
	Super::BeginPlay();
	if (auto* Character = Cast<AGinkgoCharacter>(GetOwner()))
	{
		Animations = Character->GetGinkgoMovement()->GetConfig().TraversalAnimations.LoadSynchronous();
		AddTickPrerequisiteComponent(Character->GetGinkgoMovement());
		AddTickPrerequisiteComponent(Character->GetAnimActions());
		Character->GetMesh()->AddTickPrerequisiteComponent(this);
	}
}

void UGinkgoTraversalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Cancel();
	Cleanup();
	Super::EndPlay(EndPlayReason);
}

bool UGinkgoTraversalComponent::TryStart(const FRotator& CameraRotation, const bool bMovingExit)
{
	if (IsActive() && !IsRecovering()) { return true; }
	auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	if (!Character || Character->GetLocalRole() == ROLE_SimulatedProxy || !Animations || CameraRotation.ContainsNaN()) { return false; }
	++Input.Sequence;
	Input.Yaw = FRotator::CompressAxisToShort(Ginkgo::Traversal::CameraDirection(CameraRotation).Rotation().Yaw);
	Input.bMovingExit = bMovingExit;
	Input.WeaponSlot = static_cast<int8>(Character->GetWeapons()->GetHeldSlot());
	const auto Candidate = Evaluate(FRotator(0., FRotator::DecompressAxisFromShort(Input.Yaw), 0.));
	const auto* Clip = Animations->Find(Candidate.Kind, bMovingExit);
	FGinkgoTraversalMotion Motion;
	FHitResult Hit;
	bPendingInput = Candidate.IsValid() && Clip && Ginkgo::Traversal::PrepareMotion(*Clip, Candidate, *Character, Motion)
		&& Ginkgo::Traversal::CheckMotion(Motion, *Character, Hit);
	return bPendingInput;
}

bool UGinkgoTraversalComponent::BeginAction(UGinkgoTraversalAction& Action)
{
	auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	if (!Character || ActiveAction != &Action || bCommitted || !ActiveClip.IsValid()) { return false; }
	auto* Anim = Character->GetMesh()->GetAnimInstance();
	if (!Anim) { return false; }
	SavedRootMotionMode = Anim->RootMotionMode;
	CommittedAnimInstance = Anim;
	// 物理时间由 CMC 预测/重放推进，动画只采样同一原根轨迹的姿势，不能再重复施加网格 Tick 的根增量。
	Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
	const bool bRestoreTwoHandPose = ActiveClip.bTwoHanded && Character->IsHeldWeaponInHand();
	if (EffectsSequence != SyncState.ActionSequence || bRestoreTwoHandPose)
	{
		EffectsSequence = SyncState.ActionSequence;
		auto* Weapons = Character->GetWeapons();
		Weapons->InterruptHandRequests();
		if (!Character->IsLocallyControlled() && SyncState.WeaponSlot != INDEX_NONE && Weapons->GetCarried(SyncState.WeaponSlot))
		{
			Weapons->SetHeldSlot(SyncState.WeaponSlot);
			Character->AttachWeaponToHand(SyncState.WeaponSlot);
		}
	}
	// 已预测取枪后被校正回抓沿时，旧取枪可能还未到 InHand、手持槽暂为空；根据同一次动作的槽恢复支撑姿势。
	auto* Weapons = Character->GetWeapons();
	if (ActiveClip.bTwoHanded && Weapons->GetCarried(SyncState.WeaponSlot)
		&& (!Character->IsLocallyControlled() || Weapons->GetHeldSlot() == SyncState.WeaponSlot || Weapons->GetTargetSlot() == SyncState.WeaponSlot))
	{
		StowedSlot = SyncState.WeaponSlot;
		StowedWeapon = Weapons->GetCarried(StowedSlot);
		Weapons->SetHeldSlot(StowedSlot);
		Character->AttachWeaponToStow(StowedSlot, true);
	}
	bCommitted = true;
	bPresentationReleased = false;
	return true;
}

bool UGinkgoTraversalComponent::CanContinue() const
{
	const auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	if (Character && Character->GetLocalRole() == ROLE_SimulatedProxy) { return SyncState.Phase == EGinkgoTraversalPhase::Moving; }
	if (!Character || SyncState.Phase != EGinkgoTraversalPhase::Moving || Character->GetDamageable()->IsDead()
		|| Character->GetWorkbench() || !Character->GetGinkgoMovement()->IsTraversing()) { return false; }
	int32 Index = 0;
	for (const auto* Contact : {&ActiveResult.Ledge, &ActiveResult.LeftHand, &ActiveResult.RightHand, &ActiveResult.Landing})
	{
		if (Index == 2 && !ActiveClip.bTwoHanded) { ++Index; continue; }
		const auto* Component = Contact->Component.Get();
		if (!Component || Component->Mobility == EComponentMobility::Movable || Component->IsSimulatingPhysics()
			|| Component->ComponentHasTag(TEXT("Traversal.Blocked")) || Component->GetOwner()->ActorHasTag(TEXT("Traversal.Blocked"))
			|| !Component->GetComponentTransform().Equals(SupportTransforms[Index++], .01)) { return false; }
	}
	return true;
}

FGinkgoTraversalHandPose UGinkgoTraversalComponent::GetHandPose() const
{
	FGinkgoTraversalHandPose Pose;
	Pose.bActive = IsActive();
	Pose.bDrivingRootMotion = SyncState.Phase == EGinkgoTraversalPhase::Moving;
	Pose.bTwoHanded = ActiveClip.bTwoHanded;
	if (!ActiveAction) { return Pose; }
	const auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	auto* Anim = Character ? Character->GetMesh()->GetAnimInstance() : nullptr;
	const auto* Instance = Anim ? Anim->GetMontageInstanceForID(ActiveAction->GetInstanceID()) : nullptr;
	if (!Instance) { return Pose; }
	Pose.Left = ActiveResult.LeftHand.Position + ActiveResult.LeftHand.Normal * ActiveClip.WristHeight;
	Pose.Right = ActiveResult.RightHand.Position + ActiveResult.RightHand.Normal * ActiveClip.WristHeight;
	Pose.LeftNormal = ActiveResult.LeftHand.Normal;
	Pose.RightNormal = ActiveResult.RightHand.Normal;
	Pose.Weight = Instance->GetWeight();
	Pose.LeftAlpha = ActiveClip.LeftHand.Weight(Instance->GetPosition());
	Pose.RightAlpha = ActiveClip.bTwoHanded ? ActiveClip.RightHand.Weight(Instance->GetPosition()) : 0.f;
	Pose.ArmReach = ActiveClip.ArmReach;
	Pose.MaxBodyLean = ActiveClip.MaxBodyLean;
	return Pose;
}

void UGinkgoTraversalComponent::Cancel()
{
	FinishMotion(false);
	if (auto* Character = Cast<AGinkgoCharacter>(GetOwner()))
	{
		Character->GetGinkgoMovement()->EndTraversalMovement();
		if (ActiveAction) { Character->GetAnimActions()->Cancel(ActiveAction, FAnimActionTags::AnimAction_Cancel_OwnerExit); }
	}
}

void UGinkgoTraversalComponent::ReleasePresentation()
{
	if (!bCommitted || bPresentationReleased) { return; }
	bPresentationReleased = true;
	if (auto* Character = Cast<AGinkgoCharacter>(GetOwner()))
	{
		if (auto* Warp = Character->FindComponentByClass<UGinkgoMotionWarpingComponent>())
		{
			Warp->RemoveModifiersForAnimation(ActiveClip.Montage);
		}
		if (auto* Anim = CommittedAnimInstance.Get())
		{
			if (auto* Instance = Anim->GetMontageInstanceForID(ActiveAction->GetInstanceID())) { Instance->PushDisableRootMotion(); }
		}
		// 足部支撑成立即交还下半身；停止输入时也不等待完全站直或蒙太奇自然结束。
		YieldRecoveryToMovement();
		if (ActiveAction->GetResult().Outcome == EGinkgoAnimActionOutcome::Completed && StowedSlot != INDEX_NONE
			&& !Character->GetDamageable()->IsDead() && Character->GetMesh()->GetAnimInstance() == CommittedAnimInstance.Get()
			&& Character->GetWeapons()->GetCarried(StowedSlot) == StowedWeapon
			&& Character->GetWeapons()->RequestDrawStowedWeapon(StowedSlot))
		{
			// 武器状态树从此拥有取枪及被打断的清理，攀爬淡出不能再把枪直接挂回手上。
			StowedSlot = INDEX_NONE;
			StowedWeapon = nullptr;
		}
	}
}

void UGinkgoTraversalComponent::YieldRecoveryToMovement()
{
	if (!IsRecovering()) { return; }
	if (auto* Anim = CommittedAnimInstance.Get())
	{
		if (auto* Instance = Anim->GetMontageInstanceForID(ActiveAction->GetInstanceID()); Instance)
		{
			// 完成通知已经交还根运动，动作结果仍是 Completed；这里只让出可见姿势。
			// 引擎只缩短更长的淡出；同样时长的重复请求不会重启混合，自然淡出中也能及时让出。
			FAlphaBlend Blend(ActiveClip.MovementBlendOut);
			Blend.SetBlendOption(EAlphaBlendOption::Cubic);
			Instance->Stop(Blend, false);
		}
	}
}

void UGinkgoTraversalComponent::Cleanup()
{
	ReleasePresentation();
	if (auto* Character = Cast<AGinkgoCharacter>(GetOwner()); Character && bCommitted)
	{
		if (auto* Anim = CommittedAnimInstance.Get()) { Anim->SetRootMotionMode(SavedRootMotionMode); }
		if (!Character->GetDamageable()->IsDead() && StowedSlot != INDEX_NONE && Character->GetWeapons()->GetHeldSlot() == StowedSlot
			&& Character->GetWeapons()->GetCarried(StowedSlot) == StowedWeapon) { Character->AttachWeaponToHand(StowedSlot, true); }
	}
	ActiveAction = nullptr;
	// 淡出和网格 Tick 的完成时机在各网络角色上不同；它们不能清空 CMC 仍在使用的计划和交接时长。
	if (SyncState.Phase == EGinkgoTraversalPhase::Idle)
	{
		ActiveClip = {};
		ActiveResult = {};
		ActiveMotion = {};
		MotionInterval = FVector2D::ZeroVector;
		SupportTransforms.Reset();
	}
	StowedWeapon = nullptr;
	StowedSlot = INDEX_NONE;
	CommittedAnimInstance.Reset();
	bCommitted = false;
	bPresentationReleased = false;
}

void UGinkgoTraversalComponent::TickAction()
{
	SyncPresentation();
}

FTransform UGinkgoTraversalComponent::ExtractMotion(const UAnimSequenceBase* Animation, const float From, const float To)
{
	if (SyncState.Phase != EGinkgoTraversalPhase::Moving || ActiveMotion.Montage != Animation) { return FTransform::Identity; }
	MotionInterval = FVector2D(From, FMath::Min(To, ActiveMotion.Duration));
	return ActiveMotion.Delta(From, To);
}

bool UGinkgoTraversalComponent::SweepMotion(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
	const auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	if (!Character || SyncState.Phase != EGinkgoTraversalPhase::Moving) { return false; }
	// 胶囊恢复站立时逐步增长；不能直接拿帧末的大胶囊查询帧初位置，造成低帧率下的伪重叠。
	const int32 Steps = FMath::Max(1, FMath::CeilToInt((MotionInterval.Y - MotionInterval.X) * 120.));
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		const double A = double(Index) / Steps;
		const double B = double(Index + 1) / Steps;
		if (!Ginkgo::Traversal::SweepBody(ActiveMotion, *Character,
			FMath::Lerp(MotionInterval.X, MotionInterval.Y, A), FMath::Lerp(MotionInterval.X, MotionInterval.Y, B),
			FMath::Lerp(Start, End, A), FMath::Lerp(Start, End, B), Hit)) { return false; }
	}
	return true;
}

#include "GinkgoTraversalComponent.h"

#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/GinkgoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GinkgoTraversalAction.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Weapon/GinkgoWeaponComponent.h"

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarTraversalNetLog(TEXT("ginkgo.traversal.netLog"), 0,
		TEXT("记录攀爬各网络角色的物理阶段、动画权重与位置；用于专服/拥有者/模拟代理比对。"));

	void LogTraversalNetwork(const AGinkgoCharacter& Character, const FGinkgoTraversalSyncState& State)
	{
		if (!CVarTraversalNetLog.GetValueOnGameThread() || Character.bClientUpdating) { return; }
		const auto Pose = Character.GetTraversal()->GetHandPose();
		const FVector Position = Character.GetActorLocation();
		UE_LOG(LogTemp, Log, TEXT("[TraversalNet] actor=%s role=%d handled=%u action=%u phase=%u kind=%u time=%.6f completed=%d weight=%.3f x=%.3f y=%.3f z=%.3f"),
			*Character.GetName(), Character.GetLocalRole(), State.HandledSequence, State.ActionSequence, State.Phase, State.Result.Kind,
			State.Time, State.bCompleted, Pose.Weight, Position.X, Position.Y, Position.Z);
	}
}
#endif

void UGinkgoTraversalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, ReplicatedTraversal, COND_SimulatedOnly);
}

bool UGinkgoTraversalComponent::RebuildMotion()
{
	const auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	const auto* Clip = Animations ? Animations->Find(SyncState.Result.Kind, SyncState.bMovingExit) : nullptr;
	if (!Character || !Clip || !Clip->IsValid()) { return false; }
	ActiveClip = *Clip;
	ActiveResult = SyncState.Result;
	SupportTransforms.Reset();
	for (const auto& Support : SyncState.Supports) { SupportTransforms.Add(Support); }
	return Ginkgo::Traversal::PrepareMotion(*Clip, ActiveResult, *Character, ActiveMotion, &SyncState.Origin);
}

void UGinkgoTraversalComponent::RestoreSyncState(const FGinkgoTraversalSyncState& State)
{
	SyncState = State;
	if (SyncState.Phase != EGinkgoTraversalPhase::Idle && !RebuildMotion()) { SyncState.Phase = EGinkgoTraversalPhase::Idle; }
}

void UGinkgoTraversalComponent::BeginMovement(const float DeltaTime)
{
	auto* Character = CastChecked<AGinkgoCharacter>(GetOwner());
	bPendingInput = false;
	if (Input.Sequence == SyncState.HandledSequence) { return; }
	SyncState.HandledSequence = Input.Sequence;
	if (SyncState.Phase == EGinkgoTraversalPhase::Moving) { Character->StopJumping(); return; }
	const auto Result = Evaluate(FRotator(0., FRotator::DecompressAxisFromShort(Input.Yaw), 0.));
	const auto* Clip = Animations ? Animations->Find(Result.Kind, Input.bMovingExit) : nullptr;
	FGinkgoTraversalMotion Motion;
	FHitResult Hit;
	if (!Result.IsValid() || !Clip || !Clip->IsValid() || !Ginkgo::Traversal::PrepareMotion(*Clip, Result, *Character, Motion)
		|| !Ginkgo::Traversal::CheckMotion(Motion, *Character, Hit)
		|| !Character->GetGinkgoMovement()->BeginTraversalMovement(Character->GetActorQuat()))
	{
		// 本地预测接受、服务器拒绝时仍保留普通跳跃语义；校正会把预测的攀爬换成这次跳跃。
		if (!Character->bPressedJump) { Character->Jump(); Character->CheckJumpInput(DeltaTime); }
		return;
	}
	Character->StopJumping();
	SyncState = {};
	SyncState.HandledSequence = Input.Sequence;
	SyncState.ActionSequence = Input.Sequence;
	SyncState.Phase = EGinkgoTraversalPhase::Moving;
	SyncState.bMovingExit = Input.bMovingExit;
	SyncState.WeaponSlot = Character->GetWeapons()->GetCarried(Input.WeaponSlot) ? Input.WeaponSlot : INDEX_NONE;
	SyncState.Result = Result;
	SyncState.Origin = Motion.Origin;
	int32 Index = 0;
	for (const auto* Contact : {&Result.Ledge, &Result.LeftHand, &Result.RightHand, &Result.Landing})
	{
		SyncState.Supports[Index++] = Contact->Component.IsValid() ? Contact->Component->GetComponentTransform() : FTransform::Identity;
	}
	RebuildMotion();
}

float UGinkgoTraversalComponent::AdvanceMotion(const float DeltaTime, FVector& Delta, FQuat& Facing)
{
	const float From = SyncState.Time;
	const float To = FMath::Min(From + DeltaTime, ActiveClip.MotionEndTime);
	// 与 Motion Warping 修正器共用同一原动画累计轨迹；改变的是推进时钟，不重写美术根曲线。
	ExtractMotion(ActiveClip.Montage, From, To);
	Delta = ActiveMotion.Origin.TransformVector(ActiveMotion.Evaluate(To).GetTranslation() - ActiveMotion.Evaluate(From).GetTranslation());
	const auto* Character = CastChecked<AGinkgoCharacter>(GetOwner());
	Facing = ActiveMotion.Origin.GetRotation() * ActiveMotion.Evaluate(To).GetRotation() * Character->GetBaseRotationOffset().Inverse();
	SyncState.Time = To;
	return To - From;
}

void UGinkgoTraversalComponent::FinishMotion(const bool bCompleted)
{
	if (SyncState.Phase != EGinkgoTraversalPhase::Moving) { return; }
	SyncState.Phase = EGinkgoTraversalPhase::Recovering;
	SyncState.ReleaseTime = SyncState.Time;
	SyncState.bCompleted = bCompleted;
}

void UGinkgoTraversalComponent::EndMovement(const float StartTime, const float DeltaTime)
{
	auto* Character = CastChecked<AGinkgoCharacter>(GetOwner());
	if (SyncState.Phase == EGinkgoTraversalPhase::Recovering)
	{
		SyncState.Time = FMath::Max(SyncState.Time, StartTime + DeltaTime);
		if (SyncState.Time >= SyncState.ReleaseTime + ActiveClip.MovementBlendOut + .05f)
		{
			SyncState.Phase = EGinkgoTraversalPhase::Idle;
		}
	}
	if (!Character->bClientUpdating) { SyncPresentation(); }
#if !UE_BUILD_SHIPPING
	if (SyncState.HandledSequence != 0) { LogTraversalNetwork(*Character, SyncState); }
#endif
	if (Character->HasAuthority())
	{
		const bool bChanged = ReplicatedTraversal.State.Phase != SyncState.Phase
			|| ReplicatedTraversal.State.ActionSequence != SyncState.ActionSequence
			|| ReplicatedTraversal.State.HandledSequence != SyncState.HandledSequence;
		// 普通移动不更新采样时间，避免 Idle 时也让整份复制属性持续变脏。
		if (bChanged || SyncState.Phase != EGinkgoTraversalPhase::Idle)
		{
			ReplicatedTraversal.State = SyncState;
			ReplicatedTraversal.ServerTime = GetWorld()->GetTimeSeconds();
		}
		if (bChanged) { Character->ForceNetUpdate(); }
	}
}

void UGinkgoTraversalComponent::OnRep_Traversal()
{
	RestoreSyncState(ReplicatedTraversal.State);
	SyncPresentation();
#if !UE_BUILD_SHIPPING
	LogTraversalNetwork(*CastChecked<AGinkgoCharacter>(GetOwner()), SyncState);
#endif
}

void UGinkgoTraversalComponent::SyncPresentation()
{
	auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	if (!Character || Character->bClientUpdating || !Animations) { return; }
	const bool bProxy = Character->GetLocalRole() == ROLE_SimulatedProxy;
	if (bProxy && ReplicatedTraversal.State.Phase != EGinkgoTraversalPhase::Idle)
	{
		const auto* GameState = GetWorld()->GetGameState();
		const float Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
		SyncState.Time = ReplicatedTraversal.State.Time + FMath::Clamp(Now - ReplicatedTraversal.ServerTime, 0.f, .5f);
		if (SyncState.Phase == EGinkgoTraversalPhase::Moving && SyncState.Time >= ActiveClip.MotionEndTime)
		{
			SyncState.ReleaseTime = ActiveClip.MotionEndTime;
			SyncState.Phase = EGinkgoTraversalPhase::Recovering;
			SyncState.bCompleted = true;
		}
	}
	auto* Actions = Character->GetAnimActions();
	auto* Anim = Character->GetMesh()->GetAnimInstance();
	if (!Anim || !Actions->GetRunner()) { return; }
	if (!bProxy && bCommitted && CommittedAnimInstance.Get() != Anim)
	{
		Cancel();
		Actions->FlushPending();
		Cleanup();
		return;
	}
	// 校正可能把已预测交还的动作退回接触阶段；只在重放完成后重建一次表现，不在 SavedMove 中反复收取武器。
	if (ActiveAction && (PresentedSequence != SyncState.ActionSequence || SyncState.Phase == EGinkgoTraversalPhase::Idle
		|| (bPresentationReleased && SyncState.Phase == EGinkgoTraversalPhase::Moving)))
	{
		if (ActiveAction->IsRunning()) { Actions->Cancel(ActiveAction, FAnimActionTags::AnimAction_Cancel_OwnerExit); }
		Actions->FlushPending();
		Cleanup();
	}
	if (SyncState.Phase == EGinkgoTraversalPhase::Idle) { return; }
	if (!ActiveAction)
	{
		if (SyncState.Phase == EGinkgoTraversalPhase::Recovering && PresentedSequence == SyncState.ActionSequence) { return; }
		if (!RebuildMotion()) { return; }
		PresentedSequence = SyncState.ActionSequence;
		ActiveAction = UGinkgoTraversalAction::Make(*this);
		if (!Actions->Start(ActiveAction))
		{
			if (!bProxy) { Cancel(); }
			Cleanup();
			return;
		}
		Actions->FlushPending();
	}
	auto* Instance = Anim->GetMontageInstanceForID(ActiveAction->GetInstanceID());
	if (!Instance || (SyncState.Phase == EGinkgoTraversalPhase::Moving && !ActiveAction->IsRunning()))
	{
		if (!bProxy && SyncState.Phase == EGinkgoTraversalPhase::Moving) { Cancel(); }
		Cleanup();
		return;
	}
	if (!Instance->IsRootMotionDisabled()) { Instance->PushDisableRootMotion(); }
	if (SyncState.Phase == EGinkgoTraversalPhase::Moving)
	{
		Instance->Pause();
		Instance->SetPosition(FMath::Min(SyncState.Time, ActiveClip.MotionEndTime));
	}
	else if (!bPresentationReleased)
	{
		Instance->SetPosition(SyncState.ReleaseTime);
		Instance->SetPlaying(true);
		if (SyncState.bCompleted) { Actions->NotifySimulationEvent(ActiveAction->GetInstanceID(), Ginkgo::Traversal::LandedEvent); }
		else { Actions->Cancel(ActiveAction, FAnimActionTags::AnimAction_Cancel_OwnerExit); }
		Actions->FlushPending();
		ReleasePresentation();
	}
}

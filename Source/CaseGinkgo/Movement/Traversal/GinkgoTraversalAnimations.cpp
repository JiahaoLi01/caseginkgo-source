#include "GinkgoTraversalAnimations.h"

#include "Animation/AnimMontage.h"
#include "AnimNotifies/AnimNotify_PlayMontageNotify.h"
#include "AnimNotifyState_MotionWarping.h"
#include "GinkgoTraversalWarp.h"

const FName Ginkgo::Traversal::LandedEvent(TEXT("TraversalLanded"));

float FGinkgoTraversalHandWindow::Weight(const float Time) const
{
	return FMath::SmoothStep(Begin, Contact, Time) * (1.f - FMath::SmoothStep(Release, End, Time));
}

bool FGinkgoTraversalHandWindow::IsValid(const float Duration) const
{
	return FMath::IsFinite(Begin) && FMath::IsFinite(Contact) && FMath::IsFinite(Release) && FMath::IsFinite(End)
		&& Begin >= 0.f && Begin < Contact && Contact <= Release && Release < End && End <= Duration;
}

bool FGinkgoTraversalClip::IsValid() const
{
	const bool bFieldsValid = Montage && Montage->HasRootMotion() && FMath::IsFinite(MotionEndTime) && MotionEndTime > 0.f
		&& FMath::IsFinite(ReferenceHeight) && ReferenceHeight > 0.f
		&& FMath::IsFinite(RiseEndTime) && RiseEndTime > 0.f && RiseEndTime < MotionEndTime
		&& FMath::IsFinite(ContactRootDistance) && ContactRootDistance >= 0.f && ContactRootDistance <= 100.f
		&& !VerticalScaleRange.ContainsNaN() && VerticalScaleRange.X > 0. && VerticalScaleRange.Y >= VerticalScaleRange.X
		&& FMath::IsFinite(MaxHeightCorrection) && MaxHeightCorrection >= 0.f
		&& FMath::IsFinite(MovementBlendOut) && MovementBlendOut >= .05f && MovementBlendOut <= .3f
		&& FMath::IsFinite(RecoveryDuration) && FMath::IsFinite(BlendOutDuration)
		&& BlendOutDuration >= .1f && RecoveryDuration >= BlendOutDuration && MotionEndTime + RecoveryDuration <= Montage->GetPlayLength() + .001
		&& LeftHand.IsValid(MotionEndTime) && (!bTwoHanded || RightHand.IsValid(MotionEndTime))
		&& FMath::IsFinite(WristHeight) && WristHeight >= 0.f && WristHeight <= 15.f && FMath::IsFinite(ArmReach) && ArmReach > 0.f
		&& FMath::IsFinite(MaxBodyLean) && MaxBodyLean >= 0.f && MaxBodyLean <= 60.f;
	if (!bFieldsValid) { return false; }
	// 时序和素材必须成套有效，缺少完成事件或 Warp 窗口时在抢占换弹以前拒绝启动。
	bool bHasCompletion = false;
	bool bWindow = false;
	for (const auto& Event : Montage->Notifies)
	{
		if (const auto* Notify = Cast<UAnimNotify_PlayMontageNotify>(Event.Notify))
		{
			bHasCompletion |= FName(*Notify->GetNotifyName()) == Ginkgo::Traversal::LandedEvent
				&& FMath::IsNearlyEqual(double(Event.GetTime()), MotionEndTime, .001) && Event.MontageTickType == EMontageNotifyTickType::BranchingPoint;
		}
		const auto* State = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
		const auto* Modifier = State ? Cast<UGinkgoTraversalWarp>(State->RootMotionModifier) : nullptr;
		if (Modifier)
		{
			bWindow |= Event.GetTriggerTime() == 0.f && FMath::IsNearlyEqual(Event.GetEndTriggerTime(), MotionEndTime, .001f);
		}
	}
	if (Body.Num() < 2 || Body[0].Time != 0.f || !FMath::IsNearlyEqual(Body.Last().Time, MotionEndTime, .001f)) { return false; }
	for (int32 Index = 0; Index < Body.Num(); ++Index)
	{
		const auto& Key = Body[Index];
		if (!FMath::IsFinite(Key.Time) || Key.Offset.ContainsNaN() || !FMath::IsFinite(Key.RadiusScale) || !FMath::IsFinite(Key.HeightScale)
			|| Key.RadiusScale < .25f || Key.RadiusScale > 1.f || Key.HeightScale < .25f || Key.HeightScale > 1.f
			|| (Index > 0 && Key.Time <= Body[Index - 1].Time)) { return false; }
	}
	for (const auto* Key : {&Body[0], &Body.Last()})
	{
		if (!Key->Offset.IsNearlyZero() || Key->RadiusScale != 1.f || Key->HeightScale != 1.f) { return false; }
	}
	return bHasCompletion && bWindow;
}

const FGinkgoTraversalClip* UGinkgoTraversalAnimations::Find(const EGinkgoTraversalKind Kind, const bool bMovingExit) const
{
	switch (Kind)
	{
	case EGinkgoTraversalKind::Vault: return bMovingExit ? &VaultMoving : &Vault;
	case EGinkgoTraversalKind::Mantle: return &Mantle;
	case EGinkgoTraversalKind::Climb: return &Climb;
	default: return nullptr;
	}
}

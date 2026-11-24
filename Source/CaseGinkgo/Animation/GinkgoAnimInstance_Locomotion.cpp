#include "GinkgoAnimInstance.h"

#include "AnimExecutionContextLibrary.h"
#include "AnimationRuntime.h"  // FAnimationRuntime::AdvanceTime：播放器这一帧推进以后的时刻
#include "Animation/AnimSequence.h"
#include "AI/GinkgoNoiseEmitterComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Camera/GinkgoCameraContextComponent.h"  // 落脚给相机的事件
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "SequenceEvaluatorLibrary.h"
#include "SequencePlayerLibrary.h"
#include "Utilities/Enumeration/Tags/MovementTags.h"

/**
 * 移动状态机的那一半：转换条件、根骨骼的偏航补偿、修正节点的值，以及动画蓝图各状态绑定的节点函数。
 * 设计见 Docs/Design/AnimationBlueprint.md 的"状态"；片段的距离曲线、剩余转角、同步标记由导入命令生成
 */

namespace
{
	/** 导入命令生成的曲线（GinkgoAnimationImportCommandlet） */
	const FName DistanceCurveName(TEXT("Distance"));
	const FName RemainingTurnYawCurveName(TEXT("RemainingTurnYaw"));

	/** GASP 自带的曲线：片段的速度（cm/s）与两只脚的接触 */
	const FName MoveDataSpeedCurveName(TEXT("movedata_speed"));
	const FName ContactLeftCurveName(TEXT("contact_l"));
	const FName ContactRightCurveName(TEXT("contact_r"));

	/** 导入时按接触曲线的上升沿生成的同步标记：左、右脚落地 */
	const FName LeftFootMarkerName(TEXT("L"));
	const FName RightFootMarkerName(TEXT("R"));

	/** 导入时按根骨骼的弧线生成的跳跃标记：起跳片段离地、到顶，走下边缘的片段离开边缘，落地片段着地 */
	const FName TakeoffMarkerName(TEXT("Takeoff"));
	const FName ApexMarkerName(TEXT("Apex"));
	const FName ImpactMarkerName(TEXT("Impact"));

	/** 起跳的脚看离地前这么久片段里着地的那只（离地那一刻两只脚都在离开） */
	constexpr float TakeoffContactLead = 1.f / 30.f;

	/** 预测的着地比落地片段着地前的时长还远这么多（飘过了边缘），回到下落 */
	constexpr float LandAbortMargin = 0.2f;

	/** 找落地片段里先着地的脚再离地时，每步走多远（s） */
	constexpr float LandScanStep = 1.f / 60.f;

	/** 预测的着地不到这一帧帧长的这么多倍，算下一帧着地（下一帧的帧长事先不知道，留点余量） */
	constexpr float TouchdownFrameMargin = 1.5f;

#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarLean(
		TEXT("ginkgo.anim.lean"),
		1,
		TEXT("左右倾斜：1 开，0 关（倾斜量的目标取 0，只用来对照测量脚滑与脚的跳变）。"));
#endif

	bool IsLeanEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarLean.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	/** 预测的距离小于它算已经到了（cm） */
	constexpr float DistanceMatchTolerance = 0.5f;

	FSequenceEvaluatorReference ToEvaluator(const FAnimNodeReference& Node, bool& bOutValid)
	{
		EAnimNodeReferenceConversionResult Result;
		const FSequenceEvaluatorReference Evaluator = USequenceEvaluatorLibrary::ConvertToSequenceEvaluator(Node, Result);
		bOutValid = Result == EAnimNodeReferenceConversionResult::Succeeded;
		return Evaluator;
	}

	FSequencePlayerReference ToPlayer(const FAnimNodeReference& Node, bool& bOutValid)
	{
		EAnimNodeReferenceConversionResult Result;
		const FSequencePlayerReference Player = USequencePlayerLibrary::ConvertToSequencePlayer(Node, Result);
		bOutValid = Result == EAnimNodeReferenceConversionResult::Succeeded;
		return Player;
	}

	/** 片段里第一个这个名字的同步标记的时刻，没有时为 0 */
	float FirstMarkerTime(const UAnimSequence& Sequence, const FName MarkerName)
	{
		const FAnimSyncMarker* Marker = Sequence.AuthoredSyncMarkers.FindByPredicate([MarkerName](const FAnimSyncMarker& Item) { return Item.MarkerName == MarkerName; });
		return Marker ? Marker->Time : 0.f;
	}
}

// ===== 每帧 =====

void UGinkgoAnimInstance::UpdateRootYaw(const float DeltaSeconds)
{
	using namespace Ginkgo::Animation;
	if (Snapshot.Traversal.bDrivingRootMotion)
	{
		// 保留入场前真正可见的偏航补偿，按全身覆盖渐退；不能在姿势还没有混进来时瞬间归零。
		if (!bTraversalWasDriving) { TraversalEntryRootYaw = RootYawOffset; }
		RootYawOffset = TraversalEntryRootYaw * (1.f - Snapshot.Traversal.Weight);
		RootYawOffsetVelocity = 0.;
		TurnInPlaceSequence = nullptr;
		return;
	}
	// 根骨骼的偏航补偿：胶囊这一帧转了多少，拆成平常的转动与一下子转过去的部分（快于 SnapYawRate）。
	// 一下子转过去的任何状态下都接住（网格不跟着跳）；站着时平常的转动也接住，急停没停下时全部接住，动起来以后用弹簧收回 0
	const double YawDelta = Motion.YawRate * DeltaSeconds;
	// 攀爬收势恢复控制器朝向时，全额接住胶囊转角，再由原地转身/移动的现有逻辑消化。
	const double MaxGradual = Snapshot.Traversal.bActive && !Snapshot.Traversal.bDrivingRootMotion ? 0. : SnapYawRate * DeltaSeconds;
	const double Snap = FMath::Abs(YawDelta) > MaxGradual ? YawDelta - FMath::Sign(YawDelta) * MaxGradual : 0.0;
	double Offset = RootYawOffset;
	switch (ReportedRootYawMode)
	{
	case ERootYawMode::Accumulate:
		Offset = FMath::UnwindDegrees(AccumulateRootYawOffset(Offset, YawDelta - Snap, RootYawOffsetLimit) - Snap);
		RootYawOffsetVelocity = 0.0;
		break;
	case ERootYawMode::Hold:
		Offset = FMath::UnwindDegrees(Offset - YawDelta);
		RootYawOffsetVelocity = 0.0;
		break;
	case ERootYawMode::FaceVelocity:
	{
		// 网格的根朝速度：补偿等于速度相对胶囊的角度。先接住胶囊这一帧的转动（视角转多少，速度相对胶囊的角度就反着变多少），
		// 剩下的差只来自速度的方向变了（转向、刹车），用弹簧追
		Offset = FMath::UnwindDegrees(Offset - YawDelta);
		if (Motion.Speed > MinDirectionSpeed)
		{
			double ToVelocity = FMath::UnwindDegrees(Offset - Motion.LocalAngle);
			Ginkgo::Spring::StepCritical(ToVelocity, RootYawOffsetVelocity, SlideFacingOmega, DeltaSeconds);
			Offset = FMath::UnwindDegrees(Motion.LocalAngle + ToVelocity);
		}
		else
		{
			RootYawOffsetVelocity = 0.0;
		}
		break;
	}
	default:
		Offset = FMath::UnwindDegrees(Offset - Snap);
		Ginkgo::Spring::StepCritical(Offset, RootYawOffsetVelocity, RootYawOffsetBlendOmega, DeltaSeconds);
		break;
	}
	RootYawOffset = Offset;
	// 原地转身：片段锁了根，转身由剩余转角每帧的变化把补偿还回去（Lyra 的 ProcessTurnYawCurve 的做法，剩余转角向右为正，
	// 补偿为负时向右转、补偿随之往 0 走）。按这一帧片段会走到的时刻直接从片段上取（节点函数接着按同样的帧长推进），与这一帧的姿势对齐。
	// 不读上一帧求值的曲线：那晚一帧，而且进入转身时惯性化把剩余转角与权重两条曲线各自混进来，比例不同，相除会跳上百度
	if (LocomotionState != EGinkgoLocomotionState::TurnInPlace)
	{
		TurnInPlaceSequence = nullptr;
	}
	if (TurnInPlaceSequence)
	{
		const float NextTime = FMath::Min(TurnInPlaceTime + DeltaSeconds, TurnInPlaceSequence->GetPlayLength());
		RootYawOffset -= EvaluateCurve(TurnInPlaceSequence, RemainingTurnYawCurveName, NextTime)
			- EvaluateCurve(TurnInPlaceSequence, RemainingTurnYawCurveName, TurnInPlaceTime);
	}
}

void UGinkgoAnimInstance::UpdateLocomotion(const float DeltaSeconds)
{
	using namespace Ginkgo::Animation;
	LocomotionState = ReportedState;
	const bool bOnGround = MoveState.Mode == EGinkgoAnimMode::Ground || MoveState.Mode == EGinkgoAnimMode::Slide;
	IntentLocalAngle = Intent.bHasIntent ? FMath::FindDeltaAngleDegrees(Motion.FacingYaw, Intent.Direction.Rotation().Yaw) : 0.f;

	UpdateRootYaw(DeltaSeconds);

	// 转换条件。*Finished 由节点函数在上一帧写好；不在那个状态时清掉（滑铲的两个在下面）：状态机一帧里能连着走几个转换（默认 3 个），
	// 刚进入一个状态时它的节点函数还没跑，上次留下的"放完了"会让它当帧又出去（急停 → 起步 → 循环，起步一帧都没放）
	const bool bMoving = Motion.Speed > StopMinSpeed;
	bStartFinished = bStartFinished && LocomotionState == EGinkgoLocomotionState::Start;
	bStopFinished = bStopFinished && LocomotionState == EGinkgoLocomotionState::Stop;
	bTurnInPlaceFinished = bTurnInPlaceFinished && LocomotionState == EGinkgoLocomotionState::TurnInPlace;
	bInAir = !bOnGround;
	bShouldStart = bOnGround && Intent.bHasIntent;
	bShouldCycleFromStop = bShouldStart && Motion.Speed > CycleFromStopSpeed;
	bShouldStop = bOnGround && !Intent.bHasIntent && bMoving;
	bShouldIdle = bOnGround && !Intent.bHasIntent && !bMoving;
	bShouldPivot = bOnGround && Intent.bHasIntent && Intent.AngleToVelocity > PivotAngle && Motion.Speed > PivotMinSpeed;
	bPivotFinished = (bPivotFinished && LocomotionState == EGinkgoLocomotionState::Pivot)
		|| (Intent.bHasIntent && Motion.Speed > MinDirectionSpeed && Intent.AngleToVelocity < PivotEndAngle);
	// 全身收势还可见时只保存偏航差，不提前消费被盖住的原地转身步伐。
	bShouldTurnInPlace = bOnGround && !Intent.bHasIntent && !bMoving && FMath::Abs(RootYawOffset) >= TurnInPlaceThreshold
		&& Snapshot.Traversal.Weight < .1f;

	// 滑铲：地面状态与空中都最先看 bShouldSlide（离地之后）；起身放完以后按意图、速度交给平常的状态
	bShouldSlide = MoveState.Mode == EGinkgoAnimMode::Slide;
	bSlideEnded = bOnGround && !bShouldSlide;
	bSlideStartFinished = bSlideStartFinished && LocomotionState == EGinkgoLocomotionState::SlideStart;
	bSlideExitFinished = bSlideExitFinished && LocomotionState == EGinkgoLocomotionState::SlideExit;
	bSlideExitToCycle = bSlideExitFinished && bShouldStart && bMoving;
	bSlideExitToStart = bSlideExitFinished && bShouldStart && !bMoving;
	bSlideExitToStop = bSlideExitFinished && bShouldStop;
	bSlideExitToIdle = bSlideExitFinished && bShouldIdle;

	// 落地还没交出去：Air 的三个出口（循环、急停、Idle）先不成立，着地以后接着放落地的片段。只在 Air 状态里（节点函数上一帧写的），
	// 别的状态的转换不受影响；滑铲照常最先看
	if (IsAirState(LocomotionState) && bLandHold)
	{
		bShouldStart = false;
		bShouldStop = false;
		bShouldIdle = false;
	}

	// 步幅修正：只有循环给目标，其余状态回到 1；换片段时速度比一下子变，过弹簧
	if (LocomotionState != EGinkgoLocomotionState::Cycle)
	{
		StrideScaleTarget = 1.f;
	}
	double StrideOffset = StrideScale - StrideScaleTarget;
	Ginkgo::Spring::StepCritical(StrideOffset, StrideScaleVelocity, StrideScaleOmega, DeltaSeconds);
	StrideScale = StrideScaleTarget + StrideOffset;
	// 步幅修正在根骨骼转偏航补偿之前：速度转到网格组件空间，再按补偿反着转
	const FVector ComponentVelocity = FRotator(0.0, -RootYawOffset, 0.0).RotateVector(Snapshot.MeshRotation.UnrotateVector(Motion.Velocity));
	if (ComponentVelocity.SizeSquared2D() > FMath::Square(MinDirectionSpeed))
	{
		StrideDirection = ComponentVelocity.GetSafeNormal2D();
	}

	UpdateLean(DeltaSeconds);

	// 腿部 IK 只在地面上（滑铲时脚贴着地滑，不锁）
	const double LegIKTarget = MoveState.Mode == EGinkgoAnimMode::Ground ? 1. - Snapshot.Traversal.Weight : 0.;
	double LegIKOffset = LegIKAlpha - LegIKTarget;
	Ginkgo::Spring::StepCritical(LegIKOffset, LegIKAlphaVelocity, LegIKAlphaOmega, DeltaSeconds);
	LegIKAlpha = static_cast<float>(FMath::Clamp(LegIKTarget + LegIKOffset, 0.0, 1.0));
	if (Snapshot.Traversal.bDrivingRootMotion)
	{
		// 地面脚锁随姿势进入渐退，满权重时必须完全让出，不能靠弹簧拖尾继续把腾空的脚锁在地面。
		LegIKAlpha = FMath::Min(LegIKAlpha, 1.f - Snapshot.Traversal.Weight);
	}
}

void UGinkgoAnimInstance::UpdateLean(const float DeltaSeconds)
{
	// 小白人在组件空间里朝 +Y，+X 是它的左边；向右倾斜为正
	const FVector BodyAcceleration = FRotator(0.0, -RootYawOffset, 0.0).RotateVector(Snapshot.MeshRotation.UnrotateVector(Motion.Acceleration));
	const bool bLeans = IsLeanEnabled() && !MoveState.bCrouched
		&& (LocomotionState == EGinkgoLocomotionState::Cycle || IsAirState(LocomotionState));
	const double SpeedScale = FMath::GetMappedRangeValueClamped(LeanSpeedRange, FVector2D(0.0, 1.0), Motion.Speed);
	const double Target = bLeans ? FMath::Clamp(-BodyAcceleration.X / LeanFullAcceleration, -1.0, 1.0) * SpeedScale : 0.0;
	double LeanOffset = LeanAmount - Target;
	Ginkgo::Spring::StepCritical(LeanOffset, LeanVelocity, LeanOmega, DeltaSeconds);
	LeanAmount = static_cast<float>(FMath::Clamp(Target + LeanOffset, -1.0, 1.0));
	LeanLeftAlpha = FMath::Max(-LeanAmount, 0.f);
	LeanRightAlpha = FMath::Max(LeanAmount, 0.f);
	LeanSprintWeight = static_cast<float>(FMath::GetMappedRangeValueClamped(FVector2D(Snapshot.RunSpeed, Snapshot.SprintSpeed), FVector2D(0.0, 1.0), Motion.Speed));
}

void UGinkgoAnimInstance::ReportState(const EGinkgoLocomotionState State, const ERootYawMode RootYawMode)
{
	ReportedState = State;
	ReportedRootYawMode = RootYawMode;
}

EGinkgoAnimGait UGinkgoAnimInstance::SelectGait() const
{
	if (MoveState.bCrouched)
	{
		return EGinkgoAnimGait::Crouch;
	}
	if (MoveState.bSprinting)
	{
		return EGinkgoAnimGait::Sprint;
	}
	return Intent.bHasIntent && Intent.Scale * Snapshot.GroundSpeedRatio < WalkInputScale ? EGinkgoAnimGait::Walk : EGinkgoAnimGait::Run;
}

bool UGinkgoAnimInstance::FollowStance()
{
	if ((Gait == EGinkgoAnimGait::Crouch) == MoveState.bCrouched)
	{
		return false;
	}
	Gait = SelectGait();
	return true;
}

bool UGinkgoAnimInstance::FollowStanceToStop()
{
	if (!FollowStance())
	{
		return false;
	}
	if (Gait != EGinkgoAnimGait::Crouch)
	{
		Gait = EGinkgoAnimGait::Walk;
	}
	return true;
}

UAnimSequence* UGinkgoAnimInstance::SelectStopAnim(float& OutEntryTime) const
{
	const FGinkgoGaitAnims& Anims = GetGaitAnims(Gait, Direction);
	UAnimSequence* Stop = ChooseByPlantedFoot(Anims.StopLeftFoot.Get(Direction), Anims.StopRightFoot.Get(Direction), Intent.StopDistance);
	OutEntryTime = Stop ? Ginkgo::Animation::FindTimeAtCurveValue([Stop](const float At) { return EvaluateCurve(Stop, DistanceCurveName, At); },
		-Intent.StopDistance, Stop->GetPlayLength()) : 0.f;
	return Stop;
}

UAnimSequence* UGinkgoAnimInstance::SelectSlideExitAnim(const bool bMoving) const
{
	if (MoveState.bCrouched)
	{
		return bMoving ? SlideAnims.ExitMovingCrouch : SlideAnims.ExitIdleCrouch;
	}
	if (!bMoving)
	{
		return SlideAnims.ExitIdleStand;
	}
	return Intent.Scale * Snapshot.GroundSpeedRatio < WalkInputScale ? SlideAnims.ExitMovingWalk : SlideAnims.ExitMovingRun;
}

UAnimSequence* UGinkgoAnimInstance::GetTurnInPlaceAnim() const
{
	const FGinkgoStanceAnims& Stance = GetStanceAnims();
	const TArray<TObjectPtr<UAnimSequence>>& Turns = bTurnInPlaceRight ? Stance.TurnRight : Stance.TurnLeft;
	return Turns.IsValidIndex(TurnInPlaceAngleIndex) ? Turns[TurnInPlaceAngleIndex].Get() : nullptr;
}

const FGinkgoGaitAnims& UGinkgoAnimInstance::GetGaitAnims(const EGinkgoAnimGait InGait) const
{
	switch (InGait)
	{
	case EGinkgoAnimGait::Walk:
		return WalkAnims;
	case EGinkgoAnimGait::Sprint:
		return SprintAnims;
	case EGinkgoAnimGait::Crouch:
		return CrouchAnims;
	default:
		return RunAnims;
	}
}

const FGinkgoStanceAnims& UGinkgoAnimInstance::GetStanceAnims() const
{
	return MoveState.bCrouched ? CrouchedAnims : StandAnims;
}

const FGinkgoGaitAnims& UGinkgoAnimInstance::GetGaitAnims(const EGinkgoAnimGait InGait, const EGinkgoAnimDirection InDirection) const
{
	return InGait == EGinkgoAnimGait::Sprint && InDirection != EGinkgoAnimDirection::F ? RunAnims : GetGaitAnims(InGait);
}

float UGinkgoAnimInstance::ToBodyAngle(const float AngleToFacing) const
{
	return FMath::UnwindDegrees(AngleToFacing - RootYawOffset);
}

float UGinkgoAnimInstance::GetMoveAngle() const
{
	return ToBodyAngle(Intent.bHasIntent ? IntentLocalAngle : Motion.LocalAngle);
}

void UGinkgoAnimInstance::UpdateOrientationWarping(const bool bUseIntentWithoutVelocity)
{
	float MoveAngle = 0.f;
	if (Motion.Speed > MinDirectionSpeed)
	{
		MoveAngle = ToBodyAngle(Motion.LocalAngle);
	}
	else if (bUseIntentWithoutVelocity && Intent.bHasIntent)
	{
		MoveAngle = ToBodyAngle(IntentLocalAngle);
	}
	else
	{
		// 不归零：同一个片段里修正一跳，腿就一下子转过去（斜着急停，速度低于 MinDirectionSpeed 的那一帧腿转 45°，脚掌一帧挪 40 cm）。
		// 修正节点在角度为 0 时整个不求值，自己的插值也跳过，所以这里不能交给它平滑
		return;
	}
	OrientationWarpingAngle = FMath::Clamp(FMath::UnwindDegrees(MoveAngle - Ginkgo::Animation::DirectionAngle(Direction)), -MaxOrientationWarpingAngle,
		MaxOrientationWarpingAngle);
}

float UGinkgoAnimInstance::EvaluateCurve(const UAnimSequence* Sequence, const FName CurveName, const float Time, const float Default)
{
	if (!Sequence || !Sequence->HasCurveData(CurveName, /*bForceUseRawData*/ false))
	{
		return Default;
	}
	return Sequence->EvaluateCurveData(CurveName, FAnimExtractContext(static_cast<double>(Time)));
}

UAnimSequence* UGinkgoAnimInstance::ChooseByPlantedFoot(UAnimSequence* LeftFootAnim, UAnimSequence* RightFootAnim, const float Distance) const
{
	if (!LeftFootAnim || !RightFootAnim)
	{
		return LeftFootAnim ? LeftFootAnim : RightFootAnim;
	}
	const bool bLeftPlanted = GetCurveValue(ContactLeftCurveName) >= GetCurveValue(ContactRightCurveName);
	const auto IsLeftPlantedAtEntry = [Distance](const UAnimSequence* Sequence)
	{
		const float Entry = Ginkgo::Animation::FindTimeAtCurveValue([Sequence](const float Time) { return EvaluateCurve(Sequence, DistanceCurveName, Time); },
			-Distance, Sequence->GetPlayLength());
		return EvaluateCurve(Sequence, ContactLeftCurveName, Entry) >= EvaluateCurve(Sequence, ContactRightCurveName, Entry);
	};
	const bool bLeftMatches = IsLeftPlantedAtEntry(LeftFootAnim) == bLeftPlanted;
	const bool bRightMatches = IsLeftPlantedAtEntry(RightFootAnim) == bLeftPlanted;
	return !bLeftMatches && bRightMatches ? RightFootAnim : LeftFootAnim;
}

bool UGinkgoAnimInstance::IsLeftFootNext() const
{
	const float Left = GetCurveValue(ContactLeftCurveName);
	const float Right = GetCurveValue(ContactRightCurveName);
	if (FMath::Max(Left, Right) > 0.5f)
	{
		return Left >= Right;
	}
	// 只看脚的标记：跳跃片段里还有 Takeoff、Apex、Impact
	TArray<FAnimSyncMarker> FootMarkers;
	if (PoseSequence)
	{
		FootMarkers = PoseSequence->AuthoredSyncMarkers.FilterByPredicate([](const FAnimSyncMarker& Marker)
		{
			return Marker.MarkerName == LeftFootMarkerName || Marker.MarkerName == RightFootMarkerName;
		});
	}
	return Ginkgo::Animation::NextMarkerName(FootMarkers, PoseSequenceTime) != RightFootMarkerName;
}

void UGinkgoAnimInstance::RecordSequence(const UAnimSequence* Sequence, const float Time, const float PlayRate)
{
	PoseSequence = Sequence;
	PoseSequenceTime = Time;
	LocomotionTime = Time;
#if !UE_BUILD_SHIPPING
	DebugFrame.Sequence = Sequence;
	DebugFrame.SequenceTime = Time;
	DebugFrame.PlayRate = PlayRate;
#endif
}

void UGinkgoAnimInstance::RecordPlayerTime(const FAnimUpdateContext& Context, const FSequencePlayerReference& Player)
{
	const UAnimSequenceBase* Sequence = USequencePlayerLibrary::GetSequencePure(Player);
	if (!Sequence)
	{
		return;
	}
	// 同 UAnimSequenceBase::TickAssetPlayer：先乘片段的 RateScale 再乘帧长，浮点的舍入也一样
	float Time = USequencePlayerLibrary::GetAccumulatedTime(Player);
	const float MoveDelta = USequencePlayerLibrary::GetPlayRate(Player) * Sequence->RateScale * UAnimExecutionContextLibrary::GetDeltaTime(Context);
	if (MoveDelta != 0.f)
	{
		FAnimationRuntime::AdvanceTime(USequencePlayerLibrary::GetLoopAnimation(Player), MoveDelta, Time, Sequence->GetPlayLength());
	}
	LocomotionTime = Time;
}

void UGinkgoAnimInstance::PushFootsteps()
{
	const UAnimSequence* Sequence = PoseSequence;
	const float LastTime = FootstepTime;
	const bool bSameSequence = Sequence && Sequence == FootstepSequence;
	FootstepSequence = Sequence;
	FootstepTime = LocomotionTime;
	APawn* Pawn = TryGetPawnOwner();
	if (!bSameSequence || !Snapshot.bValid || UpdateDeltaSeconds <= 0.f || !Pawn || (!Pawn->IsLocallyControlled() && !Pawn->HasAuthority()))
	{
		return;
	}
	UGinkgoCameraContextComponent* Context = Pawn->IsLocallyControlled() ? UGinkgoCameraContextComponent::Find(Pawn) : nullptr;
	AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(Pawn);
	const TArray<FAnimSyncMarker> FootMarkers = Sequence->AuthoredSyncMarkers.FilterByPredicate([](const FAnimSyncMarker& Marker)
	{
		return Marker.MarkerName == LeftFootMarkerName || Marker.MarkerName == RightFootMarkerName;
	});
	// 片段的时刻在这一帧里匀速走过：跨过的几成就是这一帧 [帧末 − 帧长, 帧末] 里的几成
	const double FrameEnd = GetWorld()->GetTimeSeconds();
	for (const TPair<FName, float>& Step : Ginkgo::Animation::CrossFootMarkers(FootMarkers, LastTime, LocomotionTime, Sequence->GetPlayLength(), LastFootstep))
	{
		FGinkgoCameraEvent Event;
		Event.Tag = FMovementTags::Movement_Event_Footstep;
		Event.Time = FrameEnd - UpdateDeltaSeconds * (1.0 - Step.Value);
		Event.Magnitude = Step.Key == LeftFootMarkerName ? 1.f : -1.f;
		if (Context) Context->PushEvent(Event);
		// NativePostEvaluateAnimation 已回到游戏线程；听觉独立于相机是否收集事件，也不在模拟代理上重复发出。
		if (Character) Character->GetNoiseEmitter()->ReportFootstep(Gait);
	}
}

// ===== 节点函数 =====

void UGinkgoAnimInstance::UpdateIdleAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	ReportState(EGinkgoLocomotionState::Idle, ERootYawMode::Accumulate);
	OrientationWarpingAngle = 0.f;
	bool bValid = false;
	const FSequencePlayerReference Player = ToPlayer(Node, bValid);
	if (!bValid)
	{
		return;
	}
	UAnimSequence* Idle = GetStanceAnims().Idle;
	if (Idle && USequencePlayerLibrary::GetSequencePure(Player) != Idle)
	{
		USequencePlayerLibrary::SetSequenceWithInertialBlending(Context, Player, Idle, CycleBlendTime);
	}
	RecordSequence(Idle, USequencePlayerLibrary::GetAccumulatedTime(Player), 1.f);
	RecordPlayerTime(Context, Player);
}

void UGinkgoAnimInstance::SetupTurnInPlaceAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	// 补偿为负：视角向右转过去了，网格的根落在左后方，向右转回来
	bTurnInPlaceRight = RootYawOffset < 0.f;
	TurnInPlaceAngleIndex = Ginkgo::Animation::TurnInPlaceIndex(FMath::Abs(RootYawOffset));
	bTurnInPlaceCrouched = MoveState.bCrouched;
	UAnimSequence* Turn = GetTurnInPlaceAnim();
	USequenceEvaluatorLibrary::SetSequence(Evaluator, Turn);
	USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, 0.f);
	bTurnInPlaceFinished = Turn == nullptr;
	// 首次 Update 保持片段起点；下一帧 UpdateLocomotion 才会在图更新之前同步推进姿势与偏航补偿。
	TurnInPlaceSequence = nullptr;
}

void UGinkgoAnimInstance::UpdateTurnInPlaceAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	ReportState(EGinkgoLocomotionState::TurnInPlace, ERootYawMode::Accumulate);
	OrientationWarpingAngle = 0.f;
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	// RotateRoot 的暴露属性在节点回调之前已经取值，不能在这里补首帧转角；否则这一帧姿势先转，
	// 下一帧根偏航一次偿还两帧角度，180° 转身尤其明显。首次保持 0，后续由 UpdateLocomotion 预先偿还再推进。
	if (TurnInPlaceSequence)
	{
		USequenceEvaluatorLibrary::AdvanceTime(Context, Evaluator, 1.f);
	}
	const UAnimSequence* Turn = Cast<UAnimSequence>(USequenceEvaluatorLibrary::GetSequence(Evaluator));
	float Time = USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator);
	const float Remaining = EvaluateCurve(Turn, RemainingTurnYawCurveName, Time);
	const bool bRotated = Turn && FMath::Abs(Remaining) < 1.f;
	// 蹲下、站起：同急停。转完之前换成另一套的同一个转身，从剩余转角相同的时刻接着转（偏航补偿接着按新片段的剩余转角还，
	// 这一帧的已经按旧片段还过了）；转完以后交给 Idle（它按站姿换片段），不等收势放完
	bool bStanceChanged = false;
	if (Turn && bTurnInPlaceCrouched != MoveState.bCrouched)
	{
		UAnimSequence* Next = bRotated ? nullptr : GetTurnInPlaceAnim();
		if (Next)
		{
			Time = Ginkgo::Animation::FindTimeAtCurveValue([Next](const float At) { return -FMath::Abs(EvaluateCurve(Next, RemainingTurnYawCurveName, At)); },
				-FMath::Abs(Remaining), Next->GetPlayLength());
			USequenceEvaluatorLibrary::SetSequenceWithInertialBlending(Context, Evaluator, Next, CycleBlendTime);
			USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Time);
			Turn = Next;
			bTurnInPlaceCrouched = MoveState.bCrouched;
		}
		else
		{
			// 站姿留着对不上，直到离开这个状态（转完之前没有另一套的片段时，转完再交出去）
			bStanceChanged = bRotated;
		}
	}
	TurnInPlaceSequence = Turn;
	TurnInPlaceTime = Time;
	// 平常放完收势再回 Idle：GASP 的转身片段后面跟着很长的收势（90° 的约 1.8 s），收势把脚放回站姿，提前交给 Idle 的话
	// 两种站姿的脚不在一处，混合时脚被带着走（turn 路线 60 fps，转完就交出去时混进 Idle 那 0.25 s 的脚滑 p90 从 14 变成 260 cm/s，
	// 再要求两只脚都着地也一样）。只有转完以后视角又把补偿推过阈值，或者站姿变了，才提前交出去
	bTurnInPlaceFinished = !Turn || Time >= Turn->GetPlayLength() - StateEndTime || (bRotated && FMath::Abs(RootYawOffset) >= TurnInPlaceThreshold)
		|| bStanceChanged;
	RecordSequence(Turn, Time, 1.f);
}

void UGinkgoAnimInstance::SetupStartAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	Gait = SelectGait();
	// 从静止起步：不看上一个方向，按意图的方向选（侧向时髋朝移动的方向）
	Direction = Ginkgo::Animation::SelectDirection(GetMoveAngle(), EGinkgoAnimDirection::F, 0.0);
	USequenceEvaluatorLibrary::SetSequence(Evaluator, GetGaitAnims(Gait, Direction).Start.Get(Direction));
	USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, 0.f);
	StartDistanceTraveled = 0.f;
	bStartFinished = false;
}

void UGinkgoAnimInstance::UpdateStartAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	ReportState(EGinkgoLocomotionState::Start, ERootYawMode::BlendOut);
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	const bool bStanceChanged = FollowStance();
	// 起步时又换了方向、速度还没到反向的门槛：换成新方向（与新站姿）的起步，从头放
	const EGinkgoAnimDirection NewDirection = Ginkgo::Animation::SelectDirection(GetMoveAngle(), Direction, DirectionDeadZone);
	if (NewDirection != Direction && Motion.Speed < PivotMinSpeed)
	{
		Direction = NewDirection;
		USequenceEvaluatorLibrary::SetSequenceWithInertialBlending(Context, Evaluator, GetGaitAnims(Gait, Direction).Start.Get(Direction), CycleBlendTime);
		USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, 0.f);
		StartDistanceTraveled = 0.f;
	}
	else if (bStanceChanged)
	{
		// 蹲下、站起：换成新站姿同一方向的起步，从片段里走过同样距离的时刻接着放
		const UAnimSequence* Current = Cast<UAnimSequence>(USequenceEvaluatorLibrary::GetSequence(Evaluator));
		UAnimSequence* Next = GetGaitAnims(Gait, Direction).Start.Get(Direction);
		if (Current && Next)
		{
			const float Traveled = EvaluateCurve(Current, DistanceCurveName, USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator));
			USequenceEvaluatorLibrary::SetSequenceWithInertialBlending(Context, Evaluator, Next, CycleBlendTime);
			USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Ginkgo::Animation::FindTimeAtCurveValue(
				[Next](const float At) { return EvaluateCurve(Next, DistanceCurveName, At); }, Traveled, Next->GetPlayLength()));
		}
	}

	// 距离匹配：按这一帧可见的根走过的距离推进，起步比片段快得多，播放速率的上限放宽
	const UAnimSequence* Start = Cast<UAnimSequence>(USequenceEvaluatorLibrary::GetSequence(Evaluator));
	float Time = USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator);
	if (Start)
	{
		Time = Ginkgo::Animation::AdvanceTimeByDistance([Start](const float At) { return EvaluateCurve(Start, DistanceCurveName, At); }, Time, Motion.FrameDistance,
			Start->GetPlayLength(), UAnimExecutionContextLibrary::GetDeltaTime(Context), StartPlayRateClamp);
		USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Time);
	}
	StartDistanceTraveled += Motion.FrameDistance;
	bStartFinished = StartDistanceTraveled >= StartBlendOutDistance;
	UpdateOrientationWarping(/*bUseIntentWithoutVelocity*/ true);
	RecordSequence(Start, Time, 1.f);
}

void UGinkgoAnimInstance::SetupCycleAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	bool bValid = false;
	const FSequencePlayerReference Player = ToPlayer(Node, bValid);
	if (!bValid)
	{
		return;
	}
	Gait = SelectGait();
	// 接着起步、反向的方向（滞回与髋的朝向连着）
	Direction = Ginkgo::Animation::SelectDirection(Motion.Speed > MinDirectionSpeed ? ToBodyAngle(Motion.LocalAngle) : GetMoveAngle(), Direction, DirectionDeadZone);
	UAnimSequence* Cycle = GetGaitAnims(Gait, Direction).Cycle.Get(Direction);
	USequencePlayerLibrary::SetSequence(Player, Cycle);
	// 步伐对上：从现在着地的那只脚在循环里落地的时刻放起。不按同步标记换算起步、反向走到的几成：那两种片段不循环，
	// 接循环时（起步只用开头 40 cm）多半还没到第一个标记，换算出来的步伐不对，实测（ground 路线 60 fps）接循环那 0.25 s 的脚滑中位数 305 变成 396 cm/s
	if (Cycle)
	{
		const bool bLeftPlanted = GetCurveValue(ContactLeftCurveName) >= GetCurveValue(ContactRightCurveName);
		USequencePlayerLibrary::SetAccumulatedTime(Player, FirstMarkerTime(*Cycle, bLeftPlanted ? LeftFootMarkerName : RightFootMarkerName));
	}
	StrideScaleTarget = 1.f;
}

void UGinkgoAnimInstance::UpdateCycleAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	ReportState(EGinkgoLocomotionState::Cycle, ERootYawMode::BlendOut);
	bool bValid = false;
	const FSequencePlayerReference Player = ToPlayer(Node, bValid);
	if (!bValid)
	{
		return;
	}
	UAnimSequence* Current = Cast<UAnimSequence>(USequencePlayerLibrary::GetSequencePure(Player));
	const EGinkgoAnimGait NewGait = SelectGait();
	// 速度还没起来（落地时没有水平速度）看意图，与 SetupCycleAnim、朝向修正一致
	const float MoveAngle = Motion.Speed > MinDirectionSpeed ? ToBodyAngle(Motion.LocalAngle) : GetMoveAngle();
	const EGinkgoAnimDirection NewDirection = Ginkgo::Animation::SelectDirection(MoveAngle, Direction, DirectionDeadZone);
	if (NewGait != Gait || NewDirection != Direction)
	{
		Gait = NewGait;
		Direction = NewDirection;
		UAnimSequence* Next = GetGaitAnims(Gait, Direction).Cycle.Get(Direction);
		if (Next && Next != Current)
		{
			// 步伐对上：原片段走到两个同步标记之间的几成，新片段同名标记之后同样的几成
			const float Time = Current ? Ginkgo::Animation::MatchMarkerPhase(Current->AuthoredSyncMarkers, USequencePlayerLibrary::GetAccumulatedTime(Player),
				Current->GetPlayLength(), Next->AuthoredSyncMarkers, Next->GetPlayLength()) : 0.f;
			USequencePlayerLibrary::SetSequenceWithInertialBlending(Context, Player, Next, CycleBlendTime);
			USequencePlayerLibrary::SetAccumulatedTime(Player, Time);
			Current = Next;
		}
	}

	// 速度比分给播放速率与步幅：片段的速度取 GASP 的 movedata_speed 曲线
	double PlayRate = 1.0;
	double Stride = 1.0;
	const float Time = USequencePlayerLibrary::GetAccumulatedTime(Player);
	const float AnimSpeed = EvaluateCurve(Current, MoveDataSpeedCurveName, Time);
	if (AnimSpeed > 1.f)
	{
		Ginkgo::Animation::SplitSpeedRatio(Motion.Speed / AnimSpeed, CyclePlayRateClamp, PlayRate, Stride);
	}
	USequencePlayerLibrary::SetPlayRate(Player, static_cast<float>(PlayRate));
	// 步幅修正拉得太长、缩得太短都会走样：夹在 0.5–1.5（侧向、后退的跑要 1.09、1.27）
	StrideScaleTarget = FMath::Clamp(static_cast<float>(Stride), 0.5f, 1.5f);
	UpdateOrientationWarping(/*bUseIntentWithoutVelocity*/ true);
	RecordSequence(Current, Time, static_cast<float>(PlayRate));
	RecordPlayerTime(Context, Player);
}

void UGinkgoAnimInstance::SetupStopAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	// 按速度的方向（滞回与髋的朝向接着循环），步态不换：松开冲刺的同时松手，仍放冲刺的急停。只有松手的同时蹲下、站起才换成新站姿的
	Direction = Ginkgo::Animation::SelectDirection(ToBodyAngle(Motion.LocalAngle), Direction, DirectionDeadZone);
	FollowStanceToStop();
	float Entry = 0.f;
	UAnimSequence* Stop = SelectStopAnim(Entry);
	USequenceEvaluatorLibrary::SetSequence(Evaluator, Stop);
	USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Entry);
	bStopFinished = Stop == nullptr;
}

void UGinkgoAnimInstance::UpdateStopAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		ReportState(EGinkgoLocomotionState::Stop, ERootYawMode::BlendOut);
		return;
	}
	const UAnimSequence* Stop = Cast<UAnimSequence>(USequenceEvaluatorLibrary::GetSequence(Evaluator));
	float Time = USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator);
	// 停下之前：距离曲线在停止点之前是负的
	const auto IsBeforeStop = [this](const UAnimSequence* Sequence, const float At)
	{
		return Intent.StopDistance > DistanceMatchTolerance && EvaluateCurve(Sequence, DistanceCurveName, At) < 0.f;
	};
	// 蹲下、站起：片段是进入时按站姿选的（GASP 的急停停下以后还有 2–5 s 的收势，之前蹲下要等它放完，stance 路线跑步急停时蹲下 5.3 s 才换）。
	// 停下之前换成新站姿同一方向、同一只脚的急停，对到剩下的停止点；停下以后交给 Idle（它按站姿换片段），不等收势放完。
	// 收势里的脚还没收回站姿，蹲下、站起时怎么都要挪到新站姿的位置：换成另一套急停、从停下之后同样久的时刻放，脚滑的峰值与交给 Idle
	// 差不多（跑步急停的收势里蹲下，右脚从身前 52 cm 挪到身后 20 cm，两种都约 610 cm/s；站起时交给 Idle 的还小一些），所以交给 Idle
	bool bStanceChanged = false;
	if (Stop && (Gait == EGinkgoAnimGait::Crouch) != MoveState.bCrouched)
	{
		if (IsBeforeStop(Stop, Time))
		{
			const bool bRightFoot = Stop == GetGaitAnims(Gait, Direction).StopRightFoot.Get(Direction);
			FollowStanceToStop();
			const FGinkgoGaitAnims& Anims = GetGaitAnims(Gait, Direction);
			if (UAnimSequence* Next = bRightFoot ? Anims.StopRightFoot.Get(Direction) : Anims.StopLeftFoot.Get(Direction))
			{
				Time = Ginkgo::Animation::FindTimeAtCurveValue([Next](const float At) { return EvaluateCurve(Next, DistanceCurveName, At); }, -Intent.StopDistance,
					Next->GetPlayLength());
				USequenceEvaluatorLibrary::SetSequenceWithInertialBlending(Context, Evaluator, Next, CycleBlendTime);
				USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Time);
				Stop = Next;
			}
		}
		else
		{
			// 步态留着对不上，直到离开这个状态（下一帧转换时还读得到）
			bStanceChanged = true;
		}
	}
	bool bStopped = true;
	if (Stop)
	{
		const auto Distance = [Stop](const float At) { return EvaluateCurve(Stop, DistanceCurveName, At); };
		// 停下之前每帧对到预测的停止点，只往前不往回；停下之后按原速放完收势
		if (IsBeforeStop(Stop, Time))
		{
			Time = FMath::Max(Time, Ginkgo::Animation::FindTimeAtCurveValue(Distance, -Intent.StopDistance, Stop->GetPlayLength()));
			USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Time);
			bStopped = false;
		}
		else
		{
			USequenceEvaluatorLibrary::AdvanceTime(Context, Evaluator, 1.f);
			Time = USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator);
		}
	}
	// 停下之前身体保持朝着跑的方向（松开冲刺时胶囊一下子转回视角，也由补偿接住），停下以后与 Idle 一样，补偿大了就原地转身
	ReportState(EGinkgoLocomotionState::Stop, bStopped ? ERootYawMode::Accumulate : ERootYawMode::Hold);
	bStopFinished = !Stop || Time >= Stop->GetPlayLength() - StateEndTime || bStanceChanged;
	// 停下以后腿保持停下时的朝向放完收势，回 Idle 时由混合转回来
	UpdateOrientationWarping(/*bUseIntentWithoutVelocity*/ false);
	RecordSequence(Stop, Time, 1.f);
}

void UGinkgoAnimInstance::SetupPivotAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	// 按原来的方向选（Pivot_F_B 是向前跑时往回），做完以后接新方向的循环
	const EGinkgoAnimDirection From = Ginkgo::Animation::SelectDirection(ToBodyAngle(Motion.LocalAngle), Direction, DirectionDeadZone);
	const FGinkgoGaitAnims& Anims = Gait == EGinkgoAnimGait::Sprint ? RunAnims : GetGaitAnims(Gait);
	UAnimSequence* Pivot = ChooseByPlantedFoot(Anims.PivotLeftFoot.Get(From), Anims.PivotRightFoot.Get(From), Intent.PivotDistance);
	USequenceEvaluatorLibrary::SetSequence(Evaluator, Pivot);
	const float Entry = Pivot ? Ginkgo::Animation::FindTimeAtCurveValue([Pivot](const float At) { return EvaluateCurve(Pivot, DistanceCurveName, At); },
		-Intent.PivotDistance, Pivot->GetPlayLength()) : 0.f;
	USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Entry);
	Direction = Ginkgo::Animation::PivotExitDirection(From);
	bPivotFinished = Pivot == nullptr;
}

void UGinkgoAnimInstance::UpdatePivotAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	ReportState(EGinkgoLocomotionState::Pivot, ERootYawMode::BlendOut);
	OrientationWarpingAngle = 0.f;
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	const UAnimSequence* Pivot = Cast<UAnimSequence>(USequenceEvaluatorLibrary::GetSequence(Evaluator));
	float Time = USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator);
	if (Pivot)
	{
		const auto Distance = [Pivot](const float At) { return EvaluateCurve(Pivot, DistanceCurveName, At); };
		// 过零之前对到预测的反向点，只往前不往回；过零之后按走过的距离推进
		if (Intent.PivotDistance > DistanceMatchTolerance && Distance(Time) < 0.f)
		{
			Time = FMath::Max(Time, Ginkgo::Animation::FindTimeAtCurveValue(Distance, -Intent.PivotDistance, Pivot->GetPlayLength()));
		}
		else
		{
			Time = Ginkgo::Animation::AdvanceTimeByDistance(Distance, Time, Motion.FrameDistance, Pivot->GetPlayLength(),
				UAnimExecutionContextLibrary::GetDeltaTime(Context), PivotPlayRateClamp);
		}
		USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Time);
	}
	bPivotFinished = bPivotFinished || !Pivot || Time >= Pivot->GetPlayLength() - StateEndTime;
	RecordSequence(Pivot, Time, 1.f);
}

bool UGinkgoAnimInstance::IsAirState(const EGinkgoLocomotionState State)
{
	return State == EGinkgoLocomotionState::Jump || State == EGinkgoLocomotionState::LedgeOff || State == EGinkgoLocomotionState::Fall
		|| State == EGinkgoLocomotionState::Land;
}

EGinkgoJumpGait UGinkgoAnimInstance::SelectJumpGait(const double HorizontalSpeed) const
{
	if (HorizontalSpeed >= JumpGaitSpeeds.Z)
	{
		return EGinkgoJumpGait::Sprint;
	}
	if (HorizontalSpeed >= JumpGaitSpeeds.Y)
	{
		return EGinkgoJumpGait::Run;
	}
	return HorizontalSpeed >= JumpGaitSpeeds.X ? EGinkgoJumpGait::Walk : EGinkgoJumpGait::Stand;
}

float UGinkgoAnimInstance::MarkerTime(const UAnimSequence& Sequence, const FName MarkerName)
{
	const FAnimSyncMarker* Marker = Sequence.AuthoredSyncMarkers.FindByPredicate([MarkerName](const FAnimSyncMarker& Item) { return Item.MarkerName == MarkerName; });
	return Marker ? Marker->Time : -1.f;
}

UAnimSequence* UGinkgoAnimInstance::ChooseByContact(const FGinkgoFootAnims& Pair, const TFunctionRef<float(const UAnimSequence&)> Time, const bool bLeft)
{
	if (!Pair.LeftFoot || !Pair.RightFoot || Pair.LeftFoot == Pair.RightFoot)
	{
		return Pair.LeftFoot ? Pair.LeftFoot.Get() : Pair.RightFoot.Get();
	}
	const auto IsLeft = [&Time](const UAnimSequence& Sequence)
	{
		const float At = Time(Sequence);
		return EvaluateCurve(&Sequence, ContactLeftCurveName, At) >= EvaluateCurve(&Sequence, ContactRightCurveName, At);
	};
	return IsLeft(*Pair.LeftFoot) != bLeft && IsLeft(*Pair.RightFoot) == bLeft ? Pair.RightFoot.Get() : Pair.LeftFoot.Get();
}

UAnimSequence* UGinkgoAnimInstance::SelectLandAnim(const double ImpactSpeed, const bool bLeftFirst) const
{
	const FGinkgoJumpDirectionAnims& Anims = JumpAnims.Get(Direction);
	const int32 JumpGait = static_cast<int32>(SelectJumpGait(Motion.Speed));
	const FGinkgoFootAnims& Pair = ImpactSpeed > HeavyLandSpeed ? Anims.LandHeavy[JumpGait] : Anims.LandLight[JumpGait];
	// 先着地的脚：片段着地后一点，两只脚的接触曲线谁先起来
	return ChooseByContact(Pair, [](const UAnimSequence& Sequence) { return MarkerTime(Sequence, ImpactMarkerName) + TakeoffContactLead; }, bLeftFirst);
}

void UGinkgoAnimInstance::BeginLand(const UAnimSequence& Land)
{
	LandImpactTime = FMath::Max(MarkerTime(Land, ImpactMarkerName), 0.f);
	bLandMoving = SelectJumpGait(Motion.Speed) != EGinkgoJumpGait::Stand;
	if (!bLandMoving)
	{
		LandHandoffTime = LandImpactTime + LandSettleTime;
		return;
	}
	// 在动的：着地后先过 0.5 的那只脚（先着地的）再落回 0.5 以下，就是下一步迈出去了
	const float Length = Land.GetPlayLength();
	const float Latest = FMath::Min(LandImpactTime + LandMoveMaxTime, Length);
	FName Planted = NAME_None;
	LandHandoffTime = Latest;
	for (float At = LandImpactTime; At < Latest; At += LandScanStep)
	{
		const float Left = EvaluateCurve(&Land, ContactLeftCurveName, At);
		const float Right = EvaluateCurve(&Land, ContactRightCurveName, At);
		if (Planted.IsNone())
		{
			Planted = Left >= 0.5f || Right >= 0.5f ? (Left >= Right ? ContactLeftCurveName : ContactRightCurveName) : NAME_None;
		}
		else if ((Planted == ContactLeftCurveName ? Left : Right) < 0.5f)
		{
			LandHandoffTime = At;
			break;
		}
	}
}

void UGinkgoAnimInstance::UpdateAirAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	const double VerticalSpeed = Snapshot.MovementVelocity.Z;
	const double PreviousSpeed = PreviousVerticalSpeed;
	PreviousVerticalSpeed = VerticalSpeed;
	bool bValid = false;
	const FSequencePlayerReference Player = ToPlayer(Node, bValid);
	if (!bValid)
	{
		ReportState(EGinkgoLocomotionState::Fall, ERootYawMode::BlendOut);
		OrientationWarpingAngle = 0.f;
		bLandHold = false;
		return;
	}
	const float DeltaTime = UAnimExecutionContextLibrary::GetDeltaTime(Context);
	const bool bOnGround = MoveState.Mode == EGinkgoAnimMode::Ground || MoveState.Mode == EGinkgoAnimMode::Slide;
	const bool bEntered = !IsAirState(LocomotionState);
	EGinkgoLocomotionState Phase = bEntered ? EGinkgoLocomotionState::Fall : LocomotionState;
	const UAnimSequence* Playing = Cast<UAnimSequence>(USequencePlayerLibrary::GetSequencePure(Player));
	// 这一帧换成的片段（没换时为空）与片段的时刻
	UAnimSequence* Next = nullptr;
	float Time = USequencePlayerLibrary::GetAccumulatedTime(Player);
	const auto Switch = [&Next, &Time](UAnimSequence* Clip, const float At)
	{
		Next = Clip;
		Time = At;
	};

	// 离开地面：刚进来，或者着地以后又离地；空中竖直速度一下子变大也算（土狼起跳）。竖直速度向上是起跳，否则是走下边缘。
	// 起跳的脚看现在着地的那只，片段里离地前最后着地的那只与它对上
	if (!bOnGround && (bEntered || bLanded || VerticalSpeed > PreviousSpeed + JumpImpulseSpeed))
	{
		bLanded = false;
		LandImpactTime = -1.f;
		Direction = Ginkgo::Animation::SelectDirection(Motion.Speed > MinDirectionSpeed ? ToBodyAngle(Motion.LocalAngle) : 0.f, Direction, DirectionDeadZone);
		const FGinkgoJumpDirectionAnims& Anims = JumpAnims.Get(Direction);
		const bool bLeftPlanted = GetCurveValue(ContactLeftCurveName) >= GetCurveValue(ContactRightCurveName);
		const auto BeforeTakeoff = [](const UAnimSequence& Sequence) { return MarkerTime(Sequence, TakeoffMarkerName) - TakeoffContactLead; };
		UAnimSequence* Clip = nullptr;
		if (VerticalSpeed > 0.0)
		{
			Phase = EGinkgoLocomotionState::Jump;
			// 这一帧的移动里离的地：离地时的竖直速度按重力往回推这一帧
			JumpTakeoffSpeed = VerticalSpeed + Snapshot.Gravity * DeltaTime;
			Clip = ChooseByContact(Anims.Start[static_cast<int32>(SelectJumpGait(Motion.Speed))], BeforeTakeoff, bLeftPlanted);
		}
		else
		{
			Phase = EGinkgoLocomotionState::LedgeOff;
			Clip = ChooseByContact(Anims.Off[SelectJumpGait(Motion.Speed) <= EGinkgoJumpGait::Walk ? 0 : 1], BeforeTakeoff, bLeftPlanted);
		}
		// 从片段离地的时刻放起，跳过之前的预备（我们一按就离地）
		Switch(Clip, Clip ? FMath::Max(MarkerTime(*Clip, TakeoffMarkerName), 0.f) : 0.f);
		Phase = Clip ? Phase : EGinkgoLocomotionState::Fall;
	}

	const UAnimSequence* Current = Next ? Next : Playing;
	if (Phase == EGinkgoLocomotionState::Jump && Current)
	{
		const float Takeoff = FMath::Max(MarkerTime(*Current, TakeoffMarkerName), 0.f);
		const float Apex = MarkerTime(*Current, ApexMarkerName);
		if (VerticalSpeed > 0.0 && JumpTakeoffSpeed > 0.0 && Apex > Takeoff)
		{
			// 上升：竖直速度从离地时的降到 0，片段从离地走到最高点，两边都是匀减速，按比例对上。撞到天花板时竖直速度一下子没了，片段跟着到顶
			Time = FMath::Max(Time, Takeoff + static_cast<float>(FMath::Clamp(1.0 - VerticalSpeed / JumpTakeoffSpeed, 0.0, 1.0)) * (Apex - Takeoff));
		}
		else if (!Next)
		{
			// 过了最高点：片段接着往下落，按原速放，放完换下落循环
			Time = FMath::Max(Time, Apex) + DeltaTime;
		}
		Phase = Time < Current->GetPlayLength() ? Phase : EGinkgoLocomotionState::Fall;
	}
	else if (Phase == EGinkgoLocomotionState::LedgeOff && Current && !Next)
	{
		Time += DeltaTime;
		Phase = Time < Current->GetPlayLength() ? Phase : EGinkgoLocomotionState::Fall;
	}

	// 落地（空中）：下落时，顺着轨迹预测的着地时间不到落地片段着地前的时长，就从片段里离着地同样远的时刻放起；之后每帧重对，只往前。
	// 预测的着地远了（飘过了边缘）回到下落
	// 落地的方向按选片段这一刻的速度相对身体：空中身体会转（冲刺朝向在离地时换回视角，偏航补偿接住以后按弹簧转回来），起跳时的不一定对
	const auto UpdateLandDirection = [this]
	{
		Direction = Ginkgo::Animation::SelectDirection(Motion.Speed > MinDirectionSpeed ? ToBodyAngle(Motion.LocalAngle) : 0.f, Direction, DirectionDeadZone);
	};
	const double TimeToImpact = Snapshot.TimeToImpact;
	if (!bOnGround && Phase != EGinkgoLocomotionState::Land && VerticalSpeed <= 0.0 && TimeToImpact >= 0.0)
	{
		UpdateLandDirection();
		UAnimSequence* Land = SelectLandAnim(Snapshot.ImpactSpeed, Snapshot.bLeftFootLower);
		if (Land && TimeToImpact <= MarkerTime(*Land, ImpactMarkerName))
		{
			Phase = EGinkgoLocomotionState::Land;
			BeginLand(*Land);
			Switch(Land, LandImpactTime - static_cast<float>(TimeToImpact));
		}
	}
	else if (!bOnGround && Phase == EGinkgoLocomotionState::Land && !bLanded)
	{
		if (TimeToImpact < 0.0 || TimeToImpact > LandImpactTime + LandAbortMargin)
		{
			Phase = EGinkgoLocomotionState::Fall;
			LandImpactTime = -1.f;
		}
		else
		{
			Time = FMath::Max(Time, LandImpactTime - static_cast<float>(TimeToImpact));
		}
	}

	// 着地：没预测到（扫不到，或刚离地就着地）时按这一刻的下落速度现选落地片段，从着地的时刻放；之后站着的按原速、在动的按走过的距离推进
	bool bRelease = false;
	if (bOnGround)
	{
		if (!bLanded)
		{
			if (Phase != EGinkgoLocomotionState::Land || LandImpactTime < 0.f)
			{
				UpdateLandDirection();
				if (UAnimSequence* Land = SelectLandAnim(FMath::Max(-PreviousSpeed, 0.0), Snapshot.bLeftFootLower))
				{
					BeginLand(*Land);
					Switch(Land, LandImpactTime);
				}
			}
			else
			{
				Time = FMath::Max(Time, LandImpactTime);
			}
			Phase = EGinkgoLocomotionState::Land;
			bLanded = true;
		}
		else if (Current && bLandMoving && Current->HasCurveData(DistanceCurveName, /*bForceUseRawData*/ false))
		{
			Time = Ginkgo::Animation::AdvanceTimeByDistance([Current](const float At) { return EvaluateCurve(Current, DistanceCurveName, At); }, Time,
				Motion.FrameDistance, Current->GetPlayLength(), DeltaTime, LandPlayRateClamp);
		}
		else
		{
			Time += DeltaTime;
		}
		// 交出去：站着的有意图立刻交（起步慢不得），没有意图放到冲击吸收完；在动的没有意图立刻交给急停，有意图放到下一步交给循环。
		// 蹲着的（按着蹲落地、没快到滑铲）立刻交，落地的片段都是站着的
		const UAnimSequence* Land = Next ? Next : Playing;
		if (!Land || LandImpactTime < 0.f || MoveState.bCrouched || Time >= Land->GetPlayLength() - StateEndTime)
		{
			bRelease = true;
		}
		else
		{
			bRelease = bLandMoving ? !Intent.bHasIntent || Time >= LandHandoffTime : Intent.bHasIntent || Time >= LandHandoffTime;
		}
	}
	bLandHold = !bRelease;

	// 下落循环按原速循环；别的阶段由这里给时刻，播放速率为 0
	if (Phase == EGinkgoLocomotionState::Fall && FallLoop && Current != FallLoop)
	{
		Switch(FallLoop, 0.f);
	}
	if (Next && Next != Playing)
	{
		USequencePlayerLibrary::SetSequenceWithInertialBlending(Context, Player, Next, CycleBlendTime);
	}
	const bool bExplicitTime = Phase != EGinkgoLocomotionState::Fall;
	USequencePlayerLibrary::SetPlayRate(Player, bExplicitTime ? 0.f : 1.f);
	if (bExplicitTime || Next)
	{
		USequencePlayerLibrary::SetAccumulatedTime(Player, Time);
	}

	// 着地那一帧胶囊可能一下子转过去（在地面上冲刺朝向恢复，最多 180°），偏航补偿平常只接住快过 SnapYawRate 的那部分，剩下的几度让网格
	// 当帧转过去、朝向修正一帧跳几度。以前着地就换状态，混合接住了；现在落地的片段接着放，接不住（air 路线 144 fps 脚跳 17 cm）。
	// 所以预测下一帧着地时，偏航补偿全部接住（下一帧生效），着地那一帧网格不转，之后照常用弹簧收回
	const bool bTouchdownNext = !bOnGround && Phase == EGinkgoLocomotionState::Land && TimeToImpact >= 0.0 && TimeToImpact <= DeltaTime * TouchdownFrameMargin;
	ReportState(Phase, bTouchdownNext ? ERootYawMode::Hold : ERootYawMode::BlendOut);
	// 落地时腿朝移动的方向（同起步、循环），从选落地片段的那一帧起就给：换片段时的跳变由混合接住，同一个片段里不跳
	if (Phase == EGinkgoLocomotionState::Land)
	{
		UpdateOrientationWarping(/*bUseIntentWithoutVelocity*/ true);
	}
	else
	{
		OrientationWarpingAngle = 0.f;
	}
	RecordSequence(Next ? Next : Playing, bExplicitTime || Next ? Time : USequencePlayerLibrary::GetAccumulatedTime(Player), 1.f);
	RecordPlayerTime(Context, Player);
#if !UE_BUILD_SHIPPING
	DebugFrame.LandImpactTime = Phase == EGinkgoLocomotionState::Land ? LandImpactTime : -1.f;
#endif
}

void UGinkgoAnimInstance::SetupSlideStartAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	// 倒下去的是现在着地（或下一步落地）的那只脚：GASP 的 Into_Lfoot 在 0.59 s 最后落左脚，之后倒下去；之前是跑过来的几步，不要
	const bool bLeft = IsLeftFootNext();
	UAnimSequence* Into = bLeft ? SlideAnims.IntoLeftFoot : SlideAnims.IntoRightFoot;
	const float Entry = Into ? Ginkgo::Animation::LastMarkerTime(Into->AuthoredSyncMarkers, bLeft ? LeftFootMarkerName : RightFootMarkerName) : 0.f;
	USequenceEvaluatorLibrary::SetSequence(Evaluator, Into);
	USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, FMath::Max(Entry, 0.f));
	bSlideStartFinished = Into == nullptr;
}

void UGinkgoAnimInstance::UpdateSlideStartAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	ReportState(EGinkgoLocomotionState::SlideStart, ERootYawMode::FaceVelocity);
	OrientationWarpingAngle = 0.f;
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	// 不按地速放：起步加速后 1168 cm/s，片段的根只有 450 cm/s，倒下去的快慢照片段
	USequenceEvaluatorLibrary::AdvanceTime(Context, Evaluator, 1.f);
	const UAnimSequence* Into = Cast<UAnimSequence>(USequenceEvaluatorLibrary::GetSequence(Evaluator));
	const float Time = USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator);
	bSlideStartFinished = !Into || Time >= FMath::Min(SlideLoopTime, Into->GetPlayLength() - StateEndTime);
	RecordSequence(Into, Time, 1.f);
}

void UGinkgoAnimInstance::UpdateSlideLoopAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	ReportState(EGinkgoLocomotionState::SlideLoop, ERootYawMode::FaceVelocity);
	OrientationWarpingAngle = 0.f;
	bool bValid = false;
	const FSequencePlayerReference Player = ToPlayer(Node, bValid);
	if (!bValid)
	{
		return;
	}
	if (SlideAnims.Loop && USequencePlayerLibrary::GetSequencePure(Player) != SlideAnims.Loop)
	{
		USequencePlayerLibrary::SetSequenceWithInertialBlending(Context, Player, SlideAnims.Loop, CycleBlendTime);
	}
	RecordSequence(SlideAnims.Loop, USequencePlayerLibrary::GetAccumulatedTime(Player), 1.f);
	RecordPlayerTime(Context, Player);
}

void UGinkgoAnimInstance::SetupSlideExitAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	// 结束的那一帧移动组件已经按蹲的输入站起来（松开蹲）或还蹲着（速度降到 SlideEndSpeed 以下）
	UAnimSequence* Exit = SelectSlideExitAnim(Intent.bHasIntent);
	USequenceEvaluatorLibrary::SetSequence(Evaluator, Exit);
	USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Exit ? FMath::Min(SlideExitStartTime, Exit->GetPlayLength()) : 0.f);
	bSlideExitFinished = Exit == nullptr;
}

void UGinkgoAnimInstance::UpdateSlideExitAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	// 起身时网格的根仍朝速度：Out_Moving 接着往前走，Out_Idle 停下以后补偿留着，回 Idle 时照常累积、超过阈值原地转身
	ReportState(EGinkgoLocomotionState::SlideExit, ERootYawMode::FaceVelocity);
	OrientationWarpingAngle = 0.f;
	bool bValid = false;
	const FSequenceEvaluatorReference Evaluator = ToEvaluator(Node, bValid);
	if (!bValid)
	{
		return;
	}
	const UAnimSequence* Exit = Cast<UAnimSequence>(USequenceEvaluatorLibrary::GetSequence(Evaluator));
	float Time = USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator);
	// 蹲下、站起：起身的片段是进入时按站姿选的。换成另一种站姿的同一种起身，停下来的从同一个时刻、接着走的从走过同样距离的时刻接着放
	const bool bMovingExit = Exit && Exit->HasCurveData(DistanceCurveName, /*bForceUseRawData*/ false);
	const bool bCrouchedExit = Exit && (Exit == SlideAnims.ExitIdleCrouch.Get() || Exit == SlideAnims.ExitMovingCrouch.Get());
	if (Exit && bCrouchedExit != MoveState.bCrouched)
	{
		if (UAnimSequence* Next = SelectSlideExitAnim(bMovingExit))
		{
			Time = bMovingExit ? Ginkgo::Animation::FindTimeAtCurveValue([Next](const float At) { return EvaluateCurve(Next, DistanceCurveName, At); },
				EvaluateCurve(Exit, DistanceCurveName, Time), Next->GetPlayLength()) : FMath::Min(Time, Next->GetPlayLength());
			USequenceEvaluatorLibrary::SetSequenceWithInertialBlending(Context, Evaluator, Next, CycleBlendTime);
			USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Time);
			Exit = Next;
		}
	}
	if (Exit && Exit->HasCurveData(DistanceCurveName, /*bForceUseRawData*/ false))
	{
		// 起身接着走的（Out_Moving，导入时生成了距离曲线）：按走过的距离推进，同起步。片段起身时只有 225–300 cm/s，
		// 冲刺速度下按原速放脚滑 500 cm/s（slide 路线 60 fps），这样站起来得快、脚跟着地面走
		Time = Ginkgo::Animation::AdvanceTimeByDistance([Exit](const float At) { return EvaluateCurve(Exit, DistanceCurveName, At); }, Time, Motion.FrameDistance,
			Exit->GetPlayLength(), UAnimExecutionContextLibrary::GetDeltaTime(Context), SlideExitPlayRateClamp);
		USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Time);
	}
	else
	{
		// 停下来的（Out_Idle）：片段从约 465 cm/s 减速到 0，与站起来、蹲着停下的摩擦差不多，按原速放
		USequenceEvaluatorLibrary::AdvanceTime(Context, Evaluator, 1.f);
		Time = USequenceEvaluatorLibrary::GetAccumulatedTime(Evaluator);
	}
	bSlideExitFinished = !Exit || Time >= FMath::Min(SlideExitEndTime, Exit->GetPlayLength() - StateEndTime);
	RecordSequence(Exit, Time, 1.f);
}

#include "GinkgoAnimMath.h"

#include "Algo/Sort.h"            // 跟步伐时标记按时刻排好
#include "Animation/AnimTypes.h"  // FAnimSyncMarker

namespace
{
	/** 不分髋的朝向的四个方向 */
	enum class ECardinal : uint8
	{
		F,
		B,
		L,
		R,
	};

	ECardinal CardinalOf(const EGinkgoAnimDirection Direction)
	{
		switch (Direction)
		{
		case EGinkgoAnimDirection::B:
			return ECardinal::B;
		case EGinkgoAnimDirection::LL:
			[[fallthrough]];
		case EGinkgoAnimDirection::LR:
			return ECardinal::L;
		case EGinkgoAnimDirection::RL:
			[[fallthrough]];
		case EGinkgoAnimDirection::RR:
			return ECardinal::R;
		default:
			return ECardinal::F;
		}
	}

	/** 角度在这个方向的扇区里：扇区两边各放宽 Widen 度 */
	bool IsInSector(const ECardinal Cardinal, const double Angle, const double Widen)
	{
		switch (Cardinal)
		{
		case ECardinal::F:
			return FMath::Abs(Angle) <= 45.0 + Widen;
		case ECardinal::B:
			return FMath::Abs(Angle) >= 135.0 - Widen;
		case ECardinal::L:
			return Angle >= -135.0 - Widen && Angle <= -45.0 + Widen;
		default:
			return Angle >= 45.0 - Widen && Angle <= 135.0 + Widen;
		}
	}

	ECardinal ClassifyCardinal(const double Angle)
	{
		if (FMath::Abs(Angle) <= 45.0)
		{
			return ECardinal::F;
		}
		if (FMath::Abs(Angle) >= 135.0)
		{
			return ECardinal::B;
		}
		return Angle < 0.0 ? ECardinal::L : ECardinal::R;
	}
}

double Ginkgo::Animation::StopDistance(const double Speed, const double Friction, const double StopSpeed)
{
	if (Speed <= 0.0 || Friction <= 0.0)
	{
		return 0.0;
	}
	// 高于 S 时 dv/dt = −F·v，速度按指数减到 S，走过 (v − S)/F；之后 dv/dt = −F·S 匀减速，走过 S²/(2FS) = S/(2F)。
	// S 为 0 时总走第一支：纯指数衰减，走过 v/F
	const double S = FMath::Max(StopSpeed, 0.0);
	if (Speed > S)
	{
		return (Speed - S * 0.5) / Friction;
	}
	return Speed * Speed / (2.0 * Friction * S);
}

double Ginkgo::Animation::PivotDistance(const double Speed, const double Friction, const double StopSpeed, const double Deceleration)
{
	const double A = FMath::Max(Deceleration, 0.0);
	if (A <= 0.0)
	{
		return StopDistance(Speed, Friction, StopSpeed);
	}
	if (Speed <= 0.0 || Friction <= 0.0)
	{
		return 0.0;
	}
	const double S = FMath::Max(StopSpeed, 0.0);
	double Distance = 0.0;
	double Remaining = Speed;
	if (Speed > S)
	{
		// dv/dt = −(F·v + A)：令 u = v + A/F，则 du/dt = −F·u，u 按指数衰减。减到 S 用时 t₁ = ln((v + A/F)/(S + A/F))/F，
		// 走过 ∫(u − A/F)dt = (v − S)/F − (A/F)·t₁
		const double Offset = A / Friction;
		const double Time = FMath::Loge((Speed + Offset) / (S + Offset)) / Friction;
		Distance = (Speed - S) / Friction - Offset * Time;
		Remaining = S;
	}
	// 低于 S 时减速度 F·S + A 不变
	return Distance + Remaining * Remaining / (2.0 * (Friction * S + A));
}

double Ginkgo::Animation::LocalAngle(const FVector& Velocity, const double FacingYaw, const double MinSpeed)
{
	if (Velocity.SizeSquared2D() <= FMath::Square(MinSpeed))
	{
		return 0.0;
	}
	const double VelocityYaw = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
	return FMath::FindDeltaAngleDegrees(FacingYaw, VelocityYaw);
}

Ginkgo::Animation::FGinkgoMotionDifferencer::FOutput Ginkgo::Animation::FGinkgoMotionDifferencer::Update(const FInput& Input)
{
	if (bHasHistory && !Input.bCut && Input.DeltaSeconds <= 0.0)
	{
		return LastOutput;
	}
	FOutput Output;
	if (!bHasHistory || Input.bCut)
	{
		// 没有上一帧：位置差分不出速度，用移动组件的；之前的校正不算进来
		Output.Velocity = FVector(Input.FallbackVelocity.X, Input.FallbackVelocity.Y, 0.0);
		Output.VerticalSpeed = Input.FallbackVelocity.Z;
		Output.bReset = true;
	}
	else
	{
		Output.CorrectionOffset = Input.CorrectionOffsetTotal - PreviousCorrectionTotal;
		const FVector Displacement = Input.Location - PreviousLocation - Output.CorrectionOffset;
		Output.Velocity = FVector(Displacement.X, Displacement.Y, 0.0) / Input.DeltaSeconds;
		Output.VerticalSpeed = Displacement.Z / Input.DeltaSeconds;
		Output.FrameDistance = Displacement.Size2D();
		Output.YawRate = FMath::FindDeltaAngleDegrees(PreviousFacingYaw, Input.FacingYaw) / Input.DeltaSeconds;
	}
	bHasHistory = true;
	PreviousLocation = Input.Location;
	PreviousFacingYaw = Input.FacingYaw;
	PreviousCorrectionTotal = Input.CorrectionOffsetTotal;
	LastOutput = Output;
	return Output;
}

double Ginkgo::Animation::DirectionAngle(const EGinkgoAnimDirection Direction)
{
	switch (CardinalOf(Direction))
	{
	case ECardinal::B:
		return 180.0;
	case ECardinal::L:
		return -90.0;
	case ECardinal::R:
		return 90.0;
	default:
		return 0.0;
	}
}

EGinkgoAnimDirection Ginkgo::Animation::SelectDirection(const double LocalAngle, const EGinkgoAnimDirection Current, const double DeadZone)
{
	const ECardinal From = CardinalOf(Current);
	if (IsInSector(From, LocalAngle, DeadZone))
	{
		return Current;
	}
	switch (ClassifyCardinal(LocalAngle))
	{
	case ECardinal::B:
		return EGinkgoAnimDirection::B;
	case ECardinal::L:
		if (From == ECardinal::R)
		{
			return Current == EGinkgoAnimDirection::RL ? EGinkgoAnimDirection::LL : EGinkgoAnimDirection::LR;
		}
		return From == ECardinal::B ? EGinkgoAnimDirection::LR : EGinkgoAnimDirection::LL;
	case ECardinal::R:
		if (From == ECardinal::L)
		{
			return Current == EGinkgoAnimDirection::LL ? EGinkgoAnimDirection::RL : EGinkgoAnimDirection::RR;
		}
		return From == ECardinal::B ? EGinkgoAnimDirection::RL : EGinkgoAnimDirection::RR;
	default:
		return EGinkgoAnimDirection::F;
	}
}

EGinkgoAnimDirection Ginkgo::Animation::PivotExitDirection(const EGinkgoAnimDirection Direction)
{
	switch (Direction)
	{
	case EGinkgoAnimDirection::B:
		return EGinkgoAnimDirection::F;
	case EGinkgoAnimDirection::LL:
		return EGinkgoAnimDirection::RL;
	case EGinkgoAnimDirection::LR:
		return EGinkgoAnimDirection::RR;
	case EGinkgoAnimDirection::RL:
		return EGinkgoAnimDirection::LL;
	case EGinkgoAnimDirection::RR:
		return EGinkgoAnimDirection::LR;
	default:
		return EGinkgoAnimDirection::B;
	}
}

void Ginkgo::Animation::SplitSpeedRatio(const double SpeedRatio, const FVector2D& PlayRateClamp, double& OutPlayRate, double& OutStrideScale)
{
	OutPlayRate = FMath::Clamp(SpeedRatio, PlayRateClamp.X, PlayRateClamp.Y);
	OutStrideScale = OutPlayRate > UE_KINDA_SMALL_NUMBER ? SpeedRatio / OutPlayRate : 1.0;
}

int32 Ginkgo::Animation::TurnInPlaceIndex(const double AbsYaw)
{
	return FMath::Clamp(FMath::RoundToInt32(AbsYaw / 45.0) - 1, 0, 3);
}

double Ginkgo::Animation::AccumulateRootYawOffset(const double Offset, const double YawDelta, const double Limit)
{
	// 已经超过上限的（急停时接住的冲刺转身）不一下子压回去，只是不再变大
	const double Bound = FMath::Max(Limit, FMath::Abs(Offset));
	return FMath::Clamp(FMath::UnwindDegrees(Offset - YawDelta), -Bound, Bound);
}

float Ginkgo::Animation::FindTimeAtCurveValue(const TFunctionRef<float(float)> Curve, const float Target, const float Length)
{
	if (Length <= 0.f || Target <= Curve(0.f))
	{
		return 0.f;
	}
	if (Target > Curve(Length))
	{
		return Length;
	}
	// 曲线不减：值小于目标的一侧往右收，30 次把区间缩到长度的十亿分之一
	float Low = 0.f;
	float High = Length;
	for (int32 Iteration = 0; Iteration < 30; ++Iteration)
	{
		const float Middle = 0.5f * (Low + High);
		if (Curve(Middle) < Target)
		{
			Low = Middle;
		}
		else
		{
			High = Middle;
		}
	}
	return 0.5f * (Low + High);
}

float Ginkgo::Animation::AdvanceTimeByDistance(const TFunctionRef<float(float)> Curve, const float CurrentTime, const float DistanceTraveled, const float Length,
	const float DeltaTime, const FVector2D& PlayRateClamp)
{
	if (DeltaTime <= 0.f || Length <= 0.f)
	{
		return CurrentTime;
	}
	const float Matched = FindTimeAtCurveValue(Curve, Curve(CurrentTime) + DistanceTraveled, Length);
	const float PlayRate = FMath::Clamp((Matched - CurrentTime) / DeltaTime, static_cast<float>(PlayRateClamp.X), static_cast<float>(PlayRateClamp.Y));
	return FMath::Min(CurrentTime + PlayRate * DeltaTime, Length);
}

namespace
{
	/** 时刻落在第几个标记之后、到下一个标记走了几成 */
	struct FMarkerPhase
	{
		int32 Previous = 0;
		float Fraction = 0.f;
	};

	/** 片段在某个时刻的 FMarkerPhase：循环片段在第一个标记之前时，算作上一圈最后一个标记之后。Markers 不能是空的，Length 要大于 0 */
	FMarkerPhase GetMarkerPhase(const TConstArrayView<FAnimSyncMarker> Markers, const float InTime, const float Length)
	{
		const float Time = FMath::Fmod(InTime, Length);
		FMarkerPhase Phase;
		Phase.Previous = Markers.Num() - 1;
		for (int32 Index = 0; Index < Markers.Num() && Markers[Index].Time <= Time; ++Index)
		{
			Phase.Previous = Index;
		}
		const int32 Next = (Phase.Previous + 1) % Markers.Num();
		const float PreviousTime = Markers[Phase.Previous].Time;
		float Span = Markers[Next].Time - PreviousTime;
		float Elapsed = Time - PreviousTime;
		Span += Span <= 0.f ? Length : 0.f;
		Elapsed += Elapsed < 0.f ? Length : 0.f;
		Phase.Fraction = Span > 0.f ? FMath::Clamp(Elapsed / Span, 0.f, 1.f) : 0.f;
		return Phase;
	}

	/** 第 Start 个标记到它的下一个标记那一段里走了 Fraction 成的时刻（绕回 0 到 Length） */
	float GetSegmentTime(const TConstArrayView<FAnimSyncMarker> Markers, const int32 Start, const float Fraction, const float Length)
	{
		const int32 End = (Start + 1) % Markers.Num();
		float Span = Markers[End].Time - Markers[Start].Time;
		Span += Span <= 0.f ? Length : 0.f;
		return FMath::Fmod(Markers[Start].Time + Fraction * Span, Length);
	}
}

float Ginkgo::Animation::MatchMarkerPhase(const TConstArrayView<FAnimSyncMarker> FromMarkers, const float FromTime, const float FromLength,
	const TConstArrayView<FAnimSyncMarker> ToMarkers, const float ToLength)
{
	const auto NormalizedFallback = [&]() { return FromLength > 0.f ? FMath::Fmod(FromTime, FromLength) / FromLength * ToLength : 0.f; };
	if (FromMarkers.IsEmpty() || ToMarkers.IsEmpty() || FromLength <= 0.f || ToLength <= 0.f)
	{
		return NormalizedFallback();
	}
	// 原片段落在哪个标记之后、走了几成；新片段：第一个同名标记，到它的下一个标记同样走几成
	const FMarkerPhase Phase = GetMarkerPhase(FromMarkers, FromTime, FromLength);
	const FName MarkerName = FromMarkers[Phase.Previous].MarkerName;
	const int32 Start = ToMarkers.IndexOfByPredicate([MarkerName](const FAnimSyncMarker& Marker) { return Marker.MarkerName == MarkerName; });
	if (Start == INDEX_NONE)
	{
		return NormalizedFallback();
	}
	return GetSegmentTime(ToMarkers, Start, Phase.Fraction, ToLength);
}

bool Ginkgo::Animation::IsSameTimeline(const TConstArrayView<FAnimSyncMarker> AMarkers, const float ALength, const TConstArrayView<FAnimSyncMarker> BMarkers,
	const float BLength)
{
	// 标记的时刻导入时按帧算好，同一条时间线上逐个相同；留 0.1 ms 的余量给存盘的舍入
	constexpr float Tolerance = 1e-4f;
	if (FMath::Abs(ALength - BLength) > Tolerance || AMarkers.Num() != BMarkers.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < AMarkers.Num(); ++Index)
	{
		if (AMarkers[Index].MarkerName != BMarkers[Index].MarkerName || FMath::Abs(AMarkers[Index].Time - BMarkers[Index].Time) > Tolerance)
		{
			return false;
		}
	}
	return true;
}

void Ginkgo::Animation::FollowMarkers(FGinkgoMarkerFollow& Follow, const TConstArrayView<FAnimSyncMarker> InLeaderMarkers, const float LeaderTime,
	const float LeaderLength, const bool bLeaderChanged, const TConstArrayView<FAnimSyncMarker> InFollowerMarkers, const float FollowerLength, const float FallbackDelta)
{
	if (FollowerLength <= 0.f)
	{
		Follow = FGinkgoMarkerFollow();
		return;
	}
	// 按时刻排好；连着同名的只算第一个：接触曲线抖了一下又起来（GASP 的 Run_Loop_RR 在 1.479、1.57 s 连着两个 R），那只脚其实没离地，
	// 按两段算的话跟的片段在第二个标记上跳一步。循环的最后一个与第一个同名时，第一个接着上一圈的最后一段
	const auto Steps = [](const TConstArrayView<FAnimSyncMarker> Markers)
	{
		TArray<FAnimSyncMarker, TInlineAllocator<16>> Sorted(Markers.GetData(), Markers.Num());
		Algo::SortBy(Sorted, &FAnimSyncMarker::Time);
		TArray<FAnimSyncMarker, TInlineAllocator<16>> Result;
		for (const FAnimSyncMarker& Marker : Sorted)
		{
			if (Result.IsEmpty() || Result.Last().MarkerName != Marker.MarkerName)
			{
				Result.Add(Marker);
			}
		}
		if (Result.Num() > 1 && Result[0].MarkerName == Result.Last().MarkerName)
		{
			Result.RemoveAt(0);
		}
		return Result;
	};
	const TArray<FAnimSyncMarker, TInlineAllocator<16>> LeaderMarkers = Steps(InLeaderMarkers);
	const TArray<FAnimSyncMarker, TInlineAllocator<16>> FollowerMarkers = Steps(InFollowerMarkers);
	const bool bLeaderMarked = !LeaderMarkers.IsEmpty() && LeaderLength > 0.f;
	const FMarkerPhase Leader = bLeaderMarked ? GetMarkerPhase(LeaderMarkers, LeaderTime, LeaderLength) : FMarkerPhase();
	const int32 LastLeaderSegment = Follow.LeaderSegment;
	Follow.LeaderSegment = bLeaderMarked ? Leader.Previous : INDEX_NONE;
	const bool bSynced = Follow.bSynced;
	Follow.bSynced = true;

	// 同一条时间线：同一个时刻（都没有标记的只是时长碰巧一样，不算）
	if (bLeaderMarked && IsSameTimeline(LeaderMarkers, LeaderLength, FollowerMarkers, FollowerLength))
	{
		Follow.Time = FMath::Clamp(LeaderTime, 0.f, FollowerLength);
		return;
	}
	const auto Advance = [&Follow, FollowerLength, FallbackDelta]
	{
		const float Time = FMath::Fmod(Follow.Time + FallbackDelta, FollowerLength);
		Follow.Time = Time < 0.f ? Time + FollowerLength : Time;
	};
	if (!bLeaderMarked || FollowerMarkers.IsEmpty())
	{
		Advance();
		return;
	}

	// 跟的片段落在哪一段：刚用上时取第一个同名标记的；之后领头的还在同一段里就留在现在这一段（几成往回走也只在段里），
	// 领头的过了标记（同一个片段，落的段变了）就往后找第一个同名的。换了片段的从现在这一段找起：同名就留着
	const FName MarkerName = LeaderMarkers[Leader.Previous].MarkerName;
	int32 Segment = INDEX_NONE;
	if (!bSynced)
	{
		Segment = FollowerMarkers.IndexOfByPredicate([MarkerName](const FAnimSyncMarker& Marker) { return Marker.MarkerName == MarkerName; });
	}
	else
	{
		const int32 Current = GetMarkerPhase(FollowerMarkers, Follow.Time, FollowerLength).Previous;
		const bool bCrossed = !bLeaderChanged && LastLeaderSegment != INDEX_NONE && LastLeaderSegment != Leader.Previous;
		for (int32 Step = bCrossed ? 1 : 0; Step <= FollowerMarkers.Num() && Segment == INDEX_NONE; ++Step)
		{
			const int32 Index = (Current + Step) % FollowerMarkers.Num();
			Segment = FollowerMarkers[Index].MarkerName == MarkerName ? Index : INDEX_NONE;
		}
	}
	if (Segment == INDEX_NONE)
	{
		Advance();
		return;
	}
	Follow.Time = GetSegmentTime(FollowerMarkers, Segment, Leader.Fraction, FollowerLength);
}

FName Ginkgo::Animation::NextMarkerName(const TConstArrayView<FAnimSyncMarker> Markers, const float Time)
{
	const FAnimSyncMarker* First = nullptr;
	const FAnimSyncMarker* Next = nullptr;
	for (const FAnimSyncMarker& Marker : Markers)
	{
		First = !First || Marker.Time < First->Time ? &Marker : First;
		Next = Marker.Time > Time && (!Next || Marker.Time < Next->Time) ? &Marker : Next;
	}
	return Next ? Next->MarkerName : First ? First->MarkerName : NAME_None;
}

float Ginkgo::Animation::LastMarkerTime(const TConstArrayView<FAnimSyncMarker> Markers, const FName MarkerName)
{
	float Last = -1.f;
	for (const FAnimSyncMarker& Marker : Markers)
	{
		Last = Marker.MarkerName == MarkerName ? FMath::Max(Last, Marker.Time) : Last;
	}
	return Last;
}

TArray<TPair<FName, float>, TInlineAllocator<2>> Ginkgo::Animation::CrossFootMarkers(const TConstArrayView<FAnimSyncMarker> Markers, const float LastTime,
	const float Time, const float Length, FName& LastStep)
{
	TArray<TPair<FName, float>, TInlineAllocator<2>> Steps;
	float Advance = Time - LastTime;
	Advance += Advance < 0.f ? Length : 0.f;
	if (Length <= 0.f || Advance <= 0.f || Advance > 0.5f * Length)
	{
		return Steps;
	}
	// 标记离 LastTime 走了多少（绕回的补一圈），落在 (0, Advance] 的就是这一帧跨过的：正好在 LastTime 上的上一帧已经算过
	TArray<TPair<FName, float>, TInlineAllocator<2>> Crossed;
	for (const FAnimSyncMarker& Marker : Markers)
	{
		float Offset = Marker.Time - LastTime;
		Offset += Offset <= 0.f ? Length : 0.f;
		if (Offset <= Advance)
		{
			Crossed.Emplace(Marker.MarkerName, Offset / Advance);
		}
	}
	Algo::SortBy(Crossed, &TPair<FName, float>::Value);
	for (const TPair<FName, float>& Step : Crossed)
	{
		if (Step.Key != LastStep)
		{
			Steps.Add(Step);
			LastStep = Step.Key;
		}
	}
	return Steps;
}

double Ginkgo::Animation::StepLoopClock(const double Clock, const float DeltaSeconds, const bool bCut)
{
	return (bCut ? 0.0 : Clock) + DeltaSeconds;
}

float Ginkgo::Animation::LoopTimeAt(const double Clock, const float Length)
{
	if (Length <= 0.f)
	{
		return 0.f;
	}
	const double Time = FMath::Fmod(Clock, static_cast<double>(Length));
	return static_cast<float>(Time < 0.0 ? Time + Length : Time);
}

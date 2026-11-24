#include "Interaction/GinkgoInteractionMath.h"

void Ginkgo::Interaction::GetCapsuleSegment(const FVector& Center, const FVector& Up, const double HalfHeight, const double Radius, FVector& OutBottom, FVector& OutTop)
{
	// 半高不小于半径（胶囊退化成球时线段缩成一点）
	const double HalfSegment = FMath::Max(HalfHeight - Radius, 0.0);
	OutBottom = Center - Up * HalfSegment;
	OutTop = Center + Up * HalfSegment;
}

double Ginkgo::Interaction::DistanceToSegment(const FVector& Point, const FVector& SegmentStart, const FVector& SegmentEnd)
{
	return FMath::PointDistToSegment(Point, SegmentStart, SegmentEnd);
}

double Ginkgo::Interaction::InteractDistance(const FVector& Point, const FVector& SegmentBottom, const FVector& SegmentTop, const bool bFromCenter)
{
	return bFromCenter ? FVector::Dist(Point, (SegmentBottom + SegmentTop) * 0.5) : DistanceToSegment(Point, SegmentBottom, SegmentTop);
}

double Ginkgo::Interaction::ViewAngleDegrees(const FVector& ViewLocation, const FVector& ViewForward, const FVector& Target)
{
	const FVector Direction = (Target - ViewLocation).GetSafeNormal();
	if (Direction.IsZero())
	{
		return 0.0;
	}
	const double Cosine = FMath::Clamp(FVector::DotProduct(ViewForward.GetSafeNormal(), Direction), -1.0, 1.0);
	return FMath::RadiansToDegrees(FMath::Acos(Cosine));
}

bool Ginkgo::Interaction::IsWithinRange(const double Distance, const double Radius, const bool bWasWithin, const double Hysteresis)
{
	return Distance <= Radius + (bWasWithin ? Hysteresis : 0.0);
}

int32 Ginkgo::Interaction::SelectFocus(const TConstArrayView<FFocusCandidate> Candidates, const double MaxAngle, const double SwitchMargin)
{
	int32 Best = INDEX_NONE;
	int32 Previous = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FFocusCandidate& Candidate = Candidates[Index];
		if (!Candidate.bEligible || Candidate.ViewAngle > MaxAngle)
		{
			continue;
		}
		if (Best == INDEX_NONE || Candidate.ViewAngle < Candidates[Best].ViewAngle)
		{
			Best = Index;
		}
		if (Candidate.bWasFocus)
		{
			Previous = Index;
		}
	}
	// 上一帧的焦点还合格：新的要明显更靠近准星才换
	if (Previous != INDEX_NONE && Best != Previous && Candidates[Best].ViewAngle > Candidates[Previous].ViewAngle - SwitchMargin)
	{
		return Previous;
	}
	return Best;
}

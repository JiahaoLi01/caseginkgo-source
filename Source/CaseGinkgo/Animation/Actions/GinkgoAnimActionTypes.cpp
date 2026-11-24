#include "Animation/Actions/GinkgoAnimActionTypes.h"

FString FGinkgoAnimActionResult::ToString() const
{
	const FString Name = StaticEnum<EGinkgoAnimActionOutcome>()->GetNameStringByValue(static_cast<int64>(Outcome));
	return Reason.IsValid() ? FString::Printf(TEXT("%s (%s)"), *Name, *Reason.ToString()) : Name;
}

bool Ginkgo::Occupancy::Blocks(const FGinkgoOccupancy& Claim, const FGinkgoOccupancy& Request)
{
	if (Request.Tag.MatchesTag(Claim.Tag))
	{
		return Request.Priority <= Claim.Priority;
	}
	if (Claim.Tag.MatchesTag(Request.Tag))
	{
		return Claim.Priority > Request.Priority;
	}
	return false;
}

bool Ginkgo::Occupancy::Overlaps(const FGinkgoOccupancy& A, const FGinkgoOccupancy& B)
{
	return A.Tag.MatchesTag(B.Tag) || B.Tag.MatchesTag(A.Tag);
}

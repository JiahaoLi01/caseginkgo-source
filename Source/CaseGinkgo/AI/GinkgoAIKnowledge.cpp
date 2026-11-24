#include "AI/GinkgoAIKnowledge.h"

void FGinkgoAIKnowledge::Observe(const FVector& Location, const FVector& AimPoint, const double Now)
{
	if (!bVisible)
	{
		AcquiredTime = Now;
	}
	LastLocation = Location;
	LastAimPoint = AimPoint;
	LastSeenTime = Now;
	bHasMemory = true;
	bVisible = true;
	bPositionCleared = false;
	ClearElapsed = 0.f;
	ForgetHearing();
}

bool FGinkgoAIKnowledge::Hear(const FVector& Location, const double Now)
{
	if (bVisible || (bHasMemory && Now <= LastSeenTime) || (bHasHearing && Now <= LastHeardTime)) return false;
	LastHeardLocation = Location;
	LastHeardTime = Now;
	bHasHearing = true;
	bHearingInvestigated = false;
	return true;
}

void FGinkgoAIKnowledge::ForgetSight()
{
	LastLocation = FVector::ZeroVector;
	LastAimPoint = FVector::ZeroVector;
	LastSeenTime = 0.0;
	AcquiredTime = 0.0;
	ClearElapsed = 0.f;
	bHasMemory = false;
	bVisible = false;
	bPositionCleared = false;
}

void FGinkgoAIKnowledge::ForgetHearing()
{
	LastHeardLocation = FVector::ZeroVector;
	LastHeardTime = 0.0;
	bHasHearing = false;
	bHearingInvestigated = false;
}

void FGinkgoAIKnowledge::LoseSight()
{
	bVisible = false;
}

void FGinkgoAIKnowledge::ConfirmEmpty(const bool bPointVisible, const float DeltaSeconds, const float RequiredSeconds)
{
	if (!bHasMemory || bVisible || bPositionCleared)
	{
		return;
	}
	ClearElapsed = bPointVisible ? ClearElapsed + FMath::Max(0.f, DeltaSeconds) : 0.f;
	bPositionCleared = bPointVisible && ClearElapsed >= RequiredSeconds;
}

bool FGinkgoAIKnowledge::IsExpired(const double Now, const float Lifetime) const
{
	return bHasMemory && !bVisible && Now - LastSeenTime >= Lifetime;
}

EGinkgoAIBehavior FGinkgoAIKnowledge::GetBehavior() const
{
	if (bVisible) return EGinkgoAIBehavior::Engage;
	if (bHasHearing) return bHearingInvestigated ? EGinkgoAIBehavior::Search : EGinkgoAIBehavior::Investigate;
	if (!bHasMemory) return EGinkgoAIBehavior::Patrol;
	return bPositionCleared ? EGinkgoAIBehavior::Search : EGinkgoAIBehavior::Pursue;
}

bool FGinkgoAIKnowledge::IsHearingExpired(const double Now, const float Lifetime) const
{
	return bHasHearing && Now - LastHeardTime >= Lifetime;
}

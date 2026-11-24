#include "Animation/GinkgoPeek.h"

#include "Combat/GinkgoCombatTypes.h"
#include "Engine/World.h"

void FGinkgoPeekState::Reset()
{
	Side = 0;
	ReleaseTime = 0.0;
	Blend.Reset(0.0);
}

void FGinkgoPeekState::Step(const bool bValid, const double ErrorDegrees, const double DeltaSeconds, const FGinkgoPeekSettings& Settings, const bool bCanEnter)
{
	const double Enter = FMath::Max(0.01, static_cast<double>(Settings.EnterAngle));
	const double Exit = FMath::Clamp(static_cast<double>(Settings.ExitAngle), 0.0, Enter);
	const bool bUsable = Settings.bEnabled && bValid && FMath::IsFinite(ErrorDegrees);
	if (!bUsable)
	{
		Side = 0;
		ReleaseTime = 0.0;
	}
	else if (FMath::Abs(ErrorDegrees) >= Enter && (bCanEnter || Side * ErrorDegrees < 0.0))
	{
		Side = ErrorDegrees > 0.0 ? -1 : 1;
		ReleaseTime = 0.0;
	}
	else if (Side != 0)
	{
		// 同侧仍超过退出阈值就保持；越过中心但未达到反向进入阈值时，也允许自然退出。
		ReleaseTime = Side * ErrorDegrees >= -Exit ? ReleaseTime + FMath::Max(0.0, DeltaSeconds) : 0.0;
		if (ReleaseTime >= Settings.ReleaseDelay && Side * ErrorDegrees >= -Exit)
		{
			Side = 0;
		}
	}
	const double Magnitude = bUsable ? FMath::Clamp(FMath::Abs(ErrorDegrees) / FMath::Max(Enter, static_cast<double>(Settings.FullAngle)), 0.2, 1.0) : 0.0;
	Blend.Step(Side * Magnitude, Settings.BlendOmega, DeltaSeconds);
	Blend.Value = FMath::Clamp(Blend.Value, -1.0, 1.0);
}

TConstArrayView<FName> Ginkgo::Animation::PeekBoneNames()
{
	static const FName Names[] = { TEXT("pelvis"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05"), TEXT("VB spine_05_ik_hand_gun") };
	return Names;
}

bool Ginkgo::Animation::PeekError(const FVector& Eye, const FQuat& View, const FVector& Hit, double& OutDegrees)
{
	const FVector Local = View.UnrotateVector(Hit - Eye);
	OutDegrees = 0.0;
	if (Local.ContainsNaN() || Local.X <= 1.0)
	{
		return false;
	}
	OutDegrees = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	return true;
}

bool Ginkgo::Animation::TracePeek(const UWorld& World, const AActor& Owner, const FVector& Eye, const FQuat& View,
	const FTransform& Gun, const FVector& MuzzleOffset, const bool bAds, const double Distance, FVector& OutPoint, double& OutDegrees, FHitResult* OutHit)
{
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoPeek), false, &Owner);
	const FVector Muzzle = Gun.TransformPosition(MuzzleOffset);
	FVector End = Muzzle + Gun.GetUnitAxis(EAxis::X) * Distance;
	if (bAds)
	{
		End = Eye + View.GetForwardVector() * Distance;
		FHitResult Aimed;
		if (World.LineTraceSingleByChannel(Aimed, Eye, End, Ginkgo::Combat::WeaponTraceChannel, Params))
		{
			End = Aimed.ImpactPoint;
		}
	}
	FHitResult Hit;
	OutPoint = World.LineTraceSingleByChannel(Hit, Muzzle, End, Ginkgo::Combat::WeaponTraceChannel, Params) ? FVector(Hit.ImpactPoint) : End;
	if (OutHit) { *OutHit = Hit; }
	return PeekError(Eye, View, OutPoint, OutDegrees);
}

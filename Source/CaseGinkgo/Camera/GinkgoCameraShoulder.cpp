#include "GinkgoCameraShoulder.h"

#include "Engine/World.h"
#include "GinkgoCameraParams.h"
#include "GinkgoSpring.h"

bool FGinkgoCameraShoulderRay::IsValid() const
{
	return FMath::IsFinite(Angle) && Angle >= -180.f && Angle <= 180.f
		&& FMath::IsFinite(Length) && Length >= 1.f && Length <= 10000.f
		&& FMath::IsFinite(Score) && Score >= 0.f && Score <= 1000.f;
}

void FGinkgoCameraShoulder::Toggle(const double Time, const FGinkgoCameraShoulderSettings& Settings)
{
	bRightShoulder = !bRightShoulder;
	CooldownUntil = Time + FMath::Max(0.f, Settings.Cooldown);
}

void FGinkgoCameraShoulder::Apply(FGinkgoCameraParams& Params, const float DeltaTime, const FGinkgoCameraShoulderSettings& Settings)
{
	const double Target = bRightShoulder ? 1.0 : -1.0;
	double Offset = Blend - Target;
	Ginkgo::Spring::StepCritical(Offset, BlendVelocity, Settings.BlendOmega, DeltaTime);
	Blend = Target + Offset;
	Params.ShoulderOffsetRight *= Blend;
	Params.PivotOffsetRight *= Blend;
}

bool FGinkgoCameraShoulder::UpdateAuto(const UWorld& World, const AActor& Self, TFunctionRef<FVector(bool)> GetOrigin, const FRotator& CameraRotation,
	const bool bAds, const bool bEnabled, const double Time, const FGinkgoCameraShoulderSettings& Settings, const bool bRestoreOnExit)
{
	// 会话转换先于冷却判断：即使退出发生在冷却中，也要立即复原目标肩位并释放下一次 ADS 的检测。
	bRestoreAdsEntryShoulder = bEnabled && bRestoreOnExit;
	if (!bAds)
	{
		EndAds();
		return false;
	}
	if (!AdsEntryShoulder.IsSet())
	{
		AdsEntryShoulder = bRightShoulder;
	}
	if (!bEnabled)
	{
		ResetDetection();
		return false;
	}
	if (Time < CooldownUntil || Time < NextScanTime)
	{
		return false;
	}
	NextScanTime = Time + FMath::Max(0.01f, Settings.ScanInterval);
	Scores = FVector2D::ZeroVector;
#if !UE_BUILD_SHIPPING
	DebugSample.Rays.Reset();
	DebugSample.Rotation = CameraRotation;
	DebugSample.Time = Time;
	DebugSample.bOnRight = bRightShoulder;
#endif
	const auto Definitions = MakeArrayView(Settings.Rays).Left(FGinkgoCameraShoulderSettings::MaxRays);
	if (!Definitions.ContainsByPredicate([](const FGinkgoCameraShoulderRay& Ray) { return Ray.IsValid(); }))
	{
		return false;
	}
	const FVector LeftOrigin = GetOrigin(false);
	const FVector RightOrigin = GetOrigin(true);
#if !UE_BUILD_SHIPPING
	DebugSample.LeftOrigin = LeftOrigin;
	DebugSample.RightOrigin = RightOrigin;
#endif
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(GinkgoAutoShoulder), false, &Self);
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FGinkgoCameraShoulderRay& Definition = Definitions[Index];
		if (!Definition.IsValid())
		{
			continue;
		}
		// 两肩的同一条配置始终平行，只改变起点；负角、正角和正前方都计入发出射线的肩位。
		const FVector TraceOffset = FRotator(CameraRotation.Pitch, CameraRotation.Yaw + Definition.Angle, 0.0).Vector() * Definition.Length;
		for (const bool bRightOrigin : {false, true})
		{
			const FVector& Origin = bRightOrigin ? RightOrigin : LeftOrigin;
			const FVector End = Origin + TraceOffset;
#if !UE_BUILD_SHIPPING
			FHitResult Hit;
			const bool bBlocked = World.LineTraceSingleByChannel(Hit, Origin, End, Settings.TraceChannel, Query);
			FDebugRay& Ray = DebugSample.Rays.AddDefaulted_GetRef();
			Ray.End = End;
			Ray.HitPoint = bBlocked ? Hit.ImpactPoint : End;
			Ray.YawOffset = Definition.Angle;
			Ray.Index = Index;
			Ray.Score = bBlocked ? Definition.Score : 0.f;
			Ray.bRightOrigin = bRightOrigin;
			Ray.bBlocked = bBlocked;
#else
			const bool bBlocked = World.LineTraceTestByChannel(Origin, End, Settings.TraceChannel, Query);
#endif
			if (bBlocked) { (bRightOrigin ? Scores.Y : Scores.X) += Definition.Score; }
		}
	}
	const double Difference = bRightShoulder ? Scores.Y - Scores.X : Scores.X - Scores.Y;
	if (Difference > FMath::Max(0.f, Settings.ScoreTolerance))
	{
		Toggle(Time, Settings);
	}
	return true;
}

void FGinkgoCameraShoulder::EndAds()
{
	if (bRestoreAdsEntryShoulder && AdsEntryShoulder.IsSet())
	{
		// 只改目标，下一帧继续走同一个弹簧；退出开镜时不把相机瞬移回旧的世界位置。
		bRightShoulder = AdsEntryShoulder.GetValue();
	}
	AdsEntryShoulder.Reset();
	bRestoreAdsEntryShoulder = false;
	ResetDetection();
}

void FGinkgoCameraShoulder::ResetDetection()
{
	NextScanTime = 0.0;
	CooldownUntil = 0.0;
	Scores = FVector2D::ZeroVector;
#if !UE_BUILD_SHIPPING
	DebugSample = FDebugSample();
#endif
}

void FGinkgoCameraShoulder::Snap()
{
	EndAds();
	Blend = bRightShoulder ? 1.0 : -1.0;
	BlendVelocity = 0.0;
}

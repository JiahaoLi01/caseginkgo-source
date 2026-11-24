#include "GinkgoCameraFollow.h"

#include "GinkgoCameraConfig.h"

FVector FGinkgoCameraFollow::Step(const FVector& Target, const FVector& Velocity, const double DeltaTime, const FGinkgoFollowSettings& Settings)
{
	bClamped = false;
	if (!bActive)
	{
		bActive = true;
		Horizontal.Reset(FVector2D(Target), FVector2D(Velocity));
		Vertical.Reset(Target.Z, Velocity.Z);
		FilteredVelocity.Reset(Velocity);
		LastTarget = Target;
		LastOffset = ComputeFeedforwardOffset(Velocity, Settings);
		return Target;
	}
	if (DeltaTime <= 0.0)
	{
		return GetValue();
	}

	// 移动组件的速度在这一帧的开头就变成了现在的值：低通的输入整帧不变（Step），前馈的偏移也整帧不变；
	// 过了低通的速度在帧里连续变化，偏移从上一帧的匀速变过去
	const bool bFiltered = Settings.FeedforwardFilterOmega > 0.f;
	if (bFiltered)
	{
		FilteredVelocity.Step(Velocity, Settings.FeedforwardFilterOmega, DeltaTime);
	}
	else
	{
		FilteredVelocity.Reset(Velocity);
	}
	const FVector Offset = ComputeFeedforwardOffset(FilteredVelocity.Value, Settings);
	const FVector OffsetFrom = bFiltered ? LastOffset : Offset;
	Horizontal.StepMoving(FVector2D(LastTarget + OffsetFrom), FVector2D(Target + Offset), Settings.Omega, DeltaTime);
	Vertical.StepMoving(LastTarget.Z + OffsetFrom.Z, Target.Z + Offset.Z, Settings.VerticalOmega, DeltaTime);
	LastTarget = Target;
	LastOffset = Offset;

	// 最大滞后量的是离胶囊中心多远（角色在画面里偏了多少），不是离前馈的目标多远
	const FVector2D HorizontalLag = Horizontal.Value - FVector2D(Target);
	if (Settings.MaxLag > 0.f && HorizontalLag.SizeSquared() > FMath::Square(static_cast<double>(Settings.MaxLag)))
	{
		Horizontal.Value = FVector2D(Target) + HorizontalLag * (Settings.MaxLag / HorizontalLag.Size());
		bClamped = true;
	}
	const double VerticalLag = Vertical.Value - Target.Z;
	if (Settings.VerticalMaxLag > 0.f && FMath::Abs(VerticalLag) > Settings.VerticalMaxLag)
	{
		Vertical.Value = Target.Z + FMath::Sign(VerticalLag) * Settings.VerticalMaxLag;
		bClamped = true;
	}
	return GetValue();
}

FVector FGinkgoCameraFollow::ComputeFeedforwardOffset(const FVector& Velocity, const FGinkgoFollowSettings& Settings)
{
	const double HorizontalScale = Settings.Omega > 0.f ? 2.0 * Settings.Feedforward / Settings.Omega : 0.0;
	const double VerticalScale = Settings.VerticalOmega > 0.f ? 2.0 * Settings.VerticalFeedforward / Settings.VerticalOmega : 0.0;
	return FVector(Velocity.X * HorizontalScale, Velocity.Y * HorizontalScale, Velocity.Z * VerticalScale);
}

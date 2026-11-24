#include "GinkgoCameraMath.h"

#include "GinkgoCameraConfig.h"

FVector Ginkgo::Camera::ComputePivot(const FVector& CapsuleCenter, const FRotator& ViewRotation, const FGinkgoCameraParams& Params)
{
	const FRotator Heading(0.0, ViewRotation.Yaw, 0.0);
	return CapsuleCenter + Heading.RotateVector(FVector(Params.PivotOffsetForward, Params.PivotOffsetRight, Params.PivotOffsetUp));
}

double Ginkgo::Camera::ComputeOrbitArmLength(const double ArmLength, const double Pitch, const FGinkgoCameraOrbitSettings& Orbit)
{
	if (!Orbit.bEnabled)
	{
		return ArmLength;
	}
	const double Angle = FRotator::NormalizeAxis(Pitch);
	const bool bUpper = Angle < 0.0;
	// 半径是水平投影，不是沿视线的距离；除以 cos 才能在指定俯仰角精确通过外环。
	// 运行时也夹紧：控制台和本地调参文件不一定经过属性面板的 ClampMin / ClampMax。
	const double RingPitch = FMath::Clamp(static_cast<double>(bUpper ? Orbit.UpperPitch : Orbit.LowerPitch), 5.0, 80.0);
	const double RadiusScale = FMath::Clamp(static_cast<double>(bUpper ? Orbit.UpperRadiusScale : Orbit.LowerRadiusScale), 0.05, 1.0);
	const double RingDistanceScale = RadiusScale / FMath::Cos(FMath::DegreesToRadians(RingPitch));
	const double T = FMath::Clamp(FMath::Abs(Angle) / RingPitch, 0.0, 1.0);
	const double Weight = T * T * (3.0 - 2.0 * T);
	return ArmLength * FMath::Lerp(1.0, RingDistanceScale, Weight);
}

FVector Ginkgo::Camera::ComputeCameraLocation(const FVector& Pivot, const FRotator& ViewRotation, const FGinkgoCameraParams& Params,
	const FGinkgoCameraOrbitSettings* Orbit)
{
	const double Distance = Orbit ? ComputeOrbitArmLength(Params.ArmLength, ViewRotation.Pitch, *Orbit) : Params.ArmLength;
	return Pivot + ViewRotation.RotateVector(FVector(-Distance, Params.ShoulderOffsetRight, Params.ShoulderOffsetUp));
}

FVector Ginkgo::Camera::ApplyCompression(const FVector& Pivot, const FVector& Boom, const double Compression)
{
	const double Length = Boom.Size();
	if (Length <= UE_KINDA_SMALL_NUMBER)
	{
		return Pivot;
	}
	return Pivot + Boom * (FMath::Max(Length - Compression, 0.0) / Length);
}

double Ginkgo::Camera::BlendCurveValue(const double Progress, const EGinkgoCameraBlendCurve Curve, const float Strength)
{
	const double P = FMath::Clamp(Progress, 0.0, 1.0);
	const double K = FMath::Max(static_cast<double>(Strength), 1.0);
	switch (Curve)
	{
	case EGinkgoCameraBlendCurve::Linear:
		break;
	case EGinkgoCameraBlendCurve::Smooth:
		{
			const double Low = FMath::Pow(P, K);
			const double High = FMath::Pow(1.0 - P, K);
			return Low / (Low + High);
		}
	case EGinkgoCameraBlendCurve::EaseOut:
		return 1.0 - FMath::Pow(1.0 - P, K);
	case EGinkgoCameraBlendCurve::EaseIn:
		return FMath::Pow(P, K);
	}
	return P;
}

double Ginkgo::Camera::BlendCurveProgress(const double Value, const EGinkgoCameraBlendCurve Curve, const float Strength)
{
	const double Y = FMath::Clamp(Value, 0.0, 1.0);
	const double Exponent = 1.0 / FMath::Max(static_cast<double>(Strength), 1.0);
	switch (Curve)
	{
	case EGinkgoCameraBlendCurve::Linear:
		break;
	case EGinkgoCameraBlendCurve::Smooth:
		{
			const double Low = FMath::Pow(Y, Exponent);
			const double High = FMath::Pow(1.0 - Y, Exponent);
			return Low / (Low + High);
		}
	case EGinkgoCameraBlendCurve::EaseOut:
		return 1.0 - FMath::Pow(1.0 - Y, Exponent);
	case EGinkgoCameraBlendCurve::EaseIn:
		return FMath::Pow(Y, Exponent);
	}
	return Y;
}

float Ginkgo::Camera::StepCurvedWeight(const float Weight, const bool bTarget, const double Time, const float BlendInTime, const float BlendOutTime,
	const EGinkgoCameraBlendCurve InCurve, const EGinkgoCameraBlendCurve OutCurve, const float Strength)
{
	if (Time <= 0.0)
	{
		return Weight;
	}
	if (bTarget)
	{
		return BlendInTime <= 0.f ? 1.f : static_cast<float>(BlendCurveValue(BlendCurveProgress(Weight, InCurve, Strength) + Time / BlendInTime, InCurve, Strength));
	}
	return BlendOutTime <= 0.f ? 0.f : static_cast<float>(1.0 - BlendCurveValue(BlendCurveProgress(1.0 - Weight, OutCurve, Strength) + Time / BlendOutTime, OutCurve, Strength));
}

float Ginkgo::Camera::ZoomedFieldOfView(const float BaseFieldOfView, const float Zoom)
{
	if (Zoom <= 1.f)
	{
		return BaseFieldOfView;
	}
	const double HalfTan = FMath::Tan(FMath::DegreesToRadians(0.5 * BaseFieldOfView)) / Zoom;
	return static_cast<float>(2.0 * FMath::RadiansToDegrees(FMath::Atan(HalfTan)));
}

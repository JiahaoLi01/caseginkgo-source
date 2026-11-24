#include "GinkgoInputMath.h"

#include "Weapon/GinkgoScope.h"  // 倍镜的灵敏度：Ginkgo::Scope::ScopeLookScale

namespace
{
	/** 权重 clamp((t − Delay) / Ramp, 0, 1) 从 0 积到 t 的积分 */
	double RampIntegral(const double Time, const double Delay, const double Ramp)
	{
		const double After = Time - Delay;
		if (After <= 0.0)
		{
			return 0.0;
		}
		if (After < Ramp)
		{
			return After * After / (2.0 * Ramp);
		}
		return After - 0.5 * Ramp;
	}
}

FVector2D Ginkgo::Input::ApplyRadialDeadZone(const FVector2D& Stick, const float Inner, const float Outer)
{
	// 大小按 float 算，同引擎的 UInputModifierDeadZone（Radial）：默认值下与原来逐位相同
	const float Size = static_cast<float>(Stick.Size());
	if (Size <= Inner || Size < UE_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	return Stick.GetSafeNormal() * FMath::Min(1.f, (Size - Inner) / FMath::Max(Outer - Inner, UE_SMALL_NUMBER));
}

float Ginkgo::Input::ApplyResponseCurve(const float Deflection, const EGinkgoResponseCurve Curve, const float Strength)
{
	const float X = FMath::Clamp(Deflection, 0.f, 1.f);
	switch (Curve)
	{
	case EGinkgoResponseCurve::Linear:
		break;
	case EGinkgoResponseCurve::Power:
		return FMath::Pow(X, Strength);
	case EGinkgoResponseCurve::SCurve:
		{
			const float Low = FMath::Pow(X, Strength);
			const float High = FMath::Pow(1.f - X, Strength);
			return Low + High > 0.f ? Low / (Low + High) : X;
		}
	}
	return X;
}

float Ginkgo::Input::FGinkgoTurnRamp::Step(const bool bAtFull, const float DeltaSeconds, const float Delay, const float Ramp)
{
	if (!bAtFull || DeltaSeconds <= 0.f)
	{
		HeldTime = 0.f;
		return 0.f;
	}
	const double Start = HeldTime;
	const double End = Start + DeltaSeconds;
	HeldTime = static_cast<float>(End);
	return static_cast<float>((RampIntegral(End, Delay, Ramp) - RampIntegral(Start, Delay, Ramp)) / DeltaSeconds);
}

float Ginkgo::Input::AdsLookScale(const float AdsAlpha, const float AdsScale, const bool bMatchFov, const float AdsZoom)
{
	const float Target = bMatchFov && AdsZoom > 1.f ? AdsScale / AdsZoom : AdsScale;
	return FMath::Lerp(1.f, Target, FMath::Clamp(AdsAlpha, 0.f, 1.f));
}

float Ginkgo::Input::MouseAccelerationGain(const float CountsPerMs, const float Strength, const float Offset, const float Cap)
{
	return FMath::Min(1.f + Strength * FMath::Max(CountsPerMs - Offset, 0.f), FMath::Max(Cap, 1.f));
}

FVector2D Ginkgo::Input::MouseLookDegrees(const FVector2D& Counts, const FGinkgoInputSettings& Settings, const float AdsAlpha, const float AdsZoom, const float DeltaSeconds,
	const float ScopeMagnification)
{
	float Gain = 1.f;
	if (Settings.bMouseAcceleration && DeltaSeconds > 0.f)
	{
		const float CountsPerMs = static_cast<float>(Counts.Size()) / (DeltaSeconds * 1000.f);
		Gain = MouseAccelerationGain(CountsPerMs, Settings.MouseAccelStrength, Settings.MouseAccelOffset, Settings.MouseAccelCap);
	}
	// 倍镜（第一人称开镜）：镜外不放大，只有镜内放大倍率那么多，所以按倍镜灵敏度 ÷ 倍率；越肩开镜照那两项
	const float Ads = ScopeMagnification > 0.f ? Ginkgo::Scope::ScopeLookScale(AdsAlpha, Settings.MouseScopeScale, ScopeMagnification)
		: AdsLookScale(AdsAlpha, Settings.MouseAdsScale, Settings.bMouseAdsMatchFov, AdsZoom);
	const float Degrees = Settings.MouseSensitivity * MouseDegreesPerCount * Gain * Ads;
	return FVector2D(Counts.X * Degrees * (Settings.bMouseInvertYaw ? -1.f : 1.f), Counts.Y * Degrees * Settings.MousePitchRatio * (Settings.bMouseInvertPitch ? -1.f : 1.f));
}

FVector2D Ginkgo::Input::StickLookDegrees(const FVector2D& Stick, const FGinkgoInputSettings& Settings, const float AdsAlpha, const float AdsZoom, FGinkgoTurnRamp& Ramp, const float DeltaSeconds,
	const float ScopeMagnification)
{
	const FVector2D Live = ApplyRadialDeadZone(Stick, Settings.GamepadLookInner, Settings.GamepadLookOuter);
	const float Deflection = static_cast<float>(Live.Size());
	const FVector2D Shaped = Settings.GamepadCurve == EGinkgoResponseCurve::Linear || Deflection <= 0.f
		? Live : Live / Deflection * ApplyResponseCurve(Deflection, Settings.GamepadCurve, Settings.GamepadCurveStrength);
	// 转身加速只在不开镜时：开镜要的是细；开镜进度过渡时跟着淡出
	const float Boost = Ramp.Step(Deflection >= FullDeflection, DeltaSeconds, Settings.GamepadExtraDelay, Settings.GamepadExtraRamp) * (1.f - FMath::Clamp(AdsAlpha, 0.f, 1.f));
	const float Ads = ScopeMagnification > 0.f ? Ginkgo::Scope::ScopeLookScale(AdsAlpha, Settings.GamepadScopeScale, ScopeMagnification)
		: AdsLookScale(AdsAlpha, Settings.GamepadAdsScale, Settings.bGamepadAdsMatchFov, AdsZoom);
	const float Yaw = (Settings.GamepadYawSpeed + Settings.GamepadExtraYaw * Boost) * Ads * DeltaSeconds * (Settings.bGamepadInvertYaw ? -1.f : 1.f);
	const float Pitch = (Settings.GamepadPitchSpeed + Settings.GamepadExtraPitch * Boost) * Ads * DeltaSeconds * (Settings.bGamepadInvertPitch ? -1.f : 1.f);
	return FVector2D(Shaped.X * Yaw, Shaped.Y * Pitch);
}

FVector2D Ginkgo::Input::StickMoveInput(const FVector2D& Stick, const FGinkgoInputSettings& Settings)
{
	return ApplyRadialDeadZone(Stick, Settings.GamepadMoveInner, Settings.GamepadMoveOuter);
}

FVector2D Ginkgo::Input::WheelMouseDegrees(const FVector2D& Counts, const FGinkgoInputSettings& Settings)
{
	const float Degrees = Settings.MouseSensitivity * MouseDegreesPerCount;
	return FVector2D(Counts.X * Degrees, Counts.Y * Degrees * Settings.MousePitchRatio);
}

FVector2D Ginkgo::Input::WheelStickInput(const FVector2D& Stick, const FGinkgoInputSettings& Settings)
{
	return ApplyRadialDeadZone(Stick, Settings.GamepadLookInner, Settings.GamepadLookOuter);
}

// ===== 按住与切换 =====

void FGinkgoHoldActions::Press(const EGinkgoHoldAction Action, const EGinkgoInputDirectory Device, const EGinkgoKeySlot Slot, const EGinkgoHoldMode Mode)
{
	const int32 Index = static_cast<int32>(Action);
	if (Mode == EGinkgoHoldMode::Toggle)
	{
		bToggled[Index] = !bToggled[Index];
	}
	else
	{
		bHeld[Index][static_cast<int32>(Device)][static_cast<int32>(Slot)] = true;
	}
	if (Action == EGinkgoHoldAction::Sprint)
	{
		bToggled[static_cast<int32>(EGinkgoHoldAction::Aim)] = false;
		bToggled[static_cast<int32>(EGinkgoHoldAction::Crouch)] = false;
		// 站着按下时给 SprintStopGrace 的时间起步
		NoMoveTime = 0.f;
	}
	else if (Action == EGinkgoHoldAction::Aim)
	{
		bToggled[static_cast<int32>(EGinkgoHoldAction::Sprint)] = false;
	}
}

void FGinkgoHoldActions::Release(const EGinkgoHoldAction Action, const EGinkgoInputDirectory Device, const EGinkgoKeySlot Slot)
{
	bHeld[static_cast<int32>(Action)][static_cast<int32>(Device)][static_cast<int32>(Slot)] = false;
}

void FGinkgoHoldActions::OnJump()
{
	bToggled[static_cast<int32>(EGinkgoHoldAction::Crouch)] = false;
}

void FGinkgoHoldActions::OnFire()
{
	bToggled[static_cast<int32>(EGinkgoHoldAction::Sprint)] = false;
}

void FGinkgoHoldActions::Tick(const bool bHasMoveInput, const float DeltaSeconds)
{
	NoMoveTime = bHasMoveInput ? 0.f : NoMoveTime + DeltaSeconds;
	if (NoMoveTime >= SprintStopGrace)
	{
		bToggled[static_cast<int32>(EGinkgoHoldAction::Sprint)] = false;
	}
}

void FGinkgoHoldActions::Reset()
{
	*this = FGinkgoHoldActions();
}

void FGinkgoHoldActions::ReleaseAllHeld()
{
	FMemory::Memzero(bHeld);
}

bool FGinkgoHoldActions::IsOn(const EGinkgoHoldAction Action) const
{
	const int32 Index = static_cast<int32>(Action);
	if (bToggled[Index])
	{
		return true;
	}
	for (const auto& Device : bHeld[Index])
	{
		for (const bool bSlotHeld : Device)
		{
			if (bSlotHeld)
			{
				return true;
			}
		}
	}
	return false;
}

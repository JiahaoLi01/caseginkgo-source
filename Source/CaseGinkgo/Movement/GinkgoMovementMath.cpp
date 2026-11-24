#include "GinkgoMovementMath.h"

void Ginkgo::Movement::Accelerate(FVector& V, const FVector& WishDir, const float SpeedCap, const float AccelStep)
{
	const double Add = SpeedCap - FVector::DotProduct(V, WishDir);
	if (Add > 0.0)
	{
		V += WishDir * FMath::Min<double>(AccelStep, Add);
	}
}

void Ginkgo::Movement::AccelerateOnGround(FVector& V, const FVector& WishDir, const float WishSpeed, const float AccelStep)
{
	const double SpeedLimit = FMath::Max<double>(V.Size(), WishSpeed);
	Accelerate(V, WishDir, WishSpeed, AccelStep);
	V = V.GetClampedToMaxSize(SpeedLimit);
}

void Ginkgo::Movement::ApplyFriction(FVector& V, const float Friction, const float StopSpeed, const float DeltaTime)
{
	const double Speed = V.Size();
	if (Speed <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}
	const double Drop = FMath::Max<double>(Speed, StopSpeed) * Friction * DeltaTime;
	V *= FMath::Max(Speed - Drop, 0.0) / Speed;
}

float Ginkgo::Movement::JumpVelocityForHeight(const float Height, const float Gravity)
{
	return FMath::Sqrt(2.f * FMath::Max(Gravity, 0.f) * FMath::Max(Height, 0.f));
}

// ===== 滑铲 =====

float Ginkgo::Movement::ApplySlideBoost(FVector& V, const float Boost, const float BoostCap)
{
	const double Speed = V.Size2D();
	const double Target = FMath::Min<double>(Speed + Boost, BoostCap);
	if (Speed <= UE_KINDA_SMALL_NUMBER || Target <= Speed)
	{
		return 0.f;
	}
	const double Scale = Target / Speed;
	V.X *= Scale;
	V.Y *= Scale;
	return static_cast<float>(Target - Speed);
}

float Ginkgo::Movement::SlideDecelerate(const float Speed, const float Decel, const float Decay, const float DeltaTime)
{
	if (Speed <= 0.f)
	{
		return 0.f;
	}
	// 用 double：λ 小时 (v + D/λ) e^(−λt) 与 D/λ 是两个大数相减，float 会丢精度
	double NewSpeed;
	if (Decay > UE_KINDA_SMALL_NUMBER)
	{
		// dv/dt = −D − λv 的解析解。令 u = v + D/λ，则 du/dt = −D − λv = −λu，u 按指数衰减：u(t) = u0 · e^(−λt)，
		// 换回 v 得 v(t) = (v0 + D/λ) e^(−λt) − D/λ。
		// 方程的平衡点是 v = −D/λ，速度朝这个负值按指数逼近，所以会在有限时间内穿过 0（t = ln(1 + λv0 / D) / λ），下面夹到 0。
		// 与帧率无关：记一帧的更新为 f_t(v) = (v + D/λ) e^(−λt) − D/λ，则 f_a(f_b(v)) = (v + D/λ) e^(−λ(a + b)) − D/λ = f_(a + b)(v)，
		// 分几帧推进结果都一样；逐帧的 v −= (D + λv)Δt 只是它的一阶近似，误差随帧长变
		const double Offset = static_cast<double>(Decel) / Decay;
		NewSpeed = (Speed + Offset) * FMath::Exp(-static_cast<double>(Decay) * DeltaTime) - Offset;
	}
	else
	{
		// λ → 0 时 D/λ 发散，取上式的极限：线性减速
		NewSpeed = Speed - static_cast<double>(Decel) * DeltaTime;
	}
	return static_cast<float>(FMath::Max(NewSpeed, 0.0));
}

FVector Ginkgo::Movement::RemoveForwardInput(const FVector& Input, const FVector& Velocity)
{
	const FVector Direction = Velocity.GetSafeNormal();
	const double Forward = FVector::DotProduct(Input, Direction);
	return Forward > 0.0 ? Input - Direction * Forward : Input;
}

void Ginkgo::Movement::SteerSlide(FVector& V, const FVector& Steer, const float SteerStep)
{
	const double Speed = V.Size();
	V = (V + Steer * SteerStep).GetClampedToMaxSize(Speed);
}

bool Ginkgo::Movement::IsSlideDirectionAllowed(const FVector& Input, const FVector& ViewForward, const float MaxAngleCos)
{
	const FVector InputDirection = Input.GetSafeNormal2D();
	return InputDirection.IsZero() || FVector::DotProduct(InputDirection, ViewForward.GetSafeNormal2D()) >= MaxAngleCos;
}

// ===== 朝向 =====

double Ginkgo::Movement::ComputeFacingYaw(const bool bSprintFacing, const FVector& Velocity, const double ViewYaw, const double MinSpeed)
{
	if (bSprintFacing && Velocity.SizeSquared2D() > FMath::Square(MinSpeed))
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
	}
	return ViewYaw;
}

// ===== lurch =====

void Ginkgo::Movement::Lurch(FVector& V, const FVector& WishDir, const float Strength)
{
	const FVector Horizontal(V.X, V.Y, 0.0);
	const FVector Redirected = Horizontal + (WishDir * Horizontal.Size() - Horizontal) * Strength;
	V.X = Redirected.X;
	V.Y = Redirected.Y;
}

uint8 Ginkgo::Movement::PackDirectionKeys(const FIntPoint Keys)
{
	const auto PackAxis = [](const int32 Value) -> uint8 { return Value > 0 ? 1 : (Value < 0 ? 2 : 0); };
	return static_cast<uint8>(PackAxis(Keys.X) | (PackAxis(Keys.Y) << 2));
}

FIntPoint Ginkgo::Movement::UnpackDirectionKeys(const uint8 Packed)
{
	const auto UnpackAxis = [](const uint8 Bits) { return Bits == 1 ? 1 : (Bits == 2 ? -1 : 0); };
	return FIntPoint(UnpackAxis(Packed & 3), UnpackAxis((Packed >> 2) & 3));
}

FVector Ginkgo::Movement::DirectionKeysToWorld(const FIntPoint Keys, const double Yaw)
{
	if (Keys == FIntPoint::ZeroValue)
	{
		return FVector::ZeroVector;
	}
	const double NetYaw = FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(Yaw));
	const FRotationMatrix YawMatrix(FRotator(0.0, NetYaw, 0.0));
	// 同 AGinkgoCharacter::Move：Y 是视角的前方，X 是右方
	return (YawMatrix.GetUnitAxis(EAxis::X) * Keys.Y + YawMatrix.GetUnitAxis(EAxis::Y) * Keys.X).GetSafeNormal2D();
}

uint8 Ginkgo::Movement::QuantizeSpeedScale(const float Scale)
{
	return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Scale, 0.f, 1.f) * 255.f));
}

float Ginkgo::Movement::UnquantizeSpeedScale(const uint8 Quantized)
{
	return static_cast<float>(Quantized) / 255.f;
}

float Ginkgo::Movement::GroundWishSpeed(const float RunSpeed, const float SprintSpeed, const float CrouchSpeed, const bool bCrouching, const bool bSprinting, const float HeldScale, const float AdsScale)
{
	if (bCrouching)
	{
		return CrouchSpeed * HeldScale * AdsScale;
	}
	return bSprinting ? SprintSpeed * HeldScale : RunSpeed * HeldScale * AdsScale;
}

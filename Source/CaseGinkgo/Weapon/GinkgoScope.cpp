#include "GinkgoScope.h"

namespace
{
	/** 畸变的 k：红、蓝各多、少色差那么多（Channel −1、+1） */
	double ChannelStrength(const FGinkgoScopeOptics& Optics, const int32 Channel)
	{
		return FMath::Max(static_cast<double>(Optics.DistortionStrength) * (1.0 - Channel * static_cast<double>(Optics.ChromaticAberration)), 0.0);
	}

	double DistortionStart(const FGinkgoScopeOptics& Optics)
	{
		return FMath::Clamp(static_cast<double>(Optics.DistortionStart), 0.0, 0.99);
	}

	/** 色差分到两边：红 +½、绿 0、蓝 −½（Channel −1、0、+1），绿是基准。同材质里的 Side */
	double ChannelSide(const int32 Channel)
	{
		return -0.5 * static_cast<double>(FMath::Clamp(Channel, -1, 1));
	}
}

float FGinkgoScopeSurround::GetRenderScale(const float FirstPerson, const float Focus) const
{
	// 以 1080p 下 5 px 的默认远景模糊作为满权重；只有枪身模糊时，镜外的街景仍需原本的细节。
	const float Weight = FMath::Clamp(FirstPerson, 0.f, 1.f) * FMath::Clamp(Focus, 0.f, 1.f) * FMath::Clamp(BlurRadius / 5.f, 0.f, 1.f);
	return FMath::Lerp(1.f, FMath::Clamp(RenderScale, 0.5f, 1.f), Weight);
}

Ginkgo::Scope::FSightFrame Ginkgo::Scope::MakeSightFrame(const FTransform& Gun, const FGinkgoScope& Scope)
{
	const FVector Eyepiece = Scope.Mount.EyepieceCenter;
	FSightFrame Sight;
	Sight.Eyepiece = Gun.TransformPosition(Eyepiece);
	Sight.Objective = Gun.TransformPosition(Eyepiece + FVector(Scope.Mount.ObjectiveDistance, 0.0, 0.0));
	Sight.Eye = Gun.TransformPosition(Eyepiece - FVector(Scope.Optics.EyeRelief, 0.0, 0.0));
	Sight.Rotation = Gun.GetRotation().GetNormalized();
	return Sight;
}

double Ginkgo::Scope::GetApparentTanHalfField(const FGinkgoScopeOptics& Optics)
{
	return FMath::Max(static_cast<double>(Optics.Magnification), 1.0) * FMath::Tan(FMath::DegreesToRadians(0.5 * static_cast<double>(Optics.TrueFieldOfView)));
}

double Ginkgo::Scope::DistortRadius(const double Rho, const FGinkgoScopeOptics& Optics, const int32 Channel)
{
	const double Start = DistortionStart(Optics);
	const double K = ChannelStrength(Optics, Channel);
	if (Rho <= Start || K <= 0.0)
	{
		return Rho;
	}
	const double Over = Rho - Start;
	const double Width = 1.0 - Start;
	return Rho + K * Over * Over * Over / (Width * Width);
}

double Ginkgo::Scope::UndistortRadius(const double SourceRho, const FGinkgoScopeOptics& Optics, const int32 Channel)
{
	const double Start = DistortionStart(Optics);
	const double K = ChannelStrength(Optics, Channel);
	if (SourceRho <= Start || K <= 0.0)
	{
		return SourceRho;
	}
	// 牛顿法：f(ρ) = ρ + k·(ρ − ρ0)³ / w² − ρ_src，f' = 1 + 3k·(ρ − ρ0)² / w² ≥ 1，从 ρ_src 出发单调收敛（f 在开始之外是凸的）
	const double Width = 1.0 - Start;
	double Rho = SourceRho;
	for (int32 Iteration = 0; Iteration < 16; ++Iteration)
	{
		const double Over = FMath::Max(Rho - Start, 0.0);
		const double Value = Rho + K * Over * Over * Over / (Width * Width) - SourceRho;
		const double Slope = 1.0 + 3.0 * K * Over * Over / (Width * Width);
		const double Step = Value / Slope;
		Rho -= Step;
		if (FMath::Abs(Step) < 1e-12)
		{
			break;
		}
	}
	return Rho;
}

float Ginkgo::Scope::ReticleCoverage(const FVector2D& Mrad, const FGinkgoScopeReticle& Reticle, const double PixelSize)
{
	const double HalfWidth = 0.5 * static_cast<double>(Reticle.PostWidth);
	const double HalfGap = 0.5 * static_cast<double>(Reticle.Gap);
	const double X = FMath::Abs(Mrad.X);
	const double Y = Mrad.Y;
	// 到各部分的边的有符号距离（里面为负）：两根横柱是从空隙的边往外一直到视场外的长条
	const double Bars = FMath::Max(HalfGap - X, FMath::Abs(Y) - HalfWidth);
	// 竖柱：横柱的中线以下，全宽；最上面 TipLength 那一段从全宽收到尖。收尖那一段按斜边的法线量（水平距离乘斜边的余弦）
	const double Tip = FMath::Max(static_cast<double>(Reticle.TipLength), 0.0);
	double PostHalf = HalfWidth;
	double Slope = 1.0;
	if (Tip > 0.0 && Y > -Tip)
	{
		PostHalf = HalfWidth * FMath::Clamp(-Y / Tip, 0.0, 1.0);
		Slope = Tip / FMath::Sqrt(Tip * Tip + HalfWidth * HalfWidth);
	}
	const double Post = FMath::Max((X - PostHalf) * Slope, Y);
	const double Distance = FMath::Min(Bars, Post);
	return static_cast<float>(FMath::Clamp(0.5 - Distance / FMath::Max(PixelSize, 1e-6), 0.0, 1.0));
}

FVector2D Ginkgo::Scope::ChannelApparent(const FVector2D& Apparent, const FVector2D& Pupil, const FGinkgoScopeOptics& Optics, const int32 Channel)
{
	// 蓝的像比红的大：显示在同一处的，蓝的取得靠里（缩一点）、红的靠外；光线从出瞳靠边的地方过，蓝的往那边偏（取的地方往反方向挪）
	const double Side = ChannelSide(Channel);
	return Apparent * (1.0 + Side * static_cast<double>(Optics.LateralColor)) + Pupil * (Side * static_cast<double>(Optics.EyeColor));
}

float Ginkgo::Scope::ExitPupilVisibility(const FSightFrame& Sight, const FGinkgoScopeOptics& Optics, const FVector& Camera, const FVector& Direction, const int32 Channel)
{
	const FVector Local = Sight.Rotation.UnrotateVector(Camera - Sight.Eyepiece);
	const FVector Ray = Sight.Rotation.UnrotateVector(Direction.GetSafeNormal());
	if (Ray.X <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	// 这条线与出瞳所在的平面（目镜后面眼距那么远）交在哪：眼睛在平面前后都一样（每个方向的光是一束出瞳那么粗的平行光）。
	// 出瞳的色差：红的出瞳离目镜远半个 PupilColor，蓝的近半个
	const double Relief = static_cast<double>(Optics.EyeRelief) + ChannelSide(Channel) * static_cast<double>(Optics.PupilColor);
	const double Along = (-Relief - Local.X) / Ray.X;
	const double Off = FVector2D(Local.Y + Along * Ray.Y, Local.Z + Along * Ray.Z).Size();
	const double Radius = 0.5 * static_cast<double>(Optics.ExitPupil);
	const double Soft = FMath::Max(static_cast<double>(Optics.EyeBoxSoftness), 0.0);
	if (Soft <= 0.0)
	{
		return Off <= Radius ? 1.f : 0.f;
	}
	return static_cast<float>(1.0 - FMath::SmoothStep(Radius - Soft, Radius + Soft, Off));
}

bool Ginkgo::Scope::ProjectThroughScope(const FSightFrame& Sight, const FGinkgoScopeOptics& Optics, const FVector& WorldPoint, FVector& OutApparentDirection,
	double& OutRho)
{
	OutRho = 0.0;
	const FVector Local = Sight.Rotation.UnrotateVector(WorldPoint - Sight.Objective);
	if (Local.X <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	// 物镜看它的真实方向（正切）乘倍率就是目镜出来的（没畸变时）；畸变是"显示在 ρ 的取 ρ_src 的"，所以反过来求显示在哪
	const FVector2D Apparent = FVector2D(Local.Y / Local.X, Local.Z / Local.X) * FMath::Max(static_cast<double>(Optics.Magnification), 1.0);
	const double SourceRho = Apparent.Size() / GetApparentTanHalfField(Optics);
	const double Rho = UndistortRadius(SourceRho, Optics);
	const FVector2D Shown = SourceRho > UE_DOUBLE_SMALL_NUMBER ? Apparent * (Rho / SourceRho) : Apparent;
	OutApparentDirection = Sight.Rotation.RotateVector(FVector(1.0, Shown.X, Shown.Y).GetSafeNormal());
	OutRho = Rho;
	return Rho <= 1.0;
}

float Ginkgo::Scope::FirstPersonAlpha(const float AdsAlpha, const bool bHasScope)
{
	return bHasScope ? FMath::Clamp(AdsAlpha, 0.f, 1.f) : 0.f;
}

float Ginkgo::Scope::ScopeLookScale(const float AdsAlpha, const float ScopeScale, const float Magnification)
{
	return FMath::Lerp(1.f, ScopeScale / FMath::Max(Magnification, 1.f), FMath::Clamp(AdsAlpha, 0.f, 1.f));
}

FTransform Ginkgo::Scope::MakePivotRotation(const FRotator& Rotation, const FVector& Pivot)
{
	const FQuat Quat = Rotation.Quaternion();
	return FTransform(Quat, Pivot - Quat.RotateVector(Pivot));
}

FVector Ginkgo::Scope::ComputeFirstPersonEye(const FTransform& Gun, const FTransform& Offset, const FGinkgoScope& Scope, FSightFrame* OutRest)
{
	// 枪的最终变换 = 偏移 × 枪口对准以后的变换：去掉偏移就是枪口对准以后的那一把
	const FSightFrame Rest = MakeSightFrame(Offset.Inverse() * Gun, Scope);
	if (OutRest)
	{
		*OutRest = Rest;
	}
	// 出瞳挪了多少，拆成沿光轴（前后）与横着（上下左右）的两份，各跟各的
	const FGinkgoScopeHandling& Handling = Scope.Handling;
	const FVector Moved = MakeSightFrame(Gun, Scope).Eye - Rest.Eye;
	const FVector Axis = Rest.GetAxis();
	const FVector Along = Axis * FVector::DotProduct(Moved, Axis);
	const FVector Followed = Along * FMath::Clamp(static_cast<double>(Handling.EyeFollowAxial), 0.0, 1.0)
		+ (Moved - Along) * FMath::Clamp(static_cast<double>(Handling.EyeFollow), 0.0, 1.0);
	return Rest.Eye + Followed - Axis * FMath::Max(static_cast<double>(Handling.EyeBehind), 0.0);
}

FVector Ginkgo::Scope::SampleShotEyeShift(const FGinkgoScopeHandling& Handling, const double Elapsed)
{
	// 参考片段 23.13–23.37 s：镜框一帧顶近，保持偏大的时间比侧摆长，最后缓下来。
	constexpr double PeakTime = 0.033;
	constexpr double EndTime = 0.22;
	if (Elapsed <= 0.0 || Elapsed >= EndTime)
	{
		return FVector::ZeroVector;
	}
	const double Push = FMath::SmoothStep(0.0, PeakTime, Elapsed) * (1.0 - FMath::SmoothStep(PeakTime, EndTime, Elapsed));
	const double SincePeak = FMath::Max(Elapsed - PeakTime, 0.0);
	const double Swing = Push * FMath::Exp(-18.0 * SincePeak) * FMath::Cos(2.0 * UE_DOUBLE_PI * 11.0 * SincePeak);
	return Handling.ShotEyeShift * FVector(Push, Swing, Swing);
}

FRotator Ginkgo::Scope::StepMoveSway(FMoveSwayState& State, const FGinkgoScopeHandling& Handling, const FVector2D& LocalVelocity, const bool bOnGround,
	const double DeltaSeconds, FVector* OutShift)
{
	const double GroundSpeed = bOnGround ? LocalVelocity.Size() : 0.0;
	// 推一段：身体的速度、地速在这一段里从 From 匀速变到 To。一步是 π，走过的距离按首尾地速的平均
	const auto Advance = [&Handling](FMoveSwayState& Target, const FVector2D& FromVelocity, const double FromSpeed, const FVector2D& ToVelocity, const double ToSpeed,
		const double Seconds)
	{
		if (Seconds <= 0.0)
		{
			return;
		}
		Target.Velocity.StepMoving(FromVelocity, ToVelocity, Handling.MoveOmega, Seconds);
		Target.GroundSpeed.StepMoving(FromSpeed, ToSpeed, Handling.MoveOmega, Seconds);
		const double Distance = 0.5 * (FromSpeed + ToSpeed) * Seconds;
		Target.Phase = FMath::Fmod(Target.Phase + UE_DOUBLE_PI * Distance / FMath::Max(static_cast<double>(Handling.StepLength), 1.0), 2.0 * UE_DOUBLE_PI);
	};
	if (!State.bStarted)
	{
		State.Velocity.Reset(LocalVelocity);
		State.GroundSpeed.Reset(GroundSpeed);
		State.bStarted = true;
	}
	else
	{
		// 存的状态推到上一帧末：上一帧里身体的速度从上一次读到的变到这一次读到的
		Advance(State, State.LastVelocity, State.LastGroundSpeed, LocalVelocity, GroundSpeed, State.LastDeltaSeconds);
	}
	State.LastVelocity = LocalVelocity;
	State.LastGroundSpeed = GroundSpeed;
	State.LastDeltaSeconds = FMath::Max(DeltaSeconds, 0.0);
	// 这一帧还没移动：照上一帧末的速度推到这一帧末，只用来算这一帧的偏与晃
	FMoveSwayState Now = State;
	Advance(Now, LocalVelocity, GroundSpeed, LocalVelocity, GroundSpeed, DeltaSeconds);

	const double Reference = FMath::Max(static_cast<double>(Handling.MoveReferenceSpeed), 1.0);
	const FVector2D Lean(FMath::Clamp(Now.Velocity.Value.X / Reference, -1.5, 1.5), FMath::Clamp(Now.Velocity.Value.Y / Reference, -1.5, 1.5));
	const double Bob = FMath::Clamp(Now.GroundSpeed.Value / Reference, 0.0, 1.5);
	// 挪：X、Z 按前进的速度，Y 按横着的速度。颠：左右两步一个来回，上下每一步一上一下（同晃一个相位）
	if (OutShift)
	{
		*OutShift = FVector(
			Handling.MoveLeanShift.X * Lean.X,
			Handling.MoveLeanShift.Y * Lean.Y + Handling.MoveBobShift.X * Bob * FMath::Sin(Now.Phase),
			Handling.MoveLeanShift.Z * Lean.X + Handling.MoveBobShift.Y * Bob * FMath::Sin(2.0 * Now.Phase));
	}
	// 偏：往前走枪口往下（俯仰按前进的速度）、往右走枪口往左落、往右侧倾（偏航、侧倾按横着的速度）。晃：偏航两步一个来回，俯仰每一步一上一下
	return FRotator(
		Handling.MoveLean.Pitch * Lean.X + Handling.MoveBob.Y * Bob * FMath::Sin(2.0 * Now.Phase),
		Handling.MoveLean.Yaw * Lean.Y + Handling.MoveBob.X * Bob * FMath::Sin(Now.Phase),
		Handling.MoveLean.Roll * Lean.Y);
}

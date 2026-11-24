#include "Control/GinkgoAimAssist.h"

#include "Combat/GinkgoCombatTypes.h"           // Ginkgo::Combat::WeaponTraceChannel：看不看得见按子弹的通道
#include "Combat/GinkgoDamageableComponent.h"
#include "Combat/GinkgoTargetRegistry.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"

DECLARE_CYCLE_STAT(TEXT("Aim assist"), STAT_GinkgoAimAssist, STATGROUP_Ginkgo3C);

// ===== 算法 =====

namespace Ginkgo::AimAssist
{
	FGinkgoAimAssistProfile MakeProfile(const FGinkgoInputSettings& Settings, const EGinkgoInputDirectory Device)
	{
		const bool bPad = Device == EGinkgoInputDirectory::Gamepad;
		FGinkgoAimAssistProfile Profile;
		Profile.bEnabled = bPad ? Settings.bGamepadAimAssist : Settings.bMouseAimAssist;
		Profile.HipStrength = FMath::Clamp(bPad ? Settings.GamepadAimAssistHip : Settings.MouseAimAssistHip, 0.f, MaxStrength);
		Profile.AdsStrength = FMath::Clamp(bPad ? Settings.GamepadAimAssistAds : Settings.MouseAimAssistAds, 0.f, MaxStrength);
		Profile.InnerRadius = FMath::Max(bPad ? Settings.GamepadAimAssistInner : Settings.MouseAimAssistInner, 0.f);
		Profile.OuterRadius = FMath::Max(bPad ? Settings.GamepadAimAssistOuter : Settings.MouseAimAssistOuter, 0.f);
		Profile.MaxDistance = FMath::Max(bPad ? Settings.GamepadAimAssistRange : Settings.MouseAimAssistRange, 0.f) * 100.f;
		Profile.TargetMotionScale = FMath::Clamp(bPad ? Settings.GamepadAimAssistFollow : Settings.MouseAimAssistFollow, 0.f, 1.f);
		return Profile;
	}

	FVector2D ClosestPointOnSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSquared = AB.SizeSquared();
		if (LengthSquared < UE_SMALL_NUMBER)
		{
			return A;
		}
		const double T = FMath::Clamp(FVector2D::DotProduct(Point - A, AB) / LengthSquared, 0.0, 1.0);
		return A + AB * T;
	}

	FVector2D ScreenFromView(const FGinkgoAimAssistField& Field, const FVector2D& View)
	{
		return Field.ScreenX * View.X + Field.ScreenY * View.Y;
	}

	FVector2D ViewFromScreen(const FGinkgoAimAssistField& Field, const FVector2D& Screen)
	{
		const FVector2D& X = Field.ScreenX;
		const FVector2D& Y = Field.ScreenY;
		const double Determinant = X.X * Y.Y - Y.X * X.Y;
		if (FMath::Abs(Determinant) < 1.e-3)
		{
			return Screen;
		}
		return FVector2D(Y.Y * Screen.X - Y.X * Screen.Y, X.X * Screen.Y - X.Y * Screen.X) / Determinant;
	}

	float Falloff(const float Distance, const float Inner, const float Outer)
	{
		if (Distance <= Inner)
		{
			return 1.f;
		}
		if (Distance >= Outer)
		{
			return 0.f;
		}
		const float T = (Distance - Inner) / (Outer - Inner);
		return 1.f - T * T * (3.f - 2.f * T);
	}

	FVector2D HalfLenzVelocity(const FVector2D& Offset, const FVector2D& RelativeVelocity, const float K)
	{
		const double Distance = Offset.Size();
		if (K <= 0.f || Distance < UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector2D::ZeroVector;
		}
		const FVector2D Radial = Offset / Distance;
		// 往外的分量；往里来（小于 0）的那一半就是去掉的“来拒”
		const double Away = FVector2D::DotProduct(RelativeVelocity, Radial);
		return Away > 0.0 ? -FMath::Min(K, MaxStrength) * Away * Radial : FVector2D::ZeroVector;
	}

	namespace
	{
		/**
		 * 准星在这里时画面上的修正速度。正好在中线上（没有径向）时按相对运动接下来往哪走定径向：
		 * 不然积分的一小步正好落在中线上，这一步就白走了，帧率不同落不落得上不一样
		 */
		FVector2D CorrectionAt(const FVector2D& Crosshair, const FVector2D& A, const FVector2D& B, const FVector2D& Relative, const FGinkgoAimAssistField& Field)
		{
			FVector2D Offset = Crosshair - ClosestPointOnSegment(Crosshair, A, B);
			const float K = Field.Strength * Falloff(Offset.Size(), Field.InnerRadius, Field.OuterRadius);
			if (Offset.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
			{
				const FVector2D Probe = Crosshair + Relative * 1.e-4;
				Offset = Probe - ClosestPointOnSegment(Probe, A, B);
			}
			return HalfLenzVelocity(Offset, Relative, K);
		}
	}

	FVector2D IntegrateFrame(const FGinkgoAimAssistField& Field, const FVector2D& InputVelocity, const float TargetMotionScale, const float DeltaTime)
	{
		if (DeltaTime <= 0.f || Field.Strength <= 0.f)
		{
			return FVector2D::ZeroVector;
		}
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / MaxStep), 1, 512);
		const double Step = DeltaTime / Steps;
		// 画面上准星跟着玩家转的走（乘视差）；相对速度这一帧不变：玩家的输入是这一帧的平均，目标的速度是上一帧量的
		const FVector2D ScreenInput = ScreenFromView(Field, InputVelocity);
		const FVector2D Relative = ScreenInput - Field.Velocity * TargetMotionScale;
		FVector2D Crosshair = FVector2D::ZeroVector;
		FVector2D ScreenCorrection = FVector2D::ZeroVector;
		// 中点法：强度随距离变，按步首的强度走一整步会偏，取半步处的。目标照它真的速度走（不乘 TargetMotionScale：那只管算不算进相对速度）
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			const FVector2D Shift = Field.Velocity * (Step * Index);
			const FVector2D Start = CorrectionAt(Crosshair, Field.SegmentA + Shift, Field.SegmentB + Shift, Relative, Field);
			const FVector2D HalfShift = Shift + Field.Velocity * (0.5 * Step);
			const FVector2D Middle = CorrectionAt(Crosshair + (ScreenInput + Start) * (0.5 * Step), Field.SegmentA + HalfShift, Field.SegmentB + HalfShift, Relative, Field);
			Crosshair += (ScreenInput + Middle) * Step;
			ScreenCorrection += Middle * Step;
		}
		// 视差这一帧不变，换回视角要转多少可以最后一起换
		return ViewFromScreen(Field, ScreenCorrection);
	}
}

// ===== 每帧 =====

namespace
{
	/** 一帧里最快能挪多远（cm / s）：比这快的是复位、传送，这一帧不算目标的速度 */
	constexpr double MaxJumpSpeed = 10000.0;

	/** 目标在画面上自己走得比这快（度 / 秒）也当作跳了 */
	constexpr double MaxTargetAngularSpeed = 360.0;

	/** 算视差时视角转多少（度） */
	constexpr double ParallaxProbe = 0.05;

	/** 相机看出去的一个方向在局部坐标里的位置（度）：X 偏航差乘俯仰的余弦，Y 俯仰差。两个都归一化：视角的俯仰存成 0 到 360 */
	FVector2D ToLocal(const FVector& Direction, const FRotator& View, const double CosPitch)
	{
		const FRotator Rotation = Direction.Rotation();
		return FVector2D(FRotator::NormalizeAxis(Rotation.Yaw - View.Yaw) * CosPitch, FRotator::NormalizeAxis(Rotation.Pitch - View.Pitch));
	}

	/** 目标的中线：配置的两根骨头；网格上没有时用包围盒的竖直中线 */
	bool GetSegment(const UGinkgoDamageableComponent& Target, const FGinkgoAimAssistConfig& Config, FVector& OutA, FVector& OutB)
	{
		const AActor* Owner = Target.GetOwner();
		if (!Owner)
		{
			return false;
		}
		if (const USkeletalMeshComponent* Mesh = Owner->FindComponentByClass<USkeletalMeshComponent>();
			Mesh && Mesh->GetBoneIndex(Config.LowerBone) != INDEX_NONE && Mesh->GetBoneIndex(Config.UpperBone) != INDEX_NONE)
		{
			OutA = Mesh->GetBoneLocation(Config.LowerBone);
			OutB = Mesh->GetBoneLocation(Config.UpperBone);
			return true;
		}
		FVector Origin, Extent;
		Owner->GetActorBounds(/*bOnlyCollidingComponents*/ true, Origin, Extent);
		OutA = Origin - FVector(0.0, 0.0, Extent.Z * 0.5);
		OutB = Origin + FVector(0.0, 0.0, Extent.Z * 0.6);
		return !Extent.IsNearlyZero();
	}

	struct FCandidate
	{
		const UGinkgoDamageableComponent* Target = nullptr;
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		FVector2D LocalA = FVector2D::ZeroVector;
		FVector2D LocalB = FVector2D::ZeroVector;
		/** 中线上离准星最近的点（参数、世界的位置、局部的位置） */
		double T = 0.0;
		FVector Nearest = FVector::ZeroVector;
		FVector2D LocalNearest = FVector2D::ZeroVector;
		float Distance = 0.f;
		float Inner = 0.f;
		float Outer = 0.f;
		/** 离中线多远占淡到零的半径多少：越小越优先 */
		float Score = 0.f;
	};
}

FRotator FGinkgoAimAssist::Update(const FInputs& In, const float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoAimAssist);

	Frame = FGinkgoAimAssistFrame();
	Frame.Device = In.Device;
	Frame.bEnabled = In.Profile.bEnabled;
	Frame.bHasInput = !In.LookInput.IsNearlyZero(1.e-6) || In.bMoveInput;
	const UGinkgoTargetRegistry* Registry = UGinkgoTargetRegistry::Get(In.World);
	// 总开关关着：连目标也不选（量目标速度的上一帧也忘掉，打开时从头量）
	if (!In.Profile.bEnabled || !Registry || !In.Config || DeltaTime <= 0.f)
	{
		Reset();
		return FRotator::ZeroRotator;
	}
	const FGinkgoAimAssistConfig& Config = *In.Config;
	const FGinkgoAimAssistProfile& Profile = In.Profile;
	const double CosPitch = FMath::Max(FMath::Cos(FMath::DegreesToRadians(In.View.Pitch)), 0.1);
	const FVector Forward = In.View.Vector();
	const auto CameraAt = [&In](const FRotator& View) { return In.PredictCamera ? In.PredictCamera(View) : In.CameraLocation; };
	const FVector Camera = CameraAt(In.View);

	// 选目标：活着、在前面、不太远、准星在它淡到零的范围以内；离中线最近（按范围归一）的先看
	TArray<FCandidate, TInlineAllocator<8>> Candidates;
	for (const TWeakObjectPtr<UGinkgoDamageableComponent>& Weak : Registry->GetTargets())
	{
		const UGinkgoDamageableComponent* Target = Weak.Get();
		FCandidate Candidate;
		if (!Target || Target->IsDead() || Target->GetOwner() == In.Self || !GetSegment(*Target, Config, Candidate.A, Candidate.B))
		{
			continue;
		}
		if (FVector::DotProduct(Candidate.A - Camera, Forward) <= 0.0 || FVector::DotProduct(Candidate.B - Camera, Forward) <= 0.0)
		{
			continue;
		}
		Candidate.Target = Target;
		Candidate.LocalA = ToLocal(Candidate.A - Camera, In.View, CosPitch);
		Candidate.LocalB = ToLocal(Candidate.B - Camera, In.View, CosPitch);
		Candidate.LocalNearest = Ginkgo::AimAssist::ClosestPointOnSegment(FVector2D::ZeroVector, Candidate.LocalA, Candidate.LocalB);
		const FVector2D AB = Candidate.LocalB - Candidate.LocalA;
		Candidate.T = AB.SizeSquared() > UE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(Candidate.LocalNearest - Candidate.LocalA, AB) / AB.SizeSquared(), 0.0, 1.0) : 0.0;
		Candidate.Nearest = FMath::Lerp(Candidate.A, Candidate.B, Candidate.T);
		Candidate.Distance = FVector::Dist(Camera, Candidate.Nearest);
		if (Candidate.Distance > Profile.MaxDistance || Candidate.Distance < 1.f)
		{
			continue;
		}
		Candidate.Inner = FMath::RadiansToDegrees(FMath::Atan(Profile.InnerRadius / Candidate.Distance));
		Candidate.Outer = FMath::RadiansToDegrees(FMath::Atan(FMath::Max(Profile.OuterRadius, Profile.InnerRadius) / Candidate.Distance));
		const float Offset = Candidate.LocalNearest.Size();
		UE_LOG(LogGinkgoControl, VeryVerbose, TEXT("[AimAssist] %s：中线 (%.3f, %.3f)–(%.3f, %.3f)，最近 (%.3f, %.3f)，%.0f cm，到零 %.2f°"), *GetNameSafe(Target->GetOwner()),
			Candidate.LocalA.X, Candidate.LocalA.Y, Candidate.LocalB.X, Candidate.LocalB.Y, Candidate.LocalNearest.X, Candidate.LocalNearest.Y, Candidate.Distance, Candidate.Outer);
		if (Candidate.Outer <= 0.f || Offset >= Candidate.Outer)
		{
			continue;
		}
		Candidate.Score = Offset / Candidate.Outer;
		Candidates.Add(Candidate);
	}
	Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score < B.Score; });

	// 看得见的才算：相机到中线上那一点打子弹的通道，先打到的不是目标自己就跳过（墙后面的不吸）
	const FCandidate* Chosen = nullptr;
	for (const FCandidate& Candidate : Candidates)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoAimAssist), /*bTraceComplex*/ false, In.Self);
		FHitResult Hit;
		if (!In.World->LineTraceSingleByChannel(Hit, Camera, Candidate.Nearest, Ginkgo::Combat::WeaponTraceChannel, Params) || Hit.GetActor() == Candidate.Target->GetOwner())
		{
			Chosen = &Candidate;
			break;
		}
		UE_LOG(LogGinkgoControl, Verbose, TEXT("[AimAssist] 看不见 %s：挡着的是 %s（%s），离相机 %.0f cm"), *GetNameSafe(Candidate.Target->GetOwner()),
			*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), Hit.Distance);
	}
	if (!Chosen)
	{
		Reset();
		return FRotator::ZeroRotator;
	}

	FGinkgoAimAssistField Field;
	Field.SegmentA = Chosen->LocalA;
	Field.SegmentB = Chosen->LocalB;
	Field.InnerRadius = Chosen->Inner;
	Field.OuterRadius = Chosen->Outer;
	Field.Strength = FMath::Clamp(FMath::Lerp(Profile.HipStrength, Profile.AdsStrength, FMath::Clamp(In.AdsAlpha, 0.f, 1.f)) * In.StrengthScale, 0.f, Ginkgo::AimAssist::MaxStrength);

	// 视差：视角往右、往上转一点点时相机挪到哪，中线上那一点在局部坐标里挪多少。准星减目标，所以取负；没有视差时是单位阵
	if (In.PredictCamera)
	{
		const FVector2D Here = ToLocal(Chosen->Nearest - Camera, In.View, CosPitch);
		const FRotator YawView = In.View + FRotator(0.0, ParallaxProbe, 0.0);
		const FRotator PitchView = In.View + FRotator(ParallaxProbe, 0.0, 0.0);
		const FVector2D ScreenX = -(ToLocal(Chosen->Nearest - CameraAt(YawView), YawView, CosPitch) - Here) / (ParallaxProbe * CosPitch);
		const FVector2D ScreenY = -(ToLocal(Chosen->Nearest - CameraAt(PitchView), PitchView, CosPitch) - Here) / ParallaxProbe;
		// 差不多退化（相机贴着目标之类）时不用
		if (ScreenX.X * ScreenY.Y - ScreenY.X * ScreenX.Y > 0.2)
		{
			Field.ScreenX = ScreenX;
			Field.ScreenY = ScreenY;
		}
	}

	// 目标在画面上自己走的速度：中线上同一个参数的点，准星减它这一帧变的，扣掉转视角带来的（视差乘转的量），剩下的就是目标自己走、自己走动带来的
	const double Now = In.World->GetTimeSeconds();
	FVector2D TargetVelocity = FVector2D::ZeroVector;
	if (LastTarget.Get() == Chosen->Target && LastTime >= 0.0 && Now > LastTime)
	{
		const double Elapsed = Now - LastTime;
		const FVector Before = FMath::Lerp(LastSegmentA, LastSegmentB, Chosen->T);
		const bool bJumped = FVector::Dist(Before, Chosen->Nearest) > MaxJumpSpeed * Elapsed || FVector::Dist(LastCamera, Camera) > MaxJumpSpeed * Elapsed;
		if (!bJumped)
		{
			const FVector2D OffsetChange = -(FMath::Lerp(Chosen->LocalA, Chosen->LocalB, Chosen->T) - FMath::Lerp(LastLocalA, LastLocalB, Chosen->T));
			const FVector2D ViewTurn(FRotator::NormalizeAxis(In.View.Yaw - LastView.Yaw) * CosPitch, FRotator::NormalizeAxis(In.View.Pitch - LastView.Pitch));
			TargetVelocity = (Ginkgo::AimAssist::ScreenFromView(Field, ViewTurn) - OffsetChange) / Elapsed;
			if (TargetVelocity.Size() > MaxTargetAngularSpeed)
			{
				TargetVelocity = FVector2D::ZeroVector;
			}
		}
	}
	Field.Velocity = TargetVelocity;
	LastTarget = Chosen->Target;
	LastSegmentA = Chosen->A;
	LastSegmentB = Chosen->B;
	LastLocalA = Chosen->LocalA;
	LastLocalB = Chosen->LocalB;
	LastCamera = Camera;
	LastView = In.View;
	LastTime = Now;

	Frame.Target = Chosen->Target->GetOwner();
	Frame.SegmentA = Chosen->A;
	Frame.SegmentB = Chosen->B;
	Frame.Nearest = Chosen->Nearest;
	Frame.Distance = Chosen->Distance;
	Frame.Offset = -Chosen->LocalNearest;
	Frame.TargetVelocity = TargetVelocity;
	Frame.ScreenX = Field.ScreenX;
	Frame.ScreenY = Field.ScreenY;
	Frame.InnerRadius = Field.InnerRadius;
	Frame.OuterRadius = Field.OuterRadius;
	Frame.Strength = Field.Strength;
	Frame.K = Field.Strength * Ginkgo::AimAssist::Falloff(Frame.Offset.Size(), Field.InnerRadius, Field.OuterRadius);

	if (Field.Strength <= 0.f || (Config.bRequireInput && !Frame.bHasInput))
	{
		return FRotator::ZeroRotator;
	}
	const FVector2D InputVelocity = FVector2D(In.LookInput.Yaw * CosPitch, In.LookInput.Pitch) / DeltaTime;
	const FVector2D Local = Ginkgo::AimAssist::IntegrateFrame(Field, InputVelocity, Profile.TargetMotionScale, DeltaTime);
	Frame.Correction = FRotator(Local.Y, Local.X / CosPitch, 0.0);
	return Frame.Correction;
}

void FGinkgoAimAssist::Reset()
{
	LastTarget.Reset();
	LastTime = -1.0;
}

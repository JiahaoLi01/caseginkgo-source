#include "GinkgoTraversalMotion.h"

#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "GinkgoTraversalAnimations.h"
#include "MotionWarpingComponent.h"

namespace
{
	/** 五次平滑权重，起止一阶与二阶导数均为零。 */
	double Smooth(const double Alpha)
	{
		const double A = FMath::Clamp(Alpha, 0., 1.);
		return A * A * A * (A * (A * 6. - 15.) + 10.);
	}

	/** 入场可能仍在蹲伏；预检与站起后的执行必须采用同一站立尺寸。 */
	float StandingHalfHeight(const ACharacter& Character)
	{
		return Character.GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()
			* Character.GetCapsuleComponent()->GetShapeScale();
	}
}

FTransform FGinkgoTraversalMotion::Evaluate(const float Time) const
{
	if (!Montage || Duration <= 0.f) { return FTransform::Identity; }
	const float T = FMath::Clamp(Time, 0.f, Duration);
	FTransform Root = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Montage, 0.f, T);
	FVector Position = Root.GetTranslation() * Scale + FVector::UpVector * HeightCorrection * Smooth(T / Duration);
	if (RiseEndTime > 0.f)
	{
		const double Progress = FVector::DotProduct(Root.GetTranslation(), Forward);
		Position += Forward * (WarpForward(Progress, T) - FVector::DotProduct(Position, Forward));
		Position = FacingCorrection.RotateVector(Position);
		Root.SetRotation(FQuat::Slerp(FQuat::Identity, FacingCorrection, Smooth(T / RiseEndTime)) * Root.GetRotation());
	}
	Root.SetTranslation(Position);
	return Root;
}

double FGinkgoTraversalMotion::WarpForward(const double SourceProgress, const float Time) const
{
	const double ExitSource = SourceEnd - SourceContact;
	const bool bHasEntry = SourceContact > .01;
	const bool bHasExit = ExitSource > .01;
	const double EntrySlope = bHasEntry ? ContactDistance / SourceContact : 0.;
	const double ExitSlope = bHasExit ? (EndDistance - ContactDistance) / ExitSource : 0.;
	// 两侧共用调和平均切线，但不能超过触边后自身的位移比例。远处起步只拉长上升段，
	// 不能把较大的入场速度带进支撑段、在动画尚未抬腿时抢先越过墙面；短入场仍平滑起步。
	const double ContactSlope = EntrySlope > 0. && ExitSlope > 0.
		? FMath::Min(ExitSlope, 2. * EntrySlope * ExitSlope / (EntrySlope + ExitSlope)) : 0.;
	if (Time <= RiseEndTime)
	{
		if (!bHasEntry) { return ContactDistance * Smooth(Time / RiseEndTime); }
		return FMath::CubicInterp(0., SourceContact * EntrySlope * EntrySlopeScale,
			ContactDistance, SourceContact * ContactSlope, SourceProgress / SourceContact);
	}
	if (!bHasExit) { return FMath::Lerp(ContactDistance, EndDistance, Smooth((Time - RiseEndTime) / (Duration - RiseEndTime))); }
	return FMath::CubicInterp(ContactDistance, ExitSource * ContactSlope, EndDistance, ExitSource * ExitSlope,
		(SourceProgress - SourceContact) / ExitSource);
}

FTransform FGinkgoTraversalMotion::Delta(const float From, const float To) const
{
	return Evaluate(To).GetRelativeTransform(Evaluate(From));
}

FGinkgoTraversalBodyKey FGinkgoTraversalMotion::BodyAt(const float Time) const
{
	if (Body.IsEmpty()) { return {}; }
	for (int32 Index = 1; Index < Body.Num(); ++Index)
	{
		if (Time > Body[Index].Time) { continue; }
		const auto& A = Body[Index - 1];
		const auto& B = Body[Index];
		const float Alpha = FMath::Clamp((Time - A.Time) / (B.Time - A.Time), 0.f, 1.f);
		FGinkgoTraversalBodyKey Result;
		Result.Time = Time;
		Result.Offset = FMath::Lerp(A.Offset, B.Offset, Alpha);
		Result.RadiusScale = FMath::Lerp(A.RadiusScale, B.RadiusScale, Alpha);
		Result.HeightScale = FMath::Lerp(A.HeightScale, B.HeightScale, Alpha);
		return Result;
	}
	return Body.Last();
}

bool Ginkgo::Traversal::PrepareMotion(const FGinkgoTraversalClip& Clip, const FGinkgoTraversalResult& Result,
	const ACharacter& Character, FGinkgoTraversalMotion& Out, const FTransform* RecordedOrigin)
{
	Out = {};
	if (!Clip.IsValid() || !Result.IsValid()) { return false; }
	const auto* Capsule = Character.GetCapsuleComponent();
	const FVector Feet = RecordedOrigin ? RecordedOrigin->GetLocation()
		: Character.GetActorLocation() - FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight();
	const FQuat Rotation = RecordedOrigin ? RecordedOrigin->GetRotation() : Character.GetActorQuat() * Character.GetBaseRotationOffset();
	const FQuat TargetRotation = Result.Forward.Rotation().Quaternion() * Character.GetBaseRotationOffset();
	Out.Montage = Clip.Montage;
	Out.Duration = Clip.MotionEndTime;
	Out.Origin = RecordedOrigin ? *RecordedOrigin : FTransform(Rotation, Feet, Character.GetMesh()->GetComponentScale());
	Out.FacingCorrection = Rotation.Inverse() * TargetRotation;
	Out.RiseEndTime = Clip.RiseEndTime;
	Out.Forward = Character.GetBaseRotationOffset().UnrotateVector(FVector::ForwardVector).GetSafeNormal2D();
	Out.Body = Clip.Body;
	const FVector SourceEnd = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Clip.Montage, 0.f, Out.Duration).GetTranslation();
	// 正常地面间距沿用进入时的 2 cm 左右余量，不把胶囊脚底强压到碰撞面上。
	const FTransform TargetFrame(TargetRotation, Feet, Out.Origin.GetScale3D());
	const FVector End = TargetFrame.InverseTransformPosition(Result.Landing.Position + FVector::UpVector * 2.15);
	const FVector Contact = TargetFrame.InverseTransformPosition(Feet + Result.Forward * (Result.WallDistance - Clip.ContactRootDistance * Capsule->GetShapeScale()));
	Out.SourceContact = FVector::DotProduct(UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Clip.Montage, 0.f, Clip.RiseEndTime).GetTranslation(), Out.Forward);
	Out.SourceEnd = FVector::DotProduct(SourceEnd, Out.Forward);
	Out.ContactDistance = FMath::Max(0., FVector::DotProduct(Contact, Out.Forward));
	Out.EndDistance = FVector::DotProduct(End, Out.Forward);
	const double FrontGap = Result.WallDistance - Capsule->GetScaledCapsuleRadius();
	Out.EntrySlopeScale = Smooth(FrontGap / 5.);
	const double HorizontalScale = SourceEnd.SizeSquared2D() > 1. ? End.Size2D() / SourceEnd.Size2D() : 1.;
	const double VerticalScale = Result.Height / (Clip.ReferenceHeight * Out.Origin.GetScale3D().Z);
	if (VerticalScale < Clip.VerticalScaleRange.X || VerticalScale > Clip.VerticalScaleRange.Y || Out.EndDistance <= Out.ContactDistance) { return false; }
	Out.Scale = FVector(HorizontalScale, HorizontalScale, VerticalScale);
	const FVector Error = End - SourceEnd * Out.Scale;
	if ((Error - Out.Forward * FVector::DotProduct(Error, Out.Forward)).Size2D() > 2. || FMath::Abs(Error.Z) > Clip.MaxHeightCorrection) { return false; }
	Out.HeightCorrection = Error.Z;
	return true;
}

bool Ginkgo::Traversal::SweepBody(const FGinkgoTraversalMotion& Motion, const ACharacter& Character,
	const float From, const float To, const FVector& StartCenter, const FVector& EndCenter, FHitResult& Hit)
{
	const auto A = Motion.BodyAt(From);
	const auto B = Motion.BodyAt(To);
	const auto* Capsule = Character.GetCapsuleComponent();
	// 2 mm 接触皮肤容许擦面；半径与半高一起收边，不把静止贴墙当作已经穿透。
	constexpr float ContactSkin = .2f;
	const float Radius = Capsule->GetScaledCapsuleRadius() * FMath::Max(A.RadiusScale, B.RadiusScale) - ContactSkin;
	const float HalfHeight = FMath::Max(Radius, StandingHalfHeight(Character) * FMath::Max(A.HeightScale, B.HeightScale) - ContactSkin);
	// 局部代理偏移跟随素材自身的根旋转；有侧身/转体的未来素材不能固定在起步朝向上查询。
	const FQuat FacingA = Motion.Origin.GetRotation() * Motion.Evaluate(From).GetRotation() * Character.GetBaseRotationOffset().Inverse();
	const FQuat FacingB = Motion.Origin.GetRotation() * Motion.Evaluate(To).GetRotation() * Character.GetBaseRotationOffset().Inverse();
	const FVector Start = StartCenter + FacingA.RotateVector(A.Offset * Capsule->GetShapeScale());
	const FVector End = EndCenter + FacingB.RotateVector(B.Offset * Capsule->GetShapeScale());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalAnimatedBody), false, &Character);
	const FCollisionResponseParams Response(Capsule->GetCollisionResponseToChannels());
	return !Character.GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Radius, HalfHeight), Params, Response);
}

bool Ginkgo::Traversal::CheckMotion(const FGinkgoTraversalMotion& Motion, const ACharacter& Character, FHitResult& Hit)
{
	const FVector CenterOffset = FVector::UpVector * StandingHalfHeight(Character);
	FVector Previous = Motion.Origin.GetLocation() + CenterOffset;
	const int32 Steps = FMath::CeilToInt(Motion.Duration * 120.f);
	for (int32 Index = 1; Index <= Steps; ++Index)
	{
		const float From = Motion.Duration * (Index - 1) / Steps;
		const float To = Motion.Duration * Index / Steps;
		const FVector Next = Motion.Origin.TransformPosition(Motion.Evaluate(To).GetTranslation()) + CenterOffset;
		if (!SweepBody(Motion, Character, From, To, Previous, Next, Hit)) { return false; }
		Previous = Next;
	}
	return true;
}

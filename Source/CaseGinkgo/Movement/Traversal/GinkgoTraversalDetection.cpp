#include "GinkgoTraversalDetection.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

bool FGinkgoTraversalSettings::IsValid() const
{
	const float Values[] = {Reach, MinHeight, MaxHeight, LowHeight, MaxVaultHeight, MaxVaultDepth, MaxLandingDrop,
		MaxDepth, ScanStep, ProbeRadius, HeightStep, MaxWallAngle, HandHalfSpan, HandInset, HeightTolerance,
		HandDepthTolerance, SupportMargin, Clearance, PathClearance};
	for (const float Value : Values) { if (!FMath::IsFinite(Value) || Value < 0.f) { return false; } }
	return Reach >= 1.f && Reach <= 200.f && MaxHeight > MinHeight && MaxHeight <= 350.f
		&& LowHeight >= MinHeight && LowHeight <= MaxHeight && MaxVaultHeight <= MaxHeight
		&& MaxDepth >= 10.f && MaxDepth <= 300.f && MaxVaultDepth <= MaxDepth && MaxLandingDrop <= 300.f
		&& ScanStep >= 2.f && ScanStep <= 20.f && ProbeRadius >= 1.f && ProbeRadius <= 15.f
		&& HeightStep >= 5.f && HeightStep <= 40.f && MaxWallAngle <= 75.f && HandHalfSpan >= 1.f && HandHalfSpan <= 45.f
		&& HandInset >= 1.f && HandInset <= 20.f && HeightTolerance <= 20.f && HandDepthTolerance <= 35.f
		&& SupportMargin <= 15.f && Clearance >= 1.f && Clearance <= 10.f && PathClearance >= 1.f && PathClearance <= 30.f
		&& TraceChannel < ECC_MAX;
}

FVector Ginkgo::Traversal::CameraDirection(const FRotator& CameraRotation)
{
	if (CameraRotation.ContainsNaN()) { return FVector::ZeroVector; }
	const FVector Projection = CameraRotation.Vector().GetSafeNormal2D();
	return Projection.IsNearlyZero() ? FRotator(0., CameraRotation.Yaw, 0.).Vector() : Projection;
}

namespace GinkgoTraversalDetection
{
	/** 配置被异常组合时也不无限发射查询；达到预算后整个结果作废，不把跳过的检查当成功。 */
	constexpr int32 QueryLimit = 1024;

	/** 将支撑结果同时保存为世界点和组件局部点；动作启动时必须重新验证组件有效性。 */
	FGinkgoTraversalContact Contact(const FHitResult& Hit)
	{
		FGinkgoTraversalContact Result;
		Result.Component = Hit.GetComponent();
		Result.Position = Hit.ImpactPoint;
		Result.Normal = Hit.ImpactNormal;
		if (const auto* Component = Hit.GetComponent())
		{
			Result.LocalPosition = Component->GetComponentTransform().InverseTransformPosition(Result.Position);
		}
		return Result;
	}

	/** 静止支撑才是当前灰盒阶段的候选；被禁止的表面仍留在身体碰撞查询里。 */
	bool SupportsTraversal(const FHitResult& Hit)
	{
		const auto* Component = Hit.GetComponent();
		return Component && Component->Mobility != EComponentMobility::Movable && !Component->IsSimulatingPhysics()
			&& Component->GetCollisionObjectType() != ECC_Pawn && Component->GetCollisionObjectType() != ECC_PhysicsBody
			&& !Component->ComponentHasTag(TEXT("Traversal.Blocked"))
			&& (!Hit.GetActor() || !Hit.GetActor()->ActorHasTag(TEXT("Traversal.Blocked")));
	}

	/** 同一次查询的临时执行器，只借用世界与输入；不缓存上一次候选，不持有角色或支撑。 */
	struct FDetector
	{
		/** 同步物理查询所用世界，由调用方保证生命周期。 */
		const UWorld& World;

		/** 同一次角色快照，位置与碰撞尺寸不可在中途重新读取。 */
		const FGinkgoTraversalQuery& Input;

		/** 已通过合法性检查的配置，本次检测期间只读。 */
		const FGinkgoTraversalSettings& Settings;

		/** 输出结果，包括实际查询次数与失败时已发现的几何。 */
		FGinkgoTraversalResult Result;

		/** 可选的调试记录，外部持有；空时不保存查询轨迹。 */
		TArray<FGinkgoTraversalProbe>* Probes;

		/** 忽略自身的场景查询参数；不忽略目标墙，身体始终需要通过碰撞检查。 */
		FCollisionQueryParams Params;

		/** 相机水平投影的右方向，世界单位向量；双手与脚底支撑环共用。 */
		FVector Right;

		FDetector(const UWorld& InWorld, const FGinkgoTraversalQuery& InInput, const FGinkgoTraversalSettings& InSettings,
			TArray<FGinkgoTraversalProbe>* InProbes)
			: World(InWorld), Input(InInput), Settings(InSettings), Probes(InProbes),
			Params(SCENE_QUERY_STAT(GinkgoTraversal), false, InInput.Owner), Right(FVector::CrossProduct(FVector::UpVector, InInput.Forward))
		{
			Result.Forward = Input.Forward;
			Result.Reach = Settings.Reach;
		}

		bool Trace(FHitResult& Hit, const FVector& Start, const FVector& End, const float Radius = 0.f)
		{
			if (++Result.QueryCount > QueryLimit) { return false; }
			const bool bHit = Radius > 0.f
				? World.SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Settings.TraceChannel, FCollisionShape::MakeSphere(Radius), Params)
				: World.LineTraceSingleByChannel(Hit, Start, End, Settings.TraceChannel, Params);
			if (Probes) { Probes->Add({Start, End, Radius, 0.f, bHit}); }
			return bHit;
		}

		bool BodyBlocked(const FVector& Start, const FVector& End, const float HalfHeight)
		{
			if (++Result.QueryCount > QueryLimit) { return true; }
			FHitResult Hit;
			// 与执行查询采用相同的 2 mm 接触皮肤；恰好贴墙不是初始穿透。
			const FCollisionShape Shape = FCollisionShape::MakeCapsule(Input.Radius - .2f, HalfHeight - .2f);
			const FCollisionResponseParams Response(Input.BodyResponses);
			const bool bHit = Start.Equals(End)
				? World.OverlapBlockingTestByChannel(Start, FQuat::Identity, Input.BodyChannel, Shape, Params, Response)
				: World.SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Input.BodyChannel, Shape, Params, Response);
			if (Probes) { Probes->Add({Start, End, Input.Radius, HalfHeight, bHit}); }
			return bHit;
		}

		bool Walkable(const FHitResult& Hit) const
		{
			if (Hit.bStartPenetrating || !SupportsTraversal(Hit)) { return false; }
			const float FloorZ = Hit.GetComponent()->GetWalkableSlopeOverride().ModifyWalkableFloorZ(Input.WalkableFloorZ);
			return Hit.ImpactNormal.Z > UE_KINDA_SMALL_NUMBER && Hit.ImpactNormal.Z >= FloorZ;
		}

		bool Down(FHitResult& Hit, const FVector& XY)
		{
			return Trace(Hit, FVector(XY.X, XY.Y, Input.Feet.Z + Settings.MaxHeight + Settings.Clearance),
				FVector(XY.X, XY.Y, Input.Feet.Z - Settings.MaxLandingDrop - Settings.Clearance));
		}

		/** 斜坡支持点应与中心切平面比较，不能把平面自然升高误判成台面不连续。 */
		static double PlaneHeight(const FGinkgoTraversalContact& Plane, const FVector& Point)
		{
			const FVector Offset(Point.X - Plane.Position.X, Point.Y - Plane.Position.Y, 0.);
			return Plane.Position.Z - FVector::DotProduct(Offset, Plane.Normal) / FMath::Max(.01, Plane.Normal.Z);
		}

		bool OnTop(const FHitResult& Hit) const
		{
			return Walkable(Hit) && FMath::Abs(Hit.ImpactPoint.Z - PlaneHeight(Result.Ledge, Hit.ImpactPoint)) <= Settings.HeightTolerance;
		}

		/** 中心及内外两圈八向支撑均需存在，避免只检查边缘而跨过内部空缝；空间被占不等于墙体太薄。 */
		bool StandAt(const FVector& XY, FGinkgoTraversalContact& Landing, const bool bOnTop, bool& bSpaceBlocked)
		{
			FHitResult Center;
			if (!Down(Center, XY) || !Walkable(Center) || (bOnTop && !OnTop(Center))) { return false; }
			if (!bOnTop && (Center.ImpactPoint.Z < Input.Feet.Z - Settings.MaxLandingDrop
				|| Center.ImpactPoint.Z > Input.Feet.Z + Input.StepHeight)) { return false; }
			const auto CenterContact = Contact(Center);
			const float Footprint = Input.Radius + Settings.SupportMargin;
			for (const float RingScale : {.5f, 1.f})
			{
				for (int32 Index = 0; Index < 8; ++Index)
				{
					const double Angle = Index * UE_PI / 4.;
					const FVector Point = Center.ImpactPoint + (Input.Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle)) * (Footprint * RingScale);
					FHitResult Support;
					if (!Down(Support, Point) || !Walkable(Support)
						|| FMath::Abs(Support.ImpactPoint.Z - PlaneHeight(CenterContact, Point)) > Settings.HeightTolerance) { return false; }
				}
			}
			const FVector CapsuleCenter = Center.ImpactPoint + FVector::UpVector * (Input.StandingHalfHeight + Settings.Clearance);
			if (BodyBlocked(CapsuleCenter, CapsuleCenter, Input.StandingHalfHeight)) { bSpaceBlocked = true; return false; }
			Landing = CenterContact;
			return true;
		}

		/** 只作保守几何预检：原地抬升、越过边沿、落下；动画接入后必须补做实际 warped 根轨迹检查。 */
		bool PathClear()
		{
			// 抓点不是倾斜薄板的最高点。沿本次确实经过的顶面切平面估算上缘，抬升高度同时覆盖
			// 抓点、顶部出口与落点；不把平面无限延长到墙后的地面，也不抬到落点以外的平台尽头。
			const double AcrossTop = FMath::Clamp(FVector::DotProduct(Result.Landing.Position - Result.Ledge.Position, Input.Forward),
				0., FMath::Max(0., Result.Depth - Settings.HandInset));
			const double TopExitZ = PlaneHeight(Result.Ledge, Result.Ledge.Position + Input.Forward * AcrossTop);
			const double LiftZ = FMath::Max3(Result.Ledge.Position.Z, TopExitZ, Result.Landing.Position.Z)
				+ Settings.PathClearance + Input.StandingHalfHeight;
			Result.Path = {Input.Feet + FVector::UpVector * (Input.StandingHalfHeight + Settings.Clearance),
				FVector(Input.Feet.X, Input.Feet.Y, LiftZ),
				FVector(Result.Landing.Position.X, Result.Landing.Position.Y, LiftZ),
				Result.Landing.Position + FVector::UpVector * (Input.StandingHalfHeight + Settings.Clearance)};
			for (int32 Index = 0; Index < Result.Path.Num(); ++Index)
			{
				if (BodyBlocked(Result.Path[Index], Result.Path[Index], Input.StandingHalfHeight)) { return false; }
				if (Index > 0 && BodyBlocked(Result.Path[Index - 1], Result.Path[Index], Input.StandingHalfHeight)) { return false; }
			}
			return true;
		}

		FGinkgoTraversalResult Fail(const EGinkgoTraversalFailure Failure)
		{
			Result.Kind = EGinkgoTraversalKind::None;
			Result.Failure = Failure;
			return Result;
		}

		FGinkgoTraversalResult Run()
		{
			const FVector Start = Input.Feet + FVector::UpVector * (Input.CurrentHalfHeight + Settings.Clearance);
			if (BodyBlocked(Start, Start, Input.CurrentHalfHeight)) { return Fail(EGinkgoTraversalFailure::InitialOverlap); }
			FHitResult Wall;
			bool bWall = false;
			double Nearest = TNumericLimits<double>::Max();
			const double Reach = Input.Radius + Settings.Reach;
			for (float Height = FMath::Max(Input.StepHeight + 1.f, Settings.ProbeRadius); Height <= Settings.MaxHeight; Height += Settings.HeightStep)
			{
				FHitResult Hit;
				const FVector Origin = Input.Feet + FVector::UpVector * Height;
				if (!Trace(Hit, Origin, Origin + Input.Forward * Reach, Settings.ProbeRadius) || Hit.bStartPenetrating) { continue; }
				if (FMath::Abs(Hit.ImpactNormal.Z) > .5) { continue; }
				const double Distance = FVector::DotProduct(Hit.ImpactPoint - Input.Feet, Input.Forward);
				if (Distance > Reach + .1) { continue; }
				if (Distance < Nearest) { Nearest = Distance; Wall = Hit; bWall = true; }
			}
			if (!bWall) { return Fail(EGinkgoTraversalFailure::NoWall); }
			if (!SupportsTraversal(Wall)) { return Fail(EGinkgoTraversalFailure::UnsupportedSurface); }
			if (FVector::DotProduct(-Wall.ImpactNormal.GetSafeNormal2D(), Input.Forward) < FMath::Cos(FMath::DegreesToRadians(Settings.MaxWallAngle)))
			{
				return Fail(EGinkgoTraversalFailure::ObliqueWall);
			}
			const FVector Front = FVector(Wall.ImpactPoint.X, Wall.ImpactPoint.Y, Input.Feet.Z);
			Result.WallDistance = Nearest;
			FHitResult Top;
			if (!Down(Top, Front + Input.Forward * Settings.HandInset) || !Walkable(Top)) { return Fail(EGinkgoTraversalFailure::NoTop); }
			Result.Ledge = Contact(Top);
			Result.Height = Top.ImpactPoint.Z - Input.Feet.Z;
			if (Result.Height < FMath::Max(Settings.MinHeight, Input.StepHeight + 1.f) || Result.Height > Settings.MaxHeight)
			{
				return Fail(EGinkgoTraversalFailure::Height);
			}
			const auto FindHand = [&](const int32 Side)
			{
				// 初始球可能只擦到下层横条的棱边，不能沿这个高度继续做零半径射线。
				// 改在实际抓取顶面下方 1 cm 找前沿，左右手各按顶面切平面求高度，也避免抓到下层横条。
				const FVector HandOffset = Right * (Side * Settings.HandHalfSpan);
				FVector Origin = Input.Feet + HandOffset;
				Origin.Z = PlaneHeight(Result.Ledge, Front + HandOffset) - 1.;
				FHitResult Face;
				if (!Trace(Face, Origin, Origin + Input.Forward * Reach) || !SupportsTraversal(Face)
					|| FVector::DotProduct(Face.ImpactNormal, Wall.ImpactNormal) < .8
					|| FMath::Abs(FVector::DotProduct(Face.ImpactPoint - Wall.ImpactPoint, Input.Forward)) > Settings.HandDepthTolerance)
				{
					return false;
				}
				FHitResult Hand;
				if (!Down(Hand, Face.ImpactPoint + Input.Forward * Settings.HandInset) || !OnTop(Hand)) { return false; }
				// 手掌接触前沿以前也不能穿过悬檐；最后一点进入支撑面的接触不作穿透处理。
				FHitResult Access;
				const FVector HandClear = Hand.ImpactPoint + FVector::UpVector * Settings.Clearance;
				const FVector Near = FVector(Input.Feet.X, Input.Feet.Y, HandClear.Z) + Right * (Side * Settings.HandHalfSpan);
				if (Trace(Access, Near, HandClear)) { return false; }
				(Side < 0 ? Result.LeftHand : Result.RightHand) = Contact(Hand);
				return true;
			};
			if (!FindHand(-1)) { return Fail(EGinkgoTraversalFailure::Hands); }
			const bool bRightSupported = FindHand(1);
			if (Result.Height > Settings.LowHeight && !bRightSupported) { return Fail(EGinkgoTraversalFailure::Hands); }

			// 顶面间断必须是下降或没有命中；向上出现的第二件障碍不会被当成后缘。
			double LastDepth = Settings.HandInset;
			Result.Depth = Settings.MaxDepth;
			for (double Depth = Settings.HandInset + Settings.ScanStep; Depth <= Settings.MaxDepth; Depth += Settings.ScanStep)
			{
				FHitResult Hit;
				const FVector Point = Front + Input.Forward * Depth;
				const bool bHit = Down(Hit, Point);
				if (bHit && OnTop(Hit)) { LastDepth = Depth; continue; }
				// 倾斜薄板的背面可能仍在高度容差内，却已不再可站。朝前且低于顶面延长线的面是
				// 向下离开的后缘；前方抬高的台阶、迎面障碍和被禁止的支撑仍必须停止搜索。
				const double TopHeight = PlaneHeight(Result.Ledge, Point);
				const bool bDescendingBackFace = bHit && !Hit.bStartPenetrating && SupportsTraversal(Hit)
					&& Hit.ImpactNormal.Z >= 0. && FVector::DotProduct(Hit.ImpactNormal, Input.Forward) > .5
					&& Hit.ImpactPoint.Z < TopHeight - UE_KINDA_SMALL_NUMBER;
				if (bHit && !bDescendingBackFace && Hit.ImpactPoint.Z >= TopHeight - Settings.HeightTolerance)
				{
					Result.Depth = LastDepth;
					break;
				}
				double Outside = Depth;
				for (int32 Step = 0; Step < 6; ++Step)
				{
					const double Mid = (LastDepth + Outside) * .5;
					FHitResult Refine;
					if (Down(Refine, Front + Input.Forward * Mid) && OnTop(Refine)) { LastDepth = Mid; }
					else { Outside = Mid; }
				}
				Result.Depth = LastDepth;
				Result.bFoundBackEdge = true;
				break;
			}

			bool bTopBlocked = false;
			const double Footprint = Input.Radius + Settings.SupportMargin;
			for (double Depth = Footprint + Settings.Clearance; Depth + Footprint <= Result.Depth; Depth += Settings.ScanStep)
			{
				if (StandAt(Front + Input.Forward * Depth, Result.Landing, true, bTopBlocked))
				{
					if (!PathClear()) { return Fail(EGinkgoTraversalFailure::PathBlocked); }
					Result.Kind = Result.Height <= Settings.LowHeight ? EGinkgoTraversalKind::Mantle : EGinkgoTraversalKind::Climb;
					Result.Failure = EGinkgoTraversalFailure::None;
					return Result;
				}
			}
			if (bTopBlocked) { return Fail(EGinkgoTraversalFailure::TopBlocked); }
			if (!Result.bFoundBackEdge || Result.Depth > Settings.MaxVaultDepth || Result.Height > Settings.MaxVaultHeight)
			{
				return Fail(EGinkgoTraversalFailure::NoSupport);
			}
			// 背后只尝试紧邻后缘的一处落点，不跨过第二堵墙、深坑去找远处地面。
			bool bLandingBlocked = false;
			if (!StandAt(Front + Input.Forward * (Result.Depth + Footprint + Settings.Clearance + 1.f), Result.Landing, false, bLandingBlocked))
			{
				return Fail(EGinkgoTraversalFailure::NoLanding);
			}
			if (!PathClear()) { return Fail(EGinkgoTraversalFailure::PathBlocked); }
			Result.Kind = EGinkgoTraversalKind::Vault;
			Result.Failure = EGinkgoTraversalFailure::None;
			return Result;
		}
	};
}

FGinkgoTraversalResult Ginkgo::Traversal::Detect(const UWorld& World, const FGinkgoTraversalQuery& Query,
	const FGinkgoTraversalSettings& Settings, TArray<FGinkgoTraversalProbe>* Probes)
{
	if (Probes) { Probes->Reset(); }
	const bool bDimensionsValid = FMath::IsFinite(Query.Radius) && FMath::IsFinite(Query.CurrentHalfHeight) && FMath::IsFinite(Query.StandingHalfHeight)
		&& Query.Radius >= 1.f && Query.Radius <= 150.f && Query.CurrentHalfHeight >= Query.Radius
		&& Query.StandingHalfHeight >= Query.CurrentHalfHeight && Query.StandingHalfHeight <= 300.f
		&& FMath::IsFinite(Query.StepHeight) && Query.StepHeight >= 0.f && Query.StepHeight < Settings.MaxHeight
		&& FMath::IsFinite(Query.WalkableFloorZ) && Query.WalkableFloorZ >= 0.f && Query.WalkableFloorZ <= 1.f;
	if (!Settings.IsValid() || !bDimensionsValid || Query.Feet.ContainsNaN() || Query.Forward.ContainsNaN()
		|| !FMath::IsNearlyZero(Query.Forward.Z) || !FMath::IsNearlyEqual(Query.Forward.SizeSquared(), 1., .001) || Query.BodyChannel >= ECC_MAX)
	{
		FGinkgoTraversalResult Invalid;
		Invalid.Failure = EGinkgoTraversalFailure::InvalidInput;
		return Invalid;
	}
	GinkgoTraversalDetection::FDetector Detector(World, Query, Settings, Probes);
	FGinkgoTraversalResult Result = Detector.Run();
	if (Result.QueryCount > GinkgoTraversalDetection::QueryLimit)
	{
		Result.Kind = EGinkgoTraversalKind::None;
		Result.Failure = EGinkgoTraversalFailure::InvalidInput;
	}
	return Result;
}

const TCHAR* Ginkgo::Traversal::Describe(const EGinkgoTraversalKind Kind)
{
	switch (Kind)
	{
	case EGinkgoTraversalKind::Vault: return TEXT("Vault");
	case EGinkgoTraversalKind::Mantle: return TEXT("Low mantle");
	case EGinkgoTraversalKind::Climb: return TEXT("High climb");
	default: return TEXT("None");
	}
}

const TCHAR* Ginkgo::Traversal::Describe(const EGinkgoTraversalFailure Failure)
{
	switch (Failure)
	{
	case EGinkgoTraversalFailure::None: return TEXT("Ready");
	case EGinkgoTraversalFailure::InvalidInput: return TEXT("Invalid query/config or query budget exceeded");
	case EGinkgoTraversalFailure::InvalidState: return TEXT("Grounded, alive, outside workbench required");
	case EGinkgoTraversalFailure::NoWall: return TEXT("No wall above step height");
	case EGinkgoTraversalFailure::InitialOverlap: return TEXT("Starting capsule overlaps geometry");
	case EGinkgoTraversalFailure::UnsupportedSurface: return TEXT("Moving/physics/forbidden support");
	case EGinkgoTraversalFailure::ObliqueWall: return TEXT("Camera direction too oblique to wall");
	case EGinkgoTraversalFailure::NoTop: return TEXT("No reachable walkable top");
	case EGinkgoTraversalFailure::Height: return TEXT("Ledge height outside configured range");
	case EGinkgoTraversalFailure::Hands: return TEXT("Required hand support missing or inaccessible");
	case EGinkgoTraversalFailure::TopBlocked: return TEXT("Top supports feet but standing capsule is blocked");
	case EGinkgoTraversalFailure::NoSupport: return TEXT("No standing footprint; not a supported thin vault");
	case EGinkgoTraversalFailure::NoLanding: return TEXT("No safe landing immediately behind obstacle");
	case EGinkgoTraversalFailure::PathBlocked: return TEXT("Conservative capsule path blocked");
	default: return TEXT("Unknown");
	}
}

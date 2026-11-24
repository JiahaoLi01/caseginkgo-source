#include "Animation/RigUnit_GinkgoLegIK.h"

#include "Animation/AnimTypes.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Camera/GinkgoSpring.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TwoBoneIK.h"
#include "Units/RigUnitContext.h"

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarLegIK(
		TEXT("ginkgo.anim.legIK"),
		1,
		TEXT("腿部 IK 的锁脚：1 开，0 关（不加修正，锁与预测照算，只用来对照测量脚滑）。"));

	TAutoConsoleVariable<int32> CVarLegIKTerrain(
		TEXT("ginkgo.anim.legIKTerrain"),
		1,
		TEXT("腿部 IK 的地形：1 开，0 关（脚照动画的高度、骨盆不动，只剩平地上的锁脚；只用来对照测量）。"));

	TAutoConsoleVariable<int32> CVarLegIKFold(
		TEXT("ginkgo.anim.legIKFold"),
		1,
		TEXT("腿部 IK 不让腿折断：1 开，0 关（脚的目标不往下压、骨盆不为它抬、够不着的每只脚都往下放，膝盖直接朝动画里膝盖的位置弯；只用来对照）。"));

	TAutoConsoleVariable<int32> CVarLegIKSettle(
		TEXT("ginkgo.anim.legIKSettle"),
		1,
		TEXT("腿部 IK 着地时等动画的脚掌慢下来再锁：1 开，0 关（接触曲线一到就锁，原来的做法；只用来对照）。"));
#endif

	/** ginkgo.anim.legIK：锁脚开没开（发行版里总是开） */
	bool IsLegIKEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarLegIK.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	/** ginkgo.anim.legIKTerrain：地形开没开（发行版里总是开） */
	bool IsTerrainEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarLegIKTerrain.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	/** ginkgo.anim.legIKFold：不让腿折断开没开（发行版里总是开） */
	bool IsFoldEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarLegIKFold.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	/** ginkgo.anim.legIKSettle：着地时等动画的脚掌慢下来再锁开没开（发行版里总是开） */
	bool IsSettleEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarLegIKSettle.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	/** 预测落脚时根骨骼的轨迹分几段积分：速度按它的转速边转边走 */
	constexpr int32 PredictionSteps = 8;

	/** 水平速度低于它时方向不可信，不算速度的转速（cm/s） */
	constexpr double MinTurnSpeed = 50.0;

	/** "还有多久落下"往下走的速率的下限（片段时间 / 真实时间）：循环最慢按 0.75 倍放 */
	constexpr float MinLandRate = 0.25f;

	/** 修正小于它（cm）不求解 */
	constexpr double MinCorrection = 0.01;

	/**
	 * 着地以后动画的脚掌每秒至少慢这么多（cm/s²）才算还在落（Settling 接着等）：混合把脚扫过去时七百多 cm/s 在约 0.1 s 里减到 0（五千到七千），
	 * 动画自己拖着着地的脚滑（冲刺转身时网格绕胶囊转、起步的距离匹配）速度不怎么变或在变快，马上锁。一帧要比上一帧慢过它乘 Δt：按每秒算，各帧率一样
	 */
	constexpr double LandingDeceleration = 1000.0;

	/** 摆动的路径上最多往下扫几次（不算两端） */
	constexpr int32 MaxPathSamples = 8;

	/** 查脚下的地面走不走得到（高台的边）时，从胶囊底到脚最多往下打几条线 */
	constexpr int32 MaxReachSamples = 24;

	/** 上楼梯预测落脚点超出发射高度时，沿水平方向逐级探高的采样步长（cm），小于常见楼梯踏面深度以便逐级抬高起点 */
	constexpr double ClimbSpacing = 25.0;

	/** 上楼梯逐级探高最多采样次数（上限覆盖约 4 m 水平预测距离） */
	constexpr int32 MaxClimbSamples = 16;

	/** 摆动的脚走到路径两头以外这么远（cm）就不按路径，照脚下扫 */
	constexpr double PathOutside = 5.0;

	/** 另一只脚的着地点离摆动的路径（两头的连线）横着不超过这么远（cm），路径才在它那里分段：两只脚左右差二三十厘米，转弯时差得多些 */
	constexpr double SplitSideDistance = 60.0;

	/**
	 * 照脚下扫的地面高度的前馈最多按这个坡度（高 / 水平走过）跟：坡上连续地变的不落后；台阶一下子高一级的超出这个坡度，缓过去
	 */
	constexpr double MaxSupportSlope = 1.0;

	/** 软 IK 在离地以后多久（s）升满、着地以后多久降回 0 */
	constexpr float SoftBlendTime = 0.1f;

	/** 法线的 Z 低于它（约 72°）算墙：不当地面倾斜脚，也不拿它推别处的高度 */
	constexpr double MinGroundNormalZ = 0.3;

	/** 清掉锁、预测与地形：第一次执行、隔了几帧没执行（权重为 0 时整个跳过）、瞬移 */
	void ResetFeet(FRigUnit_GinkgoLegIK_WorkData& WorkData)
	{
		for (FRigUnit_GinkgoLegIK_FootState& State : WorkData.Feet)
		{
			State = FRigUnit_GinkgoLegIK_FootState();
		}
		WorkData.bHasRoot = false;
		WorkData.RootVelocity = FVector::ZeroVector;
		WorkData.VelocityTurnRate = 0.f;
		WorkData.RootYawRate = 0.f;
		WorkData.PelvisBase = FRigUnit_GinkgoLegIK_Track();
		WorkData.PelvisAdjust = 0.0;
		WorkData.PelvisAdjustVelocity = 0.0;
		WorkData.PelvisOffset = 0.0;
	}

	/**
	 * 追目标：临界阻尼弹簧作用在与目标之差上，前馈目标自己的速率 Rate。误差从这一帧开始时的目标量起（目标这一帧走了 Rate × Δt），
	 * 跳变整个留在误差里、按 ω 缓过去；返回追到的值
	 */
	double Track(FRigUnit_GinkgoLegIK_Track& State, const double Target, const double Rate, const double Omega, const float DeltaTime)
	{
		if (!State.bActive)
		{
			State.bActive = true;
			State.Value = Target;
			State.Velocity = Rate;
			return Target;
		}
		double Error = State.Value - (Target - Rate * DeltaTime);
		double ErrorVelocity = State.Velocity - Rate;
		Ginkgo::Spring::StepCritical(Error, ErrorVelocity, Omega, DeltaTime);
		State.Value = Target + Error;
		State.Velocity = Rate + ErrorVelocity;
		return State.Value;
	}

	/** 根骨骼这一帧在世界里的变换，差分出速度、速度的转向与偏航速率 */
	void TrackRoot(FRigUnit_GinkgoLegIK_WorkData& WorkData, const FTransform& RootWorld, const float DeltaTime)
	{
		if (WorkData.bHasRoot && DeltaTime > 0.f)
		{
			const FVector Displacement = RootWorld.GetLocation() - WorkData.PreviousRoot.GetLocation();
			const FVector Velocity(Displacement.X / DeltaTime, Displacement.Y / DeltaTime, 0.0);
			// 速度方向的转速：前后两帧水平速度的夹角
			const bool bReliable = Velocity.SizeSquared() > FMath::Square(MinTurnSpeed) && WorkData.RootVelocity.SizeSquared() > FMath::Square(MinTurnSpeed);
			WorkData.VelocityTurnRate = bReliable
				? static_cast<float>(FMath::FindDeltaAngleDegrees(WorkData.RootVelocity.Rotation().Yaw, Velocity.Rotation().Yaw) / DeltaTime)
				: 0.f;
			WorkData.RootVelocity = Velocity;
			WorkData.RootYawRate = static_cast<float>(FMath::FindDeltaAngleDegrees(WorkData.PreviousRoot.Rotator().Yaw, RootWorld.Rotator().Yaw) / DeltaTime);
		}
		WorkData.PreviousRoot = RootWorld;
		WorkData.bHasRoot = true;
	}

	/** 按根骨骼的运动外推 Time 秒以后，落脚在根骨骼空间里的 Land 落在世界里的哪里 */
	FVector PredictLanding(const FRigUnit_GinkgoLegIK_WorkData& WorkData, const FTransform& RootWorld, const FVector& Land, const float Time)
	{
		FVector Location = RootWorld.GetLocation();
		FVector Velocity = WorkData.RootVelocity;
		const float Step = Time / PredictionSteps;
		for (int32 Index = 0; Index < PredictionSteps; ++Index)
		{
			// 每一段按段中间的方向走（先转半段、走、再转半段），转弯的弧走得准
			Velocity = Velocity.RotateAngleAxis(WorkData.VelocityTurnRate * Step * 0.5f, FVector::UpVector);
			Location += Velocity * Step;
			Velocity = Velocity.RotateAngleAxis(WorkData.VelocityTurnRate * Step * 0.5f, FVector::UpVector);
		}
		const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(WorkData.RootYawRate * Time));
		return FTransform(Turn * RootWorld.GetRotation(), Location).TransformPosition(Land);
	}

	float GetCurve(const URigHierarchy& Hierarchy, const FName Name)
	{
		return Name.IsNone() ? 0.f : Hierarchy.GetCurveValue(FRigElementKey(Name, ERigElementType::Curve));
	}

	void BeginRelease(FRigUnit_GinkgoLegIK_FootState& State)
	{
		State.Lock = EGinkgoLegIKLock::Releasing;
		State.ReleaseFrom = State.Correction;
		State.ReleaseElapsed = 0.f;
	}

	// ===== 地形 =====

	/**
	 * 往下扫地面：默认竖直地从根骨骼以上 TraceUp 扫到以下 TraceDown，并支持按局部基准高度逐级抬高起点（上多级楼梯不撞穿透也不误打天花板）。
	 * 忽略角色自己，只碰世界里的东西（WorldStatic 与 WorldDynamic）：引擎的 SphereTraceWorld 只碰 WorldStatic，
	 * 可平地模板的地板（BlockAllDynamic）是 WorldDynamic，照它会扫不到；别的角色（Pawn）、物理物件不算地面。
	 * 工作线程上跑（动画求值），引擎的场景查询可以这样用
	 */
	struct FGroundProbe
	{
		/** 当前动画求值所属的世界，由 ExecuteContext 传入 */
		const UWorld* World = nullptr;

		/** 射线与球体扫描通用查询参数（忽略角色自身） */
		FCollisionQueryParams Params;

		/** 仅阻挡 WorldStatic 与 WorldDynamic 的碰撞响应表 */
		FCollisionResponseParams Response;

		/** 相对基准高度往上抬起的探测裕量（cm，即 TraceUp） */
		double Up = 0.0;

		/** 相对基准高度往下探底的探测深度（cm，即 TraceDown） */
		double Down = 0.0;

		/** 默认探测顶面世界 Z（cm，等于 RootZ + TraceUp） */
		double Top = 0.0;

		/** 默认探测底面世界 Z（cm，等于 RootZ - TraceDown） */
		double Bottom = 0.0;

		/** 本帧累计执行的场景查询次数，供调试面板统计 */
		int32 Traces = 0;

		FGroundProbe(const UWorld* InWorld, const AActor* Owner, const double RootZ, const double InUp, const double InDown)
			: World(InWorld), Params(SCENE_QUERY_STAT(GinkgoLegIKGround), false, Owner), Response(ECR_Ignore), Up(InUp), Down(InDown), Top(RootZ + InUp), Bottom(RootZ - InDown)
		{
			Response.CollisionResponse.SetResponse(ECC_WorldStatic, ECR_Block);
			Response.CollisionResponse.SetResponse(ECC_WorldDynamic, ECR_Block);
		}

		/** 在指定的世界高度区间 [TraceBottom, TraceTop] 内竖直打一条线，返回正下方命中的高度与法线；起点穿透时视为失败 */
		bool LineAt(const FVector2D& XY, const double TraceTop, const double TraceBottom, double& OutZ, FVector& OutNormal)
		{
			FHitResult Hit;
			++Traces;
			if (!World->LineTraceSingleByChannel(Hit, FVector(XY, TraceTop), FVector(XY, TraceBottom), ECC_Visibility, Params, Response) || Hit.bStartPenetrating)
			{
				return false;
			}
			OutZ = Hit.ImpactPoint.Z;
			OutNormal = Hit.ImpactNormal;
			return true;
		}

		/** 一条竖直的线：使用默认高度区间，返回正下方那个面的高度与法线 */
		bool Line(const FVector2D& XY, double& OutZ, FVector& OutNormal)
		{
			return LineAt(XY, Top, Bottom, OutZ, OutNormal);
		}

		/** 在指定的世界高度区间 [TraceBottom, TraceTop] 内竖直下落小球，返回半径以内最高命中的世界坐标；起点穿透时视为失败 */
		bool SphereAt(const FVector2D& XY, const double TraceTop, const double TraceBottom, const double Radius, FVector& OutPoint)
		{
			FHitResult Hit;
			++Traces;
			if (!World->SweepSingleByChannel(Hit, FVector(XY, TraceTop), FVector(XY, TraceBottom), FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(Radius), Params,
				Response) || Hit.bStartPenetrating)
			{
				return false;
			}
			OutPoint = Hit.ImpactPoint;
			return true;
		}

		/** 一个竖直下落的小球：使用默认高度区间，返回半径以内最高的那一点（台阶的边落在两条线之间也碰得到） */
		bool Sphere(const FVector2D& XY, const double Radius, FVector& OutPoint)
		{
			return SphereAt(XY, Top, Bottom, Radius, OutPoint);
		}

		/**
		 * 在指定的世界高度区间内探测一只脚的支撑面：脚跟（脚踝正下方）、脚掌、脚尖各打一条线。
		 * 脚掌命中后，脚跟与脚尖的探测顶面按 Max(TraceTop, OutZ + Up) 抬高，避免脚掌刚跨上一级时脚尖落在更高一级而起点穿透。
		 * 高度、法线取脚掌正下方那个面（坡上三处都在这个面上）；脚跟、脚尖下面的地面高出"这个面延伸过去"多少（踩在台阶边上，正数）另给出，
		 * 着地的脚按它俯仰。两头都高出（脚掌踩在坑里）时脚架在两头上：先抬起两者里小的那个
		 */
		bool SupportAt(const FVector2D& Ball, const FVector2D& Heel, const FVector2D& Toe, const double TraceTop, const double TraceBottom,
			double& OutZ, FVector& OutNormal, double& OutHeelExcess, double& OutToeExcess)
		{
			OutHeelExcess = OutToeExcess = 0.0;
			if (!LineAt(Ball, TraceTop, TraceBottom, OutZ, OutNormal))
			{
				return false;
			}
			OutNormal = OutNormal.Z >= MinGroundNormalZ ? OutNormal : FVector::UpVector;
			const double EdgeTop = FMath::Max(TraceTop, OutZ + Up);
			const auto ExcessAt = [this, &Ball, &OutZ, &OutNormal, EdgeTop, TraceBottom](const FVector2D& Point)
			{
				double Z;
				FVector Normal;
				return LineAt(Point, EdgeTop, TraceBottom, Z, Normal)
					? Z - (OutZ - (OutNormal.X * (Point.X - Ball.X) + OutNormal.Y * (Point.Y - Ball.Y)) / OutNormal.Z)
					: 0.0;
			};
			OutHeelExcess = ExcessAt(Heel);
			OutToeExcess = ExcessAt(Toe);
			const double Bridge = FMath::Max(FMath::Min(OutHeelExcess, OutToeExcess), 0.0);
			OutZ += Bridge;
			OutHeelExcess -= Bridge;
			OutToeExcess -= Bridge;
			return true;
		}

		/** 使用默认高度区间探测一只脚的支撑面 */
		bool Support(const FVector2D& Ball, const FVector2D& Heel, const FVector2D& Toe, double& OutZ, FVector& OutNormal, double& OutHeelExcess, double& OutToeExcess)
		{
			return SupportAt(Ball, Heel, Toe, Top, Bottom, OutZ, OutNormal, OutHeelExcess, OutToeExcess);
		}

		/**
		 * 沿水平线段从 (From, FromZ) 朝 To 逐级抬高地面基准高度：每次相对当前地面向上 Up 向下 Down 打线，
		 * 遇到无地面、起点穿透、陡墙（Normal.Z < MinGroundNormalZ）或超过 MaxDrop 的断崖立即停止，返回沿途可行走表面到达的最后高度（cm）。
		 * 用于预测落脚点在上方多级楼梯上时，避免直接从 RootZ + TraceUp 打线在实心台阶内起点穿透，同时保证不会爬过墙体或误打天花板
		 */
		double ClimbTop(const FVector2D& From, const double FromZ, const FVector2D& To, const double MaxDrop, const double Spacing)
		{
			const FVector2D Delta = To - From;
			const double Distance = Delta.Size();
			if (Distance <= 1.0)
			{
				return FromZ;
			}
			const int32 Count = FMath::Clamp(FMath::CeilToInt(Distance / FMath::Max(Spacing, 1.0)), 1, MaxClimbSamples);
			double CurrentZ = FromZ;
			for (int32 Index = 1; Index < Count; ++Index)
			{
				double Z = CurrentZ;
				FVector Normal;
				if (!LineAt(From + Delta * (static_cast<double>(Index) / Count), CurrentZ + Up, CurrentZ - Down, Z, Normal))
				{
					break;
				}
				if (Normal.Z < MinGroundNormalZ || CurrentZ - Z > MaxDrop)
				{
					break;
				}
				CurrentZ = Z;
			}
			return CurrentZ;
		}

		/**
		 * 从胶囊底（From，高度 FromZ）走到 To 的地面（ToZ）走不走得到：沿途按 Spacing 往下打线，相邻两处落下超过 MaxDrop 的是高台的边
		 * （扫不到地面的也算），过了边的走不到（胶囊会掉下去），返回边上那一侧最后的高度；走得到时返回 ToZ。
		 * 没比胶囊底低过 MaxDrop 的不用查；楼梯一级一级下去，每段落差小，走得到
		 */
		double Reachable(const FVector2D& From, const double FromZ, const FVector2D& To, const double ToZ, const double MaxDrop, const double Spacing)
		{
			if (ToZ >= FromZ - MaxDrop)
			{
				return ToZ;
			}
			const FVector2D Delta = To - From;
			const int32 Count = FMath::Clamp(FMath::CeilToInt(Delta.Size() / FMath::Max(Spacing, 1.0)), 1, MaxReachSamples);
			double Previous = FromZ;
			for (int32 Index = 1; Index <= Count; ++Index)
			{
				double Z = ToZ;
				FVector Normal;
				if (Index < Count && !Line(From + Delta * (static_cast<double>(Index) / Count), Z, Normal))
				{
					return Previous;
				}
				if (Previous - Z > MaxDrop)
				{
					return Previous;
				}
				Previous = Z;
			}
			return ToZ;
		}
	};

	/** 摆动的路径：上凸包的拐点（沿路径的距离, 高度），按距离排好 */
	using FPathPoints = TArray<FVector2D, TInlineAllocator<MaxPathSamples + 2>>;

	/** 按距离排好的点只留上凸包（单调链）：路径从台阶的边上越过去，不从两级之间的连线里穿过去 */
	void KeepUpperHull(FPathPoints& Points)
	{
		FPathPoints Hull;
		for (const FVector2D& Point : Points)
		{
			while (Hull.Num() >= 2)
			{
				const FVector2D& A = Hull[Hull.Num() - 2];
				const FVector2D& B = Hull[Hull.Num() - 1];
				if ((B.X - A.X) * (Point.Y - A.Y) - (B.Y - A.Y) * (Point.X - A.X) < 0.0)
				{
					break;
				}
				Hull.Pop(EAllowShrinking::No);
			}
			Hull.Add(Point);
		}
		Points = MoveTemp(Hull);
	}

	/**
	 * 路径在另一只脚的着地点处分段（育碧的"虚拟地面"）：两段各自取上凸包，路径经过那一点。
	 * 下楼时摆动的脚跟着台阶往下走，不贴着出发点到落脚点的弦（跑、冲刺一步跨好几级，弦比台阶高出几十厘米）；
	 * 上楼时经过支撑脚旁边的那一段贴着它踩的踏面，不贴台阶前沿连成的线
	 */
	void KeepUpperHullThrough(FPathPoints& Points, const FVector2D& Split)
	{
		FPathPoints Before;
		FPathPoints After;
		for (const FVector2D& Point : Points)
		{
			if (Point.X < Split.X)
			{
				Before.Add(Point);
			}
			else if (Point.X > Split.X)
			{
				After.Add(Point);
			}
		}
		Before.Add(Split);
		After.Insert(Split, 0);
		KeepUpperHull(Before);
		KeepUpperHull(After);
		Points = MoveTemp(Before);
		Points.Append(After.GetData() + 1, After.Num() - 1);
	}

	/** 路径在 Distance 处的高度（两端以外取端点） */
	double EvaluatePath(const FPathPoints& Points, const double Distance)
	{
		if (Distance <= Points[0].X)
		{
			return Points[0].Y;
		}
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			if (Distance <= Points[Index].X)
			{
				const double Span = Points[Index].X - Points[Index - 1].X;
				return Span > UE_KINDA_SMALL_NUMBER ? FMath::Lerp(Points[Index - 1].Y, Points[Index].Y, (Distance - Points[Index - 1].X) / Span) : Points[Index].Y;
			}
		}
		return Points.Last().Y;
	}

	/** 旋转的角度夹在 LimitDegrees 以内（同一根轴） */
	FQuat LimitAngle(const FQuat& Rotation, const double LimitDegrees)
	{
		const double Angle = Rotation.GetAngle();
		const double Limit = FMath::DegreesToRadians(LimitDegrees);
		return Angle <= Limit || Angle < UE_KINDA_SMALL_NUMBER ? Rotation : FQuat::Slerp(FQuat::Identity, Rotation, Limit / Angle);
	}

	/** 脚按地面的法线倾斜：竖直方向转到法线的旋转，角度夹在 LimitDegrees 以内 */
	FQuat TiltToward(const FVector& Normal, const double LimitDegrees)
	{
		return LimitAngle(FQuat::FindBetweenNormals(FVector::UpVector, Normal.GetSafeNormal()), LimitDegrees);
	}

	/**
	 * 踩在台阶边上的脚绕脚掌俯仰：脚尖下面的地面高出 ToeExcess（脚尖抬起，搭在上一级的边上），或脚跟下面高出 HeelExcess（脚跟抬起）。
	 * Forward 是水平的脚的朝向；两者都不过 0.5 cm 时不转
	 */
	FQuat PitchToFit(const FVector& Forward, const double HeelExcess, const double HeelLength, const double ToeExcess, const double ToeLength)
	{
		const double ToeAngle = ToeExcess > 0.5 && ToeLength > 1.0 ? FMath::Atan2(ToeExcess, ToeLength) : 0.0;
		const double HeelAngle = HeelExcess > 0.5 && HeelLength > 1.0 ? FMath::Atan2(HeelExcess, HeelLength) : 0.0;
		// 脚尖抬起：朝向往上转；脚跟抬起：朝向往下转
		const double Pitch = ToeAngle >= HeelAngle ? ToeAngle : -HeelAngle;
		if (FMath::IsNearlyZero(Pitch))
		{
			return FQuat::Identity;
		}
		const FVector Pitched = (Forward * FMath::Cos(Pitch) + FVector::UpVector * FMath::Sin(Pitch)).GetSafeNormal();
		return FQuat::FindBetweenNormals(Forward, Pitched);
	}

	/** 一只脚这一帧要放到哪里：第一遍（锁脚、预测、地形）算好，骨盆挪完以后第二遍求解 */
	struct FFootSolve
	{
		/** 本帧脚部骨骼链是否解析有效（含大腿、小腿、脚踝） */
		bool bValid = false;

		/** 大腿骨骼在 RigHierarchy 中的索引，无效为 INDEX_NONE */
		int32 Thigh = INDEX_NONE;

		/** 小腿骨骼在 RigHierarchy 中的索引，无效为 INDEX_NONE */
		int32 Calf = INDEX_NONE;

		/** 脚踝骨骼在 RigHierarchy 中的索引，无效为 INDEX_NONE */
		int32 Foot = INDEX_NONE;

		/** 跟随脚踝平移旋转的 IK 骨骼索引，无此骨骼为 INDEX_NONE */
		int32 IKFoot = INDEX_NONE;

		/** 动画里的脚踝位置（组件空间，骨盆挪之前，cm） */
		FVector AnimFoot = FVector::ZeroVector;

		/** 脚踝的目标位置（组件空间，cm） */
		FVector FootTarget = FVector::ZeroVector;

		/** 脚掌贴合地形法线与台阶边缘所需的附加旋转（组件空间） */
		FQuat Tilt = FQuat::Identity;

		/** 本帧是否算出了可用于骨盆基准线的地面锚点 */
		bool bHasPelvisAnchor = false;

		/** 骨盆下面那条线的端点（世界空间，地面上，cm）：着地时为着地点，摆动有预测时为预测落脚点，无预测时为当前脚下地面 */
		FVector PelvisAnchor = FVector::ZeroVector;

		/** 骨盆为这只脚维持够得着约束的权重（0–1）：着地为 1，摆动进入 LandingReachTime 内随剩余落地时间平滑升到 1，刚离地为 0 */
		double ReachAlpha = 0.0;

		/** 脚部相对原始动画是否存在有效水平/竖直位移或倾斜变化 */
		bool bChanged = false;
	};
}

FRigUnit_GinkgoLegIK::FRigUnit_GinkgoLegIK()
{
	const auto MakeFoot = [](const TCHAR* Side, const TCHAR* DisableCurve)
	{
		FRigUnit_GinkgoLegIK_Foot Foot;
		Foot.Foot = FRigElementKey(*FString::Printf(TEXT("foot_%s"), Side), ERigElementType::Bone);
		Foot.Ball = FRigElementKey(*FString::Printf(TEXT("ball_%s"), Side), ERigElementType::Bone);
		Foot.IKFoot = FRigElementKey(*FString::Printf(TEXT("ik_foot_%s"), Side), ERigElementType::Bone);
		Foot.ContactCurve = *FString::Printf(TEXT("contact_%s"), Side);
		Foot.LandInCurve = *FString::Printf(TEXT("FootLandIn_%s"), Side);
		Foot.LandXCurve = *FString::Printf(TEXT("FootLandX_%s"), Side);
		Foot.LandYCurve = *FString::Printf(TEXT("FootLandY_%s"), Side);
		Foot.DisableCurve = DisableCurve;
		return Foot;
	};
	Feet = { MakeFoot(TEXT("l"), TEXT("disableleftfootik")), MakeFoot(TEXT("r"), TEXT("disablerightfootik")) };
}

FRigUnit_GinkgoLegIK_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy == nullptr || !FAnimWeight::IsRelevant(Alpha) || !WorkData.CachedRoot.UpdateCache(Root, Hierarchy))
	{
		return;
	}
	if (WorkData.Feet.Num() != Feet.Num())
	{
		WorkData.Feet.SetNum(Feet.Num());
	}

	// 隔了帧（权重为 0 的那些帧整个跳过）或瞬移了：之前的锁与差分都不作数
	const float DeltaTime = ExecuteContext.GetDeltaTime<float>();
	const FTransform& ComponentTransform = ExecuteContext.GetToWorldSpaceTransform();
	const FTransform RootWorld = Hierarchy->GetGlobalTransform(WorkData.CachedRoot) * ComponentTransform;
	const bool bGap = GFrameCounter > WorkData.LastFrame + 1;
	const bool bTeleport = WorkData.bHasRoot && FVector::Dist(RootWorld.GetLocation(), WorkData.PreviousRoot.GetLocation()) > TeleportDistance;
	WorkData.LastFrame = GFrameCounter;
	if (bGap || bTeleport)
	{
		ResetFeet(WorkData);
	}
	TrackRoot(WorkData, RootWorld, DeltaTime);

	const bool bEnabled = IsLegIKEnabled();
	const bool bFold = IsFoldEnabled();
	const bool bSettle = IsSettleEnabled();
	// 动画的地面是根骨骼的高度（片段里根骨骼贴着地面）：脚、骨盆在动画里高出它多少，放到地形上就高出地形多少
	const double RootZ = RootWorld.GetLocation().Z;
	const FVector2D RootXY(RootWorld.GetLocation().X, RootWorld.GetLocation().Y);
	const UWorld* World = ExecuteContext.GetWorld();
	const bool bTerrain = World != nullptr && IsTerrainEnabled();
	FGroundProbe Probe(World, ExecuteContext.GetOwningActor(), RootZ, TraceUp, TraceDown);
	// 脚下的地面在高台的边外（从胶囊底走不到，胶囊卡在边上没掉下去，片段的脚却落到了边外）：脚放在边上那一侧的高度，平着放。
	// 不然 IK 把脚放到一米下面的地上、骨盆为了够着它往下放，整个人沉进台子里
	const auto ClampToLedge = [&Probe, &RootXY, RootZ, MaxStepDown, PathSpacing](const FVector2D& Point, double& Z, FVector& Normal, double& HeelExcess, double& ToeExcess)
	{
		const double Ledge = Probe.Reachable(RootXY, RootZ, Point, Z, MaxStepDown, PathSpacing);
		if (Ledge <= Z)
		{
			return false;
		}
		Z = Ledge;
		Normal = FVector::UpVector;
		HeelExcess = 0.0;
		ToeExcess = 0.0;
		return true;
	};
	TArray<FFootSolve, TInlineAllocator<2>> Solves;
	Solves.SetNum(Feet.Num());
#if !UE_BUILD_SHIPPING
	TArray<UGinkgoAnimInstance::FDebugFrame::FLegIKFoot, TInlineAllocator<2>> Reports;
	Reports.SetNum(Feet.Num());
#endif

	// ===== 第一遍：每只脚的锁、预测与地形 =====
	for (int32 Index = 0; Index < Feet.Num(); ++Index)
	{
		const FRigUnit_GinkgoLegIK_Foot& Foot = Feet[Index];
		FRigUnit_GinkgoLegIK_FootState& State = WorkData.Feet[Index];
		FFootSolve& Solve = Solves[Index];
		if (!State.CachedFoot.UpdateCache(Foot.Foot, Hierarchy) || !State.CachedBall.UpdateCache(Foot.Ball, Hierarchy))
		{
			continue;
		}
		State.CachedIKFoot.UpdateCache(Foot.IKFoot, Hierarchy);
		Solve.Foot = State.CachedFoot.GetIndex();
		Solve.Calf = Hierarchy->GetFirstParent(Solve.Foot);
		Solve.Thigh = Solve.Calf != INDEX_NONE ? Hierarchy->GetFirstParent(Solve.Calf) : INDEX_NONE;
		Solve.IKFoot = State.CachedIKFoot.IsValid() ? State.CachedIKFoot.GetIndex() : INDEX_NONE;
		if (Solve.Thigh == INDEX_NONE)
		{
			continue;
		}
		Solve.bValid = true;

		// 着地：接触曲线的回差；关掉这只脚的曲线高时不算着地
		const float Contact = GetCurve(*Hierarchy, Foot.ContactCurve);
		const bool bDisabled = GetCurve(*Hierarchy, Foot.DisableCurve) > 0.5f;
		const bool bWasPlanted = State.bPlanted;
		State.bPlanted = !bDisabled && (Contact > PlantOn || (bWasPlanted && Contact >= PlantOff));
		const float SoftStep = DeltaTime / SoftBlendTime;
		State.SoftWeight = FMath::Clamp(State.SoftWeight + (State.bPlanted ? -SoftStep : SoftStep), 0.f, 1.f);
		const FTransform AnimFootComponent = Hierarchy->GetGlobalTransform(Solve.Foot);
		Solve.AnimFoot = AnimFootComponent.GetLocation();
		const FVector AnimFoot = ComponentTransform.TransformPosition(Solve.AnimFoot);
		const FVector AnimBall = ComponentTransform.TransformPosition(Hierarchy->GetGlobalTransform(State.CachedBall).GetLocation());
		const double AnimBallStep = State.bHasAnimBall ? FVector::Dist2D(AnimBall, State.PreviousAnimBall) : 0.0;
		const double AnimBallSpeed = DeltaTime > 0.f ? AnimBallStep / DeltaTime : 0.0;
		const double PreviousAnimBallSpeed = State.PreviousAnimBallSpeed;
		State.PreviousAnimBall = AnimBall;
		State.bHasAnimBall = true;
		State.PreviousAnimBallSpeed = static_cast<float>(AnimBallSpeed);

		// 锁脚：落下那一刻记下脚掌在世界里的位置（还在收回上一次的修正时，从脚现在在的地方锁起，不跳）。
		// 动画的脚掌这时还快过 LockSpeed 的（换片段的混合把脚扫过去，接触曲线先到了）先不锁（Settling）：照动画走，修正照放开的收回；
		// 之后慢到 LockSpeed 以下、不再减速（动画自己拖着脚滑的，照样要锁）或等满 MaxSettleTime 再锁。锁在半路上的话，
		// 脚后面那一截整个着地都扣着（滑只在动画还在挪时跟），蹬地时腿够不着，骨盆往下放
		const auto LockHere = [&State, &AnimBall]()
		{
			State.Lock = EGinkgoLegIKLock::Locked;
			State.LockLocation = AnimBall + State.Correction;
		};
		if (State.bPlanted && !bWasPlanted)
		{
			if (AnimBallSpeed > LockSpeed && bSettle)
			{
				// 没在收回的（照动画走着的）从 0 起：收回已经走完
				if (State.Lock != EGinkgoLegIKLock::Releasing)
				{
					State.ReleaseFrom = State.Correction;
					State.ReleaseElapsed = ReleaseTime;
				}
				State.Lock = EGinkgoLegIKLock::Settling;
				State.SettleElapsed = 0.f;
			}
			else
			{
				LockHere();
			}
		}
		else if (State.Lock == EGinkgoLegIKLock::Settling)
		{
			State.SettleElapsed += DeltaTime;
			const bool bStillLanding = AnimBallSpeed > LockSpeed && AnimBallSpeed < PreviousAnimBallSpeed - LandingDeceleration * DeltaTime
				&& State.SettleElapsed < MaxSettleTime;
			if (!State.bPlanted)
			{
				// 还没锁上就离地了：修正接着收回
				State.Lock = EGinkgoLegIKLock::Releasing;
			}
			else if (!bStillLanding)
			{
				LockHere();
			}
		}
		const bool bHeld = State.Lock == EGinkgoLegIKLock::Locked || State.Lock == EGinkgoLegIKLock::Sliding;
		if (State.bPlanted && bHeld)
		{
			FVector Offset = State.LockLocation - AnimBall;
			Offset.Z = 0.0;
			const double Distance = Offset.Size2D();
			if (Distance > UnlockDistance)
			{
				BeginRelease(State);
			}
			else
			{
				State.Lock = Distance > SlideDistance ? EGinkgoLegIKLock::Sliding : EGinkgoLegIKLock::Locked;
				if (Distance > SlideDistance)
				{
					// 滑：锁住的位置往"离动画 SlideDistance"那里挪，每秒最多 SlideSpeed，也不比动画这一帧挪得多
					const FVector Leash = AnimBall + Offset * (SlideDistance / Distance);
					const double MaxStep = FMath::Min(SlideSpeed * DeltaTime, AnimBallStep);
					const FVector Step = FVector(Leash.X - State.LockLocation.X, Leash.Y - State.LockLocation.Y, 0.0).GetClampedToMaxSize(MaxStep);
					State.LockLocation += Step;
					Offset = State.LockLocation - AnimBall;
					Offset.Z = 0.0;
				}
				State.Correction = Offset;
			}
		}
		else if (!State.bPlanted && bHeld)
		{
			BeginRelease(State);
		}
		// 收回修正：放开的收完就照动画；等着锁的收完了接着等
		if (State.Lock == EGinkgoLegIKLock::Releasing || State.Lock == EGinkgoLegIKLock::Settling)
		{
			State.ReleaseElapsed += DeltaTime;
			const float Progress = ReleaseTime > 0.f ? FMath::Clamp(State.ReleaseElapsed / ReleaseTime, 0.f, 1.f) : 1.f;
			State.Correction = State.ReleaseFrom * (1.f - FMath::SmoothStep(0.f, 1.f, Progress));
			if (Progress >= 1.f)
			{
				State.Lock = State.Lock == EGinkgoLegIKLock::Releasing ? EGinkgoLegIKLock::Free : State.Lock;
				State.Correction = FVector::ZeroVector;
			}
		}

		// 预测落脚（摆动时）：还有多久落下按曲线自己往下走的速率换成真实时间。曲线开始往下走以后才用（bLandInTrusted）
		const float LandIn = GetCurve(*Hierarchy, Foot.LandInCurve);
		if (!State.bPlanted && LandIn > 0.f)
		{
			if (State.PreviousLandIn > LandIn && DeltaTime > 0.f)
			{
				State.LandRate = FMath::Max((State.PreviousLandIn - LandIn) / DeltaTime, MinLandRate);
				State.bLandInTrusted = true;
			}
			State.TimeToLand = State.bLandInTrusted ? FMath::Min(LandIn / State.LandRate, MaxTimeToLand) : -1.f;
			if (State.bLandInTrusted)
			{
				const FVector Land(GetCurve(*Hierarchy, Foot.LandXCurve), GetCurve(*Hierarchy, Foot.LandYCurve), 0.0);
				State.PredictedLanding = PredictLanding(WorkData, RootWorld, Land, State.TimeToLand);
			}
		}
		else
		{
			State.TimeToLand = -1.f;
			State.bLandInTrusted = false;
		}
		State.PreviousLandIn = State.bPlanted ? -1.f : LandIn;
		// 够得着约束权重：着地为 1；摆动进入 LandingReachTime 内平滑升到 1，避免上限阶跃把骨盆一把拽动；
		// 落地前 1 帧若 LandIn 已提前归零但接触曲线尚未过 PlantOn，靠上一帧仍有效的 bHasLanding 保持 1 不回落
		if (State.bPlanted)
		{
			Solve.ReachAlpha = 1.0;
		}
		else if (State.bLandInTrusted && State.TimeToLand >= 0.f)
		{
			Solve.ReachAlpha = LandingReachTime > 0.f
				? FMath::SmoothStep(1.0, 0.0, static_cast<double>(State.TimeToLand) / LandingReachTime)
				: (State.TimeToLand <= 0.f ? 1.0 : 0.0);
		}
		else if (LandIn <= 0.f && State.bHasLanding)
		{
			Solve.ReachAlpha = 1.0;
		}
		else
		{
			Solve.ReachAlpha = 0.0;
		}

		// 水平的修正（乘权重）：脚整个平移
		const FVector Correction = State.Correction * Alpha;
		const FVector2D Ball(AnimBall.X + Correction.X, AnimBall.Y + Correction.Y);
		// 脚跟（脚踝正下方）、脚尖在脚掌的前后：按动画里脚踝到脚掌的方向
		const FVector2D HeelOffset(AnimFoot.X - AnimBall.X, AnimFoot.Y - AnimBall.Y);
		const FVector2D Forward = (-HeelOffset).GetSafeNormal();

		// 地形：脚下的地面高度（世界）、面的法线；着地的脚另按台阶的边俯仰
		double Terrain = RootZ;
		double TerrainRate = 0.0;
		double NextAlong = -1.0;
		bool bFromSupport = false;
		uint8 TerrainSource = 0;
		double RawTerrain = RootZ;
		bool bLedge = false;
		FVector Normal = FVector::UpVector;
		FQuat Tilt = FQuat::Identity;
		bool bHasTerrain = false;
		const FVector Forward3(Forward, 0.0);
#if !UE_BUILD_SHIPPING
		UGinkgoAnimInstance::FDebugFrame::FLegIKFoot& Report = Reports[Index];
#endif
		if (bTerrain)
		{
			double HeelExcess = 0.0;
			double ToeExcess = 0.0;
			if (State.bPlanted)
			{
				// 着地：锁住的点（滑、放开时是脚现在的位置）正下方
				bHasTerrain = Probe.Support(Ball, Ball + HeelOffset, Ball + Forward * ToeLength, Terrain, Normal, HeelExcess, ToeExcess);
				bFromSupport = bHasTerrain;
				TerrainSource = bHasTerrain ? 1 : 0;
				bLedge = bHasTerrain && ClampToLedge(Ball, Terrain, Normal, HeelExcess, ToeExcess);
				if (bHasTerrain)
				{
					// 锁着（锁住、滑）的脚按台阶的边俯仰；正在放开、还没锁上的脚被动画拖着走，脚跟、脚尖碰到高出的边就整只抬上去
					if (State.Lock == EGinkgoLegIKLock::Releasing || State.Lock == EGinkgoLegIKLock::Settling)
					{
						Terrain += FMath::Max3(HeelExcess, ToeExcess, 0.0);
						State.FitPitch = FQuat::Identity;
					}
					else
					{
						State.FitPitch = PitchToFit(Forward3, HeelExcess, HeelOffset.Size(), ToeExcess, ToeLength);
					}
					State.bHasAnchor = true;
					State.Anchor = FVector(Ball, Terrain);
					State.AnchorNormal = Normal;
					Solve.bHasPelvisAnchor = true;
					Solve.PelvisAnchor = State.Anchor;
				}
				State.bHasLanding = false;
				Tilt = TiltToward(Normal, FootTiltLimit);
			}
			else if (State.bLandInTrusted && State.TimeToLand >= 0.f && State.bHasAnchor)
			{
				// 摆动、预测可信：路径从上一次着地的点到预测的落脚点
				const FVector2D Landing(State.PredictedLanding.X, State.PredictedLanding.Y);
				double LandingZ = State.bHasLanding ? State.LandingZ : State.Anchor.Z;
				FVector LandingNormal = FVector::UpVector;
				double ProbedTop = RootZ + TraceUp;
				bool bLandingHit = Probe.SupportAt(Landing, Landing + HeelOffset, Landing + Forward * ToeLength, ProbedTop, RootZ - TraceDown,
					LandingZ, LandingNormal, HeelExcess, ToeExcess);
				if (!bLandingHit)
				{
					// 冲刺上多级楼梯时，预测落脚点常高于 RootZ + TraceUp，直接打线会在实心台阶内部起点穿透；
					// 从胶囊底沿水平方向逐级探高后再打线（遇墙、陡面或断崖立即停止，不会爬墙或误打天花板）
					const double ClimbedZ = Probe.ClimbTop(RootXY, RootZ, Landing, MaxStepDown, ClimbSpacing);
					if (ClimbedZ > RootZ + UE_KINDA_SMALL_NUMBER)
					{
						ProbedTop = ClimbedZ + TraceUp;
						bLandingHit = Probe.SupportAt(Landing, Landing + HeelOffset, Landing + Forward * ToeLength, ProbedTop, FMath::Min(RootZ, ClimbedZ) - TraceDown,
							LandingZ, LandingNormal, HeelExcess, ToeExcess);
					}
				}
				if (bLandingHit)
				{
					// 落脚点在高台的边外：落在边上那一侧的高度，路径不往下走，骨盆不跟着往下放
					bLedge = ClampToLedge(Landing, LandingZ, LandingNormal, HeelExcess, ToeExcess);
				}
				else
				{
					// 落脚点下面扫不到地面（掉下边缘或伸进墙里那种）：沿用上一帧的，没有时用起点的
					LandingZ = State.bHasLanding ? State.LandingZ : State.Anchor.Z;
					LandingNormal = FVector::UpVector;
				}

				// 沿途往下扫的点从起点按固定的间隔排（预测的落脚点挪动时它们不跟着挪，凸包不来回跳）；
				// 小球的半径取间隔的一半，相邻两次接得上，台阶的边总有一次扫到
				const FVector2D Start(State.Anchor.X, State.Anchor.Y);
				const FVector2D Path = Landing - Start;
				const double Length = Path.Size();
				const FVector2D Direction = Length > UE_KINDA_SMALL_NUMBER ? Path / Length : Forward;
				const double Spacing = FMath::Max(static_cast<double>(PathSpacing), Length / (MaxPathSamples + 1));
				const double Radius = FMath::Max(Spacing * 0.5, static_cast<double>(TraceRadius));
				FPathPoints Points;
				Points.Add(FVector2D(0.0, State.Anchor.Z));
				double PathRefZ = State.Anchor.Z;
				for (double Distance = Spacing; Distance < Length - Spacing * 0.5 && Points.Num() <= MaxPathSamples; Distance += Spacing)
				{
					FVector Hit;
					const double RefZ = FMath::Max(RootZ, PathRefZ);
					// 小球底面从 RefZ + TraceUp 起下落，沿途逐级抬高基准，避免上/下多级楼梯时球体与高阶起点穿透
					if (Probe.SphereAt(Start + Direction * Distance, RefZ + TraceUp + Radius, FMath::Min(RootZ, PathRefZ) - TraceDown, Radius, Hit))
					{
						Points.Add(FVector2D(FMath::Clamp(FVector2D::DotProduct(FVector2D(Hit.X, Hit.Y) - Start, Direction), 0.0, Length), Hit.Z));
						if (PathRefZ - Hit.Z <= MaxStepDown)
						{
							PathRefZ = Hit.Z;
						}
					}
				}
				// 镂空/悬空薄板楼梯下：初始从 RootZ + TraceUp 打线可能穿到楼梯下方低地面而未穿透；
				// 若沿途球扫已经逐级爬升到高于初探结果，按沿途高度补探一次落脚点
				if (PathRefZ + TraceUp > ProbedTop + UE_KINDA_SMALL_NUMBER && (!bLandingHit || PathRefZ > LandingZ + TraceUp * 0.5))
				{
					double HigherZ = LandingZ;
					FVector HigherNormal = LandingNormal;
					double HigherHeelExcess = 0.0;
					double HigherToeExcess = 0.0;
					if (Probe.SupportAt(Landing, Landing + HeelOffset, Landing + Forward * ToeLength, PathRefZ + TraceUp, FMath::Min(RootZ, PathRefZ) - TraceDown,
						HigherZ, HigherNormal, HigherHeelExcess, HigherToeExcess) && (!bLandingHit || HigherZ > LandingZ))
					{
						LandingZ = HigherZ;
						LandingNormal = HigherNormal;
						bLedge = ClampToLedge(Landing, LandingZ, LandingNormal, HigherHeelExcess, HigherToeExcess);
					}
				}
				// 预测越过台阶的边时终点跳一级：路径跟着跳，脚下的高度由 TerrainTrack 缓过去
				State.bHasLanding = true;
				State.LandingZ = LandingZ;
				State.LandingNormal = LandingNormal;
				Points.Add(FVector2D(Length, State.LandingZ));
				Points.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });

				// 分段点：另一只脚的着地点（踩着的，或刚离开的），在这段路的两头之间、离路不远；它还没落到这段路上时用它预测的落脚点。
				// 左脚先算，读到的右脚是上一帧的
				bool bSplit = false;
				FVector2D Split = FVector2D::ZeroVector;
				if (Feet.Num() == 2)
				{
					const FRigUnit_GinkgoLegIK_FootState& Other = WorkData.Feet[1 - Index];
					const auto TrySplit = [&bSplit, &Split, &Start, &Direction, Spacing, Length](const FVector& Point)
					{
						const FVector2D Offset = FVector2D(Point.X, Point.Y) - Start;
						const double At = FVector2D::DotProduct(Offset, Direction);
						if (!bSplit && At > Spacing * 0.5 && At < Length - Spacing * 0.5 && FMath::Abs(FVector2D::CrossProduct(Direction, Offset)) < SplitSideDistance)
						{
							bSplit = true;
							Split = FVector2D(At, Point.Z);
						}
					};
					if (Other.bHasAnchor)
					{
						TrySplit(Other.Anchor);
					}
					if (Other.bLandInTrusted && Other.bHasLanding)
					{
						TrySplit(FVector(Other.PredictedLanding.X, Other.PredictedLanding.Y, Other.LandingZ));
					}
				}
				if (bSplit)
				{
					KeepUpperHullThrough(Points, Split);
				}
				else
				{
					KeepUpperHull(Points);
				}

				// 脚掌在路径上走到哪了；脚跟、脚尖那里也不低于路径。脚走到路径两头以外（起步时往后退一步、越过了预测的落脚点）时，
				// 路径管不到它：照脚现在的位置往下扫
				const double RawAlong = FVector2D::DotProduct(Ball - Start, Direction);
				if (RawAlong >= -PathOutside && RawAlong <= Length + PathOutside)
				{
					const double Along = FMath::Clamp(RawAlong, 0.0, Length);
					const auto UnderFoot = [&Points, &HeelOffset, ToeLength, Length](const double At)
					{
						return FMath::Max3(EvaluatePath(Points, At), EvaluatePath(Points, FMath::Max(At - HeelOffset.Size(), 0.0)),
							EvaluatePath(Points, FMath::Min(At + ToeLength, Length)));
					};
					Terrain = UnderFoot(Along);
					// 前馈：脚沿着这一帧的路径走带来的变化（路径自己因预测变了而变的不算，缓过去）
					TerrainRate = State.PreviousAlong >= 0.0 && DeltaTime > 0.f ? (Terrain - UnderFoot(State.PreviousAlong)) / DeltaTime : 0.0;
					NextAlong = Along;
					bHasTerrain = true;
					TerrainSource = 2;
				}
				else
				{
					bHasTerrain = Probe.Support(Ball, Ball + HeelOffset, Ball + Forward * ToeLength, Terrain, Normal, HeelExcess, ToeExcess);
					bLedge = (bHasTerrain && ClampToLedge(Ball, Terrain, Normal, HeelExcess, ToeExcess)) || bLedge;
					Terrain += FMath::Max3(HeelExcess, ToeExcess, 0.0);
					bFromSupport = bHasTerrain;
					TerrainSource = bHasTerrain ? 3 : 0;
				}
				// 朝向：从上一次着地的面倾到落脚点的面
				const double Progress = Length > UE_KINDA_SMALL_NUMBER ? FMath::Clamp(RawAlong / Length, 0.0, 1.0) : 1.0;
				Tilt = FQuat::Slerp(TiltToward(State.AnchorNormal, FootTiltLimit), TiltToward(State.LandingNormal, FootTiltLimit), Progress);
				Solve.bHasPelvisAnchor = true;
				Solve.PelvisAnchor = FVector(Landing, State.LandingZ);
#if !UE_BUILD_SHIPPING
				for (const FVector2D& Point : Points)
				{
					if (Report.PathCount < UGinkgoAnimInstance::FDebugFrame::FLegIKFoot::MaxPath)
					{
						Report.Path[Report.PathCount++] = FVector(Start + Direction * Point.X, Point.Y);
					}
				}
#endif
			}
			else
			{
				// 刚离地还没有可信的预测、或者还没有着地过：照脚现在的位置往下扫，脚跟、脚尖碰到高出的边就整只抬上去。
				// 脚照样按脚下的面倾斜，与着地时接上（原来这几帧不倾斜，坡上离地那一帧脚的俯仰跳三十多度）
				bHasTerrain = Probe.Support(Ball, Ball + HeelOffset, Ball + Forward * ToeLength, Terrain, Normal, HeelExcess, ToeExcess);
				bLedge = bHasTerrain && ClampToLedge(Ball, Terrain, Normal, HeelExcess, ToeExcess);
				Terrain += FMath::Max3(HeelExcess, ToeExcess, 0.0);
				bFromSupport = bHasTerrain;
				TerrainSource = bHasTerrain ? 3 : 0;
				Tilt = bHasTerrain ? TiltToward(Normal, FootTiltLimit) : Tilt;
				if (bHasTerrain)
				{
					Solve.bHasPelvisAnchor = true;
					Solve.PelvisAnchor = FVector(Ball, Terrain);
				}
			}
			// 照脚下扫的：前馈它这一帧变了多少，但最多按 MaxSupportSlope 乘脚掌水平走过的（坡上不落后，台阶一下子高一级的缓过去）
			const double BallStep = State.bHasPreviousBall ? FVector2D::Distance(Ball, State.PreviousBall) : 0.0;
			if (bFromSupport && State.bHasPreviousSupport && DeltaTime > 0.f)
			{
				TerrainRate = FMath::Clamp(Terrain - State.PreviousSupport, -MaxSupportSlope * BallStep, MaxSupportSlope * BallStep) / DeltaTime;
			}
			State.bHasPreviousSupport = bFromSupport;
			State.PreviousSupport = Terrain;
			// 跳变缓过去：着地落的高度与预测不同、路径变了、换了来源，都只在这一帧跳；着地的脚要快
			RawTerrain = Terrain;
			if (bHasTerrain)
			{
				Terrain = Track(State.TerrainTrack, Terrain, TerrainRate, State.bPlanted ? PlantSettleOmega : FootSettleOmega, DeltaTime);
			}
			else
			{
				State.TerrainTrack = FRigUnit_GinkgoLegIK_Track();
			}
			State.PreviousAlong = NextAlong;
			State.bHasPreviousBall = true;
			State.PreviousBall = Ball;
			// 台阶边上的俯仰：着地以后升起，离地以后收掉
			const float FitStep = FitBlendTime > 0.f ? DeltaTime / FitBlendTime : 1.f;
			State.FitWeight = FMath::Clamp(State.FitWeight + (State.bPlanted ? FitStep : -FitStep), 0.f, 1.f);
			Tilt = LimitAngle(FQuat::Slerp(FQuat::Identity, State.FitPitch, State.FitWeight) * Tilt, FootTiltLimit);
		}
		State.bHasTerrain = bHasTerrain;
		State.TerrainZ = bHasTerrain ? Terrain : RootZ;

		// 脚掌要去的位置：水平的修正加上地形的高度差，且不低于地面上 MinBallHeight；脚绕脚掌倾斜，脚踝跟着转
		const double VerticalCorrection = bHasTerrain ? FMath::Max(Terrain - RootZ, Terrain + MinBallHeight - AnimBall.Z) * Alpha : 0.0;
		Tilt = FQuat::Slerp(FQuat::Identity, Tilt, Alpha);
		const FVector BallTarget(Ball.X, Ball.Y, AnimBall.Z + VerticalCorrection);
		const FQuat ComponentRotation = ComponentTransform.GetRotation();
		Solve.FootTarget = ComponentTransform.InverseTransformPosition(BallTarget + Tilt.RotateVector(AnimFoot - AnimBall));
		Solve.Tilt = ComponentRotation.Inverse() * Tilt * ComponentRotation;
		Solve.bChanged = !Correction.IsNearlyZero(MinCorrection) || FMath::Abs(VerticalCorrection) > MinCorrection || !Tilt.Equals(FQuat::Identity, 1e-5);

#if !UE_BUILD_SHIPPING
		Report.Lock = static_cast<uint8>(State.Lock);
		Report.bPlanted = State.bPlanted;
		Report.LockLocation = State.LockLocation;
		Report.AnimBall = AnimBall;
		Report.Correction = bEnabled ? static_cast<float>(State.Correction.Size2D()) : 0.f;
		Report.TimeToLand = State.TimeToLand;
		Report.PredictedLanding = State.PredictedLanding;
		Report.bHasTerrain = bHasTerrain;
		Report.TerrainZ = static_cast<float>(State.TerrainZ);
		Report.VerticalCorrection = bEnabled ? static_cast<float>(VerticalCorrection) : 0.f;
		Report.TerrainSource = TerrainSource;
		Report.RawTerrainZ = static_cast<float>(RawTerrain);
		Report.bLedge = bLedge;
#endif
	}

	// ===== 骨盆：两只脚的锚连成的线在骨盆正下方的高度，加上动画里骨盆高出地面多少；哪条腿够不着时再往下放，摆动的腿要折断时往上抬 =====
	// 两部分都在世界里算、各自过临界阻尼弹簧，最后才减去根骨骼：胶囊上下台阶一帧跳一级时看得见的身体不跟着跳，动画自己骨盆的起伏照旧
	const bool bHasPelvis = WorkData.CachedPelvis.UpdateCache(Pelvis, Hierarchy);
	WorkData.CachedHandIKRoot.UpdateCache(HandIKRoot, Hierarchy);
	// 脚相对大腿根最多比动画抬高多少：动画里大腿根到脚踝比腿长的 FoldLimit 长出的那一截（动画里本来就折得更紧的，不抬）
	const auto MaxFoldLift = [FoldLimit](const double AnimDistance, const double Length) { return FMath::Max(AnimDistance - Length * FoldLimit, 0.0); };
#if !UE_BUILD_SHIPPING
	double ReportLine = 0.0;
	double ReportUpper = 999.0;
	double ReportLower = -999.0;
#endif
	if (bHasPelvis && bTerrain && Solves.Num() == 2 && Solves[0].bHasPelvisAnchor && Solves[1].bHasPelvisAnchor)
	{
		const FVector AnimPelvis = ComponentTransform.TransformPosition(Hierarchy->GetGlobalTransform(WorkData.CachedPelvis).GetLocation());
		const FVector2D PelvisXY(AnimPelvis.X, AnimPelvis.Y);
		const FRigUnit_GinkgoLegIK_FootState& Foot0 = WorkData.Feet[0];
		const FRigUnit_GinkgoLegIK_FootState& Foot1 = WorkData.Feet[1];
		FVector SegA = Solves[0].PelvisAnchor;
		FVector SegB = Solves[1].PelvisAnchor;
		if (Foot0.bPlanted != Foot1.bPlanted)
		{
			// 单脚着地：骨盆尚未越过支撑脚 P 时沿 [刚离地的脚 S 的起跳点, P] 内插，越过 P 后平滑切到 [P, S 的下一落脚点]，
			// 支撑脚刚落地与后脚刚离地那一帧端点连续，且拐弯折返时按朝向下一落脚点的一侧自然切段
			const int32 Planted = Foot0.bPlanted ? 0 : 1;
			const int32 Swinging = 1 - Planted;
			const FRigUnit_GinkgoLegIK_FootState& PlantedFoot = WorkData.Feet[Planted];
			const FRigUnit_GinkgoLegIK_FootState& SwingingFoot = WorkData.Feet[Swinging];
			const bool bHasPredictedLanding = SwingingFoot.bLandInTrusted && SwingingFoot.TimeToLand >= 0.f && SwingingFoot.bHasLanding;
			if (bHasPredictedLanding)
			{
				const FVector2D PlantedXY(PlantedFoot.Anchor.X, PlantedFoot.Anchor.Y);
				const FVector2D LandingXY(SwingingFoot.PredictedLanding.X, SwingingFoot.PredictedLanding.Y);
				if (SwingingFoot.bHasAnchor && FVector2D::DotProduct(PelvisXY - PlantedXY, LandingXY - PlantedXY) <= 0.0)
				{
					SegA = SwingingFoot.Anchor;
					SegB = PlantedFoot.Anchor;
				}
				else
				{
					SegA = PlantedFoot.Anchor;
					SegB = FVector(LandingXY, SwingingFoot.LandingZ);
				}
			}
			else if (SwingingFoot.bHasAnchor)
			{
				SegA = SwingingFoot.Anchor;
				SegB = PlantedFoot.Anchor;
			}
		}
		else if (!Foot0.bPlanted && !Foot1.bPlanted)
		{
			// 腾空期（跑、冲刺两脚均离地）：用"刚蹬离的后脚着地点 → 即将落地的前脚落脚点"替代两只前探预测落脚点，
			// 始终把骨盆包在前后两步之间做内插，避免双前探点叠加负向 T 外推把骨盆抛飞或下压
			const bool bLand0 = Foot0.bLandInTrusted && Foot0.TimeToLand >= 0.f && Foot0.bHasLanding;
			const bool bLand1 = Foot1.bLandInTrusted && Foot1.TimeToLand >= 0.f && Foot1.bHasLanding;
			int32 Next = INDEX_NONE;
			if (bLand0 && bLand1)
			{
				Next = Foot0.TimeToLand <= Foot1.TimeToLand ? 0 : 1;
			}
			else if (bLand0 != bLand1)
			{
				const int32 Trusted = bLand0 ? 0 : 1;
				const int32 Other = 1 - Trusted;
				const FRigUnit_GinkgoLegIK_FootState& OtherFoot = WorkData.Feet[Other];
				// 另一只脚若已有落脚预测且 LandIn 刚好提前归零，说明它正要着地；否则它是刚离地、还在等首帧信任的脚
				Next = (OtherFoot.bHasLanding && OtherFoot.PreviousLandIn <= 0.f) ? Other : Trusted;
			}
			else if (Foot0.bHasLanding != Foot1.bHasLanding)
			{
				Next = Foot0.bHasLanding ? 0 : 1;
			}
			if (Next != INDEX_NONE && WorkData.Feet[1 - Next].bHasAnchor)
			{
				SegA = WorkData.Feet[1 - Next].Anchor;
				SegB = Solves[Next].PelvisAnchor;
			}
		}
		const FVector2D Segment(SegB.X - SegA.X, SegB.Y - SegA.Y);
		const double LengthSquared = Segment.SizeSquared();
		const double RawT = LengthSquared > 1.0 ? FVector2D::DotProduct(FVector2D(PelvisXY.X - SegA.X, PelvisXY.Y - SegA.Y), Segment) / LengthSquared : 0.5;
		// 骨盆只在前后两个锚点之间内插，不沿连线向外推，避免到达楼梯顶部或起步阶段因远端落脚点折角产生过冲或下沉
		const double T = FMath::Clamp(RawT, 0.0, 1.0);
		const double Base = FMath::Lerp(SegA.Z, SegB.Z, T);
		// 线自己的变化率：骨盆沿线内插时高度怎么变，按最大可行走坡度限幅，防止两锚水平距离很近时前馈速度爆炸
		const FVector2D RootVel2D(WorkData.RootVelocity.X, WorkData.RootVelocity.Y);
		const double MaxBaseRate = MaxSupportSlope * RootVel2D.Size();
		const double RawBaseRate = LengthSquared > 1.0 && RawT > 0.0 && RawT < 1.0
			? (SegB.Z - SegA.Z) * FVector2D::DotProduct(RootVel2D, Segment) / LengthSquared : 0.0;
		const double BaseRate = FMath::Clamp(RawBaseRate, -MaxBaseRate, MaxBaseRate);
		const double PelvisBase = Track(WorkData.PelvisBase, Base, BaseRate, PelvisOmega, DeltaTime);

		// 骨盆（大腿根随它挪）的上下限，以相对根骨骼的偏移计：
		// - 够得着（着地的脚权重为 1，摆动进入 LandingReachTime 内平滑升到 1）：大腿根到脚踝要去的地方不超过腿长的 ReachRatio
		//   （动画里本来就伸得更直的，以动画的为准）→ 不高于 Upper。水平方向上就够不着的脚，往下放也没用，不为它放；
		//   但也不许为别的腿往上抬（只会更够不着）。刚离地的脚够不着就让腿伸直：冲刺上楼梯的腾空里，刚蹬离的后脚的目标
		//   还在身后低一两级的台阶上，为它往下放会压住前脚
		// - 不折断：脚相对大腿根比动画抬得不比 MaxFoldLift 多、脚踝不高过大腿根以下 AnkleBelowHip → 不低于 Lower
		const double LineOffset = PelvisBase - RootZ;
		double Upper = TNumericLimits<double>::Max();
		double Lower = TNumericLimits<double>::Lowest();
		double FootUpper[2] = { TNumericLimits<double>::Max(), TNumericLimits<double>::Max() };
		for (int32 Index = 0; Index < Solves.Num(); ++Index)
		{
			const FFootSolve& Solve = Solves[Index];
			const FVector Thigh = Hierarchy->GetGlobalTransform(Solve.Thigh).GetLocation();
			const FVector Calf = Hierarchy->GetGlobalTransform(Solve.Calf).GetLocation();
			const double Length = FVector::Dist(Thigh, Calf) + FVector::Dist(Calf, Solve.AnimFoot);
			const double AnimDistance = FVector::Dist(Thigh, Solve.AnimFoot);
			const double Reach = FMath::Max(Length * ReachRatio, AnimDistance);
			const FVector ToTarget = Solve.FootTarget - Thigh;
			const double Room = FMath::Square(Reach) - ToTarget.SizeSquared2D();
			if (!bFold)
			{
				// 对照（ginkgo.anim.legIKFold 0）：原来的做法，每只够得着的脚都往下放，不为折叠往上抬
				Upper = Room > 0.0 ? FMath::Min(Upper, ToTarget.Z + FMath::Sqrt(Room)) : Upper;
				continue;
			}
			FootUpper[Index] = Room > 0.0 ? ToTarget.Z + FMath::Sqrt(Room) : FMath::Max(ToTarget.Z, LineOffset);
			const double FoldLift = Solve.FootTarget.Z - Solve.AnimFoot.Z - MaxFoldLift(AnimDistance, Length);
			const double HipLift = Solve.FootTarget.Z - FMath::Max(Thigh.Z - AnkleBelowHip, Solve.AnimFoot.Z);
			Lower = FMath::Max3(Lower, FoldLift, HipLift);
		}
		// 在那条线以外另挪：够不着时往下（最多 MaxReachDrop）优先；不用往下时为不折断往上（最多 MaxFoldRise），但不高过够得着的。
		// 对快落地的脚按 ReachAlpha 把上限从无够得着约束的目标平滑过渡到 ClampedUpper，消除 LandingReachTime 边界处的阶跃
		double Adjust = 0.0;
		if (!bFold)
		{
			if (Upper < LineOffset)
			{
				Adjust = FMath::Max(Upper - LineOffset, -static_cast<double>(MaxReachDrop));
			}
		}
		else
		{
			const double FoldRise = FMath::Clamp(Lower - LineOffset, 0.0, static_cast<double>(MaxFoldRise));
			const double Unconstrained = LineOffset + FoldRise;
			for (int32 Index = 0; Index < Solves.Num(); ++Index)
			{
				if (Solves[Index].ReachAlpha > 0.0)
				{
					const double ClampedUpper = FMath::Max(FootUpper[Index], LineOffset - static_cast<double>(MaxReachDrop));
					Upper = FMath::Min(Upper, FMath::Lerp(Unconstrained, ClampedUpper, Solves[Index].ReachAlpha));
				}
			}
			Adjust = FMath::Min(FoldRise, Upper - LineOffset);
		}
		double AdjustError = WorkData.PelvisAdjust - Adjust;
		Ginkgo::Spring::StepCritical(AdjustError, WorkData.PelvisAdjustVelocity, bFold ? PelvisAdjustOmega : PelvisOmega, DeltaTime);
		WorkData.PelvisAdjust = Adjust + AdjustError;
		WorkData.PelvisOffset = FMath::Clamp(LineOffset + WorkData.PelvisAdjust, -static_cast<double>(MaxPelvisOffset), static_cast<double>(MaxPelvisOffset)) * Alpha;
#if !UE_BUILD_SHIPPING
		ReportLine = LineOffset;
		ReportUpper = FMath::Min(Upper, 999.0);
		ReportLower = FMath::Max(Lower, -999.0);
#endif
	}
	else
	{
		// 没有两只脚的锚（扫不到地面）：偏移按 ω 收回 0；关掉地形作对照时直接是 0
		WorkData.PelvisBase = FRigUnit_GinkgoLegIK_Track();
		WorkData.PelvisAdjust = 0.0;
		WorkData.PelvisAdjustVelocity = 0.0;
		WorkData.PelvisOffset = bTerrain ? WorkData.PelvisOffset * FMath::Exp(-PelvisOmega * DeltaTime) : 0.0;
	}
	const bool bMovePelvis = bEnabled && bHasPelvis && FMath::Abs(WorkData.PelvisOffset) > MinCorrection;
	if (bMovePelvis)
	{
		// 组件的竖直与世界的一致（角色只绕竖直轴转）；手的 IK 根一起挪，枪与手不脱开
		const FVector Lift(0.0, 0.0, WorkData.PelvisOffset);
		FTransform PelvisTransform = Hierarchy->GetGlobalTransform(WorkData.CachedPelvis);
		PelvisTransform.AddToTranslation(Lift);
		Hierarchy->SetGlobalTransform(WorkData.CachedPelvis.GetIndex(), PelvisTransform);
		if (WorkData.CachedHandIKRoot.IsValid())
		{
			FTransform HandIKRootTransform = Hierarchy->GetGlobalTransform(WorkData.CachedHandIKRoot);
			HandIKRootTransform.AddToTranslation(Lift);
			Hierarchy->SetGlobalTransform(WorkData.CachedHandIKRoot.GetIndex(), HandIKRootTransform);
		}
	}

	// ===== 第二遍：两骨骼 IK 把腿接上（膝盖往动画里膝盖的方向弯，不拉伸）；脚的朝向照动画再倾斜；跟着脚的 IK 骨骼一起挪 =====
	for (int32 Index = 0; Index < Solves.Num(); ++Index)
	{
		const FFootSolve& Solve = Solves[Index];
		if (!Solve.bValid)
		{
			continue;
		}
		FTransform Thigh = Hierarchy->GetGlobalTransform(Solve.Thigh);
		FTransform Calf = Hierarchy->GetGlobalTransform(Solve.Calf);
		FTransform FootTransform = Hierarchy->GetGlobalTransform(Solve.Foot);
		const FVector ThighLocation = Thigh.GetLocation();
		const FVector AnimKnee = Calf.GetLocation();
		const FVector AnimAnkle = FootTransform.GetLocation();
		// 骨盆抬不够时脚的目标往下压（只往下，不低于动画的脚踝）：相对大腿根最多比动画抬高 MaxFoldLift，脚踝不高过大腿根以下 AnkleBelowHip。
		// 两条都随姿势连续地变，着地、摆动都一样，不在换状态时跳
		const double Length = FVector::Dist(ThighLocation, AnimKnee) + FVector::Dist(AnimKnee, AnimAnkle);
		const double MaxTargetZ = FMath::Min(AnimAnkle.Z + MaxFoldLift(FVector::Dist(ThighLocation, AnimAnkle), Length),
			FMath::Max(ThighLocation.Z - AnkleBelowHip, AnimAnkle.Z));
		FVector Target = Solve.FootTarget;
		Target.Z = bFold ? FMath::Min(Target.Z, MaxTargetZ) : Target.Z;
#if !UE_BUILD_SHIPPING
		Reports[Index].AnimFoot = ComponentTransform.TransformPosition(AnimAnkle);
		Reports[Index].AnimKnee = ComponentTransform.TransformPosition(AnimKnee);
		Reports[Index].FootTarget = ComponentTransform.TransformPosition(Target);
#endif
		// 摆动的脚够不着时腿慢慢伸直（软 IK），不一帧弹直：大腿根到目标超过腿长的 SoftReach（动画里本来就伸得更直的，以动画的为准）时，
		// 超出的部分按指数收进腿长以内。冲刺越过楼梯顶时脚的目标一下子落到够不着的地方，原来膝盖一帧从弯八十度变成伸直。
		// 着地的脚不软：它要踩住地面，骨盆已经为它放低了。离地、着地前后各 SoftBlendTime 里渐变（SoftWeight），蹬地那一帧脚不跳
		const float SoftWeight = WorkData.Feet[Index].SoftWeight;
		if (bFold && SoftWeight > 0.f)
		{
			const double Soft = FMath::Max(Length * SoftReach, FVector::Dist(ThighLocation, AnimAnkle));
			const FVector ToTarget = Target - ThighLocation;
			const double Distance = ToTarget.Size();
			if (Distance > Soft && Length - Soft > UE_KINDA_SMALL_NUMBER)
			{
				const double Span = Length - Soft;
				const double Softened = Soft + Span * (1.0 - FMath::Exp(-(Distance - Soft) / Span));
				Target = ThighLocation + ToTarget * (FMath::Lerp(Distance, Softened, static_cast<double>(SoftWeight)) / Distance);
			}
		}
		if (!bEnabled || (!Solve.bChanged && !bMovePelvis))
		{
			continue;
		}
		// 膝盖往哪边弯照动画：动画里的膝盖跟着整条腿转过去（大腿根 → 动画的脚踝 转到 大腿根 → 目标）。直接拿动画的膝盖当关节目标时，
		// 目标挪得多（冲刺上楼梯时摆动的脚抬起几十厘米），"大腿根 → 目标"那条线会从膝盖的另一边穿过去，膝盖一帧翻到反方向
		const FQuat LegTurn = FQuat::FindBetweenVectors(AnimAnkle - ThighLocation, Target - ThighLocation);
		const FVector JointTarget = bFold ? ThighLocation + LegTurn.RotateVector(AnimKnee - ThighLocation) : AnimKnee;
		AnimationCore::SolveTwoBoneIK(Thigh, Calf, FootTransform, JointTarget, Target, /*bAllowStretching*/ false, 1.0, 1.0);
		FootTransform.SetRotation(Solve.Tilt * FootTransform.GetRotation());
		Hierarchy->SetGlobalTransform(Solve.Thigh, Thigh);
		Hierarchy->SetGlobalTransform(Solve.Calf, Calf);
		Hierarchy->SetGlobalTransform(Solve.Foot, FootTransform);
		if (Solve.IKFoot != INDEX_NONE)
		{
			FTransform IKFoot = Hierarchy->GetGlobalTransform(Solve.IKFoot);
			IKFoot.AddToTranslation(FootTransform.GetLocation() - Solve.AnimFoot);
			IKFoot.SetRotation(Solve.Tilt * IKFoot.GetRotation());
			Hierarchy->SetGlobalTransform(Solve.IKFoot, IKFoot, /*bAffectChildren*/ false);
		}
	}

#if !UE_BUILD_SHIPPING
	const USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(ExecuteContext.GetOwningComponent());
	if (UGinkgoAnimInstance* Instance = Mesh ? Cast<UGinkgoAnimInstance>(Mesh->GetAnimInstance()) : nullptr)
	{
		for (int32 Index = 0; Index < Reports.Num(); ++Index)
		{
			Instance->ReportLegIKFoot(Index, Reports[Index]);
		}
		Instance->ReportLegIKPelvis(bEnabled ? static_cast<float>(WorkData.PelvisOffset) : 0.f, Probe.Traces, static_cast<float>(ReportLine), static_cast<float>(ReportUpper),
			static_cast<float>(ReportLower), static_cast<float>(WorkData.PelvisAdjust));
	}
#endif
}

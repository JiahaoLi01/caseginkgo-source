#include "Animation/RigUnit_GinkgoHandReach.h"

#include "Animation/AnimTypes.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Units/RigUnitContext.h"

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarReachLean(
		TEXT("ginkgo.anim.reachLean"),
		1,
		TEXT("伸手时上半身参与（第一遍全身 IK）：1 开，0 关（只用胳膊够，同拾取第 2 批最初的样子；只用来对照）。"));

	TAutoConsoleVariable<float> CVarReachSpineStiffness(
		TEXT("ginkgo.anim.reachSpineStiffness"),
		-1.f,
		TEXT("第一遍全身 IK 里脊柱的转动刚度（0–1），负的是 1（脊柱不动，弯腰全由前倾算）。只用来对照（以前是 0.9）。"));

	TAutoConsoleVariable<float> CVarReachClavicleStiffness(
		TEXT("ginkgo.anim.reachClavicleStiffness"),
		-1.f,
		TEXT("伸手时锁骨的转动刚度（0–1），负的用节点的输入。调试用。"));

	TAutoConsoleVariable<int32> CVarReachStretch(
		TEXT("ginkgo.anim.reachStretch"),
		-1,
		TEXT("伸手时允许骨骼伸长：1 允许，0 不允许，负的用节点的输入。调试用。"));

	TAutoConsoleVariable<float> CVarReachArmStretchStiffness(
		TEXT("ginkgo.anim.reachArmStretchStiffness"),
		-1.f,
		TEXT("伸长时胳膊的平移刚度（0–1），负的用节点的输入。调试用。"));

	TAutoConsoleVariable<int32> CVarReachIterations(
		TEXT("ginkgo.anim.reachIterations"),
		-1,
		TEXT("伸手那一遍全身 IK 的迭代次数，负的用节点的输入。调试用。"));

	TAutoConsoleVariable<float> CVarReachMassMultiplier(
		TEXT("ginkgo.anim.reachMassMultiplier"),
		-1.f,
		TEXT("伸手那一遍全身 IK 的质量倍数，负的用节点的输入。调试用。"));

	TAutoConsoleVariable<int32> CVarReachTurnHand(
		TEXT("ginkgo.anim.reachTurnHand"),
		1,
		TEXT("伸手时手转向东西：1 开，0 关（手的朝向照片段的，往外伸够不远；只用来对照）。"));

	TAutoConsoleVariable<int32> CVarReachBend(
		TEXT("ginkgo.anim.reachBend"),
		1,
		TEXT("伸手时够不着就前倾（脊柱往东西那边转）：1 开，0 关（只用来对照）。"));

	TAutoConsoleVariable<int32> CVarReachEvenLean(
		TEXT("ginkgo.anim.reachEvenLean"),
		0,
		TEXT("前倾的角度平分给每根脊柱（以前的样子，弯到上胸）：1 平分，0 按节点的 LeanShares。只用来对照。"));

	TAutoConsoleVariable<float> CVarReachComfortReach(
		TEXT("ginkgo.anim.reachComfortReach"),
		-1.f,
		TEXT("东西离肩关节超过多少（cm）才前倾，负的用节点的输入。调试用。"));

	TAutoConsoleVariable<float> CVarReachMaxLean(
		TEXT("ginkgo.anim.reachMaxLean"),
		-1.f,
		TEXT("前倾最多多少度，负的用节点的输入。调试用。"));

	TAutoConsoleVariable<float> CVarReachPullChainAlpha(
		TEXT("ginkgo.anim.reachPullChainAlpha"),
		-1.f,
		TEXT("伸手那一遍全身 IK 的效应器的 PullChainAlpha（0–1），负的用节点的输入。调试用。"));
#endif

	/** 手转向东西时绕的肩关节（下标是 EGinkgoHand） */
	const FRigElementKey& ShoulderBoneKey(const EGinkgoHand Hand)
	{
		static const FRigElementKey Keys[2] = { FRigElementKey(TEXT("upperarm_l"), ERigElementType::Bone), FRigElementKey(TEXT("upperarm_r"), ERigElementType::Bone) };
		return Keys[static_cast<int32>(Hand)];
	}

	/** 手最多转多少：东西在身后时片段的方向与它差得多，转得太多手腕拧得不像 */
	constexpr float MaxHandTurnDegrees = 120.f;

	/** 前倾用的点：几根脊柱的原点与肩关节（全局）与每根分到的比例（加起来是 1）。spine_01 是最下面的 */
	struct FLeanChain
	{
		/** 脊柱骨骼索引，从腰部向上排列；只在当前求解期间有效。 */
		TArray<int32, TInlineAllocator<8>> Bones;

		/** 每根脊柱当前的组件空间位置（cm），用于预估旋转后的肩位置。 */
		TArray<FVector, TInlineAllocator<8>> Origins;

		/** 各段承担的转角比例，归一化后和为 1。 */
		TArray<float, TInlineAllocator<8>> Shares;

		/** 单肩或双肩加权中心的组件空间位置（cm）；每次求解从当前姿势重建。 */
		FVector Shoulder = FVector::ZeroVector;
	};

	/** 几根脊柱绕同一根轴各转 Angle × 比例（从下往上，每根绕自己的原点，上面的跟着），肩关节转到哪 */
	FVector ShoulderAfterLean(const FLeanChain& Chain, const FVector& Axis, const float Angle)
	{
		TArray<FVector, TInlineAllocator<8>> Points = Chain.Origins;
		FVector Shoulder = Chain.Shoulder;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			const FQuat Step(Axis, Angle * Chain.Shares[Index]);
			const FVector Pivot = Points[Index];
			for (int32 Above = Index + 1; Above < Points.Num(); ++Above)
			{
				Points[Above] = Pivot + Step.RotateVector(Points[Above] - Pivot);
			}
			Shoulder = Pivot + Step.RotateVector(Shoulder - Pivot);
		}
		return Shoulder;
	}

	/**
	 * 前倾：东西离肩关节超过 ComfortReach 时，几根脊柱绕同一根轴按 LeanShares 分着往东西那边转，转到肩关节离东西正好 ComfortReach
	 * （二分，最多 MaxLean），再乘 Weight 转过去
	 * @return 转了多少度（乘过 Weight 的）
	 */
	float ApplyLean(URigHierarchy& Hierarchy, const TArrayView<const FName> LeanBones, const TArrayView<const float> LeanShares, const FVector& Shoulder,
		const FVector& Item, const float ComfortReach, const float MaxLean, const float Weight)
	{
		FLeanChain Chain;
		for (const FName& Name : LeanBones)
		{
			const int32 Index = Hierarchy.GetIndex(FRigElementKey(Name, ERigElementType::Bone));
			if (Index == INDEX_NONE)
			{
				return 0.f;
			}
			Chain.Bones.Add(Index);
			Chain.Origins.Add(Hierarchy.GetGlobalTransform(Index).GetLocation());
		}
		if (Chain.Bones.IsEmpty() || !FAnimWeight::IsRelevant(Weight))
		{
			return 0.f;
		}
		float ShareSum = 0.f;
		for (const float Share : LeanShares)
		{
			ShareSum += FMath::Max(Share, 0.f);
		}
		bool bEven = LeanShares.Num() != LeanBones.Num() || ShareSum <= UE_SMALL_NUMBER;
#if !UE_BUILD_SHIPPING
		bEven |= CVarReachEvenLean.GetValueOnAnyThread() != 0;
#endif
		for (int32 Index = 0; Index < Chain.Bones.Num(); ++Index)
		{
			Chain.Shares.Add(bEven ? 1.f / Chain.Bones.Num() : FMath::Max(LeanShares[Index], 0.f) / ShareSum);
		}
		Chain.Shoulder = Shoulder;
		if (FVector::Dist(Chain.Shoulder, Item) <= ComfortReach)
		{
			return 0.f;
		}
		const FVector Axis = FVector::CrossProduct(Chain.Shoulder - Chain.Origins[0], Item - Chain.Shoulder).GetSafeNormal();
		if (Axis.IsZero())
		{
			return 0.f;
		}
		// 转得越多肩关节离东西越近（在 MaxLean 以内单调）：二分找正好 ComfortReach 的角度，转满还不够就转满
		const float Max = FMath::DegreesToRadians(MaxLean);
		float Angle = Max;
		if (FVector::Dist(ShoulderAfterLean(Chain, Axis, Max), Item) < ComfortReach)
		{
			float Low = 0.f;
			float High = Max;
			for (int32 Iteration = 0; Iteration < 16; ++Iteration)
			{
				const float Mid = 0.5f * (Low + High);
				(FVector::Dist(ShoulderAfterLean(Chain, Axis, Mid), Item) > ComfortReach ? Low : High) = Mid;
			}
			Angle = High;
		}
		for (int32 Index = 0; Index < Chain.Bones.Num(); ++Index)
		{
			const FQuat Step(Axis, Angle * Weight * Chain.Shares[Index]);
			FTransform Transform = Hierarchy.GetGlobalTransform(Chain.Bones[Index]);
			Transform.SetRotation((Step * Transform.GetRotation()).GetNormalized());
			Hierarchy.SetGlobalTransform(Chain.Bones[Index], Transform, /*bAffectChildren*/ true);
		}
		return FMath::RadiansToDegrees(Angle * Weight);
	}

	/** ginkgo.anim.reachTurnHand：手转不转向东西（发行版里总是转） */
	bool IsHandTurnEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarReachTurnHand.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}
}

const FRigElementKey& Ginkgo::Animation::HandBoneKey(const EGinkgoHand Hand)
{
	static const FRigElementKey Keys[2] = { FRigElementKey(TEXT("hand_l"), ERigElementType::Bone), FRigElementKey(TEXT("hand_r"), ERigElementType::Bone) };
	return Keys[static_cast<int32>(Hand)];
}

bool Ginkgo::Animation::ComputeHandReachTarget(const URigHierarchy& Hierarchy, const FGinkgoHandReachRigData& Reach, const EGinkgoHand Hand, const FTransform& ToWorld,
	FTransform& OutTarget)
{
	const int32 HandIndex = Hierarchy.GetIndex(HandBoneKey(Hand));
	if (!Reach.bActive || HandIndex == INDEX_NONE)
	{
		return false;
	}
	const FTransform HandNow = Hierarchy.GetGlobalTransform(HandIndex);
	FVector Item = ToWorld.InverseTransformPosition(Reach.Item);
	// 去抓乘方位约束的系数：东西出了扇形就慢慢不再伸过去（Item 已经转回扇形的边上）
	const float Grab = Reach.Grab * Reach.Bearing;

	// 手转向东西：片段里手伸出去的方向（肩关节到手里的挂点）转到肩关节指向东西的方向，按去抓的曲线混进来。手的朝向照片段时，
	// 握的一面朝身前、朝里，往外伸手腕要伸到东西外面去（往外下只够 39 cm，伸直的胳膊到挂点有 55–58 cm）；转过去以后手心总对着东西
	FQuat Rotation = HandNow.GetRotation();
	const int32 ShoulderIndex = Hierarchy.GetIndex(ShoulderBoneKey(Hand));
	// 够不着的拉回到胳膊能到的地方：只朝那个方向伸，全身 IK 不用为够不到的目标压弯背
	if (ShoulderIndex != INDEX_NONE && Reach.ArmLimit > 0.f)
	{
		const FVector Shoulder = Hierarchy.GetGlobalTransform(ShoulderIndex).GetLocation();
		const FVector Offset = Item - Shoulder;
		const double Distance = Offset.Size();
		if (Distance > Reach.ArmLimit)
		{
			Item = Shoulder + Offset * (SoftLimitReach(Distance, Reach.ArmLimit, Reach.ArmLimitMargin) / Distance);
		}
	}
	if (ShoulderIndex != INDEX_NONE && Grab > 0.f && IsHandTurnEnabled())
	{
		const FVector Shoulder = Hierarchy.GetGlobalTransform(ShoulderIndex).GetLocation();
		const FVector From = (HandNow.TransformPosition(Reach.ItemInHand) - Shoulder).GetSafeNormal();
		const FVector To = (Item - Shoulder).GetSafeNormal();
		if (!From.IsZero() && !To.IsZero())
		{
			FVector Axis;
			float Angle = 0.f;
			FQuat::FindBetweenNormals(From, To).ToAxisAndAngle(Axis, Angle);
			Angle = FMath::Min(Angle, FMath::DegreesToRadians(MaxHandTurnDegrees));
			Rotation = FQuat(Axis, Angle * Grab) * Rotation;
		}
	}

	// 手里的挂点（按转过的手）：去抓时拉到东西上，收回时拉到拾取物指定的收纳骨骼
	const FVector Held = HandNow.GetLocation() + Rotation.RotateVector(Reach.ItemInHand);
	FVector Location = HandNow.GetLocation() + Grab * (Item - Held);
	const int32 StowIndex = Hierarchy.GetIndex(FRigElementKey(Reach.StowBone, ERigElementType::Bone));
	if (StowIndex != INDEX_NONE)
	{
		Location += Reach.Place * (Hierarchy.GetGlobalTransform(StowIndex).TransformPosition(Reach.PocketInChest) - Held);
	}
	OutTarget = FTransform(Rotation, Location);
	return true;
}

bool Ginkgo::Animation::ComputeHandReachBlend(const URigHierarchy& Hierarchy, const FGinkgoWeaponRigData& Data, const EGinkgoHand Hand, const FTransform& ToWorld,
	FTransform& OutTarget, float& OutWeight)
{
	const int32 Index = static_cast<int32>(Hand);
	OutWeight = 0.f;
	bool bAny = false;
	const auto Add = [&](const FGinkgoHandReachRigData& Reach)
	{
		FTransform Target;
		if (!FAnimWeight::IsRelevant(Reach.Weight) || !ComputeHandReachTarget(Hierarchy, Reach, Hand, ToWorld, Target))
		{
			return;
		}
		// 已经混进来的占 OutWeight，这一个占 Reach.Weight
		const float Total = OutWeight + Reach.Weight;
		if (bAny)
		{
			FTransform Blended;
			Blended.Blend(OutTarget, Target, Reach.Weight / Total);
			OutTarget = Blended;
		}
		else
		{
			OutTarget = Target;
		}
		OutWeight = Total;
		bAny = true;
	};
	for (const FGinkgoHandReachRigData& Fading : Data.FadingReach[Index])
	{
		Add(Fading);
	}
	Add(Data.Reach[Index]);
	OutWeight = FMath::Min(OutWeight, 1.f);
	return bAny;
}

float Ginkgo::Animation::GetHandReachWeight(const FGinkgoWeaponRigData& Data, const EGinkgoHand Hand)
{
	const int32 Index = static_cast<int32>(Hand);
	float Weight = Data.Reach[Index].bActive ? Data.Reach[Index].Weight : 0.f;
	for (const FGinkgoHandReachRigData& Fading : Data.FadingReach[Index])
	{
		Weight += Fading.Weight;
	}
	return FMath::Min(Weight, 1.f);
}

FRigUnit_GinkgoHandReach::FRigUnit_GinkgoHandReach()
{
	LeanBones = { TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") };
	LeanShares = { 0.45f, 0.35f, 0.15f, 0.05f, 0.f };
}

FRigUnit_GinkgoHandReach_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	bActive = false;
	Bone = Ginkgo::Animation::HandBoneKey(EGinkgoHand::Left).Name;
	Alpha = 0.f;
	LeanAngle = 0.f;
	SpineStiffnessUsed = 1.f;
	ClavicleStiffnessUsed = ClavicleStiffness;
	bAllowStretchUsed = bAllowStretch;
	ArmStretchStiffnessUsed = ArmStretchStiffness;
	PullChainAlphaUsed = PullChainAlpha;
	IterationsUsed = Iterations;
	MassMultiplierUsed = MassMultiplier;
#if !UE_BUILD_SHIPPING
	if (CVarReachLean.GetValueOnAnyThread() == 0)
	{
		return;
	}
	const auto Override = [](const float Value, const float Fallback) { return Value >= 0.f ? FMath::Clamp(Value, 0.f, 1.f) : Fallback; };
	SpineStiffnessUsed = Override(CVarReachSpineStiffness.GetValueOnAnyThread(), 1.f);
	ClavicleStiffnessUsed = Override(CVarReachClavicleStiffness.GetValueOnAnyThread(), ClavicleStiffness);
	ArmStretchStiffnessUsed = Override(CVarReachArmStretchStiffness.GetValueOnAnyThread(), ArmStretchStiffness);
	PullChainAlphaUsed = Override(CVarReachPullChainAlpha.GetValueOnAnyThread(), PullChainAlpha);
	const int32 IterationsOverride = CVarReachIterations.GetValueOnAnyThread();
	IterationsUsed = IterationsOverride > 0 ? IterationsOverride : Iterations;
	const float MassOverride = CVarReachMassMultiplier.GetValueOnAnyThread();
	MassMultiplierUsed = MassOverride >= 0.f ? MassOverride : MassMultiplier;
	const int32 Stretch = CVarReachStretch.GetValueOnAnyThread();
	bAllowStretchUsed = Stretch >= 0 ? Stretch != 0 : bAllowStretch;
#endif

	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	const USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(ExecuteContext.GetOwningComponent());
	const UGinkgoAnimInstance* Instance = Mesh ? Cast<UGinkgoAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Hierarchy || !Instance)
	{
		return;
	}
	// 两只手可以同时在伸（一只手占着时第二下用另一只手，Docs/Design/AnimActions.md「占用」）。这一遍全身 IK 只有一个效应器，给权重大的那只手；
	// 同一只手连着拾取时被顶掉的还在淡出，权重加起来
	const FGinkgoWeaponRigData& Data = Instance->GetWeaponRigData();
	if (Data.Traversal.bActive)
	{
		// 攀爬按双肩和双抓点的加权中心统一弯腰，不能先后向两手各转一遍而叠出侧扭。
		// 只改脊柱，骨盆与腿仍由全身片段驱动；后面的双臂 IK 在弯过的肩上贴合固定抓点。
		const auto& Hands = Data.Traversal;
		const float Sum = Hands.LeftAlpha + Hands.RightAlpha;
		const int32 Left = Hierarchy->GetIndex(ShoulderBoneKey(EGinkgoHand::Left));
		const int32 Right = Hierarchy->GetIndex(ShoulderBoneKey(EGinkgoHand::Right));
		if (Sum > UE_KINDA_SMALL_NUMBER && Left != INDEX_NONE && Right != INDEX_NONE)
		{
			const FVector Shoulder = (Hierarchy->GetGlobalTransform(Left).GetLocation() * Hands.LeftAlpha
				+ Hierarchy->GetGlobalTransform(Right).GetLocation() * Hands.RightAlpha) / Sum;
			const FVector Contact = ExecuteContext.GetToWorldSpaceTransform().InverseTransformPosition((Hands.Left * Hands.LeftAlpha + Hands.Right * Hands.RightAlpha) / Sum);
			LeanAngle = ApplyLean(*Hierarchy, LeanBones, LeanShares, Shoulder, Contact, Hands.ArmReach * .85f, Hands.MaxBodyLean,
				FMath::Max(Hands.LeftAlpha, Hands.RightAlpha) * Hands.Weight);
		}
		return;
	}
	const float LeftWeight = Ginkgo::Animation::GetHandReachWeight(Data, EGinkgoHand::Left);
	const float RightWeight = Ginkgo::Animation::GetHandReachWeight(Data, EGinkgoHand::Right);
	const EGinkgoHand Hand = RightWeight > LeftWeight ? EGinkgoHand::Right : EGinkgoHand::Left;
	if (!FAnimWeight::IsRelevant(FMath::Max(LeftWeight, RightWeight)))
	{
		return;
	}

	// 前倾：先把上半身往东西那边转过去，伸手的目标从转过的手算。几个在伸时各按各的权重叠（被顶掉的先）
	float Comfort = ComfortReach;
	float Lean = MaxLean;
	bool bBend = true;
#if !UE_BUILD_SHIPPING
	bBend = CVarReachBend.GetValueOnAnyThread() != 0;
	Comfort = CVarReachComfortReach.GetValueOnAnyThread() >= 0.f ? CVarReachComfortReach.GetValueOnAnyThread() : Comfort;
	Lean = CVarReachMaxLean.GetValueOnAnyThread() >= 0.f ? CVarReachMaxLean.GetValueOnAnyThread() : Lean;
#endif
	if (bBend)
	{
		for (const EGinkgoHand Side : {EGinkgoHand::Left, EGinkgoHand::Right})
		{
			const auto LeanToward = [&](const FGinkgoHandReachRigData& Reach)
			{
				const int32 ShoulderIndex = Hierarchy->GetIndex(ShoulderBoneKey(Side));
				if (Reach.bActive && ShoulderIndex != INDEX_NONE)
				{
					LeanAngle += ApplyLean(*Hierarchy, LeanBones, LeanShares, Hierarchy->GetGlobalTransform(ShoulderIndex).GetLocation(), ExecuteContext.GetToWorldSpaceTransform().InverseTransformPosition(Reach.Item),
						Comfort, Lean, Reach.Weight * Reach.Grab * Reach.Bearing);
				}
			};
			const int32 SideIndex = static_cast<int32>(Side);
			for (const FGinkgoHandReachRigData& Fading : Data.FadingReach[SideIndex])
			{
				LeanToward(Fading);
			}
			LeanToward(Data.Reach[SideIndex]);
		}
	}
	if (!Ginkgo::Animation::ComputeHandReachBlend(*Hierarchy, Data, Hand, ExecuteContext.GetToWorldSpaceTransform(), Target, Alpha))
	{
		return;
	}
	// 另一只手也在伸时只用两个权重的差：换手的那一刻（权重相等）这一遍不起作用，效应器换手不跳。只有一只手时就是它的权重
	Alpha = FMath::Max(Alpha - FMath::Min(LeftWeight, RightWeight), 0.f);
	bActive = true;
	Bone = Ginkgo::Animation::HandBoneKey(Hand).Name;
}

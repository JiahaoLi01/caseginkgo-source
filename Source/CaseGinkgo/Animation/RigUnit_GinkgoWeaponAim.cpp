#include "Animation/RigUnit_GinkgoWeaponAim.h"
#include "Throwables/GinkgoFlashbangComponent.h"

#include "Animation/AnimTypes.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Animation/RigUnit_GinkgoHandReach.h"  // 伸手的目标
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Units/RigUnitContext.h"

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarWeaponAim(
		TEXT("ginkgo.anim.weaponAim"),
		1,
		TEXT("持枪的枪口对准与双手的 IK：1 开，0 关（姿势照层与瞄准偏移的，双手的权重为 0；只用来对照）。"));
#endif

	/** ginkgo.anim.weaponAim：开没开（发行版里总是开） */
	bool IsWeaponAimEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarWeaponAim.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	/**
	 * 枪的 X 轴转到要的方向的最小转角（组件空间）。越肩：枪口 → 瞄准点。第一人称开镜（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：
	 * 瞄具的光轴平行于枪的 X 轴，眼睛（相机）就在光轴的出瞳上，所以 X 轴平行于瞄准方向就对准了，按 SightAlpha 从前者混过去
	 * @param AimDirection - 瞄准方向（组件空间，单位向量）
	 */
	FQuat AimResidual(const FTransform& Gun, const FGinkgoWeaponRigData& Data, const FVector& AimPoint, const FVector& AimDirection)
	{
		FVector Desired = (AimPoint - Gun.TransformPosition(Data.MuzzleOffset)).GetSafeNormal();
		if (Data.SightAlpha > 0.f)
		{
			Desired = FMath::Lerp(Desired, AimDirection, static_cast<double>(FMath::Min(Data.SightAlpha, 1.f))).GetSafeNormal();
		}
		return Desired.IsZero() ? FQuat::Identity : FQuat::FindBetweenNormals(Gun.GetUnitAxis(EAxis::X), Desired);
	}

	/** 这一下转角 Delta 限到加上前几遍已经转了的 Applied 不超过 MaxDegrees */
	FQuat ClampRotation(const FQuat& Delta, const FQuat& Applied, const double MaxDegrees)
	{
		const FQuat Total = Delta * Applied;
		const double Degrees = FMath::RadiansToDegrees(Total.GetAngle());
		if (Degrees <= MaxDegrees)
		{
			return Delta;
		}
		return FQuat::Slerp(FQuat::Identity, Total, MaxDegrees / Degrees) * Applied.Inverse();
	}

	/**
	 * 伸手（拾取，Docs/Design/Interaction.md「拾取子弹盒」）：这只手的目标从原来的（枪上的握法，或空手时动画的手、权重 0）换到伸手的目标
	 * （Ginkgo::Animation::ComputeHandReachBlend：连着拾取时被顶掉的还在淡出，几个按蒙太奇实例的权重混），按它们的权重。
	 * 前面的伸手节点让上半身弯过去时，这里从弯过的手出发，剩下的由后面只动胳膊的全身 IK 补
	 */
	void ApplyHandReach(const URigHierarchy& Hierarchy, const FGinkgoWeaponRigData& Data, const EGinkgoHand Hand, const FTransform& ToWorld,
		FTransform& InOutTarget, float& InOutAlpha)
	{
		FTransform ReachTarget;
		float Weight = 0.f;
		if (!Ginkgo::Animation::ComputeHandReachBlend(Hierarchy, Data, Hand, ToWorld, ReachTarget, Weight))
		{
			return;
		}
		FTransform Blended;
		Blended.Blend(InOutTarget, ReachTarget, Weight);
		InOutTarget = Blended;
		InOutAlpha = FMath::Lerp(InOutAlpha, 1.f, Weight);
	}

	/** 用同帧的骨盆解抓取；遮眼的腕点与手指朝向随头肩移动，兼容所有重定向外观。 */
	void ApplyFlashArm(const URigHierarchy& Hierarchy, const FGinkgoWeaponRigData& Data, const FTransform& ToWorld, FTransform& Target, float& Alpha)
	{
		const auto& Pose = Data.FlashArm;
		if (Pose.Throw <= 0.f && Pose.Cover <= 0.f) return;
		const auto Bone = [&Hierarchy](FName Name) { return Hierarchy.GetGlobalTransform(FRigElementKey(Name, ERigElementType::Bone)); };
		const FTransform Hand = Bone(TEXT("hand_l"));
		// 放掉左手持枪 IK，保留 ArmL 蒙太奇里的肩、肘、手指。
		Alpha *= 1.f - Pose.Throw;
		FTransform Aim;
		Aim.Blend(Hand, Target, Alpha);
		if (Pose.Belt > 0.f)
		{
			const FTransform Grip = UGinkgoFlashbangComponent::GripSocket().Inverse() * UGinkgoFlashbangComponent::BeltSocket() * Bone(TEXT("pelvis"));
			Target.Blend(Aim, Grip, Pose.Belt);
			Alpha = 1.f;
		}
		if (Pose.Release > 0.f)
		{
			// 弹体与手的挂点在出手帧重合，保留投掷片段的腕部旋转。
			Target = Aim;
			const FVector Release = ToWorld.InverseTransformPosition(Pose.ReleasePoint);
			Target.AddToTranslation(Pose.Release * (Release - Aim.TransformPosition(UGinkgoFlashbangComponent::GripSocket().GetLocation())));
			Alpha = 1.f;
		}
		if (Pose.Cover > 0.f)
		{
			const FVector Head = Bone(TEXT("head")).GetLocation();
			const FVector Right = (Bone(TEXT("upperarm_r")).GetLocation() - Bone(TEXT("upperarm_l")).GetLocation()).GetSafeNormal();
			const FVector Up = (Head - Bone(TEXT("neck_01")).GetLocation()).GetSafeNormal();
			const FVector Forward = FVector::CrossProduct(Right, Up).GetSafeNormal();
			const FVector Fingers = (Bone(TEXT("middle_01_l")).GetLocation() - Hand.GetLocation()).GetSafeNormal();
			const FQuat Turn = FQuat::FindBetweenNormals(Fingers, (Right + .2 * Up).GetSafeNormal());
			const FTransform Shield((Turn * Hand.GetRotation()).GetNormalized(), Head + 13.0 * Forward + 8.0 * Up - 4.0 * Right);
			Target.Blend(Aim, Shield, FMath::SmoothStep(0.f, 1.f, Pose.Cover));
			Alpha = 1.f;
		}
	}

	/** 攀爬手部覆盖随全身权重连续进入/退出；窗口只控制贴墙，不负责恢复持枪。 */
	void ApplyTraversalHands(const URigHierarchy& Hierarchy, const FGinkgoWeaponRigData& Data, const FTransform& ToWorld,
		FTransform& LeftTarget, float& LeftAlpha, FTransform& RightTarget, float& RightAlpha)
	{
		const auto& Pose = Data.Traversal;
		if (!Pose.bActive || Pose.Weight <= 0.f) { return; }
		for (const EGinkgoHand Hand : {EGinkgoHand::Left, EGinkgoHand::Right})
		{
			const bool bLeft = Hand == EGinkgoHand::Left;
			if (!bLeft && !Pose.bTwoHanded && Data.bArmed && Data.bGunInHand) { continue; }
			const int32 HandIndex = Hierarchy.GetIndex(Ginkgo::Animation::HandBoneKey(Hand));
			const int32 ShoulderIndex = Hierarchy.GetIndex(FRigElementKey(bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r"), ERigElementType::Bone));
			if (HandIndex == INDEX_NONE || ShoulderIndex == INDEX_NONE) { continue; }
			const FTransform Animated = Hierarchy.GetGlobalTransform(HandIndex);
			FTransform Contact = Animated;
			const FVector Shoulder = Hierarchy.GetGlobalTransform(ShoulderIndex).GetLocation();
			const FVector Wanted = ToWorld.InverseTransformPosition(bLeft ? Pose.Left : Pose.Right);
			Contact.SetLocation(Shoulder + (Wanted - Shoulder).GetClampedToMaxSize(Pose.ArmReach));
			const FQuat Tilt = FQuat::FindBetweenNormals(FVector::UpVector, bLeft ? Pose.LeftNormal : Pose.RightNormal);
			const FQuat Rotation = ToWorld.GetRotation();
			Contact.SetRotation((Rotation.Inverse() * Tilt * Rotation * Animated.GetRotation()).GetNormalized());
			FTransform Traversal;
			Traversal.Blend(Animated, Contact, bLeft ? Pose.LeftAlpha : Pose.RightAlpha);
			FTransform& Target = bLeft ? LeftTarget : RightTarget;
			float& Alpha = bLeft ? LeftAlpha : RightAlpha;
			FTransform Blended;
			Blended.Blend(Target, Traversal, Pose.Weight);
			Target = Blended;
			Alpha = FMath::Lerp(Alpha, 1.f, Pose.Weight);
		}
	}

	/**
	 * 骨骼绕自己的位置在全局里转 Delta，子骨骼跟着。用按序号的重载：没有接 FCachedRigElement 的，它会隐式转成序号，
	 * 后面的布尔参数就错位了（第三个是 bAffectChildren，不是 bInitial）
	 */
	void RotateBone(URigHierarchy& Hierarchy, const FCachedRigElement& Bone, const FQuat& Delta)
	{
		FTransform Transform = Hierarchy.GetGlobalTransform(Bone);
		Transform.SetRotation((Delta * Transform.GetRotation()).GetNormalized());
		Hierarchy.SetGlobalTransform(Bone.GetIndex(), Transform, /*bAffectChildren*/ true);
	}
}

FRigUnit_GinkgoWeaponAim::FRigUnit_GinkgoWeaponAim()
{
	SpineBones = { TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05") };
	GunBone = TEXT("VB spine_05_ik_hand_gun");
	LeftHandCurve = TEXT("HandIK_L");
	RightHandCurve = TEXT("HandIK_R");
}

FRigUnit_GinkgoWeaponAim_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	LeftHandAlpha = 0.f;
	RightHandAlpha = 0.f;
	URigHierarchy* Hierarchy = ExecuteContext.Hierarchy;
	const USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(ExecuteContext.GetOwningComponent());
	UGinkgoAnimInstance* Instance = Mesh ? Cast<UGinkgoAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Hierarchy || !Instance || !WorkData.CachedGun.UpdateCache(FRigElementKey(GunBone, ERigElementType::Bone), Hierarchy))
	{
		return;
	}
	const FGinkgoWeaponRigData& Data = Instance->GetWeaponRigData();
	const FTransform& ComponentTransform = ExecuteContext.GetToWorldSpaceTransform();
	if (WorkData.CachedSpine.Num() != SpineBones.Num())
	{
		WorkData.CachedSpine.SetNum(SpineBones.Num());
	}
	for (int32 Index = 0; Index < SpineBones.Num(); ++Index)
	{
		WorkData.CachedSpine[Index].UpdateCache(FRigElementKey(SpineBones[Index], ERigElementType::Bone), Hierarchy);
	}
	const auto ApplyBenchLean = [&](const bool bCarryHeldGun)
	{
		if (Data.BenchInstall.BodyLean <= 0.f)
		{
			return;
		}
		// 解双臂前先让肩随施力方向向工作台目标探过去，方向用世界目标转到角色网格空间。
		const FVector LeanPoint = Data.BenchInstall.bHasLeanTarget
			? Data.BenchInstall.LeanTarget
			: (Data.BenchInstall.bHasLeftTarget && Data.BenchInstall.LeftGrip > UE_SMALL_NUMBER
				? Data.BenchInstall.WristLocation
				: (Data.BenchInstall.bHasRightTarget ? Data.BenchInstall.RightWristLocation : Data.BenchInstall.WristLocation));
		const FVector TowardPart = (LeanPoint - ComponentTransform.GetLocation()).GetSafeNormal2D();
		const FVector Axis = ComponentTransform.InverseTransformVectorNoScale(FVector::CrossProduct(FVector::UpVector, TowardPart));
		if (Axis.IsNearlyZero())
		{
			return;
		}
		const int32 TopSpineIndex = WorkData.CachedSpine.FindLastByPredicate([](const FCachedRigElement& Bone) { return Bone.IsValid(); });
		const FCachedRigElement* TopSpine = TopSpineIndex != INDEX_NONE ? &WorkData.CachedSpine[TopSpineIndex] : nullptr;
		const FTransform SpineBefore = bCarryHeldGun && TopSpine ? Hierarchy->GetGlobalTransform(*TopSpine) : FTransform::Identity;
		static const FRigElementKey LowerSpine(TEXT("spine_01"), ERigElementType::Bone);
		if (WorkData.CachedLowerSpine.UpdateCache(LowerSpine, Hierarchy))
		{
			const FQuat Bend(Axis, FMath::DegreesToRadians(23.0 * Data.BenchInstall.BodyLean));
			RotateBone(*Hierarchy, WorkData.CachedLowerSpine, Bend);
		}
		const FQuat Lean(Axis, FMath::DegreesToRadians(6.0 * Data.BenchInstall.BodyLean));
		for (const auto& Spine : WorkData.CachedSpine)
		{
			if (Spine.IsValid()) { RotateBone(*Hierarchy, Spine, Lean); }
		}
		// 持枪开关台灯时枪仍在右手，ik_hand_gun 挂在 root 下，让它随上胸一起倾斜以避免右肩脱开枪托。
		if (bCarryHeldGun && TopSpine)
		{
			const FTransform SpineAfter = Hierarchy->GetGlobalTransform(*TopSpine);
			const FTransform GunRelative = Hierarchy->GetGlobalTransform(WorkData.CachedGun).GetRelativeTransform(SpineBefore);
			Hierarchy->SetGlobalTransform(WorkData.CachedGun.GetIndex(), GunRelative * SpineAfter, true);
		}
	};

#if !UE_BUILD_SHIPPING
	UGinkgoAnimInstance::FDebugFrame::FWeaponAim Report;
#endif
	if (!Data.bArmed || !IsWeaponAimEnabled())
	{
		ApplyBenchLean(false);
		// 不拿枪：只有伸手与工作台左手操作。原来的目标是动画的手、权重 0，伸手的那只手按权重换过去
		for (const EGinkgoHand Hand : { EGinkgoHand::Left, EGinkgoHand::Right })
		{
			const int32 HandIndex = Hierarchy->GetIndex(Ginkgo::Animation::HandBoneKey(Hand));
			FTransform& Target = Hand == EGinkgoHand::Left ? LeftHandTarget : RightHandTarget;
			float& Alpha = Hand == EGinkgoHand::Left ? LeftHandAlpha : RightHandAlpha;
			Target = HandIndex != INDEX_NONE ? Hierarchy->GetGlobalTransform(HandIndex) : FTransform::Identity;
			if (Hand == EGinkgoHand::Left && Data.BenchInstall.LeftGrip > 0.f)
			{
				Target.SetLocation(ComponentTransform.InverseTransformPosition(Data.BenchInstall.WristLocation));
				const FQuat MeshRotation = ComponentTransform.GetRotation();
				const FQuat WristDelta = MeshRotation.Inverse() * Data.BenchInstall.WristRotationDelta * MeshRotation;
				Target.SetRotation((WristDelta * Target.GetRotation()).GetNormalized());
				Alpha = Data.BenchInstall.LeftGrip;
			}
			ApplyHandReach(*Hierarchy, Data, Hand, ComponentTransform, Target, Alpha);
		}
		ApplyFlashArm(*Hierarchy, Data, ComponentTransform, LeftHandTarget, LeftHandAlpha);
		ApplyTraversalHands(*Hierarchy, Data, ComponentTransform, LeftHandTarget, LeftHandAlpha, RightHandTarget, RightHandAlpha);
#if !UE_BUILD_SHIPPING
		Report.LeftTarget = ComponentTransform.TransformPosition(LeftHandTarget.GetLocation());
		Report.RightTarget = ComponentTransform.TransformPosition(RightHandTarget.GetLocation());
		Report.LeftAlpha = LeftHandAlpha;
		Report.RightAlpha = RightHandAlpha;
		Instance->ReportWeaponAim(Report);
#endif
		return;
	}

	const FVector AimPoint = ComponentTransform.InverseTransformPosition(Data.AimPoint);
	const FVector AimDirection = ComponentTransform.InverseTransformVectorNoScale(Data.AimDirection).GetSafeNormal();
#if !UE_BUILD_SHIPPING
	if (Data.bHasAimPoint)
	{
		Report.ResidualBefore = FMath::RadiansToDegrees(AimResidual(Hierarchy->GetGlobalTransform(WorkData.CachedGun), Data, AimPoint, AimDirection).GetAngle());
	}
#endif

	// 1. 枪口对准：按满的算（几根脊柱各分一份，枪补剩下的，几遍），再按抬枪进度从输入的姿势混过去
	if (Data.bHasAimPoint && FAnimWeight::IsRelevant(Data.AimAlpha))
	{
		TArray<const FCachedRigElement*, TInlineAllocator<8>> Bones;
		for (const FCachedRigElement& Spine : WorkData.CachedSpine)
		{
			if (Spine.IsValid())
			{
				Bones.Add(&Spine);
			}
		}
		const int32 SpineCount = Bones.Num();
		Bones.Add(&WorkData.CachedGun);
		// 两次使用同一套求解：参考分支只量未探身枪口，实际分支才留下最终姿势。
		// 不重新运行整张图、腿 IK 或双臂 IK，也不复制一套容易与实枪漂移的瞄准算法。
		const auto SolveAim = [&](const bool bReport)
		{
			TArray<FTransform, TInlineAllocator<8>> InputLocal;
			for (const FCachedRigElement* Bone : Bones)
			{
				InputLocal.Add(Hierarchy->GetLocalTransform(*Bone));
			}
			TArray<FQuat, TInlineAllocator<8>> Applied;
			Applied.Init(FQuat::Identity, Bones.Num());
			for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
			{
				const FQuat Share = FQuat::Slerp(FQuat::Identity, AimResidual(Hierarchy->GetGlobalTransform(WorkData.CachedGun), Data, AimPoint, AimDirection), SpineShare);
				for (int32 Index = 0; Index < SpineCount; ++Index)
				{
					const FQuat Delta = ClampRotation(Share, Applied[Index], MaxSpineAngle);
					RotateBone(*Hierarchy, *Bones[Index], Delta);
					Applied[Index] = Delta * Applied[Index];
				}
				const FQuat GunDelta = ClampRotation(AimResidual(Hierarchy->GetGlobalTransform(WorkData.CachedGun), Data, AimPoint, AimDirection), Applied[SpineCount], MaxGunAngle);
				RotateBone(*Hierarchy, WorkData.CachedGun, GunDelta);
				Applied[SpineCount] = GunDelta * Applied[SpineCount];
			}
#if !UE_BUILD_SHIPPING
			if (bReport)
			{
				for (int32 Index = 0; Index < SpineCount; ++Index)
				{
					Report.SpineAngle += FMath::RadiansToDegrees(Applied[Index].GetAngle());
				}
				Report.GunAngle = FMath::RadiansToDegrees(Applied[SpineCount].GetAngle());
			}
#endif
			// 局部的旋转从下往上混：子骨骼的局部变换不随父骨骼变，先读后改也一样
			if (Data.AimAlpha < 1.f)
			{
				for (int32 Index = 0; Index < Bones.Num(); ++Index)
				{
					FTransform Blended = Hierarchy->GetLocalTransform(*Bones[Index]);
					Blended.SetRotation(FQuat::Slerp(InputLocal[Index].GetRotation(), Blended.GetRotation(), Data.AimAlpha).GetNormalized());
					Hierarchy->SetLocalTransform(Bones[Index]->GetIndex(), Blended, /*bAffectChildren*/ true);
				}
			}
		};

		const FGinkgoPeekReference& Reference = Instance->GetPeekReference();
		const TConstArrayView<FName> ReferenceNames = Ginkgo::Animation::PeekBoneNames();
		const int32 PelvisIndex = Hierarchy->GetIndex(FRigElementKey(ReferenceNames[0], ERigElementType::Bone));
		bool bCanProbe = Instance->IsPeekProbeRequested() && Reference.bValid && SpineCount == 3 && PelvisIndex != INDEX_NONE
			&& GunBone == ReferenceNames[4];
		for (int32 Index = 0; bCanProbe && Index < SpineCount; ++Index)
		{
			bCanProbe &= SpineBones[Index] == ReferenceNames[Index + 1];
		}
		if (bCanProbe)
		{
			TArray<FTransform, TInlineAllocator<4>> SavedLocal;
			for (const FCachedRigElement* Bone : Bones)
			{
				SavedLocal.Add(Hierarchy->GetLocalTransform(*Bone));
			}
			const FTransform Pelvis = Hierarchy->GetGlobalTransform(PelvisIndex);
			for (int32 Index = 0; Index < Bones.Num(); ++Index)
			{
				// 骨盆承接了转根和地形修正，但未受上半身探身影响，因此用它把参考姿势带到当前 Rig 空间。
				const FTransform Neutral = Reference.Bones[Index + 1].GetRelativeTransform(Reference.Bones[0]) * Pelvis;
				Hierarchy->SetGlobalTransform(Bones[Index]->GetIndex(), Neutral, /*bAffectChildren*/ true);
			}
			SolveAim(false);
			Instance->RecordPeekGun(Hierarchy->GetGlobalTransform(WorkData.CachedGun));
			for (int32 Index = 0; Index < Bones.Num(); ++Index)
			{
				Hierarchy->SetLocalTransform(Bones[Index]->GetIndex(), SavedLocal[Index], /*bAffectChildren*/ true);
			}
		}
		SolveAim(true);
	}

	// 枪的踢动（程序化的后坐）：在枪口对准之后，枪口对准不把它修回去；手的目标在下面，跟着枪走，肩膀也就被顶回去（全身 IK 只解锁骨与胳膊）
#if !UE_BUILD_SHIPPING
	if (Data.bHasAimPoint)
	{
		Report.ResidualAfter = FMath::RadiansToDegrees(AimResidual(Hierarchy->GetGlobalTransform(WorkData.CachedGun), Data, AimPoint, AimDirection).GetAngle());
	}
#endif
	Hierarchy->SetGlobalTransform(WorkData.CachedGun.GetIndex(), Data.GunKick * Hierarchy->GetGlobalTransform(WorkData.CachedGun), /*bAffectChildren*/ true);

	// 捡枪抓住以后的收拢（FGinkgoWeaponRigData::PickupSettle，Docs/Design/Interaction.md「丢枪与捡枪」）：枪先在右手里。右手照空手时那样
	// 走（片段的手，伸手按它的权重拉到东西上：同空手时的目标与权重解出来的位置）；枪保持抓住时的朝向，被握的那一点在手的挂点上，按进度
	// 转到持枪的位置，枪口从身前扫过去（Ginkgo::Weapon::BlendPickupSettle）。左手的目标跟着这把枪；右手的目标是一起算出来的：挂点跟着
	// 片段的手，朝向按抓住时的握法跟着枪转，右手的伸手已经算在里面，下面不再叠
	TOptional<FTransform> SettleHand;
	if (Data.PickupSettle < 1.f)
	{
		const int32 HandIndex = Hierarchy->GetIndex(Ginkgo::Animation::HandBoneKey(EGinkgoHand::Right));
		if (HandIndex != INDEX_NONE)
		{
			const FTransform Animated = Hierarchy->GetGlobalTransform(HandIndex);
			FTransform Reached = Animated;
			float ReachAlpha = 0.f;
			ApplyHandReach(*Hierarchy, Data, EGinkgoHand::Right, ComponentTransform, Reached, ReachAlpha);
			FTransform Hand;
			Hand.Blend(Animated, Reached, ReachAlpha);
			const FGinkgoPickupSettlePose Settle = Ginkgo::Weapon::BlendPickupSettle(Data.PickupStart, Data.BodyRotation, Hand,
				Hierarchy->GetGlobalTransform(WorkData.CachedGun), Data.Hands.Right, Data.PickupSettle);
			Hierarchy->SetGlobalTransform(WorkData.CachedGun.GetIndex(), Settle.Gun, /*bAffectChildren*/ true);
			SettleHand = Settle.RightHand;
		}
	}

	ApplyBenchLean(Data.BenchGunAlpha <= 0.f && Data.bGunInHand);
	// 放枪、取枪共用同一个桌面目标。先摆枪骨，下方的双手从枪的最终变换取握点。
	if (Data.BenchGunAlpha > 0.f)
	{
		FTransform Placed;
		Placed.Blend(Hierarchy->GetGlobalTransform(WorkData.CachedGun), Data.BenchRest.GetRelativeTransform(ComponentTransform), Data.BenchGunAlpha);
		Hierarchy->SetGlobalTransform(WorkData.CachedGun.GetIndex(), Placed, true);
	}
	// 2. 双手的目标与权重：枪的最终变换乘手在枪上的；枪上的蒙太奇放着时按它的曲线。枪还在背上时（拔枪开始、收枪的最后）只听曲线：
	// 片段里枪挂的骨骼在背上的插槽那里，手要去拿它时曲线才起来。单手的举枪冲刺左手照常握枪的那一份随冲刺持枪的权重放到 0
	if (Data.Traversal.bActive && !Data.Traversal.bTwoHanded && Data.bGunInHand)
	{
		// 单手动作中枪跟随原片段的右手，保持握把相对手腕的变换；淡出时连续回到普通持枪骨。
		const int32 Right = Hierarchy->GetIndex(Ginkgo::Animation::HandBoneKey(EGinkgoHand::Right));
		if (Right != INDEX_NONE)
		{
			FTransform Carried;
			Carried.Blend(Hierarchy->GetGlobalTransform(WorkData.CachedGun), Data.Hands.Right.Inverse() * Hierarchy->GetGlobalTransform(Right), Data.Traversal.Weight);
			Hierarchy->SetGlobalTransform(WorkData.CachedGun.GetIndex(), Carried, true);
		}
	}
	const FTransform Gun = Hierarchy->GetGlobalTransform(WorkData.CachedGun);
	// 左手在跟着枪机走的护木上（泵动霰弹枪）：沿枪往后挪护木走了的距离（枪的 −X）
	LeftHandTarget = Data.Hands.Left * FTransform(FVector(-Data.LeftHandSlide, 0.0, 0.0)) * Gun;
	RightHandTarget = SettleHand ? *SettleHand : Data.Hands.Right * Gun;
	const float Base = Data.bGunInHand ? 1.f - Data.GunMontageWeight : 0.f;
	LeftHandAlpha = FMath::Clamp(Base * Data.LeftHandGrip + Hierarchy->GetCurveValue(FRigElementKey(LeftHandCurve, ERigElementType::Curve)), 0.f, 1.f);
	RightHandAlpha = SettleHand ? 1.f : FMath::Clamp(Base + Hierarchy->GetCurveValue(FRigElementKey(RightHandCurve, ERigElementType::Curve)), 0.f, 1.f);
	if (Data.BenchGunAlpha > 0.f)
	{
		LeftHandAlpha = RightHandAlpha = Data.BenchGrip;
	}
	const auto ApplyBenchHand = [&](FTransform& InOutTarget, float& InOutAlpha, const float Grip, const bool bHasTarget, const FVector& WorldWrist, const FQuat& WorldDelta)
	{
		if (Grip <= 0.f)
		{
			return;
		}
		if (!bHasTarget)
		{
			InOutAlpha = Grip;
			return;
		}
		// 桌前操作给世界空间增量；枪已放桌时（InOutAlpha 为 0）直接采用目标，持枪拨灯时从护木平滑混到开关再收回。
		const FQuat MeshRotation = ComponentTransform.GetRotation();
		const FQuat WristDelta = MeshRotation.Inverse() * WorldDelta * MeshRotation;
		FTransform BenchTarget = InOutTarget;
		BenchTarget.SetLocation(ComponentTransform.InverseTransformPosition(WorldWrist));
		BenchTarget.SetRotation((WristDelta * InOutTarget.GetRotation()).GetNormalized());
		if (InOutAlpha > UE_SMALL_NUMBER)
		{
			InOutTarget.Blend(InOutTarget, BenchTarget, Grip);
			InOutAlpha = FMath::Lerp(InOutAlpha, 1.f, Grip);
		}
		else
		{
			InOutTarget = BenchTarget;
			InOutAlpha = Grip;
		}
	};
	ApplyBenchHand(LeftHandTarget, LeftHandAlpha, Data.BenchInstall.LeftGrip, Data.BenchInstall.bHasLeftTarget,
		Data.BenchInstall.WristLocation, Data.BenchInstall.WristRotationDelta);
	ApplyBenchHand(RightHandTarget, RightHandAlpha, Data.BenchInstall.RightGrip, Data.BenchInstall.bHasRightTarget,
		Data.BenchInstall.RightWristLocation, Data.BenchInstall.RightWristRotationDelta);
	// 伸手（拾取）：拿着枪时是左手，从枪上的握法换到伸手的目标，右手照常握枪。收拢时右手的伸手已经在上面的目标里了
	ApplyHandReach(*Hierarchy, Data, EGinkgoHand::Left, ComponentTransform, LeftHandTarget, LeftHandAlpha);
	if (!SettleHand)
	{
		ApplyHandReach(*Hierarchy, Data, EGinkgoHand::Right, ComponentTransform, RightHandTarget, RightHandAlpha);
	}

	ApplyFlashArm(*Hierarchy, Data, ComponentTransform, LeftHandTarget, LeftHandAlpha);
	ApplyTraversalHands(*Hierarchy, Data, ComponentTransform, LeftHandTarget, LeftHandAlpha, RightHandTarget, RightHandAlpha);

#if !UE_BUILD_SHIPPING
	Report.bArmed = true;
	Report.AimAlpha = Data.AimAlpha;
	Report.Muzzle = ComponentTransform.TransformPosition(Gun.TransformPosition(Data.MuzzleOffset));
	Report.Bore = ComponentTransform.TransformVectorNoScale(Gun.GetUnitAxis(EAxis::X));
	Report.LeftTarget = ComponentTransform.TransformPosition(LeftHandTarget.GetLocation());
	Report.RightTarget = ComponentTransform.TransformPosition(RightHandTarget.GetLocation());
	Report.LeftAlpha = LeftHandAlpha;
	Report.RightAlpha = RightHandAlpha;
	Instance->ReportWeaponAim(Report);
#endif
}

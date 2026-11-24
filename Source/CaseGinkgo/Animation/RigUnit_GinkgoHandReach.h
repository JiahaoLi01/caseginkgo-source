#pragma once

#include "CoreMinimal.h"
#include "Animation/Actions/GinkgoAnimActionTypes.h"  // EGinkgoHand
#include "Rigs/RigHierarchyDefines.h"  // FRigElementKey
#include "Units/RigUnit.h"
#include "RigUnit_GinkgoHandReach.generated.h"

class URigHierarchy;
struct FGinkgoHandReachRigData;
struct FGinkgoWeaponRigData;

namespace Ginkgo::Animation
{
	/** 一只手的骨骼（hand_l / hand_r，下标是 EGinkgoHand） */
	CASEGINKGO_API const FRigElementKey& HandBoneKey(EGinkgoHand Hand);

	/**
	 * @brief 一只手伸手的目标（组件空间）：从层级里这只手现在的变换出发，加上去抓（手里的挂点到东西上，乘 Grab）、收回（手里的挂点到胸前的收纳处，乘 Place）
	 *        两份修正。朝向：现在的手绕肩关节转向东西（片段里手伸出去的方向转到肩关节指向东西的方向，最多 120°，乘 Grab），手心总对着东西；
	 *        收回时转回片段的朝向。两份都从同一只手算，曲线重叠的那几帧也连续；Grab 为 1 时手里的挂点正好在东西上
	 * @param ToWorld - 组件到世界（东西的点是世界坐标）
	 * @return 这只手在伸手、层级里有它：OutTarget 有效
	 */
	CASEGINKGO_API bool ComputeHandReachTarget(const URigHierarchy& Hierarchy, const FGinkgoHandReachRigData& Reach, EGinkgoHand Hand, const FTransform& ToWorld,
		FTransform& OutTarget);

	/**
	 * @brief 一只手所有在伸的（被顶掉、还在淡出的与正在伸的，连着拾取时同时有几个）混成一个目标：各自的目标按蒙太奇实例的权重混，
	 *        同胳膊的槽里几个实例的姿势一样；权重是它们的和（实例的权重加起来不超过 1）。只有一个时就是它的目标与权重
	 * @return 有在伸的：OutTarget、OutWeight 有效
	 */
	CASEGINKGO_API bool ComputeHandReachBlend(const URigHierarchy& Hierarchy, const FGinkgoWeaponRigData& Data, EGinkgoHand Hand, const FTransform& ToWorld,
		FTransform& OutTarget, float& OutWeight);

	/** 一只手所有在伸的权重加起来（不超过 1） */
	CASEGINKGO_API float GetHandReachWeight(const FGinkgoWeaponRigData& Data, EGinkgoHand Hand);
}

/**
 * @struct FRigUnit_GinkgoHandReach : 伸手时上半身也参与（拾取，Docs/Design/Interaction.md「拾取子弹盒」）
 * Control Rig 里接在腿部 IK 之后、瞄准之前，后面是一个分支与第一遍全身 IK（命令 GinkgoAnimBlueprint -AddReachLean 插）：
 * - 只在有手在伸时（bActive）走第一遍全身 IK：根钉在骨盆，效应器只有伸手的那只手，只动锁骨与胳膊（脊柱的转动刚度是 1）。
 *   拿枪的那只手不当效应器，连同挂在胸口上的枪跟着胸口走（全身 IK 里不在效应器链上的骨骼跟着父骨骼刚性地走）。
 * - 后面的瞄准节点在弯过的胸口上重新算枪上的握法与伸手的目标，第二遍全身 IK（根钉在 spine_05，只动胳膊）补齐剩下的。
 * - 没有手在伸时第一遍整个跳过：别的姿势不受影响。
 * - 前倾：东西离肩关节超过 ComfortReach 时，先把 LeanBones（spine_01–05）绕同一根轴往东西那边转，转到肩关节离东西正好 ComfortReach
 *   （最多 MaxLean）。轴是"spine_01 到肩关节"叉乘"肩关节到东西"：东西在前面就前倾，在左边就往左侧倾。按蒙太奇的权重乘去抓的曲线，
 *   收回时倾斜跟着回去。角度按 LeanShares 分，弯在腰上，胸口跟着刚性地走（人够不着时弯腰，上胸弯不了）。骨盆不转：转骨盆会带着腿走，脚离开地面
 * - 连着拾取时同一只手有几个在伸（被顶掉的随自己的实例淡出）：前倾各按各的权重叠，目标按权重混（Ginkgo::Animation::ComputeHandReachBlend）
 * - 两只手同时在伸：前倾两只手的都叠；第一遍全身 IK 只有一个效应器，给权重大的那只手，比重是两个权重的差，换手的那一刻不起作用，所以不跳；两只手都满时第一遍不起作用，第二遍补齐
 * 锁骨的刚度与超伸是这里的输入（存在 Control Rig 里），原样输出给第一遍全身 IK 的骨骼设置；ginkgo.anim.reach* 几个控制台变量在调试时覆盖（发行版不编译）
 */
USTRUCT(meta = (DisplayName = "Ginkgo Hand Reach", Category = "Ginkgo", Keywords = "Reach,Pickup,Lean,IK", NodeColor = "0.6 0.45 0.3"))
struct CASEGINKGO_API FRigUnit_GinkgoHandReach : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_GinkgoHandReach();

	RIGVM_METHOD()
	virtual void Execute() override;

	/** 前倾时转的几根脊柱（从下往上），绕同一根轴各转一份（LeanShares） */
	UPROPERTY(meta = (Input))
	TArray<FName> LeanBones;

	/**
	 * 前倾的角度怎么分给 LeanBones（同样的顺序，按比例分，不用加起来等于 1；个数对不上时平分）。
	 * 默认弯在腰上（spine_01、02 占 80%），往上很快变少，spine_05 不弯：胸廓是硬的（Docs/Design/Interaction.md「弯腰」）
	 */
	UPROPERTY(meta = (Input))
	TArray<float> LeanShares;

	/** 东西离肩关节超过这个距离才前倾，倾到正好这个距离（cm）：胳膊不必伸直，剩下的由全身 IK 补 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float ComfortReach = 48.f;

	/** 前倾最多多少度（几根脊柱加起来） */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "90"))
	float MaxLean = 35.f;

	/** 锁骨的转动刚度（0–1） */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "1"))
	float ClavicleStiffness = 0.3f;

	/** 允许骨骼伸长一点去够（全身 IK 的 bAllowStretch） */
	UPROPERTY(meta = (Input))
	bool bAllowStretch = false;

	/** 伸长时胳膊（上臂、前臂、手）的平移刚度（0–1）：越大伸得越少；不伸长时不起作用 */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "1"))
	float ArmStretchStiffness = 0.9f;

	/**
	 * 效应器的 PullChainAlpha（0–1）：大于 0 时全身 IK 先把伸手的那条链（手到锁骨，到 spine_05 的分叉为止）整体往目标转、挪过去。
	 * 挪链的起点会拽动胸口，够得着时上半身也弯，所以默认 0
	 */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "1"))
	float PullChainAlpha = 0.f;

	/** 全身 IK 的迭代次数：效应器的拉力一次次往根传，次数少时传不到脊柱 */
	UPROPERTY(meta = (Input, ClampMin = "1"))
	int32 Iterations = 100;

	/** 全身 IK 的质量倍数：体的质量是它到各个子骨骼的距离之和（spine_05 挂着双臂、脖子与枪的虚拟骨骼，很重），倍数越大越难拉动 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MassMultiplier = 0.3f;

	/** 有手在伸（权重大于 0）：分支走第一遍全身 IK */
	UPROPERTY(meta = (Output))
	bool bActive = false;

	/** 伸手的那只手（hand_l / hand_r）：效应器的骨骼。两只都在伸时取权重大的；都没在伸时是 hand_l */
	UPROPERTY(meta = (Output))
	FName Bone;

	/** 它的目标（全局，就是组件空间） */
	UPROPERTY(meta = (Output))
	FTransform Target;

	/** 它的权重：胳膊槽里蒙太奇实例的混合权重。接效应器的 PositionAlpha、RotationAlpha */
	UPROPERTY(meta = (Output))
	float Alpha = 0.f;

	/**
	 * 给第一遍全身 IK 的脊柱（spine_01–05）的转动刚度：1，脊柱不动，弯腰全由前倾算。全身 IK 的刚度管的是骨骼在世界里转不转，
	 * 做不到"腰弯、胸口跟着刚性地走"；够不着时它把整段脊柱卷起来，弯在上胸（Docs/Design/Interaction.md「弯腰」）。调试时控制台变量覆盖
	 */
	UPROPERTY(meta = (Output))
	float SpineStiffnessUsed = 1.f;

	/** 给第一遍全身 IK 的：输入，或调试时控制台变量覆盖的 */
	UPROPERTY(meta = (Output))
	float ClavicleStiffnessUsed = 0.f;

	UPROPERTY(meta = (Output))
	bool bAllowStretchUsed = false;

	UPROPERTY(meta = (Output))
	float ArmStretchStiffnessUsed = 0.f;

	UPROPERTY(meta = (Output))
	float PullChainAlphaUsed = 0.f;

	UPROPERTY(meta = (Output))
	int32 IterationsUsed = 20;

	UPROPERTY(meta = (Output))
	float MassMultiplierUsed = 1.f;

	/** 这一帧前倾了多少度（乘过权重的） */
	UPROPERTY(meta = (Output))
	float LeanAngle = 0.f;
};

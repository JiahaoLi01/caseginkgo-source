#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchyCache.h"
#include "Units/RigUnit.h"
#include "RigUnit_GinkgoWeaponAim.generated.h"

/**
 * @struct FRigUnit_GinkgoWeaponAim_WorkData : 瞄准节点跨帧留着的
 */
USTRUCT()
struct CASEGINKGO_API FRigUnit_GinkgoWeaponAim_WorkData
{
	GENERATED_BODY()

	/** 转的几根脊柱（从下往上） */
	UPROPERTY()
	TArray<FCachedRigElement> CachedSpine;

	/** 桌前伸手时弯腰的起点，站姿瞄准只使用上面的脊柱链 */
	UPROPERTY()
	FCachedRigElement CachedLowerSpine;

	/** 枪挂的骨骼 */
	UPROPERTY()
	FCachedRigElement CachedGun;
};

/**
 * @struct FRigUnit_GinkgoWeaponAim : 枪口对准与双手的目标（持枪第 2 批），Control Rig 里接在腿部 IK 之后、双手的全身 IK 之前
 * 设计见 Docs/Design/Weapons.md「手与瞄准」。数据从动画实例取（UGinkgoAnimInstance::GetWeaponRigData，同腿部 IK 读动画实例）：
 * 1. 枪口对准：层的瞄准偏移已经粗对准了视线，剩下的是视差（枪口在肩旁，准星的线从相机出发）与超出瞄准偏移范围的部分。
 *    枪口方向与"枪口 → 瞄准点"之间的转角，几根脊柱各分 SpineShare、枪补剩下的，每根有限幅；转脊柱会挪枪口，所以算 Iterations 遍。
 *    按满的算好，再按抬枪进度（腰射、开镜为 1）从输入的姿势混过去：低持、冲刺时不做。
 * 2. 双手的目标：枪的最终变换乘手在枪上的变换（四个姿势的按三个进度混过），连同权重输出给后面的全身 IK 的两个效应器。
 *    权重：拿着枪为 1；枪上的蒙太奇放着时是 1 − 它的权重 + 它的 HandIK_L、HandIK_R 曲线（曲线过槽时已乘了蒙太奇的权重）。
 *    单手的举枪冲刺左手不在枪上：左手的 1 − 蒙太奇的权重那一份再乘 FGinkgoWeaponRigData::LeftHandGrip（1 − 冲刺持枪的权重）。
 * 3. 伸手（拾取，Docs/Design/Interaction.md「拾取子弹盒」）：空手、拿着枪都做。伸手的那只手按胳膊槽里蒙太奇的权重，从上面的目标（空手时是动画的手、
 *    权重 0）换到伸手的目标：动画的手加上去抓（手里的挂点到东西上）、收回（到胸前的收纳处）两份按曲线的修正。
 * 只动脊柱、枪与（后面的 IK）胳膊，腿不动。调试：ginkgo.anim.weaponAim 0 关掉对准与双手（只用来对照），发行版里总是开
 */
USTRUCT(meta = (DisplayName = "Ginkgo Weapon Aim", Category = "Ginkgo", Keywords = "Aim,Weapon,Muzzle,Hands,IK", NodeColor = "0.6 0.45 0.3"))
struct CASEGINKGO_API FRigUnit_GinkgoWeaponAim : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_GinkgoWeaponAim();

	RIGVM_METHOD()
	virtual void Execute() override;

	/** 分枪口对准的余差的几根脊柱（从下往上） */
	UPROPERTY(meta = (Input))
	TArray<FName> SpineBones;

	/** 枪挂的骨骼：枪最后补的一点转它（绕它的原点，就是握把） */
	UPROPERTY(meta = (Input))
	FName GunBone;

	/** 两只手的权重读的曲线（蒙太奇里的） */
	UPROPERTY(meta = (Input))
	FName LeftHandCurve;

	UPROPERTY(meta = (Input))
	FName RightHandCurve;

	/** 每根脊柱分余差的多少 */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "1"))
	float SpineShare = 0.25f;

	/** 每根脊柱最多转多少（度，几遍加起来） */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MaxSpineAngle = 10.f;

	/** 枪自己最多转多少（度） */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MaxGunAngle = 5.f;

	/**
	 * 算几遍：转脊柱挪了枪口，再算一遍剩下的。离墙 2.5 m 时余差约 20°，枪顶到 5°，两遍还剩 0.2–0.6°（aim 路线），四遍收干净
	 */
	UPROPERTY(meta = (Input, ClampMin = "1"))
	int32 Iterations = 4;

	/** 左手的目标（全局，就是组件空间）：接全身 IK 的效应器 */
	UPROPERTY(meta = (Output))
	FTransform LeftHandTarget;

	/** 右手的目标 */
	UPROPERTY(meta = (Output))
	FTransform RightHandTarget;

	/** 左手的权重：接效应器的 PositionAlpha、RotationAlpha */
	UPROPERTY(meta = (Output))
	float LeftHandAlpha = 0.f;

	/** 右手的权重 */
	UPROPERTY(meta = (Output))
	float RightHandAlpha = 0.f;

	UPROPERTY(transient)
	FRigUnit_GinkgoWeaponAim_WorkData WorkData;
};

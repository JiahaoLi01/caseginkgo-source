#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "Movement/Traversal/GinkgoTraversalTypes.h"
#include "GinkgoMovementConfig.generated.h"

class UGinkgoMovementWindow;
class UGinkgoTraversalAnimations;

/**
 * @class UGinkgoMovementConfig : 移动配置
 * 移动组件的全部手感参数：结构写在代码里，参数放在这里。移动组件每帧读取，PIE 中改资产立即生效。
 * 单位一律是厘米与秒；从 Source 迁移的参数乘 1.905（1 Source 单位约 1.905 cm），从 Apex / 泰坦陨落 2 迁移的乘 2.54
 * （Respawn 的 1 单位 = 1 英寸），注释里给出原值。
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoMovementConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UGinkgoMovementConfig();

	/** 攀爬蒙太奇及双手接触时序；角色开始时预加载，运行中不临时创建动画资产。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal")
	TSoftObjectPtr<UGinkgoTraversalAnimations> TraversalAnimations = TSoftObjectPtr<UGinkgoTraversalAnimations>(FSoftObjectPath(TEXT("/Game/Characters/Traversal/DA_TraversalAnimations.DA_TraversalAnimations")));

	// ===== 地面 =====

	/** 跑速：不冲刺时的 wishspeed（CS 的 250 u/s） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0, Units = "cm/s"))
	float RunSpeed = 476.25f;

	/** 冲刺速度（TF2 侦察兵的 400 u/s） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0, Units = "cm/s"))
	float SprintSpeed = 762.f;

	/**
	 * 在地面上冲刺时角色面向速度（不限方向，向后冲刺就转过身去跑）：水平速度高于它时才取速度的方向，更慢时照常面向视角。
	 * 从静止冲刺，第一帧就有 254 / 127 / 53 cm/s（30 / 60 / 144 fps）；急停时最后约 0.07 s 低于它
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0, Units = "cm/s"))
	float SprintFacingMinSpeed = 50.f;

	/** 蹲伏速度（跑速的 1/3，同 Source 的蹲伏倍率）：只在地面生效，空中蹲下不减速 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0, Units = "cm/s"))
	float CrouchSpeed = 158.75f;

	/** 地面加速系数（sv_accelerate 10）：每秒最多加 GroundAccel × wishspeed，越大起步越硬 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0))
	float GroundAccel = 10.f;

	/**
	 * 地面加速按多少 wishspeed 起算（cm/s）：wishspeed 比它小时加速也按它算。
	 * Source 的公式里，低速时摩擦每秒减 sv_friction × sv_stopspeed，加速每秒加 sv_accelerate × wishspeed，wishspeed 低于 76 cm/s（4 × 190.5 ÷ 10）时加不过摩擦，
	 * 只能在原地按帧长蹭：拿着枪蹲着开镜（98k 52 cm/s）、手柄轻推时走不起来。这个数以上与 Source 逐位相同；大了小幅度起步更硬
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0, Units = "cm/s"))
	float GroundAccelMinWishSpeed = 100.f;

	/** 地面摩擦（sv_friction 4）：越大停得越快、越不滑 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0))
	float GroundFriction = 4.f;

	/** 停止速度（sv_stopspeed 100 u/s）：低于它时摩擦按它算，松手后能在有限时间内停住 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0, Units = "cm/s"))
	float StopSpeed = 190.5f;

	/**
	 * 落地免摩擦窗口：落地后这段时间不施加地面摩擦，连跳与滑铲接跳不掉速。
	 * 替代 Source 的"起跳帧跳过摩擦"：CMC 落地那一帧剩余的时间会进 PhysWalking 吃摩擦，起跳按得再早也会掉速。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground", meta = (ClampMin = 0, ClampMax = 0.5, Units = "s"))
	float LandingFrictionGrace = 0.1f;

	// ===== 空中 =====

	/** 空中加速系数（sv_airaccelerate 10）：决定扫射转向与增益的快慢 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = 0))
	float AirAccel = 10.f;

	/** 空中输入方向分量的上限（Source 的 30 u/s）：越小越不能在空中直接加速，只能靠扫射 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = 0, Units = "cm/s"))
	float AirSpeedCap = 57.15f;

	/** 重力（HL2 的 sv_gravity 600 u/s²）：角色自己的重力，不跟世界设置走 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = 0, Units = "cm/s^2"))
	float Gravity = 1143.f;

	// ===== 跳跃 =====

	/** 跳跃高度（CS 的 57 u）：起跳速度由它和重力推出，改重力时跳跃高度不变 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jump", meta = (ClampMin = 0, Units = "cm"))
	float JumpHeight = 108.6f;

	// ===== 滑铲 =====
	// 参考 Apex Legends 与泰坦陨落 2，取 Apex 收枪时的值：我们的冲刺 762 cm/s 与 Apex 收枪冲刺 759 cm/s 几乎相同

	/** 地面进入滑铲的最低水平速度（Apex 200 u/s）：跑速达不到，实际上要冲刺 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, Units = "cm/s"))
	float SlideStartSpeed = 508.f;

	/** 落地接滑铲的最低水平速度（Apex 90 u/s）：比地面进入低，走路起跳也能落地接滑铲 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, Units = "cm/s"))
	float SlideLandHorizontalSpeed = 228.6f;

	/**
	 * 落地接滑铲的最低下落速度：从矮台阶上走下来不会接成滑铲。
	 * Apex 是 200 u/s，按泰坦陨落 2 的重力（600 u/s²）相当于从 85 cm 落下；我们的重力更小，直接换算成 508 cm/s 会高过普通起跳的落地速度
	 * （√(2 × 1143 × 108.6) ≈ 498），平地起跳就接不上滑铲了。所以按同样的落差换算：√(2 × 1143 × 84.7) ≈ 440。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, Units = "cm/s"))
	float SlideLandFallSpeed = 440.f;

	/** 开始滑铲时，移动输入与视角前向的最大夹角（Apex 与泰坦陨落 2 的点积 0.6）：防止向后、向侧面铲；没有输入时不限制 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, ClampMax = 180, Units = "Degrees"))
	float SlideMaxStartAngle = 53.13f;

	/** 起步加速（Apex 150 u/s × 收枪 1.15）：开始滑铲时水平速度加这么多，不超过 SlideBoostCap */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, Units = "cm/s"))
	float SlideBoost = 438.15f;

	/** 起步加速的上限（Apex 400 u/s × 1.15）：满速冲刺一铲就到上限；已经超过时不加也不减 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, Units = "cm/s"))
	float SlideBoostCap = 1168.4f;

	/** 线性减速（泰坦陨落 2 的 slideDecel 50 u/s²）：低速时主要靠它停下 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, Units = "cm/s^2"))
	float SlideDecel = 127.f;

	/** 指数衰减率（1/s），每秒保留 e^−SlideDecay（泰坦陨落 2 的 slideVelocityDecay：每秒保留 0.7）：高速时主要靠它减速 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0))
	float SlideDecay = 0.35667f;

	/** 转向加速度：侧向输入每秒把速度方向转过约 SlideSteerAccel / 速度 弧度，不改大小；反向输入按它刹车 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, Units = "cm/s^2"))
	float SlideSteerAccel = 600.f;

	/** 滑铲结束的速度（泰坦陨落 2 的 125 u/s）：低于它退出滑铲，还按着蹲就是蹲着走 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = 0, Units = "cm/s"))
	float SlideEndSpeed = 317.5f;

	/** 翻越/攀爬的几何查询配置；角色胶囊决定起点，相机的世界 XY 投影决定意图方向，完整方案通过后才启动动作。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal")
	FGinkgoTraversalSettings Traversal;

	/**
	 * 水平实速到攀爬接受距离的曲线：横轴 cm/s，纵轴为胶囊前缘外的 cm，与冲刺按键状态无关。
	 * 默认关键帧为 (0, 50)、(RunSpeed 默认值, 100)、(SprintSpeed 默认值, 125)，段间线性插值。
	 * 修改跑速后可同步编辑横轴关键帧；运行时不改写曲线。空曲线回退 Traversal.Reach，最终距离限制在 [1,125] cm。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal")
	FRuntimeFloatCurve TraversalReachBySpeed;

	/** 按实际水平速度（cm/s）求本次查询距离；非法输入按静止，非法曲线值回退基础距离，曲线过冲也不能超过 125 cm。 */
	float GetTraversalReach(float HorizontalSpeed) const;

	// ===== 窗口 =====

	/**
	 * 窗口定义：只是数据，状态在移动组件的模拟状态里，按这里的下标对应。类默认值带土狼时间、滑铲疲劳、跳跃疲劳与 lurch（构造函数里），配置资产从它开始。
	 * 两端用同一份配置，所以列表的顺序两端一样；运行中增删条目会让正在活动的窗口对不上定义，改完重新开始
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Windows")
	TArray<TObjectPtr<UGinkgoMovementWindow>> Windows;
};

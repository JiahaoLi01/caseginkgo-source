#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/GinkgoAnimMath.h"  // FGinkgoMarkerFollow：持枪循环跟着移动的步伐
#include "GinkgoWeaponAnimLayers.generated.h"

class UAnimSequence;
class UAnimSequenceBase;
class UBlendSpace;
class UGinkgoAnimInstance;
class UGinkgoWeaponDefinition;

/**
 * @class UGinkgoWeaponAnimLayers : 武器层的父类
 * 见 Docs/Design/Weapons.md「动画」「持枪循环」。层的蓝图实现层接口 ALI_GinkgoWeaponLayers 的两个层：
 * - UpperBodyPose：持枪姿势。低持的姿势按地速混到低持跑动的循环，再按冲刺进度换到冲刺的持枪循环，再按两个进度混到腰射、开镜
 *   （枪有开镜的循环时它代替开镜的姿势）；从 spine_01 往上（带 ik_hand_root）按网格空间的旋转分层混到移动姿势上；
 * - AimOffset：瞄准偏移，主蓝图里放在蒙太奇槽之后，拉栓、装填的片段也跟着视角转。
 * 层的蓝图（ABP_GinkgoWeaponLayers_Rifle，命令 GinkgoAnimBlueprint -AddWeaponLayers 生成，-AddHoldLoops 加上三个循环）只连节点、读这里的成员；
 * 素材每帧从手上那把枪的定义里取，同一种持法的枪共用一个层的蓝图。
 *
 * 两个层在同一个组（Weapon）里，共用这一个实例：组为空的层，引擎每个各建一个实例。
 * 数据在 NativeThreadSafeUpdateAnimation 里从主实例读：层的图更新到时才调它，那时主实例这一帧的已经算过；
 * 游戏线程上反过来，链接的实例先于主实例更新，在 NativeUpdateAnimation 里读到的是上一帧的。
 *
 * 持枪循环跟着移动的步伐：两个循环的求值器的节点函数按移动状态机这一帧所放的片段与时刻给时刻。层的图里 UpperBodyPose 的分层混合
 * 先更新输入的姿势（bUpdateBasePoseFirst），输入的姿势就是主蓝图的状态机，所以节点函数调到时状态机这一帧的已经记下了；
 * NativeThreadSafeUpdateAnimation 比它们都早（AimOffset 层先更新），在那里读到的是上一帧的。
 * 开镜的循环不跟步伐：时刻是主实例的时钟（FGinkgoWeaponAnimData::LoopClock）除以时长的余数，一直走，每次开镜接着时钟放，不从同一口呼吸开始。
 *
 * 换层不靠惯性化：链上时两个层的权重从 0 淡入，手上没枪了淡到 0，武器组件等 LayerBlendTime 淡完才撤掉层。
 * 权重为 0 时两个层原样输出，链上、撤掉的那一帧与主蓝图自己那份（徒手）完全一样。惯性化（下游的 Dead Blending）
 * 混的是整个姿势与全部曲线：跑着拔枪时腿被外推着混、腿部 IK 的落脚曲线被混乱（见 Docs/Design/Weapons.md「换层」）
 */
UCLASS()
class CASEGINKGO_API UGinkgoWeaponAnimLayers : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 换层淡入、淡出的时长（s）：武器组件撤掉层之前等它 */
	float GetLayerBlendTime() const { return LayerBlendTime; }

	// ===== 节点函数 =====
	// 层的蓝图里三个持枪循环的求值器绑定它们：Setup* 绑 On Become Relevant（刚用上：重新跟上步伐），Update* 绑 On Update（给这一帧的时刻）。
	// 在动画的工作线程上调

	/** 跑的循环刚用上（权重从 0 起来） */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Weapon", meta = (BlueprintThreadSafe))
	void SetupRunLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 跑的循环：跟着移动所放的片段的步伐（同一条时间线时取同一个时刻，见 Ginkgo::Animation::FollowMarkers） */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Weapon", meta = (BlueprintThreadSafe))
	void UpdateRunLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 冲刺的循环刚用上 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Weapon", meta = (BlueprintThreadSafe))
	void SetupSprintLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 冲刺的循环：同跑的循环 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Weapon", meta = (BlueprintThreadSafe))
	void UpdateSprintLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 开镜的循环：时刻按主实例的时钟一直走（不跟步伐，刚用上也不用重新跟） */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Weapon", meta = (BlueprintThreadSafe))
	void UpdateAimLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 低持的循环：同开镜的循环 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Weapon", meta = (BlueprintThreadSafe))
	void UpdateLoweredLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 腰射的循环：同开镜的循环 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Weapon", meta = (BlueprintThreadSafe))
	void UpdateHipLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

protected:
	//~ Begin UAnimInstance
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	//~ End UAnimInstance

	/** 低持（常态）的姿势：手上那把枪的定义里的 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UAnimSequenceBase> LoweredPose;

	/** 腰射的姿势 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UAnimSequenceBase> HipPose;

	/** 开镜的姿势 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UAnimSequenceBase> AimPose;

	/** 低持跑动的循环：同上，从定义里取；时刻由节点函数给 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UAnimSequence> RunLoop;

	/** 冲刺的持枪循环 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UAnimSequence> SprintLoop;

	/** 开镜的持枪循环：开镜时代替开镜的姿势；时刻由节点函数给 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UAnimSequence> AimLoop;

	/** 低持的持枪循环：代替低持的姿势；时刻由节点函数给 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UAnimSequence> LoweredLoop;

	/** 腰射的持枪循环：代替腰射的姿势；时刻由节点函数给 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UAnimSequence> HipLoop;

	/** 瞄准偏移（网格空间叠加，偏航 × 俯仰）：同上，从定义里取。不叫 AimOffset：层接口里的层叫这个名字，同名会冲突 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	TObjectPtr<UBlendSpace> AimOffsetBlendSpace;

	/** 低持的姿势 → 跑的循环：地速按 RunSpeedRange 从 0 到 1，过弹簧；不在地面上（空中、滑铲）时收回 0。枪没有跑的循环时为 0，单手跑的枪两只手都在枪上时也收回 0 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float RunAlpha = 0.f;

	/** 低持（含跑的循环）→ 冲刺的循环：两头缓过的冲刺进度（同 Control Rig 里左手放开枪的那个） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float SprintAlpha = 0.f;

	/** 开镜的姿势 → 开镜的循环：枪有开镜的循环、持枪循环开着时为 1，否则为 0（开镜照静止的姿势） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float AimLoopAlpha = 0.f;

	/** 低持的姿势 → 低持的循环：枪有这个循环、持枪循环开着时为 1，否则为 0 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float LoweredLoopAlpha = 0.f;

	/** 腰射的姿势 → 腰射的循环：同上 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float HipLoopAlpha = 0.f;

	/** 低持 → 腰射：两头缓过的抬枪进度 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float RaiseAlpha = 0.f;

	/** 腰射 → 开镜：两头缓过的开镜进度 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float AdsAlpha = 0.f;

	/** 瞄准偏移的偏航（度，向右为正）：视角相对网格根的，夹在瞄准偏移的范围里，超出的以后交给 Control Rig 的枪口对准 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float AimYaw = 0.f;

	/** 瞄准偏移的俯仰（度，向上为正）：低持时按定义里的 LoweredAimPitchScale 打折，抬起来以后全用；同样夹在范围里 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float AimPitch = 0.f;

	/** 瞄准偏移的权重：拿着枪为 1，冲刺时为 0（只有头看视角，以后做），过弹簧；再乘换层的淡入淡出 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float AimOffsetAlpha = 0.f;

	/** 上半身用持枪姿势的权重：换层的淡入淡出（两头缓过），拿着枪时为 1 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon")
	float UpperBodyAlpha = 0.f;

	/**
	 * 地速从 X 到 Y 时跑的循环的权重从 0 到 1：我们跑 476 cm/s（持枪倍率 0.82 时 390），GASP 走 200、侧向跑 350。
	 * 走着时上半身只带一部分跑的起伏；Y 大了持枪跑不满
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon")
	FVector2D RunSpeedRange = FVector2D(0.0, 350.0);

	/** 跑的循环的权重过的临界阻尼弹簧的角频率：离地、着地、开始滑铲时目标一下子变，约 0.2 s 换过去 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon", meta = (ClampMin = 0, Units = "rad/s"))
	float RunAlphaOmega = 15.f;

	/** 瞄准偏移的权重过的临界阻尼弹簧的角频率：进出冲刺时约 0.2 s 换过去 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon", meta = (ClampMin = 0, Units = "rad/s"))
	float AimOffsetAlphaOmega = 15.f;

	/** 换层淡入、淡出的时长：同状态机的过渡（0.2 s） */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon", meta = (ClampMin = 0, Units = "s"))
	float LayerBlendTime = 0.2f;

private:
	/** 一个持枪循环跟着移动的步伐：跨帧留着的，与上一帧领头的片段（换了片段时几成不能接着比） */
	struct FLoopFollow
	{
		Ginkgo::Animation::FGinkgoMarkerFollow Marker;
		const UAnimSequence* Leader = nullptr;
	};

	/** 主实例：层在它的网格上；编辑器里单独预览层的蓝图时没有 */
	UGinkgoAnimInstance* GetMainInstance() const;

	/** 按时钟走的循环的节点函数共用：时钟除以时长的余数给求值器 @return 这一帧的时刻；没放时为负 */
	float UpdateClockLoop(const FAnimNodeReference& Node, const UAnimSequence* Loop) const;

	/** 两个循环的节点函数共用：按移动状态机这一帧的片段与时刻推进 Follow，给求值器这一帧的时刻 */
	void FollowLocomotion(const FAnimUpdateContext& Context, const FAnimNodeReference& Node, const UAnimSequence* Loop, FLoopFollow& Follow);

	/** 用谁的素材：手上的枪，淡出时留着上一把 */
	UPROPERTY(Transient)
	TObjectPtr<const UGinkgoWeaponDefinition> Weapon;

	/** 换层的进度：0 没淡入（原样输出），1 淡入了（线性） */
	float LayerProgress = 0.f;

	/** 拔枪、收枪时层让开多少（FGinkgoWeaponAnimData::HoldPoseAlpha），乘在两个层的权重上 */
	float HoldPoseAlpha = 1.f;

	/** 它每秒最多变多少：片段的曲线最快约 5/s */
	static constexpr float HoldPoseMaxRate = 10.f;

	/** 瞄准偏移权重的弹簧：值与速度（每秒），乘换层的淡入淡出之前的 */
	double AimOffsetWeight = 0.0;
	double AimOffsetWeightVelocity = 0.0;

	/** 跑的循环的权重的弹簧：值与速度（每秒） */
	double RunWeight = 0.0;
	double RunWeightVelocity = 0.0;

	FLoopFollow RunFollow;
	FLoopFollow SprintFollow;

	/** 主实例这一帧的持枪循环的时钟（s）：开镜的循环按它取时刻 */
	double LoopClock = 0.0;

#if !UE_BUILD_SHIPPING
	/** 报给主实例的调试显示与回放轨迹：三个循环的权重与这一帧放到的时刻 */
	float DebugRunTime = -1.f;
	float DebugSprintTime = -1.f;
	float DebugAimTime = -1.f;

	void ReportLoops() const;
#endif
};

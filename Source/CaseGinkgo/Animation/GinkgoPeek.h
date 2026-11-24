#pragma once

#include "CoreMinimal.h"
#include "Camera/GinkgoSpring.h"
#include "GinkgoPeek.generated.h"

/** 自动探身的判定与过渡参数；偏差取同一瞄准视图中的水平夹角，与分辨率无关。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoPeekSettings
{
	GENERATED_BODY()

	/** 动画管线是否支持探身；玩家还需辅助功能开关启用，AI 使用独立战术请求，关闭后均平滑回正 */
	UPROPERTY(EditAnywhere, Category = "Peek")
	bool bEnabled = true;

	/** 未探身参考命中偏离准星多少度开始探身，右偏驱动左倾 */
	UPROPERTY(EditAnywhere, Category = "Peek", meta = (ClampMin = "0.01", Units = "Degrees"))
	float EnterAngle = 0.6f;

	/** 同侧偏差小于此角度才允许回正，应小于进入角度，形成滞回 */
	UPROPERTY(EditAnywhere, Category = "Peek", meta = (ClampMin = "0", Units = "Degrees"))
	float ExitAngle = 0.25f;

	/** 参考偏差达到此角度时使用完整探身姿势；较小偏差按比例混合 */
	UPROPERTY(EditAnywhere, Category = "Peek", meta = (ClampMin = "0.01", Units = "Degrees"))
	float FullAngle = 3.f;

	/** 参考偏差连续进入退出区多久才回正（秒），避免遮挡边缘的短暂丢失 */
	UPROPERTY(EditAnywhere, Category = "Peek", meta = (ClampMin = "0", Units = "s"))
	float ReleaseDelay = 0.25f;

	/** 探身权重的临界阻尼弹簧角频率（rad/s），左右切换时从当前值连续过渡 */
	UPROPERTY(EditAnywhere, Category = "Peek", meta = (ClampMin = "0", Units = "rad/s"))
	float BlendOmega = 14.f;
};

/**
 * 探身判定的跨帧状态，只由动画实例的游戏线程推进。
 * 输入必须来自未叠加探身的参考枪口；最终姿势修正了命中也不改变这个判据。
 */
struct CASEGINKGO_API FGinkgoPeekState
{
	/** 目标方向：-1 左倾、0 回正、+1 右倾；进入/退出滞回决定，不随渲染后的命中点切换 */
	int32 Side = 0;

	/** 连续满足退出条件的时长（秒）；偏差恢复或方向改变时清零 */
	double ReleaseTime = 0.0;

	/** 连续的带符号姿势权重，左右求值器分别读取负/正部分 */
	TGinkgoCriticalSpring<double> Blend;

	/**
	 * 输入失效时淡出；传送、换武器等不连续变化由拥有者调用 Reset。
	 * @param bCanEnter - 实命中未指向有效目标时才允许发起或反向探身；否则只维护已有的同侧遮挡，防止探出命中后回正造成反馈
	 */
	void Step(bool bValid, double ErrorDegrees, double DeltaSeconds, const FGinkgoPeekSettings& Settings, bool bCanEnter = true);

	/** 立即回正并丢弃方向、退出计时与速度，用于重生、传送和武器变化 */
	void Reset();
};

/**
 * 探身叠加前的五个组件空间变换：pelvis、spine_03、spine_04、spine_05、枪的虚拟骨骼。
 * 捕获节点在求姿势时写，之后同一次求值的 Control Rig 读取；以骨盆重定位，继承转根与腿 IK 的调整。
 */
struct CASEGINKGO_API FGinkgoPeekReference
{
	/** 当前动画求值是否成功捕获所有骨骼；每次线程安全更新先清零，防止未走该分支时复用旧姿势 */
	bool bValid = false;

	/** 捕获时的组件空间变换，单位厘米；下标顺序与 PeekBoneNames 一致 */
	FTransform Bones[5];
};

namespace Ginkgo::Animation
{
	/** 参考姿势固定需要的骨骼，首项为重定位用的骨盆，其余依次为瞄准链 */
	CASEGINKGO_API TConstArrayView<FName> PeekBoneNames();

	/** 在同一视图内计算命中点相对准星的水平夹角（度），正值偏右；视点后方无有效判据 */
	CASEGINKGO_API bool PeekError(const FVector& Eye, const FQuat& View, const FVector& Hit, double& OutDegrees);

	/**
	 * 从未探身枪口测参考命中：ADS 由枪口朝准星命中点检查遮挡，腰射沿解算后的参考枪管。
	 * 世界查询只在游戏线程调用，不读取 HUD、最终枪械网格或相机抖动。
	 * @param OutHit - 可选的参考枪口首个阻挡结果；未命中时为空，用于区分可伤害目标和环境遮挡
	 */
	CASEGINKGO_API bool TracePeek(const UWorld& World, const AActor& Owner, const FVector& Eye, const FQuat& View,
		const FTransform& Gun, const FVector& MuzzleOffset, bool bAds, double Distance, FVector& OutPoint, double& OutDegrees, FHitResult* OutHit = nullptr);
}

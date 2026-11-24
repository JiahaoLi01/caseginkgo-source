#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GinkgoNoiseEmitterComponent.generated.h"

enum class EGinkgoAnimGait : uint8;

/**
 * 角色发出的玩法噪声。动画的真实落脚标记与武器的实际开火分别调用，不依赖音频资产、音量或相机事件。
 * 只在权威端的游戏线程向 AI Hearing 提交；位置在发生时冻结，组件不持有听众，也不负责敌人的判断。
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoNoiseEmitterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoNoiseEmitterComponent();

	/** Hearing 刺激使用的枪声标记，与跑步、冲刺的落脚声区分。 */
	static const FName GunshotTag;

	/** 奔跑落脚声的传播半径（cm）；0 禁用此类噪声，慢走与蹲伏始终不发出脚步噪声。 */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "0", Units = "cm"))
	float RunRange = 1200.f;

	/** 冲刺落脚声的传播半径（cm）；0 禁用。实际听距还受听众自身 HearingRange 限制。 */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "0", Units = "cm"))
	float SprintRange = 2000.f;

	/** 动画评估完成后调用一次落脚事件；离地、静止、死亡及非权威端都不提交。 */
	void ReportFootstep(EGinkgoAnimGait Gait);

	/** 每次实际发射调用一次，霰弹多弹丸仍只一次；Muzzle 为世界 cm，Range 为最终传播半径，0 禁用。 */
	void ReportShot(const FVector& Muzzle, float Range);

private:
	/** 对活着的权威端角色提交冻结位置；Range 必须大于零，避免引擎把零解释为不限传播距离。 */
	void Report(const FVector& Location, float Range, FName Tag);
};

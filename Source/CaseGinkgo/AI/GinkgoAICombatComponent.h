#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GinkgoAICombatComponent.generated.h"

class AGinkgoCharacter;
class UGinkgoAIConfig;

/** 当前决策任务提交的武器意图。跟踪目击点时可以只举枪，允许射击时仍由武器机构控制实际发射。 */
enum class EGinkgoAIWeaponIntent : uint8
{
	Lowered,
	Aim,
	Fire
};

/**
 * 将状态树的跟踪和开火意图转为持枪、点射与空仓装填输入。控制器在感知、决策和转向之后显式推进。
 * 持续授权时按配置交替点射与停顿，撤销时清除武器尚未执行的开火请求。
 */
UCLASS()
class CASEGINKGO_API UGinkgoAICombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	void Initialize(AGinkgoCharacter* InCharacter, const UGinkgoAIConfig* InConfig);

	/** 切换意图时立即撤销不再允许的扳机输入，枪姿仍按已有持枪动画过渡。 */
	void SetWeaponIntent(EGinkgoAIWeaponIntent Intent);

	void TickCombat(float DeltaSeconds, bool bAimAligned);
	void Stop();

	/** 换枪时清除上一把枪的点射/装填等待；不停止武器 StateTree，避免取消正在进行的收枪和拔枪动作。 */
	void ResetWeaponInput();

private:
	/** 当前接管的角色，解除控制时清空，不延长 Pawn 生命周期。 */
	TWeakObjectPtr<AGinkgoCharacter> Character;

	/** 与控制器共享的只读定义；Initialize 设置，Stop 清空。 */
	UPROPERTY(Transient)
	TObjectPtr<const UGinkgoAIConfig> Config;

	/** 当前任务要求的枪姿与开火权限。离开任务、换枪和停止 AI 时清除，换弹期间暂时让出瞄准输入。 */
	EGinkgoAIWeaponIntent WeaponIntent = EGinkgoAIWeaponIntent::Lowered;

	/** 当前点射是否已开始；撤销授权立即重置。 */
	bool bBurstActive = false;

	/** 正在等待武器完成装填；这期间不开镜，避免开镜输入取消逐发装填。 */
	bool bReloading = false;

	/** 当前点射或点射间停顿剩余秒数；控制器每帧推进。 */
	float BurstRemaining = 0.f;

	/** 装填失败或后备耗尽时的重试间隔（秒），避免每帧发出装填输入。 */
	float ReloadRetry = 0.f;
};

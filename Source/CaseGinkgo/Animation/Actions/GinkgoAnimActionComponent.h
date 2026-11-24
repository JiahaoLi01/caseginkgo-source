#pragma once

#include "CoreMinimal.h"
#include "Animation/Actions/GinkgoAnimActionRunner.h"
#include "Animation/AnimNotifies/AnimNotify.h"  // FBranchingPointNotifyPayload：通知多播的参数
#include "Components/ActorComponent.h"
#include "GinkgoAnimActionComponent.generated.h"

class FGinkgoAnimInstanceMontageHost;
class UAnimInstance;

/**
 * @class UGinkgoAnimActionComponent : 动画动作的执行器
 * 角色上的组件：持有规则（FGinkgoAnimActionRunner），动画实例当蒙太奇宿主，把引擎的事件按实例转给规则。见 Docs/Design/AnimActions.md「时序」。
 *
 * 调用时机：
 * - 拥有者（以后是动作状态的 StateTree，现在是调试命令）随时 Start、Cancel、RequestStop。
 * - 每帧推进一次：在移动组件之后（中止条件看到这一帧移动后的位置）、网格体之前（放的蒙太奇这一帧就推进）。两条前置在 BeginPlay 里加。
 *
 * 实现注意：
 * - 引擎的通知（PlayMontageNotify 与 Window）是动画实例上的多播，按载荷里的实例 ID 交给规则；淡出与结束绑在放的那个实例上。
 * - 动画实例换了（换动画蓝图类）时重新绑定；之前的实例丢了，规则的看门会把在跑的动作中止（InstanceLost）。
 */
UCLASS(ClassGroup = (Ginkgo))
class CASEGINKGO_API UGinkgoAnimActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoAnimActionComponent();

	/**
	 * @brief 启动一个动作或链：记下来，在这个组件下一次推进时开始（同一帧，如果在它推进之前调用）
	 * @return 占用不让时假：没有开始，当场结局（FGinkgoAnimActionRunner::Start）
	 */
	bool Start(UGinkgoAnimAction* Action, FGinkgoAnimActionRunner::FFinished OnFinished = nullptr);

	/** 从已结束的移动模拟同步表现命令，不推进一次额外的动画帧。 */
	void FlushPending();

	/** 将移动模拟确认的完成点交给原动作规则；重放期间禁止调用，避免重复副作用。 */
	void NotifySimulationEvent(int32 InstanceID, FName Event);

	/** @brief 取消（硬停） */
	void Cancel(UGinkgoAnimAction* Action, const FGameplayTag& Reason);

	/** @brief 软停 */
	void RequestStop(UGinkgoAnimAction* Action, const FGameplayTag& Reason);

	/**
	 * @brief 这个部位能不能占（Ginkgo::Occupancy::Blocks）。只有本地知道：动画动作只在本地控制的角色上跑，占用不复制，服务器不看。BeginPlay 之前都能
	 */
	bool CanOccupy(const FGameplayTag& Tag, int32 Priority) const;

	/**
	 * @brief 外部占一个部位：状态、玩法逻辑都能占，不只是动作（持枪占着右臂）。比它低、盖得住的动作取消
	 * @return 句柄，ReleaseOccupancy 用；BeginPlay 之前占不了，返回 INDEX_NONE
	 */
	int32 Occupy(const FGameplayTag& Tag, int32 Priority);

	void ReleaseOccupancy(int32 Handle);

	/** 规则：读手的 IK、相机标签、在跑的动作；BeginPlay 之前为空 */
	const FGinkgoAnimActionRunner* GetRunner() const { return Runner.Get(); }

#if !UE_BUILD_SHIPPING
	/** 调试：取走上次以后发过的事件与结局（回放轨迹每帧取一次） */
	TArray<FString> TakeDebugLog();
#endif

	//~ Begin UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

private:
	/** 引擎与规则之间的一层：放、停、跳段、按 ID 查实例 */
	TSharedPtr<FGinkgoAnimInstanceMontageHost> Host;

	TSharedPtr<FGinkgoAnimActionRunner> Runner;

	/** 绑过通知的动画实例：换了就重新绑 */
	TWeakObjectPtr<UAnimInstance> BoundAnimInstance;

	/** 角色网格上现在的动画实例 */
	UAnimInstance* FindAnimInstance() const;

	/** 动画实例换了时重新绑定通知 */
	void BindAnimInstance();

	UFUNCTION()
	void HandleMontageNotifyBegin(FName NotifyName, const FBranchingPointNotifyPayload& Payload);

	UFUNCTION()
	void HandleMontageNotifyEnd(FName NotifyName, const FBranchingPointNotifyPayload& Payload);
};

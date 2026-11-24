#pragma once

#include "CoreMinimal.h"
#include "AI/GinkgoAIKnowledge.h"
#include "AI/GinkgoAITacticsComponent.h"
#include "StateTreeSchema.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "StateTreeExecutionTypes.h"
#include "GinkgoAIStateTree.generated.h"

class AGinkgoAIController;

/** 敌人决策树的 schema；只接收 AI 控制器作为外部数据，任务不依赖玩家输入或相机。 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Ginkgo Enemy AI", CommonSchema))
class CASEGINKGO_API UGinkgoAIStateTreeSchema : public UStateTreeSchema
{
	GENERATED_BODY()

public:
	/** 若勾选，首次目击后优先运行手持武器或角色定义的常规树。没有其他可用树时继续执行本树。 */
	UPROPERTY(EditAnywhere, Category = "Behavior", meta = (DisplayName = "首次目击后使用默认行为"))
	bool bUseDefaultBehaviorAfterSight = false;

	virtual bool IsStructAllowed(const UScriptStruct* Struct) const override;
	virtual bool IsClassAllowed(const UClass* Class) const override;
	virtual bool IsExternalItemAllowed(const UStruct& Struct) const override;
};

/** 无额外实例属性；行为计时与导航句柄由每个控制器独立拥有。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoAINodeData
{
	GENERATED_BODY()
};

/** 读取感知事实，既用作进入条件，也用作事实变化时回到根节点的转换条件。 */
USTRUCT(meta = (DisplayName = "AI Knowledge"))
struct CASEGINKGO_API FGinkgoAICondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGinkgoAINodeData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

	/** 普通状态匹配当前感知事实。StandGuard 条件表示本次接管尚未目击敌人，供首次目击前的警戒状态使用。 */
	UPROPERTY(EditAnywhere, Category = "Condition")
	EGinkgoAIBehavior Behavior = EGinkgoAIBehavior::Patrol;

	/** 为真时判断事实已经不成立，供状态退出转换使用。 */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;

	/** 编译时链接的控制器句柄，执行上下文在每次推进时提供当前控制器。 */
	TStateTreeExternalDataHandle<AGinkgoAIController> ControllerHandle;
};

/**
 * 执行一个行为。巡逻完成一个点即成功，搜索超时成功；交战与推进持续运行，直到事实转换中断。
 * ExitState 撤销移动与射击；仅 Engage/Pursue 的短暂视线过渡可保留墙边选点，换树和死亡始终清理 EQS 与探身。
 */
USTRUCT(meta = (DisplayName = "AI Behavior"))
struct CASEGINKGO_API FGinkgoAIBehaviorTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGinkgoAINodeData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	/** 本任务的职责，应与所在状态的进入条件一致。 */
	UPROPERTY(EditAnywhere, Category = "Behavior")
	EGinkgoAIBehavior Behavior = EGinkgoAIBehavior::Patrol;

	/** 当前任务的选位和移动偏好；冲刺树在交战、追踪和调查时使用同一套冲刺规则。 */
	UPROPERTY(EditAnywhere, Category = "Behavior")
	FGinkgoAITactics Tactics;

	/** 当前控制器的外部数据句柄；每个运行实例由自己的执行上下文解析。 */
	TStateTreeExternalDataHandle<AGinkgoAIController> ControllerHandle;
};

/** 冲刺任务实例的输入。StateTree 在进入状态前将本实例的根参数复制到这里。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoAISprintTaskData
{
	GENERATED_BODY()

	/** 根参数的持久标识，供资产生成和旧出生位迁移共用，关卡作者改名后仍能识别。 */
	static const FGuid RangeParameterID;

	/** 可见目标进入此距离（米）时停止冲刺并准备射击。0 是有效的覆盖值。 */
	UPROPERTY(EditAnywhere, Category = "Input", meta = (DisplayName = "冲刺射击距离", ClampMin = "0", ForceUnits = "m"))
	float SprintFireRangeMeters = 0.f;
};

/** 冲刺树的交战与追踪任务，沿用通用任务的执行和退出流程，距离由树的参数绑定提供。 */
USTRUCT(meta = (DisplayName = "AI Sprint To Range"))
struct CASEGINKGO_API FGinkgoAISprintTask : public FGinkgoAIBehaviorTask
{
	GENERATED_BODY()

	using FInstanceDataType = FGinkgoAISprintTaskData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "StateTreeConditionBase.h"
#include "StateTreeExecutionTypes.h"      // TStateTreeExternalDataHandle
#include "StateTreeSchema.h"
#include "StateTreeTaskBase.h"
#include "Weapon/GinkgoWeaponTypes.h"     // EGinkgoWeaponRootState
#include "GinkgoWeaponStateTree.generated.h"

class UGinkgoAnimAction;
class UGinkgoWeaponComponent;

/**
 * @class UGinkgoWeaponStateTreeSchema : 武器的动作状态的树
 * 见 Docs/Design/ActionStates.md：树只管手现在在干什么，枪的事实记在枪上。根树所有枪共用，每种枪机一棵子树，都用这个 schema。
 * 只在本地控制的角色上跑，由武器组件推进（UGinkgoWeaponComponent::UpdateHoldState），不用 UStateTreeComponent 自己的 tick。
 *
 * 节点要的东西都从武器组件来（外部数据）：它在选状态、跑任务之间一直有效，节点不用一个个绑属性。
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "Ginkgo Weapon Action States", CommonSchema))
class CASEGINKGO_API UGinkgoWeaponStateTreeSchema : public UStateTreeSchema
{
	GENERATED_BODY()

public:
	//~ Begin UStateTreeSchema
	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;
	virtual bool IsClassAllowed(const UClass* InClass) const override;
	virtual bool IsExternalItemAllowed(const UStruct& InStruct) const override;
	//~ End UStateTreeSchema
};

/** 不要实例数据的节点用它：树的编译器要求节点有这个类型 */
USTRUCT()
struct CASEGINKGO_API FGinkgoWeaponNoInstanceData
{
	GENERATED_BODY()
};

/**
 * @struct FGinkgoWeaponRootStateCondition : 根树的状态按手上的槽与目标槽选（Ginkgo::Weapon::SelectRootState）
 * 进入条件用它，"这个状态不再成立、回到根重选"的转换用它加 bInvert
 */
USTRUCT(meta = (DisplayName = "Weapon Root State"))
struct CASEGINKGO_API FGinkgoWeaponRootStateCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGinkgoWeaponNoInstanceData;

	//~ Begin FStateTreeConditionBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	//~ End FStateTreeConditionBase

	UPROPERTY(EditAnywhere, Category = "Condition")
	EGinkgoWeaponRootState State = EGinkgoWeaponRootState::Unarmed;

	/** 反过来：现在不是这个状态 */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;

	TStateTreeExternalDataHandle<UGinkgoWeaponComponent> WeaponsHandle;
};

/**
 * @struct FGinkgoWeaponActionCondition : 枪的子树的状态按枪的事实与意图选（Ginkgo::Weapon::SelectWeaponAction）
 * 进入条件用它，"这个状态不再成立、回到子树的根重选"的转换用它加 bInvert
 */
USTRUCT(meta = (DisplayName = "Weapon Action"))
struct CASEGINKGO_API FGinkgoWeaponActionCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGinkgoWeaponNoInstanceData;

	//~ Begin FStateTreeConditionBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	//~ End FStateTreeConditionBase

	UPROPERTY(EditAnywhere, Category = "Condition")
	EGinkgoWeaponAction Action = EGinkgoWeaponAction::Ready;

	/** 反过来：现在不该做这个 */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;

	TStateTreeExternalDataHandle<UGinkgoWeaponComponent> WeaponsHandle;
};

/**
 * @struct FGinkgoWeaponRaisedCondition : 枪抬到位了：低持时按开火要等它（你定的）
 */
USTRUCT(meta = (DisplayName = "Weapon Raised"))
struct CASEGINKGO_API FGinkgoWeaponRaisedCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGinkgoWeaponNoInstanceData;

	//~ Begin FStateTreeConditionBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	//~ End FStateTreeConditionBase

	/** 反过来：还没抬到位 */
	UPROPERTY(EditAnywhere, Category = "Condition")
	bool bInvert = false;

	TStateTreeExternalDataHandle<UGinkgoWeaponComponent> WeaponsHandle;
};

/**
 * @struct FGinkgoWeaponIdleTask : 等：一直跑着，什么也不做。状态靠转换离开（徒手、就绪）
 */
USTRUCT(meta = (DisplayName = "Weapon Idle"))
struct CASEGINKGO_API FGinkgoWeaponIdleTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGinkgoWeaponNoInstanceData;

	FGinkgoWeaponIdleTask();

	//~ Begin FStateTreeTaskBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	//~ End FStateTreeTaskBase
};

USTRUCT()
struct CASEGINKGO_API FGinkgoWeaponStepTaskInstanceData
{
	GENERATED_BODY()

	/** 这一步在跑的动画动作：完成了状态就完成，退出状态时还没完就取消 */
	UPROPERTY()
	TObjectPtr<UGinkgoAnimAction> Action;
};

/**
 * @struct FGinkgoWeaponStepTask : 跑一步
 * 树里只有这一种动作任务：Step 是 Weapon.Step.* 的标签，武器组件按标签找这把枪的片段，用动画动作跑（UGinkgoWeaponComponent::StartStep）。
 * 所以树里不按枪写分支，差别是子树的结构、定义里的片段与机构的开关。
 *
 * 完成：动作完成是成功，别的结局（放不起来、被顶掉）是失败。拔枪、收枪失败时目标槽作废，不反复重试；枪上的动作不用：回到子树的根按事实重选，手不空时就是就绪。
 * 打断：转换离开状态时取消动作，没到通知的事件按"跳过"不发，机构状态停在最后一个发过的事件上。
 */
USTRUCT(meta = (DisplayName = "Weapon Step"))
struct CASEGINKGO_API FGinkgoWeaponStepTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGinkgoWeaponStepTaskInstanceData;

	//~ Begin FStateTreeTaskBase
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	//~ End FStateTreeTaskBase

	/** 哪一步 */
	UPROPERTY(EditAnywhere, Category = "Step", meta = (Categories = "Weapon.Step"))
	FGameplayTag Step;

	/** 这一步里能不能开镜。拔枪、收枪、拉栓、装填不能（你说的）：按着开镜要等放完才响应 */
	UPROPERTY(EditAnywhere, Category = "Step")
	bool bAllowAim = false;

	/** 这一步里能不能抬枪：不能的时候不留在腰射。拔枪、收枪、压弹不能 */
	UPROPERTY(EditAnywhere, Category = "Step")
	bool bAllowRaise = false;

	/** 这一步两只手都在枪上：冲刺时枪不用单手举着，放低。拉栓、装填要 */
	UPROPERTY(EditAnywhere, Category = "Step")
	bool bBothHandsOnGun = false;

	/** 这一步没做成时目标槽作废：拔枪、收枪要，枪上的动作不要 */
	UPROPERTY(EditAnywhere, Category = "Step")
	bool bAbandonTargetOnFail = true;

	TStateTreeExternalDataHandle<UGinkgoWeaponComponent> WeaponsHandle;
};

#pragma once

#include "CoreMinimal.h"
#include "StateTreeReference.h"
#include "GinkgoAIBehaviorOverrides.generated.h"

class UStateTree;

/** 单个敌人的关卡行为配置。出生位在角色开始游戏前应用，状态树和武器的共享资产可供其他敌人继续使用。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoAIBehaviorOverrides
{
	GENERATED_BODY()

	/** 此敌人的决策树及根参数。只勾选要覆盖的参数，留空树时随手持武器选择决策树。 */
	UPROPERTY(EditAnywhere, Category = "AI", meta = (DisplayName = "覆盖状态树", Schema = "/Script/CaseGinkgo.GinkgoAIStateTreeSchema"))
	FStateTreeReference Reference;

	/** 载入旧关卡时，将旧树指针和当时有效的距离迁入标准参数覆盖。 */
	void MigrateLegacy();

	/** 未指定树的老出生位也可能保存过距离；只在实际选中了原冲刺树时应用这份兼容参数。 */
	const FStateTreeReference* GetParametersFor(const UStateTree* SelectedTree) const;

	/** 将出生位显式指定的配置应用到角色，保留角色类默认的决策树。 */
	void ApplyTo(FGinkgoAIBehaviorOverrides& Enemy) const;

private:
	/** 老版本只填距离、仍由武器选树的实例。迁移后用原生参数保存，仅在对应树启动时读取。 */
	UPROPERTY()
	FStateTreeReference LegacyWeaponParameters;

	/** 首版选定的树，仅用于反序列化旧关卡。 */
	UPROPERTY()
	TObjectPtr<UStateTree> StateTree_DEPRECATED;

	/** 首版独立距离（米），仅用于加载迁移。旧版的正数才表示曾覆盖。 */
	UPROPERTY()
	float SprintFireRangeMeters_DEPRECATED = 0.f;
};

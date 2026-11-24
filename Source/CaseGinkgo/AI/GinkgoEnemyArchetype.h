#pragma once

#include "Combat/GinkgoCombatTypes.h"
#include "Engine/DataAsset.h"
#include "GinkgoEnemyArchetype.generated.h"

class UGinkgoCharacterModel;

/** 敌人出生时的外观、生命和防具模板。每个实例单独持有剩余生命与耐久，可与任意武器和决策树组合。 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoEnemyArchetype : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 着装及其项目动画重定向配置。仅显示角色的端加载，专用服务器只读取基础数值。 */
	UPROPERTY(EditAnywhere, Category = "Enemy")
	TSoftObjectPtr<UGinkgoCharacterModel> Model;

	/** 满生命值（点），每次生成时回满。 */
	UPROPERTY(EditAnywhere, Category = "Enemy", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	/** 出生时穿戴的防具。每件的耐久由其覆盖部位共享，空数组表示无甲。 */
	UPROPERTY(EditAnywhere, Category = "Enemy")
	TArray<FGinkgoArmorPiece> Armor;
};

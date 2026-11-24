#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "GinkgoAIAssetsCommandlet.generated.h"

/**
 * 生成 /Game/AI 的默认、逼近、冲刺与驻守行为资产。-ForceTree 重建决策树，-WeaponBehaviors 重绑样例武器。
 * -Demo 创建原单敌人地图，-WeaponDemo 创建独立三类敌人地图；只有同时指定 -ForceDemo 才重建相应地图。
 */
UCLASS()
class UGinkgoAIAssetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoAIAssetsCommandlet();
	virtual int32 Main(const FString& Params) override;
};

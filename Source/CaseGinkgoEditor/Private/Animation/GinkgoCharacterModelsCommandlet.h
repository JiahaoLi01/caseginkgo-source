#pragma once

#include "Commandlets/Commandlet.h"
#include "GinkgoCharacterModelsCommandlet.generated.h"

/** 生成项目角色外观、IK 重定向图与三个敌人类型。重复运行只补缺失资产，保留美术和关卡调整。 */
UCLASS()
class UGinkgoCharacterModelsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoCharacterModelsCommandlet();
	virtual int32 Main(const FString& Params) override;
};

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "GinkgoEnvQueryTest_Cover.generated.h"

/** 用选位组件冻结的威胁点筛选低掩体与可侧向探身的墙边；两者平等参与后续距离评分，不读取隐藏玩家位置。 */
UCLASS(meta = (DisplayName = "Ginkgo: Cover With Firing Lane"))
class CASEGINKGO_API UGinkgoEnvQueryTest_Cover : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UGinkgoEnvQueryTest_Cover();
	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
};

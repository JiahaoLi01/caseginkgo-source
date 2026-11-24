#pragma once

#include "Commandlets/Commandlet.h"
#include "GinkgoTraversalCommandlet.generated.h"

/** 生成三类攀爬蒙太奇及数据资产；仅写 /Game/Characters/Traversal，原始序列保持只读。 */
UCLASS()
class UGinkgoTraversalCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UGinkgoTraversalCommandlet();
	virtual int32 Main(const FString& Params) override;
};

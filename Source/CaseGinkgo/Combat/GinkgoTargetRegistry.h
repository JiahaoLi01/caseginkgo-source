#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GinkgoTargetRegistry.generated.h"

class UGinkgoDamageableComponent;

/**
 * @class UGinkgoTargetRegistry : 世界里可打击的目标（有 UGinkgoDamageableComponent 的 actor）
 * 目标开始时登记、结束时去掉，辅助瞄准每帧从这里挑，不用遍历所有 actor
 */
UCLASS()
class CASEGINKGO_API UGinkgoTargetRegistry : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UGinkgoTargetRegistry* Get(const UObject* WorldContextObject);

	void Add(UGinkgoDamageableComponent* Target);
	void Remove(UGinkgoDamageableComponent* Target);

	const TArray<TWeakObjectPtr<UGinkgoDamageableComponent>>& GetTargets() const { return Targets; }

private:
	TArray<TWeakObjectPtr<UGinkgoDamageableComponent>> Targets;
};

#pragma once

#include "CoreMinimal.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "GinkgoAICharacterMovementComponent.generated.h"

/**
 * AI 的导航输入适配：把行为期望速度换成模拟输入幅度，与玩家轻推手柄共用移动、意图快照和步态选择。
 * GetMaxSpeed 保持父类语义（满幅输入下的速度），不能用降低它的方式慢走，否则满幅意图仍会选到 Run。
 * 导航单独读取行为速度，用于终点减速；已有的路径减速输入继续乘在模拟幅度上。
 */
UCLASS()
class CASEGINKGO_API UGinkgoAICharacterMovementComponent : public UGinkgoCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGinkgoAICharacterMovementComponent();
	virtual void RequestPathMove(const FVector& MoveInput) override;
	virtual float GetMaxSpeedForNavMovement() const override;

	/** 设置期望移动速度（cm/s），下一次路径输入按当时持枪、ADS、姿态允许的满幅速度换算；0 表示满幅。 */
	void SetBehaviorSpeed(float Speed) { BehaviorSpeed = FMath::Max(0.f, Speed); }

private:
	/** 当前行为的期望速度（cm/s），状态树任务进入时设置；0 使用满幅输入，不改变父类移动参数。 */
	float BehaviorSpeed = 0.f;
};

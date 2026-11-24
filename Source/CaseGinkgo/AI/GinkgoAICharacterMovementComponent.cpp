#include "AI/GinkgoAICharacterMovementComponent.h"

UGinkgoAICharacterMovementComponent::UGinkgoAICharacterMovementComponent()
{
	// 项目的 CalcVelocity 消费 Acceleration；直接请求速度会绕过这条路径，导致有导航路径却不走。
	GetNavMovementProperties()->bUseAccelerationForPaths = true;
}

void UGinkgoAICharacterMovementComponent::RequestPathMove(const FVector& MoveInput)
{
	const float FullInputSpeed = Super::GetMaxSpeed();
	const float InputScale = FullInputSpeed > UE_SMALL_NUMBER ? GetMaxSpeedForNavMovement() / FullInputSpeed : 0.f;
	// 保留导航在终点附近已经缩小的幅度。父类把输入送入 Pawn，再由相同的模拟输入管线产生
	// Acceleration / AnalogInputModifier：物理的 wishspeed 与 AnimInstance 的 Intent.Scale 因此保持一致。
	Super::RequestPathMove(MoveInput * InputScale);
}

float UGinkgoAICharacterMovementComponent::GetMaxSpeedForNavMovement() const
{
	return BehaviorSpeed > 0.f ? FMath::Min(Super::GetMaxSpeed(), BehaviorSpeed) : Super::GetMaxSpeed();
}

#include "AI/GinkgoNoiseEmitterComponent.h"

#include "Animation/GinkgoAnimTypes.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AISense_Hearing.h"

const FName UGinkgoNoiseEmitterComponent::GunshotTag(TEXT("Gunshot"));

UGinkgoNoiseEmitterComponent::UGinkgoNoiseEmitterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGinkgoNoiseEmitterComponent::ReportFootstep(const EGinkgoAnimGait Gait)
{
	const AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement || !Movement->IsMovingOnGround() || Movement->Velocity.SizeSquared2D() < 1.0) return;
	const float Range = Gait == EGinkgoAnimGait::Sprint ? SprintRange : Gait == EGinkgoAnimGait::Run ? RunRange : 0.f;
	Report(Movement->GetActorFeetLocation(), Range, Gait == EGinkgoAnimGait::Sprint ? TEXT("Sprint") : TEXT("Run"));
}

void UGinkgoNoiseEmitterComponent::ReportShot(const FVector& Muzzle, const float Range)
{
	Report(Muzzle, Range, GunshotTag);
}

void UGinkgoNoiseEmitterComponent::Report(const FVector& Location, const float Range, const FName Tag)
{
	check(IsInGameThread());
	AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(GetOwner());
	if (!Character || !Character->HasAuthority() || Character->GetDamageable()->IsDead() || Range <= 0.f) return;
	UAISense_Hearing::ReportNoiseEvent(this, Location, 1.f, Character, Range, Tag);
}

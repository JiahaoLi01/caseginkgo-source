#include "Combat/GinkgoPenetrableComponent.h"

#include "GameFramework/Actor.h"

UGinkgoPenetrableComponent::UGinkgoPenetrableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

float UGinkgoPenetrableComponent::FindThickness(const AActor* Actor)
{
	const UGinkgoPenetrableComponent* Penetrable = Actor ? Actor->FindComponentByClass<UGinkgoPenetrableComponent>() : nullptr;
	return Penetrable ? FMath::Max(Penetrable->Thickness, 0.f) : -1.f;
}

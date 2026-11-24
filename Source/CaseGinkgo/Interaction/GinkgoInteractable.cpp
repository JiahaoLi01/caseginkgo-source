#include "Interaction/GinkgoInteractable.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

AActor* Ginkgo::Interaction::GetInteractableActor(const UObject* Interactable)
{
	if (const UActorComponent* Component = Cast<UActorComponent>(Interactable))
	{
		return Component->GetOwner();
	}
	return const_cast<AActor*>(Cast<AActor>(Interactable));
}

FString Ginkgo::Interaction::DescribeInteractable(const UObject* Interactable)
{
	if (const UActorComponent* Component = Cast<UActorComponent>(Interactable))
	{
		return FString::Printf(TEXT("%s.%s"), *GetNameSafe(Component->GetOwner()), *Component->GetName());
	}
	return GetNameSafe(Interactable);
}

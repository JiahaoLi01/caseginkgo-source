#include "Interaction/GinkgoInteractableComponent.h"

#include "Engine/World.h"
#include "Interaction/GinkgoInteractionSubsystem.h"

UGinkgoInteractableComponent::UGinkgoInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ActionText = NSLOCTEXT("Ginkgo", "InteractAction", "交互");
}

FVector UGinkgoInteractableComponent::GetInteractionLocation() const
{
	return GetComponentLocation();
}

FGinkgoInteractionRanges UGinkgoInteractableComponent::GetInteractionRanges() const
{
	return {PromptRadius, InteractRadius};
}

FText UGinkgoInteractableComponent::GetInteractionText() const
{
	if (DisplayName.IsEmpty())
	{
		return ActionText;
	}
	return FText::Format(NSLOCTEXT("Ginkgo", "InteractionText", "{0} {1}"), ActionText, DisplayName);
}

bool UGinkgoInteractableComponent::ShowsPromptThroughWalls() const
{
	return bShowPromptThroughWalls;
}

EGinkgoInteractionAvailability UGinkgoInteractableComponent::GetAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
{
	return EGinkgoInteractionAvailability::Available;
}

void UGinkgoInteractableComponent::Interact(const FGinkgoInteractionContext& Context)
{
}

void UGinkgoInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UGinkgoInteractionSubsystem* Subsystem = UWorld::GetSubsystem<UGinkgoInteractionSubsystem>(GetWorld()))
	{
		Subsystem->Register(this);
	}
}

void UGinkgoInteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGinkgoInteractionSubsystem* Subsystem = UWorld::GetSubsystem<UGinkgoInteractionSubsystem>(GetWorld()))
	{
		Subsystem->Unregister(this);
	}
	Super::EndPlay(EndPlayReason);
}

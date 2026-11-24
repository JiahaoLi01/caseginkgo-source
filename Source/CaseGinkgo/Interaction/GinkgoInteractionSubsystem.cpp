#include "Interaction/GinkgoInteractionSubsystem.h"

#include "Utilities/Debug/DebugCategory.h"

void UGinkgoInteractionSubsystem::Register(UObject* Interactable)
{
	if (!Interactable || !Interactable->Implements<UGinkgoInteractable>())
	{
		UE_LOG(LogGinkgoInteraction, Warning, TEXT("[Interaction] %s 没实现 IGinkgoInteractable，不登记"), *GetNameSafe(Interactable));
		return;
	}
	Interactables.RemoveAll([](const TWeakInterfacePtr<IGinkgoInteractable>& Entry) { return !Entry.IsValid(); });
	Interactables.AddUnique(TWeakInterfacePtr<IGinkgoInteractable>(Interactable));
}

void UGinkgoInteractionSubsystem::Unregister(UObject* Interactable)
{
	Interactables.RemoveAll([Interactable](const TWeakInterfacePtr<IGinkgoInteractable>& Entry)
	{
		return !Entry.IsValid() || Entry.GetObject() == Interactable;
	});
}

bool UGinkgoInteractionSubsystem::IsRegistered(const UObject* Interactable) const
{
	return Interactable && Interactables.ContainsByPredicate([Interactable](const TWeakInterfacePtr<IGinkgoInteractable>& Entry)
	{
		return Entry.GetObject() == Interactable;
	});
}

bool UGinkgoInteractionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

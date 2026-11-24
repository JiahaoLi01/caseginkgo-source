#include "Framework/GinkgoEventHub.h"
#include "GameFramework/Actor.h"

UGinkgoEventHub* UGinkgoEventHub::Find(const AActor* OwnerActor)
{
	return IsValid(OwnerActor) ? OwnerActor->FindComponentByClass<UGinkgoEventHub>() : nullptr;
}

void UGinkgoEventHub::Subscribe(FGameplayTag Tag, FGinkgoEventListener Listener)
{
	if (Tag.IsValid() && Listener.IsBound()) { Channels.FindOrAdd(Tag).AddUnique(Listener); }
}

void UGinkgoEventHub::Unsubscribe(FGameplayTag Tag, FGinkgoEventListener Listener)
{
	if (auto* Channel = Channels.Find(Tag))
	{
		Channel->Remove(Listener);
		if (!Channel->IsBound()) { Channels.Remove(Tag); }
	}
}

void UGinkgoEventHub::Publish(const FGinkgoActorEvent& Event)
{
	if (const auto* Channel = Channels.Find(Event.Tag))
	{
		const FGinkgoEventChannel Snapshot = *Channel;
		Snapshot.Broadcast(Event);
	}
}

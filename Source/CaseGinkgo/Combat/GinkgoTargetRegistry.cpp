#include "Combat/GinkgoTargetRegistry.h"

#include "Combat/GinkgoDamageableComponent.h"
#include "Engine/World.h"

UGinkgoTargetRegistry* UGinkgoTargetRegistry::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UGinkgoTargetRegistry>() : nullptr;
}

void UGinkgoTargetRegistry::Add(UGinkgoDamageableComponent* Target)
{
	if (Target)
	{
		Targets.AddUnique(Target);
	}
}

void UGinkgoTargetRegistry::Remove(UGinkgoDamageableComponent* Target)
{
	Targets.RemoveAll([Target](const TWeakObjectPtr<UGinkgoDamageableComponent>& Weak) { return !Weak.IsValid() || Weak.Get() == Target; });
}

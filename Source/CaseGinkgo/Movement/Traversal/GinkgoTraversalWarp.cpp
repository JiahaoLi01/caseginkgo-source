#include "GinkgoTraversalWarp.h"

#include "GinkgoTraversalComponent.h"
#include "GameFramework/Actor.h"

FTransform UGinkgoTraversalWarp::ProcessRootMotion(const FTransform& InRootMotion, const float DeltaSeconds)
{
	const auto* Actor = GetActorOwner();
	auto* Traversal = Actor ? Actor->FindComponentByClass<UGinkgoTraversalComponent>() : nullptr;
	return Traversal ? Traversal->ExtractMotion(Animation.Get(), PreviousPosition, CurrentPosition) : InRootMotion;
}

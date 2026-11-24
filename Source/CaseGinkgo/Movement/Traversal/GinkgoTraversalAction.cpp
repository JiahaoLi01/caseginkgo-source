#include "GinkgoTraversalAction.h"

#include "GinkgoTraversalComponent.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"

UGinkgoTraversalAction* UGinkgoTraversalAction::Make(UGinkgoTraversalComponent& Owner)
{
	auto* Action = NewObject<UGinkgoTraversalAction>(&Owner);
	Action->Traversal = &Owner;
	Action->SetActionName(TEXT("Traversal"));
	Action->Occupy(FAnimActionTags::Occupancy_LeftArm, Ginkgo::Occupancy::PriorityTraversal);
	if (Owner.RequiresBothHands()) { Action->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityTraversal); }
	return Action;
}

void UGinkgoTraversalAction::OnStart(FGinkgoAnimActionSetup& Setup)
{
	if (!Traversal.IsValid() || !Traversal->BeginAction(*this))
	{
		Setup.GetContext().Abort(FAnimActionTags::AnimAction_Cancel_OwnerExit);
		return;
	}
	Setup.Play(Traversal->GetActiveMontage());
	Setup.CompleteAt(Ginkgo::Traversal::LandedEvent);
	Setup.AbortIf(NAME_None, [this](const FGinkgoAnimActionContext&)
	{
		return !Traversal.IsValid() || !Traversal->CanContinue();
	}, FAnimActionTags::AnimAction_Abort_Interrupted);
}

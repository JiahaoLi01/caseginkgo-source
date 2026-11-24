#include "GinkgoMotionWarpingComponent.h"

#include "RootMotionModifier.h"

void UGinkgoMotionWarpingComponent::RemoveModifiersForAnimation(const UAnimSequenceBase* Animation)
{
	if (!Animation) { return; }
	Modifiers.RemoveAll([Animation](URootMotionModifier* Modifier)
	{
		if (!Modifier || Modifier->Animation != Animation) { return false; }
		Modifier->SetState(ERootMotionModifierState::MarkedForRemoval);
		return true;
	});
}

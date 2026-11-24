#include "AI/GinkgoAIBehaviorOverrides.h"

#include "AI/GinkgoAIStateTree.h"
#include "StateTree.h"

void FGinkgoAIBehaviorOverrides::MigrateLegacy()
{
	if (!StateTree_DEPRECATED && SprintFireRangeMeters_DEPRECATED <= 0.f) { return; }
	FStateTreeReference& Destination = StateTree_DEPRECATED ? Reference : LegacyWeaponParameters;
	UStateTree* Tree = StateTree_DEPRECATED ? StateTree_DEPRECATED.Get() : LoadObject<UStateTree>(nullptr, TEXT("/Game/AI/ST_Enemy_SprintToRange"));
	if (Tree)
	{
		Tree->ConditionalPostLoad();
		Destination.SetStateTree(Tree);
		const auto* Desc = Destination.GetParameters().FindPropertyDescByID(FGinkgoAISprintTaskData::RangeParameterID);
		if (Desc && SprintFireRangeMeters_DEPRECATED > 0.f)
		{
			const FName Range = Desc->Name;
			Destination.SetPropertyOverridden(Desc->ID, true);
			Destination.GetMutableParameters().SetValueFloat(Range, SprintFireRangeMeters_DEPRECATED);
		}
	}
	StateTree_DEPRECATED = nullptr;
	SprintFireRangeMeters_DEPRECATED = 0.f;
}

const FStateTreeReference* FGinkgoAIBehaviorOverrides::GetParametersFor(const UStateTree* SelectedTree) const
{
	if (Reference.GetStateTree() == SelectedTree) { return &Reference; }
	return LegacyWeaponParameters.GetStateTree() == SelectedTree ? &LegacyWeaponParameters : nullptr;
}

void FGinkgoAIBehaviorOverrides::ApplyTo(FGinkgoAIBehaviorOverrides& Enemy) const
{
	if (Reference.IsValid()) { Enemy.Reference = Reference; }
	if (LegacyWeaponParameters.IsValid())
	{
		if (Enemy.Reference.GetStateTree() == LegacyWeaponParameters.GetStateTree()) { Enemy.Reference = LegacyWeaponParameters; }
		else { Enemy.LegacyWeaponParameters = LegacyWeaponParameters; }
	}
}

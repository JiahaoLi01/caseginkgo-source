#include "AI/GinkgoAIStateTree.h"

#include "AI/GinkgoAIController.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"
#include "StateTreeEvaluatorBase.h"

const FGuid FGinkgoAISprintTaskData::RangeParameterID = FGuid::NewDeterministicGuid(TEXT("CaseGinkgo.AI.SprintToRange.FireRangeMeters"));

bool UGinkgoAIStateTreeSchema::IsStructAllowed(const UScriptStruct* Struct) const
{
	return Struct->IsChildOf(FStateTreeConditionCommonBase::StaticStruct())
		|| Struct->IsChildOf(FStateTreeTaskCommonBase::StaticStruct())
		|| Struct->IsChildOf(FStateTreeEvaluatorCommonBase::StaticStruct());
}

bool UGinkgoAIStateTreeSchema::IsClassAllowed(const UClass* Class) const
{
	return IsChildOfBlueprintBase(Class);
}

bool UGinkgoAIStateTreeSchema::IsExternalItemAllowed(const UStruct& Struct) const
{
	return Struct.IsChildOf(AGinkgoAIController::StaticClass());
}

bool FGinkgoAICondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(ControllerHandle);
	return true;
}

bool FGinkgoAICondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const auto& AI = Context.GetExternalData(ControllerHandle);
	const bool bMatches = Behavior == EGinkgoAIBehavior::StandGuard ? !AI.HasSeenHostile() : AI.GetKnowledge().GetBehavior() == Behavior;
	return bMatches != bInvert;
}

bool FGinkgoAIBehaviorTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(ControllerHandle);
	return true;
}

EStateTreeRunStatus FGinkgoAIBehaviorTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	Context.GetExternalData(ControllerHandle).BeginBehavior(Behavior, Tactics);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FGinkgoAIBehaviorTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	return Context.GetExternalData(ControllerHandle).TickBehavior(Behavior, DeltaTime);
}

void FGinkgoAIBehaviorTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	Context.GetExternalData(ControllerHandle).EndBehavior();
}

EStateTreeRunStatus FGinkgoAISprintTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	FGinkgoAITactics Settings = Tactics;
	Settings.Positioning = EGinkgoAIPositioning::SprintToRange;
	Settings.ApproachDistance = FMath::Max(0.f, Context.GetInstanceData(*this).SprintFireRangeMeters) * 100.f;
	Context.GetExternalData(ControllerHandle).BeginBehavior(Behavior, Settings);
	return EStateTreeRunStatus::Running;
}

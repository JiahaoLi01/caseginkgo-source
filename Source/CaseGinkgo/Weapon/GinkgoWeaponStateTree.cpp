#include "Weapon/GinkgoWeaponStateTree.h"

#include "Animation/Actions/GinkgoAnimAction.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"
#include "StateTreePropertyFunctionBase.h"
#include "Weapon/GinkgoWeaponComponent.h"

bool UGinkgoWeaponStateTreeSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	return InScriptStruct->IsChildOf(FStateTreeConditionCommonBase::StaticStruct())
		|| InScriptStruct->IsChildOf(FStateTreeEvaluatorCommonBase::StaticStruct())
		|| InScriptStruct->IsChildOf(FStateTreeTaskCommonBase::StaticStruct())
		|| InScriptStruct->IsChildOf(FStateTreeConsiderationCommonBase::StaticStruct())
		|| InScriptStruct->IsChildOf(FStateTreePropertyFunctionCommonBase::StaticStruct());
}

bool UGinkgoWeaponStateTreeSchema::IsClassAllowed(const UClass* InClass) const
{
	return IsChildOfBlueprintBase(InClass);
}

bool UGinkgoWeaponStateTreeSchema::IsExternalItemAllowed(const UStruct& InStruct) const
{
	return InStruct.IsChildOf(UGinkgoWeaponComponent::StaticClass());
}

bool FGinkgoWeaponRootStateCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(WeaponsHandle);
	return true;
}

bool FGinkgoWeaponRootStateCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const UGinkgoWeaponComponent& Weapons = Context.GetExternalData(WeaponsHandle);
	return (Ginkgo::Weapon::SelectRootState(Weapons.GetHeldSlot(), Weapons.GetTargetSlot()) == State) != bInvert;
}

bool FGinkgoWeaponActionCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(WeaponsHandle);
	return true;
}

bool FGinkgoWeaponActionCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	return (Context.GetExternalData(WeaponsHandle).SelectWeaponAction() == Action) != bInvert;
}

bool FGinkgoWeaponRaisedCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(WeaponsHandle);
	return true;
}

bool FGinkgoWeaponRaisedCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	return Context.GetExternalData(WeaponsHandle).IsRaised() != bInvert;
}

FGinkgoWeaponIdleTask::FGinkgoWeaponIdleTask()
{
	bShouldCallTick = false;
}

bool FGinkgoWeaponStepTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(WeaponsHandle);
	return true;
}

EStateTreeRunStatus FGinkgoWeaponStepTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UGinkgoWeaponComponent& Weapons = Context.GetExternalData(WeaponsHandle);
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Weapons.SetAimAllowed(bAllowAim);
	Weapons.SetRaiseAllowed(bAllowRaise);
	Weapons.SetBothHandsOnGun(bBothHandsOnGun);
	Data.Action = Weapons.StartStep(Step);
	if (!Data.Action)
	{
		if (bAbandonTargetOnFail)
		{
			Weapons.AbandonTarget();
		}
		return EStateTreeRunStatus::Failed;
	}
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FGinkgoWeaponStepTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	if (!Data.Action)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (!Data.Action->IsFinished())
	{
		return EStateTreeRunStatus::Running;
	}
	if (Data.Action->GetResult().Outcome == EGinkgoAnimActionOutcome::Completed)
	{
		return EStateTreeRunStatus::Succeeded;
	}
	if (bAbandonTargetOnFail)
	{
		Context.GetExternalData(WeaponsHandle).AbandonTarget();
	}
	return EStateTreeRunStatus::Failed;
}

void FGinkgoWeaponStepTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	UGinkgoWeaponComponent& Weapons = Context.GetExternalData(WeaponsHandle);
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Weapons.SetAimAllowed(true);
	Weapons.SetRaiseAllowed(true);
	Weapons.SetBothHandsOnGun(false);
	Weapons.EndStep(Data.Action);
	Data.Action = nullptr;
}

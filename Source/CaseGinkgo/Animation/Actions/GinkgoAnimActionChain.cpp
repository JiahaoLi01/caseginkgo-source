#include "Animation/Actions/GinkgoAnimActionChain.h"

#include "Utilities/Debug/DebugCategory.h"

// ===== 定义 =====

FGinkgoAnimActionChainBuilder::FStepBuilder& FGinkgoAnimActionChainBuilder::FStepBuilder::Next(const FName Target)
{
	Builder.GetStep(Index).Next = Target;
	return *this;
}

FGinkgoAnimActionChainBuilder::FStepBuilder& FGinkgoAnimActionChainBuilder::FStepBuilder::OnAborted(const FName Target)
{
	Builder.GetStep(Index).OnAborted = Target;
	return *this;
}

FGinkgoAnimActionChainBuilder::FStepBuilder& FGinkgoAnimActionChainBuilder::FStepBuilder::On(const EGinkgoAnimActionOutcome Outcome, const FGameplayTag& Reason, const FName Target)
{
	// 取消总是结束：拥有者已经切走了，链不能再往下走
	if (ensureMsgf(Outcome == EGinkgoAnimActionOutcome::Completed || Outcome == EGinkgoAnimActionOutcome::Aborted,
		TEXT("链的转换只能按完成或中止写，取消总是结束")))
	{
		Builder.GetStep(Index).Rules.Add({Outcome, Reason, Target});
	}
	return *this;
}

FGinkgoAnimActionChainBuilder::FStepBuilder FGinkgoAnimActionChainBuilder::Step(const FName Name, FGinkgoAnimActionFactory Factory)
{
	FGinkgoAnimActionChainStep& NewStep = Chain.Steps.AddDefaulted_GetRef();
	NewStep.Name = Name;
	NewStep.Factory = MoveTemp(Factory);
	return FStepBuilder(*this, Chain.Steps.Num() - 1);
}

FGinkgoAnimActionChainBuilder::FStepBuilder FGinkgoAnimActionChainBuilder::Do(const FName Name, TFunction<void()> Function)
{
	FGinkgoAnimActionChainStep& NewStep = Chain.Steps.AddDefaulted_GetRef();
	NewStep.Name = Name;
	NewStep.Instant = MoveTemp(Function);
	return FStepBuilder(*this, Chain.Steps.Num() - 1);
}

FGinkgoAnimActionChainStep& FGinkgoAnimActionChainBuilder::GetStep(const int32 Index) const
{
	return Chain.Steps[Index];
}

// ===== 链 =====

UGinkgoAnimActionChain* UGinkgoAnimActionChain::MakeChain(UObject* Outer, const FName Name, FBuildFunction Build)
{
	UGinkgoAnimActionChain* Chain = NewObject<UGinkgoAnimActionChain>(Outer ? Outer : GetTransientPackage());
	Chain->SetActionName(Name);
	Chain->BuildFunction = MoveTemp(Build);
	return Chain;
}

FName UGinkgoAnimActionChain::GetCurrentStepName() const
{
	return Steps.IsValidIndex(CurrentIndex) ? Steps[CurrentIndex].Name : NAME_None;
}

void UGinkgoAnimActionChain::OnBuild(FGinkgoAnimActionChainBuilder& Builder)
{
	if (BuildFunction)
	{
		const FBuildFunction Function = BuildFunction;
		Function(Builder);
	}
}

void UGinkgoAnimActionChain::OnStart(FGinkgoAnimActionSetup& Setup)
{
	ensureMsgf(false, TEXT("链不自己放蒙太奇：执行器开始链时调 OnBuild，不调 OnStart"));
}

int32 UGinkgoAnimActionChain::FindStep(const FName Name) const
{
	return Steps.IndexOfByPredicate([Name](const FGinkgoAnimActionChainStep& Step) { return Step.Name == Name; });
}

int32 UGinkgoAnimActionChain::FindNext(const int32 From, const FGinkgoAnimActionResult& StepResult) const
{
	// 取消总是结束
	if (!Steps.IsValidIndex(From) || StepResult.Outcome == EGinkgoAnimActionOutcome::Cancelled)
	{
		return INDEX_NONE;
	}

	const FGinkgoAnimActionChainStep& Step = Steps[From];
	for (const FGinkgoAnimActionChainTransition& Rule : Step.Rules)
	{
		if (Rule.Outcome == StepResult.Outcome && (!Rule.Reason.IsValid() || StepResult.Reason.MatchesTag(Rule.Reason)))
		{
			return Rule.Target.IsNone() ? INDEX_NONE : FindStep(Rule.Target);
		}
	}

	const FName Target = StepResult.Outcome == EGinkgoAnimActionOutcome::Completed ? Step.Next
		: StepResult.Outcome == EGinkgoAnimActionOutcome::Aborted ? Step.OnAborted
		: NAME_None;
	return Target.IsNone() ? INDEX_NONE : FindStep(Target);
}

bool UGinkgoAnimActionChain::Validate() const
{
	bool bValid = true;
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const FGinkgoAnimActionChainStep& Step = Steps[Index];
		if (FindStep(Step.Name) != Index)
		{
			UE_LOG(LogGinkgoAnimAction, Error, TEXT("链 %s：步骤名 %s 重复了"), *GetActionName().ToString(), *Step.Name.ToString());
			bValid = false;
		}

		const auto CheckTarget = [this, &Step, &bValid](const FName Target)
		{
			if (!Target.IsNone() && FindStep(Target) == INDEX_NONE)
			{
				UE_LOG(LogGinkgoAnimAction, Error, TEXT("链 %s：步骤 %s 转到不存在的 %s"),
					*GetActionName().ToString(), *Step.Name.ToString(), *Target.ToString());
				bValid = false;
			}
		};
		CheckTarget(Step.Next);
		CheckTarget(Step.OnAborted);
		for (const FGinkgoAnimActionChainTransition& Rule : Step.Rules)
		{
			CheckTarget(Rule.Target);
		}
	}
	return bValid;
}

#include "Animation/Actions/GinkgoAnimAction.h"

#include "Animation/Actions/GinkgoAnimActionChain.h"
#include "Animation/Actions/GinkgoAnimActionRunner.h"

// ===== 回调里能做的事 =====

AActor* FGinkgoAnimActionContext::GetOwner() const
{
	return Runner.GetOwner();
}

float FGinkgoAnimActionContext::GetPosition() const
{
	return Action.InstanceID != INDEX_NONE ? Runner.Host.GetPosition(Action.InstanceID) : 0.f;
}

float FGinkgoAnimActionContext::GetEventTime(const FName Event) const
{
	return Runner.Host.GetEventTime(Action.Montage, Event);
}

bool FGinkgoAnimActionContext::IsWindowOpen(const FName Window) const
{
	return Action.OpenWindows.Contains(Window);
}

void FGinkgoAnimActionContext::Abort(const FGameplayTag& Reason)
{
	if (Action.IsRunning() && !Action.IsA<UGinkgoAnimActionChain>())
	{
		Runner.FinishLeaf(Action, FGinkgoAnimActionResult::Aborted(Reason), FGinkgoAnimActionRunner::EMontageEnd::Stop);
	}
}

void FGinkgoAnimActionContext::Complete(const FGameplayTag& Reason)
{
	if (Action.IsRunning() && !Action.IsA<UGinkgoAnimActionChain>())
	{
		Runner.FinishLeaf(Action, FGinkgoAnimActionResult::Completed(Reason), FGinkgoAnimActionRunner::EMontageEnd::Keep);
	}
}

void FGinkgoAnimActionContext::JumpToSection(const FName Section)
{
	Runner.QueueJumpToSection(Action, Section);
}

void FGinkgoAnimActionContext::SetNextSection(const FName From, const FName To)
{
	Runner.QueueSetNextSection(Action, From, To);
}

void FGinkgoAnimActionContext::SetHandIK(const EGinkgoHand Hand, const FGinkgoIKBinding& Binding)
{
	Runner.SetHandIK(Action, Hand, Binding);
}

void FGinkgoAnimActionContext::FreezeHandIK(const EGinkgoHand Hand)
{
	Runner.FreezeHandIK(Action, Hand);
}

void FGinkgoAnimActionContext::ReleaseHandIK(const EGinkgoHand Hand)
{
	Runner.ReleaseHandIK(Action, Hand);
}

void FGinkgoAnimActionContext::HoldCameraTag(const FGameplayTag& Tag)
{
	if (Action.IsRunning())
	{
		Action.CameraTags.AddTag(Tag);
	}
}

void FGinkgoAnimActionContext::ReleaseCameraTag(const FGameplayTag& Tag)
{
	Action.CameraTags.RemoveTag(Tag);
}

// ===== 开始时的声明 =====

void FGinkgoAnimActionSetup::Play(UAnimMontage* Montage, const float PlayRate, const FName StartSection, const float StartPosition, const float BlendInTime)
{
	Action.StartPosition = StartPosition;
	Action.BlendInTime = BlendInTime;
	Action.bPlayRequested = true;
	Action.Montage = Montage;
	Action.PlayRate = PlayRate;
	Action.StartSection = StartSection;
}

void FGinkgoAnimActionSetup::On(const FName Event, TFunction<void(FGinkgoAnimActionEvent&)> Handler, const EGinkgoMissedEvent Policy)
{
	FGinkgoAnimActionEventBinding& Binding = Action.Events.AddDefaulted_GetRef();
	Binding.Name = Event;
	Binding.Handler = MoveTemp(Handler);
	Binding.Policy = Policy;
}

void FGinkgoAnimActionSetup::AbortIf(const FName Window, TFunction<bool(const FGinkgoAnimActionContext&)> Condition, const FGameplayTag& Reason)
{
	FGinkgoAnimActionGuard& Guard = Action.Guards.AddDefaulted_GetRef();
	Guard.Window = Window;
	Guard.Condition = MoveTemp(Condition);
	Guard.Reason = Reason;
}

void FGinkgoAnimActionSetup::CompleteAt(const FName Event)
{
	Action.CompletionEvent = Event;
}

void FGinkgoAnimActionSetup::HoldFor(const float Seconds)
{
	ensureMsgf(!Action.bPlayRequested, TEXT("动画动作 %s：HoldFor 是没有蒙太奇的动作用的"), *Action.GetActionName().ToString());
	Action.HoldDuration = FMath::Max(Seconds, 0.f);
	Action.HoldElapsed = 0.f;
}

void FGinkgoAnimActionSetup::OnStopRequested(TFunction<bool(FGinkgoAnimActionContext&, const FGameplayTag&)> Handler)
{
	Action.StopHandler = MoveTemp(Handler);
}

void FGinkgoAnimActionSetup::OnFinished(TFunction<void(const FGinkgoAnimActionResult&)> Handler)
{
	Action.FinishHandler = MoveTemp(Handler);
}

// ===== 动作 =====

UGinkgoAnimAction* UGinkgoAnimAction::Make(UObject* Outer, const FName Name, FStartFunction Start)
{
	UGinkgoAnimAction* Action = NewObject<UGinkgoAnimAction>(Outer ? Outer : GetTransientPackage());
	Action->ActionName = Name;
	Action->StartFunction = MoveTemp(Start);
	return Action;
}

UGinkgoAnimAction* UGinkgoAnimAction::Occupy(const FGameplayTag& Tag, const int32 Priority)
{
	ensureMsgf(IsPending() && !bPlayRequested, TEXT("动画动作：占用要在启动之前声明"));
	Occupancy.Add({Tag, Priority});
	return this;
}

FName UGinkgoAnimAction::GetActionName() const
{
	return ActionName.IsNone() ? GetClass()->GetFName() : ActionName;
}

void FGinkgoAnimActionSetup::OnTick(TFunction<void(FGinkgoAnimActionContext&, float)> Handler)
{
	Action.TickHandler = MoveTemp(Handler);
}

void UGinkgoAnimAction::OnTick(FGinkgoAnimActionContext& Context, const float DeltaSeconds)
{
	if (TickHandler)
	{
		TickHandler(Context, DeltaSeconds);
	}
}

void UGinkgoAnimAction::OnStart(FGinkgoAnimActionSetup& Setup)
{
	if (StartFunction)
	{
		// 先复制一份：声明时就中止的话动作当场结束，回调都会被清掉
		const FStartFunction Function = StartFunction;
		Function(Setup);
	}
}

bool UGinkgoAnimAction::OnStopRequested(FGinkgoAnimActionContext& Context, const FGameplayTag& Reason)
{
	if (!StopHandler)
	{
		return false;
	}
	const FStopHandler Handler = StopHandler;
	return Handler(Context, Reason);
}

void UGinkgoAnimAction::OnFinished(const FGinkgoAnimActionResult& InResult)
{
	if (FinishHandler)
	{
		// 先复制一份：回调里可能再启动别的动作
		const FFinishHandler Handler = FinishHandler;
		Handler(InResult);
	}
}

void UGinkgoAnimAction::ClearCallbacks()
{
	StartFunction = nullptr;
	StopHandler = nullptr;
	FinishHandler = nullptr;
	TickHandler = nullptr;
	Events.Reset();
	Guards.Reset();
}

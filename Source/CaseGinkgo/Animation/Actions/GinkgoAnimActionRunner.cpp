#include "Animation/Actions/GinkgoAnimActionRunner.h"

#include "Algo/StableSort.h"                            // 补做按事件在蒙太奇里的时刻排先后
#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionChain.h"
#include "Animation/AnimMontage.h"                      // 日志里写蒙太奇的名字
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"

namespace Ginkgo::AnimActionRunner
{
	/** 应用阶段一次最多处理几轮：链连着走几步、新动作一放就失败，都在同一次应用里 */
	constexpr int32 MaxApplyRounds = 16;

	/** 链里连着走几个瞬时的步，就算转换写成了环 */
	constexpr int32 MaxInstantSteps = 16;
}

FGinkgoAnimActionRunner::FGinkgoAnimActionRunner(IGinkgoMontageHost& InHost, AActor* InOwner)
	: Host(InHost)
	, Owner(InOwner)
{
}

// ===== 拥有者 =====

bool FGinkgoAnimActionRunner::Start(UGinkgoAnimAction* Action, FFinished OnFinished)
{
	const bool bStarted = Action && Roots.ContainsByPredicate([Action](const FRoot& Root) { return Root.Action == Action; });
	if (!ensureMsgf(Action && Action->IsPending() && !Action->ParentChain && !bStarted, TEXT("动画动作只能启动一次；链里的步骤由链启动")))
	{
		return false;
	}
	Roots.Add({Action, MoveTemp(OnFinished)});
	if (!TryOccupy(*Action))
	{
		const FGinkgoAnimActionResult Refused = FGinkgoAnimActionResult::Aborted(FAnimActionTags::AnimAction_Abort_Occupied);
		Action->Phase = EGinkgoAnimActionPhase::Finished;
		Action->Result = Refused;
		Action->ClearCallbacks();
		AddDebugLog(*Action, TEXT("="), Refused.ToString());
		UE_LOG(LogGinkgoAnimAction, Log, TEXT("%s：%s"), *Action->GetActionName().ToString(), *Refused.ToString());
		Report(*Action, Refused);
		return false;
	}
	FCommand& Command = Commands.AddDefaulted_GetRef();
	Command.Type = FCommand::EType::Start;
	Command.Action = Action;
	return true;
}

void FGinkgoAnimActionRunner::Cancel(UGinkgoAnimAction* Action, const FGameplayTag& Reason)
{
	if (!Action)
	{
		return;
	}
	UGinkgoAnimAction& Root = GetRoot(*Action);
	if (Root.IsPending())
	{
		// 还没开始：不会放蒙太奇，那条开始的命令到时跳过
		Root.Phase = EGinkgoAnimActionPhase::Finished;
		Root.Result = FGinkgoAnimActionResult::Cancelled(Reason);
		Root.ClearCallbacks();
		ReleaseOccupancy(Root);
		Report(Root, FGinkgoAnimActionResult::Cancelled(Reason));
		return;
	}
	CancelTree(Root, FGinkgoAnimActionResult::Cancelled(Reason), EMontageEnd::Stop);
}

void FGinkgoAnimActionRunner::RequestStop(UGinkgoAnimAction* Action, const FGameplayTag& Reason)
{
	if (!Action)
	{
		return;
	}
	FCommand& Command = Commands.AddDefaulted_GetRef();
	Command.Type = FCommand::EType::RequestStop;
	Command.Action = &GetRoot(*Action);
	Command.Reason = Reason;
}

void FGinkgoAnimActionRunner::Tick(const float DeltaSeconds)
{
	Apply();

	// 先取一份：回调里可能结束别的动作
	TArray<UGinkgoAnimAction*, TInlineAllocator<4>> Leaves;
	for (const FRoot& Root : Roots)
	{
		UGinkgoAnimAction* Leaf = GetLeaf(Root.Action);
		if (Leaf && Leaf->IsRunning())
		{
			Leaves.Add(Leaf);
		}
	}

	for (UGinkgoAnimAction* Leaf : Leaves)
	{
		if (!Leaf->IsRunning())
		{
			continue;
		}

		// 看门：实例不在了，却没收到结束事件
		if (Leaf->InstanceID != INDEX_NONE && !Host.IsInstanceAlive(Leaf->InstanceID))
		{
			FinishLeaf(*Leaf, FGinkgoAnimActionResult::Aborted(FAnimActionTags::AnimAction_Abort_InstanceLost), EMontageEnd::Keep);
			continue;
		}

		for (int32 Index = 0; Index < Leaf->Guards.Num() && Leaf->IsRunning(); ++Index)
		{
			const FGinkgoAnimActionGuard& Guard = Leaf->Guards[Index];
			if (!Guard.Window.IsNone() && !Leaf->OpenWindows.Contains(Guard.Window))
			{
				continue;
			}
			// 先复制一份：条件成立时动作当场结束，回调都会被清掉
			const TFunction<bool(const FGinkgoAnimActionContext&)> Condition = Guard.Condition;
			const FGameplayTag Reason = Guard.Reason;
			const FGinkgoAnimActionContext GuardContext(*this, *Leaf);
			if (Condition && Condition(GuardContext))
			{
				FinishLeaf(*Leaf, FGinkgoAnimActionResult::Aborted(Reason), EMontageEnd::Stop);
			}
		}

		if (Leaf->IsRunning())
		{
			FGinkgoAnimActionContext Context(*this, *Leaf);
			Leaf->OnTick(Context, DeltaSeconds);
		}

		// 按时长的动作（HoldFor，没有蒙太奇）：过了时长就完成
		if (Leaf->IsRunning() && !Leaf->bPlayRequested && Leaf->HoldDuration > 0.f)
		{
			Leaf->HoldElapsed += DeltaSeconds;
			if (Leaf->HoldElapsed >= Leaf->HoldDuration)
			{
				FinishLeaf(*Leaf, FGinkgoAnimActionResult::Completed(), EMontageEnd::Keep);
			}
		}
	}

	// 中止条件成立的，同一帧就停
	Apply();

	// 结局之后等实例结束才放的手：实例不在了也放（没收到结束事件时的看门）
	for (TArray<FHandHold>& Stack : Hands)
	{
		Stack.RemoveAll([this](const FHandHold& Hold)
		{
			return !Hold.Owner.IsValid() || (Hold.ReleaseWithInstance != INDEX_NONE && !Host.IsInstanceAlive(Hold.ReleaseWithInstance));
		});
	}
}

// ===== 占用 =====

bool FGinkgoAnimActionRunner::CanOccupy(const FGameplayTag& Tag, const int32 Priority) const
{
	return !Occupants.ContainsByPredicate([Request = FGinkgoOccupancy{Tag, Priority}](const FOccupant& Occupant)
	{
		return Ginkgo::Occupancy::Blocks(Occupant.Claim, Request);
	});
}

int32 FGinkgoAnimActionRunner::Occupy(const FGameplayTag& Tag, const int32 Priority)
{
	const FGinkgoOccupancy Claim{Tag, Priority};
	const int32 Handle = NextOccupancyHandle++;
	Occupants.Add({Claim, nullptr, Handle});
	TakeOver({Claim}, nullptr);
	return Handle;
}

void FGinkgoAnimActionRunner::ReleaseOccupancy(const int32 Handle)
{
	Occupants.RemoveAll([Handle](const FOccupant& Occupant) { return Occupant.Handle == Handle; });
}

void FGinkgoAnimActionRunner::ReleaseOccupancy(const UGinkgoAnimAction& Action)
{
	Occupants.RemoveAll([&Action](const FOccupant& Occupant) { return Occupant.Action.Get() == &Action; });
}

bool FGinkgoAnimActionRunner::TryOccupy(UGinkgoAnimAction& Action)
{
	const UGinkgoAnimAction& Root = GetRoot(Action);
	for (const FGinkgoOccupancy& Claim : Action.Occupancy)
	{
		for (const FOccupant& Occupant : Occupants)
		{
			UGinkgoAnimAction* Holder = Occupant.Action.Get();
			if ((!Holder || &GetRoot(*Holder) != &Root) && Ginkgo::Occupancy::Blocks(Occupant.Claim, Claim))
			{
				return false;
			}
		}
	}
	for (const FGinkgoOccupancy& Claim : Action.Occupancy)
	{
		Occupants.Add({Claim, &Action, INDEX_NONE});
	}
	TakeOver(Action.Occupancy, &Root);
	return true;
}

void FGinkgoAnimActionRunner::TakeOver(const TConstArrayView<FGinkgoOccupancy> Claims, const UGinkgoAnimAction* Except)
{
	// 先取一份：结局的回调里可能启动别的动作
	TArray<UGinkgoAnimAction*, TInlineAllocator<4>> Beaten;
	for (const FOccupant& Occupant : Occupants)
	{
		UGinkgoAnimAction* Holder = Occupant.Action.Get();
		if (!Holder || &GetRoot(*Holder) == Except)
		{
			continue;
		}
		for (const FGinkgoOccupancy& Claim : Claims)
		{
			if (Ginkgo::Occupancy::Overlaps(Occupant.Claim, Claim) && !Ginkgo::Occupancy::Blocks(Occupant.Claim, Claim))
			{
				Beaten.AddUnique(&GetRoot(*Holder));
				break;
			}
		}
	}
	for (UGinkgoAnimAction* Root : Beaten)
	{
		Cancel(Root, FAnimActionTags::AnimAction_Cancel_Preempted);
	}
}

// ===== 引擎的事件 =====

void FGinkgoAnimActionRunner::HandleNotify(const int32 InstanceID, const FName Name)
{
	UGinkgoAnimAction* Leaf = FindRunningLeaf(InstanceID);
	if (!Leaf)
	{
		return;
	}
	AddDebugLog(*Leaf, TEXT(":"), Name.ToString());
	FireEvent(*Leaf, Name);
	if (Leaf->IsRunning() && !Leaf->CompletionEvent.IsNone() && Leaf->CompletionEvent == Name)
	{
		FinishLeaf(*Leaf, FGinkgoAnimActionResult::Completed(), EMontageEnd::Keep);
	}
}

void FGinkgoAnimActionRunner::HandleWindowBegin(const int32 InstanceID, const FName Name)
{
	if (UGinkgoAnimAction* Leaf = FindRunningLeaf(InstanceID))
	{
		Leaf->OpenWindows.Add(Name);
	}
}

void FGinkgoAnimActionRunner::HandleWindowEnd(const int32 InstanceID, const FName Name)
{
	if (UGinkgoAnimAction* Leaf = FindRunningLeaf(InstanceID))
	{
		Leaf->OpenWindows.Remove(Name);
	}
}

void FGinkgoAnimActionRunner::HandleBlendingOut(const int32 InstanceID, const bool bInterrupted)
{
	if (UGinkgoAnimAction* Leaf = FindRunningLeaf(InstanceID))
	{
		FinishLeaf(*Leaf, bInterrupted
			? FGinkgoAnimActionResult::Aborted(FAnimActionTags::AnimAction_Abort_Interrupted)
			: FGinkgoAnimActionResult::Completed(), EMontageEnd::Keep);
	}
}

void FGinkgoAnimActionRunner::HandleEnded(const int32 InstanceID, const bool bInterrupted)
{
	// 没收到淡出就结束了（淡出时长为 0）：同样的结局
	HandleBlendingOut(InstanceID, bInterrupted);

	for (TArray<FHandHold>& Stack : Hands)
	{
		Stack.RemoveAll([InstanceID](const FHandHold& Hold) { return Hold.ReleaseWithInstance == InstanceID; });
	}
}

// ===== 读 =====

const FGinkgoAnimActionRunner::FHandHold* FGinkgoAnimActionRunner::GetHandIK(const EGinkgoHand Hand) const
{
	const TArray<FHandHold>& Stack = Hands[static_cast<int32>(Hand)];
	return Stack.IsEmpty() ? nullptr : &Stack.Last();
}

FGameplayTagContainer FGinkgoAnimActionRunner::GetCameraTags() const
{
	FGameplayTagContainer Tags;
	for (const FRoot& Root : Roots)
	{
		const UGinkgoAnimAction* Leaf = GetLeaf(Root.Action);
		if (Leaf && Leaf->IsRunning())
		{
			Tags.AppendTags(Leaf->CameraTags);
		}
	}
	return Tags;
}

TArray<UGinkgoAnimAction*> FGinkgoAnimActionRunner::GetActions() const
{
	using namespace UE::Query;
	return Roots | Select([](const FRoot& Root) { return Root.Action.Get(); }) | ToArray();
}

TArray<UGinkgoAnimAction*> FGinkgoAnimActionRunner::GetRunningLeaves() const
{
	using namespace UE::Query;
	return Roots
		| Select([](const FRoot& Root) { return GetLeaf(Root.Action); })
		| Where([](const UGinkgoAnimAction* Leaf) { return Leaf && Leaf->IsRunning(); })
		| ToArray();
}

#if !UE_BUILD_SHIPPING
TArray<FString> FGinkgoAnimActionRunner::TakeDebugLog()
{
	TArray<FString> Taken = MoveTemp(DebugLog);
	DebugLog.Reset();
	return Taken;
}

FString FGinkgoAnimActionRunner::GetOccupancyText() const
{
	using namespace UE::Query;
	const TArray<FString> Parts = Occupants | Select([](const FOccupant& Occupant)
	{
		const FString Part = FString::Printf(TEXT("%s=%d"), *Occupant.Claim.Tag.ToString().Replace(TEXT("Occupancy."), TEXT("")), Occupant.Claim.Priority);
		const UGinkgoAnimAction* Holder = Occupant.Action.Get();
		return Holder ? Part + TEXT(":") + Holder->GetActionName().ToString() : Part;
	}) | ToArray();
	return FString::Join(Parts, TEXT("|"));
}
#endif

void FGinkgoAnimActionRunner::AddDebugLog(const UGinkgoAnimAction& Action, const TCHAR* Separator, const FString& What)
{
#if !UE_BUILD_SHIPPING
	// 没人取时不无限长：调试显示或回放每帧取走，这里只留最近的
	constexpr int32 MaxDebugLog = 64;
	if (DebugLog.Num() >= MaxDebugLog)
	{
		DebugLog.RemoveAt(0);
	}
	DebugLog.Add(FString::Printf(TEXT("%s%s%s"), *Action.GetActionName().ToString(), Separator, *What));
#endif
}

UGinkgoAnimAction* FGinkgoAnimActionRunner::GetActionInGroup(const FName Group) const
{
	for (const FRoot& Root : Roots)
	{
		UGinkgoAnimAction* Leaf = GetLeaf(Root.Action);
		if (Leaf && Leaf->IsRunning() && Leaf->InstanceID != INDEX_NONE && Leaf->Group == Group)
		{
			return Leaf;
		}
	}
	return nullptr;
}

void FGinkgoAnimActionRunner::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FRoot& Root : Roots)
	{
		Collector.AddReferencedObject(Root.Action);
	}
	for (FCommand& Command : Commands)
	{
		Collector.AddReferencedObject(Command.Action);
	}
}

// ===== 应用阶段 =====

void FGinkgoAnimActionRunner::Apply()
{
	if (bApplying)
	{
		return;
	}
	TGuardValue<bool> ApplyingGuard(bApplying, true);
	PlayedGroups.Reset();

	TArray<FCommand> Stops;
	for (int32 Round = 1; Commands.Num() > 0; ++Round)
	{
		if (!ensureMsgf(Round <= Ginkgo::AnimActionRunner::MaxApplyRounds, TEXT("动画动作：应用了 %d 轮还没处理完，丢掉剩下的 %d 条"),
			Ginkgo::AnimActionRunner::MaxApplyRounds, Commands.Num()))
		{
			Commands.Reset();
			break;
		}

		// 这一轮里新记下的（链的下一步、新的结局）留到下一轮
		TArray<FCommand> Batch = MoveTemp(Commands);
		Commands.Reset();
		for (FCommand& Command : Batch)
		{
			UGinkgoAnimAction* Action = Command.Action;
			switch (Command.Type)
			{
			case FCommand::EType::Start:
				// 开始之前就被取消了的跳过
				if (Action && Action->IsPending())
				{
					Enter(*Action);
				}
				break;
			case FCommand::EType::RequestStop:
				if (Action)
				{
					HandleStopRequest(*Action, Command.Reason);
				}
				break;
			case FCommand::EType::Advance:
				if (UGinkgoAnimActionChain* Chain = Cast<UGinkgoAnimActionChain>(Action))
				{
					Advance(*Chain, Command.Result);
				}
				break;
			case FCommand::EType::StopMontage:
				Stops.Add(MoveTemp(Command));
				break;
			case FCommand::EType::JumpToSection:
				if (FindRunningLeaf(Command.InstanceID))
				{
					Host.JumpToSection(Command.InstanceID, Command.Section);
				}
				break;
			case FCommand::EType::SetNextSection:
				if (FindRunningLeaf(Command.InstanceID))
				{
					Host.SetNextSection(Command.InstanceID, Command.Section, Command.NextSection);
				}
				break;
			}
		}
	}

	// 要停的蒙太奇：这次应用里同一组放了新的，已经被它顶掉（只有一次淡入淡出），不另停
	for (const FCommand& Stop : Stops)
	{
		if (!PlayedGroups.Contains(Stop.Group))
		{
			Host.StopMontage(Stop.InstanceID, -1.f);
		}
	}
}

void FGinkgoAnimActionRunner::Enter(UGinkgoAnimAction& Action)
{
	Action.Phase = EGinkgoAnimActionPhase::Running;

	if (UGinkgoAnimActionChain* Chain = Cast<UGinkgoAnimActionChain>(&Action))
	{
		FGinkgoAnimActionChainBuilder Builder(*Chain);
		Chain->OnBuild(Builder);
		if (!Action.IsRunning())
		{
			return;
		}
		if (!Chain->Validate())
		{
			Chain->FirstUnfinished = FGinkgoAnimActionResult::Aborted(FAnimActionTags::AnimAction_Abort_PlayFailed);
		}
		if (Chain->Steps.IsEmpty() || Chain->FirstUnfinished.IsFinished())
		{
			FinishChain(*Chain);
			return;
		}
		EnterStep(*Chain, 0);
		return;
	}

	{
		FGinkgoAnimActionSetup Setup(*this, Action);
		Action.OnStart(Setup);
	}
	// 声明时就结束了（回调里中止）
	if (!Action.IsRunning())
	{
		return;
	}
	// 没有蒙太奇的动作：开始后马上完成；按时长的（HoldFor）跑到时长，在 Advance 里完成
	if (!Action.bPlayRequested)
	{
		if (Action.HoldDuration <= 0.f)
		{
			FinishLeaf(Action, FGinkgoAnimActionResult::Completed(), EMontageEnd::Keep);
		}
		return;
	}

	int32 InstanceID = INDEX_NONE;
	if (Action.Montage)
	{
		Action.Group = Host.GetGroup(Action.Montage);
		// 一个槽组一个动作：先把组里原来的动作标成取消（被替换），它的蒙太奇由下面放的新蒙太奇顶掉
		if (UGinkgoAnimAction* Previous = GetActionInGroup(Action.Group))
		{
			ensureMsgf(Action.Occupancy.IsEmpty() || Previous->Occupancy.IsEmpty() || Action.Occupancy.ContainsByPredicate([Previous](const FGinkgoOccupancy& Claim)
			{
				return Previous->Occupancy.ContainsByPredicate([&Claim](const FGinkgoOccupancy& Other) { return Ginkgo::Occupancy::Overlaps(Claim, Other); });
			}), TEXT("%s 与 %s 占的部位不重叠，本来可以同时放，蒙太奇却在同一个槽组 %s：组配错了"), *Action.GetActionName().ToString(), *Previous->GetActionName().ToString(),
				*Action.Group.ToString());
			CancelTree(GetRoot(*Previous), FGinkgoAnimActionResult::Cancelled(FAnimActionTags::AnimAction_Cancel_Replaced), EMontageEnd::Keep);
		}
		InstanceID = Host.PlayMontage(Action.Montage, Action.PlayRate, Action.StartSection, Action.StartPosition, Action.BlendInTime);
	}
	if (InstanceID == INDEX_NONE)
	{
		UE_LOG(LogGinkgoAnimAction, Warning, TEXT("%s：蒙太奇 %s 放不起来"), *Action.GetActionName().ToString(), *GetNameSafe(Action.Montage));
		FinishLeaf(Action, FGinkgoAnimActionResult::Aborted(FAnimActionTags::AnimAction_Abort_PlayFailed), EMontageEnd::Keep);
		return;
	}
	PlayedGroups.Add(Action.Group);
	Action.InstanceID = InstanceID;
}

void FGinkgoAnimActionRunner::EnterStep(UGinkgoAnimActionChain& Chain, int32 Index)
{
	for (int32 InstantSteps = 0; ; ++InstantSteps)
	{
		if (!ensureMsgf(InstantSteps < Ginkgo::AnimActionRunner::MaxInstantSteps, TEXT("链 %s：连着走了 %d 个瞬时的步，转换写成了环"),
			*Chain.GetActionName().ToString(), InstantSteps))
		{
			Chain.FirstUnfinished = FGinkgoAnimActionResult::Aborted(FAnimActionTags::AnimAction_Abort_PlayFailed);
			FinishChain(Chain);
			return;
		}
		Chain.CurrentIndex = Index;
		Chain.CurrentStep = nullptr;
		if (!Chain.Steps[Index].Instant)
		{
			break;
		}

		// 瞬时的步：做完就算完成。先复制一份：回调里可能取消这条链，步骤会被清掉
		const TFunction<void()> Function = Chain.Steps[Index].Instant;
		Function();
		if (!Chain.IsRunning())
		{
			return;
		}
		Index = Chain.FindNext(Index, FGinkgoAnimActionResult::Completed());
		if (Index == INDEX_NONE)
		{
			FinishChain(Chain);
			return;
		}
	}

	const FGinkgoAnimActionFactory Factory = Chain.Steps[Index].Factory;
	const FName StepName = Chain.Steps[Index].Name;
	UGinkgoAnimAction* StepAction = Factory ? Factory() : nullptr;
	if (!Chain.IsRunning())
	{
		return;
	}
	if (!ensureMsgf(StepAction && StepAction->IsPending() && !StepAction->ParentChain, TEXT("链 %s：步骤 %s 没造出新的动作"),
		*Chain.GetActionName().ToString(), *StepName.ToString()))
	{
		// 当成这一步放不起来，照转换走
		FCommand& Command = Commands.AddDefaulted_GetRef();
		Command.Type = FCommand::EType::Advance;
		Command.Action = &Chain;
		Command.Result = FGinkgoAnimActionResult::Aborted(FAnimActionTags::AnimAction_Abort_PlayFailed);
		return;
	}

	StepAction->ParentChain = &Chain;
	if (StepAction->ActionName.IsNone())
	{
		StepAction->ActionName = StepName;
	}
	Chain.CurrentStep = StepAction;
	if (!TryOccupy(*StepAction))
	{
		// 这一步占用不让：当成这一步中止，照转换走
		StepAction->Phase = EGinkgoAnimActionPhase::Running;
		FinishLeaf(*StepAction, FGinkgoAnimActionResult::Aborted(FAnimActionTags::AnimAction_Abort_Occupied), EMontageEnd::Keep);
		return;
	}
	Enter(*StepAction);
}

void FGinkgoAnimActionRunner::Advance(UGinkgoAnimActionChain& Chain, const FGinkgoAnimActionResult& StepResult)
{
	// 链已经结束（取消、被替换）：那一步要求的推进不算
	if (!Chain.IsRunning())
	{
		return;
	}
	if (StepResult.Outcome != EGinkgoAnimActionOutcome::Completed && !Chain.FirstUnfinished.IsFinished())
	{
		Chain.FirstUnfinished = StepResult;
	}

	// 取消总是结束；提过软停的，放完这一步就结束
	const int32 Next = StepResult.Outcome == EGinkgoAnimActionOutcome::Cancelled || IsStopRequested(Chain)
		? INDEX_NONE
		: Chain.FindNext(Chain.CurrentIndex, StepResult);
	if (Next == INDEX_NONE)
	{
		FinishChain(Chain);
		return;
	}
	EnterStep(Chain, Next);
}

void FGinkgoAnimActionRunner::HandleStopRequest(UGinkgoAnimAction& Root, const FGameplayTag& Reason)
{
	if (!Root.IsRunning() || Root.bStopRequested)
	{
		return;
	}
	Root.bStopRequested = true;
	Root.StopReason = Reason;

	// 问正在放的那一步怎么收
	bool bHandled = false;
	UGinkgoAnimAction* Leaf = GetLeaf(&Root);
	if (Leaf && Leaf->IsRunning() && !Leaf->IsA<UGinkgoAnimActionChain>())
	{
		FGinkgoAnimActionContext Context(*this, *Leaf);
		bHandled = Leaf->OnStopRequested(Context, Reason);
	}
	// 不自己收（默认）：就是取消
	if (!bHandled && Root.IsRunning())
	{
		CancelTree(Root, FGinkgoAnimActionResult::Cancelled(Reason), EMontageEnd::Stop);
	}
}

// ===== 结局 =====

void FGinkgoAnimActionRunner::FinishLeaf(UGinkgoAnimAction& Leaf, FGinkgoAnimActionResult InResult, const EMontageEnd MontageEnd)
{
	// 只发一次：结局可能同时从几处来（中止条件成立的同一帧蒙太奇被顶掉），第一个到的算数
	if (!Leaf.IsRunning())
	{
		return;
	}
	// 软停之后自己收完的：完成带上软停的原因
	FGameplayTag StopReason;
	if (InResult.Outcome == EGinkgoAnimActionOutcome::Completed && !InResult.Reason.IsValid() && IsStopRequested(Leaf, &StopReason))
	{
		InResult.Reason = StopReason;
	}

	// 1. 标记：之后这个实例的事件都忽略
	Leaf.Phase = EGinkgoAnimActionPhase::Finished;
	Leaf.Result = InResult;

	// 2. 补做
	CatchUp(Leaf);

	// 3. 释放
	ReleaseHolds(Leaf);

	// 4. 要停的蒙太奇留给应用阶段
	if (MontageEnd == EMontageEnd::Stop && Leaf.InstanceID != INDEX_NONE)
	{
		FCommand& Command = Commands.AddDefaulted_GetRef();
		Command.Type = FCommand::EType::StopMontage;
		Command.InstanceID = Leaf.InstanceID;
		Command.Group = Leaf.Group;
	}

	Leaf.OnFinished(InResult);
	Leaf.ClearCallbacks();
	AddDebugLog(Leaf, TEXT("="), InResult.ToString());
	UE_LOG(LogGinkgoAnimAction, Log, TEXT("%s：%s，蒙太奇 %s，%.3f s"), *Leaf.GetActionName().ToString(), *InResult.ToString(),
		*GetNameSafe(Leaf.Montage), Leaf.InstanceID != INDEX_NONE ? Host.GetPosition(Leaf.InstanceID) : 0.f);

	// 5. 报告
	Report(Leaf, InResult);
}

void FGinkgoAnimActionRunner::FinishChain(UGinkgoAnimActionChain& Chain)
{
	if (!Chain.IsRunning())
	{
		return;
	}
	FGinkgoAnimActionResult ChainResult = Chain.FirstUnfinished.IsFinished() ? Chain.FirstUnfinished : FGinkgoAnimActionResult::Completed();
	FGameplayTag StopReason;
	if (ChainResult.Outcome == EGinkgoAnimActionOutcome::Completed && !ChainResult.Reason.IsValid() && IsStopRequested(Chain, &StopReason))
	{
		ChainResult.Reason = StopReason;
	}

	// 动作基类的私有成员经基类访问
	UGinkgoAnimAction& ChainAction = Chain;
	ChainAction.Phase = EGinkgoAnimActionPhase::Finished;
	ChainAction.Result = ChainResult;
	ReleaseOccupancy(ChainAction);
	ChainAction.OnFinished(ChainResult);
	ChainAction.ClearCallbacks();
	Chain.BuildFunction = nullptr;
	Chain.Steps.Reset();
	AddDebugLog(Chain, TEXT("="), ChainResult.ToString());
	UE_LOG(LogGinkgoAnimAction, Log, TEXT("%s：%s"), *Chain.GetActionName().ToString(), *ChainResult.ToString());

	Report(Chain, ChainResult);
}

void FGinkgoAnimActionRunner::CancelTree(UGinkgoAnimAction& Action, const FGinkgoAnimActionResult& InResult, const EMontageEnd MontageEnd)
{
	if (!Action.IsRunning())
	{
		return;
	}
	if (UGinkgoAnimActionChain* Chain = Cast<UGinkgoAnimActionChain>(&Action))
	{
		// 先取消正在放的那一步：它的补做、释放照常；它要求的推进到时链已经结束，会被忽略
		if (UGinkgoAnimAction* Step = Chain->CurrentStep; Step && Step->IsRunning())
		{
			CancelTree(*Step, InResult, MontageEnd);
		}
		// 取消盖过路径上之前没完成的步：拥有者要知道是它叫停的
		Chain->FirstUnfinished = InResult;
		FinishChain(*Chain);
		return;
	}
	FinishLeaf(Action, InResult, MontageEnd);
}

void FGinkgoAnimActionRunner::Report(UGinkgoAnimAction& Action, const FGinkgoAnimActionResult& InResult)
{
	if (UGinkgoAnimActionChain* Parent = Action.ParentChain)
	{
		FCommand& Command = Commands.AddDefaulted_GetRef();
		Command.Type = FCommand::EType::Advance;
		Command.Action = Parent;
		Command.Result = InResult;
		return;
	}

	const int32 Index = Roots.IndexOfByPredicate([&Action](const FRoot& Root) { return Root.Action == &Action; });
	if (Index == INDEX_NONE)
	{
		return;
	}
	// 先从列表里拿掉再回调：回调里可以马上启动下一个动作
	const FFinished OnFinished = MoveTemp(Roots[Index].OnFinished);
	Roots.RemoveAt(Index);
	if (OnFinished)
	{
		OnFinished(Action, InResult);
	}
}

// ===== 事件、补做与释放 =====

void FGinkgoAnimActionRunner::FireEvent(UGinkgoAnimAction& Leaf, const FName Name)
{
	FGinkgoAnimActionContext Context(*this, Leaf);
	for (int32 Index = 0; Index < Leaf.Events.Num() && Leaf.IsRunning(); ++Index)
	{
		FGinkgoAnimActionEventBinding& Binding = Leaf.Events[Index];
		if (Binding.Name != Name)
		{
			continue;
		}
		++Binding.FiredCount;
		// 先复制一份：回调里中止时动作当场结束，回调都会被清掉
		const TFunction<void(FGinkgoAnimActionEvent&)> Handler = Binding.Handler;
		FGinkgoAnimActionEvent Event{Name, false, FGinkgoAnimActionResult(), Context};
		if (Handler)
		{
			Handler(Event);
		}
	}
}

void FGinkgoAnimActionRunner::CatchUp(UGinkgoAnimAction& Leaf)
{
	struct FMissed
	{
		int32 Index = INDEX_NONE;
		float Time = 0.f;
	};
	TArray<FMissed, TInlineAllocator<8>> Missed;
	for (int32 Index = 0; Index < Leaf.Events.Num(); ++Index)
	{
		const FGinkgoAnimActionEventBinding& Binding = Leaf.Events[Index];
		if (Binding.Policy == EGinkgoMissedEvent::CatchUp && Binding.FiredCount == 0)
		{
			// 蒙太奇里没有这个事件的排在最后，按声明的先后
			const float Time = Leaf.Montage ? Host.GetEventTime(Leaf.Montage, Binding.Name) : -1.f;
			Missed.Add({Index, Time < 0.f ? TNumericLimits<float>::Max() : Time});
		}
	}
	Algo::StableSortBy(Missed, &FMissed::Time);

	FGinkgoAnimActionContext Context(*this, Leaf);
	for (const FMissed& Entry : Missed)
	{
		FGinkgoAnimActionEventBinding& Binding = Leaf.Events[Entry.Index];
		++Binding.FiredCount;
		AddDebugLog(Leaf, TEXT(":"), Binding.Name.ToString() + TEXT("(catch-up)"));
		const TFunction<void(FGinkgoAnimActionEvent&)> Handler = Binding.Handler;
		FGinkgoAnimActionEvent Event{Binding.Name, true, Leaf.Result, Context};
		if (Handler)
		{
			Handler(Event);
		}
	}
}

void FGinkgoAnimActionRunner::ReleaseHolds(UGinkgoAnimAction& Leaf)
{
	Leaf.CameraTags.Reset();
	ReleaseOccupancy(Leaf);

	const bool bInstanceAlive = Leaf.InstanceID != INDEX_NONE && Host.IsInstanceAlive(Leaf.InstanceID);
	for (TArray<FHandHold>& Stack : Hands)
	{
		for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
		{
			FHandHold& Hold = Stack[Index];
			if (Hold.Owner.Get() != &Leaf || Hold.ReleaseWithInstance != INDEX_NONE)
			{
				continue;
			}
			if (bInstanceAlive)
			{
				// 冻结，等实例结束再放：曲线随蒙太奇淡出，权重降到 0 之前目标不能没有，否则手会跳
				Hold.bFrozen = true;
				Hold.ReleaseWithInstance = Leaf.InstanceID;
			}
			else
			{
				Stack.RemoveAt(Index);
			}
		}
	}
}

void FGinkgoAnimActionRunner::QueueJumpToSection(UGinkgoAnimAction& Action, const FName Section)
{
	if (!Action.IsRunning() || Action.InstanceID == INDEX_NONE)
	{
		return;
	}
	FCommand& Command = Commands.AddDefaulted_GetRef();
	Command.Type = FCommand::EType::JumpToSection;
	Command.InstanceID = Action.InstanceID;
	Command.Section = Section;
}

void FGinkgoAnimActionRunner::QueueSetNextSection(UGinkgoAnimAction& Action, const FName From, const FName To)
{
	if (!Action.IsRunning() || Action.InstanceID == INDEX_NONE)
	{
		return;
	}
	FCommand& Command = Commands.AddDefaulted_GetRef();
	Command.Type = FCommand::EType::SetNextSection;
	Command.InstanceID = Action.InstanceID;
	Command.Section = From;
	Command.NextSection = To;
}

// ===== 手的栈 =====

void FGinkgoAnimActionRunner::SetHandIK(UGinkgoAnimAction& Action, const EGinkgoHand Hand, const FGinkgoIKBinding& Binding)
{
	if (!Action.IsRunning())
	{
		return;
	}
	TArray<FHandHold>& Stack = Hands[static_cast<int32>(Hand)];
	// 自己已经持有这只手时就地换掉，不改在栈里的位置
	FHandHold* Held = Stack.FindByPredicate([&Action](const FHandHold& Hold)
	{
		return Hold.Owner.Get() == &Action && Hold.ReleaseWithInstance == INDEX_NONE;
	});
	if (Held)
	{
		Held->Binding = Binding;
		Held->bFrozen = false;
		return;
	}
	FHandHold& NewHold = Stack.AddDefaulted_GetRef();
	NewHold.Owner = &Action;
	NewHold.Binding = Binding;
}

void FGinkgoAnimActionRunner::FreezeHandIK(UGinkgoAnimAction& Action, const EGinkgoHand Hand)
{
	for (FHandHold& Hold : Hands[static_cast<int32>(Hand)])
	{
		if (Hold.Owner.Get() == &Action && Hold.ReleaseWithInstance == INDEX_NONE)
		{
			Hold.bFrozen = true;
		}
	}
}

void FGinkgoAnimActionRunner::ReleaseHandIK(UGinkgoAnimAction& Action, const EGinkgoHand Hand)
{
	Hands[static_cast<int32>(Hand)].RemoveAll([&Action](const FHandHold& Hold)
	{
		return Hold.Owner.Get() == &Action && Hold.ReleaseWithInstance == INDEX_NONE;
	});
}

// ===== 查找 =====

UGinkgoAnimAction* FGinkgoAnimActionRunner::FindRunningLeaf(const int32 InstanceID) const
{
	if (InstanceID == INDEX_NONE)
	{
		return nullptr;
	}
	for (const FRoot& Root : Roots)
	{
		UGinkgoAnimAction* Leaf = GetLeaf(Root.Action);
		if (Leaf && Leaf->IsRunning() && Leaf->InstanceID == InstanceID)
		{
			return Leaf;
		}
	}
	return nullptr;
}

UGinkgoAnimAction* FGinkgoAnimActionRunner::GetLeaf(UGinkgoAnimAction* Action)
{
	while (const UGinkgoAnimActionChain* Chain = Cast<UGinkgoAnimActionChain>(Action))
	{
		if (!Chain->CurrentStep)
		{
			break;
		}
		Action = Chain->CurrentStep;
	}
	return Action;
}

UGinkgoAnimAction& FGinkgoAnimActionRunner::GetRoot(UGinkgoAnimAction& Action)
{
	UGinkgoAnimAction* Root = &Action;
	while (Root->ParentChain)
	{
		Root = Root->ParentChain;
	}
	return *Root;
}

bool FGinkgoAnimActionRunner::IsStopRequested(const UGinkgoAnimAction& Action, FGameplayTag* OutReason)
{
	for (const UGinkgoAnimAction* Current = &Action; Current; Current = Current->ParentChain)
	{
		if (Current->bStopRequested)
		{
			if (OutReason)
			{
				*OutReason = Current->StopReason;
			}
			return true;
		}
	}
	return false;
}

#include "GinkgoMovementWindowManager.h"

#include "Utilities/Debug/DebugCategory.h"

namespace
{
	/** Δt 累加有舍入：18 个 1/60 s 加起来可能比 0.3 s 小一点点，没有这个容差时长边界会晚一次移动 */
	constexpr float PhaseTimeTolerance = 1e-5f;

	/** 一次推进最多跨过几个阶段：防止配错的零长阶段互相切换卡住 */
	constexpr int32 MaxPhaseStepsPerAdvance = 16;

	/** 状态对应的定义；定义已经不在列表里、或阶段越界时为空，调用方把这个窗口当作已结束 */
	const UGinkgoMovementWindow* FindDefinition(const FGinkgoMovementWindowManager::FDefinitions Definitions, const FGinkgoMovementWindowState& Window)
	{
		const UGinkgoMovementWindow* Definition = Definitions.IsValidIndex(Window.Definition) ? Definitions[Window.Definition].Get() : nullptr;
		return Definition && Definition->Phases.IsValidIndex(Window.Phase) ? Definition : nullptr;
	}

	bool MatchesFlag(const EGinkgoWindowEventFlagFilter Filter, const bool bFlag)
	{
		switch (Filter)
		{
		case EGinkgoWindowEventFlagFilter::Set:
			return bFlag;
		case EGinkgoWindowEventFlagFilter::Clear:
			return !bFlag;
		default:
			return true;
		}
	}
}

void FGinkgoMovementWindowManager::Advance(const FDefinitions Definitions, FWindows& Windows, const float DeltaTime)
{
	for (int32 Index = 0; Index < Windows.Num();)
	{
		const UGinkgoMovementWindow* Definition = FindDefinition(Definitions, Windows[Index]);
		if (!Definition)
		{
			EndWindow(Windows, Index);
			continue;
		}
		FGinkgoMovementWindowState& Window = Windows[Index];
		Window.PhaseTime += DeltaTime;
		bool bEnded = false;
		for (int32 Step = 0; Step < MaxPhaseStepsPerAdvance; ++Step)
		{
			const FGinkgoWindowPhase& Phase = Definition->Phases[Window.Phase];
			if (Phase.Duration <= 0.f || Window.PhaseTime + PhaseTimeTolerance < Phase.Duration)
			{
				break;
			}
			if (!Definition->Phases.IsValidIndex(Phase.NextPhase))
			{
				EndWindow(Windows, Index);
				bEnded = true;
				break;
			}
			// 多出来的时间带进下一个阶段：整个窗口的时长不随帧率变
			const float Overflow = FMath::Max(Window.PhaseTime - Phase.Duration, 0.f);
			RecordChange(Window.Definition, Window.Phase, Phase.NextPhase);
			Window.Phase = static_cast<uint8>(Phase.NextPhase);
			Window.PhaseTime = Overflow;
		}
		if (!bEnded)
		{
			++Index;
		}
	}
	RebuildTags(Definitions, Windows);
}

bool FGinkgoMovementWindowManager::HandleEvent(const FDefinitions Definitions, FWindows& Windows, const FGinkgoWindowEvent& Event, FGinkgoWindowEventContext& Context)
{
	// ===== 1. 行为：按优先级处理，可以消费事件、用掉自己 =====
	Context.Tags = &Tags;
	TArray<int32, TInlineAllocator<FGinkgoMovementSimState::MaxWindows>> Order;
	for (int32 Index = 0; Index < Windows.Num(); ++Index)
	{
		if (FindDefinition(Definitions, Windows[Index]))
		{
			Order.Add(Index);
		}
	}
	// 稳定排序：优先级相同时先推入的先处理
	Order.StableSort([&](const int32 A, const int32 B)
	{
		return Definitions[Windows[A].Definition]->Priority > Definitions[Windows[B].Definition]->Priority;
	});
	bool bConsumed = false;
	uint32 UsedUp = 0;
	for (const int32 Index : Order)
	{
		const FGinkgoMovementWindowState& Window = Windows[Index];
		const FGinkgoWindowBehaviorResult Result = Definitions[Window.Definition]->ApplyBehavior(Event, Window.PhaseTime, Window.Param, Context);
		if (Result.bEndWindow)
		{
			UsedUp |= 1u << Index;
		}
		if (Result.bConsumeEvent)
		{
			bConsumed = true;
			break;
		}
	}
	// 从后往前删，前面的下标不变
	for (int32 Index = Windows.Num() - 1; Index >= 0; --Index)
	{
		if (UsedUp & (1u << Index))
		{
			EndWindow(Windows, Index);
		}
	}

	// ===== 2. 切换：结束事件与当前阶段的切换表 =====
	for (int32 Index = 0; Index < Windows.Num();)
	{
		const UGinkgoMovementWindow* Definition = FindDefinition(Definitions, Windows[Index]);
		if (!Definition)
		{
			EndWindow(Windows, Index);
			continue;
		}
		FGinkgoMovementWindowState& Window = Windows[Index];
		const FGinkgoWindowTransition* Transition = Definition->Phases[Window.Phase].Transitions.FindByPredicate(
			[&](const FGinkgoWindowTransition& Candidate) { return Candidate.Event == Event.Type; });
		if (!Transition && !Definition->EndEvents.Contains(Event.Type))
		{
			++Index;
			continue;
		}
		const int32 TargetPhase = Transition && !Definition->EndEvents.Contains(Event.Type) ? Transition->TargetPhase : INDEX_NONE;
		if (!Definition->Phases.IsValidIndex(TargetPhase))
		{
			EndWindow(Windows, Index);
			continue;
		}
		RecordChange(Window.Definition, Window.Phase, TargetPhase);
		Window.Phase = static_cast<uint8>(TargetPhase);
		Window.PhaseTime = 0.f;
		++Index;
	}
	RebuildTags(Definitions, Windows);

	// ===== 3. 推入，推入全部做完后清除 =====
	// 这次推入或刷新的窗口（按下标的位掩码）不清；清除放到最后，结果与变化的记录都与定义的先后无关
	uint32 Fresh = 0;
	FGameplayTagContainer Cleared;
	for (int32 DefinitionIndex = 0; DefinitionIndex < Definitions.Num() && DefinitionIndex <= MAX_uint8; ++DefinitionIndex)
	{
		const UGinkgoMovementWindow* Definition = Definitions[DefinitionIndex].Get();
		if (!Definition || Definition->Phases.IsEmpty() || !Definition->PushEvents.Contains(Event.Type) || !MatchesFlag(Definition->PushFlag, Event.bFlag)
			|| (!Definition->PushQuery.IsEmpty() && !Definition->PushQuery.Matches(Tags)))
		{
			continue;
		}
		const FGinkgoMovementWindowState NewWindow{static_cast<uint8>(DefinitionIndex), 0, 0.f, Event.Value};
		const int32 Existing = Windows.IndexOfByPredicate([&](const FGinkgoMovementWindowState& Window) { return Window.Definition == DefinitionIndex; });
		if (Existing != INDEX_NONE && Definition->Repush != EGinkgoWindowRepush::Stack)
		{
			if (Definition->Repush != EGinkgoWindowRepush::Refresh)
			{
				continue;
			}
			RecordChange(DefinitionIndex, Windows[Existing].Phase, 0);
			Windows[Existing] = NewWindow;
			Fresh |= 1u << Existing;
		}
		else if (Windows.Num() >= FGinkgoMovementSimState::MaxWindows)
		{
			// 两端同样放不下，结果仍然一致；说明容量要调大
			UE_LOG(LogGinkgoMovement, Warning, TEXT("[Window] 活动窗口已满（%d 个），%s 没有推入"), FGinkgoMovementSimState::MaxWindows,
				*Definition->Tag.ToString());
			continue;
		}
		else
		{
			Windows.Add(NewWindow);
			RecordChange(DefinitionIndex, INDEX_NONE, 0);
			Fresh |= 1u << (Windows.Num() - 1);
		}
		Cleared.AppendTags(Definition->ClearedWindows);
		// 后面定义的推入条件要看到这次推入（看不到清除，清除在最后）
		RebuildTags(Definitions, Windows);
	}
	ClearWindows(Definitions, Windows, Cleared, Fresh);
	RebuildTags(Definitions, Windows);
	return bConsumed;
}

bool FGinkgoMovementWindowManager::HasAbility(const FDefinitions Definitions, const FWindows& Windows, const FGameplayTag& Ability) const
{
	for (const FGinkgoMovementWindowState& Window : Windows)
	{
		const UGinkgoMovementWindow* Definition = FindDefinition(Definitions, Window);
		if (Definition && Definition->GrantedAbilities.HasTag(Ability))
		{
			return true;
		}
	}
	return false;
}

float FGinkgoMovementWindowManager::GetMultiplier(const FDefinitions Definitions, const FWindows& Windows, const FGameplayTag& Category) const
{
	FGameplayTagContainer Blocked;
	for (const FGinkgoMovementWindowState& Window : Windows)
	{
		if (const UGinkgoMovementWindow* Definition = FindDefinition(Definitions, Window))
		{
			Blocked.AppendTags(Definition->BlockedCategories);
		}
	}
	float Multiplier = 1.f;
	for (const FGinkgoMovementWindowState& Window : Windows)
	{
		const UGinkgoMovementWindow* Definition = FindDefinition(Definitions, Window);
		if (!Definition)
		{
			continue;
		}
		for (const FGinkgoWindowMultiplier& Contribution : Definition->Multipliers)
		{
			if (Contribution.Category.MatchesTag(Category) && (Contribution.Phase == INDEX_NONE || Contribution.Phase == Window.Phase)
				&& !Contribution.Category.MatchesAny(Blocked))
			{
				Multiplier *= Contribution.Value ? Contribution.Value->Evaluate(Window.PhaseTime, Window.Param) : 1.f;
			}
		}
	}
	return Multiplier;
}

void FGinkgoMovementWindowManager::RebuildTags(const FDefinitions Definitions, const FWindows& Windows)
{
	// 没有窗口是常态，每次移动都会走到这里
	if (Windows.IsEmpty() && Tags.IsEmpty())
	{
		return;
	}
	Tags.Reset();
	for (const FGinkgoMovementWindowState& Window : Windows)
	{
		const UGinkgoMovementWindow* Definition = FindDefinition(Definitions, Window);
		if (!Definition)
		{
			continue;
		}
		if (Definition->Tag.IsValid())
		{
			Tags.AddTag(Definition->Tag);
		}
		if (Definition->Phases[Window.Phase].Tag.IsValid())
		{
			Tags.AddTag(Definition->Phases[Window.Phase].Tag);
		}
	}
}

TArray<FGinkgoMovementWindowManager::FChange, TInlineAllocator<8>> FGinkgoMovementWindowManager::TakeChanges()
{
	TArray<FChange, TInlineAllocator<8>> Taken = MoveTemp(Changes);
	Changes.Reset();
	return Taken;
}

void FGinkgoMovementWindowManager::EndWindow(FWindows& Windows, const int32 Index)
{
	RecordChange(Windows[Index].Definition, Windows[Index].Phase, INDEX_NONE);
	Windows.RemoveAt(Index);
}

void FGinkgoMovementWindowManager::ClearWindows(const FDefinitions Definitions, FWindows& Windows, const FGameplayTagContainer& Cleared, uint32& Fresh)
{
	if (Cleared.IsEmpty())
	{
		return;
	}
	// 从后往前删，还没看过的窗口下标不变
	for (int32 Index = Windows.Num() - 1; Index >= 0; --Index)
	{
		const UGinkgoMovementWindow* Definition = FindDefinition(Definitions, Windows[Index]);
		if ((Fresh & (1u << Index)) || !Definition
			|| !(Definition->Tag.MatchesAny(Cleared) || Definition->Phases[Windows[Index].Phase].Tag.MatchesAny(Cleared)))
		{
			continue;
		}
		EndWindow(Windows, Index);
		// 后面的窗口前移一位，它们的位跟着移
		const uint32 Below = Fresh & ((1u << Index) - 1);
		Fresh = Below | ((Fresh >> (Index + 1)) << Index);
	}
}

void FGinkgoMovementWindowManager::RecordChange(const int32 Definition, const int32 FromPhase, const int32 ToPhase)
{
	++ChangeCount;
	Changes.Add({Definition, FromPhase, ToPhase});
}

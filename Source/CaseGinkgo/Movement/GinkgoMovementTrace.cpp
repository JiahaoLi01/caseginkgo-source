#include "GinkgoMovementTrace.h"

#if GINKGO_MOVEMENT_TRACE_ENABLED

#include "GameFramework/Character.h"
#include "GinkgoCharacterMovementComponent.h"
#include "Misc/ScopeRWLock.h"
#include "ObjectTrace.h"
#include "Trace/Trace.inl"
#include "TraceFilter.h"

const TCHAR* FGinkgoMovementTrace::ChannelName = TEXT("GinkgoMovement");

UE_TRACE_CHANNEL(GinkgoMovementChannel, "CaseGinkgo movement: window state per move, window events and changes, client corrections");

// 事件与字段的名字要与编辑器模块的分析器一致（CaseGinkgoEditor/Private/Trace/GinkgoMovementTraceAnalyzer.cpp）

UE_TRACE_EVENT_BEGIN(GinkgoMovement, StringId, NoSync | Important)
	UE_TRACE_EVENT_FIELD(uint32, Id)
	UE_TRACE_EVENT_FIELD(UE::Trace::WideString, Value)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(GinkgoMovement, WindowState)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, Frame)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint8, Replay)
	UE_TRACE_EVENT_FIELD(uint8[], Definitions)
	UE_TRACE_EVENT_FIELD(uint8[], Phases)
	UE_TRACE_EVENT_FIELD(float[], PhaseTimes)
	UE_TRACE_EVENT_FIELD(float[], PhaseDurations)
	UE_TRACE_EVENT_FIELD(float[], Params)
	UE_TRACE_EVENT_FIELD(uint32[], WindowTags)
	UE_TRACE_EVENT_FIELD(uint32[], PhaseTags)
	UE_TRACE_EVENT_FIELD(uint32[], MultiplierCategories)
	UE_TRACE_EVENT_FIELD(float[], MultiplierValues)
	UE_TRACE_EVENT_FIELD(uint32[], BlockedCategories)
	UE_TRACE_EVENT_FIELD(uint32[], Abilities)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(GinkgoMovement, WindowEvent)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, Frame)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint8, Replay)
	UE_TRACE_EVENT_FIELD(uint8, EventType)
	UE_TRACE_EVENT_FIELD(float, Value)
	UE_TRACE_EVENT_FIELD(uint8, Flag)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(GinkgoMovement, WindowChange)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, Frame)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint8, Replay)
	UE_TRACE_EVENT_FIELD(uint8, Dispatched)
	UE_TRACE_EVENT_FIELD(uint8, Definition)
	UE_TRACE_EVENT_FIELD(uint32, WindowTag)
	UE_TRACE_EVENT_FIELD(int32, FromPhase)
	UE_TRACE_EVENT_FIELD(int32, ToPhase)
	UE_TRACE_EVENT_FIELD(uint32, FromPhaseTag)
	UE_TRACE_EVENT_FIELD(uint32, ToPhaseTag)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(GinkgoMovement, Correction)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, RecordingTime)
	UE_TRACE_EVENT_FIELD(uint64, Frame)
	UE_TRACE_EVENT_FIELD(uint64, ComponentId)
	UE_TRACE_EVENT_FIELD(uint8, Replay)
	UE_TRACE_EVENT_FIELD(float, PositionError)
UE_TRACE_EVENT_END()

namespace
{
	FRWLock NameIdLock;
	TMap<FName, uint32> NameIds;
	uint32 LastNameId = 0;

	/**
	 * @brief 名字的编号：第一次见到时发一个 StringId 事件；None 为 0
	 * @note 编号在整个进程里不变。StringId 是 Important 事件，之后开始的录制也会先收到它
	 */
	uint32 TraceNameId(const FName InName)
	{
		if (InName.IsNone())
		{
			return 0;
		}
		{
			FReadScopeLock ReadLock(NameIdLock);
			if (const uint32* Found = NameIds.Find(InName))
			{
				return *Found;
			}
		}
		uint32 Id = 0;
		{
			FWriteScopeLock WriteLock(NameIdLock);
			if (const uint32* Found = NameIds.Find(InName))
			{
				return *Found;
			}
			Id = ++LastNameId;
			NameIds.Add(InName, Id);
		}
		const FString Value = InName.ToString();
		UE_TRACE_LOG(GinkgoMovement, StringId, GinkgoMovementChannel, Value.Len() * sizeof(TCHAR))
			<< StringId.Id(Id)
			<< StringId.Value(*Value, Value.Len());
		return Id;
	}

	uint32 TraceTagId(const FGameplayTag& Tag)
	{
		return TraceNameId(Tag.GetTagName());
	}

	/** 客户端正在重放旧的移动 */
	uint8 IsReplaying(const UGinkgoCharacterMovementComponent& Movement)
	{
		const ACharacter* Character = Movement.GetCharacterOwner();
		return Character && Character->bClientUpdating ? 1 : 0;
	}

	/** 能不能记这个组件：通道开着、对象没被追踪过滤器挡掉；能记时先记下对象本身，Rewind Debugger 才认得它 */
	bool BeginTrace(const UGinkgoCharacterMovementComponent& Movement)
	{
		if (!FGinkgoMovementTrace::IsEnabled() || CANNOT_TRACE_OBJECT(&Movement))
		{
			return false;
		}
		TRACE_OBJECT(&Movement);
		return true;
	}
}

bool FGinkgoMovementTrace::IsEnabled()
{
	return UE_TRACE_CHANNELEXPR_IS_ENABLED(GinkgoMovementChannel);
}

void FGinkgoMovementTrace::TraceWindowState(const UGinkgoCharacterMovementComponent& Movement)
{
	if (!BeginTrace(Movement))
	{
		return;
	}
	const FGinkgoMovementWindowManager::FDefinitions Definitions = Movement.GetWindowDefinitions();
	const FGinkgoMovementSimState::FWindows& Windows = Movement.GetSimState().Windows;
	constexpr int32 InlineCount = FGinkgoMovementSimState::MaxWindows;
	TArray<uint8, TInlineAllocator<InlineCount>> DefinitionIndices;
	TArray<uint8, TInlineAllocator<InlineCount>> Phases;
	TArray<float, TInlineAllocator<InlineCount>> PhaseTimes;
	TArray<float, TInlineAllocator<InlineCount>> PhaseDurations;
	TArray<float, TInlineAllocator<InlineCount>> Params;
	TArray<uint32, TInlineAllocator<InlineCount>> WindowTags;
	TArray<uint32, TInlineAllocator<InlineCount>> PhaseTags;
	TArray<FGameplayTag, TInlineAllocator<InlineCount>> Categories;
	FGameplayTagContainer Blocked;
	FGameplayTagContainer Abilities;
	for (const FGinkgoMovementWindowState& Window : Windows)
	{
		const UGinkgoMovementWindow* Definition = Definitions.IsValidIndex(Window.Definition) ? Definitions[Window.Definition].Get() : nullptr;
		const FGinkgoWindowPhase* Phase = Definition && Definition->Phases.IsValidIndex(Window.Phase) ? &Definition->Phases[Window.Phase] : nullptr;
		DefinitionIndices.Add(Window.Definition);
		Phases.Add(Window.Phase);
		PhaseTimes.Add(Window.PhaseTime);
		PhaseDurations.Add(Phase ? Phase->Duration : 0.f);
		Params.Add(Window.Param);
		WindowTags.Add(Definition ? TraceTagId(Definition->Tag) : 0);
		PhaseTags.Add(Phase ? TraceTagId(Phase->Tag) : 0);
		if (Definition)
		{
			for (const FGinkgoWindowMultiplier& Multiplier : Definition->Multipliers)
			{
				if (Multiplier.Category.IsValid())
				{
					Categories.AddUnique(Multiplier.Category);
				}
			}
			Blocked.AppendTags(Definition->BlockedCategories);
			Abilities.AppendTags(Definition->GrantedAbilities);
		}
	}
	// 倍率记涉及的每个类别上组合后的值（屏蔽也算进去），与移动组件查询时得到的一样
	TArray<uint32, TInlineAllocator<InlineCount>> CategoryIds;
	TArray<float, TInlineAllocator<InlineCount>> CategoryValues;
	for (const FGameplayTag& Category : Categories)
	{
		CategoryIds.Add(TraceTagId(Category));
		CategoryValues.Add(Movement.GetWindowMultiplier(Category));
	}
	TArray<uint32, TInlineAllocator<InlineCount>> BlockedIds;
	for (const FGameplayTag& Tag : Blocked)
	{
		BlockedIds.Add(TraceTagId(Tag));
	}
	TArray<uint32, TInlineAllocator<InlineCount>> AbilityIds;
	for (const FGameplayTag& Tag : Abilities)
	{
		AbilityIds.Add(TraceTagId(Tag));
	}

	UE_TRACE_LOG(GinkgoMovement, WindowState, GinkgoMovementChannel)
		<< WindowState.Cycle(FPlatformTime::Cycles64())
		<< WindowState.RecordingTime(FObjectTrace::GetObjectWorldElapsedTime(&Movement))
		<< WindowState.Frame(GFrameCounter)
		<< WindowState.ComponentId(FObjectTrace::GetObjectId(&Movement))
		<< WindowState.Replay(IsReplaying(Movement))
		<< WindowState.Definitions(DefinitionIndices.GetData(), DefinitionIndices.Num())
		<< WindowState.Phases(Phases.GetData(), Phases.Num())
		<< WindowState.PhaseTimes(PhaseTimes.GetData(), PhaseTimes.Num())
		<< WindowState.PhaseDurations(PhaseDurations.GetData(), PhaseDurations.Num())
		<< WindowState.Params(Params.GetData(), Params.Num())
		<< WindowState.WindowTags(WindowTags.GetData(), WindowTags.Num())
		<< WindowState.PhaseTags(PhaseTags.GetData(), PhaseTags.Num())
		<< WindowState.MultiplierCategories(CategoryIds.GetData(), CategoryIds.Num())
		<< WindowState.MultiplierValues(CategoryValues.GetData(), CategoryValues.Num())
		<< WindowState.BlockedCategories(BlockedIds.GetData(), BlockedIds.Num())
		<< WindowState.Abilities(AbilityIds.GetData(), AbilityIds.Num());
}

void FGinkgoMovementTrace::TraceWindowEvent(const UGinkgoCharacterMovementComponent& Movement, const FGinkgoWindowEvent& Event)
{
	if (!BeginTrace(Movement))
	{
		return;
	}
	UE_TRACE_LOG(GinkgoMovement, WindowEvent, GinkgoMovementChannel)
		<< WindowEvent.Cycle(FPlatformTime::Cycles64())
		<< WindowEvent.RecordingTime(FObjectTrace::GetObjectWorldElapsedTime(&Movement))
		<< WindowEvent.Frame(GFrameCounter)
		<< WindowEvent.ComponentId(FObjectTrace::GetObjectId(&Movement))
		<< WindowEvent.Replay(IsReplaying(Movement))
		<< WindowEvent.EventType(static_cast<uint8>(Event.Type))
		<< WindowEvent.Value(Event.Value)
		<< WindowEvent.Flag(Event.bFlag ? 1 : 0);
}

void FGinkgoMovementTrace::TraceWindowChange(const UGinkgoCharacterMovementComponent& Movement, const int32 DefinitionIndex, const UGinkgoMovementWindow* Definition,
	const int32 FromPhase, const int32 ToPhase, const bool bDispatched)
{
	if (!BeginTrace(Movement))
	{
		return;
	}
	const auto PhaseTagId = [Definition](const int32 Phase)
	{
		return Definition && Definition->Phases.IsValidIndex(Phase) ? TraceTagId(Definition->Phases[Phase].Tag) : 0;
	};
	UE_TRACE_LOG(GinkgoMovement, WindowChange, GinkgoMovementChannel)
		<< WindowChange.Cycle(FPlatformTime::Cycles64())
		<< WindowChange.RecordingTime(FObjectTrace::GetObjectWorldElapsedTime(&Movement))
		<< WindowChange.Frame(GFrameCounter)
		<< WindowChange.ComponentId(FObjectTrace::GetObjectId(&Movement))
		<< WindowChange.Replay(IsReplaying(Movement))
		<< WindowChange.Dispatched(bDispatched ? 1 : 0)
		<< WindowChange.Definition(static_cast<uint8>(DefinitionIndex))
		<< WindowChange.WindowTag(Definition ? TraceTagId(Definition->Tag) : 0)
		<< WindowChange.FromPhase(FromPhase)
		<< WindowChange.ToPhase(ToPhase)
		<< WindowChange.FromPhaseTag(PhaseTagId(FromPhase))
		<< WindowChange.ToPhaseTag(PhaseTagId(ToPhase));
}

void FGinkgoMovementTrace::TraceCorrection(const UGinkgoCharacterMovementComponent& Movement, const float PositionError)
{
	if (!BeginTrace(Movement))
	{
		return;
	}
	UE_TRACE_LOG(GinkgoMovement, Correction, GinkgoMovementChannel)
		<< Correction.Cycle(FPlatformTime::Cycles64())
		<< Correction.RecordingTime(FObjectTrace::GetObjectWorldElapsedTime(&Movement))
		<< Correction.Frame(GFrameCounter)
		<< Correction.ComponentId(FObjectTrace::GetObjectId(&Movement))
		<< Correction.Replay(IsReplaying(Movement))
		<< Correction.PositionError(PositionError);
}

#endif // GINKGO_MOVEMENT_TRACE_ENABLED

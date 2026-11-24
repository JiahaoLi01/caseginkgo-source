#include "GinkgoWindowTimelineModel.h"

namespace
{
	using FChange = FGinkgoMovementWindowManager::FChange;

	/** 阶段的时长；定义已经不在列表里、阶段越界或不定长时为 0 */
	float PhaseDurationOf(const FGinkgoMovementWindowManager::FDefinitions Definitions, const uint8 Definition, const uint8 Phase)
	{
		const UGinkgoMovementWindow* Window = Definitions.IsValidIndex(Definition) ? Definitions[Definition].Get() : nullptr;
		return Window && Window->Phases.IsValidIndex(Phase) ? Window->Phases[Phase].Duration : 0.f;
	}

	bool IsDefinitionIndex(const int32 Definition)
	{
		return Definition >= 0 && Definition <= MAX_uint8;
	}
}

void FGinkgoWindowTimelineModel::AddChange(const int32 Definition)
{
	if (!IsDefinitionIndex(Definition))
	{
		return;
	}
	Segmenter.NotifyChanged(static_cast<uint8>(Definition));
	Changed.AddUnique(static_cast<uint8>(Definition));
}

void FGinkgoWindowTimelineModel::AddEvent(const FGinkgoWindowEvent& Event, const TConstArrayView<FChange> Changes)
{
	// 方向键每按一下都是一次事件，平时跑动时按个不停；只画用掉了 lurch 的那一下
	if (Event.Type == EGinkgoWindowEvent::DirectionPressed && Changes.IsEmpty())
	{
		return;
	}
	FEvent& Entry = Events.AddDefaulted_GetRef();
	Entry.Event = Event;
	if (Changes.IsEmpty())
	{
		return;
	}
	Entry.Chain = NextChain++;
	for (const FChange& Change : Changes)
	{
		if (!IsDefinitionIndex(Change.Definition))
		{
			continue;
		}
		const uint8 Definition = static_cast<uint8>(Change.Definition);
		// 这些变化也经过 OnMovementWindowChanged；这里再记一次，只接这一个事件时段也切得对
		AddChange(Definition);

		// 一种窗口在一次事件里的几次变化合成一条连线：推入过就是推入（先结束、再推入是从头开始），否则以最后一次为准
		const ELinkKind Kind = Change.FromPhase == INDEX_NONE ? ELinkKind::Push : (Change.ToPhase == INDEX_NONE ? ELinkKind::End : ELinkKind::Phase);
		if (FLink* Link = Entry.Links.FindByPredicate([&](const FLink& Candidate) { return Candidate.Definition == Definition; }))
		{
			Link->Kind = Link->Kind == ELinkKind::Push ? ELinkKind::Push : Kind;
		}
		else
		{
			Entry.Links.Add({Definition, Kind});
		}

		// 同一帧里被几个事件改过的窗口，属于最后一个事件的链
		if (FEventChange* Existing = EventChanges.FindByPredicate([&](const FEventChange& Candidate) { return Candidate.Definition == Definition; }))
		{
			Existing->Chain = Entry.Chain;
		}
		else
		{
			EventChanges.Add({Definition, Entry.Chain});
		}
	}
}

void FGinkgoWindowTimelineModel::AddCorrection(const float PositionError)
{
	Corrections.Add(PositionError);
}

void FGinkgoWindowTimelineModel::EndFrame(const double Now, const double InFrameStart, const TConstArrayView<FGinkgoMovementWindowState> Windows,
	const FGinkgoMovementWindowManager::FDefinitions Definitions, FFrame& Out)
{
	const double FrameStart = FMath::Min(InFrameStart, Now);
	Out = FFrame();
	Out.EventTime = FrameStart;

	TArray<FGinkgoWindowSegmenter::FWindow, TInlineAllocator<8>> Snapshot;
	for (const FGinkgoMovementWindowState& Window : Windows)
	{
		Snapshot.Add({Window.Definition, Window.Phase, Window.PhaseTime});
	}
	FGinkgoWindowSegmenter::FEnds Ends;
	FGinkgoWindowSegmenter::FPresences Present;
	Segmenter.Update(Snapshot, Ends, Present);

	const auto FindEventChange = [&](const uint8 Definition)
	{
		return EventChanges.FindByPredicate([&](const FEventChange& Candidate) { return Candidate.Definition == Definition; });
	};
	const auto FindOpen = [&](const uint8 Definition)
	{
		return Open.IndexOfByPredicate([&](const FOpen& Candidate) { return Candidate.Definition == Definition; });
	};

	// ===== 新的段：先定开始的时刻，同一种窗口换阶段时旧的段在同一点结束 =====
	// 第一帧不限制开始：开时间轴之前就在的窗口从它真正开始的时刻画起
	const double EarliestStart = bFirstFrame ? TNumericLimits<double>::Lowest() : FrameStart;
	TArray<FOpen, TInlineAllocator<8>> Opened;
	for (const FGinkgoWindowSegmenter::FPresence& Presence : Present)
	{
		if (!Presence.bStarted)
		{
			continue;
		}
		const FGinkgoMovementWindowState& Window = Windows[Presence.Index];
		FSegmentStart& Start = Out.Starts.AddDefaulted_GetRef();
		Start.Id = NextSegmentId++;
		Start.Definition = Window.Definition;
		Start.Phase = Window.Phase;
		Start.Duration = PhaseDurationOf(Definitions, Window.Definition, Window.Phase);
		Start.Count = Presence.Count;
		Start.bExplained = Presence.bByChange || bFirstFrame;
		if (const FEventChange* EventChange = FindEventChange(Window.Definition))
		{
			// 事件推入、刷新或换的阶段：与事件同一点
			Start.Time = FrameStart;
			Start.Chain = EventChange->Chain;
		}
		else
		{
			// 按时长换的阶段、开时间轴之前就在的、校正加上的：从模拟给的已过时间倒推。按时长换阶段的接着用原来那一段的链
			Start.Time = FMath::Clamp(Now - Window.PhaseTime, EarliestStart, Now);
			const int32 Previous = FindOpen(Window.Definition);
			Start.Chain = Previous != INDEX_NONE ? Open[Previous].Chain : INDEX_NONE;
		}
		Opened.Add({Window.Definition, Start.Id, Start.Chain, Start.Duration});
	}

	// ===== 结束的段 =====
	for (const FGinkgoWindowSegmenter::FEnd& End : Ends)
	{
		const int32 OpenIndex = FindOpen(End.Definition);
		if (OpenIndex == INDEX_NONE)
		{
			continue;
		}
		const FOpen& Segment = Open[OpenIndex];
		// 有变化、却不是事件引起的：管理器在移动结束时按时长推进，换了阶段或结束
		const bool bByDuration = !FindEventChange(End.Definition) && Changed.Contains(End.Definition) && Segment.Duration > 0.f;
		FSegmentEnd& SegmentEnd = Out.Ends.AddDefaulted_GetRef();
		SegmentEnd.Id = Segment.Id;
		SegmentEnd.Elapsed = bByDuration ? Segment.Duration : End.LastPhaseTime;
		SegmentEnd.bExplained = End.bByChange;
		const FSegmentStart* Restart = Out.Starts.FindByPredicate([&](const FSegmentStart& Start) { return Start.Definition == End.Definition; });
		if (Restart)
		{
			SegmentEnd.Time = Restart->Time;
		}
		else if (bByDuration)
		{
			SegmentEnd.Time = FMath::Clamp(FrameStart + (Segment.Duration - End.LastPhaseTime), FrameStart, Now);
		}
		else
		{
			SegmentEnd.Time = FrameStart;
		}
		Open.RemoveAt(OpenIndex, EAllowShrinking::No);
	}
	Open.Append(Opened);

	Out.Events = MoveTemp(Events);
	Out.Corrections = MoveTemp(Corrections);
	Events.Reset();
	Corrections.Reset();
	Changed.Reset();
	EventChanges.Reset();
	bFirstFrame = false;
}

void FGinkgoWindowTimelineModel::Reset()
{
	Segmenter.Reset();
	Open.Reset();
	Changed.Reset();
	EventChanges.Reset();
	Events.Reset();
	Corrections.Reset();
	bFirstFrame = true;
}

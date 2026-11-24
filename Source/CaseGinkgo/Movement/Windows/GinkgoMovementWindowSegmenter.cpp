#include "GinkgoMovementWindowSegmenter.h"

void FGinkgoWindowSegmenter::NotifyChanged(const uint8 Definition)
{
	Changed.AddUnique(Definition);
}

void FGinkgoWindowSegmenter::Update(const TConstArrayView<FWindow> Windows, FEnds& OutEnds, FPresences& OutPresent)
{
	OutEnds.Reset();
	OutPresent.Reset();

	// 每种窗口在快照里的第一个与个数
	for (int32 Index = 0; Index < Windows.Num(); ++Index)
	{
		FPresence* Presence = OutPresent.FindByPredicate([&](const FPresence& Candidate) { return Candidate.Definition == Windows[Index].Definition; });
		if (!Presence)
		{
			Presence = &OutPresent.AddDefaulted_GetRef();
			Presence->Definition = Windows[Index].Definition;
			Presence->Index = Index;
		}
		++Presence->Count;
	}

	// 开着的段：不在快照里了、换了阶段、计时倒退或者有变化，就在这份快照上结束
	for (int32 OpenIndex = 0; OpenIndex < Open.Num();)
	{
		FOpen& Segment = Open[OpenIndex];
		const FPresence* Presence = OutPresent.FindByPredicate([&](const FPresence& Candidate) { return Candidate.Definition == Segment.Definition; });
		const FWindow* Window = Presence ? &Windows[Presence->Index] : nullptr;
		const bool bChanged = Changed.Contains(Segment.Definition);
		if (Window && !bChanged && Window->Phase == Segment.Phase && Window->PhaseTime >= Segment.LastPhaseTime)
		{
			Segment.LastPhaseTime = Window->PhaseTime;
			++OpenIndex;
			continue;
		}
		OutEnds.Add({Segment.Definition, Segment.Phase, Segment.LastPhaseTime, bChanged});
		Open.RemoveAt(OpenIndex, EAllowShrinking::No);
	}

	// 快照里有、却没有开着的段的窗口：开新的段
	for (FPresence& Presence : OutPresent)
	{
		if (Open.ContainsByPredicate([&](const FOpen& Segment) { return Segment.Definition == Presence.Definition; }))
		{
			continue;
		}
		const FWindow& Window = Windows[Presence.Index];
		Presence.bStarted = true;
		Presence.bByChange = Changed.Contains(Presence.Definition);
		Open.Add({Window.Definition, Window.Phase, Window.PhaseTime});
	}
	Changed.Reset();
}

void FGinkgoWindowSegmenter::Reset()
{
	Open.Reset();
	Changed.Reset();
}

#include "GinkgoMovementTraceData.h"

#include "Algo/BinarySearch.h"

void FGinkgoMovementTraceData::AppendName(const uint32 Id, const FName Name)
{
	Names.Add(Id, Name);
}

void FGinkgoMovementTraceData::AppendSnapshot(const uint64 ObjectId, FGinkgoTraceSnapshot&& Snapshot)
{
	FGinkgoTraceObject& Object = Objects.FindOrAdd(ObjectId);
	// 重放的快照不切段：它们记的是过去的移动，时刻却是这一帧；这一帧最后那个第一次模拟的快照已经反映了校正的结果
	if (!Snapshot.bReplay)
	{
		UpdateSegments(Object, Snapshot);
	}
	Object.Snapshots.Add(MoveTemp(Snapshot));
}

void FGinkgoMovementTraceData::AppendMarker(const uint64 ObjectId, const FGinkgoTraceMarker& Marker)
{
	FGinkgoTraceObject& Object = Objects.FindOrAdd(ObjectId);
	// 发给了表现层的窗口变化：这种窗口的段在下一个快照重新开始
	if (Marker.Kind == EGinkgoTraceMarkerKind::WindowChange && Marker.bDispatched)
	{
		Object.Segmenter.NotifyChanged(Marker.Definition);
	}
	Object.Markers.Add(Marker);
}

void FGinkgoMovementTraceData::UpdateSegments(FGinkgoTraceObject& Object, const FGinkgoTraceSnapshot& Snapshot)
{
	TArray<FGinkgoWindowSegmenter::FWindow, TInlineAllocator<8>> Windows;
	for (const FGinkgoTraceWindow& Window : Snapshot.Windows)
	{
		Windows.Add({Window.Definition, Window.Phase, Window.PhaseTime});
	}
	FGinkgoWindowSegmenter::FEnds Ends;
	FGinkgoWindowSegmenter::FPresences Present;
	Object.Segmenter.Update(Windows, Ends, Present);

	// 结束的段在这次快照的时刻结束
	for (const FGinkgoWindowSegmenter::FEnd& End : Ends)
	{
		int32 SegmentIndex = INDEX_NONE;
		if (Object.OpenSegments.RemoveAndCopyValue(End.Definition, SegmentIndex))
		{
			FGinkgoTraceSegment& Segment = Object.Segments[SegmentIndex];
			Segment.EndTime = Snapshot.Time;
			Segment.EndFrame = Snapshot.Frame;
			Segment.bEndedByChange = End.bByChange;
		}
	}

	// 接着的段记下同时最多几个；新的段按快照里的先后加，段的数组仍按开始时间排
	for (const FGinkgoWindowSegmenter::FPresence& Presence : Present)
	{
		if (!Presence.bStarted)
		{
			if (const int32* SegmentIndex = Object.OpenSegments.Find(Presence.Definition))
			{
				FGinkgoTraceSegment& Segment = Object.Segments[*SegmentIndex];
				Segment.Count = FMath::Max(Segment.Count, Presence.Count);
			}
			continue;
		}
		const FGinkgoTraceWindow& Window = Snapshot.Windows[Presence.Index];
		FGinkgoTraceSegment Segment;
		Segment.StartTime = Snapshot.Time;
		Segment.StartFrame = Snapshot.Frame;
		Segment.Definition = Window.Definition;
		Segment.Phase = Window.Phase;
		Segment.WindowTag = Window.WindowTag;
		Segment.PhaseTag = Window.PhaseTag;
		Segment.PhaseDuration = Window.PhaseDuration;
		Segment.Param = Window.Param;
		Segment.Count = Presence.Count;
		Segment.bStartedByChange = Presence.bByChange;
		Object.OpenSegments.Add(Presence.Definition, Object.Segments.Add(Segment));
		Object.Definitions.AddUnique(Presence.Definition);
	}
}

int32 FGinkgoMovementTraceData::FindSnapshotIndexAt(const FGinkgoTraceObject& Object, const double Time) const
{
	// 第一个晚于这个时刻的快照之前；同一帧里重放的快照排在第一次模拟的快照之前，往回跳过它们
	int32 Index = Algo::UpperBoundBy(Object.Snapshots, Time, &FGinkgoTraceSnapshot::Time) - 1;
	while (Index >= 0 && Object.Snapshots[Index].bReplay)
	{
		--Index;
	}
	return Index;
}

int32 FGinkgoMovementTraceData::CountReplaysBefore(const FGinkgoTraceObject& Object, const int32 SnapshotIndex) const
{
	if (!Object.Snapshots.IsValidIndex(SnapshotIndex))
	{
		return 0;
	}
	const uint64 Frame = Object.Snapshots[SnapshotIndex].Frame;
	int32 Count = 0;
	for (int32 Index = SnapshotIndex - 1; Index >= 0 && Object.Snapshots[Index].Frame == Frame && Object.Snapshots[Index].bReplay; --Index)
	{
		++Count;
	}
	return Count;
}

void FGinkgoMovementTraceData::FindMarkerRange(const FGinkgoTraceObject& Object, const double Start, const double End, int32& OutFirst, int32& OutLast) const
{
	OutFirst = Algo::LowerBoundBy(Object.Markers, Start, &FGinkgoTraceMarker::Time);
	OutLast = Algo::UpperBoundBy(Object.Markers, End, &FGinkgoTraceMarker::Time);
}

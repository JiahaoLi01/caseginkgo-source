#include "Misc/AutomationTest.h"
#include "Trace/GinkgoMovementTraceData.h"

#if WITH_DEV_AUTOMATION_TESTS

// 编辑器模块自己的测试：测试模块（CaseGinkgoTests）也进开发版的游戏，不能依赖编辑器模块
namespace GinkgoMovementTraceDataTests
{
	constexpr uint64 ObjectId = 42;
	constexpr double FrameTime = 1.0 / 60.0;

	/** 阶段已过时间是 float */
	constexpr float PhaseStep = static_cast<float>(FrameTime);
	constexpr uint64 FirstFrame = 100;

	double TimeOf(const int32 FrameIndex)
	{
		return FrameIndex * FrameTime;
	}

	FGinkgoTraceWindow MakeWindow(const uint8 Definition, const uint8 Phase, const float PhaseTime)
	{
		return {Definition, Phase, PhaseTime, 0.3f, 0.f, 10u + Definition, 20u + Phase};
	}

	FGinkgoTraceSnapshot MakeSnapshot(const int32 FrameIndex, const TArray<FGinkgoTraceWindow>& Windows, const bool bReplay = false)
	{
		FGinkgoTraceSnapshot Snapshot;
		Snapshot.Time = TimeOf(FrameIndex);
		Snapshot.Frame = FirstFrame + FrameIndex;
		Snapshot.bReplay = bReplay;
		Snapshot.Windows = Windows;
		return Snapshot;
	}

	FGinkgoTraceMarker MakeChange(const int32 FrameIndex, const uint8 Definition, const int32 FromPhase, const int32 ToPhase, const bool bDispatched = true)
	{
		FGinkgoTraceMarker Marker;
		Marker.Kind = EGinkgoTraceMarkerKind::WindowChange;
		Marker.Time = TimeOf(FrameIndex);
		Marker.Frame = FirstFrame + FrameIndex;
		Marker.bReplay = !bDispatched;
		Marker.bDispatched = bDispatched;
		Marker.Definition = Definition;
		Marker.FromPhase = FromPhase;
		Marker.ToPhase = ToPhase;
		return Marker;
	}

	FGinkgoTraceMarker MakeCorrection(const int32 FrameIndex)
	{
		FGinkgoTraceMarker Marker;
		Marker.Kind = EGinkgoTraceMarkerKind::Correction;
		Marker.Time = TimeOf(FrameIndex);
		Marker.Frame = FirstFrame + FrameIndex;
		Marker.PositionError = 0.05f;
		return Marker;
	}

	bool IsSegment(const FGinkgoTraceSegment& Segment, const uint8 Definition, const uint8 Phase, const int32 StartFrame, const int32 EndFrame,
		const bool bStartedByChange, const bool bEndedByChange)
	{
		const bool bEndMatches = EndFrame == INDEX_NONE ? Segment.IsOpen() : (!Segment.IsOpen() && Segment.EndTime == TimeOf(EndFrame) && Segment.EndFrame == FirstFrame + EndFrame);
		return Segment.Definition == Definition && Segment.Phase == Phase && Segment.StartTime == TimeOf(StartFrame) && Segment.StartFrame == FirstFrame + StartFrame
			&& bEndMatches && Segment.bStartedByChange == bStartedByChange && Segment.bEndedByChange == bEndedByChange;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoMovementTraceSegmentsTest, "CaseGinkgo.Editor.MovementTrace.Segments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoMovementTraceSegmentsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementTraceDataTests;

	// 一个窗口的一生：推入、按快照看出的换阶段、换回阶段 0、同一阶段里的刷新（只有变化分得出来）、结束
	{
		FGinkgoMovementTraceData Data;
		Data.AppendSnapshot(ObjectId, MakeSnapshot(0, {}));
		Data.AppendMarker(ObjectId, MakeChange(1, 0, INDEX_NONE, 0));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(1, {MakeWindow(0, 0, PhaseStep)}));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(2, {MakeWindow(0, 0, 2.f * PhaseStep)}));
		Data.AppendMarker(ObjectId, MakeChange(3, 0, 0, 1));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(3, {MakeWindow(0, 1, 0.01f)}));
		Data.AppendMarker(ObjectId, MakeChange(4, 0, 1, 0));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(4, {MakeWindow(0, 0, PhaseStep)}));
		Data.AppendMarker(ObjectId, MakeChange(5, 0, 0, 0));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(5, {MakeWindow(0, 0, PhaseStep)}));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(6, {MakeWindow(0, 0, 2.f * PhaseStep)}));
		Data.AppendMarker(ObjectId, MakeChange(7, 0, 0, INDEX_NONE));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(7, {}));
		const FGinkgoTraceObject* Object = Data.FindObject(ObjectId);
		if (!TestNotNull(TEXT("有这个组件的数据"), Object))
		{
			return false;
		}
		TestEqual(TEXT("四段"), Object->Segments.Num(), 4);
		if (Object->Segments.Num() == 4)
		{
			TestTrue(TEXT("推入到换阶段"), IsSegment(Object->Segments[0], 0, 0, 1, 3, true, true));
			TestTrue(TEXT("第二个阶段"), IsSegment(Object->Segments[1], 0, 1, 3, 4, true, true));
			TestTrue(TEXT("换回阶段 0，到刷新为止"), IsSegment(Object->Segments[2], 0, 0, 4, 5, true, true));
			TestTrue(TEXT("刷新：阶段与计时都与上一个快照一样，靠变化分开；到结束为止"), IsSegment(Object->Segments[3], 0, 0, 5, 7, true, true));
		}
		TestEqual(TEXT("没有开着的段"), Object->OpenSegments.Num(), 0);
		TestEqual(TEXT("出现过一种窗口"), Object->Definitions.Num(), 1);
	}

	// 校正：录制开始时已经活动的窗口，被校正去掉（没有变化）；另一个窗口被校正加上。同一帧里重放的快照不切段
	{
		FGinkgoMovementTraceData Data;
		Data.AppendSnapshot(ObjectId, MakeSnapshot(0, {MakeWindow(0, 0, PhaseStep)}));
		Data.AppendMarker(ObjectId, MakeCorrection(1));
		Data.AppendMarker(ObjectId, MakeChange(1, 0, 0, 1, false));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(1, {MakeWindow(0, 1, 0.01f)}, true));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(1, {}, true));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(1, {}));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(2, {MakeWindow(1, 0, PhaseStep)}));
		const FGinkgoTraceObject* Object = Data.FindObject(ObjectId);
		if (!TestNotNull(TEXT("有这个组件的数据"), Object))
		{
			return false;
		}
		TestEqual(TEXT("两段"), Object->Segments.Num(), 2);
		if (Object->Segments.Num() == 2)
		{
			TestTrue(TEXT("录制开始时已经活动，校正去掉：开始与结束都没有变化"), IsSegment(Object->Segments[0], 0, 0, 0, 1, false, false));
			TestTrue(TEXT("校正加上的窗口：开始没有变化，还开着"), IsSegment(Object->Segments[1], 1, 0, 2, INDEX_NONE, false, false));
		}

		// 查快照：同一帧里跳过重放的，取第一次模拟的那个
		const int32 AtCorrection = Data.FindSnapshotIndexAt(*Object, TimeOf(1));
		TestEqual(TEXT("校正那一帧取第一次模拟的快照"), AtCorrection, 3);
		TestEqual(TEXT("那一帧之前重放了两次移动"), Data.CountReplaysBefore(*Object, AtCorrection), 2);
		TestEqual(TEXT("两帧之间取前一帧"), Data.FindSnapshotIndexAt(*Object, TimeOf(1) + 0.5 * FrameTime), 3);
		TestEqual(TEXT("第一个快照之前没有"), Data.FindSnapshotIndexAt(*Object, -1.0), INDEX_NONE);
		TestEqual(TEXT("第一个快照那一帧没有重放"), Data.CountReplaysBefore(*Object, 0), 0);

		int32 First = 0;
		int32 Last = 0;
		Data.FindMarkerRange(*Object, TimeOf(1), TimeOf(1), First, Last);
		TestEqual(TEXT("校正那一帧有两个点"), Last - First, 2);
		Data.FindMarkerRange(*Object, TimeOf(2), TimeOf(5), First, Last);
		TestEqual(TEXT("之后没有点"), Last - First, 0);
	}

	// 叠加：同一种窗口两个时，段只跟第一个，记下个数
	{
		FGinkgoMovementTraceData Data;
		Data.AppendMarker(ObjectId, MakeChange(0, 0, INDEX_NONE, 0));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(0, {MakeWindow(0, 0, PhaseStep)}));
		Data.AppendMarker(ObjectId, MakeChange(1, 0, INDEX_NONE, 0));
		Data.AppendSnapshot(ObjectId, MakeSnapshot(1, {MakeWindow(0, 0, 2.f * PhaseStep), MakeWindow(0, 0, PhaseStep)}));
		const FGinkgoTraceObject* Object = Data.FindObject(ObjectId);
		if (TestNotNull(TEXT("有这个组件的数据"), Object))
		{
			// 第二次推入是一次变化，第一个窗口的段因此重新开始：叠加的窗口推入时，轨道上也看得出来
			TestEqual(TEXT("推入第二个时重新开始一段"), Object->Segments.Num(), 2);
			if (Object->Segments.Num() == 2)
			{
				TestEqual(TEXT("新的段记下同时两个"), Object->Segments[1].Count, 2);
			}
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

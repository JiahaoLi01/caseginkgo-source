#include "GinkgoMovementTraceAnalyzer.h"

#include "GinkgoMovementTraceProvider.h"
#include "TraceServices/Model/AnalysisSession.h"

namespace
{
	/** 数组字段读成 TArray */
	template <typename T>
	TArray<T> ReadArray(const UE::Trace::IAnalyzer::FEventData& EventData, const ANSICHAR* FieldName)
	{
		const TArrayView<const T> View = EventData.GetArrayView<T>(FieldName);
		return TArray<T>(View.GetData(), View.Num());
	}

	/** 各类事件共有的字段：录制时间、引擎帧号、是否在重放 */
	FGinkgoTraceMarker ReadMarkerHeader(const UE::Trace::IAnalyzer::FEventData& EventData, const EGinkgoTraceMarkerKind Kind)
	{
		FGinkgoTraceMarker Marker;
		Marker.Kind = Kind;
		Marker.Time = EventData.GetValue<double>("RecordingTime");
		Marker.Frame = EventData.GetValue<uint64>("Frame");
		Marker.bReplay = EventData.GetValue<uint8>("Replay") != 0;
		return Marker;
	}
}

FGinkgoMovementTraceAnalyzer::FGinkgoMovementTraceAnalyzer(TraceServices::IAnalysisSession& InSession, FGinkgoMovementTraceProvider& InProvider)
	: Session(InSession)
	, Provider(InProvider)
{
}

void FGinkgoMovementTraceAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	FInterfaceBuilder& Builder = Context.InterfaceBuilder;
	Builder.RouteEvent(RouteId_StringId, "GinkgoMovement", "StringId");
	Builder.RouteEvent(RouteId_WindowState, "GinkgoMovement", "WindowState");
	Builder.RouteEvent(RouteId_WindowEvent, "GinkgoMovement", "WindowEvent");
	Builder.RouteEvent(RouteId_WindowChange, "GinkgoMovement", "WindowChange");
	Builder.RouteEvent(RouteId_Correction, "GinkgoMovement", "Correction");
}

bool FGinkgoMovementTraceAnalyzer::OnEvent(const uint16 RouteId, const EStyle Style, const FOnEventContext& Context)
{
	TraceServices::FAnalysisSessionEditScope EditScope(Session);
	const FEventData& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_StringId:
	{
		FString Value;
		EventData.GetString("Value", Value);
		Provider.Edit().AppendName(EventData.GetValue<uint32>("Id"), FName(Value));
		break;
	}
	case RouteId_WindowState:
	{
		FGinkgoTraceSnapshot Snapshot;
		Snapshot.Time = EventData.GetValue<double>("RecordingTime");
		Snapshot.Frame = EventData.GetValue<uint64>("Frame");
		Snapshot.bReplay = EventData.GetValue<uint8>("Replay") != 0;
		const TArrayView<const uint8> Definitions = EventData.GetArrayView<uint8>("Definitions");
		const TArrayView<const uint8> Phases = EventData.GetArrayView<uint8>("Phases");
		const TArrayView<const float> PhaseTimes = EventData.GetArrayView<float>("PhaseTimes");
		const TArrayView<const float> PhaseDurations = EventData.GetArrayView<float>("PhaseDurations");
		const TArrayView<const float> Params = EventData.GetArrayView<float>("Params");
		const TArrayView<const uint32> WindowTags = EventData.GetArrayView<uint32>("WindowTags");
		const TArrayView<const uint32> PhaseTags = EventData.GetArrayView<uint32>("PhaseTags");
		// 各数组一样长；防止坏数据，取最短的
		int32 Count = Definitions.Num();
		for (const int32 Num : {Phases.Num(), PhaseTimes.Num(), PhaseDurations.Num(), Params.Num(), WindowTags.Num(), PhaseTags.Num()})
		{
			Count = FMath::Min(Count, Num);
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Snapshot.Windows.Add({Definitions[Index], Phases[Index], PhaseTimes[Index], PhaseDurations[Index], Params[Index], WindowTags[Index], PhaseTags[Index]});
		}
		Snapshot.MultiplierCategories = ReadArray<uint32>(EventData, "MultiplierCategories");
		Snapshot.MultiplierValues = ReadArray<float>(EventData, "MultiplierValues");
		Snapshot.BlockedCategories = ReadArray<uint32>(EventData, "BlockedCategories");
		Snapshot.Abilities = ReadArray<uint32>(EventData, "Abilities");
		Provider.Edit().AppendSnapshot(EventData.GetValue<uint64>("ComponentId"), MoveTemp(Snapshot));
		break;
	}
	case RouteId_WindowEvent:
	{
		FGinkgoTraceMarker Marker = ReadMarkerHeader(EventData, EGinkgoTraceMarkerKind::WindowEvent);
		Marker.EventType = EventData.GetValue<uint8>("EventType");
		Marker.Value = EventData.GetValue<float>("Value");
		Marker.bFlag = EventData.GetValue<uint8>("Flag") != 0;
		Provider.Edit().AppendMarker(EventData.GetValue<uint64>("ComponentId"), Marker);
		break;
	}
	case RouteId_WindowChange:
	{
		FGinkgoTraceMarker Marker = ReadMarkerHeader(EventData, EGinkgoTraceMarkerKind::WindowChange);
		Marker.bDispatched = EventData.GetValue<uint8>("Dispatched") != 0;
		Marker.Definition = EventData.GetValue<uint8>("Definition");
		Marker.WindowTag = EventData.GetValue<uint32>("WindowTag");
		Marker.FromPhase = EventData.GetValue<int32>("FromPhase");
		Marker.ToPhase = EventData.GetValue<int32>("ToPhase");
		Marker.FromPhaseTag = EventData.GetValue<uint32>("FromPhaseTag");
		Marker.ToPhaseTag = EventData.GetValue<uint32>("ToPhaseTag");
		Provider.Edit().AppendMarker(EventData.GetValue<uint64>("ComponentId"), Marker);
		break;
	}
	case RouteId_Correction:
	{
		FGinkgoTraceMarker Marker = ReadMarkerHeader(EventData, EGinkgoTraceMarkerKind::Correction);
		Marker.PositionError = EventData.GetValue<float>("PositionError");
		Provider.Edit().AppendMarker(EventData.GetValue<uint64>("ComponentId"), Marker);
		break;
	}
	default:
		break;
	}
	return true;
}

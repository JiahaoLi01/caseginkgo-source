#include "GinkgoMovementRewindDebugger.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GinkgoMovementTraceProvider.h"
#include "IRewindDebugger.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/GinkgoMovementTrace.h"
#include "Movement/Windows/GinkgoMovementWindow.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Trace/Trace.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "GinkgoMovementRewindDebugger"

namespace
{
	/** 窗口快照的容量，详情里写"几 / 8" */
	constexpr int32 MaxWindows = FGinkgoMovementSimState::MaxWindows;

	/** 阶段的颜色，按阶段的下标轮着用 */
	const FLinearColor PhaseColors[] = {
		FLinearColor(0.32f, 0.58f, 1.f, 0.9f),
		FLinearColor(1.f, 0.66f, 0.24f, 0.9f),
		FLinearColor(0.42f, 0.85f, 0.45f, 0.9f),
		FLinearColor(0.78f, 0.5f, 1.f, 0.9f),
		FLinearColor(1.f, 0.45f, 0.6f, 0.9f),
	};

	const FLinearColor EventColor(0.95f, 0.95f, 0.95f);
	const FLinearColor ReplayColor(0.45f, 0.45f, 0.45f);
	const FLinearColor DispatchedChangeColor(1.f, 0.85f, 0.2f);
	const FLinearColor CorrectionColor(1.f, 0.25f, 0.2f);
	const FLinearColor CursorColor(1.f, 1.f, 1.f, 0.8f);

	/**
	 * @brief 在 Rewind Debugger 当前的分析会话里找这个组件的追踪数据，在会话的读锁里调用回调
	 * @return 找到了数据
	 */
	bool ReadTraceObject(const uint64 ObjectId, TFunctionRef<void(const FGinkgoMovementTraceData&, const FGinkgoTraceObject&)> Callback)
	{
		const IRewindDebugger* RewindDebugger = IRewindDebugger::Instance();
		const TraceServices::IAnalysisSession* Session = RewindDebugger ? RewindDebugger->GetAnalysisSession() : nullptr;
		if (!Session)
		{
			return false;
		}
		TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
		const FGinkgoMovementTraceProvider* Provider = Session->ReadProvider<FGinkgoMovementTraceProvider>(FGinkgoMovementTraceProvider::ProviderName);
		const FGinkgoTraceObject* Object = Provider ? Provider->Read().FindObject(ObjectId) : nullptr;
		if (!Object)
		{
			return false;
		}
		Callback(Provider->Read(), *Object);
		return true;
	}

	TAttribute<TRange<double>> RewindDebuggerViewRange()
	{
		return TAttribute<TRange<double>>::CreateLambda([]() { return IRewindDebugger::Instance()->GetCurrentViewRange(); });
	}

	FString NameOf(const FGinkgoMovementTraceData& Data, const uint32 Id)
	{
		return Id == 0 ? FString(TEXT("-")) : Data.GetName(Id).ToString();
	}

	/** 阶段的短名：阶段标签的最后一节（Movement.Window.Probe.A → A），没有标签时写下标 */
	FString PhaseLabel(const FGinkgoMovementTraceData& Data, const uint32 PhaseTag, const int32 Phase)
	{
		const FString Tag = Data.GetName(PhaseTag).ToString();
		int32 Dot = INDEX_NONE;
		if (PhaseTag != 0 && Tag.FindLastChar(TEXT('.'), Dot))
		{
			return Tag.RightChop(Dot + 1);
		}
		return PhaseTag != 0 ? Tag : FString::Printf(TEXT("阶段 %d"), Phase);
	}

	FString EventName(const uint8 EventType)
	{
		return StaticEnum<EGinkgoWindowEvent>()->GetDisplayNameTextByValue(EventType).ToString();
	}

	/** 最后一个第一次模拟的快照的时刻：还没结束的段画到这里 */
	double LastSnapshotTime(const FGinkgoTraceObject& Object)
	{
		for (int32 Index = Object.Snapshots.Num() - 1; Index >= 0; --Index)
		{
			if (!Object.Snapshots[Index].bReplay)
			{
				return Object.Snapshots[Index].Time;
			}
		}
		return 0.0;
	}

	/** 第一个第一次模拟的快照的时刻：从这一刻就在的段不是变化开始的，是录制开始时已经活动 */
	double FirstSnapshotTime(const FGinkgoTraceObject& Object)
	{
		for (const FGinkgoTraceSnapshot& Snapshot : Object.Snapshots)
		{
			if (!Snapshot.bReplay)
			{
				return Snapshot.Time;
			}
		}
		return 0.0;
	}

	/** 一个点的说明 */
	FString DescribeMarker(const FGinkgoMovementTraceData& Data, const FGinkgoTraceMarker& Marker)
	{
		const FString Where = FString::Printf(TEXT("%.3f s，帧 %llu：%s"), Marker.Time, Marker.Frame, Marker.bReplay ? TEXT("重放里，") : TEXT(""));
		switch (Marker.Kind)
		{
		case EGinkgoTraceMarkerKind::WindowEvent:
			return Where + FString::Printf(TEXT("事件 %s，数值 %.1f，标志 %s"), *EventName(Marker.EventType), Marker.Value, Marker.bFlag ? TEXT("真") : TEXT("假"));
		case EGinkgoTraceMarkerKind::WindowChange:
		{
			FString Change;
			if (Marker.FromPhase == INDEX_NONE)
			{
				Change = FString::Printf(TEXT("推入，进入 %s"), *PhaseLabel(Data, Marker.ToPhaseTag, Marker.ToPhase));
			}
			else if (Marker.ToPhase == INDEX_NONE)
			{
				Change = FString::Printf(TEXT("在 %s 结束"), *PhaseLabel(Data, Marker.FromPhaseTag, Marker.FromPhase));
			}
			else
			{
				Change = FString::Printf(TEXT("%s → %s"), *PhaseLabel(Data, Marker.FromPhaseTag, Marker.FromPhase), *PhaseLabel(Data, Marker.ToPhaseTag, Marker.ToPhase));
			}
			return Where + FString::Printf(TEXT("%s %s，%s"), *NameOf(Data, Marker.WindowTag), *Change,
				Marker.bDispatched ? TEXT("发给了表现层") : TEXT("重放里重算，没有发给表现层"));
		}
		case EGinkgoTraceMarkerKind::Correction:
			return Where + (Marker.PositionError < 0.f ? FString(TEXT("校正（相对底座的位置）")) : FString::Printf(TEXT("校正，位置差 %.2f cm"), Marker.PositionError));
		default:
			return Where;
		}
	}

	FLinearColor MarkerColor(const FGinkgoTraceMarker& Marker)
	{
		switch (Marker.Kind)
		{
		case EGinkgoTraceMarkerKind::WindowChange:
			return Marker.bDispatched ? DispatchedChangeColor : ReplayColor;
		case EGinkgoTraceMarkerKind::Correction:
			return CorrectionColor;
		default:
			return Marker.bReplay ? ReplayColor : EventColor;
		}
	}

	// ===== 时间轴控件 =====

	/**
	 * @class SGinkgoTraceTimeline : 画一条轨道的段与点
	 * 段画成色块，放得下时在里面写阶段名；没有对应窗口变化的边界画一道红线。点画成竖线。
	 * 悬停时提示最近的点（4 像素内，同一时刻的都列出来），否则提示所在的段。
	 */
	class SGinkgoTraceTimeline : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGinkgoTraceTimeline) {}
			SLATE_ARGUMENT(TSharedPtr<const FGinkgoTraceTimelineData>, Timeline)
			SLATE_ATTRIBUTE(TRange<double>, ViewRange)
			SLATE_ARGUMENT(TOptional<double>, CursorTime)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Timeline = InArgs._Timeline;
			ViewRange = InArgs._ViewRange;
			CursorTime = InArgs._CursorTime;
			SetToolTipText(TAttribute<FText>::CreateSP(this, &SGinkgoTraceTimeline::GetHoveredToolTip));
		}

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(100.f, 20.f);
		}

		virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
		{
			HoverX = static_cast<float>(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X);
			HoverWidth = static_cast<float>(MyGeometry.GetLocalSize().X);
			return FReply::Unhandled();
		}

		virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
		{
			HoverX = -1.f;
		}

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
			int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const TRange<double> Range = ViewRange.Get();
			const double Span = Range.Size<double>();
			const FVector2D Size = AllottedGeometry.GetLocalSize();
			if (!Timeline.IsValid() || Span <= 0.0)
			{
				return LayerId;
			}
			const auto ToX = [&Range, Span, &Size](const double Time)
			{
				return static_cast<float>((Time - Range.GetLowerBoundValue()) / Span * Size.X);
			};
			const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
			const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			const float BoxHeight = static_cast<float>(Size.Y) - 4.f;
			const auto DrawBar = [&](const int32 Layer, const float X, const float Width, const FLinearColor& Color)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(Width, BoxHeight), FSlateLayoutTransform(FVector2f(X, 2.f))),
					WhiteBrush, ESlateDrawEffect::None, Color);
			};

			for (const FGinkgoTraceTimelineData::FSpan& Box : Timeline->Spans)
			{
				const float X0 = FMath::Max(ToX(Box.Start), 0.f);
				const float X1 = FMath::Min(ToX(Box.End), static_cast<float>(Size.X));
				if (X1 <= X0)
				{
					continue;
				}
				DrawBar(LayerId, X0, X1 - X0, Box.Color);
				if (Box.bUnexplainedStart)
				{
					DrawBar(LayerId + 1, X0, 2.f, CorrectionColor);
				}
				if (Box.bUnexplainedEnd)
				{
					DrawBar(LayerId + 1, X1 - 2.f, 2.f, CorrectionColor);
				}
				const FVector2D TextSize = FontMeasure->Measure(Box.Label, Font);
				if (TextSize.X + 6.f < X1 - X0)
				{
					FSlateDrawElement::MakeText(OutDrawElements, LayerId + 1,
						AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(X0 + 3.f, (static_cast<float>(Size.Y) - static_cast<float>(TextSize.Y)) / 2.f))),
						Box.Label, Font, ESlateDrawEffect::None, FLinearColor::Black);
				}
			}
			for (const FGinkgoTraceTimelineData::FPoint& Point : Timeline->Points)
			{
				const float X = ToX(Point.Time);
				if (X >= -2.f && X <= Size.X + 2.f)
				{
					DrawBar(LayerId + 2, X - 1.5f, 3.f, Point.Color);
				}
			}
			if (CursorTime.IsSet())
			{
				DrawBar(LayerId + 3, ToX(CursorTime.GetValue()) - 0.5f, 1.f, CursorColor);
			}
			return LayerId + 4;
		}

	private:
		FText GetHoveredToolTip() const
		{
			const TRange<double> Range = ViewRange.Get();
			const double Span = Range.Size<double>();
			if (!Timeline.IsValid() || HoverX < 0.f || HoverWidth <= 0.f || Span <= 0.0)
			{
				return FText::GetEmpty();
			}
			const double Time = Range.GetLowerBoundValue() + HoverX / HoverWidth * Span;
			const double Tolerance = 4.0 * Span / HoverWidth;
			// 最近的点，同一时刻的点一起列出
			const FGinkgoTraceTimelineData::FPoint* Nearest = nullptr;
			for (const FGinkgoTraceTimelineData::FPoint& Point : Timeline->Points)
			{
				if (FMath::Abs(Point.Time - Time) <= Tolerance && (!Nearest || FMath::Abs(Point.Time - Time) < FMath::Abs(Nearest->Time - Time)))
				{
					Nearest = &Point;
				}
			}
			if (Nearest)
			{
				TStringBuilder<1024> Text;
				for (const FGinkgoTraceTimelineData::FPoint& Point : Timeline->Points)
				{
					if (Point.Time == Nearest->Time)
					{
						Text.Appendf(TEXT("%s%s"), Text.Len() > 0 ? TEXT("\n") : TEXT(""), *Point.ToolTip.ToString());
					}
				}
				return FText::FromString(Text.ToString());
			}
			for (const FGinkgoTraceTimelineData::FSpan& Box : Timeline->Spans)
			{
				if (Box.Start <= Time && Time < Box.End)
				{
					return Box.ToolTip;
				}
			}
			return FText::GetEmpty();
		}

		TSharedPtr<const FGinkgoTraceTimelineData> Timeline;

		TAttribute<TRange<double>> ViewRange;

		TOptional<double> CursorTime;

		float HoverX = -1.f;

		float HoverWidth = 0.f;
	};
}

// ===== 轨道与导出图片共用的部分 =====

void BuildGinkgoWindowLane(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, const uint8 Definition, const double ViewStart, const double ViewEnd,
	FGinkgoTraceTimelineData& OutTimeline)
{
	const double FirstTime = FirstSnapshotTime(Object);
	const double LastTime = LastSnapshotTime(Object);
	for (const FGinkgoTraceSegment& Segment : Object.Segments)
	{
		const double End = Segment.IsOpen() ? LastTime : Segment.EndTime;
		if (Segment.Definition != Definition || End < ViewStart || Segment.StartTime > ViewEnd)
		{
			continue;
		}
		const FString Phase = PhaseLabel(Data, Segment.PhaseTag, Segment.Phase);
		const bool bFromRecordingStart = Segment.StartTime == FirstTime;
		const TCHAR* StartReason = Segment.bStartedByChange ? TEXT("窗口变化") : (bFromRecordingStart ? TEXT("录制开始时已经活动") : TEXT("没有对应的窗口变化：校正放回了服务器的状态"));
		const TCHAR* EndReason = Segment.IsOpen() ? TEXT("还在") : (Segment.bEndedByChange ? TEXT("窗口变化") : TEXT("没有对应的窗口变化：校正放回了服务器的状态"));

		FGinkgoTraceTimelineData::FSpan& Box = OutTimeline.Spans.AddDefaulted_GetRef();
		Box.Start = Segment.StartTime;
		Box.End = End;
		Box.Color = PhaseColors[Segment.Phase % UE_ARRAY_COUNT(PhaseColors)];
		Box.Label = FText::FromString(Segment.Count > 1 ? FString::Printf(TEXT("%s ×%d"), *Phase, Segment.Count) : Phase);
		Box.bUnexplainedStart = !Segment.bStartedByChange && !bFromRecordingStart;
		Box.bUnexplainedEnd = !Segment.IsOpen() && !Segment.bEndedByChange;
		const FString EndFrame = Segment.IsOpen() ? FString(TEXT("…")) : FString::Printf(TEXT("%llu"), Segment.EndFrame);
		const FString Duration = Segment.PhaseDuration > 0.f ? FString::Printf(TEXT("%.3f s"), Segment.PhaseDuration) : FString(TEXT("不定长"));
		const FString Stack = Segment.Count > 1 ? FString::Printf(TEXT("，同时 %d 个"), Segment.Count) : FString();
		Box.ToolTip = FText::FromString(FString::Printf(TEXT("%s  阶段 %d %s\n%.3f → %.3f s，%.3f s（帧 %llu → %s）\n阶段时长 %s，参数 %.1f%s\n开始：%s\n结束：%s"),
			*NameOf(Data, Segment.WindowTag), Segment.Phase, *NameOf(Data, Segment.PhaseTag), Segment.StartTime, End, End - Segment.StartTime, Segment.StartFrame, *EndFrame,
			*Duration, Segment.Param, *Stack, StartReason, EndReason));
	}
}

void BuildGinkgoMovementPoints(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, const double ViewStart, const double ViewEnd,
	const bool bOverviewOnly, FGinkgoTraceTimelineData& OutTimeline)
{
	int32 First = 0;
	int32 Last = 0;
	Data.FindMarkerRange(Object, ViewStart, ViewEnd, First, Last);
	for (int32 Index = First; Index < Last; ++Index)
	{
		const FGinkgoTraceMarker& Marker = Object.Markers[Index];
		const bool bInOverview = (Marker.Kind == EGinkgoTraceMarkerKind::WindowChange && Marker.bDispatched) || Marker.Kind == EGinkgoTraceMarkerKind::Correction;
		if (!bOverviewOnly || bInOverview)
		{
			OutTimeline.Points.Add({Marker.Time, MarkerColor(Marker), FText::FromString(DescribeMarker(Data, Marker))});
		}
	}
}

FString DescribeGinkgoMovementAt(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, const double Time)
{
	const int32 SnapshotIndex = Data.FindSnapshotIndexAt(Object, Time);
	if (!Object.Snapshots.IsValidIndex(SnapshotIndex))
	{
		return FString::Printf(TEXT("%.3f s 之前没有快照"), Time);
	}
	const FGinkgoTraceSnapshot& Snapshot = Object.Snapshots[SnapshotIndex];
	TStringBuilder<2048> Text;
	Text.Appendf(TEXT("%.3f s 的快照：%.3f s，帧 %llu"), Time, Snapshot.Time, Snapshot.Frame);
	if (const int32 Replays = Data.CountReplaysBefore(Object, SnapshotIndex); Replays > 0)
	{
		Text.Appendf(TEXT("，这一帧收到校正后重放了 %d 次移动"), Replays);
	}
	Text.Appendf(TEXT("\n\n活动的窗口 %d / %d\n"), Snapshot.Windows.Num(), MaxWindows);
	for (const FGinkgoTraceWindow& Window : Snapshot.Windows)
	{
		const FString Timing = Window.PhaseDuration > 0.f
			? FString::Printf(TEXT("%.3f / %.3f s"), Window.PhaseTime, Window.PhaseDuration)
			: FString::Printf(TEXT("%.3f s，不定长"), Window.PhaseTime);
		Text.Appendf(TEXT("  %s  阶段 %d %s  %s  参数 %.1f\n"), *NameOf(Data, Window.WindowTag), Window.Phase, *NameOf(Data, Window.PhaseTag), *Timing, Window.Param);
	}
	if (Snapshot.Windows.IsEmpty())
	{
		Text.Append(TEXT("  （没有）\n"));
	}
	Text.Append(TEXT("\n倍率\n"));
	for (int32 Index = 0; Index < FMath::Min(Snapshot.MultiplierCategories.Num(), Snapshot.MultiplierValues.Num()); ++Index)
	{
		Text.Appendf(TEXT("  %s  ×%.3f\n"), *NameOf(Data, Snapshot.MultiplierCategories[Index]), Snapshot.MultiplierValues[Index]);
	}
	if (Snapshot.MultiplierCategories.IsEmpty())
	{
		Text.Append(TEXT("  （没有贡献，都是 1）\n"));
	}
	const auto AppendTags = [&Text, &Data](const TCHAR* Title, const TArray<uint32>& Ids)
	{
		Text.Appendf(TEXT("%s："), Title);
		for (int32 Index = 0; Index < Ids.Num(); ++Index)
		{
			Text.Appendf(TEXT("%s%s"), Index > 0 ? TEXT("，") : TEXT(""), *NameOf(Data, Ids[Index]));
		}
		Text.Append(Ids.IsEmpty() ? TEXT("（没有）\n") : TEXT("\n"));
	};
	AppendTags(TEXT("屏蔽的类别"), Snapshot.BlockedCategories);
	AppendTags(TEXT("能力"), Snapshot.Abilities);

	// 这一帧里发生的事，重放里的也列出来并标明
	Text.Append(TEXT("\n这一帧\n"));
	int32 First = 0;
	int32 Last = 0;
	Data.FindMarkerRange(Object, Snapshot.Time, Snapshot.Time, First, Last);
	int32 Listed = 0;
	for (int32 Index = First; Index < Last; ++Index)
	{
		if (Object.Markers[Index].Frame == Snapshot.Frame)
		{
			Text.Appendf(TEXT("  %s\n"), *DescribeMarker(Data, Object.Markers[Index]));
			++Listed;
		}
	}
	if (Listed == 0)
	{
		Text.Append(TEXT("  （没有窗口事件、变化或校正）\n"));
	}
	return Text.ToString();
}

FText GetGinkgoWindowDisplayName(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, const uint8 Definition)
{
	const FGinkgoTraceSegment* Segment = Object.Segments.FindByPredicate([Definition](const FGinkgoTraceSegment& Candidate) { return Candidate.Definition == Definition; });
	return Segment && Segment->WindowTag != 0
		? FText::FromName(Data.GetName(Segment->WindowTag))
		: FText::Format(LOCTEXT("UnnamedWindow", "窗口 {0}"), static_cast<int32>(Definition));
}

TSharedRef<SWidget> MakeGinkgoTraceTimeline(TSharedPtr<const FGinkgoTraceTimelineData> Timeline, TAttribute<TRange<double>> ViewRange, TOptional<double> CursorTime)
{
	return SNew(SGinkgoTraceTimeline).Timeline(MoveTemp(Timeline)).ViewRange(MoveTemp(ViewRange)).CursorTime(CursorTime);
}

// ===== FGinkgoMovementTrackBase =====

FGinkgoMovementTrackBase::FGinkgoMovementTrackBase(const uint64 InObjectId)
	: ObjectId(InObjectId)
	, Timeline(MakeShared<FGinkgoTraceTimelineData>())
	, DetailsText(MakeShared<FText>())
{
}

bool FGinkgoMovementTrackBase::UpdateInternal()
{
	const IRewindDebugger* RewindDebugger = IRewindDebugger::Instance();
	const TRange<double> ViewRange = RewindDebugger->GetCurrentViewRange();
	const double ScrubTime = RewindDebugger->GetScrubTime();
	const bool bIsSelected = GetIsSelected();
	ReadTraceObject(ObjectId, [this, &ViewRange, ScrubTime, bIsSelected](const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object)
	{
		FGinkgoTraceTimelineData& Built = *Timeline;
		Built.Spans.Reset();
		Built.Points.Reset();
		BuildTimeline(Data, Object, ViewRange.GetLowerBoundValue(), ViewRange.GetUpperBoundValue(), Built);
		// 详情只在选中时算：每帧都会更新
		if (bIsSelected)
		{
			*DetailsText = FText::FromString(DescribeGinkgoMovementAt(Data, Object, ScrubTime));
		}
		UpdateChildren(Data, Object);
	});
	return false;
}

TSharedPtr<SWidget> FGinkgoMovementTrackBase::GetTimelineViewInternal()
{
	return MakeGinkgoTraceTimeline(Timeline, RewindDebuggerViewRange());
}

TSharedPtr<SWidget> FGinkgoMovementTrackBase::GetDetailsViewInternal()
{
	const TSharedRef<FText> Text = DetailsText;
	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(6.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
			.Text_Lambda([Text]() { return *Text; })
		];
}

TOptional<double> FGinkgoMovementTrackBase::GetStepFrameTimeInternal(const RewindDebugger::EStepMode StepMode, const FScrubTimeInformation& ScrubTime) const
{
	TOptional<double> Result;
	ReadTraceObject(ObjectId, [this, StepMode, &ScrubTime, &Result](const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object)
	{
		TArray<double> Times;
		CollectStepTimes(Object, Times);
		// 很近的算同一时刻，避免停在原地
		constexpr double Epsilon = 1e-4;
		const double Current = ScrubTime.ElapsedTime;
		for (const double Time : Times)
		{
			if (Time == TNumericLimits<double>::Max())
			{
				continue;
			}
			if (StepMode == RewindDebugger::EStepMode::Forward && Time > Current + Epsilon && (!Result.IsSet() || Time < Result.GetValue()))
			{
				Result = Time;
			}
			else if (StepMode == RewindDebugger::EStepMode::Backward && Time < Current - Epsilon && (!Result.IsSet() || Time > Result.GetValue()))
			{
				Result = Time;
			}
		}
	});
	return Result;
}

// ===== FGinkgoWindowLaneTrack =====

FGinkgoWindowLaneTrack::FGinkgoWindowLaneTrack(const uint64 InObjectId, const uint8 InDefinition, const FText& InDisplayName)
	: FGinkgoMovementTrackBase(InObjectId)
	, Definition(InDefinition)
	, DisplayName(InDisplayName)
{
}

FSlateIcon FGinkgoWindowLaneTrack::GetIconInternal()
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.Tracks.Sub");
}

FName FGinkgoWindowLaneTrack::GetNameInternal() const
{
	return FName(TEXT("GinkgoMovementWindow"), Definition + 1);
}

void FGinkgoWindowLaneTrack::BuildTimeline(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, const double ViewStart, const double ViewEnd,
	FGinkgoTraceTimelineData& OutTimeline) const
{
	BuildGinkgoWindowLane(Data, Object, Definition, ViewStart, ViewEnd, OutTimeline);
}

void FGinkgoWindowLaneTrack::CollectStepTimes(const FGinkgoTraceObject& Object, TArray<double>& OutTimes) const
{
	for (const FGinkgoTraceSegment& Segment : Object.Segments)
	{
		if (Segment.Definition == Definition)
		{
			OutTimes.Add(Segment.StartTime);
			OutTimes.Add(Segment.EndTime);
		}
	}
}

// ===== FGinkgoMovementEventsTrack =====

FGinkgoMovementEventsTrack::FGinkgoMovementEventsTrack(const uint64 InObjectId)
	: FGinkgoMovementTrackBase(InObjectId)
{
}

FSlateIcon FGinkgoMovementEventsTrack::GetIconInternal()
{
	return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.Tracks.Event");
}

FName FGinkgoMovementEventsTrack::GetNameInternal() const
{
	return TEXT("GinkgoMovementEvents");
}

FText FGinkgoMovementEventsTrack::GetDisplayNameInternal() const
{
	return LOCTEXT("EventsTrack", "事件");
}

void FGinkgoMovementEventsTrack::BuildTimeline(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, const double ViewStart, const double ViewEnd,
	FGinkgoTraceTimelineData& OutTimeline) const
{
	BuildGinkgoMovementPoints(Data, Object, ViewStart, ViewEnd, false, OutTimeline);
}

void FGinkgoMovementEventsTrack::CollectStepTimes(const FGinkgoTraceObject& Object, TArray<double>& OutTimes) const
{
	for (const FGinkgoTraceMarker& Marker : Object.Markers)
	{
		OutTimes.Add(Marker.Time);
	}
}

// ===== FGinkgoMovementTrack =====

FGinkgoMovementTrack::FGinkgoMovementTrack(const uint64 InObjectId)
	: FGinkgoMovementTrackBase(InObjectId)
{
	Children.Add(MakeShared<FGinkgoMovementEventsTrack>(InObjectId));
}

bool FGinkgoMovementTrack::UpdateInternal()
{
	FGinkgoMovementTrackBase::UpdateInternal();
	for (const TSharedPtr<RewindDebugger::FRewindDebuggerTrack>& Child : Children)
	{
		Child->Update();
	}
	const bool bChanged = bChildrenChanged;
	bChildrenChanged = false;
	return bChanged;
}

FSlateIcon FGinkgoMovementTrack::GetIconInternal()
{
	return FSlateIconFinder::FindIconForClass(UCharacterMovementComponent::StaticClass());
}

FName FGinkgoMovementTrack::GetNameInternal() const
{
	return TEXT("GinkgoMovement");
}

FText FGinkgoMovementTrack::GetDisplayNameInternal() const
{
	return LOCTEXT("MovementTrack", "Ginkgo 移动");
}

TConstArrayView<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> FGinkgoMovementTrack::GetChildrenInternal(TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& OutTracks) const
{
	return Children;
}

void FGinkgoMovementTrack::BuildTimeline(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, const double ViewStart, const double ViewEnd,
	FGinkgoTraceTimelineData& OutTimeline) const
{
	// 折叠时的总览：发给表现层的窗口变化与校正，事件与重放里的变化在"事件"里
	BuildGinkgoMovementPoints(Data, Object, ViewStart, ViewEnd, true, OutTimeline);
}

void FGinkgoMovementTrack::CollectStepTimes(const FGinkgoTraceObject& Object, TArray<double>& OutTimes) const
{
	for (const FGinkgoTraceSegment& Segment : Object.Segments)
	{
		OutTimes.Add(Segment.StartTime);
		OutTimes.Add(Segment.EndTime);
	}
	for (const FGinkgoTraceMarker& Marker : Object.Markers)
	{
		if (Marker.Kind == EGinkgoTraceMarkerKind::Correction)
		{
			OutTimes.Add(Marker.Time);
		}
	}
}

void FGinkgoMovementTrack::UpdateChildren(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object)
{
	// 新出现的窗口加一条轨道，放在"事件"之前
	for (const uint8 Definition : Object.Definitions)
	{
		if (!LaneDefinitions.Contains(Definition))
		{
			Children.Insert(MakeShared<FGinkgoWindowLaneTrack>(ObjectId, Definition, GetGinkgoWindowDisplayName(Data, Object, Definition)), LaneDefinitions.Num());
			LaneDefinitions.Add(Definition);
			bChildrenChanged = true;
		}
	}
}

// ===== FGinkgoMovementTrackCreator =====

FName FGinkgoMovementTrackCreator::GetTargetTypeNameInternal() const
{
	return UGinkgoCharacterMovementComponent::StaticClass()->GetFName();
}

FName FGinkgoMovementTrackCreator::GetNameInternal() const
{
	return TEXT("GinkgoMovement");
}

void FGinkgoMovementTrackCreator::GetTrackTypesInternal(TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const
{
	Types.Add({GetNameInternal(), LOCTEXT("TrackType", "Ginkgo 移动")});
}

TSharedPtr<RewindDebugger::FRewindDebuggerTrack> FGinkgoMovementTrackCreator::CreateTrackInternal(const RewindDebugger::FObjectId& InObjectId) const
{
	return MakeShared<FGinkgoMovementTrack>(InObjectId.GetMainId());
}

bool FGinkgoMovementTrackCreator::HasDebugInfoInternal(const RewindDebugger::FObjectId& InObjectId) const
{
	return ReadTraceObject(InObjectId.GetMainId(), [](const FGinkgoMovementTraceData&, const FGinkgoTraceObject&) {});
}

// ===== FGinkgoMovementRewindDebuggerRuntimeExtension =====

void FGinkgoMovementRewindDebuggerRuntimeExtension::RecordingStarted()
{
#if GINKGO_MOVEMENT_TRACE_ENABLED
	UE::Trace::ToggleChannel(FGinkgoMovementTrace::ChannelName, true);
#endif
}

void FGinkgoMovementRewindDebuggerRuntimeExtension::RecordingStopped()
{
#if GINKGO_MOVEMENT_TRACE_ENABLED
	UE::Trace::ToggleChannel(FGinkgoMovementTrace::ChannelName, false);
#endif
}

#undef LOCTEXT_NAMESPACE

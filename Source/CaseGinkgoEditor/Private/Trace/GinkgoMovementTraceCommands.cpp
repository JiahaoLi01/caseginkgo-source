#include "Engine/TextureRenderTarget2D.h"
#include "GinkgoMovementRewindDebugger.h"
#include "GinkgoMovementTraceProvider.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "TextureResource.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "UObject/StrongObjectPtr.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// 移动追踪的两个编辑器命令：导出 CSV（测量脚本逐帧比对用）与画成图片（不开 Rewind Debugger 检查轨道的样子）

namespace
{
	const TCHAR* MarkerKindName(const EGinkgoTraceMarkerKind Kind)
	{
		switch (Kind)
		{
		case EGinkgoTraceMarkerKind::WindowEvent:
			return TEXT("event");
		case EGinkgoTraceMarkerKind::WindowChange:
			return TEXT("change");
		default:
			return TEXT("correction");
		}
	}

	/** 同步分析一个追踪文件；分析不了时打日志并返回空。大文件会卡住编辑器一会儿 */
	TSharedPtr<const TraceServices::IAnalysisSession> AnalyzeTraceFile(const FString& Path)
	{
		ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
		const TSharedPtr<TraceServices::IAnalysisService> AnalysisService = TraceServicesModule.GetAnalysisService();
		TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalysisService.IsValid() ? AnalysisService->Analyze(*Path) : nullptr;
		if (!Session.IsValid())
		{
			UE_LOG(LogGinkgoMovement, Error, TEXT("[Trace] 分析不了 %s"), *Path);
		}
		return Session;
	}

	const FGinkgoMovementTraceProvider* ReadProvider(const TraceServices::IAnalysisSession& Session, const FString& Path)
	{
		const FGinkgoMovementTraceProvider* Provider = Session.ReadProvider<FGinkgoMovementTraceProvider>(FGinkgoMovementTraceProvider::ProviderName);
		if (!Provider)
		{
			UE_LOG(LogGinkgoMovement, Error, TEXT("[Trace] %s 里没有移动追踪的数据提供者（编辑器模块的分析模块没有注册上？）"), *Path);
		}
		return Provider;
	}

	FString ResolveOutputPath(const FString& Path)
	{
		return FPaths::IsRelative(Path) ? FPaths::ProjectDir() / Path : Path;
	}

	// ===== ginkgo.trace.dumpWindows =====

	/**
	 * @brief 分析一个追踪文件，把移动追踪的段与点写成 CSV
	 * @note 每个移动组件一行 summary（快照数与其中重放的个数），之后是段（segment）与点（event、change、correction）
	 */
	void DumpMovementTrace(const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogGinkgoMovement, Warning, TEXT("[Trace] 用法：ginkgo.trace.dumpWindows <追踪文件> <输出 CSV>"));
			return;
		}
		const TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalyzeTraceFile(Args[0]);
		if (!Session.IsValid())
		{
			return;
		}

		TStringBuilder<65536> Csv;
		Csv.Append(TEXT("object,kind,start_time,end_time,start_frame,end_frame,definition,window,phase,phase_tag,count,started_by_change,ended_by_change,replay,detail\n"));
		int32 ObjectCount = 0;
		int32 SnapshotCount = 0;
		int32 ReplayCount = 0;
		int32 SegmentCount = 0;
		int32 MarkerCount = 0;
		{
			TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
			const FGinkgoMovementTraceProvider* Provider = ReadProvider(*Session, Args[0]);
			if (!Provider)
			{
				return;
			}
			const FGinkgoMovementTraceData& Data = Provider->Read();
			for (const TPair<uint64, FGinkgoTraceObject>& Entry : Data.GetObjects())
			{
				++ObjectCount;
				int32 ObjectReplays = 0;
				for (const FGinkgoTraceSnapshot& Snapshot : Entry.Value.Snapshots)
				{
					ObjectReplays += Snapshot.bReplay ? 1 : 0;
				}
				SnapshotCount += Entry.Value.Snapshots.Num();
				ReplayCount += ObjectReplays;
				Csv.Appendf(TEXT("%llu,summary,0,0,0,0,0,,0,,0,0,0,0,snapshots=%d replays=%d\n"), Entry.Key, Entry.Value.Snapshots.Num(), ObjectReplays);
				for (const FGinkgoTraceSegment& Segment : Entry.Value.Segments)
				{
					++SegmentCount;
					Csv.Appendf(TEXT("%llu,segment,%.6f,%.6f,%llu,%llu,%d,%s,%d,%s,%d,%d,%d,0,\n"), Entry.Key, Segment.StartTime, Segment.IsOpen() ? -1.0 : Segment.EndTime,
						Segment.StartFrame, Segment.IsOpen() ? 0ull : Segment.EndFrame, Segment.Definition, *Data.GetName(Segment.WindowTag).ToString(), Segment.Phase,
						*Data.GetName(Segment.PhaseTag).ToString(), Segment.Count, Segment.bStartedByChange ? 1 : 0, Segment.bEndedByChange ? 1 : 0);
				}
				for (const FGinkgoTraceMarker& Marker : Entry.Value.Markers)
				{
					++MarkerCount;
					FString Detail;
					switch (Marker.Kind)
					{
					case EGinkgoTraceMarkerKind::WindowEvent:
						Detail = FString::Printf(TEXT("type=%d value=%.3f flag=%d"), Marker.EventType, Marker.Value, Marker.bFlag ? 1 : 0);
						break;
					case EGinkgoTraceMarkerKind::WindowChange:
						Detail = FString::Printf(TEXT("from=%d to=%d dispatched=%d"), Marker.FromPhase, Marker.ToPhase, Marker.bDispatched ? 1 : 0);
						break;
					default:
						Detail = FString::Printf(TEXT("error=%.3f"), Marker.PositionError);
						break;
					}
					Csv.Appendf(TEXT("%llu,%s,%.6f,%.6f,%llu,%llu,%d,%s,%d,%s,1,0,0,%d,%s\n"), Entry.Key, MarkerKindName(Marker.Kind), Marker.Time, Marker.Time, Marker.Frame,
						Marker.Frame, Marker.Definition, *Data.GetName(Marker.WindowTag).ToString(), Marker.ToPhase, *Data.GetName(Marker.ToPhaseTag).ToString(),
						Marker.bReplay ? 1 : 0, *Detail);
				}
			}
		}
		const FString OutputPath = ResolveOutputPath(Args[1]);
		const bool bSaved = FFileHelper::SaveStringToFile(Csv.ToString(), *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogGinkgoMovement, Log, TEXT("[Trace] %s：%d 个移动组件，%d 个快照（其中重放 %d 个），%d 段，%d 个点；%s %s"), *Args[0], ObjectCount, SnapshotCount,
			ReplayCount, SegmentCount, MarkerCount, bSaved ? TEXT("写到") : TEXT("写入失败："), *OutputPath);
	}

	FAutoConsoleCommand DumpMovementTraceCommand(
		TEXT("ginkgo.trace.dumpWindows"),
		TEXT("分析一个追踪文件，把移动追踪的段与点写成 CSV：ginkgo.trace.dumpWindows <追踪文件> <输出 CSV>。\n")
		TEXT("段与 Rewind Debugger 里窗口轨道上的格子相同（按第一次模拟的快照切），点是窗口事件、窗口变化与校正。"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpMovementTrace));

	// ===== ginkgo.trace.renderTimeline =====

	/** 图里一条轨道：左边名字，右边时间轴 */
	TSharedRef<SWidget> MakeLaneRow(const FText& Name, const TSharedRef<FGinkgoTraceTimelineData>& Timeline, const TRange<double>& ViewRange, const double Cursor)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(220.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.85f))
					.Text(Name)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.09f, 0.09f, 0.1f))
				.Padding(0.f)
				[
					SNew(SBox)
					.HeightOverride(24.f)
					[
						MakeGinkgoTraceTimeline(Timeline, ViewRange, Cursor)
					]
				]
			];
	}

	/**
	 * @brief 把一个追踪文件里"Ginkgo 移动"的轨道画成图片
	 * @note 与 Rewind Debugger 用同样的数据与控件，只是离屏画出来，竖线是拖到的时刻，下面是那一刻的详情。要渲染，不能 -nullrhi
	 */
	void RenderMovementTrace(const TArray<FString>& Args)
	{
		if (Args.Num() < 5)
		{
			UE_LOG(LogGinkgoMovement, Warning, TEXT("[Trace] 用法：ginkgo.trace.renderTimeline <追踪文件> <输出 PNG> <开始秒> <结束秒> <拖到的秒>"));
			return;
		}
		const TRange<double> ViewRange(FCString::Atod(*Args[2]), FCString::Atod(*Args[3]));
		const double Cursor = FCString::Atod(*Args[4]);
		const TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalyzeTraceFile(Args[0]);
		if (!Session.IsValid())
		{
			return;
		}

		const TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		FString Details;
		{
			TraceServices::FAnalysisSessionReadScope ReadScope(*Session);
			const FGinkgoMovementTraceProvider* Provider = ReadProvider(*Session, Args[0]);
			if (!Provider)
			{
				return;
			}
			const FGinkgoMovementTraceData& Data = Provider->Read();
			// 快照最多的那个组件：联网的客户端上是本地角色
			const FGinkgoTraceObject* Object = nullptr;
			for (const TPair<uint64, FGinkgoTraceObject>& Entry : Data.GetObjects())
			{
				if (!Object || Entry.Value.Snapshots.Num() > Object->Snapshots.Num())
				{
					Object = &Entry.Value;
				}
			}
			if (!Object)
			{
				UE_LOG(LogGinkgoMovement, Error, TEXT("[Trace] %s 里没有移动组件的数据"), *Args[0]);
				return;
			}
			Rows->AddSlot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.ColorAndOpacity(FLinearColor::White)
					.Text(FText::FromString(FString::Printf(TEXT("%s    %.2f – %.2f s，白色竖线 = 拖到的 %.3f s"), *FPaths::GetCleanFilename(Args[0]),
						ViewRange.GetLowerBoundValue(), ViewRange.GetUpperBoundValue(), Cursor)))
				];
			const auto AddLane = [&Rows, &ViewRange, Cursor](const FText& Name, const TSharedRef<FGinkgoTraceTimelineData>& Timeline)
			{
				Rows->AddSlot().AutoHeight().Padding(0.f, 2.f)[MakeLaneRow(Name, Timeline, ViewRange, Cursor)];
			};
			const TSharedRef<FGinkgoTraceTimelineData> Overview = MakeShared<FGinkgoTraceTimelineData>();
			BuildGinkgoMovementPoints(Data, *Object, ViewRange.GetLowerBoundValue(), ViewRange.GetUpperBoundValue(), true, *Overview);
			AddLane(FText::FromString(TEXT("Ginkgo 移动（总览）")), Overview);
			for (const uint8 Definition : Object->Definitions)
			{
				const TSharedRef<FGinkgoTraceTimelineData> Lane = MakeShared<FGinkgoTraceTimelineData>();
				BuildGinkgoWindowLane(Data, *Object, Definition, ViewRange.GetLowerBoundValue(), ViewRange.GetUpperBoundValue(), *Lane);
				AddLane(FText::FromString(TEXT("  ") + GetGinkgoWindowDisplayName(Data, *Object, Definition).ToString()), Lane);
			}
			const TSharedRef<FGinkgoTraceTimelineData> Events = MakeShared<FGinkgoTraceTimelineData>();
			BuildGinkgoMovementPoints(Data, *Object, ViewRange.GetLowerBoundValue(), ViewRange.GetUpperBoundValue(), false, *Events);
			AddLane(FText::FromString(TEXT("  事件")), Events);
			Details = DescribeGinkgoMovementAt(Data, *Object, Cursor);
		}
		Rows->AddSlot()
			.AutoHeight()
			.Padding(0.f, 12.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				.ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.9f))
				.Text(FText::FromString(Details))
			];
		const TSharedRef<SWidget> Root = SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.025f))
			.Padding(14.f)
			[
				Rows
			];

		// 先排版，按要的高度画
		constexpr float Width = 1400.f;
		Root->SlatePrepass(1.f);
		const FVector2D DrawSize(Width, FMath::CeilToFloat(Root->GetDesiredSize().Y));
		// 不做伽马校正：渲染目标本身按 sRGB 存，再校正一次颜色会发白
		FWidgetRenderer Renderer(false);
		const TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, false));
		Renderer.DrawWidget(Target.Get(), Root, DrawSize, 0.f);
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels) || Pixels.Num() != static_cast<int32>(DrawSize.X * DrawSize.Y))
		{
			UE_LOG(LogGinkgoMovement, Error, TEXT("[Trace] 读不回渲染结果（-nullrhi 时没有渲染）"));
			return;
		}
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
		}
		const FString OutputPath = ResolveOutputPath(Args[1]);
		const bool bSaved = FImageUtils::SaveImageAutoFormat(*OutputPath, FImageView(Pixels.GetData(), static_cast<int32>(DrawSize.X), static_cast<int32>(DrawSize.Y)));
		UE_LOG(LogGinkgoMovement, Log, TEXT("[Trace] %s：%.0f × %.0f，%s %s"), *Args[0], DrawSize.X, DrawSize.Y, bSaved ? TEXT("画到") : TEXT("写入失败："), *OutputPath);
	}

	FAutoConsoleCommand RenderMovementTraceCommand(
		TEXT("ginkgo.trace.renderTimeline"),
		TEXT("把追踪文件里\"Ginkgo 移动\"的轨道画成图片：ginkgo.trace.renderTimeline <追踪文件> <输出 PNG> <开始秒> <结束秒> <拖到的秒>。\n")
		TEXT("与 Rewind Debugger 用同样的数据与控件，离屏画出来，下面附上拖到的那一刻的详情。要渲染（不能 -nullrhi，可以 -RenderOffscreen）。"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RenderMovementTrace));
}

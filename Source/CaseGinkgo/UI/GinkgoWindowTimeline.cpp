#include "GinkgoWindowTimeline.h"

#include "Algo/Compare.h"
#include "Control/GinkgoPlayerController.h"              // ginkgo.showWindowTimeline 找本地控制器
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"                // 视口的 DPI 缩放：页面第一次布局用的大小
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/Windows/GinkgoMovementWindow.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "SWeftViewport.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

DECLARE_CYCLE_STAT(TEXT("Window timeline"), STAT_GinkgoWindowTimeline, STATGROUP_Ginkgo3C);

namespace
{
	using FJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	using FJsonWriterFactory = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	using FChange = FGinkgoMovementWindowManager::FChange;

	/** 页面的目录：Content/UI 随包（DefaultGame.ini 的 DirectoriesToAlwaysStageAsUFS），打包后从 pak 读 */
	const TCHAR* PageDirectory = TEXT("UI/WindowTimeline");

	const TCHAR* CommandName = TEXT("ginkgo.showWindowTimeline");

	/** 控件在视口上的层级：在游戏的其他界面之上 */
	constexpr int32 WidgetZOrder = 20;

	/** 轨道上显示最近几秒：现在的位置在轨道宽度的 82% 处，左边是这几秒 */
	constexpr float HistorySeconds = 4.f;

	/** 标签的最后一节（Movement.Window.Probe.A → A） */
	FString TagLeaf(const FGameplayTag& Tag)
	{
		const FString Name = Tag.ToString();
		int32 Dot = INDEX_NONE;
		return Name.FindLastChar(TEXT('.'), Dot) ? Name.RightChop(Dot + 1) : Name;
	}

	/** 事件的枚举名（如 Jumped），页面按它取显示的文字：枚举的显示名是元数据，打包的游戏里没有 */
	FString EventName(const EGinkgoWindowEvent Type)
	{
		return StaticEnum<EGinkgoWindowEvent>()->GetNameStringByValue(static_cast<int64>(Type));
	}

	const TCHAR* LinkKindName(const FGinkgoWindowTimelineModel::ELinkKind Kind)
	{
		switch (Kind)
		{
		case FGinkgoWindowTimelineModel::ELinkKind::Push:
			return TEXT("push");
		case FGinkgoWindowTimelineModel::ELinkKind::End:
			return TEXT("end");
		default:
			return TEXT("phase");
		}
	}

	/** 一条消息：带 op 的 JSON 对象，其余字段由 Body 写 */
	template <typename TBody>
	FString WriteMessage(const TCHAR* Op, TBody&& Body)
	{
		FString Json;
		const TSharedRef<FJsonWriter> Writer = FJsonWriterFactory::Create(&Json);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("op"), Op);
		Body(*Writer);
		Writer->WriteObjectEnd();
		Writer->Close();
		return Json;
	}

	void WriteEventNames(FJsonWriter& Writer, const TCHAR* Identifier, const TArray<EGinkgoWindowEvent>& Events)
	{
		Writer.WriteArrayStart(Identifier);
		for (const EGinkgoWindowEvent Event : Events)
		{
			Writer.WriteValue(EventName(Event));
		}
		Writer.WriteArrayEnd();
	}

	/** 完整的标签名，页面按它取显示的文字：标签的注释是编辑器的数据，打包的游戏里没有 */
	void WriteTags(FJsonWriter& Writer, const TCHAR* Identifier, const FGameplayTagContainer& Tags)
	{
		Writer.WriteArrayStart(Identifier);
		for (const FGameplayTag& Tag : Tags)
		{
			Writer.WriteValue(Tag.ToString());
		}
		Writer.WriteArrayEnd();
	}

	void WriteMultipliers(FJsonWriter& Writer, const TArray<FGinkgoWindowMultiplier>& Multipliers)
	{
		Writer.WriteArrayStart(TEXT("multipliers"));
		for (const FGinkgoWindowMultiplier& Multiplier : Multipliers)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("category"), Multiplier.Category.ToString());
			Writer.WriteValue(TEXT("phase"), Multiplier.Phase);
			// 常量写出数值（空着时为 1）；曲线与以后的自定义计算随时间变，不写
			if (const UGinkgoWindowValueConstant* Constant = Cast<UGinkgoWindowValueConstant>(Multiplier.Value))
			{
				Writer.WriteValue(TEXT("value"), Constant->Value);
			}
			else if (!Multiplier.Value)
			{
				Writer.WriteValue(TEXT("value"), 1.f);
			}
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
	}

	FAutoConsoleCommandWithWorldAndArgs CCmdShowWindowTimeline(
		CommandName,
		TEXT("窗口时间轴：ginkgo.showWindowTimeline [0|1]，不带参数时切换。\n")
		TEXT("屏幕下方一条时间轴：最上面是窗口事件，下面每种窗口一条轨道、一个阶段一段，事件与它推入、结束、换阶段的窗口之间画连线，一条因果链一种颜色。")
		TEXT("开发版与发行版都有；发行版没有控制台，用 F6，或命令行 -GinkgoWindowTimeline 启动就打开。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AGinkgoPlayerController* PlayerController = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!PlayerController || !PlayerController->IsLocalController())
			{
				UE_LOG(LogGinkgoUI, Warning, TEXT("[Timeline] %s：这个世界里没有本地的玩家控制器"), CommandName);
				return;
			}
			FGinkgoWindowTimeline& Timeline = PlayerController->GetOrCreateWindowTimeline();
			if (Args.IsEmpty())
			{
				Timeline.Toggle();
			}
			else if (FCString::Atoi(*Args[0]) != 0)
			{
				Timeline.Show();
			}
			else
			{
				Timeline.Hide();
			}
		}));
}

FGinkgoWindowTimeline::FGinkgoWindowTimeline(APlayerController& InController)
	: Controller(&InController)
{
}

FGinkgoWindowTimeline::~FGinkgoWindowTimeline()
{
	Hide();
}

FKey FGinkgoWindowTimeline::GetToggleKey()
{
	// F1–F5、F9、F11 是引擎在开发版里的调试键（BaseInput.ini 的 DebugExecBindings），F8 是编辑器里 PIE 的弹出
	return EKeys::F6;
}

bool FGinkgoWindowTimeline::IsRequestedOnCommandLine()
{
	return FParse::Param(FCommandLine::Get(), TEXT("GinkgoWindowTimeline"));
}

bool FGinkgoWindowTimeline::Show()
{
	if (IsShown())
	{
		return true;
	}
	const APlayerController* PlayerController = Controller.Get();
	const UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	UWeftSubsystem* Weft = UWeftSubsystem::Get();
	if (!Viewport || !Weft || !Weft->IsAvailable())
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[Timeline] 打不开窗口时间轴：%s"),
			!Viewport ? TEXT("没有游戏视口（无界面运行）") : TEXT("weft 在这里不运行（-nullrhi，或没有编出 weft 的运行时库）"));
		return false;
	}
	UWeftView* NewView = Weft->CreateView();
	if (!NewView)
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[Timeline] 打不开窗口时间轴：weft 没有给出 View"));
		return false;
	}
	View.Reset(NewView);
	ViewportClient = Viewport;
	// 只显示、不接指针：页面铺满视口，鼠标与键盘照常交给游戏
	Widget = SNew(SWeftViewport).View(NewView).Interactive(false);
	Viewport->AddViewportWidgetContent(Widget.ToSharedRef(), WidgetZOrder);
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FGinkgoWindowTimeline::OnWorldPostActorTick);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FGinkgoWindowTimeline::OnTicker));
	UE_LOG(LogGinkgoUI, Log, TEXT("[Timeline] 打开窗口时间轴（%s 或 %s 0 关闭）"), *GetToggleKey().GetDisplayName().ToString(), CommandName);
	return true;
}

void FGinkgoWindowTimeline::Hide()
{
	if (!IsShown())
	{
		return;
	}
	BindMovement(nullptr);
	FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	PostActorTickHandle.Reset();
	TickerHandle.Reset();
	if (UGameViewportClient* Viewport = ViewportClient.Get(); Viewport && Widget)
	{
		Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
	}
	Widget.Reset();
	View.Reset();
	ViewportClient.Reset();
	Model.Reset();
	PendingMessages.Reset();
	SentDefinitions.Reset();
	bLanesSent = false;
	bLoaded = false;
	LastSampleTime = -1.0;
	bPaused = false;
	PausedAtPageTime = 0.0;
	PausedTotal = 0.0;
	UE_LOG(LogGinkgoUI, Log, TEXT("[Timeline] 关闭窗口时间轴"));
}

void FGinkgoWindowTimeline::Toggle()
{
	if (IsShown())
	{
		Hide();
	}
	else
	{
		Show();
	}
}

float FGinkgoWindowTimeline::GetPanelTop() const
{
	if (!IsShown())
	{
		return 0.f;
	}
	// page.css 的 .wt（离下沿 24 + 上下内边距 12+10 + 顶底边框 2+1）、.wt-head（26 + 下边距 6）、.wt-scale（18），page.js 的 EVENT_ROW、PITCH
	constexpr float Bottom = 24.f;
	constexpr float Chrome = 12.f + 10.f + 2.f + 1.f;
	constexpr float Head = 26.f + 6.f;
	constexpr float EventRow = 72.f;
	constexpr float Pitch = 64.f;
	constexpr float Scale = 18.f;
	return Bottom + Chrome + Head + EventRow + Pitch * FMath::Max(SentDefinitions.Num(), 1) + Scale;
}

void FGinkgoWindowTimeline::OnWorldPostActorTick(UWorld* InWorld, const ELevelTick TickType, const float DeltaSeconds)
{
	const APlayerController* PlayerController = Controller.Get();
	if (!PlayerController || InWorld != PlayerController->GetWorld() || !View)
	{
		return;
	}
	SCOPE_CYCLE_COUNTER(STAT_GinkgoWindowTimeline);

	// 视口有了大小才载入：View 一开始是 1 × 1，页面第一次布局在那么窄的宽度上，文字会折成很长一列
	if (!bLoaded)
	{
		FVector2D Pixels = FVector2D::ZeroVector;
		if (const UGameViewportClient* Viewport = ViewportClient.Get())
		{
			Viewport->GetViewportSize(Pixels);
		}
		if (Pixels.X < 2.0 || Pixels.Y < 2.0)
		{
			return;
		}
		const float DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y)));
		View->Resize(Pixels / DPIScale, DPIScale);
		const FString Directory = FPaths::ProjectContentDir() / PageDirectory;
		const double LoadStart = FPlatformTime::Seconds();
		if (!View->LoadDirectory(Directory))
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Timeline] 页面载入失败：%s（原因见 LogWeft）"), *Directory);
			Hide();
			return;
		}
		UE_LOG(LogGinkgoUI, Log, TEXT("[Timeline] 页面载入 %.1f ms"), (FPlatformTime::Seconds() - LoadStart) * 1000.0);
		bLoaded = true;
		QueueConfig();
	}

	// 跟上控制器现在控制的角色：换了角色时间轴从头开始
	const APawn* Pawn = PlayerController->GetPawn();
	UGinkgoCharacterMovementComponent* Current = Pawn ? Cast<UGinkgoCharacterMovementComponent>(Pawn->GetMovementComponent()) : nullptr;
	if (Current != Movement.Get())
	{
		BindMovement(Current);
	}

	const double Now = GetTimelineTime();
	const FGinkgoMovementWindowManager::FDefinitions Definitions = Current ? Current->GetWindowDefinitions() : FGinkgoMovementWindowManager::FDefinitions();
	const bool bSameDefinitions = SentDefinitions.Num() == Definitions.Num()
		&& Algo::Compare(SentDefinitions, Definitions, [](const TWeakObjectPtr<const UObject>& Sent, const TObjectPtr<UGinkgoMovementWindow>& Definition)
		{
			return Sent.Get() == Definition.Get();
		});
	if (!bLanesSent || !bSameDefinitions)
	{
		Model.Reset();
		QueueLanes(Definitions);
	}
	if (Current)
	{
		// 这一帧的移动从上一次取样时开始；第一次取样往前推一帧
		const double FrameStart = LastSampleTime >= 0.0 ? LastSampleTime : Now - DeltaSeconds;
		FGinkgoWindowTimelineModel::FFrame Frame;
		Model.EndFrame(Now, FrameStart, Current->GetSimState().Windows, Definitions, Frame);
		if (!Frame.IsEmpty())
		{
			QueueFrame(Frame);
		}
	}
	LastSampleTime = Now;
	FlushMessages();
}

bool FGinkgoWindowTimeline::OnTicker(const float DeltaTime)
{
	const APlayerController* PlayerController = Controller.Get();
	const UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!World || !View || !bLoaded)
	{
		return true;
	}
	const bool bWorldPaused = World->IsPaused();
	if (bWorldPaused == bPaused)
	{
		return true;
	}
	if (bWorldPaused)
	{
		PausedAtPageTime = View->GetPageTime();
		UE_LOG(LogGinkgoUI, Log, TEXT("[Timeline] 世界暂停，时间轴停在 %.3f s"), PausedAtPageTime - PausedTotal);
	}
	else
	{
		const double Paused = View->GetPageTime() - PausedAtPageTime;
		PausedTotal += Paused;
		UE_LOG(LogGinkgoUI, Log, TEXT("[Timeline] 世界继续，这次暂停了 %.3f s"), Paused);
	}
	bPaused = bWorldPaused;
	// 暂停时世界不更新，等不到所有 Actor 更新之后那一刻，这里直接交给页面
	QueueClock();
	FlushMessages();
	return true;
}

void FGinkgoWindowTimeline::BindMovement(UGinkgoCharacterMovementComponent* InMovement)
{
	if (UGinkgoCharacterMovementComponent* Old = Movement.Get())
	{
		Old->OnMovementWindowChanged.Remove(WindowChangedHandle);
		Old->OnMovementWindowEvent.Remove(WindowEventHandle);
		Old->OnMovementCorrected.Remove(CorrectedHandle);
	}
	WindowChangedHandle.Reset();
	WindowEventHandle.Reset();
	CorrectedHandle.Reset();
	Movement = InMovement;
	// 轨道在下一次取样时重新发，时间轴从头开始
	bLanesSent = false;
	if (!InMovement)
	{
		return;
	}
	WindowChangedHandle = InMovement->OnMovementWindowChanged.AddLambda([this](const UGinkgoMovementWindow* Definition, int32, int32)
	{
		// 事件引起的变化在 OnMovementWindowEvent 里也有；按时长推进引起的只从这里来
		if (const UGinkgoCharacterMovementComponent* Bound = Movement.Get())
		{
			Model.AddChange(Bound->GetWindowDefinitions().IndexOfByPredicate([Definition](const TObjectPtr<UGinkgoMovementWindow>& Candidate)
			{
				return Candidate.Get() == Definition;
			}));
		}
	});
	WindowEventHandle = InMovement->OnMovementWindowEvent.AddLambda([this](const FGinkgoWindowEvent& Event, const TConstArrayView<FChange> Changes)
	{
		Model.AddEvent(Event, Changes);
	});
	CorrectedHandle = InMovement->OnMovementCorrected.AddLambda([this](const float PositionError)
	{
		Model.AddCorrection(PositionError);
	});
}

double FGinkgoWindowTimeline::GetTimelineTime() const
{
	if (!View)
	{
		return 0.0;
	}
	return (bPaused ? PausedAtPageTime : View->GetPageTime()) - PausedTotal;
}

void FGinkgoWindowTimeline::FlushMessages()
{
	if (!bLoaded || !View || PendingMessages.IsEmpty())
	{
		return;
	}
	const FString Json = TEXT("[") + FString::Join(PendingMessages, TEXT(",")) + TEXT("]");
	PendingMessages.Reset();
	if (!View->Emit(TEXT("timeline"), Json))
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[Timeline] 页面没有收下这一帧的数据（事件队列满了，或 View 已经关闭）"));
	}
}

// ===== 交给页面的消息 =====

void FGinkgoWindowTimeline::QueueConfig()
{
	PendingMessages.Add(WriteMessage(TEXT("config"), [](FJsonWriter& Writer)
	{
		Writer.WriteValue(TEXT("hotkey"), GetToggleKey().GetDisplayName().ToString());
		// 发行版没有控制台：提示里只写键
		Writer.WriteValue(TEXT("command"), ALLOW_CONSOLE ? CommandName : TEXT(""));
		Writer.WriteValue(TEXT("history"), HistorySeconds);
	}));
}

void FGinkgoWindowTimeline::QueueLanes(const FGinkgoMovementWindowManager::FDefinitions Definitions)
{
	PendingMessages.Add(WriteMessage(TEXT("lanes"), [Definitions](FJsonWriter& Writer)
	{
		Writer.WriteArrayStart(TEXT("lanes"));
		for (int32 Index = 0; Index < Definitions.Num(); ++Index)
		{
			const UGinkgoMovementWindow* Definition = Definitions[Index].Get();
			Writer.WriteObjectStart();
			if (!Definition)
			{
				Writer.WriteValue(TEXT("name"), FString::Printf(TEXT("窗口 %d"), Index));
				Writer.WriteValue(TEXT("missing"), true);
				Writer.WriteObjectEnd();
				continue;
			}
			const FString Name = !Definition->DisplayName.IsEmpty() ? Definition->DisplayName.ToString()
				: (Definition->Tag.IsValid() ? TagLeaf(Definition->Tag) : FString::Printf(TEXT("窗口 %d"), Index));
			Writer.WriteValue(TEXT("name"), Name);
			Writer.WriteValue(TEXT("tag"), Definition->Tag.ToString());
			WriteEventNames(Writer, TEXT("push"), Definition->PushEvents);
			// 推入时对事件标志的要求：lurch 只在不疲劳的起跳时推入。页面按事件把它写成"不疲劳的起跳"
			if (Definition->PushFlag != EGinkgoWindowEventFlagFilter::Any)
			{
				Writer.WriteValue(TEXT("pushFlag"), Definition->PushFlag == EGinkgoWindowEventFlagFilter::Set ? TEXT("set") : TEXT("clear"));
			}
			// 有行为的窗口是子类（lurch）：写类名，页面按它取说明
			if (Definition->GetClass() != UGinkgoMovementWindow::StaticClass())
			{
				Writer.WriteValue(TEXT("behavior"), Definition->GetClass()->GetName());
			}
			WriteEventNames(Writer, TEXT("endEvents"), Definition->EndEvents);
			WriteTags(Writer, TEXT("abilities"), Definition->GrantedAbilities);
			WriteTags(Writer, TEXT("blocked"), Definition->BlockedCategories);
			WriteTags(Writer, TEXT("cleared"), Definition->ClearedWindows);
			WriteMultipliers(Writer, Definition->Multipliers);
			Writer.WriteArrayStart(TEXT("phases"));
			for (int32 PhaseIndex = 0; PhaseIndex < Definition->Phases.Num(); ++PhaseIndex)
			{
				const FGinkgoWindowPhase& Phase = Definition->Phases[PhaseIndex];
				// 没有标签的阶段：只有一个时就是窗口本身（土狼时间），用窗口的名字
				const FString Label = Phase.Tag.IsValid() ? TagLeaf(Phase.Tag)
					: (Definition->Phases.Num() == 1 ? Name : FString::Printf(TEXT("阶段 %d"), PhaseIndex));
				Writer.WriteObjectStart();
				Writer.WriteValue(TEXT("label"), Label);
				Writer.WriteValue(TEXT("duration"), Phase.Duration);
				Writer.WriteValue(TEXT("next"), Phase.NextPhase);
				Writer.WriteArrayStart(TEXT("transitions"));
				for (const FGinkgoWindowTransition& Transition : Phase.Transitions)
				{
					Writer.WriteObjectStart();
					Writer.WriteValue(TEXT("event"), EventName(Transition.Event));
					Writer.WriteValue(TEXT("target"), Transition.TargetPhase);
					Writer.WriteObjectEnd();
				}
				Writer.WriteArrayEnd();
				Writer.WriteObjectEnd();
			}
			Writer.WriteArrayEnd();
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
	}));
	SentDefinitions.Reset();
	for (const TObjectPtr<UGinkgoMovementWindow>& Definition : Definitions)
	{
		SentDefinitions.Add(Definition.Get());
	}
	bLanesSent = true;
}

void FGinkgoWindowTimeline::QueueClock()
{
	PendingMessages.Add(WriteMessage(TEXT("clock"), [this](FJsonWriter& Writer)
	{
		Writer.WriteValue(TEXT("paused"), bPaused);
		Writer.WriteValue(TEXT("t"), GetTimelineTime());
		Writer.WriteValue(TEXT("offset"), PausedTotal);
	}));
}

void FGinkgoWindowTimeline::QueueFrame(const FGinkgoWindowTimelineModel::FFrame& Frame)
{
	PendingMessages.Add(WriteMessage(TEXT("frame"), [&Frame](FJsonWriter& Writer)
	{
		Writer.WriteValue(TEXT("t"), Frame.EventTime);
		Writer.WriteArrayStart(TEXT("ends"));
		for (const FGinkgoWindowTimelineModel::FSegmentEnd& End : Frame.Ends)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("id"), End.Id);
			Writer.WriteValue(TEXT("t"), End.Time);
			Writer.WriteValue(TEXT("elapsed"), End.Elapsed);
			Writer.WriteValue(TEXT("ok"), End.bExplained);
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
		Writer.WriteArrayStart(TEXT("starts"));
		for (const FGinkgoWindowTimelineModel::FSegmentStart& Start : Frame.Starts)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("id"), Start.Id);
			Writer.WriteValue(TEXT("lane"), static_cast<int32>(Start.Definition));
			Writer.WriteValue(TEXT("phase"), static_cast<int32>(Start.Phase));
			Writer.WriteValue(TEXT("t"), Start.Time);
			Writer.WriteValue(TEXT("duration"), Start.Duration);
			Writer.WriteValue(TEXT("count"), Start.Count);
			Writer.WriteValue(TEXT("chain"), Start.Chain);
			Writer.WriteValue(TEXT("ok"), Start.bExplained);
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
		Writer.WriteArrayStart(TEXT("events"));
		for (const FGinkgoWindowTimelineModel::FEvent& Event : Frame.Events)
		{
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("type"), EventName(Event.Event.Type));
			Writer.WriteValue(TEXT("value"), Event.Event.Value);
			Writer.WriteValue(TEXT("flag"), Event.Event.bFlag);
			Writer.WriteValue(TEXT("chain"), Event.Chain);
			Writer.WriteArrayStart(TEXT("links"));
			for (const FGinkgoWindowTimelineModel::FLink& Link : Event.Links)
			{
				Writer.WriteObjectStart();
				Writer.WriteValue(TEXT("lane"), static_cast<int32>(Link.Definition));
				Writer.WriteValue(TEXT("kind"), LinkKindName(Link.Kind));
				Writer.WriteObjectEnd();
			}
			Writer.WriteArrayEnd();
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
		Writer.WriteArrayStart(TEXT("corrections"));
		for (const float PositionError : Frame.Corrections)
		{
			Writer.WriteValue(PositionError);
		}
		Writer.WriteArrayEnd();
	}));
}

#include "Misc/AutomationTest.h"
#include "Movement/Windows/GinkgoMovementWindowManager.h"
#include "Movement/Windows/GinkgoMovementWindowSegmenter.h"
#include "UI/GinkgoWindowTimelineModel.h"
#include "Utilities/Enumeration/Tags/TestTags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoWindowTimelineTests
{
	using FSegmenter = FGinkgoWindowSegmenter;
	using FModel = FGinkgoWindowTimelineModel;

	constexpr float Dt = 1.f / 60.f;

	/** 页面的时钟从这里开始：不从 0 开始，开始时刻倒推到负数也看得出来 */
	constexpr double ClockStart = 10.0;

	FGinkgoWindowPhase MakePhase(const FGameplayTag& Tag, const float Duration, const int32 NextPhase = INDEX_NONE,
		const TArray<FGinkgoWindowTransition>& Transitions = {})
	{
		FGinkgoWindowPhase Phase;
		Phase.Tag = Tag;
		Phase.Duration = Duration;
		Phase.NextPhase = NextPhase;
		Phase.Transitions = Transitions;
		return Phase;
	}

	/** 同探针窗口：起跳推入，阶段 0 0.3 s 后换到阶段 1，阶段 1 落地或 1 s 后结束 */
	UGinkgoMovementWindow* MakeProbeLike()
	{
		UGinkgoMovementWindow* Window = NewObject<UGinkgoMovementWindow>(GetTransientPackage());
		Window->Tag = FTestTags::GinkgoTest_Window_A;
		Window->PushEvents = {EGinkgoWindowEvent::Jumped};
		Window->Phases = {MakePhase(FTestTags::GinkgoTest_Window_A_First, 0.3f, 1),
			MakePhase(FTestTags::GinkgoTest_Window_A_Second, 1.f, INDEX_NONE, {{EGinkgoWindowEvent::Landed, INDEX_NONE}})};
		return Window;
	}

	/** 一个阶段 0.05 s（三帧），滑铲开始时推入，再推入时刷新 */
	UGinkgoMovementWindow* MakeShort()
	{
		UGinkgoMovementWindow* Window = NewObject<UGinkgoMovementWindow>(GetTransientPackage());
		Window->Tag = FTestTags::GinkgoTest_Window_B;
		Window->PushEvents = {EGinkgoWindowEvent::SlideStarted};
		Window->Repush = EGinkgoWindowRepush::Refresh;
		Window->Phases = {MakePhase(FGameplayTag(), 0.05f)};
		return Window;
	}

	/**
	 * 一个管理器、一份窗口状态与一个时间轴，照移动组件的顺序喂：事件先经管理器、发出变化、再发事件；移动结束时按 Δt 推进、发出变化；
	 * 然后在所有 Actor 更新之后取样。页面的时钟每帧也走 Δt
	 */
	struct FHarness
	{
		TArray<TObjectPtr<UGinkgoMovementWindow>> Definitions;
		FGinkgoMovementWindowManager Manager;
		FGinkgoMovementSimState::FWindows Windows;
		FModel Model;
		int32 Frame = 0;

		double TimeOf(const int32 FrameIndex) const
		{
			return ClockStart + FrameIndex * static_cast<double>(Dt);
		}

		/** 这一帧的移动里发出一个事件（OnMovementWindowChanged 之后 OnMovementWindowEvent） */
		void Send(const EGinkgoWindowEvent Type, const float Value = 0.f, const bool bFlag = false)
		{
			const FGinkgoWindowEvent Event{Type, Value, bFlag};
			FGinkgoWindowEventContext Context;
			Manager.HandleEvent(Definitions, Windows, Event, Context);
			const TArray<FGinkgoMovementWindowManager::FChange, TInlineAllocator<8>> Changes = Manager.TakeChanges();
			for (const FGinkgoMovementWindowManager::FChange& Change : Changes)
			{
				Model.AddChange(Change.Definition);
			}
			Model.AddEvent(Event, Changes);
		}

		/** 这一帧的移动结束（按 Δt 推进）并取样 */
		FModel::FFrame EndFrame()
		{
			Manager.Advance(Definitions, Windows, Dt);
			for (const FGinkgoMovementWindowManager::FChange& Change : Manager.TakeChanges())
			{
				Model.AddChange(Change.Definition);
			}
			return Sample();
		}

		/** 只取样，不推进：校正整体换掉窗口状态的那一帧 */
		FModel::FFrame Sample()
		{
			++Frame;
			FModel::FFrame Out;
			Model.EndFrame(TimeOf(Frame), TimeOf(Frame - 1), Windows, Definitions, Out);
			return Out;
		}
	};

	bool HasLink(const FModel::FEvent& Event, const uint8 Definition, const FModel::ELinkKind Kind)
	{
		return Event.Links.ContainsByPredicate([&](const FModel::FLink& Link) { return Link.Definition == Definition && Link.Kind == Kind; });
	}
}

// ===== 切段规则 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowSegmenterTest, "CaseGinkgo.Movement.Window.Segmenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowSegmenterTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWindowTimelineTests;
	FSegmenter Segmenter;
	FSegmenter::FEnds Ends;
	FSegmenter::FPresences Present;

	// 推入：有变化
	Segmenter.NotifyChanged(0);
	Segmenter.Update({{0, 0, Dt}}, Ends, Present);
	TestEqual(TEXT("推入：没有结束的段"), Ends.Num(), 0);
	TestTrue(TEXT("推入：开了一段，由变化引起"), Present.Num() == 1 && Present[0].bStarted && Present[0].bByChange && Present[0].Index == 0);

	// 同一阶段、计时变大、没有变化：接着原来的段
	Segmenter.Update({{0, 0, 2.f * Dt}}, Ends, Present);
	TestTrue(TEXT("接着：不结束也不开新的"), Ends.IsEmpty() && Present.Num() == 1 && !Present[0].bStarted);

	// 换阶段、没有变化（例如校正）：旧的段结束、新的开始，都不是变化引起的；结束的段带着上一份快照的计时
	Segmenter.Update({{0, 1, 0.01f}}, Ends, Present);
	TestTrue(TEXT("换阶段：旧的段结束"), Ends.Num() == 1 && Ends[0].Phase == 0 && !Ends[0].bByChange && Ends[0].LastPhaseTime == 2.f * Dt);
	TestTrue(TEXT("换阶段：新的段开始"), Present.Num() == 1 && Present[0].bStarted && !Present[0].bByChange);

	// 计时倒退、没有变化：从头开始
	Segmenter.Update({{0, 1, 0.005f}}, Ends, Present);
	TestTrue(TEXT("计时倒退：结束再开始"), Ends.Num() == 1 && Present.Num() == 1 && Present[0].bStarted);

	// 刷新：阶段与计时都看不出来，靠变化
	Segmenter.NotifyChanged(0);
	Segmenter.Update({{0, 1, 0.006f}}, Ends, Present);
	TestTrue(TEXT("刷新：结束与开始都由变化引起"), Ends.Num() == 1 && Ends[0].bByChange && Present.Num() == 1 && Present[0].bStarted && Present[0].bByChange);

	// 叠加：同一种窗口两个，段跟着第一个，记下个数；另一种窗口另开一段
	Segmenter.Update({{0, 1, 0.007f}, {1, 0, Dt}, {0, 0, Dt}}, Ends, Present);
	TestEqual(TEXT("叠加：快照里两种窗口"), Present.Num(), 2);
	if (Present.Num() == 2)
	{
		TestTrue(TEXT("叠加：第一种接着原来的段，有两个"), Present[0].Definition == 0 && !Present[0].bStarted && Present[0].Count == 2 && Present[0].Index == 0);
		TestTrue(TEXT("叠加：第二种开了新的段"), Present[1].Definition == 1 && Present[1].bStarted && Present[1].Index == 1);
	}

	// 不在快照里了：结束
	Segmenter.Update({}, Ends, Present);
	TestTrue(TEXT("都结束"), Ends.Num() == 2 && Present.IsEmpty() && Segmenter.NumOpenSegments() == 0);
	return true;
}

// ===== 时间轴：时刻、链与连线 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowTimelineTimesTest, "CaseGinkgo.UI.WindowTimeline.Times",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowTimelineTimesTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWindowTimelineTests;
	FHarness Harness;
	Harness.Definitions = {MakeProbeLike(), MakeShort()};
	TestTrue(TEXT("开头一帧什么也没有"), Harness.EndFrame().IsEmpty());

	// 起跳推入：段与事件都在这一帧的移动开始时，属于事件的链，连线是推入
	Harness.Send(EGinkgoWindowEvent::Jumped);
	const FModel::FFrame Jump = Harness.EndFrame();
	const int32 JumpFrame = Harness.Frame;
	if (!TestTrue(TEXT("起跳：一段一事件"), Jump.Starts.Num() == 1 && Jump.Events.Num() == 1 && Jump.Ends.IsEmpty()))
	{
		return false;
	}
	const FModel::FSegmentStart First = Jump.Starts[0];
	TestEqual(TEXT("起跳：事件在移动开始时"), Jump.EventTime, Harness.TimeOf(JumpFrame - 1));
	TestEqual(TEXT("起跳：段与事件同一点"), First.Time, Jump.EventTime);
	TestTrue(TEXT("起跳：阶段 0，0.3 s，有变化"), First.Definition == 0 && First.Phase == 0 && First.Duration == 0.3f && First.bExplained);
	TestTrue(TEXT("起跳：段与事件同一条链"), First.Chain == Jump.Events[0].Chain && First.Chain >= 0);
	TestTrue(TEXT("起跳：连线是推入"), HasLink(Jump.Events[0], 0, FModel::ELinkKind::Push) && Jump.Events[0].Links.Num() == 1);

	// 按时长换阶段：管理器在第 18 次推进（0.3 s，推入那次移动也算一整段）时换，旧的段在新的段开始处结束
	FModel::FFrame Switch;
	for (int32 Step = 0; Step < 30 && Switch.Ends.IsEmpty(); ++Step)
	{
		Switch = Harness.EndFrame();
	}
	TestEqual(TEXT("换阶段：第 18 次推进"), Harness.Frame - JumpFrame + 1, 18);
	if (!TestTrue(TEXT("换阶段：一段结束一段开始"), Switch.Ends.Num() == 1 && Switch.Starts.Num() == 1))
	{
		return false;
	}
	const FModel::FSegmentStart Second = Switch.Starts[0];
	TestEqual(TEXT("换阶段：旧的段"), Switch.Ends[0].Id, First.Id);
	TestEqual(TEXT("换阶段：标的是阶段时长"), Switch.Ends[0].Elapsed, 0.3f);
	TestEqual(TEXT("换阶段：新的段从现在减去带过来的时间开始"), Second.Time, Harness.TimeOf(Harness.Frame) - Harness.Windows[0].PhaseTime, 1e-9);
	TestEqual(TEXT("换阶段：旧的段在同一点结束"), Switch.Ends[0].Time, Second.Time);
	TestEqual(TEXT("换阶段：画出来的长度就是 0.3 s"), Second.Time - First.Time, 0.3, 1e-6);
	TestTrue(TEXT("换阶段：阶段 1，接着用原来的链，有变化"), Second.Phase == 1 && Second.Chain == First.Chain && Second.bExplained && Switch.Ends[0].bExplained);
	TestTrue(TEXT("换阶段：没有事件"), Switch.Events.IsEmpty());

	// 落地结束：段在这一帧的移动开始时结束，标的是上一帧的已过时间；落地开一条新的链，连线是结束
	for (int32 Step = 0; Step < 5; ++Step)
	{
		TestTrue(TEXT("阶段 1 中间没有变化"), Harness.EndFrame().IsEmpty());
	}
	const float PhaseTimeBeforeLanding = Harness.Windows[0].PhaseTime;
	Harness.Send(EGinkgoWindowEvent::Landed);
	const FModel::FFrame Land = Harness.EndFrame();
	if (!TestTrue(TEXT("落地：一段结束、一个事件"), Land.Ends.Num() == 1 && Land.Starts.IsEmpty() && Land.Events.Num() == 1))
	{
		return false;
	}
	TestEqual(TEXT("落地：结束的是阶段 1"), Land.Ends[0].Id, Second.Id);
	TestEqual(TEXT("落地：在移动开始时结束"), Land.Ends[0].Time, Harness.TimeOf(Harness.Frame - 1));
	TestEqual(TEXT("落地：标的是落地之前的已过时间"), Land.Ends[0].Elapsed, PhaseTimeBeforeLanding);
	TestTrue(TEXT("落地：新的链，连线是结束"), Land.Events[0].Chain > First.Chain && HasLink(Land.Events[0], 0, FModel::ELinkKind::End));
	TestEqual(TEXT("落地：没有开着的段"), Harness.Model.NumOpenSegments(), 0);

	// 没有引起变化的事件不属于任何链；同一次移动里的两个事件按先后，各自一条链
	Harness.Send(EGinkgoWindowEvent::SlideEnded);
	const FModel::FFrame Idle = Harness.EndFrame();
	TestTrue(TEXT("没有作用的事件：没有链、没有连线"), Idle.Events.Num() == 1 && Idle.Events[0].Chain == INDEX_NONE && Idle.Events[0].Links.IsEmpty());
	Harness.Send(EGinkgoWindowEvent::Landed);
	Harness.Send(EGinkgoWindowEvent::SlideStarted, 438.f, true);
	const FModel::FFrame Slide = Harness.EndFrame();
	if (!TestTrue(TEXT("同一次移动：两个事件、一段"), Slide.Events.Num() == 2 && Slide.Starts.Num() == 1))
	{
		return false;
	}
	TestTrue(TEXT("同一次移动：按先后"), Slide.Events[0].Event.Type == EGinkgoWindowEvent::Landed && Slide.Events[1].Event.Type == EGinkgoWindowEvent::SlideStarted);
	TestTrue(TEXT("同一次移动：落地没有作用，滑铲开始推入"), Slide.Events[0].Chain == INDEX_NONE && HasLink(Slide.Events[1], 1, FModel::ELinkKind::Push));
	TestEqual(TEXT("同一次移动：事件带着数值"), Slide.Events[1].Event.Value, 438.f);
	const FModel::FSegmentStart Short = Slide.Starts[0];
	TestTrue(TEXT("同一次移动：段属于滑铲开始的链"), Short.Definition == 1 && Short.Chain == Slide.Events[1].Chain && Short.Time == Slide.EventTime);

	// 按时长结束：三帧（0.05 s）后结束，结束的时刻是开始加阶段时长
	FModel::FFrame Expire;
	for (int32 Step = 0; Step < 10 && Expire.Ends.IsEmpty(); ++Step)
	{
		Expire = Harness.EndFrame();
	}
	if (TestTrue(TEXT("按时长结束：一段结束"), Expire.Ends.Num() == 1 && Expire.Starts.IsEmpty()))
	{
		TestEqual(TEXT("按时长结束：标的是阶段时长"), Expire.Ends[0].Elapsed, 0.05f);
		TestEqual(TEXT("按时长结束：开始加 0.05 s"), Expire.Ends[0].Time, Short.Time + 0.05, 1e-6);
		TestTrue(TEXT("按时长结束：不早于这一帧的移动开始"), Expire.Ends[0].Time >= Harness.TimeOf(Harness.Frame - 1));
	}

	// 刷新：活动中再推入，旧的段在事件处结束、新的段从事件处开始，属于新事件的链；连线是换阶段（刷新回到阶段 0）
	Harness.Send(EGinkgoWindowEvent::SlideStarted, 438.f, true);
	const FModel::FFrame Push = Harness.EndFrame();
	TestTrue(TEXT("刷新前：推入一段"), Push.Starts.Num() == 1);
	const float PhaseTimeBeforeRefresh = Harness.Windows[0].PhaseTime;
	Harness.Send(EGinkgoWindowEvent::SlideStarted, 200.f, true);
	const FModel::FFrame Refresh = Harness.EndFrame();
	if (TestTrue(TEXT("刷新：一段结束一段开始"), Refresh.Ends.Num() == 1 && Refresh.Starts.Num() == 1 && Refresh.Events.Num() == 1))
	{
		TestEqual(TEXT("刷新：在事件处结束"), Refresh.Ends[0].Time, Refresh.EventTime);
		TestEqual(TEXT("刷新：标的是刷新之前的已过时间"), Refresh.Ends[0].Elapsed, PhaseTimeBeforeRefresh);
		TestTrue(TEXT("刷新：新的段在事件处、属于新的链"), Refresh.Starts[0].Time == Refresh.EventTime && Refresh.Starts[0].Chain == Refresh.Events[0].Chain
			&& Refresh.Starts[0].Chain != Push.Starts[0].Chain);
		TestTrue(TEXT("刷新：连线是换阶段"), HasLink(Refresh.Events[0], 1, FModel::ELinkKind::Phase));
	}
	return true;
}

// ===== 时间轴：校正与开时间轴之前就在的窗口 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowTimelineCorrectionTest, "CaseGinkgo.UI.WindowTimeline.Correction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowTimelineCorrectionTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWindowTimelineTests;
	FHarness Harness;
	Harness.Definitions = {MakeProbeLike(), MakeShort()};

	// 开时间轴时窗口已经在：从它真正开始的时刻画起（不受这一帧的限制），不算预测错，不属于任何链
	Harness.Windows.Add({0, 1, 0.5f, 0.f});
	const FModel::FFrame Opened = Harness.Sample();
	if (TestTrue(TEXT("已经在：开一段"), Opened.Starts.Num() == 1))
	{
		TestEqual(TEXT("已经在：从 0.5 s 之前开始"), Opened.Starts[0].Time, Harness.TimeOf(Harness.Frame) - 0.5, 1e-6);
		TestTrue(TEXT("已经在：算作有变化、不属于任何链"), Opened.Starts[0].bExplained && Opened.Starts[0].Chain == INDEX_NONE);
	}

	// 校正把窗口去掉（整体换成服务器的状态，没有变化）：段在这一帧的移动开始时结束，标作没有对应的变化；这一帧带着校正
	Harness.Windows.Reset();
	Harness.Model.AddCorrection(0.05f);
	const FModel::FFrame Removed = Harness.Sample();
	if (TestTrue(TEXT("校正去掉：一段结束"), Removed.Ends.Num() == 1))
	{
		TestFalse(TEXT("校正去掉：没有对应的变化"), Removed.Ends[0].bExplained);
		TestEqual(TEXT("校正去掉：在移动开始时结束"), Removed.Ends[0].Time, Harness.TimeOf(Harness.Frame - 1));
	}
	TestTrue(TEXT("校正去掉：带着校正的位置差"), Removed.Corrections.Num() == 1 && Removed.Corrections[0] == 0.05f);

	// 校正加上窗口：从模拟给的已过时间倒推，但不早于这一帧的移动开始；标作没有对应的变化
	Harness.Windows.Add({1, 0, 0.2f, 0.f});
	const FModel::FFrame Added = Harness.Sample();
	if (TestTrue(TEXT("校正加上：开一段"), Added.Starts.Num() == 1))
	{
		TestFalse(TEXT("校正加上：没有对应的变化"), Added.Starts[0].bExplained);
		TestEqual(TEXT("校正加上：限制在这一帧里"), Added.Starts[0].Time, Harness.TimeOf(Harness.Frame - 1));
		TestEqual(TEXT("校正加上：不属于任何链"), Added.Starts[0].Chain, static_cast<int32>(INDEX_NONE));
	}

	// 从头开始：下一帧又当作第一帧
	Harness.Model.Reset();
	TestEqual(TEXT("从头开始：没有开着的段"), Harness.Model.NumOpenSegments(), 0);
	const FModel::FFrame Restarted = Harness.Sample();
	TestTrue(TEXT("从头开始：还在的窗口重新开一段，算作有变化"), Restarted.Starts.Num() == 1 && Restarted.Starts[0].bExplained);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

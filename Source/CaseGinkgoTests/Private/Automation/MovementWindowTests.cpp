#include "Misc/AutomationTest.h"
#include "Movement/GinkgoMovementConfig.h"
#include "Movement/Windows/GinkgoLurchWindow.h"
#include "Movement/Windows/GinkgoMovementWindowManager.h"
#include "MovementWindowTestTypes.h"
#include "Utilities/Enumeration/Tags/MovementTags.h"
#include "Utilities/Enumeration/Tags/TestTags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoMovementWindowTests
{
	// 测试自己的标签，不依赖游戏里配的窗口。定义在运行时模块里（FTestTags）：原生标签不能定义在测试模块这种 DeveloperTool 模块里
	const FNativeGameplayTag& TagA = FTestTags::GinkgoTest_Window_A;
	const FNativeGameplayTag& TagAFirst = FTestTags::GinkgoTest_Window_A_First;
	const FNativeGameplayTag& TagASecond = FTestTags::GinkgoTest_Window_A_Second;
	const FNativeGameplayTag& TagB = FTestTags::GinkgoTest_Window_B;
	const FNativeGameplayTag& TagEffect = FTestTags::GinkgoTest_Effect;
	const FNativeGameplayTag& TagEffectSpeed = FTestTags::GinkgoTest_Effect_Speed;
	const FNativeGameplayTag& TagEffectJump = FTestTags::GinkgoTest_Effect_Jump;
	const FNativeGameplayTag& TagAbility = FTestTags::GinkgoTest_Ability;
	const FNativeGameplayTag& TagAbilityAirJump = FTestTags::GinkgoTest_Ability_AirJump;

	using FDefinitionList = TArray<TObjectPtr<UGinkgoMovementWindow>>;
	using FWindows = FGinkgoMovementWindowManager::FWindows;

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

	template <typename TWindow = UGinkgoMovementWindow>
	TWindow* MakeWindow(const FGameplayTag& Tag, const TArray<EGinkgoWindowEvent>& PushEvents, const TArray<FGinkgoWindowPhase>& Phases,
		const EGinkgoWindowRepush Repush = EGinkgoWindowRepush::Refresh)
	{
		TWindow* Window = NewObject<TWindow>(GetTransientPackage());
		Window->Tag = Tag;
		Window->PushEvents = PushEvents;
		Window->Phases = Phases;
		Window->Repush = Repush;
		return Window;
	}

	/** 一项常量倍率 */
	FGinkgoWindowMultiplier MakeMultiplier(UObject* Outer, const FGameplayTag& Category, const float Value, const int32 Phase = INDEX_NONE)
	{
		UGinkgoWindowValueConstant* Constant = NewObject<UGinkgoWindowValueConstant>(Outer);
		Constant->Value = Value;
		FGinkgoWindowMultiplier Multiplier;
		Multiplier.Category = Category;
		Multiplier.Phase = Phase;
		Multiplier.Value = Constant;
		return Multiplier;
	}

	/** 一组定义、一个管理器与一份窗口状态，相当于一个移动组件里的窗口部分 */
	struct FHarness
	{
		FDefinitionList Definitions;
		FGinkgoMovementWindowManager Manager;
		FWindows Windows;
		/** 最近一次处理事件的上下文：速度从 0 开始，行为改过之后的样子 */
		FGinkgoWindowEventContext Context;

		bool Send(const EGinkgoWindowEvent Type, const float Value = 0.f, const bool bFlag = false)
		{
			Context = FGinkgoWindowEventContext();
			return Manager.HandleEvent(Definitions, Windows, {Type, Value, bFlag}, Context);
		}

		void Advance(const float DeltaTime)
		{
			Manager.Advance(Definitions, Windows, DeltaTime);
		}

		/** 按固定步长推进，返回第一次满足条件时走了几步；MaxSteps 步内都不满足时返回 -1 */
		template <typename TPredicate>
		int32 StepsUntil(const float DeltaTime, const int32 MaxSteps, TPredicate Predicate)
		{
			for (int32 Step = 1; Step <= MaxSteps; ++Step)
			{
				Advance(DeltaTime);
				if (Predicate())
				{
					return Step;
				}
			}
			return INDEX_NONE;
		}

		float Multiplier(const FGameplayTag& Category) const
		{
			return Manager.GetMultiplier(Definitions, Windows, Category);
		}

		bool HasAbility(const FGameplayTag& Ability) const
		{
			return Manager.HasAbility(Definitions, Windows, Ability);
		}

		const FGameplayTagContainer& Tags() const
		{
			return Manager.GetTags();
		}

		/** 某个定义的活动窗口有几个 */
		int32 Count(const int32 Definition) const
		{
			int32 Result = 0;
			for (const FGinkgoMovementWindowState& Window : Windows)
			{
				Result += Window.Definition == Definition ? 1 : 0;
			}
			return Result;
		}
	};

	bool IsChange(const FGinkgoMovementWindowManager::FChange& Change, const int32 Definition, const int32 FromPhase, const int32 ToPhase)
	{
		return Change.Definition == Definition && Change.FromPhase == FromPhase && Change.ToPhase == ToPhase;
	}
}

// ===== 推入 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowPushTest, "CaseGinkgo.Movement.Window.Push",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowPushTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	// 推入事件对得上才推入：从第一个阶段开始，参数取事件的数值，记一次变化
	{
		FHarness H;
		H.Definitions = {MakeWindow(TagA, {EGinkgoWindowEvent::Jumped, EGinkgoWindowEvent::SlideStarted}, {MakePhase(TagAFirst, 1.f)})};
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("别的事件不推入"), H.Windows.Num(), 0);
		H.Send(EGinkgoWindowEvent::SlideStarted, 438.15f, true);
		TestEqual(TEXT("推入事件推入一个"), H.Windows.Num(), 1);
		if (H.Windows.Num() == 1)
		{
			TestEqual(TEXT("从第一个阶段开始"), int32(H.Windows[0].Phase), 0);
			TestEqual(TEXT("已过时间从 0 开始"), H.Windows[0].PhaseTime, 0.f);
			TestEqual(TEXT("参数取事件的数值"), H.Windows[0].Param, 438.15f);
		}
		const auto Changes = H.Manager.TakeChanges();
		TestTrue(TEXT("记下一次推入（原来的阶段为 -1）"), Changes.Num() == 1 && IsChange(Changes[0], 0, INDEX_NONE, 0));
		TestEqual(TEXT("变化累计一次"), int32(H.Manager.GetChangeCount()), 1);
		TestEqual(TEXT("取走之后没有了"), H.Manager.TakeChanges().Num(), 0);
	}

	// 再次推入：刷新从头开始、参数换成新的；忽略什么也不做；叠加再推入一个独立计时的
	{
		const TArray<FGinkgoWindowPhase> TwoPhases = {MakePhase(TagAFirst, 0.3f, 1), MakePhase(TagASecond, 1.f)};
		FHarness H;
		H.Definitions = {
			MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, TwoPhases, EGinkgoWindowRepush::Refresh),
			MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, TwoPhases, EGinkgoWindowRepush::Ignore),
			MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 1.f)}, EGinkgoWindowRepush::Stack),
		};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f);
		H.Advance(0.4f);
		H.Manager.TakeChanges();
		H.Send(EGinkgoWindowEvent::Jumped, 7.f);
		TestEqual(TEXT("刷新：仍只有一个"), H.Count(0), 1);
		TestEqual(TEXT("忽略：仍只有一个"), H.Count(1), 1);
		TestEqual(TEXT("叠加：两个"), H.Count(2), 2);
		if (H.Windows.Num() == 4)
		{
			// 状态按推入的先后排：刷新、忽略、叠加的第一个，再是叠加的第二个
			TestEqual(TEXT("刷新：回到第一个阶段"), int32(H.Windows[0].Phase), 0);
			TestEqual(TEXT("刷新：已过时间清零"), H.Windows[0].PhaseTime, 0.f);
			TestEqual(TEXT("刷新：参数换成新的"), H.Windows[0].Param, 7.f);
			TestEqual(TEXT("忽略：阶段不变"), int32(H.Windows[1].Phase), 1);
			TestEqual(TEXT("忽略：已过时间不变"), H.Windows[1].PhaseTime, 0.1f, 1e-5f);
			TestEqual(TEXT("忽略：参数不变"), H.Windows[1].Param, 1.f);
			TestEqual(TEXT("叠加：第一个照常计时"), H.Windows[2].PhaseTime, 0.4f, 1e-5f);
			TestEqual(TEXT("叠加：第二个从 0 开始"), H.Windows[3].PhaseTime, 0.f);
			TestEqual(TEXT("叠加：第二个带新的参数"), H.Windows[3].Param, 7.f);
		}
		const auto Changes = H.Manager.TakeChanges();
		TestTrue(TEXT("刷新记作从原来的阶段进入阶段 0，忽略不记，叠加记一次推入"),
			Changes.Num() == 2 && IsChange(Changes[0], 0, 1, 0) && IsChange(Changes[1], 2, INDEX_NONE, 0));
		H.Advance(0.7f);
		TestEqual(TEXT("叠加：先推入的到时结束，后推入的还在"), H.Count(2), 1);
	}

	// 事件标志：要求为真的只在标志为真时推入，要求为假的反之
	{
		FHarness H;
		UGinkgoMovementWindow* WhenSet = MakeWindow(TagA, {EGinkgoWindowEvent::SlideStarted}, {MakePhase(TagAFirst, 1.f)});
		WhenSet->PushFlag = EGinkgoWindowEventFlagFilter::Set;
		UGinkgoMovementWindow* WhenClear = MakeWindow(TagB, {EGinkgoWindowEvent::SlideStarted}, {MakePhase(FGameplayTag(), 1.f)});
		WhenClear->PushFlag = EGinkgoWindowEventFlagFilter::Clear;
		H.Definitions = {WhenSet, WhenClear};
		H.Send(EGinkgoWindowEvent::SlideStarted, 438.15f, true);
		TestTrue(TEXT("标志为真：只推入要求为真的"), H.Count(0) == 1 && H.Count(1) == 0);
		H.Send(EGinkgoWindowEvent::SlideStarted, 0.f, false);
		TestTrue(TEXT("标志为假：推入要求为假的，要求为真的不刷新"), H.Count(1) == 1 && H.Windows[0].Param == 438.15f);
	}

	// 推入条件看汇总标签；同一个事件里，后面的定义看得到前面的定义刚推入的窗口
	{
		const FGameplayTagQuery NeedsB = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(TagB));
		UGinkgoMovementWindow* Gated = MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 1.f)});
		Gated->PushQuery = NeedsB;

		FHarness H;
		H.Definitions = {Gated, MakeWindow(TagB, {EGinkgoWindowEvent::Landed}, {MakePhase(FGameplayTag(), 1.f)})};
		H.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("条件不满足时不推入"), H.Count(0), 0);
		H.Send(EGinkgoWindowEvent::Landed);
		H.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("条件满足时推入"), H.Count(0), 1);

		FHarness Before;
		Before.Definitions = {MakeWindow(TagB, {EGinkgoWindowEvent::Jumped}, {MakePhase(FGameplayTag(), 1.f)}), Gated};
		Before.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("前面的定义刚推入的窗口算进条件"), Before.Count(1), 1);

		FHarness After;
		After.Definitions = {Gated, MakeWindow(TagB, {EGinkgoWindowEvent::Jumped}, {MakePhase(FGameplayTag(), 1.f)})};
		After.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("后面的定义推入的窗口不算进条件"), After.Count(0), 0);
	}

	// 容量：满了就不再推入，并打一条警告
	{
		FHarness H;
		H.Definitions = {MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 1.f)}, EGinkgoWindowRepush::Stack)};
		AddExpectedMessagePlain(TEXT("活动窗口已满"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		for (int32 Push = 0; Push <= FGinkgoMovementSimState::MaxWindows; ++Push)
		{
			H.Send(EGinkgoWindowEvent::Jumped);
		}
		TestEqual(TEXT("最多到容量"), H.Windows.Num(), FGinkgoMovementSimState::MaxWindows);
	}
	return true;
}

// ===== 阶段：按时长与事件结束 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowPhasesTest, "CaseGinkgo.Movement.Window.Phases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowPhasesTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	// 按时长：0.3 s 后换到 1 s 的第二个阶段，再结束。推入的那次移动算一整段 Δt，多出来的时间带进下一个阶段，
	// 所以结束落在第 ceil(1.3 × fps) 次移动上，与帧率无关。18 个 1/60 s 浮点累加起来比 0.3 小一点点，容差让它落在第 18 次
	{
		struct FCase
		{
			int32 Fps;
			int32 SwitchStep;
			int32 EndStep;
		};
		const FCase Cases[] = {{30, 9, 39}, {60, 18, 78}, {144, 44, 188}};
		UGinkgoMovementWindow* Probe = MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 0.3f, 1), MakePhase(TagASecond, 1.f)});
		for (const FCase& Case : Cases)
		{
			FHarness H;
			H.Definitions = {Probe};
			H.Send(EGinkgoWindowEvent::Jumped);
			const float DeltaTime = 1.f / Case.Fps;
			const int32 Switch = H.StepsUntil(DeltaTime, 1000, [&H] { return H.Windows.Num() == 1 && H.Windows[0].Phase == 1; });
			TestEqual(*FString::Printf(TEXT("%d fps：第 %d 次移动换到第二个阶段"), Case.Fps, Case.SwitchStep), Switch, Case.SwitchStep);
			if (H.Windows.Num() == 1)
			{
				TestEqual(*FString::Printf(TEXT("%d fps：多出来的时间带进第二个阶段"), Case.Fps), H.Windows[0].PhaseTime, Switch * DeltaTime - 0.3f, 1e-5f);
			}
			const int32 Remaining = H.StepsUntil(DeltaTime, 1000, [&H] { return H.Windows.IsEmpty(); });
			TestEqual(*FString::Printf(TEXT("%d fps：第 %d 次移动结束"), Case.Fps, Case.EndStep), Switch + Remaining, Case.EndStep);
			const auto Changes = H.Manager.TakeChanges();
			TestTrue(*FString::Printf(TEXT("%d fps：推入、换阶段、结束各记一次"), Case.Fps),
				Changes.Num() == 3 && IsChange(Changes[0], 0, INDEX_NONE, 0) && IsChange(Changes[1], 0, 0, 1) && IsChange(Changes[2], 0, 1, INDEX_NONE));
		}
	}

	// 一次推进跨过几个阶段：每个 0.1 s，推进 0.25 s 落在第三个阶段，已过 0.05 s
	{
		FHarness H;
		H.Definitions = {MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 0.1f, 1), MakePhase(TagASecond, 0.1f, 2), MakePhase(FGameplayTag(), 0.1f)})};
		H.Send(EGinkgoWindowEvent::Jumped);
		H.Manager.TakeChanges();
		H.Advance(0.25f);
		TestTrue(TEXT("一次跨过两个阶段"), H.Windows.Num() == 1 && H.Windows[0].Phase == 2);
		if (H.Windows.Num() == 1)
		{
			TestEqual(TEXT("剩下的时间留在第三个阶段"), H.Windows[0].PhaseTime, 0.05f, 1e-5f);
		}
		const auto Changes = H.Manager.TakeChanges();
		TestTrue(TEXT("两次换阶段都记下"), Changes.Num() == 2 && IsChange(Changes[0], 0, 0, 1) && IsChange(Changes[1], 0, 1, 2));
		H.Advance(0.06f);
		TestEqual(TEXT("最后一个阶段到时结束"), H.Windows.Num(), 0);
	}

	// 事件切换：不定长的阶段只由事件离开；切换后从 0 计时；切到 -1 结束；当前阶段没有这个事件的切换就不动
	{
		FHarness H;
		H.Definitions = {MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {
			MakePhase(TagAFirst, 0.f, INDEX_NONE, {{EGinkgoWindowEvent::Landed, 1}, {EGinkgoWindowEvent::WalkedOffLedge, INDEX_NONE}}),
			MakePhase(TagASecond, 0.75f),
		})};
		H.Send(EGinkgoWindowEvent::Jumped);
		H.Advance(100.f);
		TestTrue(TEXT("不定长的阶段不会到时"), H.Windows.Num() == 1 && H.Windows[0].Phase == 0);
		H.Send(EGinkgoWindowEvent::Landed);
		TestTrue(TEXT("落地切到第二个阶段"), H.Windows.Num() == 1 && H.Windows[0].Phase == 1);
		if (H.Windows.Num() == 1)
		{
			TestEqual(TEXT("切换后从 0 计时"), H.Windows[0].PhaseTime, 0.f);
		}
		H.Advance(0.2f);
		H.Send(EGinkgoWindowEvent::Landed);
		TestTrue(TEXT("第二个阶段没有落地的切换，不动"), H.Windows.Num() == 1 && H.Windows[0].Phase == 1);
		H.Advance(0.6f);
		TestEqual(TEXT("第二个阶段到时结束"), H.Windows.Num(), 0);

		H.Send(EGinkgoWindowEvent::Jumped);
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestEqual(TEXT("切到 -1 结束"), H.Windows.Num(), 0);
	}

	// 结束事件：任何阶段都结束，比阶段的切换表优先
	{
		FHarness H;
		UGinkgoMovementWindow* Window = MakeWindow(TagA, {EGinkgoWindowEvent::SlideStarted}, {
			MakePhase(TagAFirst, 0.f, INDEX_NONE, {{EGinkgoWindowEvent::SlideEnded, 1}}),
			MakePhase(TagASecond, 0.f),
		});
		Window->EndEvents = {EGinkgoWindowEvent::SlideEnded};
		H.Definitions = {Window};
		H.Send(EGinkgoWindowEvent::SlideStarted);
		H.Send(EGinkgoWindowEvent::SlideEnded);
		TestEqual(TEXT("结束事件比切换表优先"), H.Windows.Num(), 0);
	}

	// 同一个事件既结束又推入同一种窗口：先切换再推入，结果是从头开始（定义为"忽略"也一样，因为旧的已经结束了）
	{
		FHarness H;
		UGinkgoMovementWindow* Window = MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 1.f)}, EGinkgoWindowRepush::Ignore);
		Window->EndEvents = {EGinkgoWindowEvent::Jumped};
		H.Definitions = {Window};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f);
		H.Advance(0.5f);
		H.Manager.TakeChanges();
		H.Send(EGinkgoWindowEvent::Jumped, 2.f);
		TestTrue(TEXT("从头开始，带新的参数"), H.Windows.Num() == 1 && H.Windows[0].PhaseTime == 0.f && H.Windows[0].Param == 2.f);
		const auto Changes = H.Manager.TakeChanges();
		TestTrue(TEXT("记作一次结束和一次推入"), Changes.Num() == 2 && IsChange(Changes[0], 0, 0, INDEX_NONE) && IsChange(Changes[1], 0, INDEX_NONE, 0));
	}

	// 定义对不上的窗口（下标越界、定义为空、阶段越界）推进或处理事件时直接结束，不影响别的窗口
	{
		FHarness H;
		H.Definitions = {MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 1.f)}), nullptr};
		H.Windows.Add({5, 0, 0.f, 0.f});
		H.Windows.Add({0, 0, 0.f, 0.f});
		H.Windows.Add({1, 0, 0.f, 0.f});
		H.Windows.Add({0, 3, 0.f, 0.f});
		H.Advance(0.1f);
		TestTrue(TEXT("推进时只留下对得上的"), H.Windows.Num() == 1 && H.Windows[0].Definition == 0 && H.Windows[0].Phase == 0);
		TestEqual(TEXT("三个都记作结束"), H.Manager.TakeChanges().Num(), 3);
		H.Windows.Add({7, 0, 0.f, 0.f});
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("处理事件时也去掉"), H.Windows.Num(), 1);
	}
	return true;
}

// ===== 贡献：倍率、屏蔽、能力 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowContributionsTest, "CaseGinkgo.Movement.Window.Contributions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowContributionsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	// 倍率：同一类别的贡献相乘；查父类别时子类别的贡献也算；限定阶段的贡献只在那个阶段算；父类别上的贡献不算进子类别的查询
	{
		UGinkgoMovementWindow* X = MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 0.2f, 1), MakePhase(TagASecond, 1.f)});
		X->Multipliers = {MakeMultiplier(X, TagEffectSpeed, 0.5f), MakeMultiplier(X, TagEffectJump, 0.25f, 1)};
		UGinkgoMovementWindow* Y = MakeWindow(TagB, {EGinkgoWindowEvent::Jumped}, {MakePhase(FGameplayTag(), 1.f)});
		Y->Multipliers = {MakeMultiplier(Y, TagEffectSpeed, 0.8f)};
		UGinkgoMovementWindow* Parent = MakeWindow(FGameplayTag(), {EGinkgoWindowEvent::Landed}, {MakePhase(FGameplayTag(), 0.f)});
		Parent->Multipliers = {MakeMultiplier(Parent, TagEffect, 0.5f)};
		UGinkgoMovementWindow* NoValue = MakeWindow(FGameplayTag(), {EGinkgoWindowEvent::Landed}, {MakePhase(FGameplayTag(), 0.f)});
		FGinkgoWindowMultiplier Empty;
		Empty.Category = TagEffectSpeed;
		NoValue->Multipliers = {Empty};

		FHarness H;
		H.Definitions = {X, Y, Parent, NoValue};
		TestEqual(TEXT("没有窗口时为 1"), H.Multiplier(TagEffectSpeed), 1.f);
		H.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("两个窗口的同类贡献相乘"), H.Multiplier(TagEffectSpeed), 0.4f, 1e-6f);
		TestEqual(TEXT("限定第二个阶段的贡献在第一个阶段不算"), H.Multiplier(TagEffectJump), 1.f);
		TestEqual(TEXT("查父类别时子类别的贡献也算"), H.Multiplier(TagEffect), 0.4f, 1e-6f);
		H.Advance(0.25f);
		TestEqual(TEXT("到了第二个阶段才算"), H.Multiplier(TagEffectJump), 0.25f, 1e-6f);
		TestEqual(TEXT("查父类别时所有子类别相乘"), H.Multiplier(TagEffect), 0.1f, 1e-6f);
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("父类别上的贡献不算进子类别的查询；空着的数值来源为 1"), H.Multiplier(TagEffectSpeed), 0.4f, 1e-6f);
		TestEqual(TEXT("父类别上的贡献算进父类别的查询"), H.Multiplier(TagEffect), 0.05f, 1e-6f);
		TestEqual(TEXT("无效的类别查不到贡献"), H.Multiplier(FGameplayTag()), 1.f);
	}

	// 曲线：按当前阶段已过的时间求值
	{
		UGinkgoMovementWindow* Window = MakeWindow(TagA, {EGinkgoWindowEvent::Landed}, {MakePhase(TagAFirst, 1.f)});
		UGinkgoWindowValueCurve* Curve = NewObject<UGinkgoWindowValueCurve>(Window);
		Curve->Curve.GetRichCurve()->AddKey(0.f, 0.25f);
		Curve->Curve.GetRichCurve()->AddKey(0.6f, 1.f);
		FGinkgoWindowMultiplier Recovery;
		Recovery.Category = TagEffectJump;
		Recovery.Value = Curve;
		Window->Multipliers = {Recovery};
		FHarness H;
		H.Definitions = {Window};
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("曲线：刚推入时取开头的值"), H.Multiplier(TagEffectJump), 0.25f, 1e-6f);
		H.Advance(0.3f);
		TestEqual(TEXT("曲线：按已过时间线性插值"), H.Multiplier(TagEffectJump), 0.625f, 1e-5f);
		TestEqual(TEXT("没有键的曲线为 1"), NewObject<UGinkgoWindowValueCurve>()->Evaluate(0.3f, 0.f), 1.f);
	}

	// 屏蔽：被任何活动窗口屏蔽的类别（及子类别）的贡献都不算，不论来自哪个窗口
	{
		UGinkgoMovementWindow* Penalty = MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 0.f)});
		Penalty->Multipliers = {MakeMultiplier(Penalty, TagEffectJump, 0.25f), MakeMultiplier(Penalty, TagEffectSpeed, 0.5f)};
		UGinkgoMovementWindow* BlockJump = MakeWindow(TagB, {EGinkgoWindowEvent::WalkedOffLedge}, {MakePhase(FGameplayTag(), 0.2f)});
		BlockJump->BlockedCategories = FGameplayTagContainer(TagEffectJump);
		UGinkgoMovementWindow* BlockAll = MakeWindow(FGameplayTag(), {EGinkgoWindowEvent::Landed}, {MakePhase(FGameplayTag(), 0.f)});
		BlockAll->BlockedCategories = FGameplayTagContainer(TagEffect);
		FHarness H;
		H.Definitions = {Penalty, BlockJump, BlockAll};
		H.Send(EGinkgoWindowEvent::Jumped);
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestEqual(TEXT("屏蔽的类别为 1"), H.Multiplier(TagEffectJump), 1.f);
		TestEqual(TEXT("别的类别照常"), H.Multiplier(TagEffectSpeed), 0.5f, 1e-6f);
		H.Advance(0.25f);
		TestEqual(TEXT("屏蔽的窗口结束后又算"), H.Multiplier(TagEffectJump), 0.25f, 1e-6f);
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("屏蔽父类别时子类别都不算"), H.Multiplier(TagEffectSpeed), 1.f);
	}

	// 能力：任何活动窗口给出就为真，查父能力也为真；窗口结束后就没有了
	{
		UGinkgoMovementWindow* Coyote = MakeWindow(TagA, {EGinkgoWindowEvent::WalkedOffLedge}, {MakePhase(TagAFirst, 0.2f)});
		Coyote->GrantedAbilities = FGameplayTagContainer(TagAbilityAirJump);
		FHarness H;
		H.Definitions = {Coyote};
		TestFalse(TEXT("没有窗口时没有能力"), H.HasAbility(TagAbilityAirJump));
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestTrue(TEXT("窗口给出能力"), H.HasAbility(TagAbilityAirJump));
		TestTrue(TEXT("查父能力也为真"), H.HasAbility(TagAbility));
		TestFalse(TEXT("没给的能力为假"), H.HasAbility(TagB));
		H.Advance(0.2f);
		TestFalse(TEXT("窗口结束后没有能力"), H.HasAbility(TagAbilityAirJump));
	}
	return true;
}

// ===== 行为：优先级、消费、用掉 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowBehaviorTest, "CaseGinkgo.Movement.Window.Behavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowBehaviorTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	// 四个处理起跳的窗口：优先级 0、5、5、10。优先级 5 的两个里 MidB 先推入（未起跳离地时），其余在落地时按定义的顺序推入
	TArray<FString> CallLog;
	const auto MakeBehavior = [&CallLog](const TCHAR* Name, const int32 Priority, const float VelocityDeltaX, const EGinkgoWindowEvent PushEvent)
	{
		UGinkgoTestBehaviorWindow* Window = NewObject<UGinkgoTestBehaviorWindow>(GetTransientPackage());
		Window->LogName = Name;
		Window->PushEvents = {PushEvent};
		Window->Phases = {MakePhase(FGameplayTag(), 0.f)};
		Window->Priority = Priority;
		Window->VelocityDeltaX = VelocityDeltaX;
		Window->CallLog = &CallLog;
		return Window;
	};
	UGinkgoTestBehaviorWindow* Low = MakeBehavior(TEXT("Low"), 0, 1000.f, EGinkgoWindowEvent::Landed);
	UGinkgoTestBehaviorWindow* MidA = MakeBehavior(TEXT("MidA"), 5, 100.f, EGinkgoWindowEvent::Landed);
	UGinkgoTestBehaviorWindow* MidB = MakeBehavior(TEXT("MidB"), 5, 10.f, EGinkgoWindowEvent::WalkedOffLedge);
	UGinkgoTestBehaviorWindow* High = MakeBehavior(TEXT("High"), 10, 1.f, EGinkgoWindowEvent::Landed);
	MidB->Tag = TagB;
	Low->EndEvents = {EGinkgoWindowEvent::Jumped};
	UGinkgoMovementWindow* PushedByJump = MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 1.f)});

	const auto Setup = [&](FHarness& H)
	{
		H.Definitions = {Low, MidA, MidB, High, PushedByJump};
		H.Send(EGinkgoWindowEvent::WalkedOffLedge, 3.f);
		H.Send(EGinkgoWindowEvent::Landed);
		H.Advance(0.1f);
		CallLog.Reset();
	};

	// 按优先级处理，相同时先推入的先；行为改的速度留在上下文里；行为看到自己的已过时间与参数，以及处理这次事件之前的汇总标签
	{
		FHarness H;
		Setup(H);
		const bool bConsumed = H.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("处理顺序：优先级大的先，相同时先推入的先"), FString::Join(CallLog, TEXT(",")), FString(TEXT("High,MidB,MidA,Low")));
		TestFalse(TEXT("没有窗口消费"), bConsumed);
		TestEqual(TEXT("每个行为都改了速度"), H.Context.Velocity.X, 1111.0);
		TestEqual(TEXT("行为看到自己的已过时间"), MidB->SeenPhaseTime, 0.1f, 1e-6f);
		TestEqual(TEXT("行为看到推入时的参数"), MidB->SeenParam, 3.f);
		TestTrue(TEXT("行为看到处理这次事件之前的汇总标签"), High->SeenTags.HasTagExact(TagB) && !High->SeenTags.HasTagExact(TagA));
		CallLog.Reset();
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("只处理自己那种事件"), CallLog.Num(), 0);
	}

	// 消费：更低的收不到；结束事件与推入照常
	{
		MidB->bConsume = true;
		FHarness H;
		Setup(H);
		const bool bConsumed = H.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("消费后更低的收不到"), FString::Join(CallLog, TEXT(",")), FString(TEXT("High,MidB")));
		TestTrue(TEXT("返回被消费了"), bConsumed);
		TestEqual(TEXT("只有收到事件的改了速度"), H.Context.Velocity.X, 11.0);
		TestEqual(TEXT("没收到事件的照样按结束事件结束"), H.Count(0), 0);
		TestEqual(TEXT("推入不受消费影响"), H.Count(4), 1);
		MidB->bConsume = false;
	}

	// 用掉自己：处理完就结束，别的不受影响；更高优先级消费了，更低的就用不掉
	{
		High->bEndSelf = true;
		MidA->bEndSelf = true;
		MidB->bConsume = true;
		FHarness H;
		Setup(H);
		H.Manager.TakeChanges();
		H.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("用掉自己的结束"), H.Count(3), 0);
		TestEqual(TEXT("没收到事件的用不掉"), H.Count(1), 1);
		TestEqual(TEXT("消费的那个还在"), H.Count(2), 1);
		const auto Changes = H.Manager.TakeChanges();
		TestTrue(TEXT("记下用掉、结束事件、推入三次变化"), Changes.Num() == 3 && IsChange(Changes[0], 3, 0, INDEX_NONE)
			&& IsChange(Changes[1], 0, 0, INDEX_NONE) && IsChange(Changes[2], 4, INDEX_NONE, 0));
		High->bEndSelf = false;
		MidA->bEndSelf = false;
		MidB->bConsume = false;
	}
	return true;
}

// ===== 汇总标签 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowTagsTest, "CaseGinkgo.Movement.Window.Tags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowTagsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	UGinkgoMovementWindow* A = MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 0.2f, 1), MakePhase(TagASecond, 0.5f)});
	UGinkgoMovementWindow* B = MakeWindow(TagB, {EGinkgoWindowEvent::Landed}, {MakePhase(FGameplayTag(), 0.f)});
	B->EndEvents = {EGinkgoWindowEvent::SlideEnded};
	FHarness H;
	H.Definitions = {A, B};

	// 汇总标签是窗口标签加当前阶段的标签，推入、换阶段、结束之后都重建
	TestEqual(TEXT("没有窗口时为空"), H.Tags().Num(), 0);
	H.Send(EGinkgoWindowEvent::Jumped);
	TestTrue(TEXT("推入：窗口标签与第一个阶段的标签"), H.Tags().Num() == 2 && H.Tags().HasTagExact(TagA) && H.Tags().HasTagExact(TagAFirst));
	FWindows Pushed = H.Windows;
	H.Advance(0.25f);
	TestTrue(TEXT("换阶段：换成第二个阶段的标签"), H.Tags().Num() == 2 && H.Tags().HasTagExact(TagASecond) && !H.Tags().HasTagExact(TagAFirst));
	H.Send(EGinkgoWindowEvent::Landed);
	TestTrue(TEXT("阶段没有标签时只加窗口标签"), H.Tags().Num() == 3 && H.Tags().HasTagExact(TagB));
	const FGameplayTag WindowRoot = TagA.GetTag().RequestDirectParent();
	TestTrue(TEXT("父标签不在快照里，查父标签时照样匹配"), !H.Tags().HasTagExact(WindowRoot) && H.Tags().HasTag(WindowRoot));
	H.Advance(0.5f);
	TestTrue(TEXT("结束：去掉这个窗口的标签"), H.Tags().Num() == 1 && H.Tags().HasTagExact(TagB));
	H.Send(EGinkgoWindowEvent::SlideEnded);
	TestEqual(TEXT("都结束后为空"), H.Tags().Num(), 0);

	// 状态被整体换掉（合并退回、校正放回）后由调用方重建
	H.Windows = Pushed;
	H.Manager.RebuildTags(H.Definitions, H.Windows);
	TestTrue(TEXT("按换进来的状态重建"), H.Tags().Num() == 2 && H.Tags().HasTagExact(TagA) && H.Tags().HasTagExact(TagAFirst));
	return true;
}

// ===== 清除：推入时结束别的窗口 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowClearTest, "CaseGinkgo.Movement.Window.Clear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowClearTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	// 目标：落地推入，两个阶段；清除者：走下边缘推入，清除窗口 A
	UGinkgoMovementWindow* Target = MakeWindow(TagA, {EGinkgoWindowEvent::Landed}, {MakePhase(TagAFirst, 0.2f, 1), MakePhase(TagASecond, 1.f)});
	UGinkgoMovementWindow* Clearer = MakeWindow(TagB, {EGinkgoWindowEvent::WalkedOffLedge}, {MakePhase(FGameplayTag(), 1.f)});
	Clearer->ClearedWindows.AddTag(TagA);

	// 推入时结束标签对得上的窗口；变化先是推入，再是清除
	{
		FHarness H;
		H.Definitions = {Target, Clearer};
		H.Send(EGinkgoWindowEvent::Landed);
		H.Manager.TakeChanges();
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestTrue(TEXT("推入清除者，结束目标"), H.Count(0) == 0 && H.Count(1) == 1);
		const TArray<FGinkgoMovementWindowManager::FChange, TInlineAllocator<8>> Changes = H.Manager.TakeChanges();
		TestTrue(TEXT("变化：推入，再结束"), Changes.Num() == 2 && IsChange(Changes[0], 1, INDEX_NONE, 0) && IsChange(Changes[1], 0, 0, INDEX_NONE));
		TestTrue(TEXT("汇总标签里没有目标"), !H.Tags().HasTag(TagA) && H.Tags().HasTagExact(TagB));
		// 清除只在推入的那一刻：清除者还在时照样能推入目标（要挡住，用目标的推入条件）
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("之后照常推入"), H.Count(0), 1);
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestTrue(TEXT("刷新也清除"), H.Count(0) == 0 && H.Count(1) == 1);
	}

	// 也按当前阶段的标签匹配：只清第二个阶段
	{
		UGinkgoMovementWindow* SecondOnly = MakeWindow(TagB, {EGinkgoWindowEvent::WalkedOffLedge}, {MakePhase(FGameplayTag(), 1.f)});
		SecondOnly->ClearedWindows.AddTag(TagASecond);
		FHarness H;
		H.Definitions = {Target, SecondOnly};
		H.Send(EGinkgoWindowEvent::Landed);
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestEqual(TEXT("第一个阶段不清"), H.Count(0), 1);
		H.Advance(0.25f);
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestEqual(TEXT("第二个阶段清掉"), H.Count(0), 0);
	}

	// 忽略的再次推入不算推入，不清
	{
		UGinkgoMovementWindow* Ignoring = MakeWindow(TagB, {EGinkgoWindowEvent::WalkedOffLedge}, {MakePhase(FGameplayTag(), 1.f)}, EGinkgoWindowRepush::Ignore);
		Ignoring->ClearedWindows.AddTag(TagA);
		FHarness H;
		H.Definitions = {Target, Ignoring};
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		H.Send(EGinkgoWindowEvent::Landed);
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestEqual(TEXT("忽略时不清"), H.Count(0), 1);
	}

	// 同一个事件推入或刷新的窗口不清，与定义的先后无关
	{
		UGinkgoMovementWindow* SameEventTarget = MakeWindow(TagA, {EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 1.f)});
		UGinkgoMovementWindow* SameEventClearer = MakeWindow(TagB, {EGinkgoWindowEvent::Jumped}, {MakePhase(FGameplayTag(), 1.f)});
		SameEventClearer->ClearedWindows.AddTag(TagA);
		for (const bool bClearerFirst : {false, true})
		{
			FHarness H;
			H.Definitions = bClearerFirst ? FDefinitionList{SameEventClearer, SameEventTarget} : FDefinitionList{SameEventTarget, SameEventClearer};
			H.Send(EGinkgoWindowEvent::Jumped);
			TestEqual(*FString::Printf(TEXT("清除者排在%s：同一个事件推入的目标还在"), bClearerFirst ? TEXT("前面") : TEXT("后面")), H.Windows.Num(), 2);
			H.Manager.TakeChanges();
			H.Send(EGinkgoWindowEvent::Jumped);
			// 清除放在推入之后：两个都只是刷新，不会先清掉目标、再重新推入
			const TArray<FGinkgoMovementWindowManager::FChange, TInlineAllocator<8>> Changes = H.Manager.TakeChanges();
			TestTrue(*FString::Printf(TEXT("清除者排在%s：同一个事件刷新的目标还在，变化只有两次刷新"), bClearerFirst ? TEXT("前面") : TEXT("后面")),
				H.Windows.Num() == 2 && Changes.Num() == 2 && Changes[0].FromPhase == 0 && Changes[0].ToPhase == 0 && Changes[1].FromPhase == 0 && Changes[1].ToPhase == 0);
		}
	}

	// 清掉的窗口后面的窗口前移：两个旧的目标中间夹着别的窗口，这次推入的目标与清除者排在最后
	{
		UGinkgoMovementWindow* Stacked = MakeWindow(TagA, {EGinkgoWindowEvent::Landed, EGinkgoWindowEvent::Jumped}, {MakePhase(TagAFirst, 1.f)},
			EGinkgoWindowRepush::Stack);
		UGinkgoMovementWindow* Bystander = MakeWindow(TagB, {EGinkgoWindowEvent::SlideStarted}, {MakePhase(FGameplayTag(), 1.f)});
		UGinkgoMovementWindow* JumpClearer = MakeWindow(TagB, {EGinkgoWindowEvent::Jumped}, {MakePhase(FGameplayTag(), 1.f)});
		JumpClearer->ClearedWindows.AddTag(TagA);
		FHarness H;
		H.Definitions = {Stacked, Bystander, JumpClearer};
		H.Send(EGinkgoWindowEvent::Landed);
		H.Send(EGinkgoWindowEvent::SlideStarted);
		H.Advance(0.1f);
		H.Send(EGinkgoWindowEvent::Landed);
		H.Advance(0.1f);
		H.Send(EGinkgoWindowEvent::Jumped);
		TestTrue(TEXT("旧的两个目标都清掉，别的窗口、这次推入的目标与清除者还在"), H.Windows.Num() == 3 && H.Count(0) == 1 && H.Count(1) == 1 && H.Count(2) == 1);
		if (H.Windows.Num() == 3)
		{
			TestTrue(TEXT("留下的顺序不变：别的窗口、新的目标、清除者"), H.Windows[0].Definition == 1 && H.Windows[1].Definition == 0 && H.Windows[2].Definition == 2);
			TestEqual(TEXT("留下的目标是这次推入的"), H.Windows[1].PhaseTime, 0.f);
		}
	}
	return true;
}

// ===== 土狼时间：移动配置的类默认值带的那个窗口 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowCoyoteTimeTest, "CaseGinkgo.Movement.Window.CoyoteTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowCoyoteTimeTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	const FGameplayTag& JumpWhileFalling = FMovementTags::Movement_Window_Ability_JumpWhileFalling;
	const FGameplayTag& JumpPenalty = FMovementTags::Movement_Window_Effect_JumpPenalty;
	// 按标签找：命令行带 -GinkgoWindowProbe 时列表里还有探针
	const TObjectPtr<UGinkgoMovementWindow>* Found = GetDefault<UGinkgoMovementConfig>()->Windows.FindByPredicate([](const TObjectPtr<UGinkgoMovementWindow>& Window)
	{
		return Window && Window->Tag == FMovementTags::Movement_Window_Coyote;
	});
	if (!TestNotNull(TEXT("移动配置的类默认值带土狼时间"), Found))
	{
		return false;
	}
	UGinkgoMovementWindow* Coyote = *Found;
	// 一个由同一事件推入、在跳跃惩罚上给 0.5 的窗口，代替以后的跳跃疲劳
	UGinkgoMovementWindow* Penalty = MakeWindow(TagA, {EGinkgoWindowEvent::WalkedOffLedge}, {MakePhase(TagAFirst, 0.f)});
	Penalty->Multipliers = {MakeMultiplier(Penalty, JumpPenalty, 0.5f)};

	// 走下边缘时推入，给出空中起跳；屏蔽跳跃惩罚。起跳事件结束它，惩罚又算：移动组件在起跳事件之前读跳跃高度的倍率，土狼起跳因此不受惩罚
	{
		FHarness H;
		H.Definitions = {Coyote, Penalty};
		TestFalse(TEXT("推入之前没有空中起跳"), H.HasAbility(JumpWhileFalling));
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestTrue(TEXT("走下边缘后给出空中起跳"), H.HasAbility(JumpWhileFalling));
		TestEqual(TEXT("土狼时间里跳跃惩罚被屏蔽"), H.Multiplier(JumpPenalty), 1.f);
		H.Advance(1.f / 60.f);
		H.Send(EGinkgoWindowEvent::Jumped);
		TestFalse(TEXT("起跳结束土狼时间"), H.HasAbility(JumpWhileFalling));
		TestEqual(TEXT("土狼时间结束后惩罚又算"), H.Multiplier(JumpPenalty), 0.5f, 1e-6f);
	}
	{
		FHarness H;
		H.Definitions = {Coyote};
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		H.Advance(1.f / 60.f);
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("落地结束土狼时间"), H.Count(0), 0);
		H.Send(EGinkgoWindowEvent::Jumped);
		TestEqual(TEXT("起跳不推入土狼时间"), H.Count(0), 0);
	}

	// 0.2 s：推入那次移动也推进一整段 Δt，到时那次移动结束时去掉。活过的移动次数决定能在走下边缘之后的第几次移动起跳：
	// 30 fps 活过 6 次（第 N 次移动走下边缘，N + 1 到 N + 5 能跳），60 fps 12 次，144 fps 29 次（0.2 × 144 = 28.8，向上取整）
	for (const TPair<int32, int32>& Expected : {TPair<int32, int32>(30, 6), TPair<int32, int32>(60, 12), TPair<int32, int32>(144, 29)})
	{
		FHarness H;
		H.Definitions = {Coyote};
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		const int32 Moves = H.StepsUntil(1.f / Expected.Key, 100, [&H, &JumpWhileFalling] { return !H.HasAbility(JumpWhileFalling); });
		TestEqual(*FString::Printf(TEXT("%d fps：活过的移动次数"), Expected.Key), Moves, Expected.Value);
	}
	return true;
}

// ===== 滑铲疲劳：移动配置的类默认值带的另一个窗口 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowSlideFatigueTest, "CaseGinkgo.Movement.Window.SlideFatigue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowSlideFatigueTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	const FGameplayTag& SlideBoost = FMovementTags::Movement_Window_Effect_SlideBoost;
	const TArray<TObjectPtr<UGinkgoMovementWindow>>& Defaults = GetDefault<UGinkgoMovementConfig>()->Windows;
	const int32 FatigueIndex = Defaults.IndexOfByPredicate([](const TObjectPtr<UGinkgoMovementWindow>& Window)
	{
		return Window && Window->Tag == FMovementTags::Movement_Window_SlideFatigue;
	});
	const int32 CoyoteIndex = Defaults.IndexOfByPredicate([](const TObjectPtr<UGinkgoMovementWindow>& Window)
	{
		return Window && Window->Tag == FMovementTags::Movement_Window_Coyote;
	});
	if (!TestTrue(TEXT("移动配置的类默认值带滑铲疲劳"), FatigueIndex != INDEX_NONE))
	{
		return false;
	}
	// 状态按下标对应定义：新加的窗口排在已有的后面，土狼时间的下标不变
	TestTrue(TEXT("滑铲疲劳排在土狼时间后面"), CoyoteIndex != INDEX_NONE && CoyoteIndex < FatigueIndex);
	UGinkgoMovementWindow* Fatigue = Defaults[FatigueIndex];

	// 移动组件按这时的倍率算起步加速，再发滑铲开始：第一铲读到 1，之后 2 s 内的每一铲读到 0，也都重新计时
	{
		FHarness H;
		H.Definitions = {Fatigue};
		TestEqual(TEXT("没滑过时起步加速的倍率为 1"), H.Multiplier(SlideBoost), 1.f);
		H.Send(EGinkgoWindowEvent::SlideStarted, 438.15f, true);
		TestEqual(TEXT("滑铲开始推入"), H.Count(0), 1);
		TestEqual(TEXT("疲劳时起步加速的倍率为 0"), H.Multiplier(SlideBoost), 0.f);
		TestEqual(TEXT("只影响起步加速，跳跃惩罚不变"), H.Multiplier(FMovementTags::Movement_Window_Effect_JumpPenalty), 1.f);
		TestFalse(TEXT("不给能力"), H.HasAbility(FMovementTags::Movement_Window_Ability_JumpWhileFalling));
		TestTrue(TEXT("汇总标签里有滑铲疲劳"), H.Tags().HasTagExact(FMovementTags::Movement_Window_SlideFatigue));

		// 滑铲结束、起跳、落地、走下边缘都不结束它：滑铲跳落地接的那一铲照样没有加速
		for (const EGinkgoWindowEvent Event : {EGinkgoWindowEvent::SlideEnded, EGinkgoWindowEvent::Jumped, EGinkgoWindowEvent::Landed, EGinkgoWindowEvent::WalkedOffLedge})
		{
			H.Send(Event);
		}
		TestEqual(TEXT("别的事件不结束滑铲疲劳"), H.Count(0), 1);

		// 1.5 s 后再铲（没拿到加速，标志为假）：刷新，从 0 重新计时
		H.Advance(1.5f);
		H.Send(EGinkgoWindowEvent::SlideStarted, 0.f, false);
		TestTrue(TEXT("没拿到加速的滑铲开始也刷新"), H.Count(0) == 1 && H.Windows[0].PhaseTime == 0.f);
		H.Advance(1.5f);
		TestEqual(TEXT("刷新后再过 1.5 s 还在：从第二铲算起"), H.Multiplier(SlideBoost), 0.f);
	}

	// 2 s：推入那次移动也推进一整段 Δt，第 N 次移动开始滑铲时，第 N + K 次移动（K = 2 s / Δt）读到的倍率回到 1，
	// 即两次滑铲开始的移动相隔整 2 s。2 s 是 30 / 60 / 144 fps 帧长的整数倍，三个帧率都不差帧
	// （144 fps 下 288 次 float 累加比 2 少 5.5e-6 s，靠管理器的 1e-5 s 容差才不晚一次）
	for (const int32 Fps : {30, 60, 144})
	{
		FHarness H;
		H.Definitions = {Fatigue};
		H.Send(EGinkgoWindowEvent::SlideStarted, 438.15f, true);
		const int32 Moves = H.StepsUntil(1.f / Fps, 1000, [&H, &SlideBoost] { return H.Multiplier(SlideBoost) == 1.f; });
		TestEqual(*FString::Printf(TEXT("%d fps：疲劳的移动次数"), Fps), Moves, 2 * Fps);
	}
	return true;
}

// ===== 跳跃疲劳：类默认值带的第三个窗口，土狼时间推入时清除它 =====

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowJumpFatigueTest, "CaseGinkgo.Movement.Window.JumpFatigue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowJumpFatigueTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	const FGameplayTag& JumpPenalty = FMovementTags::Movement_Window_Effect_JumpPenalty;
	const TArray<TObjectPtr<UGinkgoMovementWindow>>& Defaults = GetDefault<UGinkgoMovementConfig>()->Windows;
	const auto IndexOf = [&Defaults](const FGameplayTag& Tag)
	{
		return Defaults.IndexOfByPredicate([&Tag](const TObjectPtr<UGinkgoMovementWindow>& Window) { return Window && Window->Tag == Tag; });
	};
	const int32 FatigueIndex = IndexOf(FMovementTags::Movement_Window_JumpFatigue);
	const int32 CoyoteIndex = IndexOf(FMovementTags::Movement_Window_Coyote);
	const int32 SlideFatigueIndex = IndexOf(FMovementTags::Movement_Window_SlideFatigue);
	if (!TestTrue(TEXT("移动配置的类默认值带跳跃疲劳与土狼时间"), FatigueIndex != INDEX_NONE && CoyoteIndex != INDEX_NONE))
	{
		return false;
	}
	// 状态按下标对应定义：新加的窗口排在已有的后面
	TestTrue(TEXT("跳跃疲劳排在已有的默认窗口后面"), CoyoteIndex < FatigueIndex && SlideFatigueIndex < FatigueIndex);
	UGinkgoMovementWindow* Fatigue = Defaults[FatigueIndex];
	UGinkgoMovementWindow* Coyote = Defaults[CoyoteIndex];

	// 起跳打开开关（Armed，不打折，在空中多久都不结束），落地开始计时（Recovering）：0.15 s 内 1/4，之后线性回升，0.75 s 到时
	{
		FHarness H;
		H.Definitions = {Fatigue};
		TestEqual(TEXT("没跳过时不打折"), H.Multiplier(JumpPenalty), 1.f);
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		TestTrue(TEXT("起跳推入 Armed"), H.Count(0) == 1 && H.Tags().HasTagExact(FMovementTags::Movement_Window_JumpFatigue_Armed));
		H.Advance(2.f);
		TestTrue(TEXT("Armed 不打折，不定长"), H.Count(0) == 1 && H.Multiplier(JumpPenalty) == 1.f);
		H.Send(EGinkgoWindowEvent::Landed);
		TestTrue(TEXT("落地转入 Recovering"), H.Tags().HasTagExact(FMovementTags::Movement_Window_JumpFatigue_Recovering));
		TestEqual(TEXT("落地那一刻 1/4"), H.Multiplier(JumpPenalty), 0.25f, 1e-6f);
		float Elapsed = 0.f;
		for (const TPair<float, float>& Expected : {TPair<float, float>(0.1f, 0.25f), TPair<float, float>(0.15f, 0.25f), TPair<float, float>(0.3f, 0.4375f),
			TPair<float, float>(0.45f, 0.625f), TPair<float, float>(0.6f, 0.8125f), TPair<float, float>(0.74f, 0.9875f)})
		{
			H.Advance(Expected.Key - Elapsed);
			Elapsed = Expected.Key;
			TestEqual(*FString::Printf(TEXT("落地后 %.2f s 的倍率"), Expected.Key), H.Multiplier(JumpPenalty), Expected.Value, 1e-5f);
		}
		H.Advance(0.75f - Elapsed);
		TestTrue(TEXT("0.75 s 到时结束，不再打折"), H.Count(0) == 0 && H.Multiplier(JumpPenalty) == 1.f);
	}

	// Recovering 里再起跳：这一跳读到刷新前的倍率（移动组件在起跳事件之前读），之后刷新回 Armed，参数是这次的倍率
	{
		FHarness H;
		H.Definitions = {Fatigue};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		H.Send(EGinkgoWindowEvent::Landed);
		H.Advance(0.05f);
		const float Scale = H.Multiplier(JumpPenalty);
		H.Send(EGinkgoWindowEvent::Jumped, Scale, Scale < 1.f);
		TestEqual(TEXT("连跳的这一跳只有 1/4"), Scale, 0.25f, 1e-6f);
		TestTrue(TEXT("再起跳刷新回 Armed"), H.Count(0) == 1 && H.Windows[0].Phase == 0 && H.Windows[0].PhaseTime == 0.f);
		TestEqual(TEXT("参数是这次起跳的倍率"), H.Windows[0].Param, 0.25f, 1e-6f);
	}

	// 走下边缘：跳跃疲劳自己不处理；土狼时间推入时清除它，之后落地也不再有疲劳
	{
		FHarness Alone;
		Alone.Definitions = {Fatigue};
		Alone.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		Alone.Send(EGinkgoWindowEvent::Landed);
		Alone.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestEqual(TEXT("没有土狼时间时，走下边缘不结束跳跃疲劳"), Alone.Count(0), 1);

		FHarness H;
		H.Definitions = {Coyote, Fatigue};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		H.Send(EGinkgoWindowEvent::Landed);
		H.Advance(0.1f);
		H.Manager.TakeChanges();
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		TestTrue(TEXT("推入土狼时间，清除跳跃疲劳"), H.Count(0) == 1 && H.Count(1) == 0);
		const TArray<FGinkgoMovementWindowManager::FChange, TInlineAllocator<8>> Changes = H.Manager.TakeChanges();
		TestTrue(TEXT("变化：推入土狼时间，结束 Recovering"), Changes.Num() == 2 && IsChange(Changes[0], 0, INDEX_NONE, 0) && IsChange(Changes[1], 1, 1, INDEX_NONE));
		H.Advance(0.25f);
		H.Send(EGinkgoWindowEvent::Landed);
		TestTrue(TEXT("之后落地也没有疲劳"), H.Windows.IsEmpty() && H.Multiplier(JumpPenalty) == 1.f);
	}

	// 土狼起跳算一次普通的地面起跳：起跳事件结束土狼时间，跳跃疲劳从 Armed 开始
	{
		FHarness H;
		H.Definitions = {Coyote, Fatigue};
		H.Send(EGinkgoWindowEvent::WalkedOffLedge);
		H.Advance(1.f / 60.f);
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		TestTrue(TEXT("土狼起跳打开疲劳开关"), H.Count(0) == 0 && H.Count(1) == 1 && H.Windows.Num() == 1 && H.Windows[0].Phase == 0);
	}

	// Recovering 0.75 s：落地那次移动把阶段计时清零后也推进一整段 Δt，第 R 次移动结束时去掉，R 是 R·Δt ≥ 0.75 的最小整数
	for (const TPair<int32, int32>& Expected : {TPair<int32, int32>(30, 23), TPair<int32, int32>(60, 45), TPair<int32, int32>(144, 108)})
	{
		FHarness H;
		H.Definitions = {Fatigue};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		H.Send(EGinkgoWindowEvent::Landed);
		const int32 Moves = H.StepsUntil(1.f / Expected.Key, 1000, [&H] { return H.Windows.IsEmpty(); });
		TestEqual(*FString::Printf(TEXT("%d fps：Recovering 的移动次数"), Expected.Key), Moves, Expected.Value);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWindowLurchTest, "CaseGinkgo.Movement.Window.Lurch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWindowLurchTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementWindowTests;

	const TArray<TObjectPtr<UGinkgoMovementWindow>>& Defaults = GetDefault<UGinkgoMovementConfig>()->Windows;
	const auto IndexOf = [&Defaults](const FGameplayTag& Tag)
	{
		return Defaults.IndexOfByPredicate([&Tag](const TObjectPtr<UGinkgoMovementWindow>& Window) { return Window && Window->Tag == Tag; });
	};
	const int32 LurchIndex = IndexOf(FMovementTags::Movement_Window_Lurch);
	UGinkgoLurchWindow* Lurch = LurchIndex != INDEX_NONE ? Cast<UGinkgoLurchWindow>(Defaults[LurchIndex]) : nullptr;
	if (!TestNotNull(TEXT("移动配置的类默认值带 lurch 窗口"), Lurch))
	{
		return false;
	}
	// 状态按下标对应定义：新加的窗口排在已有的后面
	TestTrue(TEXT("lurch 排在跳跃疲劳后面"), IndexOf(FMovementTags::Movement_Window_JumpFatigue) < LurchIndex);

	const FVector Forward(762.0, 0.0, 0.0);
	const FVector RightInput(0.0, 1.0, 0.0);
	// 一次方向键按下：这次移动的速度与按着的方向键合起来的方向；输入加速度与它相同（没有反方向的两个键一起按着）
	const auto Press = [](FHarness& H, const FVector& Velocity, const FVector& KeyDirection)
	{
		H.Context = FGinkgoWindowEventContext();
		H.Context.Velocity = Velocity;
		H.Context.Acceleration = KeyDirection * 1000.0;
		H.Context.KeyDirection = KeyDirection;
		return H.Manager.HandleEvent(H.Definitions, H.Windows, {EGinkgoWindowEvent::DirectionPressed, 1.f, false}, H.Context);
	};

	// 强度：α = 0.8 乘曲线，曲线 0.2 s 内为 0.7（2026-10-28 你调的），之后线性降到 0.4 s 时为 0。满强度 0.56
	constexpr float FullStrength = 0.56f;
	for (const TPair<float, float>& Expected : {TPair<float, float>(0.f, FullStrength), TPair<float, float>(0.1f, FullStrength), TPair<float, float>(0.2f, FullStrength),
		TPair<float, float>(0.3f, 0.28f), TPair<float, float>(0.35f, 0.14f), TPair<float, float>(0.4f, 0.f)})
	{
		TestEqual(*FString::Printf(TEXT("已过 %.2f s 的强度"), Expected.Key), Lurch->GetStrengthAt(Expected.Key, 1.f), Expected.Value, 1e-5f);
	}

	// 只有不疲劳的起跳推入
	{
		FHarness H;
		H.Definitions = {Lurch};
		H.Send(EGinkgoWindowEvent::Jumped, 0.25f, true);
		TestEqual(TEXT("疲劳的起跳不推入"), H.Count(0), 0);
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		TestEqual(TEXT("不疲劳的起跳推入"), H.Count(0), 1);
	}

	// 起跳那次移动里的按下不算；之后第一次按下按满强度转向，用掉窗口，事件被消费；再按什么也不改
	{
		FHarness H;
		H.Definitions = {Lurch};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		TestFalse(TEXT("起跳那次移动里的按下不消费"), Press(H, Forward, RightInput));
		TestTrue(TEXT("起跳那次移动里的按下不转向、不用掉窗口"), H.Context.Velocity == Forward && H.Count(0) == 1);
		H.Advance(1.f / 60.f);
		H.Manager.TakeChanges();
		TestTrue(TEXT("之后的按下被消费"), Press(H, Forward, RightInput));
		TestEqual(TEXT("满强度向侧面按：转到 (0.44, 0.56) × 速度"), H.Context.Velocity, FVector(335.28, 426.72, 0.0), 1e-3f);
		const TArray<FGinkgoMovementWindowManager::FChange, TInlineAllocator<8>> Changes = H.Manager.TakeChanges();
		TestTrue(TEXT("用掉窗口：变化是结束"), H.Windows.IsEmpty() && Changes.Num() == 1 && IsChange(Changes[0], 0, 0, INDEX_NONE));
		TestFalse(TEXT("用掉以后再按不消费"), Press(H, H.Context.Velocity, -RightInput));
	}

	// 没有方向（左右抵消）、超过速度上限时不转，窗口留着；强度随时间衰减
	{
		FHarness H;
		H.Definitions = {Lurch};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		H.Advance(0.1f);
		TestFalse(TEXT("没有方向时不消费"), Press(H, Forward, FVector::ZeroVector));
		TestEqual(TEXT("没有方向时窗口留着"), H.Count(0), 1);
		const FVector TooFast(Lurch->MaxSpeed + 1.0, 0.0, 0.0);
		TestFalse(TEXT("超过速度上限时不消费"), Press(H, TooFast, RightInput));
		TestTrue(TEXT("超过速度上限时不转、窗口留着"), H.Context.Velocity == TooFast && H.Count(0) == 1);
		H.Advance(0.2f);
		TestTrue(TEXT("0.3 s 时按下"), Press(H, Forward, RightInput));
		TestEqual(TEXT("0.3 s 时强度 0.28"), H.Context.Velocity, FVector(762.0 * 0.72, 762.0 * 0.28, 0.0), 1e-3f);
	}

	// 方向取按着的方向键（后按的赢），不取输入加速度：按住 A 向左跑、起跳，按下 D 时 A 还没松开，输入加速度左右抵消为零，
	// 方向键合起来是 D（后按的），照样向右转满强度
	{
		FHarness H;
		H.Definitions = {Lurch};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		H.Advance(0.1f);
		H.Context = FGinkgoWindowEventContext();
		H.Context.Velocity = FVector(0.0, -762.0, 0.0);
		H.Context.Acceleration = FVector::ZeroVector;
		H.Context.KeyDirection = RightInput;
		TestTrue(TEXT("输入加速度为零、方向键向右时照样转"), H.Manager.HandleEvent(H.Definitions, H.Windows, {EGinkgoWindowEvent::DirectionPressed, 1.f, false}, H.Context));
		TestEqual(TEXT("往回按：反向、剩 |2k − 1|"), H.Context.Velocity, FVector(0.0, 762.0 * (2.0 * double(FullStrength) - 1.0), 0.0), 1e-3f);
	}

	// 落地结束；0.4 s 到时结束：60 fps 下算上起跳那次移动，第 24 次移动结束时去掉
	{
		FHarness H;
		H.Definitions = {Lurch};
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		H.Send(EGinkgoWindowEvent::Landed);
		TestEqual(TEXT("落地结束"), H.Count(0), 0);
		H.Send(EGinkgoWindowEvent::Jumped, 1.f, false);
		TestEqual(TEXT("60 fps 下 0.4 s 是 24 次移动"), H.StepsUntil(1.f / 60.f, 100, [&H] { return H.Windows.IsEmpty(); }), 24);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

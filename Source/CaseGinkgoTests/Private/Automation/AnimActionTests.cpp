#include "Algo/Count.h"
#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionChain.h"
#include "Animation/Actions/GinkgoAnimActionRunner.h"
#include "Animation/AnimMontage.h"
#include "Misc/AutomationTest.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Utilities/Enumeration/Tags/TestTags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoAnimActionTests
{
	constexpr float DeltaTime = 1.f / 60.f;

	/** 骨架里的两个槽组：全身 DefaultSlot、上半身 UpperBody */
	const FName FullBody(TEXT("DefaultGroup"));
	const FName UpperBody(TEXT("Partials"));

	/**
	 * 假宿主：照引擎的规则放与停，事件由测试按帧发。
	 * - 放新的蒙太奇时，以打断的方式停掉同一组里原来的（StopAllMontagesByGroupName）。
	 * - 停的时候发淡出事件：默认当场回调（在动画 tick 之外调用时引擎就是这样）；bQueueStopEvents 时排到 FlushQueued（在动画 tick 里调用时）。
	 * - 被打断的实例不再发通知：Notify 照此过滤；ForceNotify 不管，用来查执行器自己的过滤。
	 */
	class FFakeMontageHost final : public IGinkgoMontageHost
	{
	public:
		struct FInstance
		{
			UAnimMontage* Montage = nullptr;
			FName Group;
			bool bAlive = true;
			bool bStopped = false;
			bool bInterrupted = false;
		};

		FGinkgoAnimActionRunner* Runner = nullptr;
		bool bQueueStopEvents = false;
		bool bFailPlays = false;
		TMap<int32, FInstance> Instances;
		TMap<const UAnimMontage*, FName> Groups;
		TMap<const UAnimMontage*, TMap<FName, float>> EventTimes;

		/** 调用记录："Play <蒙太奇> #1"、"PlayFailed"、"Stop #1"、"Jump #1 Close"、"Next #1 Loop Close" */
		TArray<FString> Calls;

		virtual int32 PlayMontage(UAnimMontage* Montage, const float /*PlayRate*/, const FName /*StartSection*/, const float /*StartPosition*/, const float /*BlendInTime*/) override
		{
			if (!Montage || bFailPlays)
			{
				Calls.Add(TEXT("PlayFailed"));
				return INDEX_NONE;
			}
			const FName Group = GetGroup(Montage);
			TArray<int32> SameGroup;
			for (const TPair<int32, FInstance>& Pair : Instances)
			{
				if (Pair.Value.bAlive && !Pair.Value.bStopped && Pair.Value.Group == Group)
				{
					SameGroup.Add(Pair.Key);
				}
			}
			for (const int32 Interrupted : SameGroup)
			{
				StopInstance(Interrupted);
			}

			const int32 InstanceID = NextID++;
			FInstance NewInstance;
			NewInstance.Montage = Montage;
			NewInstance.Group = Group;
			Instances.Add(InstanceID, NewInstance);
			Calls.Add(FString::Printf(TEXT("Play %s #%d"), *Montage->GetName(), InstanceID));
			return InstanceID;
		}

		virtual void StopMontage(const int32 InstanceID, const float /*BlendOutTime*/) override
		{
			Calls.Add(FString::Printf(TEXT("Stop #%d"), InstanceID));
			StopInstance(InstanceID);
		}

		virtual void JumpToSection(const int32 InstanceID, const FName Section) override
		{
			Calls.Add(FString::Printf(TEXT("Jump #%d %s"), InstanceID, *Section.ToString()));
		}

		virtual void SetNextSection(const int32 InstanceID, const FName From, const FName To) override
		{
			Calls.Add(FString::Printf(TEXT("Next #%d %s %s"), InstanceID, *From.ToString(), *To.ToString()));
		}

		virtual bool IsInstanceAlive(const int32 InstanceID) const override
		{
			const FInstance* Instance = Instances.Find(InstanceID);
			return Instance && Instance->bAlive;
		}

		virtual float GetPosition(const int32 /*InstanceID*/) const override
		{
			return 0.f;
		}

		virtual FName GetGroup(const UAnimMontage* Montage) const override
		{
			const FName* Group = Groups.Find(Montage);
			return Group ? *Group : FullBody;
		}

		virtual float GetEventTime(const UAnimMontage* Montage, const FName Event) const override
		{
			const TMap<FName, float>* Times = EventTimes.Find(Montage);
			const float* Time = Times ? Times->Find(Event) : nullptr;
			return Time ? *Time : -1.f;
		}

		// ===== 测试按帧发的事件 =====

		/** 照引擎：被打断的实例不再发通知 */
		void Notify(const int32 InstanceID, const FName Name)
		{
			const FInstance* Instance = Instances.Find(InstanceID);
			if (Instance && Instance->bAlive && !Instance->bInterrupted)
			{
				Runner->HandleNotify(InstanceID, Name);
			}
		}

		/** 不管引擎的规则硬发：查执行器自己的过滤 */
		void ForceNotify(const int32 InstanceID, const FName Name)
		{
			Runner->HandleNotify(InstanceID, Name);
		}

		void WindowBegin(const int32 InstanceID, const FName Name)
		{
			Runner->HandleWindowBegin(InstanceID, Name);
		}

		void WindowEnd(const int32 InstanceID, const FName Name)
		{
			Runner->HandleWindowEnd(InstanceID, Name);
		}

		/** 放到结尾前一个淡出时长：自然淡出，不算打断 */
		void AutoBlendOut(const int32 InstanceID)
		{
			Instances.FindChecked(InstanceID).bStopped = true;
			Runner->HandleBlendingOut(InstanceID, false);
		}

		void End(const int32 InstanceID)
		{
			FInstance& Instance = Instances.FindChecked(InstanceID);
			Instance.bAlive = false;
			Runner->HandleEnded(InstanceID, Instance.bInterrupted);
		}

		/** 实例丢了，一个事件都没有（例如动画实例被重新初始化） */
		void Vanish(const int32 InstanceID)
		{
			Instances.FindChecked(InstanceID).bAlive = false;
		}

		void FlushQueued()
		{
			TArray<TFunction<void()>> Events = MoveTemp(Queued);
			Queued.Reset();
			for (const TFunction<void()>& Event : Events)
			{
				Event();
			}
		}

		int32 LastID() const
		{
			return NextID - 1;
		}

		int32 Count(const FString& Prefix) const
		{
			return static_cast<int32>(Algo::CountIf(Calls, [&Prefix](const FString& Call) { return Call.StartsWith(Prefix); }));
		}

	private:
		TArray<TFunction<void()>> Queued;
		int32 NextID = 1;

		void StopInstance(const int32 InstanceID)
		{
			FInstance* Instance = Instances.Find(InstanceID);
			if (!Instance || !Instance->bAlive || Instance->bStopped)
			{
				return;
			}
			Instance->bStopped = true;
			Instance->bInterrupted = true;
			TFunction<void()> Event = [this, InstanceID] { Runner->HandleBlendingOut(InstanceID, true); };
			if (bQueueStopEvents)
			{
				Queued.Add(MoveTemp(Event));
			}
			else
			{
				Event();
			}
		}
	};

	UAnimMontage* MakeMontage(FFakeMontageHost& Host, const TCHAR* Name, const FName Group)
	{
		const FName ObjectName = MakeUniqueObjectName(GetTransientPackage(), UAnimMontage::StaticClass(), FName(Name));
		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage(), ObjectName);
		Host.Groups.Add(Montage, Group);
		return Montage;
	}

	/** 放 Montage 的动作；Extra 接着声明别的 */
	UGinkgoAnimAction* MakePlay(UAnimMontage* Montage, const TCHAR* Name, TFunction<void(FGinkgoAnimActionSetup&)> Extra = nullptr)
	{
		return UGinkgoAnimAction::Make(nullptr, Name, [Montage, Extra](FGinkgoAnimActionSetup& Setup)
		{
			Setup.Play(Montage);
			if (Extra)
			{
				Extra(Setup);
			}
		});
	}

	/** 记下拥有者收到的结局 */
	struct FRecorded
	{
		int32 Count = 0;
		FGinkgoAnimActionResult Last;

		FGinkgoAnimActionRunner::FFinished Callback()
		{
			return [this](UGinkgoAnimAction&, const FGinkgoAnimActionResult& Result)
			{
				++Count;
				Last = Result;
			};
		}
	};

	void TestResult(FAutomationTestBase& Test, const TCHAR* What, const FGinkgoAnimActionResult& Actual,
		const EGinkgoAnimActionOutcome Outcome, const FGameplayTag& Reason = FGameplayTag())
	{
		const FGinkgoAnimActionResult Expected(Outcome, Reason);
		Test.TestTrue(FString::Printf(TEXT("%s：%s，应为 %s"), What, *Actual.ToString(), *Expected.ToString()),
			Actual.Outcome == Outcome && Actual.Reason == Reason);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionLifecycleTest, "CaseGinkgo.Animation.Action.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* Montage = MakeMontage(Host, TEXT("Pickup"), FullBody);

	int32 Hits = 0;
	UGinkgoAnimAction* Action = MakePlay(Montage, TEXT("Pickup"), [&Hits](FGinkgoAnimActionSetup& Setup)
	{
		Setup.On(TEXT("Hit"), [&Hits](FGinkgoAnimActionEvent&) { ++Hits; }, EGinkgoMissedEvent::CatchUp);
	});
	FRecorded Recorded;
	Runner.Start(Action, Recorded.Callback());
	TestEqual(TEXT("启动时只记下，应用阶段才放"), Host.Count(TEXT("Play ")), 0);

	Runner.Tick(DeltaTime);
	TestEqual(TEXT("应用阶段放了一次"), Host.Count(TEXT("Play ")), 1);
	TestTrue(TEXT("在跑"), Action->IsRunning());
	const int32 InstanceID = Host.LastID();
	TestEqual(TEXT("记下实例 ID"), Action->GetInstanceID(), InstanceID);

	Host.Notify(InstanceID, TEXT("Hit"));
	TestEqual(TEXT("事件发了一次"), Hits, 1);

	Host.AutoBlendOut(InstanceID);
	TestEqual(TEXT("自然淡出时报告一次"), Recorded.Count, 1);
	TestResult(*this, TEXT("自然淡出"), Recorded.Last, EGinkgoAnimActionOutcome::Completed);
	TestEqual(TEXT("发过的事件不补做"), Hits, 1);

	// 淡出里、结束后再来的事件都不进来
	Host.ForceNotify(InstanceID, TEXT("Hit"));
	Host.End(InstanceID);
	Runner.Tick(DeltaTime);
	TestEqual(TEXT("结局之后的事件不进来"), Hits, 1);
	TestEqual(TEXT("只报告一次"), Recorded.Count, 1);
	TestEqual(TEXT("完成的不停，让它自己放完"), Host.Count(TEXT("Stop")), 0);
	TestEqual(TEXT("执行器里没有动作了"), Runner.GetActions().Num(), 0);

	// 没有蒙太奇的动作：开始后马上完成
	FRecorded Instant;
	Runner.Start(UGinkgoAnimAction::Make(nullptr, TEXT("Instant"), [](FGinkgoAnimActionSetup&) {}), Instant.Callback());
	Runner.Tick(DeltaTime);
	TestResult(*this, TEXT("没有蒙太奇的动作"), Instant.Last, EGinkgoAnimActionOutcome::Completed);
	TestEqual(TEXT("没有蒙太奇的动作不放"), Host.Count(TEXT("Play ")), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionOnlyOnceTest, "CaseGinkgo.Animation.Action.OnlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionOnlyOnceTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* Montage = MakeMontage(Host, TEXT("Door"), FullBody);

	const auto MakeGuarded = [Montage](bool& bOutOfReach)
	{
		return MakePlay(Montage, TEXT("Guarded"), [&bOutOfReach](FGinkgoAnimActionSetup& Setup)
		{
			Setup.AbortIf(NAME_None, [&bOutOfReach](const FGinkgoAnimActionContext&) { return bOutOfReach; },
				FTestTags::GinkgoTest_AnimAction_Abort);
		});
	};

	// 中止条件成立：同一帧停掉；停的时候淡出事件当场回来，不再算一次
	{
		bool bOutOfReach = false;
		FRecorded Recorded;
		Runner.Start(MakeGuarded(bOutOfReach), Recorded.Callback());
		Runner.Tick(DeltaTime);
		const int32 InstanceID = Host.LastID();
		Runner.Tick(DeltaTime);
		TestEqual(TEXT("条件不成立时一直在跑"), Recorded.Count, 0);

		bOutOfReach = true;
		Runner.Tick(DeltaTime);
		TestResult(*this, TEXT("中止条件"), Recorded.Last, EGinkgoAnimActionOutcome::Aborted, FTestTags::GinkgoTest_AnimAction_Abort);
		TestEqual(TEXT("同一帧停掉"), Host.Count(FString::Printf(TEXT("Stop #%d"), InstanceID)), 1);
		TestEqual(TEXT("停的时候当场回来的淡出不再算一次"), Recorded.Count, 1);
		Host.End(InstanceID);
		TestEqual(TEXT("结束事件不再算一次"), Recorded.Count, 1);
	}

	// 被别人停了：中止（Interrupted），不另停；同一帧中止条件也成立，第一个到的算数
	{
		bool bOutOfReach = false;
		FRecorded Recorded;
		Runner.Start(MakeGuarded(bOutOfReach), Recorded.Callback());
		Runner.Tick(DeltaTime);
		const int32 InstanceID = Host.LastID();
		Runner.HandleBlendingOut(InstanceID, true);
		bOutOfReach = true;
		Runner.Tick(DeltaTime);
		TestResult(*this, TEXT("被别人停了"), Recorded.Last, EGinkgoAnimActionOutcome::Aborted, FAnimActionTags::AnimAction_Abort_Interrupted);
		TestEqual(TEXT("只报告一次"), Recorded.Count, 1);
		TestEqual(TEXT("已经停了的不另停"), Host.Count(FString::Printf(TEXT("Stop #%d"), InstanceID)), 0);
	}

	// 没有淡出就结束了（淡出时长为 0）：同样的结局
	{
		bool bOutOfReach = false;
		FRecorded Recorded;
		Runner.Start(MakeGuarded(bOutOfReach), Recorded.Callback());
		Runner.Tick(DeltaTime);
		Host.End(Host.LastID());
		TestResult(*this, TEXT("只有结束事件"), Recorded.Last, EGinkgoAnimActionOutcome::Completed);
		TestEqual(TEXT("只报告一次"), Recorded.Count, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionCatchUpTest, "CaseGinkgo.Animation.Action.CatchUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionCatchUpTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* Montage = MakeMontage(Host, TEXT("Pickup"), FullBody);
	TMap<FName, float>& Times = Host.EventTimes.Add(Montage);
	Times.Add(TEXT("Fired"), 0.1f);
	Times.Add(TEXT("Early"), 0.2f);
	Times.Add(TEXT("Skipped"), 0.4f);
	Times.Add(TEXT("Late"), 0.6f);

	TArray<FString> Order;
	const auto Record = [&Order](const TCHAR* Label)
	{
		return [&Order, Label](FGinkgoAnimActionEvent& Event)
		{
			Order.Add(FString::Printf(TEXT("%s %s %s"), Label, Event.bCaughtUp ? TEXT("caught-up") : TEXT("fired"), *Event.Result.ToString()));
		};
	};

	bool bOutOfReach = false;
	UGinkgoAnimAction* Action = MakePlay(Montage, TEXT("Pickup"), [&](FGinkgoAnimActionSetup& Setup)
	{
		// 声明的先后故意与在蒙太奇里的先后不同；Missing 在蒙太奇里没有
		Setup.On(TEXT("Missing"), Record(TEXT("Missing")), EGinkgoMissedEvent::CatchUp);
		Setup.On(TEXT("Late"), Record(TEXT("Late")), EGinkgoMissedEvent::CatchUp);
		Setup.On(TEXT("Skipped"), Record(TEXT("Skipped")), EGinkgoMissedEvent::Skip);
		Setup.On(TEXT("Early"), Record(TEXT("Early")), EGinkgoMissedEvent::CatchUp);
		Setup.On(TEXT("Fired"), Record(TEXT("Fired")), EGinkgoMissedEvent::CatchUp);
		Setup.AbortIf(NAME_None, [&bOutOfReach](const FGinkgoAnimActionContext&) { return bOutOfReach; }, FTestTags::GinkgoTest_AnimAction_Abort);
	});
	FRecorded Recorded;
	Runner.Start(Action, Recorded.Callback());
	Runner.Tick(DeltaTime);
	const int32 InstanceID = Host.LastID();

	// 循环段里一个事件可以发多次
	Host.Notify(InstanceID, TEXT("Fired"));
	Host.Notify(InstanceID, TEXT("Fired"));
	bOutOfReach = true;
	Runner.Tick(DeltaTime);

	const FString Running = FGinkgoAnimActionResult().ToString();
	const FString Abort = FGinkgoAnimActionResult::Aborted(FTestTags::GinkgoTest_AnimAction_Abort).ToString();
	const TArray<FString> Expected = {
		FString::Printf(TEXT("Fired fired %s"), *Running),
		FString::Printf(TEXT("Fired fired %s"), *Running),
		FString::Printf(TEXT("Early caught-up %s"), *Abort),
		FString::Printf(TEXT("Late caught-up %s"), *Abort),
		FString::Printf(TEXT("Missing caught-up %s"), *Abort),
	};
	TestEqual(TEXT("补做按在蒙太奇里的时刻先后（没有的排最后），跳过的不做，发过的不补"),
		FString::Join(Order, TEXT(" | ")), FString::Join(Expected, TEXT(" | ")));
	TestEqual(TEXT("报告一次"), Recorded.Count, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionInstancesTest, "CaseGinkgo.Animation.Action.Instances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionInstancesTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* Montage = MakeMontage(Host, TEXT("Pickup"), FullBody);

	int32 FirstHits = 0;
	int32 SecondHits = 0;
	const auto CountHits = [](int32& Hits)
	{
		return [&Hits](FGinkgoAnimActionSetup& Setup)
		{
			Setup.On(TEXT("Hit"), [&Hits](FGinkgoAnimActionEvent&) { ++Hits; }, EGinkgoMissedEvent::Skip);
		};
	};

	// 连着拾取两次：第一个自然淡出完成，蒙太奇还在淡出时第二个放同一个蒙太奇
	FRecorded FirstRecorded;
	Runner.Start(MakePlay(Montage, TEXT("First"), CountHits(FirstHits)), FirstRecorded.Callback());
	Runner.Tick(DeltaTime);
	const int32 FirstID = Host.LastID();
	Host.AutoBlendOut(FirstID);

	FRecorded SecondRecorded;
	Runner.Start(MakePlay(Montage, TEXT("Second"), CountHits(SecondHits)), SecondRecorded.Callback());
	Runner.Tick(DeltaTime);
	const int32 SecondID = Host.LastID();
	TestTrue(TEXT("同一个蒙太奇连放两次是两个实例"), FirstID != SecondID);

	// 引擎实际不会发（被顶掉的实例不再发通知），这里硬发，查执行器自己按实例的过滤
	Host.ForceNotify(FirstID, TEXT("Hit"));
	TestEqual(TEXT("上一个实例的事件不进下一个动作"), SecondHits, 0);
	TestEqual(TEXT("结束了的动作也不收"), FirstHits, 0);

	Host.Notify(SecondID, TEXT("Hit"));
	TestEqual(TEXT("自己实例的事件"), SecondHits, 1);
	TestResult(*this, TEXT("第一个"), FirstRecorded.Last, EGinkgoAnimActionOutcome::Completed);
	TestEqual(TEXT("第二个还在跑"), SecondRecorded.Count, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionChainHopTest, "CaseGinkgo.Animation.Action.ChainHop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionChainHopTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;

	// 换段时上一步的"被打断"回调：在 Montage_Play 里当场回来，或者排到帧末，两种都不能改上一步的结局
	for (const bool bQueue : {false, true})
	{
		const FString When = bQueue ? TEXT("排到帧末") : TEXT("当场回调");
		FFakeMontageHost Host;
		FGinkgoAnimActionRunner Runner(Host);
		Host.Runner = &Runner;
		Host.bQueueStopEvents = bQueue;
		UAnimMontage* Stow = MakeMontage(Host, TEXT("Stow"), UpperBody);
		UAnimMontage* Draw = MakeMontage(Host, TEXT("Draw"), UpperBody);

		TArray<FString> Order;
		UGinkgoAnimActionChain* Chain = UGinkgoAnimActionChain::MakeChain(nullptr, TEXT("SwitchWeapon"), [&](FGinkgoAnimActionChainBuilder& Builder)
		{
			Builder.Step(TEXT("Stow"), [&]
			{
				return MakePlay(Stow, TEXT("Stow"), [](FGinkgoAnimActionSetup& Setup) { Setup.CompleteAt(TEXT("Done")); });
			}).Next(TEXT("Relink"));
			Builder.Do(TEXT("Relink"), [&Order] { Order.Add(TEXT("Relink")); }).Next(TEXT("Draw"));
			Builder.Step(TEXT("Draw"), [&] { return MakePlay(Draw, TEXT("Draw")); });
		});
		FRecorded Recorded;
		Runner.Start(Chain, Recorded.Callback());
		Runner.Tick(DeltaTime);
		const int32 StowID = Host.LastID();
		UGinkgoAnimAction* StowAction = Chain->GetCurrentStep();
		TestTrue(When + TEXT("：收枪在跑"), StowAction && StowAction->IsRunning());
		if (!StowAction)
		{
			return false;
		}

		Host.Notify(StowID, TEXT("Done"));
		TestResult(*this, *(When + TEXT("：收枪放到完成点")), StowAction->GetResult(), EGinkgoAnimActionOutcome::Completed);
		TestEqual(When + TEXT("：完成点之后还没换段"), Host.Count(TEXT("Play ")), 1);

		Runner.Tick(DeltaTime);
		Host.FlushQueued();
		const int32 DrawID = Host.LastID();
		TestEqual(When + TEXT("：换层在收枪之后、拔枪之前"), FString::Join(Order, TEXT(",")), FString(TEXT("Relink")));
		TestEqual(When + TEXT("：下一帧放拔枪"), Host.Count(TEXT("Play ")), 2);
		TestTrue(When + TEXT("：拔枪是新的实例"), DrawID != StowID);
		TestResult(*this, *(When + TEXT("：拔枪顶掉收枪时回来的打断不改收枪的结局")), StowAction->GetResult(), EGinkgoAnimActionOutcome::Completed);
		TestEqual(When + TEXT("：同组顶掉，不另停"), Host.Count(TEXT("Stop")), 0);
		TestTrue(When + TEXT("：链还在跑，当前步是拔枪"), Chain->IsRunning() && Chain->GetCurrentStepName() == FName(TEXT("Draw")));

		Host.AutoBlendOut(DrawID);
		Runner.Tick(DeltaTime);
		TestEqual(When + TEXT("：链报告一次"), Recorded.Count, 1);
		TestResult(*this, *(When + TEXT("：链")), Recorded.Last, EGinkgoAnimActionOutcome::Completed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionChainTransitionsTest, "CaseGinkgo.Animation.Action.ChainTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionChainTransitionsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* TakeOut = MakeMontage(Host, TEXT("TakeOut"), UpperBody);
	UAnimMontage* Throw = MakeMontage(Host, TEXT("Throw"), UpperBody);
	UAnimMontage* PutBack = MakeMontage(Host, TEXT("PutBack"), UpperBody);

	/** 投掷：拿出 → 投出；拿出被中止就收回去；按原因 Branch 中止的去 Recover */
	TArray<FString> Created;
	bool bAbort = false;
	bool bBranch = false;
	const auto MakeThrowChain = [&]()
	{
		return UGinkgoAnimActionChain::MakeChain(nullptr, TEXT("Throw"), [&](FGinkgoAnimActionChainBuilder& Builder)
		{
			Builder.Step(TEXT("TakeOut"), [&]
			{
				Created.Add(TEXT("TakeOut"));
				return MakePlay(TakeOut, TEXT("TakeOut"), [&](FGinkgoAnimActionSetup& Setup)
				{
					Setup.AbortIf(NAME_None, [&bAbort](const FGinkgoAnimActionContext&) { return bAbort; }, FTestTags::GinkgoTest_AnimAction_Abort);
					Setup.AbortIf(NAME_None, [&bBranch](const FGinkgoAnimActionContext&) { return bBranch; }, FTestTags::GinkgoTest_AnimAction_Branch);
				});
			}).Next(TEXT("Throw")).OnAborted(TEXT("PutBack")).On(EGinkgoAnimActionOutcome::Aborted, FTestTags::GinkgoTest_AnimAction_Branch, TEXT("Recover"));
			Builder.Step(TEXT("Throw"), [&] { Created.Add(TEXT("Throw")); return MakePlay(Throw, TEXT("Throw")); });
			Builder.Step(TEXT("PutBack"), [&] { Created.Add(TEXT("PutBack")); return MakePlay(PutBack, TEXT("PutBack")); });
			Builder.Step(TEXT("Recover"), [&] { Created.Add(TEXT("Recover")); return MakePlay(PutBack, TEXT("Recover")); });
		});
	};

	// 都完成：拿出 → 投出，没写 Next 的投出之后结束
	{
		Created.Reset();
		bAbort = false;
		bBranch = false;
		FRecorded Recorded;
		Runner.Start(MakeThrowChain(), Recorded.Callback());
		Runner.Tick(DeltaTime);
		Host.AutoBlendOut(Host.LastID());
		Runner.Tick(DeltaTime);
		Host.AutoBlendOut(Host.LastID());
		Runner.Tick(DeltaTime);
		TestEqual(TEXT("都完成时走过的步"), FString::Join(Created, TEXT(",")), FString(TEXT("TakeOut,Throw")));
		TestResult(*this, TEXT("都完成的链"), Recorded.Last, EGinkgoAnimActionOutcome::Completed);
	}

	// 拿出被中止：转去收回，同一帧放；链的结局是第一个没完成的步
	{
		Created.Reset();
		bAbort = false;
		bBranch = false;
		FRecorded Recorded;
		Runner.Start(MakeThrowChain(), Recorded.Callback());
		Runner.Tick(DeltaTime);
		const int32 TakeOutID = Host.LastID();
		const int32 PlaysBefore = Host.Count(TEXT("Play "));
		bAbort = true;
		Runner.Tick(DeltaTime);
		TestEqual(TEXT("中止后同一帧放收回"), Host.Count(TEXT("Play ")) - PlaysBefore, 1);
		TestEqual(TEXT("同组的收回顶掉拿出，不另停"), Host.Count(FString::Printf(TEXT("Stop #%d"), TakeOutID)), 0);
		Host.AutoBlendOut(Host.LastID());
		Runner.Tick(DeltaTime);
		TestEqual(TEXT("中止时走过的步"), FString::Join(Created, TEXT(",")), FString(TEXT("TakeOut,PutBack")));
		TestResult(*this, TEXT("链的结局是拿出的中止"), Recorded.Last, EGinkgoAnimActionOutcome::Aborted, FTestTags::GinkgoTest_AnimAction_Abort);
	}

	// 按原因写的转换比 OnAborted 优先
	{
		Created.Reset();
		bAbort = false;
		bBranch = false;
		FRecorded Recorded;
		Runner.Start(MakeThrowChain(), Recorded.Callback());
		Runner.Tick(DeltaTime);
		bBranch = true;
		Runner.Tick(DeltaTime);
		Host.AutoBlendOut(Host.LastID());
		Runner.Tick(DeltaTime);
		TestEqual(TEXT("按原因转换"), FString::Join(Created, TEXT(",")), FString(TEXT("TakeOut,Recover")));
		TestResult(*this, TEXT("按原因转换的链"), Recorded.Last, EGinkgoAnimActionOutcome::Aborted, FTestTags::GinkgoTest_AnimAction_Branch);
	}

	// 拥有者取消：当场报告，正在放的那一步停掉，不再往下走
	{
		Created.Reset();
		bAbort = false;
		bBranch = false;
		FRecorded Recorded;
		UGinkgoAnimActionChain* Chain = MakeThrowChain();
		Runner.Start(Chain, Recorded.Callback());
		Runner.Tick(DeltaTime);
		const int32 TakeOutID = Host.LastID();
		Runner.Cancel(Chain, FTestTags::GinkgoTest_AnimAction_Stop);
		TestEqual(TEXT("取消当场报告"), Recorded.Count, 1);
		TestResult(*this, TEXT("取消的链"), Recorded.Last, EGinkgoAnimActionOutcome::Cancelled, FTestTags::GinkgoTest_AnimAction_Stop);
		Runner.Tick(DeltaTime);
		TestEqual(TEXT("正在放的那一步在应用阶段停掉"), Host.Count(FString::Printf(TEXT("Stop #%d"), TakeOutID)), 1);
		TestEqual(TEXT("取消之后不往下走"), FString::Join(Created, TEXT(",")), FString(TEXT("TakeOut")));
		TestEqual(TEXT("只报告一次"), Recorded.Count, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionGroupsTest, "CaseGinkgo.Animation.Action.Groups",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionGroupsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* Door = MakeMontage(Host, TEXT("Door"), FullBody);
	UAnimMontage* Reload = MakeMontage(Host, TEXT("Reload"), UpperBody);
	UAnimMontage* Execution = MakeMontage(Host, TEXT("Execution"), FullBody);

	// 不同组的同时跑：全身开门、上半身换弹
	FRecorded DoorRecorded;
	FRecorded ReloadRecorded;
	UGinkgoAnimAction* DoorAction = MakePlay(Door, TEXT("Door"), [](FGinkgoAnimActionSetup& Setup)
	{
		Setup.HoldCameraTag(FTestTags::GinkgoTest_AnimAction_Camera);
	});
	UGinkgoAnimAction* ReloadAction = MakePlay(Reload, TEXT("Reload"));
	Runner.Start(DoorAction, DoorRecorded.Callback());
	Runner.Start(ReloadAction, ReloadRecorded.Callback());
	Runner.Tick(DeltaTime);
	TestTrue(TEXT("不同组的同时跑"), DoorAction->IsRunning() && ReloadAction->IsRunning());
	TestTrue(TEXT("全身组里是开门"), Runner.GetActionInGroup(FullBody) == DoorAction);
	TestTrue(TEXT("上半身组里是换弹"), Runner.GetActionInGroup(UpperBody) == ReloadAction);
	TestTrue(TEXT("持有的相机标签"), Runner.GetCameraTags().HasTagExact(FTestTags::GinkgoTest_AnimAction_Camera));

	// 同一组放了新动作的蒙太奇：旧的取消（Replaced），由新的顶掉，不另停；别的组不受影响
	const int32 DoorID = DoorAction->GetInstanceID();
	FRecorded ExecutionRecorded;
	Runner.Start(MakePlay(Execution, TEXT("Execution")), ExecutionRecorded.Callback());
	Runner.Tick(DeltaTime);
	TestResult(*this, TEXT("同一组放了新动作"), DoorRecorded.Last, EGinkgoAnimActionOutcome::Cancelled, FAnimActionTags::AnimAction_Cancel_Replaced);
	TestEqual(TEXT("被替换的只报告一次"), DoorRecorded.Count, 1);
	TestEqual(TEXT("被替换的由新的顶掉，不另停"), Host.Count(FString::Printf(TEXT("Stop #%d"), DoorID)), 0);
	TestTrue(TEXT("别的组不受影响"), ReloadAction->IsRunning() && ReloadRecorded.Count == 0);
	TestFalse(TEXT("被替换的相机标签放了"), Runner.GetCameraTags().HasTagExact(FTestTags::GinkgoTest_AnimAction_Camera));
	TestEqual(TEXT("在跑的动作"), Runner.GetActions().Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionHandIKTest, "CaseGinkgo.Animation.Action.HandIK",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionHandIKTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* Pick = MakeMontage(Host, TEXT("Pick"), FullBody);
	UAnimMontage* Stow = MakeMontage(Host, TEXT("StowItem"), FullBody);

	const FName ItemSocket(TEXT("Item"));
	const FName HipSocket(TEXT("Hip"));
	FGinkgoIKBinding ItemBinding;
	ItemBinding.Socket = ItemSocket;
	FGinkgoIKBinding HipBinding;
	HipBinding.Socket = HipSocket;

	// 拾取：伸手时左手绑到物品，完成点之后换到挂腰上的那一步，同一只手
	bool bAbort = false;
	UGinkgoAnimActionChain* Chain = UGinkgoAnimActionChain::MakeChain(nullptr, TEXT("PickUp"), [&](FGinkgoAnimActionChainBuilder& Builder)
	{
		Builder.Step(TEXT("Pick"), [&]
		{
			return MakePlay(Pick, TEXT("Pick"), [&](FGinkgoAnimActionSetup& Setup)
			{
				Setup.HoldHandIK(EGinkgoHand::Left, ItemBinding);
				Setup.CompleteAt(TEXT("Done"));
			});
		}).Next(TEXT("Stow"));
		Builder.Step(TEXT("Stow"), [&]
		{
			return MakePlay(Stow, TEXT("Stow"), [&](FGinkgoAnimActionSetup& Setup)
			{
				Setup.HoldHandIK(EGinkgoHand::Left, HipBinding);
				Setup.AbortIf(NAME_None, [&bAbort](const FGinkgoAnimActionContext&) { return bAbort; }, FTestTags::GinkgoTest_AnimAction_Abort);
			});
		});
	});
	const auto Top = [&Runner]() { return Runner.GetHandIK(EGinkgoHand::Left); };
	const auto TopIs = [&Top](const FName Socket, const bool bFrozen)
	{
		const FGinkgoAnimActionRunner::FHandHold* Hold = Top();
		return Hold && Hold->Binding.Socket == Socket && Hold->bFrozen == bFrozen;
	};

	Runner.Start(Chain);
	Runner.Tick(DeltaTime);
	const int32 PickID = Host.LastID();
	TestTrue(TEXT("持有：栈顶是物品"), TopIs(ItemSocket, false));
	TestTrue(TEXT("右手没人持有"), Runner.GetHandIK(EGinkgoHand::Right) == nullptr);

	Host.Notify(PickID, TEXT("Done"));
	TestTrue(TEXT("完成后冻结，实例还在，不放"), TopIs(ItemSocket, true));

	Runner.Tick(DeltaTime);
	const int32 StowID = Host.LastID();
	TestTrue(TEXT("下一步接过同一只手"), TopIs(HipSocket, false));

	Host.End(PickID);
	TestTrue(TEXT("上一步的实例结束才放，不影响下一步"), TopIs(HipSocket, false));

	bAbort = true;
	Runner.Tick(DeltaTime);
	TestTrue(TEXT("中止时冻结，不清"), TopIs(HipSocket, true));
	Host.End(StowID);
	TestTrue(TEXT("实例结束才放"), Top() == nullptr);

	// 看门：实例丢了、一个事件都没有，也会结束、也放
	FRecorded Recorded;
	Runner.Start(MakePlay(Pick, TEXT("Lost"), [&](FGinkgoAnimActionSetup& Setup) { Setup.HoldHandIK(EGinkgoHand::Right, ItemBinding); }), Recorded.Callback());
	Runner.Tick(DeltaTime);
	Host.Vanish(Host.LastID());
	Runner.Tick(DeltaTime);
	TestResult(*this, TEXT("实例丢了"), Recorded.Last, EGinkgoAnimActionOutcome::Aborted, FAnimActionTags::AnimAction_Abort_InstanceLost);
	TestTrue(TEXT("实例丢了也放"), Runner.GetHandIK(EGinkgoHand::Right) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionSoftStopTest, "CaseGinkgo.Animation.Action.SoftStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionSoftStopTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;

	// 默认：软停就是取消，蒙太奇停掉
	{
		UAnimMontage* Montage = MakeMontage(Host, TEXT("Plain"), UpperBody);
		UGinkgoAnimAction* Action = MakePlay(Montage, TEXT("Plain"));
		FRecorded Recorded;
		Runner.Start(Action, Recorded.Callback());
		Runner.Tick(DeltaTime);
		const int32 InstanceID = Host.LastID();
		Runner.RequestStop(Action, FTestTags::GinkgoTest_AnimAction_Stop);
		TestEqual(TEXT("软停在应用阶段才处理"), Recorded.Count, 0);
		Runner.Tick(DeltaTime);
		TestResult(*this, TEXT("不自己收的软停"), Recorded.Last, EGinkgoAnimActionOutcome::Cancelled, FTestTags::GinkgoTest_AnimAction_Stop);
		TestEqual(TEXT("蒙太奇停掉"), Host.Count(FString::Printf(TEXT("Stop #%d"), InstanceID)), 1);
	}

	// 98k 换弹：自己收，跳到闭栓那一段，放完算完成；链不再往下走
	{
		UAnimMontage* Reload = MakeMontage(Host, TEXT("Reload98k"), UpperBody);
		UAnimMontage* After = MakeMontage(Host, TEXT("After"), UpperBody);
		bool bAfterCreated = false;
		UGinkgoAnimActionChain* Chain = UGinkgoAnimActionChain::MakeChain(nullptr, TEXT("ReloadThenAim"), [&](FGinkgoAnimActionChainBuilder& Builder)
		{
			Builder.Step(TEXT("Reload"), [&]
			{
				return MakePlay(Reload, TEXT("Reload"), [](FGinkgoAnimActionSetup& Setup)
				{
					Setup.OnStopRequested([](FGinkgoAnimActionContext& Context, const FGameplayTag&)
					{
						Context.JumpToSection(TEXT("Close"));
						return true;
					});
				});
			}).Next(TEXT("After"));
			Builder.Step(TEXT("After"), [&] { bAfterCreated = true; return MakePlay(After, TEXT("After")); });
		});
		FRecorded Recorded;
		Runner.Start(Chain, Recorded.Callback());
		Runner.Tick(DeltaTime);
		const int32 InstanceID = Host.LastID();
		Runner.RequestStop(Chain, FTestTags::GinkgoTest_AnimAction_Stop);
		Runner.Tick(DeltaTime);
		TestEqual(TEXT("跳到收尾的一段"), Host.Count(FString::Printf(TEXT("Jump #%d Close"), InstanceID)), 1);
		TestEqual(TEXT("自己收的还在跑"), Recorded.Count, 0);
		TestEqual(TEXT("自己收的不停"), Host.Count(FString::Printf(TEXT("Stop #%d"), InstanceID)), 0);

		Host.AutoBlendOut(InstanceID);
		Runner.Tick(DeltaTime);
		TestResult(*this, TEXT("自己收完的完成带上软停的原因"), Recorded.Last, EGinkgoAnimActionOutcome::Completed, FTestTags::GinkgoTest_AnimAction_Stop);
		TestFalse(TEXT("软停之后链不再往下走"), bAfterCreated);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoOccupancyRuleTest, "CaseGinkgo.Animation.Action.Occupancy.Rule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoOccupancyRuleTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Occupancy;
	const FGameplayTag Arm = FAnimActionTags::Occupancy_RightArm;
	const FGameplayTag Hand = FAnimActionTags::Occupancy_RightArm_Hand;
	const FGameplayTag Other = FAnimActionTags::Occupancy_LeftArm;
	const auto Blocked = [](const FGameplayTag& ClaimTag, const int32 Claim, const FGameplayTag& RequestTag, const int32 Request)
	{
		return Blocks(FGinkgoOccupancy{ClaimTag, Claim}, FGinkgoOccupancy{RequestTag, Request});
	};
	// 你定的规则
	TestFalse(TEXT("持枪 0，切枪 5：能执行"), Blocked(Arm, PriorityHold, Arm, PriorityWeaponSwap));
	TestTrue(TEXT("等于不能执行"), Blocked(Arm, 5, Arm, 5));
	TestTrue(TEXT("小于不能执行"), Blocked(Arm, 5, Arm, 0));
	TestTrue(TEXT("持枪 0，右手的拾取 0：用不了右手"), Blocked(Arm, PriorityHold, Arm, PriorityPickup));
	TestTrue(TEXT("右臂占着，新的右臂.右手：除非优先级更大否则不能执行"), Blocked(Arm, 0, Hand, 0));
	TestFalse(TEXT("右臂占着 0，新的右臂.右手 1：优先级更大"), Blocked(Arm, 0, Hand, 1));
	TestFalse(TEXT("右臂.右手在执行，右臂能执行"), Blocked(Hand, 0, Arm, 0));
	TestFalse(TEXT("右臂.右手 5 在执行，右臂 5：能执行"), Blocked(Hand, 5, Arm, 5));
	// 我加的一条（你同意的）：下级的优先级严格更高才挡上级
	TestTrue(TEXT("右臂.右手 5 在执行，右臂 0：不能穿过去"), Blocked(Hand, 5, Arm, 0));
	TestFalse(TEXT("另一条胳膊不相干"), Blocked(Other, 9, Arm, 0));
	TestFalse(TEXT("手与另一条胳膊不相干"), Blocked(Other, 9, Hand, 0));

	TestTrue(TEXT("同一个部位重叠"), Overlaps(FGinkgoOccupancy{Arm, 0}, FGinkgoOccupancy{Arm, 1}));
	TestTrue(TEXT("上下级重叠"), Overlaps(FGinkgoOccupancy{Hand, 0}, FGinkgoOccupancy{Arm, 1}));
	TestFalse(TEXT("两条胳膊不重叠"), Overlaps(FGinkgoOccupancy{Other, 0}, FGinkgoOccupancy{Arm, 1}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoOccupancyGateTest, "CaseGinkgo.Animation.Action.Occupancy.Gate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoOccupancyGateTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	using namespace Ginkgo::Occupancy;
	const FGameplayTag RightArm = FAnimActionTags::Occupancy_RightArm;
	const FGameplayTag LeftArm = FAnimActionTags::Occupancy_LeftArm;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* Right = MakeMontage(Host, TEXT("Right"), TEXT("ArmRight"));
	UAnimMontage* Left = MakeMontage(Host, TEXT("Left"), TEXT("ArmLeft"));

	// 枪在手上：外部占着右臂
	const int32 Hold = Runner.Occupy(RightArm, PriorityHold);
	TestFalse(TEXT("拿着枪：右手不能拾取"), Runner.CanOccupy(RightArm, PriorityPickup));
	TestTrue(TEXT("拿着枪：左手能拾取"), Runner.CanOccupy(LeftArm, PriorityPickup));
	TestTrue(TEXT("拿着枪：能收枪"), Runner.CanOccupy(RightArm, PriorityWeaponSwap));

	// 不让的：OnStart 不调，当场结局，不放蒙太奇
	int32 Started = 0;
	UGinkgoAnimAction* Refused = UGinkgoAnimAction::Make(nullptr, TEXT("PickupRight"), [&Started, Right](FGinkgoAnimActionSetup& Setup)
	{
		++Started;
		Setup.Play(Right);
	})->Occupy(RightArm, PriorityPickup);
	FRecorded RefusedResult;
	TestFalse(TEXT("不让：启动返回假"), Runner.Start(Refused, RefusedResult.Callback()));
	TestEqual(TEXT("不让：当场报告一次"), RefusedResult.Count, 1);
	TestResult(*this, TEXT("不让"), RefusedResult.Last, EGinkgoAnimActionOutcome::Aborted, FAnimActionTags::AnimAction_Abort_Occupied);
	Runner.Tick(DeltaTime);
	TestEqual(TEXT("不让：OnStart 不调"), Started, 0);
	TestEqual(TEXT("不让：不放蒙太奇"), Host.Count(TEXT("Play ")), 0);
	TestEqual(TEXT("不让：执行器里没有它"), Runner.GetActions().Num(), 0);

	// 左手拾取与拔枪同时放：各占各的胳膊，各在各的槽组
	FRecorded PickupResult;
	UGinkgoAnimAction* Pickup = MakePlay(Left, TEXT("PickupLeft"))->Occupy(LeftArm, PriorityPickup);
	TestTrue(TEXT("左手的拾取能开始"), Runner.Start(Pickup, PickupResult.Callback()));
	TestFalse(TEXT("启动时就占上了：还没应用，再来一个左手也不让"), Runner.CanOccupy(LeftArm, PriorityPickup));
	FRecorded DrawResult;
	UGinkgoAnimAction* Draw = MakePlay(Right, TEXT("Draw"))->Occupy(RightArm, PriorityWeaponSwap);
	TestTrue(TEXT("拔枪能开始（比持枪高）"), Runner.Start(Draw, DrawResult.Callback()));
	Runner.Tick(DeltaTime);
	TestEqual(TEXT("两个蒙太奇都在放"), Host.Count(TEXT("Play ")), 2);
	TestTrue(TEXT("拾取在跑"), Pickup->IsRunning());
	TestTrue(TEXT("拔枪在跑"), Draw->IsRunning());
	TestFalse(TEXT("拔枪中：再拔一次不让（等于不能执行）"), Runner.CanOccupy(RightArm, PriorityWeaponSwap));

	// 到结局就放，不等淡出；外部的还在
	Host.AutoBlendOut(Pickup->GetInstanceID());
	TestTrue(TEXT("拾取完成：左臂空了（蒙太奇还在淡出）"), Runner.CanOccupy(LeftArm, PriorityPickup));
	Host.AutoBlendOut(Draw->GetInstanceID());
	TestTrue(TEXT("拔枪完成：能再拔"), Runner.CanOccupy(RightArm, PriorityWeaponSwap));
	TestFalse(TEXT("持枪还占着右臂"), Runner.CanOccupy(RightArm, PriorityPickup));
	Runner.ReleaseOccupancy(Hold);
	TestTrue(TEXT("枪收起来了：右手能拾取"), Runner.CanOccupy(RightArm, PriorityPickup));

	// 还没应用就取消：占用也放
	UGinkgoAnimAction* Cancelled = MakePlay(Right, TEXT("Cancelled"))->Occupy(RightArm, PriorityPickup);
	Runner.Start(Cancelled);
	TestFalse(TEXT("等应用时占着"), Runner.CanOccupy(RightArm, PriorityPickup));
	Runner.Cancel(Cancelled, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	TestTrue(TEXT("取消了：放了"), Runner.CanOccupy(RightArm, PriorityPickup));

	// 没声明占用的不看门，也不挡别人
	TestTrue(TEXT("没声明占用的照常"), Runner.Start(MakePlay(Right, TEXT("NoClaim"))));
	TestTrue(TEXT("别人照常"), Runner.CanOccupy(RightArm, PriorityPickup));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoOccupancyPreemptTest, "CaseGinkgo.Animation.Action.Occupancy.Preempt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoOccupancyPreemptTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	using namespace Ginkgo::Occupancy;
	const FGameplayTag RightArm = FAnimActionTags::Occupancy_RightArm;
	const FGameplayTag RightHand = FAnimActionTags::Occupancy_RightArm_Hand;
	const FGameplayTag LeftArm = FAnimActionTags::Occupancy_LeftArm;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* Right = MakeMontage(Host, TEXT("Right"), TEXT("ArmRight"));
	UAnimMontage* Fingers = MakeMontage(Host, TEXT("Fingers"), TEXT("HandRight"));
	UAnimMontage* Left = MakeMontage(Host, TEXT("Left"), TEXT("ArmLeft"));
	UAnimMontage* Upper = MakeMontage(Host, TEXT("Upper"), UpperBody);

	// 右手在拾取时切枪：拾取马上结局（取消、抢占，补做照常：中止即完成），它的蒙太奇由新的顶掉，不另停
	int32 Stowed = 0;
	FRecorded PickupResult;
	UGinkgoAnimAction* Pickup = MakePlay(Right, TEXT("Pickup"), [&Stowed](FGinkgoAnimActionSetup& Setup)
	{
		Setup.On(TEXT("Stow"), [&Stowed](FGinkgoAnimActionEvent&) { ++Stowed; }, EGinkgoMissedEvent::CatchUp);
	})->Occupy(RightArm, PriorityPickup);
	Runner.Start(Pickup, PickupResult.Callback());
	Runner.Tick(DeltaTime);
	UGinkgoAnimAction* Draw = MakePlay(Right, TEXT("Draw"))->Occupy(RightArm, PriorityWeaponSwap);
	TestTrue(TEXT("切枪比拾取高"), Runner.Start(Draw));
	TestResult(*this, TEXT("拾取被抢占"), PickupResult.Last, EGinkgoAnimActionOutcome::Cancelled, FAnimActionTags::AnimAction_Cancel_Preempted);
	TestEqual(TEXT("抢占时补做"), Stowed, 1);
	Runner.Tick(DeltaTime);
	TestEqual(TEXT("同一组：只有一次放，不另停"), Host.Count(TEXT("Stop")), 0);
	TestTrue(TEXT("切枪在跑"), Draw->IsRunning());

	// 上半身的动作占着右臂（低），切枪在另一个组：抢占时要真的停
	Runner.Cancel(Draw, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	Runner.Tick(DeltaTime);
	Host.Calls.Reset();
	UGinkgoAnimAction* Reload = MakePlay(Upper, TEXT("Reload"))->Occupy(RightArm, 3)->Occupy(LeftArm, 3);
	Runner.Start(Reload);
	Runner.Tick(DeltaTime);
	TestFalse(TEXT("换弹占两条胳膊：左手不能拾取"), Runner.CanOccupy(LeftArm, PriorityPickup));
	UGinkgoAnimAction* Stow = MakePlay(Right, TEXT("Stow"))->Occupy(RightArm, PriorityWeaponSwap);
	Runner.Start(Stow);
	Runner.Tick(DeltaTime);
	TestEqual(TEXT("不同组：换弹的蒙太奇另停"), Host.Count(TEXT("Stop")), 1);
	TestTrue(TEXT("换弹结束了，两条胳膊的占用都放"), Reload->IsFinished() && Runner.CanOccupy(LeftArm, PriorityPickup));
	Runner.Cancel(Stow, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	Runner.Tick(DeltaTime);

	// 下级在跑，上级来了：放行并取消下级；下级优先级更高时不让，下级接着跑
	UGinkgoAnimAction* Relax = MakePlay(Fingers, TEXT("Relax"))->Occupy(RightHand, 0);
	Runner.Start(Relax);
	Runner.Tick(DeltaTime);
	UGinkgoAnimAction* Over = MakePlay(Right, TEXT("Over"))->Occupy(RightArm, 0);
	TestTrue(TEXT("右臂.右手 0 在跑：右臂 0 能执行"), Runner.Start(Over));
	TestTrue(TEXT("下级取消"), Relax->IsFinished() && Relax->GetResult().Reason == FAnimActionTags::AnimAction_Cancel_Preempted);
	Runner.Tick(DeltaTime);
	TestFalse(TEXT("右臂 0 在跑：右臂.右手 0 不让"), Runner.CanOccupy(RightHand, 0));
	TestTrue(TEXT("右臂 0 在跑：右臂.右手 1 让"), Runner.CanOccupy(RightHand, 1));
	Runner.Cancel(Over, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	Runner.Tick(DeltaTime);

	UGinkgoAnimAction* Button = MakePlay(Fingers, TEXT("Button"))->Occupy(RightHand, 5);
	Runner.Start(Button);
	Runner.Tick(DeltaTime);
	FRecorded BlockedResult;
	TestFalse(TEXT("右臂.右手 5 在跑：右臂 0 不让"), Runner.Start(MakePlay(Right, TEXT("Blocked"))->Occupy(RightArm, 0), BlockedResult.Callback()));
	TestResult(*this, TEXT("上级被挡"), BlockedResult.Last, EGinkgoAnimActionOutcome::Aborted, FAnimActionTags::AnimAction_Abort_Occupied);
	TestTrue(TEXT("下级接着跑"), Button->IsRunning());
	Runner.Cancel(Button, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	Runner.Tick(DeltaTime);

	// 外部占：比动作高的把它取消，相等的不动
	UGinkgoAnimAction* Running = MakePlay(Left, TEXT("Running"))->Occupy(LeftArm, 0);
	Runner.Start(Running);
	Runner.Tick(DeltaTime);
	const int32 Same = Runner.Occupy(LeftArm, 0);
	TestTrue(TEXT("外部相等：动作照常放"), Running->IsRunning());
	const int32 Higher = Runner.Occupy(LeftArm, 10);
	TestTrue(TEXT("外部更高：动作取消"), Running->IsFinished() && Running->GetResult().Reason == FAnimActionTags::AnimAction_Cancel_Preempted);
	TestFalse(TEXT("外部 10 占着：0 不让"), Runner.CanOccupy(LeftArm, 0));
	Runner.ReleaseOccupancy(Higher);
	TestFalse(TEXT("还有一个外部的 0"), Runner.CanOccupy(LeftArm, 0));
	Runner.ReleaseOccupancy(Same);
	TestTrue(TEXT("都放了"), Runner.CanOccupy(LeftArm, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoOccupancyChainTest, "CaseGinkgo.Animation.Action.Occupancy.Chain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoOccupancyChainTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	using namespace Ginkgo::Occupancy;
	const FGameplayTag RightArm = FAnimActionTags::Occupancy_RightArm;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	UAnimMontage* First = MakeMontage(Host, TEXT("First"), TEXT("ArmRight"));
	UAnimMontage* Second = MakeMontage(Host, TEXT("Second"), TEXT("ArmRight"));

	// 链自己占着：它的步占同一个部位、优先级相等也不挡自己；步与步之间不空出来
	UGinkgoAnimActionChain* Chain = UGinkgoAnimActionChain::MakeChain(nullptr, TEXT("Swap"), [&](FGinkgoAnimActionChainBuilder& Builder)
	{
		Builder.Step(TEXT("First"), [&] { return MakePlay(First, TEXT("First"))->Occupy(RightArm, PriorityWeaponSwap); }).Next(TEXT("Second"));
		Builder.Step(TEXT("Second"), [&] { return MakePlay(Second, TEXT("Second"))->Occupy(RightArm, PriorityWeaponSwap); });
	});
	Chain->Occupy(RightArm, PriorityWeaponSwap);
	FRecorded ChainResult;
	TestTrue(TEXT("链能开始"), Runner.Start(Chain, ChainResult.Callback()));
	Runner.Tick(DeltaTime);
	TestEqual(TEXT("第一步在放"), Host.Count(TEXT("Play ")), 1);
	Host.AutoBlendOut(Host.LastID());
	TestFalse(TEXT("第一步完成：链还占着"), Runner.CanOccupy(RightArm, PriorityWeaponSwap));
	Runner.Tick(DeltaTime);
	TestEqual(TEXT("第二步在放"), Host.Count(TEXT("Play ")), 2);
	Host.AutoBlendOut(Host.LastID());
	Runner.Tick(DeltaTime);
	TestResult(*this, TEXT("链完成"), ChainResult.Last, EGinkgoAnimActionOutcome::Completed);
	TestTrue(TEXT("链完成：放了"), Runner.CanOccupy(RightArm, PriorityWeaponSwap));

	// 链里的一步占用不让：当成这一步中止，链结束
	const int32 Ladder = Runner.Occupy(RightArm, 10);
	FRecorded BlockedChain;
	UGinkgoAnimActionChain* Blocked = UGinkgoAnimActionChain::MakeChain(nullptr, TEXT("Blocked"), [&](FGinkgoAnimActionChainBuilder& Builder)
	{
		Builder.Step(TEXT("First"), [&] { return MakePlay(First, TEXT("First"))->Occupy(RightArm, PriorityWeaponSwap); });
	});
	Runner.Start(Blocked, BlockedChain.Callback());
	Runner.Tick(DeltaTime);
	TestResult(*this, TEXT("这一步不让"), BlockedChain.Last, EGinkgoAnimActionOutcome::Aborted, FAnimActionTags::AnimAction_Abort_Occupied);
	TestEqual(TEXT("没有多放"), Host.Count(TEXT("Play ")), 2);
	Runner.ReleaseOccupancy(Ladder);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimActionFailuresTest, "CaseGinkgo.Animation.Action.Failures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimActionFailuresTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimActionTests;
	FFakeMontageHost Host;
	FGinkgoAnimActionRunner Runner(Host);
	Host.Runner = &Runner;
	AddExpectedMessage(TEXT("放不起来"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2, false);

	// 宿主放不起来：中止（PlayFailed）
	{
		UAnimMontage* Montage = MakeMontage(Host, TEXT("Broken"), FullBody);
		Host.bFailPlays = true;
		FRecorded Recorded;
		Runner.Start(MakePlay(Montage, TEXT("Broken")), Recorded.Callback());
		Runner.Tick(DeltaTime);
		Host.bFailPlays = false;
		TestResult(*this, TEXT("宿主放不起来"), Recorded.Last, EGinkgoAnimActionOutcome::Aborted, FAnimActionTags::AnimAction_Abort_PlayFailed);
	}

	// 调了 Play 却传空：算放不起来，不是没有蒙太奇的动作
	{
		FRecorded Recorded;
		Runner.Start(MakePlay(nullptr, TEXT("NullMontage")), Recorded.Callback());
		Runner.Tick(DeltaTime);
		TestResult(*this, TEXT("蒙太奇是空的"), Recorded.Last, EGinkgoAnimActionOutcome::Aborted, FAnimActionTags::AnimAction_Abort_PlayFailed);
	}

	// 开始之前取消：不放，报告一次
	{
		UAnimMontage* Montage = MakeMontage(Host, TEXT("Never"), FullBody);
		UGinkgoAnimAction* Action = MakePlay(Montage, TEXT("Never"));
		const int32 PlaysBefore = Host.Count(TEXT("Play "));
		FRecorded Recorded;
		Runner.Start(Action, Recorded.Callback());
		Runner.Cancel(Action, FTestTags::GinkgoTest_AnimAction_Stop);
		Runner.Tick(DeltaTime);
		TestResult(*this, TEXT("开始之前取消"), Recorded.Last, EGinkgoAnimActionOutcome::Cancelled, FTestTags::GinkgoTest_AnimAction_Stop);
		TestEqual(TEXT("报告一次"), Recorded.Count, 1);
		TestEqual(TEXT("不放"), Host.Count(TEXT("Play ")) - PlaysBefore, 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

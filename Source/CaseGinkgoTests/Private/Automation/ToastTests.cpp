#include "Misc/AutomationTest.h"
#include "UI/GinkgoToast.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	using EStep = FGinkgoToastQueue::EStep;

	FGinkgoToast ToastNamed(const TCHAR* Title, const float HoldSeconds = 0.f)
	{
		FGinkgoToast Toast;
		Toast.Title = FText::FromString(Title);
		Toast.HoldSeconds = HoldSeconds;
		return Toast;
	}

	FString CurrentTitle(const FGinkgoToastQueue& Queue)
	{
		return Queue.GetCurrent() ? Queue.GetCurrent()->Title.ToString() : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoToastQueueTest, "CaseGinkgo.UI.Toast",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoToastQueueTest::RunTest(const FString& Parameters)
{
	FGinkgoToastTimings Timings;
	Timings.In = 0.25f;
	Timings.Hold = 4.f;
	Timings.Out = 0.25f;
	Timings.Gap = 0.5f;

	FGinkgoToastQueue Queue;
	TestTrue(TEXT("空的队列什么也不做"), Queue.Advance(0.0, Timings) == EStep::None);
	TestNull(TEXT("没有正在显示的"), Queue.GetCurrent());

	// 先进先出：第一条马上出，第二条等着
	Queue.Push(ToastNamed(TEXT("一")));
	Queue.Push(ToastNamed(TEXT("二")));
	TestEqual(TEXT("排了两条"), Queue.GetNumPending(), 2);
	TestTrue(TEXT("第一条出现"), Queue.Advance(10.0, Timings) == EStep::Show);
	TestEqual(TEXT("先排的先出"), CurrentTitle(Queue), FString(TEXT("一")));
	TestEqual(TEXT("还有一条等着"), Queue.GetNumPending(), 1);

	// 一帧最多一件事；一次一条：显示着的时候第二条不出
	TestTrue(TEXT("这一帧没有别的事"), Queue.Advance(10.0, Timings) == EStep::None);
	TestTrue(TEXT("出现 + 停留没到：不消失"), Queue.Advance(14.2, Timings) == EStep::None);
	TestEqual(TEXT("还是第一条"), CurrentTitle(Queue), FString(TEXT("一")));

	// 显示着的时候来的新的不打断
	Queue.Push(ToastNamed(TEXT("三"), 9.f));
	TestTrue(TEXT("新来的不打断"), Queue.Advance(14.21, Timings) == EStep::None);
	TestEqual(TEXT("新来的排在后面"), Queue.GetNumPending(), 2);

	// 出现 + 停留到了：消失；消失的动画 + 间隔之内不出下一条
	TestTrue(TEXT("到时间消失"), Queue.Advance(14.25, Timings) == EStep::Hide);
	TestNull(TEXT("消失以后没有正在显示的"), Queue.GetCurrent());
	TestTrue(TEXT("消失 + 间隔没到：不出下一条"), Queue.Advance(14.99, Timings) == EStep::None);
	TestTrue(TEXT("到时间出下一条"), Queue.Advance(15.0, Timings) == EStep::Show);
	TestEqual(TEXT("第二条"), CurrentTitle(Queue), FString(TEXT("二")));

	// 时间不走（游戏暂停、设置界面开着时队列的时钟不走）：什么也不变
	TestTrue(TEXT("时间不走：不消失"), Queue.Advance(15.0, Timings) == EStep::None);
	TestEqual(TEXT("时间不走：还是这一条"), CurrentTitle(Queue), FString(TEXT("二")));

	// 自己带停留时间的：按它自己的
	TestTrue(TEXT("第二条消失"), Queue.Advance(19.25, Timings) == EStep::Hide);
	TestTrue(TEXT("第三条出现"), Queue.Advance(20.0, Timings) == EStep::Show);
	TestEqual(TEXT("第三条"), CurrentTitle(Queue), FString(TEXT("三")));
	TestTrue(TEXT("默认的停留到了，它还在"), Queue.Advance(24.25, Timings) == EStep::None);
	TestTrue(TEXT("它自己的停留到了"), Queue.Advance(29.25, Timings) == EStep::Hide);

	// 排空以后：什么也不做，再排的等间隔过了才出
	TestTrue(TEXT("排空了"), Queue.Advance(40.0, Timings) == EStep::None);
	TestEqual(TEXT("没有等着的"), Queue.GetNumPending(), 0);
	Queue.Push(ToastNamed(TEXT("四")));
	TestTrue(TEXT("空闲时来的马上出"), Queue.Advance(40.0, Timings) == EStep::Show);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

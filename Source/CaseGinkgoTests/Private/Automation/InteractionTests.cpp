#include "Interaction/GinkgoInteractionMath.h"
#include "Interaction/GinkgoInteractionSubsystem.h"
#include "InteractionTestTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInteractionDistanceTest, "CaseGinkgo.Interaction.Distance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInteractionDistanceTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Interaction;
	// 角色的胶囊（引擎默认，半径 34、半高 88）站在地上：中心离地 88，中轴线段从离地 34 到 142
	const FVector Center(0.0, 0.0, 88.0);
	FVector Bottom, Top;
	GetCapsuleSegment(Center, FVector::UpVector, 88.0, 34.0, Bottom, Top);
	TestEqual(TEXT("下面的半球球心"), Bottom, FVector(0.0, 0.0, 34.0));
	TestEqual(TEXT("上面的半球球心"), Top, FVector(0.0, 0.0, 142.0));

	// 在胶囊的高度范围内：就是水平距离，与高低无关
	TestEqual(TEXT("桌上（离地 90）"), DistanceToSegment(FVector(150.0, 0.0, 90.0), Bottom, Top), 150.0, 1e-4);
	TestEqual(TEXT("胸口（离地 140）"), DistanceToSegment(FVector(150.0, 0.0, 140.0), Bottom, Top), 150.0, 1e-4);
	// 在脚下：只多出离下面的球心那一截高度（29 cm）
	TestEqual(TEXT("地上（离地 5）"), DistanceToSegment(FVector(150.0, 0.0, 5.0), Bottom, Top), FMath::Sqrt(150.0 * 150.0 + 29.0 * 29.0), 1e-4);
	TestEqual(TEXT("正下方"), DistanceToSegment(FVector(0.0, 0.0, 5.0), Bottom, Top), 29.0, 1e-4);
	// 交互距离 180：按中轴线段量，地上的枪在水平 177 cm 处就够得着（桌上的 180）；按胶囊中心量，地上的要走到 160 cm 以内
	TestTrue(TEXT("按中轴线段量，地上的枪在水平 177 cm 处够得着"), DistanceToSegment(FVector(177.0, 0.0, 5.0), Bottom, Top) <= 180.0);
	TestTrue(TEXT("按中心量，地上的枪在水平 161 cm 处就够不着"), FVector::Dist(FVector(161.0, 0.0, 5.0), Center) > 180.0);

	// 蹲下（引擎默认半高 40）：中心降到离地 40，线段只剩 34 到 46；地上的东西与站着时一样近，头顶上的远了
	GetCapsuleSegment(FVector(0.0, 0.0, 40.0), FVector::UpVector, 40.0, 34.0, Bottom, Top);
	TestEqual(TEXT("蹲下时上面的球心降到 46"), Top.Z, 46.0, 1e-9);
	TestEqual(TEXT("蹲下时地上的东西一样近"), DistanceToSegment(FVector(0.0, 0.0, 5.0), Bottom, Top), 29.0, 1e-4);
	TestEqual(TEXT("蹲下时头顶上方的东西远了"), DistanceToSegment(FVector(0.0, 0.0, 150.0), Bottom, Top), 104.0, 1e-4);

	// 半高小于半径（退化成球）：线段缩成中心一点
	GetCapsuleSegment(Center, FVector::UpVector, 20.0, 34.0, Bottom, Top);
	TestEqual(TEXT("退化成球时线段是一点"), Bottom, Top);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInteractionViewAngleTest, "CaseGinkgo.Interaction.ViewAngle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInteractionViewAngleTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Interaction;
	const FVector View(0.0, 0.0, 0.0);
	const FVector Forward(1.0, 0.0, 0.0);
	TestEqual(TEXT("正前方"), ViewAngleDegrees(View, Forward, FVector(500.0, 0.0, 0.0)), 0.0, 1e-6);
	TestEqual(TEXT("正侧面"), ViewAngleDegrees(View, Forward, FVector(0.0, 300.0, 0.0)), 90.0, 1e-6);
	TestEqual(TEXT("正后方"), ViewAngleDegrees(View, Forward, FVector(-10.0, 0.0, 0.0)), 180.0, 1e-6);
	TestEqual(TEXT("斜下方 45°"), ViewAngleDegrees(View, Forward, FVector(100.0, 0.0, -100.0)), 45.0, 1e-6);
	TestEqual(TEXT("前方不是单位向量也行"), ViewAngleDegrees(View, Forward * 7.0, FVector(100.0, 100.0, 0.0)), 45.0, 1e-6);
	TestEqual(TEXT("目标就在视点上算 0"), ViewAngleDegrees(View, Forward, View), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInteractionHysteresisTest, "CaseGinkgo.Interaction.Hysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInteractionHysteresisTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Interaction;
	// 交互距离 180，滞回 20：进来要到 180 以内，出去要过 200
	TestTrue(TEXT("在边上算进来"), IsWithinRange(180.0, 180.0, false, 20.0));
	TestFalse(TEXT("边外一点进不来"), IsWithinRange(180.1, 180.0, false, 20.0));
	TestTrue(TEXT("已经在里面时 190 还算在里面"), IsWithinRange(190.0, 180.0, true, 20.0));
	TestTrue(TEXT("已经在里面时 200 还算在里面"), IsWithinRange(200.0, 180.0, true, 20.0));
	TestFalse(TEXT("过了 200 才出去"), IsWithinRange(200.1, 180.0, true, 20.0));

	// 站在 190 来回晃：进来以后不闪
	bool bInside = false;
	const double Path[] = {185.0, 179.0, 190.0, 195.0, 186.0, 199.0};
	int32 Changes = 0;
	for (const double Distance : Path)
	{
		const bool bNow = IsWithinRange(Distance, 180.0, bInside, 20.0);
		Changes += bNow != bInside ? 1 : 0;
		bInside = bNow;
	}
	TestEqual(TEXT("在 179 到 199 之间晃只变一次"), Changes, 1);

	// 服务器的判断：当作已经在里面，多放的是容差
	TestTrue(TEXT("服务器上 230 还在 180 + 60 以内"), IsWithinRange(230.0, 180.0, true, 60.0));
	TestFalse(TEXT("服务器上 241 超了"), IsWithinRange(241.0, 180.0, true, 60.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInteractionFocusTest, "CaseGinkgo.Interaction.Focus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInteractionFocusTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Interaction;
	constexpr double MaxAngle = 60.0;
	constexpr double Margin = 3.0;

	TestEqual(TEXT("没有候选"), SelectFocus({}, MaxAngle, Margin), int32(INDEX_NONE));
	{
		const FFocusCandidate One[] = {{30.0, true, false}};
		TestEqual(TEXT("一个合格的候选就是焦点"), SelectFocus(One, MaxAngle, Margin), 0);
	}
	{
		const FFocusCandidate Behind[] = {{75.0, true, false}};
		TestEqual(TEXT("超过 60° 的不算"), SelectFocus(Behind, MaxAngle, Margin), int32(INDEX_NONE));
	}
	{
		const FFocusCandidate Far[] = {{5.0, false, false}};
		TestEqual(TEXT("不合格的（交互距离外、眼睛看不见）不算"), SelectFocus(Far, MaxAngle, Margin), int32(INDEX_NONE));
	}
	{
		const FFocusCandidate Two[] = {{20.0, true, false}, {8.0, true, false}};
		TestEqual(TEXT("离准星近的那个"), SelectFocus(Two, MaxAngle, Margin), 1);
	}
	{
		// 上一帧是 0（10°），1 只近了 2°：不换
		const FFocusCandidate Close[] = {{10.0, true, true}, {8.0, true, false}};
		TestEqual(TEXT("近得不够 3° 不换"), SelectFocus(Close, MaxAngle, Margin), 0);
		// 近了 4°：换
		const FFocusCandidate Better[] = {{10.0, true, true}, {6.0, true, false}};
		TestEqual(TEXT("近了 3° 以上就换"), SelectFocus(Better, MaxAngle, Margin), 1);
	}
	{
		// 上一帧的焦点不合格了（走出交互距离）：直接换成最近的
		const FFocusCandidate Lost[] = {{2.0, false, true}, {40.0, true, false}};
		TestEqual(TEXT("上一个焦点不合格就换"), SelectFocus(Lost, MaxAngle, Margin), 1);
		// 上一帧的焦点转到 60° 外
		const FFocusCandidate Turned[] = {{61.0, true, true}, {59.0, true, false}};
		TestEqual(TEXT("上一个焦点转出 60° 就换"), SelectFocus(Turned, MaxAngle, Margin), 1);
	}
	{
		// 两个来回晃的角度：只在差够 3° 时才跳，不每帧来回
		int32 Focus = INDEX_NONE;
		int32 Switches = 0;
		const double Left[] = {10.0, 10.5, 11.0, 10.0, 9.5, 13.5};
		const double Right[] = {11.0, 10.0, 9.0, 9.5, 10.0, 10.0};
		for (int32 Frame = 0; Frame < UE_ARRAY_COUNT(Left); ++Frame)
		{
			const FFocusCandidate Candidates[] = {{Left[Frame], true, Focus == 0}, {Right[Frame], true, Focus == 1}};
			const int32 Next = SelectFocus(Candidates, MaxAngle, Margin);
			Switches += Focus != INDEX_NONE && Next != Focus ? 1 : 0;
			Focus = Next;
		}
		TestEqual(TEXT("挨着的两件东西只在差够 3° 时换一次"), Switches, 1);
		TestEqual(TEXT("最后换到右边"), Focus, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInteractionRegistryTest, "CaseGinkgo.Interaction.Registry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInteractionRegistryTest::RunTest(const FString& Parameters)
{
	// 子系统的表只认接口：假的目标不是组件、不在世界里也能登记
	UGinkgoInteractionSubsystem* Subsystem = NewObject<UGinkgoInteractionSubsystem>(GetTransientPackage());
	UGinkgoFakeInteractable* First = NewObject<UGinkgoFakeInteractable>(GetTransientPackage());
	UGinkgoFakeInteractable* Second = NewObject<UGinkgoFakeInteractable>(GetTransientPackage());
	UGinkgoNotInteractable* Other = NewObject<UGinkgoNotInteractable>(GetTransientPackage());

	Subsystem->Register(First);
	Subsystem->Register(Second);
	Subsystem->Register(First);
	TestEqual(TEXT("同一个目标只登记一次"), Subsystem->GetInteractables().Num(), 2);
	TestTrue(TEXT("登记了的在表里"), Subsystem->IsRegistered(First));

	AddExpectedMessagePlain(TEXT("没实现 IGinkgoInteractable"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Subsystem->Register(Other);
	TestFalse(TEXT("没实现接口的不收"), Subsystem->IsRegistered(Other));
	TestFalse(TEXT("空的不在表里"), Subsystem->IsRegistered(nullptr));

	Subsystem->Unregister(First);
	TestFalse(TEXT("注销了就不在表里"), Subsystem->IsRegistered(First));
	TestTrue(TEXT("别的还在"), Subsystem->IsRegistered(Second));

	// 表里存的是接口：拿出来能直接调
	IGinkgoInteractable* Target = Subsystem->GetInteractables()[0].Get();
	if (TestNotNull(TEXT("表里的接口能取出来"), Target))
	{
		FText Reason;
		TestEqual(TEXT("条件"), Target->GetAvailability({}, Reason), EGinkgoInteractionAvailability::Available);
		Target->Interact({});
		TestEqual(TEXT("行为"), Second->InteractCount, 1);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

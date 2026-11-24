#include "Misc/AutomationTest.h"
#include "Movement/GinkgoMovementMath.h"

#if WITH_DEV_AUTOMATION_TESTS

// 具名命名空间而不是匿名的：合并编译时，别的测试文件里同名的局部变量不会因为遮蔽全局声明而报错
namespace GinkgoSlideMathTests
{
	// 移动配置的默认滑铲参数（UGinkgoMovementConfig）
	constexpr float SlideBoost = 438.15f;
	constexpr float SlideBoostCap = 1168.4f;
	constexpr float SlideDecel = 127.f;
	constexpr float SlideDecay = 0.35667f;
	constexpr float SlideEndSpeed = 317.5f;
	constexpr float SprintSpeed = 762.f;

	const FVector ForwardDir(1.0, 0.0, 0.0);
	const FVector RightDir(0.0, 1.0, 0.0);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSlideBoostTest, "CaseGinkgo.Movement.Math.SlideBoost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSlideBoostTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	using namespace GinkgoSlideMathTests;

	FVector V(SprintSpeed, 0.0, 0.0);
	float Applied = ApplySlideBoost(V, SlideBoost, SlideBoostCap);
	TestEqual(TEXT("冲刺一铲到上限"), V.X, double(SlideBoostCap), 1e-3);
	TestEqual(TEXT("返回实际加的量"), Applied, SlideBoostCap - SprintSpeed, 1e-3f);

	V = FVector(300.0, 400.0, 0.0);
	ApplySlideBoost(V, SlideBoost, SlideBoostCap);
	TestEqual(TEXT("离上限远时加满"), V.Size2D(), 500.0 + SlideBoost, 1e-3);
	TestTrue(TEXT("方向不变"), V.GetSafeNormal().Equals(FVector(0.6, 0.8, 0.0), 1e-6));

	// 只加不减：已经比上限快时不被拉慢，动量能从别的能力带进滑铲
	V = FVector(1300.0, 0.0, 0.0);
	Applied = ApplySlideBoost(V, SlideBoost, SlideBoostCap);
	TestEqual(TEXT("超过上限时速度不变"), V.X, 1300.0);
	TestEqual(TEXT("超过上限时加了 0"), Applied, 0.f);

	V = FVector(SprintSpeed, 0.0, -300.0);
	ApplySlideBoost(V, SlideBoost, SlideBoostCap);
	TestEqual(TEXT("只放大水平分量"), V.Size2D(), double(SlideBoostCap), 1e-3);
	TestEqual(TEXT("竖直分量不变"), V.Z, -300.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSlideDecelerateTest, "CaseGinkgo.Movement.Math.SlideDecelerate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSlideDecelerateTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	using namespace GinkgoSlideMathTests;

	// 与帧率无关：144 fps 逐帧推进 0.5 秒，与一次推进 0.5 秒相同
	const float OneStep = SlideDecelerate(SlideBoostCap, SlideDecel, SlideDecay, 0.5f);
	float Stepped = SlideBoostCap;
	for (int32 Frame = 0; Frame < 72; ++Frame)
	{
		Stepped = SlideDecelerate(Stepped, SlideDecel, SlideDecay, 1.f / 144.f);
	}
	TestEqual(TEXT("逐帧推进与一次推进相同"), Stepped, OneStep, 1e-2f);

	// 解析解与 dv/dt = −D − λv 的细步长数值积分一致
	double Numeric = SlideBoostCap;
	for (int32 Step = 0; Step < 50000; ++Step)
	{
		Numeric -= (SlideDecel + SlideDecay * Numeric) * 1e-5;
	}
	TestEqual(TEXT("解析解与数值积分一致"), double(OneStep), Numeric, 1e-2);

	// 设计文档的数：平地从上限约 2.29 秒降到结束速度
	TestEqual(TEXT("平地从上限约 2.29 秒滑完"), SlideDecelerate(SlideBoostCap, SlideDecel, SlideDecay, 2.29f), SlideEndSpeed, 1.f);

	TestEqual(TEXT("减到 0 为止，不反向"), SlideDecelerate(100.f, SlideDecel, SlideDecay, 10.f), 0.f);
	TestEqual(TEXT("只有线性减速"), SlideDecelerate(500.f, 100.f, 0.f, 1.f), 400.f, 1e-3f);
	TestEqual(TEXT("只有指数衰减"), SlideDecelerate(500.f, 0.f, SlideDecay, 1.f), 500.f * FMath::Exp(-SlideDecay), 1e-2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSlideSteerTest, "CaseGinkgo.Movement.Math.SlideSteer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSlideSteerTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	using namespace GinkgoSlideMathTests;
	const FVector Moving(800.0, 0.0, 0.0);
	const FVector ForwardRight = (ForwardDir + RightDir).GetSafeNormal();

	TestTrue(TEXT("向前的输入整个去掉"), RemoveForwardInput(ForwardDir, Moving).IsNearlyZero());
	TestTrue(TEXT("反向输入保留"), RemoveForwardInput(-ForwardDir, Moving).Equals(-ForwardDir));
	TestTrue(TEXT("侧向输入保留"), RemoveForwardInput(RightDir, Moving).Equals(RightDir));
	TestTrue(TEXT("斜前方只剩侧向部分"), RemoveForwardInput(ForwardRight, Moving).Equals(FVector(0.0, ForwardRight.Y, 0.0), 1e-6));

	FVector V(1000.0, 0.0, 0.0);
	SteerSlide(V, RemoveForwardInput(RightDir, V), 10.f);
	TestEqual(TEXT("侧向输入不改速度大小"), V.Size(), 1000.0, 1e-3);
	TestTrue(TEXT("侧向输入让方向转向输入"), V.Y > 0.0);

	V = FVector(1000.0, 0.0, 0.0);
	SteerSlide(V, RemoveForwardInput(-ForwardDir, V), 10.f);
	TestEqual(TEXT("反向输入按 SteerStep 刹车"), V.X, 990.0, 1e-3);

	// 一直按着侧向（世界方向不变）2 秒：速度大小从不增加，方向持续转向输入，转过 45° 以上
	V = FVector(1000.0, 0.0, 0.0);
	double MaxSpeed = 0.0;
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		SteerSlide(V, RemoveForwardInput(RightDir, V), 600.f / 60.f);
		MaxSpeed = FMath::Max(MaxSpeed, V.Size());
	}
	TestTrue(TEXT("一直按着侧向，速度不增加"), MaxSpeed <= 1000.0 + 1e-3);
	TestTrue(TEXT("一直按着侧向，方向转过 45° 以上"), V.Y > V.X);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSlideDirectionTest, "CaseGinkgo.Movement.Math.SlideDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSlideDirectionTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	using namespace GinkgoSlideMathTests;
	const float MaxAngleCos = FMath::Cos(FMath::DegreesToRadians(53.13f));

	TestTrue(TEXT("向前可以铲"), IsSlideDirectionAllowed(ForwardDir, ForwardDir, MaxAngleCos));
	TestTrue(TEXT("斜前方 45° 可以铲"), IsSlideDirectionAllowed((ForwardDir + RightDir).GetSafeNormal(), ForwardDir, MaxAngleCos));
	TestFalse(TEXT("60° 不能铲"), IsSlideDirectionAllowed(FVector(0.5, 0.866, 0.0), ForwardDir, MaxAngleCos));
	TestFalse(TEXT("侧向不能铲"), IsSlideDirectionAllowed(RightDir, ForwardDir, MaxAngleCos));
	TestFalse(TEXT("向后不能铲"), IsSlideDirectionAllowed(-ForwardDir, ForwardDir, MaxAngleCos));
	TestTrue(TEXT("没有输入时不限制"), IsSlideDirectionAllowed(FVector::ZeroVector, ForwardDir, MaxAngleCos));
	TestTrue(TEXT("视角俯仰不影响"), IsSlideDirectionAllowed(ForwardDir, FVector(0.5, 0.0, -0.866), MaxAngleCos));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

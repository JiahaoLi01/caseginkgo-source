#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOrbitGeometryTest, "CaseGinkgo.Camera.Orbit.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraOrbitGeometryTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Camera;
	FGinkgoCameraOrbitSettings Orbit;
	FGinkgoCameraParams Params;
	Params.ShoulderOffsetRight = 0.f;
	Params.ShoulderOffsetUp = 0.f;
	const FVector Pivot(130, -250, 80);
	for (const float Length : {0.f, 126.f, 150.f, 300.f})
	{
		Params.ArmLength = Length;
		TestEqual(TEXT("中环精确使用参数层臂长"), ComputeOrbitArmLength(Length, 0, Orbit), static_cast<double>(Length));
		const FVector Upper = ComputeCameraLocation(Pivot, FRotator(-60, 37, 0), Params, &Orbit) - Pivot;
		const FVector Lower = ComputeCameraLocation(Pivot, FRotator(45, 37, 0), Params, &Orbit) - Pivot;
		TestEqual(TEXT("上环水平半径随臂长同比缩放"), Upper.Size2D(), Length * 0.35, 1e-3);
		TestEqual(TEXT("下环水平半径独立配置"), Lower.Size2D(), Length * 0.5, 1e-3);
		TestEqual(TEXT("低头看时相机在跟随点上方"), Upper.Z, Length * 0.35 * FMath::Sqrt(3.0), 1e-3);
		TestEqual(TEXT("抬头看时相机在跟随点下方"), Lower.Z, -Length * 0.5, 1e-3);
	}
	Params.ArmLength = 150.f;
	for (const double Sign : {-1.0, 1.0})
	{
		double PreviousRadius = Params.ArmLength;
		for (int32 Angle = 0; Angle <= 89; ++Angle)
		{
			const FVector Boom = ComputeCameraLocation(Pivot, FRotator(Sign * Angle, 0, 0), Params, &Orbit) - Pivot;
			TestTrue(TEXT("默认轨道水平半径单调收紧"), Boom.Size2D() <= PreviousRadius + 1e-4);
			TestTrue(TEXT("没有越过跟随点或产生无效位置"), Boom.X <= 0 && !Boom.ContainsNaN());
			PreviousRadius = Boom.Size2D();
		}
	}
	// 用空间位置的一阶差分检查跨中环、跨外环时的切线，防止只验证位置连续而漏掉手感上的折点。
	for (const double Angle : {-60.0, 0.0, 45.0})
	{
		constexpr double Step = 0.001;
		const auto Position = [&](double Pitch) { return ComputeCameraLocation(Pivot, FRotator(Pitch, 0, 0), Params, &Orbit); };
		const FVector Before = (Position(Angle) - Position(Angle - Step)) / Step;
		const FVector After = (Position(Angle + Step) - Position(Angle)) / Step;
		TestTrue(TEXT("三环交界处切线连续"), Before.Equals(After, 0.002));
	}
	TestEqual(TEXT("负角与网络规范化后的角度一致"), ComputeOrbitArmLength(150, -60, Orbit), ComputeOrbitArmLength(150, 300, Orbit), 1e-9);
	TestEqual(TEXT("超过上环仍可继续俯视，不再改变视线距离"), ComputeOrbitArmLength(150, -85, Orbit), ComputeOrbitArmLength(150, -60, Orbit), 1e-9);
	const double LowerDistance = ComputeOrbitArmLength(150, 35, Orbit);
	Orbit.UpperRadiusScale = 0.2f;
	TestEqual(TEXT("上环修改不影响下环"), ComputeOrbitArmLength(150, 35, Orbit), LowerDistance, 1e-9);
	Params.ShoulderOffsetRight = -68.f;
	Params.ShoulderOffsetUp = -3.f;
	const FRotator View(-35, 120, 7);
	const FVector Local = View.UnrotateVector(ComputeCameraLocation(Pivot, View, Params, &Orbit) - Pivot);
	TestEqual(TEXT("换肩偏移仍在视角空间，不被轨道半径缩放"), Local.Y, -68.0, 1e-3);
	TestEqual(TEXT("垂直肩偏移不变"), Local.Z, -3.0, 1e-3);
	Orbit.bEnabled = false;
	TestEqual(TEXT("关闭精确恢复原有球面轨道"), ComputeCameraLocation(Pivot, View, Params, &Orbit), ComputeCameraLocation(Pivot, View, Params));
	Orbit.bEnabled = true;
	Orbit.UpperPitch = 90.f;
	Orbit.UpperRadiusScale = -3.f;
	TestTrue(TEXT("外部配置越界仍产生有限正距离"), FMath::IsFinite(ComputeOrbitArmLength(150, -90, Orbit)) && ComputeOrbitArmLength(150, -90, Orbit) > 0);
	return true;
}

#endif

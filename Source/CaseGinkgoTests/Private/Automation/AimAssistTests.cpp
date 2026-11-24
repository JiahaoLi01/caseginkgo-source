#include "Control/GinkgoAimAssist.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	struct FSweep
	{
		/** 准星走到哪了（局部坐标，度） */
		FVector2D Crosshair = FVector2D::ZeroVector;
		/** 准星减目标中线上最近的点 */
		FVector2D Offset = FVector2D::ZeroVector;
	};

	/**
	 * 按游戏里的做法逐帧积分：每一帧开始时把目标按准星现在的位置重新给出（相对准星的局部坐标），玩家匀速转，目标匀速走
	 * @param Fps - 帧率
	 * @param Frames - 几帧：时长是 Frames / Fps，比较的时候要是整数帧
	 */
	FSweep Simulate(const double Fps, const int32 Frames, const FVector2D& InputVelocity, FVector2D A, FVector2D B, const FVector2D& TargetVelocity,
		const float Strength, const float Inner, const float Outer, const float MotionScale)
	{
		FSweep Sweep;
		const double DeltaTime = 1.0 / Fps;
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			FGinkgoAimAssistField Field;
			Field.SegmentA = A - Sweep.Crosshair;
			Field.SegmentB = B - Sweep.Crosshair;
			Field.Velocity = TargetVelocity;
			Field.InnerRadius = Inner;
			Field.OuterRadius = Outer;
			Field.Strength = Strength;
			Sweep.Crosshair += InputVelocity * DeltaTime + Ginkgo::AimAssist::IntegrateFrame(Field, InputVelocity, MotionScale, DeltaTime);
			A += TargetVelocity * DeltaTime;
			B += TargetVelocity * DeltaTime;
		}
		Sweep.Offset = Sweep.Crosshair - Ginkgo::AimAssist::ClosestPointOnSegment(Sweep.Crosshair, A, B);
		return Sweep;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAimAssistHalfLenzTest, "CaseGinkgo.Control.AimAssist.HalfLenz",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAimAssistHalfLenzTest::RunTest(const FString& Parameters)
{
	// 准星在目标右边 1°
	const FVector2D Right(1.0, 0.0);
	TestTrue(TEXT("往里来（来）：不管"), Ginkgo::AimAssist::HalfLenzVelocity(Right, FVector2D(-10.0, 0.0), 0.6f).IsNearlyZero());
	TestTrue(TEXT("往外走（去）：往外的速度减掉强度那么多"), Ginkgo::AimAssist::HalfLenzVelocity(Right, FVector2D(10.0, 0.0), 0.6f).Equals(FVector2D(-6.0, 0.0), 1.e-6));
	TestTrue(TEXT("切向的不管"), Ginkgo::AimAssist::HalfLenzVelocity(Right, FVector2D(0.0, 10.0), 0.6f).IsNearlyZero());
	TestTrue(TEXT("斜着往外：只减径向的那部分"), Ginkgo::AimAssist::HalfLenzVelocity(Right, FVector2D(10.0, 10.0), 0.6f).Equals(FVector2D(-6.0, 0.0), 1.e-6));
	TestTrue(TEXT("斜着往里：不管"), Ginkgo::AimAssist::HalfLenzVelocity(Right, FVector2D(-10.0, 10.0), 0.6f).IsNearlyZero());
	TestTrue(TEXT("强度 0：不管"), Ginkgo::AimAssist::HalfLenzVelocity(Right, FVector2D(10.0, 0.0), 0.f).IsNearlyZero());
	TestTrue(TEXT("正在中线上（没有径向）：不管"), Ginkgo::AimAssist::HalfLenzVelocity(FVector2D::ZeroVector, FVector2D(10.0, 0.0), 0.6f).IsNearlyZero());
	TestTrue(TEXT("强度最多到 0.95：往外总还剩一点，出得去"), Ginkgo::AimAssist::HalfLenzVelocity(Right, FVector2D(10.0, 0.0), 2.f).Equals(FVector2D(-9.5, 0.0), 1.e-6));
	// 准星在目标左上：径向是左上
	const FVector2D UpLeft(-3.0, 4.0);
	const FVector2D Away = Ginkgo::AimAssist::HalfLenzVelocity(UpLeft, FVector2D(-3.0, 4.0), 0.5f);
	TestTrue(TEXT("别的方向也一样：沿径向往外的减一半"), Away.Equals(FVector2D(1.5, -2.0), 1.e-6));

	TestEqual(TEXT("满强度以内是 1"), Ginkgo::AimAssist::Falloff(0.5f, 1.f, 3.f), 1.f);
	TestEqual(TEXT("边上是 1"), Ginkgo::AimAssist::Falloff(1.f, 1.f, 3.f), 1.f);
	TestEqual(TEXT("中间是一半"), Ginkgo::AimAssist::Falloff(2.f, 1.f, 3.f), 0.5f, 1.e-6f);
	TestEqual(TEXT("到零的半径以外是 0"), Ginkgo::AimAssist::Falloff(3.f, 1.f, 3.f), 0.f);
	bool bMonotonic = true;
	for (float Distance = 0.f; Distance < 4.f; Distance += 0.01f)
	{
		bMonotonic &= Ginkgo::AimAssist::Falloff(Distance + 0.01f, 1.f, 3.f) <= Ginkgo::AimAssist::Falloff(Distance, 1.f, 3.f);
	}
	TestTrue(TEXT("越远越弱"), bMonotonic);
	TestEqual(TEXT("两个半径一样：里面 1"), Ginkgo::AimAssist::Falloff(1.f, 1.f, 1.f), 1.f);
	TestEqual(TEXT("两个半径一样：外面 0"), Ginkgo::AimAssist::Falloff(1.01f, 1.f, 1.f), 0.f);

	TestTrue(TEXT("线段上最近的点：中间"), Ginkgo::AimAssist::ClosestPointOnSegment(FVector2D(2.0, 0.5), FVector2D(0.0, -1.0), FVector2D(0.0, 1.0)).Equals(FVector2D(0.0, 0.5), 1.e-9));
	TestTrue(TEXT("线段上最近的点：过了头是端点"), Ginkgo::AimAssist::ClosestPointOnSegment(FVector2D(2.0, 5.0), FVector2D(0.0, -1.0), FVector2D(0.0, 1.0)).Equals(FVector2D(0.0, 1.0), 1.e-9));
	TestTrue(TEXT("线段缩成一点"), Ginkgo::AimAssist::ClosestPointOnSegment(FVector2D(2.0, 5.0), FVector2D(1.0, 1.0), FVector2D(1.0, 1.0)).Equals(FVector2D(1.0, 1.0), 1.e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAimAssistSweepTest, "CaseGinkgo.Control.AimAssist.Sweep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAimAssistSweepTest::RunTest(const FString& Parameters)
{
	// 目标的中线竖着，在准星右边 3°；满强度 0.5°，淡到零 2.5°；往右匀速 30°/s 扫过去
	const FVector2D A(3.0, -2.0), B(3.0, 2.0);
	const FVector2D Input(30.0, 0.0);
	constexpr float Strength = 0.6f, Inner = 0.5f, Outer = 2.5f;

	// 靠近的时候（来）一点不管：还没到中线的这段，同没有辅助完全一样
	{
		// 13 帧走 2.71°，还在中线左边
		const FSweep Before = Simulate(144.0, 13, Input, A, B, FVector2D::ZeroVector, Strength, Inner, Outer, 1.f);
		TestEqual(TEXT("靠近时不减速：走的同输入一样"), Before.Crosshair.X, 13.0 / 144.0 * 30.0, 1.e-9);
	}

	// 过了中线（去）：在满强度里往外的速度只剩四成，比没有辅助的慢；淡到零以外又同输入一样快
	const FSweep Plain = Simulate(144.0, 72, Input, A, B, FVector2D::ZeroVector, 0.f, Inner, Outer, 1.f);
	const FSweep Assisted = Simulate(144.0, 72, Input, A, B, FVector2D::ZeroVector, Strength, Inner, Outer, 1.f);
	TestTrue(TEXT("过了中线以后被拉住：走得比没有辅助的少"), Assisted.Crosshair.X < Plain.Crosshair.X - 0.5);
	TestTrue(TEXT("但出得去：最后在淡到零的半径以外"), Assisted.Offset.X > Outer);
	{
		// 过中线以后 0.25°（还在满强度里）再走一帧
		const FSweep Inside = Simulate(960.0, 100, Input, A, B, FVector2D::ZeroVector, Strength, Inner, Outer, 1.f);
		const FSweep Later = Simulate(960.0, 101, Input, A, B, FVector2D::ZeroVector, Strength, Inner, Outer, 1.f);
		const double Speed = (Later.Crosshair.X - Inside.Crosshair.X) * 960.0;
		TestEqual(TEXT("满强度里往外的速度：输入的 1 − 强度"), Speed, 30.0 * (1.0 - Strength), 0.05);
	}

	// 与帧率无关：同样的输入，30、60、120、144、240 fps 都扫 0.5 s。积分的一小步里跨过中线的那一步，跨过之前不修、之后修，
	// 所以最多差 强度 × 速度 × 步长（0.6 × 30°/s × 1/960 s ≈ 0.019°，1080p、90° 视场下 0.2 个像素）
	double Spread = 0.0;
	for (const double Fps : {30.0, 60.0, 120.0, 144.0, 240.0})
	{
		Spread = FMath::Max(Spread, FMath::Abs(Simulate(Fps, FMath::RoundToInt(0.5 * Fps), Input, A, B, FVector2D::ZeroVector, Strength, Inner, Outer, 1.f).Crosshair.X - Assisted.Crosshair.X));
	}
	AddInfo(FString::Printf(TEXT("扫过 0.5 s：没有辅助走 %.4f°，有辅助 %.4f°；五种帧率最多差 %.6f°"), Plain.Crosshair.X, Assisted.Crosshair.X, Spread));
	TestTrue(TEXT("五种帧率差不到 0.02°"), Spread < 0.02);

	// 沿着身体上下走：径向是水平的，竖直的走不管
	{
		const FSweep Along = Simulate(144.0, 18, FVector2D(0.0, 20.0), FVector2D(0.0, -5.0), FVector2D(0.0, 5.0), FVector2D::ZeroVector, Strength, Inner, Outer, 1.f);
		TestEqual(TEXT("沿着中线上下走不减速"), Along.Crosshair.Y, 2.5, 1.e-9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAimAssistParallaxTest, "CaseGinkgo.Control.AimAssist.Parallax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAimAssistParallaxTest::RunTest(const FString& Parameters)
{
	// 越肩的相机：视角转 1°，准星在目标上只走 0.87°（还带一点斜）
	FGinkgoAimAssistField Field;
	Field.ScreenX = FVector2D(0.87, 0.02);
	Field.ScreenY = FVector2D(-0.01, 0.9);
	const FVector2D View(3.0, -2.0);
	TestTrue(TEXT("视角换成画面再换回来"), Ginkgo::AimAssist::ViewFromScreen(Field, Ginkgo::AimAssist::ScreenFromView(Field, View)).Equals(View, 1.e-9));

	// 准星在中线右边 0.1°（满强度以内），往右转：画面上往外的速度剩 1 − 强度，切向（这里是竖直的）不动
	Field.SegmentA = FVector2D(-0.1, -2.0);
	Field.SegmentB = FVector2D(-0.1, 2.0);
	Field.InnerRadius = 2.f;
	Field.OuterRadius = 4.f;
	Field.Strength = 0.6f;
	const FVector2D Input(30.0, 0.0);
	const FVector2D Correction = Ginkgo::AimAssist::IntegrateFrame(Field, Input, 1.f, 1.f / 144.f);
	const FVector2D Plain = Ginkgo::AimAssist::ScreenFromView(Field, Input / 144.0);
	const FVector2D Assisted = Ginkgo::AimAssist::ScreenFromView(Field, Input / 144.0 + Correction);
	TestEqual(TEXT("视差下画面上往外的速度剩 1 − 强度"), Assisted.X, Plain.X * 0.4, 1.e-6);
	TestEqual(TEXT("视差下画面上切向的不动"), Assisted.Y, Plain.Y, 1.e-6);

	// 退化的（行列式差不多是 0）当作没有视差
	FGinkgoAimAssistField Degenerate;
	Degenerate.ScreenX = FVector2D(1.0, 1.0);
	Degenerate.ScreenY = FVector2D(1.0, 1.0);
	TestTrue(TEXT("退化时不换"), Ginkgo::AimAssist::ViewFromScreen(Degenerate, View).Equals(View, 1.e-12));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAimAssistTargetMotionTest, "CaseGinkgo.Control.AimAssist.TargetMotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAimAssistTargetMotionTest::RunTest(const FString& Parameters)
{
	// 准星在目标右边 0.2°（满强度以内，范围放大到 2°，这 0.125 s 一直在满强度里），目标往左走 10°/s，玩家不转
	const FVector2D A(-0.2, -2.0), B(-0.2, 2.0);
	const FVector2D Left(-10.0, 0.0);
	constexpr float Strength = 0.6f, Inner = 2.f, Outer = 4.f;
	const FSweep Follow = Simulate(144.0, 18, FVector2D::ZeroVector, A, B, Left, Strength, Inner, Outer, 1.f);
	TestEqual(TEXT("算目标的运动：准星跟着走，速度是目标的强度倍"), Follow.Crosshair.X, -10.0 * 0.125 * Strength, 1.e-6);
	const FSweep Stay = Simulate(144.0, 18, FVector2D::ZeroVector, A, B, Left, Strength, Inner, Outer, 0.f);
	TestEqual(TEXT("不算目标的运动：准星不动"), Stay.Crosshair.X, 0.0, 1.e-9);

	// 目标往准星这边走（来）：不管
	const FSweep Coming = Simulate(144.0, 1, FVector2D::ZeroVector, FVector2D(-0.4, -2.0), FVector2D(-0.4, 2.0), FVector2D(10.0, 0.0), Strength, Inner, Outer, 1.f);
	TestEqual(TEXT("目标往准星这边走：准星不动"), Coming.Crosshair.X, 0.0, 1.e-9);

	// 跟着目标转（玩家转得同目标一样快）：相对速度是 0，没有修正
	const FSweep Tracking = Simulate(144.0, 18, Left, A, B, Left, Strength, Inner, Outer, 1.f);
	TestEqual(TEXT("跟得上目标时不修"), Tracking.Crosshair.X, -1.25, 1.e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAimAssistProfileTest, "CaseGinkgo.Control.AimAssist.Profile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAimAssistProfileTest::RunTest(const FString& Parameters)
{
	// 玩家的设置换成算法用的：两种设备各取各的，最远从 m 换成 cm
	FGinkgoInputSettings Settings;
	Settings.GamepadAimAssistHip = 0.7f;
	Settings.GamepadAimAssistAds = 0.9f;
	Settings.GamepadAimAssistInner = 40.f;
	Settings.GamepadAimAssistOuter = 250.f;
	Settings.GamepadAimAssistRange = 80.f;
	Settings.GamepadAimAssistFollow = 0.5f;
	const FGinkgoAimAssistProfile Pad = Ginkgo::AimAssist::MakeProfile(Settings, EGinkgoInputDirectory::Gamepad);
	TestEqual(TEXT("手柄：不开镜的强度"), Pad.HipStrength, 0.7f);
	TestEqual(TEXT("手柄：开镜的强度"), Pad.AdsStrength, 0.9f);
	TestEqual(TEXT("手柄：满强度的半径"), Pad.InnerRadius, 40.f);
	TestEqual(TEXT("手柄：淡到零的半径"), Pad.OuterRadius, 250.f);
	TestEqual(TEXT("手柄：最远换成 cm"), Pad.MaxDistance, 8000.f);
	TestEqual(TEXT("手柄：跟随目标"), Pad.TargetMotionScale, 0.5f);

	const FGinkgoInputSettings Defaults;
	const FGinkgoAimAssistProfile Mouse = Ginkgo::AimAssist::MakeProfile(Settings, EGinkgoInputDirectory::KeyboardMouse);
	TestEqual(TEXT("键鼠取键鼠的（没改过，是默认值）：不开镜的强度"), Mouse.HipStrength, Defaults.MouseAimAssistHip);
	TestEqual(TEXT("键鼠：淡到零的半径"), Mouse.OuterRadius, Defaults.MouseAimAssistOuter);
	TestEqual(TEXT("键鼠：跟随目标"), Mouse.TargetMotionScale, Defaults.MouseAimAssistFollow);

	// 总开关：手柄默认开启，键鼠默认关闭；修改一端不影响另一端
	TestTrue(TEXT("手柄辅助瞄准默认开着"), Pad.bEnabled);
	TestFalse(TEXT("键鼠辅助瞄准默认关闭"), Mouse.bEnabled);
	FGinkgoInputSettings Swapped;
	Swapped.bGamepadAimAssist = false;
	Swapped.bMouseAimAssist = true;
	TestFalse(TEXT("手柄的关了：手柄的关着"), Ginkgo::AimAssist::MakeProfile(Swapped, EGinkgoInputDirectory::Gamepad).bEnabled);
	TestTrue(TEXT("手柄的关了：键鼠单开不受影响"), Ginkgo::AimAssist::MakeProfile(Swapped, EGinkgoInputDirectory::KeyboardMouse).bEnabled);

	// 设置界面最多给到算法的上限；字段被直接写大了（不走 WriteSetting）也夹回来：再大准星就出不去了
	const FGinkgoInputSettingDef* Hip = Ginkgo::Input::FindSettingDef(TEXT("gamepad.aimAssistHip"));
	TestTrue(TEXT("设置界面的强度到算法的上限为止"), Hip && Hip->Max == static_cast<double>(Ginkgo::AimAssist::MaxStrength));
	Settings.GamepadAimAssistHip = 3.f;
	TestEqual(TEXT("强度夹到上限"), Ginkgo::AimAssist::MakeProfile(Settings, EGinkgoInputDirectory::Gamepad).HipStrength, Ginkgo::AimAssist::MaxStrength);
	return true;
}

#endif

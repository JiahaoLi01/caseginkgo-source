#include "Control/GinkgoInputMath.h"
#include "Control/GinkgoInputSettings.h"
#include "HAL/FileManager.h"
#include "InputActionValue.h"
#include "InputModifiers.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace Ginkgo::Input;

namespace GinkgoInputSettingsTests
{
	const EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** 引擎的径向死区：做设置界面之前摇杆走的就是它 */
	FVector2D EngineDeadZone(UInputModifierDeadZone& Modifier, const FVector2D& Stick)
	{
		return Modifier.ModifyRaw(nullptr, FInputActionValue(EInputActionValueType::Axis2D, FVector(Stick.X, Stick.Y, 0.0)), 1.f / 60.f).Get<FVector2D>();
	}

	/** 按住一段时间总共转了多少度（frames 个 dt）：转身加速的帧率无关看它 */
	FVector2D HoldStick(const FVector2D& Stick, const FGinkgoInputSettings& Settings, const double Seconds, const int32 Fps)
	{
		FGinkgoTurnRamp Ramp;
		FVector2D Total = FVector2D::ZeroVector;
		const float Dt = 1.f / Fps;
		for (int32 Frame = 0; Frame < FMath::RoundToInt32(Seconds * Fps); ++Frame)
		{
			Total += StickLookDegrees(Stick, Settings, 0.f, 1.f, Ramp, Dt);
		}
		return Total;
	}

	FString TemporaryFile(const TCHAR* Name)
	{
		return FPaths::AutomationTransientDir() / TEXT("InputSettings") / Name;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputDeadZoneTest, "CaseGinkgo.Control.Input.DeadZone", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputDeadZoneTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("中心是零"), ApplyRadialDeadZone(FVector2D::ZeroVector, 0.2f, 1.f), FVector2D::ZeroVector);
	TestEqual(TEXT("内死区里是零"), ApplyRadialDeadZone(FVector2D(0.1, 0.1), 0.2f, 1.f), FVector2D::ZeroVector);
	TestEqual(TEXT("内死区的边上是零"), ApplyRadialDeadZone(FVector2D(0.2, 0.0), 0.2f, 1.f), FVector2D::ZeroVector);
	TestEqual(TEXT("外圈以外是单位向量"), ApplyRadialDeadZone(FVector2D(0.9, 0.0), 0.2f, 0.8f).X, 1.0, 1e-6);
	TestEqual(TEXT("中间线性：0.6 在 0.2–1 的一半"), ApplyRadialDeadZone(FVector2D(0.6, 0.0), 0.2f, 1.f).X, 0.5, 1e-6);
	const FVector2D Diagonal = ApplyRadialDeadZone(FVector2D(0.5, 0.5), 0.2f, 1.f);
	TestEqual(TEXT("方向不变"), Diagonal.X, Diagonal.Y, 1e-9);
	TestEqual(TEXT("斜着按半径算"), Diagonal.Size(), (FMath::Sqrt(0.5) - 0.2) / 0.8, 1e-6);
	TestEqual(TEXT("内死区 0 时小的值不丢"), ApplyRadialDeadZone(FVector2D(0.01, 0.0), 0.f, 1.f).X, 0.01, 1e-6);

	// 默认的内外圈上与引擎的修改器（做设置界面之前用的）比一遍：一圈方向 × 一串大小
	UInputModifierDeadZone* Engine = NewObject<UInputModifierDeadZone>();
	const FGinkgoInputSettings Defaults;
	double Worst = 0.0;
	for (int32 Angle = 0; Angle < 72; ++Angle)
	{
		for (int32 Step = 0; Step <= 110; ++Step)
		{
			const double Radians = Angle * 5.0 * UE_DOUBLE_PI / 180.0;
			const FVector2D Stick = FVector2D(FMath::Cos(Radians), FMath::Sin(Radians)) * (Step * 0.01);
			Worst = FMath::Max(Worst, (ApplyRadialDeadZone(Stick, Defaults.GamepadLookInner, Defaults.GamepadLookOuter) - GinkgoInputSettingsTests::EngineDeadZone(*Engine, Stick)).Size());
		}
	}
	TestTrue(FString::Printf(TEXT("默认值与引擎的径向死区一样（最大差 %g）"), Worst), Worst < 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputResponseCurveTest, "CaseGinkgo.Control.Input.ResponseCurve", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputResponseCurveTest::RunTest(const FString& Parameters)
{
	const EGinkgoResponseCurve Curves[] = {EGinkgoResponseCurve::Linear, EGinkgoResponseCurve::Power, EGinkgoResponseCurve::SCurve};
	for (const EGinkgoResponseCurve Curve : Curves)
	{
		for (const float Strength : {1.f, 1.5f, 2.f, 3.f, 4.f})
		{
			TestEqual(TEXT("0 映到 0"), ApplyResponseCurve(0.f, Curve, Strength), 0.f, 1e-6f);
			TestEqual(TEXT("1 映到 1"), ApplyResponseCurve(1.f, Curve, Strength), 1.f, 1e-6f);
			float Previous = 0.f;
			bool bMonotonic = true;
			bool bContinuous = true;
			for (int32 Step = 1; Step <= 1000; ++Step)
			{
				const float Value = ApplyResponseCurve(Step * 0.001f, Curve, Strength);
				bMonotonic = bMonotonic && Value >= Previous;
				// 最陡的是强度 4 的 S 形，中点斜率 4
				bContinuous = bContinuous && Value - Previous < 0.01f;
				Previous = Value;
			}
			TestTrue(TEXT("单调"), bMonotonic);
			TestTrue(TEXT("没有跳变"), bContinuous);
		}
		TestEqual(TEXT("超出范围夹回来"), ApplyResponseCurve(1.5f, Curve, 2.f), 1.f, 1e-6f);
		TestEqual(TEXT("强度 1 就是线性"), ApplyResponseCurve(0.37f, Curve, 1.f), 0.37f, 1e-5f);
	}
	TestEqual(TEXT("线性不管强度"), ApplyResponseCurve(0.5f, EGinkgoResponseCurve::Linear, 3.f), 0.5f, 1e-6f);
	TestEqual(TEXT("指数 2：0.5 → 0.25"), ApplyResponseCurve(0.5f, EGinkgoResponseCurve::Power, 2.f), 0.25f, 1e-6f);
	TestEqual(TEXT("S 形一半还是一半"), ApplyResponseCurve(0.5f, EGinkgoResponseCurve::SCurve, 3.f), 0.5f, 1e-6f);
	TestEqual(TEXT("S 形 2：0.25 → 0.1"), ApplyResponseCurve(0.25f, EGinkgoResponseCurve::SCurve, 2.f), 0.1f, 1e-6f);
	TestEqual(TEXT("S 形对称"), ApplyResponseCurve(0.2f, EGinkgoResponseCurve::SCurve, 2.5f) + ApplyResponseCurve(0.8f, EGinkgoResponseCurve::SCurve, 2.5f), 1.f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputMouseTest, "CaseGinkgo.Control.Input.Mouse", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputMouseTest::RunTest(const FString& Parameters)
{
	const FGinkgoInputSettings Defaults;
	// 默认灵敏度 0.7（0.7 × 0.022 = 0.0154°/count），且默认开启越肩开镜按视场补偿
	const double DefaultDegPerCount = static_cast<double>(Defaults.MouseSensitivity) * 0.022;
	const FVector2D Plain = MouseLookDegrees(FVector2D(1000.0, -200.0), Defaults, 0.f, 1.f, 1.f / 60.f);
	TestEqual(TEXT("默认 1000 count 转 15.4°"), Plain.X, 1000.0 * DefaultDegPerCount, 1e-3);
	TestEqual(TEXT("向上为正"), Plain.Y, -200.0 * DefaultDegPerCount, 1e-3);
	TestEqual(TEXT("开镜默认按视场换算：除以缩放倍率"), MouseLookDegrees(FVector2D(1000.0, 0.0), Defaults, 1.f, 1.732f, 1.f / 60.f).X, 1000.0 * DefaultDegPerCount / 1.732, 1e-3);

	FGinkgoInputSettings Settings;
	Settings.MouseSensitivity = 2.f;
	TestEqual(TEXT("灵敏度：Source 的尺度"), MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 0.f, 1.f, 1.f / 60.f).X, 44.0, 1e-3);
	Settings.MousePitchRatio = 0.5f;
	TestEqual(TEXT("竖直比例"), MouseLookDegrees(FVector2D(0.0, 1000.0), Settings, 0.f, 1.f, 1.f / 60.f).Y, 22.0, 1e-3);
	Settings.bMouseInvertPitch = true;
	TestEqual(TEXT("反转上下"), MouseLookDegrees(FVector2D(0.0, 1000.0), Settings, 0.f, 1.f, 1.f / 60.f).Y, -22.0, 1e-3);
	Settings.bMouseInvertYaw = true;
	TestEqual(TEXT("反转左右"), MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 0.f, 1.f, 1.f / 60.f).X, -44.0, 1e-3);

	// 开镜的倍率
	Settings = FGinkgoInputSettings();
	Settings.bMouseAdsMatchFov = false;
	Settings.MouseAdsScale = 0.5f;
	const double BaseThousand = 1000.0 * DefaultDegPerCount;
	TestEqual(TEXT("不开镜不受开镜倍率影响"), MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 0.f, 1.732f, 1.f / 60.f).X, BaseThousand, 1e-3);
	TestEqual(TEXT("开镜到位时乘倍率"), MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 1.f, 1.732f, 1.f / 60.f).X, BaseThousand * 0.5, 1e-3);
	TestEqual(TEXT("开镜到一半时一半"), MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 0.5f, 1.732f, 1.f / 60.f).X, BaseThousand * 0.75, 1e-3);
	Settings.MouseAdsScale = 1.f;
	Settings.bMouseAdsMatchFov = true;
	TestEqual(TEXT("按视场换算：除以缩放倍率"), MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 1.f, 1.732f, 1.f / 60.f).X, BaseThousand / 1.732, 1e-3);
	TestEqual(TEXT("徒手（缩放 1）不换算"), AdsLookScale(1.f, 1.f, true, 1.f), 1.f, 1e-6f);

	// 加速度：倍率
	TestEqual(TEXT("比起点慢时不加"), MouseAccelerationGain(2.f, 0.1f, 3.f, 3.f), 1.f, 1e-6f);
	TestEqual(TEXT("线性往上"), MouseAccelerationGain(8.f, 0.1f, 3.f, 3.f), 1.5f, 1e-6f);
	TestEqual(TEXT("到上限不再加"), MouseAccelerationGain(100.f, 0.1f, 3.f, 3.f), 3.f, 1e-6f);
	TestEqual(TEXT("上限小于 1 也不减速"), MouseAccelerationGain(100.f, 0.1f, 3.f, 0.5f), 1.f, 1e-6f);

	// 加速度：匀速时与帧率无关（按一帧的量算的 Source 做不到）
	Settings = FGinkgoInputSettings();
	Settings.bMouseAcceleration = true;
	const double CountsPerSecond = 20000.0;
	const auto TotalAt = [&Settings, CountsPerSecond](const int32 Fps)
	{
		double Total = 0.0;
		for (int32 Frame = 0; Frame < Fps; ++Frame)
		{
			Total += MouseLookDegrees(FVector2D(CountsPerSecond / Fps, 0.0), Settings, 0.f, 1.f, 1.f / Fps).X;
		}
		return Total;
	};
	const double Expected = CountsPerSecond * DefaultDegPerCount * MouseAccelerationGain(20.f, Settings.MouseAccelStrength, Settings.MouseAccelOffset, Settings.MouseAccelCap);
	for (const int32 Fps : {30, 60, 144, 240})
	{
		TestEqual(FString::Printf(TEXT("匀速一秒转的角度与帧率无关（%d fps）"), Fps), TotalAt(Fps), Expected, 0.05);
	}
	Settings.bMouseAcceleration = false;
	TestEqual(TEXT("关着时不加"), MouseLookDegrees(FVector2D(CountsPerSecond / 60, 0.0), Settings, 0.f, 1.f, 1.f / 60.f).X, CountsPerSecond / 60 * DefaultDegPerCount, 1e-2);
	Settings.bMouseAcceleration = true;
	TestEqual(TEXT("帧时长是 0 时不除零"), MouseLookDegrees(FVector2D(100.0, 0.0), Settings, 0.f, 1.f, 0.f).X, 100.0 * DefaultDegPerCount, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputStickTest, "CaseGinkgo.Control.Input.Stick", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputStickTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoInputSettingsTests;
	const FGinkgoInputSettings Defaults;
	FGinkgoTurnRamp Ramp;
	// 默认值：死区 0.2–1、S 形曲线（强度 2）、满偏 150°/s、上下反转、移动外圈 0.8
	TestEqual(TEXT("默认满偏 150°/s"), StickLookDegrees(FVector2D(1.0, 0.0), Defaults, 0.f, 1.f, Ramp, 0.5f).X, 75.0, 1e-4);
	// 偏转 0.5 过死区后为 0.375，S 形强度 2 映射 0.375² / (0.375² + 0.625²) = 9/34
	TestEqual(TEXT("默认 S 形曲线 0.5 偏转转速"), StickLookDegrees(FVector2D(0.5, 0.0), Defaults, 0.f, 1.f, Ramp, 1.f).X, 150.0 * (9.0 / 34.0), 1e-3);
	TestEqual(TEXT("死区里不转"), StickLookDegrees(FVector2D(0.15, 0.0), Defaults, 0.f, 1.f, Ramp, 1.f), FVector2D::ZeroVector);
	TestEqual(TEXT("默认反转上下：摇杆往上推，视角往下"), StickLookDegrees(FVector2D(0.0, 1.0), Defaults, 0.f, 1.f, Ramp, 1.f).Y, -150.0, 1e-4);
	TestEqual(TEXT("默认移动摇杆外圈 0.8 即满幅"), StickMoveInput(FVector2D(0.8, 0.0), Defaults).X, 1.0, 1e-6);

	FGinkgoInputSettings Settings;
	Settings.GamepadYawSpeed = 300.f;
	Settings.GamepadPitchSpeed = 100.f;
	const FVector2D Speeds = StickLookDegrees(FVector2D(1.0, 0.0), Settings, 0.f, 1.f, Ramp, 1.f);
	TestEqual(TEXT("水平转速"), Speeds.X, 300.0, 1e-4);
	TestEqual(TEXT("竖直转速"), StickLookDegrees(FVector2D(0.0, 1.0), Settings, 0.f, 1.f, Ramp, 1.f).Y, -100.0, 1e-4);
	Settings.bGamepadInvertPitch = false;
	Settings.bGamepadInvertYaw = true;
	TestEqual(TEXT("反转左右"), StickLookDegrees(FVector2D(1.0, 0.0), Settings, 0.f, 1.f, Ramp, 1.f).X, -300.0, 1e-4);
	TestEqual(TEXT("关掉反转上下：摇杆往上推，视角往上"), StickLookDegrees(FVector2D(0.0, 1.0), Settings, 0.f, 1.f, Ramp, 1.f).Y, 100.0, 1e-4);

	// 响应曲线作用在半径上，方向不变
	Settings = FGinkgoInputSettings();
	Settings.GamepadCurve = EGinkgoResponseCurve::Power;
	Settings.GamepadCurveStrength = 2.f;
	const FVector2D Curved = StickLookDegrees(FVector2D(0.6, 0.0), Settings, 0.f, 1.f, Ramp, 1.f);
	TestEqual(TEXT("指数：过了死区 0.5 → 0.25"), Curved.X, 37.5, 1e-3);
	const FVector2D Diagonal = StickLookDegrees(FVector2D(0.5, 0.5), Settings, 0.f, 1.f, Ramp, 1.f);
	TestEqual(TEXT("曲线不改方向（默认反转上下）"), Diagonal.X, -Diagonal.Y, 1e-4);

	// 开镜
	Settings = FGinkgoInputSettings();
	Settings.GamepadAdsScale = 0.4f;
	TestEqual(TEXT("开镜倍率"), StickLookDegrees(FVector2D(1.0, 0.0), Settings, 1.f, 1.732f, Ramp, 1.f).X, 60.0, 1e-3);
	Settings.GamepadAdsScale = 1.f;
	Settings.bGamepadAdsMatchFov = true;
	TestEqual(TEXT("按视场换算"), StickLookDegrees(FVector2D(1.0, 0.0), Settings, 1.f, 1.732f, Ramp, 1.f).X, 150.0 / 1.732, 1e-2);

	// 移动摇杆只有死区，用自己的内外圈
	Settings = FGinkgoInputSettings();
	Settings.GamepadMoveInner = 0.1f;
	Settings.GamepadMoveOuter = 0.6f;
	TestEqual(TEXT("移动的内死区"), StickMoveInput(FVector2D(0.35, 0.0), Settings).X, 0.5, 1e-6);
	TestEqual(TEXT("移动的外圈"), StickMoveInput(FVector2D(0.7, 0.0), Settings).X, 1.0, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputTurnRampTest, "CaseGinkgo.Control.Input.TurnRamp", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputTurnRampTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoInputSettingsTests;
	FGinkgoInputSettings Settings;
	Settings.GamepadExtraYaw = 200.f;
	Settings.GamepadExtraDelay = 0.1f;
	Settings.GamepadExtraRamp = 0.3f;
	// 按住 1 s：基础 150° + 额外 200 × ∫权重。权重 0.1 s 以前是 0、0.1–0.4 s 爬到 1、之后是 1，积分 = 0.15 + 0.6 = 0.75
	const double Expected = 150.0 + 200.0 * 0.75;
	for (const int32 Fps : {30, 60, 144, 240, 37})
	{
		// 帧不正好落在 1 s 上时比的是 Frames / Fps 那么长
		const int32 Frames = FMath::RoundToInt32(1.0 * Fps);
		const double Seconds = static_cast<double>(Frames) / Fps;
		const double Exact = 150.0 * Seconds + 200.0 * (Seconds - 0.25);
		TestEqual(FString::Printf(TEXT("按住 1 s 转的角度与帧率无关（%d fps）"), Fps), HoldStick(FVector2D(1.0, 0.0), Settings, Seconds, Fps).X, Exact, 1e-3);
	}
	TestEqual(TEXT("算出来的总量"), HoldStick(FVector2D(1.0, 0.0), Settings, 1.0, 60).X, Expected, 1e-3);
	TestEqual(TEXT("延迟之内不加"), HoldStick(FVector2D(1.0, 0.0), Settings, 0.1, 60).X, 15.0, 1e-3);

	FGinkgoTurnRamp Ramp;
	TestEqual(TEXT("一下子满（爬升 0）"), Ramp.Step(true, 0.1f, 0.05f, 0.f), 0.5f, 1e-5f);
	TestEqual(TEXT("之后都是 1"), Ramp.Step(true, 0.1f, 0.05f, 0.f), 1.f, 1e-5f);
	TestEqual(TEXT("松开清零"), Ramp.Step(false, 0.1f, 0.05f, 0.f), 0.f, 1e-5f);
	TestEqual(TEXT("再按从头算"), Ramp.Step(true, 0.05f, 0.05f, 0.f), 0.f, 1e-5f);

	// 不满偏不加；开镜不加（显式用线性曲线便于按比例核算 0.75）
	Settings.GamepadCurve = EGinkgoResponseCurve::Linear;
	Settings.GamepadYawSpeed = 100.f;
	TestEqual(TEXT("不满偏不加（过了死区 0.75）"), HoldStick(FVector2D(0.8, 0.0), Settings, 1.0, 60).X, 75.0, 1e-3);
	FGinkgoTurnRamp AdsRamp;
	double AdsTotal = 0.0;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		AdsTotal += StickLookDegrees(FVector2D(1.0, 0.0), Settings, 1.f, 1.f, AdsRamp, 1.f / 60.f).X;
	}
	TestEqual(TEXT("开镜到位时不加"), AdsTotal, 100.0, 1e-3);
	// 默认额外转速是 0：什么时候都不加
	TestEqual(TEXT("默认不加"), HoldStick(FVector2D(1.0, 0.0), FGinkgoInputSettings(), 2.0, 60).X, 300.0, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputHoldActionsTest, "CaseGinkgo.Control.Input.HoldActions", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputHoldActionsTest::RunTest(const FString& Parameters)
{
	constexpr EGinkgoHoldAction Crouch = EGinkgoHoldAction::Crouch;
	constexpr EGinkgoHoldAction Sprint = EGinkgoHoldAction::Sprint;
	constexpr EGinkgoHoldAction Aim = EGinkgoHoldAction::Aim;
	constexpr EGinkgoInputDirectory Keys = EGinkgoInputDirectory::KeyboardMouse;
	constexpr EGinkgoInputDirectory Pad = EGinkgoInputDirectory::Gamepad;
	constexpr EGinkgoKeySlot First = EGinkgoKeySlot::Primary;
	constexpr EGinkgoKeySlot Second = EGinkgoKeySlot::Secondary;
	constexpr EGinkgoHoldMode Hold = EGinkgoHoldMode::Hold;
	constexpr EGinkgoHoldMode Toggle = EGinkgoHoldMode::Toggle;

	// 按住：按着才有；两种设备都按着，松开一个还在（同原来一个动作绑两种键）
	{
		FGinkgoHoldActions Actions;
		Actions.Press(Sprint, Keys, First, Hold);
		TestTrue(TEXT("按下就开"), Actions.IsOn(Sprint));
		Actions.Press(Sprint, Pad, First, Hold);
		Actions.Release(Sprint, Keys, First);
		TestTrue(TEXT("另一种设备还按着"), Actions.IsOn(Sprint));
		Actions.Press(Sprint, Pad, Second, Hold);
		Actions.Release(Sprint, Pad, First);
		TestTrue(TEXT("同一种设备的另一个键位还按着"), Actions.IsOn(Sprint));
		Actions.Release(Sprint, Pad, Second);
		TestFalse(TEXT("都松开就关"), Actions.IsOn(Sprint));
		// 按住的冲刺不受"停下来"影响，开镜也不收它（按着就是按着）
		Actions.Press(Sprint, Keys, First, Hold);
		Actions.Tick(false, 1.f);
		Actions.Press(Aim, Keys, First, Hold);
		TestTrue(TEXT("按住的冲刺站着也在"), Actions.IsOn(Sprint));
		TestTrue(TEXT("按住的开镜同时也在"), Actions.IsOn(Aim));
	}
	// 切换：按一下开、再按一下关，松开不管
	{
		FGinkgoHoldActions Actions;
		Actions.Press(Crouch, Keys, First, Toggle);
		Actions.Release(Crouch, Keys, First);
		TestTrue(TEXT("按一下开"), Actions.IsOn(Crouch));
		Actions.Press(Crouch, Keys, First, Toggle);
		Actions.Release(Crouch, Keys, First);
		TestFalse(TEXT("再按一下关"), Actions.IsOn(Crouch));
		Actions.Press(Crouch, Pad, First, Toggle);
		TestTrue(TEXT("手柄切换开"), Actions.IsOn(Crouch));
		Actions.Press(Crouch, Keys, First, Hold);
		Actions.Release(Crouch, Keys, First);
		TestTrue(TEXT("键盘按住松开，手柄切换的还开着"), Actions.IsOn(Crouch));
	}
	// 同一个动作的两个键位各选各的：Ctrl 按住蹲、C 切换蹲
	{
		FGinkgoHoldActions Actions;
		Actions.Press(Crouch, Keys, First, Hold);
		TestTrue(TEXT("按着 Ctrl 蹲"), Actions.IsOn(Crouch));
		Actions.Release(Crouch, Keys, First);
		TestFalse(TEXT("松开 Ctrl 站起来"), Actions.IsOn(Crouch));
		Actions.Press(Crouch, Keys, Second, Toggle);
		Actions.Release(Crouch, Keys, Second);
		TestTrue(TEXT("点一下 C 蹲着"), Actions.IsOn(Crouch));
		Actions.Press(Crouch, Keys, First, Hold);
		Actions.Release(Crouch, Keys, First);
		TestTrue(TEXT("C 切换着，按住 Ctrl 再松开还蹲着"), Actions.IsOn(Crouch));
		Actions.Press(Crouch, Keys, First, Hold);
		Actions.Press(Crouch, Keys, Second, Toggle);
		Actions.Release(Crouch, Keys, Second);
		TestTrue(TEXT("按着 Ctrl 时点 C 关切换：Ctrl 还按着，还蹲着"), Actions.IsOn(Crouch));
		Actions.Release(Crouch, Keys, First);
		TestFalse(TEXT("松开 Ctrl 才站起来"), Actions.IsOn(Crouch));
	}
	// 切换的几条规则
	{
		FGinkgoHoldActions Actions;
		Actions.Press(Crouch, Keys, First, Toggle);
		Actions.OnJump();
		TestFalse(TEXT("起跳收掉切换的蹲"), Actions.IsOn(Crouch));

		Actions.Press(Aim, Keys, First, Toggle);
		Actions.Press(Crouch, Keys, First, Toggle);
		Actions.Press(Sprint, Keys, First, Hold);
		TestFalse(TEXT("开始冲刺收掉切换的开镜"), Actions.IsOn(Aim));
		TestFalse(TEXT("开始冲刺收掉切换的蹲"), Actions.IsOn(Crouch));
		Actions.Release(Sprint, Keys, First);

		Actions.Press(Sprint, Keys, First, Toggle);
		TestTrue(TEXT("切换的冲刺开"), Actions.IsOn(Sprint));
		Actions.Press(Aim, Keys, First, Hold);
		TestFalse(TEXT("开始开镜收掉切换的冲刺"), Actions.IsOn(Sprint));
		Actions.Release(Aim, Keys, First);

		Actions.Press(Sprint, Keys, First, Toggle);
		Actions.Tick(false, 0.2f);
		TestTrue(TEXT("没有移动输入还没够久：还开着（换方向时经过中心）"), Actions.IsOn(Sprint));
		Actions.Tick(true, 0.01f);
		Actions.Tick(false, 0.2f);
		TestTrue(TEXT("有过移动输入从头算"), Actions.IsOn(Sprint));
		Actions.Tick(false, 0.1f);
		TestFalse(TEXT("停下来够久：关"), Actions.IsOn(Sprint));
	}
	// 清掉
	{
		FGinkgoHoldActions Actions;
		Actions.Press(Sprint, Keys, First, Hold);
		Actions.Press(Crouch, Keys, First, Toggle);
		Actions.ReleaseAllHeld();
		TestFalse(TEXT("打开设置时按着的放掉"), Actions.IsOn(Sprint));
		TestTrue(TEXT("切换的留着"), Actions.IsOn(Crouch));
		Actions.Reset();
		TestFalse(TEXT("改了操作方式时都清掉"), Actions.IsOn(Crouch));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputSettingsTableTest, "CaseGinkgo.Control.Input.SettingsTable", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputSettingsTableTest::RunTest(const FString& Parameters)
{
	const FGinkgoInputSettings Defaults;
	TSet<FString> Seen;
	for (const FGinkgoInputSettingDef& Def : GetSettingDefs())
	{
		TestFalse(FString::Printf(TEXT("%s 名字不重复"), *Def.Id), Seen.Contains(Def.Id));
		Seen.Add(Def.Id);
		TestTrue(FString::Printf(TEXT("%s 名字的前缀是它的目录"), *Def.Id), Def.Id.StartsWith(Def.Directory == EGinkgoInputDirectory::Gamepad ? TEXT("gamepad.") : Def.Directory == EGinkgoInputDirectory::Accessibility ? TEXT("accessibility.") : TEXT("mouse.")));
		const double Value = Def.Get(Defaults);
		switch (Def.Kind)
		{
		case FGinkgoInputSettingDef::EKind::Number:
			TestTrue(FString::Printf(TEXT("%s 默认值在范围里"), *Def.Id), Value >= Def.Min && Value <= Def.Max);
			TestTrue(FString::Printf(TEXT("%s 一格比范围小"), *Def.Id), Def.Step > 0.0 && Def.Step < Def.Max - Def.Min);
			break;
		case FGinkgoInputSettingDef::EKind::Toggle:
			TestTrue(FString::Printf(TEXT("%s 是 0 或 1"), *Def.Id), Value == 0.0 || Value == 1.0);
			break;
		case FGinkgoInputSettingDef::EKind::Choice:
			TestTrue(FString::Printf(TEXT("%s 默认值在选项里"), *Def.Id), Def.Options.IsValidIndex(FMath::RoundToInt32(Value)));
			break;
		}
		// 写进去、读出来是同一个数（每一项的 Get、Set 接的是同一个字段）；别的项不动
		FGinkgoInputSettings Written;
		const double Other = Def.Kind == FGinkgoInputSettingDef::EKind::Number ? (Value == Def.Max ? Def.Min : Def.Max)
			: Def.Kind == FGinkgoInputSettingDef::EKind::Toggle ? 1.0 - Value : (Value == 0.0 ? 1.0 : 0.0);
		WriteSetting(Written, Def, Other);
		TestEqual(FString::Printf(TEXT("%s 写得进去"), *Def.Id), Def.Get(Written), static_cast<double>(static_cast<float>(Other)), 1e-6);
		TestEqual(FString::Printf(TEXT("%s 只改它自己"), *Def.Id), CountDifferences(Written, Defaults), 1);
	}

	// 外面来的值：夹到范围里、NaN 不收、选项超出不改
	const FGinkgoInputSettingDef* Inner = FindSettingDef(TEXT("gamepad.lookInner"));
	const FGinkgoInputSettingDef* Curve = FindSettingDef(TEXT("gamepad.curve"));
	if (TestNotNull(TEXT("有内死区"), Inner) && TestNotNull(TEXT("有曲线"), Curve))
	{
		FGinkgoInputSettings Settings;
		WriteSetting(Settings, *Inner, 9.0);
		TestEqual(TEXT("夹到上限"), Settings.GamepadLookInner, 0.5f, 1e-6f);
		WriteSetting(Settings, *Inner, -1.0);
		TestEqual(TEXT("夹到下限"), Settings.GamepadLookInner, 0.f, 1e-6f);
		WriteSetting(Settings, *Inner, std::numeric_limits<double>::quiet_NaN());
		TestEqual(TEXT("NaN 不收"), Settings.GamepadLookInner, 0.f, 1e-6f);
		WriteSetting(Settings, *Curve, 7.0);
		TestTrue(TEXT("选项超出不改"), Settings.GamepadCurve == Defaults.GamepadCurve);
	}
	TestNull(TEXT("没有的名字"), FindSettingDef(TEXT("gamepad.nothing")));

	// 外圈的下限比内死区的上限大：任何组合都不会除以零、不会反过来
	for (const TCHAR* Pair : {TEXT("gamepad.look"), TEXT("gamepad.move")})
	{
		const FGinkgoInputSettingDef* In = FindSettingDef(FString(Pair) + TEXT("Inner"));
		const FGinkgoInputSettingDef* Out = FindSettingDef(FString(Pair) + TEXT("Outer"));
		if (TestNotNull(TEXT("有内死区"), In) && TestNotNull(TEXT("有外圈"), Out))
		{
			TestTrue(TEXT("外圈总比内死区大"), Out->Min > In->Max);
		}
	}

	// 恢复默认只管这个目录
	{
		FGinkgoInputSettings Settings;
		Settings.GamepadYawSpeed = 300.f;
		Settings.MouseSensitivity = 3.f;
		const EGinkgoInputDirectory Pad = EGinkgoInputDirectory::Gamepad;
		TestEqual(TEXT("手柄这边不同 1 项"), CountDifferences(Settings, Defaults, &Pad), 1);
		CopyDirectory(Settings, Defaults, Pad);
		TestEqual(TEXT("手柄回到默认"), Settings.GamepadYawSpeed, Defaults.GamepadYawSpeed, 1e-6f);
		TestEqual(TEXT("鼠标不动"), Settings.MouseSensitivity, 3.f, 1e-6f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputSettingsJsonTest, "CaseGinkgo.Control.Input.SettingsJson", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputSettingsJsonTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoInputSettingsTests;
	const FGinkgoInputSettings Defaults;

	// 默认值：一项也不写
	const FString DefaultText = SettingsToJson(Defaults);
	TestTrue(TEXT("默认值不写"), DefaultText.Contains(TEXT("\"values\"")) && !DefaultText.Contains(TEXT("gamepad.")) && !DefaultText.Contains(TEXT("mouse.")));

	// 每一项都改成别的值，写出去再读回来逐项相同（float 的值写回去不差一位）
	FGinkgoInputSettings Changed;
	for (const FGinkgoInputSettingDef& Def : GetSettingDefs())
	{
		const double Value = Def.Get(Defaults);
		const double Other = Def.Kind == FGinkgoInputSettingDef::EKind::Number ? FMath::Lerp(Def.Min, Def.Max, Value == Def.Min ? 0.777 : 0.123)
			: Def.Kind == FGinkgoInputSettingDef::EKind::Toggle ? 1.0 - Value : (Value == 0.0 ? Def.Options.Num() - 1.0 : 0.0);
		WriteSetting(Changed, Def, Other);
	}
	const FString Text = SettingsToJson(Changed);
	FGinkgoInputSettings Read;
	TArray<FString> Issues;
	TestTrue(TEXT("读得懂"), SettingsFromJson(Text, Read, &Issues));
	TestEqual(TEXT("没有跳过的"), Issues.Num(), 0);
	TestEqual(TEXT("读回来逐项相同"), CountDifferences(Read, Changed), 0);
	TestEqual(TEXT("全都和默认值不同"), CountDifferences(Read, Defaults), GetSettingDefs().Num());

	// 好读的数：7.95 写成 7.95
	{
		FGinkgoInputSettings Settings;
		Settings.MouseSensitivity = 7.95f;
		TestTrue(TEXT("6 位够时写 6 位"), SettingsToJson(Settings).Contains(TEXT("7.95,")) || SettingsToJson(Settings).Contains(TEXT("7.95\n")) || SettingsToJson(Settings).Contains(TEXT("7.95\r")));
	}

	// 缺项是默认值；超出范围夹回来；类型不对、不认识的跳过；选项不认识跳过
	{
		const FString Partial = TEXT("{\"version\": 1, \"values\": {\"gamepad.yawSpeed\": 9999, \"gamepad.pitchSpeed\": \"快\", \"gamepad.invertPitch\": true, \"gamepad.curve\": \"power\", ")
			TEXT("\"mouse.sensitivity\": 3.5, \"future.setting\": 1}}");
		FGinkgoInputSettings Settings;
		TArray<FString> PartialIssues;
		TestTrue(TEXT("是 JSON 就读"), SettingsFromJson(Partial, Settings, &PartialIssues));
		TestEqual(TEXT("超出范围夹回来"), Settings.GamepadYawSpeed, 600.f, 1e-3f);
		TestEqual(TEXT("类型不对跳过，是默认值"), Settings.GamepadPitchSpeed, Defaults.GamepadPitchSpeed, 1e-3f);
		TestTrue(TEXT("开关"), Settings.bGamepadInvertPitch);
		TestTrue(TEXT("选择"), Settings.GamepadCurve == EGinkgoResponseCurve::Power);
		TestEqual(TEXT("数"), Settings.MouseSensitivity, 3.5f, 1e-5f);
		TestEqual(TEXT("缺的项是默认值"), Settings.MousePitchRatio, Defaults.MousePitchRatio, 1e-6f);
		TestEqual(TEXT("记下跳过了几项"), PartialIssues.Num(), 2);

		FGinkgoInputSettings Unknown;
		TArray<FString> UnknownIssues;
		SettingsFromJson(TEXT("{\"values\": {\"gamepad.curve\": \"什么\"}}"), Unknown, &UnknownIssues);
		TestTrue(TEXT("选项不认识，是默认值"), Unknown.GamepadCurve == Defaults.GamepadCurve);
		TestEqual(TEXT("选项不认识，记一条"), UnknownIssues.Num(), 1);
	}

	// 不是 JSON、没有 values
	{
		FGinkgoInputSettings Settings;
		Settings.GamepadYawSpeed = 1.f;
		TestFalse(TEXT("不是 JSON"), SettingsFromJson(TEXT("{坏了"), Settings));
		TestEqual(TEXT("读不懂时是默认值"), Settings.GamepadYawSpeed, Defaults.GamepadYawSpeed, 1e-6f);
		TestTrue(TEXT("没有 values 也算读懂"), SettingsFromJson(TEXT("{\"version\": 1}"), Settings));
	}

	// 文件：存、读、没有文件、坏文件留成 .corrupt、改了再存不留临时文件
	{
		const FString Path = TemporaryFile(TEXT("Settings.json"));
		IFileManager& Files = IFileManager::Get();
		Files.Delete(*Path);
		Files.Delete(*(Path + TEXT(".corrupt")));
		FGinkgoInputSettings Settings;
		Settings.GamepadYawSpeed = 1.f;
		TestTrue(TEXT("没有文件时是默认值、不算错"), LoadSettingsFile(Path, Settings));
		TestEqual(TEXT("没有文件时是默认值"), CountDifferences(Settings, Defaults), 0);

		FString Error;
		TestTrue(TEXT("存得进"), SaveSettingsFile(Path, Changed, Error));
		TestFalse(TEXT("不留临时文件"), Files.FileExists(*(Path + TEXT(".tmp"))));
		TestTrue(TEXT("读得回"), LoadSettingsFile(Path, Settings));
		TestEqual(TEXT("文件读回来逐项相同"), CountDifferences(Settings, Changed), 0);

		FFileHelper::SaveStringToFile(TEXT("不是 JSON"), *Path);
		TestFalse(TEXT("坏文件报错"), LoadSettingsFile(Path, Settings));
		TestEqual(TEXT("坏文件用默认值"), CountDifferences(Settings, Defaults), 0);
		TestTrue(TEXT("坏文件留着"), Files.FileExists(*(Path + TEXT(".corrupt"))));
		TestFalse(TEXT("原来的名字空出来"), Files.FileExists(*Path));
		Files.Delete(*(Path + TEXT(".corrupt")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputKeyBindingsTest, "CaseGinkgo.Control.Input.KeyBindings", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputKeyBindingsTest::RunTest(const FString& Parameters)
{
	constexpr EGinkgoInputDirectory Mouse = EGinkgoInputDirectory::KeyboardMouse;
	constexpr EGinkgoInputDirectory Pad = EGinkgoInputDirectory::Gamepad;
	const TArray<FGinkgoKeyBindingDef>& Defs = GetKeyBindingDefs();
	const FGinkgoInputSettings Defaults;

	// 表：下标算得对、名字不重复、前缀是目录、默认键绑得上、一个目录里默认键不撞、默认值不算差别
	TestEqual(TEXT("键盘十九个动作、手柄十五个动作，各两个键位"), Defs.Num(), 68);
	TestEqual(TEXT("每个键位一份"), Defaults.Keys.Num(), Defs.Num());
	TSet<FString> Ids;
	for (int32 Index = 0; Index < Defs.Num(); ++Index)
	{
		const FGinkgoKeyBindingDef& Def = Defs[Index];
		TestEqual(FString::Printf(TEXT("%s 的下标算得对"), *Def.Id), KeyBindingIndex(Def.Directory, Def.Action, Def.Slot), Index);
		TestEqual(FString::Printf(TEXT("%s 按名字找得到"), *Def.Id), FindKeyBinding(Def.Id), Index);
		TestFalse(FString::Printf(TEXT("%s 名字不重复"), *Def.Id), Ids.Contains(Def.Id));
		Ids.Add(Def.Id);
		TestTrue(FString::Printf(TEXT("%s 前缀是目录"), *Def.Id), Def.Id.StartsWith(Def.Directory == Pad ? TEXT("gamepad.") : TEXT("mouse.")));
		TestTrue(FString::Printf(TEXT("%s 的行名是它去掉键位"), *Def.Id), Def.Id == Def.RowId + (Def.Slot == EGinkgoKeySlot::Primary ? TEXT(".primary") : TEXT(".secondary")));
		TestTrue(FString::Printf(TEXT("%s 的默认键绑得上"), *Def.Id), !Def.DefaultKey.IsValid() || CheckKey(Def.Directory, Def.DefaultKey) == EGinkgoKeyCheck::Ok);
		TestEqual(FString::Printf(TEXT("%s 的默认键不撞"), *Def.Id), FindKeyConflict(Defaults, Index, Def.DefaultKey), INDEX_NONE);
		TestEqual(FString::Printf(TEXT("%s 只有蹲、冲刺、开镜有方式"), *Def.Id), Def.bHasMode, Def.Action == EGinkgoKeyAction::Crouch || Def.Action == EGinkgoKeyAction::Sprint || Def.Action == EGinkgoKeyAction::Aim);
	}
	TestEqual(TEXT("手柄没有方向键"), KeyBindingIndex(Pad, EGinkgoKeyAction::MoveForward, EGinkgoKeySlot::Primary), INDEX_NONE);

	// 做改键之前的键位都在
	const auto KeyOf = [&Defaults](const EGinkgoInputDirectory Directory, const EGinkgoKeyAction Action, const EGinkgoKeySlot Slot) -> const FGinkgoKeyBinding&
	{
		return Defaults.Keys[KeyBindingIndex(Directory, Action, Slot)];
	};
	TestTrue(TEXT("Q 取出闪光弹"), KeyOf(Mouse, EGinkgoKeyAction::Flashbang, EGinkgoKeySlot::Primary).Key == EKeys::Q);
	TestTrue(TEXT("Ctrl 按住蹲"), KeyOf(Mouse, EGinkgoKeyAction::Crouch, EGinkgoKeySlot::Primary).Key == EKeys::LeftControl && KeyOf(Mouse, EGinkgoKeyAction::Crouch, EGinkgoKeySlot::Primary).Mode == EGinkgoHoldMode::Hold);
	TestTrue(TEXT("C 切换蹲"), KeyOf(Mouse, EGinkgoKeyAction::Crouch, EGinkgoKeySlot::Secondary).Key == EKeys::C && KeyOf(Mouse, EGinkgoKeyAction::Crouch, EGinkgoKeySlot::Secondary).Mode == EGinkgoHoldMode::Toggle);
	TestTrue(TEXT("空格跳"), KeyOf(Mouse, EGinkgoKeyAction::Jump, EGinkgoKeySlot::Primary).Key == EKeys::SpaceBar);
	TestTrue(TEXT("滚轮下滚跳"), KeyOf(Mouse, EGinkgoKeyAction::Jump, EGinkgoKeySlot::Secondary).Key == EKeys::MouseScrollDown);
	TestTrue(TEXT("手柄 B 蹲"), KeyOf(Pad, EGinkgoKeyAction::Crouch, EGinkgoKeySlot::Primary).Key == EKeys::Gamepad_FaceButton_Right);
	TestTrue(TEXT("1 第一把"), KeyOf(Mouse, EGinkgoKeyAction::Slot1, EGinkgoKeySlot::Primary).Key == EKeys::One);
	TestTrue(TEXT("2 第二把"), KeyOf(Mouse, EGinkgoKeyAction::Slot2, EGinkgoKeySlot::Primary).Key == EKeys::Two);
	TestTrue(TEXT("3 收枪"), KeyOf(Mouse, EGinkgoKeyAction::Holster, EGinkgoKeySlot::Primary).Key == EKeys::Three);
	TestFalse(TEXT("键盘的切枪默认空着"), KeyOf(Mouse, EGinkgoKeyAction::SwitchWeapon, EGinkgoKeySlot::Primary).Key.IsValid());
	TestTrue(TEXT("鼠标左键开火"), KeyOf(Mouse, EGinkgoKeyAction::Fire, EGinkgoKeySlot::Primary).Key == EKeys::LeftMouseButton);
	TestTrue(TEXT("R 装填"), KeyOf(Mouse, EGinkgoKeyAction::Reload, EGinkgoKeySlot::Primary).Key == EKeys::R);
	TestTrue(TEXT("Shift 切换冲刺"), KeyOf(Mouse, EGinkgoKeyAction::Sprint, EGinkgoKeySlot::Primary).Key == EKeys::LeftShift && KeyOf(Mouse, EGinkgoKeyAction::Sprint, EGinkgoKeySlot::Primary).Mode == EGinkgoHoldMode::Toggle);
	TestTrue(TEXT("手柄左摇杆切换冲刺"), KeyOf(Pad, EGinkgoKeyAction::Sprint, EGinkgoKeySlot::Primary).Key == EKeys::Gamepad_LeftThumbstick && KeyOf(Pad, EGinkgoKeyAction::Sprint, EGinkgoKeySlot::Primary).Mode == EGinkgoHoldMode::Toggle);
	TestTrue(TEXT("F 主键拿手枪、4 辅助键拿手枪"), KeyOf(Mouse, EGinkgoKeyAction::Sidearm, EGinkgoKeySlot::Primary).Key == EKeys::F && KeyOf(Mouse, EGinkgoKeyAction::Sidearm, EGinkgoKeySlot::Secondary).Key == EKeys::Four);
	TestTrue(TEXT("手柄 RT 开火"), KeyOf(Pad, EGinkgoKeyAction::Fire, EGinkgoKeySlot::Primary).Key == EKeys::Gamepad_RightTrigger);
	TestTrue(TEXT("手柄 RB 装填"), KeyOf(Pad, EGinkgoKeyAction::Reload, EGinkgoKeySlot::Primary).Key == EKeys::Gamepad_RightShoulder);
	TestTrue(TEXT("手柄十字下收枪"), KeyOf(Pad, EGinkgoKeyAction::Holster, EGinkgoKeySlot::Primary).Key == EKeys::Gamepad_DPad_Down);
	TestTrue(TEXT("手柄 Y 切枪"), KeyOf(Pad, EGinkgoKeyAction::SwitchWeapon, EGinkgoKeySlot::Primary).Key == EKeys::Gamepad_FaceButton_Top);
	TestTrue(TEXT("G 丢枪"), KeyOf(Mouse, EGinkgoKeyAction::DropWeapon, EGinkgoKeySlot::Primary).Key == EKeys::G);
	TestTrue(TEXT("手柄十字右丢枪"), KeyOf(Pad, EGinkgoKeyAction::DropWeapon, EGinkgoKeySlot::Primary).Key == EKeys::Gamepad_DPad_Right);
	TestFalse(TEXT("手柄的第一把默认空着"), KeyOf(Pad, EGinkgoKeyAction::Slot1, EGinkgoKeySlot::Primary).Key.IsValid());
	TestFalse(TEXT("手柄的辅助键默认空着"), KeyOf(Pad, EGinkgoKeyAction::Crouch, EGinkgoKeySlot::Secondary).Key.IsValid());

	// 键位提示用的键（交互的提示圈）：按设备取，主键空着时用辅助键，都空着时无效
	TestTrue(TEXT("提示：键鼠的交互是 E"), FindBoundKey(Defaults, Mouse, EGinkgoKeyAction::Interact) == EKeys::E);
	TestTrue(TEXT("提示：手柄的交互是 X"), FindBoundKey(Defaults, Pad, EGinkgoKeyAction::Interact) == EKeys::Gamepad_FaceButton_Left);
	{
		FGinkgoInputSettings Rebound = Defaults;
		Rebound.Keys[KeyBindingIndex(Mouse, EGinkgoKeyAction::Interact, EGinkgoKeySlot::Primary)].Key = EKeys::F;
		TestTrue(TEXT("提示：改了键跟着改"), FindBoundKey(Rebound, Mouse, EGinkgoKeyAction::Interact) == EKeys::F);
		Rebound.Keys[KeyBindingIndex(Mouse, EGinkgoKeyAction::Interact, EGinkgoKeySlot::Primary)].Key = EKeys::Invalid;
		Rebound.Keys[KeyBindingIndex(Mouse, EGinkgoKeyAction::Interact, EGinkgoKeySlot::Secondary)].Key = EKeys::ThumbMouseButton;
		TestTrue(TEXT("提示：主键空着用辅助键"), FindBoundKey(Rebound, Mouse, EGinkgoKeyAction::Interact) == EKeys::ThumbMouseButton);
		Rebound.Keys[KeyBindingIndex(Mouse, EGinkgoKeyAction::Interact, EGinkgoKeySlot::Secondary)].Key = EKeys::Invalid;
		TestFalse(TEXT("提示：都空着时无效"), FindBoundKey(Rebound, Mouse, EGinkgoKeyAction::Interact).IsValid());
		TestFalse(TEXT("提示：手柄没有的动作（方向）无效"), FindBoundKey(Defaults, Pad, EGinkgoKeyAction::MoveForward).IsValid());
	}

	// 能绑什么
	TestTrue(TEXT("键盘的键"), CheckKey(Mouse, EKeys::J) == EGinkgoKeyCheck::Ok);
	TestTrue(TEXT("鼠标的侧键"), CheckKey(Mouse, EKeys::ThumbMouseButton) == EGinkgoKeyCheck::Ok);
	TestTrue(TEXT("滚轮"), CheckKey(Mouse, EKeys::MouseScrollUp) == EGinkgoKeyCheck::Ok);
	TestTrue(TEXT("手柄的键不能给键盘的键位"), CheckKey(Mouse, EKeys::Gamepad_FaceButton_Top) == EGinkgoKeyCheck::WrongDevice);
	TestTrue(TEXT("键盘的键不能给手柄的键位"), CheckKey(Pad, EKeys::J) == EGinkgoKeyCheck::WrongDevice);
	TestTrue(TEXT("鼠标键不能给手柄的键位"), CheckKey(Pad, EKeys::LeftMouseButton) == EGinkgoKeyCheck::WrongDevice);
	TestTrue(TEXT("Esc 留给设置界面"), CheckKey(Mouse, EKeys::Escape) == EGinkgoKeyCheck::Reserved);
	TestTrue(TEXT("F10 留给设置界面"), CheckKey(Mouse, EKeys::F10) == EGinkgoKeyCheck::Reserved);
	TestTrue(TEXT("Delete 留给清空"), CheckKey(Mouse, EKeys::Delete) == EGinkgoKeyCheck::Reserved);
	TestTrue(TEXT("菜单键留给设置界面"), CheckKey(Pad, EKeys::Gamepad_Special_Right) == EGinkgoKeyCheck::Reserved);
	TestTrue(TEXT("视图键留给清空"), CheckKey(Pad, EKeys::Gamepad_Special_Left) == EGinkgoKeyCheck::Reserved);
	TestTrue(TEXT("扳机"), CheckKey(Pad, EKeys::Gamepad_RightTrigger) == EGinkgoKeyCheck::Ok);
	TestTrue(TEXT("摇杆的方向不是按钮"), CheckKey(Pad, EKeys::Gamepad_LeftStick_Up) == EGinkgoKeyCheck::NotAButton);
	TestTrue(TEXT("摇杆的轴不是按钮"), CheckKey(Pad, EKeys::Gamepad_LeftX) == EGinkgoKeyCheck::NotAButton);
	TestTrue(TEXT("鼠标的轴不是按钮"), CheckKey(Mouse, EKeys::MouseX) == EGinkgoKeyCheck::NotAButton);
	TestTrue(TEXT("空的键不是按钮"), CheckKey(Mouse, EKeys::Invalid) == EGinkgoKeyCheck::NotAButton);

	// 互换：一个键不会绑两个动作；两种设备不撞
	const int32 Jump = KeyBindingIndex(Mouse, EGinkgoKeyAction::Jump, EGinkgoKeySlot::Primary);
	const int32 Crouch = KeyBindingIndex(Mouse, EGinkgoKeyAction::Crouch, EGinkgoKeySlot::Primary);
	const int32 Sprint2 = KeyBindingIndex(Mouse, EGinkgoKeyAction::Sprint, EGinkgoKeySlot::Secondary);
	const int32 PadJump = KeyBindingIndex(Pad, EGinkgoKeyAction::Jump, EGinkgoKeySlot::Primary);
	{
		FGinkgoInputSettings Settings;
		TestEqual(TEXT("Ctrl 撞蹲的主键"), FindKeyConflict(Settings, Jump, EKeys::LeftControl), Crouch);
		TestEqual(TEXT("换成自己的键不算撞"), FindKeyConflict(Settings, Crouch, EKeys::LeftControl), INDEX_NONE);
		TestEqual(TEXT("空的键不撞"), FindKeyConflict(Settings, Sprint2, EKeys::Invalid), INDEX_NONE);
		AssignKey(Settings, Jump, EKeys::LeftControl);
		TestTrue(TEXT("跳跃换成 Ctrl"), Settings.Keys[Jump].Key == EKeys::LeftControl);
		TestTrue(TEXT("蹲换成空格"), Settings.Keys[Crouch].Key == EKeys::SpaceBar);
		TestEqual(TEXT("互换是两项差别"), CountDifferences(Settings, Defaults), 2);
		TestTrue(TEXT("方式留在原来的键位上"), Settings.Keys[Crouch].Mode == EGinkgoHoldMode::Hold);
		AssignKey(Settings, Sprint2, EKeys::LeftControl);
		TestTrue(TEXT("空的键位拿走别人的键：别人空出来"), !Settings.Keys[Jump].Key.IsValid() && Settings.Keys[Sprint2].Key == EKeys::LeftControl);
		TestEqual(TEXT("手柄的键位只和手柄比"), FindKeyConflict(Settings, PadJump, EKeys::SpaceBar), INDEX_NONE);
	}

	// 差别：键与方式各算一项；恢复默认只管这个目录
	{
		FGinkgoInputSettings Settings;
		const EGinkgoInputDirectory Directory = Pad;
		Settings.Keys[Crouch].Key = EKeys::J;
		Settings.Keys[Crouch].Mode = EGinkgoHoldMode::Toggle;
		Settings.Keys[PadJump].Key = EKeys::Gamepad_FaceButton_Top;
		TestEqual(TEXT("键与方式各算一项"), CountDifferences(Settings, Defaults), 3);
		TestEqual(TEXT("只比手柄"), CountDifferences(Settings, Defaults, &Directory), 1);
		CopyDirectory(Settings, Defaults, Pad);
		TestTrue(TEXT("手柄回到默认"), Settings.Keys[PadJump].Key == EKeys::Gamepad_FaceButton_Bottom);
		TestTrue(TEXT("键盘不动"), Settings.Keys[Crouch].Key == EKeys::J);
		// 没有方式的动作，方式不算差别
		Settings = FGinkgoInputSettings();
		Settings.Keys[Jump].Mode = EGinkgoHoldMode::Toggle;
		TestEqual(TEXT("跳跃没有方式"), CountDifferences(Settings, Defaults), 0);
	}

	// 文件：默认值不写；改的写得回来；清空写得回来
	{
		TestFalse(TEXT("默认键位不写"), SettingsToJson(Defaults).Contains(TEXT("\"keys\"")));
		FGinkgoInputSettings Settings;
		Settings.Keys[Crouch].Key = EKeys::J;
		Settings.Keys[Crouch].Mode = EGinkgoHoldMode::Toggle;
		Settings.Keys[Jump].Key = EKeys::Invalid;
		Settings.Keys[PadJump].Key = EKeys::Gamepad_DPad_Up;
		Settings.Keys[KeyBindingIndex(Pad, EGinkgoKeyAction::Aim, EGinkgoKeySlot::Primary)].Mode = EGinkgoHoldMode::Toggle;
		FGinkgoInputSettings Read;
		TArray<FString> Issues;
		TestTrue(TEXT("读得懂"), SettingsFromJson(SettingsToJson(Settings), Read, &Issues));
		TestEqual(TEXT("没有跳过的"), Issues.Num(), 0);
		TestEqual(TEXT("读回来逐项相同"), CountDifferences(Read, Settings), 0);
		TestFalse(TEXT("空的键读回来是空的"), Read.Keys[Jump].Key.IsValid());
	}

	// 手改的文件：不认识的、绑不了的跳过；撞了的后面的空出来
	{
		const FString Text = TEXT("{\"values\": {}, \"keys\": {")
			TEXT("\"mouse.jump.primary\": {\"key\": \"J\"}, ")
			TEXT("\"mouse.crouch.primary\": {\"key\": \"J\", \"mode\": \"toggle\"}, ")
			TEXT("\"mouse.sprint.primary\": {\"key\": \"NoSuchKey\"}, ")
			TEXT("\"mouse.aim.primary\": {\"key\": \"Gamepad_FaceButton_Top\"}, ")
			TEXT("\"gamepad.jump.primary\": {\"key\": \"Gamepad_Special_Right\"}, ")
			TEXT("\"mouse.interact.primary\": {\"key\": \"Escape\"}, ")
			TEXT("\"mouse.jump.secondary\": {\"mode\": \"toggle\"}, ")
			TEXT("\"gamepad.crouch.primary\": {\"mode\": \"什么\"}, ")
			TEXT("\"mouse.fly.primary\": {\"key\": \"F\"}, ")
			TEXT("\"mouse.moveLeft.primary\": 3}}");
		FGinkgoInputSettings Read;
		TArray<FString> Issues;
		TestTrue(TEXT("是 JSON 就读"), SettingsFromJson(Text, Read, &Issues));
		TestTrue(TEXT("读得懂的留下"), Read.Keys[Jump].Key == EKeys::J);
		TestFalse(TEXT("撞了的后面的空出来"), Read.Keys[Crouch].Key.IsValid());
		TestTrue(TEXT("方式还是读了"), Read.Keys[Crouch].Mode == EGinkgoHoldMode::Toggle);
		TestTrue(TEXT("不认识的键是默认值"), Read.Keys[KeyBindingIndex(Mouse, EGinkgoKeyAction::Sprint, EGinkgoKeySlot::Primary)].Key == EKeys::LeftShift);
		TestTrue(TEXT("另一种设备的键是默认值"), Read.Keys[KeyBindingIndex(Mouse, EGinkgoKeyAction::Aim, EGinkgoKeySlot::Primary)].Key == EKeys::RightMouseButton);
		TestTrue(TEXT("留给界面的键是默认值"), Read.Keys[PadJump].Key == EKeys::Gamepad_FaceButton_Bottom);
		TestTrue(TEXT("留给界面的键是默认值（键盘）"), Read.Keys[KeyBindingIndex(Mouse, EGinkgoKeyAction::Interact, EGinkgoKeySlot::Primary)].Key == EKeys::E);
		// 跳过的：NoSuchKey、手柄的键、菜单键、Esc、跳跃没有方式、读不懂的方式、不认识的键位、不是对象的、撞了的
		TestEqual(TEXT("记下跳过了几项"), Issues.Num(), 9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputSettingsCatalogTest, "CaseGinkgo.Control.Input.Catalog", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoInputSettingsCatalogTest::RunTest(const FString& Parameters)
{
	// 打包的游戏里没有元数据，名字、单位与说明都在页面的 catalog.js：每一项都要写了
	FString Catalog;
	if (!TestTrue(TEXT("读得到 catalog.js"), FFileHelper::LoadFileToString(Catalog, *(FPaths::ProjectContentDir() / TEXT("UI/Settings/catalog.js")))))
	{
		return false;
	}
	for (const FGinkgoInputSettingDef& Def : GetSettingDefs())
	{
		TestTrue(FString::Printf(TEXT("catalog.js 写了 %s"), *Def.Id), Catalog.Contains(FString::Printf(TEXT("'%s'"), *Def.Id)));
	}
	for (const FGinkgoKeyBindingDef& Def : GetKeyBindingDefs())
	{
		TestTrue(FString::Printf(TEXT("catalog.js 写了 %s"), *Def.RowId), Catalog.Contains(FString::Printf(TEXT("'%s'"), *Def.RowId)));
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAccessibilitySettingsTest, "CaseGinkgo.Control.Input.Accessibility", GinkgoInputSettingsTests::TestFlags)
bool FGinkgoAccessibilitySettingsTest::RunTest(const FString& Parameters)
{
	const FGinkgoInputSettings Defaults;
	FGinkgoInputSettings Changed;
	const FGinkgoInputSettingDef* Def = FindSettingDef(TEXT("accessibility.adsAutoShoulder"));
	const auto* PeekDef = FindSettingDef(TEXT("accessibility.autoPeek"));
	if (!TestNotNull(TEXT("辅助功能有自动探头开关"), PeekDef)) { return false; }
	TestEqual(TEXT("自动探头属于辅助功能目录"), PeekDef->Directory, EGinkgoInputDirectory::Accessibility);
	TestTrue(TEXT("自动探头默认开启"), Defaults.bAutoPeek);
	WriteSetting(Changed, *PeekDef, 0);
	if (!TestNotNull(TEXT("辅助功能有自动换肩开关"), Def)) { return false; }
	TestEqual(TEXT("开关属于独立目录"), Def->Directory, EGinkgoInputDirectory::Accessibility);
	TestTrue(TEXT("默认启用自动换肩"), Defaults.bAdsAutoShoulder);
	WriteSetting(Changed, *Def, 0);
	const auto* RestoreDef = FindSettingDef(TEXT("accessibility.restoreShoulderOnAdsExit"));
	if (!TestNotNull(TEXT("辅助功能有退出复原开关"), RestoreDef)) { return false; }
	TestTrue(TEXT("退出 ADS 复原肩位默认开启"), Defaults.bRestoreShoulderOnAdsExit);
	TestEqual(TEXT("字幕默认字号 20"), Defaults.SubtitleSize, 20.f, 1e-4f);
	TestEqual(TEXT("字幕默认衬底不透明度 0.55"), Defaults.SubtitleBackdrop, 0.55f, 1e-4f);
	WriteSetting(Changed, *RestoreDef, 0);
	const int32 Binding = FindKeyBinding(TEXT("mouse.switchShoulder.primary"));
	if (!TestTrue(TEXT("V 有可改键定义"), Changed.Keys.IsValidIndex(Binding))) { return false; }
	TestEqual(TEXT("默认按 V 换肩"), Changed.Keys[Binding].Key, EKeys::V);
	Changed.Keys[Binding].Key = EKeys::B;
	Changed.MouseSensitivity = 3;
	FGinkgoInputSettings Loaded;
	TestTrue(TEXT("新增设置可读写"), SettingsFromJson(SettingsToJson(Changed), Loaded));
	TestEqual(TEXT("开关与改键都完整保留"), CountDifferences(Changed, Loaded), 0);
	TestFalse(TEXT("自动探头的关闭状态能存盘读回"), Loaded.bAutoPeek);
	TestFalse(TEXT("退出复原的关闭状态能存盘读回"), Loaded.bRestoreShoulderOnAdsExit);
	CopyDirectory(Loaded, Defaults, EGinkgoInputDirectory::Accessibility);
	TestTrue(TEXT("恢复辅助功能默认值重新开启探头"), Loaded.bAutoPeek);
	TestTrue(TEXT("只恢复辅助功能开关"), Loaded.bAdsAutoShoulder);
	TestTrue(TEXT("同目录的复原开关也恢复默认开启"), Loaded.bRestoreShoulderOnAdsExit);
	TestEqual(TEXT("不改鼠标灵敏度"), Loaded.MouseSensitivity, 3.f);
	TestEqual(TEXT("不改手动换肩键"), Loaded.Keys[Binding].Key, EKeys::B);
	TestEqual(TEXT("辅助目录没有键位"), KeyBindingIndex(EGinkgoInputDirectory::Accessibility, EGinkgoKeyAction::SwitchShoulder, EGinkgoKeySlot::Primary), INDEX_NONE);
	TestTrue(TEXT("旧设置可读取"), SettingsFromJson(TEXT("{\"version\":1,\"values\":{\"mouse.sensitivity\":3}}"), Loaded));
	TestTrue(TEXT("旧设置缺少新字段时使用默认值"), Loaded.bAdsAutoShoulder);
	TestTrue(TEXT("旧设置缺少探头字段时默认启用"), Loaded.bAutoPeek);
	TestTrue(TEXT("旧设置缺少退出复原字段时沿用新默认开启"), Loaded.bRestoreShoulderOnAdsExit);
	TestEqual(TEXT("旧文件仍得到 V 默认键"), Loaded.Keys[Binding].Key, EKeys::V);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

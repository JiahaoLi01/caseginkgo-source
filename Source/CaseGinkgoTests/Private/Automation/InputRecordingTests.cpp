#include "Utilities/Debug/InputReplay/GinkgoInputRecording.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FGinkgoInputSample MakeSample(const double Time, const double Yaw, const FVector2D Move, const bool bJump = false, const bool bCrouch = false)
	{
		FGinkgoInputSample Sample;
		Sample.Time = Time;
		Sample.ControlRotation = FRotator(0.0, Yaw, 0.0);
		Sample.Move = Move;
		Sample.bJumpPressed = bJump;
		Sample.bCrouchHeld = bCrouch;
		return Sample;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputRecordingEvaluateTest, "CaseGinkgo.Control.InputRecording.Evaluate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInputRecordingEvaluateTest::RunTest(const FString& Parameters)
{
	FGinkgoInputRecording Recording;
	Recording.Samples.Add(MakeSample(0.0, 350.0, FVector2D(0.0, 1.0)));
	Recording.Samples.Add(MakeSample(0.1, 10.0, FVector2D(1.0, 0.0), false, true));

	// 视角按最短角度插值：350° 到 10° 的中点是 0°，而不是 180°
	const FGinkgoInputSample Middle = Recording.Evaluate(0.05);
	TestEqual(TEXT("视角跨过 0° 时走最短的一侧"), FRotator::NormalizeAxis(Middle.ControlRotation.Yaw), 0.0, 1e-6);
	TestTrue(TEXT("移动输入取前一个采样"), Middle.Move.Equals(FVector2D(0.0, 1.0)));
	TestFalse(TEXT("按住类按键取前一个采样"), Middle.bCrouchHeld);

	const FGinkgoInputSample End = Recording.Evaluate(0.1);
	TestTrue(TEXT("到达采样时刻时取这个采样"), End.Move.Equals(FVector2D(1.0, 0.0)));
	TestTrue(TEXT("到达采样时刻时按键也是这个采样的"), End.bCrouchHeld);

	TestTrue(TEXT("早于第一个采样时取第一个"), Recording.Evaluate(-1.0).Move.Equals(FVector2D(0.0, 1.0)));
	TestTrue(TEXT("晚于最后一个采样时取最后一个"), Recording.Evaluate(5.0).Move.Equals(FVector2D(1.0, 0.0)));
	TestEqual(TEXT("录制时长是最后一个采样的时刻"), Recording.GetDuration(), 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputRecordingJumpTest, "CaseGinkgo.Control.InputRecording.JumpPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInputRecordingJumpTest::RunTest(const FString& Parameters)
{
	FGinkgoInputRecording Recording;
	Recording.Samples.Add(MakeSample(0.0, 0.0, FVector2D::ZeroVector, true));
	Recording.Samples.Add(MakeSample(0.05, 0.0, FVector2D::ZeroVector));
	Recording.Samples.Add(MakeSample(0.1, 0.0, FVector2D::ZeroVector, true));

	// 区间是左开右闭：每一帧只认上一帧之后、这一帧为止按下的跳跃，不会重复触发也不会漏掉
	TestTrue(TEXT("开头的按下落在 (-1, 0]"), Recording.HasJumpPressBetween(-1.0, 0.0));
	TestFalse(TEXT("(0, 0.05] 没有按下"), Recording.HasJumpPressBetween(0.0, 0.05));
	TestTrue(TEXT("(0.05, 0.1] 有按下"), Recording.HasJumpPressBetween(0.05, 0.1));
	TestFalse(TEXT("(0.1, 1] 没有按下"), Recording.HasJumpPressBetween(0.1, 1.0));
	TestTrue(TEXT("低帧率下一帧跨过多个采样也不漏"), Recording.HasJumpPressBetween(0.01, 0.2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputRecordingInteractTest, "CaseGinkgo.Control.InputRecording.InteractPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInputRecordingInteractTest::RunTest(const FString& Parameters)
{
	FGinkgoInputRecording Recording;
	Recording.Samples.Add(MakeSample(0.0, 0.0, FVector2D::ZeroVector));
	Recording.Samples.Add(MakeSample(0.05, 0.0, FVector2D::ZeroVector));
	Recording.Samples.Add(MakeSample(0.1, 0.0, FVector2D::ZeroVector));
	Recording.Samples[1].bInteractPressed = true;

	// 同跳跃：区间左开右闭，每一帧只认上一帧之后、这一帧为止的按下
	TestFalse(TEXT("(-1, 0] 没有按下"), Recording.HasInteractPressBetween(-1.0, 0.0));
	TestTrue(TEXT("(0, 0.05] 有按下"), Recording.HasInteractPressBetween(0.0, 0.05));
	TestFalse(TEXT("(0.05, 1] 没有按下"), Recording.HasInteractPressBetween(0.05, 1.0));
	TestTrue(TEXT("低帧率下一帧跨过多个采样也不漏"), Recording.HasInteractPressBetween(0.01, 0.2));
	TestFalse(TEXT("取值不带按下沿"), Recording.Evaluate(0.05).bInteractPressed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputRecordingWeaponKeysTest, "CaseGinkgo.Control.InputRecording.WeaponKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInputRecordingWeaponKeysTest::RunTest(const FString& Parameters)
{
	FGinkgoInputRecording Recording;
	Recording.Samples.Add(MakeSample(0.0, 0.0, FVector2D::ZeroVector));
	Recording.Samples.Add(MakeSample(0.05, 0.0, FVector2D::ZeroVector));
	Recording.Samples.Add(MakeSample(0.1, 0.0, FVector2D::ZeroVector));
	Recording.Samples[1].WeaponKeys = EGinkgoWeaponKey::Holster;
	Recording.Samples[2].WeaponKeys = EGinkgoWeaponKey::Slot2;

	// 同跳跃：区间左开右闭，每一帧只认上一帧之后、这一帧为止的按下
	TestEqual(TEXT("(-1, 0] 没有按下"), Recording.GetWeaponKeysBetween(-1.0, 0.0), EGinkgoWeaponKey::None);
	TestEqual(TEXT("(0, 0.05] 是收枪"), Recording.GetWeaponKeysBetween(0.0, 0.05), EGinkgoWeaponKey::Holster);
	TestEqual(TEXT("低帧率下一帧跨过多个采样，两个键都不漏"), Recording.GetWeaponKeysBetween(0.01, 0.2), EGinkgoWeaponKey::Holster | EGinkgoWeaponKey::Slot2);
	TestEqual(TEXT("取值不带按下沿"), Recording.Evaluate(0.05).WeaponKeys, EGinkgoWeaponKey::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputRecordingDirectionPressTest, "CaseGinkgo.Control.InputRecording.DirectionPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInputRecordingDirectionPressTest::RunTest(const FString& Parameters)
{
	FGinkgoInputRecording Recording;
	Recording.Samples.Add(MakeSample(0.0, 0.0, FVector2D(0.0, 1.0)));
	Recording.Samples.Add(MakeSample(0.05, 0.0, FVector2D(1.0, 1.0)));
	Recording.Samples.Add(MakeSample(0.1, 0.0, FVector2D(-1.0, 0.0)));
	Recording.Samples[1].DirectionPresses = 1;
	Recording.Samples[2].DirectionPresses = 2;

	// 同跳跃按下沿：区间左开右闭，低帧率下一帧跨过几个采样时按下次数加起来
	TestEqual(TEXT("(0, 0.05] 按了 1 次"), int32(Recording.CountDirectionPressesBetween(0.0, 0.05)), 1);
	TestEqual(TEXT("(0.05, 0.1] 按了 2 次"), int32(Recording.CountDirectionPressesBetween(0.05, 0.1)), 2);
	TestEqual(TEXT("一帧跨过两个采样时加起来"), int32(Recording.CountDirectionPressesBetween(0.0, 0.2)), 3);
	TestEqual(TEXT("(0.1, 1] 没有按下"), int32(Recording.CountDirectionPressesBetween(0.1, 1.0)), 0);
	TestEqual(TEXT("Evaluate 不带按下次数"), int32(Recording.Evaluate(0.05).DirectionPresses), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoInputRecordingCsvTest, "CaseGinkgo.Control.InputRecording.Csv",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoInputRecordingCsvTest::RunTest(const FString& Parameters)
{
	FGinkgoInputRecording Recording;
	Recording.Start.bRelative = true;
	Recording.Start.Location = FVector(0.0, 0.0, 2000.0);
	Recording.Start.Velocity = FVector(476.25, 0.0, 0.0);
	Recording.Start.ControlRotation = FRotator(-10.0, 90.0, 0.0);
	Recording.Start.MovementMode = MOVE_Falling;
	Recording.Samples.Add(MakeSample(0.0, 0.0, FVector2D(1.0, 0.0), true));
	Recording.Samples.Add(MakeSample(0.016667, 4.0, FVector2D(0.5, -0.5), false, true));
	Recording.Samples[1].DirectionPresses = 2;
	// 按住 D 时按下 A：移动输入左右抵消，方向键的朝向是后按的 A
	Recording.Samples[1].DirectionKeys = FIntPoint(-1, 1);
	Recording.Samples[1].bInteractPressed = true;
	Recording.Samples[1].bAimHeld = true;
	Recording.Samples[1].WeaponKeys = EGinkgoWeaponKey::Holster | EGinkgoWeaponKey::Switch;
	Recording.Samples[1].bFireHeld = true;
	// v8：装填键按着、弹种轮盘的鼠标（一帧动的量）与摇杆
	Recording.Samples[0].WheelMouse = FVector2D(1.5, -2.0);
	Recording.Samples[1].bReloadHeld = true;
	Recording.Samples[1].WheelMouse = FVector2D(0.25, -3.5);
	Recording.Samples[1].WheelStick = FVector2D(0.0, -0.8);

	FGinkgoInputRecording Loaded;
	FString Error;
	TestTrue(TEXT("写出的 CSV 能读回"), FGinkgoInputRecording::FromCsv(Recording.ToCsv(), Loaded, Error));
	TestTrue(TEXT("开始状态是相对的"), Loaded.Start.bRelative);
	TestEqual(TEXT("开始位置"), Loaded.Start.Location, Recording.Start.Location);
	TestEqual(TEXT("开始速度"), Loaded.Start.Velocity, Recording.Start.Velocity);
	TestEqual(TEXT("开始视角"), Loaded.Start.ControlRotation, Recording.Start.ControlRotation);
	TestEqual(TEXT("开始移动模式"), int32(Loaded.Start.MovementMode), int32(MOVE_Falling));
	if (TestEqual(TEXT("采样数"), Loaded.Samples.Num(), 2))
	{
		TestEqual(TEXT("时刻"), Loaded.Samples[1].Time, 0.016667, 1e-6);
		TestEqual(TEXT("视角"), Loaded.Samples[1].ControlRotation.Yaw, 4.0, 1e-4);
		TestTrue(TEXT("移动输入"), Loaded.Samples[1].Move.Equals(FVector2D(0.5, -0.5)));
		TestTrue(TEXT("跳跃按下沿"), Loaded.Samples[0].bJumpPressed);
		TestTrue(TEXT("蹲"), Loaded.Samples[1].bCrouchHeld);
		TestEqual(TEXT("方向键按下"), int32(Loaded.Samples[1].DirectionPresses), 2);
		TestEqual(TEXT("方向键的朝向"), Loaded.Samples[1].DirectionKeys, FIntPoint(-1, 1));
		TestEqual(TEXT("朝向同移动输入一样取前一个采样"), Loaded.Evaluate(0.02).DirectionKeys, FIntPoint(-1, 1));
		TestFalse(TEXT("第一个采样没按交互键"), Loaded.Samples[0].bInteractPressed);
		TestTrue(TEXT("交互键按下沿"), Loaded.Samples[1].bInteractPressed);
		TestFalse(TEXT("第一个采样没按开镜"), Loaded.Samples[0].bAimHeld);
		TestTrue(TEXT("开镜按着"), Loaded.Samples[1].bAimHeld);
		TestTrue(TEXT("开镜同蹲、冲刺一样取前一个采样"), Loaded.Evaluate(0.02).bAimHeld);
		TestEqual(TEXT("第一个采样没按换枪的键"), Loaded.Samples[0].WeaponKeys, EGinkgoWeaponKey::None);
		TestEqual(TEXT("换枪的键"), Loaded.Samples[1].WeaponKeys, EGinkgoWeaponKey::Holster | EGinkgoWeaponKey::Switch);
		TestFalse(TEXT("第一个采样没按着开火键"), Loaded.Samples[0].bFireHeld);
		TestTrue(TEXT("开火键按着"), Loaded.Samples[1].bFireHeld);
		TestTrue(TEXT("开火键按着同开镜一样取前一个采样"), Loaded.Evaluate(0.02).bFireHeld);
		TestTrue(TEXT("装填键按着同开镜一样取前一个采样"), !Loaded.Samples[0].bReloadHeld && Loaded.Evaluate(0.02).bReloadHeld);
		TestTrue(TEXT("轮盘的摇杆取前一个采样"), Loaded.Evaluate(0.02).WheelStick.Equals(FVector2D(0.0, -0.8), 1e-4));
		TestTrue(TEXT("Evaluate 不带轮盘的鼠标（按区间加）"), Loaded.Evaluate(0.02).WheelMouse.IsZero());
		TestTrue(TEXT("轮盘的鼠标：(−1, 1] 两帧加起来"), Loaded.GetWheelMouseBetween(-1.0, 1.0).Equals(FVector2D(1.75, -5.5), 1e-4));
		TestTrue(TEXT("轮盘的鼠标：(0, 1] 只有第二帧"), Loaded.GetWheelMouseBetween(0.0, 1.0).Equals(FVector2D(0.25, -3.5), 1e-4));
	}

	// v7（没有装填键按着、轮盘那五列）：读出来没按着，旧路线的装填同一帧按下松开，就是短按
	const FString V7 = TEXT("# CaseGinkgo input recording v7\n# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim,weapon_keys,fire_held\n0,0,0,0,1,0,0,0,0,0,1,1,1,32,1\n");
	if (TestTrue(TEXT("v7 的录制能读"), FGinkgoInputRecording::FromCsv(V7, Loaded, Error)) && TestEqual(TEXT("v7 读出一个采样"), Loaded.Samples.Num(), 1))
	{
		TestEqual(TEXT("v7 的装填键按下沿"), int32(Loaded.Samples[0].WeaponKeys), 32);
		TestTrue(TEXT("v7 的开火键按着"), Loaded.Samples[0].bFireHeld);
		TestFalse(TEXT("v7 没有装填键按着"), Loaded.Samples[0].bReloadHeld);
		TestTrue(TEXT("v7 没有轮盘的鼠标与摇杆"), Loaded.Samples[0].WheelMouse.IsZero() && Loaded.Samples[0].WheelStick.IsZero());
	}

	// v6（没有开火键按着那一列）：读出来没按着，连发的枪回放旧录制也只响按下沿那一发
	const FString V6 = TEXT("# CaseGinkgo input recording v6\n# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim,weapon_keys\n0,0,0,0,1,0,0,0,0,0,1,1,1,16\n");
	if (TestTrue(TEXT("v6 的录制能读"), FGinkgoInputRecording::FromCsv(V6, Loaded, Error)) && TestEqual(TEXT("v6 读出一个采样"), Loaded.Samples.Num(), 1))
	{
		TestEqual(TEXT("v6 的换枪键"), int32(Loaded.Samples[0].WeaponKeys), 16);
		TestFalse(TEXT("v6 没有开火键按着"), Loaded.Samples[0].bFireHeld);
	}

	// v5（没有换枪的键那一列）：读出来没按过
	const FString V5 = TEXT("# CaseGinkgo input recording v5\n# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim\n0,0,0,0,1,0,0,0,0,0,1,1,1\n");
	if (TestTrue(TEXT("v5 的录制能读"), FGinkgoInputRecording::FromCsv(V5, Loaded, Error)) && TestEqual(TEXT("v5 读出一个采样"), Loaded.Samples.Num(), 1))
	{
		TestTrue(TEXT("v5 的开镜键"), Loaded.Samples[0].bAimHeld);
		TestEqual(TEXT("v5 没有换枪的键"), Loaded.Samples[0].WeaponKeys, EGinkgoWeaponKey::None);
	}

	// v4（没有开镜键那一列）：读出来没按过开镜
	const FString V4 = TEXT("# CaseGinkgo input recording v4\n# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact\n0,0,0,0,1,0,0,0,0,0,1,1\n");
	if (TestTrue(TEXT("v4 的录制能读"), FGinkgoInputRecording::FromCsv(V4, Loaded, Error)) && TestEqual(TEXT("v4 读出一个采样"), Loaded.Samples.Num(), 1))
	{
		TestTrue(TEXT("v4 的交互键"), Loaded.Samples[0].bInteractPressed);
		TestFalse(TEXT("v4 没有开镜键"), Loaded.Samples[0].bAimHeld);
	}

	// v3（没有交互键那一列）：读出来没按过交互键
	const FString V3 = TEXT("# CaseGinkgo input recording v3\n# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y\n0,0,0,0,1,0,0,0,1,0,1\n");
	if (TestTrue(TEXT("v3 的录制能读"), FGinkgoInputRecording::FromCsv(V3, Loaded, Error)) && TestEqual(TEXT("v3 读出一个采样"), Loaded.Samples.Num(), 1))
	{
		TestEqual(TEXT("v3 的方向键朝向"), Loaded.Samples[0].DirectionKeys, FIntPoint(0, 1));
		TestFalse(TEXT("v3 没有交互键"), Loaded.Samples[0].bInteractPressed);
	}

	// v2（没有方向键朝向那两列）：朝向按移动输入每个分量的符号取
	const FString V2 = TEXT("# CaseGinkgo input recording v2\n# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press\n0,0,0,0.5,-1,0,0,1,1\n");
	if (TestTrue(TEXT("v2 的录制能读"), FGinkgoInputRecording::FromCsv(V2, Loaded, Error)) && TestEqual(TEXT("v2 读出一个采样"), Loaded.Samples.Num(), 1))
	{
		TestEqual(TEXT("v2 的方向键按下"), int32(Loaded.Samples[0].DirectionPresses), 1);
		TestEqual(TEXT("v2 的朝向按移动输入的符号取"), Loaded.Samples[0].DirectionKeys, FIntPoint(1, -1));
	}

	// v1（没有方向键按下那一列）照样能读，读出来没有按下；测量脚本生成的路线大多是 v1
	const FString V1 = TEXT("# CaseGinkgo input recording v1\n# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint\n0,0,0,0,1,1,0,1\n");
	if (TestTrue(TEXT("v1 的录制能读"), FGinkgoInputRecording::FromCsv(V1, Loaded, Error)) && TestEqual(TEXT("v1 读出一个采样"), Loaded.Samples.Num(), 1))
	{
		TestTrue(TEXT("v1 的冲刺"), Loaded.Samples[0].bSprintHeld);
		TestEqual(TEXT("v1 没有方向键按下"), int32(Loaded.Samples[0].DirectionPresses), 0);
	}
	const FString V1Mixed = TEXT("# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint\n0,0,0,0,1,1,0,1,1\n");
	TestFalse(TEXT("v1 的表头配 v2 的行时拒绝"), FGinkgoInputRecording::FromCsv(V1Mixed, Loaded, Error));

	const FString BadColumns = TEXT("# CaseGinkgo input recording v1\n# start absolute mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint\n0,0,0,0\n");
	TestFalse(TEXT("列数不对时拒绝"), FGinkgoInputRecording::FromCsv(BadColumns, Loaded, Error));
	TestTrue(TEXT("错误信息带行号"), Error.Contains(TEXT("第 4 行")));

	const FString NoStart = TEXT("time,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y\n0,0,0,0,0,0,0,0,0,0,0\n");
	TestFalse(TEXT("缺少开始状态时拒绝"), FGinkgoInputRecording::FromCsv(NoStart, Loaded, Error));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoShoulderRecordingTest, "CaseGinkgo.Control.Recording.Shoulder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoShoulderRecordingTest::RunTest(const FString& Parameters)
{
	FGinkgoInputRecording Recording;
	for (double Time : {0.0, 0.1, 0.2})
	{
		FGinkgoInputSample Sample;
		Sample.Time = Time;
		Sample.bSwitchShoulderPressed = Time > 0;
		Recording.Samples.Add(Sample);
	}
	TestFalse(TEXT("插值不重复换肩按下沿"), Recording.Evaluate(0.15).bSwitchShoulderPressed);
	TestTrue(TEXT("跨过一次按下沿切换"), Recording.ShouldSwitchShoulderBetween(0, 0.15));
	TestFalse(TEXT("跨过两次按下沿抵消"), Recording.ShouldSwitchShoulderBetween(0, 0.2));
	TestFalse(TEXT("左端点不重复消费"), Recording.ShouldSwitchShoulderBetween(0.1, 0.15));
	FGinkgoInputRecording Loaded;
	FString Error;
	TestTrue(TEXT("v9 换肩输入可读写"), FGinkgoInputRecording::FromCsv(Recording.ToCsv(), Loaded, Error));
	TestTrue(TEXT("CSV 保留换肩按下沿"), Loaded.ShouldSwitchShoulderBetween(0, 0.15));
	const FString V8 = TEXT("# CaseGinkgo input recording v8\n# start relative mode=MOVE_Walking\ntime,pitch,yaw,move_x,move_y,jump,crouch,sprint,direction_press,keys_x,keys_y,interact,aim,weapon_keys,fire_held,reload_held,wheel_x,wheel_y,stick_x,stick_y\n0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0\n");
	TestTrue(TEXT("旧 v8 输入可读取"), FGinkgoInputRecording::FromCsv(V8, Loaded, Error));
	TestFalse(TEXT("旧录制没有换肩按下沿"), Loaded.ShouldSwitchShoulderBetween(-1, 1));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

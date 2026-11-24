#include "Animation/AnimTypes.h"
#include "Animation/GinkgoAnimMath.h"
#include "Misc/AutomationTest.h"
#include "Movement/GinkgoMovementMath.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoAnimationMathTests
{
	/** 参照的步长：比任何帧都短得多，按帧离散的误差在 0.1 cm 以下 */
	constexpr float ReferenceStep = 1e-4f;

	/** 参照：按移动组件的顺序（先摩擦，再按新速度移动）用小步推进，没有输入，累计到停下走过的距离 */
	double SimulateStop(const double Speed, const float Friction, const float StopSpeed)
	{
		FVector V(Speed, 0.0, 0.0);
		double Distance = 0.0;
		for (int32 Index = 0; Index < 10'000'000 && V.X > 0.0; ++Index)
		{
			Ginkgo::Movement::ApplyFriction(V, Friction, StopSpeed, ReferenceStep);
			Distance += V.X * ReferenceStep;
		}
		return Distance;
	}

	/** 参照：同上，但一直按着反方向（地面加速，wishspeed 给定），累计到速度过零之前走过的距离 */
	double SimulatePivot(const double Speed, const float Friction, const float StopSpeed, const float GroundAccel, const float WishSpeed)
	{
		const FVector Backward(-1.0, 0.0, 0.0);
		FVector V(Speed, 0.0, 0.0);
		double Distance = 0.0;
		for (int32 Index = 0; Index < 10'000'000; ++Index)
		{
			Ginkgo::Movement::ApplyFriction(V, Friction, StopSpeed, ReferenceStep);
			Ginkgo::Movement::AccelerateOnGround(V, Backward, WishSpeed, GroundAccel * WishSpeed * ReferenceStep);
			if (V.X <= 0.0)
			{
				break;
			}
			Distance += V.X * ReferenceStep;
		}
		return Distance;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimStopDistanceTest, "CaseGinkgo.Animation.Math.StopDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimStopDistanceTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimationMathTests;
	// 默认的地面参数：摩擦 4，停止速度 190.5
	constexpr float Friction = 4.f;
	constexpr float StopSpeed = 190.5f;
	TestEqual(TEXT("冲刺速度：(v − S/2)/F"), Ginkgo::Animation::StopDistance(762.0, Friction, StopSpeed), 166.6875, 1e-9);
	TestEqual(TEXT("跑速"), Ginkgo::Animation::StopDistance(476.25, Friction, StopSpeed), 95.25, 1e-9);
	TestEqual(TEXT("停止速度以下：v²/(2FS)"), Ginkgo::Animation::StopDistance(100.0, Friction, StopSpeed), 100.0 * 100.0 / (2.0 * Friction * StopSpeed), 1e-9);
	TestEqual(TEXT("正好在停止速度上两支相接"), Ginkgo::Animation::StopDistance(StopSpeed, Friction, StopSpeed), StopSpeed / (2.0 * Friction), 1e-9);

	// 与移动组件的摩擦对照：连续时间的解是小步推进的极限
	for (const double Speed : {762.0, 476.25, 300.0, 190.5, 158.75, 50.0})
	{
		const double Expected = SimulateStop(Speed, Friction, StopSpeed);
		TestEqual(*FString::Printf(TEXT("%.2f cm/s 与小步推进相同"), Speed), Ginkgo::Animation::StopDistance(Speed, Friction, StopSpeed), Expected, 0.1);
	}

	TestEqual(TEXT("停止速度为 0：纯指数衰减，v/F"), Ginkgo::Animation::StopDistance(400.0, Friction, 0.0), 100.0, 1e-9);
	TestEqual(TEXT("不动时为 0"), Ginkgo::Animation::StopDistance(0.0, Friction, StopSpeed), 0.0);
	TestEqual(TEXT("没有摩擦停不下来，为 0"), Ginkgo::Animation::StopDistance(762.0, 0.0, StopSpeed), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimPivotDistanceTest, "CaseGinkgo.Animation.Math.PivotDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimPivotDistanceTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAnimationMathTests;
	constexpr float Friction = 4.f;
	constexpr float StopSpeed = 190.5f;
	constexpr float GroundAccel = 10.f;

	// 反向前的速度、反向时的 wishspeed：冲刺中按反方向（wishspeed 是冲刺速度）、跑步、蹲伏、停止速度以下、冲刺速度下松开冲刺再反向
	struct FCase
	{
		double Speed;
		float WishSpeed;
	};
	const FCase Cases[] = {{762.0, 762.f}, {476.25, 476.25f}, {158.75, 158.75f}, {120.0, 476.25f}, {762.0, 476.25f}};
	for (const FCase& Case : Cases)
	{
		const double Expected = SimulatePivot(Case.Speed, Friction, StopSpeed, GroundAccel, Case.WishSpeed);
		TestEqual(*FString::Printf(TEXT("%.2f cm/s、wishspeed %.2f 与小步推进相同"), Case.Speed, Case.WishSpeed),
			Ginkgo::Animation::PivotDistance(Case.Speed, Friction, StopSpeed, GroundAccel * Case.WishSpeed), Expected, 0.1);
	}

	TestEqual(TEXT("冲刺速度下反向：30.19 cm"), Ginkgo::Animation::PivotDistance(762.0, Friction, StopSpeed, GroundAccel * 762.0), 30.186, 1e-3);
	TestTrue(TEXT("反向比松手停得近"), Ginkgo::Animation::PivotDistance(476.25, Friction, StopSpeed, GroundAccel * 476.25) < Ginkgo::Animation::StopDistance(476.25, Friction, StopSpeed));
	TestEqual(TEXT("没有反向的加速时就是停止距离"), Ginkgo::Animation::PivotDistance(762.0, Friction, StopSpeed, 0.0), Ginkgo::Animation::StopDistance(762.0, Friction, StopSpeed), 1e-9);
	TestEqual(TEXT("停止速度为 0 也不出 NaN"), Ginkgo::Animation::PivotDistance(400.0, Friction, 0.0, 4000.0), (400.0 - 1000.0 * FMath::Loge(1400.0 / 1000.0)) / Friction, 1e-9);
	TestEqual(TEXT("不动时为 0"), Ginkgo::Animation::PivotDistance(0.0, Friction, StopSpeed, 4762.5), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimLocalAngleTest, "CaseGinkgo.Animation.Math.LocalAngle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimLocalAngleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("向右移动为 +90"), Ginkgo::Animation::LocalAngle(FVector(0.0, 300.0, 0.0), 0.0, 10.0), 90.0, 1e-9);
	TestEqual(TEXT("面向 +Y 时向 +X 移动是向左，−90"), Ginkgo::Animation::LocalAngle(FVector(300.0, 0.0, 0.0), 90.0, 10.0), -90.0, 1e-9);
	TestEqual(TEXT("跨过 ±180 取近的一边"), Ginkgo::Animation::LocalAngle(FRotator(0.0, -170.0, 0.0).Vector() * 300.0, 170.0, 10.0), 20.0, 1e-6);
	TestEqual(TEXT("后退为 ±180"), FMath::Abs(Ginkgo::Animation::LocalAngle(FVector(-300.0, 0.0, 0.0), 0.0, 10.0)), 180.0, 1e-9);
	TestEqual(TEXT("只看水平"), Ginkgo::Animation::LocalAngle(FVector(0.0, 300.0, -900.0), 0.0, 10.0), 90.0, 1e-9);
	TestEqual(TEXT("太慢时方向不可信，为 0"), Ginkgo::Animation::LocalAngle(FVector(0.0, 5.0, 0.0), 0.0, 10.0), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimMotionDifferencerTest, "CaseGinkgo.Animation.Math.MotionDifferencer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimMotionDifferencerTest::RunTest(const FString& Parameters)
{
	using FDifferencer = Ginkgo::Animation::FGinkgoMotionDifferencer;
	const auto MakeInput = [](const FVector& Location, const double Yaw, const FVector& Correction, const double DeltaSeconds, const bool bCut = false)
	{
		FDifferencer::FInput Input;
		Input.Location = Location;
		Input.FacingYaw = Yaw;
		Input.CorrectionOffsetTotal = Correction;
		Input.FallbackVelocity = FVector(111.0, 222.0, 333.0);
		Input.DeltaSeconds = DeltaSeconds;
		Input.bCut = bCut;
		return Input;
	};

	FDifferencer Differencer;
	// 第一次没有上一帧：速度取移动组件的，之前累计的校正不算
	FDifferencer::FOutput Output = Differencer.Update(MakeInput(FVector(0.0, 0.0, 100.0), 0.0, FVector(500.0, 0.0, 0.0), 0.01));
	TestTrue(TEXT("第一次：重置"), Output.bReset);
	TestEqual(TEXT("第一次：水平速度取移动组件的"), Output.Velocity, FVector(111.0, 222.0, 0.0));
	TestEqual(TEXT("第一次：竖直速度取移动组件的"), Output.VerticalSpeed, 333.0);
	TestEqual(TEXT("第一次：没有扣校正"), Output.CorrectionOffset, FVector::ZeroVector);

	// 水平、竖直分开差分
	Output = Differencer.Update(MakeInput(FVector(10.0, 0.0, 102.0), 0.0, FVector(500.0, 0.0, 0.0), 0.01));
	TestFalse(TEXT("第二次：不重置"), Output.bReset);
	TestEqual(TEXT("水平速度是水平位移除以帧长"), Output.Velocity, FVector(1000.0, 0.0, 0.0), 1e-6f);
	TestEqual(TEXT("竖直速度单独差分"), Output.VerticalSpeed, 200.0, 1e-9);
	TestEqual(TEXT("这一帧的水平位移"), Output.FrameDistance, 10.0, 1e-9);

	// 校正：胶囊被挪了 (50, −30, 4)，加上这一帧自己走的 (10, 0, 0)；扣掉之后只剩自己走的
	Output = Differencer.Update(MakeInput(FVector(70.0, -30.0, 106.0), 0.0, FVector(550.0, -30.0, 4.0), 0.01));
	TestEqual(TEXT("校正挪的量按帧取差"), Output.CorrectionOffset, FVector(50.0, -30.0, 4.0), 1e-6f);
	TestEqual(TEXT("扣掉校正后水平速度不变"), Output.Velocity, FVector(1000.0, 0.0, 0.0), 1e-6f);
	TestEqual(TEXT("扣掉校正后竖直速度为 0"), Output.VerticalSpeed, 0.0, 1e-9);
	TestEqual(TEXT("扣掉校正后的位移"), Output.FrameDistance, 10.0, 1e-9);

	// 帧长为 0：没有新的信息，返回上一次的结果，历史不动
	const FDifferencer::FOutput Paused = Differencer.Update(MakeInput(FVector(5000.0, 0.0, 0.0), 0.0, FVector(550.0, -30.0, 4.0), 0.0));
	TestEqual(TEXT("帧长为 0：返回上一次的速度"), Paused.Velocity, Output.Velocity, 1e-6f);
	Output = Differencer.Update(MakeInput(FVector(75.0, -30.0, 106.0), 0.0, FVector(550.0, -30.0, 4.0), 0.01));
	TestEqual(TEXT("帧长为 0 的那次不进历史"), Output.Velocity, FVector(500.0, 0.0, 0.0), 1e-6f);

	// 转速跨过 ±180 取近的一边
	Differencer.Update(MakeInput(FVector(75.0, -30.0, 106.0), 170.0, FVector(550.0, -30.0, 4.0), 0.01));
	Output = Differencer.Update(MakeInput(FVector(75.0, -30.0, 106.0), -170.0, FVector(550.0, -30.0, 4.0), 0.1));
	TestEqual(TEXT("转速：170 到 −170 是向右 20°"), Output.YawRate, 200.0, 1e-9);

	// 清掉历史：传送过去的那一帧不差分
	Output = Differencer.Update(MakeInput(FVector(9000.0, 9000.0, 0.0), 0.0, FVector(550.0, -30.0, 4.0), 0.01, true));
	TestTrue(TEXT("清掉历史：重置"), Output.bReset);
	TestEqual(TEXT("清掉历史：速度取移动组件的"), Output.Velocity, FVector(111.0, 222.0, 0.0));
	TestEqual(TEXT("清掉历史：位移为 0"), Output.FrameDistance, 0.0);
	Output = Differencer.Update(MakeInput(FVector(9000.0, 9005.0, 0.0), 0.0, FVector(550.0, -30.0, 4.0), 0.01));
	TestEqual(TEXT("清掉历史之后从新的位置差分"), Output.Velocity, FVector(0.0, 500.0, 0.0), 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimSelectDirectionTest, "CaseGinkgo.Animation.Math.SelectDirection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimSelectDirectionTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Animation::SelectDirection;
	using EDirection = EGinkgoAnimDirection;
	constexpr double DeadZone = 10.0;
	// 枚举按整数比，失败时打得出值
	const auto TestDirection = [this](const TCHAR* What, const EDirection Actual, const EDirection Expected)
	{
		TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	};

	// 四个扇区以 ±45°、±135° 为界（没有滞回时）
	TestDirection(TEXT("0° 向前"), SelectDirection(0.0, EDirection::F, 0.0), EDirection::F);
	TestDirection(TEXT("44° 还是向前"), SelectDirection(44.0, EDirection::F, 0.0), EDirection::F);
	TestDirection(TEXT("180° 向后"), SelectDirection(180.0, EDirection::F, 0.0), EDirection::B);
	TestDirection(TEXT("−180° 也是向后"), SelectDirection(-180.0, EDirection::F, 0.0), EDirection::B);

	// 滞回：越过边界不到 DeadZone 时不换
	TestDirection(TEXT("向前时 50° 不换"), SelectDirection(50.0, EDirection::F, DeadZone), EDirection::F);
	TestDirection(TEXT("向前时 56° 换到向右"), SelectDirection(56.0, EDirection::F, DeadZone), EDirection::RR);
	TestDirection(TEXT("向右时 40° 不换回向前"), SelectDirection(40.0, EDirection::RR, DeadZone), EDirection::RR);
	TestDirection(TEXT("向右时 34° 换回向前"), SelectDirection(34.0, EDirection::RR, DeadZone), EDirection::F);
	TestDirection(TEXT("向后时 128° 不换"), SelectDirection(128.0, EDirection::B, DeadZone), EDirection::B);

	// 髋的朝向：从前方转到侧向朝移动的方向，从后方转过来朝另一侧，左右之间反向时髋不转
	TestDirection(TEXT("从前方向左：LL"), SelectDirection(-90.0, EDirection::F, DeadZone), EDirection::LL);
	TestDirection(TEXT("从前方向右：RR"), SelectDirection(90.0, EDirection::F, DeadZone), EDirection::RR);
	TestDirection(TEXT("从后方向左：LR"), SelectDirection(-90.0, EDirection::B, DeadZone), EDirection::LR);
	TestDirection(TEXT("从后方向右：RL"), SelectDirection(90.0, EDirection::B, DeadZone), EDirection::RL);
	TestDirection(TEXT("LL 反向到右：RL（髋还朝左）"), SelectDirection(90.0, EDirection::LL, DeadZone), EDirection::RL);
	TestDirection(TEXT("LR 反向到右：RR"), SelectDirection(90.0, EDirection::LR, DeadZone), EDirection::RR);
	TestDirection(TEXT("RL 反向到左：LL"), SelectDirection(-90.0, EDirection::RL, DeadZone), EDirection::LL);
	TestDirection(TEXT("RR 反向到左：LR"), SelectDirection(-90.0, EDirection::RR, DeadZone), EDirection::LR);
	TestDirection(TEXT("侧向里留着原来的髋"), SelectDirection(-100.0, EDirection::LR, DeadZone), EDirection::LR);

	// 反向之后的方向与 GASP 反向片段的结尾一致
	TestDirection(TEXT("Pivot_F_B 之后向后"), Ginkgo::Animation::PivotExitDirection(EDirection::F), EDirection::B);
	TestDirection(TEXT("Pivot_LL_RL 之后是 RL"), Ginkgo::Animation::PivotExitDirection(EDirection::LL), EDirection::RL);
	TestDirection(TEXT("Pivot_RR_LR 之后是 LR"), Ginkgo::Animation::PivotExitDirection(EDirection::RR), EDirection::LR);
	TestEqual(TEXT("左右的角度"), Ginkgo::Animation::DirectionAngle(EDirection::LR), -90.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimLocomotionMathTest, "CaseGinkgo.Animation.Math.Locomotion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimLocomotionMathTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Animation;
	const FVector2D CycleClamp(0.75, 1.25);
	double PlayRate = 0.0;
	double Stride = 0.0;
	// 侧向跑：476 对片段的 350，速度比 1.36，播放速率夹到 1.25，步幅 1.09（动画文档的数）
	SplitSpeedRatio(476.25 / 350.0, CycleClamp, PlayRate, Stride);
	TestEqual(TEXT("侧向跑：播放速率夹到 1.25"), PlayRate, 1.25, 1e-9);
	TestEqual(TEXT("侧向跑：步幅 1.09"), Stride, 476.25 / 350.0 / 1.25, 1e-9);
	SplitSpeedRatio(0.9, CycleClamp, PlayRate, Stride);
	TestEqual(TEXT("范围内：全给播放速率"), PlayRate, 0.9, 1e-9);
	TestEqual(TEXT("范围内：步幅 1"), Stride, 1.0, 1e-9);

	TestEqual(TEXT("60° 用 45° 的转身"), TurnInPlaceIndex(60.0), 0);
	TestEqual(TEXT("70° 用 90° 的"), TurnInPlaceIndex(70.0), 1);
	TestEqual(TEXT("135° 用 135° 的"), TurnInPlaceIndex(135.0), 2);
	TestEqual(TEXT("170° 用 180° 的"), TurnInPlaceIndex(170.0), 3);
	TestEqual(TEXT("更大也是 180° 的"), TurnInPlaceIndex(400.0), 3);

	TestEqual(TEXT("胶囊向右转 30°，补偿 −30°"), AccumulateRootYawOffset(0.0, 30.0, 135.0), -30.0, 1e-9);
	TestEqual(TEXT("补偿夹在上限"), AccumulateRootYawOffset(-120.0, 30.0, 135.0), -135.0, 1e-9);
	TestEqual(TEXT("已经超过上限的不压回去"), AccumulateRootYawOffset(165.0, 5.0, 135.0), 160.0, 1e-9);
	TestEqual(TEXT("已经超过上限的也不再变大"), AccumulateRootYawOffset(165.0, -10.0, 135.0), 165.0, 1e-9);
	TestEqual(TEXT("跨过 ±180 时绕回来"), AccumulateRootYawOffset(170.0, -20.0, 180.0), -170.0, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimDistanceMatchingTest, "CaseGinkgo.Animation.Math.DistanceMatching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimDistanceMatchingTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Animation;
	// 急停的距离曲线：2 秒里从 −200 cm 匀减速到 0（d = −200 (1 − t/2)²），之后停着
	const auto Stop = [](const float Time) { const float Remaining = FMath::Max(1.f - Time / 2.f, 0.f); return -200.f * Remaining * Remaining; };
	TestEqual(TEXT("离停止点 50 cm：t = 1 s"), FindTimeAtCurveValue(Stop, -50.f, 3.f), 1.f, 1e-4f);
	TestEqual(TEXT("比首帧还远：从头"), FindTimeAtCurveValue(Stop, -500.f, 3.f), 0.f);
	TestEqual(TEXT("到了停止点之后：第一次到 0 的时刻附近"), FindTimeAtCurveValue(Stop, 0.f, 3.f), 2.f, 1e-3f);

	// 起步的距离曲线：v = 100 cm/s 匀速
	const auto Start = [](const float Time) { return 100.f * Time; };
	const FVector2D Clamp(0.75, 3.0);
	TestEqual(TEXT("这一帧走了 1.6667 cm、帧长 1/60 s：正好原速"), AdvanceTimeByDistance(Start, 0.5f, 100.f / 60.f, 2.f, 1.f / 60.f, Clamp), 0.5f + 1.f / 60.f, 1e-4f);
	TestEqual(TEXT("走了 5 倍：播放速率夹到 3"), AdvanceTimeByDistance(Start, 0.5f, 500.f / 60.f, 2.f, 1.f / 60.f, Clamp), 0.5f + 3.f / 60.f, 1e-4f);
	TestEqual(TEXT("不动：播放速率夹到 0.75"), AdvanceTimeByDistance(Start, 0.5f, 0.f, 2.f, 1.f / 60.f, Clamp), 0.5f + 0.75f / 60.f, 1e-4f);
	TestEqual(TEXT("不过片段的结尾"), AdvanceTimeByDistance(Start, 1.99f, 100.f, 2.f, 1.f / 60.f, Clamp), 2.f, 1e-6f);
	TestEqual(TEXT("帧长为 0 不动"), AdvanceTimeByDistance(Start, 0.5f, 10.f, 2.f, 0.f, Clamp), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimMarkerPhaseTest, "CaseGinkgo.Animation.Math.MarkerPhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimMarkerPhaseTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Animation::MatchMarkerPhase;
	const auto Markers = [](std::initializer_list<TPair<float, const TCHAR*>> Items)
	{
		TArray<FAnimSyncMarker> Result;
		for (const TPair<float, const TCHAR*>& Item : Items)
		{
			FAnimSyncMarker& Marker = Result.AddDefaulted_GetRef();
			Marker.Time = Item.Key;
			Marker.MarkerName = Item.Value;
		}
		return Result;
	};
	// 原片段 1 s：L 在 0.1，R 在 0.6；新片段 0.8 s：L 在 0.2，R 在 0.6
	const TArray<FAnimSyncMarker> From = Markers({ { 0.1f, TEXT("L") }, { 0.6f, TEXT("R") } });
	const TArray<FAnimSyncMarker> To = Markers({ { 0.2f, TEXT("L") }, { 0.6f, TEXT("R") } });
	TestEqual(TEXT("L 之后走了一半：新片段 L 之后的一半"), MatchMarkerPhase(From, 0.35f, 1.f, To, 0.8f), 0.4f, 1e-5f);
	TestEqual(TEXT("R 之后走了三成：新片段 R 之后的三成"), MatchMarkerPhase(From, 0.75f, 1.f, To, 0.8f), 0.6f + 0.3f * 0.4f, 1e-5f);
	TestEqual(TEXT("第一个标记之前算上一圈的最后一个之后"), MatchMarkerPhase(From, 0.05f, 1.f, To, 0.8f), FMath::Fmod(0.6f + 0.9f * 0.4f, 0.8f), 1e-5f);
	TestEqual(TEXT("没有标记时按归一化的时刻"), MatchMarkerPhase({}, 0.5f, 1.f, To, 0.8f), 0.4f, 1e-6f);
	TestEqual(TEXT("新片段没有同名标记时按归一化的时刻"), MatchMarkerPhase(From, 0.5f, 1.f, Markers({ { 0.3f, TEXT("X") } }), 0.8f), 0.4f, 1e-6f);

	// 下一步落哪只脚（滑铲的进入用）：过了最后一个标记绕回第一个
	using Ginkgo::Animation::NextMarkerName;
	TestEqual(TEXT("第一个标记之前：下一个是 L"), NextMarkerName(From, 0.05f), FName(TEXT("L")));
	TestEqual(TEXT("正好在 L 上：下一个是 R"), NextMarkerName(From, 0.1f), FName(TEXT("R")));
	TestEqual(TEXT("最后一个之后绕回 L"), NextMarkerName(From, 0.8f), FName(TEXT("L")));
	TestEqual(TEXT("没有标记"), NextMarkerName({}, 0.5f), FName());

	// 那只脚最后一次落地的时刻（GASP 的 Into_Lfoot：R 0.288、L 0.588）
	using Ginkgo::Animation::LastMarkerTime;
	const TArray<FAnimSyncMarker> Into = Markers({ { 0.288f, TEXT("R") }, { 0.588f, TEXT("L") } });
	TestEqual(TEXT("最后一次落左脚"), LastMarkerTime(Into, TEXT("L")), 0.588f);
	TestEqual(TEXT("最后一次落右脚"), LastMarkerTime(Into, TEXT("R")), 0.288f);
	TestEqual(TEXT("同名的取最后一个"), LastMarkerTime(Markers({ { 0.2f, TEXT("L") }, { 0.9f, TEXT("L") }, { 0.5f, TEXT("R") } }), TEXT("L")), 0.9f);
	TestEqual(TEXT("没有这个名字"), LastMarkerTime(Into, TEXT("X")), -1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimFollowMarkersTest, "CaseGinkgo.Animation.Math.FollowMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimFollowMarkersTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Animation;
	const auto Markers = [](std::initializer_list<TPair<float, const TCHAR*>> Items)
	{
		TArray<FAnimSyncMarker> Result;
		for (const TPair<float, const TCHAR*>& Item : Items)
		{
			FAnimSyncMarker& Marker = Result.AddDefaulted_GetRef();
			Marker.Time = Item.Key;
			Marker.MarkerName = Item.Value;
		}
		return Result;
	};
	// 跟的是两步一圈的循环（2 s：L 0.2、R 0.7、L 1.2、R 1.7），像 GASP 向前跑的循环与在它上面做的持枪循环
	const TArray<FAnimSyncMarker> Loop = Markers({ { 0.2f, TEXT("L") }, { 0.7f, TEXT("R") }, { 1.2f, TEXT("L") }, { 1.7f, TEXT("R") } });

	// 同一条时间线：时刻相同，不管上一帧在哪
	TestTrue(TEXT("时长与标记都相同是同一条时间线"), IsSameTimeline(Loop, 2.f, Loop, 2.f));
	TestFalse(TEXT("时长不同不是"), IsSameTimeline(Loop, 2.f, Loop, 2.1f));
	TestFalse(TEXT("标记的时刻不同不是"), IsSameTimeline(Loop, 2.f, Markers({ { 0.2f, TEXT("L") }, { 0.7f, TEXT("R") }, { 1.25f, TEXT("L") }, { 1.7f, TEXT("R") } }), 2.f));
	FGinkgoMarkerFollow Same;
	for (const float Time : { 0.1f, 1.35f, 1.99f, 0.4f })
	{
		FollowMarkers(Same, Loop, Time, 2.f, false, Loop, 2.f, 1.f / 60.f);
		TestEqual(FString::Printf(TEXT("同一条时间线上 %.2f s 跟到同一个时刻"), Time), Same.Time, Time);
	}

	// 别的片段（侧向跑，1 s 一圈：L 0.1、R 0.6）按标记跟：刚用上时落在第一个同名标记的那一段，走到同样的几成
	const TArray<FAnimSyncMarker> Strafe = Markers({ { 0.1f, TEXT("L") }, { 0.6f, TEXT("R") } });
	FGinkgoMarkerFollow Follow;
	FollowMarkers(Follow, Strafe, 0.35f, 1.f, true, Loop, 2.f, 1.f / 60.f);
	TestEqual(TEXT("刚用上：L 之后走了一半，第一个 L 之后的一半"), Follow.Time, 0.45f, 1e-5f);
	// 领头的走两圈（四步），跟的也走四步、正好一圈；第二圈落在跟的第二步上（MatchMarkerPhase 会跳回第一步），一路不跳
	float Previous = Follow.Time;
	float LargestStep = 0.f;
	float AtSecondLap = -1.f;
	for (int32 Frame = 1; Frame <= 200; ++Frame)
	{
		const float LeaderTime = FMath::Fmod(0.35f + 0.01f * Frame, 1.f);
		FollowMarkers(Follow, Strafe, LeaderTime, 1.f, false, Loop, 2.f, 1.f / 60.f);
		float Step = Follow.Time - Previous;
		Step += Step < -1.f ? 2.f : 0.f;
		LargestStep = FMath::Max(LargestStep, FMath::Abs(Step));
		AtSecondLap = Frame == 100 ? Follow.Time : AtSecondLap;
		Previous = Follow.Time;
	}
	TestTrue(FString::Printf(TEXT("一路往前、不跳（一帧最多 %.4f s）"), LargestStep), LargestStep < 0.0101f);
	TestEqual(TEXT("领头的第二圈同一处，跟的在第二步上"), AtSecondLap, 1.45f, 1e-4f);
	TestEqual(TEXT("领头的走了两圈，跟的正好一圈"), Follow.Time, 0.45f, 1e-4f);

	// 领头的换了片段（几成接着，同名的标记）：留在现在这一段，不跳；换到同一条时间线上时取它的时刻
	const TArray<FAnimSyncMarker> Back = Markers({ { 0.3f, TEXT("R") }, { 0.75f, TEXT("L") } });
	FGinkgoMarkerFollow Switched = Follow;
	FollowMarkers(Switched, Back, 0.75f + 0.5f * 0.55f, 1.f, true, Loop, 2.f, 1.f / 60.f);
	TestEqual(TEXT("换成后退的片段、L 之后的一半：留在跟的这一段"), Switched.Time, 0.45f, 1e-4f);
	FollowMarkers(Switched, Loop, 1.5f, 2.f, true, Loop, 2.f, 1.f / 60.f);
	TestEqual(TEXT("换回同一条时间线：取它的时刻"), Switched.Time, 1.5f);

	// 领头的连着两个同名标记（GASP 的 Run_Loop_RR 有连着两个 R）：只算第一个，过了第二个接着走、不跳
	const TArray<FAnimSyncMarker> Twice = Markers({ { 0.1f, TEXT("L") }, { 0.4f, TEXT("L") }, { 0.8f, TEXT("R") } });
	FGinkgoMarkerFollow Forward;
	FollowMarkers(Forward, Twice, 0.39f, 1.f, true, Loop, 2.f, 1.f / 60.f);
	const float BeforeSecond = Forward.Time;
	FollowMarkers(Forward, Twice, 0.41f, 1.f, false, Loop, 2.f, 1.f / 60.f);
	TestEqual(TEXT("过了第二个同名标记：L 0.1 → R 0.8 这一段接着走（0.02 s 的 5/7）"), Forward.Time - BeforeSecond, 0.02f / 0.7f * 0.5f, 1e-5f);
	// 排序：标记不按时刻排着时也一样
	const TArray<FAnimSyncMarker> Shuffled = Markers({ { 0.6f, TEXT("R") }, { 0.1f, TEXT("L") } });
	FGinkgoMarkerFollow Unsorted;
	FollowMarkers(Unsorted, Shuffled, 0.35f, 1.f, true, Loop, 2.f, 1.f / 60.f);
	TestEqual(TEXT("标记没排好也按时刻算"), Unsorted.Time, 0.45f, 1e-5f);

	// 领头的没有标记（站着的循环、下落）：照帧长往前走，绕回
	FGinkgoMarkerFollow Free;
	Free.Time = 1.99f;
	Free.bSynced = true;
	FollowMarkers(Free, {}, 0.5f, 3.f, false, Loop, 2.f, 0.02f);
	TestEqual(TEXT("没有标记时往前走一帧，过了结尾绕回"), Free.Time, 0.01f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimCrossFootMarkersTest, "CaseGinkgo.Animation.Math.CrossFootMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimCrossFootMarkersTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Animation::CrossFootMarkers;
	const auto Markers = [](std::initializer_list<TPair<float, const TCHAR*>> Items)
	{
		TArray<FAnimSyncMarker> Result;
		for (const TPair<float, const TCHAR*>& Item : Items)
		{
			FAnimSyncMarker& Marker = Result.AddDefaulted_GetRef();
			Marker.Time = Item.Key;
			Marker.MarkerName = Item.Value;
		}
		return Result;
	};
	// 循环 0.8 s：L 在 0（首帧着地）、R 在 0.4
	const TArray<FAnimSyncMarker> Loop = Markers({ { 0.f, TEXT("L") }, { 0.4f, TEXT("R") } });
	FName Last;
	auto Steps = CrossFootMarkers(Loop, 0.35f, 0.45f, 0.8f, Last);
	TestTrue(TEXT("跨过 R：走了这一帧的一半"), Steps.Num() == 1 && Steps[0].Key == TEXT("R") && FMath::IsNearlyEqual(Steps[0].Value, 0.5f, 1e-5f) && Last == TEXT("R"));
	Steps = CrossFootMarkers(Loop, 0.78f, 0.02f, 0.8f, Last);
	TestTrue(TEXT("过了末尾接着从头：跨过 0 上的 L"), Steps.Num() == 1 && Steps[0].Key == TEXT("L") && FMath::IsNearlyEqual(Steps[0].Value, 0.5f, 1e-5f));
	TestEqual(TEXT("正好停在标记上的那一帧算过，下一帧不再算"), CrossFootMarkers(Loop, 0.4f, 0.45f, 0.8f, Last).Num(), 0);
	TestEqual(TEXT("一帧走了半圈以上：时刻被设过，不算"), CrossFootMarkers(Loop, 0.1f, 0.55f, 0.8f, Last).Num(), 0);
	TestEqual(TEXT("往回退一点：不算（当成绕回就走了大半圈）"), CrossFootMarkers(Loop, 0.45f, 0.38f, 0.8f, Last).Num(), 0);

	// 连着同名的只算第一个（GASP 的 Run_Loop_RR 在 1.479、1.57 s 连着两个 R）：跨帧也一样
	const TArray<FAnimSyncMarker> Flicker = Markers({ { 0.2f, TEXT("L") }, { 0.6f, TEXT("R") }, { 0.65f, TEXT("R") } });
	Last = NAME_None;
	Steps = CrossFootMarkers(Flicker, 0.55f, 0.7f, 1.f, Last);
	TestTrue(TEXT("一帧里连着两个 R：只算第一个"), Steps.Num() == 1 && Steps[0].Key == TEXT("R"));
	Last = TEXT("R");
	TestEqual(TEXT("上一帧落的是 R：这一帧的 R 不算"), CrossFootMarkers(Flicker, 0.62f, 0.66f, 1.f, Last).Num(), 0);
	TestEqual(TEXT("片段的时长不是正的"), CrossFootMarkers(Loop, 0.1f, 0.2f, 0.f, Last).Num(), 0);

	// 各个帧率下落脚的时刻相同：片段按原速走 2 s，落脚的时刻 = 帧末 − 帧长 × (1 − 几成)，与标记的时刻（加上第几圈）只差浮点舍入
	for (const float Rate : { 30.f, 60.f, 144.f })
	{
		FName Previous;
		float Time = 0.03f;
		double Largest = 0.0;
		int32 Count = 0;
		const int32 Frames = FMath::RoundToInt32(Rate * 2.f);
		for (int32 Frame = 1; Frame <= Frames; ++Frame)
		{
			const float LastTime = Time;
			Time = FMath::Fmod(Time + 1.f / Rate, 0.8f);
			for (const TPair<FName, float>& Step : CrossFootMarkers(Loop, LastTime, Time, 0.8f, Previous))
			{
				const double At = Frame / static_cast<double>(Rate) - (1.0 - Step.Value) / Rate;
				const double Marker = (Step.Key == TEXT("L") ? 0.0 : 0.4) - 0.03;
				const double Expected = Marker + 0.8 * FMath::RoundToDouble((At - Marker) / 0.8);
				Largest = FMath::Max(Largest, FMath::Abs(At - Expected));
				++Count;
			}
		}
		TestTrue(FString::Printf(TEXT("%.0f fps：2 s 落 %d 步，时刻与标记差 %.2e s"), Rate, Count, Largest), Count == 5 && Largest < 1e-4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAnimLoopClockTest, "CaseGinkgo.Animation.Math.LoopClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAnimLoopClockTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Animation;
	// 一直走：不切时加上帧长；切过的那一帧从这一帧的帧长起（切在上一帧末）
	TestEqual(TEXT("不切时加上帧长"), StepLoopClock(5.0, 0.25f, false), 5.25);
	TestEqual(TEXT("切过的那一帧从帧长起"), StepLoopClock(5.0, 0.25f, true), 0.25);

	// 落在开镜的循环（16 s）的哪一刻：除以时长的余数，绕过几圈也一样；时长不是正的取 0
	TestEqual(TEXT("第一圈里就是时钟"), LoopTimeAt(3.5, 16.f), 3.5f);
	TestEqual(TEXT("正好一圈回到 0"), LoopTimeAt(16.0, 16.f), 0.f);
	TestEqual(TEXT("一小时以后照样绕"), LoopTimeAt(3600.0 + 7.25, 16.f), 7.25f, 1e-4f);
	TestEqual(TEXT("时长不是正的取 0"), LoopTimeAt(3.5, 0.f), 0.f);

	// 各个帧率下切过以后走到同一个时刻取到同一帧：回放的轨迹逐行比开镜的循环。切之前走了多久都不影响
	for (const float Rate : { 30.f, 60.f, 144.f })
	{
		double Clock = 123.456;
		const int32 Frames = FMath::RoundToInt32(Rate * 17.5f);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			Clock = StepLoopClock(Clock, 1.f / Rate, Frame == 0);
		}
		TestEqual(FString::Printf(TEXT("%.0f fps：切过以后 17.5 s 落在第 1.5 s"), Rate), LoopTimeAt(Clock, 16.f), 1.5f, 1e-5f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

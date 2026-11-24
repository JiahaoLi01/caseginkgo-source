#include "Misc/AutomationTest.h"
#include "Movement/GinkgoMovementSimState.h"
#include "Serialization/BitReader.h"
#include "Serialization/BitWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoMovementNetTests
{
	/** 写进比特流再读出来 */
	FGinkgoMovementSimState RoundTrip(FGinkgoMovementSimState State, FGinkgoMovementSimState Into, int64& OutBits)
	{
		FBitWriter Writer(0, true);
		State.NetSerialize(Writer);
		OutBits = Writer.GetNumBits();
		FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
		Into.NetSerialize(Reader);
		return Into;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSimStateSerializeTest, "CaseGinkgo.Movement.Net.SimStateSerialize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSimStateSerializeTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementNetTests;
	int64 Bits = 0;

	// 浮点不量化：读出来逐位相同
	FGinkgoMovementSimState Sliding;
	Sliding.TimeSinceLanded = 0.0371f;
	Sliding.bSliding = true;
	Sliding.SlideTime = 0.8125f;
	Sliding.SlideBoost = 406.4f;
	const FGinkgoMovementSimState SlidingRead = RoundTrip(Sliding, FGinkgoMovementSimState(), Bits);
	TestTrue(TEXT("读出在滑铲"), SlidingRead.bSliding);
	TestEqual(TEXT("落地计时逐位相同"), SlidingRead.TimeSinceLanded, Sliding.TimeSinceLanded);
	TestEqual(TEXT("已滑时间逐位相同"), SlidingRead.SlideTime, Sliding.SlideTime);
	TestEqual(TEXT("起步加速逐位相同"), SlidingRead.SlideBoost, Sliding.SlideBoost);
	TestEqual(TEXT("在滑铲、没有窗口时 109 位：落地计时 32 + 标记 1 + 滑铲两项 64 + 窗口个数 4 + 处理过的按下计数 8"), Bits, int64(109));

	// 不在滑铲时不写滑铲的两项，读出时清零（接收方原来的旧值不能留下）
	FGinkgoMovementSimState Standing;
	Standing.TimeSinceLanded = 0.5f;
	Standing.SlideTime = 3.f;
	FGinkgoMovementSimState Stale;
	Stale.bSliding = true;
	Stale.SlideTime = 9.f;
	Stale.SlideBoost = 100.f;
	const FGinkgoMovementSimState StandingRead = RoundTrip(Standing, Stale, Bits);
	TestEqual(TEXT("不在滑铲、没有窗口时只写 45 位"), Bits, int64(45));
	TestFalse(TEXT("读出不在滑铲"), StandingRead.bSliding);
	TestEqual(TEXT("读出时清掉滑铲已滑时间"), StandingRead.SlideTime, 0.f);
	TestEqual(TEXT("读出时清掉起步加速"), StandingRead.SlideBoost, 0.f);
	TestEqual(TEXT("落地计时逐位相同"), StandingRead.TimeSinceLanded, 0.5f);

	// 处理过的方向键按下计数：原样读写，不同就算不一致（服务器晚一次移动才处理到的按下，校正放回它，重放不再处理）
	FGinkgoMovementSimState Pressed = Standing;
	Pressed.HandledDirectionPressCount = 255;
	TestEqual(TEXT("处理过的按下计数原样读出"), int32(RoundTrip(Pressed, FGinkgoMovementSimState(), Bits).HandledDirectionPressCount), 255);

	// 比对：合并时多算的一次 Δt（至少一帧）超出 1 ms 的容差，零点几毫秒的落地时刻差在容差内
	constexpr float TimeTolerance = 1e-3f;
	constexpr float SpeedTolerance = 1.f;
	FGinkgoMovementSimState Drifted = Sliding;
	Drifted.SlideTime += 1.f / 144.f;
	TestEqual(TEXT("多算一帧 Δt 时计时差一帧"), Sliding.MaxTimeDifference(Drifted), 1.f / 144.f, 1e-6f);
	TestFalse(TEXT("多算一帧 Δt 算不一致"), Sliding.NearlyEquals(Drifted, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState Jittered = Sliding;
	Jittered.TimeSinceLanded += 0.000116f;
	TestTrue(TEXT("零点一毫秒的落地时刻差算一致"), Sliding.NearlyEquals(Jittered, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState Boosted = Sliding;
	Boosted.SlideBoost += 438.15f;
	TestFalse(TEXT("起步加速给了与没给算不一致"), Sliding.NearlyEquals(Boosted, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState NotSliding = Sliding;
	NotSliding.bSliding = false;
	TestFalse(TEXT("滑铲与否不同算不一致"), Sliding.NearlyEquals(NotSliding, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState StandingOther = Standing;
	StandingOther.SlideTime = 7.f;
	TestTrue(TEXT("都不在滑铲时不比滑铲的两项"), Standing.NearlyEquals(StandingOther, TimeTolerance, SpeedTolerance));
	TestFalse(TEXT("处理过的按下计数不同算不一致"), Standing.NearlyEquals(Pressed, TimeTolerance, SpeedTolerance));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSimStateWindowsTest, "CaseGinkgo.Movement.Net.SimStateWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSimStateWindowsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoMovementNetTests;
	int64 Bits = 0;

	// 窗口逐个写下标、阶段与两个浮点，浮点不量化；接收方原来的窗口整个换掉
	FGinkgoMovementSimState WithWindows;
	WithWindows.TimeSinceLanded = 0.25f;
	WithWindows.Windows.Add({2, 1, 0.0123f, 438.15f});
	WithWindows.Windows.Add({0, 0, 1.f / 144.f, 0.f});
	FGinkgoMovementSimState Stale;
	Stale.Windows.SetNum(5);
	const FGinkgoMovementSimState Read = RoundTrip(WithWindows, Stale, Bits);
	TestEqual(TEXT("两个窗口 205 位：45 + 2 × (8 + 8 + 32 + 32)"), Bits, int64(205));
	TestEqual(TEXT("读出两个窗口"), Read.Windows.Num(), 2);
	for (int32 Index = 0; Index < FMath::Min(Read.Windows.Num(), WithWindows.Windows.Num()); ++Index)
	{
		const FGinkgoMovementWindowState& Expected = WithWindows.Windows[Index];
		TestEqual(TEXT("定义的下标相同"), int32(Read.Windows[Index].Definition), int32(Expected.Definition));
		TestEqual(TEXT("阶段相同"), int32(Read.Windows[Index].Phase), int32(Expected.Phase));
		TestEqual(TEXT("阶段已过时间逐位相同"), Read.Windows[Index].PhaseTime, Expected.PhaseTime, 0.f);
		TestEqual(TEXT("参数逐位相同"), Read.Windows[Index].Param, Expected.Param, 0.f);
	}
	TestEqual(TEXT("没有窗口时读出也清掉原来的窗口"), RoundTrip(FGinkgoMovementSimState(), Stale, Bits).Windows.Num(), 0);

	// 坏数据：个数超过容量时只读到容量为止，不撑破固定容量的数组
	FBitWriter Writer(0, true);
	float TimeSinceLanded = 1.f;
	Writer << TimeSinceLanded;
	uint8 SlidingBit = 0;
	Writer.SerializeBits(&SlidingBit, 1);
	uint8 Count = 15;
	Writer.SerializeBits(&Count, 4);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FGinkgoMovementWindowState Window;
		Writer << Window.Definition << Window.Phase << Window.PhaseTime << Window.Param;
	}
	FBitReader Reader(Writer.GetData(), Writer.GetNumBits());
	FGinkgoMovementSimState Overflowed;
	Overflowed.NetSerialize(Reader);
	TestEqual(TEXT("个数超过容量时只读到容量"), Overflowed.Windows.Num(), FGinkgoMovementSimState::MaxWindows);
	TestFalse(TEXT("没有读过头"), Reader.IsError());

	// 比对：窗口的个数、定义与阶段不同算不一致；阶段已过时间与别的计时一样按 1 ms 比，参数按起步加速的容差比
	constexpr float TimeTolerance = 1e-3f;
	constexpr float SpeedTolerance = 1.f;
	TestTrue(TEXT("相同的窗口一致"), WithWindows.NearlyEquals(Read, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState OtherPhase = WithWindows;
	OtherPhase.Windows[0].Phase = 0;
	TestFalse(TEXT("阶段不同算不一致"), WithWindows.NearlyEquals(OtherPhase, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState Missing = WithWindows;
	Missing.Windows.RemoveAt(1);
	TestFalse(TEXT("个数不同算不一致"), WithWindows.NearlyEquals(Missing, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState Late = WithWindows;
	Late.Windows[1].PhaseTime += 1.f / 144.f;
	TestEqual(TEXT("多算一帧时阶段已过时间差一帧"), WithWindows.MaxTimeDifference(Late), 1.f / 144.f, 1e-6f);
	TestFalse(TEXT("多算一帧算不一致"), WithWindows.NearlyEquals(Late, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState Jittered = WithWindows;
	Jittered.Windows[1].PhaseTime += 0.0001f;
	TestTrue(TEXT("零点一毫秒的差算一致"), WithWindows.NearlyEquals(Jittered, TimeTolerance, SpeedTolerance));
	FGinkgoMovementSimState OtherParam = WithWindows;
	OtherParam.Windows[0].Param = 0.f;
	TestFalse(TEXT("参数不同算不一致"), WithWindows.NearlyEquals(OtherParam, TimeTolerance, SpeedTolerance));
	TestEqual(TEXT("窗口对不上时不比阶段已过时间"), WithWindows.MaxTimeDifference(OtherPhase), 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

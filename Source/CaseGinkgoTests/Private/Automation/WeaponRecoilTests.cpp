#include "Misc/AutomationTest.h"
#include "Weapon/GinkgoRecoilPattern.h"
#include "Weapon/GinkgoWeaponTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoWeaponRecoilTests
{
	UGinkgoRecoilPattern* MakePattern(const TArray<FVector2f>& Shots, const int32 LoopFrom = 0)
	{
		UGinkgoRecoilPattern* Pattern = NewObject<UGinkgoRecoilPattern>();
		Pattern->Shots = Shots;
		Pattern->LoopFrom = LoopFrom;
		return Pattern;
	}

	FGinkgoPatternRecoil MakeRecoil(UGinkgoRecoilPattern* Pattern)
	{
		FGinkgoPatternRecoil Recoil;
		Recoil.Pattern = Pattern;
		Recoil.ApplyTime = 0.06f;
		Recoil.RecoveryDelay = 0.15f;
		Recoil.RecoveryDecay = 6.f;
		Recoil.RecoveryLinear = 8.f;
		Recoil.ShotRecoveryRate = 8.f;
		return Recoil;
	}

	/**
	 * 按固定帧长打 Shots 发（发射间隔 0.1 s），再走 Tail 秒，玩家不动
	 * @return 视线一共动了多少
	 */
	FVector2D Spray(FGinkgoRecoilState& State, const UGinkgoRecoilPattern& Pattern, const FGinkgoPatternRecoil& Recoil, const int32 Shots, const double Tail, const double FrameTime)
	{
		FVector2D View = FVector2D::ZeroVector;
		double NextShot = 0.0;
		int32 Fired = 0;
		const double End = (Shots - 1) * 0.1 + Tail;
		for (double Time = 0.0; Time < End - UE_KINDA_SMALL_NUMBER; Time += FrameTime)
		{
			if (Fired < Shots && Time >= NextShot - UE_KINDA_SMALL_NUMBER)
			{
				Ginkgo::Weapon::AddRecoilKick(State, Pattern, Recoil, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
				NextShot += 0.1;
				++Fired;
			}
			View += Ginkgo::Weapon::StepRecoil(State, FVector2D::ZeroVector, FrameTime);
		}
		return View;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoRecoilPatternStepTest, "CaseGinkgo.Weapon.Recoil.PatternStep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGinkgoRecoilPatternStepTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponRecoilTests;
	const UGinkgoRecoilPattern* Empty = MakePattern({});
	TestEqual(TEXT("空的模板不动"), Empty->GetStepAfterShot(3), FVector2f::ZeroVector);
	const UGinkgoRecoilPattern* One = MakePattern({FVector2f(1.f, 2.f)});
	TestEqual(TEXT("只有一发不动"), One->GetStepAfterShot(0), FVector2f::ZeroVector);

	const UGinkgoRecoilPattern* Pattern = MakePattern({FVector2f(0.f, 0.f), FVector2f(1.f, 1.f), FVector2f(3.f, 2.f), FVector2f(2.f, 5.f)}, 1);
	TestEqual(TEXT("第 0 发以后"), Pattern->GetStepAfterShot(0), FVector2f(1.f, 1.f));
	TestEqual(TEXT("负的按 0 算"), Pattern->GetStepAfterShot(-2), FVector2f(1.f, 1.f));
	TestEqual(TEXT("第 2 发以后是最后一步"), Pattern->GetStepAfterShot(2), FVector2f(-1.f, 3.f));
	TestEqual(TEXT("打到头从 LoopFrom 接着走"), Pattern->GetStepAfterShot(3), FVector2f(2.f, 1.f));
	TestEqual(TEXT("循环"), Pattern->GetStepAfterShot(4), FVector2f(-1.f, 3.f));
	TestEqual(TEXT("循环再一圈"), Pattern->GetStepAfterShot(5), FVector2f(2.f, 1.f));

	UGinkgoRecoilPattern* LoopTooBig = MakePattern({FVector2f(0.f, 0.f), FVector2f(1.f, 1.f), FVector2f(3.f, 2.f)}, 99);
	TestEqual(TEXT("LoopFrom 太大按最后一步循环"), LoopTooBig->GetStepAfterShot(7), FVector2f(2.f, 1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoRecoilFrameRateTest, "CaseGinkgo.Weapon.Recoil.FrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGinkgoRecoilFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponRecoilTests;
	TArray<FVector2f> Shots;
	for (int32 Index = 0; Index < 30; ++Index)
	{
		Shots.Add(FVector2f(FMath::Sin(Index * 0.7f) * 3.f, FMath::Min(Index * 1.2f, 11.f)));
	}
	UGinkgoRecoilPattern* Pattern = MakePattern(Shots);
	const FGinkgoPatternRecoil Recoil = MakeRecoil(Pattern);
	const int32 Fired = 12;

	// 打 12 发，第 12 发的增量摊完（还在回复开始之前）：视线正好在第 13 发的位置，同帧率无关
	for (const double FrameTime : {1.0 / 30.0, 1.0 / 60.0, 1.0 / 144.0})
	{
		FGinkgoRecoilState State;
		const FVector2D View = Spray(State, *Pattern, Recoil, Fired, 0.09, FrameTime);
		const FVector2f Expected = Shots[Fired];
		TestEqual(FString::Printf(TEXT("%.0f fps：偏航走到第 13 发"), 1.0 / FrameTime), View.X, static_cast<double>(Expected.X), 1e-4);
		TestEqual(FString::Printf(TEXT("%.0f fps：俯仰走到第 13 发"), 1.0 / FrameTime), View.Y, static_cast<double>(Expected.Y), 1e-4);
		TestEqual(FString::Printf(TEXT("%.0f fps：一共动的就是还没压的"), 1.0 / FrameTime), State.Accumulated.Y, View.Y, 1e-9);
		TestEqual(FString::Printf(TEXT("%.0f fps：回放要加的同样多"), 1.0 / FrameTime), State.Applied.Y, View.Y, 1e-9);
	}

	// 回复也与帧率无关：一发走完以后回到同一时刻（三个帧长都整除 1/3 s），三个帧率差不到 0.001°
	UGinkgoRecoilPattern* Big = MakePattern({FVector2f(0.f, 0.f), FVector2f(0.f, 10.f)});
	FGinkgoPatternRecoil Quick = MakeRecoil(Big);
	Quick.ApplyTime = 0.03f;
	Quick.RecoveryDelay = 0.05f;
	double First = 0.0;
	for (const int32 Fps : {144, 60, 30})
	{
		FGinkgoRecoilState Back;
		Ginkgo::Weapon::AddRecoilKick(Back, *Big, Quick, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		double Pitch = 0.0;
		for (int32 Frame = 0; Frame < Fps / 3; ++Frame)
		{
			Pitch += Ginkgo::Weapon::StepRecoil(Back, FVector2D::ZeroVector, 1.0 / Fps).Y;
		}
		First = First == 0.0 ? Pitch : First;
		TestTrue(FString::Printf(TEXT("%d fps：还没回完，比的是回到一半的"), Fps), Pitch > 0.1);
		TestEqual(FString::Printf(TEXT("%d fps：回复以后的俯仰"), Fps), Pitch, First, 1e-3);
	}

	// 走够久全回来，打到第几发也退回 0
	FGinkgoRecoilState State;
	const FVector2D View = Spray(State, *Pattern, Recoil, Fired, 5.0, 1.0 / 60.0);
	TestEqual(TEXT("全回来了"), View.Size(), 0.0, 1e-6);
	TestEqual(TEXT("打到第几发退回 0"), static_cast<double>(State.ShotIndex), 0.0, 1e-6);
	TestEqual(TEXT("回放要加的也是 0"), State.Applied.Size(), 0.0, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoRecoilCompensationTest, "CaseGinkgo.Weapon.Recoil.Compensation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGinkgoRecoilCompensationTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponRecoilTests;
	UGinkgoRecoilPattern* Pattern = MakePattern({FVector2f(0.f, 0.f), FVector2f(0.f, 2.f), FVector2f(0.f, 4.f)});
	const FGinkgoPatternRecoil Recoil = MakeRecoil(Pattern);

	// 全压掉：玩家把上抬的都压回去，停火以后不再往下掉
	{
		FGinkgoRecoilState State;
		Ginkgo::Weapon::AddRecoilKick(State, *Pattern, Recoil, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		TestEqual(TEXT("第一帧走一部分"), Ginkgo::Weapon::StepRecoil(State, FVector2D::ZeroVector, 0.03).Y, 2.0 * 0.75, 1e-5);
		Ginkgo::Weapon::StepRecoil(State, FVector2D(0.0, -1.5), 0.03);
		TestEqual(TEXT("压的这 1.5° 抵掉了，增量走完"), State.Accumulated.Y, 0.5, 1e-5);
		Ginkgo::Weapon::StepRecoil(State, FVector2D(0.0, -0.5), 0.0);
		TestEqual(TEXT("全压掉了"), State.Accumulated.Y, 0.0, 1e-5);
		const FVector2D Later = Ginkgo::Weapon::StepRecoil(State, FVector2D::ZeroVector, 1.0);
		TestEqual(TEXT("压掉的不回"), Later.Y, 0.0, 1e-5);
	}
	// 压过头不算：没有还没回复的了，多压的是玩家自己想往下看
	{
		FGinkgoRecoilState State;
		Ginkgo::Weapon::AddRecoilKick(State, *Pattern, Recoil, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		Ginkgo::Weapon::StepRecoil(State, FVector2D::ZeroVector, 0.06);
		Ginkgo::Weapon::StepRecoil(State, FVector2D(0.0, -5.0), 0.0);
		TestEqual(TEXT("压过头到 0 为止"), State.Accumulated.Y, 0.0, 1e-5);
	}
	// 同方向的输入不算压：玩家往上看，回复照回
	{
		FGinkgoRecoilState State;
		Ginkgo::Weapon::AddRecoilKick(State, *Pattern, Recoil, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		Ginkgo::Weapon::StepRecoil(State, FVector2D::ZeroVector, 0.06);
		Ginkgo::Weapon::StepRecoil(State, FVector2D(3.0, 5.0), 0.0);
		TestEqual(TEXT("同方向不变"), State.Accumulated.Y, 2.0, 1e-5);
	}
	// 回复只回还没压掉的：压掉一半，回的也只有一半
	{
		FGinkgoRecoilState State;
		Ginkgo::Weapon::AddRecoilKick(State, *Pattern, Recoil, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		Ginkgo::Weapon::StepRecoil(State, FVector2D::ZeroVector, 0.06);
		Ginkgo::Weapon::StepRecoil(State, FVector2D(0.0, -1.0), 0.0);
		const FVector2D Back = Ginkgo::Weapon::StepRecoil(State, FVector2D::ZeroVector, 5.0);
		TestEqual(TEXT("只回一半"), Back.Y, -1.0, 1e-5);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoRecoilShotIndexTest, "CaseGinkgo.Weapon.Recoil.ShotIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGinkgoRecoilShotIndexTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponRecoilTests;
	UGinkgoRecoilPattern* Pattern = MakePattern({FVector2f(0.f, 0.f), FVector2f(0.f, 1.f), FVector2f(0.f, 3.f), FVector2f(0.f, 6.f)});
	const FGinkgoPatternRecoil Recoil = MakeRecoil(Pattern);

	// 点射：隔 0.3 s 打一发，回复 0.15 s × 8 发/秒，每次都退回头几发，一直打在模板的开头
	FGinkgoRecoilState Tap;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Ginkgo::Weapon::AddRecoilKick(Tap, *Pattern, Recoil, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		Ginkgo::Weapon::StepRecoil(Tap, FVector2D::ZeroVector, 0.3);
		TestEqual(TEXT("点射退回第 0 发"), static_cast<double>(Tap.ShotIndex), 0.0, 1e-6);
	}

	// 连着打：一直往后走
	FGinkgoRecoilState Burst;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Ginkgo::Weapon::AddRecoilKick(Burst, *Pattern, Recoil, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
		Ginkgo::Weapon::StepRecoil(Burst, FVector2D::ZeroVector, 0.1);
	}
	TestEqual(TEXT("连着打走到第 3 发"), static_cast<double>(Burst.ShotIndex), 3.0, 1e-6);

	// 换了模板从头算
	const UGinkgoRecoilPattern* Other = MakePattern({FVector2f(0.f, 0.f), FVector2f(1.f, 0.f)});
	Ginkgo::Weapon::AddRecoilKick(Burst, *Other, Recoil, FVector2D(1.0, 1.0), FVector2D::ZeroVector);
	TestEqual(TEXT("换了模板从第 0 发算，打完是 1"), static_cast<double>(Burst.ShotIndex), 1.0, 1e-6);

	// 射击之间的时刻：接着响的迟到不累积；到点以后才点的从现在算，速点快不过射速（你发现的：以前晚了不到一个间隔都算连着打，下一枪只隔 0.014 s）
	TestEqual(TEXT("接着响，迟到一帧：从该响的时刻排"), Ginkgo::Weapon::GetNextShotTime(1.0, 1.004, 0.1, true), 1.1, 1e-9);
	TestEqual(TEXT("接着响，卡了一下（晚了一个间隔以上）：不补"), Ginkgo::Weapon::GetNextShotTime(1.0, 1.25, 0.1, true), 1.35, 1e-9);
	TestEqual(TEXT("到点以后才点的：从现在排，下一枪隔满一个间隔"), Ginkgo::Weapon::GetNextShotTime(1.0, 1.09, 0.1, false), 1.19, 1e-9);
	TestEqual(TEXT("停了很久：从现在排"), Ginkgo::Weapon::GetNextShotTime(1.0, 3.0, 0.1, false), 3.1, 1e-9);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoRecoilImportTest, "CaseGinkgo.Weapon.Recoil.Import",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGinkgoRecoilImportTest::RunTest(const FString& Parameters)
{
	UGinkgoRecoilPattern* Pattern = NewObject<UGinkgoRecoilPattern>();
	FString Error;
	TestTrue(TEXT("读得进来"), Pattern->ImportFromJson(TEXT("{\"format\":\"ginkgo.recoil\",\"version\":1,\"name\":\"t\",\"shots\":[[0,0],[1.5,-2]],\"loopFrom\":1,\"jitter\":[0.1,0.2]}"), Error));
	TestEqual(TEXT("两发"), Pattern->Shots.Num(), 2);
	TestEqual(TEXT("第二发"), Pattern->Shots[1], FVector2f(1.5f, -2.f));
	TestEqual(TEXT("循环"), Pattern->LoopFrom, 1);
	TestEqual(TEXT("扰动"), Pattern->Jitter, FVector2f(0.1f, 0.2f));

	const TCHAR* const Bad[] = {
		TEXT("不是 JSON"),
		TEXT("{\"format\":\"别的\",\"version\":1,\"shots\":[[0,0]]}"),
		TEXT("{\"format\":\"ginkgo.recoil\",\"version\":2,\"shots\":[[0,0]]}"),
		TEXT("{\"format\":\"ginkgo.recoil\",\"version\":1,\"shots\":[]}"),
		TEXT("{\"format\":\"ginkgo.recoil\",\"version\":1,\"shots\":[[0]]}"),
		TEXT("{\"format\":\"ginkgo.recoil\",\"version\":1,\"shots\":[[0,\"a\"]]}"),
		TEXT("{\"format\":\"ginkgo.recoil\",\"version\":1,\"shots\":[[0,0]],\"loopFrom\":3}"),
		TEXT("{\"format\":\"ginkgo.recoil\",\"version\":1,\"shots\":[[0,0]],\"jitter\":[-1,0]}"),
	};
	for (const TCHAR* const Text : Bad)
	{
		Error.Reset();
		TestFalse(FString::Printf(TEXT("读不进来：%s"), Text), Pattern->ImportFromJson(Text, Error));
		TestFalse(FString::Printf(TEXT("有原因：%s"), Text), Error.IsEmpty());
		TestEqual(FString::Printf(TEXT("读不成时模板不变：%s"), Text), Pattern->Shots.Num(), 2);
	}
	return true;
}

#endif

#endif

#include "Camera/GinkgoCameraContextComponent.h"
#include "Camera/GinkgoSpring.h"
#include "Misc/AutomationTest.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoWeaponHoldTests
{
	/** 按固定帧长推进 Seconds 秒（最后一帧补齐），返回推进后的状态 */
	FGinkgoHoldState Run(FGinkgoHoldState State, const EGinkgoHoldStance Stance, const FGinkgoHoldTimings& Timings, const double Seconds, const double FrameTime)
	{
		double Elapsed = 0.0;
		while (Elapsed < Seconds - UE_KINDA_SMALL_NUMBER)
		{
			const double Step = FMath::Min(FrameTime, Seconds - Elapsed);
			Ginkgo::Weapon::StepHoldState(State, Stance, Timings, static_cast<float>(Step));
			Elapsed += Step;
		}
		return State;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponHoldStanceTest, "CaseGinkgo.Weapon.Hold.Stance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponHoldStanceTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	FGinkgoHoldInput Input;
	TestEqual(TEXT("没枪是徒手，按着开镜也是"), SelectStance([&] { FGinkgoHoldInput In = Input; In.bAimHeld = true; return In; }()), EGinkgoHoldStance::Unarmed);

	Input.bArmed = true;
	TestEqual(TEXT("拿着枪的常态是低持"), SelectStance(Input), EGinkgoHoldStance::Lowered);
	Input.HipHoldRemaining = 0.5f;
	TestEqual(TEXT("腰射还没留完是腰射"), SelectStance(Input), EGinkgoHoldStance::Hip);
	Input.bAimHeld = true;
	TestEqual(TEXT("按着开镜是开镜，腰射留的时间不管"), SelectStance(Input), EGinkgoHoldStance::Ads);
	Input.bGroundSprinting = true;
	TestEqual(TEXT("地面上冲刺打断开镜：枪放低"), SelectStance(Input), EGinkgoHoldStance::Sprint);
	Input.bBothHandsOnGun = true;
	TestEqual(TEXT("冲刺时拉栓、装填：两只手在枪上，不用单手举着，低持"), SelectStance(Input), EGinkgoHoldStance::Lowered);
	Input.bBothHandsOnGun = false;

	TestTrue(TEXT("腰射要抵肩"), WantsRaised(EGinkgoHoldStance::Hip));
	TestTrue(TEXT("开镜要抵肩"), WantsRaised(EGinkgoHoldStance::Ads));
	TestFalse(TEXT("低持不抵肩"), WantsRaised(EGinkgoHoldStance::Lowered));
	TestFalse(TEXT("冲刺不抵肩"), WantsRaised(EGinkgoHoldStance::Sprint));
	TestTrue(TEXT("只有开镜要开镜"), WantsAds(EGinkgoHoldStance::Ads) && !WantsAds(EGinkgoHoldStance::Hip));
	TestTrue(TEXT("只有冲刺要冲刺的持法"), WantsSprint(EGinkgoHoldStance::Sprint) && !WantsSprint(EGinkgoHoldStance::Lowered) && !WantsSprint(EGinkgoHoldStance::Unarmed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponHoldProgressTest, "CaseGinkgo.Weapon.Hold.Progress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponHoldProgressTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	using GinkgoWeaponHoldTests::Run;
	FGinkgoHoldTimings Timings;
	Timings.RaiseTime = 0.18f;
	Timings.LowerTime = 0.25f;
	Timings.AdsTime = 0.3f;
	Timings.HipHoldTime = 1.5f;

	// 抬枪按时间走：30 / 60 / 144 fps 都在 0.18 s 正好到满，之前一帧还没到（要等抬枪才能开火，到满的时刻要与帧率无关）
	for (const double Fps : { 30.0, 60.0, 144.0 })
	{
		const FString Label = FString::Printf(TEXT("%.0f fps"), Fps);
		const FGinkgoHoldState Raised = Run(FGinkgoHoldState(), EGinkgoHoldStance::Hip, Timings, 0.18, 1.0 / Fps);
		TestTrue(Label + TEXT("：0.18 s 抬到位"), Raised.IsRaised());
		const FGinkgoHoldState Almost = Run(FGinkgoHoldState(), EGinkgoHoldStance::Hip, Timings, 0.18 - 0.5 / Fps, 1.0 / Fps);
		TestFalse(Label + TEXT("：差半帧还没抬到位"), Almost.IsRaised());
		TestEqual(Label + TEXT("：腰射不开镜"), Raised.AdsProgress, 0.f);
	}

	// 从低持直接按开镜：抬枪与开镜一起走，抬枪先到
	const FGinkgoHoldState Mid = Run(FGinkgoHoldState(), EGinkgoHoldStance::Ads, Timings, 0.18, 1.0 / 60.0);
	TestTrue(TEXT("0.18 s 时抬到位了"), Mid.IsRaised());
	TestEqual(TEXT("开镜走了 0.18 / 0.3"), Mid.AdsProgress, 0.6f, 1e-4f);
	const FGinkgoHoldState Aimed = Run(Mid, EGinkgoHoldStance::Ads, Timings, 0.12, 1.0 / 60.0);
	TestEqual(TEXT("0.3 s 时开镜满了"), Aimed.AdsProgress, 1.f);

	// 放低用放低的时间，开镜退出用开镜时间
	const FGinkgoHoldState Lowered = Run(Aimed, EGinkgoHoldStance::Lowered, Timings, 0.125, 1.0 / 60.0);
	TestEqual(TEXT("放低 0.125 s 走了一半（放低时间 0.25 s）"), Lowered.RaiseProgress, 0.5f, 1e-4f);
	TestEqual(TEXT("开镜退出 0.125 s 剩 1 − 0.125 / 0.3"), Lowered.AdsProgress, 1.f - 0.125f / 0.3f, 1e-4f);
	const FGinkgoHoldState Sprinting = Run(Aimed, EGinkgoHoldStance::Sprint, Timings, 1.0, 1.0 / 60.0);
	TestTrue(TEXT("冲刺时两个都收回 0"), Sprinting.RaiseProgress == 0.f && Sprinting.AdsProgress == 0.f);

	// 冲刺进度：进出冲刺都按冲刺时间走，30 / 60 / 144 fps 都在 0.2 s 正好到满（冲刺的持枪循环、左手放开枪按它）
	Timings.SprintTime = 0.2f;
	for (const double Fps : { 30.0, 60.0, 144.0 })
	{
		const FString Label = FString::Printf(TEXT("%.0f fps"), Fps);
		TestEqual(Label + TEXT("：0.2 s 冲刺进度到满"), Run(FGinkgoHoldState(), EGinkgoHoldStance::Sprint, Timings, 0.2, 1.0 / Fps).SprintProgress, 1.f);
		TestTrue(Label + TEXT("：差半帧还没满"), Run(FGinkgoHoldState(), EGinkgoHoldStance::Sprint, Timings, 0.2 - 0.5 / Fps, 1.0 / Fps).SprintProgress < 1.f);
	}
	TestEqual(TEXT("冲刺了 1 s：冲刺进度满"), Sprinting.SprintProgress, 1.f);
	const FGinkgoHoldState SprintToAds = Run(Sprinting, EGinkgoHoldStance::Ads, Timings, 0.1, 1.0 / 60.0);
	TestEqual(TEXT("松开冲刺接着开镜：冲刺进度 0.1 s 回一半"), SprintToAds.SprintProgress, 0.5f, 1e-4f);
	TestTrue(TEXT("同时抬枪、开镜往上走"), SprintToAds.RaiseProgress > 0.5f && SprintToAds.AdsProgress > 0.3f);
	TestEqual(TEXT("别的姿态收回 0"), Run(Sprinting, EGinkgoHoldStance::Lowered, Timings, 1.0, 1.0 / 60.0).SprintProgress, 0.f);

	// 腰射留的时间往下减，不减到负
	FGinkgoHoldState Hip;
	Hip.HipHoldRemaining = Timings.HipHoldTime;
	Hip = Run(Hip, EGinkgoHoldStance::Hip, Timings, 1.0, 1.0 / 30.0);
	TestEqual(TEXT("留的时间过了 1 s 剩 0.5 s"), Hip.HipHoldRemaining, 0.5f, 1e-4f);
	Hip = Run(Hip, EGinkgoHoldStance::Hip, Timings, 2.0, 1.0 / 30.0);
	TestEqual(TEXT("留的时间不减到负"), Hip.HipHoldRemaining, 0.f);

	// 时间 ≤ 0 时直接到
	TestEqual(TEXT("抬枪时间为 0 时一帧到满"), StepProgress(0.f, true, 0.f, 0.2f, 0.001f), 1.f);
	TestEqual(TEXT("放低时间为 0 时一帧到 0"), StepProgress(1.f, false, 0.2f, 0.f, 0.001f), 0.f);

	// 两头缓：端点不动、中点对称、中间单调
	TestEqual(TEXT("缓：0 → 0"), EaseProgress(0.f), 0.f);
	TestEqual(TEXT("缓：1 → 1"), EaseProgress(1.f), 1.f);
	TestEqual(TEXT("缓：0.5 → 0.5"), EaseProgress(0.5f), 0.5f, 1e-6f);
	TestTrue(TEXT("缓：0.25 时慢于线性"), EaseProgress(0.25f) < 0.25f);
	TestEqual(TEXT("缓：超出的夹住"), EaseProgress(1.5f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponHandTargetsTest, "CaseGinkgo.Weapon.Hold.HandTargets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponHandTargetsTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 四个姿势里手在枪上的变换，同层里的姿势一样按三个进度混：低持 → 冲刺 → 腰射 → 开镜（Docs/Design/Weapons.md「手与瞄准」「持枪循环」）
	const auto Make = [](const double X, const double Yaw)
	{
		FGinkgoWeaponHandTargets Hands;
		Hands.Left = FTransform(FRotator(0.0, Yaw, 0.0), FVector(X, 0.0, 0.0));
		Hands.Right = FTransform(FRotator(0.0, -Yaw, 0.0), FVector(-X, 0.0, 0.0));
		return Hands;
	};
	const FGinkgoWeaponHandTargets Lowered = Make(40.0, 0.0);
	const FGinkgoWeaponHandTargets Sprint = Make(20.0, -30.0);
	const FGinkgoWeaponHandTargets Hip = Make(30.0, 20.0);
	const FGinkgoWeaponHandTargets Aim = Make(36.0, 40.0);
	const auto Same = [this](const TCHAR* What, const FGinkgoWeaponHandTargets& Actual, const FGinkgoWeaponHandTargets& Expected)
	{
		TestTrue(What, Actual.Left.Equals(Expected.Left, 1e-4) && Actual.Right.Equals(Expected.Right, 1e-4));
	};
	Same(TEXT("三个进度为 0 时是低持的"), BlendHandTargets(Lowered, Sprint, Hip, Aim, 0.f, 0.f, 0.f), Lowered);
	Same(TEXT("冲刺满时是冲刺的"), BlendHandTargets(Lowered, Sprint, Hip, Aim, 1.f, 0.f, 0.f), Sprint);
	Same(TEXT("抬枪满、开镜为 0 时是腰射的，不管冲刺进度"), BlendHandTargets(Lowered, Sprint, Hip, Aim, 0.6f, 1.f, 0.f), Hip);
	Same(TEXT("开镜满时是开镜的，不管抬枪进度"), BlendHandTargets(Lowered, Sprint, Hip, Aim, 0.f, 0.3f, 1.f), Aim);
	const FGinkgoWeaponHandTargets Half = BlendHandTargets(Lowered, Sprint, Hip, Aim, 0.f, 0.5f, 0.f);
	TestEqual(TEXT("一半时位置在中间"), Half.Left.GetLocation().X, 35.0, 1e-4);
	TestEqual(TEXT("一半时旋转也在中间（球面插值）"), Half.Left.Rotator().Yaw, 10.0, 1e-3);
	const FGinkgoWeaponHandTargets FromSprint = BlendHandTargets(Lowered, Sprint, Hip, Aim, 0.5f, 0.f, 0.f);
	TestEqual(TEXT("冲刺进度一半时在低持与冲刺中间"), FromSprint.Right.GetLocation().X, -30.0, 1e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponPickupSettleTest, "CaseGinkgo.Weapon.Hold.PickupSettle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponPickupSettleTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 捡枪的收拢（Docs/Design/Interaction.md「丢枪与捡枪」）。角色朝 +X（身体的坐标就是这个坐标）；地上的枪侧躺、枪口朝右，持枪时枪口朝左前下：
	// 两个朝向差不多反着，球面插值的最短那条从头顶往后翻（MG3 的 pickup 路线里枪口甩到肩后，再一帧翻 100°）
	const FQuat Body = FQuat::Identity;
	FGinkgoPickupSettleStart Start;
	Start.GunRotation = FRotator(0.0, 90.0, 90.0).Quaternion();
	Start.HandRotation = FRotator(-60.0, 20.0, 10.0).Quaternion();
	Start.ItemInHand = FVector(-7.82, 5.95, -0.87);
	const FTransform Hand(Start.HandRotation, FVector(30.0, 15.0, 20.0));
	const FTransform Gun(FRotator(-35.0, -50.0, 0.0), FVector(20.0, 10.0, 120.0));
	const FTransform GunHand(FRotator(10.0, -80.0, 30.0), FVector(-3.0, 2.0, -6.0));
	const FVector Held = GunHand.TransformPosition(Start.ItemInHand);
	const auto Settle = [&](const float Alpha) { return BlendPickupSettle(Start, Body, Hand, Gun, GunHand, Alpha); };

	const FGinkgoPickupSettlePose First = Settle(0.f);
	TestTrue(TEXT("开始时右手就是抓住时的手，不拉它"), First.RightHand.Equals(Hand, 1e-3));
	TestTrue(TEXT("开始时枪是地上的朝向"), First.Gun.GetRotation().Equals(Start.GunRotation, 1e-5));
	TestTrue(TEXT("开始时枪上被握的那一点在手的挂点上"), First.Gun.TransformPosition(Held).Equals(Hand.TransformPosition(Start.ItemInHand), 1e-3));

	const FGinkgoPickupSettlePose Last = Settle(1.f);
	TestTrue(TEXT("结束时枪就是持枪的"), Last.Gun.Equals(Gun, 1e-3));
	TestTrue(TEXT("结束时右手就是持枪的握法"), Last.RightHand.Equals(GunHand * Gun, 1e-3));
	TestTrue(TEXT("进度出了 0–1 夹回来"), Settle(1.4f).Gun.Equals(Gun, 1e-3));

	const FGinkgoPickupSettlePose Half = Settle(0.5f);
	TestTrue(TEXT("那一点直线走：一半时在两头中间"),
		Half.Gun.TransformPosition(Held).Equals(0.5 * (Hand.TransformPosition(Start.ItemInHand) + Gun.TransformPosition(Held)), 1e-3));

	// 一路扫过去：枪口不朝后、不朝上，相邻两步不跳（一共转 150° 上下，一百步每步一两度），手的挂点一直在被握的那一点
	double MaxGunStep = 0.0;
	double MaxHandStep = 0.0;
	double MaxHandMove = 0.0;
	double MinForward = 1.0;
	double MaxUp = -1.0;
	double MaxGap = 0.0;
	FGinkgoPickupSettlePose Previous = First;
	for (int32 Step = 1; Step <= 100; ++Step)
	{
		const FGinkgoPickupSettlePose Pose = Settle(Step / 100.f);
		const FVector Bore = Pose.Gun.GetUnitAxis(EAxis::X);
		MinForward = FMath::Min(MinForward, Bore.X);
		MaxUp = FMath::Max(MaxUp, Bore.Z);
		MaxGunStep = FMath::Max(MaxGunStep, FMath::RadiansToDegrees(Pose.Gun.GetRotation().AngularDistance(Previous.Gun.GetRotation())));
		MaxHandStep = FMath::Max(MaxHandStep, FMath::RadiansToDegrees(Pose.RightHand.GetRotation().AngularDistance(Previous.RightHand.GetRotation())));
		MaxHandMove = FMath::Max(MaxHandMove, FVector::Dist(Pose.RightHand.GetLocation(), Previous.RightHand.GetLocation()));
		MaxGap = FMath::Max(MaxGap, FVector::Dist(Pose.RightHand.TransformPosition(Start.ItemInHand), Pose.Gun.TransformPosition(Held)));
		Previous = Pose;
	}
	TestTrue(FString::Printf(TEXT("枪口从身前扫过去，不朝后（枪管朝前的分量最小 %.3f）"), MinForward), MinForward > -1e-3);
	TestTrue(FString::Printf(TEXT("枪口不往上甩（枪管朝上的分量最大 %.3f）"), MaxUp), MaxUp < 1e-3);
	TestTrue(FString::Printf(TEXT("枪的朝向相邻两步不跳（最大 %.2f°）"), MaxGunStep), MaxGunStep < 3.0);
	TestTrue(FString::Printf(TEXT("右手的朝向相邻两步不跳（最大 %.2f°）"), MaxHandStep), MaxHandStep < 4.0);
	TestTrue(FString::Printf(TEXT("右手相邻两步不跳（最大 %.2f cm）"), MaxHandMove), MaxHandMove < 3.0);
	TestTrue(FString::Printf(TEXT("手的挂点一直在被握的那一点（最大差 %.4f cm）"), MaxGap), MaxGap < 1e-2);

	// 转的路：枪口从右后方转到左前方，走身前（偏航 170° → −50°，中间是 60°），不走最短的身后那条
	const FQuat BackRight = FRotator(0.0, 170.0, 0.0).Quaternion();
	const FQuat FrontLeft = FRotator(-20.0, -50.0, 0.0).Quaternion();
	TestEqual(TEXT("走身前：一半时偏航 60°"), BlendTurnInFront(Body, BackRight, FrontLeft, 0.5f).Rotator().Yaw, 60.0, 1e-3);
	TestTrue(TEXT("最短的那条走身后（对照）"), FQuat::Slerp(BackRight, FrontLeft, 0.5f).GetAxisX().X < 0.0);
	// 只看相对角色的：角色转了，路跟着转
	const FQuat Turned = FRotator(0.0, 35.0, 0.0).Quaternion();
	TestTrue(TEXT("角色转了，转的路跟着转"),
		BlendTurnInFront(Turned, Turned * BackRight, Turned * FrontLeft, 0.3f).Equals(Turned * BlendTurnInFront(Body, BackRight, FrontLeft, 0.3f), 1e-5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponBarrelHeatTest, "CaseGinkgo.Weapon.Hold.BarrelHeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponBarrelHeatTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 枪管的热（Docs/Design/Weapons.md「枪管的热」）：牛顿冷却，同帧长无关
	TestEqual(TEXT("过一个时间常数剩 1/e"), CoolBarrel(100.f, 15.f, 15.f), static_cast<float>(100.0 / UE_EULERS_NUMBER), 1e-3f);
	float Stepped = 100.f;
	for (int32 Frame = 0; Frame < 144; ++Frame)
	{
		Stepped = CoolBarrel(Stepped, 15.f, 1.f / 144.f);
	}
	TestEqual(TEXT("144 帧凉 1 s 同一次凉 1 s 一样"), Stepped, CoolBarrel(100.f, 15.f, 1.f), 1e-3f);
	TestEqual(TEXT("帧长是负的不加热"), CoolBarrel(50.f, 15.f, -1.f), 50.f, 1e-6f);

	// 热浪：Start 以下只有开枪那一下的热气，Full 满，中间两头缓过，加起来夹到 1
	TestEqual(TEXT("凉的枪管没有热浪"), HeatHazeAlpha(10.f, 20.f, 110.f, 0.f), 0.f, 1e-6f);
	TestEqual(TEXT("凉的枪管开一枪只有那一下"), HeatHazeAlpha(10.f, 20.f, 110.f, 0.3f), 0.3f, 1e-6f);
	TestEqual(TEXT("Start 与 Full 中间一半"), HeatHazeAlpha(65.f, 20.f, 110.f, 0.f), 0.5f, 1e-4f);
	TestEqual(TEXT("Full 以上满"), HeatHazeAlpha(300.f, 20.f, 110.f, 0.f), 1.f, 1e-6f);
	TestEqual(TEXT("加上那一下也不过 1"), HeatHazeAlpha(100.f, 20.f, 110.f, 0.5f), 1.f, 1e-6f);
	TestEqual(TEXT("Full 不比 Start 大：过了 Start 就满"), HeatHazeAlpha(25.f, 20.f, 20.f, 0.f), 1.f, 1e-6f);

	// 默认的数（AK 的薄枪管）按射手们说的：连着打 10–15 发才看得见，隔着瞄的几枪没有，一个弹匣打空很明显，半分钟内凉下去
	const UGinkgoWeaponDefinition* Defaults = GetDefault<UGinkgoWeaponDefinition>();
	const auto Fire = [Defaults](const int32 Shots, const float Interval, float Heat)
	{
		for (int32 Shot = 0; Shot < Shots; ++Shot)
		{
			Heat = CoolBarrel(Heat, Defaults->HeatCoolTime, Interval) + Defaults->HeatPerShot;
		}
		return Heat;
	};
	const auto Haze = [Defaults](const float Heat) { return HeatHazeAlpha(Heat, Defaults->HeatHazeStart, Defaults->HeatHazeFull, 0.f); };
	TestEqual(TEXT("隔 1.5 s 瞄着打三枪没有枪管的热浪"), Haze(Fire(3, 1.5f, 0.f)), 0.f, 1e-6f);
	TestTrue(FString::Printf(TEXT("一秒十发打 15 发看得见（%.2f）"), Haze(Fire(15, 0.1f, 0.f))), Haze(Fire(15, 0.1f, 0.f)) > 0.1f);
	const float Dumped = Fire(30, 0.1f, 0.f);
	TestTrue(FString::Printf(TEXT("一个弹匣 30 发打空很明显（%.2f）"), Haze(Dumped)), Haze(Dumped) > 0.6f);
	TestEqual(TEXT("打空以后 30 s 枪管的热浪没了"), Haze(CoolBarrel(Dumped, Defaults->HeatCoolTime, 30.f)), 0.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponRootStateTest, "CaseGinkgo.Weapon.States.RootState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponRootStateTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	TestEqual(TEXT("手上没枪也不要拿：徒手"), SelectRootState(INDEX_NONE, INDEX_NONE), EGinkgoWeaponRootState::Unarmed);
	TestEqual(TEXT("手上没枪要拿：拔枪"), SelectRootState(INDEX_NONE, 1), EGinkgoWeaponRootState::Draw);
	TestEqual(TEXT("手上就是目标：持枪"), SelectRootState(1, 1), EGinkgoWeaponRootState::Armed);
	TestEqual(TEXT("要徒手：收枪"), SelectRootState(0, INDEX_NONE), EGinkgoWeaponRootState::Stow);
	TestEqual(TEXT("要换枪：先收枪"), SelectRootState(0, 1), EGinkgoWeaponRootState::Stow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponKeysTest, "CaseGinkgo.Weapon.States.WeaponKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponKeysTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	const TArray<bool> Both = {true, true};
	const TArray<bool> FirstOnly = {true, false};
	const TArray<bool> SecondOnly = {false, true};
	const TArray<bool> None = {false, false};

	// 收枪键（文档「收枪键」那张表）
	TestEqual(TEXT("手上有枪：收到背上"), ApplyWeaponKey(EGinkgoWeaponKey::Holster, 1, 1, Both), INDEX_NONE);
	TestEqual(TEXT("徒手：拔上一次的"), ApplyWeaponKey(EGinkgoWeaponKey::Holster, INDEX_NONE, 1, Both), 1);
	TestEqual(TEXT("上一次的丢了：拔第一个有枪的"), ApplyWeaponKey(EGinkgoWeaponKey::Holster, INDEX_NONE, 1, FirstOnly), 0);
	TestEqual(TEXT("还没拿过：拔第一个有枪的"), ApplyWeaponKey(EGinkgoWeaponKey::Holster, INDEX_NONE, INDEX_NONE, SecondOnly), 1);
	TestEqual(TEXT("一把枪也没有：还是徒手"), ApplyWeaponKey(EGinkgoWeaponKey::Holster, INDEX_NONE, 0, None), INDEX_NONE);
	// 放到一半再按：改的是放完以后去哪，连按两下回到原来的
	const int32 Pressed = ApplyWeaponKey(EGinkgoWeaponKey::Holster, 0, 0, Both);
	TestEqual(TEXT("收枪放到一半再按：放完拔回来"), ApplyWeaponKey(EGinkgoWeaponKey::Holster, Pressed, 0, Both), 0);

	// 第一把、第二把
	TestEqual(TEXT("那个槽有枪：去那个槽"), ApplyWeaponKey(EGinkgoWeaponKey::Slot2, 0, 0, Both), 1);
	TestEqual(TEXT("手上就是这把：不响应"), ApplyWeaponKey(EGinkgoWeaponKey::Slot1, 0, 0, Both), 0);
	TestEqual(TEXT("槽空着：不响应"), ApplyWeaponKey(EGinkgoWeaponKey::Slot2, 0, 0, FirstOnly), 0);
	TestEqual(TEXT("徒手：拔那把"), ApplyWeaponKey(EGinkgoWeaponKey::Slot2, INDEX_NONE, 0, Both), 1);
	TestEqual(TEXT("徒手、槽空着：还是徒手"), ApplyWeaponKey(EGinkgoWeaponKey::Slot2, INDEX_NONE, 0, FirstOnly), INDEX_NONE);

	// 切枪
	TestEqual(TEXT("换另一个槽"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, 0, 0, Both), 1);
	TestEqual(TEXT("换回来"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, 1, 1, Both), 0);
	TestEqual(TEXT("只有这一把：不响应"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, 0, 0, FirstOnly), 0);
	TestEqual(TEXT("徒手：拔上一次的"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, INDEX_NONE, 1, Both), 1);

	TestEqual(TEXT("FindDrawSlot：上一次的"), FindDrawSlot(1, Both), 1);
	TestEqual(TEXT("FindDrawSlot：一把也没有"), FindDrawSlot(0, None), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponActionTest, "CaseGinkgo.Weapon.States.SelectAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponActionTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 枪上是什么，就选什么（文档「枪记住什么」那张表）
	FGinkgoWeaponActionFacts Loaded;
	Loaded.bCanFire = true;
	Loaded.bFeedHasRounds = true;
	TestEqual(TEXT("上好膛，不按键：就绪"), SelectWeaponAction(Loaded), EGinkgoWeaponAction::Ready);

	FGinkgoWeaponActionFacts Wants = Loaded;
	Wants.bWantsFire = true;
	TestEqual(TEXT("上好膛，想开火：开火"), SelectWeaponAction(Wants), EGinkgoWeaponAction::Fire);

	FGinkgoWeaponActionFacts Spent;
	Spent.bChamberSpent = true;
	Spent.bFeedHasRounds = true;
	TestEqual(TEXT("开火以后、抛壳之前：接着拉栓"), SelectWeaponAction(Spent), EGinkgoWeaponAction::CycleBolt);
	Spent.bWantsFire = true;
	TestEqual(TEXT("拉栓之前想开火：先拉栓，意图留着"), SelectWeaponAction(Spent), EGinkgoWeaponAction::CycleBolt);
	Spent.bFeedHasRounds = false;
	TestEqual(TEXT("最后一发打完：也拉栓，把空壳抛出去"), SelectWeaponAction(Spent), EGinkgoWeaponAction::CycleBolt);

	FGinkgoWeaponActionFacts Open;
	Open.bActionOpen = true;
	Open.bFeedHasRounds = true;
	TestEqual(TEXT("抛壳以后、推到底之前，弹仓有弹：自动闭栓（你定的）"), SelectWeaponAction(Open), EGinkgoWeaponAction::CloseBolt);
	Open.bWantsFire = true;
	TestEqual(TEXT("同上，想开火：先闭栓"), SelectWeaponAction(Open), EGinkgoWeaponAction::CloseBolt);
	Open.bFeedHasRounds = false;
	TestEqual(TEXT("弹仓空了，想开火：咔"), SelectWeaponAction(Open), EGinkgoWeaponAction::DryFire);
	Open.bWantsFire = false;
	TestEqual(TEXT("弹仓空了，栓开着等装填"), SelectWeaponAction(Open), EGinkgoWeaponAction::Ready);

	FGinkgoWeaponActionFacts Empty;
	Empty.bWantsFire = true;
	TestEqual(TEXT("膛空、栓关着，想开火：咔"), SelectWeaponAction(Empty), EGinkgoWeaponAction::DryFire);

	// 装填（文档「每种枪机一棵子树」的顺序）
	FGinkgoWeaponActionFacts Reload = Loaded;
	Reload.bWantsReload = true;
	Reload.bCanLoad = true;
	TestEqual(TEXT("栓关着，想装填：开栓"), SelectWeaponAction(Reload), EGinkgoWeaponAction::OpenBolt);
	Reload.bCanFire = false;
	Reload.bChamberSpent = true;
	TestEqual(TEXT("膛里空壳，想装填：不用拉栓，开栓就抛了"), SelectWeaponAction(Reload), EGinkgoWeaponAction::OpenBolt);
	Reload.bChamberSpent = false;
	Reload.bActionOpen = true;
	TestEqual(TEXT("栓开着，手不在拉柄上（拔枪后接着压）：起手"), SelectWeaponAction(Reload), EGinkgoWeaponAction::ReloadStart);
	Reload.bHandAtAction = true;
	TestEqual(TEXT("手在拉柄上：压一发，弹仓有弹也不闭栓"), SelectWeaponAction(Reload), EGinkgoWeaponAction::LoadRound);
	Reload.bCanLoad = false;
	TestEqual(TEXT("压满了、没子弹：闭栓"), SelectWeaponAction(Reload), EGinkgoWeaponAction::CloseBolt);
	Reload.bCanLoad = true;
	Reload.bWantsReload = false;
	Reload.bWantsFire = true;
	TestEqual(TEXT("按了开火：闭栓再开火"), SelectWeaponAction(Reload), EGinkgoWeaponAction::CloseBolt);
	Reload.bFeedHasRounds = false;
	TestEqual(TEXT("弹仓空的，按了开火：咔"), SelectWeaponAction(Reload), EGinkgoWeaponAction::DryFire);
	Reload.bWantsFire = false;
	Reload.bWantsReload = true;
	Reload.bHandAtAction = false;
	TestEqual(TEXT("弹仓空的，栓开着：接着压，先起手"), SelectWeaponAction(Reload), EGinkgoWeaponAction::ReloadStart);
	FGinkgoWeaponActionFacts Useless = Loaded;
	Useless.bWantsReload = true;
	Useless.bCanLoad = false;
	TestEqual(TEXT("压不了的装填不算"), SelectWeaponAction(Useless), EGinkgoWeaponAction::Ready);

	FGinkgoWeaponActionFacts Busy = Spent;
	Busy.bHandsFree = false;
	TestEqual(TEXT("比它高的占着：等放开，不反复重试"), SelectWeaponAction(Busy), EGinkgoWeaponAction::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponGunKickTest, "CaseGinkgo.Weapon.Hold.GunKick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponGunKickTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	const FGinkgoGunKick Kick;
	// 从开枪走到某一刻，帧长不同
	const auto Run = [&Kick](const double Seconds, const double Frame)
	{
		FGinkgoGunKickState State;
		AddGunKick(State, Kick, 1.f, -1.f);
		const int32 Steps = FMath::RoundToInt(Seconds / Frame);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			StepGunKick(State, Kick, static_cast<float>(Frame));
		}
		return State;
	};
	// 写的是峰值：细的步长走一遍，最大的就是它
	FGinkgoGunKickState Fine;
	AddGunKick(Fine, Kick, 1.f, -1.f);
	double MaxBack = 0.0, MaxClimb = 0.0, MaxYaw = 0.0, MinRoll = 0.0;
	for (int32 Index = 0; Index < 4000; ++Index)
	{
		StepGunKick(Fine, Kick, 0.00025f);
		MaxBack = FMath::Max(MaxBack, Fine.Back);
		MaxClimb = FMath::Max(MaxClimb, Fine.Rotation.Pitch);
		MaxYaw = FMath::Max(MaxYaw, Fine.Rotation.Yaw);
		MinRoll = FMath::Min(MinRoll, Fine.Rotation.Roll);
	}
	TestEqual(TEXT("往后顶的峰值同数据"), MaxBack, static_cast<double>(Kick.Back), 1e-3);
	TestEqual(TEXT("上扬的峰值同数据"), MaxClimb, static_cast<double>(Kick.Climb), 1e-3);
	TestEqual(TEXT("左右的峰值乘这一枪的随机"), MaxYaw, static_cast<double>(Kick.Yaw), 1e-3);
	TestEqual(TEXT("滚转是负的"), MinRoll, -static_cast<double>(Kick.Roll), 1e-3);
	TestTrue(TEXT("1 s 以后回到 0"), FMath::Abs(Fine.Rotation.Pitch) < 0.01 && FMath::Abs(Fine.Back) < 0.01);

	// 解析解：与帧长无关
	const FGinkgoGunKickState A = Run(0.1, 1.0 / 30.0);
	const FGinkgoGunKickState B = Run(0.1, 1.0 / 240.0);
	TestEqual(TEXT("30 fps 与 240 fps 走到同一刻，上扬相同"), A.Rotation.Pitch, B.Rotation.Pitch, 1e-3);
	TestEqual(TEXT("往后顶也相同"), A.Back, B.Back, 1e-3);

	// 变换：绕枪托转、往后顶
	FGinkgoGunKickState Static;
	TestTrue(TEXT("静止时是单位变换"), MakeGunKickTransform(Static, Kick).Equals(FTransform::Identity, 1e-6));
	Static.Rotation.Pitch = 5.0;
	Static.Back = 2.0;
	const FTransform Transform = MakeGunKickTransform(Static, Kick);
	const FVector Butt = Transform.TransformPosition(Kick.Pivot);
	TestEqual(TEXT("枪托只往后顶，不跟着转"), Butt, Kick.Pivot - FVector(2.0, 0.0, 0.0), 1e-3f);
	const FVector Muzzle = Transform.TransformPosition(FVector(80.0, 0.0, 0.0));
	TestTrue(TEXT("枪口往上抬"), Muzzle.Z > 5.0);
	TestEqual(TEXT("枪管方向上扬 5 度"), FMath::RadiansToDegrees(FMath::Asin(Transform.GetUnitAxis(EAxis::X).Z)), 5.0, 1e-3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponCueDefaultsTest, "CaseGinkgo.Weapon.Hold.CueDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponCueDefaultsTest::RunTest(const FString& Parameters)
{
	// 类默认值：六个时刻都配了声音；枪栓的声音都有提前量，开枪按下就响
	const UGinkgoWeaponDefinition* Defaults = GetDefault<UGinkgoWeaponDefinition>();
	TestEqual(TEXT("六个时刻"), Defaults->Cues.Num(), 6);
	for (const TPair<FGameplayTag, FGinkgoWeaponCue>& Cue : Defaults->Cues)
	{
		TestFalse(FString::Printf(TEXT("%s 配了声音"), *Cue.Key.ToString()), Cue.Value.Sound.IsNull());
		TestTrue(FString::Printf(TEXT("%s 的音量"), *Cue.Key.ToString()), Cue.Value.Volume > 0.f);
		TestEqual(FString::Printf(TEXT("%s 的提前量"), *Cue.Key.ToString()), Cue.Value.Lead > 0.f, Cue.Key != FGameplayTag(FWeaponTags::Weapon_Cue_Fire));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponHipAimTest, "CaseGinkgo.Weapon.Hold.HipAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponHipAimTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	constexpr double Omega = 20.0;
	constexpr double Zeta = 0.7;
	const auto Turn = [&](FGinkgoHipAimState& State, const FVector2D& Rate, const double Seconds, const double Fps, const double MaxLag = 0.0)
	{
		const int32 Frames = FMath::RoundToInt32(Seconds * Fps);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			StepHipAim(State, Rate / Fps, Omega, Zeta, MaxLag, 1.0 / Fps);
		}
	};

	// 匀速转：稳态落后 2ζΩ/ω，两个轴各算各的
	FGinkgoHipAimState Steady;
	Turn(Steady, FVector2D(180.0, -60.0), 2.0, 144.0);
	TestEqual(TEXT("180°/s：稳态落后 12.6°"), Steady.Lag.X, -12.6, 1e-6);
	TestEqual(TEXT("-60°/s：稳态落后 4.2°"), Steady.Lag.Y, 4.2, 1e-6);
	TestEqual(TEXT("稳态时瞄准方向同视线一样快"), Steady.Velocity.X, 180.0, 1e-6);

	// 停下：先回来，过了中心一点，再收住。阶跃的过冲是 exp(-πζ/√(1-ζ²)) = 4.6%
	FGinkgoHipAimState Stop;
	Stop.Lag = FVector2D(-10.0, 0.0);
	double Peak = 0.0;
	for (int32 Frame = 0; Frame < 1000; ++Frame)
	{
		StepHipAim(Stop, FVector2D::ZeroVector, Omega, Zeta, 0.0, 1.0 / 1000.0);
		Peak = FMath::Max(Peak, Stop.Lag.X);
	}
	TestEqual(TEXT("过冲 4.6%"), Peak, 10.0 * FMath::Exp(-UE_DOUBLE_PI * Zeta / FMath::Sqrt(1.0 - Zeta * Zeta)), 1e-3);
	TestTrue(TEXT("1 s 以后收住"), Stop.Lag.Size() < 1e-3);

	// 视线的路径一样时与帧率无关
	FGinkgoHipAimState Rates[3];
	const double FpsList[3] = {30.0, 60.0, 144.0};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Turn(Rates[Index], FVector2D(120.0, 40.0), 0.5, FpsList[Index]);
		Turn(Rates[Index], FVector2D::ZeroVector, 1.0 / 3.0, FpsList[Index]);
	}
	TestEqual(TEXT("30 fps 同 144 fps"), Rates[0].Lag.X, Rates[2].Lag.X, 1e-9);
	TestEqual(TEXT("60 fps 同 144 fps"), Rates[1].Lag.Y, Rates[2].Lag.Y, 1e-9);

	// 甩镜头：最多落后这么多，贴着上限时跟着视线走，视线停了回来
	FGinkgoHipAimState Flick;
	double Farthest = 0.0;
	for (int32 Frame = 0; Frame < 72; ++Frame)
	{
		StepHipAim(Flick, FVector2D(1000.0, 0.0) / 144.0, Omega, Zeta, 30.0, 1.0 / 144.0);
		Farthest = FMath::Max(Farthest, FMath::Abs(Flick.Lag.X));
	}
	TestEqual(TEXT("贴着上限"), Farthest, 30.0, 1e-9);
	TestEqual(TEXT("贴着上限时跟着视线走"), Flick.Velocity.X, 1000.0, 1e-6);
	Turn(Flick, FVector2D::ZeroVector, 1.0, 144.0, 30.0);
	TestTrue(TEXT("视线停了回到中心"), Flick.Lag.Size() < 1e-2);

	// 没有弹簧：瞄准方向就是视线
	FGinkgoHipAimState None;
	None.Lag = FVector2D(5.0, 5.0);
	StepHipAim(None, FVector2D(1.0, 0.0), 0.0, Zeta, 30.0, 1.0 / 60.0);
	TestTrue(TEXT("ω 为 0：没有偏差"), None.Lag.IsZero());

	// 开镜满了就是视线，中间按比例
	const FRotator View(10.0, 350.0, 0.0);
	const FVector2D Lag(-5.0, 2.0);
	TestTrue(TEXT("没开镜：全部偏差"), MakeHipAimRotation(View, Lag, 0.f).Equals(FRotator(12.0, 345.0, 0.0), 1e-6));
	TestTrue(TEXT("开镜一半：一半偏差"), MakeHipAimRotation(View, Lag, 0.5f).Equals(FRotator(11.0, 347.5, 0.0), 1e-6));
	TestTrue(TEXT("开镜满：就是视线"), MakeHipAimRotation(View, Lag, 1.f).Equals(View, 1e-6));
	TestEqual(TEXT("俯仰夹住，不翻过头顶"), MakeHipAimRotation(FRotator(89.0, 0.0, 0.0), FVector2D(0.0, 5.0), 0.f).Pitch, 89.9, 1e-6);
	TestEqual(TEXT("俯仰按 ±180 算"), MakeHipAimRotation(FRotator(350.0, 0.0, 0.0), FVector2D::ZeroVector, 0.f).Pitch, -10.0, 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponRoundVisualsTest, "CaseGinkgo.Weapon.Hold.RoundVisuals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponRoundVisualsTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	FGinkgoRoundVisuals Rounds;
	Rounds.FeedTop = FVector(19.78, 0.0, 5.45);
	Rounds.ChamberRise = 0.88f;
	Rounds.EjectDirection = FVector(0.0, 1.0, 0.0);
	Rounds.EjectSpeed = FFloatInterval(250.f, 350.f);
	Rounds.EjectSpread = 10.f;
	Rounds.EjectSpin = FFloatInterval(1500.f, 2600.f);
	FGinkgoBoltRig Bolt;
	Bolt.Stroke = 9.45f;

	TestEqual(TEXT("枪栓拉到底：躺在托弹板上"), MakeFeedRoundTransform(Rounds, Bolt, 1.f).GetLocation(), Rounds.FeedTop, 1e-6f);
	TestEqual(TEXT("枪栓推到底：进了膛，到枪管的轴"), MakeFeedRoundTransform(Rounds, Bolt, 0.f).GetLocation(), FVector(29.23, 0.0, 6.33), 1e-5f);
	TestEqual(TEXT("推到一半：走一半"), MakeFeedRoundTransform(Rounds, Bolt, 0.5f).GetLocation().X, 19.78 + 9.45 / 2.0, 1e-5);

	// 枪转了 90°（枪的右边朝世界的 -X）：速度跟着枪，角度散在 10° 以内，加上枪自己的速度
	const FQuat Gun(FVector::UpVector, UE_DOUBLE_HALF_PI);
	const FVector Forward = Gun.RotateVector(FVector::RightVector);
	const FVector GunVelocity(100.0, 0.0, 0.0);
	FRandomStream Random(7);
	double Farthest = 0.0;
	for (int32 Index = 0; Index < 200; ++Index)
	{
		const FVector Relative = SampleEjectVelocity(Rounds, Gun, GunVelocity, Random) - GunVelocity;
		const double Speed = Relative.Size();
		TestTrue(TEXT("速度在范围里"), Speed >= 249.9 && Speed <= 350.1);
		Farthest = FMath::Max(Farthest, FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Relative.GetSafeNormal(), Forward))));
		const double Spin = SampleEjectSpin(Rounds, Gun, Random).Size();
		TestTrue(TEXT("翻得多快在范围里"), Spin >= 1499.9 && Spin <= 2600.1);
	}
	TestTrue(TEXT("方向散在 10° 以内"), Farthest <= 10.01);

	// 撞一下的音量：不够快不出声，够了以后按平方根长到 1
	Rounds.HitSpeed = FFloatInterval(45.f, 700.f);
	Rounds.HitQuietVolume = 0.2f;
	TestEqual(TEXT("贴着地滚：没有声音"), ShellHitVolume(Rounds, 10.0), 0.f);
	TestEqual(TEXT("刚够出声"), ShellHitVolume(Rounds, 45.0), 0.2f);
	TestEqual(TEXT("撞得最重：音量满"), ShellHitVolume(Rounds, 700.0), 1.f);
	TestEqual(TEXT("再重也是满的"), ShellHitVolume(Rounds, 2000.0), 1.f);
	TestEqual(TEXT("速度的四分之一处：一半的响度范围"), ShellHitVolume(Rounds, 45.0 + 655.0 / 4.0), 0.6f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponHitMarkTest, "CaseGinkgo.Weapon.Hold.HitMark",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponHitMarkTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	TestEqual(TEXT("重合：不画"), HitMarkScale(0.0), 0.f);
	TestEqual(TEXT("正常瞄准时最大的差：不画"), HitMarkScale(6.4), 0.f);
	TestEqual(TEXT("门槛上：不画"), HitMarkScale(8.0), 0.f);
	TestEqual(TEXT("中间：一半大"), HitMarkScale(12.0), 0.5f);
	TestEqual(TEXT("差得多：画满"), HitMarkScale(16.0), 1.f);
	TestEqual(TEXT("枪口被挡：画满"), HitMarkScale(150.0), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMuzzleWispTest, "CaseGinkgo.Weapon.Hold.MuzzleWisp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMuzzleWispTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	TestEqual(TEXT("刚开枪：枪上配的浓度"), MuzzleWispAlpha(1.f, 0.8f), 0.8f);
	TestEqual(TEXT("还剩四分之一：先冒得足，后面越来越细"), MuzzleWispAlpha(0.25f, 0.8f), 0.1f);
	TestEqual(TEXT("冒完：没有了"), MuzzleWispAlpha(0.f, 0.8f), 0.f);
	TestEqual(TEXT("超出范围：夹住"), MuzzleWispAlpha(1.5f, 0.8f), 0.8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponBoltPoseTest, "CaseGinkgo.Weapon.Hold.BoltPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponBoltPoseTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 不放片段时读机构状态，放片段时读曲线，淡入淡出时在两者之间
	const FGinkgoBoltPose Clip{0.25f, 0.75f};
	TestTrue(TEXT("枪机关着、不放片段：关着压下"), BlendBoltPose(false, 0.f, Clip) == FGinkgoBoltPose{0.f, 0.f});
	TestTrue(TEXT("枪机开着、不放片段：拉到底、抬到头"), BlendBoltPose(true, 0.f, Clip) == FGinkgoBoltPose{1.f, 1.f});
	TestTrue(TEXT("片段比重满：就是片段的"), BlendBoltPose(true, 1.f, Clip) == Clip);
	TestTrue(TEXT("比重超过 1 按 1"), BlendBoltPose(false, 1.5f, Clip) == Clip);
	const FGinkgoBoltPose Half = BlendBoltPose(true, 0.5f, Clip);
	TestEqual(TEXT("淡入淡出：滑动在两者之间"), Half.Travel, 0.625f, 1e-6f);
	TestEqual(TEXT("淡入淡出：抬柄在两者之间"), Half.Lift, 0.875f, 1e-6f);

	// 98k 的尺寸（rifle_skeletal.py 写的）：枪管的轴 (12.31, 0, 6.335)，球头关着时 (12.134, 3.251, 3.122)，都是枪的空间。骨骼的参考旋转不影响结果
	const FGinkgoBoltRig Rig;
	const FVector Bore(12.31, 0.0, 6.335);
	const FVector Knob(12.134, 3.251, 3.122);
	const FTransform CarrierRef(Bore);
	const FTransform HandleRef(FQuat(FVector(0.3, 0.5, 0.8).GetSafeNormal(), 1.1), Bore);
	const FVector KnobInHandle = HandleRef.InverseTransformPosition(Knob);
	const auto KnobAt = [&](const FGinkgoBoltPose& Pose) { return MakeBoltHandleTransform(HandleRef, Rig, Pose).TransformPosition(KnobInHandle); };
	TestEqual(TEXT("关着：滑动的骨骼不动"), MakeBoltCarrierTransform(CarrierRef, Rig, FGinkgoBoltPose()).GetLocation(), Bore, 1e-6f);
	TestEqual(TEXT("关着：球头不动"), KnobAt(FGinkgoBoltPose()), Knob, 1e-6f);
	TestEqual(TEXT("拉到底：滑动的骨骼往后 9.45 cm"), MakeBoltCarrierTransform(CarrierRef, Rig, {1.f, 1.f}).GetLocation(), Bore - FVector(9.45, 0.0, 0.0), 1e-5f);
	TestEqual(TEXT("抬到头、没拉：球头绕枪管转到上面，同一边"), KnobAt({0.f, 1.f}), FVector(12.134, 3.213, 9.586), 1e-5f);
	TestEqual(TEXT("拉到底、抬到头：球头再往后 9.45 cm"), KnobAt({1.f, 1.f}), FVector(2.684, 3.213, 9.586), 1e-5f);
	const double Radius = FVector2D(3.251, 3.122 - 6.335).Size();
	const FVector Mid = KnobAt({0.f, 0.5f});
	TestEqual(TEXT("抬到一半：离枪管的轴一样远"), FVector2D(Mid.Y, Mid.Z - 6.335).Size(), Radius, 1e-5);
	TestEqual(TEXT("抬到一半：转了 45°"), FMath::RadiansToDegrees(FMath::Atan2(Mid.Y, -(Mid.Z - 6.335))), FMath::RadiansToDegrees(FMath::Atan2(3.251, 6.335 - 3.122)) + 45.0, 1e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponCameraRecoilTest, "CaseGinkgo.Weapon.Hold.CameraRecoil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponCameraRecoilTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 从冲量走一遍，取最大的
	const auto PeakOf = [](const double Impulse, const double Omega, const double Zeta)
	{
		double Value = 0.0;
		double Velocity = Impulse;
		double Peak = 0.0;
		for (int32 Index = 0; Index < 4000; ++Index)
		{
			Ginkgo::Spring::StepDamped(Value, Velocity, Omega, Zeta, 0.00025);
			Peak = FMath::Abs(Value) > FMath::Abs(Peak) ? Value : Peak;
		}
		return Peak;
	};

	// 写的是峰值：范围写成一样就是固定的，取到的数走出来就是它，改弹簧不改幅度
	FGinkgoCameraRecoil Fixed;
	Fixed.Pitch = FFloatInterval(2.f, 2.f);
	Fixed.Yaw = FFloatInterval(-0.5f, -0.5f);
	Fixed.Roll = FFloatInterval(1.f, 1.f);
	Fixed.FieldOfView = FFloatInterval(1.5f, 1.5f);
	for (const float Omega : {14.f, 50.f})
	{
		Fixed.Omega = FFloatInterval(Omega, Omega);
		Fixed.Zeta = FFloatInterval(0.4f, 0.4f);
		const FRandomStream Random(1);
		const FGinkgoCameraImpulse Impulse = SampleCameraRecoil(Fixed, Random);
		TestEqual(TEXT("俯仰的峰值同配置"), PeakOf(Impulse.Rotation.Pitch, Omega, 0.4), 2.0, 1e-3);
		TestEqual(TEXT("偏航带符号：不随机往一边时就是配置的那一边"), PeakOf(Impulse.Rotation.Yaw, Omega, 0.4), -0.5, 1e-3);
		TestEqual(TEXT("侧倾的峰值同配置"), PeakOf(Impulse.Rotation.Roll, Omega, 0.4), 1.0, 1e-3);
		TestEqual(TEXT("视场的峰值同配置"), PeakOf(Impulse.FieldOfView, Omega, 0.4), 1.5, 1e-3);
		TestEqual(TEXT("弹簧同配置"), Impulse.Omega, Omega);
	}

	// 范围：每个数都在范围里，真的在变；弹簧的 ω、ζ 也能随机。每枪随机往一边时两边都有，大小还在范围里
	FGinkgoCameraRecoil Ranged;
	Ranged.Pitch = FFloatInterval(1.f, 3.f);
	Ranged.Yaw = FFloatInterval(0.5f, 1.f);
	Ranged.Roll = FFloatInterval(0.5f, 1.f);
	Ranged.bRandomSide = true;
	Ranged.Omega = FFloatInterval(20.f, 40.f);
	Ranged.Zeta = FFloatInterval(0.3f, 0.6f);
	const FRandomStream Random(19980812);
	double MinPitch = TNumericLimits<double>::Max(), MaxPitch = 0.0, MinOmega = TNumericLimits<double>::Max(), MaxOmega = 0.0;
	int32 YawRight = 0, RollRight = 0, SameSide = 0;
	bool bInRange = true;
	constexpr int32 Shots = 400;
	for (int32 Shot = 0; Shot < Shots; ++Shot)
	{
		const FGinkgoCameraImpulse Impulse = SampleCameraRecoil(Ranged, Random);
		bInRange &= Impulse.Omega >= 20.f && Impulse.Omega <= 40.f && Impulse.Zeta >= 0.3f && Impulse.Zeta <= 0.6f;
		const double ToPeak = Ginkgo::Spring::DampedImpulseForPeak(1.0, Impulse.Omega, Impulse.Zeta);
		const double Pitch = Impulse.Rotation.Pitch / ToPeak;
		const double Yaw = Impulse.Rotation.Yaw / ToPeak;
		const double Roll = Impulse.Rotation.Roll / ToPeak;
		bInRange &= Pitch >= 1.0 - 1e-5 && Pitch <= 3.0 + 1e-5 && FMath::Abs(Yaw) >= 0.5 - 1e-5 && FMath::Abs(Yaw) <= 1.0 + 1e-5 && FMath::Abs(Roll) >= 0.5 - 1e-5 && FMath::Abs(Roll) <= 1.0 + 1e-5;
		MinPitch = FMath::Min(MinPitch, Pitch);
		MaxPitch = FMath::Max(MaxPitch, Pitch);
		MinOmega = FMath::Min(MinOmega, static_cast<double>(Impulse.Omega));
		MaxOmega = FMath::Max(MaxOmega, static_cast<double>(Impulse.Omega));
		YawRight += Yaw > 0.0;
		RollRight += Roll > 0.0;
		SameSide += (Yaw > 0.0) == (Roll > 0.0);
	}
	AddInfo(FString::Printf(TEXT("%d 枪：俯仰 %.3f–%.3f，ω %.2f–%.2f，往右 %d / %d，同边 %d"), Shots, MinPitch, MaxPitch, MinOmega, MaxOmega, YawRight, RollRight, SameSide));
	TestTrue(TEXT("每个数都在范围里"), bInRange);
	TestTrue(TEXT("俯仰取遍了范围"), MinPitch < 1.1 && MaxPitch > 2.9);
	TestTrue(TEXT("ω 取遍了范围"), MinOmega < 21.0 && MaxOmega > 39.0);
	TestTrue(TEXT("偏航、侧倾两边都有"), YawRight > Shots / 4 && YawRight < Shots * 3 / 4 && RollRight > Shots / 4 && RollRight < Shots * 3 / 4);
	TestTrue(TEXT("偏航、侧倾各自随机"), SameSide > Shots / 4 && SameSide < Shots * 3 / 4);

	// 同一个种子取到的数一样（帧率不影响）
	const FRandomStream A(7), B(7);
	const FGinkgoCameraImpulse FromA = SampleCameraRecoil(Ranged, A);
	const FGinkgoCameraImpulse FromB = SampleCameraRecoil(Ranged, B);
	TestTrue(TEXT("同一个种子取到的数一样"), FromA.Rotation == FromB.Rotation && FromA.Omega == FromB.Omega);

	// 回弹：不单独配时就是踢出去的弹簧；单独配时在它自己的范围里取，踢出去取到的数不变
	const FRandomStream Together(11);
	const FGinkgoCameraImpulse Same = SampleCameraRecoil(Ranged, Together);
	TestTrue(TEXT("回弹不单独配：同踢出去的弹簧"), Same.ReturnOmega == Same.Omega && Same.ReturnZeta == Same.Zeta);
	FGinkgoCameraRecoil Separate = Ranged;
	Separate.bSeparateReturn = true;
	Separate.ReturnOmega = FFloatInterval(6.f, 10.f);
	Separate.ReturnZeta = FFloatInterval(0.8f, 1.f);
	const FRandomStream Apart(11);
	const FGinkgoCameraImpulse Split = SampleCameraRecoil(Separate, Apart);
	TestTrue(TEXT("回弹单独配：踢出去取到的数不变"), Split.Rotation == Same.Rotation && Split.FieldOfView == Same.FieldOfView && Split.Omega == Same.Omega && Split.Zeta == Same.Zeta);
	float MinReturnOmega = TNumericLimits<float>::Max(), MaxReturnOmega = 0.f, MinReturnZeta = TNumericLimits<float>::Max(), MaxReturnZeta = 0.f;
	for (int32 Shot = 0; Shot < Shots; ++Shot)
	{
		const FGinkgoCameraImpulse Impulse = SampleCameraRecoil(Separate, Apart);
		MinReturnOmega = FMath::Min(MinReturnOmega, Impulse.ReturnOmega);
		MaxReturnOmega = FMath::Max(MaxReturnOmega, Impulse.ReturnOmega);
		MinReturnZeta = FMath::Min(MinReturnZeta, Impulse.ReturnZeta);
		MaxReturnZeta = FMath::Max(MaxReturnZeta, Impulse.ReturnZeta);
	}
	TestTrue(TEXT("回弹的 ω 在范围里，真的在变"), MinReturnOmega >= 6.f && MinReturnOmega < 6.2f && MaxReturnOmega <= 10.f && MaxReturnOmega > 9.8f);
	TestTrue(TEXT("回弹的 ζ 在范围里，真的在变"), MinReturnZeta >= 0.8f && MinReturnZeta < 0.82f && MaxReturnZeta <= 1.f && MaxReturnZeta > 0.98f);

	// 默认是 98k 的两层
	const UGinkgoWeaponDefinition* Defaults = GetDefault<UGinkgoWeaponDefinition>();
	TestEqual(TEXT("默认两层"), Defaults->CameraRecoil.Num(), 2);
	if (Defaults->CameraRecoil.Num() == 2)
	{
		TestTrue(TEXT("第一层是踢动"), Defaults->CameraRecoil[0].Name == TEXT("Kick"));
		TestTrue(TEXT("抖动每枪随机往一边"), Defaults->CameraRecoil[1].bRandomSide && !Defaults->CameraRecoil[0].bRandomSide);
		TestTrue(TEXT("踢动的回弹单独配，抖动不单独配"), Defaults->CameraRecoil[0].bSeparateReturn && !Defaults->CameraRecoil[1].bSeparateReturn);
	}

	// 拉栓的相机震动（2026-11-12 你说的：拉栓时也给一些单独的相机震动，不用很大）：默认是 98k 的四下，各一层，峰值都不到 1°。
	// 拉机柄在右边：扳起往右滚、压下滚回来；拉到底往上一顿、推到底往下一顿
	TestEqual(TEXT("拉栓的四个提示点各有震动"), Defaults->CueCameraShakes.Num(), 4);
	const auto LayerOf = [Defaults](const FGameplayTag& Cue) -> const FGinkgoCameraRecoil*
	{
		const FGinkgoCueCameraShake* Shake = Defaults->CueCameraShakes.Find(Cue);
		return Shake && Shake->Layers.Num() == 1 ? &Shake->Layers[0] : nullptr;
	};
	const FGinkgoCameraRecoil* Lift = LayerOf(FWeaponTags::Weapon_Cue_BoltLift);
	const FGinkgoCameraRecoil* Pull = LayerOf(FWeaponTags::Weapon_Cue_BoltPull);
	const FGinkgoCameraRecoil* Push = LayerOf(FWeaponTags::Weapon_Cue_BoltPush);
	const FGinkgoCameraRecoil* Press = LayerOf(FWeaponTags::Weapon_Cue_BoltPress);
	TestTrue(TEXT("扳起、拉到底、推到底、压下各一层"), Lift && Pull && Push && Press);
	if (Lift && Pull && Push && Press)
	{
		bool bSmall = true;
		for (const FGinkgoCameraRecoil* Layer : {Lift, Pull, Push, Press})
		{
			for (const FFloatInterval& Range : {Layer->Pitch, Layer->Yaw, Layer->Roll})
			{
				bSmall &= FMath::Max(FMath::Abs(Range.Min), FMath::Abs(Range.Max)) < 1.f;
			}
			bSmall &= !Layer->bRandomSide && Layer->FieldOfView.Max == 0.f;
		}
		TestTrue(TEXT("都不到 1°、方向固定、不跳视场"), bSmall);
		TestTrue(TEXT("扳起往右滚、压下滚回来"), Lift->Roll.Min > 0.f && Press->Roll.Max < 0.f);
		TestTrue(TEXT("拉到底往上一顿、推到底往下一顿"), Pull->Pitch.Min > 0.f && Push->Pitch.Max < 0.f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponSlotKindTest, "CaseGinkgo.Weapon.Slots.Kinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponSlotKindTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	TestTrue(TEXT("第一个槽是主武器"), GetSlotKind(0) == EGinkgoWeaponSlotKind::Primary);
	TestTrue(TEXT("第二个槽是主武器"), GetSlotKind(1) == EGinkgoWeaponSlotKind::Primary);
	TestTrue(TEXT("第三个槽是手枪"), GetSlotKind(SidearmSlot) == EGinkgoWeaponSlotKind::Sidearm);

	const bool Empty[] = {false, false, false};
	const bool OnePrimary[] = {true, false, false};
	const bool TwoPrimaries[] = {true, true, false};
	const bool All[] = {true, true, true};
	TestEqual(TEXT("主武器进第一个空的"), FindFreeSlot(EGinkgoWeaponSlotKind::Primary, Empty), 0);
	TestEqual(TEXT("第一个有了进第二个"), FindFreeSlot(EGinkgoWeaponSlotKind::Primary, OnePrimary), 1);
	TestEqual(TEXT("两个主武器都满了：不进手枪的槽"), FindFreeSlot(EGinkgoWeaponSlotKind::Primary, TwoPrimaries), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("手枪进手枪的槽"), FindFreeSlot(EGinkgoWeaponSlotKind::Sidearm, TwoPrimaries), SidearmSlot);
	TestEqual(TEXT("手枪的槽满了"), FindFreeSlot(EGinkgoWeaponSlotKind::Sidearm, All), static_cast<int32>(INDEX_NONE));

	// 键：4 拿手枪；切枪轮流；手枪的槽空着时 4 不响应
	const bool RifleAndPistol[] = {true, false, true};
	TestEqual(TEXT("按 4 拿手枪"), ApplyWeaponKey(EGinkgoWeaponKey::Sidearm, 0, 0, RifleAndPistol), SidearmSlot);
	TestEqual(TEXT("手枪的槽空着：不响应"), ApplyWeaponKey(EGinkgoWeaponKey::Sidearm, 0, 0, OnePrimary), 0);
	TestEqual(TEXT("切枪：步枪换手枪"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, 0, 0, RifleAndPistol), SidearmSlot);
	TestEqual(TEXT("切枪：手枪换步枪"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, SidearmSlot, 0, RifleAndPistol), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponPickupPlanTest, "CaseGinkgo.Weapon.Slots.PickupPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponPickupPlanTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	constexpr EGinkgoWeaponSlotKind Primary = EGinkgoWeaponSlotKind::Primary;
	constexpr EGinkgoWeaponSlotKind Sidearm = EGinkgoWeaponSlotKind::Sidearm;
	const auto Check = [this](const TCHAR* What, const FGinkgoWeaponPickupPlan& Plan, const int32 Slot, const int32 Drop, const bool bStow)
	{
		TestEqual(FString::Printf(TEXT("%s：捡到哪个槽"), What), Plan.Slot, Slot);
		TestEqual(FString::Printf(TEXT("%s：谁掉地上"), What), Plan.DropSlot, Drop);
		TestEqual(FString::Printf(TEXT("%s：手上的要不要先收"), What), Plan.bStowHeld, bStow);
	};
	// 有空槽：放进去，手上有枪先收
	Check(TEXT("徒手，全空"), PlanWeaponPickup(Primary, {false, false, false}, INDEX_NONE, INDEX_NONE), 0, INDEX_NONE, false);
	Check(TEXT("拿着步枪，还有一个主武器槽"), PlanWeaponPickup(Primary, {true, false, false}, 0, 0), 1, INDEX_NONE, true);
	Check(TEXT("拿着步枪捡手枪，手枪槽空着"), PlanWeaponPickup(Sidearm, {true, false, false}, 0, 0), SidearmSlot, INDEX_NONE, true);
	Check(TEXT("拿着手枪捡步枪，主武器有空槽"), PlanWeaponPickup(Primary, {true, false, true}, SidearmSlot, SidearmSlot), 1, INDEX_NONE, true);
	// 你说的：拿着步枪捡手枪，身上已经有手枪：手枪直接掉地上，手上的步枪照样收
	Check(TEXT("拿着步枪捡手枪，身上有手枪"), PlanWeaponPickup(Sidearm, {true, false, true}, 0, 0), SidearmSlot, SidearmSlot, true);
	// 你说的：步枪一样，关键看是不是同一种槽位
	Check(TEXT("拿着手枪捡步枪，两把步枪都在背上，上次拿的第一把"), PlanWeaponPickup(Primary, {true, true, true}, SidearmSlot, 0), 0, 0, true);
	Check(TEXT("拿着手枪捡步枪，上次拿的手枪，掉靠后的"), PlanWeaponPickup(Primary, {true, true, true}, SidearmSlot, SidearmSlot), 1, 1, true);
	// 同一种槽位满了、手上的就是这一种：丢手上的，不用收
	Check(TEXT("拿着第一把步枪捡步枪，主武器满了"), PlanWeaponPickup(Primary, {true, true, false}, 0, 0), 0, 0, false);
	Check(TEXT("拿着第二把步枪捡步枪，主武器满了"), PlanWeaponPickup(Primary, {true, true, true}, 1, 1), 1, 1, false);
	Check(TEXT("拿着手枪捡手枪"), PlanWeaponPickup(Sidearm, {true, false, true}, SidearmSlot, SidearmSlot), SidearmSlot, SidearmSlot, false);
	// 徒手，这一种满了：身上的掉，上一次拿过的优先
	Check(TEXT("徒手捡步枪，上次拿的第二把"), PlanWeaponPickup(Primary, {true, true, false}, INDEX_NONE, 1), 1, 1, false);
	Check(TEXT("徒手捡手枪，身上有手枪"), PlanWeaponPickup(Sidearm, {true, false, true}, INDEX_NONE, 0), SidearmSlot, SidearmSlot, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponTubeSelectTest, "CaseGinkgo.Weapon.States.SelectTube",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponTubeSelectTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Weapon::SelectWeaponAction;
	using Ginkgo::Weapon::ShouldLoadTwo;
	// 泵动霰弹枪（Docs/Design/ActionStates.md「霰弹枪」）：关着枪机压，抬到装填位压，压完收回来；打完一枪先泵好，压完膛还空着就泵一下
	FGinkgoWeaponActionFacts Ready;
	Ready.bLoadsWithActionClosed = true;
	Ready.bCanFire = true;
	Ready.bFeedHasRounds = true;
	TestTrue(TEXT("都不想：就绪"), SelectWeaponAction(Ready) == EGinkgoWeaponAction::Ready);
	Ready.bWantsFire = true;
	TestTrue(TEXT("想开火"), SelectWeaponAction(Ready) == EGinkgoWeaponAction::Fire);

	FGinkgoWeaponActionFacts Fired;
	Fired.bLoadsWithActionClosed = true;
	Fired.bChamberSpent = true;
	Fired.bFeedHasRounds = true;
	Fired.bWantsReload = true;
	Fired.bCanLoad = true;
	TestTrue(TEXT("开完一枪想装填：先泵（不同于 98k 开栓装填）"), SelectWeaponAction(Fired) == EGinkgoWeaponAction::CycleBolt);

	FGinkgoWeaponActionFacts Reload;
	Reload.bLoadsWithActionClosed = true;
	Reload.bCanFire = true;
	Reload.bWantsReload = true;
	Reload.bCanLoad = true;
	TestTrue(TEXT("想装填：抬到装填位，不开栓"), SelectWeaponAction(Reload) == EGinkgoWeaponAction::ReloadStart);
	Reload.bHandAtAction = true;
	TestTrue(TEXT("手在装填口下面、还能压：压"), SelectWeaponAction(Reload) == EGinkgoWeaponAction::LoadRound);
	Reload.bCanLoad = false;
	TestTrue(TEXT("压满了：收回来"), SelectWeaponAction(Reload) == EGinkgoWeaponAction::ReloadEnd);
	Reload.bCanLoad = true;
	Reload.bWantsReload = false;
	Reload.bWantsFire = true;
	TestTrue(TEXT("按了开火：先收回来再开"), SelectWeaponAction(Reload) == EGinkgoWeaponAction::ReloadEnd);

	FGinkgoWeaponActionFacts Empty;
	Empty.bLoadsWithActionClosed = true;
	Empty.bChamberEmpty = true;
	Empty.bFeedHasRounds = true;
	Empty.bWantsReload = true;
	TestTrue(TEXT("压满了收回来，膛还空着：泵一下"), SelectWeaponAction(Empty) == EGinkgoWeaponAction::CycleBolt);
	Empty.bFeedHasRounds = false;
	Empty.bWantsReload = false;
	Empty.bWantsFire = true;
	TestTrue(TEXT("管里也空了：咔"), SelectWeaponAction(Empty) == EGinkgoWeaponAction::DryFire);

	FGinkgoWeaponActionFacts Open;
	Open.bLoadsWithActionClosed = true;
	Open.bActionOpen = true;
	TestTrue(TEXT("泵到一半被打断：推回去"), SelectWeaponAction(Open) == EGinkgoWeaponAction::CloseBolt);

	TestTrue(TEXT("空 4 个、后备 24：两发"), ShouldLoadTwo(4, 24));
	TestTrue(TEXT("空 2 个、后备 2：两发"), ShouldLoadTwo(2, 2));
	TestFalse(TEXT("只缺一发：一发"), ShouldLoadTwo(1, 24));
	TestFalse(TEXT("只剩一发：一发"), ShouldLoadTwo(4, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponOpenBoltSelectTest, "CaseGinkgo.Weapon.States.SelectOpenBolt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponOpenBoltSelectTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Weapon::SelectWeaponAction;
	// MG3（Docs/Design/ActionStates.md「MG3」）：开膛待击，枪机开着、膛空就是准备好了；不装填，只有开火、空扣、放松
	FGinkgoWeaponActionFacts Ready;
	Ready.bOpenBolt = true;
	Ready.bActionOpen = true;
	Ready.bChamberEmpty = true;
	Ready.bFeedHasRounds = true;
	Ready.bCanFire = true;
	TestTrue(TEXT("枪机开着、膛空、弹链有弹：就绪，不当成要拉机柄"), SelectWeaponAction(Ready) == EGinkgoWeaponAction::Ready);
	Ready.bWantsReload = true;
	TestTrue(TEXT("按装填：不响应"), SelectWeaponAction(Ready) == EGinkgoWeaponAction::Ready);
	Ready.bWantsReload = false;
	Ready.bWantsFire = true;
	TestTrue(TEXT("想开火：开火"), SelectWeaponAction(Ready) == EGinkgoWeaponAction::Fire);

	FGinkgoWeaponActionFacts Empty;
	Empty.bOpenBolt = true;
	Empty.bChamberEmpty = true;
	Empty.bWantsFire = true;
	TestTrue(TEXT("打空了（枪机在前面、膛空）想开火：空扣"), SelectWeaponAction(Empty) == EGinkgoWeaponAction::DryFire);
	Empty.bWantsFire = false;
	Empty.bIdleRelaxDue = true;
	TestTrue(TEXT("站够了：放松"), SelectWeaponAction(Empty) == EGinkgoWeaponAction::IdleRelax);
	Empty.bHandsFree = false;
	TestTrue(TEXT("胳膊被别的占着：就绪"), SelectWeaponAction(Empty) == EGinkgoWeaponAction::Ready);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponHeavySlotTest, "CaseGinkgo.Weapon.Slots.Heavy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponHeavySlotTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 重武器（MG3，你说的）：只能拿在手上，收不起来，没有键；换枪、收枪就扔在地上，捡别的枪时也是丢它
	TestTrue(TEXT("第四个槽是重武器"), GetSlotKind(HeavySlot) == EGinkgoWeaponSlotKind::Heavy);
	TestFalse(TEXT("重武器收不起来"), CanStow(EGinkgoWeaponSlotKind::Heavy));
	TestTrue(TEXT("一号主武器换二号：手在背上"), IsBackToBackSwap(0, 1));
	TestTrue(TEXT("二号主武器换一号：手在背上"), IsBackToBackSwap(1, 0));
	TestFalse(TEXT("同一个槽不算"), IsBackToBackSwap(0, 0));
	TestFalse(TEXT("收起来不接"), IsBackToBackSwap(0, INDEX_NONE));
	TestFalse(TEXT("空手不接"), IsBackToBackSwap(INDEX_NONE, 0));
	TestFalse(TEXT("换手枪不接：枪套在腰上"), IsBackToBackSwap(0, SidearmSlot));
	TestFalse(TEXT("从手枪换不接"), IsBackToBackSwap(SidearmSlot, 0));
	TestFalse(TEXT("重武器不接"), IsBackToBackSwap(HeavySlot, 0));
	TestTrue(TEXT("主武器、手枪收得起来"), CanStow(EGinkgoWeaponSlotKind::Primary) && CanStow(EGinkgoWeaponSlotKind::Sidearm));
	const bool ThreeCarried[] = {true, true, true, false};
	TestEqual(TEXT("重武器进第四个槽"), FindFreeSlot(EGinkgoWeaponSlotKind::Heavy, ThreeCarried), HeavySlot);
	TestEqual(TEXT("主武器满了：不进重武器的槽"), FindFreeSlot(EGinkgoWeaponSlotKind::Primary, ThreeCarried), static_cast<int32>(INDEX_NONE));

	constexpr EGinkgoWeaponSlotKind Primary = EGinkgoWeaponSlotKind::Primary;
	constexpr EGinkgoWeaponSlotKind Sidearm = EGinkgoWeaponSlotKind::Sidearm;
	constexpr EGinkgoWeaponSlotKind Heavy = EGinkgoWeaponSlotKind::Heavy;
	const auto Check = [this](const TCHAR* What, const FGinkgoWeaponPickupPlan& Plan, const int32 Slot, const int32 Drop, const bool bStow, const bool bDropHeld)
	{
		TestEqual(FString::Printf(TEXT("%s：捡到哪个槽"), What), Plan.Slot, Slot);
		TestEqual(FString::Printf(TEXT("%s：谁掉地上"), What), Plan.DropSlot, Drop);
		TestEqual(FString::Printf(TEXT("%s：手上的要不要先收"), What), Plan.bStowHeld, bStow);
		TestEqual(FString::Printf(TEXT("%s：手上的要不要扔"), What), Plan.bDropHeld, bDropHeld);
	};
	Check(TEXT("拿着步枪捡 MG3：步枪照常收"), PlanWeaponPickup(Heavy, {true, false, true, false}, 0, 0), HeavySlot, INDEX_NONE, true, false);
	Check(TEXT("徒手捡 MG3"), PlanWeaponPickup(Heavy, {true, false, true, false}, INDEX_NONE, 0), HeavySlot, INDEX_NONE, false, false);
	Check(TEXT("拿着 MG3 捡步枪，主武器有空槽：MG3 扔掉"), PlanWeaponPickup(Primary, {true, false, true, true}, HeavySlot, 0), 1, INDEX_NONE, false, true);
	Check(TEXT("拿着 MG3 捡步枪，主武器满了：上次拿的那把从背上掉，MG3 也扔掉"), PlanWeaponPickup(Primary, {true, true, true, true}, HeavySlot, 0), 0, 0, false, true);
	Check(TEXT("拿着 MG3 捡手枪，手枪槽空着：MG3 扔掉"), PlanWeaponPickup(Sidearm, {true, false, false, true}, HeavySlot, 0), SidearmSlot, INDEX_NONE, false, true);
	Check(TEXT("拿着 MG3 捡 MG3：丢手上的"), PlanWeaponPickup(Heavy, {true, false, false, true}, HeavySlot, 0), HeavySlot, HeavySlot, false, false);

	// 键：拿着 MG3（第四个槽）。组件看到目标变了、手上的收不起来，就先把它扔了再去拔
	const bool WithHeavy[] = {true, false, true, true};
	TestEqual(TEXT("按 1：拔步枪"), ApplyWeaponKey(EGinkgoWeaponKey::Slot1, HeavySlot, 0, WithHeavy), 0);
	TestEqual(TEXT("按 4：拔手枪"), ApplyWeaponKey(EGinkgoWeaponKey::Sidearm, HeavySlot, 0, WithHeavy), SidearmSlot);
	TestEqual(TEXT("收枪：空手"), ApplyWeaponKey(EGinkgoWeaponKey::Holster, HeavySlot, 0, WithHeavy), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("切枪：回到上一次拿的步枪"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, HeavySlot, 0, WithHeavy), 0);
	TestEqual(TEXT("切枪：上一次拿的是手枪"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, HeavySlot, SidearmSlot, WithHeavy), SidearmSlot);
	const bool OnlyHeavy[] = {false, false, false, true};
	TestEqual(TEXT("只有 MG3：切枪不响应"), ApplyWeaponKey(EGinkgoWeaponKey::Switch, HeavySlot, INDEX_NONE, OnlyHeavy), HeavySlot);
	TestEqual(TEXT("只有 MG3：收枪是空手"), ApplyWeaponKey(EGinkgoWeaponKey::Holster, HeavySlot, INDEX_NONE, OnlyHeavy), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponDetachableSelectTest, "CaseGinkgo.Weapon.States.SelectDetachable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponDetachableSelectTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Weapon::SelectWeaponAction;
	FGinkgoWeaponActionFacts Facts;
	Facts.bDetachableFeed = true;
	Facts.bCanFire = true;
	Facts.bFeedHasRounds = true;
	Facts.bCanLoad = true;
	TestTrue(TEXT("都不想：就绪"), SelectWeaponAction(Facts) == EGinkgoWeaponAction::Ready);
	Facts.bWantsFire = true;
	TestTrue(TEXT("想开火"), SelectWeaponAction(Facts) == EGinkgoWeaponAction::Fire);
	Facts.bWantsFire = false;
	Facts.bWantsReload = true;
	TestTrue(TEXT("枪机关着想装填：战术换弹"), SelectWeaponAction(Facts) == EGinkgoWeaponAction::ReloadTactical);
	Facts.bCanLoad = false;
	TestTrue(TEXT("满的：不换"), SelectWeaponAction(Facts) == EGinkgoWeaponAction::Ready);

	FGinkgoWeaponActionFacts Locked;
	Locked.bDetachableFeed = true;
	Locked.bActionOpen = true;
	Locked.bCanLoad = true;
	Locked.bWantsFire = true;
	TestTrue(TEXT("打空了挂住，想开火：咔"), SelectWeaponAction(Locked) == EGinkgoWeaponAction::DryFire);
	Locked.bWantsFire = false;
	Locked.bWantsReload = true;
	TestTrue(TEXT("打空了挂住，想装填：空仓换弹"), SelectWeaponAction(Locked) == EGinkgoWeaponAction::ReloadEmpty);
	Locked.bWantsReload = false;
	Locked.bFeedHasRounds = true;
	TestTrue(TEXT("换到一半收枪，拔出来弹匣有弹：放枪机"), SelectWeaponAction(Locked) == EGinkgoWeaponAction::CloseBolt);

	// 没有空仓挂机（AK）：打空了枪机是关着的，膛是空的，同样是空仓换弹，换了弹匣要拉机柄
	FGinkgoWeaponActionFacts Closed;
	Closed.bDetachableFeed = true;
	Closed.bChamberEmpty = true;
	Closed.bCanLoad = true;
	Closed.bWantsFire = true;
	TestTrue(TEXT("打空了枪机关着，想开火：咔"), SelectWeaponAction(Closed) == EGinkgoWeaponAction::DryFire);
	Closed.bWantsFire = false;
	Closed.bWantsReload = true;
	TestTrue(TEXT("打空了枪机关着，想装填：空仓换弹"), SelectWeaponAction(Closed) == EGinkgoWeaponAction::ReloadEmpty);
	Closed.bWantsReload = false;
	Closed.bFeedHasRounds = true;
	TestTrue(TEXT("换到一半收枪，拔出来弹匣有弹：拉机柄"), SelectWeaponAction(Closed) == EGinkgoWeaponAction::CloseBolt);
	Closed.bDetachableFeed = false;
	TestTrue(TEXT("弹仓的枪（98k）膛空栓关着不算要拉机柄：要按键才开栓"), SelectWeaponAction(Closed) == EGinkgoWeaponAction::Ready);

	// 放松：别的都不想的时候才放
	FGinkgoWeaponActionFacts Relax;
	Relax.bDetachableFeed = true;
	Relax.bCanFire = true;
	Relax.bFeedHasRounds = true;
	Relax.bIdleRelaxDue = true;
	TestTrue(TEXT("站够了：放松"), SelectWeaponAction(Relax) == EGinkgoWeaponAction::IdleRelax);
	Relax.bWantsFire = true;
	TestTrue(TEXT("按开火先于放松"), SelectWeaponAction(Relax) == EGinkgoWeaponAction::Fire);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponSpreadComputeTest, "CaseGinkgo.Weapon.Spread.Compute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponSpreadComputeTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	FGinkgoWeaponSpread Spread;
	Spread.HipDefault = 1.f;
	Spread.AdsDefault = 0.2f;
	Spread.HipMax = 3.f;
	Spread.AdsMax = 1.f;
	Spread.AirFactor = 2.f;
	const auto Compute = [&Spread](const float Bloom, const float AdsAlpha, const float Speed, const float AirAlpha, const float CrouchAlpha = 0.f, const float SlideAlpha = 0.f)
	{
		return ComputeSpread(Spread, FGinkgoSpreadInput{Bloom, AdsAlpha, Speed, AirAlpha, CrouchAlpha, SlideAlpha});
	};
	TestEqual(TEXT("腰射默认"), Compute(0.f, 0.f, 0.f, 0.f), 1.f);
	TestEqual(TEXT("开镜默认"), Compute(0.f, 1.f, 0.f, 0.f), 0.2f);
	TestEqual(TEXT("开镜进度一半"), Compute(0.f, 0.5f, 0.f, 0.f), 0.6f, 1e-5f);
	TestEqual(TEXT("累积加在默认上"), Compute(0.5f, 0.f, 0.f, 0.f), 1.5f);
	TestEqual(TEXT("腰射的上限"), Compute(5.f, 0.f, 0.f, 0.f), 3.f);
	TestEqual(TEXT("开镜的上限"), Compute(5.f, 1.f, 0.f, 0.f), 1.f);
	TestEqual(TEXT("累积最多：腰射"), GetMaxSpreadBloom(Spread, 0.f), 2.f);
	TestEqual(TEXT("累积最多：开镜"), GetMaxSpreadBloom(Spread, 1.f), 0.8f);

	// 系数乘在结果上，上限管不着它们
	TestEqual(TEXT("没有键时速度不算"), Compute(0.f, 0.f, 600.f, 0.f), 1.f);
	FRichCurve* Speed = Spread.SpeedFactor.GetRichCurve();
	Speed->AddKey(100.f, 1.f);
	Speed->AddKey(500.f, 3.f);
	TestEqual(TEXT("站着"), Compute(0.f, 0.f, 0.f, 0.f), 1.f);
	TestEqual(TEXT("速度在两个键中间"), Compute(0.f, 0.f, 300.f, 0.f), 2.f, 1e-5f);
	TestEqual(TEXT("比最后一个键快"), Compute(0.f, 0.f, 900.f, 0.f), 3.f);
	TestEqual(TEXT("空中"), Compute(0.f, 0.f, 0.f, 1.f), 2.f);
	TestEqual(TEXT("离地缓到一半"), Compute(0.f, 0.f, 0.f, 0.5f), 1.5f);
	TestEqual(TEXT("都乘在上限上"), Compute(5.f, 0.f, 900.f, 1.f), 3.f * 3.f * 2.f);

	// 蹲姿、滑铲：默认 0.7、0.8（你给的），也乘在上限上，比重缓过
	TestEqual(TEXT("蹲姿的默认"), Spread.CrouchFactor, 0.7f);
	TestEqual(TEXT("滑铲的默认"), Spread.SlideFactor, 0.8f);
	TestEqual(TEXT("蹲着"), Compute(0.f, 0.f, 0.f, 0.f, 1.f), 0.7f);
	TestEqual(TEXT("蹲下缓到一半"), Compute(0.f, 0.f, 0.f, 0.f, 0.5f), 0.85f, 1e-5f);
	TestEqual(TEXT("滑铲"), Compute(0.f, 0.f, 0.f, 0.f, 0.f, 1.f), 0.8f);
	TestEqual(TEXT("滑铲也乘速度"), Compute(0.f, 0.f, 900.f, 0.f, 0.f, 1.f), 3.f * 0.8f, 1e-5f);
	TestEqual(TEXT("蹲着也乘在上限上"), Compute(5.f, 0.f, 0.f, 0.f, 1.f), 3.f * 0.7f, 1e-5f);
	TestEqual(TEXT("姿态系数：只有空中"), GetSpreadPostureFactor(Spread, FGinkgoSpreadInput{0.f, 0.f, 0.f, 1.f}), 2.f);

	// 上限配得比默认小：不往下压，累积是 0
	Spread.HipMax = 0.5f;
	TestEqual(TEXT("上限比默认小"), Compute(1.f, 0.f, 0.f, 0.f), 1.f);
	TestEqual(TEXT("上限比默认小：累积"), GetMaxSpreadBloom(Spread, 0.f), 0.f);

	// 圆锥在画面上：视场 90° 时焦距 720 px（1080p 高），开镜拉近时同样的扩散画得大
	TestEqual(TEXT("1° 在 90° 的视场上"), SpreadToPixels(1.f, 90.f), 12.568, 1e-3);
	TestEqual(TEXT("1° 在 60° 的视场上"), SpreadToPixels(1.f, 60.f), 21.768, 1e-3);
	TestEqual(TEXT("0°"), SpreadToPixels(0.f, 90.f), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponSpreadSampleTest, "CaseGinkgo.Weapon.Spread.Sample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponSpreadSampleTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	const auto AngleBetween = [](const FVector& A, const FVector& B) { return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A | B, -1.0, 1.0))); };
	for (const FVector& Aimed : {FVector::ForwardVector, FVector::UpVector, FVector(1.0, -2.0, 0.5).GetSafeNormal()})
	{
		FRandomStream Random(20261105);
		TArray<double> Angles;
		FVector Sum = FVector::ZeroVector;
		for (int32 Index = 0; Index < 4000; ++Index)
		{
			const FVector Direction = SampleSpreadDirection(Aimed, 2.f, Random);
			TestTrue(TEXT("单位向量"), Direction.IsUnit(1e-6));
			Angles.Add(AngleBetween(Aimed, Direction));
			Sum += Direction;
		}
		Angles.Sort();
		TestTrue(TEXT("都在圆锥里"), Angles.Last() <= 2.0 + 1e-6);
		TestTrue(TEXT("到得了圆锥的边"), Angles.Last() >= 1.99);
		// 半径均匀取：一半的子弹在半径的一半以内，四分之一在四分之一以内
		TestEqual(TEXT("中位数"), Angles[Angles.Num() / 2], 1.0, 0.06);
		TestEqual(TEXT("四分之一"), Angles[Angles.Num() / 4], 0.5, 0.06);
		// 方位均匀：平均下来不偏向哪一边
		TestTrue(TEXT("不偏向哪一边"), AngleBetween(Aimed, Sum.GetSafeNormal()) < 0.05);
	}
	// 扩散是 0：就是瞄准的方向；两个数照取，后面的枪取到的数不变
	FRandomStream A(7), B(7);
	const FVector Aimed = FVector::ForwardVector;
	TestEqual(TEXT("扩散是 0"), SampleSpreadDirection(Aimed, 0.f, A), Aimed);
	SampleSpreadDirection(Aimed, 3.f, B);
	TestTrue(TEXT("扩散是 0 也取数"), SampleSpreadDirection(Aimed, 2.f, A).Equals(SampleSpreadDirection(Aimed, 2.f, B), 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponCrosshairDashTest, "CaseGinkgo.Weapon.Hold.CrosshairDash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponCrosshairDashTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	TestEqual(TEXT("没有横线的枪"), CrosshairDashOffset(0.f, 30.f), 0.f);
	TestEqual(TEXT("圆锥小：停在最少的那里"), CrosshairDashOffset(5.f, 3.f), 5.f);
	TestEqual(TEXT("圆锥大：内端在圆锥上"), CrosshairDashOffset(5.f, 30.f), 30.f);

	// 左手：98k 一直握着（单手冲刺除外），M1911 跑、冲刺都放开，抬枪回来，片段放开时放开
	TestEqual(TEXT("两手的枪跑着也握"), MakeLeftHandGrip(true, 0.f, true, 1.f, 0.f, 1.f), 1.f);
	TestEqual(TEXT("单手冲刺放开"), MakeLeftHandGrip(false, 1.f, true, 0.f, 0.f, 1.f), 0.f);
	TestEqual(TEXT("单手跑放开"), MakeLeftHandGrip(false, 0.f, false, 1.f, 0.f, 1.f), 0.f);
	TestEqual(TEXT("跑着抬枪：回到枪上"), MakeLeftHandGrip(false, 0.f, false, 1.f, 1.f, 1.f), 1.f);
	TestEqual(TEXT("片段里左手离开枪"), MakeLeftHandGrip(true, 0.f, true, 0.f, 0.f, 0.f), 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

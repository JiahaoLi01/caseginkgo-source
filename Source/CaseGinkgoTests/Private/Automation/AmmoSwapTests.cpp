#include "Combat/GinkgoAmmoDefinition.h"
#include "Control/GinkgoInputMath.h"
#include "Misc/AutomationTest.h"
#include "Utilities/Enumeration/Tags/TestTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponMechanism.h"
#include "Weapon/GinkgoWeaponTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// 弹种与弹种轮盘（Docs/Design/Weapons.md「切换弹种」）：机构的直接压膛、换弹种的装填、轮盘的选法、装填键的长按、选子树、同一口径的弹种、子弹改的扩散

namespace GinkgoAmmoSwapTests
{
	using Ginkgo::Weapon::ApplyMechanismEvent;

	/** 泵动霰弹枪：管状弹仓 4 发，关着枪机能压，能直接压膛（2026-11-06 你定的） */
	FGinkgoMagazineRules PumpShotgun()
	{
		FGinkgoMagazineRules Rules;
		Rules.Feed = EGinkgoFeed::TubeMagazine;
		Rules.Capacity = 4;
		Rules.bLoadNeedsOpenAction = false;
		Rules.bCanLoadChamber = true;
		return Rules;
	}

	/** 98k：内置弹仓 5 发，压弹要开栓，不能直接压膛（控制供弹的抓壳钩） */
	FGinkgoMagazineRules Mauser98k()
	{
		FGinkgoMagazineRules Rules;
		Rules.Feed = EGinkgoFeed::InternalMagazine;
		Rules.Capacity = 5;
		Rules.bLoadNeedsOpenAction = true;
		return Rules;
	}

	FGameplayTag Buck() { return FTestTags::GinkgoTest_Ammo_Shell; }
	FGameplayTag Slug() { return FTestTags::GinkgoTest_Ammo_Special; }
	FGameplayTag Ball() { return FTestTags::GinkgoTest_Ammo_Rifle; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAmmoChamberLoadTest, "CaseGinkgo.Weapon.Mechanism.ChamberLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAmmoChamberLoadTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAmmoSwapTests;
	using Ginkgo::Weapon::CanLoadChamber;
	const FGinkgoMagazineRules Rules = PumpShotgun();
	// 满的 4 + 1 鹿弹，换独头弹：护木拉到底（膛里的鹿弹回后备）、从抛壳口放一发独头弹、推回去（膛里有了，不从管里推）
	FGinkgoMechanismState State = Ginkgo::Weapon::MakeLoadedState(Rules, Buck());
	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Buck(), 10);
	Reserve.Add(Slug(), 3);
	TestTrue(TEXT("满的鹿弹：压独头弹进膛有用"), CanLoadChamber(Rules, State, Reserve, Slug()));
	TestFalse(TEXT("膛里就是鹿弹：再压鹿弹没用"), CanLoadChamber(Rules, State, Reserve, Buck()));
	TestFalse(TEXT("枪机关着：放不进去"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundChamber, Slug()));
	TestTrue(TEXT("拉开"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag()));
	TestEqual(TEXT("膛里的鹿弹回后备"), Reserve.Count(Buck()), 11);
	TestTrue(TEXT("放一发独头弹"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundChamber, Slug()));
	TestFalse(TEXT("膛里有了：不能再放"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundChamber, Slug()));
	TestTrue(TEXT("推回去"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag()));
	TestTrue(TEXT("膛里是独头弹的活弹，管里还是 4 发鹿弹"), State.CanFire() && State.ChamberAmmo == Slug() && State.Feed.Num() == 4 && State.Feed.Last() == Buck());
	TestEqual(TEXT("独头弹后备少一发"), Reserve.Count(Slug()), 2);
	TestFalse(TEXT("膛里已经是独头弹：不用再压"), CanLoadChamber(Rules, State, Reserve, Slug()));

	// 枪里一发也没有（紧急装填）：拉开（没东西出来）、放一发、推回去
	FGinkgoMechanismState Dry;
	FGinkgoAmmoReserve DryReserve;
	DryReserve.Add(Buck(), 1);
	TestTrue(TEXT("空枪：压膛有用"), CanLoadChamber(Rules, Dry, DryReserve, Buck()));
	ApplyMechanismEvent(Rules, Dry, DryReserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag());
	ApplyMechanismEvent(Rules, Dry, DryReserve, EGinkgoMechanismEvent::RoundChamber, Buck());
	ApplyMechanismEvent(Rules, Dry, DryReserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag());
	TestTrue(TEXT("空枪压了一发：能响，管里空着"), Dry.CanFire() && Dry.Feed.IsEmpty() && DryReserve.Count(Buck()) == 0);
	TestFalse(TEXT("后备没了：压不了"), CanLoadChamber(Rules, FGinkgoMechanismState(), DryReserve, Buck()));

	// 膛里是空壳（开完一枪没泵）：拉开时空壳抛掉，同样能放
	FGinkgoMechanismState Fired = Ginkgo::Weapon::MakeLoadedState(Rules, Buck());
	FGinkgoAmmoReserve FiredReserve;
	FiredReserve.Add(Slug(), 1);
	ApplyMechanismEvent(Rules, Fired, FiredReserve, EGinkgoMechanismEvent::Fire, FGameplayTag());
	TestTrue(TEXT("膛里空壳：压膛有用"), CanLoadChamber(Rules, Fired, FiredReserve, Slug()));
	ApplyMechanismEvent(Rules, Fired, FiredReserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag());
	TestTrue(TEXT("空壳抛掉以后放一发"), ApplyMechanismEvent(Rules, Fired, FiredReserve, EGinkgoMechanismEvent::RoundChamber, Slug()));

	// 98k 不能直接压膛（控制供弹）
	const FGinkgoMagazineRules Rifle = Mauser98k();
	FGinkgoMechanismState Open;
	Open.Action = EGinkgoAction::Open;
	FGinkgoAmmoReserve RifleReserve;
	RifleReserve.Add(Ball(), 5);
	TestFalse(TEXT("98k：压膛没用"), CanLoadChamber(Rifle, Open, RifleReserve, Ball()));
	TestFalse(TEXT("98k：放不进去"), ApplyMechanismEvent(Rifle, Open, RifleReserve, EGinkgoMechanismEvent::RoundChamber, Ball()));
	TestEqual(TEXT("98k：后备没少"), RifleReserve.Count(Ball()), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAmmoSwapLoadTest, "CaseGinkgo.Weapon.Mechanism.SwapLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAmmoSwapLoadTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoAmmoSwapTests;
	using Ginkgo::Weapon::CanLoadRound;
	using Ginkgo::Weapon::GetNextRoundAmmo;
	// 98k 换特殊弹（你定的：开栓 → 压特殊弹 → 闭栓）：4 + 1 满的普通弹，换成特殊弹时开栓把膛里那一发退回后备，弹仓有空，压一发，闭栓推进膛的就是它
	const FGinkgoMagazineRules Rules = Mauser98k();
	FGinkgoMechanismState State = Ginkgo::Weapon::MakeLoadedState(Rules, Ball());
	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Ball(), 20);
	Reserve.Add(Slug(), 5);
	TestFalse(TEXT("满的普通弹：再装普通弹没用（4 + 1 是满的）"), CanLoadRound(Rules, State, Reserve, Ball()));
	TestTrue(TEXT("换特殊弹：装得进"), CanLoadRound(Rules, State, Reserve, Slug()));
	ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag());
	TestEqual(TEXT("开栓：膛里的普通弹回后备"), Reserve.Count(Ball()), 21);
	TestTrue(TEXT("弹仓 4 发，还能压特殊弹"), CanLoadRound(Rules, State, Reserve, Slug()));
	TestTrue(TEXT("压一发特殊弹"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundIn, Slug()));
	TestFalse(TEXT("弹仓满了"), CanLoadRound(Rules, State, Reserve, Slug()));
	ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag());
	TestTrue(TEXT("闭栓：膛里是特殊弹，弹仓 4 发普通弹"), State.ChamberAmmo == Slug() && State.CanFire() && State.Feed.Num() == 4 && State.Feed.Last() == Ball());
	TestEqual(TEXT("下一枪是特殊弹"), GetNextRoundAmmo(State), Slug());
	TestFalse(TEXT("膛里就是特殊弹、4 + 1：再装特殊弹没用"), CanLoadRound(Rules, State, Reserve, Slug()));
	TestTrue(TEXT("换回普通弹：装得进（开栓把特殊弹退回后备）"), CanLoadRound(Rules, State, Reserve, Ball()));

	// 半满：2 + 1 普通弹换特殊弹，压三发特殊弹在上面，打的顺序是特殊、特殊、特殊、普通、普通
	FGinkgoMechanismState Half;
	Half.Feed.Init(Ball(), 2);
	Half.Chamber = EGinkgoChamber::Live;
	Half.ChamberAmmo = Ball();
	ApplyMechanismEvent(Rules, Half, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag());
	int32 Loaded = 0;
	while (CanLoadRound(Rules, Half, Reserve, Slug()) && ApplyMechanismEvent(Rules, Half, Reserve, EGinkgoMechanismEvent::RoundIn, Slug()))
	{
		++Loaded;
	}
	ApplyMechanismEvent(Rules, Half, Reserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag());
	TestEqual(TEXT("半满：压了三发特殊弹"), Loaded, 3);
	TArray<FGameplayTag> Order;
	for (int32 Shot = 0; Shot < 5 && Ginkgo::Weapon::CanFire(Rules, Half); ++Shot)
	{
		Order.Add(GetNextRoundAmmo(Half));
		ApplyMechanismEvent(Rules, Half, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag());
		ApplyMechanismEvent(Rules, Half, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag());
		ApplyMechanismEvent(Rules, Half, Reserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag());
	}
	TestTrue(TEXT("打的顺序：特殊 ×3、普通 ×2"), Order == TArray<FGameplayTag>({Slug(), Slug(), Slug(), Ball(), Ball()}));

	// 下一枪：膛里的活弹；空壳、空膛时是拉一下进膛的那一发；一发也没有时为空
	FGinkgoMechanismState Next;
	Next.Feed = {Ball(), Slug()};
	TestEqual(TEXT("膛空：下一枪是供弹最上面那一发"), GetNextRoundAmmo(Next), Slug());
	Next.Chamber = EGinkgoChamber::Spent;
	Next.ChamberAmmo = Ball();
	TestEqual(TEXT("膛里空壳：同上"), GetNextRoundAmmo(Next), Slug());
	Next.Chamber = EGinkgoChamber::Live;
	TestEqual(TEXT("膛里活弹：就是它"), GetNextRoundAmmo(Next), Ball());
	TestFalse(TEXT("一发也没有：空"), GetNextRoundAmmo(FGinkgoMechanismState()).IsValid());

	// 霰弹枪的管：关着枪机压，换了弹种也只看管里有没有空位
	const FGinkgoMagazineRules Pump = PumpShotgun();
	FGinkgoMechanismState Tube = Ginkgo::Weapon::MakeLoadedState(Pump, Buck());
	TestFalse(TEXT("霰弹枪 4 + 1：管满了，换弹种也压不进管"), CanLoadRound(Pump, Tube, Reserve, Slug()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAmmoWheelSectorsTest, "CaseGinkgo.Weapon.AmmoWheel.Sectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAmmoWheelSectorsTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 第 0 项在正上方，往右排，每项占它中心左右各半格（X 右、Y 上）
	TestEqual(TEXT("两项：上是 0"), PickWheelSector(FVector2D(0.1, 1.0), 2), 0);
	TestEqual(TEXT("两项：下是 1"), PickWheelSector(FVector2D(-0.2, -1.0), 2), 1);
	TestEqual(TEXT("两项：偏上一点的右边还是 0"), PickWheelSector(FVector2D(1.0, 0.05), 2), 0);
	TestEqual(TEXT("两项：偏下一点的右边是 1"), PickWheelSector(FVector2D(1.0, -0.05), 2), 1);
	TestEqual(TEXT("四项：右是 1"), PickWheelSector(FVector2D(1.0, 0.2), 4), 1);
	TestEqual(TEXT("四项：下是 2"), PickWheelSector(FVector2D(0.3, -1.0), 4), 2);
	TestEqual(TEXT("四项：左是 3"), PickWheelSector(FVector2D(-1.0, -0.3), 4), 3);
	TestEqual(TEXT("四项：左上偏上是 0"), PickWheelSector(FVector2D(-0.9, 1.0), 4), 0);
	TestEqual(TEXT("三项：右下是 1"), PickWheelSector(FVector2D(0.87, -0.5), 3), 1);
	TestEqual(TEXT("一项：总是 0"), PickWheelSector(FVector2D(0.0, -1.0), 1), 0);
	TestTrue(TEXT("四项的第 1 项朝右"), GetWheelSectorDirection(1, 4).Equals(FVector2D(1.0, 0.0), 1.e-6));
	TestTrue(TEXT("两项的第 1 项朝下"), GetWheelSectorDirection(1, 2).Equals(FVector2D(0.0, -1.0), 1.e-6));

	// 鼠标动的方向：攒起来，夹在半径以内（短：换方向动一点就转过去），不动时收回去
	FVector2D Motion = StepWheelMotion(FVector2D::ZeroVector, FVector2D(0.0, -5.0), 24.f, 0.f, 0.f);
	TestTrue(TEXT("没有收回（时间常数 0 是全收回，只留这一帧的）"), Motion.Equals(FVector2D(0.0, -5.0), 1.e-6));
	Motion = StepWheelMotion(FVector2D(0.0, -5.0), FVector2D(0.0, -3.0), 24.f, 1.e6f, 0.016f);
	TestTrue(TEXT("攒起来"), Motion.Equals(FVector2D(0.0, -8.0), 1.e-3));
	Motion = StepWheelMotion(Motion, FVector2D(0.0, -40.0), 24.f, 1.e6f, 0.016f);
	TestTrue(TEXT("夹在半径上"), Motion.Equals(FVector2D(0.0, -24.0), 1.e-3));
	Motion = StepWheelMotion(Motion, FVector2D(0.0, 30.0), 24.f, 1.e6f, 0.016f);
	TestTrue(TEXT("往反方向动回来"), Motion.Equals(FVector2D(0.0, 6.0), 1.e-3));
	Motion = StepWheelMotion(FVector2D(0.0, 6.0), FVector2D::ZeroVector, 6.f, 0.12f, 0.12f);
	TestEqual(TEXT("不动时按时间常数收回去：一个时间常数以后剩 1/e"), Motion.Y, 6.0 / UE_EULERS_NUMBER, 1.e-6);
	FVector2D Settled(0.0, 6.0);
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Settled = StepWheelMotion(Settled, FVector2D::ZeroVector, 6.f, 0.12f, 1.f / 60.f);
	}
	TestTrue(TEXT("停一秒：收回到死区以下"), Settled.Size() < 0.01);
	TestTrue(TEXT("停下来以后往反方向动一点就够了（过死区）"), StepWheelMotion(Settled, FVector2D(0.0, -2.6), 6.f, 0.12f, 1.f / 60.f).Y <= -2.5);

	// 选：摇杆推过死区按摇杆，不然鼠标的方向过了死区按它，都没有时留着
	TestEqual(TEXT("都在死区里：留着"), SelectWheelEntry(FVector2D(0.0, -3.0), FVector2D(0.1, -0.2), 2, 0, 8.f, 0.5f), 0);
	TestEqual(TEXT("鼠标往下过了死区：1"), SelectWheelEntry(FVector2D(0.0, -9.0), FVector2D::ZeroVector, 2, 0, 8.f, 0.5f), 1);
	TestEqual(TEXT("摇杆往上推：0，盖过鼠标"), SelectWheelEntry(FVector2D(0.0, -20.0), FVector2D(0.0, 0.9), 2, 1, 8.f, 0.5f), 0);
	TestEqual(TEXT("摇杆松开回中：留着上一次选的"), SelectWheelEntry(FVector2D::ZeroVector, FVector2D::ZeroVector, 2, 1, 8.f, 0.5f), 1);
	TestEqual(TEXT("没有项：没有"), SelectWheelEntry(FVector2D(0.0, 20.0), FVector2D::ZeroVector, 0, 0, 8.f, 0.5f), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAmmoReloadKeyTest, "CaseGinkgo.Weapon.AmmoWheel.ReloadKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAmmoReloadKeyTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Weapon::StepReloadKey;
	constexpr float Hold = 0.25f;
	constexpr float Frame = 1.f / 60.f;

	// 没有轮盘的枪：按下就装（同以前）
	FGinkgoReloadKey Key;
	TestTrue(TEXT("没有轮盘：按下就装"), StepReloadKey(Key, true, true, false, false, Hold, Frame) == EGinkgoReloadKeyAction::Reload);
	TestTrue(TEXT("没有轮盘：按着不再装"), StepReloadKey(Key, false, true, false, false, Hold, Frame) == EGinkgoReloadKeyAction::None);

	// 有轮盘：短按在松开时装
	Key = FGinkgoReloadKey();
	TestTrue(TEXT("有轮盘：按下先等"), StepReloadKey(Key, true, true, true, true, Hold, Frame) == EGinkgoReloadKeyAction::None);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		StepReloadKey(Key, false, true, true, true, Hold, Frame);
	}
	TestTrue(TEXT("0.1 s 松开：装"), StepReloadKey(Key, false, false, true, true, Hold, Frame) == EGinkgoReloadKeyAction::Reload);
	TestFalse(TEXT("松开以后不等了"), Key.bPending);

	// 按够了打开，松开确认
	Key = FGinkgoReloadKey();
	StepReloadKey(Key, true, true, true, true, Hold, Frame);
	EGinkgoReloadKeyAction Opened = EGinkgoReloadKeyAction::None;
	int32 Frames = 0;
	while (Opened == EGinkgoReloadKeyAction::None && Frames < 60)
	{
		Opened = StepReloadKey(Key, false, true, true, true, Hold, Frame);
		++Frames;
	}
	TestTrue(TEXT("按够了：打开"), Opened == EGinkgoReloadKeyAction::OpenWheel);
	TestEqual(TEXT("60 fps 第 15 帧打开（0.25 s）"), Frames, 15);
	TestTrue(TEXT("开着按着：不再开"), StepReloadKey(Key, false, true, true, true, Hold, Frame) == EGinkgoReloadKeyAction::None);
	TestTrue(TEXT("松开：确认"), StepReloadKey(Key, false, false, true, true, Hold, Frame) == EGinkgoReloadKeyAction::ConfirmWheel);

	// 按够了却开不了（开着镜）：接着等，松开当短按
	Key = FGinkgoReloadKey();
	StepReloadKey(Key, true, true, true, false, Hold, Frame);
	for (int32 Index = 0; Index < 30; ++Index)
	{
		TestTrue(TEXT("开不了：不开"), StepReloadKey(Key, false, true, true, false, Hold, Frame) == EGinkgoReloadKeyAction::None);
	}
	TestTrue(TEXT("开得了了（还按着）：打开"), StepReloadKey(Key, false, true, true, true, Hold, Frame) == EGinkgoReloadKeyAction::OpenWheel);
	Key = FGinkgoReloadKey();
	StepReloadKey(Key, true, true, true, false, Hold, Frame);
	StepReloadKey(Key, false, true, true, false, Hold, 0.5f);
	TestTrue(TEXT("一直开不了，松开：装"), StepReloadKey(Key, false, false, true, false, Hold, Frame) == EGinkgoReloadKeyAction::Reload);

	// 同一帧里按下又松开（点得快、回放的旧录制）：就是短按
	Key = FGinkgoReloadKey();
	TestTrue(TEXT("同一帧按下松开：装"), StepReloadKey(Key, true, false, true, true, Hold, Frame) == EGinkgoReloadKeyAction::Reload);
	TestFalse(TEXT("不等了"), Key.bPending);

	// 取消（开镜）时调的一方清掉：松开什么也不做
	Key = FGinkgoReloadKey();
	StepReloadKey(Key, true, true, true, true, Hold, Frame);
	StepReloadKey(Key, false, true, true, true, Hold, 0.3f);
	Key = FGinkgoReloadKey();
	TestTrue(TEXT("取消以后松开：什么也不做"), StepReloadKey(Key, false, false, true, true, Hold, Frame) == EGinkgoReloadKeyAction::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAmmoSelectChamberTest, "CaseGinkgo.Weapon.States.SelectChamberLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAmmoSelectChamberTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Weapon::SelectWeaponAction;
	// 霰弹枪从抛壳口压膛（换弹种、紧急装填）：先于泵动、装填；手在装填口下面时先收回来；护木在后面、膛空时接着压，膛里有了推回去
	FGinkgoWeaponActionFacts Swap;
	Swap.bLoadsWithActionClosed = true;
	Swap.bCanFire = true;
	Swap.bFeedHasRounds = true;
	Swap.bWantsLoadChamber = true;
	Swap.bCanLoadChamber = true;
	TestTrue(TEXT("满的换弹种：压膛"), SelectWeaponAction(Swap) == EGinkgoWeaponAction::LoadChamber);
	Swap.bCanLoadChamber = false;
	TestTrue(TEXT("压不了（后备没了）：就绪"), SelectWeaponAction(Swap) == EGinkgoWeaponAction::Ready);
	Swap.bCanLoadChamber = true;
	Swap.bWantsReload = true;
	Swap.bCanLoad = true;
	Swap.bHandAtAction = true;
	TestTrue(TEXT("压管到一半要压膛：先收回来"), SelectWeaponAction(Swap) == EGinkgoWeaponAction::ReloadEnd);
	Swap.bHandAtAction = false;
	TestTrue(TEXT("收回来以后：压膛，再装管"), SelectWeaponAction(Swap) == EGinkgoWeaponAction::LoadChamber);

	FGinkgoWeaponActionFacts Fired;
	Fired.bLoadsWithActionClosed = true;
	Fired.bChamberSpent = true;
	Fired.bFeedHasRounds = true;
	Fired.bWantsLoadChamber = true;
	Fired.bCanLoadChamber = true;
	TestTrue(TEXT("开完一枪要压膛：不先泵（拉开时空壳就出来了）"), SelectWeaponAction(Fired) == EGinkgoWeaponAction::LoadChamber);

	FGinkgoWeaponActionFacts Open;
	Open.bLoadsWithActionClosed = true;
	Open.bActionOpen = true;
	Open.bChamberEmpty = true;
	Open.bWantsLoadChamber = true;
	Open.bCanLoadChamber = true;
	TestTrue(TEXT("护木在后面、膛空（压膛做到一半）：接着压"), SelectWeaponAction(Open) == EGinkgoWeaponAction::LoadChamber);
	Open.bChamberEmpty = false;
	Open.bCanLoadChamber = false;
	TestTrue(TEXT("膛里已经放进去了：推回去"), SelectWeaponAction(Open) == EGinkgoWeaponAction::CloseBolt);
	Open.bChamberEmpty = true;
	Open.bWantsLoadChamber = false;
	TestTrue(TEXT("不压膛时护木在后面：推回去（同以前）"), SelectWeaponAction(Open) == EGinkgoWeaponAction::CloseBolt);

	// 不是管状弹仓的枪不看压膛（98k 换弹种走装填）
	FGinkgoWeaponActionFacts Bolt;
	Bolt.bCanFire = true;
	Bolt.bFeedHasRounds = true;
	Bolt.bWantsLoadChamber = true;
	Bolt.bCanLoadChamber = true;
	Bolt.bWantsReload = true;
	Bolt.bCanLoad = true;
	TestTrue(TEXT("98k：开栓装填"), SelectWeaponAction(Bolt) == EGinkgoWeaponAction::OpenBolt);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAmmoChoicesTest, "CaseGinkgo.Combat.AmmoChoices",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAmmoChoicesTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Combat::FilterAmmoChoices;
	const FGameplayTag Buck = FWeaponTags::Ammo_Gauge12_Buckshot;
	const FGameplayTag Slug = FWeaponTags::Ammo_Gauge12_Slug;
	const FGameplayTag Ball = FWeaponTags::Ammo_Mauser792x57_Standard;
	const FGameplayTag SmK = FWeaponTags::Ammo_Mauser792x57_SmK;
	const FGameplayTag Kalash = FWeaponTags::Ammo_M43762x39_Standard;
	const TArray<FGameplayTag> Known = {Ball, Kalash, Buck, Slug, SmK};
	const auto Choices = FilterAmmoChoices(Buck, Known);
	TestTrue(TEXT("霰弹枪：鹿弹、独头弹"), Choices.Num() == 2 && Choices[0] == Buck && Choices[1] == Slug);
	const auto Rifle = FilterAmmoChoices(Ball, Known);
	TestTrue(TEXT("98k：普通弹第一，再是 S.m.K."), Rifle.Num() == 2 && Rifle[0] == Ball && Rifle[1] == SmK);
	TestEqual(TEXT("AK：只有一种"), FilterAmmoChoices(Kalash, Known).Num(), 1);
	TestEqual(TEXT("枪的弹种排第一，即使列表里排在后面"), FilterAmmoChoices(Slug, Known)[0], Slug);
	TestEqual(TEXT("空的：没有"), FilterAmmoChoices(FGameplayTag(), Known).Num(), 0);

	// 设置里的真资产（GinkgoCombat -Ammo 生成）：两种特殊弹找得到，独头弹一颗弹丸、开镜更准，S.m.K. 比普通弹穿得多、护甲伤害大、肉伤小一点
	const auto Real = UGinkgoAmmoDefinition::GetChoices(Buck);
	TestTrue(TEXT("设置里霰弹枪的两种"), Real.Contains(Buck) && Real.Contains(Slug));
	const UGinkgoAmmoDefinition* SlugData = UGinkgoAmmoDefinition::Find(Slug);
	const UGinkgoAmmoDefinition* SmKData = UGinkgoAmmoDefinition::Find(SmK);
	const UGinkgoAmmoDefinition* BallData = UGinkgoAmmoDefinition::Find(Ball);
	if (TestNotNull(TEXT("独头弹有数据"), SlugData))
	{
		TestEqual(TEXT("独头弹：一颗弹丸"), SlugData->WeaponEffects.PelletCount, 1);
		TestTrue(TEXT("独头弹：开镜更准"), SlugData->WeaponEffects.AdsSpreadScale < 1.f);
		TestTrue(TEXT("独头弹：有开局后备"), SlugData->StartingReserve > 0);
	}
	if (TestNotNull(TEXT("S.m.K. 有数据"), SmKData) && TestNotNull(TEXT("普通弹有数据"), BallData))
	{
		TestTrue(TEXT("S.m.K.：穿甲率更高、护甲伤害更大"), SmKData->ArmorPenetration > BallData->ArmorPenetration && SmKData->ArmorDamage > BallData->ArmorDamage);
		TestEqual(TEXT("S.m.K.：不改枪的弹丸数"), SmKData->WeaponEffects.PelletCount, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAmmoSpreadScaleTest, "CaseGinkgo.Weapon.Spread.AmmoScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAmmoSpreadScaleTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Weapon;
	// 子弹改的扩散乘在默认与上限上（独头弹开镜 ×0.12）：开镜进度按原样混
	FGinkgoWeaponSpread Spread;
	Spread.HipDefault = 4.5f;
	Spread.AdsDefault = 2.5f;
	Spread.HipMax = 6.5f;
	Spread.AdsMax = 3.5f;
	Spread.CrouchFactor = 1.f;
	Spread.SlideFactor = 1.f;
	FGinkgoSpreadInput Input;
	Input.AdsAlpha = 1.f;
	TestEqual(TEXT("鹿弹开镜：2.5°"), ComputeSpread(Spread, Input), 2.5f, 1.e-5f);
	Input.AdsScale = 0.12f;
	TestEqual(TEXT("独头弹开镜：0.3°"), ComputeSpread(Spread, Input), 0.3f, 1.e-5f);
	Input.AdsAlpha = 0.f;
	TestEqual(TEXT("独头弹腰射：照枪的 4.5°"), ComputeSpread(Spread, Input), 4.5f, 1.e-5f);
	Input.AdsAlpha = 1.f;
	Input.Bloom = 5.f;
	TestEqual(TEXT("独头弹开镜连射：到缩过的上限 0.42°"), ComputeSpread(Spread, Input), 0.42f, 1.e-5f);
	TestEqual(TEXT("累积最多：缩过的上限减缩过的默认"), GetMaxSpreadBloom(Spread, 1.f, 1.f, 0.12f), 0.12f, 1.e-5f);
	TestEqual(TEXT("累积最多：不缩时同以前"), GetMaxSpreadBloom(Spread, 1.f), 1.f, 1.e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAmmoWheelInputTest, "CaseGinkgo.Control.Input.WheelInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAmmoWheelInputTest::RunTest(const FString& Parameters)
{
	// 轮盘的鼠标按灵敏度换成度，不反转、不加速（拨轮盘按手往哪推）；摇杆只有视角的死区，不反转
	FGinkgoInputSettings Settings;
	Settings.MouseSensitivity = 2.f;
	Settings.bMouseInvertPitch = true;
	Settings.bMouseInvertYaw = true;
	Settings.bMouseAcceleration = true;
	const FVector2D Degrees = Ginkgo::Input::WheelMouseDegrees(FVector2D(10.0, 20.0), Settings);
	TestTrue(TEXT("鼠标：count × 灵敏度 × 0.022，不反转、不加速"), Degrees.Equals(FVector2D(0.44, 0.88), 1.e-5));
	Settings.bGamepadInvertPitch = true;
	const FVector2D Stick = Ginkgo::Input::WheelStickInput(FVector2D(0.0, 1.0), Settings);
	TestTrue(TEXT("摇杆往上推就是上（不反转）"), Stick.Y > 0.99);
	TestTrue(TEXT("摇杆死区里是零"), Ginkgo::Input::WheelStickInput(FVector2D(0.1, 0.1), Settings).IsZero());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

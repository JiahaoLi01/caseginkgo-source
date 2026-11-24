#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Utilities/Enumeration/Tags/TestTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponMechanism.h"
#include "Weapon/GinkgoWeaponTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoWeaponMechanismTests
{
	using Ginkgo::Weapon::ApplyMechanismEvent;
	using Ginkgo::Weapon::MakeLoadedState;

	FGinkgoMagazineRules MakeRules(const EGinkgoFeed Feed, const int32 Capacity, const bool bLoadNeedsOpenAction, const bool bSelfLoading)
	{
		FGinkgoMagazineRules Rules;
		Rules.Feed = Feed;
		Rules.Capacity = Capacity;
		Rules.bLoadNeedsOpenAction = bLoadNeedsOpenAction;
		Rules.bSelfLoading = bSelfLoading;
		return Rules;
	}

	/** 98k：内置弹仓 5 发，压弹要开栓，手动拉栓 */
	FGinkgoMagazineRules Mauser98k()
	{
		return MakeRules(EGinkgoFeed::InternalMagazine, 5, /*bLoadNeedsOpenAction*/ true, /*bSelfLoading*/ false);
	}

	/** AK：可拆弹匣 30 发，自动装填 */
	FGinkgoMagazineRules Kalashnikov()
	{
		return MakeRules(EGinkgoFeed::DetachableMagazine, 30, /*bLoadNeedsOpenAction*/ false, /*bSelfLoading*/ true);
	}

	/** 泵动霰弹枪：管状弹仓 5 发，关着枪机也能压，手动泵动 */
	FGinkgoMagazineRules PumpShotgun()
	{
		return MakeRules(EGinkgoFeed::TubeMagazine, 5, /*bLoadNeedsOpenAction*/ false, /*bSelfLoading*/ false);
	}

	/** M79：单发中折榴弹发射器，无供弹仓，直接入膛 */
	FGinkgoMagazineRules BreakLauncher()
	{
		FGinkgoMagazineRules Rules = MakeRules(EGinkgoFeed::SingleShot, 0, /*bLoadNeedsOpenAction*/ true, /*bSelfLoading*/ false);
		Rules.bCanLoadChamber = true;
		return Rules;
	}

	FGameplayTag Standard()
	{
		return FWeaponTags::Ammo_Mauser792x57_Standard;
	}

	/** 枪里加后备里的活弹：除了开火，什么事件都不该让它变 */
	int32 TotalLive(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve)
	{
		int32 Total = State.CountLiveRounds();
		for (const TPair<FGameplayTag, int32>& Entry : Reserve.Rounds)
		{
			Total += Entry.Value;
		}
		return Total;
	}

	/** 依次发一串事件，返回做成了几个 */
	int32 ApplyAll(const FGinkgoMagazineRules& Rules, FGinkgoMechanismState& State, FGinkgoAmmoReserve& Reserve, std::initializer_list<EGinkgoMechanismEvent> Events,
		const FGameplayTag& Ammo)
	{
		int32 Done = 0;
		for (const EGinkgoMechanismEvent Event : Events)
		{
			Done += ApplyMechanismEvent(Rules, State, Reserve, Event, Ammo) ? 1 : 0;
		}
		return Done;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismMauserCycleTest, "CaseGinkgo.Weapon.Mechanism.Mauser98kCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismMauserCycleTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	const FGinkgoMagazineRules Rules = Mauser98k();
	FGinkgoAmmoReserve Reserve;
	FGinkgoMechanismState State = MakeLoadedState(Rules, Standard());
	TestEqual(TEXT("新拿到的 98k：弹仓 4 发"), State.Feed.Num(), 4);
	TestTrue(TEXT("新拿到的 98k：膛里一发活弹，枪栓关着，能开火"), State.Chamber == EGinkgoChamber::Live && State.ChamberAmmo == Standard() && State.CanFire());

	TestTrue(TEXT("开火"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag()));
	TestTrue(TEXT("开火以后膛里是这一发的空壳，弹仓不动"), State.Chamber == EGinkgoChamber::Spent && State.ChamberAmmo == Standard() && State.Feed.Num() == 4);
	const FGinkgoMechanismState Spent = State;
	TestFalse(TEXT("空壳打不响"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag()));
	TestTrue(TEXT("打不响的那一下什么也不改"), State == Spent);

	TestTrue(TEXT("开栓"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag()));
	TestTrue(TEXT("开栓抛掉空壳：空膛，空壳不回后备"), State.Chamber == EGinkgoChamber::Empty && !State.ChamberAmmo.IsValid() && Reserve.Count(Standard()) == 0);
	TestFalse(TEXT("开着的栓不能再开"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag()));
	TestFalse(TEXT("栓开着打不响"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag()));
	TestTrue(TEXT("闭栓"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag()));
	TestTrue(TEXT("闭栓把弹仓最上面那一发推进膛：弹仓 3 发"), State.CanFire() && State.Feed.Num() == 3);

	// 打空：每一发都是开火 → 开栓 → 闭栓
	int32 Fired = 1;
	while (ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag()))
	{
		++Fired;
		ApplyAll(Rules, State, Reserve, {EGinkgoMechanismEvent::ActionOpen, EGinkgoMechanismEvent::ActionClose}, FGameplayTag());
	}
	TestEqual(TEXT("一共打出 5 发"), Fired, 5);
	TestTrue(TEXT("打空以后：弹仓空、空膛、栓关着"), State.Feed.IsEmpty() && State.Chamber == EGinkgoChamber::Empty && State.Action == EGinkgoAction::Closed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismMauserReloadTest, "CaseGinkgo.Weapon.Mechanism.Mauser98kReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismMauserReloadTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	const FGinkgoMagazineRules Rules = Mauser98k();
	// 设计里的例子：栓关着、弹仓 3 发 + 膛里活弹
	FGinkgoMechanismState State;
	State.Feed.Init(Standard(), 3);
	State.Chamber = EGinkgoChamber::Live;
	State.ChamberAmmo = Standard();
	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Standard(), 10);

	TestFalse(TEXT("栓关着压不进去"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundIn, Standard()));
	TestEqual(TEXT("压不进去时不从后备取"), Reserve.Count(Standard()), 10);
	TestTrue(TEXT("开栓"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag()));
	TestTrue(TEXT("开栓时膛里的活弹扣下来回到后备"), State.Chamber == EGinkgoChamber::Empty && Reserve.Count(Standard()) == 11 && State.Feed.Num() == 3);
	TestEqual(TEXT("压两发"), ApplyAll(Rules, State, Reserve, {EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::RoundIn}, Standard()), 2);
	TestFalse(TEXT("弹仓满了（5 发）压不进第 6 发"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundIn, Standard()));
	TestTrue(TEXT("闭栓"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag()));
	TestTrue(TEXT("完了是弹仓 4 + 膛 1"), State.Feed.Num() == 4 && State.CanFire());
	TestEqual(TEXT("从后备净取一发"), Reserve.Count(Standard()), 9);

	TestFalse(TEXT("98k 没有可拆弹匣"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagOut, FGameplayTag()));
	ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag());
	State.Feed.Reset();
	FGinkgoAmmoReserve Empty;
	TestFalse(TEXT("后备没有这种弹时压不进去"), ApplyMechanismEvent(Rules, State, Empty, EGinkgoMechanismEvent::RoundIn, Standard()));
	TestTrue(TEXT("压不进去时弹仓不动"), State.Feed.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismOrderTest, "CaseGinkgo.Weapon.Mechanism.RoundOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismOrderTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	const FGinkgoMagazineRules Rules = Mauser98k();
	const FGameplayTag Special = FTestTags::GinkgoTest_Ammo_Special;

	// 压一发特殊弹：开栓（膛里的普通弹回后备）→ 压特殊弹 → 闭栓，下一发就是它
	FGinkgoMechanismState State = MakeLoadedState(Rules, Standard());
	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Special, 1);
	ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag());
	TestEqual(TEXT("开栓：膛里的普通弹按弹种回到后备"), Reserve.Count(Standard()), 1);
	TestTrue(TEXT("压一发特殊弹"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundIn, Special));
	ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag());
	TestTrue(TEXT("最后压进去的最先进膛：膛里是特殊弹"), State.Chamber == EGinkgoChamber::Live && State.ChamberAmmo == Special);
	TestEqual(TEXT("弹仓里还是 4 发普通弹"), State.Feed.Num(), 4);
	ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag());
	TestTrue(TEXT("空壳也记着弹种"), State.Chamber == EGinkgoChamber::Spent && State.ChamberAmmo == Special);
	ApplyAll(Rules, State, Reserve, {EGinkgoMechanismEvent::ActionOpen, EGinkgoMechanismEvent::ActionClose}, FGameplayTag());
	TestTrue(TEXT("下一发是普通弹"), State.ChamberAmmo == Standard());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismDetachableTest, "CaseGinkgo.Weapon.Mechanism.DetachableMagazine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismDetachableTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	const FGinkgoMagazineRules Rules = Kalashnikov();
	const FGameplayTag Rifle = FTestTags::GinkgoTest_Ammo_Rifle;
	FGinkgoAmmoReserve Reserve;
	FGinkgoMechanismState State = MakeLoadedState(Rules, Rifle);
	TestTrue(TEXT("新拿到的 AK：29 + 1"), State.Feed.Num() == 29 && State.CanFire());

	TestTrue(TEXT("开火"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag()));
	TestTrue(TEXT("自动装填：同一个事件里抛壳、上膛"), State.CanFire() && State.Feed.Num() == 28);
	int32 Fired = 1;
	while (ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag()))
	{
		++Fired;
	}
	TestEqual(TEXT("一共打出 30 发"), Fired, 30);
	TestTrue(TEXT("打空后枪机不停在后方，膛空"), State.Action == EGinkgoAction::Closed && State.Chamber == EGinkgoChamber::Empty);
	TestFalse(TEXT("可拆弹匣不能逐发压"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundIn, Rifle));

	// 中途换弹：膛里有弹，卸匣 → 装匣，31 发
	State = MakeLoadedState(Rules, Rifle);
	Reserve.Add(Rifle, 60);
	for (int32 Shot = 0; Shot < 10; ++Shot)
	{
		ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag());
	}
	TestTrue(TEXT("卸匣"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagOut, FGameplayTag()));
	TestTrue(TEXT("卸下的弹匣里的 19 发回到后备"), Reserve.Count(Rifle) == 79 && !State.bMagazineIn && State.Feed.IsEmpty());
	TestTrue(TEXT("弹匣卸了，膛里那一发还能开"), State.CanFire());
	TestTrue(TEXT("装匣"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagIn, Rifle));
	TestTrue(TEXT("中途换弹以后是 30 + 1"), State.Feed.Num() == 30 && State.CanFire() && State.CountLiveRounds() == 31);
	TestEqual(TEXT("装匣从后备取了 30 发"), Reserve.Count(Rifle), 49);

	// 空仓换弹：卸匣 → 装匣 → 拉机柄，29 + 1
	State = FGinkgoMechanismState();
	TestEqual(TEXT("空仓换弹"), ApplyAll(Rules, State, Reserve, {EGinkgoMechanismEvent::MagOut, EGinkgoMechanismEvent::MagIn, EGinkgoMechanismEvent::ActionOpen,
		EGinkgoMechanismEvent::ActionClose}, Rifle), 4);
	TestTrue(TEXT("空仓换弹以后是 29 + 1"), State.Feed.Num() == 29 && State.CanFire());

	// 后备不够时装上的是没满的弹匣
	FGinkgoAmmoReserve Few;
	Few.Add(Rifle, 12);
	State = FGinkgoMechanismState();
	ApplyAll(Rules, State, Few, {EGinkgoMechanismEvent::MagOut, EGinkgoMechanismEvent::MagIn}, Rifle);
	TestTrue(TEXT("后备 12 发：装上 12 发的弹匣，后备空了"), State.Feed.Num() == 12 && Few.Count(Rifle) == 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismTubeTest, "CaseGinkgo.Weapon.Mechanism.TubeMagazine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismTubeTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	const FGinkgoMagazineRules Rules = PumpShotgun();
	const FGameplayTag Shell = FTestTags::GinkgoTest_Ammo_Shell;
	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Shell, 8);
	FGinkgoMechanismState State;
	TestEqual(TEXT("关着枪机也能从装弹口压，压到满（5 发）为止"), ApplyAll(Rules, State, Reserve, {EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::RoundIn,
		EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::RoundIn}, Shell), 5);
	TestFalse(TEXT("膛空打不响"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag()));
	ApplyAll(Rules, State, Reserve, {EGinkgoMechanismEvent::ActionOpen, EGinkgoMechanismEvent::ActionClose}, FGameplayTag());
	TestTrue(TEXT("泵动一次上膛：管里 4 发 + 膛 1"), State.Feed.Num() == 4 && State.CanFire());
	ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag());
	TestTrue(TEXT("手动的：开火以后膛里是空壳"), State.Chamber == EGinkgoChamber::Spent);
	TestTrue(TEXT("关着枪机补一发"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundIn, Shell));
	ApplyAll(Rules, State, Reserve, {EGinkgoMechanismEvent::ActionOpen, EGinkgoMechanismEvent::ActionClose}, FGameplayTag());
	TestTrue(TEXT("泵动：抛壳、上膛，管里 4 发"), State.CanFire() && State.Feed.Num() == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismTubeTopOffTest, "CaseGinkgo.Weapon.Mechanism.TubeTopOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismTubeTopOffTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	using Ginkgo::Weapon::CanLoadAfterCycle;
	using Ginkgo::Weapon::CanLoadRound;
	// 泵动霰弹枪 4 + 1（Docs/Design/ActionStates.md「霰弹枪」）：从空的压满管，膛还空着时不算压完，泵一下上膛再补一发
	const FGinkgoMagazineRules Rules = MakeRules(EGinkgoFeed::TubeMagazine, 4, /*bLoadNeedsOpenAction*/ false, /*bSelfLoading*/ false);
	const FGameplayTag Shell = FTestTags::GinkgoTest_Ammo_Shell;
	const FGinkgoMechanismState New = MakeLoadedState(Rules, Shell);
	TestTrue(TEXT("新拿到的：管里 4 发 + 膛 1"), New.Feed.Num() == 4 && New.CanFire());

	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Shell, 10);
	FGinkgoMechanismState Empty;
	TestTrue(TEXT("全空：压得下"), CanLoadRound(Rules, Empty, Reserve, Shell));
	TestFalse(TEXT("全空：还压得下，不用泵"), CanLoadAfterCycle(Rules, Empty, Reserve, Shell));
	ApplyAll(Rules, Empty, Reserve, {EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::RoundIn}, Shell);
	TestFalse(TEXT("管满了、膛空：压不下"), CanLoadRound(Rules, Empty, Reserve, Shell));
	TestTrue(TEXT("管满了、膛空、后备有：泵一下还能补一发"), CanLoadAfterCycle(Rules, Empty, Reserve, Shell));
	FGinkgoAmmoReserve None;
	TestFalse(TEXT("后备没有：不补"), CanLoadAfterCycle(Rules, Empty, None, Shell));
	ApplyMechanismEvent(Rules, Empty, Reserve, EGinkgoMechanismEvent::ActionOpen, FGameplayTag());
	TestTrue(TEXT("泵到一半（枪机开着）：还算，推回去就上膛"), CanLoadAfterCycle(Rules, Empty, Reserve, Shell));
	ApplyMechanismEvent(Rules, Empty, Reserve, EGinkgoMechanismEvent::ActionClose, FGameplayTag());
	TestTrue(TEXT("泵一下：膛 1、管 3"), Empty.CanFire() && Empty.Feed.Num() == 3);
	TestFalse(TEXT("膛里有了：不用再泵"), CanLoadAfterCycle(Rules, Empty, Reserve, Shell));
	TestTrue(TEXT("补一发"), ApplyMechanismEvent(Rules, Empty, Reserve, EGinkgoMechanismEvent::RoundIn, Shell));
	TestTrue(TEXT("4 + 1，后备少了 5 发"), Empty.Feed.Num() == 4 && Empty.CanFire() && Reserve.Count(Shell) == 5);
	ApplyMechanismEvent(Rules, Empty, Reserve, EGinkgoMechanismEvent::Fire, FGameplayTag());
	TestTrue(TEXT("满的开一枪：膛里空壳、管满，泵一下还能补"), CanLoadAfterCycle(Rules, Empty, Reserve, Shell));

	FGinkgoMechanismState Mauser = MakeLoadedState(Mauser98k(), Standard());
	Mauser.Chamber = EGinkgoChamber::Empty;
	Mauser.ChamberAmmo = FGameplayTag();
	Mauser.Feed.Init(Standard(), 5);
	FGinkgoAmmoReserve Rifle;
	Rifle.Add(Standard(), 5);
	TestFalse(TEXT("98k 压弹要开栓：没有泵一下再补这回事"), CanLoadAfterCycle(Mauser98k(), Mauser, Rifle, Standard()));
	TestFalse(TEXT("可拆弹匣：没有"), CanLoadAfterCycle(Kalashnikov(), FGinkgoMechanismState(), Rifle, Standard()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismInvariantsTest, "CaseGinkgo.Weapon.Mechanism.Invariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismInvariantsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	// 在任意两个事件之间打断都要停在一致的状态上：随机的一串事件（固定种子）里，每一步之后
	// 活弹只在开火时少一发，别的事件不生不灭；供弹不超过容量；空膛没有弹种、有东西的膛有；只有可拆弹匣会不在枪上
	const TPair<const TCHAR*, FGinkgoMagazineRules> Cases[] = {
		{TEXT("98k"), Mauser98k()}, {TEXT("AK"), Kalashnikov()}, {TEXT("泵动霰弹枪"), PumpShotgun()}, {TEXT("M79"), BreakLauncher()} };
	const FGameplayTag Kinds[] = {Standard(), FTestTags::GinkgoTest_Ammo_Special};
	for (const TPair<const TCHAR*, FGinkgoMagazineRules>& Case : Cases)
	{
		FRandomStream Random(20261030);
		FGinkgoAmmoReserve Reserve;
		Reserve.Add(Kinds[0], 40);
		Reserve.Add(Kinds[1], 5);
		FGinkgoMechanismState State = MakeLoadedState(Case.Value, Kinds[0]);
		int32 Total = TotalLive(State, Reserve);
		int32 Broken = 0;
		for (int32 Step = 0; Step < 4000 && Broken == 0; ++Step)
		{
			const EGinkgoMechanismEvent Event = static_cast<EGinkgoMechanismEvent>(Random.RandRange(0, static_cast<int32>(EGinkgoMechanismEvent::RoundChamber)));
			const bool bDone = ApplyMechanismEvent(Case.Value, State, Reserve, Event, Kinds[Random.RandRange(0, 1)]);
			Total -= bDone && Event == EGinkgoMechanismEvent::Fire ? 1 : 0;
			const bool bConsistent = TotalLive(State, Reserve) == Total && State.Feed.Num() <= Case.Value.Capacity
				&& (State.Chamber == EGinkgoChamber::Empty) == !State.ChamberAmmo.IsValid()
				&& (State.bMagazineIn || Case.Value.Feed == EGinkgoFeed::DetachableMagazine);
			if (!bConsistent)
			{
				++Broken;
				AddError(FString::Printf(TEXT("%s 第 %d 步（%s）以后不一致"), Case.Key, Step, *StaticEnum<EGinkgoMechanismEvent>()->GetNameStringByValue(static_cast<int64>(Event))));
			}
		}
		TestEqual(FString::Printf(TEXT("%s：4000 个随机事件里每一步都一致"), Case.Key), Broken, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismCanLoadTest, "CaseGinkgo.Weapon.Mechanism.CanLoadRound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismCanLoadTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	using Ginkgo::Weapon::CanLoadRound;
	const FGameplayTag Ammo = FWeaponTags::Ammo_Mauser792x57_Standard;
	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Ammo, 10);
	FGinkgoAmmoReserve None;

	FGinkgoMechanismState Full = MakeLoadedState(Mauser98k(), Ammo);
	TestEqual(TEXT("98k 上好膛是 4 + 1"), Full.Feed.Num(), 4);
	TestFalse(TEXT("4 + 1 就是满的：开栓再压，闭栓以后还是 4 + 1"), CanLoadRound(Mauser98k(), Full, Reserve, Ammo));

	FGinkgoMechanismState Fired = Full;
	ApplyMechanismEvent(Mauser98k(), Fired, Reserve, EGinkgoMechanismEvent::Fire, Ammo);
	TestTrue(TEXT("膛里是空壳：压得下"), CanLoadRound(Mauser98k(), Fired, Reserve, Ammo));
	TestFalse(TEXT("后备没有：压不了"), CanLoadRound(Mauser98k(), Fired, None, Ammo));

	FGinkgoMechanismState Open = Full;
	FGinkgoAmmoReserve Taken = Reserve;
	ApplyMechanismEvent(Mauser98k(), Open, Taken, EGinkgoMechanismEvent::ActionOpen, Ammo);
	TestEqual(TEXT("开栓时活弹回到后备"), Taken.Count(Ammo), 11);
	TestTrue(TEXT("栓开着比弹仓：4 发还压得下一发"), CanLoadRound(Mauser98k(), Open, Taken, Ammo));
	ApplyMechanismEvent(Mauser98k(), Open, Taken, EGinkgoMechanismEvent::RoundIn, Ammo);
	TestFalse(TEXT("弹仓 5 发压不下了"), CanLoadRound(Mauser98k(), Open, Taken, Ammo));

	FGinkgoMechanismState Tube = MakeLoadedState(PumpShotgun(), Ammo);
	TestTrue(TEXT("霰弹枪新拿到的上好膛又补满了：管满 + 膛 1"), Tube.Feed.Num() == PumpShotgun().Capacity && Tube.CanFire());
	TestFalse(TEXT("霰弹枪关着枪机比管：满的压不下"), CanLoadRound(PumpShotgun(), Tube, Reserve, Ammo));
	ApplyMechanismEvent(PumpShotgun(), Tube, Reserve, EGinkgoMechanismEvent::Fire, Ammo);
	ApplyMechanismEvent(PumpShotgun(), Tube, Reserve, EGinkgoMechanismEvent::ActionOpen, Ammo);
	ApplyMechanismEvent(PumpShotgun(), Tube, Reserve, EGinkgoMechanismEvent::ActionClose, Ammo);
	TestTrue(TEXT("开一枪泵一下，管里空出一个：还能压一发"), CanLoadRound(PumpShotgun(), Tube, Reserve, Ammo));
	ApplyMechanismEvent(PumpShotgun(), Tube, Reserve, EGinkgoMechanismEvent::RoundIn, Ammo);
	TestFalse(TEXT("霰弹枪的管满了"), CanLoadRound(PumpShotgun(), Tube, Reserve, Ammo));

	// 可拆弹匣：换的是整个弹匣，膛里不算
	FGinkgoMechanismState Ak = MakeLoadedState(Kalashnikov(), Ammo);
	TestEqual(TEXT("AK 上好膛是 29 + 1"), Ak.Feed.Num(), 29);
	TestTrue(TEXT("29 + 1 还能换一个满的：30 + 1"), CanLoadRound(Kalashnikov(), Ak, Reserve, Ammo));
	TestFalse(TEXT("后备没有：换不了"), CanLoadRound(Kalashnikov(), Ak, None, Ammo));
	FGinkgoAmmoReserve ForSwap = Reserve;
	ApplyMechanismEvent(Kalashnikov(), Ak, ForSwap, EGinkgoMechanismEvent::MagOut, Ammo);
	TestTrue(TEXT("弹匣不在枪上：能装"), CanLoadRound(Kalashnikov(), Ak, ForSwap, Ammo));
	ApplyMechanismEvent(Kalashnikov(), Ak, ForSwap, EGinkgoMechanismEvent::MagIn, Ammo);
	TestFalse(TEXT("满匣 + 1：不用换了"), CanLoadRound(Kalashnikov(), Ak, ForSwap, Ammo));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismLockOpenTest, "CaseGinkgo.Weapon.Mechanism.LockOpenOnEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismLockOpenTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	using Ginkgo::Weapon::CanLoadRound;
	const FGameplayTag Ammo = FWeaponTags::Ammo_ACP45_Standard;
	FGinkgoMagazineRules Rules;
	Rules.Feed = EGinkgoFeed::DetachableMagazine;
	Rules.Capacity = 7;
	Rules.bLoadNeedsOpenAction = false;
	Rules.bSelfLoading = true;
	Rules.bLockOpenOnEmpty = true;
	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Ammo, 10);

	FGinkgoMechanismState State = MakeLoadedState(Rules, Ammo);
	TestEqual(TEXT("上好膛是 6 + 1"), State.Feed.Num(), 6);
	for (int32 Shot = 0; Shot < 6; ++Shot)
	{
		TestTrue(TEXT("开得了"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, Ammo));
		TestTrue(TEXT("自动上膛：膛里又是活弹"), State.Chamber == EGinkgoChamber::Live);
		TestTrue(TEXT("枪机回去了"), State.Action == EGinkgoAction::Closed);
	}
	TestEqual(TEXT("弹匣打空了，膛里还有一发"), State.Feed.Num(), 0);
	TestTrue(TEXT("最后一发"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, Ammo));
	TestTrue(TEXT("打空了：枪机挂在后面"), State.Action == EGinkgoAction::Open);
	TestTrue(TEXT("膛是空的"), State.Chamber == EGinkgoChamber::Empty);
	TestFalse(TEXT("开不了了"), State.CanFire());
	TestEqual(TEXT("后备没动"), Reserve.Count(Ammo), 10);

	// 空仓换弹：卸下、装上（后备 10 取 7），放枪机回去，上膛
	TestTrue(TEXT("能换"), CanLoadRound(Rules, State, Reserve, Ammo));
	TestTrue(TEXT("卸下"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagOut, Ammo));
	TestTrue(TEXT("装上"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagIn, Ammo));
	TestEqual(TEXT("后备取走 7 发"), Reserve.Count(Ammo), 3);
	TestTrue(TEXT("枪机还挂着"), State.Action == EGinkgoAction::Open);
	TestTrue(TEXT("放回去"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, Ammo));
	TestEqual(TEXT("6 + 1"), State.Feed.Num(), 6);
	TestTrue(TEXT("上了膛"), State.CanFire());

	// 战术换弹：膛里那一发留着，弹匣里的回到后备
	FGinkgoAmmoReserve Tactical;
	Tactical.Add(Ammo, 10);
	FGinkgoMechanismState Fired = MakeLoadedState(Rules, Ammo);
	ApplyMechanismEvent(Rules, Fired, Tactical, EGinkgoMechanismEvent::Fire, Ammo);
	TestEqual(TEXT("开了一枪：5 + 1"), Fired.Feed.Num(), 5);
	ApplyMechanismEvent(Rules, Fired, Tactical, EGinkgoMechanismEvent::MagOut, Ammo);
	TestEqual(TEXT("卸下的 5 发回到后备"), Tactical.Count(Ammo), 15);
	ApplyMechanismEvent(Rules, Fired, Tactical, EGinkgoMechanismEvent::MagIn, Ammo);
	TestEqual(TEXT("装满 7 发"), Fired.Feed.Num(), 7);
	TestEqual(TEXT("后备 8"), Tactical.Count(Ammo), 8);
	TestTrue(TEXT("膛里那一发还在：7 + 1"), Fired.CanFire());
	TestFalse(TEXT("满匣 + 1：不用换了"), CanLoadRound(Rules, Fired, Tactical, Ammo));

	// 没有挂机的自动枪（AK）：打空了枪机照样回去
	FGinkgoMagazineRules NoLock = Rules;
	NoLock.bLockOpenOnEmpty = false;
	FGinkgoMechanismState Last = MakeLoadedState(NoLock, Ammo);
	Last.Feed.Reset();
	ApplyMechanismEvent(NoLock, Last, Reserve, EGinkgoMechanismEvent::Fire, Ammo);
	TestTrue(TEXT("没有挂机：枪机关着"), Last.Action == EGinkgoAction::Closed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismOpenBoltTest, "CaseGinkgo.Weapon.Mechanism.OpenBoltBelt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismOpenBoltTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	// MG3（Private/anim/MG3/README.txt）：开膛待击，弹链从一个弹箱里走，游戏里不装填（你定的），250 发
	FGinkgoMagazineRules Rules = MakeRules(EGinkgoFeed::Belt, 250, /*bLoadNeedsOpenAction*/ false, /*bSelfLoading*/ true);
	Rules.bOpenBolt = true;
	const FGameplayTag Ball = FWeaponTags::Ammo_NATO762x51_Ball;
	FGinkgoMechanismState State = MakeLoadedState(Rules, Ball);
	TestEqual(TEXT("新拿到的：弹箱里 250 发"), State.Feed.Num(), 250);
	TestTrue(TEXT("枪机挂在后面、膛空（没有膛里多一发）"), State.Action == EGinkgoAction::Open && State.Chamber == EGinkgoChamber::Empty);
	TestTrue(TEXT("能开火：看枪机与供弹，不看膛"), Ginkgo::Weapon::CanFire(Rules, State));
	TestFalse(TEXT("闭膛的规则会说不能开火"), State.CanFire());

	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Ball, 100);
	TestTrue(TEXT("开一枪"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, Ball));
	TestTrue(TEXT("开一枪：少一发，枪机回到后面，膛又是空的"), State.Feed.Num() == 249 && State.Action == EGinkgoAction::Open && State.Chamber == EGinkgoChamber::Empty);
	int32 Fired = 1;
	while (ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, Ball))
	{
		++Fired;
	}
	TestEqual(TEXT("一共 250 发"), Fired, 250);
	TestTrue(TEXT("打空：枪机走到前面停在空膛上（没有空仓挂机）"), State.Feed.IsEmpty() && State.Action == EGinkgoAction::Closed && State.Chamber == EGinkgoChamber::Empty);
	TestFalse(TEXT("打空了不能开火"), Ginkgo::Weapon::CanFire(Rules, State));

	// 不装填：后备有这种弹也不行，压弹、换弹匣都不做
	TestFalse(TEXT("不能装填"), Ginkgo::Weapon::CanLoadRound(Rules, State, Reserve, Ball));
	TestFalse(TEXT("泵一下也压不了"), Ginkgo::Weapon::CanLoadAfterCycle(Rules, State, Reserve, Ball));
	TestEqual(TEXT("压弹、卸弹匣、装弹匣都不做"), ApplyAll(Rules, State, Reserve, {EGinkgoMechanismEvent::RoundIn, EGinkgoMechanismEvent::MagOut, EGinkgoMechanismEvent::MagIn}, Ball), 0);
	TestEqual(TEXT("后备没动"), Reserve.Count(Ball), 100);

	// 闭膛的枪不受影响：CanFire 同状态自己的
	const FGinkgoMagazineRules Closed = Kalashnikov();
	const FGinkgoMechanismState Loaded = MakeLoadedState(Closed, Standard());
	TestEqual(TEXT("闭膛的枪：同 FGinkgoMechanismState::CanFire"), Ginkgo::Weapon::CanFire(Closed, Loaded), Loaded.CanFire());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponMechanismSingleShotTest, "CaseGinkgo.Weapon.Mechanism.SingleShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponMechanismSingleShotTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoWeaponMechanismTests;
	using Ginkgo::Weapon::CanLoadRound;
	using Ginkgo::Weapon::SelectWeaponAction;

	const FGinkgoMagazineRules Rules = BreakLauncher();
	const FGameplayTag Grenade = FWeaponTags::Ammo_Grenade40x46_HE;
	FGinkgoAmmoReserve Reserve;
	Reserve.Add(Grenade, 5);

	// 1. 初始满装：无供弹仓，炮膛内 1 发活弹，闭膛可发射
	FGinkgoMechanismState State = MakeLoadedState(Rules, Grenade);
	TestTrue(TEXT("M79 新拿到：无供弹仓，膛里 1 发活弹，闭膛可击发"), State.Feed.IsEmpty() && State.Chamber == EGinkgoChamber::Live && State.Action == EGinkgoAction::Closed && State.CanFire());

	// 2. 有弹重装填（折开 -> 高举重力甩出旧榴弹回后备 -> 装入新榴弹 -> 合膛）：净发数守恒
	{
		FGinkgoWeaponActionFacts Facts;
		Facts.bSingleShot = true;
		Facts.bCanFire = true;
		Facts.bWantsReload = true;
		Facts.bCanLoad = CanLoadRound(Rules, State, Reserve, Grenade);
		TestTrue(TEXT("膛里有活弹且后备有弹：按装填走有弹重装填（ReloadTactical）"), Facts.bCanLoad && SelectWeaponAction(Facts) == EGinkgoWeaponAction::ReloadTactical);

		TestTrue(TEXT("折开炮管：ActionOpen"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, Grenade));
		TestTrue(TEXT("折开瞬间活弹仍在敞开膛内，等待高举重力甩出"), State.Action == EGinkgoAction::Open && State.Chamber == EGinkgoChamber::Live && Reserve.Count(Grenade) == 5);
		TestTrue(TEXT("高举甩弹脱膛：MagOut 将旧活弹退回后备"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagOut, Grenade));
		TestTrue(TEXT("脱膛后膛空，后备临时 +1"), State.Chamber == EGinkgoChamber::Empty && Reserve.Count(Grenade) == 6);
		TestTrue(TEXT("装入新榴弹：RoundChamber"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundChamber, Grenade));
		TestTrue(TEXT("新榴弹入膛，后备回到 5"), State.Chamber == EGinkgoChamber::Live && Reserve.Count(Grenade) == 5);

		// 若装入新弹后未合膛即切枪，再切回时不在重新装填，应直接选 CloseBolt 合膛
		FGinkgoWeaponActionFacts ResumeClose;
		ResumeClose.bSingleShot = true;
		ResumeClose.bActionOpen = true;
		TestEqual(TEXT("新弹入膛未合膛：切回直接选 CloseBolt"), SelectWeaponAction(ResumeClose), EGinkgoWeaponAction::CloseBolt);

		TestTrue(TEXT("合膛闭锁：ActionClose"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, Grenade));
		TestTrue(TEXT("有弹换弹完成：闭膛可击发，总弹数守恒"), State.CanFire() && Reserve.Count(Grenade) == 5);
	}

	// 3. 击发与空仓换弹（击发 -> 膛内空壳 -> 折开 -> 捏出空壳 -> 装入新弹 -> 合膛）
	{
		TestTrue(TEXT("开火击发"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, Grenade));
		TestTrue(TEXT("击发后膛内留空壳、炮膛仍闭锁"), State.Chamber == EGinkgoChamber::Spent && State.Action == EGinkgoAction::Closed && !State.CanFire());

		FGinkgoWeaponActionFacts EmptyReload;
		EmptyReload.bSingleShot = true;
		EmptyReload.bChamberSpent = true;
		EmptyReload.bWantsReload = true;
		EmptyReload.bCanLoad = CanLoadRound(Rules, State, Reserve, Grenade);
		TestEqual(TEXT("膛内空壳按装填：选空仓换弹（ReloadEmpty）"), SelectWeaponAction(EmptyReload), EGinkgoWeaponAction::ReloadEmpty);

		TestTrue(TEXT("折开炮管"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, Grenade));
		TestTrue(TEXT("捏出空铝壳：空壳不进后备"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagOut, Grenade)
			&& State.Chamber == EGinkgoChamber::Empty && Reserve.Count(Grenade) == 5);
		TestTrue(TEXT("塞入新榴弹并合膛"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundChamber, Grenade)
			&& ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, Grenade));
		TestTrue(TEXT("空仓换弹后可再次击发，后备少 1 发"), State.CanFire() && Reserve.Count(Grenade) == 4);
	}

	// 4. 三种弹头切换：膛里装着高爆弹时换装闪光弹/镖弹，折开高举将高爆弹退回高爆后备，再从新弹种后备取 1 发入膛
	{
		const FGameplayTag Flash = FWeaponTags::Ammo_Grenade40x46_Flash;
		const FGameplayTag Flechette = FWeaponTags::Ammo_Grenade40x46_Flechette;
		Reserve.Add(Flash, 3);
		Reserve.Add(Flechette, 4);

		TestTrue(TEXT("有高爆弹在膛时能换装闪光弹"), CanLoadRound(Rules, State, Reserve, Flash));
		TestTrue(TEXT("折开并高举甩出原有高爆弹"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, Flash)
			&& ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagOut, Flash));
		TestEqual(TEXT("原有高爆弹回到高爆后备（4 -> 5）"), Reserve.Count(Grenade), 5);
		TestTrue(TEXT("装入闪光弹并合膛"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundChamber, Flash)
			&& ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, Flash));
		TestTrue(TEXT("当前膛内为闪光弹，闪光弹后备剩 2"), State.CanFire() && State.ChamberAmmo == Flash && Reserve.Count(Flash) == 2);

		TestTrue(TEXT("击发闪光弹后空仓换入镖弹"), ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::Fire, Flash)
			&& ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionOpen, Flechette)
			&& ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::MagOut, Flechette)
			&& ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::RoundChamber, Flechette)
			&& ApplyMechanismEvent(Rules, State, Reserve, EGinkgoMechanismEvent::ActionClose, Flechette));
		TestTrue(TEXT("当前膛内为镖弹，闪光空壳不回后备（仍为 2），镖弹后备剩 3"), State.CanFire() && State.ChamberAmmo == Flechette
			&& Reserve.Count(Flash) == 2 && Reserve.Count(Flechette) == 3 && Reserve.Count(Grenade) == 5);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

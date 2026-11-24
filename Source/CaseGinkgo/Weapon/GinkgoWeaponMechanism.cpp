#include "Weapon/GinkgoWeaponMechanism.h"

namespace
{
	/** 膛空、供弹有弹（弹匣在枪上）时把最上面那一发推进膛 */
	void ChamberFromFeed(FGinkgoMechanismState& State)
	{
		if (State.Chamber != EGinkgoChamber::Empty || !State.bMagazineIn || State.Feed.IsEmpty())
		{
			return;
		}
		State.Chamber = EGinkgoChamber::Live;
		State.ChamberAmmo = State.Feed.Pop(EAllowShrinking::No);
	}

	/** 膛里的东西出来：空壳抛掉，活弹扣下来回到后备 */
	void ClearChamber(FGinkgoMechanismState& State, FGinkgoAmmoReserve& Reserve)
	{
		if (State.Chamber == EGinkgoChamber::Live)
		{
			Reserve.Add(State.ChamberAmmo, 1);
		}
		State.Chamber = EGinkgoChamber::Empty;
		State.ChamberAmmo = FGameplayTag();
	}
}

// ===== FGinkgoMechanismState =====

bool FGinkgoMechanismState::operator==(const FGinkgoMechanismState& Other) const
{
	return Feed == Other.Feed && bMagazineIn == Other.bMagazineIn && Chamber == Other.Chamber && ChamberAmmo == Other.ChamberAmmo && Action == Other.Action;
}

// ===== FGinkgoAmmoReserve =====

int32 FGinkgoAmmoReserve::Count(const FGameplayTag& Ammo) const
{
	const int32* Found = Rounds.Find(Ammo);
	return Found ? *Found : 0;
}

void FGinkgoAmmoReserve::Add(const FGameplayTag& Ammo, const int32 InRounds)
{
	if (Ammo.IsValid() && InRounds > 0)
	{
		Rounds.FindOrAdd(Ammo) += InRounds;
	}
}

bool FGinkgoAmmoReserve::TakeOne(const FGameplayTag& Ammo)
{
	return TakeUpTo(Ammo, 1) == 1;
}

int32 FGinkgoAmmoReserve::TakeUpTo(const FGameplayTag& Ammo, const int32 MaxRounds)
{
	int32* Found = Rounds.Find(Ammo);
	const int32 Taken = Found ? FMath::Clamp(MaxRounds, 0, *Found) : 0;
	if (Taken > 0)
	{
		*Found -= Taken;
	}
	return Taken;
}

// ===== 规则 =====

bool Ginkgo::Weapon::ApplyMechanismEvent(const FGinkgoMagazineRules& Rules, FGinkgoMechanismState& State, FGinkgoAmmoReserve& Reserve,
	const EGinkgoMechanismEvent Event, const FGameplayTag& Ammo)
{
	const bool bDetachable = Rules.Feed == EGinkgoFeed::DetachableMagazine;
	const bool bBelt = Rules.Feed == EGinkgoFeed::Belt;
	const bool bSingleShot = Rules.Feed == EGinkgoFeed::SingleShot;
	switch (Event)
	{
	case EGinkgoMechanismEvent::Fire:
		if (!CanFire(Rules, State))
		{
			return false;
		}
		// 开膛待击：枪机往前从供弹取最上面那一发送进膛、击发，空壳在枪机回来时抛出，枪机停在后面，膛又是空的；
		// 供弹打空了枪机回来没有东西挂住，走到前面停在空膛上
		if (Rules.bOpenBolt)
		{
			State.Feed.Pop(EAllowShrinking::No);
			State.Chamber = EGinkgoChamber::Empty;
			State.ChamberAmmo = FGameplayTag();
			State.Action = State.Feed.IsEmpty() ? EGinkgoAction::Closed : EGinkgoAction::Open;
			return true;
		}
		// 这一发的空壳留在膛里（弹种照旧）；自动装填的在同一个事件里抛壳、上膛，枪机不停在后方（AK 打空后也不停）
		State.Chamber = EGinkgoChamber::Spent;
		if (Rules.bSelfLoading)
		{
			ClearChamber(State, Reserve);
			ChamberFromFeed(State);
			// 没有弹匣时真枪的挂机杆没有东西顶，枪机会回去；这里也挂住：不然多一种"枪机关着、膛空"，换上弹匣还要拉一次套筒
			if (Rules.bLockOpenOnEmpty && State.Chamber == EGinkgoChamber::Empty)
			{
				State.Action = EGinkgoAction::Open;
			}
		}
		return true;

	case EGinkgoMechanismEvent::ActionOpen:
		if (State.Action != EGinkgoAction::Closed)
		{
			return false;
		}
		State.Action = EGinkgoAction::Open;
		// 中折单发（M79）无弹簧抛壳挺，折开炮管只将底缘微退 9.5mm，空壳或活弹留在敞开膛内，由后续 MagOut（捏取退壳 / 高举重力甩弹）退出
		if (!bSingleShot)
		{
			ClearChamber(State, Reserve);
		}
		return true;

	case EGinkgoMechanismEvent::ActionClose:
		if (State.Action != EGinkgoAction::Open)
		{
			return false;
		}
		State.Action = EGinkgoAction::Closed;
		ChamberFromFeed(State);
		return true;

	case EGinkgoMechanismEvent::RoundIn:
		// 逐发压的供弹才有（弹链、单发不进供弹仓）；先看前提，最后才从后备取（取不到时什么也没改）
		if (bDetachable || bBelt || bSingleShot || State.Feed.Num() >= Rules.Capacity || (Rules.bLoadNeedsOpenAction && State.Action != EGinkgoAction::Open)
			|| !Reserve.TakeOne(Ammo))
		{
			return false;
		}
		State.Feed.Add(Ammo);
		return true;

	case EGinkgoMechanismEvent::MagOut:
		if (bSingleShot)
		{
			if (State.Action != EGinkgoAction::Open || State.Chamber == EGinkgoChamber::Empty)
			{
				return false;
			}
			ClearChamber(State, Reserve);
			return true;
		}
		if (!bDetachable || !State.bMagazineIn)
		{
			return false;
		}
		for (const FGameplayTag& Round : State.Feed)
		{
			Reserve.Add(Round, 1);
		}
		State.Feed.Reset();
		State.bMagazineIn = false;
		return true;

	case EGinkgoMechanismEvent::MagIn:
		if (!bDetachable || State.bMagazineIn || !Ammo.IsValid())
		{
			return false;
		}
		// 后备不够时装上的是没满的弹匣
		State.Feed.Init(Ammo, Reserve.TakeUpTo(Ammo, Rules.Capacity));
		State.bMagazineIn = true;
		return true;

	case EGinkgoMechanismEvent::RoundChamber:
		// 从抛壳口放进膛：枪机拉到底时膛里的已经出来了（ActionOpen）。同 RoundIn，前提都满足了才从后备取
		if (!Rules.bCanLoadChamber || State.Action != EGinkgoAction::Open || State.Chamber != EGinkgoChamber::Empty || !Reserve.TakeOne(Ammo))
		{
			return false;
		}
		State.Chamber = EGinkgoChamber::Live;
		State.ChamberAmmo = Ammo;
		return true;
	}
	return false;
}

bool Ginkgo::Weapon::CanLoadRound(const FGinkgoMagazineRules& Rules, const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo)
{
	// 弹链只有挂在枪上的那一个弹箱（MG3，你定的）：不装填
	if (Rules.Feed == EGinkgoFeed::Belt || !Ammo.IsValid() || Reserve.Count(Ammo) <= 0)
	{
		return false;
	}
	// 单发中折（M79）：无弹时按装填走空仓换弹，膛内已有活弹时按装填走有弹重装填（折开高举重力甩弹并换入新弹）
	if (Rules.Feed == EGinkgoFeed::SingleShot)
	{
		return true;
	}
	if (Rules.Feed == EGinkgoFeed::DetachableMagazine)
	{
		return !State.bMagazineIn || State.Feed.Num() < Rules.Capacity;
	}
	// 压弹要开着枪机的（98k），枪机关着时要先开栓，膛里的活弹扣下来回到后备，闭栓时又推一发进去：比的是枪里总共能打的发数。98k 的 4 + 1 就是满的。
	// 膛里那一发不是要压的弹种（换了弹种）：开栓把它退回后备，比的是弹仓，压进去的这一种闭栓时进膛
	const bool bChamberSwapped = State.Chamber == EGinkgoChamber::Live && State.ChamberAmmo != Ammo;
	const bool bCountChamber = Rules.bLoadNeedsOpenAction && State.Action == EGinkgoAction::Closed && !bChamberSwapped;
	return (bCountChamber ? State.CountLiveRounds() : State.Feed.Num()) < Rules.Capacity;
}

bool Ginkgo::Weapon::CanLoadChamber(const FGinkgoMagazineRules& Rules, const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo)
{
	return Rules.bCanLoadChamber && Ammo.IsValid() && Reserve.Count(Ammo) > 0 && !(State.Chamber == EGinkgoChamber::Live && State.ChamberAmmo == Ammo);
}

FGameplayTag Ginkgo::Weapon::GetNextRoundAmmo(const FGinkgoMechanismState& State)
{
	if (State.Chamber == EGinkgoChamber::Live)
	{
		return State.ChamberAmmo;
	}
	return State.bMagazineIn && !State.Feed.IsEmpty() ? State.Feed.Last() : FGameplayTag();
}

bool Ginkgo::Weapon::CanLoadAfterCycle(const FGinkgoMagazineRules& Rules, const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo)
{
	// 枪机开着也算（泵到一半：推回去就上膛）；膛里是空壳也算（开完一枪先泵，管里空出一个）。逐发压的才有（弹链、可拆弹匣没有）
	const bool bLoadsByRound = Rules.Feed == EGinkgoFeed::InternalMagazine || Rules.Feed == EGinkgoFeed::TubeMagazine;
	return bLoadsByRound && !Rules.bLoadNeedsOpenAction && State.Chamber != EGinkgoChamber::Live
		&& State.Feed.Num() >= Rules.Capacity && Reserve.Count(Ammo) > 0;
}

bool Ginkgo::Weapon::CanFire(const FGinkgoMagazineRules& Rules, const FGinkgoMechanismState& State)
{
	return Rules.bOpenBolt ? State.Action == EGinkgoAction::Open && State.bMagazineIn && !State.Feed.IsEmpty() : State.CanFire();
}

FGinkgoMechanismState Ginkgo::Weapon::MakeLoadedState(const FGinkgoMagazineRules& Rules, const FGameplayTag& Ammo)
{
	FGinkgoMechanismState State;
	if (Rules.Feed == EGinkgoFeed::SingleShot)
	{
		if (Ammo.IsValid())
		{
			State.Chamber = EGinkgoChamber::Live;
			State.ChamberAmmo = Ammo;
		}
		return State;
	}
	if (Ammo.IsValid())
	{
		State.Feed.Init(Ammo, FMath::Max(Rules.Capacity, 0));
	}
	// 开膛待击：拉过机柄，枪机挂在后面，膛空，弹箱里多少发就是多少（MG3 是 250）
	if (Rules.bOpenBolt)
	{
		State.Action = State.Feed.IsEmpty() ? EGinkgoAction::Closed : EGinkgoAction::Open;
		return State;
	}
	ChamberFromFeed(State);
	// 关着枪机就能压的（管状弹仓）上好膛以后再补满：霰弹枪 4 + 1。98k 压弹要开栓（补不了），可拆弹匣不补（满匣 + 1 要换弹匣时才有）
	if (Ammo.IsValid() && Rules.Feed != EGinkgoFeed::DetachableMagazine && !Rules.bLoadNeedsOpenAction)
	{
		while (State.Feed.Num() < Rules.Capacity)
		{
			State.Feed.Add(Ammo);
		}
	}
	return State;
}

// ===== UGinkgoMagazineMechanism =====

FGinkgoMechanismState UGinkgoMagazineMechanism::MakeLoadedState(const FGameplayTag& Ammo) const
{
	return Ginkgo::Weapon::MakeLoadedState(Rules, Ammo);
}

bool UGinkgoMagazineMechanism::CanLoadRound(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const
{
	return Ginkgo::Weapon::CanLoadRound(Rules, State, Reserve, Ammo);
}

bool UGinkgoMagazineMechanism::CanLoadAfterCycle(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const
{
	return Ginkgo::Weapon::CanLoadAfterCycle(Rules, State, Reserve, Ammo);
}

bool UGinkgoMagazineMechanism::CanLoadChamber(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const
{
	return Ginkgo::Weapon::CanLoadChamber(Rules, State, Reserve, Ammo);
}

bool UGinkgoMagazineMechanism::CanFire(const FGinkgoMechanismState& State) const
{
	return Ginkgo::Weapon::CanFire(Rules, State);
}

FGameplayTag UGinkgoMagazineMechanism::GetFiringAmmo(const FGinkgoMechanismState& State) const
{
	// 开膛待击：开火时从弹链最上面取一发（Fire 里 Feed.Pop），膛一直空着
	if (Rules.bOpenBolt)
	{
		return State.Feed.IsEmpty() ? FGameplayTag() : State.Feed.Last();
	}
	return Super::GetFiringAmmo(State);
}

bool UGinkgoMagazineMechanism::ApplyEvent(FGinkgoMechanismState& State, FGinkgoAmmoReserve& Reserve, const EGinkgoMechanismEvent Event, const FGameplayTag& Ammo) const
{
	return Ginkgo::Weapon::ApplyMechanismEvent(Rules, State, Reserve, Event, Ammo);
}

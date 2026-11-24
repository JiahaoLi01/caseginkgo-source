#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"         // 每一发记着弹种（标签）
#include "UObject/Object.h"
#include "GinkgoWeaponMechanism.generated.h"

/**
 * @enum EGinkgoChamber : 膛里是什么
 */
UENUM(BlueprintType)
enum class EGinkgoChamber : uint8
{
	/** 空膛 */
	Empty,
	/** 活弹：枪机关着就能开火 */
	Live,
	/** 空壳：开过火，要开一次枪机（拉栓、泵动）抛掉 */
	Spent,
};

/**
 * @enum EGinkgoAction : 枪机
 */
UENUM(BlueprintType)
enum class EGinkgoAction : uint8
{
	Closed,
	Open,
};

/**
 * @enum EGinkgoFeed : 供弹的种类
 */
UENUM(BlueprintType)
enum class EGinkgoFeed : uint8
{
	/** 内置弹仓，逐发压（98k） */
	InternalMagazine,
	/** 管状弹仓，逐发压（泵动霰弹枪） */
	TubeMagazine,
	/** 可拆弹匣，整个换（AK） */
	DetachableMagazine,
	/** 弹链，从挂在枪上的弹箱里走（MG3）：游戏里只有这一个弹箱，不装填（你定的），打完就空了 */
	Belt,

	/** 单发无供弹仓（M79）：只有炮膛内这一发，折开后捏出空壳或高举甩出原有榴弹，再直接往膛里装填新弹 */
	SingleShot,
};

/**
 * @enum EGinkgoMechanismEvent : 改机构状态的事件
 * 一套词，所有枪共用。第 4 批由蒙太奇的通知发；现在只有调试命令 ginkgo.weapon.event。
 * 前提与结果见 Docs/Design/Weapons.md「机构：供弹、膛、枪机」的事件表
 */
UENUM(BlueprintType)
enum class EGinkgoMechanismEvent : uint8
{
	/** 枪机关着、膛里是活弹：膛里变成这一发的空壳；自动装填的接着抛壳、上膛 */
	Fire,
	/** 枪机关着：枪机开，空壳抛出，活弹扣下来回到后备 */
	ActionOpen,
	/** 枪机开着：枪机关；膛空、供弹有弹（弹匣在枪上）就把最上面那一发推进膛 */
	ActionClose,
	/** 逐发压的供弹没满、机构要求时枪机开着、后备里有这种弹：取一发压在最上面 */
	RoundIn,
	/** 可拆弹匣在枪上：卸下，里面的弹回到后备 */
	MagOut,
	/** 可拆弹匣不在枪上：从后备装满一个新弹匣，装上 */
	MagIn,
	/**
	 * 机构允许直接压膛（霰弹枪，FGinkgoMagazineRules::bCanLoadChamber）、枪机开着、膛空、后备里有这种弹：取一发直接放进膛（从抛壳口，紧急装填、换弹种）。
	 * 闭上枪机时膛里已经有了，不再从供弹推
	 */
	RoundChamber,
};

/**
 * @struct FGinkgoMechanismState : 一把枪的机械状态（值类型）
 * 只在本地，同射击（Docs/Design/Weapons.md「联网」）：武器组件每个槽一份，只在事件上改（Ginkgo::Weapon::ApplyMechanismEvent），
 * 被打断时也停在一致的状态上。界面读它画膛、供弹与枪栓
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoMechanismState
{
	GENERATED_BODY()

	/** 供弹里的那一摞，从下往上：最后一个是最上面的，上膛时最先进膛。最多是机构的容量 */
	UPROPERTY(SaveGame, VisibleInstanceOnly, Category = "Mechanism")
	TArray<FGameplayTag> Feed;

	/** 可拆弹匣在不在枪上；别的供弹总在 */
	UPROPERTY(SaveGame, VisibleInstanceOnly, Category = "Mechanism")
	bool bMagazineIn = true;

	/** 膛内的空膛、活弹或空壳状态；读档不把空壳当作可射击弹药。 */
	UPROPERTY(SaveGame, VisibleInstanceOnly, Category = "Mechanism")
	EGinkgoChamber Chamber = EGinkgoChamber::Empty;

	/** 膛里那一发（活弹或空壳）的弹种；空膛时为空 */
	UPROPERTY(SaveGame, VisibleInstanceOnly, Category = "Mechanism")
	FGameplayTag ChamberAmmo;

	/** 枪机开闭状态；动作被打断和检查点恢复时保持机构事件已提交的状态。 */
	UPROPERTY(SaveGame, VisibleInstanceOnly, Category = "Mechanism")
	EGinkgoAction Action = EGinkgoAction::Closed;

	/** 枪里能打的发数：供弹里的加膛里的活弹 */
	int32 CountLiveRounds() const { return Feed.Num() + (Chamber == EGinkgoChamber::Live ? 1 : 0); }

	/** 现在扣扳机能不能响：枪机关着、膛里是活弹 */
	bool CanFire() const { return Action == EGinkgoAction::Closed && Chamber == EGinkgoChamber::Live; }

	bool operator==(const FGinkgoMechanismState& Other) const;
};

/**
 * @struct FGinkgoAmmoReserve : 后备子弹
 * 在武器组件上，按弹种记数，不在枪里：压弹、装弹匣从这里取，开栓时扣下来的活弹、卸下的弹匣里的弹回到这里（只在本地）
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoAmmoReserve
{
	GENERATED_BODY()

	/** 某种弹有几发 */
	int32 Count(const FGameplayTag& Ammo) const;

	/** 加几发；空的弹种、不是正数时不加 */
	void Add(const FGameplayTag& Ammo, int32 InRounds);

	/** @brief 取一发 @return 取到没有：没有这种弹时什么也不改 */
	bool TakeOne(const FGameplayTag& Ammo);

	/** @brief 最多取 MaxRounds 发 @return 取到几发 */
	int32 TakeUpTo(const FGameplayTag& Ammo, int32 MaxRounds);

	/** 弹种 → 发数；取完的留着 0 */
	UPROPERTY(SaveGame, VisibleInstanceOnly, Category = "Mechanism")
	TMap<FGameplayTag, int32> Rounds;
};

/**
 * @struct FGinkgoMagazineRules : 供弹—膛—枪机这一种机构的开关
 * 98k、泵动霰弹枪、AK 的差别都在这几个开关里，规则只写一份（Ginkgo::Weapon::ApplyMechanismEvent）
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoMagazineRules
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanism")
	EGinkgoFeed Feed = EGinkgoFeed::InternalMagazine;

	/** 供弹最多几发（不算膛里那一发）：98k 5，AK 的弹匣 30；单发无供弹仓（M79）为 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanism", meta = (ClampMin = 0))
	int32 Capacity = 5;

	/** 逐发压时枪机要开着：98k 要；泵动霰弹枪从下面的装弹口压，关着就能压 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanism")
	bool bLoadNeedsOpenAction = true;

	/** 自动装填：开火的同一个事件里退壳、上膛（AK）。手动的（拉栓、泵动）开火以后要自己走一遍开、关 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanism")
	bool bSelfLoading = false;

	/** 空仓挂机：自动装填的枪打完最后一发，枪机停在后面（M1911 的挂机杆）。AK 没有这个，打空了枪机照样回去 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanism", meta = (EditCondition = "bSelfLoading"))
	bool bLockOpenOnEmpty = false;

	/**
	 * 开膛待击（MG3）：准备好时枪机在后面、膛是空的，扣扳机枪机才往前从供弹里取一发送进膛、击发，再被打回后面停住，所以没有"膛里多一发"。
	 * 供弹打空时枪机走到前面停在空膛上（没有空仓挂机）。能不能开火看枪机开着、供弹有弹，不看膛
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanism", meta = (EditCondition = "bSelfLoading"))
	bool bOpenBolt = false;

	/**
	 * 能直接压膛：枪机开着时从抛壳口放一发进膛（RoundChamber，紧急装填、换弹种，Docs/Design/Weapons.md「切换弹种」）。泵动霰弹枪能（2026-11-06 你定的）；
	 * 98k 不能：控制供弹，爪式抓壳钩从弹仓里就扣住弹底缘带着走，不是设计来从膛里那一发的底缘上跨过去的（见设计「98k 为什么最多 5 发」）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanism")
	bool bCanLoadChamber = false;
};

/**
 * 机构的规则：写成纯函数，测试直接调（CaseGinkgo.Weapon.Mechanism.*）
 */
namespace Ginkgo::Weapon
{
	/**
	 * @brief 一个事件改机构状态；前提不满足时什么也不改
	 * @param Ammo - RoundIn 压哪种弹、MagIn 装哪种弹；别的事件不用
	 * @return 改了没有
	 * @note 开栓时膛里的活弹按弹种回到后备（你定的）：以后压一发特殊弹就是"开栓 → 压特殊弹 → 闭栓"，活弹不能凭空退回弹仓
	 */
	CASEGINKGO_API bool ApplyMechanismEvent(const FGinkgoMagazineRules& Rules, FGinkgoMechanismState& State, FGinkgoAmmoReserve& Reserve,
		EGinkgoMechanismEvent Event, const FGameplayTag& Ammo);

	/**
	 * @brief 装满、上好膛的状态（新拿到的枪）：供弹压满 Ammo，再闭一次枪机，最上面那一发进膛
	 * @note 98k 是弹仓 4 + 膛 1；AK 是 29 + 1（同空仓换弹拉过机柄）
	 */
	CASEGINKGO_API FGinkgoMechanismState MakeLoadedState(const FGinkgoMagazineRules& Rules, const FGameplayTag& Ammo);

	/**
	 * @brief 按装填还有没有用：逐发压的，压得下、后备有这种弹；可拆弹匣的，弹匣没满或者不在枪上、后备有这种弹
	 * @note 压弹要开着枪机的（98k），枪机关着时比的是枪里总共能打的发数：开栓时膛里的活弹回到后备，闭栓时又推一发进去，所以 4 + 1 就是满的。
	 *       膛里那一发不是要压的弹种时（换了弹种，Docs/Design/Weapons.md「切换弹种」）比的是弹仓：开栓把它退回后备，弹仓没满就压得进这一种，闭栓推进膛的就是它。
	 *       可拆弹匣不比膛：膛里留着一发时换上满的，就是满匣 + 1
	 */
	CASEGINKGO_API bool CanLoadRound(const FGinkgoMagazineRules& Rules, const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo);

	/**
	 * @brief 从抛壳口直接压膛有没有用：机构允许（bCanLoadChamber），后备有这种弹，膛里不是这种弹的活弹（是的话压了等于没压）
	 * @note 枪机关着、膛里有东西也算：压膛那一步先把枪机拉到底，膛里的出来（活弹回后备）
	 */
	CASEGINKGO_API bool CanLoadChamber(const FGinkgoMagazineRules& Rules, const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo);

	/**
	 * @brief 下一枪打出去的是哪种弹：膛里的活弹；膛里没有活弹时是拉一下会进膛的那一发（供弹最上面的，弹匣在枪上）
	 * @return 枪里一发也没有时为空
	 */
	CASEGINKGO_API FGameplayTag GetNextRoundAmmo(const FGinkgoMechanismState& State);

	/**
	 * @brief 现在压不下、泵一下就能再压一发：关着枪机压的（管状弹仓）压满了，膛里不是活弹（空的、空壳；枪机开着也算），后备有这种弹。
	 * 装填到这里不算完：收回来、泵一下上膛，管里空出一个再补一发，按一次装填就是 4 + 1
	 */
	CASEGINKGO_API bool CanLoadAfterCycle(const FGinkgoMagazineRules& Rules, const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo);

	/**
	 * @brief 现在扣扳机能不能响：闭膛的枪机关着、膛里是活弹（FGinkgoMechanismState::CanFire）；开膛待击的枪机在后面、供弹有弹
	 */
	CASEGINKGO_API bool CanFire(const FGinkgoMagazineRules& Rules, const FGinkgoMechanismState& State);
}

/**
 * @class UGinkgoWeaponMechanism : 机构
 * 武器定义里内联的对象（同移动配置里的窗口）：规则在资产里，状态是值类型（FGinkgoMechanismState），在武器组件上。
 * 真正长得不一样的机构（转轮的几个膛、双管）以后另写一个子类，定义里换一个对象，动作状态与界面不用改
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories, BlueprintType)
class CASEGINKGO_API UGinkgoWeaponMechanism : public UObject
{
	GENERATED_BODY()

public:
	/** 新拿到这把枪时的状态 */
	virtual FGinkgoMechanismState MakeLoadedState(const FGameplayTag& Ammo) const PURE_VIRTUAL(UGinkgoWeaponMechanism::MakeLoadedState, return {};);

	/** @brief 一个事件改状态；前提不满足时不改 @return 改了没有 */
	virtual bool ApplyEvent(FGinkgoMechanismState& State, FGinkgoAmmoReserve& Reserve, EGinkgoMechanismEvent Event, const FGameplayTag& Ammo) const
		PURE_VIRTUAL(UGinkgoWeaponMechanism::ApplyEvent, return false;);

	/** 按装填还有没有用：见 Ginkgo::Weapon::CanLoadRound */
	virtual bool CanLoadRound(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const PURE_VIRTUAL(UGinkgoWeaponMechanism::CanLoadRound, return false;);

	/** 压满了、泵一下还能再压：见 Ginkgo::Weapon::CanLoadAfterCycle */
	virtual bool CanLoadAfterCycle(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const { return false; }

	/** 从抛壳口直接压膛有没有用：见 Ginkgo::Weapon::CanLoadChamber。不允许直接压膛的机构总是假 */
	virtual bool CanLoadChamber(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const { return false; }

	/** 机构允许直接压膛（不看现在的状态与后备）：换弹种走压膛还是走装填 */
	virtual bool AllowsChamberLoad() const { return false; }

	/** 能不能装填（弹链不能）：轮盘只给能装填的枪 */
	virtual bool IsReloadable() const { return GetFeed() != EGinkgoFeed::Belt; }

	/** 供弹最多几发（界面按它画格子） */
	virtual int32 GetCapacity() const PURE_VIRTUAL(UGinkgoWeaponMechanism::GetCapacity, return 0;);

	/** 供弹的种类（界面：可拆弹匣卸下时另画） */
	virtual EGinkgoFeed GetFeed() const PURE_VIRTUAL(UGinkgoWeaponMechanism::GetFeed, return EGinkgoFeed::InternalMagazine;);

	/** 现在扣扳机能不能响：见 Ginkgo::Weapon::CanFire */
	virtual bool CanFire(const FGinkgoMechanismState& State) const { return State.CanFire(); }

	/** 开膛待击（动作状态走自己的分支，界面不画膛里那一发、不写"栓开"） */
	virtual bool IsOpenBolt() const { return false; }

	/**
	 * @brief 现在扣扳机打出去的是哪一发的弹种（Docs/Design/Damage.md「命中」）：膛里那一发；开膛待击的是弹链最上面那一发
	 * @return 打不响、或者这一发没记弹种时为空（武器组件用枪的弹种）
	 */
	virtual FGameplayTag GetFiringAmmo(const FGinkgoMechanismState& State) const { return State.Chamber == EGinkgoChamber::Live ? State.ChamberAmmo : FGameplayTag(); }
};

/**
 * @class UGinkgoMagazineMechanism : 供弹—膛—枪机
 * 第一种机构：98k、泵动霰弹枪、AK 都是它，差别是 Rules 里的几个开关
 */
UCLASS(meta = (DisplayName = "Magazine Mechanism"))
class CASEGINKGO_API UGinkgoMagazineMechanism : public UGinkgoWeaponMechanism
{
	GENERATED_BODY()

public:
	//~ Begin UGinkgoWeaponMechanism
	virtual FGinkgoMechanismState MakeLoadedState(const FGameplayTag& Ammo) const override;
	virtual bool ApplyEvent(FGinkgoMechanismState& State, FGinkgoAmmoReserve& Reserve, EGinkgoMechanismEvent Event, const FGameplayTag& Ammo) const override;
	virtual bool CanLoadRound(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const override;
	virtual bool CanLoadAfterCycle(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const override;
	virtual bool CanLoadChamber(const FGinkgoMechanismState& State, const FGinkgoAmmoReserve& Reserve, const FGameplayTag& Ammo) const override;
	virtual bool AllowsChamberLoad() const override { return Rules.bCanLoadChamber && Rules.Feed != EGinkgoFeed::SingleShot; }
	virtual int32 GetCapacity() const override { return Rules.Capacity; }
	virtual EGinkgoFeed GetFeed() const override { return Rules.Feed; }
	virtual bool CanFire(const FGinkgoMechanismState& State) const override;
	virtual bool IsOpenBolt() const override { return Rules.bOpenBolt; }
	virtual FGameplayTag GetFiringAmmo(const FGinkgoMechanismState& State) const override;
	//~ End UGinkgoWeaponMechanism

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mechanism", meta = (ShowOnlyInnerProperties))
	FGinkgoMagazineRules Rules;
};

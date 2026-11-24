#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"             // 穿射一路打到的
#include "GinkgoCombatTypes.generated.h"

class USoundBase;

/** 人身上的部位（Docs/Design/Damage.md「部位」）：子弹对每个部位有倍率，护甲按部位盖 */
UENUM(BlueprintType)
enum class EGinkgoBodyPart : uint8
{
	Head		UMETA(DisplayName = "头部"),
	UpperChest	UMETA(DisplayName = "上胸"),
	Abdomen		UMETA(DisplayName = "腹部"),
	UpperLimbs	UMETA(DisplayName = "上肢"),
	Legs		UMETA(DisplayName = "腿部"),
	Feet		UMETA(DisplayName = "脚"),
};

/** 子弹怎么送到目标（Docs/Design/Damage.md）：射线当场结算；抛射物生成受重力作用的飞行物，触碰后按战斗部类型结算 */
UENUM(BlueprintType)
enum class EGinkgoAmmoDelivery : uint8
{
	Hitscan		UMETA(DisplayName = "射线"),
	Projectile	UMETA(DisplayName = "抛射物"),
};

/** 抛射物弹药（EGinkgoAmmoDelivery::Projectile）触碰引爆时的战斗部效果类型 */
UENUM(BlueprintType)
enum class EGinkgoProjectilePayload : uint8
{
	/** 高爆破片：直击伤害加遮挡检测破片范围伤害，伴随火球与烟尘 */
	HighExplosive	UMETA(DisplayName = "高爆榴弹"),

	/** 碰炸闪光：触碰即触发与手投闪光弹完全一致的近身与视线致盲、强光球及爆闪音效 */
	Flashbang		UMETA(DisplayName = "碰炸闪光弹"),
};

/** @struct FGinkgoBodyPartMultipliers : 子弹对六个部位的倍率 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoBodyPartMultipliers
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float Head = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float UpperChest = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float Abdomen = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float UpperLimbs = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float Legs = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float Feet = 0.6f;

	/** @brief 这个部位的倍率 */
	float Get(EGinkgoBodyPart Part) const;
};

/** @struct FGinkgoDamageFalloff : 距离衰减：满伤害到 FullRange，之后直线降到 MinRange 时的 MinMultiplier，再远不变。MinRange 不比 FullRange 远时不衰减 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoDamageFalloff
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0, Units = "cm"))
	float FullRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0, Units = "cm"))
	float MinRange = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0, ClampMax = 1))
	float MinMultiplier = 1.f;
};

/** @struct FGinkgoArmorPiece : 一件护甲（COD 战区式：护甲是第二条血）：盖几个部位，有自己的耐久 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoArmorPiece
{
	GENERATED_BODY()

	/** 叫什么（日志、靶子上占位的形状按它找） */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadOnly, Category = "Armor")
	FName Name;

	/** 盖哪几个部位 */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadOnly, Category = "Armor")
	TArray<EGinkgoBodyPart> Covers;

	/** 此件护甲的满耐久，单位护甲点数；读档同时恢复上限和剩余值，不触发补满。 */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadOnly, Category = "Armor", meta = (ClampMin = 0))
	float MaxDurability = 100.f;

	/** 还剩多少（运行时，回满时是 MaxDurability） */
	UPROPERTY(SaveGame, VisibleInstanceOnly, BlueprintReadOnly, Category = "Armor")
	float Durability = 0.f;
};

/** @struct FGinkgoTargetSpawn : 开局放的一个人形靶（Docs/Design/Damage.md「靶子」）：在角色面前多远、往右挪多少，穿不穿背心、戴不戴头盔 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoTargetSpawn
{
	GENERATED_BODY()

	FGinkgoTargetSpawn() = default;
	FGinkgoTargetSpawn(const float InForward, const float InRight, const bool bInVest, const bool bInHelmet)
		: Forward(InForward), Right(InRight), bVest(bInVest), bHelmet(bInHelmet)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (Units = "cm"))
	float Forward = 700.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (Units = "cm"))
	float Right = 0.f;

	/** 背心：盖上胸、腹部 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	bool bVest = false;

	/** 头盔：盖头部 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target")
	bool bHelmet = false;
};

/** @struct FGinkgoHitDamage : 一下的数：子弹的伤害、护甲伤害已经乘过枪、部位、距离的倍率 */
struct FGinkgoHitDamage
{
	/** 不挡时扣的血 */
	float Health = 0.f;
	/** 打在护甲上时扣的护甲 */
	float Armor = 0.f;
	/** 穿甲率：护甲还有时直接扣血的那一份（0–1） */
	float Penetration = 0.f;
};

/** @struct FGinkgoDamageOutcome : 一下打完扣了什么 */
struct FGinkgoDamageOutcome
{
	float HealthDamage = 0.f;
	float ArmorDamage = 0.f;
	/** 打在护甲上（这件护甲打之前还有） */
	bool bArmorHit = false;
	/** 这一下把护甲打穿了 */
	bool bArmorBroken = false;
};

/**
 * @struct FGinkgoHitConfirm : 命中的白 X（Docs/Design/Damage.md「命中的白 X」）：一枪打中了可打击的目标就记一次，界面看 Serial 变了就弹一次。
 * 霰弹一枪几颗弹丸算一次，Hits 是打中了几下
 */
struct FGinkgoHitConfirm
{
	/** 第几次（打中了目标的那几枪数下来） */
	int32 Serial = 0;
	/** 这一枪打中几下 */
	int32 Hits = 0;
	bool bKill = false;
	bool bHead = false;
	/** 打死的那一下在头上（命中的声音按它分爆头打死：霰弹一颗擦到头、别的几颗打死的不算） */
	bool bKillHead = false;
	/** 打在护甲上（有一下就算） */
	bool bArmor = false;
	/** 有一下把护甲打穿了 */
	bool bArmorBroken = false;
	/** 这一枪一共扣的血、护甲 */
	float HealthDamage = 0.f;
	float ArmorDamage = 0.f;
	/** 最后一下打在哪 */
	EGinkgoBodyPart Part = EGinkgoBodyPart::UpperChest;
	/** 世界时间 */
	double Time = 0.0;
};

namespace Ginkgo::Combat
{
	/** 一颗子弹最多碰几个东西（你定的 8，Docs/Design/Damage.md「穿射」）：打到的都算，停在里面的那个也算。一路的检测结果放在定长的数组里 */
	inline constexpr int32 MaxPenetrationHits = 8;
}

/** @struct FGinkgoPenetrationHit : 一颗子弹路上打到的一个东西（Docs/Design/Damage.md「穿射」） */
struct FGinkgoPenetrationHit
{
	FHitResult Hit;
	/** 它的厚度（UGinkgoPenetrableComponent）；穿不过的东西为负 */
	float Thickness = -1.f;
	/** 子弹到它时剩的掩体穿射系数 */
	float Remaining = 0.f;
	/** 它挨的伤害乘多少：第一个是 1，穿过东西以后是到它时剩的系数（你说的：0.7 穿过厚 0.5 的剩 0.2，下一个只挨 0.2 倍），不过 1 */
	float DamageScale = 1.f;
	/** 子弹从它里面穿过去了；为假时停在它里面 */
	bool bPassed = false;
};

/** 一颗子弹的路：按先后，最多 MaxPenetrationHits 个，定长（你说的 TFixedAllocator），不分配 */
using FGinkgoPenetrationPath = TArray<FGinkgoPenetrationHit, TFixedAllocator<Ginkgo::Combat::MaxPenetrationHits>>;

/** 命中的声音（Docs/Design/Damage.md「命中的声音」）：一枪打中了目标放一声，几种同时成立时按先后取一种（Ginkgo::Combat::SelectHitSound） */
UENUM(BlueprintType)
enum class EGinkgoHitSound : uint8
{
	None		UMETA(DisplayName = "没有"),
	Normal		UMETA(DisplayName = "命中"),
	Armor		UMETA(DisplayName = "打在护甲上"),
	ArmorBreak	UMETA(DisplayName = "打穿护甲"),
	Head		UMETA(DisplayName = "爆头"),
	Kill		UMETA(DisplayName = "打死"),
	HeadKill	UMETA(DisplayName = "爆头打死"),
};

/** @struct FGinkgoHitSound : 一种命中的声音与它的音量 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoHitSound
{
	GENERATED_BODY()

	FGinkgoHitSound() = default;
	FGinkgoHitSound(const TCHAR* Path, const float InVolume) : Sound(FSoftObjectPath(Path)), Volume(InVolume) {}

	/** 2D 的声音（只有开枪的本地玩家听得到）。空的不放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit", meta = (ClampMin = 0))
	float Volume = 1.f;
};

/**
 * @struct FGinkgoHitSounds : 命中的声音（你给的六种，2026-11-08，Private/audio/source/Hit；打穿、爆头打死后来换成 hit_new 里的）：同白 X 一起，一枪一声。
 * MetaSound 在 /Game/UI/Audio/Hit，每种几条随机放、不连着重复；样本切过开头的空白，按听感的响度对齐到普通击杀（Private/audio/tools/split_hit_audio.py）。
 * 音量是你调的（2026-11-08：都是 1.3，打穿 1.6；Config/DefaultGame.ini 里存着，这里的默认同它一样）
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoHitSounds
{
	GENERATED_BODY()

	/** 没有护甲、不是头的一下 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	FGinkgoHitSound Normal = FGinkgoHitSound(TEXT("/Game/UI/Audio/Hit/MS_Hit_Normal.MS_Hit_Normal"), 1.3f);

	/** 打在护甲上（护甲还在） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	FGinkgoHitSound Armor = FGinkgoHitSound(TEXT("/Game/UI/Audio/Hit/MS_Hit_Armor.MS_Hit_Armor"), 1.3f);

	/** 这一枪把一件护甲打穿了 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	FGinkgoHitSound ArmorBreak = FGinkgoHitSound(TEXT("/Game/UI/Audio/Hit/MS_Hit_ArmorBreak.MS_Hit_ArmorBreak"), 1.6f);

	/** 打在头上（头盔挡住的也算） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	FGinkgoHitSound Head = FGinkgoHitSound(TEXT("/Game/UI/Audio/Hit/MS_Hit_Head.MS_Hit_Head"), 1.3f);

	/** 打死（打死的那一下不在头上） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	FGinkgoHitSound Kill = FGinkgoHitSound(TEXT("/Game/UI/Audio/Hit/MS_Hit_Kill.MS_Hit_Kill"), 1.3f);

	/** 打死的那一下在头上 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit")
	FGinkgoHitSound HeadKill = FGinkgoHitSound(TEXT("/Game/UI/Audio/Hit/MS_Hit_HeadKill.MS_Hit_HeadKill"), 1.3f);

	/**
	 * 命中、打在护甲上、爆头这三种：新的一声出来时前一声还在响，前一声这么快淡掉（连发时 AK 一枪 0.1 s，MG3 0.05 s，不叠成一片）。
	 * 打穿护甲、打死、爆头打死不归它管，只有队列满了让位的那一声按这个时长淡（PriorityCapacity）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit", meta = (ClampMin = 0, Units = "s"))
	float FadePrevious = 0.06f;

	/**
	 * 打穿护甲、打死、爆头打死走自己的队列，最多同时响这么多声（你说的，2026-11-08：这三种不随下一次命中取消，最大容量 3）：
	 * 放完自己出队；满了再来一声，最老的那一声淡掉（FadePrevious）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hit", meta = (ClampMin = 1))
	int32 PriorityCapacity = 3;

	/** @return 这一种的声音；None 是空 */
	const FGinkgoHitSound* Get(EGinkgoHitSound Kind) const;
};

namespace Ginkgo::Combat
{
	/**
	 * 开枪、准星的射线用的通道（Config/DefaultEngine.ini 的 Weapon，默认挡）：角色的胶囊不挡，人体网格按物理资产的形体挡，抛出去的弹壳不挡。
	 * 原来的 Visibility 胶囊和人体网格都不挡，打不到人
	 */
	inline constexpr ECollisionChannel WeaponTraceChannel = ECC_GameTraceChannel1;

	/** @brief 距离衰减的倍率（Distance 是 cm） */
	CASEGINKGO_API float FalloffMultiplier(const FGinkgoDamageFalloff& Falloff, float Distance);

	/**
	 * @brief 一下打下去（Docs/Design/Damage.md「伤害怎么算」，COD 战区式）：护甲还有时穿甲率那一份直接扣血，剩下的扣护甲（按护甲伤害）；
	 *        护甲这一下被打穿时，没挡住的那一份接着扣血。没有护甲（InOutArmor 为 0）时全扣血
	 * @param InOutArmor - 盖着这个部位的那件护甲还剩多少，扣过的写回；没有护甲时传 0
	 */
	CASEGINKGO_API FGinkgoDamageOutcome ApplyHitDamage(const FGinkgoHitDamage& Hit, float& InOutArmor);

	/**
	 * @brief 穿过一个东西以后剩多少掩体穿射系数（Docs/Design/Damage.md「穿射」，纯函数）：到它时剩的减它的厚度（你说的：0.7 穿过厚 0.5 的剩 0.2）
	 * @param Remaining - 到它时剩的
	 * @param Thickness - 它的厚度；穿不过的东西（没有 UGinkgoPenetrableComponent）传负数
	 * @return 穿过去剩多少；穿不过去时为 0，子弹停在它里面：不够、刚好用完（差不到 UE_KINDA_SMALL_NUMBER）、穿不过的东西
	 */
	CASEGINKGO_API float GetPenetrationLeft(float Remaining, float Thickness);

	/**
	 * @brief 一颗子弹往前穿（Docs/Design/Damage.md「穿射」）：从 First 起一个一个记下来，穿得过去就从它后面接着找，
	 *        穿不过去、满 MaxPenetrationHits 个、后面没东西了就停。世界的部分（多厚、接着打线）从外面给，测试不用世界
	 * @param First - 子弹打到的第一个东西
	 * @param Coefficient - 子弹的掩体穿射系数（UGinkgoAmmoDefinition::CoverPenetration）
	 * @param GetThickness - 打到的这个东西多厚，穿不过的返回负数
	 * @param TraceOn - 从刚穿过的东西接着往前打线（不再打到它）：打到了返回真，填 OutNext
	 * @param OutPath - 一路打到的，按先后；最后一个 bPassed 为真时子弹穿过它以后没再打到东西
	 */
	CASEGINKGO_API void TracePenetration(const FHitResult& First, float Coefficient, TFunctionRef<float(const FHitResult&)> GetThickness,
		TFunctionRef<bool(const FHitResult& Through, FHitResult& OutNext)> TraceOn, FGinkgoPenetrationPath& OutPath);

	/**
	 * @brief 打到的骨骼算哪个部位：从它往上找第一根在表里的（扭转骨、手指跟着父骨骼），找不到用 Default
	 * @param GetParent - 一根骨骼的父骨骼，根上是 NAME_None
	 */
	CASEGINKGO_API EGinkgoBodyPart FindBodyPart(FName Bone, const TMap<FName, EGinkgoBodyPart>& Parts, EGinkgoBodyPart Default, TFunctionRef<FName(FName)> GetParent);

	/** @brief 人体模型（UEFN Mannequin）的骨骼到部位的表（Docs/Design/Damage.md「部位」） */
	CASEGINKGO_API TMap<FName, EGinkgoBodyPart> MakeMannequinBodyParts();

	/** @brief 部位的中文名（日志、调试文字） */
	CASEGINKGO_API const TCHAR* DescribeBodyPart(EGinkgoBodyPart Part);

	/**
	 * @brief 这一枪放哪种命中的声音（Docs/Design/Damage.md「命中的声音」）。一枪一声，几种同时成立时按先后（我定的）：
	 *        爆头打死（打死的那一下在头上）、打死、打穿护甲、爆头、打在护甲上、命中。霰弹一枪几颗弹丸按整枪算
	 * @return 没打中目标时 None
	 */
	CASEGINKGO_API EGinkgoHitSound SelectHitSound(const FGinkgoHitConfirm& Confirm);

	/** @brief 这一种走不走自己的队列（打穿护甲、打死、爆头打死，FGinkgoHitSounds::PriorityCapacity） */
	CASEGINKGO_API bool IsPriorityHitSound(EGinkgoHitSound Sound);

	/**
	 * @brief 一声进这几种自己的队列（Docs/Design/Damage.md「命中的声音」，先进先出）：放完的先出队，还是满的就让最老的让位
	 * @param Queue - 还在响的，老的在前
	 * @param Capacity - 最大容量，小于 1 按 1
	 * @param IsRinging - 这一声还在响吗
	 * @param Evict - 让位的那一声（它还在响）
	 */
	template <typename TVoice, typename TIsRinging, typename TEvict>
	void PushPriorityHitSound(TArray<TVoice>& Queue, TVoice Voice, const int32 Capacity, TIsRinging IsRinging, TEvict Evict)
	{
		Queue.RemoveAll([&IsRinging](const TVoice& Ringing)
		{
			return !IsRinging(Ringing);
		});
		while (Queue.Num() >= FMath::Max(Capacity, 1))
		{
			Evict(Queue[0]);
			Queue.RemoveAt(0);
		}
		Queue.Add(MoveTemp(Voice));
	}

	/** @brief 命中的声音的中文名（日志） */
	CASEGINKGO_API const TCHAR* DescribeHitSound(EGinkgoHitSound Sound);

	/** @brief ginkgo.combat.showDamage 开没开（发行版里总是关） */
	CASEGINKGO_API bool IsShowDamageEnabled();
}

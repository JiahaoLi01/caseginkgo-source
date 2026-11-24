#pragma once

#include "CoreMinimal.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "GinkgoAmmoDefinition.generated.h"

class UMaterialInterface;
class UNiagaraSystem;
class USoundBase;
class UStaticMesh;

/**
 * @struct FGinkgoAmmoWeaponEffects : 一些子弹改枪的属性（你说的：12 号独头弹提高霰弹枪开镜的精度，只留一颗弹丸，Docs/Design/Weapons.md「弹种」）
 * 打出去的是哪一发就按哪一发：弹丸数在开火时取，扩散按膛里那一发（下一枪）算，准星也就跟着变
 */
USTRUCT(BlueprintType)
struct FGinkgoAmmoWeaponEffects
{
	GENERATED_BODY()

	/** 一枪几颗弹丸：0 照枪的（UGinkgoWeaponDefinition::PelletCount），独头弹 1 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = 0, ClampMax = 32))
	int32 PelletCount = 0;

	/** 腰射的扩散（默认与上限）乘多少 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = 0))
	float HipSpreadScale = 1.f;

	/** 开镜的扩散（默认与上限）乘多少：独头弹只有一颗、不散开，剩下的是瞄的误差 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon", meta = (ClampMin = 0))
	float AdsSpreadScale = 1.f;
};

/**
 * @struct FGinkgoAmmoTrail : 子弹飞过留下的一道淡淡的烟（你说的：每一种子弹一道淡淡的硝烟弹道，配在子弹上，因为榴弹之类的没有。Docs/Design/Weapons.md「弹道的烟」）
 * 从枪口到打到的地方（没打到东西到射线的最远处），每颗弹丸一道。按弹速从枪口往前长出来（射线是一下子打到的，烟只是看起来），慢慢散开、淡掉。
 * 最短隔设置的 BulletTrailMinInterval（0.1 s，AK-47 一枪的间隔）一道：AK 每枪都有，MG3 隔一枪一道
 */
USTRUCT(BlueprintType)
struct FGinkgoAmmoTrail
{
	GENERATED_BODY()

	/** 烟的系统（命令 GinkgoWeaponFx -Trail 生成）。空的没有烟：榴弹之类的 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	TSoftObjectPtr<UNiagaraSystem> System = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Weapons/FX/NS_BulletTrail.NS_BulletTrail")));

	/**
	 * 多浓（0–1，粒子的透明度）：材质里随变宽、随年龄再淡，开枪后 0.1 s 时中间最浓处约是它的一半，0.8 s 时约一成。
	 * 一枪几颗弹丸的（鹿弹）每一颗淡一些。默认同别的几项一样照 98k 的 s.S.（你在编辑器里调的 0.2，2026-11-08）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail", meta = (ClampMin = 0, ClampMax = 1))
	float Opacity = 0.2f;

	/**
	 * 烟的颜色：吃场景的光，再按同样的颜色自己亮一份（材质 M_BulletTrail 的 EMISSIVE），大过 1 的是更亮（底色到 1 为止）。
	 * 浅白（你说的，2026-11-07：照 Apex 的经验浅白比灰的看得清）：只吃光的浅白同这张图的地面、天一样亮，0.9 时刚开枪那一阵看不出来，1.5 时侧面比地面亮约 60 级
	 * （Docs/Design/Weapons.md「弹道的烟」）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	FLinearColor Color = FLinearColor(1.5f, 1.5f, 1.5f);

	/** 散到最后多宽。刚出来只有它的约七分之一（材质 M_BulletTrail 里随年龄变宽） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail", meta = (ClampMin = 0, Units = "cm"))
	float Width = 14.f;

	/** 留多久 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail", meta = (ClampMin = 0.05, Units = "s"))
	float Lifetime = 1.2f;

	/** 弹速（枪口初速，m/s）：烟按这个速度从枪口往前长 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail", meta = (ClampMin = 1, ForceUnits = "m/s"))
	float Speed = 760.f;
};

/** @struct FGinkgoTrailShape : 一道烟的形状（Ginkgo::Combat::MakeTrailShape） */
struct FGinkgoTrailShape
{
	/** 沿线摆多少个点（飘带的节点）。0 是太短，不放 */
	int32 Points = 0;
	/** 长到头的快慢：材质里烟长到全长的 年龄（0–1）× Reveal，所以 1 / Reveal 是长到头时过了寿命的几分之几 */
	float Reveal = 0.f;
};

/**
 * @class UGinkgoAmmoDefinition : 一种子弹（Docs/Design/Damage.md「部分」）
 * 武器只是发射器，伤害、护甲伤害、穿甲率、掩体穿射系数、部位倍率、距离衰减都是子弹的；枪只给一个倍率（UGinkgoWeaponDefinition::DamageMultiplier）。
 * 按弹种标签（Ammo.*）找：机构记着每一发是什么弹，开的是哪一发就按哪一份算。设置里的 AmmoDefinitions 列着全部，命令 GinkgoCombat -Ammo 生成。
 * 一把枪能用哪几种：父标签同这把枪的弹种的（同一口径，GetChoices），弹种轮盘列的就是它们（Docs/Design/Weapons.md「切换弹种」）
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoAmmoDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 弹种（Ammo.*）：机构记的每一发、后备都按它 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (Categories = "Ammo"))
	FGameplayTag Ammo;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	FText DisplayName;

	/** 轮盘上的短名字（"00 鹿弹"、"独头弹"）。空的用 DisplayName */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FText ShortName;

	/** 轮盘中间的一行说明（"一颗弹头，开镜准"） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FText Description;

	/** 武器栏里膛里那一发旁边写的（"12 Gauge Slug"）。空的写枪的口径（UGinkgoWeaponDefinition::CaliberName） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FText HudLabel;

	/** 武器栏、轮盘画哪一种子弹（页面 Content/UI/WeaponHud 的 ROUNDS 认的名字，独头弹是 ShotgunSlug）。空的画枪的（HudIcon） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Display")
	FName HudRound;

	/**
	 * 拿到一把用这种口径、却不是这种弹的枪时（槽里换了枪，同 UGinkgoWeaponDefinition::StartingReserve 每拿到一次给一次）带多少发：特殊弹的开局后备写在子弹上
	 * （98k、AK 等的数据资产不动）。枪自己的弹种按枪的 StartingReserve
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (ClampMin = 0))
	int32 StartingReserve = 0;

	/** 改枪的属性（独头弹：一颗弹丸、开镜更准） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	FGinkgoAmmoWeaponEffects WeaponEffects;

	/**
	 * 这种子弹自己的枪声（你说的，2026-11-08：98k 的穿甲弹、霰弹枪的独头弹有新的枪声）。空的用枪的（枪的 Cues 里的 Weapon.Cue.Fire）；
	 * 音量照枪的 Weapon.Cue.Fire（样本按听感的响度对齐到这把枪原来的枪声，Private/audio/tools/split_special_shots.py）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	TSoftObjectPtr<USoundBase> FireSound;

	/** 投送方式：常规枪弹与钢镖霰弹走射线即刻结算，高爆榴弹与碰炸闪光弹走抛射物飞行 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	EGinkgoAmmoDelivery Delivery = EGinkgoAmmoDelivery::Hitscan;

	/** 抛射物触碰引爆时的战斗部效果类型（仅在 Delivery 为 Projectile 时生效） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (EditCondition = "Delivery == EGinkgoAmmoDelivery::Projectile"))
	EGinkgoProjectilePayload ProjectilePayload = EGinkgoProjectilePayload::HighExplosive;

	/** 抛射物飞行期间显示的弹头静态网格（局部 +X 朝弹尖）；为空时回退到默认 40mm 高爆弹头网格 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo", meta = (EditCondition = "Delivery == EGinkgoAmmoDelivery::Projectile"))
	TSoftObjectPtr<UStaticMesh> ProjectileMesh;

	/** 该弹种退膛或重力甩出时生成的物理整弹静态网格；为空时回退到武器定义的 Rounds.LiveRound */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	TSoftObjectPtr<UStaticMesh> LiveRoundMesh;

	/** 该弹种在武器骨骼网格弹药材质槽（RoundMaterialSlot）上的覆盖材质；为空时保留武器默认弹药材质 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ammo")
	TSoftObjectPtr<UMaterialInterface> RoundMaterial;

	/** 一颗弹丸的伤害（霰弹是一颗的），乘枪的倍率、部位的倍率、距离衰减 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float Damage = 30.f;

	/** 打在护甲上时扣护甲的数，乘同样的倍率：同伤害一样时护甲 1:1 挡，大的打护甲更快（穿甲弹），小的更慢（鹿弹） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float ArmorDamage = 30.f;

	/** 穿甲率：护甲还有时直接扣血的那一份（0–1） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0, ClampMax = 1))
	float ArmorPenetration = 0.f;

	/**
	 * 掩体穿射系数（Docs/Design/Damage.md「穿射」，你说的，同穿甲率是两回事；放在子弹上是你定的）：子弹能穿过挂了 UGinkgoPenetrableComponent 的东西（角色、一些墙）。
	 * 每穿过一个减去它的厚度；打到下一个东西时伤害、护甲伤害乘剩下的（0.7 穿过厚 0.5 的剩 0.2，下一个只挨 0.2 倍；大过 1 的只多穿，伤害不超过原来）；
	 * 剩的不比下一个的厚度大就停在它里面（照样挨这一下）。0 什么都穿不过：手枪弹、霰弹（你定的，只有步枪弹能穿）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0))
	float CoverPenetration = 0.f;

	/** 对六个部位的倍率 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	FGinkgoBodyPartMultipliers BodyParts;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage")
	FGinkgoDamageFalloff Falloff;

	/** 打死时把打到的那一块推多快（沿子弹的方向，加在那一块的速度上）：布娃娃倒向子弹来的方向 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Damage", meta = (ClampMin = 0, Units = "cm/s"))
	float KillPush = 300.f;

	/** 飞过留下的一道淡淡的烟。榴弹之类的把 System 清空 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Trail")
	FGinkgoAmmoTrail Trail;

	/**
	 * @brief 按弹种找（设置里的 AmmoDefinitions）。第一次调时读进来，记住
	 * @return 没有这种子弹的数据时为空（打到东西不扣血，日志写一次）
	 */
	static const UGinkgoAmmoDefinition* Find(const FGameplayTag& Ammo);

	/**
	 * @brief 一把弹种是 Standard 的枪能用哪几种：设置里的 AmmoDefinitions 里父标签同它一样的（同一口径），Standard 排第一，别的照设置里的顺序
	 * @return 至少有 Standard 自己（读不到它的数据时也列着）
	 */
	static TArray<FGameplayTag, TInlineAllocator<8>> GetChoices(const FGameplayTag& Standard);

	/** 短名字：ShortName，空的用 DisplayName */
	FText GetShortName() const { return ShortName.IsEmpty() ? DisplayName : ShortName; }
};

namespace Ginkgo::Combat
{
	/**
	 * @brief 同一口径的弹种（GetChoices 的规则，纯函数）：Known 里父标签同 Standard 的父标签一样的，Standard 排第一，别的照 Known 的顺序，不重复
	 * @note Standard 没有父标签（不是 Ammo.<口径>.<种>）时只有它自己
	 */
	CASEGINKGO_API TArray<FGameplayTag, TInlineAllocator<8>> FilterAmmoChoices(const FGameplayTag& Standard, TConstArrayView<FGameplayTag> Known);

	/** 弹道的烟：沿线隔多远一个点（cm）。散开时每一段各自飘，隔得太远弯得不圆 */
	constexpr double TrailPointSpacing = 50.0;
	/** 最多几个点：100 m 的射线到这里就封顶，点隔得远一些 */
	constexpr int32 TrailMaxPoints = 160;
	/** 短过这个（cm）不放：贴着墙开枪，烟藏在枪口的火光里 */
	constexpr double TrailMinLength = 30.0;
	/**
	 * 飘带比烟宽这么多倍（材质 M_BulletTrail 的 GEOMETRY 同它一样）：远处的烟在材质里至少画 1.5 个像素宽，要有地方画。
	 * 自己的相机里烟顺着视线往远处走，10 m 外 2 cm 宽的线不到一个像素，就没了
	 */
	constexpr float TrailGeometryScale = 3.f;

	/**
	 * @brief 一道烟的形状（纯函数）：沿线摆几个点、多快长到头
	 * @param Length - 枪口到打到的地方（cm）
	 * @return 太短、没有系统、不透明度是 0 时 Points 为 0
	 */
	CASEGINKGO_API FGinkgoTrailShape MakeTrailShape(const FGinkgoAmmoTrail& Trail, double Length);
}

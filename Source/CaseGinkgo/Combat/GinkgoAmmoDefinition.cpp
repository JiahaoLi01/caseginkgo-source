#include "Combat/GinkgoAmmoDefinition.h"

#include "Framework/GinkgoDeveloperSettings.h"
#include "UObject/GCObject.h"
#include "Utilities/Debug/DebugCategory.h"

namespace
{
	/** 读进来的子弹，强引用着：不然垃圾回收一次就收走，下一枪又要同步读盘 */
	class FAmmoCache final : public FGCObject
	{
	public:
		TMap<FGameplayTag, TObjectPtr<UGinkgoAmmoDefinition>> Loaded;
		/** 设置里的顺序（读进来的，不重复）：轮盘按它排 */
		TArray<FGameplayTag> Order;
		/** 读不到的弹种：只警告一次 */
		TSet<FGameplayTag> Missing;

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override
		{
			Collector.AddReferencedObjects(Loaded);
		}

		virtual FString GetReferencerName() const override
		{
			return TEXT("GinkgoAmmoCache");
		}
	};

	FAmmoCache& GetAmmoCache()
	{
		static FAmmoCache Cache;
		return Cache;
	}

	/** 把设置里的列表读一遍（已经在内存里的不再读盘），新读到的写一行日志 */
	void LoadListed(FAmmoCache& Cache)
	{
		for (const TSoftObjectPtr<UGinkgoAmmoDefinition>& Path : UGinkgoDeveloperSettings::Get()->AmmoDefinitions)
		{
			UGinkgoAmmoDefinition* Definition = Path.LoadSynchronous();
			if (!Definition || !Definition->Ammo.IsValid() || Cache.Loaded.Contains(Definition->Ammo))
			{
				continue;
			}
			Cache.Loaded.Add(Definition->Ammo, Definition);
			Cache.Order.Add(Definition->Ammo);
			// 读进来的数写一行：测量（Scripts/Measurements/combat.py、ammo.py）按它算该扣多少，手调过的数也对得上
			const FGinkgoBodyPartMultipliers& Parts = Definition->BodyParts;
			const FGinkgoDamageFalloff& Falloff = Definition->Falloff;
			const FGinkgoAmmoWeaponEffects& Effects = Definition->WeaponEffects;
			const FGinkgoAmmoTrail& Trail = Definition->Trail;
			UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 子弹 %s：伤害 %.3f，护甲伤害 %.3f，穿甲率 %.4f，部位 %.4f/%.4f/%.4f/%.4f/%.4f/%.4f，衰减 %.1f 到 %.1f cm 降到 %.4f，弹丸 %d，扩散 ×%.4f/×%.4f，开局 %d，")
				TEXT("弹道的烟 %s 弹速 %.1f m/s 浓 %.3f 宽 %.2f cm 留 %.3f s，掩体穿射 %.4f"),
				*Definition->Ammo.ToString(), Definition->Damage, Definition->ArmorDamage, Definition->ArmorPenetration, Parts.Head, Parts.UpperChest, Parts.Abdomen,
				Parts.UpperLimbs, Parts.Legs, Parts.Feet, Falloff.FullRange, Falloff.MinRange, Falloff.MinMultiplier, Effects.PelletCount, Effects.HipSpreadScale,
				Effects.AdsSpreadScale, Definition->StartingReserve, Trail.System.IsNull() ? TEXT("没有") : TEXT("有"), Trail.Speed, Trail.Opacity, Trail.Width, Trail.Lifetime,
				Definition->CoverPenetration);
		}
	}
}

const UGinkgoAmmoDefinition* UGinkgoAmmoDefinition::Find(const FGameplayTag& Ammo)
{
	if (!Ammo.IsValid())
	{
		return nullptr;
	}
	FAmmoCache& Cache = GetAmmoCache();
	if (const TObjectPtr<UGinkgoAmmoDefinition>* Found = Cache.Loaded.Find(Ammo); Found && *Found)
	{
		return *Found;
	}
	// 没记着：把设置里的列表读一遍
	LoadListed(Cache);
	if (const TObjectPtr<UGinkgoAmmoDefinition>* Found = Cache.Loaded.Find(Ammo); Found && *Found)
	{
		return *Found;
	}
	if (!Cache.Missing.Contains(Ammo))
	{
		Cache.Missing.Add(Ammo);
		UE_LOG(LogGinkgoCombat, Warning, TEXT("[Combat] 没有子弹 %s 的数据（设置的 AmmoDefinitions，命令 GinkgoCombat -Ammo 生成）：打到东西不扣血"), *Ammo.ToString());
	}
	return nullptr;
}

TArray<FGameplayTag, TInlineAllocator<8>> UGinkgoAmmoDefinition::GetChoices(const FGameplayTag& Standard)
{
	FAmmoCache& Cache = GetAmmoCache();
	// 列表读全了才知道同一口径有哪几种：设置里的条数比读到的多（还没读过，或者有读不到的）时读一遍，读不到的不反复读盘
	static bool bListedOnce = false;
	if (!bListedOnce)
	{
		bListedOnce = true;
		LoadListed(Cache);
	}
	return Ginkgo::Combat::FilterAmmoChoices(Standard, Cache.Order);
}

TArray<FGameplayTag, TInlineAllocator<8>> Ginkgo::Combat::FilterAmmoChoices(const FGameplayTag& Standard, const TConstArrayView<FGameplayTag> Known)
{
	TArray<FGameplayTag, TInlineAllocator<8>> Choices;
	if (!Standard.IsValid())
	{
		return Choices;
	}
	Choices.Add(Standard);
	const FGameplayTag Caliber = Standard.RequestDirectParent();
	if (!Caliber.IsValid())
	{
		return Choices;
	}
	for (const FGameplayTag& Tag : Known)
	{
		if (Tag.IsValid() && Tag != Standard && Tag.RequestDirectParent() == Caliber)
		{
			Choices.AddUnique(Tag);
		}
	}
	return Choices;
}

FGinkgoTrailShape Ginkgo::Combat::MakeTrailShape(const FGinkgoAmmoTrail& Trail, const double Length)
{
	FGinkgoTrailShape Shape;
	if (Trail.System.IsNull() || Trail.Opacity <= 0.f || Trail.Lifetime <= 0.f || Trail.Speed <= 0.f || !(Length >= TrailMinLength))
	{
		return Shape;
	}
	// 两头各一个点，中间按间隔：100 m 封顶 160 个
	Shape.Points = FMath::Clamp(FMath::CeilToInt32(Length / TrailPointSpacing) + 1, 2, TrailMaxPoints);
	// 长到头要 Length / 弹速 秒，是寿命的这么多分之一（弹速是 m/s，长度是 cm）
	Shape.Reveal = static_cast<float>(static_cast<double>(Trail.Lifetime) * Trail.Speed * 100.0 / Length);
	return Shape;
}

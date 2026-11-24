#include "GinkgoCombatCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoCombatCommandlet, Log, All);

namespace GinkgoCombat
{
	/** 一种子弹的数（Docs/Design/Damage.md「子弹的数」，我定的，你手调过的几个同你调的一样） */
	struct FAmmoSpec
	{
		const TCHAR* Package;
		FGameplayTag Ammo;
		const TCHAR* DisplayName;
		float Damage;
		float ArmorDamage;
		float Penetration;
		/** 头、上胸、腹、上肢、腿、脚 */
		float Parts[6];
		/** 满伤害到多远、降到多远（m）、降到多少：距离都是 0 时不衰减 */
		float FullRange;
		float MinRange;
		float MinMultiplier;
		/** 打死时推打到的那一块（cm/s） */
		float KillPush;
		/** 弹种轮盘与武器栏（Docs/Design/Weapons.md「切换弹种」）：短名字、说明、武器栏写的口径、画哪种子弹。空的不写 */
		const TCHAR* ShortName = nullptr;
		const TCHAR* Description = nullptr;
		const TCHAR* HudLabel = nullptr;
		const TCHAR* HudRound = nullptr;
		/** 拿到这一口径的枪时另带多少发（特殊弹） */
		int32 StartingReserve = 0;
		/** 改枪的属性：弹丸数（0 照枪的）、腰射与开镜的扩散倍率 */
		int32 PelletCount = 0;
		float HipSpreadScale = 1.f;
		float AdsSpreadScale = 1.f;

		/** 投送方式：常规枪弹与钢镖霰弹走射线，高爆榴弹与碰炸闪光弹走抛射物 */
		EGinkgoAmmoDelivery Delivery = EGinkgoAmmoDelivery::Hitscan;

		/** 抛射物触碰引爆时的战斗部类型（高爆破片 / 碰炸闪光） */
		EGinkgoProjectilePayload ProjectilePayload = EGinkgoProjectilePayload::HighExplosive;

		/** 抛射物飞行静态网格软路径（为空时用默认弹头） */
		const TCHAR* ProjectileMesh = nullptr;

		/** 退膛活弹静态网格软路径（为空时用武器默认活弹） */
		const TCHAR* LiveRoundMesh = nullptr;

		/** 武器骨骼网格弹药材质槽覆盖材质软路径（为空时不覆盖） */
		const TCHAR* RoundMaterial = nullptr;
	};

	TArray<FAmmoSpec> MakeAmmoSpecs()
	{
		// 步枪弹推得重一点；霰弹一枪九颗，推的只是打死人的那一颗，所以一颗的推力按整枪给。
		// 特殊弹（2026-11-07，Docs/Design/Weapons.md「弹种」）：
		// - 12 号独头弹（1 盎司 Foster 式，Federal 的表：枪口 491 m/s、3418 J，46 m 剩 69%）：一颗弹头（你说的），开镜的扩散 ×0.075（你说的：开镜更准；霰弹枪开镜 4°（你手调的）→ 0.3°），
		//   伤害按步枪的部位表，上胸 102.5，20 m 内一枪倒，70 m 降到 0.55；铅弹头打护甲穿得少（0.1）、震得多（110）
		// - S.m.K.（钢芯尖头穿甲弹，约 12 g、785 m/s，100 m 打穿 8–12 mm 钢板）：护甲伤害 160；钢芯不变形，没穿护甲时比 s.S. 稍弱
		// 你 2026-11-08 15:55 在编辑器里调的，按资产同步回来（你说以你的为准，-Force 写出来同你调的一样）：s.S. 伤害 117，S.m.K. 伤害 110、穿甲率 1.0，M43 头部 2.7，.45 ACP 头部 2.4
		return {
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57"), FWeaponTags::Ammo_Mauser792x57_Standard, TEXT("7.92×57 mm s.S. 普通弹"), 117.f, 90.f, 0.25f,
				{2.5f, 1.25f, 1.f, 0.7f, 0.75f, 0.6f}, 0.f, 0.f, 1.f, 450.f,
				TEXT("s.S. 普通弹"), TEXT("铅芯重尖弹"), TEXT("7.92 × 57mm s.S."), TEXT("Mauser98k")},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_M43762x39"), FWeaponTags::Ammo_M43762x39_Standard, TEXT("7.62×39 mm M43"), 34.f, 34.f, 0.15f,
				{2.7f, 1.1f, 1.f, 0.75f, 0.8f, 0.6f}, 50.f, 150.f, 0.8f, 320.f},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_NATO762x51"), FWeaponTags::Ammo_NATO762x51_Ball, TEXT("7.62×51 mm NATO"), 38.f, 42.f, 0.2f,
				{2.f, 1.1f, 1.f, 0.75f, 0.8f, 0.6f}, 60.f, 200.f, 0.8f, 380.f},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_ACP45"), FWeaponTags::Ammo_ACP45_Standard, TEXT(".45 ACP"), 30.f, 18.f, 0.05f,
				{2.4f, 1.1f, 1.f, 0.75f, 0.8f, 0.6f}, 20.f, 60.f, 0.6f, 260.f},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Gauge12Buckshot"), FWeaponTags::Ammo_Gauge12_Buckshot, TEXT("12 号 00 鹿弹"), 13.f, 10.f, 0.f,
				{1.5f, 1.f, 1.f, 0.8f, 0.8f, 0.6f}, 8.f, 30.f, 0.4f, 520.f,
				TEXT("00 鹿弹"), TEXT("九颗弹丸，近处一枪倒"), TEXT("12 Gauge 00 Buck"), TEXT("Shotgun")},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Gauge12Slug"), FWeaponTags::Ammo_Gauge12_Slug, TEXT("12 号独头弹"), 82.f, 110.f, 0.1f,
				{2.f, 1.25f, 1.f, 0.75f, 0.8f, 0.6f}, 20.f, 70.f, 0.55f, 600.f,
				TEXT("独头弹"), TEXT("一颗弹头，开镜更准"), TEXT("12 Gauge Slug"), TEXT("ShotgunSlug"), 8, 1, 1.f, 0.075f},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57SmK"), FWeaponTags::Ammo_Mauser792x57_SmK, TEXT("7.92×57 mm S.m.K. 穿甲弹"), 110.f, 160.f, 1.f,
				{2.5f, 1.25f, 1.f, 0.7f, 0.75f, 0.6f}, 0.f, 0.f, 1.f, 430.f,
				TEXT("S.m.K. 穿甲弹"), TEXT("钢芯，护甲挡得少"), TEXT("7.92 × 57mm S.m.K."), TEXT("Mauser98kSmK"), 15},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46"), FWeaponTags::Ammo_Grenade40x46_HE, TEXT("40×46 mm M406 高爆榴弹"), 360.f, 400.f, 0.45f,
				{1.25f, 1.f, 1.f, 0.9f, 0.9f, 0.85f}, 0.f, 0.f, 1.f, 850.f,
				TEXT("M406 高爆弹"), TEXT("76 m/s 抛物线，碰炸破片范围伤害"), TEXT("40 × 46mm M406 HE"), TEXT("M79"), 0, 1, 1.f, 1.f,
				EGinkgoAmmoDelivery::Projectile, EGinkgoProjectilePayload::HighExplosive,
				TEXT("/Game/Weapons/M79/Rounds/SM_M79_Projectile.SM_M79_Projectile"),
				TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round.SM_M79_Round"),
				TEXT("/Game/Weapons/M79/M_M79_Grenade.M_M79_Grenade")},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flash"), FWeaponTags::Ammo_Grenade40x46_Flash, TEXT("40×46 mm 碰炸闪光弹"), 0.f, 0.f, 0.f,
				{1.f, 1.f, 1.f, 1.f, 1.f, 1.f}, 0.f, 0.f, 1.f, 0.f,
				TEXT("碰炸闪光弹"), TEXT("82 m/s 高速直射触碰即爆，致盲敌人"), TEXT("40 × 46mm Stun Flash"), TEXT("M79Flash"), 6, 1, 1.f, 1.f,
				EGinkgoAmmoDelivery::Projectile, EGinkgoProjectilePayload::Flashbang,
				TEXT("/Game/Weapons/M79/Rounds/SM_M79_Projectile_Flash.SM_M79_Projectile_Flash"),
				TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round_Flash.SM_M79_Round_Flash"),
				TEXT("/Game/Weapons/M79/M_M79_Grenade_Flash.M_M79_Grenade_Flash")},
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flechette"), FWeaponTags::Ammo_Grenade40x46_Flechette, TEXT("40×46 mm XM576 钢镖霰弹"), 11.f, 9.f, 0.25f,
				{1.5f, 1.f, 1.f, 0.8f, 0.8f, 0.6f}, 10.f, 35.f, 0.4f, 520.f,
				TEXT("XM576 镖弹"), TEXT("16 枚射线钢镖齐射，近战面杀伤"), TEXT("40 × 46mm 16-Flechette"), TEXT("M79Flechette"), 8, 16, 2.f, 14.f,
				EGinkgoAmmoDelivery::Hitscan, EGinkgoProjectilePayload::HighExplosive,
				nullptr,
				TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round_Flechette.SM_M79_Round_Flechette"),
				TEXT("/Game/Weapons/M79/M_M79_Grenade_Flechette.M_M79_Grenade_Flechette")},
		};
	}

	/**
	 * 弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：弹速是各自的枪口初速（m/s，烟按它从枪口长出来）；散到多宽、留多久按口径（我定的）：
	 * 步枪弹留得久，手枪弹、鹿弹窄、短；浓淡是你手调的。系统、颜色照 FGinkgoAmmoTrail 的默认
	 */
	struct FTrailSpec
	{
		const TCHAR* Package;
		float Speed;
		float Opacity;
		float Width;
		float Lifetime;
	};

	const FTrailSpec* FindTrailSpec(const TCHAR* Package)
	{
		// 浓淡是你在编辑器里手调的（2026-11-08 00:40 的那一版，按资产导出的文本抄回来，以后 -Trail 写出来同你调的一样）
		static const FTrailSpec Specs[] = {
			// s.S.：98k 约 760 m/s
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57"), 760.f, 0.2f, 14.f, 1.2f},
			// S.m.K.：约 785 m/s
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57SmK"), 785.f, 0.2f, 14.f, 1.2f},
			// M43：AK-47 约 715 m/s
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_M43762x39"), 715.f, 0.12f, 12.f, 1.f},
			// 7.62 × 51：MG3 的 565 mm 枪管约 820 m/s
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_NATO762x51"), 820.f, 0.1f, 14.f, 1.f},
			// .45 ACP：230 gr 约 253 m/s，亚音速
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_ACP45"), 253.f, 0.08f, 10.f, 0.8f},
			// 00 鹿弹：约 400 m/s，一枪九颗
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Gauge12Buckshot"), 400.f, 0.17f, 8.f, 0.8f},
			// 独头弹：Federal 的表 491 m/s
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Gauge12Slug"), 491.f, 0.12f, 14.f, 1.f},
			// M406 40 × 46mm SR 低速榴弹：M79 标准出膛初速 76 m/s，抛射物本身可见，不画射线烟轨
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46"), 76.f, 0.f, 18.f, 0.6f},
			// 40 × 46mm 碰炸闪光弹：轻量铝弹头出膛初速 82 m/s（远快于手投闪光弹 19.2 m/s），抛射物飞行不画射线烟轨
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flash"), 82.f, 0.f, 18.f, 0.5f},
			// XM576 40 × 46mm 16 钢镖霰弹：约 300 m/s，单次 16 道细长硝烟轨迹
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flechette"), 300.f, 0.14f, 6.5f, 0.75f},
		};
		for (const FTrailSpec& Spec : Specs)
		{
			if (FCString::Strcmp(Spec.Package, Package) == 0)
			{
				return &Spec;
			}
		}
		return nullptr;
	}

	/** 写弹道的烟的几项（系统、颜色照默认）。@return 改了没有 */
	bool WriteTrail(UGinkgoAmmoDefinition& Ammo, const FTrailSpec& Spec)
	{
		FGinkgoAmmoTrail Trail;
		if (Spec.Opacity <= 0.f)
		{
			Trail.System.Reset();
		}
		Trail.Speed = Spec.Speed;
		Trail.Opacity = Spec.Opacity;
		Trail.Width = Spec.Width;
		Trail.Lifetime = Spec.Lifetime;
		const FGinkgoAmmoTrail& Old = Ammo.Trail;
		const bool bChanged = Old.System != Trail.System || Old.Color != Trail.Color || Old.Speed != Trail.Speed || Old.Opacity != Trail.Opacity || Old.Width != Trail.Width
			|| Old.Lifetime != Trail.Lifetime;
		Ammo.Trail = Trail;
		UE_LOG(LogGinkgoCombatCommandlet, Display, TEXT("%s 弹道的烟：弹速 %.0f m/s，浓 %.2f，散到 %.0f cm，留 %.1f s%s"), Spec.Package, Trail.Speed, Trail.Opacity, Trail.Width,
			Trail.Lifetime, bChanged ? TEXT("") : TEXT("（同原来一样）"));
		return bChanged;
	}

	/**
	 * 掩体穿射系数（Docs/Design/Damage.md「穿射」，你定的，2026-11-11：放在子弹上，只有步枪弹能穿；.45 ACP、鹿弹、独头弹 0，表里没有的都是 0）。
	 * 穿过东西以后伤害乘剩下的；人的厚度 0.3，穿过一个人后 s.S. 剩 0.45、S.m.K. 0.7、7.62 × 51 0.4、M43 0.3
	 */
	float FindCoverPenetration(const TCHAR* Package)
	{
		static const TPair<const TCHAR*, float> Specs[] = {
			// s.S.：12.8 g、98k 约 760 m/s（约 3700 J），全威力步枪弹
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57"), 0.75f},
			// S.m.K.：钢芯（100 m 打穿 8–12 mm 钢板），最强
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57SmK"), 1.f},
			// 7.62 × 51：9.5 g、MG3 约 820 m/s（约 3200 J）
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_NATO762x51"), 0.7f},
			// M43：7.9 g、AK 约 715 m/s（约 2000 J），软钢芯
			{TEXT("/Game/Weapons/Ammo/DA_Ammo_M43762x39"), 0.6f},
		};
		for (const TPair<const TCHAR*, float>& Spec : Specs)
		{
			if (FCString::Strcmp(Spec.Key, Package) == 0)
			{
				return Spec.Value;
			}
		}
		return 0.f;
	}

	/** @return 改了没有 */
	bool WriteCoverPenetration(UGinkgoAmmoDefinition& Ammo, const TCHAR* Package)
	{
		const float Value = FindCoverPenetration(Package);
		const bool bChanged = Ammo.CoverPenetration != Value;
		Ammo.CoverPenetration = Value;
		UE_LOG(LogGinkgoCombatCommandlet, Display, TEXT("%s 掩体穿射系数：%.2f%s"), Package, Value, bChanged ? TEXT("") : TEXT("（同原来一样）"));
		return bChanged;
	}

	/** 子弹自己的枪声（你说的，2026-11-08：穿甲弹、独头弹有新的枪声）：别的子弹用枪的 */
	const TCHAR* FindFireSound(const TCHAR* Package)
	{
		if (FCString::Strcmp(Package, TEXT("/Game/Weapons/Ammo/DA_Ammo_Mauser792x57SmK")) == 0)
		{
			return TEXT("/Game/Weapons/Mauser98k/Audio/MS_Mauser98k_FireSmK.MS_Mauser98k_FireSmK");
		}
		if (FCString::Strcmp(Package, TEXT("/Game/Weapons/Ammo/DA_Ammo_Gauge12Slug")) == 0)
		{
			return TEXT("/Game/Weapons/Shotgun/Audio/MS_Shotgun_FireSlug.MS_Shotgun_FireSlug");
		}
		if (FCString::Strcmp(Package, TEXT("/Game/Weapons/Ammo/DA_Ammo_Grenade40x46Flechette")) == 0)
		{
			return TEXT("/Game/Weapons/Shotgun/Audio/MS_Shotgun_Fire.MS_Shotgun_Fire");
		}
		return nullptr;
	}

	/** @return 改了没有 */
	bool WriteFireSound(UGinkgoAmmoDefinition& Ammo, const TCHAR* Path, const TCHAR* Package)
	{
		const TSoftObjectPtr<USoundBase> Sound{FSoftObjectPath(Path)};
		const bool bChanged = Ammo.FireSound != Sound;
		Ammo.FireSound = Sound;
		UE_LOG(LogGinkgoCombatCommandlet, Display, TEXT("%s 枪声：%s%s"), Package, Path, bChanged ? TEXT("") : TEXT("（同原来一样）"));
		return bChanged;
	}

	/** 写轮盘、武器栏用的几项（全名、名字、说明、口径、图）；Only 为真时只写空着的（已有的资产不动别的数，可能手调过） */
	bool WriteDisplay(UGinkgoAmmoDefinition& Ammo, const FAmmoSpec& Spec, const bool bOnlyEmpty)
	{
		bool bChanged = false;
		const auto SetText = [bOnlyEmpty, &bChanged](FText& Field, const TCHAR* Value)
		{
			if (Value && (!bOnlyEmpty || Field.IsEmpty()) && !Field.ToString().Equals(Value))
			{
				Field = FText::AsCultureInvariant(Value);
				bChanged = true;
			}
		};
		// 全名是轮盘中间的标题：同一口径两种以上时要写出是哪一种（s.S. 原来叫"7.92×57 mm 毛瑟"，2026-11-07 改）
		SetText(Ammo.DisplayName, Spec.DisplayName);
		SetText(Ammo.ShortName, Spec.ShortName);
		SetText(Ammo.Description, Spec.Description);
		SetText(Ammo.HudLabel, Spec.HudLabel);
		if (Spec.HudRound && (!bOnlyEmpty || Ammo.HudRound.IsNone()) && Ammo.HudRound != FName(Spec.HudRound))
		{
			Ammo.HudRound = FName(Spec.HudRound);
			bChanged = true;
		}
		return bChanged;
	}

	bool SaveAmmo(UPackage& Package, UGinkgoAmmoDefinition& Ammo, const FAmmoSpec& Spec)
	{
		Package.MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Spec.Package, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(&Package, &Ammo, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoCombatCommandlet, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		return true;
	}

	/**
	 * @param bLabels - 已有的也按这里重写全名、名字、说明、口径、图（-Labels：数不动）
	 * @param bTrail - 已有的也按这里重写弹道的烟（-Trail：别的不动）
	 * @param bFireSound - 已有的也写子弹自己的枪声（-FireSound：别的不动）
	 * @param bCover - 已有的也写掩体穿射系数（-Cover：别的不动，你手调过的数留着）
	 */
	bool WriteAmmo(const FAmmoSpec& Spec, const bool bForce, const bool bLabels, const bool bTrail, const bool bFireSound, const bool bCover)
	{
		const FString AssetName = FPackageName::GetShortName(Spec.Package);
		// 已有的不动它的数（可能手调过，-Force 才按这里的数重写），只补空着的名字、说明、口径、图（这一轮新加的几项）；-Labels 时这几项按这里重写
		if (FPackageName::DoesPackageExist(Spec.Package) && !bForce)
		{
			UPackage* Existing = LoadPackage(nullptr, Spec.Package, LOAD_None);
			UGinkgoAmmoDefinition* Ammo = Existing ? FindObject<UGinkgoAmmoDefinition>(Existing, *AssetName) : nullptr;
			if (!Ammo)
			{
				UE_LOG(LogGinkgoCombatCommandlet, Error, TEXT("读不到 %s"), Spec.Package);
				return false;
			}
			const FTrailSpec* Trail = FindTrailSpec(Spec.Package);
			const bool bTrailChanged = bTrail && Trail && WriteTrail(*Ammo, *Trail);
			const TCHAR* FireSound = FindFireSound(Spec.Package);
			const bool bFireSoundChanged = bFireSound && FireSound && WriteFireSound(*Ammo, FireSound, Spec.Package);
			const bool bCoverChanged = bCover && WriteCoverPenetration(*Ammo, Spec.Package);
			if (!WriteDisplay(*Ammo, Spec, /*bOnlyEmpty*/ !bLabels) && !bTrailChanged && !bFireSoundChanged && !bCoverChanged)
			{
				UE_LOG(LogGinkgoCombatCommandlet, Display, TEXT("%s 已经有了，不动（-Force 按这里的数重写）"), Spec.Package);
				return true;
			}
			UE_LOG(LogGinkgoCombatCommandlet, Display, TEXT("%s 已经有了，伤害的数不动，存下；全名、名字、说明、口径、图：%s / %s / %s / %s / %s"), Spec.Package,
				*Ammo->DisplayName.ToString(), *Ammo->ShortName.ToString(), *Ammo->Description.ToString(), *Ammo->HudLabel.ToString(), *Ammo->HudRound.ToString());
			return SaveAmmo(*Existing, *Ammo, Spec);
		}
		UPackage* Package = CreatePackage(Spec.Package);
		Package->FullyLoad();
		// 已有的原地改：设置、别的资产引用着它
		UGinkgoAmmoDefinition* Ammo = FindObject<UGinkgoAmmoDefinition>(Package, *AssetName);
		const bool bCreated = Ammo == nullptr;
		if (bCreated)
		{
			Ammo = NewObject<UGinkgoAmmoDefinition>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		Ammo->Ammo = Spec.Ammo;
		Ammo->DisplayName = FText::AsCultureInvariant(Spec.DisplayName);
		Ammo->Delivery = Spec.Delivery;
		Ammo->ProjectilePayload = Spec.ProjectilePayload;
		Ammo->ProjectileMesh = Spec.ProjectileMesh ? TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Spec.ProjectileMesh)) : TSoftObjectPtr<UStaticMesh>();
		Ammo->LiveRoundMesh = Spec.LiveRoundMesh ? TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Spec.LiveRoundMesh)) : TSoftObjectPtr<UStaticMesh>();
		Ammo->RoundMaterial = Spec.RoundMaterial ? TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Spec.RoundMaterial)) : TSoftObjectPtr<UMaterialInterface>();
		Ammo->Damage = Spec.Damage;
		Ammo->ArmorDamage = Spec.ArmorDamage;
		Ammo->ArmorPenetration = Spec.Penetration;
		Ammo->CoverPenetration = FindCoverPenetration(Spec.Package);
		Ammo->BodyParts.Head = Spec.Parts[0];
		Ammo->BodyParts.UpperChest = Spec.Parts[1];
		Ammo->BodyParts.Abdomen = Spec.Parts[2];
		Ammo->BodyParts.UpperLimbs = Spec.Parts[3];
		Ammo->BodyParts.Legs = Spec.Parts[4];
		Ammo->BodyParts.Feet = Spec.Parts[5];
		Ammo->Falloff.FullRange = Spec.FullRange * 100.f;
		Ammo->Falloff.MinRange = Spec.MinRange * 100.f;
		Ammo->Falloff.MinMultiplier = Spec.MinMultiplier;
		Ammo->KillPush = Spec.KillPush;
		Ammo->StartingReserve = Spec.StartingReserve;
		Ammo->WeaponEffects.PelletCount = Spec.PelletCount;
		Ammo->WeaponEffects.HipSpreadScale = Spec.HipSpreadScale;
		Ammo->WeaponEffects.AdsSpreadScale = Spec.AdsSpreadScale;
		WriteDisplay(*Ammo, Spec, /*bOnlyEmpty*/ false);
		if (const FTrailSpec* Trail = FindTrailSpec(Spec.Package))
		{
			WriteTrail(*Ammo, *Trail);
		}
		if (const TCHAR* FireSound = FindFireSound(Spec.Package))
		{
			WriteFireSound(*Ammo, FireSound, Spec.Package);
		}
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Ammo);
		}
		if (!SaveAmmo(*Package, *Ammo, Spec))
		{
			return false;
		}
		UE_LOG(LogGinkgoCombatCommandlet, Display,
			TEXT("%s（%s）：%s，伤害 %.0f，护甲伤害 %.0f，穿甲率 %.2f，掩体穿射 %.2f，头 %.2f 上胸 %.2f 腹 %.2f 上肢 %.2f 腿 %.2f 脚 %.2f，衰减 %.0f 到 %.0f m 降到 %.2f，推 %.0f cm/s，")
			TEXT("开局 %d 发，弹丸 %d，扩散 ×%.2f / ×%.2f，%s"),
			Spec.Package, *Spec.Ammo.ToString(), bCreated ? TEXT("新建") : TEXT("重写"), Spec.Damage, Spec.ArmorDamage, Spec.Penetration, Ammo->CoverPenetration,
			Spec.Parts[0], Spec.Parts[1], Spec.Parts[2], Spec.Parts[3], Spec.Parts[4], Spec.Parts[5], Spec.FullRange, Spec.MinRange, Spec.MinMultiplier, Spec.KillPush,
			Spec.StartingReserve, Spec.PelletCount, Spec.HipSpreadScale, Spec.AdsSpreadScale, *Ammo->GetShortName().ToString());
		return true;
	}
}

UGinkgoCombatCommandlet::UGinkgoCombatCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGinkgoCombatCommandlet::Main(const FString& Params)
{
	using namespace GinkgoCombat;
	if (FParse::Param(*Params, TEXT("Ammo")))
	{
		const bool bForce = FParse::Param(*Params, TEXT("Force"));
		const bool bLabels = FParse::Param(*Params, TEXT("Labels"));
		// -Trail：已有的也按表重写弹道的烟（Docs/Design/Weapons.md「弹道的烟」），别的数不动
		const bool bTrail = FParse::Param(*Params, TEXT("Trail"));
		// -FireSound：已有的也写子弹自己的枪声，别的数不动
		const bool bFireSound = FParse::Param(*Params, TEXT("FireSound"));
		// -Cover：已有的也按表写掩体穿射系数（Docs/Design/Damage.md「穿射」），别的数不动
		const bool bCover = FParse::Param(*Params, TEXT("Cover"));
		// -Only=DA_Ammo_Gauge12Slug：只写这一个（-Force 重写一种的数，别的不动）
		FString Only;
		FParse::Value(*Params, TEXT("Only="), Only);
		bool bWritten = true;
		for (const FAmmoSpec& Spec : MakeAmmoSpecs())
		{
			if (Only.IsEmpty() || FPackageName::GetShortName(Spec.Package) == Only)
			{
				bWritten &= WriteAmmo(Spec, bForce, bLabels, bTrail, bFireSound, bCover);
			}
		}
		return bWritten ? 0 : 1;
	}
	UE_LOG(LogGinkgoCombatCommandlet, Error, TEXT("用法：-run=GinkgoCombat -Ammo [-Force] [-Labels] [-Trail] [-FireSound] [-Cover] [-Only=DA_Ammo_Gauge12Slug]"));
	return 1;
}

#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Combat/GinkgoGrenadeProjectile.h"
#include "Engine/HitResult.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponMechanism.h"
#include "Weapon/GinkgoWeaponTypes.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoCombatTests
{
	constexpr float Tolerance = 1e-3f;

	/** 一种测试用的子弹（不读资产）：数照 Docs/Design/Damage.md「子弹的数」 */
	UGinkgoAmmoDefinition* MakeAmmo(const FGameplayTag& Tag, const float Damage, const float ArmorDamage, const float Penetration, const FGinkgoBodyPartMultipliers& Parts,
		const FGinkgoDamageFalloff& Falloff = FGinkgoDamageFalloff())
	{
		UGinkgoAmmoDefinition* Ammo = NewObject<UGinkgoAmmoDefinition>();
		Ammo->Ammo = Tag;
		Ammo->Damage = Damage;
		Ammo->ArmorDamage = ArmorDamage;
		Ammo->ArmorPenetration = Penetration;
		Ammo->BodyParts = Parts;
		Ammo->Falloff = Falloff;
		return Ammo;
	}

	FGinkgoBodyPartMultipliers MakeParts(const float Head, const float UpperChest, const float Abdomen, const float UpperLimbs, const float Legs, const float Feet)
	{
		FGinkgoBodyPartMultipliers Parts;
		Parts.Head = Head;
		Parts.UpperChest = UpperChest;
		Parts.Abdomen = Abdomen;
		Parts.UpperLimbs = UpperLimbs;
		Parts.Legs = Legs;
		Parts.Feet = Feet;
		return Parts;
	}

	/** 7.62 × 39：34 / 34 / 0.15，头 2、上胸 1.1；50 m 起衰减，150 m 0.8 */
	UGinkgoAmmoDefinition* MakeKalashnikovAmmo()
	{
		FGinkgoDamageFalloff Falloff;
		Falloff.FullRange = 5000.f;
		Falloff.MinRange = 15000.f;
		Falloff.MinMultiplier = 0.8f;
		return MakeAmmo(FWeaponTags::Ammo_M43762x39_Standard, 34.f, 34.f, 0.15f, MakeParts(2.f, 1.1f, 1.f, 0.75f, 0.8f, 0.6f), Falloff);
	}

	FGinkgoArmorPiece MakeVest(const float Durability = 100.f)
	{
		FGinkgoArmorPiece Vest;
		Vest.Name = TEXT("Vest");
		Vest.Covers = {EGinkgoBodyPart::UpperChest, EGinkgoBodyPart::Abdomen};
		Vest.MaxDurability = Durability;
		return Vest;
	}

	/** 一个靶子：没有网格，打到哪里都算 DefaultPart（上胸） */
	UGinkgoDamageableComponent* MakeTarget(const TArray<FGinkgoArmorPiece>& Armor)
	{
		UGinkgoDamageableComponent* Target = NewObject<UGinkgoDamageableComponent>();
		Target->SetArmor(Armor);
		Target->Revive();
		return Target;
	}

	/** 一直打到死，返回打了几枪（上限 50） */
	int32 ShotsToKill(UGinkgoDamageableComponent& Target, const UGinkgoAmmoDefinition& Ammo, const float Distance)
	{
		const FHitResult Hit;
		for (int32 Shot = 1; Shot <= 50; ++Shot)
		{
			if (Target.ApplyBulletHit(Ammo, 1.f, Hit, FVector::ForwardVector, Distance, nullptr).bKilled)
			{
				return Shot;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatArmorMathTest, "CaseGinkgo.Combat.ArmorMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatArmorMathTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCombatTests;
	using Ginkgo::Combat::ApplyHitDamage;

	// 没有护甲：全扣血
	{
		float Armor = 0.f;
		const FGinkgoDamageOutcome Outcome = ApplyHitDamage({37.4f, 37.4f, 0.15f}, Armor);
		TestEqual(TEXT("没有护甲：全扣血"), Outcome.HealthDamage, 37.4f, Tolerance);
		TestTrue(TEXT("没有护甲：不算打在护甲上"), !Outcome.bArmorHit && !Outcome.bArmorBroken && Outcome.ArmorDamage == 0.f);
	}
	// 护甲还多：穿甲率那一份扣血，剩下的扣护甲
	{
		float Armor = 100.f;
		const FGinkgoDamageOutcome Outcome = ApplyHitDamage({37.4f, 37.4f, 0.15f}, Armor);
		TestEqual(TEXT("护甲挡着：穿甲的 15% 扣血"), Outcome.HealthDamage, 37.4f * 0.15f, Tolerance);
		TestEqual(TEXT("护甲挡着：不穿甲的 85% 扣护甲"), Outcome.ArmorDamage, 37.4f * 0.85f, Tolerance);
		TestEqual(TEXT("护甲剩下的"), Armor, 100.f - 37.4f * 0.85f, Tolerance);
		TestTrue(TEXT("打在护甲上，没打穿"), Outcome.bArmorHit && !Outcome.bArmorBroken);
	}
	// 这一下打穿：护甲只吃掉它剩的那么多，没挡住的那一份接着扣血（98k 打上胸：100 / 112.5 / 0.25，护甲剩 10）
	{
		float Armor = 10.f;
		const FGinkgoDamageOutcome Outcome = ApplyHitDamage({100.f, 112.5f, 0.25f}, Armor);
		const float Absorbed = 10.f / (112.5f * 0.75f);
		TestEqual(TEXT("打穿：护甲扣到 0"), Armor, 0.f, Tolerance);
		TestEqual(TEXT("打穿：扣的护甲就是剩的"), Outcome.ArmorDamage, 10.f, Tolerance);
		TestEqual(TEXT("打穿：穿甲的一份加上没挡住的"), Outcome.HealthDamage, 25.f + 75.f * (1.f - Absorbed), Tolerance);
		TestTrue(TEXT("打穿"), Outcome.bArmorHit && Outcome.bArmorBroken);
	}
	// 刚好打完：算打穿，没有多出来的
	{
		float Armor = 40.f;
		const FGinkgoDamageOutcome Outcome = ApplyHitDamage({40.f, 40.f, 0.f}, Armor);
		TestTrue(TEXT("刚好打完：算打穿"), Outcome.bArmorBroken && Armor == 0.f);
		TestEqual(TEXT("刚好打完：不扣血"), Outcome.HealthDamage, 0.f, Tolerance);
	}
	// 全穿甲：护甲不扣，全扣血
	{
		float Armor = 50.f;
		const FGinkgoDamageOutcome Outcome = ApplyHitDamage({30.f, 30.f, 1.f}, Armor);
		TestEqual(TEXT("全穿甲：全扣血"), Outcome.HealthDamage, 30.f, Tolerance);
		TestEqual(TEXT("全穿甲：护甲不动"), Armor, 50.f, Tolerance);
	}
	// 伤害与护甲都守恒：扣的血 + 没扣的那一份（被护甲挡掉的）= 基础伤害（护甲伤害同伤害一样时）
	for (const float Start : {5.f, 20.f, 31.79f, 60.f, 100.f})
	{
		float Armor = Start;
		const FGinkgoDamageOutcome Outcome = ApplyHitDamage({37.4f, 37.4f, 0.15f}, Armor);
		TestEqual(*FString::Printf(TEXT("护甲 %.2f：扣的血加挡掉的等于伤害"), Start), Outcome.HealthDamage + Outcome.ArmorDamage, 37.4f, Tolerance);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatFalloffTest, "CaseGinkgo.Combat.Falloff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatFalloffTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Combat::FalloffMultiplier;
	FGinkgoDamageFalloff Falloff;
	Falloff.FullRange = 5000.f;
	Falloff.MinRange = 15000.f;
	Falloff.MinMultiplier = 0.8f;
	TestEqual(TEXT("近处满伤害"), FalloffMultiplier(Falloff, 100.f), 1.f, GinkgoCombatTests::Tolerance);
	TestEqual(TEXT("FullRange 上满伤害"), FalloffMultiplier(Falloff, 5000.f), 1.f, GinkgoCombatTests::Tolerance);
	TestEqual(TEXT("中间直线降"), FalloffMultiplier(Falloff, 10000.f), 0.9f, GinkgoCombatTests::Tolerance);
	TestEqual(TEXT("MinRange 上是 MinMultiplier"), FalloffMultiplier(Falloff, 15000.f), 0.8f, GinkgoCombatTests::Tolerance);
	TestEqual(TEXT("再远不变"), FalloffMultiplier(Falloff, 40000.f), 0.8f, GinkgoCombatTests::Tolerance);
	TestEqual(TEXT("没配衰减：多远都满"), FalloffMultiplier(FGinkgoDamageFalloff(), 100000.f), 1.f, GinkgoCombatTests::Tolerance);
	Falloff.MinRange = Falloff.FullRange;
	TestEqual(TEXT("MinRange 不比 FullRange 远：不衰减"), FalloffMultiplier(Falloff, 100000.f), 1.f, GinkgoCombatTests::Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatBodyPartsTest, "CaseGinkgo.Combat.BodyParts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatBodyPartsTest::RunTest(const FString& Parameters)
{
	// 人体模型的一截骨骼树（子 → 父）：扭转骨、手指、IK 骨骼不在表里，往上找
	const TMap<FName, FName> Parents = {
		{TEXT("root"), NAME_None}, {TEXT("pelvis"), TEXT("root")}, {TEXT("spine_01"), TEXT("pelvis")}, {TEXT("spine_02"), TEXT("spine_01")},
		{TEXT("spine_03"), TEXT("spine_02")}, {TEXT("spine_04"), TEXT("spine_03")}, {TEXT("spine_05"), TEXT("spine_04")}, {TEXT("neck_01"), TEXT("spine_05")},
		{TEXT("neck_02"), TEXT("neck_01")}, {TEXT("head"), TEXT("neck_02")}, {TEXT("clavicle_r"), TEXT("spine_05")}, {TEXT("upperarm_r"), TEXT("clavicle_r")},
		{TEXT("lowerarm_r"), TEXT("upperarm_r")}, {TEXT("lowerarm_twist_01_r"), TEXT("lowerarm_r")}, {TEXT("hand_r"), TEXT("lowerarm_r")},
		{TEXT("index_01_r"), TEXT("hand_r")}, {TEXT("thigh_l"), TEXT("pelvis")}, {TEXT("thigh_twist_01_l"), TEXT("thigh_l")}, {TEXT("calf_l"), TEXT("thigh_l")},
		{TEXT("foot_l"), TEXT("calf_l")}, {TEXT("ball_l"), TEXT("foot_l")}, {TEXT("ik_foot_root"), TEXT("root")}, {TEXT("ik_foot_l"), TEXT("ik_foot_root")},
	};
	const TMap<FName, EGinkgoBodyPart> Parts = Ginkgo::Combat::MakeMannequinBodyParts();
	const auto Find = [&Parts, &Parents](const TCHAR* Bone)
	{
		return Ginkgo::Combat::FindBodyPart(Bone, Parts, EGinkgoBodyPart::UpperChest, [&Parents](const FName Child)
		{
			const FName* Parent = Parents.Find(Child);
			return Parent ? *Parent : NAME_None;
		});
	};
	const TPair<const TCHAR*, EGinkgoBodyPart> Expected[] = {
		{TEXT("head"), EGinkgoBodyPart::Head}, {TEXT("neck_01"), EGinkgoBodyPart::Head}, {TEXT("spine_05"), EGinkgoBodyPart::UpperChest},
		{TEXT("clavicle_r"), EGinkgoBodyPart::UpperChest}, {TEXT("spine_02"), EGinkgoBodyPart::Abdomen}, {TEXT("pelvis"), EGinkgoBodyPart::Abdomen},
		{TEXT("upperarm_r"), EGinkgoBodyPart::UpperLimbs}, {TEXT("lowerarm_twist_01_r"), EGinkgoBodyPart::UpperLimbs}, {TEXT("index_01_r"), EGinkgoBodyPart::UpperLimbs},
		{TEXT("thigh_twist_01_l"), EGinkgoBodyPart::Legs}, {TEXT("calf_l"), EGinkgoBodyPart::Legs}, {TEXT("foot_l"), EGinkgoBodyPart::Feet},
		{TEXT("ball_l"), EGinkgoBodyPart::Feet},
	};
	for (const TPair<const TCHAR*, EGinkgoBodyPart>& Entry : Expected)
	{
		TestEqual(*FString::Printf(TEXT("%s 算%s"), Entry.Key, Ginkgo::Combat::DescribeBodyPart(Entry.Value)), Find(Entry.Key), Entry.Value);
	}
	TestEqual(TEXT("IK 骨骼找不到部位：用默认的"), Find(TEXT("ik_foot_l")), EGinkgoBodyPart::UpperChest);
	TestEqual(TEXT("不认识的骨骼：用默认的"), Find(TEXT("no_such_bone")), EGinkgoBodyPart::UpperChest);
	// 表里六个部位都有
	TSet<EGinkgoBodyPart> Covered;
	for (const TPair<FName, EGinkgoBodyPart>& Entry : Parts)
	{
		Covered.Add(Entry.Value);
	}
	TestEqual(TEXT("人体模型的表：六个部位都有"), Covered.Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatDamageableTest, "CaseGinkgo.Combat.Damageable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatDamageableTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCombatTests;
	const UGinkgoAmmoDefinition* Ak = MakeKalashnikovAmmo();

	// 没有护甲：上胸 37.4 一枪，三枪倒（Damage.md）
	UGinkgoDamageableComponent* Bare = MakeTarget({});
	TestEqual(TEXT("AK 打没穿背心的上胸：三枪"), ShotsToKill(*Bare, *Ak, 1000.f), 3);

	// 背心是第二条血：先挡 85%，打穿那一下没挡住的接着扣血，一共六枪
	UGinkgoDamageableComponent* Vested = MakeTarget({MakeVest()});
	int32 Died = 0;
	Vested->OnDied.AddLambda([&Died](UGinkgoDamageableComponent&, const FGinkgoDamageEvent&) { ++Died; });
	const FHitResult Hit;
	const FGinkgoDamageEvent First = Vested->ApplyBulletHit(*Ak, 1.f, Hit, FVector::ForwardVector, 1000.f, nullptr);
	TestEqual(TEXT("第一枪：部位是默认的上胸"), First.Part, EGinkgoBodyPart::UpperChest);
	TestEqual(TEXT("第一枪：伤害 34 × 1.1"), First.BaseDamage, 37.4f, Tolerance);
	TestEqual(TEXT("第一枪：扣血只有穿甲的一份"), First.Outcome.HealthDamage, 37.4f * 0.15f, Tolerance);
	TestTrue(TEXT("第一枪：打在背心上"), First.Outcome.bArmorHit && First.ArmorPiece == TEXT("Vest"));
	TestEqual(TEXT("背心：一共六枪"), 1 + ShotsToKill(*Vested, *Ak, 1000.f), 6);
	TestEqual(TEXT("背心打穿了"), Vested->GetArmor()[0].Durability, 0.f, Tolerance);
	TestEqual(TEXT("死了一次"), Died, 1);
	TestTrue(TEXT("死了"), Vested->IsDead() && Vested->GetHealth() == 0.f);
	const FGinkgoDamageEvent AfterDeath = Vested->ApplyBulletHit(*Ak, 1.f, Hit, FVector::ForwardVector, 1000.f, nullptr);
	TestTrue(TEXT("死了以后打不扣、不再死一次"), AfterDeath.BaseDamage == 0.f && !AfterDeath.bKilled && Died == 1);

	// 复位：血、护甲回满
	Vested->Revive();
	TestTrue(TEXT("复位：血满、背心满"), !Vested->IsDead() && Vested->GetHealth() == Vested->GetMaxHealth() && Vested->GetArmor()[0].Durability == 100.f);

	// 枪的倍率、距离衰减乘在一起：1.1 倍的枪在 150 m 外（0.8）
	UGinkgoDamageableComponent* Far = MakeTarget({});
	const FGinkgoDamageEvent FarHit = Far->ApplyBulletHit(*Ak, 1.1f, Hit, FVector::ForwardVector, 15000.f, nullptr);
	TestEqual(TEXT("枪的倍率 × 部位 × 衰减"), FarHit.BaseDamage, 34.f * 1.1f * 1.1f * 0.8f, Tolerance);

	// 背心不盖的部位不扣它：只有头盔的靶子，打上胸（默认部位）直接扣血
	FGinkgoArmorPiece Helmet;
	Helmet.Name = TEXT("Helmet");
	Helmet.Covers = {EGinkgoBodyPart::Head};
	Helmet.MaxDurability = 50.f;
	UGinkgoDamageableComponent* HelmetOnly = MakeTarget({Helmet});
	const FGinkgoDamageEvent Chest = HelmetOnly->ApplyBulletHit(*Ak, 1.f, Hit, FVector::ForwardVector, 1000.f, nullptr);
	TestTrue(TEXT("头盔不盖上胸：全扣血、头盔不动"), !Chest.Outcome.bArmorHit && HelmetOnly->GetArmor()[0].Durability == 50.f);
	TestEqual(TEXT("头盔不盖上胸：扣满"), Chest.Outcome.HealthDamage, 37.4f, Tolerance);

	// Damage.md 的几个数：98k 上胸一枪倒，M1911 上胸四枪，九颗鹿弹近处全中一枪倒、30 m 外一颗只剩 0.4
	const UGinkgoAmmoDefinition* Mauser = MakeAmmo(FWeaponTags::Ammo_Mauser792x57_Standard, 80.f, 90.f, 0.25f, MakeParts(2.5f, 1.25f, 1.f, 0.7f, 0.75f, 0.6f));
	TestEqual(TEXT("98k 上胸：一枪"), ShotsToKill(*MakeTarget({}), *Mauser, 3000.f), 1);
	const UGinkgoAmmoDefinition* Pistol = MakeAmmo(FWeaponTags::Ammo_ACP45_Standard, 30.f, 18.f, 0.05f, MakeParts(2.f, 1.1f, 1.f, 0.75f, 0.8f, 0.6f));
	TestEqual(TEXT("M1911 上胸：四枪"), ShotsToKill(*MakeTarget({}), *Pistol, 500.f), 4);
	FGinkgoDamageFalloff Buckshot;
	Buckshot.FullRange = 800.f;
	Buckshot.MinRange = 3000.f;
	Buckshot.MinMultiplier = 0.4f;
	const UGinkgoAmmoDefinition* Pellet = MakeAmmo(FWeaponTags::Ammo_Gauge12_Buckshot, 13.f, 10.f, 0.f, MakeParts(1.5f, 1.f, 1.f, 0.8f, 0.8f, 0.6f), Buckshot);
	UGinkgoDamageableComponent* Close = MakeTarget({});
	bool bCloseKill = false;
	for (int32 Index = 0; Index < 9; ++Index)
	{
		bCloseKill |= Close->ApplyBulletHit(*Pellet, 1.f, Hit, FVector::ForwardVector, 300.f, nullptr).bKilled;
	}
	TestTrue(TEXT("九颗鹿弹近处全中上胸：117，一枪倒"), bCloseKill);
	TestEqual(TEXT("30 m 外一颗鹿弹：13 × 0.4"), MakeTarget({})->ApplyBulletHit(*Pellet, 1.f, Hit, FVector::ForwardVector, 3000.f, nullptr).BaseDamage, 5.2f, Tolerance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatAmmoAssetsTest, "CaseGinkgo.Combat.AmmoAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatAmmoAssetsTest::RunTest(const FString& Parameters)
{
	// 七种弹都有数据（设置的 AmmoDefinitions，GinkgoCombat -Ammo 生成；2026-11-07 加了独头弹、S.m.K.）。数可以手调，这里只查找得到、范围对
	for (const FGameplayTag& Tag : {FWeaponTags::Ammo_Mauser792x57_Standard.GetTag(), FWeaponTags::Ammo_M43762x39_Standard.GetTag(), FWeaponTags::Ammo_NATO762x51_Ball.GetTag(),
		FWeaponTags::Ammo_ACP45_Standard.GetTag(), FWeaponTags::Ammo_Gauge12_Buckshot.GetTag(), FWeaponTags::Ammo_Gauge12_Slug.GetTag(), FWeaponTags::Ammo_Mauser792x57_SmK.GetTag()})
	{
		const UGinkgoAmmoDefinition* Ammo = UGinkgoAmmoDefinition::Find(Tag);
		if (!TestNotNull(*FString::Printf(TEXT("%s 有数据"), *Tag.ToString()), Ammo))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 的弹种对得上"), *Tag.ToString()), Ammo->Ammo, Tag);
		TestTrue(*FString::Printf(TEXT("%s：伤害为正、穿甲率在 0–1"), *Tag.ToString()), Ammo->Damage > 0.f && Ammo->ArmorPenetration >= 0.f && Ammo->ArmorPenetration <= 1.f);
		TestEqual(*FString::Printf(TEXT("%s：射线"), *Tag.ToString()), Ammo->Delivery, EGinkgoAmmoDelivery::Hitscan);
		// 弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：每种子弹都有，系统读得到，弹速在手枪弹到步枪弹之间，淡
		const FGinkgoAmmoTrail& Trail = Ammo->Trail;
		TestTrue(*FString::Printf(TEXT("%s：有弹道的烟，系统的资产在"), *Tag.ToString()), !Trail.System.IsNull()
			&& FPackageName::DoesPackageExist(Trail.System.ToSoftObjectPath().GetLongPackageName()));
		TestTrue(*FString::Printf(TEXT("%s：弹速 %.0f m/s 在 200–1000"), *Tag.ToString(), Trail.Speed), Trail.Speed > 200.f && Trail.Speed < 1000.f);
		TestTrue(*FString::Printf(TEXT("%s：有浓淡（%.2f），留不到 3 s"), *Tag.ToString(), Trail.Opacity), Trail.Opacity > 0.f && Trail.Opacity <= 1.f
			&& Trail.Lifetime > 0.f && Trail.Lifetime < 3.f);
	}

	// M79 的三种 40 × 46mm 弹头：同属 Ammo.Grenade40x46 口径，弹种轮盘按 HE -> Flash -> Flechette 顺序列出
	const TArray<FGameplayTag, TInlineAllocator<8>> M79Choices = UGinkgoAmmoDefinition::GetChoices(FWeaponTags::Ammo_Grenade40x46_HE);
	TestEqual(TEXT("40mm 口径一共三种弹头"), M79Choices.Num(), 3);
	if (M79Choices.Num() == 3)
	{
		TestEqual(TEXT("第 1 种是 M406 高爆弹"), M79Choices[0], FWeaponTags::Ammo_Grenade40x46_HE.GetTag());
		TestEqual(TEXT("第 2 种是碰炸闪光弹"), M79Choices[1], FWeaponTags::Ammo_Grenade40x46_Flash.GetTag());
		TestEqual(TEXT("第 3 种是 XM576 钢镖霰弹"), M79Choices[2], FWeaponTags::Ammo_Grenade40x46_Flechette.GetTag());
	}

	const UGinkgoAmmoDefinition* Grenade = UGinkgoAmmoDefinition::Find(FWeaponTags::Ammo_Grenade40x46_HE);
	if (TestNotNull(TEXT("Ammo.Grenade40x46.HE 有数据"), Grenade))
	{
		TestEqual(TEXT("40mm 高爆榴弹：抛射物投送"), Grenade->Delivery, EGinkgoAmmoDelivery::Projectile);
		TestEqual(TEXT("40mm 高爆榴弹：高爆破片战斗部"), Grenade->ProjectilePayload, EGinkgoProjectilePayload::HighExplosive);
		TestEqual(TEXT("40mm 高爆榴弹：扩大两倍后的基础伤害为 360"), Grenade->Damage, 360.f, GinkgoCombatTests::Tolerance);
		TestEqual(TEXT("40mm 高爆榴弹：扩大两倍后的护甲伤害为 400"), Grenade->ArmorDamage, 400.f, GinkgoCombatTests::Tolerance);
		TestTrue(TEXT("40mm 高爆榴弹：穿甲率在合法区间"), Grenade->ArmorPenetration >= 0.f && Grenade->ArmorPenetration <= 1.f);
		TestTrue(TEXT("40mm 高爆榴弹：出膛初速 50–120 m/s 且无射线烟轨"), Grenade->Trail.Speed >= 50.f && Grenade->Trail.Speed <= 120.f && Grenade->Trail.System.IsNull());
		TestTrue(TEXT("40mm 高爆榴弹：飞行弹头与活弹网格存在"), !Grenade->ProjectileMesh.IsNull() && !Grenade->LiveRoundMesh.IsNull());
	}

	const UGinkgoAmmoDefinition* FlashRound = UGinkgoAmmoDefinition::Find(FWeaponTags::Ammo_Grenade40x46_Flash);
	if (TestNotNull(TEXT("Ammo.Grenade40x46.Flash 有数据"), FlashRound))
	{
		TestEqual(TEXT("40mm 闪光弹：抛射物投送"), FlashRound->Delivery, EGinkgoAmmoDelivery::Projectile);
		TestEqual(TEXT("40mm 闪光弹：碰炸闪光战斗部"), FlashRound->ProjectilePayload, EGinkgoProjectilePayload::Flashbang);
		TestTrue(TEXT("40mm 闪光弹：初速明显快于手投闪光弹（> 40 m/s）且无直接伤害"), FlashRound->Trail.Speed > 40.f && FlashRound->Damage == 0.f);
		TestTrue(TEXT("40mm 闪光弹：开局自带特殊弹后备，且飞行弹头与活弹网格存在"), FlashRound->StartingReserve > 0 && !FlashRound->ProjectileMesh.IsNull() && !FlashRound->LiveRoundMesh.IsNull());
	}

	const UGinkgoAmmoDefinition* Flechette = UGinkgoAmmoDefinition::Find(FWeaponTags::Ammo_Grenade40x46_Flechette);
	if (TestNotNull(TEXT("Ammo.Grenade40x46.Flechette 有数据"), Flechette))
	{
		TestEqual(TEXT("40mm 镖弹：与普通霰弹一致走射线检测"), Flechette->Delivery, EGinkgoAmmoDelivery::Hitscan);
		TestEqual(TEXT("40mm 镖弹：单次发射 16 枚钢镖"), Flechette->WeaponEffects.PelletCount, 16);
		TestTrue(TEXT("40mm 镖弹：单枚伤害为正、有烟轨且开局自带特殊弹后备"), Flechette->Damage > 0.f && !Flechette->Trail.System.IsNull()
			&& Flechette->Trail.Opacity > 0.f && Flechette->StartingReserve > 0 && !Flechette->LiveRoundMesh.IsNull());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatGrenadeBlastMathTest, "CaseGinkgo.Combat.GrenadeBlastMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatGrenadeBlastMathTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Combat::ComputeExplosionScale;
	using GinkgoCombatTests::Tolerance;

	TestEqual(TEXT("扩大两倍后的内圈满伤害半径为 320 cm"), AGinkgoGrenadeProjectile::DefaultInnerRadius, 320.f, Tolerance);
	TestEqual(TEXT("扩大两倍后的外圈杀伤半径为 1100 cm"), AGinkgoGrenadeProjectile::DefaultBlastRadius, 1100.f, Tolerance);
	TestEqual(TEXT("高爆特效半径缩放倍率为 2.0"), AGinkgoGrenadeProjectile::BlastVfxRadiusScale, 2.f, Tolerance);
	TestEqual(TEXT("爆心满额伤害"), ComputeExplosionScale(0.f, AGinkgoGrenadeProjectile::DefaultInnerRadius, AGinkgoGrenadeProjectile::DefaultBlastRadius, 0.2f), 1.f, Tolerance);
	TestEqual(TEXT("内圈边界满额伤害"), ComputeExplosionScale(320.f, AGinkgoGrenadeProjectile::DefaultInnerRadius, AGinkgoGrenadeProjectile::DefaultBlastRadius, 0.2f), 1.f, Tolerance);
	TestEqual(TEXT("内外圈中点线性衰减到 0.6"), ComputeExplosionScale(710.f, AGinkgoGrenadeProjectile::DefaultInnerRadius, AGinkgoGrenadeProjectile::DefaultBlastRadius, 0.2f), 0.6f, Tolerance);
	TestEqual(TEXT("外圈边界保留最低比例 0.2"), ComputeExplosionScale(1100.f, AGinkgoGrenadeProjectile::DefaultInnerRadius, AGinkgoGrenadeProjectile::DefaultBlastRadius, 0.2f), 0.2f, Tolerance);
	TestEqual(TEXT("超出杀伤半径归零"), ComputeExplosionScale(1101.f, AGinkgoGrenadeProjectile::DefaultInnerRadius, AGinkgoGrenadeProjectile::DefaultBlastRadius, 0.2f), 0.f, Tolerance);
	return true;
}

namespace
{
	/**
	 * 按着扳机连发 Shots 枪（同武器组件：第一枪在 0，后面的到点以后的第一帧响，GetScheduledShotTime / GetNextShotTime 排），每一枪留没留弹道的烟
	 * @param Dt - 帧长
	 */
	TArray<bool> FireBurstTrails(const double Interval, const double Dt, const int32 Shots, const double MinInterval)
	{
		TArray<bool> Trails;
		double NextShot = -1e9;
		double LastTrail = -1e9;
		for (int32 Frame = 0; Trails.Num() < Shots && Frame < 100000; ++Frame)
		{
			const double Now = Frame * Dt;
			const bool bChained = !Trails.IsEmpty();
			if (bChained && Now < NextShot - 1e-9)
			{
				continue;
			}
			const double Scheduled = Ginkgo::Weapon::GetScheduledShotTime(NextShot, Now, Interval, bChained);
			NextShot = Ginkgo::Weapon::GetNextShotTime(NextShot, Now, Interval, bChained);
			const bool bTrail = Ginkgo::Weapon::ShouldLeaveTrail(Scheduled, LastTrail, MinInterval);
			if (bTrail)
			{
				LastTrail = Scheduled;
			}
			Trails.Add(bTrail);
		}
		return Trails;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWeaponTrailCadenceTest, "CaseGinkgo.Weapon.TrailCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWeaponTrailCadenceTest::RunTest(const FString& Parameters)
{
	// 弹道的烟最短隔 0.1 s 一道（你说的：AK-47 的射速刚好每枪都留，MG3 对齐到它）：按排的时刻数，任何帧率一样
	const double MinInterval = 0.1;
	for (const double Fps : {25.0, 30.0, 60.0, 144.0, 240.0})
	{
		const TArray<bool> Ak = FireBurstTrails(60.0 / 600.0, 1.0 / Fps, 30, MinInterval);
		TestEqual(*FString::Printf(TEXT("AK 600 发/分，%.0f fps：30 枪 30 道"), Fps), Ak.FilterByPredicate([](const bool b) { return b; }).Num(), 30);
		const TArray<bool> Mg3 = FireBurstTrails(60.0 / 1200.0, 1.0 / Fps, 100, MinInterval);
		int32 Alternating = 0;
		for (int32 Index = 0; Index < Mg3.Num(); ++Index)
		{
			Alternating += Mg3[Index] == (Index % 2 == 0) ? 1 : 0;
		}
		TestEqual(*FString::Printf(TEXT("MG3 1200 发/分，%.0f fps：隔一枪一道（100 枪 50 道，单数的枪有）"), Fps), Alternating, 100);
	}
	// 真的隔着的帧：144 fps 时 AK 两枪隔 14、15 帧（0.097、0.104 s），排的都是 0.1 s
	TestEqual(TEXT("排的时刻：接着响的是该响的那一刻"), Ginkgo::Weapon::GetScheduledShotTime(1.0, 1.004, 0.1, true), 1.0, 1e-12);
	TestEqual(TEXT("排的时刻：晚了一个间隔以上的从现在算"), Ginkgo::Weapon::GetScheduledShotTime(1.0, 1.2, 0.1, true), 1.2, 1e-12);
	TestEqual(TEXT("排的时刻：不是接着响的从现在算"), Ginkgo::Weapon::GetScheduledShotTime(1.0, 1.004, 0.1, false), 1.004, 1e-12);
	TestFalse(TEXT("隔 0.0999 s 不留"), Ginkgo::Weapon::ShouldLeaveTrail(1.0999 - 1e-3, 1.0, 0.1));
	TestTrue(TEXT("隔 0.1 s 少一点点（浮点）也留"), Ginkgo::Weapon::ShouldLeaveTrail(1.1 - 1e-9, 1.0, 0.1));
	TestTrue(TEXT("最短间隔 0：每枪都留"), Ginkgo::Weapon::ShouldLeaveTrail(1.0, 1.0, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatTrailShapeTest, "CaseGinkgo.Combat.TrailShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatTrailShapeTest::RunTest(const FString& Parameters)
{
	// 弹道的烟的形状（Ginkgo::Combat::MakeTrailShape）：沿线隔 50 cm 一个点、两头都有，160 个封顶；长到头 = 长度 / 弹速，是寿命的 1 / Reveal
	FGinkgoAmmoTrail Trail;
	Trail.Lifetime = 1.2f;
	Trail.Speed = 760.f;
	const FGinkgoTrailShape Ten = Ginkgo::Combat::MakeTrailShape(Trail, 1000.0);
	TestEqual(TEXT("10 m：21 个点"), Ten.Points, 21);
	TestEqual(TEXT("10 m：寿命 1.2 s × 760 m/s / 10 m"), Ten.Reveal, 91.2f, 1e-3f);
	TestEqual(TEXT("10 m：长到头 13.2 ms"), Trail.Lifetime / Ten.Reveal, 1000.f / (760.f * 100.f), 1e-6f);
	const FGinkgoTrailShape Far = Ginkgo::Combat::MakeTrailShape(Trail, 10000.0);
	TestEqual(TEXT("100 m：点数封顶"), Far.Points, Ginkgo::Combat::TrailMaxPoints);
	TestEqual(TEXT("100 m：Reveal 9.12"), Far.Reveal, 9.12f, 1e-3f);
	TestEqual(TEXT("刚过最短：两个点"), Ginkgo::Combat::MakeTrailShape(Trail, 31.0).Points, 2);
	TestEqual(TEXT("短过 30 cm：不放"), Ginkgo::Combat::MakeTrailShape(Trail, 20.0).Points, 0);
	FGinkgoAmmoTrail Faint = Trail;
	Faint.Opacity = 0.f;
	TestEqual(TEXT("不透明度 0：不放"), Ginkgo::Combat::MakeTrailShape(Faint, 1000.0).Points, 0);
	FGinkgoAmmoTrail Grenade = Trail;
	Grenade.System.Reset();
	TestEqual(TEXT("没有系统（榴弹之类的）：不放"), Ginkgo::Combat::MakeTrailShape(Grenade, 1000.0).Points, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatFiringAmmoTest, "CaseGinkgo.Weapon.Mechanism.FiringAmmo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatFiringAmmoTest::RunTest(const FString& Parameters)
{
	// 打出去的是膛里那一发（Docs/Design/Damage.md「命中」）；开膛待击的 MG3 是弹链最上面那一发
	const FGameplayTag Ball = FWeaponTags::Ammo_NATO762x51_Ball;
	const FGameplayTag Standard = FWeaponTags::Ammo_M43762x39_Standard;
	UGinkgoMagazineMechanism* Closed = NewObject<UGinkgoMagazineMechanism>();
	Closed->Rules.Feed = EGinkgoFeed::DetachableMagazine;
	Closed->Rules.Capacity = 30;
	Closed->Rules.bSelfLoading = true;
	FGinkgoMechanismState State = Closed->MakeLoadedState(Standard);
	TestEqual(TEXT("闭膛：膛里那一发"), Closed->GetFiringAmmo(State), Standard);
	State.ChamberAmmo = Ball;
	TestEqual(TEXT("闭膛：按膛里那一发记的弹种，不按弹匣"), Closed->GetFiringAmmo(State), Ball);
	State.Chamber = EGinkgoChamber::Spent;
	TestFalse(TEXT("膛里是空壳：没有"), Closed->GetFiringAmmo(State).IsValid());

	UGinkgoMagazineMechanism* Open = NewObject<UGinkgoMagazineMechanism>();
	Open->Rules.Feed = EGinkgoFeed::Belt;
	Open->Rules.Capacity = 250;
	Open->Rules.bSelfLoading = true;
	Open->Rules.bOpenBolt = true;
	FGinkgoMechanismState Belt = Open->MakeLoadedState(Ball);
	TestFalse(TEXT("开膛待击：膛是空的"), Belt.Chamber == EGinkgoChamber::Live);
	TestEqual(TEXT("开膛待击：弹链最上面那一发"), Open->GetFiringAmmo(Belt), Ball);
	Belt.Feed.Last() = Standard;
	TestEqual(TEXT("开膛待击：开火时取的那一发（Feed.Last）"), Open->GetFiringAmmo(Belt), Standard);
	Belt.Feed.Reset();
	TestFalse(TEXT("弹链打空：没有"), Open->GetFiringAmmo(Belt).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatHitSoundTest, "CaseGinkgo.Combat.HitSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatHitSoundTest::RunTest(const FString& Parameters)
{
	// 一枪一声，几种同时成立时的先后（Docs/Design/Damage.md「命中的声音」）：爆头打死、打死、打穿护甲、爆头、打在护甲上、命中
	using Ginkgo::Combat::SelectHitSound;
	const auto Shot = [](const bool bKill, const bool bKillHead, const bool bArmorBroken, const bool bHead, const bool bArmor, const int32 Hits = 1)
	{
		FGinkgoHitConfirm Confirm;
		Confirm.Hits = Hits;
		Confirm.bKill = bKill;
		Confirm.bKillHead = bKillHead;
		Confirm.bArmorBroken = bArmorBroken;
		Confirm.bHead = bHead;
		Confirm.bArmor = bArmor;
		return Confirm;
	};
	TestEqual(TEXT("没打中目标：不放"), SelectHitSound(Shot(false, false, false, false, false, 0)), EGinkgoHitSound::None);
	TestEqual(TEXT("没护甲的身上：命中"), SelectHitSound(Shot(false, false, false, false, false)), EGinkgoHitSound::Normal);
	TestEqual(TEXT("背心挡住：打在护甲上"), SelectHitSound(Shot(false, false, false, false, true)), EGinkgoHitSound::Armor);
	TestEqual(TEXT("背心打穿"), SelectHitSound(Shot(false, false, true, false, true)), EGinkgoHitSound::ArmorBreak);
	TestEqual(TEXT("没头盔的头：爆头"), SelectHitSound(Shot(false, false, false, true, false)), EGinkgoHitSound::Head);
	TestEqual(TEXT("头盔挡住的头：还是爆头"), SelectHitSound(Shot(false, false, false, true, true)), EGinkgoHitSound::Head);
	TestEqual(TEXT("一枪打穿头盔：打穿盖过爆头"), SelectHitSound(Shot(false, false, true, true, true)), EGinkgoHitSound::ArmorBreak);
	TestEqual(TEXT("打死在身上"), SelectHitSound(Shot(true, false, false, false, false)), EGinkgoHitSound::Kill);
	TestEqual(TEXT("打死在头上"), SelectHitSound(Shot(true, true, false, true, false)), EGinkgoHitSound::HeadKill);
	TestEqual(TEXT("打穿护甲的同一枪打死：打死盖过打穿"), SelectHitSound(Shot(true, false, true, false, true)), EGinkgoHitSound::Kill);
	TestEqual(TEXT("打穿头盔的同一枪打死在头上"), SelectHitSound(Shot(true, true, true, true, true)), EGinkgoHitSound::HeadKill);
	TestEqual(TEXT("霰弹一颗擦到头、别的几颗打死在身上：打死"), SelectHitSound(Shot(true, false, false, true, false, 9)), EGinkgoHitSound::Kill);
	TestEqual(TEXT("霰弹有一颗在头上、没打死：爆头"), SelectHitSound(Shot(false, false, false, true, false, 9)), EGinkgoHitSound::Head);

	// 设置里六种都配了、资产都在，音量不是 0
	const FGinkgoHitSounds& Sounds = UGinkgoDeveloperSettings::Get()->HitSounds;
	TestNull(TEXT("None 没有声音"), Sounds.Get(EGinkgoHitSound::None));
	for (const EGinkgoHitSound Kind : {EGinkgoHitSound::Normal, EGinkgoHitSound::Armor, EGinkgoHitSound::ArmorBreak, EGinkgoHitSound::Head, EGinkgoHitSound::Kill, EGinkgoHitSound::HeadKill})
	{
		const FGinkgoHitSound* Item = Sounds.Get(Kind);
		const FString Name = Ginkgo::Combat::DescribeHitSound(Kind);
		if (!TestNotNull(*FString::Printf(TEXT("%s：配了"), *Name), Item))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s：资产在（%s）"), *Name, *Item->Sound.ToString()),
			!Item->Sound.IsNull() && FPackageName::DoesPackageExist(Item->Sound.ToSoftObjectPath().GetLongPackageName()));
		TestTrue(*FString::Printf(TEXT("%s：音量 %.2f"), *Name, Item->Volume), Item->Volume > 0.f);
	}
	TestTrue(TEXT("前一声淡掉的时长在一枪的间隔左右"), Sounds.FadePrevious >= 0.f && Sounds.FadePrevious < 0.5f);

	// 两条队列（你说的，11-08）：打穿护甲、打死、爆头打死走自己的队列，默认容量 3，别的命中碰不到
	using Ginkgo::Combat::IsPriorityHitSound;
	TestFalse(TEXT("命中不进队列"), IsPriorityHitSound(EGinkgoHitSound::Normal));
	TestFalse(TEXT("打在护甲上不进队列"), IsPriorityHitSound(EGinkgoHitSound::Armor));
	TestFalse(TEXT("爆头不进队列"), IsPriorityHitSound(EGinkgoHitSound::Head));
	TestFalse(TEXT("没有声音不进队列"), IsPriorityHitSound(EGinkgoHitSound::None));
	TestTrue(TEXT("打穿护甲进队列"), IsPriorityHitSound(EGinkgoHitSound::ArmorBreak));
	TestTrue(TEXT("打死进队列"), IsPriorityHitSound(EGinkgoHitSound::Kill));
	TestTrue(TEXT("爆头打死进队列"), IsPriorityHitSound(EGinkgoHitSound::HeadKill));
	TestEqual(TEXT("队列默认的容量"), FGinkgoHitSounds().PriorityCapacity, 3);
	TestTrue(TEXT("设置里的容量至少 1"), Sounds.PriorityCapacity >= 1);

	// 队列本身：先进先出，放完的先出队，满了最老的让位
	TArray<int32> Queue;
	TSet<int32> Ringing;
	TArray<int32> Evicted;
	const auto Push = [&Queue, &Ringing, &Evicted](const int32 Voice, const int32 Capacity = 3)
	{
		Ginkgo::Combat::PushPriorityHitSound(Queue, Voice, Capacity,
			[&Ringing](const int32 Other)
			{
				return Ringing.Contains(Other);
			},
			[&Ringing, &Evicted](const int32 Oldest)
			{
				Evicted.Add(Oldest);
				Ringing.Remove(Oldest);
			});
		Ringing.Add(Voice);
	};
	Push(1);
	Push(2);
	Push(3);
	TestTrue(TEXT("三声都留着"), Queue == TArray<int32>({1, 2, 3}) && Evicted.IsEmpty());
	Push(4);
	TestTrue(TEXT("第四声：最老的让位"), Queue == TArray<int32>({2, 3, 4}) && Evicted == TArray<int32>({1}));
	Ringing.Remove(3);
	Push(5);
	TestTrue(TEXT("中间那一声放完了：出队，不挤别的"), Queue == TArray<int32>({2, 4, 5}) && Evicted == TArray<int32>({1}));
	Ringing.Empty();
	Push(6);
	TestTrue(TEXT("都放完了：只剩新的这一声"), Queue == TArray<int32>({6}) && Evicted == TArray<int32>({1}));
	Push(7);
	Push(8, 1);
	TestTrue(TEXT("容量改小：多的都让位，老的先"), Queue == TArray<int32>({8}) && Evicted == TArray<int32>({1, 6, 7}));
	Push(9, 0);
	TestTrue(TEXT("容量小于 1 按 1"), Queue == TArray<int32>({9}) && Evicted == TArray<int32>({1, 6, 7, 8}));
	return true;
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCombatM79FlashImpactTest, "CaseGinkgo.Combat.M79FlashImpact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCombatM79FlashImpactTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	ADD_LATENT_AUTOMATION_COMMAND(GinkgoTraversalTests::FStartStandalonePIE());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FUntilCommand>([this]()
	{
		UWorld* World = GEditor->PlayWorld;
		if (!World || !World->HasBegunPlay())
		{
			return false;
		}

		const UGinkgoAmmoDefinition* FlashAmmo = UGinkgoAmmoDefinition::Find(FWeaponTags::Ammo_Grenade40x46_Flash);
		if (!TestNotNull(TEXT("读取 40mm 碰炸闪光弹数据资产"), FlashAmmo))
		{
			return true;
		}

		// 在高空搭一面同时阻挡视线与子弹通道的靶墙，验证 82 m/s 榴弹触碰即爆且致盲范围与手投闪光弹完全一致
		const FVector WallFront(0.0, 0.0, 22000.0);
		AActor* Wall = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(100.0, 450.0, 300.0));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		Box->SetCollisionResponseToChannel(Ginkgo::Combat::WeaponTraceChannel, ECR_Block);
		Box->SetCanEverAffectNavigation(false);
		Box->RegisterComponent();
		Box->SetWorldLocation(WallFront + FVector(100.0, 0.0, 0.0));

		const auto SpawnObserver = [&](const FVector& EyeTarget, const FVector& LookDir) -> AGinkgoAICharacter*
		{
			const FRotator Facing = LookDir.Rotation();
			AGinkgoAICharacter* Enemy = World->SpawnActor<AGinkgoAICharacter>(EyeTarget, Facing);
			if (AGinkgoAIController* AI = Enemy ? Cast<AGinkgoAIController>(Enemy->GetController()) : nullptr)
			{
				Enemy->AddActorWorldOffset(EyeTarget - Enemy->GetPawnViewLocation());
				AI->SetControlRotation(Facing);
			}
			return Enemy;
		};

		AGinkgoAICharacter* NearBehindWall = SpawnObserver(WallFront + FVector(1200.0, 0.0, 0.0), FVector::ForwardVector);
		AGinkgoAICharacter* FarVisibleFacing = SpawnObserver(WallFront + FVector(-4000.0, 0.0, 0.0), FVector::ForwardVector);
		AGinkgoAICharacter* FarBehindWall = SpawnObserver(WallFront + FVector(5000.0, 0.0, 0.0), -FVector::ForwardVector);
		AGinkgoAICharacter* FarFacingAway = SpawnObserver(WallFront + FVector(-4000.0, 2500.0, 0.0), FVector(0.0, 1.0, 0.0));
		if (!TestTrue(TEXT("四名观测 AI 已就绪"), NearBehindWall != nullptr && FarVisibleFacing != nullptr && FarBehindWall != nullptr && FarFacingAway != nullptr))
		{
			return true;
		}

		// 从墙前 100 cm 以 82 m/s（8200 cm/s）直射：仅需约 0.012 s（第 1 个 60 Hz 步内）即撞墙引爆，不等待 2 s 延时引信
		const FVector MuzzlePos = WallFront - FVector(100.0, 0.0, 0.0);
		AGinkgoGrenadeProjectile* Projectile = World->SpawnActor<AGinkgoGrenadeProjectile>(MuzzlePos, FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("生成 M79 碰炸闪光弹抛射物"), Projectile))
		{
			return true;
		}
		Projectile->InitializeProjectile(FVector(FlashAmmo->Trail.Speed * 100.f, 0.0, 0.0), FlashAmmo, 1.f, nullptr);
		Projectile->Tick(1.f / 30.f);

		TestTrue(TEXT("高速直射首步触碰墙面立即引爆（无延时反弹）"), Projectile->HasDetonated());
		UPointLightComponent* Light = Projectile->FindComponentByClass<UPointLightComponent>();
		TestTrue(TEXT("引爆瞬间强白闪光点光源点亮"), Light != nullptr && Light->IsVisible() && Light->Intensity > 100000.f);
		TestTrue(TEXT("15 m 近身球内隔墙背对敌人受闪遮眼"), NearBehindWall->bCoverEyes);
		TestTrue(TEXT("40 m 正对爆点且无遮挡敌人受闪遮眼"), FarVisibleFacing->bCoverEyes);
		TestFalse(TEXT("50 m 隔墙正对敌人被墙体挡住不受闪"), FarBehindWall->bCoverEyes);
		TestFalse(TEXT("远处背对爆点敌人不受闪"), FarFacingAway->bCoverEyes);
		return true;
	}, [this]() { AddError(TEXT("等待 M79FlashImpact 测试 PIE 世界超时")); return true; }, 60.f));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_EDITOR

#endif // WITH_DEV_AUTOMATION_TESTS

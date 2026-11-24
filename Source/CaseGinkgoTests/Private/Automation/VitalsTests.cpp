#include "UI/GinkgoVitals.h"

#include "AI/GinkgoAICharacter.h"
#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoPlayerCameraComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoVitalsTest, "CaseGinkgo.UI.Vitals.DamageArmorAndRevive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoVitalsTest::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("玩家角色默认持有实际伤害组件"), GetDefault<AGinkgoCharacter>()->GetDamageable());
	TestTrue(TEXT("玩家角色默认启用全身护甲"), GetDefault<AGinkgoCharacter>()->GetDamageable()->HasFullBodyArmor());
	TestFalse(TEXT("无 Pawn / 伤害组件时 HUD 隐藏"), FGinkgoVitals::Read(nullptr).bAvailable);
	auto* Damageable = NewObject<UGinkgoDamageableComponent>();
	Damageable->Revive();
	const FGinkgoVitals Full = FGinkgoVitals::Read(Damageable);
	TestTrue(TEXT("默认满生命可见，无护甲行"), Full.bAvailable && Full.Health == Full.MaxHealth && !Full.HasArmor());
	TestTrue(TEXT("不变的数据可用于抑制重复消息"), Full == FGinkgoVitals::Read(Damageable));
	FGinkgoArmorPiece Vest;
	Vest.Name = TEXT("Vest");
	Vest.Covers = {EGinkgoBodyPart::UpperChest};
	Vest.MaxDurability = 100.f;
	FGinkgoArmorPiece Helmet;
	Helmet.Name = TEXT("Helmet");
	Helmet.Covers = {EGinkgoBodyPart::Head};
	Helmet.MaxDurability = 50.f;
	FGinkgoArmorPiece Empty;
	Empty.MaxDurability = 0.f;
	Damageable->SetArmor({Vest, Helmet, Empty});
	FGinkgoVitals State = FGinkgoVitals::Read(Damageable);
	TestTrue(TEXT("穿甲后显示总量，忽略不提供保护的装备"), State.HasArmor() && State.MaxArmor == 150.f);
	auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
	Ammo->Damage = 20.f;
	Ammo->ArmorDamage = 40.f;
	Ammo->ArmorPenetration = 0.f;
	Ammo->BodyParts.UpperChest = 1.f;
	Damageable->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
	State = FGinkgoVitals::Read(Damageable);
	TestEqual(TEXT("真实结算后的总耐久传到 HUD"), State.Armor, 110.f);
	TestEqual(TEXT("分部位模式的头盔耐久保持独立"), Damageable->GetArmor()[1].Durability, 50.f);
	TestEqual(TEXT("完全挡住时生命不误减"), State.Health, Full.Health);
	Ammo->ArmorDamage = 200.f;
	Damageable->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
	State = FGinkgoVitals::Read(Damageable);
	TestTrue(TEXT("剩余耐久与已损坏装备上限均参与读数"), State.HasArmor() && State.Armor == 50.f && State.MaxArmor == 150.f);
	Damageable->SetArmor({Vest});
	Damageable->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
	State = FGinkgoVitals::Read(Damageable);
	TestFalse(TEXT("最后一件护甲耗尽后隐藏护甲行"), State.HasArmor());
	TestTrue(TEXT("破甲溢出后生命值来自真实结算"), State.Health > 0.f && State.Health < Full.Health);
	Ammo->Damage = 1000.f;
	Damageable->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
	State = FGinkgoVitals::Read(Damageable);
	TestTrue(TEXT("死亡仍显示零生命，不把死亡当作没有角色"), State.bAvailable && State.Health == 0.f);
	Damageable->Revive();
	State = FGinkgoVitals::Read(Damageable);
	TestTrue(TEXT("复活刷新满血满甲"), State.Health == Full.Health && State.HasArmor() && State.Armor == 100.f);
	Damageable->SetArmor({});
	TestFalse(TEXT("卸下护甲立即隐藏"), FGinkgoVitals::Read(Damageable).HasArmor());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFullBodyArmorTest, "CaseGinkgo.Combat.FullBodyArmor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFullBodyArmorTest::RunTest(const FString& Parameters)
{
	auto* Player = NewObject<UGinkgoDamageableComponent>();
	Player->SetFullBodyArmor(true);
	Player->Revive();
	auto* Enemy = NewObject<UGinkgoDamageableComponent>();
	Enemy->Revive();
	TestFalse(TEXT("敌人默认继续按命中部位防护"), Enemy->HasFullBodyArmor());
	FGinkgoArmorPiece First;
	First.Name = TEXT("First");
	First.MaxDurability = 10.f;
	First.Covers = {EGinkgoBodyPart::UpperChest};
	FGinkgoArmorPiece Second;
	Second.Name = TEXT("Second");
	Second.MaxDurability = 20.f;
	Player->SetArmor({First, Second});
	Enemy->SetArmor({First, Second});
	TestEqual(TEXT("全身模式不要求装备声明 Covers"), FGinkgoVitals::Read(Player).MaxArmor, 30.f);

	auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
	Ammo->Damage = 40.f;
	Ammo->ArmorDamage = 25.f;
	Ammo->ArmorPenetration = 0.f;
	Ammo->BodyParts.Head = 1.f;
	Ammo->BodyParts.Feet = 1.f;
	auto* Mesh = NewObject<USkeletalMeshComponent>();
	FHitResult Hit;
	Hit.Component = Mesh;
	Hit.BoneName = TEXT("head");
	const FGinkgoDamageEvent Head = Player->ApplyBulletHit(*Ammo, 1.f, Hit, FVector::ForwardVector, 0.f, nullptr);
	TestEqual(TEXT("实际识别到头部命中"), Head.Part, EGinkgoBodyPart::Head);
	TestTrue(TEXT("全身甲抵挡头部命中且跨装备不提前漏血"), Head.Outcome.bArmorHit && !Head.Outcome.bArmorBroken && Head.HealthLeft == 100.f);
	TestEqual(TEXT("跨越第一件扣到第二件的剩余总甲"), Head.ArmorLeft, 5.f);
	TestTrue(TEXT("各装备耐久与总池一致"), Player->GetArmor()[0].Durability == 0.f && Player->GetArmor()[1].Durability == 5.f);
	const FGinkgoDamageEvent EnemyHead = Enemy->ApplyBulletHit(*Ammo, 1.f, Hit, FVector::ForwardVector, 0.f, nullptr);
	TestTrue(TEXT("敌人没戴头盔，头部命中仍直接扣血"), !EnemyHead.Outcome.bArmorHit && EnemyHead.HealthLeft == 60.f && Enemy->GetArmor()[0].Durability == 10.f);

	Hit.BoneName = TEXT("foot_l");
	Ammo->ArmorDamage = 10.f;
	const FGinkgoDamageEvent Foot = Player->ApplyBulletHit(*Ammo, 1.f, Hit, FVector::ForwardVector, 0.f, nullptr);
	TestEqual(TEXT("实际识别到脚部命中"), Foot.Part, EGinkgoBodyPart::Feet);
	TestTrue(TEXT("脚部也受全身甲保护，仅溢出一半伤害"), Foot.Outcome.bArmorBroken && Foot.HealthLeft == 80.f && Foot.ArmorLeft == 0.f);
	TestFalse(TEXT("池耗尽时护甲环隐藏"), FGinkgoVitals::Read(Player).HasArmor());
	Player->Revive();
	Ammo->ArmorDamage = 20.f;
	Ammo->ArmorPenetration = 0.25f;
	const FGinkgoDamageEvent Pierced = Player->ApplyBulletHit(*Ammo, 1.f, Hit, FVector::ForwardVector, 0.f, nullptr);
	TestTrue(TEXT("全身模式仍遵守穿甲比例，只结算一次"), Pierced.HealthLeft == 90.f && Pierced.ArmorLeft == 15.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoHealthRegenAndVignetteTest, "CaseGinkgo.Combat.PlayerHealthRegenAndVignette",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoHealthRegenAndVignetteTest::RunTest(const FString& Parameters)
{
	// 1. 角色默认配置：玩家启用 3 s 等待 + 2 s 满血恢复，AI 禁用自动回血
	const UGinkgoDamageableComponent* PlayerDefault = GetDefault<AGinkgoCharacter>()->GetDamageable();
	const UGinkgoDamageableComponent* AiDefault = GetDefault<AGinkgoAICharacter>()->GetDamageable();
	if (!TestNotNull(TEXT("玩家角色持有默认伤害组件"), PlayerDefault) || !TestNotNull(TEXT("AI 角色持有默认伤害组件"), AiDefault))
	{
		return false;
	}
	TestTrue(TEXT("玩家默认开启脱战回血"), PlayerDefault->IsHealthRegenEnabled());
	TestEqual(TEXT("玩家默认脱战等待时间为 3.0 s"), PlayerDefault->GetHealthRegenDelay(), 3.f, 1e-3f);
	TestEqual(TEXT("玩家默认 0 到满血恢复时长为 2.0 s"), PlayerDefault->GetHealthRegenFullDuration(), 2.f, 1e-3f);
	TestFalse(TEXT("AI 默认关闭自动回血"), AiDefault->IsHealthRegenEnabled());

	// 2. 脱战回血时序：3 s 内不回血、受击（含纯护甲命中）重置计时、3 s 后按 MaxHealth / 2 s 线性回满且死亡绝不回血
	auto* Comp = NewObject<UGinkgoDamageableComponent>();
	Comp->SetHealthRegenEnabled(true);
	Comp->Revive();

	auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
	Ammo->Damage = 80.f;
	Ammo->ArmorDamage = 0.f;
	Ammo->ArmorPenetration = 0.f;
	Ammo->BodyParts.UpperChest = 1.f;

	Comp->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
	TestEqual(TEXT("受击扣至 20 血"), Comp->GetHealth(), 20.f, 1e-3f);

	Comp->StepHealthRegen(2.9f);
	TestEqual(TEXT("2.9 s 未满 3 s 等待期，血量不恢复"), Comp->GetHealth(), 20.f, 1e-3f);

	// 护甲挡下一次攻击也属于受击，应当重置 3 s 计时
	FGinkgoArmorPiece Vest;
	Vest.Name = TEXT("Vest");
	Vest.Covers = {EGinkgoBodyPart::UpperChest};
	Vest.MaxDurability = 100.f;
	Comp->SetArmor({Vest});
	Ammo->Damage = 10.f;
	Ammo->ArmorDamage = 10.f;
	Comp->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
	TestEqual(TEXT("护甲全挡未扣血"), Comp->GetHealth(), 20.f, 1e-3f);

	Comp->StepHealthRegen(2.5f);
	TestEqual(TEXT("重置计时后经过 2.5 s 仍在等待期"), Comp->GetHealth(), 20.f, 1e-3f);

	Comp->StepHealthRegen(0.5f);
	TestEqual(TEXT("刚好到达 3.0 s 临界点尚未多回血"), Comp->GetHealth(), 20.f, 1e-3f);

	Comp->StepHealthRegen(1.0f);
	TestEqual(TEXT("进入回血 1.0 s 后恢复 50% 生命上限（20 -> 70）"), Comp->GetHealth(), 70.f, 1e-3f);

	Comp->StepHealthRegen(0.6f);
	TestEqual(TEXT("再过 0.6 s 刚好回满 100 血（0->100 共 2.0 s）"), Comp->GetHealth(), 100.f, 1e-3f);

	Comp->StepHealthRegen(2.0f);
	TestEqual(TEXT("满血后继续推进不溢出上限"), Comp->GetHealth(), 100.f, 1e-3f);

	// 跨帧越过 3 s 临界时仅按超出 3 s 的余量回血
	Comp->SetArmor({});
	Ammo->Damage = 99.f;
	Comp->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
	TestEqual(TEXT("残血至 1 HP"), Comp->GetHealth(), 1.f, 1e-3f);
	Comp->StepHealthRegen(3.5f);
	TestEqual(TEXT("单步 3.5 s 仅对超出的 0.5 s 回血 25 点（1 -> 26）"), Comp->GetHealth(), 26.f, 1e-3f);
	Comp->StepHealthRegen(1.48f);
	TestEqual(TEXT("从 1 HP 累计回血 1.98 s 即回满"), Comp->GetHealth(), 100.f, 1e-3f);

	// 死亡后不可回血
	Ammo->Damage = 200.f;
	Comp->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
	TestTrue(TEXT("致命伤害后进入死亡态"), Comp->IsDead() && Comp->GetHealth() == 0.f);
	Comp->StepHealthRegen(10.f);
	TestTrue(TEXT("死亡后经过 10 s 仍保持死亡零血"), Comp->IsDead() && Comp->GetHealth() == 0.f);

	// 3. 血量暗角后处理曲线：满血回归基准黑暗角，随缺血单调增强并在低血量平滑混入深暗赤黑压色
	const FGinkgoHealthVignetteSettings Settings;
	float Intensity0 = 0.f, Intensity25 = 0.f, Intensity50 = 0.f, Intensity75 = 0.f, Intensity100 = 0.f;
	FLinearColor Color0, Color25, Color50, Color75, Color100;
	Ginkgo::Camera::ComputeHealthVignette(0.00f, Settings, Intensity0, Color0);
	Ginkgo::Camera::ComputeHealthVignette(0.25f, Settings, Intensity25, Color25);
	Ginkgo::Camera::ComputeHealthVignette(0.50f, Settings, Intensity50, Color50);
	Ginkgo::Camera::ComputeHealthVignette(0.75f, Settings, Intensity75, Color75);
	Ginkgo::Camera::ComputeHealthVignette(1.00f, Settings, Intensity100, Color100);

	TestEqual(TEXT("满血暗角等于基准值"), Intensity0, Settings.BaseIntensity, 1e-4f);
	TestEqual(TEXT("满血暗角压色为纯黑"), Color0.R, 0.f, 1e-4f);
	TestTrue(TEXT("暗角强度随缺血比例严格单调递增"),
		Intensity0 < Intensity25 && Intensity25 < Intensity50 && Intensity50 < Intensity75 && Intensity75 < Intensity100);
	TestEqual(TEXT("空血暗角达到最大设定值"), Intensity100, Settings.MaxIntensity, 1e-4f);
	TestTrue(TEXT("轻伤（75% 血）保持纯黑暗角，中低血量逐步混入深暗赤黑压色"),
		Color25.R <= 1e-4f && Color50.R > 0.f && Color75.R > Color50.R && FMath::IsNearlyEqual(Color100.R, Settings.LowHealthColor.R, 1e-4f));
	return true;
}

#endif

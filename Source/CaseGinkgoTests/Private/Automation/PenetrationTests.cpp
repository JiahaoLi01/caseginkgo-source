#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Combat/GinkgoPenetrableComponent.h"
#include "Engine/HitResult.h"
#include "Misc/AutomationTest.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"

#if WITH_DEV_AUTOMATION_TESTS

// 掩体穿射（Docs/Design/Damage.md「穿射」）：剩的系数、一颗子弹的路、8 个的上限、穿过以后的伤害、子弹的数据

namespace GinkgoPenetrationTests
{
	constexpr float Tolerance = 1e-4f;

	/** 一排假的东西：第 N 个的厚度是 Thicknesses[N]（负的穿不过），Item 记是第几个；后面没东西了打不到 */
	struct FFakeLine
	{
		TArray<float> Thicknesses;
		/** 接着打了几次线 */
		int32 Traces = 0;

		void Trace(const float Coefficient, FGinkgoPenetrationPath& OutPath)
		{
			FHitResult First;
			First.Item = 0;
			Ginkgo::Combat::TracePenetration(First, Coefficient, [this](const FHitResult& Hit)
			{
				return Thicknesses[Hit.Item];
			}, [this](const FHitResult& Through, FHitResult& OutNext)
			{
				++Traces;
				if (!Thicknesses.IsValidIndex(Through.Item + 1))
				{
					return false;
				}
				OutNext = FHitResult();
				OutNext.Item = Through.Item + 1;
				return true;
			}, OutPath);
		}
	};

	/** 路上每个东西挨的倍数 */
	TArray<float> Scales(const FGinkgoPenetrationPath& Path)
	{
		TArray<float> Result;
		for (const FGinkgoPenetrationHit& Entry : Path)
		{
			Result.Add(Entry.DamageScale);
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPenetrationLeftTest, "CaseGinkgo.Combat.PenetrationLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPenetrationLeftTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Combat::GetPenetrationLeft;
	using GinkgoPenetrationTests::Tolerance;
	TestEqual(TEXT("你说的：0.7 穿过厚 0.5 的剩 0.2"), GetPenetrationLeft(0.7f, 0.5f), 0.2f, Tolerance);
	TestEqual(TEXT("S.m.K. 1.0 穿过一个人（0.3）剩 0.7"), GetPenetrationLeft(1.f, 0.3f), 0.7f, Tolerance);
	TestEqual(TEXT("不够：0.2 碰到厚 0.3 的停在里面"), GetPenetrationLeft(0.2f, 0.3f), 0.f);
	TestEqual(TEXT("刚好用完也停：0.5 碰到厚 0.5 的"), GetPenetrationLeft(0.5f, 0.5f), 0.f);
	// AK 的 0.6 穿过一个人剩 0.3（浮点算出来的），再碰到一个人：刚好用完，停在里面，不按舍入一会儿穿过去
	TestEqual(TEXT("刚好用完也停：0.6 − 0.3 再碰 0.3"), GetPenetrationLeft(GetPenetrationLeft(0.6f, 0.3f), 0.3f), 0.f);
	TestEqual(TEXT("差得比 UE_KINDA_SMALL_NUMBER 少也停"), GetPenetrationLeft(0.3f + UE_KINDA_SMALL_NUMBER * 0.5f, 0.3f), 0.f);
	TestTrue(TEXT("多出 UE_KINDA_SMALL_NUMBER 以上就穿过去"), GetPenetrationLeft(0.3f + UE_KINDA_SMALL_NUMBER * 4.f, 0.3f) > 0.f);
	TestEqual(TEXT("穿不过的东西（没有组件，厚度为负）：停"), GetPenetrationLeft(0.7f, -1.f), 0.f);
	TestEqual(TEXT("系数 0 什么都穿不过，厚 0 的也不行"), GetPenetrationLeft(0.f, 0.f), 0.f);
	TestEqual(TEXT("厚 0 的东西：不减"), GetPenetrationLeft(0.7f, 0.f), 0.7f, Tolerance);
	TestEqual(TEXT("系数大过 1：照减"), GetPenetrationLeft(1.5f, 0.3f), 1.2f, Tolerance);
	TestEqual(TEXT("人的厚度是 0.3（你定的）"), UGinkgoPenetrableComponent::CharacterThickness, 0.3f);
	TestEqual(TEXT("组件默认就是人的厚度"), GetDefault<UGinkgoPenetrableComponent>()->Thickness, UGinkgoPenetrableComponent::CharacterThickness);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPenetrationPathTest, "CaseGinkgo.Combat.PenetrationPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPenetrationPathTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoPenetrationTests;
	constexpr float Person = UGinkgoPenetrableComponent::CharacterThickness;
	FGinkgoPenetrationPath Path;

	// 你说的例子：0.7 穿过厚 0.5 的墙，后面的人只挨 0.2 倍；0.2 不够穿过人（0.3），停在他里面
	{
		FFakeLine Line{{0.5f, Person, Person}};
		Line.Trace(0.7f, Path);
		TestEqual(TEXT("墙后一个人：碰到两个"), Path.Num(), 2);
		if (Path.Num() == 2)
		{
			TestEqual(TEXT("墙：第一个挨满的"), Path[0].DamageScale, 1.f);
			TestTrue(TEXT("墙：穿过去"), Path[0].bPassed);
			TestEqual(TEXT("人：到他时剩 0.2"), Path[1].Remaining, 0.2f, Tolerance);
			TestEqual(TEXT("人：只挨 0.2 倍"), Path[1].DamageScale, 0.2f, Tolerance);
			TestFalse(TEXT("人：0.2 穿不过 0.3，停在他里面"), Path[1].bPassed);
		}
	}

	// 一列人，S.m.K.（1.0）：1、0.7、0.4、0.1，第四个停在里面
	{
		FFakeLine Line{{Person, Person, Person, Person, Person}};
		Line.Trace(1.f, Path);
		const TArray<float> Got = Scales(Path);
		const TArray<float> Want = {1.f, 0.7f, 0.4f, 0.1f};
		TestEqual(TEXT("S.m.K. 一列人：打到四个"), Got.Num(), Want.Num());
		for (int32 Index = 0; Index < FMath::Min(Got.Num(), Want.Num()); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("S.m.K. 第 %d 个人挨的倍数"), Index + 1), Got[Index], Want[Index], Tolerance);
		}
		TestTrue(TEXT("S.m.K.：停在第四个里面"), !Path.IsEmpty() && !Path.Last().bPassed);
	}

	// AK（0.6）：穿过第一个人剩 0.3，第二个人挨 0.3 倍，刚好用完停在他里面
	{
		FFakeLine Line{{Person, Person, Person}};
		Line.Trace(0.6f, Path);
		TestEqual(TEXT("AK 一列人：打到两个"), Path.Num(), 2);
		if (Path.Num() == 2)
		{
			TestEqual(TEXT("AK 第二个人：0.3 倍"), Path[1].DamageScale, 0.3f, Tolerance);
			TestFalse(TEXT("AK 第二个人：刚好用完，停在里面"), Path[1].bPassed);
		}
	}

	// 穿不过的东西挡在前面：只有它，停
	{
		FFakeLine Line{{-1.f, Person}};
		Line.Trace(1.f, Path);
		TestTrue(TEXT("穿不过的墙：只碰到它，停，挨满的"), Path.Num() == 1 && !Path[0].bPassed && Path[0].DamageScale == 1.f);
		TestEqual(TEXT("穿不过的墙：不再往后打线"), Line.Traces, 0);
	}

	// 系数 0（手枪弹、霰弹）：打到的第一个人挨满的，停在他里面，同以前一样
	{
		FFakeLine Line{{Person, Person}};
		Line.Trace(0.f, Path);
		TestTrue(TEXT("系数 0：只打到第一个，挨满的，停"), Path.Num() == 1 && !Path[0].bPassed && Path[0].DamageScale == 1.f);
	}

	// 厚墙（混凝土 1.0）挡住 S.m.K.：刚好用完，停在墙里
	{
		FFakeLine Line{{1.f, Person}};
		Line.Trace(1.f, Path);
		TestTrue(TEXT("混凝土挡住 S.m.K."), Path.Num() == 1 && !Path[0].bPassed);
	}

	// 穿过最后一个，后面没东西了：最后一个是穿过去的
	{
		FFakeLine Line{{Person}};
		Line.Trace(0.75f, Path);
		TestTrue(TEXT("后面没东西：一个，穿过去了"), Path.Num() == 1 && Path[0].bPassed);
		TestEqual(TEXT("后面没东西：往后打了一次线"), Line.Traces, 1);
	}

	// 系数大过 1：只多穿，伤害不超过原来
	{
		FFakeLine Line{{Person, Person, Person, Person, Person, Person}};
		Line.Trace(1.5f, Path);
		const TArray<float> Got = Scales(Path);
		const TArray<float> Want = {1.f, 1.f, 0.9f, 0.6f, 0.3f};
		TestEqual(TEXT("系数 1.5：打到五个"), Got.Num(), Want.Num());
		for (int32 Index = 0; Index < FMath::Min(Got.Num(), Want.Num()); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("系数 1.5 第 %d 个挨的倍数（不过 1）"), Index + 1), Got[Index], Want[Index], Tolerance);
		}
	}

	// 8 个的上限（你定的）：十二层薄板（0.01），系数 1 本来都穿得过；碰到第 8 个就停在它里面，数组是定长的
	{
		FFakeLine Line;
		Line.Thicknesses.Init(0.01f, 12);
		Line.Trace(1.f, Path);
		TestEqual(TEXT("上限：只碰 8 个"), Path.Num(), Ginkgo::Combat::MaxPenetrationHits);
		TestEqual(TEXT("上限是 8"), Ginkgo::Combat::MaxPenetrationHits, 8);
		TestEqual(TEXT("上限：往后打了 7 次线"), Line.Traces, 7);
		bool bFirstSevenPassed = true;
		for (int32 Index = 0; Index + 1 < Path.Num(); ++Index)
		{
			bFirstSevenPassed &= Path[Index].bPassed;
		}
		TestTrue(TEXT("上限：前 7 个穿过去"), bFirstSevenPassed);
		TestTrue(TEXT("上限：第 8 个系数还够，也停在里面"), !Path.IsEmpty() && !Path.Last().bPassed
			&& Ginkgo::Combat::GetPenetrationLeft(Path.Last().Remaining, Path.Last().Thickness) > 0.f);
		TestEqual(TEXT("上限：第 8 个挨 0.93 倍"), Path.IsEmpty() ? 0.f : Path.Last().DamageScale, 0.93f, Tolerance);
		TestEqual(TEXT("数组是定长的：容量就是 8"), Path.Max(), Ginkgo::Combat::MaxPenetrationHits);
	}

	// 同一个数组再用：先清空，不接着上一颗的
	{
		FFakeLine Line{{Person}};
		Line.Trace(0.f, Path);
		TestEqual(TEXT("再用：先清空"), Path.Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPenetrationDamageTest, "CaseGinkgo.Combat.PenetrationDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPenetrationDamageTest::RunTest(const FString& Parameters)
{
	using GinkgoPenetrationTests::Tolerance;
	// AK 的 M43（Damage.md「子弹的数」：34 / 34 / 0.15，上胸 1.1）：穿过一个人以后剩 0.3，伤害、护甲伤害都乘 0.3
	UGinkgoAmmoDefinition* Ak = NewObject<UGinkgoAmmoDefinition>();
	Ak->Ammo = FWeaponTags::Ammo_M43762x39_Standard;
	Ak->Damage = 34.f;
	Ak->ArmorDamage = 34.f;
	Ak->ArmorPenetration = 0.15f;
	Ak->BodyParts.UpperChest = 1.1f;
	const FHitResult Hit;

	UGinkgoDamageableComponent* Bare = NewObject<UGinkgoDamageableComponent>();
	Bare->Revive();
	const FGinkgoDamageEvent Through = Bare->ApplyBulletHit(*Ak, 1.f, Hit, FVector::ForwardVector, 1000.f, nullptr, 0.3f, 1);
	TestEqual(TEXT("穿过一个人：伤害 37.4 × 0.3"), Through.BaseDamage, 37.4f * 0.3f, 1e-3f);
	TestEqual(TEXT("穿过一个人：没护甲全扣血"), Through.Outcome.HealthDamage, 37.4f * 0.3f, 1e-3f);
	TestTrue(TEXT("事件里记着穿过几个、乘多少"), Through.Penetrated == 1 && FMath::IsNearlyEqual(Through.PenetrationScale, 0.3f, Tolerance));

	FGinkgoArmorPiece Vest;
	Vest.Name = TEXT("Vest");
	Vest.Covers = {EGinkgoBodyPart::UpperChest, EGinkgoBodyPart::Abdomen};
	Vest.MaxDurability = 100.f;
	UGinkgoDamageableComponent* Vested = NewObject<UGinkgoDamageableComponent>();
	Vested->SetArmor({Vest});
	Vested->Revive();
	const FGinkgoDamageEvent Armored = Vested->ApplyBulletHit(*Ak, 1.f, Hit, FVector::ForwardVector, 1000.f, nullptr, 0.3f, 1);
	TestEqual(TEXT("穿过一个人打在背心上：穿甲的一份扣血"), Armored.Outcome.HealthDamage, 37.4f * 0.3f * 0.15f, 1e-3f);
	TestEqual(TEXT("穿过一个人打在背心上：护甲伤害也乘 0.3"), Armored.Outcome.ArmorDamage, 37.4f * 0.3f * 0.85f, 1e-3f);

	// 没穿过东西：同以前一样（默认 1 倍）
	UGinkgoDamageableComponent* Direct = NewObject<UGinkgoDamageableComponent>();
	Direct->Revive();
	const FGinkgoDamageEvent Straight = Direct->ApplyBulletHit(*Ak, 1.f, Hit, FVector::ForwardVector, 1000.f, nullptr);
	TestTrue(TEXT("没穿过东西：1 倍，0 个"), FMath::IsNearlyEqual(Straight.BaseDamage, 37.4f, 1e-3f) && Straight.Penetrated == 0 && Straight.PenetrationScale == 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPenetrationAmmoTest, "CaseGinkgo.Combat.PenetrationAmmo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPenetrationAmmoTest::RunTest(const FString& Parameters)
{
	// 你定的（2026-11-11）：只有步枪弹能穿，手枪弹、鹿弹、独头弹穿不过。数可以手调，这里只查谁能穿
	for (const FGameplayTag& Tag : {FWeaponTags::Ammo_Mauser792x57_Standard.GetTag(), FWeaponTags::Ammo_Mauser792x57_SmK.GetTag(), FWeaponTags::Ammo_NATO762x51_Ball.GetTag(),
		FWeaponTags::Ammo_M43762x39_Standard.GetTag()})
	{
		const UGinkgoAmmoDefinition* Ammo = UGinkgoAmmoDefinition::Find(Tag);
		TestTrue(*FString::Printf(TEXT("%s：步枪弹能穿（%.2f）"), *Tag.ToString(), Ammo ? Ammo->CoverPenetration : -1.f), Ammo && Ammo->CoverPenetration > 0.f);
	}
	for (const FGameplayTag& Tag : {FWeaponTags::Ammo_ACP45_Standard.GetTag(), FWeaponTags::Ammo_Gauge12_Buckshot.GetTag(), FWeaponTags::Ammo_Gauge12_Slug.GetTag()})
	{
		const UGinkgoAmmoDefinition* Ammo = UGinkgoAmmoDefinition::Find(Tag);
		TestTrue(*FString::Printf(TEXT("%s：穿不过（%.2f）"), *Tag.ToString(), Ammo ? Ammo->CoverPenetration : -1.f), Ammo && Ammo->CoverPenetration == 0.f);
	}
	// S.m.K. 比 s.S. 穿得多（钢芯）
	const UGinkgoAmmoDefinition* Standard = UGinkgoAmmoDefinition::Find(FWeaponTags::Ammo_Mauser792x57_Standard);
	const UGinkgoAmmoDefinition* SmK = UGinkgoAmmoDefinition::Find(FWeaponTags::Ammo_Mauser792x57_SmK);
	TestTrue(TEXT("S.m.K. 比 s.S. 穿得多"), Standard && SmK && SmK->CoverPenetration > Standard->CoverPenetration);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

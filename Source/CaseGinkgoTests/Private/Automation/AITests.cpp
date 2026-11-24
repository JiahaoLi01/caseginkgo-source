#include "AI/GinkgoAIKnowledge.h"

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAICharacterMovementComponent.h"
#include "AI/GinkgoAIConfig.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "Misc/AutomationTest.h"
#include "StateTree.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAIMemoryTest, "CaseGinkgo.AI.Memory.VisibilityAndSearch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAIMemoryTest::RunTest(const FString& Parameters)
{
	FGinkgoAIKnowledge Memory;
	TestEqual(TEXT("无记忆时巡逻"), Memory.GetBehavior(), EGinkgoAIBehavior::Patrol);
	Memory.Observe(FVector::ZeroVector, FVector(0.0, 0.0, 120.0), 10.0);
	TestEqual(TEXT("世界原点也是有效目击位置"), Memory.GetBehavior(), EGinkgoAIBehavior::Engage);
	Memory.Observe(FVector(200.0, 20.0, 0.0), FVector(200.0, 20.0, 120.0), 11.0);
	TestEqual(TEXT("持续看见不重复延迟首次开火"), Memory.AcquiredTime, 10.0);
	Memory.LoseSight();
	TestEqual(TEXT("丢失后推进"), Memory.GetBehavior(), EGinkgoAIBehavior::Pursue);
	TestEqual(TEXT("丢失视野冻结位置"), Memory.LastLocation, FVector(200.0, 20.0, 0.0));
	TestEqual(TEXT("丢失不刷新目击时间"), Memory.LastSeenTime, 11.0);
	Memory.ConfirmEmpty(true, 0.3f, 0.5f);
	Memory.ConfirmEmpty(false, 1.f, 0.5f);
	Memory.ConfirmEmpty(true, 0.3f, 0.5f);
	TestEqual(TEXT("遮挡中断空置确认，不能累加零碎视线"), Memory.GetBehavior(), EGinkgoAIBehavior::Pursue);
	Memory.ConfirmEmpty(true, 0.21f, 0.5f);
	TestEqual(TEXT("连续看清空置位置进入搜索"), Memory.GetBehavior(), EGinkgoAIBehavior::Search);
	Memory.Observe(FVector(300.0, 30.0, 0.0), FVector(300.0, 30.0, 120.0), 15.0);
	TestEqual(TEXT("搜索中再次看到立即交战"), Memory.GetBehavior(), EGinkgoAIBehavior::Engage);
	TestEqual(TEXT("重新目击产生新的反应窗口"), Memory.AcquiredTime, 15.0);
	TestFalse(TEXT("重新目击清除空置确认"), Memory.bPositionCleared);
	Memory.ConfirmEmpty(true, 100.f, 0.5f);
	TestFalse(TEXT("可见目标不因空置计时进入搜索"), Memory.bPositionCleared);
	TestFalse(TEXT("可见目标不因记忆寿命遗忘"), Memory.IsExpired(100.0, 30.f));
	Memory.LoseSight();
	TestFalse(TEXT("寿命之前保持记忆"), Memory.IsExpired(44.9, 30.f));
	TestTrue(TEXT("不可达目标也会超时遗忘"), Memory.IsExpired(45.0, 30.f));
	Memory.Forget();
	TestEqual(TEXT("遗忘回巡逻"), Memory.GetBehavior(), EGinkgoAIBehavior::Patrol);
	TestFalse(TEXT("遗忘后没有陈旧空置事实"), Memory.bPositionCleared);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAIIntegrationDefaultsTest, "CaseGinkgo.AI.Integration.DefaultsAndAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAIIntegrationDefaultsTest::RunTest(const FString& Parameters)
{
	const AGinkgoAICharacter* Enemy = GetDefault<AGinkgoAICharacter>();
	const auto* Movement = Cast<UGinkgoAICharacterMovementComponent>(Enemy->GetCharacterMovement());
	TestNotNull(TEXT("敌人确实替换为导航适配移动组件"), Movement);
	if (Movement) TestTrue(TEXT("路径通过加速度驱动项目移动"), Movement->GetNavMovementProperties().bUseAccelerationForPaths);
	TestFalse(TEXT("敌人保留分部位护甲"), Enemy->GetDamageable()->HasFullBodyArmor());
	TestTrue(TEXT("玩家仍使用原移动组件"), GetDefault<AGinkgoCharacter>()->GetCharacterMovement()->GetClass() == UGinkgoCharacterMovementComponent::StaticClass());
	const UGinkgoAIConfig* Config = Enemy->Definition.LoadSynchronous();
	TestNotNull(TEXT("默认行为定义已落盘"), Config);
	if (!Config) return false;
	TestTrue(TEXT("默认决策树可运行"), Config->StateTree && Config->StateTree->IsReadyToRun());
	TestTrue(TEXT("默认掩体查询包含实际选点规则"), Config->CoverQuery && !Config->CoverQuery->GetOptions().IsEmpty());
	TestNotNull(TEXT("敌人初始武器可加载"), Config->Weapon.LoadSynchronous());
	const TCHAR* Variants[] = { TEXT("/Game/AI/DA_Enemy_Pistol"), TEXT("/Game/AI/DA_Enemy_Shotgun") };
	for (const TCHAR* Path : Variants)
	{
		const UGinkgoAIConfig* Variant = LoadObject<UGinkgoAIConfig>(nullptr, Path);
		if (!TestNotNull(TEXT("新增敌人定义已经落盘"), Variant)) continue;
		const UGinkgoWeaponDefinition* Weapon = Variant->Weapon.LoadSynchronous();
		TestTrue(TEXT("变体开局武器携带独立且可运行的 AI 默认树"), Weapon && Weapon->DefaultAIStateTree
			&& Weapon->DefaultAIStateTree->IsReadyToRun() && Weapon->DefaultAIStateTree != Config->StateTree);
	}
	UGinkgoWeaponComponent* Weapons = NewObject<UGinkgoWeaponComponent>();
	Weapons->SetTriggerHeld(true);
	Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Fire);
	Weapons->CancelFireIntent();
	TestFalse(TEXT("撤销射击授权不遗留排队的第一枪"), Weapons->WantsFire());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAIHearingMemoryTest, "CaseGinkgo.AI.Memory.HearingAndSightPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAIHearingMemoryTest::RunTest(const FString& Parameters)
{
	FGinkgoAIKnowledge Memory;
	TestTrue(TEXT("世界原点可以是声音发生处"), Memory.Hear(FVector::ZeroVector, 1.0));
	TestEqual(TEXT("只听到声音先调查"), Memory.GetBehavior(), EGinkgoAIBehavior::Investigate);
	TestFalse(TEXT("声音不生成目击事实"), Memory.bHasMemory || Memory.bVisible);
	Memory.ConfirmEmpty(true, 10.f, 0.5f);
	TestEqual(TEXT("看见声源位置也必须走近调查"), Memory.GetBehavior(), EGinkgoAIBehavior::Investigate);
	Memory.FinishInvestigation();
	TestEqual(TEXT("抵达后进入搜索"), Memory.GetBehavior(), EGinkgoAIBehavior::Search);
	TestFalse(TEXT("同一或过时刺激不能反复重启调查"), Memory.Hear(FVector(999.0), 1.0));
	TestTrue(TEXT("新的声音打断搜索"), Memory.Hear(FVector(200.0, 0.0, 140.0), 2.0));
	TestEqual(TEXT("新声音重新调查"), Memory.GetBehavior(), EGinkgoAIBehavior::Investigate);
	Memory.ForgetSight();
	TestTrue(TEXT("没有目击目标引用不能清掉听觉"), Memory.bHasHearing);
	TestFalse(TEXT("听觉寿命之前保持"), Memory.IsHearingExpired(31.9, 30.f));
	TestTrue(TEXT("听觉也有独立超时"), Memory.IsHearingExpired(32.0, 30.f));
	Memory.Observe(FVector(400.0, 0.0, 0.0), FVector(400.0, 0.0, 120.0), 3.0);
	TestFalse(TEXT("目击后丢弃旧声音"), Memory.bHasHearing);
	TestFalse(TEXT("当前可见时声音不能抢走交战"), Memory.Hear(FVector::ZeroVector, 4.0));
	Memory.LoseSight();
	TestFalse(TEXT("旧声音不覆盖更近的目击"), Memory.Hear(FVector::ZeroVector, 2.5));
	TestTrue(TEXT("丢失视野后可以听到新位置"), Memory.Hear(FVector(700.0, 0.0, 0.0), 5.0));
	TestEqual(TEXT("听声不刷新压制时间"), Memory.LastSeenTime, 3.0);
	TestEqual(TEXT("听声不篡改目击点"), Memory.LastLocation, FVector(400.0, 0.0, 0.0));
	TestEqual(TEXT("搜索中心使用新的声音位置"), Memory.GetSearchLocation(), FVector(700.0, 0.0, 0.0));
	Memory.ForgetHearing();
	TestEqual(TEXT("听觉失效仍保留独立的目击记忆"), Memory.GetBehavior(), EGinkgoAIBehavior::Pursue);
	Memory.Forget();
	TestFalse(TEXT("停树或搜索结束同时清理两种事实"), Memory.bHasMemory || Memory.bHasHearing);
	return true;
}

#endif

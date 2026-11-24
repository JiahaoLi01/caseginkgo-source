#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/BoxComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "StateTree.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"

namespace
{
	/** 真正通过感知、导航和武器机构验证射击边界，临时遮挡只改变视线，不阻断测试路线。 */
	class FSprintToRangeScenario : public IAutomationLatentCommand
	{
	public:
		explicit FSprintToRangeScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 75.)
			{
				Test.AddError(FString::Printf(TEXT("冲刺树 PIE 超时，阶段 %d"), Stage));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			if (!World || !World->HasBegunPlay() || World->GetTimeSeconds() < .5f) { return false; }
			TActorIterator<AGinkgoAICharacter> Enemies(World);
			auto* NPC = Enemies ? *Enemies : nullptr;
			auto* AI = NPC ? Cast<AGinkgoAIController>(NPC->GetController()) : nullptr;
			auto* PC = World->GetFirstPlayerController();
			auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!AI || !Player) { return false; }
			auto* Weapons = NPC->GetWeapons();
			const auto* Rifle = Weapons->GetMechanismState(0);
			if (!Rifle) { return false; }
			auto* Movement = NPC->GetGinkgoMovement();
			auto& Reference = NPC->BehaviorOverrides.Reference;
			const FVector Target(1100., -1200., 96.);
			const double Distance = FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation());
			auto ResetPositions = [&]()
			{
				AI->StopMovement();
				Movement->StopMovementImmediately();
				NPC->SetActorLocationAndRotation(FVector(1100., 1200., 96.), FRotator(0., -90., 0.));
				AI->SetControlRotation(FRotator(0., -90., 0.));
				Player->SetActorLocation(Target);
			};
			switch (Stage)
			{
			case 0:
				Reference.SetStateTree(LoadObject<UStateTree>(nullptr, TEXT("/Game/AI/ST_Enemy_SprintToRange")));
				if (!Test.TestNotNull(TEXT("冲刺状态树已编译"), Reference.GetStateTree())) { return true; }
				NPC->PatrolRoute = nullptr;
				Player->GetGinkgoMovement()->DisableMovement();
				Player->GetDamageable()->RestoreSaveState(5000.f, 5000.f, {});
				ResetPositions();
				Rounds = Rifle->CountLiveRounds();
				++Stage;
				break;
			case 1:
				if (!AI->GetKnowledge().bVisible || !Movement->IsSprintFacing() || NPC->GetVelocity().Size2D() < 400.) { break; }
				Test.TestTrue(TEXT("视野内远目标触发真实冲刺且未开火"), Distance > 1500. && Rifle->CountLiveRounds() == Rounds);
				{
					auto* Obstacle = World->SpawnActor<AActor>();
					auto* Box = NewObject<UBoxComponent>(Obstacle);
					Obstacle->SetRootComponent(Box);
					Box->SetCanEverAffectNavigation(false);
					Box->SetBoxExtent(FVector(750., 10., 240.));
					Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
					Box->SetCollisionResponseToAllChannels(ECR_Ignore);
					Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
					Box->RegisterComponent();
					Box->SetWorldLocation(Target + FVector(0., 200., 0.));
					Wall = Box;
				}
				++Stage;
				break;
			case 2:
				if (AI->GetKnowledge().bVisible || Distance > 1450.) { break; }
				Test.TestTrue(TEXT("进入默认距离但隔着墙时继续冲刺且没有射击"), Movement->WantsToSprint() && Rifle->CountLiveRounds() == Rounds);
				Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				ResetPositions();
				++Stage;
				break;
			case 3:
				if (Rifle->CountLiveRounds() >= Rounds) { break; }
				Test.TestTrue(TEXT("默认 15 米处停下并实际射击"), AI->GetKnowledge().bVisible && !Movement->WantsToSprint() && Distance > 1300. && Distance <= 1500.);
				{
					const FName Range(TEXT("SprintFireRangeMeters"));
					const auto* Parameter = Reference.GetParameters().FindPropertyDescByName(Range);
					if (!Test.TestNotNull(TEXT("启动树前可覆盖的射击距离"), Parameter)) { return true; }
					Reference.SetPropertyOverridden(Parameter->ID, true);
					Reference.GetMutableParameters().SetValueFloat(Range, 8.f);
					// 原生引用在 Start 时传入参数，重新接管同一名敌人以实际运行带覆盖的实例。
					AI->UnPossess();
					AI->Possess(NPC);
				}
				Rounds = Rifle->CountLiveRounds();
				++Stage;
				break;
			case 4:
				if (Distance < 850. || !Movement->IsSprintFacing()) { break; }
				Test.TestEqual(TEXT("个体距离改短后重新冲刺时停火"), Rifle->CountLiveRounds(), Rounds);
				++Stage;
				break;
			case 5:
				if (Rifle->CountLiveRounds() >= Rounds) { break; }
				Test.TestTrue(TEXT("本敌人在 8 米内才重新射击"), AI->GetKnowledge().bVisible && !Movement->WantsToSprint() && Distance <= 800.);
				Reference.SetPropertyOverridden(Reference.GetParameters().FindPropertyDescByName(TEXT("SprintFireRangeMeters"))->ID, false);
				AI->UnPossess();
				AI->Possess(NPC);
				ResetPositions();
				Rounds = Rifle->CountLiveRounds();
				++Stage;
				break;
			case 6:
				if (Rifle->CountLiveRounds() >= Rounds) { break; }
				Test.TestTrue(TEXT("取消覆盖后的实际射击距离恢复为 15 米"), AI->GetKnowledge().bVisible && !Movement->WantsToSprint() && Distance > 1300. && Distance <= 1500.);
				{
					auto* Pistol = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911"));
					Weapons->SetCarried(2, Pistol);
					Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Sidearm);
				}
				++Stage;
				break;
			case 7:
				if (Weapons->GetHeldSlot() != 2 || !Weapons->GetHeldWeapon()) { break; }
				Test.TestTrue(TEXT("换到手枪仍运行个体覆盖树"), AI->GetBehaviorTree() == Reference.GetStateTree());
				Reference.SetStateTree(nullptr);
				++Stage;
				break;
			case 8:
				if (AI->GetBehaviorTree() != Weapons->GetHeldWeapon()->DefaultAIStateTree) { break; }
				Test.TestFalse(TEXT("移除覆盖恢复武器树并释放冲刺输入"), Movement->WantsToSprint());
				return true;
			}
			return false;
		}

	private:
		/** 自动化框架持有至场景退出，汇总真实 PIE 行为的断言。 */
		FAutomationTestBase& Test;

		/** 实际时间超时起点（秒），避免导航异常时一直等待。 */
		double Started = FPlatformTime::Seconds();

		/** 依次检查远距、遮挡、默认和覆盖距离、换枪以及解除覆盖。 */
		int32 Stage = 0;

		/** 当前阶段开始时的枪内实弹数，通过弹药消耗确认真实射击。 */
		int32 Rounds = 0;

		/** 由测试世界拥有的临时遮挡；进入射程后移除视线阻挡。 */
		TWeakObjectPtr<UBoxComponent> Wall;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAISprintToRangeTest, "CaseGinkgo.AI.World.SprintToRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoAISprintToRangeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FSprintToRangeScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

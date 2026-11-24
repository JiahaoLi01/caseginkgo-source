#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoAIController.h"
#include "Components/SphereComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "StateTree.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"

namespace
{
	/** 在真实导航道路上让玩家消失。先从远处看见空地，再单独遮住目击点，验证到达和视线两个条件各自生效。 */
	class FPursuitScenario : public IAutomationLatentCommand
	{
	public:
		FPursuitScenario(FAutomationTestBase& InTest, const FString& InDefinition) : Test(InTest), DefinitionPath(InDefinition) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45.)
			{
				Test.AddError(FString::Printf(TEXT("最后目击点测试超时，阶段 %d，配置 %s"), Stage, *DefinitionPath));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			if (!World || !World->HasBegunPlay() || World->GetTimeSeconds() < .5) return false;
			TActorIterator<AGinkgoAICharacter> Enemies(World);
			auto* NPC = Enemies ? *Enemies : nullptr;
			auto* AI = NPC ? Cast<AGinkgoAIController>(NPC->GetController()) : nullptr;
			auto* PC = World->GetFirstPlayerController();
			auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!AI || !Player) return false;
			auto* Weapons = NPC->GetWeapons();
			auto* Mechanism = Weapons->GetMechanismState(Weapons->GetHeldSlot());
			const auto& Memory = AI->GetKnowledge();
			const auto& Pose = Weapons->GetHoldState();
			const double Now = World->GetTimeSeconds();
			switch (Stage)
			{
			case 0:
			{
				auto* Source = LoadObject<UGinkgoAIConfig>(nullptr, *DefinitionPath);
				auto* Weapon = Source ? Source->Weapon.LoadSynchronous() : nullptr;
				if (!Test.TestNotNull(TEXT("能加载该常规树的武器"), Weapon)) return true;
				auto* Config = DuplicateObject<UGinkgoAIConfig>(Source, NPC);
				Config->SuppressionDuration = 2.5f;
				AI->UnPossess();
				NPC->Definition = Config;
				NPC->PatrolRoute = nullptr;
				NPC->GetCharacterMovement()->StopMovementImmediately();
				NPC->SetActorLocationAndRotation(FVector(1100., 1200., 96.), FRotator(0., -90., 0.));
				Player->GetCharacterMovement()->DisableMovement();
				Player->SetActorLocation(FVector(1100., -1200., 96.));
				const int32 Slot = Weapon->SlotKind == EGinkgoWeaponSlotKind::Sidearm ? 2 : 0;
				Weapons->SetCarried(Slot, Weapon);
				Weapons->EquipStartingWeapon(Slot);
				AI->Possess(NPC);
				AI->SetControlRotation(FRotator(0., -90., 0.));
				++Stage;
				break;
			}
			case 1:
				if (!Memory.bVisible || Pose.AdsProgress < .95f || !Mechanism) break;
				Test.TestTrue(TEXT("运行手持武器自己的默认树"), AI->GetBehaviorTree() == Weapons->GetHeldWeapon()->DefaultAIStateTree.Get());
				LastAim = Memory.LastAimPoint;
				Player->SetActorLocation(FVector(6500., -6500., 96.));
				++Stage;
				break;
			case 2:
				if (AI->GetActiveBehavior() != EGinkgoAIBehavior::Pursue || !Mechanism) break;
				RoundsAtLoss = Mechanism->CountLiveRounds();
				++Stage;
				break;
			case 3:
			{
				const double SinceSight = Now - Memory.LastSeenTime;
				if (SinceSight <= AI->GetDefinition()->SuppressionDuration + .5) break;
				FVector Aim;
				if (!Test.TestTrue(TEXT("远处可看清空地，仍在向目击点推进"), !AI->HasReached(Memory.LastLocation)
					&& AI->GetActiveBehavior() == EGinkgoAIBehavior::Pursue && NPC->GetVelocity().Size2D() > 20.)) return true;
				Test.TestTrue(TEXT("丢失视线后向旧位置实际消耗子弹"), Mechanism && Mechanism->CountLiveRounds() < RoundsAtLoss);
				Test.TestTrue(TEXT("压制结束仍跟踪冻结的躯干点"), AI->GetTacticalAimPoint(Aim) && Aim.Equals(LastAim, 1.));
				RoundsAfterSuppression = Mechanism ? Mechanism->CountLiveRounds() : -1;
				auto* Obstacle = World->SpawnActor<AActor>();
				auto* Screen = NewObject<USphereComponent>(Obstacle);
				Obstacle->SetRootComponent(Screen);
				Screen->SetCanEverAffectNavigation(false);
				Screen->SetSphereRadius(10.f);
				Screen->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Screen->SetCollisionResponseToAllChannels(ECR_Ignore);
				Screen->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
				Screen->RegisterComponent();
				Screen->SetWorldLocation(LastAim);
				VisibilityScreen = Screen;
				++Stage;
				break;
			}
			case 4:
				if (!AI->HasReached(Memory.LastLocation))
				{
					// 霰弹枪最后一次泵动会暂时占用瞄准。用仍在走动的多帧验收泵动结束后的真实枪姿。
					if (NPC->GetVelocity().Size2D() > 20. && Pose.Stance == EGinkgoHoldStance::Ads && Pose.AdsProgress > .95f)
					{
						if (AimingSince < 0.) AimingSince = Now;
						bAimedWhileMoving |= Now - AimingSince >= .5;
					}
					else AimingSince = -1.;
					break;
				}
				Test.TestTrue(TEXT("停火后仍以完整瞄准姿势连续移动至少半秒"), bAimedWhileMoving);
				ArrivedAt = Now;
				++Stage;
				break;
			case 5:
				if (Now - ArrivedAt < AI->GetDefinition()->ClearPositionDelay + .5) break;
				Test.TestTrue(TEXT("抵达但目击点被遮挡时，仍举枪且没有继续射击"), AI->GetActiveBehavior() == EGinkgoAIBehavior::Pursue
					&& Pose.AdsProgress > .95f && !Weapons->WantsFire() && Mechanism && Mechanism->CountLiveRounds() == RoundsAfterSuppression);
				VisibilityScreen->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				++Stage;
				break;
			case 6:
				if (AI->GetActiveBehavior() != EGinkgoAIBehavior::Search || Pose.AdsProgress > .01f) break;
				Test.TestTrue(TEXT("确认空置后退出瞄准并停止射击"), Memory.bPositionCleared && !Weapons->WantsFire()
					&& Mechanism && Mechanism->CountLiveRounds() == RoundsAfterSuppression);
				return true;
			}
			return false;
		}

	private:
		/** 自动化框架持有至 PIE 结束，记录状态、枪姿、实弹数和位移断言。 */
		FAutomationTestBase& Test;

		/** 本次使用的常规敌人定义路径，对应真实的武器和默认树。 */
		FString DefinitionPath;

		/** 场景开始的实际时间（秒），导航或感知失效时结束测试。 */
		double Started = FPlatformTime::Seconds();

		/** 首次抵达被冻结的脚下位置的世界时间（秒），用于验证连续遮挡。 */
		double ArrivedAt = 0.;

		/** 压制结束后最近一个连续举枪移动区间的起始时间（世界秒），负数表示当前未在采样。 */
		double AimingSince = -1.;

		/** 进入到达范围前是否实际采样到持续的完整瞄准与水平移动。 */
		bool bAimedWhileMoving = false;

		/** 测试依次覆盖目击、压制、持枪推进、抵达和解除遮挡。 */
		int32 Stage = 0;

		/** 开始推进时的枪内实弹数。 */
		int32 RoundsAtLoss = 0;

		/** 压制窗口关闭后的枪内实弹数，应保持到进入搜索。 */
		int32 RoundsAfterSuppression = 0;

		/** 玩家离开前最后可见的躯干位置（世界 cm）。 */
		FVector LastAim = FVector::ZeroVector;

		/** PIE 世界拥有的临时遮挡，仅阻挡视线查询，保留到目击点的完整导航路径。 */
		TWeakObjectPtr<USphereComponent> VisibilityScreen;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FGinkgoPursuitWorldTest, "CaseGinkgo.AI.World.LastSightPursuit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FGinkgoPursuitWorldTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = { TEXT("Rifle"), TEXT("Pistol"), TEXT("Shotgun") };
	Commands = { TEXT("/Game/AI/DA_Enemy"), TEXT("/Game/AI/DA_Enemy_Pistol"), TEXT("/Game/AI/DA_Enemy_Shotgun") };
}

bool FGinkgoPursuitWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FPursuitScenario>(*this, Parameters));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

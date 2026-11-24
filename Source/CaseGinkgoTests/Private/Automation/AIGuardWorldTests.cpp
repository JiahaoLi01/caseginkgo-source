#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoAITacticsComponent.h"
#include "AI/GinkgoNoiseEmitterComponent.h"
#include "AI/GinkgoPatrolRoute.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTree.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"

namespace
{
	/** 在同一条巡逻线上分别验证两种警戒。只移动玩家或发出声音，目击、导航和武器输入都由 PIE 驱动。 */
	class FGuardScenario : public IAutomationLatentCommand
	{
	public:
		FGuardScenario(FAutomationTestBase& InTest, bool bInPermanent, bool bInOnlyTree)
			: Test(InTest), bPermanent(bInPermanent), bOnlyTree(bInOnlyTree) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45.)
			{
				Test.AddError(FString::Printf(TEXT("警戒测试超时，阶段 %d"), Stage));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			if (!World || !World->HasBegunPlay() || World->GetTimeSeconds() < .5) return false;
			TActorIterator<AGinkgoAICharacter> Enemies(World);
			auto* Enemy = Enemies ? *Enemies : nullptr;
			auto* AI = Enemy ? Cast<AGinkgoAIController>(Enemy->GetController()) : nullptr;
			auto* PC = World->GetFirstPlayerController();
			auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!AI || !Player) return false;
			auto* Weapons = Enemy->GetWeapons();
			const auto& Memory = AI->GetKnowledge();
			const double Now = World->GetTimeSeconds();
			const double Elapsed = Now - StageStarted;
			const double Displacement = FVector::Dist2D(Post, Enemy->GetActorLocation());
			if (Stage > 0 && (bPermanent || Stage <= 2))
			{
				if (!Test.TestTrue(TEXT("驻守期间胶囊留在原位且没有导航或掩体查询"),
					Displacement < 2. && AI->GetMoveStatus() == EPathFollowingStatus::Idle
					&& AI->GetTactics()->GetPendingQuery() == INDEX_NONE && !AI->GetTactics()->HasCover())) return true;
			}
			auto Advance = [&]() { ++Stage; StageStarted = Now; };
			const auto* Magazine = Weapons->GetMechanismState(2);
			switch (Stage)
			{
			case 0:
			{
				auto* Tree = LoadObject<UStateTree>(nullptr, bPermanent ? TEXT("/Game/AI/ST_Enemy_HoldPosition") : TEXT("/Game/AI/ST_Enemy_WaitForSight"));
				auto* Pistol = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911"));
				if (!Test.TestTrue(TEXT("警戒树已编译"), Tree && Tree->IsReadyToRun()) || !Test.TestNotNull(TEXT("手枪定义"), Pistol)
					|| !Test.TestTrue(TEXT("已有巡逻路线和远处的下一个巡逻点"), Enemy->PatrolRoute && Enemy->PatrolRoute->Points.Num() > 1)) return true;
				auto* Config = DuplicateObject<UGinkgoAIConfig>(Enemy->Definition.LoadSynchronous(), Enemy);
				Config->MemoryLifetime = 2.f;
				if (bOnlyTree)
				{
					// 同一棵待命树也是武器和角色的默认树，验证资产内的普通行为分支能接管。
					Pistol = DuplicateObject<UGinkgoWeaponDefinition>(Pistol, Enemy);
					Pistol->DefaultAIStateTree = Tree;
					Config->StateTree = Tree;
				}
				AI->UnPossess();
				Enemy->Definition = Config;
				Enemy->BehaviorOverrides.Reference.SetStateTree(Tree);
				Enemy->StartingPatrolPoint = 1;
				Enemy->GetCharacterMovement()->StopMovementImmediately();
				Enemy->SetActorLocationAndRotation(Post, FRotator(0., -90., 0.));
				Player->GetCharacterMovement()->DisableMovement();
				Player->SetActorLocation(FVector(0., -1000., 96.));
				AI->Possess(Enemy);
				AI->SetControlRotation(FRotator(0., -90., 0.));
				Weapons->SetCarried(2, Pistol);
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Sidearm);
				Advance();
				break;
			}
			case 1:
				if (Elapsed < 1. || Weapons->GetHeldSlot() != 2 || !Magazine || !Weapons->GetHeldWeapon()) break;
				Test.TestEqual(TEXT("换枪且有巡逻线时仍在原地警戒"), AI->GetActiveBehavior(), EGinkgoAIBehavior::StandGuard);
				Test.TestFalse(TEXT("隔墙尚未目击"), AI->HasSeenHostile());
				Rounds = Magazine->CountLiveRounds();
				Player->GetNoiseEmitter()->ReportShot(Player->GetActorLocation(), 6000.f);
				Advance();
				break;
			case 2:
			{
				if (Elapsed < 1. || !Memory.bHasHearing) break;
				FVector Aim;
				Test.TestTrue(TEXT("听到隔墙声音只转向声源"), !AI->HasSeenHostile() && AI->GetTacticalAimPoint(Aim) && Aim.Equals(Memory.LastHeardLocation, 2.));
				Test.TestEqual(TEXT("仅有声音时没有射击"), Magazine->CountLiveRounds(), Rounds);
				Player->SetActorLocation(FVector(700., 0., 96.));
				Advance();
				break;
			}
			case 3:
				if (!Memory.bVisible || !Magazine || Magazine->CountLiveRounds() >= Rounds || (!bPermanent && !bOnlyTree && Displacement < 50.)) break;
				Test.TestTrue(TEXT("首次目击后实际开火并按守则选树"), AI->HasSeenHostile() && AI->GetBehaviorTree() == (bPermanent
					? Enemy->BehaviorOverrides.Reference.GetStateTree() : Weapons->GetHeldWeapon()->DefaultAIStateTree.Get()));
				Test.TestEqual(TEXT("驻守保留警戒任务，待命树转入普通交战任务"), AI->GetActiveBehavior(),
					bPermanent ? EGinkgoAIBehavior::StandGuard : EGinkgoAIBehavior::Engage);
				Player->SetActorLocation(FVector(6000., -6000., 96.));
				Advance();
				break;
			case 4:
				if (Elapsed < 3. || Memory.bHasMemory) break;
				Test.TestTrue(TEXT("视觉记忆过期仍记得曾目击"), AI->HasSeenHostile());
				Test.TestEqual(TEXT("遗忘后按各自树驻守或巡逻"), AI->GetActiveBehavior(),
					bPermanent ? EGinkgoAIBehavior::StandGuard : EGinkgoAIBehavior::Patrol);
				AI->UnPossess();
				AI->Possess(Enemy);
				Advance();
				break;
			case 5:
				if (Elapsed < .5) break;
				Test.TestFalse(TEXT("重新接管时重置首次目击"), AI->HasSeenHostile());
				Test.TestEqual(TEXT("重新接管时恢复放置的警戒树"), AI->GetBehaviorTree(), Enemy->BehaviorOverrides.Reference.GetStateTree());
				return true;
			}
			return false;
		}

	private:
		/** 自动化框架持有至 PIE 结束，用来记录行为和真实位移。 */
		FAutomationTestBase& Test;

		/** 为真时全程要求守住原位，为假时要求首次目击后交回手枪树。 */
		bool bPermanent;

		/** 为真时让三个来源都指向待命树，覆盖关卡只有这一棵可用决策树的情形。 */
		bool bOnlyTree;

		/** 测试墙北侧的岗点（世界 cm），偏离量只比较水平面。 */
		const FVector Post{0., 700., 96.};

		/** 场景开始的实际时间（秒），避免感知或导航异常时一直等待。 */
		double Started = FPlatformTime::Seconds();

		/** 当前阶段进入的世界时间（秒）。 */
		double StageStarted = 0.;

		/** 依次验证巡逻、听声、目击、丢失目标与重新接管。 */
		int32 Stage = 0;

		/** 目击前手枪的实弹数，用于确认两种警戒均通过武器机构开火。 */
		int32 Rounds = 0;
	};
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FGinkgoGuardWorldTest, "CaseGinkgo.AI.World.Guard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FGinkgoGuardWorldTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	Names = { TEXT("WaitForSight"), TEXT("HoldPosition"), TEXT("WaitForSightStandalone") };
	Commands = Names;
}

bool FGinkgoGuardWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGuardScenario>(*this, Parameters == TEXT("HoldPosition"), Parameters == TEXT("WaitForSightStandalone")));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

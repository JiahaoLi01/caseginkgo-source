#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoAITacticsComponent.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "StateTree.h"
#include "Tests/AutomationEditorCommon.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"

namespace
{
	/** 在原单敌人地图验证实际收拔枪、导航、射击及墙边动画，不向感知记忆写伪造目击。 */
	class FGinkgoAIWeaponScenario : public IAutomationLatentCommand
	{
	public:
		explicit FGinkgoAIWeaponScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 80.0)
			{
				Test.AddError(FString::Printf(TEXT("武器 AI 场景超时，阶段 %d"), Stage));
				return true;
			}
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay() || World->GetTimeSeconds() < 0.5f) return false;
			AGinkgoAICharacter* NPC = nullptr;
			for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It) { NPC = *It; break; }
			auto* AI = NPC ? Cast<AGinkgoAIController>(NPC->GetController()) : nullptr;
			auto* Player = World->GetFirstPlayerController() ? Cast<AGinkgoCharacter>(World->GetFirstPlayerController()->GetPawn()) : nullptr;
			if (!AI || !Player) return false;
			if (Player->GetDamageable()->GetHealth() < 50.f) Player->GetDamageable()->Revive();
			UGinkgoWeaponComponent* Weapons = NPC->GetWeapons();
			const double Now = World->GetTimeSeconds();
			const double Elapsed = Now - StageStarted;
			const auto Advance = [&]() { ++Stage; StageStarted = Now; };
			const auto Position = [&]()
			{
				AI->StopMovement();
				NPC->GetCharacterMovement()->StopMovementImmediately();
				NPC->SetActorLocation(FVector(1100.0, 800.0, 96.0));
				NPC->SetActorRotation(FRotator(0.0, -90.0, 0.0));
				AI->SetControlRotation(FRotator(0.0, -90.0, 0.0));
				Player->SetActorLocation(FVector(1100.0, -800.0, 96.0));
			};
			const auto CheckTree = [&](const UGinkgoWeaponDefinition* Weapon)
			{
				Test.TestTrue(TEXT("实际手持枪决定决策树"), Weapon && AI->GetBehaviorTree() == Weapon->DefaultAIStateTree);
				Test.TestEqual(TEXT("近战武器选择主动逼近"), AI->GetTactics()->GetPositioning(), EGinkgoAIPositioning::Approach);
				Test.TestEqual(TEXT("逼近不残留掩体查询"), AI->GetTactics()->GetPendingQuery(), INDEX_NONE);
				Test.TestFalse(TEXT("逼近不保留旧掩体占位"), AI->GetTactics()->HasCover());
			};
			switch (Stage)
			{
			case 0:
				Pistol = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911"));
				Shotgun = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/Shotgun/DA_Shotgun"));
				if (!Test.TestNotNull(TEXT("手枪资产"), Pistol.Get()) || !Test.TestNotNull(TEXT("霰弹枪资产"), Shotgun.Get())) return true;
				Weapons->SetCarried(2, Pistol.Get());
				Weapons->SetCarried(1, Shotgun.Get());
				Weapons->SetReserveRounds(Pistol->AmmoType, 56);
				Weapons->SetReserveRounds(Shotgun->AmmoType, 32);
				NPC->PatrolRoute = nullptr;
				Player->GetCharacterMovement()->DisableMovement();
				Position();
				Advance();
				break;
			case 1:
				if (Elapsed < 1.0) break;
				Test.TestTrue(TEXT("换枪前真实感知已发现玩家"), AI->GetKnowledge().bVisible);
				AcquiredTime = AI->GetKnowledge().AcquiredTime;
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Sidearm);
				Advance();
				break;
			case 2:
				if (Elapsed < 5.0 && (Weapons->GetHeldWeapon() != Pistol.Get() || !Weapons->IsShotStepClear())) break;
				CheckTree(Pistol.Get());
				Test.TestEqual(TEXT("实际切枪没有重置目击反应起点"), AI->GetKnowledge().AcquiredTime, AcquiredTime);
				Position();
				InitialDistance = FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation());
				InitialRounds = Weapons->GetMechanismState(2)->CountLiveRounds();
				Advance();
				break;
			case 3:
				if (Elapsed < 6.0) break;
				Test.TestTrue(TEXT("手枪实际向玩家拉近至少 350 cm"), FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation()) < InitialDistance - 350.0);
				Test.TestTrue(TEXT("逼近期间手枪实际射击消耗弹药"), Weapons->GetMechanismState(2)->CountLiveRounds() < InitialRounds);
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Slot2);
				Advance();
				break;
			case 4:
				if (Elapsed < 5.0 && (Weapons->GetHeldWeapon() != Shotgun.Get() || !Weapons->IsShotStepClear())) break;
				CheckTree(Shotgun.Get());
				Position();
				InitialDistance = FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation());
				InitialRounds = Weapons->GetMechanismState(1)->CountLiveRounds();
				Advance();
				break;
			case 5:
				if (Elapsed < 7.0) break;
				Test.TestTrue(TEXT("霰弹枪实际向玩家拉近至少 500 cm"), FVector::Dist2D(NPC->GetActorLocation(), Player->GetActorLocation()) < InitialDistance - 500.0);
				Test.TestTrue(TEXT("霰弹枪仍通过机构射击"), Weapons->GetMechanismState(1)->CountLiveRounds() < InitialRounds);
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Slot1);
				Position();
				Advance();
				break;
			case 6:
				if (Elapsed < 5.0 && (Weapons->GetHeldSlot() != 0 || !Weapons->IsShotStepClear() || !AI->GetKnowledge().bVisible)) break;
				Position();
				{
					AActor* WallActor = World->SpawnActor<AActor>();
					UBoxComponent* Box = NewObject<UBoxComponent>(WallActor);
					WallActor->SetRootComponent(Box);
					Box->SetBoxExtent(FVector(13.0, 15.0, 140.0));
					Box->SetCollisionProfileName(TEXT("BlockAll"));
					Box->SetCanEverAffectNavigation(false);
					Box->RegisterComponent();
					Box->SetWorldLocation(FVector(1093.0, 650.0, 140.0));
					Wall = Box;
					FGinkgoAICoverPose Pose;
					Test.TestTrue(TEXT("高墙边缘能明确识别右侧探身射界"), AI->GetTactics()->EvaluateCover(FVector(1100.0, 800.0, 0.0), AI->GetKnowledge().LastAimPoint, Pose) && Pose.Peek > 0.f);
					Box->SetWorldLocation(FVector(1107.0, 650.0, 140.0));
					Test.TestTrue(TEXT("镜像墙边明确识别左侧探身"), AI->GetTactics()->EvaluateCover(FVector(1100.0, 800.0, 0.0), AI->GetKnowledge().LastAimPoint, Pose) && Pose.Peek < 0.f);
					Box->SetWorldLocation(FVector(1100.0, 650.0, 47.5));
					Box->SetBoxExtent(FVector(100.0, 15.0, 47.5));
					Test.TestTrue(TEXT("原有低掩体仍合格且不要求探身"), AI->GetTactics()->EvaluateCover(FVector(1100.0, 800.0, 0.0), AI->GetKnowledge().LastAimPoint, Pose) && Pose.Peek == 0.f);
					Box->SetBoxExtent(FVector(100.0, 15.0, 140.0));
					Box->SetWorldLocation(FVector(1100.0, 650.0, 140.0));
					Test.TestFalse(TEXT("完整墙面不能被当作可探穿的掩体"), AI->GetTactics()->EvaluateCover(FVector(1100.0, 800.0, 0.0), AI->GetKnowledge().LastAimPoint, Pose));
					Box->SetBoxExtent(FVector(13.0, 15.0, 140.0));
					Box->SetWorldLocation(FVector(1093.0, 650.0, 140.0));
					AI->GetTactics()->Reset();
					AI->GetTactics()->TickEngagement();
				}
				Advance();
				break;
			case 7:
				if (Now >= NextPeekLog)
				{
					NextPeekLog = Now + 0.5;
					const UGinkgoAITacticsComponent* Tactics = AI->GetTactics();
					const auto* Anim = Cast<UGinkgoAnimInstance>(NPC->GetMesh()->GetAnimInstance());
					Test.AddInfo(FString::Printf(TEXT("墙边 %.2f s: visible=%d behavior=%d query=%d cover=%d point=%s side=%.1f intent=%.1f anim=%.2f pawn=%s"),
						Elapsed, AI->GetKnowledge().bVisible, static_cast<int32>(AI->GetActiveBehavior()), Tactics->GetPendingQuery(), Tactics->HasCover(),
						*Tactics->GetCoverPose().Ground.ToString(), Tactics->GetCoverPose().Peek, NPC->GetTacticalPeek(), Anim ? Anim->GetPeekWeight() : 0.0, *NPC->GetActorLocation().ToString()));
				}
				if (const auto* Anim = Cast<UGinkgoAnimInstance>(NPC->GetMesh()->GetAnimInstance()))
				{
					if (NPC->GetTacticalPeek() > 0.f && Anim->GetPeekWeight() > 0.5) ++PeekSamples;
				}
				if (Elapsed < 3.0) break;
				Test.TestTrue(TEXT("真实 EQS 选中墙边并在动画管线中持续右倾"), PeekSamples >= 20);
				Test.AddInfo(FString::Printf(TEXT("墙边探身的有效动画样本 %d，最终请求 %.2f"), PeekSamples, NPC->GetTacticalPeek()));
				Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Sidearm);
				Advance();
				break;
			case 8:
				if (Elapsed < 5.0 && (Weapons->GetHeldWeapon() != Pistol.Get() || !Weapons->IsShotStepClear())) break;
				CheckTree(Pistol.Get());
				Test.TestEqual(TEXT("墙边切到手枪清除探身意图"), NPC->GetTacticalPeek(), 0.f);
				{
					auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
					Ammo->Damage = 10000.f;
					NPC->GetDamageable()->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, Player);
				}
				Test.TestFalse(TEXT("不同武器的 AI 死亡仍能停树"), AI->IsAIActive());
				Test.TestEqual(TEXT("死亡清理查询"), AI->GetTactics()->GetPendingQuery(), INDEX_NONE);
				return true;
			}
			return false;
		}

	private:
		/** 当前测试的断言接收者，由自动化框架持有到潜伏命令完成。 */
		FAutomationTestBase& Test;

		/** 真实时间超时起点（秒），防止动画或导航失败后无限等待。 */
		double Started = FPlatformTime::Seconds();

		/** 顺序阶段索引，每次完成后推进；世界时间与真实超时分别计算。 */
		int32 Stage = 0;

		/** 当前阶段开始的世界秒数，用于允许收拔枪、移动和动画实际推进。 */
		double StageStarted = 0.0;

		/** 换枪前的首次目击时间（世界秒），检查不会因换树重置感知。 */
		double AcquiredTime = 0.0;

		/** 本阶段起始水平距离（cm），供实际导航后的距离比较。 */
		double InitialDistance = 0.0;

		/** 本阶段起始枪内弹药数，验证真实机构确实发射。 */
		int32 InitialRounds = 0;

		/** 真实动画权重超过一半的墙边探身帧数，排除单帧写值即通过。 */
		int32 PeekSamples = 0;

		/** 墙边阶段的下次诊断输出世界秒数，半秒一次，便于定位感知/选点/动画哪一层未衔接。 */
		double NextPeekLog = 0.0;

		/** 被真实武器槽持有的手枪资产，弱引用不延长测试世界生命周期。 */
		TWeakObjectPtr<UGinkgoWeaponDefinition> Pistol;

		/** 被真实武器槽持有的霰弹枪资产，切枪时不直接改动画或机构状态。 */
		TWeakObjectPtr<UGinkgoWeaponDefinition> Shotgun;

		/** 测试临时墙，离开场景由 PIE 世界清理。 */
		TWeakObjectPtr<UBoxComponent> Wall;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAIWeaponWorldTest, "CaseGinkgo.AI.World.WeaponTacticsAndCoverPeek",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAIWeaponWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGinkgoAIWeaponScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

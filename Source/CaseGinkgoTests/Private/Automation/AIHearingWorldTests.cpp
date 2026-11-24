#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoNoiseEmitterComponent.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "Weapon/GinkgoWeaponAttachment.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"

namespace
{
	/** 使用真实 PIE、动画落脚和武器状态树；不直接写敌人记忆，检验事件到感知再到导航的完整路径。 */
	class FGinkgoAIHearingScenario : public IAutomationLatentCommand
	{
	public:
		explicit FGinkgoAIHearingScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 75.0)
			{
				Test.AddError(FString::Printf(TEXT("听觉 PIE 超时，阶段 %d"), Stage));
				return true;
			}
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay()) return false;
			if (!Enemy.IsValid())
			{
				for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It) { Enemy = *It; break; }
			}
			AGinkgoAICharacter* NPC = Enemy.Get();
			AGinkgoAIController* AI = NPC ? Cast<AGinkgoAIController>(NPC->GetController()) : nullptr;
			APlayerController* PC = World->GetFirstPlayerController();
			AGinkgoCharacter* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!NPC || !AI || !Player) return false;
			const double Now = World->GetTimeSeconds();
			const double Elapsed = Now - StageStarted;
			UGinkgoWeaponComponent* Weapons = Player->GetWeapons();
			const FGinkgoAIKnowledge& Memory = AI->GetKnowledge();
			auto Advance = [&]()
			{
				Test.AddInfo(FString::Printf(TEXT("听觉阶段 %d @ %.2fs，行为 %s，目击 %d，听觉 %d"), Stage, Now,
					*UEnum::GetValueAsString(AI->GetActiveBehavior()), Memory.bVisible, Memory.bHasHearing));
				++Stage;
				StageStarted = Now;
			};
			auto ResetListener = [&]()
			{
				AI->UnPossess();
				AI->Possess(NPC);
				NPC->GetCharacterMovement()->DisableMovement();
			};
			if (Stage >= 2 && Stage <= 4)
			{
				// 在中央高墙背面横向来回走，始终没有视线；输入驱动真实步态与片段标记，不直接报告脚步。
				if (Player->GetActorLocation().X > 200.0) Direction = -1.f;
				if (Player->GetActorLocation().X < -200.0) Direction = 1.f;
				Player->AddMovementInput(FVector(Direction, 0.0, 0.0), Stage == 2 ? 0.25f : 1.f);
				Player->SetWantsToSprint(Stage == 4);
			}
			if (Stage >= 5) Weapons->UpdateHoldState(World->GetDeltaSeconds());
			switch (Stage)
			{
			case 0:
				if (Now < 0.5) break;
				PC->SetActorTickEnabled(false);
				NPC->PatrolRoute = nullptr;
				AI->StopMovement();
				NPC->SetActorLocation(FVector(0.0, 700.0, 96.0));
				NPC->GetCharacterMovement()->DisableMovement();
				Player->SetActorLocation(FVector(0.0, -180.0, 96.0));
				NPC->GetNoiseEmitter()->ReportShot(NPC->GetActorLocation(), 6000.f);
				{
					AGinkgoAICharacter* Friend = World->SpawnActor<AGinkgoAICharacter>(FVector(-5000.0, 5000.0, 96.0), FRotator::ZeroRotator);
					if (!Test.TestNotNull(TEXT("友方声音源"), Friend)) return true;
					if (AGinkgoAIController* FriendAI = Cast<AGinkgoAIController>(Friend->GetController())) FriendAI->StopAI();
					Friend->GetCharacterMovement()->DisableMovement();
					Friend->GetNoiseEmitter()->ReportShot(NPC->GetActorLocation(), 6000.f);
				}
				Player->GetNoiseEmitter()->ReportShot(Player->GetActorLocation(), 100.f);
				Advance();
				break;
			case 1:
				if (Elapsed < 0.5) break;
				Test.TestFalse(TEXT("自身、友方枪声与传播半径外的枪声不会唤醒"), Memory.bHasHearing);
				Advance();
				break;
			case 2:
				if (Elapsed < 2.0) break;
				Test.TestFalse(TEXT("真实慢走动画不发脚步噪声"), Memory.bHasHearing);
				Test.TestTrue(TEXT("慢走静音在真实移动期间验证"), Player->GetVelocity().Size2D() > 30.0
					&& CastChecked<UGinkgoAnimInstance>(Player->GetMesh()->GetAnimInstance())->GetGait() == EGinkgoAnimGait::Walk);
				Advance();
				break;
			case 3:
				if (Elapsed < 2.0) break;
				Test.TestTrue(TEXT("真实 Run 落脚标记使隔墙 AI 听见"), Memory.bHasHearing);
				Test.TestFalse(TEXT("隔墙听到奔跑不冒充目击"), Memory.bHasMemory);
				NPC->SetActorLocation(FVector(0.0, 1100.0, 96.0));
				Player->SetActorLocation(FVector(0.0, -400.0, 96.0));
				ResetListener();
				Advance();
				break;
			case 4:
				if (Elapsed < 2.0) break;
				Test.TestTrue(TEXT("真实 Sprint 落脚在奔跑听距之外仍能听见"), Memory.bHasHearing);
				Player->SetWantsToSprint(false);
				Player->GetCharacterMovement()->DisableMovement();
				PC->SetControlRotation(FRotator(-45.0, -90.0, 0.0));
				{
					UGinkgoWeaponDefinition* Rifle = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
					if (!Test.TestNotNull(TEXT("实际 AK 定义"), Rifle)) return true;
					Weapons->SetCarried(0, Rifle);
					Weapons->SetHeldSlot(0);
					Player->AttachWeaponToHand(0);
				}
				ResetListener();
				Advance();
				break;
			case 5:
				if (Elapsed < 1.0) break;
				InitialRounds = Weapons->GetMechanismState(0)->CountLiveRounds();
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Fire);
				Advance();
				break;
			case 6:
				if (Elapsed < 1.0) break;
				Test.TestTrue(TEXT("原装 AK 经武器树实际消耗子弹"), Weapons->GetMechanismState(0)->CountLiveRounds() < InitialRounds);
				Test.TestTrue(TEXT("关闭音频设备也能听见实际开火"), Memory.bHasHearing);
				{
					UGinkgoWeaponAttachment* Part = LoadObject<UGinkgoWeaponAttachment>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47Suppressor.DA_AK47Suppressor"));
					if (!Test.TestNotNull(TEXT("实际消音器定义"), Part)) return true;
					Test.TestTrue(TEXT("消音器保持非零听距"), Part->FireNoiseRangeScale > 0.f);
					Weapons->InstallAttachment(0, Part);
					Test.TestTrue(TEXT("消音器已装在实际射击槽"), Weapons->GetAttachments(0).Parts.Contains(Part));
				}
				ResetListener();
				Advance();
				break;
			case 7:
				if (Elapsed < 0.5) break;
				InitialRounds = Weapons->GetMechanismState(0)->CountLiveRounds();
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Fire);
				Advance();
				break;
			case 8:
				if (Elapsed < 1.0) break;
				Test.TestTrue(TEXT("消音 AK 真实发射"), Weapons->GetMechanismState(0)->CountLiveRounds() < InitialRounds);
				Test.TestTrue(TEXT("消音 AK 同样产生听觉事件"), Memory.bHasHearing);
				Test.TestFalse(TEXT("仅听声从不授权 AI 开火"), NPC->GetWeapons()->WantsFire());
				ResetListener();
				NPC->SetActorLocation(FVector(0.0, 700.0, 96.0));
				NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				// 声源事件带发生时位置；随后把玩家移走，检验导航不读取 Actor 的新位置。
				Player->GetNoiseEmitter()->ReportShot(FrozenNoise, 6000.f);
				Player->SetActorLocation(FVector(5000.0, -5000.0, 96.0));
				Advance();
				break;
			case 9:
				if (Elapsed < 0.5) break;
				Test.TestEqual(TEXT("听觉记录发生处而非声源当前位置"), Memory.LastHeardLocation, FrozenNoise);
				Test.TestEqual(TEXT("状态树进入调查任务"), AI->GetActiveBehavior(), EGinkgoAIBehavior::Investigate);
				Test.TestTrue(TEXT("前往声源时常规武器保持举枪且不向声音开火"),
					NPC->GetWeapons()->GetHoldState().Stance == EGinkgoHoldStance::Ads && !NPC->GetWeapons()->WantsFire());
				Advance();
				break;
			case 10:
				if (AI->GetActiveBehavior() != EGinkgoAIBehavior::Search && Elapsed < 7.0) break;
				Test.TestEqual(TEXT("投影高处枪声到导航后真正抵达并开始搜索"), AI->GetActiveBehavior(), EGinkgoAIBehavior::Search);
				Test.TestTrue(TEXT("实际抵达旧声源而非跟踪玩家"), FVector::Dist2D(NPC->GetActorLocation(), FrozenNoise) < 100.0);
				Test.TestFalse(TEXT("调查和搜索全程未获得视觉记忆"), Memory.bHasMemory);
				Test.TestEqual(TEXT("AI 未向声音盲射消耗子弹"), NPC->GetWeapons()->GetMechanismState(0)->CountLiveRounds(), 30);
				Player->GetNoiseEmitter()->ReportShot(FrozenNoise + FVector(400.0, 0.0, 0.0), 6000.f);
				Advance();
				break;
			case 11:
				if (Elapsed < 0.5) break;
				Test.TestEqual(TEXT("新声音打断搜索，更新调查目的地"), AI->GetActiveBehavior(), EGinkgoAIBehavior::Investigate);
				AI->StopAI();
				Test.TestFalse(TEXT("停树清理听觉线索"), Memory.bHasHearing);
				{
					// 只改临时定义，检验通用配件距离系数；磁盘上的消音器参数保持原值。
					UGinkgoWeaponDefinition* Rifle = DuplicateObject<UGinkgoWeaponDefinition>(Weapons->GetHeldWeapon(), GetTransientPackage());
					UGinkgoWeaponAttachment* Part = DuplicateObject<UGinkgoWeaponAttachment>(Weapons->GetAttachments(0).Parts[0], Rifle);
					for (FGinkgoWeaponAttachmentMount& Mount : Rifle->Attachments)
					{
						if (Mount.Part == Weapons->GetAttachments(0).Parts[0]) Mount.Part = Part;
					}
					Part->FireNoiseRangeScale = 0.1f;
					Rifle->FireNoiseRange = 6000.f;
					Weapons->SetCarried(0, Rifle);
					Weapons->SetHeldSlot(0);
					Player->AttachWeaponToHand(0);
					Weapons->InstallAttachment(0, Part);
				}
				NPC->SetActorLocation(FVector(0.0, 1100.0, 96.0));
				Player->SetActorLocation(FVector(0.0, -400.0, 96.0));
				ResetListener();
				Advance();
				break;
			case 12:
				if (Elapsed < 1.0) break;
				InitialRounds = Weapons->GetMechanismState(0)->CountLiveRounds();
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Fire);
				Advance();
				break;
			case 13:
				if (Elapsed < 1.0) break;
				Test.TestTrue(TEXT("距离系数实验实际射击"), Weapons->GetMechanismState(0)->CountLiveRounds() < InitialRounds);
				Test.TestFalse(TEXT("0.1 配件距离系数让 15 米外听不到原本 60 米的枪声"), Memory.bHasHearing);
				NPC->SetActorLocation(FVector(0.0, 100.0, 96.0));
				ResetListener();
				Advance();
				break;
			case 14:
				if (Elapsed < 0.5) break;
				Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Fire);
				Advance();
				break;
			case 15:
				if (Elapsed < 1.0) break;
				Test.TestTrue(TEXT("同一缩短听距的配件在 5 米内仍能被听到"), Memory.bHasHearing);
				Test.TestFalse(TEXT("缩短听距不改变隔墙没有目击的事实"), Memory.bHasMemory);
				AI->StopAI();
				return true;
			}
			return false;
		}

	private:
		/** 自动化报告接收者，在本命令结束前由框架持有。 */
		FAutomationTestBase& Test;

		/** 真实时间超时基准（秒），PIE 无法启动时也能结束。 */
		double Started = FPlatformTime::Seconds();

		/** 当前验收阶段，由每阶段的世界时间和实际状态推进。 */
		int32 Stage = 0;

		/** 当前阶段开始的世界时间（秒），Advance 更新。 */
		double StageStarted = 0.0;

		/** 本次 PIE 的受测敌人，地图退出后失效。 */
		TWeakObjectPtr<AGinkgoAICharacter> Enemy;

		/** 沿高墙背面移动的世界 X 方向，在左右边界切换，保证脚步实验始终无视线。 */
		float Direction = 1.f;

		/** 单次实际开火前的弹量，防止只测到音效或伪造的开火输入。 */
		int32 InitialRounds = 0;

		/** 枪口高度的冻结声源位置（世界 cm），导航应投影到地面，但不能篡改此感知事实。 */
		const FVector FrozenNoise = FVector(200.0, 700.0, 145.0);
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAIHearingWorldTest, "CaseGinkgo.AI.World.HearingFootstepsShotsAndInvestigation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAIHearingWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGinkgoAIHearingScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

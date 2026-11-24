#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "Character/GinkgoCharacter.h"
#include "Checkpoint/GinkgoCheckpoint.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HighResScreenshot.h"
#include "Misc/CommandLine.h"
#include "PlatformFeatures.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "SaveGameSystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "Weapon/GinkgoWeaponComponent.h"

namespace
{
	/** 完整 PIE 世界：通过重叠进入，等待地点淡出，死亡触发真正的同图重载，再检查还原与敌人回滚。 */
	class FCheckpointWorldScenario : public IAutomationLatentCommand
	{
	public:
		explicit FCheckpointWorldScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual ~FCheckpointWorldScenario() override
		{
			auto* Files = IPlatformFeaturesModule::Get().GetSaveGameSystem();
			Files->DeleteGame(false, *Slot, 0);
			Files->DeleteGame(false, *(Slot + TEXT(".json")), 0);
		}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 100.) { Test.AddError(FString::Printf(TEXT("检查点完整地图回归超时，阶段 %d"), Stage)); return true; }
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			auto* PC = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
			auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!World || !World->HasBegunPlay() || !Player || World->GetTimeSeconds() < 1.) { return false; }
			auto* Progress = UGinkgoCheckpointSubsystem::Get(World);
			for (TActorIterator<AGinkgoAIController> AI(World); AI; ++AI) { AI->StopAI(); }
			if (Stage == 0)
			{
				OldWorld = World;
				Progress->SaveSlotName = Slot;
				TArray<AGinkgoAICharacter*> Enemies;
				for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It) { Enemies.Add(*It); }
				if (!Test.TestTrue(TEXT("测试地图至少两名关卡敌人"), Enemies.Num() >= 2)) { return true; }
				BeforeId = UGinkgoCheckpointSubsystem::MakeStableId(*Enemies[0], Enemies[0]->PersistentEnemyId);
				AfterId = UGinkgoCheckpointSubsystem::MakeStableId(*Enemies[1], Enemies[1]->PersistentEnemyId);
				Kill(*Enemies[0]);
				AfterEnemy = Enemies[1];
				Player->GetDamageable()->RestoreSaveState(61.f, 100.f, {});
				auto* Point = World->SpawnActor<AGinkgoCheckpoint>(Player->GetActorLocation() + FVector(800., 0., 0.), FRotator::ZeroRotator);
				Point->CheckpointId = TEXT("Automation_Entrance");
				Point->Announcement.LocationName = FText::FromString(TEXT("San Lorenzo"));
				Point->RespawnPoint = FTransform(FRotator::ZeroRotator, FVector(350., 0., 0.));
				Respawn = Point->GetRespawnTransform();
				Player->GetCharacterMovement()->StopMovementImmediately();
				Player->SetActorLocation(Point->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
				Test.TestTrue(TEXT("胶囊进入体积即已存档"), Progress->GetState().bHasCheckpoint);
				Test.TestTrue(TEXT("保存的敌人包含进入前击杀"), Progress->GetState().DefeatedEnemies.Contains(BeforeId));
				Test.TestTrue(TEXT("触发后 HUD 已隐藏"), PC->IsGameplayHudSuppressed());
				StageStarted = FPlatformTime::Seconds();
				Stage = 1;
				return false;
			}
			if (Stage == 1)
			{
				if (!bTitleShot && FPlatformTime::Seconds() - StageStarted > 1.) { Shot(TEXT("title")); bTitleShot = true; }
				if (PC->IsGameplayHudSuppressed()) { return false; }
				StageStarted = FPlatformTime::Seconds();
				Stage = 2;
				return false;
			}
			if (Stage == 2)
			{
				if (FPlatformTime::Seconds() - StageStarted < .8) { return false; }
				Shot(TEXT("saved"));
				Kill(*AfterEnemy.Get());
				Test.TestTrue(TEXT("存档后击杀记录在工作状态中"), Progress->GetState().DefeatedEnemies.Contains(AfterId));
				Kill(*Player);
				DeathTime = World->GetTimeSeconds();
				Test.TestTrue(TEXT("致命伤当帧骨盆开始物理模拟"), Player->GetMesh()->IsSimulatingPhysics(TEXT("pelvis")));
				Test.TestTrue(TEXT("布娃娃接管碰撞并停止角色移动"), Player->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision
					&& Player->GetCharacterMovement()->MovementMode == MOVE_None);
				Stage = 3;
				return false;
			}
			if (Stage == 3)
			{
				const bool bLoading = UGinkgoSaveGameSubsystem::Get(World)->IsLoadingSaveGame();
				if (World == OldWorld.Get())
				{
					const double Elapsed = World->GetTimeSeconds() - DeathTime;
					if (Elapsed < 2. && bLoading) { Test.AddError(TEXT("死亡后不足两秒已开始读档")); return true; }
					if (Elapsed >= 1.5 && Elapsed < 2.)
					{
						bSawDeathPause = Player->GetDamageable()->IsDead() && Player->GetMesh()->IsSimulatingPhysics(TEXT("pelvis"));
					}
					return false;
				}
				if (bLoading) { return false; }
				Test.TestTrue(TEXT("加载前有可见的倒地停留"), bSawDeathPause);
				Test.TestFalse(TEXT("复活身体已退出布娃娃"), Player->GetMesh()->IsSimulatingPhysics(TEXT("pelvis")));
				Test.TestTrue(TEXT("同一 GameInstance 保留测试槽"), Progress->SaveSlotName == Slot);
				Test.TestEqual(TEXT("复活生命来自检查点"), Player->GetDamageable()->GetHealth(), 61.f);
				Test.TestTrue(TEXT("复活在指定位置"), FVector::Dist2D(Player->GetActorLocation(), Respawn.GetLocation()) < 5.);
				Test.TestFalse(TEXT("读档不重新播放已抵达地点"), PC->IsGameplayHudSuppressed());
				bool bBeforeExists = false;
				bool bAfterAlive = false;
				for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It)
				{
					const FString Id = UGinkgoCheckpointSubsystem::MakeStableId(**It, It->PersistentEnemyId);
					bBeforeExists |= Id == BeforeId;
					bAfterAlive |= Id == AfterId && !It->GetDamageable()->IsDead();
				}
				Test.TestFalse(TEXT("已存档死亡的敌人没有复生"), bBeforeExists);
				Test.TestTrue(TEXT("检查点后死亡的敌人重新活着出现"), bAfterAlive);
				Test.TestFalse(TEXT("工作死亡状态回滚到存档"), Progress->GetState().DefeatedEnemies.Contains(AfterId));
				return true;
			}
			return false;
		}

	private:
		static void Kill(AGinkgoCharacter& Character)
		{
			auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
			Ammo->Damage = 10000.f;
			Character.GetDamageable()->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
		}

		static void Shot(const TCHAR* Name)
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("CheckpointPreview")) && FApp::CanEverRender())
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Checkpoint") / FString(Name), true, false);
			}
		}

		/** 所属自动化测试，用于跨帧汇总断言。 */
		FAutomationTestBase& Test;

		/** 测试开始的实际秒数；跨地图后世界时间清零，超时仍然有效。 */
		double Started = FPlatformTime::Seconds();

		/** 当前阶段起始的实际秒数。 */
		double StageStarted = 0.;

		/** 致命伤发生时旧世界的游戏时间（秒），与实际读档计时器使用同一时钟。 */
		double DeathTime = 0.;

		/** 已在读档前的最后半秒内观察到旧角色保持死亡和物理模拟。 */
		bool bSawDeathPause = false;

		/** 0 进入、1 地点显示、2 toast 和死亡、3 新地图检查。 */
		int32 Stage = 0;

		/** 地点完整显现时只截一帧；渲染可选，不影响无渲染测试。 */
		bool bTitleShot = false;

		/** 识别世界确实重建，不强引用旧世界而阻止 GC。 */
		TWeakObjectPtr<UWorld> OldWorld;

		/** 存档后才击杀的敌人；仅在旧世界有效。 */
		TWeakObjectPtr<AGinkgoAICharacter> AfterEnemy;

		/** 存档前死亡的关卡敌人完整稳定 ID。 */
		FString BeforeId;

		/** 存档后死亡的关卡敌人完整稳定 ID。 */
		FString AfterId;

		/** 与触发位置不同的复活变换，用于验证配置生效。 */
		FTransform Respawn;

		/** 不与玩家正式文件相交的独立槽名，析构清理。 */
		FString Slot = TEXT("Automation_CheckpointWorld_") + FGuid::NewGuid().ToString();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointWorldTest, "CaseGinkgo.Checkpoint.WorldReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoCheckpointWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/Checkpoint/L_CheckpointTest")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FCheckpointWorldScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

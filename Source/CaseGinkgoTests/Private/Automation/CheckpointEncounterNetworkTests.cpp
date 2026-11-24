#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoEnemyArchetype.h"
#include "Checkpoint/GinkgoCheckpoint.h"
#include "Checkpoint/GinkgoCheckpointEnemySpawn.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/GinkgoVitals.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace Ginkgo::CheckpointTests
{
	class FStartEncounterNetworkPIE : public IAutomationLatentCommand
	{
	public:
		virtual bool Update() override
		{
			FRequestPlaySessionParams Params;
			Params.EditorPlaySettings = DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
			Params.EditorPlaySettings->SetPlayNetMode(PIE_Client);
			Params.EditorPlaySettings->SetRunUnderOneProcess(true);
			Params.EditorPlaySettings->SetPlayNumberOfClients(2);
			GEditor->RequestPlaySession(Params);
			return true;
		}
	};

	/** 分别从拥有者和旁观者读取同一个玩家，验证服务器伤害、HUD 采样与布娃娃跨网同步。 */
	class FCheckPlayerVitalsReplication : public IAutomationLatentCommand
	{
	public:
		explicit FCheckPlayerVitalsReplication(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 55.)
			{
				Test.AddError(FString::Printf(TEXT("玩家生命复制超时，阶段 %d"), Stage));
				return true;
			}
			AGinkgoCharacter* Player = nullptr;
			TArray<UWorld*, TInlineAllocator<2>> Clients;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (Context.WorldType != EWorldType::PIE || !World || !World->HasBegunPlay()) { continue; }
				if (World->GetNetMode() == NM_DedicatedServer)
				{
					for (TActorIterator<AGinkgoAIController> AI(World); AI; ++AI) { AI->StopAI(); }
					if (auto* PC = World->GetFirstPlayerController()) { Player = Cast<AGinkgoCharacter>(PC->GetPawn()); }
				}
				else if (World->GetNetMode() == NM_Client) { Clients.Add(World); }
			}
			if (!Player || !Player->GetPlayerState() || Clients.Num() != 2) { return false; }
			auto* Damage = Player->GetDamageable();
			if (Stage == 0)
			{
				FGinkgoArmorPiece Armor;
				Armor.Name = TEXT("Vest");
				Armor.MaxDurability = 50.f;
				Armor.Durability = 27.f;
				Armor.Covers.Add(EGinkgoBodyPart::UpperChest);
				Damage->RestoreSaveState(83.f, 120.f, {Armor});
				++Stage;
				return false;
			}
			const FGinkgoVitals Expected = FGinkgoVitals::Read(Damage);
			int32 Owners = 0;
			for (UWorld* Client : Clients)
			{
				const auto* PC = Cast<AGinkgoPlayerController>(Client->GetFirstPlayerController());
				if (!PC || !PC->GetPawn()) { return false; }
				AGinkgoCharacter* Replica = nullptr;
				for (TActorIterator<AGinkgoCharacter> It(Client); It; ++It)
				{
					if (It->GetPlayerState() && It->GetPlayerState()->GetPlayerId() == Player->GetPlayerState()->GetPlayerId()) { Replica = *It; break; }
				}
				if (!Replica) { return false; }
				auto* Source = Replica->GetDamageable();
				if (!(FGinkgoVitals::Read(Source) == Expected) || Source->IsDead() != Damage->IsDead()) { return false; }
				Owners += PC->GetPawn() == Replica;
				if (Stage == 1)
				{
					Test.TestTrue(TEXT("服务器同步了装备名、覆盖部位与全身护甲模式"), Source->HasFullBodyArmor() && Source->GetArmor().Num() == 1
						&& Source->GetArmor()[0].Name == TEXT("Vest") && Source->GetArmor()[0].Covers.Contains(EGinkgoBodyPart::UpperChest));
					Source->Revive();
					Source->SetArmor({});
					Test.TestTrue(TEXT("客户端不能回满生命或卸下权威护甲"), FGinkgoVitals::Read(Source) == Expected);
				}
				if (Stage == 3 || Stage == 4)
				{
					Test.TestEqual(TEXT("生死状态驱动骨盆物理"), Replica->GetMesh()->IsSimulatingPhysics(TEXT("pelvis")), Source->IsDead());
					Test.TestEqual(TEXT("布娃娃接管胶囊碰撞"), Replica->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision, Source->IsDead());
					if (PC->GetPawn() != Replica)
					{
						const UGinkgoDamageableComponent* Other = PC->GetPawn()->FindComponentByClass<UGinkgoDamageableComponent>();
						Test.TestTrue(TEXT("另一位玩家的本地 HUD 仍显示自己的生命"), Other && FGinkgoVitals::Read(Other).Health > 0.f && !Other->IsDead());
					}
				}
			}
			if (!Test.TestEqual(TEXT("两个客户端分别是拥有者和旁观者"), Owners, 1)) { return true; }
			if (Stage == 1 || Stage == 2)
			{
				auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
				Ammo->Damage = Ammo->ArmorDamage = Stage == 1 ? 12.f : 10000.f;
				Ammo->ArmorPenetration = .5f;
				const auto Hit = Damage->ApplyBulletHit(*Ammo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
				Test.TestTrue(TEXT("本次命中同时消耗血量和护甲"), Hit.Outcome.HealthDamage > 0.f && Hit.Outcome.ArmorDamage > 0.f);
				Test.TestEqual(TEXT("仅第二次伤害致死"), Hit.bKilled, Stage == 2);
			}
			else if (Stage == 3)
			{
				Test.TestTrue(TEXT("死者的 HUD 生命归零，耗尽的护甲环收起"), Expected.Health == 0.f && !Expected.HasArmor());
				Damage->RestoreSaveState(64.f, 120.f, {});
			}
			else { return true; }
			++Stage;
			return false;
		}

	private:
		/** 接收两个客户端的断言，生命周期由自动化队列控制。 */
		FAutomationTestBase& Test;

		/** 整个网络场景的实际超时起点（秒）。 */
		double Started = FPlatformTime::Seconds();

		/** 依次检查初始化、带甲、受伤、死亡和原地恢复后的复制状态。 */
		int32 Stage = 0;
	};

	/** 在独立服务器激活一个可重复检查点，等待两台客户端各收到一个新增角色。 */
	class FCheckEncounterReplication : public IAutomationLatentCommand
	{
	public:
		explicit FCheckEncounterReplication(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual ~FCheckEncounterReplication() override
		{
			auto* Files = IPlatformFeaturesModule::Get().GetSaveGameSystem();
			Files->DeleteGame(false, *Slot, 0);
			Files->DeleteGame(false, *(Slot + TEXT(".json")), 0);
		}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 45.)
			{
				Test.AddError(TEXT("双客户端未能在时限内同步检查点敌人。"));
				return true;
			}
			UWorld* Server = nullptr;
			TArray<UWorld*, TInlineAllocator<2>> Clients;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (Context.WorldType != EWorldType::PIE || !World || !World->HasBegunPlay()) { continue; }
				if (World->GetNetMode() == NM_DedicatedServer) { Server = World; }
				if (World->GetNetMode() == NM_Client && World->GetFirstPlayerController()
					&& World->GetFirstPlayerController()->GetPawn()) { Clients.Add(World); }
			}
			if (!Server || Clients.Num() != 2) { return false; }
			auto CountEnemies = [](UWorld* World)
			{
				int32 Count = 0;
				for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It) { ++Count; }
				return Count;
			};
			if (Expected == INDEX_NONE)
			{
				const int32 Before = CountEnemies(Server);
				if (CountEnemies(Clients[0]) != Before || CountEnemies(Clients[1]) != Before) { return false; }
				auto* PC = Server->GetFirstPlayerController();
				auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
				if (!Player) { return false; }
				auto* Point = Server->SpawnActor<AGinkgoCheckpoint>(FVector(0., 0., 10000.), FRotator::ZeroRotator);
				Point->Announcement.LocationName = FText::FromString(TEXT("Entrance"));
				Point->Announcement.Sound.Reset();
				Point->bRepeatable = true;
				auto* Marker = Server->SpawnActor<AGinkgoCheckpointEnemySpawn>(Player->GetActorLocation()
					+ FVector(350., 250., -Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), FRotator::ZeroRotator);
				Marker->Checkpoint = Point;
				Marker->EnemyType = LoadObject<UGinkgoEnemyArchetype>(nullptr, TEXT("/Game/AI/Enemies/DA_Enemy_Heavy"));
				Marker->WeaponOverride = TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911")));
				auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(Server);
				Checkpoints->SaveSlotName = Slot;
				SpawnMarker = Marker;
				Expected = Before + 1;
				if (!Test.TestTrue(TEXT("服务器触发并重复抵达"), Point->TryActivate(Player) && Point->TryActivate(Player))
					|| !Test.TestEqual(TEXT("重复抵达只入队一个异步出生位"), Checkpoints->GetPendingEnemySpawnCount(), 1)) { return true; }
				return false;
			}
			if (!Spawned.IsValid())
			{
				auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(Server);
				if (Checkpoints && Checkpoints->HasPendingEnemySpawns()) { return false; }
				if (!Test.TestEqual(TEXT("服务器异步分帧后只生成一个敌人"), CountEnemies(Server), Expected)) { return true; }
				for (TActorIterator<AGinkgoAICharacter> It(Server); It; ++It)
				{
					if (It->PatrolRoute == SpawnMarker.Get()) { Spawned = *It; break; }
				}
				if (!Test.TestTrue(TEXT("出生位指定的手枪在服务器手中"), Spawned.IsValid() && SpawnMarker.IsValid()
					&& Spawned->GetWeapons()->GetHeldWeapon() == SpawnMarker->WeaponOverride.Get())) { return true; }
				Test.TestTrue(TEXT("专服只用源骨架，独立初始化 500 点重甲"), Spawned->GetPresentationMesh() == Spawned->GetMesh()
					&& Spawned->GetDamageable()->GetArmor().Num() == 1 && Spawned->GetDamageable()->GetArmor()[0].Durability == 500.f);
			}
			for (UWorld* Client : Clients)
			{
				if (CountEnemies(Client) != Expected) { return false; }
			}
			if (bDropped)
			{
				for (UWorld* Client : Clients)
				{
					int32 Empty = 0;
					for (TActorIterator<AGinkgoAICharacter> It(Client); It; ++It)
					{
						if (It->GetWeapons()->FindDrawSlot() == INDEX_NONE && !It->IsHeldWeaponInHand()
							&& !It->GetWeapons()->GetHoldState().Weapon && It->GetWeapons()->GetHoldState().Stance == EGinkgoHoldStance::Unarmed) { ++Empty; }
					}
					if (Empty != 1) { return false; }
				}
				return true;
			}
			// 等携带槽也到达，再检查两台客户端的实际模型挂点和供动画读取的姿势。
			for (UWorld* Client : Clients)
			{
				for (TActorIterator<AGinkgoAICharacter> It(Client); It; ++It)
				{
					if (It->GetWeapons()->FindDrawSlot() == INDEX_NONE) { return false; }
				}
			}
			auto CheckHands = [&](UWorld* World)
			{
				for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It)
				{
					const auto* Weapons = It->GetWeapons();
					Test.TestTrue(TEXT("敌人出生后武器模型可见且已挂到手中"), It->IsHeldWeaponInHand() && It->GetHeldWeaponMesh()->IsVisible());
					Test.TestTrue(TEXT("动画收到当前手持武器和持枪姿势"), Weapons->GetHeldWeapon()
						&& Weapons->GetHoldState().Weapon == Weapons->GetHeldWeapon() && Weapons->GetHoldState().Stance != EGinkgoHoldStance::Unarmed);
				}
			};
			CheckHands(Server);
			for (UWorld* Client : Clients)
			{
				CheckHands(Client);
				bool bHeavyVisible = false;
				for (TActorIterator<AGinkgoAICharacter> It(Client); It; ++It)
				{
					Test.TestEqual(TEXT("客户端敌人均为服务器的模拟代理"), It->GetLocalRole(), ROLE_SimulatedProxy);
					if (It->EnemyType == Spawned->EnemyType)
					{
						bHeavyVisible |= GetNameSafe(It->GetPresentationMesh()->GetSkeletalMeshAsset()) == TEXT("SK_Grave_Mercer");
					}
				}
				Test.TestTrue(TEXT("客户端为复制的重甲敌人显示 Grave Mercer"), bHeavyVisible);
			}
			auto* Weapons = Spawned->GetWeapons();
			Weapons->DropWeapon(Weapons->GetHeldSlot(), false);
			bDropped = true;
			return false;
		}

	private:
		/** 自动化断言的拥有者，在用例完成前有效。 */
		FAutomationTestBase& Test;

		/** 服务器和两台客户端使用真实时间共用的超时起点，单位秒。 */
		double Started = FPlatformTime::Seconds();

		/** 激活后各世界的人数，INDEX_NONE 表示仍在等待初始同步。 */
		int32 Expected = INDEX_NONE;

		/** 本用例创建的检查点敌人出生位，用于跨帧定位异步生成的权威敌人。 */
		TWeakObjectPtr<AGinkgoCheckpointEnemySpawn> SpawnMarker;

		/** 本用例由检查点生成的权威角色，待两个客户端看见手枪后验证掉落。 */
		TWeakObjectPtr<AGinkgoAICharacter> Spawned;

		/** 服务器已丢枪，正等待两端卸下模型并退出持枪动画。 */
		bool bDropped = false;

		/** 服务器使用的临时存档槽，结束时移除。 */
		FString Slot = TEXT("Automation_EncounterNet_") + FGuid::NewGuid().ToString();
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoEncounterNetworkTest, "CaseGinkgo.Checkpoint.Encounter.TwoClients",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoEncounterNetworkTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/Checkpoint/L_CheckpointTest")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<Ginkgo::CheckpointTests::FStartEncounterNetworkPIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<Ginkgo::CheckpointTests::FCheckEncounterReplication>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPlayerVitalsNetworkTest, "CaseGinkgo.Checkpoint.PlayerVitals.TwoClients",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoPlayerVitalsNetworkTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/Checkpoint/L_CheckpointTest")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<Ginkgo::CheckpointTests::FStartEncounterNetworkPIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<Ginkgo::CheckpointTests::FCheckPlayerVitalsReplication>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

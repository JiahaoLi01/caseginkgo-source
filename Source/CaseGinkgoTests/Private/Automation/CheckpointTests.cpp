#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoEnemyArchetype.h"
#include "AI/GinkgoAIConfig.h"
#include "Character/GinkgoCharacter.h"
#include "Checkpoint/GinkgoCheckpoint.h"
#include "Checkpoint/GinkgoCheckpointEnemySpawn.h"
#include "StateTree.h"
#include "Checkpoint/GinkgoCheckpointSaveNode.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/BoxComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "PlatformFeatures.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "SaveGameSystem.h"
#include "UObject/UObjectGlobals.h"
#include "UI/GinkgoLocationTitle.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "UI/GinkgoUiFontSubsystem.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace GinkgoCheckpointTests
{
	constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** 独立世界及真实存档文件；每个用例使用随机槽位，不改玩家的 Checkpoint 槽。 */
	struct FScene
	{
		/** 独立游戏实例，拥有存档和检查点子系统；析构前显式 Shutdown。 */
		UGameInstance* Instance = nullptr;

		/** 测试世界，在析构时连同 Actor 一起销毁。 */
		UWorld* World = nullptr;

		/** 真实存档系统，用于验证节点、资产引用及嵌套 SaveGame 字段。 */
		UGinkgoSaveGameSubsystem* Save = nullptr;

		/** 检查点数据拥有者。 */
		UGinkgoCheckpointSubsystem* Checkpoints = nullptr;

		/** 已接管角色的本地权威控制器。 */
		AGinkgoPlayerController* Controller = nullptr;

		/** 真实角色，初始化动作执行器后再恢复武器快照。 */
		AGinkgoCharacter* Player = nullptr;

		/** 唯一文件名；析构删除二进制和开发用 JSON。 */
		FString Slot;

		FScene()
		{
			const FString Id = FGuid::NewGuid().ToString();
			Slot = TEXT("Automation_Checkpoint_") + Id;
			Instance = NewObject<UGameInstance>(GEngine);
			Instance->AddToRoot();
			Instance->InitializeStandalone(*Id, CreatePackage(*(TEXT("/Temp/Checkpoint_") + Id)));
			World = Instance->GetWorld();
			World->InitializeActorsForPlay(FURL());
			Save = Instance->GetSubsystem<UGinkgoSaveGameSubsystem>();
			Checkpoints = Instance->GetSubsystem<UGinkgoCheckpointSubsystem>();
			Checkpoints->SaveSlotName = Slot;
			Player = World->SpawnActor<AGinkgoCharacter>(FVector(0., 0., 90.), FRotator::ZeroRotator);
			Controller = World->SpawnActor<AGinkgoPlayerController>();
			Controller->SetAsLocalPlayerController();
			Controller->Possess(Player);
			Player->DispatchBeginPlay();
			Player->GetWeapons()->StopActionStates();
		}

		~FScene()
		{
			World->BeginTearingDown();
			for (TActorIterator<AActor> Actor(World); Actor; ++Actor) { Actor->RouteEndPlay(EEndPlayReason::Quit); }
			Instance->Shutdown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			Instance->RemoveFromRoot();
			auto* Files = IPlatformFeaturesModule::Get().GetSaveGameSystem();
			Files->DeleteGame(false, *Slot, 0);
			Files->DeleteGame(false, *(Slot + TEXT(".json")), 0);
		}

		AGinkgoCheckpoint* NewCheckpoint()
		{
			auto* Point = World->SpawnActor<AGinkgoCheckpoint>(FVector(2000., 0., 0.), FRotator(0., 30., 0.));
			Point->Announcement.LocationName = FText::FromString(TEXT("旧城街区"));
			Point->Announcement.Sound.Reset();
			Point->RespawnPoint = FTransform(FRotator(0., 45., 0.), FVector(200., 0., 90.));
			return Point;
		}

		AGinkgoAICharacter* NewEnemy(const FName Id)
		{
			auto* Enemy = World->SpawnActor<AGinkgoAICharacter>(FVector(3000., 0., 90.), FRotator::ZeroRotator);
			Enemy->PersistentEnemyId = Id;
			return Enemy;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointSnapshotTest, "CaseGinkgo.Checkpoint.SnapshotRoundTrip", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointSnapshotTest::RunTest(const FString& Parameters)
{
	GinkgoCheckpointTests::FScene Scene;
	auto* Point = Scene.NewCheckpoint();
	auto* Weapons = Scene.Player->GetWeapons();
	auto* Definition = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
	if (!TestNotNull(TEXT("真实 AK 定义"), Definition)) { return false; }
	Weapons->SetCarried(0, Definition);
	FGinkgoWeaponSaveState Expected = Weapons->CaptureSaveState();
	auto& Slot = Expected.Slots[0];
	const FGameplayTag Ammo = Slot.LoadAmmo;
	TestTrue(TEXT("默认弹种已初始化"), Ammo.IsValid());
	Slot.Mechanism.Feed = {Ammo, Ammo, Ammo};
	Slot.Mechanism.Chamber = EGinkgoChamber::Spent;
	Slot.Mechanism.ChamberAmmo = Ammo;
	Slot.Mechanism.Action = EGinkgoAction::Open;
	Slot.Mechanism.bMagazineIn = false;
	// 安装一个真实可选件，验证嵌套 UObject 引用保存，不只覆盖默认配件。
	if (!Definition->Attachments.IsEmpty()) { Slot.Attachments.Parts.AddUnique(Definition->Attachments.Last().Part); }
	Expected.Reserve.Rounds.Reset();
	Expected.Reserve.Rounds.Add(Ammo, 17);
	Expected.HeldSlot = INDEX_NONE;
	Expected.LastHeldSlot = 0;
	TestTrue(TEXT("设置非默认机构/收枪状态"), Weapons->RestoreSaveState(Expected));
	FGinkgoArmorPiece Armor;
	Armor.Name = TEXT("CheckpointVest");
	Armor.Covers = {EGinkgoBodyPart::UpperChest, EGinkgoBodyPart::Abdomen};
	Armor.MaxDurability = 150.f;
	Armor.Durability = 37.f;
	Scene.Player->GetDamageable()->RestoreSaveState(43.f, 120.f, {Armor});
	TestTrue(TEXT("激活立即完成写档"), Point->TryActivate(Scene.Player));
	TestTrue(TEXT("大字还没播放就已写入文件"), IPlatformFeaturesModule::Get().GetSaveGameSystem()->DoesSaveGameExist(*Scene.Slot, 0));
	TestTrue(TEXT("触发帧 HUD 已被隐藏"), Scene.Controller->IsGameplayHudSuppressed());
	TestFalse(TEXT("同一个检查点默认不重复触发"), Point->TryActivate(Scene.Player));
	Weapons->SetCarried(0, nullptr);
	Scene.Player->GetDamageable()->RestoreSaveState(5.f, 100.f, {});
	Scene.Player->SetActorLocation(FVector(500., 500., 100.));
	TestTrue(TEXT("节点从真实二进制存档加载"), Scene.Save->Load(Scene.Slot));
	Scene.Checkpoints->RestorePendingPlayer(*Scene.Controller);
	TestEqual(TEXT("恢复剩余生命而非补满"), Scene.Player->GetDamageable()->GetHealth(), 43.f);
	TestEqual(TEXT("生命上限"), Scene.Player->GetDamageable()->GetMaxHealth(), 120.f);
	const auto& RestoredArmor = Scene.Player->GetDamageable()->GetArmor();
	if (TestEqual(TEXT("护甲件数"), RestoredArmor.Num(), 1))
	{
		TestEqual(TEXT("剩余耐久"), RestoredArmor[0].Durability, 37.f);
		TestEqual(TEXT("护甲名称"), RestoredArmor[0].Name, Armor.Name);
		TestTrue(TEXT("覆盖部位"), RestoredArmor[0].Covers == Armor.Covers);
	}
	TestTrue(TEXT("位于配置复活点"), Scene.Player->GetActorLocation().Equals(Point->GetRespawnTransform().GetLocation(), .01));
	const auto Actual = Weapons->CaptureSaveState();
	TestEqual(TEXT("武器资产"), Actual.Slots[0].Definition.Get(), Definition);
	TestTrue(TEXT("配件资产引用"), Actual.Slots[0].Attachments.Parts == Expected.Slots[0].Attachments.Parts);
	TestTrue(TEXT("逐发弹种和排列"), Actual.Slots[0].Mechanism.Feed == Slot.Mechanism.Feed);
	TestEqual(TEXT("空壳不会变成活弹"), Actual.Slots[0].Mechanism.Chamber, EGinkgoChamber::Spent);
	TestEqual(TEXT("枪机保持开着"), Actual.Slots[0].Mechanism.Action, EGinkgoAction::Open);
	TestFalse(TEXT("不会偷偷插回弹匣"), Actual.Slots[0].Mechanism.bMagazineIn);
	TestEqual(TEXT("后备不叠加开局赠弹"), Actual.Reserve.Count(Ammo), 17);
	Weapons->UpdateHoldState(.016f);
	TestEqual(TEXT("首帧不会强制拔出已收好的枪"), Weapons->GetHeldSlot(), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoEncounterSpawnTest, "CaseGinkgo.Checkpoint.Encounter.SpawnAndRestore", GinkgoCheckpointTests::Flags)
bool FGinkgoEncounterSpawnTest::RunTest(const FString& Parameters)
{
	GinkgoCheckpointTests::FScene Scene;
	auto* Entrance = Scene.NewCheckpoint();
	auto* Plaza = Scene.NewCheckpoint();
	Entrance->bRepeatable = true;
	auto AddSpawn = [&](AGinkgoCheckpoint* Point, double Y)
	{
		auto* Marker = Scene.World->SpawnActor<AGinkgoCheckpointEnemySpawn>(FVector(4000., Y, 0.), FRotator(0., 90., 0.));
		Marker->Checkpoint = Point;
		return Marker;
	};
	auto EnemiesFor = [&](AGinkgoCheckpointEnemySpawn* Marker)
	{
		TArray<AGinkgoAICharacter*> Found;
		for (TActorIterator<AGinkgoAICharacter> It(Scene.World); It; ++It)
		{
			if (It->PatrolRoute == Marker) { Found.Add(*It); }
		}
		return Found;
	};
	auto* First = AddSpawn(Entrance, 0.);
	auto* Second = AddSpawn(Entrance, 800.);
	auto* Later = AddSpawn(Plaza, 1600.);
	auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911"));
	if (!TestNotNull(TEXT("测试武器可用"), Weapon)) { return false; }
	First->WeaponOverride = Weapon;
	First->EnemyType = LoadObject<UGinkgoEnemyArchetype>(nullptr, TEXT("/Game/AI/Enemies/DA_Enemy_Light"));
	auto& Reference = First->BehaviorOverrides.Reference;
	Reference.SetStateTree(LoadObject<UStateTree>(nullptr, TEXT("/Game/AI/ST_Enemy_SprintToRange")));
	const FName Range(TEXT("SprintFireRangeMeters"));
	const auto* Parameter = Reference.GetParameters().FindPropertyDescByName(Range);
	if (!TestNotNull(TEXT("冲刺树声明射击距离"), Parameter)) { return false; }
	Reference.SetPropertyOverridden(Parameter->ID, true);
	Reference.GetMutableParameters().SetValueFloat(Range, 8.f);
	auto HasSpawnParameters = [&](const FStateTreeReference& Instance)
	{
		return Instance.GetStateTree() == Reference.GetStateTree() && Instance.IsPropertyOverridden(Parameter->ID)
			&& Instance.GetParameters().GetValueFloat(Range).GetValue() == 8.f;
	};
	First->Points.AddDefaulted_GetRef().Location = FVector(200., 0., 0.);
	First->Points[0].WaitSeconds = 2.f;
	auto StepAsyncSpawnFrame = [&]()
	{
		// 独立无 Tick 世界里先推进后台异步包加载，再执行单帧预算落地；真实游戏由引擎与 Ticker 自动逐帧推进。
		FlushAsyncLoading();
		Scene.Checkpoints->ProcessPendingEnemySpawns();
	};
	auto DrainAsyncSpawns = [&](const int32 MaxFrames = 16)
	{
		for (int32 Frame = 0; Frame < MaxFrames && Scene.Checkpoints->HasPendingEnemySpawns(); ++Frame)
		{
			StepAsyncSpawnFrame();
		}
	};
	TestEqual(TEXT("抵达前不生成"), EnemiesFor(First).Num(), 0);
	TestTrue(TEXT("抵达入口"), Entrance->TryActivate(Scene.Player));
	TestEqual(TEXT("抵达当帧不阻塞同步生成敌人"), EnemiesFor(First).Num() + EnemiesFor(Second).Num(), 0);
	TestEqual(TEXT("入口两名敌人已进入异步分帧队列"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 2);

	// 推进第一帧：预载完成后按距离优先仅落地更近的 First（Y=0），Second（Y=800）留待下一帧。
	StepAsyncSpawnFrame();
	if (EnemiesFor(First).Num() == 0 && Scene.Checkpoints->HasPendingEnemySpawns())
	{
		// 若首跳异步预载需级联多跳，再推进一次完成预载后的首帧落地。
		StepAsyncSpawnFrame();
	}
	if (!TestEqual(TEXT("首帧优先落地距离更近的第一个出生位"), EnemiesFor(First).Num(), 1)
		|| !TestEqual(TEXT("每帧上限 1 生效，第二个出生位推迟到下一帧"), EnemiesFor(Second).Num(), 0)) { return false; }

	// 推进第二帧：落地剩余的 Second。
	StepAsyncSpawnFrame();
	auto Found = EnemiesFor(First);
	if (!TestEqual(TEXT("入口第一个出生位生效"), Found.Num(), 1)
		|| !TestEqual(TEXT("第二帧落地同一入口的第二个出生位"), EnemiesFor(Second).Num(), 1)
		|| !TestFalse(TEXT("全部落地后待生成队列清空"), Scene.Checkpoints->HasPendingEnemySpawns())) { return false; }
	auto* Original = Found[0];
	TestTrue(TEXT("生成时应用敌人的行为覆盖"), HasSpawnParameters(Original->BehaviorOverrides.Reference));
	if (!Original->HasActorBegunPlay()) { Original->DispatchBeginPlay(); }
	TestTrue(TEXT("出生位生成的轻甲守卫拥有一件满耐久胸甲"), Original->GetDamageable()->GetArmor().Num() == 1
		&& Original->GetDamageable()->GetArmor()[0].Durability == 100.f);
	TestEqual(TEXT("实际持有出生位指定武器"), Original->GetWeapons()->GetHeldWeapon(), Weapon);
	TestTrue(TEXT("手枪出生时直接挂手并从低持进入动画"), Original->IsHeldWeaponInHand()
		&& Original->GetWeapons()->GetHoldState().Stance == EGinkgoHoldStance::Lowered
		&& Original->GetWeapons()->GetHoldState().Weapon == Weapon);
	auto* DefaultGuard = EnemiesFor(Second)[0];
	if (!DefaultGuard->HasActorBegunPlay()) { DefaultGuard->DispatchBeginPlay(); }
	TestTrue(TEXT("未覆盖装备的守卫直接手持 AI 默认配枪"), DefaultGuard->IsHeldWeaponInHand()
		&& DefaultGuard->Definition.IsValid() && DefaultGuard->GetWeapons()->GetHeldWeapon() == DefaultGuard->Definition->Weapon.Get()
		&& DefaultGuard->GetWeapons()->GetHoldState().Weapon == DefaultGuard->GetWeapons()->GetHeldWeapon());
	TestTrue(TEXT("出生朝向沿用标记的偏航"), FMath::IsNearlyEqual(Original->GetActorRotation().Yaw, 90., .1));
	TestTrue(TEXT("巡逻点随标记旋转到世界坐标"), Original->PatrolRoute->GetPointLocation(0).Equals(FVector(4000., 200., 0.), .01));
	TestEqual(TEXT("另一组未提前生成"), EnemiesFor(Later).Num(), 0);
	TestTrue(TEXT("重复抵达可保存"), Entrance->TryActivate(Scene.Player));
	TestFalse(TEXT("存活敌人的出生位重复抵达不重新入队"), Scene.Checkpoints->HasPendingEnemySpawns());
	Found = EnemiesFor(First);
	if (!TestEqual(TEXT("重复激活不增加人数"), Found.Num(), 1)) { return false; }
	TestEqual(TEXT("存活敌人保留原实例"), Found[0], Original);
	const FName Id = Original->PersistentEnemyId;
	Scene.Checkpoints->RecordEnemyDeath(*Original);
	Original->Destroy();
	TestTrue(TEXT("载入击杀前的存档"), Scene.Save->Load(Scene.Slot));
	DrainAsyncSpawns();
	Found = EnemiesFor(First);
	if (!TestEqual(TEXT("未保存的击杀回滚"), Found.Num(), 1)) { return false; }
	TestEqual(TEXT("重建后仍为同一持久身份"), Found[0]->PersistentEnemyId, Id);
	TestTrue(TEXT("读档重建使用出生位的同一敌人类型"), Found[0]->EnemyType == First->EnemyType);
	TestTrue(TEXT("读档重建保留出生位的决策树和距离"), HasSpawnParameters(Found[0]->BehaviorOverrides.Reference));
	Scene.Checkpoints->RecordEnemyDeath(*Found[0]);
	Found[0]->Destroy();
	TestTrue(TEXT("下一处检查点保存已有击杀"), Plaza->TryActivate(Scene.Player));
	DrainAsyncSpawns();
	TestEqual(TEXT("新组在抵达时生成"), EnemiesFor(Later).Num(), 1);
	TestTrue(TEXT("重新加载两组进度"), Scene.Save->Load(Scene.Slot));
	DrainAsyncSpawns();
	TestEqual(TEXT("已保存的击杀不复生"), EnemiesFor(First).Num(), 0);
	TestEqual(TEXT("入口存活敌人被还原"), EnemiesFor(Second).Num(), 1);
	TestEqual(TEXT("新组存活敌人被还原"), EnemiesFor(Later).Num(), 1);
	auto* Streamed = AddSpawn(Plaza, 2400.);
	Streamed->DispatchBeginPlay();
	TestEqual(TEXT("延迟流送进入世界的出生位先入队不阻塞当帧"), EnemiesFor(Streamed).Num(), 0);
	TestEqual(TEXT("延迟流送出生位已进入待生成队列"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 1);
	DrainAsyncSpawns();
	TestEqual(TEXT("已抵达组的出生位延迟进入世界时也异步生效"), EnemiesFor(Streamed).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointAsyncSpawnQueueTest, "CaseGinkgo.Checkpoint.Encounter.AsyncFrameSlicedQueue", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointAsyncSpawnQueueTest::RunTest(const FString& Parameters)
{
	GinkgoCheckpointTests::FScene Scene;
	auto* Point = Scene.NewCheckpoint();
	Point->bRepeatable = true;

	auto AddSpawn = [&](const double X)
	{
		auto* Marker = Scene.World->SpawnActor<AGinkgoCheckpointEnemySpawn>(FVector(X, 0., 0.), FRotator::ZeroRotator);
		Marker->Checkpoint = Point;
		return Marker;
	};
	auto HasSpawned = [&](const AGinkgoCheckpointEnemySpawn* Marker)
	{
		for (TActorIterator<AGinkgoAICharacter> It(Scene.World); It; ++It)
		{
			if (It->PatrolRoute == Marker) { return true; }
		}
		return false;
	};

	// 故意乱序创建 4 个不同距离的出生位，验证队列按距离玩家由近到远排序并每帧严格落地 1 人。
	auto* Far = AddSpawn(3200.);
	auto* Near = AddSpawn(800.);
	auto* MidFar = AddSpawn(2400.);
	auto* MidNear = AddSpawn(1600.);

	TestTrue(TEXT("首次激活检查点入队 4 名敌人"), Point->TryActivate(Scene.Player));
	TestEqual(TEXT("激活当帧 4 名敌人均在排队"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 4);
	TestFalse(TEXT("激活当帧不直接同步生成最近敌人"), HasSpawned(Near));

	// 尚未落地前重复激活可重复检查点，不得重复入队。
	TestTrue(TEXT("排队期间重复激活允许保存"), Point->TryActivate(Scene.Player));
	TestEqual(TEXT("重复激活不会令同一出生位重复入队"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 4);

	// 队列中途销毁 MidFar（模拟子关卡卸载），队列应立刻取消该条目。
	MidFar->Destroy();
	TestEqual(TEXT("销毁待生成出生位后队列长度立即减 1"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 3);

	auto StepUntilSpawn = [&](const int32 ExpectedCountAfter)
	{
		for (int32 Try = 0; Try < 6 && Scene.Checkpoints->GetPendingEnemySpawnCount() > ExpectedCountAfter; ++Try)
		{
			FlushAsyncLoading();
			Scene.Checkpoints->ProcessPendingEnemySpawns();
		}
	};

	// 第 1 个落地必须是最近的 Near
	StepUntilSpawn(2);
	TestTrue(TEXT("第 1 帧优先落地最近的出生位 Near"), HasSpawned(Near));
	TestFalse(TEXT("第 1 帧尚未落地 MidNear"), HasSpawned(MidNear));
	TestFalse(TEXT("第 1 帧尚未落地 Far"), HasSpawned(Far));
	TestEqual(TEXT("第 1 帧后剩余 2 名待生成"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 2);

	// 第 2 个落地必须是次近的 MidNear
	StepUntilSpawn(1);
	TestTrue(TEXT("第 2 帧按距离次序落地 MidNear"), HasSpawned(MidNear));
	TestFalse(TEXT("第 2 帧尚未落地 Far"), HasSpawned(Far));
	TestEqual(TEXT("第 2 帧后剩余 1 名待生成"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 1);

	// 队列仍有剩余时重开新游戏，必须立即清空取消，后续推进不再落地 Far
	Scene.Checkpoints->ClearCheckpointForNewGame();
	TestFalse(TEXT("新游戏重置立即清空待生成队列"), Scene.Checkpoints->HasPendingEnemySpawns());
	Scene.Checkpoints->ProcessPendingEnemySpawns();
	TestFalse(TEXT("取消后最远出生位 Far 不再生成"), HasSpawned(Far));

	// 验证可配置每帧上限 MaxEnemySpawnsPerFrame = 2 时单帧落地 2 人
	auto* PointBatch = Scene.NewCheckpoint();
	auto* BatchA = Scene.World->SpawnActor<AGinkgoCheckpointEnemySpawn>(FVector(1000., 600., 0.), FRotator::ZeroRotator);
	auto* BatchB = Scene.World->SpawnActor<AGinkgoCheckpointEnemySpawn>(FVector(1400., 600., 0.), FRotator::ZeroRotator);
	auto* BatchC = Scene.World->SpawnActor<AGinkgoCheckpointEnemySpawn>(FVector(1800., 600., 0.), FRotator::ZeroRotator);
	BatchA->Checkpoint = PointBatch;
	BatchB->Checkpoint = PointBatch;
	BatchC->Checkpoint = PointBatch;
	Scene.Checkpoints->MaxEnemySpawnsPerFrame = 2;
	Scene.Checkpoints->MaxEnemySpawnFrameBudgetMs = 500.f;
	TestTrue(TEXT("批量测试检查点激活成功"), PointBatch->TryActivate(Scene.Player));
	TestEqual(TEXT("批量测试入队 3 名敌人"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 3);
	FlushAsyncLoading();
	Scene.Checkpoints->ProcessPendingEnemySpawns();
	if (Scene.Checkpoints->GetPendingEnemySpawnCount() == 3)
	{
		FlushAsyncLoading();
		Scene.Checkpoints->ProcessPendingEnemySpawns();
	}
	TestTrue(TEXT("每帧上限 2 时首帧落地 BatchA 与 BatchB"), HasSpawned(BatchA) && HasSpawned(BatchB));
	TestFalse(TEXT("第 3 名敌人 BatchC 留待下一帧"), HasSpawned(BatchC));
	TestEqual(TEXT("首帧后队列剩余 1 人"), Scene.Checkpoints->GetPendingEnemySpawnCount(), 1);
	FlushAsyncLoading();
	Scene.Checkpoints->ProcessPendingEnemySpawns();
	TestTrue(TEXT("下一帧落地 BatchC 并清空队列"), HasSpawned(BatchC) && !Scene.Checkpoints->HasPendingEnemySpawns());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointEnemiesTest, "CaseGinkgo.Checkpoint.EnemyProgress", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointEnemiesTest::RunTest(const FString& Parameters)
{
	GinkgoCheckpointTests::FScene Scene;
	auto* Before = Scene.NewEnemy(TEXT("BeforeCheckpoint"));
	auto* After = Scene.NewEnemy(TEXT("AfterCheckpoint"));
	auto* Unstable = Scene.NewEnemy(NAME_None);
	Scene.Checkpoints->RecordEnemyDeath(*Unstable);
	TestFalse(TEXT("没有稳定 ID 的动态敌人不污染进度"), Scene.Checkpoints->IsEnemyDefeated(*Unstable));
	Scene.Checkpoints->RecordEnemyDeath(*Before);
	Scene.Checkpoints->RecordEnemyDeath(*Before);
	TestEqual(TEXT("重复死亡只记一次"), Scene.Checkpoints->GetState().DefeatedEnemies.Num(), 1);
	TestTrue(TEXT("保存第一名敌人"), Scene.NewCheckpoint()->TryActivate(Scene.Player));
	Scene.Checkpoints->RecordEnemyDeath(*After);
	TestTrue(TEXT("存档后死亡进入工作状态"), Scene.Checkpoints->IsEnemyDefeated(*After));
	TestTrue(TEXT("恢复旧进度"), Scene.Save->Load(Scene.Slot));
	TestTrue(TEXT("存档前的敌人被移除"), Before->IsActorBeingDestroyed());
	TestFalse(TEXT("存档后的敌人不被误删"), After->IsActorBeingDestroyed());
	TestFalse(TEXT("存档后击杀回滚"), Scene.Checkpoints->IsEnemyDefeated(*After));
	TestTrue(TEXT("移除敌人不会污染通用关卡销毁节点"), Scene.Save->GetDestroyedLevelActors().IsEmpty());
	auto* StreamedAgain = Scene.NewEnemy(TEXT("BeforeCheckpoint"));
	Scene.Checkpoints->RemoveDefeatedEnemy(*StreamedAgain);
	TestTrue(TEXT("后续流送/生成的同一敌人也保持死亡"), StreamedAgain->IsActorBeingDestroyed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointMissingNodeTest, "CaseGinkgo.Checkpoint.LegacySave", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointMissingNodeTest::RunTest(const FString& Parameters)
{
	GinkgoCheckpointTests::FScene Scene;
	Scene.Save->GetRegistry().Unregister(FGinkgoCheckpointSaveNode::NodeGuid);
	TestTrue(TEXT("写旧版无检查点节点存档"), Scene.Save->Save(Scene.Slot));
	Scene.Save->GetRegistry().Register(MakeShared<FGinkgoCheckpointSaveNode>());
	Scene.Checkpoints->RecordEnemyDeath(*Scene.NewEnemy(TEXT("Unsaved")));
	TestTrue(TEXT("旧版存档可以读取"), Scene.Save->Load(Scene.Slot));
	TestFalse(TEXT("无检查点复活点"), Scene.Checkpoints->GetState().bHasCheckpoint);
	TestTrue(TEXT("缺失节点清空旧的工作进度"), Scene.Checkpoints->GetState().DefeatedEnemies.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointTitleTest, "CaseGinkgo.Checkpoint.TitleTiming", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointTitleTest::RunTest(const FString& Parameters)
{
	FGinkgoLocationTitleQueue Queue;
	FGinkgoCheckpointAnnouncement A;
	FGinkgoLocationTitleSettings Settings;
	A.LocationName = FText::FromString(TEXT("广场"));
	Settings.FadeIn = .5f;
	Settings.Hold = 2.f;
	Settings.FadeOut = .5f;
	A.bSaved = true;
	Queue.Push(A, Settings);
	TestTrue(TEXT("尚未淡入也立即压住 HUD"), Queue.IsActive());
	TestEqual(TEXT("只开始一次"), Queue.Advance(0.f), FGinkgoLocationTitleQueue::EStep::Begin);
	TestEqual(TEXT("开始透明"), Queue.GetOpacity(), 0.f);
	Queue.Advance(.5f);
	TestEqual(TEXT("淡入完成"), Queue.GetOpacity(), 1.f);
	Queue.Advance(2.f);
	TestTrue(TEXT("淡出前不能出存档提示"), Queue.IsActive());
	TestEqual(TEXT("暂停不给时间不会结束"), Queue.Advance(0.f), FGinkgoLocationTitleQueue::EStep::None);
	A.bSaved = false;
	Queue.Push(A, Settings);
	TestTrue(TEXT("当前结果不会被下一次写档失败覆盖"), Queue.GetCurrent()->bSaved);
	TestEqual(TEXT("淡出完才返回结束事件"), Queue.Advance(.5f), FGinkgoLocationTitleQueue::EStep::Finish);
	TestTrue(TEXT("相邻地点间不闪回 HUD"), Queue.IsActive());
	TestEqual(TEXT("排队开始下一处"), Queue.Advance(0.f), FGinkgoLocationTitleQueue::EStep::Begin);
	TestFalse(TEXT("失败结果独立传给提示"), Queue.GetCurrent()->bSaved);
	TestEqual(TEXT("低帧率也只结束一次"), Queue.Advance(10.f), FGinkgoLocationTitleQueue::EStep::Finish);
	TestFalse(TEXT("队列结束归还 HUD"), Queue.IsActive());
	TestEqual(TEXT("结束后不会重复弹提示"), Queue.Advance(10.f), FGinkgoLocationTitleQueue::EStep::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointInstantTitleTest, "CaseGinkgo.Checkpoint.ZeroFadeAndSharedSettings", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointInstantTitleTest::RunTest(const FString& Parameters)
{
	FGinkgoCheckpointAnnouncement Announcement;
	Announcement.LocationName = FText::FromString(TEXT("San Lorenzo"));
	for (const FVector2f Durations : {FVector2f(0.f, 0.f), FVector2f(0.f, .5f), FVector2f(.5f, 0.f)})
	{
		FGinkgoLocationTitleSettings Settings;
		Settings.Hold = 2.f;
		Settings.FadeIn = Durations.X;
		Settings.FadeOut = Durations.Y;
		FGinkgoLocationTitleQueue Queue;
		Queue.Push(Announcement, Settings);
		// 入队后的项目设置修改不能改变已经在显示/排队的标题。
		Settings.FadeIn = 20.f;
		Settings.FontFamily = TEXT("Unrelated");
		TestEqual(TEXT("开始时立即发出 Begin"), Queue.Advance(0.f), FGinkgoLocationTitleQueue::EStep::Begin);
		TestEqual(TEXT("零淡入首帧就是完全不透明"), Queue.GetOpacity(), Durations.X == 0.f ? 1.f : 0.f);
		TestTrue(TEXT("零时长不会产生 NaN"), FMath::IsFinite(Queue.GetOpacity()));
		TestEqual(TEXT("显示快照不受后续修改影响"), Queue.GetSettings()->FontFamily, FString(TEXT("Barlow Condensed")));
		Queue.Advance(Durations.X + 2.f - .25f);
		TestEqual(TEXT("停留段完整显示"), Queue.GetOpacity(), 1.f);
		if (Durations.Y > 0.f)
		{
			Queue.Advance(.5f);
			TestTrue(TEXT("单独淡出仍有中间透明度"), Queue.GetOpacity() > 0.f && Queue.GetOpacity() < 1.f);
			TestEqual(TEXT("正常淡出结束"), Queue.Advance(.25f), FGinkgoLocationTitleQueue::EStep::Finish);
		}
		else
		{
			TestEqual(TEXT("零淡出精确在停留终点结束，无最小时长尾巴"), Queue.Advance(.25f), FGinkgoLocationTitleQueue::EStep::Finish);
		}
		TestFalse(TEXT("结束立即归还 HUD"), Queue.IsActive());
		TestEqual(TEXT("结束没有残留透明文字"), Queue.GetOpacity(), 0.f);
	}
	TestNull(TEXT("逐点淡入属性已经移除"), FGinkgoCheckpointAnnouncement::StaticStruct()->FindPropertyByName(TEXT("FadeIn")));
	TestNull(TEXT("逐点淡出属性已经移除"), FGinkgoCheckpointAnnouncement::StaticStruct()->FindPropertyByName(TEXT("FadeOut")));
	TestNull(TEXT("逐点停留属性已经移除"), FGinkgoCheckpointAnnouncement::StaticStruct()->FindPropertyByName(TEXT("Hold")));
	TestEqual(TEXT("默认共用停留五秒"), FGinkgoLocationTitleSettings().Hold, 5.f);
	FGinkgoLocationTitleQueue FiveSeconds;
	FGinkgoLocationTitleSettings SharedSettings;
	FiveSeconds.Push(Announcement, SharedSettings);
	SharedSettings.Hold = 30.f;
	FiveSeconds.Advance(0.f);
	TestEqual(TEXT("五秒前不会提前结束"), FiveSeconds.Advance(4.99f), FGinkgoLocationTitleQueue::EStep::None);
	TestEqual(TEXT("恰好五秒结束，修改项目设置不影响已入队快照"), FiveSeconds.Advance(.01f), FGinkgoLocationTitleQueue::EStep::Finish);
	const FProperty* Shared = UGinkgoDeveloperSettings::StaticClass()->FindPropertyByName(TEXT("LocationTitle"));
	TestTrue(TEXT("项目设置持久保存共用配置"), Shared && Shared->HasAnyPropertyFlags(CPF_Config));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointFontTest, "CaseGinkgo.Checkpoint.BundledFonts", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointFontTest::RunTest(const FString& Parameters)
{
	auto* Fonts = GEngine->GetEngineSubsystem<UGinkgoUiFontSubsystem>();
	if (TestNotNull(TEXT("字体在引擎启动时注册"), Fonts))
	{
		TestEqual(TEXT("三种字重的正体与斜体均被 Weft 接受"), Fonts->GetRegisteredFaceCount(), 6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointFailureTest, "CaseGinkgo.Checkpoint.ActivationAndFailure", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointFailureTest::RunTest(const FString& Parameters)
{
	GinkgoCheckpointTests::FScene Scene;
	auto* Point = Scene.NewCheckpoint();
	TestFalse(TEXT("没有玩家不能激活"), Point->TryActivate(nullptr));
	TestFalse(TEXT("AI 不能占用玩家存档"), Point->TryActivate(Scene.NewEnemy(TEXT("AI"))));
	const FText Location = Point->Announcement.LocationName;
	Point->Announcement.LocationName = FText::GetEmpty();
	TestFalse(TEXT("空地点拒绝触发"), Point->TryActivate(Scene.Player));
	Point->Announcement.LocationName = Location;
	// 问号不是合法 Windows 文件名；只尝试本用例槽位下的文件，不覆盖玩家存档。
	Scene.Checkpoints->SaveSlotName = Scene.Slot + TEXT("?Invalid");
	TestFalse(TEXT("写档失败明确返回失败"), Point->TryActivate(Scene.Player));
	TestFalse(TEXT("失败不替换已有复活点"), Scene.Checkpoints->GetState().bHasCheckpoint);
	TestTrue(TEXT("失败不标记已抵达，允许重试"), Scene.Checkpoints->GetState().ReachedCheckpoints.IsEmpty());
	Scene.Checkpoints->SaveSlotName = Scene.Slot;
	TestTrue(TEXT("失败后可以重试成功"), Point->TryActivate(Scene.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCheckpointIdentityTest, "CaseGinkgo.Checkpoint.StreamingIdentity", GinkgoCheckpointTests::Flags)
bool FGinkgoCheckpointIdentityTest::RunTest(const FString& Parameters)
{
	GinkgoCheckpointTests::FScene Scene;
	const FString Suffix = FGuid::NewGuid().ToString();
	ULevel* Left = NewObject<ULevel>(CreatePackage(*(TEXT("/Temp/Left_") + Suffix)), TEXT("PersistentLevel"));
	ULevel* Right = NewObject<ULevel>(CreatePackage(*(TEXT("/Temp/Right_") + Suffix)), TEXT("PersistentLevel"));
	Left->OwningWorld = Scene.World;
	Right->OwningWorld = Scene.World;
	FActorSpawnParameters Params;
	Params.Name = TEXT("SameEnemyName");
	Params.OverrideLevel = Left;
	auto* A = Scene.World->SpawnActor<AGinkgoAICharacter>(Params);
	Params.OverrideLevel = Right;
	auto* B = Scene.World->SpawnActor<AGinkgoAICharacter>(Params);
	TestNotEqual(TEXT("子关卡同名敌人有不同身份"), UGinkgoCheckpointSubsystem::MakeStableId(*A), UGinkgoCheckpointSubsystem::MakeStableId(*B));
	A->SetFlags(RF_WasLoaded);
	Scene.Checkpoints->RecordEnemyDeath(*A);
	TestTrue(TEXT("已加载关卡敌人无需手写 ID"), Scene.Checkpoints->IsEnemyDefeated(*A));
	TestFalse(TEXT("不会误杀另一子关卡的同名对象"), Scene.Checkpoints->IsEnemyDefeated(*B));
	A->Destroy();
	B->Destroy();
	return true;
}

#endif

#include "AI/GinkgoAICharacter.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoPracticeTarget.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/GinkgoGameMode.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/GinkgoWorkbench.h"
#include "Misc/AutomationTest.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"
#include "Weapon/GinkgoWeaponPickup.h"
#include "Weapon/GinkgoWeaponSaveState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoGameModeFormalLoadoutTest, "CaseGinkgo.Framework.GameMode.FormalLoadout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoGameModeFormalLoadoutTest::RunTest(const FString& Parameters)
{
	// 1. 核对正式游戏模式类默认配置：仅配 M4 与 M1911，各 2 个弹匣后备
	const AGinkgoGameMode* Defaults = GetDefault<AGinkgoGameMode>();
	if (!TestNotNull(TEXT("正式游戏模式 CDO 有效"), Defaults))
	{
		return false;
	}
	TestEqual(TEXT("默认 Pawn 为 AGinkgoCharacter"), Defaults->DefaultPawnClass.Get(), AGinkgoCharacter::StaticClass());
	TestEqual(TEXT("默认控制器为 AGinkgoPlayerController"), Defaults->PlayerControllerClass.Get(), AGinkgoPlayerController::StaticClass());
	const TArray<FGinkgoStartingWeaponEntry>& Loadout = Defaults->GetStartingLoadout();
	if (!TestEqual(TEXT("默认开局配装恰好包含 2 把武器"), Loadout.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("第一把开局武器为 M4"), Loadout[0].Weapon.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Weapons/M4/DA_M4.DA_M4")));
	TestEqual(TEXT("M4 默认配 2 个弹匣后备"), Loadout[0].ReserveMagazines, 2);
	TestEqual(TEXT("第二把开局武器为 M1911"), Loadout[1].Weapon.ToSoftObjectPath().ToString(), FString(TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911")));
	TestEqual(TEXT("M1911 默认配 2 个弹匣后备"), Loadout[1].ReserveMagazines, 2);

	auto* M4 = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
	auto* M1911 = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911"));
	if (!TestNotNull(TEXT("M4 资产可加载"), M4) || !TestNotNull(TEXT("M1911 资产可加载"), M1911))
	{
		return false;
	}

	// 2. 在干净独立世界中挂载正式 AGinkgoGameMode 并执行完整 RestartPlayer 流程
	const FString WorldName = TEXT("GameModeFormalTest") + FGuid::NewGuid().ToString();
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*WorldName, CreatePackage(*(TEXT("/Temp/") + WorldName)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		Instance->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		Instance->RemoveFromRoot();
	};

	World->GetWorldSettings()->DefaultGameMode = AGinkgoGameMode::StaticClass();
	World->SetGameMode(FURL());
	auto* GameMode = World->GetAuthGameMode<AGinkgoGameMode>();
	auto* Start = World->SpawnActor<APlayerStart>(FVector(0., 0., 120.), FRotator::ZeroRotator);
	auto* PC = World->SpawnActor<AGinkgoPlayerController>();
	if (!TestNotNull(TEXT("世界装配正式游戏模式"), GameMode) || !TestNotNull(TEXT("生成出生点"), Start) || !TestNotNull(TEXT("生成玩家控制器"), PC))
	{
		return false;
	}
	PC->SetAsLocalPlayerController();
	GameMode->RestartPlayer(PC);

	auto* Player = Cast<AGinkgoCharacter>(PC->GetPawn());
	if (!TestNotNull(TEXT("RestartPlayer 生成并附身玩家角色"), Player))
	{
		return false;
	}
	if (!Player->HasActorBegunPlay())
	{
		Player->DispatchBeginPlay();
	}

	// 3. 验证不生成任何调试靶子、调试敌人、地面武器或预览改装桌
	int32 TargetCount = 0;
	for (TActorIterator<AGinkgoPracticeTarget> It(World); It; ++It) { ++TargetCount; }
	int32 EnemyCount = 0;
	for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It) { ++EnemyCount; }
	int32 PickupCount = 0;
	for (TActorIterator<AGinkgoWeaponPickup> It(World); It; ++It) { ++PickupCount; }
	int32 WorkbenchCount = 0;
	for (TActorIterator<AGinkgoWorkbench> It(World); It; ++It) { ++WorkbenchCount; }

	TestEqual(TEXT("不生成调试人形靶"), TargetCount, 0);
	TestEqual(TEXT("不生成调试敌人"), EnemyCount, 0);
	TestEqual(TEXT("不生成调试地面武器"), PickupCount, 0);
	TestEqual(TEXT("不生成调试预览改装桌"), WorkbenchCount, 0);

	// 4. 验证武器槽：只发放 M4（主槽 1）与 M1911（手枪槽），其余槽位为空
	UGinkgoWeaponComponent* Weapons = Player->GetWeapons();
	if (!TestNotNull(TEXT("玩家持有武器组件"), Weapons))
	{
		return false;
	}
	TestEqual(TEXT("主武器槽 1 为 M4"), Weapons->GetCarried(0), M4);
	TestNull(TEXT("主武器槽 2 为空（不再发放 98k / AK-47）"), Weapons->GetCarried(1));
	TestEqual(TEXT("手枪槽为 M1911"), Weapons->GetCarried(2), M1911);
	TestNull(TEXT("重武器槽为空"), Weapons->GetCarried(3));

	// 5. 验证弹药：枪内满匣 + 精确 2 个弹匣后备，不含多余弹种
	const int32 M4MagCapacity = Weapons->GetMechanism(0) ? Weapons->GetMechanism(0)->GetCapacity() : 0;
	const int32 M1911MagCapacity = Weapons->GetMechanism(2) ? Weapons->GetMechanism(2)->GetCapacity() : 0;
	TestTrue(TEXT("M4 与 M1911 有效弹匣容量为正"), M4MagCapacity > 0 && M1911MagCapacity > 0);
	TestEqual(TEXT("M4 枪内已装满首匣"), Weapons->GetMechanismState(0) ? Weapons->GetMechanismState(0)->CountLiveRounds() : 0, M4MagCapacity);
	TestEqual(TEXT("M1911 枪内已装满首匣"), Weapons->GetMechanismState(2) ? Weapons->GetMechanismState(2)->CountLiveRounds() : 0, M1911MagCapacity);

	const FGinkgoAmmoReserve& Reserve = Weapons->GetAmmoReserve();
	TestEqual(TEXT("后备弹药表仅包含 M4 与 M1911 两种弹药"), Reserve.Rounds.Num(), 2);
	TestEqual(TEXT("M4 后备恰好为 2 个弹匣"), Reserve.Count(M4->AmmoType), M4MagCapacity * 2);
	TestEqual(TEXT("M1911 后备恰好为 2 个弹匣"), Reserve.Count(M1911->AmmoType), M1911MagCapacity * 2);

	// 6. 验证首帧更新默认将 M4 持于手中，M1911 挂于腰间
	Weapons->UpdateHoldState(1.f / 60.f);
	TestEqual(TEXT("开局默认手持 M4（主槽 1）"), Weapons->GetHeldSlot(), 0);
	TestEqual(TEXT("手持武器为 M4"), Weapons->GetHeldWeapon(), M4);

	// 7. 验证存档恢复能够准确覆写开局后备而不被开局覆盖污染
	FGinkgoWeaponSaveState Saved = Weapons->CaptureSaveState();
	Saved.Reserve.Rounds.FindOrAdd(M4->AmmoType) = 17;
	Saved.Reserve.Rounds.FindOrAdd(M1911->AmmoType) = 5;
	TestTrue(TEXT("存档恢复成功"), Weapons->RestoreSaveState(Saved));
	TestEqual(TEXT("存档恢复后 M4 后备为存档值"), Weapons->GetAmmoReserve().Count(M4->AmmoType), 17);
	TestEqual(TEXT("存档恢复后 M1911 后备为存档值"), Weapons->GetAmmoReserve().Count(M1911->AmmoType), 5);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

#include "GinkgoGameMode.h"

#include "Character/GinkgoCharacter.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoGameMode, Log, All);

namespace
{
	/** 正式开局默认主武器：模块化 M4 步枪 */
	const TCHAR* const DefaultPrimaryWeaponPath = TEXT("/Game/Weapons/M4/DA_M4.DA_M4");

	/** 正式开局默认副武器：M1911 手枪 */
	const TCHAR* const DefaultSidearmWeaponPath = TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911");

	/** 正式开局每把武器默认配发的后备弹匣数（不含已装入枪内的首匣） */
	constexpr int32 DefaultStartingReserveMagazines = 2;
}

AGinkgoGameMode::AGinkgoGameMode()
{
	DefaultPawnClass = AGinkgoCharacter::StaticClass();
	PlayerControllerClass = AGinkgoPlayerController::StaticClass();

	FGinkgoStartingWeaponEntry Primary;
	Primary.Weapon = TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(DefaultPrimaryWeaponPath));
	Primary.ReserveMagazines = DefaultStartingReserveMagazines;
	StartingLoadout.Add(Primary);

	FGinkgoStartingWeaponEntry Sidearm;
	Sidearm.Weapon = TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(DefaultSidearmWeaponPath));
	Sidearm.ReserveMagazines = DefaultStartingReserveMagazines;
	StartingLoadout.Add(Sidearm);
}

void AGinkgoGameMode::GrantStartingLoadout(AGinkgoCharacter& Character) const
{
	if (!Character.HasAuthority() || !UGinkgoDeveloperSettings::Get()->bStartWithWeapon)
	{
		return;
	}
	if (!Character.GetMesh() || !Character.GetMesh()->GetSkeletalMeshAsset())
	{
		return;
	}
	UGinkgoWeaponComponent* Weapons = Character.GetWeapons();
	if (!Weapons)
	{
		return;
	}

	// 先清空各槽，保证重复复活或自定义子类覆盖时不会残留旧槽位武器。
	for (int32 Slot = 0; Slot < UGinkgoWeaponComponent::SlotCount; ++Slot)
	{
		if (Weapons->GetCarried(Slot))
		{
			Weapons->SetCarried(Slot, nullptr);
		}
	}

	TMap<FGameplayTag, int32> ReserveByAmmo;
	for (const FGinkgoStartingWeaponEntry& Entry : StartingLoadout)
	{
		UGinkgoWeaponDefinition* Weapon = Entry.Weapon.IsNull() ? nullptr : Entry.Weapon.LoadSynchronous();
		if (!Weapon)
		{
			UE_LOG(LogGinkgoGameMode, Warning, TEXT("[GameMode] 开局武器资产加载失败，已跳过：%s"), *Entry.Weapon.ToString());
			continue;
		}
		const int32 Slot = Weapons->FindFreeSlot(Weapon->SlotKind);
		if (Slot == INDEX_NONE)
		{
			UE_LOG(LogGinkgoGameMode, Warning, TEXT("[GameMode] 槽位已满，无法发放开局武器：%s"), *Weapon->GetName());
			continue;
		}
		Weapons->SetCarried(Slot, Weapon);
		if (Weapon->AmmoType.IsValid())
		{
			const UGinkgoWeaponMechanism* Mechanism = Weapons->GetMechanism(Slot);
			const int32 MagazineCapacity = Mechanism ? FMath::Max(Mechanism->GetCapacity(), 0) : 0;
			const int32 ReserveRounds = MagazineCapacity * FMath::Max(Entry.ReserveMagazines, 0);
			ReserveByAmmo.FindOrAdd(Weapon->AmmoType) += ReserveRounds;
		}
	}

	TArray<FGinkgoStartingAmmoEntry> StartingReserves;
	StartingReserves.Reserve(ReserveByAmmo.Num());
	for (const TPair<FGameplayTag, int32>& Pair : ReserveByAmmo)
	{
		FGinkgoStartingAmmoEntry Item;
		Item.Ammo = Pair.Key;
		Item.Rounds = Pair.Value;
		StartingReserves.Add(Item);
	}
	Weapons->ApplyStartingReserve(StartingReserves);
}

void AGinkgoGameMode::RestartPlayer(AController* NewPlayer)
{
	auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(this);
	FTransform Spawn;
	if (Checkpoints && Checkpoints->GetRespawnTransform(Spawn)) { RestartPlayerAtTransform(NewPlayer, Spawn); }
	else { Super::RestartPlayer(NewPlayer); }
	// 初次载图时 Pawn 的 BeginPlay 可能尚未发生，下一帧再恢复，避免开局装备覆盖存档。
	const TWeakObjectPtr<AController> Player = NewPlayer;
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Player]()
	{
		if (Player.IsValid())
		{
			if (auto* System = UGinkgoCheckpointSubsystem::Get(this)) { System->RestorePendingPlayer(*Player); }
		}
	}));
}

void AGinkgoGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	Super::FinishRestartPlayer(NewPlayer, StartRotation);
	if (AGinkgoCharacter* Character = NewPlayer ? Cast<AGinkgoCharacter>(NewPlayer->GetPawn()) : nullptr)
	{
		GrantStartingLoadout(*Character);
	}
}

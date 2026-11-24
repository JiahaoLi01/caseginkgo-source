#include "AI/GinkgoAICombatComponent.h"

#include "AI/GinkgoAIConfig.h"
#include "Character/GinkgoCharacter.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"

void UGinkgoAICombatComponent::Initialize(AGinkgoCharacter* InCharacter, const UGinkgoAIConfig* InConfig)
{
	Stop();
	Character = InCharacter;
	Config = InConfig;
}

void UGinkgoAICombatComponent::SetWeaponIntent(const EGinkgoAIWeaponIntent Intent)
{
	WeaponIntent = Intent;
	if (Intent != EGinkgoAIWeaponIntent::Fire)
	{
		bBurstActive = false;
		BurstRemaining = 0.f;
		if (Character.IsValid()) Character->GetWeapons()->CancelFireIntent();
	}
}

void UGinkgoAICombatComponent::TickCombat(const float DeltaSeconds, const bool bAimAligned)
{
	if (!Character.IsValid() || !Config) return;
	UGinkgoWeaponComponent* Weapons = Character->GetWeapons();
	const FGinkgoMechanismState* Mechanism = Weapons->GetMechanismState(Weapons->GetHeldSlot());
	ReloadRetry = FMath::Max(0.f, ReloadRetry - DeltaSeconds);
	if (bReloading && ReloadRetry < 1.f && Mechanism && Mechanism->CountLiveRounds() > 0
		&& Weapons->IsShotStepClear() && Weapons->SelectWeaponAction() == EGinkgoWeaponAction::Ready)
	{
		bReloading = false;
	}
	if (Mechanism && Mechanism->CountLiveRounds() == 0 && ReloadRetry <= 0.f)
	{
		Weapons->CancelFireIntent();
		Weapons->SetWantsToAim(false);
		Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Reload);
		bReloading = true;
		ReloadRetry = 2.f;
	}
	// 一帧短按装填。轮盘逻辑仍由武器组件处理，AI 不参与 UI。
	Weapons->UpdateAmmoWheel(false, FVector2D::ZeroVector, FVector2D::ZeroVector, DeltaSeconds);
	Weapons->SetWantsToAim(WeaponIntent != EGinkgoAIWeaponIntent::Lowered && !bReloading);
	BurstRemaining = FMath::Max(0.f, BurstRemaining - DeltaSeconds);
	if (WeaponIntent != EGinkgoAIWeaponIntent::Fire || !bAimAligned || bReloading || !Mechanism || Mechanism->CountLiveRounds() == 0)
	{
		Weapons->CancelFireIntent();
		bBurstActive = false;
	}
	else if (bBurstActive)
	{
		if (BurstRemaining <= 0.f)
		{
			Weapons->CancelFireIntent();
			bBurstActive = false;
			BurstRemaining = Config->BurstPause;
		}
	}
	else if (BurstRemaining <= 0.f && Weapons->IsShotStepClear())
	{
		Weapons->SetTriggerHeld(true);
		Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Fire);
		bBurstActive = true;
		BurstRemaining = Config->BurstDuration;
	}
	Weapons->UpdateHoldState(DeltaSeconds);
}

void UGinkgoAICombatComponent::Stop()
{
	ResetWeaponInput();
	if (Character.IsValid())
	{
		Character->GetWeapons()->CancelFireIntent();
		Character->GetWeapons()->SetWantsToAim(false);
		Character->GetWeapons()->StopActionStates();
	}
	Character.Reset();
	Config = nullptr;
}

void UGinkgoAICombatComponent::ResetWeaponInput()
{
	if (Character.IsValid())
	{
		Character->GetWeapons()->CancelFireIntent();
		Character->GetWeapons()->SetWantsToAim(false);
	}
	WeaponIntent = EGinkgoAIWeaponIntent::Lowered;
	bBurstActive = false;
	bReloading = false;
	BurstRemaining = 0.f;
	ReloadRetry = 0.f;
}

#include "Weapon/GinkgoWeaponComponent.h"

#include "Character/GinkgoCharacter.h"
#include "Weapon/GinkgoWeaponSaveState.h"

FGinkgoWeaponSaveState UGinkgoWeaponComponent::CaptureSaveState() const
{
	FGinkgoWeaponSaveState Result;
	Result.Slots.SetNum(SlotCount);
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		auto& Saved = Result.Slots[Slot];
		Saved.Definition = GetCarried(Slot);
		Saved.Attachments = GetAttachments(Slot);
		if (const FGinkgoMechanismState* Mechanism = GetMechanismState(Slot)) { Saved.Mechanism = *Mechanism; }
		Saved.LoadAmmo = GetLoadAmmo(Slot);
	}
	Result.Reserve = AmmoReserve;
	Result.HeldSlot = HeldSlot;
	Result.LastHeldSlot = LastHeldSlot;
	return Result;
}

bool UGinkgoWeaponComponent::RestoreSaveState(const FGinkgoWeaponSaveState& State)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || State.Slots.Num() != SlotCount)
	{
		return false;
	}
	CancelFireIntent();
	SetWantsToAim(false);
	StopActionStates();
	SetHeldSlot(INDEX_NONE);
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		SetCarried(Slot, nullptr);
		SetCarried(Slot, State.Slots[Slot].Definition);
		InstalledAttachments[Slot] = State.Slots[Slot].Attachments;
	}
	RefreshAttachmentMeshes();
	// SetCarried 会初始化弹药；必须在全部槽初始化以后整体覆盖，避免开局后备和已存档后备叠加。
	StartingReserveEntries.Reset();
	StartingReserveSerial = 0;
	AppliedStartingReserveSerial = 0;
	AmmoReserve = State.Reserve;
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		SetMechanismState(Slot, State.Slots[Slot].Mechanism);
		SetLoadAmmo(Slot, State.Slots[Slot].LoadAmmo);
	}
	SetHeldSlot(State.HeldSlot);
	LastHeldSlot = GetCarried(State.LastHeldSlot) ? State.LastHeldSlot : INDEX_NONE;
	TargetSlot = HeldSlot;
	bStartHoldDone = true;
	if (AGinkgoCharacter* Character = GetCharacter())
	{
		Character->ApplyCarriedWeapons();
		if (HeldSlot != INDEX_NONE) { Character->AttachWeaponToHand(HeldSlot); }
	}
	GetOwner()->ForceNetUpdate();
	return true;
}

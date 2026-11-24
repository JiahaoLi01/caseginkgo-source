#include "Throwables/GinkgoFlashPickup.h"
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AGinkgoFlashPickup::AGinkgoFlashPickup()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Model(TEXT("/Game/Weapons/Throwables/SM_M84"));
	Mesh->SetStaticMesh(Model.Object);
	HandMesh = Model.Object;
	ItemInHandLeft = UGinkgoFlashbangComponent::GripSocket().GetLocation();
	StowBone = TEXT("pelvis");
	PocketInChestLeft = UGinkgoFlashbangComponent::BeltSocket().GetLocation();
	Interactable->DisplayName = NSLOCTEXT("Ginkgo", "FlashbangPickup", "闪光弹");
}

EGinkgoInteractionAvailability AGinkgoFlashPickup::GetPickupAvailability(const FGinkgoInteractionContext& Context, FText& Reason) const
{
	if (Context.Character && Context.Character->GetFlashbang()->GetPhase() == EGinkgoFlashPhase::Empty)
		return EGinkgoInteractionAvailability::Available;
	Reason = NSLOCTEXT("Ginkgo", "FlashSlotBusy", "闪光弹槽位已占用");
	return EGinkgoInteractionAvailability::Blocked;
}

EGinkgoInteractionAvailability AGinkgoFlashPickup::GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& Reason) const
{
	if (Context.Character && IsHandAvailable(*Context.Character->GetAnimActions(), EGinkgoHand::Left)) return EGinkgoInteractionAvailability::Available;
	Reason = NSLOCTEXT("Ginkgo", "FlashLeftBusy", "左手被占用");
	return EGinkgoInteractionAvailability::Blocked;
}

void AGinkgoFlashPickup::BeginLocalInteract(const FGinkgoInteractionContext& Context)
{
	if (Context.Character) Context.Character->GetFlashbang()->SetLocalPickup(true);
	Super::BeginLocalInteract(Context);
}

void AGinkgoFlashPickup::OnInteractResult(const FGinkgoInteractionContext& Context, EGinkgoInteractionResult Result)
{
	Super::OnInteractResult(Context, Result);
	if (Context.Character && Result != EGinkgoInteractionResult::Accepted) Context.Character->GetFlashbang()->SetLocalPickup(false);
}

void AGinkgoFlashPickup::OnServerClaimed(const FGinkgoInteractionContext& Context)
{
	if (Context.Character) Context.Character->GetFlashbang()->RestoreInventory(true);
}

void AGinkgoFlashPickup::GrantLocal(AGinkgoCharacter& Character)
{
	Character.GetFlashbang()->SetLocalPickup(false);
}

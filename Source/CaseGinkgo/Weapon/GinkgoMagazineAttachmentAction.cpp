#include "Weapon/GinkgoMagazineAttachmentAction.h"

#include "Animation/AnimSequence.h"
#include "Character/GinkgoCharacter.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoModularWeaponVisual.h"

UGinkgoMagazineAttachmentAction* UGinkgoMagazineAttachmentAction::Make(AGinkgoCharacter& Owner, int32 InSlot, UGinkgoWeaponAttachment& InPart)
{
	auto* Action = NewObject<UGinkgoMagazineAttachmentAction>(&Owner);
	Action->SetActionName(TEXT("ReplaceMagazine"));
	Action->Character = &Owner;
	Action->Slot = InSlot;
	Action->Weapon = Owner.GetWeapons()->GetCarried(InSlot);
	Action->Part = &InPart;
	Action->Occupy(FAnimActionTags::Occupancy_LeftArm, Ginkgo::Occupancy::PriorityWeaponSwap)
		->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityWeaponSwap);
	return Action;
}

void UGinkgoMagazineAttachmentAction::OnStart(FGinkgoAnimActionSetup& Setup)
{
	auto* Owner = Character.Get();
	const auto* Mount = Weapon ? Weapon->FindAttachment(Part) : nullptr;
	const auto* Step = Weapon ? Weapon->Steps.Find(FWeaponTags::Weapon_Step_ReloadTactical) : nullptr;
	auto* Mesh = Part ? Part->ModuleMesh.LoadSynchronous() : nullptr;
	if (!Owner || Owner->GetWeapons()->GetCarried(Slot) != Weapon || !Mount || Mount->ReloadSpareBone.IsNone()
		|| Mount->ModuleBone.IsNone() || !Step || !Step->Montage || !Step->GunTrack || !Mesh
		|| !Owner->GetWeapons()->CanInstallAttachment(Slot, Part))
	{
		Setup.GetContext().Abort(FAnimActionTags::AnimAction_Cancel_OwnerExit);
		return;
	}
	Gun = Owner->GetWeaponSlotComponent(Slot);
	if (!Gun.IsValid())
	{
		Setup.GetContext().Abort(FAnimActionTags::AnimAction_Cancel_OwnerExit);
		return;
	}
	MagazineBone = Mount->ModuleBone;
	SpareBone = Mount->ReloadSpareBone;
	Track = Step->GunTrack;
	bMagazineWasHidden = Gun->IsBoneHiddenByName(MagazineBone);
	bSpareWasHidden = Gun->IsBoneHiddenByName(SpareBone);
	Preview = NewObject<UPoseableMeshComponent>(Owner);
	Preview->SetupAttachment(Gun.Get());
	Preview->SetSkinnedAssetAndUpdate(Mesh);
	Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Preview->SetCanEverAffectNavigation(false);
	Preview->RegisterComponent();
	SamplePreview(0.f);
	const float Rate = 1.f / FGinkgoAttachmentEffects::TimeScale(Owner->GetWeapons()->GetAttachmentEffects(Slot).ReloadTime);
	Setup.Play(Step->Montage, Rate);
	if (Setup.GetContext().GetEventTime(Ginkgo::WeaponEvents::MagIn) < 0.f)
	{
		Setup.GetContext().Abort(FAnimActionTags::AnimAction_Cancel_OwnerExit);
		return;
	}
	Setup.AbortIf(NAME_None, [this](const FGinkgoAnimActionContext&)
	{
		return !Character.IsValid() || Character->GetWeapons()->GetCarried(Slot) != Weapon || !Gun.IsValid();
	}, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	Setup.On(Ginkgo::WeaponEvents::MagOut, [this](FGinkgoAnimActionEvent&)
	{
		if (Character.IsValid()) { Character->GetWeapons()->PlayContactShake(Slot, Ginkgo::WeaponEvents::MagOut); }
	}, EGinkgoMissedEvent::Skip);
	Setup.On(Ginkgo::WeaponEvents::MagIn, [this](FGinkgoAnimActionEvent& Event)
	{
		if (!Character.IsValid() || bSeated) { return; }
		auto* Weapons = Character->GetWeapons();
		if (Character->HasAuthority() && !Weapons->InstallWorkbenchMagazine(Slot, Part))
		{
			Event.Context.Abort(FAnimActionTags::AnimAction_Cancel_OwnerExit);
			return;
		}
		Weapons->PlayContactShake(Slot, Ginkgo::WeaponEvents::MagIn);
		bSeated = true;
		SamplePreview(Event.Context.GetPosition());
	}, EGinkgoMissedEvent::Skip);
}

void UGinkgoMagazineAttachmentAction::OnTick(FGinkgoAnimActionContext& Context, float DeltaSeconds)
{
	Super::OnTick(Context, DeltaSeconds);
	SamplePreview(Context.GetPosition());
}

void UGinkgoMagazineAttachmentAction::SamplePreview(float Time)
{
	if (!Preview || !Track || !Gun.IsValid()) { return; }
	Gun->HideBoneByName(SpareBone, EPhysBodyOp::PBO_None);
	if (bSeated) { Gun->HideBoneByName(MagazineBone, EPhysBodyOp::PBO_None); }
	const auto* Mesh = Cast<USkeletalMesh>(Preview->GetSkinnedAsset());
	const auto& Ref = Mesh->GetRefSkeleton();
	const auto& Source = Track->GetSkeleton()->GetReferenceSkeleton();
	for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
	{
		const FName Name = Ref.GetBoneName(Bone);
		FTransform Pose = Ref.GetRefBonePose()[Bone];
		// 模块文件中也有一份备用弹匣，只画 mag，装入前把它映射到 mag_new 的同源轨道。
		const FName SourceName = Name == MagazineBone && !bSeated ? SpareBone : Name;
		const int32 SourceBone = Source.FindBoneIndex(SourceName);
		if (SourceBone != INDEX_NONE) { Track->GetBoneTransform(Pose, FSkeletonPoseBoneIndex(SourceBone), FAnimExtractContext(double(Time)), false); }
		if (Name == SpareBone) { Pose.SetScale3D(FVector::ZeroVector); }
		Preview->BoneSpaceTransforms[Bone] = Pose;
	}
	Preview->RefreshBoneTransforms();
}

void UGinkgoMagazineAttachmentAction::OnFinished(const FGinkgoAnimActionResult& InResult)
{
	Super::OnFinished(InResult);
	bool bTransferred = false;
	if (bSeated && Preview && Gun.IsValid() && Character.IsValid() && Character->GetWeapons()->GetCarried(Slot) == Weapon)
	{
		if (auto* Visual = Character->GetWeapons()->GetModularVisual(Slot))
		{
			bTransferred = Visual->HoldPreview(*Part, *Gun, *Preview, MagazineBone, bMagazineWasHidden);
		}
	}
	if (Gun.IsValid())
	{
		if (!bTransferred && !bMagazineWasHidden) { Gun->UnHideBoneByName(MagazineBone); }
		if (!bSpareWasHidden) { Gun->UnHideBoneByName(SpareBone); }
	}
	if (Preview && !bTransferred) { Preview->DestroyComponent(); }
	Preview = nullptr;
}

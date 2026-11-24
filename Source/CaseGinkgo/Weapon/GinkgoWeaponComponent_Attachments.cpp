#include "Weapon/GinkgoWeaponComponent.h"

#include "Character/GinkgoCharacter.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Weapon/GinkgoWeaponDefinition.h"

UGinkgoModularWeaponVisual* UGinkgoWeaponComponent::GetModularVisual(const int32 Slot) const
{
	return AttachmentMeshes.IsValidIndex(Slot) ? AttachmentMeshes[Slot].GetModularVisual() : nullptr;
}

bool UGinkgoWeaponComponent::CanInstallAttachment(const int32 Slot, const UGinkgoWeaponAttachment* Part) const
{
	const auto* Weapon = GetCarried(Slot);
	return Weapon && Weapon->CanInstall(GetAttachments(Slot), Part);
}

void UGinkgoWeaponComponent::InstallAttachment(const int32 Slot, UGinkgoWeaponAttachment* Part)
{
	if (!GetOwner()->HasAuthority() || !CanInstallAttachment(Slot, Part)) { return; }
	const auto* PreviousMechanism = GetMechanism(Slot);
	const int32 PreviousCapacity = PreviousMechanism ? PreviousMechanism->GetCapacity() : 0;
	InstalledAttachments[Slot] = GetCarried(Slot)->WithAttachment(GetAttachments(Slot), Part);
	RefreshAttachmentMeshes();
	// 仅真正换小弹匣时退弹。复制和拾取恢复会先收到武器再收到模块，不能按中间的默认容量裁掉原枪弹药。
	if (const auto* Mechanism = GetMechanism(Slot); Mechanism && GetMechanismState(Slot) && Mechanism->GetCapacity() < PreviousCapacity)
	{
		auto& Feed = Mechanisms[Slot].Feed;
		while (Feed.Num() > Mechanism->GetCapacity()) { AmmoReserve.Add(Feed.Pop(), 1); }
	}
	GetOwner()->ForceNetUpdate();
}

bool UGinkgoWeaponComponent::InstallWorkbenchMagazine(const int32 Slot, UGinkgoWeaponAttachment* Part)
{
	auto* Weapon = GetCarried(Slot);
	const auto* Magazine = Cast<UGinkgoMagazineMechanism>(GetMechanism(Slot));
	if (!GetOwner()->HasAuthority() || !Part || !Part->bInstallWithReload || !CanInstallAttachment(Slot, Part)
		|| !GetMechanismState(Slot) || !Magazine || Magazine->Rules.Feed != EGinkgoFeed::DetachableMagazine)
	{
		return false;
	}
	InstallAttachment(Slot, Part);
	const int32 Capacity = GetMechanism(Slot)->GetCapacity();
	RefillWorkbenchMagazine(Slot, Weapon, Capacity);
	const auto* Character = GetCharacter();
	if (Character && Character->IsPlayerControlled() && !Character->IsLocallyControlled())
	{
		ClientRefillWorkbenchMagazine(Slot, Weapon, Capacity);
	}
	return true;
}

void UGinkgoWeaponComponent::ClientRefillWorkbenchMagazine_Implementation(const int32 Slot, UGinkgoWeaponDefinition* Weapon, const int32 Capacity)
{
	RefillWorkbenchMagazine(Slot, Weapon, Capacity);
}

void UGinkgoWeaponComponent::RefillWorkbenchMagazine(const int32 Slot, const UGinkgoWeaponDefinition* Weapon, const int32 Capacity)
{
	if (!Weapon || GetCarried(Slot) != Weapon || !GetMechanismState(Slot)) { return; }
	const auto* Magazine = Cast<UGinkgoMagazineMechanism>(Weapon->Mechanism);
	if (!Magazine) { return; }
	FGinkgoMagazineRules Rules = Magazine->Rules;
	Rules.Capacity = Capacity;
	auto& State = Mechanisms[Slot];
	Ginkgo::Weapon::ApplyMechanismEvent(Rules, State, AmmoReserve, EGinkgoMechanismEvent::MagOut, {});
	ResolveLoadAmmo(Slot);
	Ginkgo::Weapon::ApplyMechanismEvent(Rules, State, AmmoReserve, EGinkgoMechanismEvent::MagIn, GetLoadAmmo(Slot));
}

const FGinkgoAttachmentEffects& UGinkgoWeaponComponent::GetAttachmentEffects(const int32 Slot) const
{
	static const FGinkgoAttachmentEffects Empty;
	return GetCarried(Slot) && AttachmentEffects.IsValidIndex(Slot) ? AttachmentEffects[Slot] : Empty;
}

const UGinkgoWeaponMechanism* UGinkgoWeaponComponent::GetMechanism(const int32 Slot) const
{
	const auto* Weapon = GetCarried(Slot);
	if (!Weapon) { return nullptr; }
	return EffectiveMechanismSources[Slot] == Weapon && EffectiveMechanisms[Slot]
		? EffectiveMechanisms[Slot].Get() : Weapon->Mechanism.Get();
}

void UGinkgoWeaponComponent::RefreshAttachmentMeshes()
{
	AGinkgoCharacter* Character = GetCharacter();
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const auto* Weapon = GetCarried(Slot);
		auto& Effects = AttachmentEffects[Slot];
		Effects = {};
		for (const UGinkgoWeaponAttachment* Part : GetAttachments(Slot).Parts)
		{
			if (Weapon && Weapon->FindAttachment(Part)) { Effects.Add(Part->HandlingEffects); }
		}
		const auto* Source = Weapon ? Cast<UGinkgoMagazineMechanism>(Weapon->Mechanism) : nullptr;
		if (Source && Effects.MagazineCapacity > 0)
		{
			if (!EffectiveMechanisms[Slot] || EffectiveMechanismSources[Slot] != Weapon)
			{
				EffectiveMechanisms[Slot] = DuplicateObject<UGinkgoMagazineMechanism>(Source, this);
				EffectiveMechanismSources[Slot] = const_cast<UGinkgoWeaponDefinition*>(Weapon);
			}
			CastChecked<UGinkgoMagazineMechanism>(EffectiveMechanisms[Slot])->Rules.Capacity = Effects.MagazineCapacity;
		}
		else
		{
			EffectiveMechanisms[Slot] = nullptr;
			EffectiveMechanismSources[Slot] = nullptr;
		}
		if (Character)
		{
			if (auto* Gun = Character->GetWeaponSlotComponent(Slot))
			{
				AttachmentMeshes[Slot].Update(Weapon, GetAttachments(Slot), *Gun);
			}
		}
	}
}

FVector UGinkgoWeaponComponent::GetMuzzleOffset(const int32 Slot) const
{
	const auto* Weapon = GetCarried(Slot);
	if (!Weapon) { return FVector::ZeroVector; }
	for (const UGinkgoWeaponAttachment* Part : GetAttachments(Slot).Parts)
	{
		const auto* Mount = Weapon->FindAttachment(Part);
		if (Mount && !Mount->MuzzleLocation.IsZero()) { return Mount->MuzzleLocation; }
	}
	return Weapon->MuzzleOffset;
}

FVector UGinkgoWeaponComponent::GetSupportHandOffset(const int32 Slot) const
{
	FVector Result = FVector::ZeroVector;
	if (const auto* Weapon = GetCarried(Slot))
	{
		for (const UGinkgoWeaponAttachment* Part : GetAttachments(Slot).Parts)
		{
			if (const auto* Mount = Weapon->FindAttachment(Part)) { Result += Mount->SupportHandOffset; }
		}
	}
	return Result;
}

void UGinkgoWeaponComponent::ApplyModulePose(const UGinkgoWeaponDefinition& Weapon, const FGinkgoInstalledAttachments& Installed, UPoseableMeshComponent& Mesh)
{
	const auto* Asset = Cast<USkeletalMesh>(Mesh.GetSkinnedAsset());
	if (!Weapon.Modularity || !Asset) { return; }
	for (const UGinkgoWeaponAttachment* Part : Installed.Parts)
	{
		const auto* Mount = Weapon.FindAttachment(Part);
		if (!Mount || Mount->MuzzleLocation.IsZero()) { continue; }
		const int32 Bone = Asset->GetRefSkeleton().FindBoneIndex(TEXT("mount_muzzle"));
		if (Mesh.BoneSpaceTransforms.IsValidIndex(Bone))
		{
			// 配置使用枪空间 cm，BoneSpaceTransforms 则是父骨骼局部空间。
			// FBX 根骨骼可能带单位换算的缩放；直接加 cm 会把枪口件推远（M4 为 100 倍）。
			// 此时新姿势尚未 Refresh，沿本帧局部姿势求父变换，不能读组件缓存的旧骨骼矩阵。
			FTransform ParentToGun = FTransform::Identity;
			for (int32 Parent = Asset->GetRefSkeleton().GetParentIndex(Bone); Parent != INDEX_NONE;
				Parent = Asset->GetRefSkeleton().GetParentIndex(Parent))
			{
				ParentToGun = ParentToGun * Mesh.BoneSpaceTransforms[Parent];
			}
			Mesh.BoneSpaceTransforms[Bone].AddToTranslation(ParentToGun.InverseTransformVector(Mount->MuzzleLocation - Weapon.MuzzleOffset));
		}
	}
}

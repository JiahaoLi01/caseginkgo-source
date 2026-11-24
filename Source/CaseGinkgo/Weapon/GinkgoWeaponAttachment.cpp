#include "Weapon/GinkgoWeaponAttachment.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Components/PoseableMeshComponent.h"
#include "Weapon/GinkgoModularWeaponVisual.h"

void FGinkgoAttachmentMeshes::Update(const UGinkgoWeaponDefinition* Weapon, const FGinkgoInstalledAttachments& Installed, USceneComponent& Parent)
{
	if (Weapon && Weapon->Modularity)
	{
		for (UStaticMeshComponent* Mesh : Meshes) { if (Mesh) { Mesh->DestroyComponent(); } }
		Meshes.Reset();
		if (auto* Gun = Cast<UPoseableMeshComponent>(&Parent))
		{
			if (!ModularVisual) { ModularVisual = NewObject<UGinkgoModularWeaponVisual>(Parent.GetOwner()); }
			ModularVisual->Update(*Weapon, Installed, *Gun);
		}
		return;
	}
	if (ModularVisual) { ModularVisual->Reset(); ModularVisual = nullptr; }
	// 网格按已装配件的顺序复用。武器和配件分开复制，先到的一边也只能画当前武器接受的配件。
	for (int32 Index = Installed.Parts.Num(); Index < Meshes.Num(); ++Index)
	{
		if (Meshes[Index])
		{
			Meshes[Index]->DestroyComponent();
		}
	}
	Meshes.SetNum(Installed.Parts.Num());
	for (int32 Index = 0; Index < Installed.Parts.Num(); ++Index)
	{
		const UGinkgoWeaponAttachment* Part = Installed.Parts[Index];
		const FGinkgoWeaponAttachmentMount* Mount = Weapon ? Weapon->FindAttachment(Part) : nullptr;
		auto& Mesh = Meshes[Index];
		if (!Mount)
		{
			if (Mesh)
			{
				Mesh->DestroyComponent();
				Mesh = nullptr;
			}
			continue;
		}
		if (!Mesh)
		{
			Mesh = NewObject<UStaticMeshComponent>(Parent.GetOwner());
			Mesh->SetupAttachment(&Parent);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->RegisterComponent();
		}
		Mesh->SetStaticMesh(Part->Mesh.LoadSynchronous());
		Mesh->SetRelativeTransform(Mount->RelativeTransform);
		Mesh->SetVisibility(Parent.IsVisible());
	}
}

void FGinkgoAttachmentMeshes::Reset()
{
	if (ModularVisual) { ModularVisual->Reset(); ModularVisual = nullptr; }
	for (UStaticMeshComponent* Mesh : Meshes) { if (IsValid(Mesh)) { Mesh->DestroyComponent(); } }
	Meshes.Reset();
}

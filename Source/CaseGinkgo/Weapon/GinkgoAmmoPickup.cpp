#include "Weapon/GinkgoAmmoPickup.h"

#include "Character/GinkgoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Net/UnrealNetwork.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"

AGinkgoAmmoPickup::AGinkgoAmmoPickup()
{
	AmmoType = FWeaponTags::Ammo_Mauser792x57_Standard;
	AmmoName = NSLOCTEXT("Ginkgo", "AmmoBoxName", "7.92 × 57mm Mauser");
	// 网格的原点在盒底中心，合上盖是 10.9 × 6.9 × 6.4 cm：手抓盒子的中间
	GraspPoint = FVector(0.0, 0.0, 3.2);
}

void AGinkgoAmmoPickup::SetRounds(const int32 InRounds)
{
	Rounds = FMath::Max(InRounds, 0);
	ApplyRounds();
}

void AGinkgoAmmoPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyDefaultMesh();
	ApplyRounds();
}

void AGinkgoAmmoPickup::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyDefaultMesh();
	ApplyRounds();
}

void AGinkgoAmmoPickup::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && Rounds <= 0)
	{
		Rounds = FMath::RandRange(FMath::Min(MinRounds, MaxRounds), FMath::Max(MinRounds, MaxRounds));
		ApplyRounds();
	}
}

void AGinkgoAmmoPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Rounds);
}

void AGinkgoAmmoPickup::GrantLocal(AGinkgoCharacter& Character)
{
	if (UGinkgoWeaponComponent* Weapons = Character.GetWeapons())
	{
		Weapons->AddReserveRounds(AmmoType, Rounds);
	}
}

void AGinkgoAmmoPickup::OnRep_Rounds()
{
	ApplyRounds();
}

void AGinkgoAmmoPickup::ApplyDefaultMesh()
{
	// 关卡里摆的可以自己换网格；没换的（和调试命令生成的）用设置里的子弹盒，资产不在时没有网格，照样能拾取
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	if (!HandMesh && !Settings->AmmoBoxHandMesh.IsNull())
	{
		HandMesh = Settings->AmmoBoxHandMesh.LoadSynchronous();
	}
	if (Mesh->GetStaticMesh())
	{
		return;
	}
	Mesh->SetStaticMesh(Settings->AmmoBoxMesh.IsNull() ? nullptr : Settings->AmmoBoxMesh.LoadSynchronous());
}

void AGinkgoAmmoPickup::ApplyRounds()
{
	Interactable->DisplayName = Rounds > 0 ? FText::Format(NSLOCTEXT("Ginkgo", "AmmoBoxRounds", "{0}（{1} 发）"), AmmoName, FText::AsNumber(Rounds)) : AmmoName;
}

#include "Weapon/GinkgoModularWeaponVisual.h"

#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "Weapon/GinkgoModularWeaponData.h"
#include "Weapon/GinkgoWeaponDefinition.h"

void UGinkgoModularWeaponVisual::Update(const UGinkgoWeaponDefinition& Weapon, const FGinkgoInstalledAttachments& Installed, UPoseableMeshComponent& Target)
{
	if (Definition.IsValid() && (Definition != &Weapon || Mesh != &Target)) { Reset(); }
	for (int32 Index = Handoffs.Num() - 1; Index >= 0; --Index)
	{
		auto& Handoff = Handoffs[Index];
		if (Installed.Parts.Contains(Handoff.Part)) { Handoff.bRequested = true; }
		else if (Handoff.bRequested) { ReleaseHandoff(Handoff); Handoffs.RemoveAt(Index); }
	}
	FString Key;
	for (const auto& Mount : Weapon.Attachments)
	{
		if (Mount.Part && Installed.Parts.Contains(Mount.Part))
		{
			Key += Mount.Mount.ToString() + TEXT("=") + Mount.Part->ModularOption.ToString() + TEXT(";");
		}
	}
	if (Definition == &Weapon && Mesh == &Target && Configuration == Key) { return; }
	const auto* Data = Weapon.Modularity.Get();
	Object = Data ? Data->Object.LoadSynchronous() : nullptr;
	if (!Object || Target.GetNetMode() == NM_DedicatedServer) { return; }
	Definition = &Weapon;
	Mesh = &Target;
	Configuration = Key;
	PendingInstalled = Installed;
	++Generation;
	FTSTicker::GetCoreTicker().RemoveTicker(LoadingTick);
	LoadingTick.Reset();
	if (!Object->IsCompiled() || Object->IsLoading())
	{
#if WITH_EDITOR
		Object->ConditionalAutoCompile();
#endif
		LoadingTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			if (!Mesh.IsValid() || !Definition.IsValid()) { LoadingTick.Reset(); return false; }
			if (!Object->IsCompiled() || Object->IsLoading()) { return true; }
			LoadingTick.Reset();
			Generate();
			return false;
		}));
		return;
	}
	Generate();
}

void UGinkgoModularWeaponVisual::BeginDestroy()
{
	Reset();
	Super::BeginDestroy();
}

bool UGinkgoModularWeaponVisual::HasHandoff(const FName Bone) const
{
	return Handoffs.ContainsByPredicate([Bone](const auto& Handoff) { return Handoff.Bone == Bone; });
}

bool UGinkgoModularWeaponVisual::HoldPreview(const UGinkgoWeaponAttachment& Part, UPoseableMeshComponent& Target,
	UPoseableMeshComponent& Preview, const FName Bone, bool bWasHidden)
{
	if (Mesh != &Target || !Definition.IsValid() || Bone.IsNone() || DisplayedInstalled.Parts.Contains(&Part)) { return false; }
	// 同一插槽快速重复操作时沿用最初的隐藏约束，不能把上一份占位设置的隐藏当成永久状态。
	for (int32 Index = Handoffs.Num() - 1; Index >= 0; --Index)
	{
		if (Handoffs[Index].Bone != Bone) { continue; }
		bWasHidden = Handoffs[Index].bWasHidden;
		ReleaseHandoff(Handoffs[Index]);
		Handoffs.RemoveAt(Index);
	}
	auto& Handoff = Handoffs.AddDefaulted_GetRef();
	Handoff.Part = &Part;
	Handoff.Preview = &Preview;
	Handoff.Bone = Bone;
	Handoff.bWasHidden = bWasHidden;
	Handoff.bRequested = PendingInstalled.Parts.Contains(&Part);
	Preview.AttachToComponent(&Target, FAttachmentTransformRules::KeepWorldTransform);
	Target.HideBoneByName(Bone, EPhysBodyOp::PBO_None);
	return true;
}

void UGinkgoModularWeaponVisual::ReleaseHandoff(FGinkgoModuleHandoff& Handoff)
{
	if (Mesh.IsValid())
	{
		if (Handoff.bWasHidden) { Mesh->HideBoneByName(Handoff.Bone, EPhysBodyOp::PBO_None); }
		else { Mesh->UnHideBoneByName(Handoff.Bone); }
	}
	if (IsValid(Handoff.Preview)) { Handoff.Preview->DestroyComponent(); }
	Handoff.Preview = nullptr;
}

void UGinkgoModularWeaponVisual::Reset()
{
	++Generation;
	FTSTicker::GetCoreTicker().RemoveTicker(LoadingTick);
	LoadingTick.Reset();
	for (auto& Handoff : Handoffs) { ReleaseHandoff(Handoff); }
	Handoffs.Reset();
	DisplayedInstalled = {};
	PendingInstalled = {};
	Configuration.Reset();
	Definition.Reset();
	Mesh.Reset();
	Instance = nullptr;
	Object = nullptr;
}

void UGinkgoModularWeaponVisual::Generate()
{
	if (!Definition.IsValid() || !Mesh.IsValid()) { return; }
	const uint32 Request = Generation;
	Instance = Object->CreateInstance();
	if (!Instance) { Configuration.Reset(); return; }
	for (const auto& Mount : Definition->Attachments)
	{
		if (Mount.Part && PendingInstalled.Parts.Contains(Mount.Part))
		{
			Instance->SetIntParameterSelectedOption(Mount.Mount.ToString(), Mount.Part->ModularOption.ToString());
		}
	}
	FInstanceUpdateNativeDelegate Done;
	Done.AddWeakLambda(this, [this, Request, ComponentName = Definition->Modularity->ComponentName](const FUpdateContext& Result)
	{
		if (Request != Generation || !Mesh.IsValid() || Instance != Result.Instance) { return; }
		auto* Generated = Result.Instance->GetSkeletalMeshComponentSkeletalMesh(ComponentName);
		if (Result.UpdateResult != EUpdateResult::Success || !Generated)
		{
			UE_LOG(LogTemp, Error, TEXT("[M4 Mutable] 外观生成失败：%s，结果 %d"), *Configuration, int32(Result.UpdateResult));
			Configuration.Reset();
			return;
		}
		// 同一骨架仍按名字保留当前机械姿势，防止换模块把挂机状态或正在播放的弹匣重置。
		TMap<FName, FTransform> Pose;
		TArray<FName> HiddenBones;
		if (const auto* Previous = Cast<USkeletalMesh>(Mesh->GetSkinnedAsset()))
		{
			const auto& Ref = Previous->GetRefSkeleton();
			for (int32 Bone = 0; Bone < Ref.GetNum() && Bone < Mesh->BoneSpaceTransforms.Num(); ++Bone)
			{
				Pose.Add(Ref.GetBoneName(Bone), Mesh->BoneSpaceTransforms[Bone]);
				const auto& Visibility = Mesh->GetEditableBoneVisibilityStates();
				if (Visibility.IsValidIndex(Bone) && Visibility[Bone] == BVS_ExplicitlyHidden) { HiddenBones.Add(Ref.GetBoneName(Bone)); }
			}
		}
		Mesh->SetSkinnedAssetAndUpdate(Generated);
		const auto& Ref = Generated->GetRefSkeleton();
		Mesh->BoneSpaceTransforms = Ref.GetRefBonePose();
		for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
		{
			if (const auto* Transform = Pose.Find(Ref.GetBoneName(Bone))) { Mesh->BoneSpaceTransforms[Bone] = *Transform; }
		}
		Mesh->EmptyOverrideMaterials();
		const auto Materials = Instance->GetSkeletalMeshComponentOverrideMaterials(ComponentName);
		for (int32 Index = 0; Index < Materials.Num(); ++Index) { Mesh->SetMaterial(Index, Materials[Index]); }
		// SetSkinnedAssetAndUpdate 会重建骨骼显示状态；保留动作中的遮挡，不能在异步回调那一帧露出原件。
		for (const FName Bone : HiddenBones) { Mesh->HideBoneByName(Bone, EPhysBodyOp::PBO_None); }
		DisplayedInstalled = PendingInstalled;
		for (int32 Index = Handoffs.Num() - 1; Index >= 0; --Index)
		{
			if (!DisplayedInstalled.Parts.Contains(Handoffs[Index].Part)) { continue; }
			ReleaseHandoff(Handoffs[Index]);
			Handoffs.RemoveAt(Index);
		}
		Mesh->RefreshBoneTransforms();
	});
	Instance->UpdateSkeletalMeshAsyncResult(Done, true, true);
}

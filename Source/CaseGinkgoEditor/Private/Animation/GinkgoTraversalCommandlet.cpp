#include "GinkgoTraversalCommandlet.h"
#include "GinkgoTraversalBaking.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AnimationBlueprintLibrary.h"
#include "AnimNotifyState_MotionWarping.h"
#include "AnimNotifies/AnimNotify_PlayMontageNotify.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Movement/Traversal/GinkgoTraversalAnimations.h"
#include "Movement/Traversal/GinkgoTraversalWarp.h"
#include "UObject/SavePackage.h"

namespace
{
	/** 固定输出到本功能目录，可重复执行；不会重新生成武器或其他蒙太奇。 */
	template <typename T> T* Asset(const FString& Name)
	{
		const FString PackageName = TEXT("/Game/Characters/Traversal/") + Name;
		if (auto* Existing = LoadObject<T>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet)) { return Existing; }
		auto* Object = NewObject<T>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Object);
		return Object;
	}

	bool SaveTraversalAsset(UObject& Object)
	{
		Object.MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Object.GetOutermost(), &Object,
			*FPackageName::LongPackageNameToFilename(Object.GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}

	bool Build(const FString& Kind, UAnimSequence* Sequence, FGinkgoTraversalClip& Clip)
	{
		if (!Sequence || !Sequence->bEnableRootMotion || Sequence->GetPlayLength() + .001 < Clip.MotionEndTime + Clip.RecoveryDuration) { return false; }
		auto* Montage = Asset<UAnimMontage>(TEXT("AM_Traversal_") + Kind);
		Montage->SetSkeleton(Sequence->GetSkeleton());
		Montage->SlotAnimTracks.SetNum(1);
		auto& Track = Montage->SlotAnimTracks[0];
		Track.SlotName = TEXT("DefaultSlot");
		Track.AnimTrack.AnimSegments.Reset();
		FAnimSegment Segment;
		Segment.SetAnimReference(Sequence, true);
		Segment.AnimEndTime = Clip.MotionEndTime + Clip.RecoveryDuration;
		Track.AnimTrack.AnimSegments.Add(Segment);
		Montage->SetCompositeLength(Segment.AnimEndTime);
		static_cast<UAnimCompositeBase*>(Montage)->UpdateCommonTargetFrameRate();
		Montage->CompositeSections.Reset();
		Montage->AddAnimCompositeSection(TEXT("Traversal"), 0.f);
		Montage->BlendIn.SetBlendTime(.18f);
		Montage->BlendIn.SetBlendOption(EAlphaBlendOption::Cubic);
		Montage->BlendOut.SetBlendTime(Clip.BlendOutDuration);
		// 足部支撑事件在运行时主动让出全身姿势；保留尾段供淡出采样，自然淡出仅作为兜底。
		Montage->BlendOutTriggerTime = Clip.BlendOutDuration;
		Montage->bEnableAutoBlendOut = true;
		Montage->Notifies.Reset();
		Montage->AnimNotifyTracks.Reset();
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, TEXT("TraversalWarp"));
		auto* Notify = Cast<UAnimNotifyState_MotionWarping>(UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(
			Montage, TEXT("TraversalWarp"), 0.f, Clip.MotionEndTime, UAnimNotifyState_MotionWarping::StaticClass()));
		if (!Notify) { return false; }
		Notify->RootMotionModifier = NewObject<UGinkgoTraversalWarp>(Notify);
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, TEXT("TraversalEvents"));
		auto* Landed = Cast<UAnimNotify_PlayMontageNotify>(UAnimationBlueprintLibrary::AddAnimationNotifyEvent(
			Montage, TEXT("TraversalEvents"), Clip.MotionEndTime, UAnimNotify_PlayMontageNotify::StaticClass()));
		if (!Landed) { return false; }
		// NotifyName 是受保护属性，按项目已有蒙太奇命令的反射方式填入事件名。
		FindFProperty<FNameProperty>(Landed->GetClass(), TEXT("NotifyName"))->SetPropertyValue_InContainer(Landed, Ginkgo::Traversal::LandedEvent);
		for (auto& Event : Montage->Notifies)
		{
			// 零时刻窗口不能带编辑器的正向触发微偏移，否则第一帧会应用未适配的原始根运动。
			if (Event.NotifyStateClass == Notify) { Event.TriggerTimeOffset = 0.f; Event.EndTriggerTimeOffset = 0.f; }
			if (Event.Notify == Landed) { Event.MontageTickType = EMontageNotifyTickType::BranchingPoint; }
		}
		Montage->PostEditChange();
		Clip.Montage = Montage;
		return Clip.IsValid() && SaveTraversalAsset(*Montage);
	}
}

UGinkgoTraversalCommandlet::UGinkgoTraversalCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGinkgoTraversalCommandlet::Main(const FString& Params)
{
	auto* Data = Asset<UGinkgoTraversalAnimations>(TEXT("DA_TraversalAnimations"));
	Data->Vault.MotionEndTime = .8f;
	Data->Vault.ReferenceHeight = 100.f;
	Data->Vault.RiseEndTime = .26f;
	Data->Vault.ContactRootDistance = 22.f;
	Data->Vault.bTwoHanded = false;
	// 单手支撑按实际臂长留出弯肘余量，不能沿用双肩平均中心的可达距离。
	Data->Vault.ArmReach = 55.f;
	Data->Vault.RecoveryDuration = .6f;
	Data->Vault.BlendOutDuration = .35f;
	Data->Vault.MovementBlendOut = .24f;
	Data->Vault.LeftHand = {.18f, .26f, .4f, .48f};
	Data->Vault.RightHand = {};
	// 碰撞代理独立描述收腿/越沿姿势，位置曲线始终来自原动画。
	Data->Vault.Body = {{0.f, {}, 1.f, 1.f}, {.1f, FVector(0., 0., 26.), .65f, .67f}, {.76f, FVector(0., 0., 26.), .65f, .67f}, {.8f, {}, 1.f, 1.f}};
	Data->VaultMoving = Data->Vault;
	// 1.0 s 时右脚已稳、左脚接近台面，骨盆还未站直；从这里拆掉全身恢复段，交给 Locomotion。
	Data->Mantle.MotionEndTime = 1.f;
	Data->Mantle.ReferenceHeight = 100.f;
	Data->Mantle.RiseEndTime = .35f;
	Data->Mantle.ContactRootDistance = 34.f;
	Data->Mantle.LeftHand = {.25f, .35f, .58f, .72f};
	Data->Mantle.RightHand = {};
	Data->Mantle.bTwoHanded = false;
	Data->Mantle.ArmReach = 55.f;
	Data->Mantle.RecoveryDuration = .5f;
	Data->Mantle.BlendOutDuration = .35f;
	Data->Mantle.MovementBlendOut = .18f;
	// 收腿时骨盆在根后约 12 cm，代理跟随躯干而不是前移的根；保留高矮台沿的身体余量。
	Data->Mantle.Body = {{0.f, {}, 1.f, 1.f}, {.3f, FVector(-12.5, 0., 22.), .65f, .67f}, {.9f, FVector(-12.5, 0., 22.), .65f, .67f}, {1.f, {}, 1.f, 1.f}};
	// 1.8 s 已从抓沿转为足部支撑，不等 2.33 s 的完全站直；拔枪只占右臂，与走跑并行。
	Data->Climb.MotionEndTime = 1.8f;
	Data->Climb.ReferenceHeight = 250.f;
	Data->Climb.RiseEndTime = .8f;
	Data->Climb.ContactRootDistance = 34.f;
	Data->Climb.bTwoHanded = true;
	Data->Climb.RecoveryDuration = .6f;
	Data->Climb.BlendOutDuration = .35f;
	Data->Climb.MovementBlendOut = .18f;
	Data->Climb.Body = {{0.f, {}, 1.f, 1.f}, {.6f, FVector(-10., 0., 22.), .65f, .67f}, {1.7f, FVector(-10., 0., 22.), .65f, .67f}, {1.8f, {}, 1.f, 1.f}};
	Data->Climb.LeftHand = {.6f, .8f, 1.45f, 1.65f};
	Data->Climb.RightHand = {.6f, .8f, 1.2f, 1.4f};
	const bool bVault = Build(TEXT("Vault"), Ginkgo::Traversal::Editor::BakeVault(false), Data->Vault);
	const bool bVaultMoving = Build(TEXT("VaultMoving"), Ginkgo::Traversal::Editor::BakeVault(true), Data->VaultMoving);
	const bool bMantle = Build(TEXT("Mantle"), LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Animations/Traversal/Mantle/M_Neutral_Traversal_Mantle_1_0_stand_F_Lfoot")), Data->Mantle);
	const bool bClimb = Build(TEXT("Climb"), LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Animations/Traversal/Climb/M_Neutral_Traversal_Climb_Start_2_5_stand_F_Lfoot")), Data->Climb);
	if (!bVault || !bVaultMoving || !bMantle || !bClimb || !SaveTraversalAsset(*Data)) { return 1; }
	UE_LOG(LogTemp, Display, TEXT("TRAVERSAL_ANIMATIONS_READY: 4 montages, 4 whole-clip warp windows, single-hand vault/mantle, two-hand climb"));
	return 0;
}

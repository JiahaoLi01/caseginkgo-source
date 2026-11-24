#include "GinkgoTraversalBaking.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "AnimPose.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

namespace Ginkgo::Traversal::Editor
{
	UAnimSequence* BakeVault(const bool bMovingExit)
	{
		const FString Folder = TEXT("/Game/Characters/UEFN_Mannequin/Animations/");
		auto* Source = LoadObject<UAnimSequence>(nullptr, *(Folder + TEXT("Traversal/Vault/M_Neutral_Traversal_Vault_1_0_run_F_Lfoot")));
		auto* Landing = LoadObject<UAnimSequence>(nullptr, *(Folder + (bMovingExit
			? TEXT("Jump/M_Neutral_Jump_F_Land_Run_Light_Rfoot") : TEXT("Jump/M_Neutral_Jump_F_Land_Stand_Light_Rfoot"))));
		if (!Source || !Landing || Source->GetSkeleton() != Landing->GetSkeleton()) { return nullptr; }
		const FString Name = bMovingExit ? TEXT("AS_Traversal_Vault_Left_RunExit") : TEXT("AS_Traversal_Vault_Left_StopExit");
		const FString PackageName = TEXT("/Game/Characters/Traversal/") + Name;
		auto* Result = LoadObject<UAnimSequence>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Result)
		{
			Result = NewObject<UAnimSequence>(CreatePackage(*PackageName), *Name, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Result);
			Result->SetSkeleton(Source->GetSkeleton());
		}
		const auto& Ref = Source->GetSkeleton()->GetReferenceSkeleton();
		TArray<FBoneIndexType> Required;
		for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone) { Required.Add(Bone); }
		FBoneContainer Container(Required, UE::Anim::FCurveFilterSettings(), *Source->GetSkeleton());
		TArray<FCompactPoseBoneIndex> Mirrors;
		TCustomBoneIndexArray<FQuat, FCompactPoseBoneIndex> ReferenceRotations;
		ReferenceRotations.SetNum(Ref.GetNum());
		for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
		{
			const FString BoneName = Ref.GetBoneName(Bone).ToString();
			FString Other = BoneName;
			if (Other.EndsWith(TEXT("_l"))) { Other = Other.LeftChop(2) + TEXT("_r"); }
			else if (Other.EndsWith(TEXT("_r"))) { Other = Other.LeftChop(2) + TEXT("_l"); }
			const int32 Pair = Ref.FindBoneIndex(*Other);
			Mirrors.Add(FCompactPoseBoneIndex(Pair == INDEX_NONE ? Bone : Pair));
			const int32 Parent = Ref.GetParentIndex(Bone);
			ReferenceRotations[FCompactPoseBoneIndex(Bone)] = Parent == INDEX_NONE ? Ref.GetRefBonePose()[Bone].GetRotation()
				: ReferenceRotations[FCompactPoseBoneIndex(Parent)] * Ref.GetRefBonePose()[Bone].GetRotation();
		}
		/** 每根真实骨骼的 30 FPS 轨道；虚拟骨骼由 Skeleton 在运行时重新求解。 */
		TArray<TArray<FTransform>> Tracks;
		Tracks.SetNum(Ref.GetRawBoneNum());
		FAnimPoseEvaluationOptions Options;
		Options.EvaluationType = EAnimDataEvalType::Raw;
		Options.bShouldRetarget = false;
		Options.bExtractRootMotion = false;
		FVector RootOrigin = FVector::ZeroVector;
		FVector LandedRoot = FVector::ZeroVector;
		for (int32 Frame = 0; Frame <= 42; ++Frame)
		{
			FMemMark Mark(FMemStack::Get());
			const double Time = Frame / 30.;
			FAnimPose SourcePose;
			FAnimPose LandPose;
			// run 主体为源片段 0.60–1.40 秒；最后 0.20 秒从脱手姿势平滑接入落地的预触地段。
			UAnimPoseExtensions::GetAnimPoseAtTime(Source, .6 + FMath::Min(Time, .8), Options, SourcePose);
			UAnimPoseExtensions::GetAnimPoseAtTime(Landing, FMath::Max(.3, .3 + Time - .6), Options, LandPose);
			if (!SourcePose.IsValid() || !LandPose.IsValid()) { return nullptr; }
			FCompactPose Mirrored;
			Mirrored.SetBoneContainer(&Container);
			for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
			{
				Mirrored[FCompactPoseBoneIndex(Bone)] = UAnimPoseExtensions::GetBonePose(SourcePose, Ref.GetBoneName(Bone), EAnimPoseSpaces::Local);
			}
			// 使用引擎的参考姿势补偿镜像，不能简单把手的位置取负而保留原骨轴旋转。
			FAnimationRuntime::MirrorPose(Mirrored, EAxis::X, Mirrors, ReferenceRotations);
			if (Frame == 0) { RootOrigin = Mirrored[FCompactPoseBoneIndex(0)].GetLocation(); }
			FVector Root = Mirrored[FCompactPoseBoneIndex(0)].GetLocation() - RootOrigin;
			if (Frame == 24) { LandedRoot = Root; }
			if (Frame > 24) { Root = LandedRoot; }
			for (int32 Bone = 0; Bone < Ref.GetRawBoneNum(); ++Bone)
			{
				FTransform Pose;
				Pose.Blend(Mirrored[FCompactPoseBoneIndex(Bone)], UAnimPoseExtensions::GetBonePose(LandPose, Ref.GetBoneName(Bone), EAnimPoseSpaces::Local),
					FMath::SmoothStep(.6, .8, Time));
				if (Bone == 0) { Pose = FTransform(FQuat::Identity, Root); }
				Tracks[Bone].Add(Pose);
			}
		}
		auto& Controller = Result->GetController();
		Controller.OpenBracket(FText::FromString(TEXT("Build single-hand traversal and landing")), false);
		Controller.InitializeModel();
		Controller.ResetModel(false);
		Controller.SetFrameRate(FFrameRate(30, 1), false);
		Controller.SetNumberOfFrames(FFrameNumber(42), false);
		for (int32 Bone = 0; Bone < Tracks.Num(); ++Bone)
		{
			TArray<FVector3f> Positions;
			TArray<FQuat4f> Rotations;
			TArray<FVector3f> Scales;
			for (const auto& Transform : Tracks[Bone])
			{
				Positions.Add(FVector3f(Transform.GetLocation()));
				Rotations.Add(FQuat4f(Transform.GetRotation()));
				Scales.Add(FVector3f(Transform.GetScale3D()));
			}
			if (!Controller.AddBoneCurve(Ref.GetBoneName(Bone), false)
				|| !Controller.SetBoneTrackKeys(Ref.GetBoneName(Bone), Positions, Rotations, Scales, false))
			{
				Controller.CloseBracket(false);
				return nullptr;
			}
		}
		Controller.NotifyPopulated();
		Controller.CloseBracket(false);
		Result->bEnableRootMotion = true;
		Result->bForceRootLock = true;
		Result->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
		Result->PostEditChange();
		Result->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Result->GetOutermost(), Result,
			*FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), Args) ? Result : nullptr;
	}
}

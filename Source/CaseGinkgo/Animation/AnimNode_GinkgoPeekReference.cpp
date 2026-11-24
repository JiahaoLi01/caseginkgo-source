#include "Animation/AnimNode_GinkgoPeekReference.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/GinkgoAnimInstance.h"

void FAnimNode_GinkgoPeekReference::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Source.Initialize(Context);
}

void FAnimNode_GinkgoPeekReference::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Source.CacheBones(Context);
	Bones.Reset();
	for (const FName Name : Ginkgo::Animation::PeekBoneNames())
	{
		FBoneReference& Bone = Bones.AddDefaulted_GetRef();
		Bone.BoneName = Name;
		Bone.Initialize(Context.AnimInstanceProxy->GetRequiredBones());
	}
}

void FAnimNode_GinkgoPeekReference::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Source.Update(Context);
}

void FAnimNode_GinkgoPeekReference::Evaluate_AnyThread(FPoseContext& Output)
{
	Source.Evaluate(Output);
	auto* Instance = Cast<UGinkgoAnimInstance>(Output.AnimInstanceProxy->GetAnimInstanceObject());
	if (!Instance || !Instance->IsPeekProbeRequested() || Bones.Num() != 5)
	{
		return;
	}
	const FBoneContainer& Required = Output.Pose.GetBoneContainer();
	for (const FBoneReference& Bone : Bones)
	{
		if (!Bone.IsValidToEvaluate(Required)) { return; }
	}
	FCSPose<FCompactPose> ComponentPose;
	ComponentPose.InitPose(Output.Pose);
	FGinkgoPeekReference Reference;
	Reference.bValid = true;
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		Reference.Bones[Index] = ComponentPose.GetComponentSpaceTransform(Bones[Index].GetCompactPoseIndex(Required));
	}
	Instance->RecordPeekReference(Reference);
}

void FAnimNode_GinkgoPeekReference::GatherDebugData(FNodeDebugData& DebugData)
{
	DebugData.AddDebugItem(DebugData.GetNodeName(this));
	Source.GatherDebugData(DebugData);
}

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "AnimNode_GinkgoPeekReference.generated.h"

/**
 * 原样透传姿势，并在探身叠加之前捕获骨盆、脊柱与枪的参考变换。
 * 不做世界查询、不修改骨骼；捕获结果由同一次求值中下游的 WeaponAim 求解，再供下一帧游戏线程测阻挡。
 */
USTRUCT(BlueprintInternalUseOnly)
struct CASEGINKGO_API FAnimNode_GinkgoPeekReference : public FAnimNode_Base
{
	GENERATED_BODY()

	/** 已完成持枪、AO、手臂动作和移动倾斜的输入姿势，尚未叠加探身 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Links)
	FPoseLink Source;

	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate_AnyThread(FPoseContext& Output) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

private:
	/** 按当前 RequiredBones 缓存的五个骨骼，LOD 切换时由 CacheBones 重建 */
	TArray<FBoneReference> Bones;
};

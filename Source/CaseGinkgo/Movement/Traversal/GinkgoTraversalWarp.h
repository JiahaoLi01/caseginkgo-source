#pragma once

#include "RootMotionModifier.h"
#include "GinkgoTraversalWarp.generated.h"

/**
 * Motion Warping 执行已预检的整段动画适配；本节点不查询碰撞、不夹方向、不追逐折线目标。
 * 同一累积轨迹做相邻时刻差分，避免每帧重新追赶端点产生速度突变。
 */
UCLASS(meta = (DisplayName = "Ginkgo Traversal Warp"))
class CASEGINKGO_API UGinkgoTraversalWarp : public URootMotionModifier
{
	GENERATED_BODY()

public:
	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override;
};

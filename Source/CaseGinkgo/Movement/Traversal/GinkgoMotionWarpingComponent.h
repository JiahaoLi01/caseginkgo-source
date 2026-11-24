#pragma once

#include "MotionWarpingComponent.h"
#include "GinkgoMotionWarpingComponent.generated.h"

/**
 * 项目根运动修正组件，补充按素材释放修正器的生命周期接口。
 * 引擎仅公开 Disable；禁用后若不再提取根运动，旧窗口会留在数组里，阻止下一次播放同一素材创建新窗口。
 */
UCLASS(ClassGroup = Movement)
class CASEGINKGO_API UGinkgoMotionWarpingComponent : public UMotionWarpingComponent
{
	GENERATED_BODY()

public:
	/** 游戏线程在本次移动完成后调用；仅释放指定素材的窗口，不修改其他动作的修正器或 Warp Target。 */
	void RemoveModifiersForAnimation(const UAnimSequenceBase* Animation);
};

#pragma once

#include "NativeGameplayTags.h"

/** 感知系统向受控角色的事件中心发布的三种感知结果。 */
namespace Ginkgo::Perception
{
	CASEGINKGO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TargetSeen);
	CASEGINKGO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GunshotHeard);
	CASEGINKGO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TargetLost);
}

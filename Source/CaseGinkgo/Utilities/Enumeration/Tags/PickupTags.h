#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * @namespace FPickupTags : 拾取的动作给的结局原因
 * 见 Docs/Design/Interaction.md「拾取子弹盒」。中止就是马上完成（给效果、清掉手里的东西、停掉蒙太奇）；取消是服务器拒绝了，不给。
 * 执行器自己的原因在 FAnimActionTags。手写 `extern CASEGINKGO_API` 的原因同那里。
 */
namespace FPickupTags
{
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Abort_OutOfReach;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Abort_Timeout;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Abort_Posture;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Cancel_Rejected;
}

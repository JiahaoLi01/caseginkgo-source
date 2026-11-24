#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * @namespace FAnimActionTags : 动画动作的结局原因与占用的部位
 * 执行器（FGinkgoAnimActionRunner）自己给出的原因；动作与拥有者给的原因（够不着、退出状态）在各自那里定义。
 * 见 Docs/Design/AnimActions.md「结局」「占用」。
 *
 * 手写 `extern CASEGINKGO_API` 而不用 `UE_DECLARE_GAMEPLAY_TAG_EXTERN`：引擎的宏不带导出标记，
 * 测试模块（CaseGinkgoTests，另一个 DLL）引用时会链接失败（LNK2001）。
 */
namespace FAnimActionTags
{
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Abort_Interrupted;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Abort_PlayFailed;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Abort_InstanceLost;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Abort_Occupied;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Cancel_Replaced;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Cancel_OwnerExit;
	extern CASEGINKGO_API FNativeGameplayTag AnimAction_Cancel_Preempted;

	extern CASEGINKGO_API FNativeGameplayTag Occupancy_LeftArm;
	extern CASEGINKGO_API FNativeGameplayTag Occupancy_LeftArm_Hand;
	extern CASEGINKGO_API FNativeGameplayTag Occupancy_RightArm;
	extern CASEGINKGO_API FNativeGameplayTag Occupancy_RightArm_Hand;
}

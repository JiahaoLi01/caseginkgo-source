#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * @namespace FMovementTags : 移动状态标签
 * 移动组件在每帧移动结束后覆盖写进视角目标上的相机上下文组件；相机修改器的激活条件（FGameplayTagQuery）
 * 只读这些标签，不依赖任何组件类型。见 Docs/Design/CameraPipeline.md「修改器激活：相机上下文组件」。
 *
 * 手写 `extern CASEGINKGO_API` 而不用 `UE_DECLARE_GAMEPLAY_TAG_EXTERN`：引擎的宏不带导出标记，
 * 测试模块（CaseGinkgoTests，另一个 DLL）引用时会链接失败（LNK2001）。
 */
namespace FMovementTags
{
	extern CASEGINKGO_API FNativeGameplayTag Movement_Sprint;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Slide;
	extern CASEGINKGO_API FNativeGameplayTag Movement_WallRun;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Grapple;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Grounded;

	// ===== 相机上下文：连续量与事件 =====
	// 移动组件每次移动后覆盖写连续量；事件只在第一次模拟时发（客户端重放不发）

	extern CASEGINKGO_API FNativeGameplayTag Movement_Value_HorizontalSpeed;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Value_SlideBoost;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Value_RunSpeed;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Event_SlideStarted;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Event_Landed;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Event_Footstep;

	// ===== 窗口：倍率类别 =====
	// 移动组件在代码里查询的类别

	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_Effect_SlideBoost;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_Effect_JumpPenalty;

	// ===== 窗口：能力 =====
	// 移动组件或角色在代码里查询的能力

	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_Ability_JumpWhileFalling;

	// ===== 窗口：移动配置默认带的窗口 =====
	// 定义在 UGinkgoMovementConfig 的构造函数里（类默认值，配置资产从它开始），所以标签也在这里

	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_Coyote;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_SlideFatigue;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_JumpFatigue;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_JumpFatigue_Armed;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_JumpFatigue_Recovering;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_Lurch;

#if !UE_BUILD_SHIPPING
	// ===== 窗口：调试用的探针（-GinkgoWindowProbe） =====

	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_Probe;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_Probe_A;
	extern CASEGINKGO_API FNativeGameplayTag Movement_Window_Probe_B;
#endif
}

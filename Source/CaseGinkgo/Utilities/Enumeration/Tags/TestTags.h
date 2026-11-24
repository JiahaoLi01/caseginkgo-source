#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * @namespace FTestTags : 自动化测试用的标签
 * 只给测试模块（CaseGinkgoTests）用，发行版与 Test 配置里没有（WITH_DEV_AUTOMATION_TESTS）。
 *
 * 放在运行时模块里而不是测试模块里：5.8 要求原生标签定义在 Runtime 或 RuntimeAndProgram 类型的模块里（两端的标签表要一致）。
 * 定义在 DeveloperTool 模块里时，每个加载它的进程（编辑器、游戏、专用服务器）启动时每个标签都触发一次 ensure，
 * 写一份崩溃报告并启动 CrashReportClient，它在后台处理转储时会拖慢正在跑的测量。
 * 根是 GinkgoTest，不在 Movement.Window 下，编辑器里窗口定义的标签选择器看不到它们。
 */
namespace FTestTags
{
	// ===== 窗口 =====

	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Window_A;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Window_A_First;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Window_A_Second;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Window_B;

	// ===== 倍率类别与能力 =====

	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Effect;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Effect_Speed;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Effect_Jump;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Ability;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Ability_AirJump;

	// ===== 动画动作 =====

	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_AnimAction_Abort;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_AnimAction_Branch;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_AnimAction_Stop;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_AnimAction_Camera;

	// ===== 弹种 =====

	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Ammo_Special;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Ammo_Rifle;
	extern CASEGINKGO_API FNativeGameplayTag GinkgoTest_Ammo_Shell;
}

#endif // WITH_DEV_AUTOMATION_TESTS

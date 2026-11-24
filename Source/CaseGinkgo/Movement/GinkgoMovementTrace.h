#pragma once

#include "CoreMinimal.h"
#include "ObjectTraceDefines.h"  // OBJECT_TRACE_ENABLED
#include "Trace/Config.h"        // UE_TRACE_ENABLED

/** 移动的追踪（给 Rewind Debugger 用）：发行版不编译，没有对象追踪时也不编译 */
#define GINKGO_MOVEMENT_TRACE_ENABLED (UE_TRACE_ENABLED && OBJECT_TRACE_ENABLED && !UE_BUILD_SHIPPING)

#if GINKGO_MOVEMENT_TRACE_ENABLED

class UGinkgoCharacterMovementComponent;
class UGinkgoMovementWindow;
struct FGinkgoWindowEvent;

/**
 * @struct FGinkgoMovementTrace : 移动的追踪
 * 写进 UE Trace 的通道 GinkgoMovement：Rewind Debugger 开始录制时由编辑器模块（CaseGinkgoEditor）打开，
 * 无界面运行时用命令行 -trace=GinkgoMovement,Object 打开。通道关着时每个入口只多一次判断。
 *
 * 四种事件，都带录制时间（世界从录制开始经过的时间，Rewind Debugger 的时间轴用它）、引擎帧号与移动组件的对象编号：
 * 1. 窗口快照（WindowState）：每次移动结束时的活动窗口（定义、阶段、已过时间、时长、参数）、各倍率类别的值、屏蔽的类别、能力。
 *    客户端重放旧的移动时也记，另作标记：时间轴只按第一次模拟的快照画，重放的快照说明这一帧发生过校正
 * 2. 窗口事件（WindowEvent）：起跳、落地、未起跳离地、滑铲开始与结束
 * 3. 窗口变化（WindowChange）：推入、换阶段、结束，以及有没有发给表现层（客户端重放时不发）
 * 4. 校正（Correction）：客户端收到的服务器校正与位置差
 *
 * 标签按 FName 编号，名字用 Important 事件发一次：晚开始的录制也会收到之前发过的名字。
 * 分析与显示在编辑器模块里（Trace/GinkgoMovementTraceAnalyzer、Provider、RewindDebugger）。
 */
struct CASEGINKGO_API FGinkgoMovementTrace
{
	/** 追踪通道的名字 */
	static const TCHAR* ChannelName;

	/** 通道开着：调用方据此跳过准备数据 */
	static bool IsEnabled();

	/** 一次移动结束时的窗口快照 */
	static void TraceWindowState(const UGinkgoCharacterMovementComponent& Movement);

	/** 一次窗口事件，在交给窗口管理器之前记 */
	static void TraceWindowEvent(const UGinkgoCharacterMovementComponent& Movement, const FGinkgoWindowEvent& Event);

	/**
	 * @brief 一次窗口变化
	 * @param bDispatched - 发给了表现层；客户端重放时为假
	 */
	static void TraceWindowChange(const UGinkgoCharacterMovementComponent& Movement, int32 DefinitionIndex, const UGinkgoMovementWindow* Definition,
		int32 FromPhase, int32 ToPhase, bool bDispatched);

	/**
	 * @brief 客户端收到的一次校正
	 * @param PositionError - 被校正那次移动结束时客户端与服务器的位置差（cm）；基于底座的相对位置时为 -1
	 */
	static void TraceCorrection(const UGinkgoCharacterMovementComponent& Movement, float PositionError);
};

#endif // GINKGO_MOVEMENT_TRACE_ENABLED

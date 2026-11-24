#pragma once

#include "CoreMinimal.h"
#include "GinkgoCameraModifiers.h"  // FGinkgoCameraOverlayState、FGinkgoCameraOverlayStep：状态与输入是值类型

class UGinkgoCameraConfig;

/**
 * @struct FGinkgoCameraOverlayLayer : ⑤ 叠加层的状态
 * 玩家相机组件持有，下标同配置里的叠加效果。每帧推进全部效果，输出相加；碰撞之后只改旋转与 FOV。
 * 纯计算，不碰世界，单独测试（CaseGinkgo.Camera.Overlay.*）。
 */
struct CASEGINKGO_API FGinkgoCameraOverlayLayer
{
	/**
	 * @brief 推进一帧
	 * @param Config - 相机配置里的叠加效果，每帧读取
	 * @param Input - 上下文、这一帧的事件、帧长与时钟
	 * @param bEnabled - 叠加层开着；关着时（调试的 ginkgo.camera.overlay 0）输出 0、状态清空，对照用
	 * @return 各效果的输出之和：视角空间里的旋转（度）与 FOV（度）
	 */
	FGinkgoCameraOverlayOutput Step(const UGinkgoCameraConfig& Config, const FGinkgoCameraOverlayStep& Input, bool bEnabled = true);

	/** @brief 下一次推进从静止开始：第一帧、换视角目标、传送之后 */
	void Reset() { States.Reset(); }

	/** 各效果的状态，下标同配置 */
	TConstArrayView<FGinkgoCameraOverlayState> GetStates() const { return States; }

	/**
	 * @brief 叠加的旋转加到视角上：在视角空间里转（先视角、再叠加），抬头时的偏航仍是绕相机自己的上方
	 * @param View - 视角（加上参数层的侧倾）
	 * @param Overlay - 叠加的旋转（视角空间）
	 */
	static FRotator Compose(const FRotator& View, const FRotator& Overlay);

private:
	TArray<FGinkgoCameraOverlayState> States;
};

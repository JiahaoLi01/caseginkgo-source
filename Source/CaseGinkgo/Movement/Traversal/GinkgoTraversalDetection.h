#pragma once

#include "GinkgoTraversalTypes.h"

class UWorld;

namespace Ginkgo::Traversal
{
	/** 相机前向投影到世界 XY；垂直视角使用相机 Yaw，非有限旋转返回零，永不读取角色朝向。 */
	CASEGINKGO_API FVector CameraDirection(const FRotator& CameraRotation);

	/**
	 * @brief 在给定角色快照前方寻找可用的翻越或攀爬几何方案，不启动动作、不移动胶囊。
	 * @param Probes - 可选探测记录，进入时清空；调试关闭时传空，不积累历史状态。
	 * @note 本阶段支持静止碰撞体。顶部站立失败只有在明确检测到薄墙和有效背后落点时才尝试翻越。
	 */
	CASEGINKGO_API FGinkgoTraversalResult Detect(const UWorld& World, const FGinkgoTraversalQuery& Query,
		const FGinkgoTraversalSettings& Settings, TArray<FGinkgoTraversalProbe>* Probes = nullptr);

	/** 英文调试说明，引擎屏幕调试字体不保证中文字形；不作为玩法判断依据。 */
	CASEGINKGO_API const TCHAR* Describe(EGinkgoTraversalFailure Failure);

	/** 英文调试动作名，与动画资源名字解耦。 */
	CASEGINKGO_API const TCHAR* Describe(EGinkgoTraversalKind Kind);
}

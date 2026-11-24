#pragma once

#include "CoreMinimal.h"

class AGinkgoPlayerCameraManager;
class UGinkgoCameraConfig;
class UWorld;

/**
 * 相机的调试工具（发行版没有这个模块）：
 * - GinkgoCameraDebug.cpp：ginkgo.showCamera、ginkgo.camera.showLayers / curves / poseView，两层的开关 ginkgo.camera.modifiers / overlay；
 * - GinkgoCameraConfigPath.cpp：按路径读写配置的一项，ginkgo.camera.set / get / list；
 * - GinkgoCameraTuning.cpp：调参面板（weft 页面，F7 或 ginkgo.camera.tune）与本地的调参文件。
 * 都挂在主模块的钩子上（AGinkgoPlayerCameraManager::OnDebugView：相机这一帧算完以后），主模块不知道这里
 */
namespace Ginkgo::Debug::Camera
{
	/** @brief 模块启动时挂上：相机算完以后的钩子、画布上的曲线、调参面板的热键 */
	void Startup();

	/** @brief 模块关闭时摘下 */
	void Shutdown();

	/** 这个世界里本地玩家的 Ginkgo 相机；没有时为空 */
	AGinkgoPlayerCameraManager* FindLocalCamera(const UWorld* World);

	/** 相机正在用的配置（资产或类默认值），可写：调试工具直接改它（PIE 里也生效，不存盘） */
	UGinkgoCameraConfig& GetMutableConfig(const AGinkgoPlayerCameraManager& Camera);

	/** @brief 调参面板与本地的调参文件（GinkgoCameraTuning.cpp）：挂上热键与控制台命令；每帧看到相机时记下默认值、套用本地文件 */
	void StartupTuning();
	void ShutdownTuning();
	void OnCameraSeen(AGinkgoPlayerCameraManager& Camera);
}

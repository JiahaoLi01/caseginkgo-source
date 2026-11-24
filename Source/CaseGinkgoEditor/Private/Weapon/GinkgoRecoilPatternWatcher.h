#pragma once

#include "CoreMinimal.h"
#include "IDirectoryWatcher.h"

/**
 * @class FGinkgoRecoilPatternWatcher : 网页工具（Tools/RecoilEditor）存了 .recoil，编辑器里对应的模板马上换，下一发就是新的
 * 只盯 Tools/RecoilEditor/patterns 这一个目录。引擎自带的自动重导入要改编辑器的偏好：默认每次都问，
 * 不问的话 Content/UI 里新加的 PNG 也会被自动导入成贴图。
 * 文件内容没变（同记下的哈希一样）不重读。别的目录里的 .recoil 照常手动 Reimport
 */
class FGinkgoRecoilPatternWatcher
{
public:
	/** @brief 开始盯：只在有界面的编辑器里，命令行工具不盯 */
	void Start();

	/** @brief 不盯了 */
	void Stop();

private:
	/**
	 * @brief 目录里有文件变了
	 * @param Changes 变了的文件
	 */
	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);

	/** 盯的目录（绝对路径） */
	FString Directory;

	/** 目录监视的句柄 */
	FDelegateHandle WatchHandle;
};

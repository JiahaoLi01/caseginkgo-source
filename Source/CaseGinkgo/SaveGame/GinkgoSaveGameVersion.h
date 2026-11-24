#pragma once

#include "CoreMinimal.h"

/**
 * @class FGinkgoSaveGameVersion : 全局的存档格式版本
 * 只给跨节点的格式变更用；节点自己的字段演进走节点的版本（节点的 GUID 同时是版本 GUID），不要改这里。
 * 读档时这个版本缺失或比当前的新，就当作不是本项目能读的存档。
 */
class CASEGINKGO_API FGinkgoSaveGameVersion
{
public:
	enum Type
	{
		/** 节点化的格式：文件头 → 各节点的数据 → 节点目录 → 版本表 */
		Initial,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	static const FGuid GUID;
};

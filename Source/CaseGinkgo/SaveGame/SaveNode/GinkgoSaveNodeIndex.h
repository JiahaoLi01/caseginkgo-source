#pragma once

#include "CoreMinimal.h"

class FArchive;

/**
 * @struct FGinkgoSaveNodeIndexEntry : 存档末尾节点目录里的一条
 * 读档时按节点 ID 直接跳到它的数据，不用按写入顺序一个个解析，也才能跳过认不得的节点。
 */
struct FGinkgoSaveNodeIndexEntry
{
	FGuid NodeId;
	uint64 Offset = 0;
	uint64 Size = 0;

	friend FArchive& operator<<(FArchive& Ar, FGinkgoSaveNodeIndexEntry& Entry)
	{
		Ar << Entry.NodeId;
		Ar << Entry.Offset;
		Ar << Entry.Size;
		return Ar;
	}
};

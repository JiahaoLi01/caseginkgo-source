#pragma once

#include "CoreMinimal.h"
#include "SaveGame/GinkgoSaveGameProxyArchive.h"        // TGinkgoSaveGameProxyArchive
#include "SaveGame/SaveNode/GinkgoSaveNodeIndex.h"      // FGinkgoSaveNodeIndexEntry
#include "Serialization/Formatters/BinaryArchiveFormatter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"

#if WITH_TEXT_ARCHIVE_SUPPORT
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"
#endif

class UGinkgoSaveGameSubsystem;

/** 让子系统能用一个指针持有正在进行的读档，不用知道模板参数 */
class FGinkgoSaveGameSerializer : public TSharedFromThis<FGinkgoSaveGameSerializer>
{
public:
	virtual ~FGinkgoSaveGameSerializer() = default;
};

/**
 * @class TGinkgoSaveGameSerializer : 节点化的存档读写
 *
 * 文件的布局（设计见 Docs/Design/SaveGame.md）：
 *  - 文件头：地图名、引擎与包版本、节点目录的位置、版本表的位置（后两个只有二进制有）
 *  - 各节点的数据：按 FGinkgoSaveNodeRegistry 的拓扑序
 *  - 节点目录（只有二进制有）：(节点 ID, 偏移, 大小)[]
 *  - 版本表：全局版本 + 每个节点的版本
 * 二进制的文件整个用 zlib 压缩，前面是压缩前的大小。
 *
 * 读档的流程：读文件头 → 读版本表 → 读节点目录 → 存档就在当前地图时原地恢复，否则 SeamlessTravel 过去 → 按拓扑序调度节点。
 *
 * @tparam bIsLoading 是否在读档
 * @tparam bIsTextFormat 文本格式（开发期的 JSON，只能存不能读）
 */
template <bool bIsLoading, bool bIsTextFormat = false>
class TGinkgoSaveGameSerializer final : public FGinkgoSaveGameSerializer
{
	using FSaveGameMemoryArchive = std::conditional_t<bIsLoading, FMemoryReader, FMemoryWriter>;

	static_assert(!bIsLoading || !bIsTextFormat, "文本格式只能存，不能读");
	static_assert(WITH_TEXT_ARCHIVE_SUPPORT || !bIsTextFormat, "这个构建没有文本存档的支持");

#if WITH_TEXT_ARCHIVE_SUPPORT
	using FSaveGameFormatter = std::conditional_t<bIsTextFormat, FJsonArchiveOutputFormatter, FBinaryArchiveFormatter>;
#else
	using FSaveGameFormatter = FBinaryArchiveFormatter;
#endif

public:
	explicit TGinkgoSaveGameSerializer(UGinkgoSaveGameSubsystem* InSaveSubsystem);

	bool Save(const FString& SlotName);
	/** bReloadMap 用于检查点复活；默认保持普通读档原有的同图原地恢复语义。 */
	bool Load(const FString& SlotName, bool bReloadMap = false);

private:
	static FString GetSaveName(const FString& InputName);

	void OnMapLoad(UWorld* InWorld);

	/**
	 * 文件头。
	 * 实现注意：存档时调用两次，第一次写占位的偏移，全部写完后回到开头再写一次真的偏移；两次的字节数必须相同，靠地图名只取一次。
	 */
	void SerializeHeader();

	/** 存档：按拓扑序写所有节点，同时记下节点目录 */
	void SerializeNodes();

	/** 读写存档末尾的节点目录（只有二进制有） */
	void SerializeNodeIndex();

	/** 读写版本表 */
	void SerializeVersions();

	/**
	 * 读档：按拓扑序调度每个注册的节点。
	 * 第一遍恢复数据：目录里有的 Seek 过去调 Serialize；没有的调 OnMissingFromSave；存档的版本比代码新时中止。
	 * 第二遍调所有节点的 PostLoadGame。
	 * 目录里有、现在没注册的节点直接跳过。
	 */
	void DispatchNodesOnLoad();

	const TWeakObjectPtr<UGinkgoSaveGameSubsystem> SaveSubsystem;
	TArray<uint8> Data;
	FSaveGameMemoryArchive Archive;
	TGinkgoSaveGameProxyArchive<bIsLoading> ProxyArchive;
	FSaveGameFormatter Formatter;
	FStructuredArchive StructuredArchive;

	FStructuredArchive::FSlot RootSlot;
	FStructuredArchive::FRecord RootRecord;

	FString MapName;
	uint64 NodeIndexOffset = 0;
	uint64 VersionOffset = 0;

	/** 存档时累积；读档时从文件末尾读出 */
	TArray<FGinkgoSaveNodeIndexEntry> NodeIndex;
};

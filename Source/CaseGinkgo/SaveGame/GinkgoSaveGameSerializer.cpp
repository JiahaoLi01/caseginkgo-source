#include "SaveGame/GinkgoSaveGameSerializer.h"

#include "Engine/World.h"
#include "Misc/Compression.h"
#include "Misc/EngineVersion.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "PlatformFeatures.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "SaveGame/GinkgoSaveGameVersion.h"
#include "SaveGame/SaveNode/GinkgoSaveNode.h"
#include "SaveGame/SaveNode/GinkgoSaveNodeContext.h"
#include "SaveGame/SaveNode/GinkgoSaveNodeRegistry.h"
#include "SaveGameSystem.h"
#include "UObject/CoreRedirects.h"
#include "UObject/Package.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"

DECLARE_CYCLE_STAT(TEXT("Save: header"), STAT_GinkgoSaveHeader, STATGROUP_Ginkgo3C);
DECLARE_CYCLE_STAT(TEXT("Save: nodes"), STAT_GinkgoSaveNodes, STATGROUP_Ginkgo3C);
DECLARE_CYCLE_STAT(TEXT("Save: node index"), STAT_GinkgoSaveNodeIndex, STATGROUP_Ginkgo3C);
DECLARE_CYCLE_STAT(TEXT("Save: versions"), STAT_GinkgoSaveVersions, STATGROUP_Ginkgo3C);
DECLARE_CYCLE_STAT(TEXT("Save: dispatch nodes on load"), STAT_GinkgoSaveDispatchNodes, STATGROUP_Ginkgo3C);
DECLARE_CYCLE_STAT(TEXT("Save: post load game"), STAT_GinkgoSavePostLoad, STATGROUP_Ginkgo3C);

namespace
{
	/** 解压后比这个还大就当文件坏了，不去分配 */
	constexpr int64 MaxSaveSize = 256ll * 1024 * 1024;
	constexpr int64 SizePrefix = sizeof(int64);
	constexpr int32 MaxNodeCount = 4096;

	FString GetSaveMapName(const UWorld* World)
	{
		const FString Loaded = World->GetOutermost()->GetLoadedPath().GetPackageName();
		return Loaded.IsEmpty() ? World->GetOutermost()->GetName() : Loaded;
	}

	/**
	 * 压缩整个存档，文件开头是压缩前的大小。
	 * 为什么不用 FArchive::SerializeCompressed：它读到不对的块头时直接 Fatal，文件坏了游戏就跟着崩；FCompression 失败时只返回 false。
	 */
	bool CompressSave(const TArray<uint8>& Raw, TArray<uint8>& OutFile)
	{
		int64 CompressedSize = 0;
		if (!FCompression::CompressMemoryBound(NAME_Zlib, CompressedSize, Raw.Num()))
		{
			return false;
		}
		OutFile.SetNumUninitialized(SizePrefix + CompressedSize);

		const int64 RawSize = Raw.Num();
		FMemory::Memcpy(OutFile.GetData(), &RawSize, SizePrefix);
		if (!FCompression::CompressMemory(NAME_Zlib, OutFile.GetData() + SizePrefix, CompressedSize, Raw.GetData(), RawSize))
		{
			return false;
		}
		OutFile.SetNum(SizePrefix + CompressedSize);
		return true;
	}

	bool UncompressSave(const TArray<uint8>& File, TArray<uint8>& OutRaw)
	{
		int64 RawSize = 0;
		if (File.Num() <= SizePrefix)
		{
			return false;
		}
		FMemory::Memcpy(&RawSize, File.GetData(), SizePrefix);
		if (RawSize <= 0 || RawSize > MaxSaveSize)
		{
			return false;
		}
		OutRaw.SetNumUninitialized(RawSize);
		return FCompression::UncompressMemory(NAME_Zlib, OutRaw.GetData(), RawSize, File.GetData() + SizePrefix, File.Num() - SizePrefix);
	}
}

template <bool bIsLoading, bool bIsTextFormat>
TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::TGinkgoSaveGameSerializer(UGinkgoSaveGameSubsystem* InSaveSubsystem)
	: SaveSubsystem(InSaveSubsystem)
	, Archive(Data)
	, ProxyArchive(Archive)
	, Formatter(ProxyArchive)
	, StructuredArchive(Formatter)
	, RootSlot(StructuredArchive.Open())
	, RootRecord(RootSlot.EnterRecord())
{
	static_cast<FArchive&>(ProxyArchive).SetIsTextFormat(bIsTextFormat);

	Archive.UsingCustomVersion(FGinkgoSaveGameVersion::GUID);
}

template <bool bIsLoading, bool bIsTextFormat>
bool TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::Save(const FString& SlotName)
{
	check(!bIsLoading);

	TRACE_BOOKMARK(TEXT("Begin: SaveGame[%s]"), bIsTextFormat ? TEXT("Text") : TEXT("Binary"));

	ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (!SaveSystem)
	{
		return false;
	}

	// 文件头，偏移先占位
	SerializeHeader();

	SerializeNodes();
	if (Archive.IsError())
	{
		return false;
	}

	if (!bIsTextFormat)
	{
		NodeIndexOffset = Archive.Tell();
	}
	SerializeNodeIndex();

	if (!bIsTextFormat)
	{
		VersionOffset = Archive.Tell();
	}
	SerializeVersions();

	// 回填文件头里的偏移
	if (!bIsTextFormat)
	{
		Archive.Seek(0);
		SerializeHeader();
	}

	StructuredArchive.Close();

	if (bIsTextFormat)
	{
		return SaveSystem->SaveGame(false, *GetSaveName(SlotName), 0, Data);
	}

	TArray<uint8> File;
	return CompressSave(Data, File) && SaveSystem->SaveGame(false, *GetSaveName(SlotName), 0, File);
}

template <bool bIsLoading, bool bIsTextFormat>
bool TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::Load(const FString& SlotName, const bool bReloadMap)
{
	check(bIsLoading && !bIsTextFormat);

	TRACE_BOOKMARK(TEXT("Begin: LoadSaveGame[%s]"), bIsTextFormat ? TEXT("Text") : TEXT("Binary"));

	TArray<uint8> File;
	ISaveGameSystem* SaveGameSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem();
	if (!SaveGameSystem || !SaveGameSystem->LoadGame(false, *GetSaveName(SlotName), 0, File))
	{
		return false;
	}

	if (!UncompressSave(File, Data))
	{
		return false;
	}

	// 文件头：拿到后面两段的位置
	SerializeHeader();
	if (Archive.IsError() || NodeIndexOffset >= uint64(Data.Num()) || VersionOffset >= uint64(Data.Num()))
	{
		return false;
	}

	// 版本表要先读：调度时要查每个节点存档时的版本
	{
		const uint64 InitialPosition = Archive.Tell();
		ON_SCOPE_EXIT
		{
			Archive.Seek(InitialPosition);
		};

		Archive.Seek(VersionOffset);
		SerializeVersions();
	}
	if (Archive.IsError() || Archive.CustomVer(FGinkgoSaveGameVersion::GUID) < 0 || Archive.CustomVer(FGinkgoSaveGameVersion::GUID) > FGinkgoSaveGameVersion::LatestVersion)
	{
		SaveSubsystem->LastError = TEXT("存档格式不属于当前项目或版本不受支持。");
		return false;
	}

	{
		const uint64 InitialPosition = Archive.Tell();
		ON_SCOPE_EXIT
		{
			Archive.Seek(InitialPosition);
		};

		Archive.Seek(NodeIndexOffset);
		SerializeNodeIndex();
	}

	if (MapName.IsEmpty() || Archive.IsError())
	{
		return false;
	}

	check(SaveSubsystem.IsValid());
	UWorld* World = SaveSubsystem->GetWorld();
	if (!World || World->IsInSeamlessTravel())
	{
		return false;
	}
	if (bReloadMap && World->GetNetMode() == NM_Client)
	{
		SaveSubsystem->LastError = TEXT("客户端不能单独重载权威检查点地图。");
		return false;
	}

	// 存档就在当前地图：原地恢复，不重新加载关卡
	if (!bReloadMap && GetSaveMapName(World) == MapName)
	{
		OnMapLoad(World);
		return !Archive.IsError();
	}

	// 地图不在了要在这里报：SeamlessTravel 失败不回调，读档的状态就永远结束不了
	if (!FPackageName::DoesPackageExist(MapName))
	{
		SaveSubsystem->LastError = FString::Printf(TEXT("存档所在的地图不存在：%s"), *MapName);
		return false;
	}

	FCoreUObjectDelegates::PostLoadMapWithWorld.AddThreadSafeSP(this, &TGinkgoSaveGameSerializer::OnMapLoad);
	if (bReloadMap)
	{
		// 干净重建关卡：存档后死亡的敌人和生成的掉落物不能残留；普通读档保留原有行为。
		if (World->GetNetMode() == NM_Standalone) { UGameplayStatics::OpenLevel(World, FName(MapName)); }
		else if (!World->ServerTravel(MapName + TEXT("?restart"), true))
		{
			FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
			SaveSubsystem->LastError = TEXT("服务器未能启动检查点地图重载。");
			return false;
		}
	}
	else { World->SeamlessTravel(MapName, true); }

	return true;
}

template <bool bIsLoading, bool bIsTextFormat>
FString TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::GetSaveName(const FString& InputName)
{
	return bIsTextFormat ? InputName + TEXT(".json") : InputName;
}

template <bool bIsLoading, bool bIsTextFormat>
void TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::OnMapLoad(UWorld* InWorld)
{
	// 这个委托是全局的，多开 PIE 时别的实例载入地图也会到这里
	if (!SaveSubsystem.IsValid() || SaveSubsystem->GetWorld() != InWorld)
	{
		return;
	}

	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);

	DispatchNodesOnLoad();
	if (Archive.IsError())
	{
		SaveSubsystem->LastError = TEXT("存档数据恢复失败，已中止后续加载。详情见日志。");
		SaveSubsystem->CurrentSerializer.Reset();
		return;
	}

	SaveSubsystem->OnLoadComplete();
	TRACE_BOOKMARK(TEXT("End: LoadSaveGame[%s]"), bIsTextFormat ? TEXT("Text") : TEXT("Binary"));
}

template <bool bIsLoading, bool bIsTextFormat>
void TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeHeader()
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveHeader);

	// 地图名只取一次：回填时写同样的字节数
	if (!bIsLoading && MapName.IsEmpty())
	{
		check(SaveSubsystem.IsValid());
		MapName = GetSaveMapName(SaveSubsystem->GetWorld());
	}

	RootRecord << SA_VALUE(TEXT("Map"), MapName);

	FEngineVersion EngineVersion;
	FPackageFileVersion PackageVersion;

	if (!bIsLoading)
	{
		EngineVersion = FEngineVersion::Current();
		PackageVersion = GPackageFileUEVersion;
	}

	RootRecord << SA_VALUE(TEXT("EngineVersion"), EngineVersion);

	if (!bIsTextFormat)
	{
		// 不经过结构化存档，要原始读写
		Archive << PackageVersion;

		RootRecord << SA_VALUE(TEXT("NodeIndexOffset"), NodeIndexOffset);
		RootRecord << SA_VALUE(TEXT("VersionOffset"), VersionOffset);
	}

	if (bIsLoading)
	{
		Archive.SetEngineVer(EngineVersion);
		Archive.SetUEVer(PackageVersion);
	}
}

template <bool bIsLoading, bool bIsTextFormat>
void TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeNodes()
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveNodes);

	check(!bIsLoading);

	FGinkgoSaveNodeRegistry& Registry = SaveSubsystem->GetRegistry();
	const TArray<FGuid>& SaveOrder = Registry.GetSaveOrder();

	NodeIndex.Reset();
	NodeIndex.Reserve(SaveOrder.Num());

	auto AddRedirectFn = [this](const FSoftObjectPath& From, const FSoftObjectPath& To)
	{
		ProxyArchive.AddRedirect(From, To);
	};

	for (const FGuid& NodeId : SaveOrder)
	{
		IGinkgoSaveNode* Node = Registry.Find(NodeId);
		check(Node);

		// 记下这个节点的版本，写进版本表
		Archive.UsingCustomVersion(NodeId);
		Archive.SetCustomVersion(NodeId, Node->GetCurrentVersion(), Node->GetClassificationTag().GetTagName());

		// 文本格式用分类标签做 JSON 的键（给人看）；没设或重名时退回 GUID。二进制里字段名不进文件
		const FGameplayTag ClassificationTag = Node->GetClassificationTag();
		const FString NodeFieldName = (bIsTextFormat && ClassificationTag.IsValid())
			? ClassificationTag.ToString()
			: NodeId.ToString(EGuidFormats::DigitsWithHyphens);
		const uint64 BeginOffset = Archive.Tell();

		FStructuredArchive::FRecord NodeRecord = RootRecord.EnterRecord(*NodeFieldName);

		// 文本格式多写一个节点 ID，方便和版本表对照
		if (bIsTextFormat)
		{
			FString NodeIdField = NodeId.ToString(EGuidFormats::DigitsWithHyphens);
			NodeRecord.EnterField(TEXT("_NodeId")) << NodeIdField;
		}

		// 上下文析构时才写字段表，要在算大小之前结束
		{
			FGinkgoSaveNodeContext Ctx(NodeRecord, Node->GetCurrentVersion(), /*bIsLoading*/ false, SaveSubsystem.Get(), AddRedirectFn);
			Node->Serialize(Ctx);
		}
		if (Archive.IsError())
		{
			return;
		}

		NodeIndex.Add({NodeId, BeginOffset, Archive.Tell() - BeginOffset});
	}
}

template <bool bIsLoading, bool bIsTextFormat>
void TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeNodeIndex()
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveNodeIndex);

	if (bIsTextFormat)
	{
		return;
	}

	FArchive& RawArchive = static_cast<FArchive&>(ProxyArchive);

	int32 EntryCount = NodeIndex.Num();
	RawArchive << EntryCount;
	if (RawArchive.IsLoading() && (EntryCount < 0 || EntryCount > MaxNodeCount))
	{
		RawArchive.SetError();
		return;
	}

	if (bIsLoading)
	{
		NodeIndex.SetNum(EntryCount);
	}

	for (FGinkgoSaveNodeIndexEntry& Entry : NodeIndex)
	{
		RawArchive << Entry;
		if (bIsLoading && (Entry.Offset > uint64(Data.Num()) || Entry.Size > uint64(Data.Num()) - Entry.Offset))
		{
			RawArchive.SetError();
			return;
		}
	}
}

template <bool bIsLoading, bool bIsTextFormat>
void TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::SerializeVersions()
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveVersions);

	FCustomVersionContainer VersionContainer;
	if (!bIsLoading)
	{
		VersionContainer = Archive.GetCustomVersions();
	}

	VersionContainer.Serialize(RootRecord.EnterField(TEXT("Versions")));

	if (bIsLoading)
	{
		Archive.SetCustomVersions(VersionContainer);
	}
}

template <bool bIsLoading, bool bIsTextFormat>
void TGinkgoSaveGameSerializer<bIsLoading, bIsTextFormat>::DispatchNodesOnLoad()
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveDispatchNodes);

	check(bIsLoading);

	TMap<FGuid, FGinkgoSaveNodeIndexEntry> IndexMap;
	IndexMap.Reserve(NodeIndex.Num());
	for (const FGinkgoSaveNodeIndexEntry& Entry : NodeIndex)
	{
		IndexMap.Add(Entry.NodeId, Entry);
	}

	FGinkgoSaveNodeRegistry& Registry = SaveSubsystem->GetRegistry();
	const TArray<FGuid>& LoadOrder = Registry.GetSaveOrder();

	auto AddRedirectFn = [this](const FSoftObjectPath& From, const FSoftObjectPath& To)
	{
		ProxyArchive.AddRedirect(From, To);
	};

	UGinkgoSaveGameSubsystem* Subsystem = SaveSubsystem.Get();

	// ===== 第一遍：恢复数据 =====
	for (const FGuid& NodeId : LoadOrder)
	{
		IGinkgoSaveNode* Node = Registry.Find(NodeId);
		check(Node);

		if (const FGinkgoSaveNodeIndexEntry* Entry = IndexMap.Find(NodeId))
		{
			Archive.Seek(Entry->Offset);

			UE_LOG(LogGinkgoSave, Log, TEXT("[Save] 读节点 %s：偏移 %llu，大小 %llu"), *Node->GetClassificationTag().ToString(), Entry->Offset, Entry->Size);

			const int32 SavedVersion = Archive.CustomVer(NodeId);
			const int32 CurrentVersion = Node->GetCurrentVersion();

			if (SavedVersion > CurrentVersion)
			{
				UE_LOG(LogGinkgoSave, Error, TEXT("[Save] 节点 %s（%s）存档的版本 %d 比代码的 %d 新，存档来自更新的版本"),
					*Node->GetClassificationTag().ToString(),
					*NodeId.ToString(EGuidFormats::DigitsWithHyphens),
					SavedVersion, CurrentVersion);
				Archive.SetError();
				return;
			}

			FStructuredArchive::FRecord NodeRecord = RootRecord.EnterRecord(*NodeId.ToString(EGuidFormats::DigitsWithHyphens));

			// 迁移链（GetMigrations）、节点改名（GetReplaces）还没有接：要做语义迁移时在这里按 SavedVersion 执行
			FGinkgoSaveNodeContext Ctx(NodeRecord, SavedVersion, /*bIsLoading*/ true, Subsystem, AddRedirectFn);
			Node->Serialize(Ctx);
			if (Archive.IsError())
			{
				return;
			}
		}
		else
		{
			FGinkgoSaveNodeContext Ctx(Subsystem);
			Node->OnMissingFromSave(Ctx);
		}
	}

	// ===== 第二遍：所有数据都就位了 =====
	{
		SCOPE_CYCLE_COUNTER(STAT_GinkgoSavePostLoad);

		for (const FGuid& NodeId : LoadOrder)
		{
			IGinkgoSaveNode* Node = Registry.Find(NodeId);
			check(Node);

			FGinkgoSaveNodeContext Ctx(Subsystem);
			Node->PostLoadGame(Ctx);
		}
	}
}

#if WITH_TEXT_ARCHIVE_SUPPORT
template class TGinkgoSaveGameSerializer<false, true>;
#endif

template class TGinkgoSaveGameSerializer<false, false>;
template class TGinkgoSaveGameSerializer<true, false>;

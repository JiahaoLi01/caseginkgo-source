#include "SaveGame/SaveNode/Core/GinkgoCoreActorsSaveNode.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "SaveGame/GinkgoSaveGameLibrary.h"
#include "SaveGame/GinkgoSaveGameObject.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "SaveGame/SaveNode/GinkgoSaveNodeContext.h"
#include "Serialization/Archive.h"
#include "UObject/DevObjectVersion.h"
#include "UObject/Package.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"
#include "Utilities/Enumeration/Tags/SaveGameTags.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"

DECLARE_CYCLE_STAT(TEXT("Save core actors: resolve"), STAT_GinkgoSaveCoreActorsResolve, STATGROUP_Ginkgo3C);
DECLARE_CYCLE_STAT(TEXT("Save core actors: actor frame"), STAT_GinkgoSaveCoreActorsFrame, STATGROUP_Ginkgo3C);
DECLARE_CYCLE_STAT(TEXT("Save core actors: data"), STAT_GinkgoSaveCoreActorsData, STATGROUP_Ginkgo3C);

const FGuid FGinkgoCoreActorsSaveNode::NodeGuid(0xEE554325, 0x296045A3, 0x8C542609, 0x07EDE5ED);

// 让 FArchive::UsingCustomVersion(NodeGuid) 查得到这个 GUID
static FDevVersionRegistration GRegisterGinkgoCoreActorsSaveNode(
	FGinkgoCoreActorsSaveNode::NodeGuid,
	FGinkgoCoreActorsSaveNode::LatestVersion,
	TEXT("GinkgoCoreActorsSaveNode"));

namespace
{
	/** 读档时能接收数据的 Actor：还活着，并且还实现了接口（类改过时可能没有了） */
	bool CanRestore(const AActor* Actor)
	{
		return IsValid(Actor) && Actor->Implements<UGinkgoSaveGameObject>();
	}

	/**
	 * 读档第一遍：找到或生成存档里的这个 Actor。
	 * @return 找不到、生成不了时返回空，这个 Actor 的数据跳过
	 */
	AActor* ResolveActor(UWorld& World, const FString& ActorName, const FSoftClassPath& Class, const FGuid& SpawnID, const TMap<FGuid, AActor*>& SpawnIDs)
	{
		ULevel* Level = World.GetCurrentLevel();

		if (Class.IsNull())
		{
			// 关卡里摆的：按名字找
			AActor* Placed = FindObjectFast<AActor>(Level, *ActorName);
			if (CanRestore(Placed))
			{
				return Placed;
			}
			UE_LOG(LogGinkgoSave, Warning, TEXT("[Save] 存档里的关卡 Actor %s 不在了，跳过它的数据"), *ActorName);
			return nullptr;
		}

		if (SpawnID.IsValid())
		{
			if (AActor* const* Found = SpawnIDs.Find(SpawnID))
			{
				return *Found;
			}
		}

		UClass* ActorClass = Class.TryLoadClass<AActor>();
		if (!ActorClass)
		{
			UE_LOG(LogGinkgoSave, Warning, TEXT("[Save] 存档里的 Actor %s 的类 %s 没有了，跳过它的数据"), *ActorName, *Class.ToString());
			return nullptr;
		}

		// 同一张地图上原地读档时，存档时生成的 Actor 多半还在：接着用它
		if (AActor* Existing = FindObjectFast<AActor>(Level, *ActorName); CanRestore(Existing) && Existing->GetClass() == ActorClass)
		{
			return Existing;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.OverrideLevel = Level;
		SpawnParameters.Name = *ActorName;
		// 这个名字可能还被占着（同名的刚销毁、还没回收）：要求原名的话引擎直接 Fatal。换一个名字，指向它的引用靠重定向接上
		SpawnParameters.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		SpawnParameters.bNoFail = true;

		AActor* Spawned = World.SpawnActor(ActorClass, nullptr, nullptr, SpawnParameters);
		if (!CanRestore(Spawned))
		{
			UE_LOG(LogGinkgoSave, Warning, TEXT("[Save] 存档里的 Actor %s 生成不出来（类 %s），跳过它的数据"), *ActorName, *Class.ToString());
			return nullptr;
		}

		if (SpawnID.IsValid() && Spawned->Implements<UGinkgoSaveGameSpawnActor>())
		{
			IGinkgoSaveGameSpawnActor::Execute_SetSpawnID(Spawned, SpawnID);
		}
		return Spawned;
	}
}

FSoftObjectPath FGinkgoCoreActorsSaveNode::MakeLevelActorPath(const ULevel& Level, const FString& ActorName)
{
	return FSoftObjectPath(FTopLevelAssetPath(Level.GetPackage()->GetFName(), Level.GetOuter()->GetFName()), FString(LevelSubPathPrefix) + ActorName);
}

FGameplayTag FGinkgoCoreActorsSaveNode::GetClassificationTag() const
{
	return FSaveNodeTags::Core_Actors;
}

void FGinkgoCoreActorsSaveNode::Serialize(FGinkgoSaveNodeContext& Ctx)
{
	UGinkgoSaveGameSubsystem* SaveSubsystem = Ctx.GetSubsystem();
	if (!ensure(SaveSubsystem))
	{
		return;
	}

	UWorld* World = Ctx.GetWorld();
	if (!ensure(World))
	{
		return;
	}

	FStructuredArchive::FRecord& Record = Ctx.GetRecord();
	FArchive& Archive = Ctx.GetUnderlyingArchive();
	const bool bIsLoading = Ctx.IsLoading();
	const bool bIsTextFormat = Archive.IsTextFormat();

	int32 ActorsNum = 0;
	TArray<AActor*> Actors;

	const uint64 ActorsPosition = Archive.Tell();
	static const FArchiveFieldName ActorFieldName{TEXT("Actors")};

	if (bIsLoading)
	{
		// 第一遍：找到或生成 Actor，登记路径重定向
		SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveCoreActorsResolve);

		TMap<FGuid, AActor*> SpawnIDs;
		for (const TWeakObjectPtr<AActor>& ActorPtr : SaveSubsystem->GetSaveGameActors())
		{
			if (AActor* Actor = ActorPtr.Get(); IsValid(Actor) && Actor->Implements<UGinkgoSaveGameSpawnActor>())
			{
				const FGuid SpawnID = IGinkgoSaveGameSpawnActor::Execute_GetSpawnID(Actor);
				if (SpawnID.IsValid())
				{
					SpawnIDs.Add(SpawnID, Actor);
				}
			}
		}

		FStructuredArchive::FMap ActorMap = Record.EnterMap(ActorFieldName, ActorsNum);
		if (Archive.IsError() || ActorsNum < 0)
		{
			Archive.SetError();
			return;
		}
		Actors.SetNumZeroed(ActorsNum);

		for (int32 ActorIndex = 0; ActorIndex < ActorsNum && !Archive.IsError(); ++ActorIndex)
		{
			AActor*& Actor = Actors[ActorIndex];

			SerializeActorFrame(Ctx, ActorMap, Actor,
				[&](const FString& ActorName, const FSoftClassPath& Class, const FGuid& SpawnID, FStructuredArchive::FSlot& /*Slot*/)
				{
					Actor = ResolveActor(*World, ActorName, Class, SpawnID, SpawnIDs);

					if (Actor && !Class.IsNull())
					{
						// 新的 Actor 路径可能和存档时不同（名字变了）：把后面读到的旧路径改成新的
						Ctx.AddSoftPathRedirect(MakeLevelActorPath(*World->GetCurrentLevel(), ActorName), FSoftObjectPath(Actor));
					}
				});
		}
	}
	else
	{
		using namespace UE::Query;
		// 流送关卡卸载时不走 Destroy，弱指针可能已经失效：不写，别在第二遍崩
		Actors = SaveSubsystem->GetSaveGameActors()
			| Select([](const TWeakObjectPtr<AActor>& ActorPtr) { return ActorPtr.Get(); })
			| Where([](const AActor* Actor) { return IsValid(Actor); })
			| ToArray();
		ActorsNum = Actors.Num();
	}

	// 第二遍：读写 Actor 的数据
	{
		SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveCoreActorsData);

		// 二进制读档：回到第一遍的起点，再读一遍
		if (bIsLoading && !bIsTextFormat)
		{
			Archive.Seek(ActorsPosition);
		}

		FStructuredArchive::FMap ActorMap = Record.EnterMap(ActorFieldName, ActorsNum);

		for (int32 ActorIndex = 0; ActorIndex < ActorsNum && !Archive.IsError(); ++ActorIndex)
		{
			AActor* Actor = Actors[ActorIndex];

			SerializeActorFrame(Ctx, ActorMap, Actor,
				[&](const FString& /*ActorName*/, const FSoftClassPath& /*Class*/, const FGuid& /*SpawnID*/, FStructuredArchive::FSlot& Slot)
				{
					// 第一遍没找到的：外框会按大小跳过它的数据
					if (!Actor)
					{
						return;
					}

					Actor->SerializeScriptProperties(Slot.EnterAttribute(TEXT("Properties")));

					FStructuredArchive::FSlot CustomDataSlot = Slot.EnterAttribute(TEXT("Data"));
					FStructuredArchive::FRecord CustomDataRecord = CustomDataSlot.EnterRecord();

					FGinkgoSaveGameArchive SaveGameArchive(CustomDataRecord, Actor);
					IGinkgoSaveGameObject::Execute_OnSerialize(Actor, SaveGameArchive, bIsLoading);
				});
		}
	}
}

void FGinkgoCoreActorsSaveNode::SerializeActorFrame(
	FGinkgoSaveNodeContext& Ctx,
	FStructuredArchive::FMap& ActorMap,
	AActor*& Actor,
	TFunctionRef<void(const FString&, const FSoftClassPath&, const FGuid&, FStructuredArchive::FSlot&)> BodyFunction)
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoSaveCoreActorsFrame);

	FArchive& Archive = Ctx.GetUnderlyingArchive();
	const bool bIsLoading = Ctx.IsLoading();
	const bool bIsTextFormat = Archive.IsTextFormat();

	FString ActorName;
	FSoftClassPath Class;
	FGuid SpawnID;

	if (!bIsLoading)
	{
		ActorName = Actor->GetName();

		if (!UGinkgoSaveGameLibrary::WasObjectLoaded(Actor))
		{
			Class = Actor->GetClass();
		}

		if (Actor->Implements<UGinkgoSaveGameSpawnActor>())
		{
			SpawnID = IGinkgoSaveGameSpawnActor::Execute_GetSpawnID(Actor);
		}
	}

	FStructuredArchive::FSlot ActorSlot = ActorMap.EnterElement(ActorName);

	if (TOptional<FStructuredArchive::FSlot> ClassSlot = ActorSlot.TryEnterAttribute(TEXT("Class"), !Class.IsNull()))
	{
		ClassSlot.GetValue() << Class;
	}

	if (TOptional<FStructuredArchive::FSlot> GuidSlot = ActorSlot.TryEnterAttribute(TEXT("GUID"), SpawnID.IsValid()))
	{
		GuidSlot.GetValue() << SpawnID;
	}

	uint64 DataSize = 0;
	if (!bIsTextFormat)
	{
		Archive << DataSize;
	}

	const uint64 BeginDataPosition = Archive.Tell();

	BodyFunction(ActorName, Class, SpawnID, ActorSlot);

	if (!bIsTextFormat)
	{
		if (bIsLoading)
		{
			// 跳到记录的末尾：内容读多了、读少了、没读，都不影响后面的 Actor
			Archive.Seek(BeginDataPosition + DataSize);
		}
		else
		{
			const uint64 EndDataPosition = Archive.Tell();
			DataSize = EndDataPosition - BeginDataPosition;

			Archive.Seek(BeginDataPosition - sizeof(DataSize));
			Archive << DataSize;
			Archive.Seek(EndDataPosition);
		}
	}
}

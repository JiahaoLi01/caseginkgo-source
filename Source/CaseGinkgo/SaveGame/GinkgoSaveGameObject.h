#pragma once

#include "CoreMinimal.h"
#include "Serialization/StructuredArchive.h"    // FStructuredArchive::FRecord
#include "UObject/Interface.h"
#include "GinkgoSaveGameObject.generated.h"

/**
 * @struct FGinkgoSaveGameArchive : 按名字读写字段的一段存档
 *
 * 为什么需要：二进制的结构化存档不写字段名，加字段、删字段、调换顺序之后按位置读就错位了。
 * 这里记一张“字段名 → 偏移”的表放在这一段的末尾，读的时候按名字跳过去；存档里没有的字段返回 false，调用方保留默认值。
 * 文本格式（开发期的 JSON）本来就带字段名，不记这张表。
 *
 * 实现注意：析构时才把表写下去、把位置移到这一段的末尾，所以要等它析构以后才能接着往下写别的东西。
 */
USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct CASEGINKGO_API FGinkgoSaveGameArchive
{
	GENERATED_BODY()

public:
	FGinkgoSaveGameArchive() = default;
	FGinkgoSaveGameArchive(FStructuredArchive::FRecord& InRecord, UObject* InObject);
	~FGinkgoSaveGameArchive();

	bool IsValid() const
	{
		return Record != nullptr;
	}

	FStructuredArchive::FRecord& GetRecord() const
	{
		return *Record;
	}

	/**
	 * @brief 读写一个字段
	 * @param FieldName - 字段名
	 * @param SerializeFunction - 真正读写的函数，参数是这个字段的 FStructuredArchive::FSlot
	 * @return 这个字段确实读写了：读档时存档里没有、存档时同名的已经写过，都返回 false
	 */
	template<typename FSerializeFunc>
	bool SerializeField(FName FieldName, FSerializeFunc SerializeFunction)
	{
		if (!IsValid())
		{
			return false;
		}

		FArchive& Archive = Record->GetUnderlyingArchive();

		if (Archive.IsSaving() && Fields.Contains(FieldName))
		{
			return false;
		}

		// 文本格式不能乱跳，也不需要
		if (!Archive.IsTextFormat())
		{
			if (Archive.IsLoading())
			{
				if (!Fields.Contains(FieldName))
				{
					return false;
				}

				Archive.Seek(StartPosition + Fields[FieldName]);
			}
			else
			{
				Fields.Add(FieldName, Archive.Tell() - StartPosition);
			}
		}

		SerializeFunction(Record->EnterField(*FieldName.ToString()));

		return true;
	}

private:
	FGinkgoSaveGameArchive(FGinkgoSaveGameArchive&) = delete;

	FStructuredArchive::FRecord* Record = nullptr;
	TWeakObjectPtr<> Object;
	uint64 StartPosition = 0;
	uint64 EndPosition = 0;

	/** 字段名 → 相对这一段开头的偏移 */
	TMap<FName, uint64> Fields;
};

// 不能复制：析构时要写东西
template<>
struct TStructOpsTypeTraits<FGinkgoSaveGameArchive> : public TStructOpsTypeTraitsBase2<FGinkgoSaveGameArchive>
{
	enum
	{
		WithCopy = false
	};
};

UINTERFACE(MinimalAPI)
class UGinkgoSaveGameObject : public UInterface
{
	GENERATED_BODY()
};

/**
 * @interface IGinkgoSaveGameObject : 要进存档的对象
 * 实现了它的 Actor 由存档系统自动追踪（UGinkgoSaveGameSubsystem）。存的是标了 SaveGame 的属性，再加 OnSerialize 里自己读写的东西。
 */
class CASEGINKGO_API IGinkgoSaveGameObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 在对象标了 SaveGame 的属性读写完之后调用，读写那些不能标 SaveGame 的东西
	 * @param Archive - 用来读写字段
	 * @param bIsLoading - true 读档；false 存档
	 * @return 成功；用不用随调用方（返回值也让它不至于变成不能返回的事件）
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SaveGame")
	bool OnSerialize(UPARAM(ref) FGinkgoSaveGameArchive& Archive, bool bIsLoading);
};

UINTERFACE(MinimalAPI)
class UGinkgoSaveGameSpawnActor : public UInterface
{
	GENERATED_BODY()
};

/**
 * @interface IGinkgoSaveGameSpawnActor : 给 Actor 一个固定的生成 ID
 * 有的 Actor 不是（也不能）由存档系统生成，比如玩家角色：游戏模式在读档之前就生成好了。
 * 读档时按这个 ID 找到已经生成的那个，把数据读到它身上，不再生成一个新的。
 */
class CASEGINKGO_API IGinkgoSaveGameSpawnActor
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveGame|Spawn")
	const FGuid GetSpawnID() const;

	UFUNCTION(BlueprintNativeEvent, Category = "SaveGame|Spawn")
	bool SetSpawnID(const FGuid& NewID);
};

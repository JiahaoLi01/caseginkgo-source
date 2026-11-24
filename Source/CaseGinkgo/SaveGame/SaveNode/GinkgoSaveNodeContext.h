#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "SaveGame/GinkgoSaveGameObject.h"      // FGinkgoSaveGameArchive：字段按名字读写
#include "Serialization/StructuredArchive.h"
#include "Templates/UniquePtr.h"
#include "UObject/SoftObjectPath.h"

class FArchive;
class UGinkgoSaveGameSubsystem;
class UWorld;

/**
 * @class FGinkgoSaveNodeContext : 节点读写时的上下文
 *
 * 由序列化器在调用节点前构造。两种：带 Record（Serialize）；不带（OnMissingFromSave、PostLoadGame，调 Field 会 checkf）。
 *
 * 三个字段函数：
 *  - Field(Name, Value)                         必填，读档时没有视为存档损坏，中止读档
 *  - OptionalField(Name, Value, Default)        可选，读档时没有就给默认值
 *  - FieldSince(Name, Value, SinceVer, Default) 显式的版本门
 *
 * 实现注意：字段走 FGinkgoSaveGameArchive，按名字找偏移，所以加字段、删字段、调换顺序都不会错位。
 * 二进制的 TryEnterField 只是按位置读一个布尔，新加的字段在旧存档里读到的是别的数据，不能用来做“可选”。
 * 同一个节点不要混用字段函数和直接读写 GetRecord()：字段按偏移跳，混着读的位置对不上。
 */
class CASEGINKGO_API FGinkgoSaveNodeContext
{
public:
	using FAddSoftPathRedirectFn = TFunction<void(const FSoftObjectPath& /*From*/, const FSoftObjectPath& /*To*/)>;

	/** Serialize 用：带着可以读写的 Record */
	FGinkgoSaveNodeContext(
		FStructuredArchive::FRecord InRecord,
		int32 InSavedVersion,
		bool bInIsLoading,
		UGinkgoSaveGameSubsystem* InSubsystem,
		FAddSoftPathRedirectFn InAddRedirect = nullptr);

	/** OnMissingFromSave、PostLoadGame 用：没有 Record，节点只能查内存状态 */
	explicit FGinkgoSaveNodeContext(UGinkgoSaveGameSubsystem* InSubsystem);

	/** 是否在读档（OnMissingFromSave 也算读档） */
	bool IsLoading() const { return bIsLoading; }

	/** 是否带着存档 */
	bool HasArchive() const { return Record.IsSet(); }

	/** 存档里这个节点的版本；存档时等于当前版本；OnMissingFromSave 时 INDEX_NONE */
	int32 GetSavedVersion() const { return SavedVersion; }

	UGinkgoSaveGameSubsystem* GetSubsystem() const { return Subsystem; }

	UWorld* GetWorld() const;

	/** 直接访问 Record，字段函数表达不了的结构用（比如 Actor 节点带大小前缀的布局）。不要和字段函数混用 */
	FStructuredArchive::FRecord& GetRecord();

	/** 直接访问底层的 FArchive，要 Seek、Tell 时用 */
	FArchive& GetUnderlyingArchive();

	/** 加一条软路径重定向：之后读到的指向 From 的引用改成 To。生成的 Actor 路径变了的时候用 */
	void AddSoftPathRedirect(const FSoftObjectPath& From, const FSoftObjectPath& To);

	// ===== 字段 =====

	/** 必填字段：读档时没有视为存档损坏 */
	template <typename T>
	void Field(const TCHAR* Name, T& Value)
	{
		if (!SerializeNamedField(Name, Value) && bIsLoading)
		{
			MarkFieldMissing(Name);
		}
	}

	/** 可选字段：存档时总是写；读档时没有就给 Default。加字段、删字段都靠它吸收，不用写版本判断 */
	template <typename T>
	void OptionalField(const TCHAR* Name, T& Value, T Default = T{})
	{
		if (!SerializeNamedField(Name, Value) && bIsLoading)
		{
			Value = MoveTemp(Default);
		}
	}

	/** 显式的版本门：存档的版本比 SinceVersion 旧时不读，给 Default */
	template <typename T>
	void FieldSince(const TCHAR* Name, T& Value, int32 SinceVersion, T Default = T{})
	{
		if (bIsLoading && SavedVersion < SinceVersion)
		{
			Value = MoveTemp(Default);
			return;
		}
		Field(Name, Value);
	}

private:
	template <typename T>
	bool SerializeNamedField(const TCHAR* Name, T& Value)
	{
		checkf(HasArchive(), TEXT("FGinkgoSaveNodeContext 的字段函数只能在 Serialize 里用（OnMissingFromSave、PostLoadGame 没有存档）"));
		return GetFields().SerializeField(FName(Name), [&Value](FStructuredArchive::FSlot Slot) { Slot << Value; });
	}

	FGinkgoSaveGameArchive& GetFields();
	void MarkFieldMissing(const TCHAR* Name);

	TOptional<FStructuredArchive::FRecord> Record;
	/** 第一次用字段函数时才建：只用 GetRecord() 的节点不多写字段表。要在 Record 之后析构之前写完字段表，所以声明在 Record 后面 */
	TUniquePtr<FGinkgoSaveGameArchive> Fields;
	int32 SavedVersion = INDEX_NONE;
	bool bIsLoading = true;
	UGinkgoSaveGameSubsystem* Subsystem = nullptr;
	FAddSoftPathRedirectFn AddRedirectFn;
};

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SaveGame/GinkgoSaveGameObject.h"      // FGinkgoSaveGameArchive
#include "GinkgoSaveGameLibrary.generated.h"

template<typename SerializeObject>
concept GinkgoSerializableUObject = requires(SerializeObject Object, FStructuredArchive::FRecord& Record)
{
	{Object->SerializeStructuredArchive(Record, true)};
	{Object->SerializeStructuredArchive(Record, false)};
	requires std::convertible_to<SerializeObject, UObject*> ||
			 std::convertible_to<SerializeObject, TObjectPtr<UObject>> ||
			 requires { typename SerializeObject::ElementType; };
};

/**
 * @class UGinkgoSaveGameLibrary : 存档读写的工具函数
 */
UCLASS()
class CASEGINKGO_API UGinkgoSaveGameLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * @brief 这个对象是不是从资产里加载出来的（StaticMesh、关卡里摆的 Actor 之类）
	 * @return 是从资产加载的
	 */
	UFUNCTION(BlueprintCallable, Category = "SaveGame|Utilities")
	static bool WasObjectLoaded(const UObject* Object);

	/**
	 * @brief 这个 Archive 是在读档还是在存档
	 * @return true 读档；false 存档
	 */
	UFUNCTION(BlueprintCallable, Category = "SaveGame|Utilities")
	static bool IsLoading(const FGinkgoSaveGameArchive& Archive);

	/**
	 * @brief 读写 Actor 的位置，会动的 Actor 用
	 * @note 读档时会直接把 Actor 放过去
	 * @return 确实读写了
	 */
	UFUNCTION(BlueprintCallable, Category = "SaveGame|Utilities", meta = (DefaultToSelf = "Actor"))
	static bool SerializeActorTransform(UPARAM(ref) FGinkgoSaveGameArchive& Archive, AActor* Actor);

	/**
	 * @brief 读写一个属性（蓝图用）
	 * @param Value - 要读写的属性，必须是变量
	 * @param bSave - true 时存档也写；读档总是读
	 * @return 确实读写了
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "SaveGame|Utilities", meta = (CustomStructureParam = "Value", AdvancedDisplay = "bSave"))
	static bool SerializeItem(UPARAM(ref) FGinkgoSaveGameArchive& Archive, UPARAM(ref) int32& Value, bool bSave = true);
	DECLARE_FUNCTION(execSerializeItem);

	/**
	 * @brief 把蓝图枚举的版本记进存档
	 * @param VersionEnum - 要在 UGinkgoSaveGameSettings 里配过
	 * @return 版本号：存档时是枚举的最新值，读档时是存档里记的；没有时 -1
	 */
	UFUNCTION(BlueprintCallable, Category = "SaveGame|Utilities")
	static int32 UseCustomVersion(UPARAM(ref) FGinkgoSaveGameArchive& Archive, const UEnum* VersionEnum);

	/**
	 * @brief 读写一个 UObject：存它的类和名字，读档时新建
	 * @param SerializeName - 字段名
	 * @param InObject - 要读写的对象，读档时可以是空的
	 * @param Outer - 读档时新建的对象的 Outer
	 */
	template<typename SerializedObject> requires GinkgoSerializableUObject<SerializedObject>
	static void SerializeUObject(const FName& SerializeName, FGinkgoSaveGameArchive& Archive, SerializedObject& InObject, UObject* Outer, const bool bIsLoading);

	/** 在结构化存档的一个 Slot 里读写标签容器（按字符串存，标签改名后读不回来的直接丢掉） */
	static void SerializeGameplayTagContainer(FStructuredArchive::FSlot Slot, FGameplayTagContainer& TagContainer, bool bIsLoading);
};

template <typename SerializedObject> requires GinkgoSerializableUObject<SerializedObject>
void UGinkgoSaveGameLibrary::SerializeUObject(
	const FName& SerializeName,
	FGinkgoSaveGameArchive& Archive,
	SerializedObject& InObject,
	UObject* Outer,
	const bool bIsLoading)
{
	static const FArchiveFieldName ObjectDataName{TEXT("Data")};

	FString ObjectName;
	FSoftClassPath Class;

	if (!IsLoading(Archive))
	{
		check(InObject != nullptr);
		ObjectName = InObject->GetName();
		Class = InObject->GetClass();
	}

	Archive.SerializeField(SerializeName, [Class, ObjectName, &Archive, &InObject, Outer, bIsLoading](FStructuredArchiveSlot Slot) mutable
	{
		FStructuredArchive::FRecord Record = Slot.EnterRecord();
		Record.EnterField(TEXT("Class")) << Class;
		Record.EnterField(TEXT("Name")) << ObjectName;

		if (IsLoading(Archive))
		{
			if (const UClass* LoadedClass = Class.TryLoadClass<UObject>())
			{
				if constexpr (std::is_pointer_v<SerializedObject>)
				{
					InObject = NewObject<std::remove_pointer_t<SerializedObject>>(Outer, LoadedClass, FName{ObjectName});
				}
				else if constexpr (requires { typename SerializedObject::ElementType; })
				{
					using ElementType = SerializedObject::ElementType;
					InObject = NewObject<ElementType>(Outer, LoadedClass, FName{ObjectName});
				}
			}
		}

		if (InObject)
		{
			FStructuredArchive::FRecord Data = Record.EnterRecord(ObjectDataName);
			InObject->SerializeStructuredArchive(Data, bIsLoading);
		}
	});
}

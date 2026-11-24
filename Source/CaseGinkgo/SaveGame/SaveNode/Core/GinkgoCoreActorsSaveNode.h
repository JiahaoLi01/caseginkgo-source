#pragma once

#include "CoreMinimal.h"
#include "SaveGame/SaveNode/GinkgoSaveNode.h"
#include "Serialization/StructuredArchive.h"
#include "Templates/Function.h"

class AActor;
class ULevel;

/**
 * @class FGinkgoCoreActorsSaveNode : 内置节点，存所有实现了 IGinkgoSaveGameObject 的 Actor
 *
 * 存档：写每个 Actor 的类（生成的才有）、生成 ID（有的才有）、标了 SaveGame 的属性、OnSerialize 自己写的数据。
 * 读档分两遍，都在这个节点的 Serialize 里：第一遍找到或生成 Actor，登记路径重定向；第二遍读数据，这样 Actor 之间的引用都接得上。
 *
 * 读档时找不到的 Actor（关卡改过、类删了、世界分区的格子没载入）跳过并警告，不崩，靠每个 Actor 前面的大小跳过它的数据。
 *
 * 如果以后有 Actor 的 OnSerialize 要查别的节点恢复的数据，把那个节点加进 GetDependencies。
 */
class CASEGINKGO_API FGinkgoCoreActorsSaveNode : public IGinkgoSaveNode
{
public:
	static const FGuid NodeGuid;

	enum EVersion
	{
		Initial,

		// -----<new versions can be added above this line>-------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	/** 关卡里 Actor 软路径的子路径前缀 */
	static constexpr const TCHAR* LevelSubPathPrefix = TEXT("PersistentLevel.");

	/** 这个关卡里叫这个名字的 Actor 的软路径 */
	static FSoftObjectPath MakeLevelActorPath(const ULevel& Level, const FString& ActorName);

	virtual FGuid GetNodeId() const override { return NodeGuid; }
	virtual FGameplayTag GetClassificationTag() const override;
	virtual int32 GetCurrentVersion() const override { return LatestVersion; }

	virtual void Serialize(FGinkgoSaveNodeContext& Ctx) override;

private:
	/**
	 * 读写一个 Actor 的外框：{类（生成的才有）}、{生成 ID（有的才有）}、{数据大小（只有二进制有，读档时靠它跳过这个 Actor）}、{内容，由 BodyFunction 读写}
	 */
	void SerializeActorFrame(
		FGinkgoSaveNodeContext& Ctx,
		FStructuredArchive::FMap& ActorMap,
		AActor*& Actor,
		TFunctionRef<void(
			const FString& ActorName,
			const FSoftClassPath& Class,
			const FGuid& SpawnID,
			FStructuredArchive::FSlot& BodySlot)> BodyFunction);
};

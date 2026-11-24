#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SaveGame/GinkgoSaveGameObject.h"
#include "SaveGameTestTypes.generated.h"

/** @class AGinkgoSaveTestActor : 测试用的、要进存档的 Actor */
UCLASS(HideDropdown, NotBlueprintable, NotPlaceable)
class AGinkgoSaveTestActor : public AActor, public IGinkgoSaveGameObject
{
	GENERATED_BODY()

public:
	AGinkgoSaveTestActor();

	UPROPERTY(SaveGame)
	int32 Count = 0;

	/** 默认值不是 0：等于默认值时也要进存档，否则原地读档回不到默认值 */
	UPROPERTY(SaveGame)
	int32 WithDefault = 5;

	UPROPERTY(SaveGame)
	FString Label;

	UPROPERTY(SaveGame)
	TArray<int32> Numbers;

	UPROPERTY(SaveGame)
	TObjectPtr<AActor> Friend = nullptr;

	UPROPERTY(SaveGame)
	TSoftObjectPtr<AActor> SoftFriend;

	/** 没标 SaveGame 的不进存档 */
	UPROPERTY()
	int32 NotSaved = 0;

	/** OnSerialize 里按名字读写的 */
	int32 Custom = 0;

	virtual bool OnSerialize_Implementation(FGinkgoSaveGameArchive& Archive, bool bIsLoading) override;
};

/** @class AGinkgoSaveTestSpawnActor : 带生成 ID 的，像玩家角色：读档之前已经由别人生成好了 */
UCLASS(HideDropdown, NotBlueprintable, NotPlaceable)
class AGinkgoSaveTestSpawnActor : public AGinkgoSaveTestActor, public IGinkgoSaveGameSpawnActor
{
	GENERATED_BODY()

public:
	FGuid SpawnId;

	virtual const FGuid GetSpawnID_Implementation() const override { return SpawnId; }
	virtual bool SetSpawnID_Implementation(const FGuid& NewID) override { SpawnId = NewID; return true; }
};

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"                                   // UWorld::InitializationValues
#include "SaveGame/SaveNode/GinkgoSaveNodeRegistry.h"       // 节点注册表是成员
#include "Subsystems/GameInstanceSubsystem.h"
#include "GinkgoSaveGameSubsystem.generated.h"

class FGinkgoSaveGameSerializer;

DECLARE_MULTICAST_DELEGATE(FOnGinkgoSaveGameEvent);

/**
 * @class UGinkgoSaveGameSubsystem : 存档系统
 * 管存档、读档的生命周期，追踪当前世界里要进存档的 Actor。要存什么由节点决定（IGinkgoSaveNode），各系统把节点注册到 GetRegistry()。
 * 设计见 Docs/Design/SaveGame.md。
 */
UCLASS()
class CASEGINKGO_API UGinkgoSaveGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 全部节点的 PostLoadGame 跑完后广播，给不属于任何节点的订阅者。节点自己的后处理用 IGinkgoSaveNode::PostLoadGame */
	FOnGinkgoSaveGameEvent OnSaveGameLoaded;

	/** 存档之前广播，订阅者把要存的状态整理好 */
	FOnGinkgoSaveGameEvent OnBeforeSaveGame;

	/** 每个 GameInstance 有自己的节点：多开 PIE 时互不影响 */
	FGinkgoSaveNodeRegistry& GetRegistry() { return NodeRegistry; }

	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	bool Save(const FString& SlotName = TEXT("DefaultSaveGame"));

	/** 读档成功或已开始；bReloadMap 为 true 时重建地图，供检查点复活撤销存档后的世界变化。完成广播 OnSaveGameLoaded。 */
	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	bool Load(const FString& SlotName = TEXT("DefaultSaveGame"), bool bReloadMap = false);

	UFUNCTION(BlueprintCallable, Category = "SaveGame")
	bool IsLoadingSaveGame() const;

	UFUNCTION(BlueprintPure, Category = "SaveGame")
	FString GetLastError() const { return LastError; }

	/** 当前世界里实现了 IGinkgoSaveGameObject 的 Actor，自动追踪 */
	TSet<TWeakObjectPtr<AActor>>& GetSaveGameActors() { return SaveGameActors; }
	const TSet<TWeakObjectPtr<AActor>>& GetSaveGameActors() const { return SaveGameActors; }

	/** 运行时被销毁的关卡 Actor，读档后要重新销毁 */
	TSet<FSoftObjectPath>& GetDestroyedLevelActors() { return DestroyedLevelActors; }
	const TSet<FSoftObjectPath>& GetDestroyedLevelActors() const { return DestroyedLevelActors; }

	UFUNCTION(BlueprintCallable, Category = "SaveGame", meta = (WorldContext = "WorldContextObject"))
	static UGinkgoSaveGameSubsystem* Get(UObject* WorldContextObject);

protected:
	void OnWorldInitialized(UWorld* InWorld, const UWorld::InitializationValues);
	void OnActorsInitialized(const FActorsInitializedParams& Params);
	void OnWorldCleanup(UWorld* InWorld, bool, bool);

	void OnActorPreSpawn(AActor* InActor);
	void OnActorDestroyed(AActor* InActor);

	void OnLoadComplete();

private:
	template<bool, bool>
	friend class TGinkgoSaveGameSerializer;

	TSharedPtr<FGinkgoSaveGameSerializer, ESPMode::ThreadSafe> CurrentSerializer;
	FString LastError;
	FGinkgoSaveNodeRegistry NodeRegistry;

	/** 已经挂上生成、销毁回调的世界。子系统初始化和世界初始化的先后在 PIE 和单机不一样，同一个世界只挂一次 */
	TWeakObjectPtr<UWorld> HookedWorld;

	TSet<TWeakObjectPtr<AActor>> SaveGameActors;
	TSet<FSoftObjectPath> DestroyedLevelActors;
};

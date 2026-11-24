#pragma once

#include "CoreMinimal.h"
#include "Checkpoint/GinkgoCheckpointState.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GinkgoCheckpointSubsystem.generated.h"

class AGinkgoCharacter;
class AGinkgoCheckpoint;
class AGinkgoCheckpointEnemySpawn;
class AGinkgoAICharacter;
class AController;
class ULevel;
class FGinkgoCheckpointSaveNode;
struct FStreamableHandle;
struct FStreamableManager;

/** 检查点进度拥有者：立即写档、追踪敌人死亡、恢复复活点及玩家快照，并负责检查点敌人的异步预载与分帧生成调度。 */
UCLASS()
class CASEGINKGO_API UGinkgoCheckpointSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 只由触发器提交；保存失败恢复上一个检查点状态，允许玩家离开后重试。 */
	bool Activate(AGinkgoCheckpoint& Checkpoint, AGinkgoCharacter* Player);

	/** 读取检查点并重载其地图，恢复存档后的敌人击杀与掉落物变化；失败时保留当前世界。 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool RestartFromCheckpoint();

	/** 是否存在可立即读取的检查点存档；客户端或正在读档期间返回 false。 */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	bool HasAvailableCheckpoint() const;

	/** 清空内存检查点进度并删除本地存档槽，供主菜单点击「新游戏」从头开始时调用。 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void ClearCheckpointForNewGame();

	/** 最近一次成功抵达的检查点地点名；尚未抵达或旧档未记录时为空文本。 */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	FText GetCurrentLocationName() const { return State.LocationName; }

	/** 当前世界可用的复活点；未抵达或地图不同返回 false。 */
	bool GetRespawnTransform(FTransform& OutTransform) const;

	/** 游戏模式生成/接管角色后调用；读档完成早于玩家生成时延后到这里恢复。 */
	void RestorePendingPlayer(AController& Controller);

	/** 权威敌人首次死亡时记录；重放死亡、重复通知不会产生重复条目。 */
	void RecordEnemyDeath(const AGinkgoAICharacter& Enemy);

	/** 敌人 BeginPlay 与读档后处理共用；流送进来的已击杀敌人也不能再次生成。 */
	bool IsEnemyDefeated(const AGinkgoAICharacter& Enemy) const;

	/** 只移除已击杀敌人，不触发其死亡掉枪逻辑；进度由本系统持有，不混入 Core.DestroyedActors。 */
	void RemoveDefeatedEnemy(AGinkgoAICharacter& Enemy);

	/** 将单个检查点敌人出生位加入异步预载与分帧生成队列；读档时先立即回收该出生位的旧敌人实例。客户端忽略。 */
	void EnqueueEnemySpawn(AGinkgoCheckpointEnemySpawn& Marker, bool bReload = false);

	/** 取消指定出生位尚未落地的异步预载与分帧生成请求，并立即释放其流式加载句柄；在出生位 EndPlay 时调用。 */
	void CancelPendingEnemySpawn(const AGinkgoCheckpointEnemySpawn& Marker);

	/** 取消并清空所有待生成的检查点敌人请求与流式句柄；在重开新游戏、读档切换与世界销毁时调用。 */
	void CancelAllPendingEnemySpawns();

	/** 当前仍在后台预载或等待分帧落地的检查点敌人数量；为 0 表示队列已清空。 */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	int32 GetPendingEnemySpawnCount() const { return PendingEnemySpawns.Num(); }

	/** 是否仍有检查点敌人处于异步预载或待分帧生成状态。 */
	UFUNCTION(BlueprintPure, Category = "Checkpoint")
	bool HasPendingEnemySpawns() const { return !PendingEnemySpawns.IsEmpty(); }

	/** 推进一次分帧生成周期：更新后台预载阶段并按单帧预算落地就绪敌人；供引擎帧回调与无 Tick 循环的单元测试共用。 */
	void ProcessPendingEnemySpawns();

	/** 调试和回归检查只读快照；运行时由激活、死亡和存档节点更新。 */
	const FGinkgoCheckpointState& GetState() const { return State; }

	/** 检查点的逻辑槽名；测试使用独立槽位，正式游戏默认 Checkpoint。客户端不写档。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint")
	FString SaveSlotName = TEXT("Checkpoint");

	/** 每帧最多落地的检查点敌人数量（个），默认 1 以消除集中生成导致的帧率尖峰；取值下限为 1。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint", meta = (ClampMin = "1"))
	int32 MaxEnemySpawnsPerFrame = 1;

	/** 队列头部同时执行异步资源预加载的出生位上限（个），避免大量出生位同时触发磁盘 I/O 峰值；取值下限为 1。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint", meta = (ClampMin = "1"))
	int32 MaxConcurrentEnemyPreloads = 4;

	/** 当 MaxEnemySpawnsPerFrame 大于 1 时的单帧生成软时间预算（毫秒）；至少保证每帧落地 1 名已就绪敌人。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Checkpoint", meta = (ClampMin = "0.5"))
	float MaxEnemySpawnFrameBudgetMs = 4.f;

	/** 显式 ID 按持久地图限定作用域；未提供时使用含子关卡名的 Actor 路径，统一去除 PIE 前缀。 */
	static FString MakeStableId(const AActor& Actor, FName ExplicitId = NAME_None);

	static UGinkgoCheckpointSubsystem* Get(const UObject* Context);

private:
	friend class FGinkgoCheckpointSaveNode;

	/** 单个待分帧异步生成的检查点敌人请求；由子系统持有至敌人完成落地、被取消或所属世界销毁。 */
	struct FPendingEnemySpawn
	{
		/** 队列条目所处的异步预载与落地阶段。 */
		enum class EPhase : uint8
		{
			/** 尚未启动资源预载，等待并发预载槽位。 */
			Queued,

			/** 正在通过 FStreamableManager 在后台线程异步加载依赖资产。 */
			Preloading,

			/** 所需资产已驻留内存，等待下一帧预算槽位执行 SpawnActor。 */
			ReadyToSpawn
		};

		/** 目标出生位弱引用；子关卡卸载或 Actor 销毁时失效，下次推进自动丢弃并释放句柄。 */
		TWeakObjectPtr<AGinkgoCheckpointEnemySpawn> Marker;

		/** 当前异步状态，由 AdvanceEnemySpawnPreload 单向推进至 ReadyToSpawn。 */
		EPhase Phase = EPhase::Queued;

		/** 已发起过异步加载请求的软资产路径集合，避免缺失或无效引用导致死循环重试。 */
		TSet<FSoftObjectPath> AttemptedPaths;

		/** 已执行的级联预载跳数（上限 3 跳：主配置 → 外观与默认武器 → 武器网格/特效/音频/弹种）。 */
		int32 PreloadHop = 0;

		/** 已启动的全部流式句柄集合，保持强引用直至敌人落地完成，防止预载资产提前被 GC 回收。 */
		TArray<TSharedPtr<FStreamableHandle>> StreamableHandles;

		/** 当前正在等待完成的一跳流式句柄；完成或无需加载时为空。 */
		TSharedPtr<FStreamableHandle> ActiveHandle;

		/** 入队时绑定的队列代数，用于过滤已被重置或读档淘汰的旧异步回调。 */
		uint64 Generation = 0;
	};

	/** 已恢复的数据作用于当前世界；节点全部读完后才生成角色/移除敌人。 */
	void ApplyLoadedState();

	/** 后加载子关卡也应用死亡进度；全局委托必须过滤所属 GameInstance。 */
	void HandleLevelAdded(ULevel* Level, UWorld* World);

	/** 世界开始销毁时立刻取消未完成的异步预载与分帧生成，防止跨世界残留句柄。 */
	void HandleWorldBeginTearDown(UWorld* World);

	/** 将一批候选出生位按距离参考点由近到远排序后依次入队，优先落地玩家身边的敌人。 */
	void EnqueueEnemySpawnsSorted(TArray<AGinkgoCheckpointEnemySpawn*>& Markers, const FVector& PriorityOrigin);

	/** 获取用于异步加载资产的流式管理器；优先复用全局 UAssetManager，缺失时退回子系统自有实例。 */
	FStreamableManager& GetStreamableManager();

	/** 为队列头部不超过上限的条目推进异步预载，使后台 I/O 与前台分帧落地流水并行。 */
	void PumpEnemySpawnPreloads();

	/** 推进单条请求的级联异步预载；返回 false 表示对应出生位已失效或不再满足生成条件，应从队列移除。 */
	bool AdvanceEnemySpawnPreload(FPendingEnemySpawn& Entry);

	/** 异步加载完成回调入口；仅推进下一跳预载而不直接同步 SpawnActor，严格保持分帧预算。 */
	void HandleEnemyPreloadCompleted(TWeakObjectPtr<AGinkgoCheckpointEnemySpawn> WeakMarker, uint64 ExpectedGeneration);

	/** 释放单条请求持有的全部流式句柄；bCancel 为真时同时取消未完成的加载任务。 */
	static void ReleasePendingSpawnHandles(FPendingEnemySpawn& Entry, bool bCancel);

	/** 确保核心帧调度器处于运行状态，仅在队列非空时注册，避免空闲逐帧开销。 */
	void EnsureEnemySpawnTicker();

	/** 注销分帧生成回调句柄，在队列清空、取消或子系统反初始化时调用。 */
	void StopEnemySpawnTicker();

	/** 核心帧回调；驱动一次 ProcessPendingEnemySpawns 并在队列为空时自动退订。 */
	bool TickEnemySpawnQueue(float DeltaTime);

	/** 受反射追踪的进度快照，强引用的资产必须保持到存档写完以及跨地图恢复完成。 */
	UPROPERTY(Transient)
	FGinkgoCheckpointState State;

	/** 节点恢复后置位，成功应用给已生成玩家后清除；跨地图期间保持。 */
	bool bPlayerRestorePending = false;

	/** 仅存档节点 PostLoad 应用新快照时为真；重载地图期间不能用旧世界尚未存档的击杀移除敌人。 */
	bool bApplyingLoadedState = false;

	/** 待异步预载并分帧落地的检查点敌人队列；由子系统在权威端/单机端维护，空闲时为空。 */
	TArray<FPendingEnemySpawn> PendingEnemySpawns;

	/** 当前待生成队列归属的世界弱引用；切图或世界重建时据此自动作废旧队列。 */
	TWeakObjectPtr<UWorld> PendingSpawnWorld;

	/** 单调递增的队列代数；每次清空队列时自增，确保过期的异步预载回调不会误操作新队列。 */
	uint64 EnemySpawnQueueGeneration = 0;

	/** 分帧生成核心 Ticker 句柄；仅在 PendingEnemySpawns 非空期间有效，空闲自动解除注册。 */
	FTSTicker::FDelegateHandle EnemySpawnTickerHandle;

	/** 当全局 UAssetManager 未初始化时按需创建的备用流式加载器，Deinitialize 时随子系统销毁。 */
	TSharedPtr<FStreamableManager> FallbackStreamableManager;

	/** 正在执行 AdvanceEnemySpawnPreload 时置位，防止 RequestAsyncLoad 同步回调导致重入。 */
	bool bAdvancingEnemyPreload = false;
};

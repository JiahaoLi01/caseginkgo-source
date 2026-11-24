#pragma once

#include "AI/GinkgoAIBehaviorOverrides.h"
#include "AI/GinkgoPatrolRoute.h"
#include "GinkgoCheckpointEnemySpawn.generated.h"

class AGinkgoAICharacter;
class AGinkgoCheckpoint;
class UGinkgoAIConfig;
class UGinkgoEnemyArchetype;
class UGinkgoCheckpointSubsystem;
class UGinkgoWeaponDefinition;

/** 一个敌人的关卡出生位，同时提供其巡逻路线。Actor 原点在脚底，局部 X 轴是初始朝向。 */
UCLASS(meta = (DisplayName = "Checkpoint Enemy Spawn"))
class CASEGINKGO_API AGinkgoCheckpointEnemySpawn : public AGinkgoPatrolRoute
{
	GENERATED_BODY()

public:
	AGinkgoCheckpointEnemySpawn();
	virtual void PostLoad() override;

	/** 触发该出生位的检查点，编辑器模式将二者放在同一子关卡。未指定时不生成。 */
	UPROPERTY(EditInstanceOnly, Category = "Encounter")
	TObjectPtr<AGinkgoCheckpoint> Checkpoint;

	/** 生成的敌方角色类型，默认沿用项目的 AI 角色。 */
	UPROPERTY(EditAnywhere, Category = "Encounter")
	TSubclassOf<AGinkgoAICharacter> EnemyClass;

	/** 生成角色的外观、生命与分区护甲。留空时继承 EnemyClass 的出生类型。 */
	UPROPERTY(EditAnywhere, Category = "Encounter", meta = (DisplayName = "敌人类型"))
	TSoftObjectPtr<UGinkgoEnemyArchetype> EnemyType;

	/** 该敌人的行为和备弹配置，留空时沿用角色类默认值。 */
	UPROPERTY(EditAnywhere, Category = "Encounter")
	TSoftObjectPtr<UGinkgoAIConfig> DefinitionOverride;

	/** 该出生位的起始武器，留空时沿用角色配置。 */
	UPROPERTY(EditAnywhere, Category = "Encounter")
	TSoftObjectPtr<UGinkgoWeaponDefinition> WeaponOverride;

	/** 仅覆盖本出生位指定的行为项，未指定的项保留敌人类默认值；读档重建时再次应用。 */
	UPROPERTY(EditAnywhere, Category = "Encounter")
	FGinkgoAIBehaviorOverrides BehaviorOverrides;

	/**
	 * @brief 判定本出生位按当前检查点进度是否需要生成敌人
	 * @param Progress - 检查点进度子系统，用于核对已抵达检查点与已击杀记录
	 * @param OutEnemyId - 可选输出该出生位对应的稳定敌人持久化 ID；不满足生成条件时不写入
	 * @return 满足权威端、未生成、检查点已抵达且未被击杀时返回 true
	 */
	bool CanSpawnForProgress(const UGinkgoCheckpointSubsystem& Progress, FName* OutEnemyId = nullptr) const;

	/** 立即销毁本出生位当前已生成的敌人并清空弱引用，用于读档重建或卸载回收。 */
	void DestroySpawnedEnemy();

	/**
	 * @brief 收集本出生位尚未常驻内存的软引用资产路径，供子系统分跳异步预加载
	 * @param AlreadyAttempted - 本次生成流程已请求过的路径集合，用于跳过无效或重复引用
	 * @param OutMissingPaths - 输出本跳需要通过 FStreamableManager 异步加载的路径列表（去重）
	 * @note 纯内存查询，不触发任何同步 LoadSynchronous；专用服务器自动跳过仅客户端使用的外观模型与模块网格
	 */
	void CollectUnloadedAssets(const TSet<FSoftObjectPath>& AlreadyAttempted, TArray<FSoftObjectPath>& OutMissingPaths) const;

	/** 立即按当前检查点进度执行单次落地生成；由子系统在异步预载完成后按分帧预算调用。读档时可先销毁旧实例。 */
	void ApplyProgress(const UGinkgoCheckpointSubsystem& Progress, bool bReload = false);

protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	virtual void Destroyed() override;

private:
	/** 服务器本次生成的角色，随子关卡卸载而离开世界。死亡进度使用出生位的稳定路径另行保存。 */
	TWeakObjectPtr<AGinkgoAICharacter> SpawnedEnemy;
};

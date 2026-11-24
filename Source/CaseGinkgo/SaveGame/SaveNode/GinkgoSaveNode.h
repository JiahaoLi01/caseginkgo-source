#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class FGinkgoSaveNodeContext;
class IGinkgoSaveNodeMigration;

/**
 * @class IGinkgoSaveNode : 一块可以独立读写的存档（节点）
 *
 * 节点是注册、依赖、版本管理的基本单位。想进存档的系统实现它，注册到 FGinkgoSaveNodeRegistry。
 * 设计见 Docs/Design/SaveGame.md。
 *
 * 约定：
 *  - 节点 ID 是写死的 FGuid 常量，同时用作这个节点的 FCustomVersion GUID，要在 .cpp 里用 FDevVersionRegistration 注册
 *  - 节点 ID 不能重复，注册时发现重复直接 fatal
 *  - 依赖都是硬依赖：依赖的节点没注册，存档、读档时 fatal
 */
class CASEGINKGO_API IGinkgoSaveNode
{
public:
	virtual ~IGinkgoSaveNode() = default;

	/** 节点的固定标识：返回写死的 FGuid 常量 */
	virtual FGuid GetNodeId() const = 0;

	/** 给人看的分类标签（如 SaveNode.Core.Actors）：日志、环检测、JSON 的键用，不决定节点的身份 */
	virtual FGameplayTag GetClassificationTag() const = 0;

	/** 代码当前的节点版本。枚举只往后加，不重排 */
	virtual int32 GetCurrentVersion() const = 0;

	/** 读档时必须先恢复的节点 */
	virtual void GetDependencies(TArray<FGuid>& Out) const {}

	/**
	 * 节点改名、合并时列出旧 ID。
	 * @note 接口先搬过来了，读档的调度还没有读它，真要改节点 ID 时再接上。
	 */
	virtual void GetReplaces(TArray<FGuid>& Out) const {}

	/**
	 * 这个节点的迁移步骤。
	 * @note 接口先搬过来了，读档的调度还没有读它，见 IGinkgoSaveNodeMigration。
	 */
	virtual void GetMigrations(TArray<TSharedRef<IGinkgoSaveNodeMigration>>& Out) const {}

	/** 读写的统一入口，用 Ctx.IsLoading() 区分。字段优先用 Ctx 的 Field、OptionalField、FieldSince */
	virtual void Serialize(FGinkgoSaveNodeContext& Ctx) = 0;

	/** 存档里没有这个节点时的回退（比如给默认值）。按拓扑序穿插执行：依赖的节点已经走过 Serialize 或这个函数 */
	virtual void OnMissingFromSave(FGinkgoSaveNodeContext& Ctx) {}

	/**
	 * 读档的第二遍：所有节点的 Serialize、OnMissingFromSave 走完以后再调一遍。在这里广播、重建缓存、接跨节点引用、通知界面。
	 * 依赖的节点已经走完这个函数。只在读档时调；不管这个节点走的是 Serialize 还是 OnMissingFromSave 都调一次。
	 * Ctx 不带存档，调 Field 会 checkf。
	 */
	virtual void PostLoadGame(FGinkgoSaveNodeContext& Ctx) {}
};

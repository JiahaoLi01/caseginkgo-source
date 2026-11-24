#pragma once

#include "CoreMinimal.h"

class FGinkgoSaveNodeContext;

/**
 * @class IGinkgoSaveNodeMigration : 节点版本迁移的一步，把节点的内存数据从 vN 升到 v(N+1)
 *
 * @note 接口先搬过来了，读档的调度还没有调它（见 TGinkgoSaveGameSerializer::DispatchNodesOnLoad），要做语义迁移时再接上。
 * 现在节点的版本演进靠 FGinkgoSaveNodeContext 的 OptionalField、FieldSince。
 */
class CASEGINKGO_API IGinkgoSaveNodeMigration
{
public:
	virtual ~IGinkgoSaveNodeMigration() = default;

	/** 输入的版本号 N；这一步把数据从 vN 升到 v(N+1) */
	virtual int32 FromVersion() const = 0;

	/** 执行迁移：执行前数据是 v(FromVersion()) 的，执行后是 v(FromVersion() + 1) 的 */
	virtual void Apply(FGinkgoSaveNodeContext& Ctx) = 0;
};

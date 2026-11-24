#pragma once

#include "CoreMinimal.h"

class IGinkgoSaveNode;

/**
 * @class FGinkgoSaveNodeRegistry : 存档节点的注册表，每个 GameInstance 一份
 *
 * 各系统在自己的 Initialize 里把节点注册进来；序列化器按 GetSaveOrder() 的拓扑序读写。
 *
 * 约定：
 *  - 节点 ID 重复：注册时立即 fatal（复制节点忘了换 GUID 的典型情况）
 *  - 拓扑排序在第一次 GetSaveOrder() 时做，注册、反注册后重算
 *  - 有环、依赖的节点没注册：把名字打全再 fatal
 *  - 不加锁：同一时间只有一个存档操作
 */
class CASEGINKGO_API FGinkgoSaveNodeRegistry
{
public:
	/** 构造是公开的：测试建独立的注册表验证拓扑，游戏里用 UGinkgoSaveGameSubsystem::GetRegistry() */
	FGinkgoSaveNodeRegistry() = default;

	/** 注册节点；ID 重复时 fatal */
	void Register(TSharedRef<IGinkgoSaveNode> Node);

	/** 反注册；找不到就什么也不做 */
	void Unregister(const FGuid& NodeId);

	/** 拓扑序的节点 ID。依赖缺失、有环时 fatal。返回值在下一次注册、反注册之前有效 */
	const TArray<FGuid>& GetSaveOrder();

	/** 按 ID 找节点；找不到返回 nullptr */
	IGinkgoSaveNode* Find(const FGuid& NodeId) const;

	int32 Num() const { return Nodes.Num(); }

private:
	/** Kahn 算法 + 环检测，写 CachedOrder */
	void ResolveTopology();

	/** 节点 ID 换成给人看的字符串，打日志用 */
	FString DescribeNode(const FGuid& NodeId) const;

	TMap<FGuid, TSharedRef<IGinkgoSaveNode>> Nodes;
	TArray<FGuid> CachedOrder;
	bool bDirty = true;
};

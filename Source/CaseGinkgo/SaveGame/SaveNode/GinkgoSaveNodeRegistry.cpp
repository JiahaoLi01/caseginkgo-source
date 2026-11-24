#include "SaveGame/SaveNode/GinkgoSaveNodeRegistry.h"

#include "SaveGame/SaveNode/GinkgoSaveNode.h"
#include "Utilities/Debug/DebugCategory.h"

void FGinkgoSaveNodeRegistry::Register(TSharedRef<IGinkgoSaveNode> Node)
{
	const FGuid NodeId = Node->GetNodeId();

	checkf(NodeId.IsValid(), TEXT("存档节点 '%s' 的 ID 不是合法的 GUID"), *Node->GetClassificationTag().ToString());

	if (const TSharedRef<IGinkgoSaveNode>* Existing = Nodes.Find(NodeId))
	{
		UE_LOG(LogGinkgoSave, Error, TEXT("[Save] 节点 ID %s 重复：新的 '%s'，已有的 '%s'"),
			*NodeId.ToString(EGuidFormats::DigitsWithHyphens),
			*Node->GetClassificationTag().ToString(),
			*(*Existing)->GetClassificationTag().ToString());
		checkf(false, TEXT("存档节点的 ID 重复，见日志"));
		return;
	}

	Nodes.Add(NodeId, Node);
	bDirty = true;

	UE_LOG(LogGinkgoSave, Verbose, TEXT("[Save] 注册节点 %s"), *DescribeNode(NodeId));
}

void FGinkgoSaveNodeRegistry::Unregister(const FGuid& NodeId)
{
	if (Nodes.Remove(NodeId) > 0)
	{
		bDirty = true;
	}
}

IGinkgoSaveNode* FGinkgoSaveNodeRegistry::Find(const FGuid& NodeId) const
{
	const TSharedRef<IGinkgoSaveNode>* Node = Nodes.Find(NodeId);
	return Node ? &Node->Get() : nullptr;
}

const TArray<FGuid>& FGinkgoSaveNodeRegistry::GetSaveOrder()
{
	if (bDirty)
	{
		ResolveTopology();
		bDirty = false;
	}
	return CachedOrder;
}

FString FGinkgoSaveNodeRegistry::DescribeNode(const FGuid& NodeId) const
{
	const TSharedRef<IGinkgoSaveNode>* Node = Nodes.Find(NodeId);
	return FString::Printf(TEXT("%s (%s)"),
		Node ? *(*Node)->GetClassificationTag().ToString() : TEXT("<没有注册>"),
		*NodeId.ToString(EGuidFormats::DigitsWithHyphens));
}

void FGinkgoSaveNodeRegistry::ResolveTopology()
{
	CachedOrder.Reset();
	CachedOrder.Reserve(Nodes.Num());

	// 每个节点还差几个依赖没满足；谁依赖它（它满足以后要给谁减一）
	TMap<FGuid, int32> InDegree;
	InDegree.Reserve(Nodes.Num());
	TMap<FGuid, TArray<FGuid>> DependedBy;
	DependedBy.Reserve(Nodes.Num());

	TArray<FGuid> Dependencies;
	for (const TPair<FGuid, TSharedRef<IGinkgoSaveNode>>& Pair : Nodes)
	{
		Dependencies.Reset();
		Pair.Value->GetDependencies(Dependencies);

		InDegree.Add(Pair.Key, Dependencies.Num());

		for (const FGuid& Dependency : Dependencies)
		{
			DependedBy.FindOrAdd(Dependency).Add(Pair.Key);
		}
	}

	// 依赖的节点必须都注册了
	bool bHasMissingDependency = false;
	for (const TPair<FGuid, TArray<FGuid>>& Pair : DependedBy)
	{
		if (!Nodes.Contains(Pair.Key))
		{
			bHasMissingDependency = true;
			UE_LOG(LogGinkgoSave, Error, TEXT("[Save] 依赖的节点 %s 没有注册，依赖它的是："), *Pair.Key.ToString(EGuidFormats::DigitsWithHyphens));
			for (const FGuid& Dependent : Pair.Value)
			{
				UE_LOG(LogGinkgoSave, Error, TEXT("  - %s"), *DescribeNode(Dependent));
			}
		}
	}
	checkf(!bHasMissingDependency, TEXT("存档节点依赖的节点没有注册，见日志"));

	// Kahn 拓扑排序
	TArray<FGuid> Queue;
	Queue.Reserve(Nodes.Num());
	for (const TPair<FGuid, int32>& Pair : InDegree)
	{
		if (Pair.Value == 0)
		{
			Queue.Add(Pair.Key);
		}
	}

	for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
	{
		const FGuid Current = Queue[Cursor];
		CachedOrder.Add(Current);

		if (const TArray<FGuid>* Dependents = DependedBy.Find(Current))
		{
			for (const FGuid& Dependent : *Dependents)
			{
				int32& Remaining = InDegree[Dependent];
				if (--Remaining == 0)
				{
					Queue.Add(Dependent);
				}
			}
		}
	}

	// 排不完的都在某个环里
	if (CachedOrder.Num() < Nodes.Num())
	{
		UE_LOG(LogGinkgoSave, Error, TEXT("[Save] 存档节点的依赖有环，还在环里的："));
		for (const TPair<FGuid, int32>& Pair : InDegree)
		{
			if (Pair.Value > 0)
			{
				UE_LOG(LogGinkgoSave, Error, TEXT("  - %s（还差 %d 个依赖）"), *DescribeNode(Pair.Key), Pair.Value);
			}
		}
		CachedOrder.Reset();
		checkf(false, TEXT("存档节点的依赖有环，见日志"));
	}

	UE_LOG(LogGinkgoSave, Verbose, TEXT("[Save] 排好了 %d 个节点"), CachedOrder.Num());
}

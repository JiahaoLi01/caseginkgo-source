#pragma once

#include "CoreMinimal.h"
#include "SaveGame/SaveNode/GinkgoSaveNode.h"

/** 检查点的独立存档节点；采集在触发时完成，Serialize 无副作用，二进制和开发期 JSON 写同一快照。 */
class CASEGINKGO_API FGinkgoCheckpointSaveNode : public IGinkgoSaveNode
{
public:
	/** 固定节点 GUID，同时作为该节点的版本 GUID。 */
	static const FGuid NodeGuid;

	virtual FGuid GetNodeId() const override { return NodeGuid; }
	virtual FGameplayTag GetClassificationTag() const override;
	virtual int32 GetCurrentVersion() const override { return 0; }
	virtual void GetDependencies(TArray<FGuid>& Out) const override;
	virtual void Serialize(FGinkgoSaveNodeContext& Ctx) override;
	virtual void OnMissingFromSave(FGinkgoSaveNodeContext& Ctx) override;
	virtual void PostLoadGame(FGinkgoSaveNodeContext& Ctx) override;
};

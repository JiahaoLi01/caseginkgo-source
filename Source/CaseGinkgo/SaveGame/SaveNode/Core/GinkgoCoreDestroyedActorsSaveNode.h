#pragma once

#include "CoreMinimal.h"
#include "SaveGame/SaveNode/GinkgoSaveNode.h"

/**
 * @class FGinkgoCoreDestroyedActorsSaveNode : 内置节点，存运行时被销毁的关卡 Actor
 *
 * 存档：写 UGinkgoSaveGameSubsystem 记下的被销毁的关卡 Actor 的名字。
 * 读档：找到同名的关卡 Actor 销毁掉，让存档时已经没了的东西读档后还是没了。
 * 依赖 SaveNode.Core.Actors：要销毁的可能是 Actors 节点处理过的对象。
 */
class CASEGINKGO_API FGinkgoCoreDestroyedActorsSaveNode : public IGinkgoSaveNode
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

	virtual FGuid GetNodeId() const override { return NodeGuid; }
	virtual FGameplayTag GetClassificationTag() const override;
	virtual int32 GetCurrentVersion() const override { return LatestVersion; }

	virtual void GetDependencies(TArray<FGuid>& Out) const override;

	virtual void Serialize(FGinkgoSaveNodeContext& Ctx) override;
};

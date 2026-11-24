#pragma once

#include "Animation/Actions/GinkgoAnimAction.h"
#include "GinkgoTraversalAction.generated.h"

class UGinkgoTraversalComponent;

/** 一次全身攀爬动作；占用与蒙太奇交给 AnimAction，物理与恢复资源由角色的 Traversal 组件持有。 */
UCLASS()
class CASEGINKGO_API UGinkgoTraversalAction : public UGinkgoAnimAction
{
	GENERATED_BODY()

public:
	static UGinkgoTraversalAction* Make(UGinkgoTraversalComponent& Owner);

protected:
	virtual void OnStart(FGinkgoAnimActionSetup& Setup) override;

private:
	/** 本次请求所属组件，动作不延长角色生命周期；开始时失效则直接中止。 */
	TWeakObjectPtr<UGinkgoTraversalComponent> Traversal;
};

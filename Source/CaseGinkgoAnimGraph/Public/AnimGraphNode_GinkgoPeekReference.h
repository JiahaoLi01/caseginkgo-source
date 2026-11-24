#pragma once

#include "AnimGraphNode_Base.h"
#include "Animation/AnimNode_GinkgoPeekReference.h"
#include "AnimGraphNode_GinkgoPeekReference.generated.h"

/** 自动探身的参考姿势捕获节点；编辑器只负责展示与编译，运行时不依赖编辑器模块。 */
UCLASS()
class CASEGINKGOANIMGRAPH_API UAnimGraphNode_GinkgoPeekReference : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	/** 姿势透传与参考骨骼捕获的运行时节点 */
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_GinkgoPeekReference Node;

	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override { return NSLOCTEXT("Ginkgo", "PeekReference", "Ginkgo Peek Reference"); }
	virtual FString GetNodeCategory() const override { return TEXT("Ginkgo"); }
};

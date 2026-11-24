#pragma once

#include "Framework/GinkgoEventHub.h"
#include "Presentation/GinkgoDialogueSet.h"
#include "GinkgoDialogueComponent.generated.h"

/** 权威端订阅本对象的事件，按台词表选句并发送给附近玩家。组件不读取 AI 黑板或控制器状态。 */
UCLASS(ClassGroup=(Ginkgo), meta=(BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoDialogueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开局加载并订阅其中的事件；留空的对象保持静音。 */
	UPROPERTY(EditAnywhere, Category="Dialogue")
	TSoftObjectPtr<UGinkgoDialogueSet> DialogueSet;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	UFUNCTION()
	void HandleEvent(const FGinkgoActorEvent& Event);

	/** 已订阅的台词表，BeginPlay 到 EndPlay 持有强引用，供事件回调同步取句。 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoDialogueSet> ActiveSet;

	struct FPlayback
	{
		/** 上一次说出的变体下标；INDEX_NONE 表示尚未播放。 */
		int32 LastLine = INDEX_NONE;

		/** 允许再次响应本标签的世界时间（秒）。 */
		double ReadyAt = 0.;
	};

	/** 当前角色各标签的独立播放记录，不在资产上写运行时状态。 */
	TMap<FGameplayTag, FPlayback> Playback;
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "GinkgoEventHub.generated.h"

/** 对象域事件的一次快照，由发布者填写；中心不保留历史，也不复制感知信息给客户端。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoActorEvent
{
	GENERATED_BODY()

	/** 精确匹配的事件名；同一标签在不同 Actor 的中心相互独立。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag Tag;

	/** 事件涉及的对象，可为空；例如失去视线前最后一次看见的角色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<AActor> Subject = nullptr;

	/** 事件发生时采样的世界坐标（cm），不会跟随 Subject 后续移动。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector Location = FVector::ZeroVector;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FGinkgoEventListener, const FGinkgoActorEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGinkgoEventChannel, const FGinkgoActorEvent&, Event);

/** 挂到需要共享事件的 Actor。订阅按标签隔离；同一对象与回调可重复注册，销毁监听对象后弱委托自然失效。 */
UCLASS(ClassGroup=(Ginkgo), meta=(BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoEventHub : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="Ginkgo|Events")
	static UGinkgoEventHub* Find(const AActor* OwnerActor);

	UFUNCTION(BlueprintCallable, Category="Ginkgo|Events")
	void Subscribe(FGameplayTag Tag, FGinkgoEventListener Listener);

	UFUNCTION(BlueprintCallable, Category="Ginkgo|Events")
	void Unsubscribe(FGameplayTag Tag, FGinkgoEventListener Listener);

	/** 同步通知本次发布开始时的订阅者，回调内修改订阅从下一次发布起生效。 */
	UFUNCTION(BlueprintCallable, Category="Ginkgo|Events")
	void Publish(const FGinkgoActorEvent& Event);

private:
	/** 仅游戏线程访问；委托弱引用订阅者，最后一个订阅撤销时移除频道。 */
	TMap<FGameplayTag, FGinkgoEventChannel> Channels;
};

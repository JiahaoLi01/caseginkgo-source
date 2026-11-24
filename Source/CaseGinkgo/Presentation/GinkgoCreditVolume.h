#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/GinkgoCredit.h"
#include "GinkgoCreditVolume.generated.h"

class AGinkgoCharacter;
class AGinkgoPlayerController;
class UBoxComponent;

/** 关卡署名触发体积。只负责内容与进入规则，表现/排队属于进入玩家的本地 HUD，不存档或修改复活点。 */
UCLASS(Blueprintable, meta = (DisplayName = "Ginkgo Credit Volume"))
class CASEGINKGO_API AGinkgoCreditVolume : public AActor
{
	GENERATED_BODY()

public:
	AGinkgoCreditVolume();

	/** 按数组顺序逐组播放的署名；组内作者与作品同屏，整组内容为空时忽略。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Credits", meta = (TitleProperty = "Name"))
	TArray<FGinkgoCredit> Credits = {FGinkgoCredit()};

	/** 默认每个玩家在本次关卡生命周期内只触发一次；开启后离开再进入会重新排队，不在体积内无限循环。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Credits")
	bool bRepeatable = false;

	/** 可调 Box Extent 的触发区域，只与玩家胶囊重叠，不阻挡移动，也不响应武器网格。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Credits")
	TObjectPtr<UBoxComponent> Trigger;

	/** 与胶囊重叠共用的激活入口；仅权威端存活玩家有效，成功后通过拥有者 RPC 入队。 */
	UFUNCTION(BlueprintCallable, Category = "Credits")
	bool TryActivate(AGinkgoCharacter* Player);

private:
	/** 本次关卡已播放的控制器，弱引用不延长玩家生命周期；不写入存档。 */
	TSet<TWeakObjectPtr<AGinkgoPlayerController>> Triggered;

	/** 本次进入已激活的控制器；EndOverlap 清除，避免重复重叠通知连续入队。 */
	TSet<TWeakObjectPtr<AGinkgoPlayerController>> Inside;

	/** 只接收玩家胶囊的首次重叠。 */
	UFUNCTION()
	void HandleBegin(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);

	/** 胶囊离开后重新允许 Repeatable 触发。 */
	UFUNCTION()
	void HandleEnd(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex);
};

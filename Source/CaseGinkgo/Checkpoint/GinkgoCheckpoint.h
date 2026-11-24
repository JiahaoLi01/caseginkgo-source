#pragma once

#include "CoreMinimal.h"
#include "Checkpoint/GinkgoCheckpointState.h"
#include "GameFramework/Actor.h"
#include "GinkgoCheckpoint.generated.h"

class AGinkgoCharacter;
class UBoxComponent;
class UArrowComponent;

/** 可直接拖入关卡的检查点。体积只提交激活意图，存档/敌人进度属于子系统，地点表现属于本地控制器。 */
UCLASS(Blueprintable)
class CASEGINKGO_API AGinkgoCheckpoint : public AActor
{
	GENERATED_BODY()

public:
	AGinkgoCheckpoint();
	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** 手动激活与重叠使用同一路径；只有权威端、存活玩家能通过，重复和读档期间的触发会被拒绝。 */
	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	bool TryActivate(AGinkgoCharacter* Player);

	/** 关卡内可选的稳定 ID；空值使用 Actor 路径，改名后希望延续存档时应显式填写。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Checkpoint")
	FName CheckpointId;

	/** 地点大字和提示音的配置，不承担存档操作。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	FGinkgoCheckpointAnnouncement Announcement;

	/** 复活胶囊中心相对触发器的变换，单位 cm/度；编辑器中的可拖动控件与箭头表示此位置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint", meta = (MakeEditWidget = true))
	FTransform RespawnPoint = FTransform(FVector(300., 0., 90.));

	/** 默认同一存档进度只触发一次；开启后离开再进入可以重新记录，不会在体积内逐帧写档。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	bool bRepeatable = false;

	/** 触发区域，可在组件详情中改 Box Extent；只与 Pawn 重叠，无阻挡。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Checkpoint")
	TObjectPtr<UBoxComponent> Trigger;

	/** 检查点成功存档并处理关联出生位后，在服务器调用，供关卡追加其他刷新行为。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Checkpoint", meta = (DisplayName = "On Enemy Refresh Requested"))
	void OnEnemyRefreshRequested(AGinkgoCharacter* Player);

	/** 返回已去除缩放及俯仰/翻滚的世界复活变换。 */
	FTransform GetRespawnTransform() const;

private:
	/** 仅用于编辑器定位朝向，不参与碰撞和触发。 */
	UPROPERTY(VisibleAnywhere, Category = "Checkpoint")
	TObjectPtr<UArrowComponent> RespawnArrow;

	/** 过滤玩家胶囊以免身体网格或武器组件重复触发。 */
	UFUNCTION()
	void HandleOverlap(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);
};

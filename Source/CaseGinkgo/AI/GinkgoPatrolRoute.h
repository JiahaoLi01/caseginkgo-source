#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GinkgoPatrolRoute.generated.h"

class USplineComponent;

/** 一个路线点的位置与停留时间；路线 Actor 的变换统一作用于全部点。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoPatrolPoint
{
	GENERATED_BODY()

	/** 相对路线 Actor 的位置（cm），编辑器中可以拖动；运行时通过路线变换转成世界坐标。 */
	UPROPERTY(EditAnywhere, Category = "Patrol", meta = (MakeEditWidget))
	FVector Location = FVector::ZeroVector;

	/** 到达此点后停留的秒数；0 表示不停留。被战斗打断后回到此点重新计时。 */
	UPROPERTY(EditAnywhere, Category = "Patrol", meta = (ClampMin = "0"))
	float WaitSeconds = 0.f;
};

/** 关卡中的闭合巡逻路线。只定义路线，不保存任何敌人的进度；多个 AI 可共享并从不同点开始。 */
UCLASS()
class CASEGINKGO_API AGinkgoPatrolRoute : public AActor
{
	GENERATED_BODY()

public:
	AGinkgoPatrolRoute();
	virtual void OnConstruction(const FTransform& Transform) override;
	FVector GetPointLocation(int32 Index) const;

	/** 按序循环经过的路线点；空数组让 AI 原地待命，单点可以用作固定哨位。 */
	UPROPERTY(EditAnywhere, Category = "Patrol")
	TArray<FGinkgoPatrolPoint> Points;

private:
	/** 编辑器路线预览，由 Points 重建，使用直线段；不参与导航和碰撞，游戏中隐藏。 */
	UPROPERTY(VisibleAnywhere, Category = "Patrol")
	TObjectPtr<USplineComponent> Preview;
};

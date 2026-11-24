#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Interaction/GinkgoInteractable.h"
#include "GinkgoInteractableComponent.generated.h"

/**
 * @class UGinkgoInteractableComponent : 被交互组件
 * IGinkgoInteractable 的默认实现，见 Docs/Design/Interaction.md「被交互组件」。挂到任何 Actor 上它就能被交互，不用改 Actor 的类。
 * - 数据按实例配：名字、动作的字、两个距离、隔墙显示。
 * - 它的位置就是提示圈画在哪、距离从哪量，所以放在东西的中心（枪的中段、门的把手）。一个 Actor 要几种交互就挂几个。
 * - 开始时登记到 UGinkgoInteractionSubsystem，结束时注销。
 * - 条件与行为是虚函数：默认可以交互、什么也不做，一种交互写一个子类覆盖（拾取是 UGinkgoPickupComponent）。
 *
 * 联网：交互方的 RPC 直接引用它。构造函数里建的组件名字稳定，能按路径在网络上引用，所以它本身不用复制；它的 Actor 要能在网络上引用
 * （关卡里摆的，或复制的）。
 */
UCLASS(ClassGroup = (Ginkgo), meta = (BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoInteractableComponent : public USceneComponent, public IGinkgoInteractable
{
	GENERATED_BODY()

public:
	UGinkgoInteractableComponent();

	/** 提示里的名字，比如"98k" */
	UPROPERTY(EditAnywhere, Category = "Interaction")
	FText DisplayName;

	/** 动作的字，比如"拾取"：提示里和名字拼成"拾取 98k" */
	UPROPERTY(EditAnywhere, Category = "Interaction")
	FText ActionText;

	/** 离多远开始提示（空心圈）。大了远处就冒出圈，小了跑过去时圈出来得晚 */
	UPROPERTY(EditAnywhere, Category = "Interaction", meta = (ClampMin = 0, Units = "cm"))
	float PromptRadius = 600.f;

	/** 离多远可以交互：距离量到角色胶囊的中轴线段，与东西摆得高低无关 */
	UPROPERTY(EditAnywhere, Category = "Interaction", meta = (ClampMin = 0, Units = "cm"))
	float InteractRadius = 180.f;

	/** 隔着墙也显示提示圈。能不能交互照样要从角色的眼睛看得见 */
	UPROPERTY(EditAnywhere, Category = "Interaction")
	bool bShowPromptThroughWalls = false;

	//~ Begin IGinkgoInteractable
	virtual FVector GetInteractionLocation() const override;
	virtual FGinkgoInteractionRanges GetInteractionRanges() const override;
	virtual FText GetInteractionText() const override;
	virtual bool ShowsPromptThroughWalls() const override;
	/** 默认可以交互：子类覆盖 */
	virtual EGinkgoInteractionAvailability GetAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const override;
	/** 默认什么也不做：子类覆盖 */
	virtual void Interact(const FGinkgoInteractionContext& Context) override;
	//~ End IGinkgoInteractable

protected:
	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent
};

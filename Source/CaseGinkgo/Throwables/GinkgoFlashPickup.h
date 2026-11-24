#pragma once

#include "Interaction/GinkgoPickup.h"
#include "GinkgoFlashPickup.generated.h"

/** 关卡可放置的闪光弹。拾取由左臂完成，服务器确认后占用一个腰侧槽位。 */
UCLASS(meta = (DisplayName = "闪光弹拾取物"))
class CASEGINKGO_API AGinkgoFlashPickup : public AGinkgoPickup
{
	GENERATED_BODY()

public:
	AGinkgoFlashPickup();
	virtual void BeginLocalInteract(const FGinkgoInteractionContext& Context) override;
	virtual void OnInteractResult(const FGinkgoInteractionContext& Context, EGinkgoInteractionResult Result) override;
	virtual EGinkgoInteractionAvailability GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const override;

protected:
	virtual void GrantLocal(AGinkgoCharacter& Character) override;
	virtual void OnServerClaimed(const FGinkgoInteractionContext& Context) override;
	virtual EGinkgoInteractionAvailability GetPickupAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const override;
	virtual int32 GetPickupPriority() const override { return Ginkgo::Occupancy::PriorityThrow; }
	virtual TOptional<EGinkgoHand> GetRequiredHand() const override { return EGinkgoHand::Left; }
};

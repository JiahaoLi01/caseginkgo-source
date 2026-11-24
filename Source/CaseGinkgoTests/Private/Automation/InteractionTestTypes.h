#pragma once

#include "CoreMinimal.h"
#include "Interaction/GinkgoInteractable.h"
#include "InteractionTestTypes.generated.h"

/**
 * @class UGinkgoFakeInteractable : 测试用的交互目标
 * 不是组件、不在世界里：交互方与子系统只认接口，测试用它就够
 */
UCLASS(HideDropdown, NotBlueprintable)
class UGinkgoFakeInteractable : public UObject, public IGinkgoInteractable
{
	GENERATED_BODY()

public:
	FVector Location = FVector::ZeroVector;
	EGinkgoInteractionAvailability Availability = EGinkgoInteractionAvailability::Available;
	int32 InteractCount = 0;

	virtual FVector GetInteractionLocation() const override { return Location; }
	virtual FGinkgoInteractionRanges GetInteractionRanges() const override { return {}; }
	virtual FText GetInteractionText() const override { return FText::FromString(TEXT("测试")); }
	virtual EGinkgoInteractionAvailability GetAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const override { return Availability; }
	virtual void Interact(const FGinkgoInteractionContext& Context) override { ++InteractCount; }
};

/** 没实现接口的对象：子系统不该收 */
UCLASS(HideDropdown, NotBlueprintable)
class UGinkgoNotInteractable : public UObject
{
	GENERATED_BODY()
};

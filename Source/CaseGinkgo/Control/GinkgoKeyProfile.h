#pragma once

#include "CoreMinimal.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "GinkgoKeyProfile.generated.h"

/**
 * @class UGinkgoKeyProfile : 玩家的键位（DefaultInput.ini 的 DefaultPlayerMappableKeyProfileClass）
 * 只改一件事：重建映射时不按键的种类筛。引擎的默认实现要玩家的键与默认键种类相同（都是手柄的键，或都不是），否则当作没有改过、用回默认键，
 * 而清空的键（EKeys::Invalid）不算手柄的键，手柄的键位就清不掉。这里的键能不能绑，设置界面已经查过（Ginkgo::Input::CheckKey）。
 */
UCLASS()
class CASEGINKGO_API UGinkgoKeyProfile : public UEnhancedPlayerMappableKeyProfile
{
	GENERATED_BODY()

public:
	//~ Begin UEnhancedPlayerMappableKeyProfile
	virtual int32 GetPlayerMappedKeysForRebuildControlMappings(const FEnhancedActionKeyMapping& DefaultMapping, TArray<FKey>& OutKeys) const override;
	//~ End UEnhancedPlayerMappableKeyProfile
};

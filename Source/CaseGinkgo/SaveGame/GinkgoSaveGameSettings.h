#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GinkgoSaveGameSettings.generated.h"

/** @struct FGinkgoSaveGameVersionInfo : 一个蓝图枚举的存档版本，配上它固定的 GUID */
USTRUCT(BlueprintType, BlueprintInternalUseOnly)
struct FGinkgoSaveGameVersionInfo
{
	GENERATED_BODY()

public:
	FGinkgoSaveGameVersionInfo() : Id(FGuid::NewGuid()) {}

	/** 构造时随机生成（每条记录一个新 GUID），引擎的成员确定性检查要显式豁免 */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "Version", Meta = (IgnoreForMemberInitializationTest))
	FGuid Id;

	UPROPERTY(EditAnywhere, Category = "Version")
	TObjectPtr<UEnum> Enum = nullptr;
};

/**
 * @class UGinkgoSaveGameSettings : 存档系统的设置
 * 在 项目设置 → Game → 存档 里配置。C++ 节点的版本不用这里，这里只给蓝图的 UseCustomVersion 用。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "存档"))
class CASEGINKGO_API UGinkgoSaveGameSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	FGuid GetVersionId(const UEnum* VersionEnum) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	/** 蓝图的版本枚举：调 UGinkgoSaveGameLibrary::UseCustomVersion 之前要在这里配好 */
	UPROPERTY(EditAnywhere, Config, Category = "Version")
	TArray<FGinkgoSaveGameVersionInfo> Versions;

private:
	mutable TMap<const UEnum*, FGuid> CachedVersions;
};

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GinkgoModularWeaponData.generated.h"

class UCustomizableObject;

/**
 * 复杂模块武器的外观配置，目前由 M4 使用。Mutable 只生成外观；装配、效果和复制仍归武器实例。
 * 每个武器挂点对应一个 Mutable 整数选项参数，选项名来自配件的 ModularOption。
 * 默认完整网格由武器定义提供，首次异步生成期间保持可见；普通武器不引用这份数据。
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoModularWeaponData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 已编译的 Mutable 组合资产，由本机外观对象按需载入；服务器上的玩法不依赖生成结果 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TSoftObjectPtr<UCustomizableObject> Object;

	/** Mutable 输出组件名，与图中的 Skeletal Mesh Component 名一致 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	FName ComponentName = TEXT("Weapon");

	/** 各姿态后坐系数允许保留的最低比例，0.4 表示叠加后至少保留该姿态原值的 40% */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Effects", meta = (ClampMin = "0", ClampMax = "1"))
	float MinimumRecoilFraction = .4f;
};

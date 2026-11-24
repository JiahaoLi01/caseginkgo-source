#pragma once

#include "Engine/DataAsset.h"
#include "GinkgoCharacterModel.generated.h"

class UAnimInstance;
class USkeletalMesh;

/** 一套角色外观。项目骨架先完成移动、武器和攀爬求解，再由这个模型的动画图读取父网格的最终姿势。 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoCharacterModel : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 实际绘制的完整网格，保留该角色自己的骨架、材质和 LOD。 */
	UPROPERTY(EditAnywhere, Category = "Model")
	TObjectPtr<USkeletalMesh> Mesh;

	/** 此套着装省略的部件，按 Mesh 的材质槽名配置。各级 LOD 共用；留空时显示整套模型。 */
	UPROPERTY(EditAnywhere, Category = "Model")
	TArray<FName> HiddenMaterialSlots;

	/** 目标骨架上的重定向图。游戏逻辑和动画通知仍在源网格运行。 */
	UPROPERTY(EditAnywhere, Category = "Model")
	TSubclassOf<UAnimInstance> RetargetClass;
};

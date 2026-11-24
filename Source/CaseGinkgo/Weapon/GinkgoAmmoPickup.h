#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"         // 弹种
#include "Interaction/GinkgoPickup.h"
#include "GinkgoAmmoPickup.generated.h"

/**
 * @class AGinkgoAmmoPickup : 子弹盒
 * 第一种拾取，见 Docs/Design/Interaction.md「拾取子弹盒」：给 4–7 发子弹，加到后备里（按弹种）。
 * - 发数由服务器在开始时掷（或生成时指定），复制给各端，提示里写出来："拾取 7.92 × 57mm Mauser（5 发）"。
 * - 效果在拾取的那台机器上给：后备只在本地，同射击。
 * - 网格默认用设置里的 AmmoBoxMesh（Private/models/MauserAmmoBox 的 SM_MauserAmmoBox，盖开着、带两发散弹），原点在盒底中心。
 */
UCLASS()
class CASEGINKGO_API AGinkgoAmmoPickup : public AGinkgoPickup
{
	GENERATED_BODY()

public:
	AGinkgoAmmoPickup();

	/** @brief 指定发数：只在服务器上，或在生成时（FinishSpawning 之前）设；不设时开始时在 MinRounds–MaxRounds 里掷 */
	void SetRounds(int32 InRounds);

	int32 GetRounds() const { return Rounds; }
	const FGameplayTag& GetAmmoType() const { return AmmoType; }

	//~ Begin AActor
	/** 编辑器里摆放时预览网格 */
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

protected:
	//~ Begin AGinkgoPickup
	/** 后备加 Rounds 发 AmmoType */
	virtual void GrantLocal(AGinkgoCharacter& Character) override;
	//~ End AGinkgoPickup

private:
	UFUNCTION()
	void OnRep_Rounds();

	/** 没指定网格时用设置里的子弹盒 */
	void ApplyDefaultMesh();

	/** 提示里写名字与发数 */
	void ApplyRounds();

	/** 弹种：后备按它记数 */
	UPROPERTY(EditAnywhere, Category = "Ammo", meta = (Categories = "Ammo"))
	FGameplayTag AmmoType;

	/** 提示里的名字 */
	UPROPERTY(EditAnywhere, Category = "Ammo")
	FText AmmoName;

	/** 一盒最少给几发（服务器掷，含两头）。你说的 4–7 发 */
	UPROPERTY(EditAnywhere, Category = "Ammo", meta = (ClampMin = 1))
	int32 MinRounds = 4;

	/** 一盒最多给几发 */
	UPROPERTY(EditAnywhere, Category = "Ammo", meta = (ClampMin = 1))
	int32 MaxRounds = 7;

	/** 这一盒给几发：服务器定，复制给各端；0 是还没掷 */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Rounds, Category = "Ammo", meta = (ClampMin = 0))
	int32 Rounds = 0;
};

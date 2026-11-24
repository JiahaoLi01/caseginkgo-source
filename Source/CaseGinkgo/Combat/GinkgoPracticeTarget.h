#pragma once

#include "CoreMinimal.h"
#include "Combat/GinkgoCombatTypes.h"
#include "GameFramework/Actor.h"
#include "GinkgoPracticeTarget.generated.h"

class UCapsuleComponent;
class UGinkgoDamageableComponent;
class UGinkgoPenetrableComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FGinkgoDamageEvent;

/**
 * @class AGinkgoPracticeTarget : 站着的人形靶（Docs/Design/Damage.md「靶子」）
 * 人体模型放站着的循环，可打击（UGinkgoDamageableComponent）：网格按物理资产的形体挡子弹，胶囊只挡人、不挡子弹。
 * 子弹穿得过（UGinkgoPenetrableComponent，人的厚度 0.3，Docs/Design/Damage.md「穿射」）。
 * 穿背心、戴头盔的：占位的形状挂在骨骼上，不挡子弹，被打穿的那件藏起来。打死了网格变布娃娃，最后那一下按子弹的方向推一把，过一会儿复位
 */
UCLASS()
class CASEGINKGO_API AGinkgoPracticeTarget : public AActor
{
	GENERATED_BODY()

public:
	AGinkgoPracticeTarget();

	/** @brief 配护甲（开局放的时候）：背心盖上胸、腹部，头盔盖头部，耐久按设置 */
	void Configure(const FGinkgoTargetSpawn& Spawn);

	UGinkgoDamageableComponent* GetDamageable() const { return Damageable; }
	USkeletalMeshComponent* GetMesh() const { return Mesh; }

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

	/** 护甲的名字（部件上的 Name、占位的形状按它找） */
	static const FName VestName;
	static const FName HelmetName;

protected:
	void HandleDamaged(UGinkgoDamageableComponent& Component, const FGinkgoDamageEvent& Event);
	void HandleDied(UGinkgoDamageableComponent& Component, const FGinkgoDamageEvent& Event);

	/** @brief 复位：布娃娃收回来，站着放循环，血、护甲回满，形状都出来 */
	void Respawn();

	/** @brief 站着放循环（每个靶子从不同的地方开始，不一起呼吸） */
	void PlayIdle();

	/** @brief 活着时的碰撞：胶囊挡人，网格只给查询（子弹按形体挡），都不挡相机 */
	void ApplyAliveCollision();

	UPROPERTY(VisibleAnywhere, Category = "Target")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, Category = "Target")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Target")
	TObjectPtr<UGinkgoDamageableComponent> Damageable;

	/** 子弹穿得过：人的厚度（组件的默认值，你定的 0.3）。打死的布娃娃照样按它穿 */
	UPROPERTY(VisibleAnywhere, Category = "Target")
	TObjectPtr<UGinkgoPenetrableComponent> Penetrable;

	/** 背心、头盔占位的形状（引擎的基本形状，挂在 spine_04、head 上） */
	UPROPERTY(VisibleAnywhere, Category = "Target")
	TObjectPtr<UStaticMeshComponent> VestShape;

	UPROPERTY(VisibleAnywhere, Category = "Target")
	TObjectPtr<UStaticMeshComponent> HelmetShape;

	FGinkgoTargetSpawn Setup;
	FTimerHandle RespawnTimer;
};

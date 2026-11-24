#pragma once

#include "CoreMinimal.h"
#include "Engine/NetSerialization.h"
#include "Subsystems/WorldSubsystem.h"
#include "GinkgoBulletImpact.generated.h"

class UDecalComponent;
class UMaterialInterface;
class UNiagaraSystem;
class UPrimitiveComponent;

/** 单个真实弹着点的表现数据。固定表面用世界空间，可动表面用组件或骨骼空间，延迟到达的血迹也能随姿势移动。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoBulletImpact
{
	GENERATED_BODY()

	/** 只有可动接收面需要传递组件引用。 */
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Receiver;

	/** 蒙皮表面命中的物理骨骼，None 表示使用组件空间。 */
	UPROPERTY()
	FName Bone;

	/** 接触点（cm），空间由 bAttached 决定。 */
	UPROPERTY()
	FVector_NetQuantize10 Point = FVector::ZeroVector;

	/** 接触面向外的单位法线，与 Point 同空间。 */
	UPROPERTY()
	FVector_NetQuantizeNormal Normal = FVector::UpVector;

	/** 可动接收面已销毁或尚未复制到客户端时丢弃此条，防止把局部坐标当作世界坐标。 */
	UPROPERTY()
	bool bAttached = false;

	/** 可受伤的骨骼身体使用血迹，场景几何使用尘土、碎片与弹痕。 */
	UPROPERTY()
	bool bBlood = false;

	/** 从实际阻挡命中提取表现数据；射空、初始穿透或接收面已销毁时返回 false。 */
	static bool FromHit(const FHitResult& Hit, FGinkgoBulletImpact& Out);
};

/** 每个游戏世界共享一份弹着特效和贴花预算。粒子完成后由 Niagara 回池，贴花限量且自行淡出。 */
UCLASS()
class CASEGINKGO_API UGinkgoBulletImpactSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	void Play(const FGinkgoBulletImpact& Impact);

private:
	/** 向表面外侧扬起的短尘团和碎片，开局加载。 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> Dust;

	/** 命中角色时喷出的少量血雾，开局加载。 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> BloodMist;

	/** 普通表面弹痕投影材质。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Chip;

	/** 随角色命中骨骼移动的血迹投影材质。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Stain;

	/** 按创建顺序循环使用槽位，弱引用允许目标销毁和贴花自然到期。 */
	TArray<TWeakObjectPtr<UDecalComponent>> Marks;

	/** 预算用尽时要替换的最老槽位。 */
	int32 NextMark = 0;
};

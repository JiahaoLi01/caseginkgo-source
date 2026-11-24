#pragma once

#include "CoreMinimal.h"
#include "Combat/GinkgoCombatTypes.h"
#include "GameFramework/Actor.h"
#include "GinkgoGrenadeProjectile.generated.h"

class UGinkgoAmmoDefinition;
class UGinkgoWeaponComponent;
class UMaterialInterface;
class UNiagaraSystem;
class UPointLightComponent;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

namespace Ginkgo::Combat
{
	/**
	 * @brief 计算破片爆炸在给定距离上的伤害倍率：内圈满额，外圈线性衰减到 MinScale，超出外半径归零
	 * @param Distance - 爆心到目标受击点的世界距离（cm，非负）
	 * @param InnerRadius - 满伤害内圈半径（cm）
	 * @param OuterRadius - 爆炸最大作用半径（cm）
	 * @param MinScale - 外圈边界保留的最低伤害比例（0–1）
	 * @return 伤害与击杀冲量共用的缩放倍率（0–1）
	 */
	CASEGINKGO_API float ComputeExplosionScale(float Distance, float InnerRadius, float OuterRadius, float MinScale = 0.2f);
}

/**
 * @class AGinkgoGrenadeProjectile : 40×46mm 榴弹发射器抛射物（M406 高爆弹 / 碰炸闪光弹）
 * 由武器组件在发射 EGinkgoAmmoDelivery::Projectile 弹药时生成，持有出膛初速、弹种数据与发射武器的伤害倍率。
 * 飞行阶段按固定频率（60 Hz）在 Weapon 通道做受重力作用的球形扫掠，保证不同帧率下弹道与落点一致，并让弹头沿速度方向俯仰及绕弹轴自旋。
 * 首次触碰阻挡体即在接触点立即引爆，按 UGinkgoAmmoDefinition::ProjectilePayload 分流：
 * - HighExplosive：直击满额命中 + 遮挡检测破片范围伤害，回传白/红 X 与命中声，播放专用高爆特效 NS_Sub_EXP_Mid_002_02，
 *   并按听音距离分流爆炸声（25 m 内播近炸声 MS_M79_ExplodeClose，25 m 外播远炸声 MS_M79_Explode）；
 * - Flashbang：执行与手投 AGinkgoFlashProjectile 完全一致的近身球与视线致盲、强光球衰减（0.16 s）及 MS_Flashbang_Explode 爆闪声。
 */
UCLASS()
class CASEGINKGO_API AGinkgoGrenadeProjectile : public AActor
{
	GENERATED_BODY()

public:
	/** 高爆榴弹满伤害内圈半径（cm，3.2 m 内满额致死） */
	static constexpr float DefaultInnerRadius = 320.f;

	/** 高爆榴弹破片杀伤外圈半径（cm，11.0 m 外无伤害） */
	static constexpr float DefaultBlastRadius = 1100.f;

	/** 高爆榴弹爆炸特效与火光的半径缩放倍率（用于驱动 NS_Sub_EXP_Mid_002_02 的各发射器空间 User 参数与点光源衰减半径） */
	static constexpr float BlastVfxRadiusScale = 2.f;

	/** 高爆榴弹近距爆炸声（MS_M79_ExplodeClose）的最大听音分界距离（cm，25 m 内播近炸声，超出播远炸声） */
	static constexpr float CloseExplosionDistance = 2500.f;

	/** 最长飞行寿命（秒），超出后自动销毁以防飞出地图边界常驻 */
	static constexpr float MaxFlightSeconds = 12.f;

	AGinkgoGrenadeProjectile();

	/**
	 * @brief 在权威端对世界内所有活动 AI 执行与手投闪光弹完全一致的近身重叠与视线致盲判定
	 * @param World - 当前游戏世界
	 * @param Location - 爆闪中心世界空间坐标（cm）
	 * @param IgnoredActor - 视线遮挡检测忽略的自身弹体 Actor（可为空）
	 * @return 本次被成功致盲的 AI 敌人数量
	 */
	static int32 ApplyFlashBurstToAI(UWorld& World, const FVector& Location, const AActor* IgnoredActor = nullptr);

	/**
	 * @brief 由武器组件在生成后立即注入弹道与伤害参数
	 * @param InVelocity - 世界空间出膛初速向量（cm/s，如高爆弹 7600 cm/s、闪光弹 8200 cm/s）
	 * @param InAmmo - 这一发榴弹的数据资产指针；决定飞行网格与触碰引爆的战斗部类型
	 * @param InWeaponMultiplier - 发射武器的伤害倍率（UGinkgoWeaponDefinition::DamageMultiplier）
	 * @param InWeapons - 发射者的武器组件弱引用，用于回传高爆命中白 X 或客户端请求权威端闪光致盲
	 */
	void InitializeProjectile(const FVector& InVelocity, const UGinkgoAmmoDefinition* InAmmo, float InWeaponMultiplier, UGinkgoWeaponComponent* InWeapons);

	/** 是否已经完成触碰引爆结算 */
	bool HasDetonated() const { return bDetonated; }

	/** 引爆后的世界空间爆心位置（cm）；未引爆时为零向量 */
	const FVector& GetExplosionLocation() const { return ExplosionLocation; }

	/** 高爆榴弹触碰引爆时生成的 Niagara 爆炸特效资产，默认指向 NS_Sub_EXP_Mid_002_02 */
	UNiagaraSystem* GetBlastSystem() const { return BlastSystem; }

	/** 25 m 内高爆榴弹触碰引爆时播放的三维近距爆炸音效资产，默认指向六段随机采样的 MS_M79_ExplodeClose */
	USoundBase* GetHighExplosiveCloseSound() const { return HighExplosiveCloseSound; }

	/** 25 m 外高爆榴弹触碰引爆时播放的三维远距爆炸音效资产，默认指向六段随机采样的 MS_M79_Explode */
	USoundBase* GetHighExplosiveSound() const { return HighExplosiveSound; }

	/** 碰炸闪光弹触碰引爆时播放的三维空间音效资产，默认指向与手投闪光弹一致的 MS_Flashbang_Explode */
	USoundBase* GetFlashExplodeSound() const { return FlashExplodeSound; }

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

private:
	/** 在碰撞点执行对应战斗部（高爆破片或碰炸闪光）的结算与表现，生命周期内只执行一次 */
	void Detonate(const FHitResult& ImpactHit);

	/** 出膛飞行弹头网格组件（局部 +X 朝弹尖），引爆瞬间隐藏 */
	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> Body;

	/** 闪光弹引爆瞬间短暂发光的球体网格组件，平时与高爆模式均隐藏 */
	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UStaticMeshComponent> BurstSphere;

	/** 引爆瞬间的闪光点光源组件，平时隐藏，高爆呈暖橙火光、闪光弹呈同手投一致的强白闪光 */
	UPROPERTY(VisibleAnywhere, Category = "Projectile")
	TObjectPtr<UPointLightComponent> BurstLight;

	/** 闪光弹发光球体使用的自发光材质资产（M_FlashBurst），构造时加载 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BurstMaterial;

	/** 高爆榴弹触碰引爆时的火焰、浓烟、火星与碎屑 Niagara 系统资产（NS_Sub_EXP_Mid_002_02），构造时加载 */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> BlastSystem;

	/** 25 m 内高爆榴弹触碰引爆时的三维近距爆炸音效资产（MS_M79_ExplodeClose，内嵌 SA_M79_Explode 衰减），构造时加载 */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> HighExplosiveCloseSound;

	/** 25 m 外高爆榴弹触碰引爆时的三维远距爆炸音效资产（MS_M79_Explode，内嵌 SA_M79_Explode 衰减），构造时加载 */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> HighExplosiveSound;

	/** 碰炸闪光弹触碰引爆时的三维空间爆闪音效资产（MS_Flashbang_Explode，与手投闪光弹一致），构造时加载 */
	UPROPERTY(Transient)
	TObjectPtr<USoundBase> FlashExplodeSound;

	/** 这一发抛射物的弹种定义；由 InitializeProjectile 注入，飞行与引爆期间只读 */
	UPROPERTY(Transient)
	TObjectPtr<const UGinkgoAmmoDefinition> AmmoDefinition;

	/** 这一发抛射物的战斗部类型，由 InitializeProjectile 从弹种定义缓存 */
	EGinkgoProjectilePayload Payload = EGinkgoProjectilePayload::HighExplosive;

	/** 发射者的武器组件弱引用；角色销毁后自动失效 */
	TWeakObjectPtr<UGinkgoWeaponComponent> FiringWeapons;

	/** 发射瞬间记录的世界空间枪口坐标（cm），用于日志与飞行距离计算 */
	FVector MuzzleOrigin = FVector::ZeroVector;

	/** 当前离散模拟步的世界空间位置（cm），由游戏线程固定步长推进 */
	FVector SimulatedPosition = FVector::ZeroVector;

	/** 当前离散模拟步的世界空间速度向量（cm/s），每步叠加世界重力加速度 */
	FVector SimulatedVelocity = FVector::ZeroVector;

	/** 发射武器的伤害倍率（无量纲，默认 1.0），由 InitializeProjectile 写入 */
	float WeaponDamageMultiplier = 1.f;

	/** 已推进的飞行或爆后时间（秒），引爆前累计飞行时长，引爆后归零改记闪光/火球消散时长 */
	float ElapsedSeconds = 0.f;

	/** 已完成的固定频率（60 Hz）物理扫掠步数，防止帧率波动改变抛物线积分 */
	int32 SimulatedSteps = 0;

	/** 绕弹体 +X 轴累积的膛线自旋角（度），仅用于飞行期的视觉旋转表现 */
	float SpinDegrees = 0.f;

	/** 引爆后置为真，停止后续飞行扫掠并转入爆闪或火球衰减阶段 */
	bool bDetonated = false;

	/** 引爆点世界空间坐标（cm），未引爆时为零向量 */
	FVector ExplosionLocation = FVector::ZeroVector;
};

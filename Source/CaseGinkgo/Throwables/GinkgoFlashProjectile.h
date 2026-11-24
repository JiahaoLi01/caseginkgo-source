#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GinkgoFlashProjectile.generated.h"

class AGinkgoCharacter;
class UPointLightComponent;
class USoundBase;
class UStaticMeshComponent;

/** 游戏中的飞行物。轨迹提示和权威端移动使用同一次球形扫掠算法；爆闪位置复制给客户端。 */
UCLASS()
class CASEGINKGO_API AGinkgoFlashProjectile : public AActor
{
	GENERATED_BODY()

public:
	AGinkgoFlashProjectile();

	/** 发射起点与速度均为世界空间；预览与生成共用。 */
	static void GetLaunch(const AGinkgoCharacter& Character, FVector& Start, FVector& Velocity, const FVector& ReleasedAim = FVector::ZeroVector);

	/** 使用与服务器相同的球形扫掠，预测从脱手到爆闪的世界空间轨迹（cm）。最后一点是预计爆点，包含空中到时的情况。 */
	static void PredictPath(const UWorld& World, const AActor* Thrower, FVector Start, FVector Velocity, TArray<FVector>& OutPoints);

	/** 由碰撞法向速度大小（cm/s）换算碰撞声音量 [0, 1]；低于起振门限的擦碰或停稳微接触返回 0。 */
	static float ComputeImpactVolume(double NormalSpeed);

	/** 碰撞墙面或地面时播放的三维空间音效资产，默认指向随机采样的 MS_Flashbang_Impact。 */
	USoundBase* GetImpactSound() const { return ImpactSound; }

	/** 引信到期爆闪时播放的三维空间音效资产，默认指向能量增强后的 MS_Flashbang_Explode。 */
	USoundBase* GetExplodeSound() const { return ExplodeSound; }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 弹体脱手生成后到爆闪的游戏秒数，落地和反弹均不重置计时。 */
	static constexpr double FuseSeconds = 2.0;

	/** 世界空间速度（cm/s）。脱手时由投掷组件赋初值，之后由权威端飞行模拟更新，停稳后为零。 */
	FVector LaunchVelocity = FVector::ZeroVector;

private:
	/** 弹体外观，爆闪时换成短暂可见的自发光球。 */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Body;

	/** 短暂发光的几何体，预载使第一次触发不阻塞游戏线程。 */
	UPROPERTY()
	TObjectPtr<UStaticMesh> BurstMesh;

	/** 配套的自发光材质，在飞行物类加载时取得。 */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BurstMaterial;

	/** 只照亮场景，随爆闪淡出。 */
	UPROPERTY()
	TObjectPtr<UPointLightComponent> Light;

	/** 弹体碰撞墙面或地面时的三维空间 MetaSound，六条裁剪变体自带近距缓降的风格化衰减。 */
	UPROPERTY()
	TObjectPtr<USoundBase> ImpactSound;

	/** 弹体引信到期爆闪时的三维空间 MetaSound，带瞬态与胸腔低频增强及交战距离风格化衰减。 */
	UPROPERTY()
	TObjectPtr<USoundBase> ExplodeSound;

	/** 服务器已发生爆闪；只触发一次本地表现。 */
	UPROPERTY(ReplicatedUsing = OnRep_Burst)
	bool bBurst = false;

	/** 脱手后经过的游戏秒数，爆闪开始时清零并用于淡出。 */
	double Elapsed = 0.;

	/** 服务器已经完成的飞行模拟步数；剩余不足一步的时间保留在 Elapsed 中。 */
	int32 SimulatedSteps = 0;

	/** 权威端上次派发碰撞声的飞行模拟秒数（相对生成时刻）；负值表示尚未播放过碰撞声。 */
	double LastImpactSoundTime = -1.0;

	/** 服务器检测到弹体在支撑面上停稳后置位，余下时间只推进爆闪倒计时。 */
	bool bSettled = false;

	/** 本地已触发过一次爆闪爆炸声后置位，避免权威端调用与复制回调重复发声。 */
	bool bBurstSoundPlayed = false;

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayImpact(FVector_NetQuantize Location, float Volume);

	UFUNCTION()
	void OnRep_Burst();

	void Burst();
};

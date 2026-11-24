#pragma once

#include "CoreMinimal.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Components/ActorComponent.h"
#include "GinkgoDamageableComponent.generated.h"

class UGinkgoAmmoDefinition;
class UGinkgoDamageableComponent;

/** @struct FGinkgoDamageEvent : 一颗子弹打在一个目标上（Docs/Design/Damage.md「伤害怎么算」） */
struct FGinkgoDamageEvent
{
	const UGinkgoAmmoDefinition* Ammo = nullptr;
	TWeakObjectPtr<AActor> Instigator;
	FVector Point = FVector::ZeroVector;
	/** 子弹的方向（单位向量） */
	FVector Direction = FVector::ForwardVector;
	FName Bone;
	EGinkgoBodyPart Part = EGinkgoBodyPart::UpperChest;
	/** 枪口到命中点（cm） */
	float Distance = 0.f;
	/** 掩体穿射（Docs/Design/Damage.md「穿射」）：这颗子弹打到它之前穿过了几个东西，伤害乘多少（没穿过东西时 0 个、1 倍） */
	int32 Penetrated = 0;
	float PenetrationScale = 1.f;
	/** 乘过枪、穿射、部位、距离的倍率，护甲挡之前 */
	float BaseDamage = 0.f;
	FGinkgoDamageOutcome Outcome;
	/** 打在哪件护甲上（没打在护甲上时为 NAME_None），它还剩多少 */
	FName ArmorPiece;
	float ArmorLeft = 0.f;
	float HealthLeft = 0.f;
	/** 这一下打死的 */
	bool bKilled = false;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FGinkgoOnDamaged, UGinkgoDamageableComponent&, const FGinkgoDamageEvent&);

/**
 * @class UGinkgoDamageableComponent : 可打击的目标（Docs/Design/Damage.md）
 * 有这个组件的 actor 就能被子弹打：生命、护甲与命中部位。敌人默认按装备覆盖部位防护，玩家用全身共享的总护甲池。
 * 两种模式复用同一穿甲与溢出伤害公式；全身模式先合并结算再扣各装备耐久，HUD 不决定防护范围。
 * 玩家角色开启组件复制，由服务器结算生命和护甲。靶子与 AI 沿用自己的本地伤害流程。
 */
UCLASS(ClassGroup = (Ginkgo), meta = (BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoDamageableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoDamageableComponent();

	/**
	 * @brief 一颗子弹打到这个目标：算部位、乘倍率、护甲挡、扣血，广播 OnDamaged（打死时再 OnDied）
	 * @param WeaponMultiplier - 枪的倍率（UGinkgoWeaponDefinition::DamageMultiplier）
	 * @param Hit - 打到的那一下（骨骼名算部位）
	 * @param Direction - 子弹的方向
	 * @param Distance - 枪口到命中点（cm，距离衰减）
	 * @param PenetrationScale - 掩体穿射（Docs/Design/Damage.md「穿射」）：穿过东西以后伤害、护甲伤害乘多少（到它时剩的系数），没穿过东西是 1
	 * @param Penetrated - 打到它之前穿过了几个东西（日志）
	 * @return 打完的结果；已经死了时什么也不扣（BaseDamage 为 0）
	 */
	FGinkgoDamageEvent ApplyBulletHit(const UGinkgoAmmoDefinition& Ammo, float WeaponMultiplier, const FHitResult& Hit, const FVector& Direction, float Distance,
		AActor* Instigator, float PenetrationScale = 1.f, int32 Penetrated = 0);

	/** @brief 打到的骨骼算哪个部位（不是骨骼网格时是 DefaultPart） */
	EGinkgoBodyPart FindBodyPart(const FHitResult& Hit) const;

	/** @brief 血、护甲回满（开始时、靶子复位时） */
	void Revive();

	/** 出生时从角色类型复制生命上限与防具，再回满本实例。运行中的损耗由本组件持有。 */
	void InitializeVitals(float InMaxHealth, const TArray<FGinkgoArmorPiece>& InArmor);

	bool IsDead() const { return bDead; }
	float GetHealth() const { return Health; }
	float GetMaxHealth() const { return MaxHealth; }
	const TArray<FGinkgoArmorPiece>& GetArmor() const { return Armor; }

	/** 是否把全部护甲耐久视为全身共用的防护池；玩家启用，敌人默认按 Covers 分部位保护 */
	bool HasFullBodyArmor() const { return bFullBodyArmor; }

	/** 由角色配置防护模式，不改变已装备护甲或当前耐久；伤害与显示共同读取，不能只在 HUD 合并 */
	void SetFullBodyArmor(bool bEnabled) { if (CanWriteVitals()) { bFullBodyArmor = bEnabled; } }

	/** @brief 装配一组护甲并回满各件耐久；防护范围由组件模式决定，玩家与靶子共用此入口 */
	void SetArmor(const TArray<FGinkgoArmorPiece>& InArmor);

	/** 复活后恢复已存档生命与护甲；保留耐久损耗，不广播伤害/死亡事件。无效生命数据返回 false。 */
	bool RestoreSaveState(float SavedHealth, float SavedMaxHealth, const TArray<FGinkgoArmorPiece>& SavedArmor);

	/** 是否启用脱战自动缓慢回血；玩家角色启用，AI 与练习靶保持关闭 */
	bool IsHealthRegenEnabled() const { return bHealthRegenEnabled; }

	/** 配置脱战自动回血开关并按当前生命状态同步组件 Tick 启停 */
	void SetHealthRegenEnabled(bool bEnabled);

	/** 脱战回血需等待的连续无受击时长（秒，默认 3.0 s） */
	float GetHealthRegenDelay() const { return HealthRegenDelay; }

	/** 从 0 恢复至满血所需的总时长（秒，默认 2.0 s） */
	float GetHealthRegenFullDuration() const { return HealthRegenFullDuration; }

	/** 推进一帧脱战自动回血状态；供 TickComponent 调用及自动化测试直接验证 */
	void StepHealthRegen(float DeltaSeconds);

	/** 每一下打完（打死的那一下也有） */
	FGinkgoOnDamaged OnDamaged;

	/** 打死的那一下 */
	FGinkgoOnDamaged OnDied;

	/** 死亡与复活的状态切换；结算端与收到复制的客户端都会通知，供角色应用布娃娃或恢复动画。 */
	FSimpleMulticastDelegate OnLifeStateChanged;

	//~ Begin UActorComponent
	/** 血、护甲回满，登记成世界里的目标（UGinkgoTargetRegistry，辅助瞄准从那里挑） */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

protected:
	/** 初始生命上限；BeginPlay / Revive 时回满，单位为生命点数 */
	UPROPERTY(EditAnywhere, Replicated, Category = "Health", meta = (ClampMin = 1))
	float MaxHealth = 100.f;

	/** 当前生命点数；玩家从服务器接收，HUD 用它和 MaxHealth 计算生命环。 */
	UPROPERTY(VisibleInstanceOnly, Replicated, Category = "Health")
	float Health = 0.f;

	/** 生命归零后置位，后续子弹不再结算；Revive 清除 */
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing = OnRep_Dead, Category = "Health")
	bool bDead = false;

	/** 是否启用脱战自动缓慢回血；仅玩家控制角色默认开启，AI 与靶子默认关闭 */
	UPROPERTY(EditAnywhere, Category = "Health|Regen")
	bool bHealthRegenEnabled = false;

	/** 受击后开始缓慢回血前需保持不受击的等待时长（秒，非负） */
	UPROPERTY(EditAnywhere, Category = "Health|Regen", meta = (ClampMin = 0, Units = "s"))
	float HealthRegenDelay = 3.f;

	/** 从 0 生命恢复至 MaxHealth 所需的总时长（秒，正数；每秒回血速率为 MaxHealth / 该值） */
	UPROPERTY(EditAnywhere, Category = "Health|Regen", meta = (ClampMin = 0.01, Units = "s"))
	float HealthRegenFullDuration = 2.f;

	/** 全身防护时将有效护甲汇总结算，再按装备顺序扣耐久；默认 false 保持敌人按部位的规则 */
	UPROPERTY(EditAnywhere, Replicated, Category = "Armor")
	bool bFullBodyArmor = false;

	/** 护甲装备及当前耐久；分部位模式按 Covers 选第一件，全身模式忽略 Covers 并共享总耐久 */
	UPROPERTY(EditAnywhere, Replicated, Category = "Armor")
	TArray<FGinkgoArmorPiece> Armor;

	/** 骨骼到部位：打到的骨骼往上找第一根在表里的。默认是人体模型的（Ginkgo::Combat::MakeMannequinBodyParts） */
	UPROPERTY(EditAnywhere, Category = "Body")
	TMap<FName, EGinkgoBodyPart> BoneParts;

	/** 找不到部位时（不是骨骼网格、骨骼不在表里）算哪个 */
	UPROPERTY(EditAnywhere, Category = "Body")
	EGinkgoBodyPart DefaultPart = EGinkgoBodyPart::UpperChest;

private:
	/** 开启复制时只有权威端可改生命状态；非复制目标和独立组件按原有本地规则结算。 */
	bool CanWriteVitals() const;

	/** 按当前写入权限、回血开关与残血存活状态启停组件 Tick，满血或非回血目标零开销 */
	void RefreshRegenTick();

	/** 将生命状态变化交给身体表现。网络回调不重放命中、击杀或读档逻辑。 */
	UFUNCTION()
	void OnRep_Dead();

	/** 距离最近一次受击或残血读档后累计的无受击时间（秒）；满血、受击或复活时清零 */
	float TimeSinceLastDamaged = 0.f;
};

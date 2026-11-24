#pragma once

#include "CoreMinimal.h"
#include "AI/GinkgoAIBehaviorOverrides.h"
#include "Character/GinkgoCharacter.h"
#include "GinkgoAICharacter.generated.h"

class AGinkgoPatrolRoute;
class UGinkgoDialogueComponent;
class UGinkgoAIConfig;
class UGinkgoEnemyArchetype;
class UGinkgoWeaponDefinition;
struct FGinkgoDamageEvent;

/**
 * 可直接放入关卡的敌人，复用角色的武器、动画与伤害管线。控制器只在服务器上运行。
 * 开局装备来自 AI 定义，不生成玩家的测试枪、工作台与靶子；死亡时由权威端掉落携带的武器，再停止行为、取消动作并进入布娃娃。
 */
UCLASS()
class CASEGINKGO_API AGinkgoAICharacter : public AGinkgoCharacter
{
	GENERATED_BODY()

public:
	explicit AGinkgoAICharacter(const FObjectInitializer& ObjectInitializer);
	virtual void PostLoad() override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;
	virtual void ApplyCarriedWeapons() override;
	virtual FVector GetPawnViewLocation() const override;
	virtual float GetTacticalPeek() const override { return TacticalPeek; }
	virtual bool GetTacticalAimPoint(FVector& OutPoint) const override;

	/** 权威端选位组件设置探身请求；动画继续负责平滑、换弹/切枪的淡出和最终枪口解算。 */
	void SetTacticalPeek(float Amount);

	/** 服务器使用的台词订阅者；可在敌人蓝图或关卡实例中指定不同的台词表。 */
	UPROPERTY(VisibleAnywhere, Category="AI")
	TObjectPtr<UGinkgoDialogueComponent> Dialogue;

	/** 控制器在失明与恢复时更新；所有客户端用同一状态驱动左臂遮眼姿势。 */
	UPROPERTY(Replicated, Transient)
	bool bCoverEyes = false;

	/** 行为与装备的共享定义；默认资产由 GinkgoAIAssets 命令生成，可为不同敌人另建资产。 */
	UPROPERTY(EditAnywhere, Category = "AI")
	TSoftObjectPtr<UGinkgoAIConfig> Definition;

	/** 出生时选定的敌人类型，默认普通无甲。服务器应用基础数值，同步给客户端以选择同一套外观。 */
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_EnemyType, Category = "AI", meta = (DisplayName = "敌人类型"))
	TSoftObjectPtr<UGinkgoEnemyArchetype> EnemyType;

	/** 单个出生位指定的起始武器，留空时使用 AI 定义的武器。权威端在开始游戏前赋值。 */
	UPROPERTY(EditInstanceOnly, Category = "AI")
	TSoftObjectPtr<UGinkgoWeaponDefinition> StartingWeaponOverride;

	/** 该角色的状态树与冲刺射击距离；关卡可直接配置，也可由检查点出生位在生成时覆盖。 */
	UPROPERTY(EditAnywhere, Category = "AI")
	FGinkgoAIBehaviorOverrides BehaviorOverrides;

	/** 关卡中的巡逻路线；空值表示原地警戒，失去目标后仍可搜索。 */
	UPROPERTY(EditInstanceOnly, Category = "AI")
	TObjectPtr<AGinkgoPatrolRoute> PatrolRoute;

	/** 首个巡逻点索引，控制器接管时夹到路线范围；战斗结束后保留已走到的进度。 */
	UPROPERTY(EditAnywhere, Category = "AI", meta = (ClampMin = "0"))
	int32 StartingPatrolPoint = 0;

	/** 存档中的敌人 ID；关卡放置时可留空使用含子关卡名的稳定路径。运行时刷新脚本必须指定同一出生位的固定 ID。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "AI|Persistence")
	FName PersistentEnemyId;

protected:
	virtual const UGinkgoCharacterModel* ResolveCharacterModel() const override;

	virtual void InitializeStartingEquipment() override;

private:
	/** 初始属性晚于 BeginPlay 到达时仅刷新外观，避免重新装备护甲。 */
	UFUNCTION()
	void OnRep_EnemyType();

	/** 仅发给模拟代理的手持槽与上半身姿态。服务器在发送前采样，客户端在此快照和携带槽任一到达时对齐。 */
	UPROPERTY(ReplicatedUsing = OnRep_HoldPose)
	FGinkgoReplicatedHoldPose ReplicatedHoldPose;

	/** 重建敌人在此客户端的持枪表现，不推进权威端的武器动作。 */
	UFUNCTION()
	void OnRep_HoldPose();

	/** 当前战术请求的带符号探身量 [-1,1]，由选位组件更新并复制；退出任务、死亡或换决策树归零。 */
	UPROPERTY(Replicated)
	float TacticalPeek = 0.f;

	void HandleDied(UGinkgoDamageableComponent& Component, const FGinkgoDamageEvent& Event);
};

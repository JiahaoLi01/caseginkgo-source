#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "AI/GinkgoAIKnowledge.h"
#include "Perception/AIPerceptionTypes.h"
#include "StateTreeInstanceData.h"
#include "StateTreeExecutionTypes.h"
#include "GinkgoAIController.generated.h"

class AGinkgoAICharacter;
class UGinkgoAICombatComponent;
class UGinkgoAITacticsComponent;
class UGinkgoAIConfig;
class UGinkgoWeaponDefinition;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UStateTree;
struct FStateTreeExecutionContext;
struct FGinkgoAITactics;

/**
 * 敌人的感知、记忆与导航拥有者。StateTree 决定当前任务，Combat 组件把任务意图送入现有武器管线。
 * 感知之后才推进树；隐藏目标只保留身份和冻结位置，不通过 Actor 引用更新压制、搜索或 EQS 的目标点。
 * 生命周期从 OnPossess 开始，死亡、EndPlay 或 UnPossess 都停止树、导航、EQS 和武器输入。
 */
UCLASS()
class CASEGINKGO_API AGinkgoAIController : public AAIController
{
	GENERATED_BODY()

public:
	AGinkgoAIController();
	virtual void Tick(float DeltaSeconds) override;
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;

	const FGinkgoAIKnowledge& GetKnowledge() const { return Knowledge; }
	const UGinkgoAIConfig* GetDefinition() const { return Config; }
	EGinkgoAIBehavior GetActiveBehavior() const { return ActiveBehavior; }
	bool IsAIActive() const { return bActive; }
	bool HasSeenHostile() const { return bHasSeenHostile; }
	int32 GetPatrolIndex() const { return PatrolIndex; }
	const UStateTree* GetBehaviorTree() const { return BehaviorTree; }
	UGinkgoAITacticsComponent* GetTactics() const { return Tactics; }
	AActor* GetKnownTarget() const { return TargetActor.Get(); }

	/** 当前任务的世界空间瞄准点（cm）；丢失视线后只能来自冻结记忆，没有有效任务时返回 false。 */
	bool GetTacticalAimPoint(FVector& OutPoint) const { OutPoint = AimPoint; return bActive && bHasAimPoint; }

	/** 状态树任务入口、逐帧推进与出口；只允许交战/追踪的短暂视线过渡保留墙边选点，换树或停止时彻底清理。 */
	void BeginBehavior(EGinkgoAIBehavior Behavior, const FGinkgoAITactics& Settings);
	EStateTreeRunStatus TickBehavior(EGinkgoAIBehavior Behavior, float DeltaSeconds);
	void EndBehavior();
	void StopAI();

	/** 闪光暂时停止决策并清空感知；重复命中只能延长剩余时间（秒）。仅由服务器调用。 */
	void ApplyFlash(float Duration);

	/** 从当前眼位按配置的视锥和场景遮挡检查世界空间点。Radius 为本次查询的最远距离（cm），0 关闭。
	 * 这是几何判定，即使感知暂时停用也可用于新的爆闪；射线首先命中 Target 本身也算可见。 */
	bool HasUnobstructedViewOf(const FVector& Point, float Radius, const AActor* Target = nullptr) const;

	/** 复用导航节流；Point 为世界 cm，Acceptance < 0 使用巡逻容差，否则用精确半径（cm），不另加胶囊半径。 */
	void MoveToPoint(const FVector& Point, float Acceptance = -1.f);
	bool HasReached(const FVector& Point) const;

private:
	/** Hearing 的成功刺激才产生调查线索；使用 StimulusLocation，不读取声源当前坐标。 */
	UFUNCTION()
	void HandlePerception(AActor* Source, FAIStimulus Stimulus);

	void UpdateSight(float DeltaSeconds);
	bool CanSeePoint(const FVector& Point, const AActor* Target = nullptr) const;
	bool RefreshWeaponTree();
	void StopTree();
	bool TickTree(float DeltaSeconds);
	void DrawDebug() const;
	bool CollectTreeData(const FStateTreeExecutionContext& Context, const UStateTree* Tree,
		TArrayView<const FStateTreeExternalDataDesc> Descs, TArrayView<FStateTreeDataView> Views);

	/** 视力配置，控制器接管时从 Config 设置；由感知组件维护可见候选集。 */
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	/** 听力配置，接管时读取定义；Hearing 只按传播半径过滤，敌我判断由回调复核。 */
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	/** 武器输入适配组件，由本控制器在树与朝向之后显式推进。 */
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UGinkgoAICombatComponent> Combat;

	/** 交战选位与墙边探身，由状态树任务配置；生命周期跟随控制器，不放进武器或共享数据资产。 */
	UPROPERTY(VisibleAnywhere, Category = "AI")
	TObjectPtr<UGinkgoAITacticsComponent> Tactics;

	/** 当前接管角色的只读定义；接管时同步加载，解除控制后清空。 */
	UPROPERTY(Transient)
	TObjectPtr<const UGinkgoAIConfig> Config;

	/** 依次选取出生位覆盖、手持武器、角色定义中的树。首次目击可释放警戒覆盖，仅在有效树改变时重建实例。 */
	UPROPERTY(Transient)
	TObjectPtr<const UStateTree> BehaviorTree;

	/** 上次推进的手持武器，换枪时重置点射/装填适配状态；弱引用不额外延长武器定义生命周期。 */
	TWeakObjectPtr<const UGinkgoWeaponDefinition> BehaviorWeapon;

	/** 每个控制器独立的状态树实例，停止后释放；资产不保存运行时状态。 */
	UPROPERTY(Transient)
	FStateTreeInstanceData TreeData;

	/** 目击目标身份；仅在感知确认可见时读取位置。失效后只丢弃视觉记忆，不延长目标生命周期。 */
	TWeakObjectPtr<AActor> TargetActor;

	/** 冻结的视觉与听觉记忆；由感知、抵达声源、搜索结束与 StopAI 修改。 */
	FGinkgoAIKnowledge Knowledge;

	/** 本次接管期间是否亲眼确认过敌对玩家。声音、丢失目标、遗忘和换枪都不改变此标志，停止 AI 时重置。 */
	bool bHasSeenHostile = false;

	/** 正在执行的树任务，仅用于执行与调试；决策条件读取 Knowledge。 */
	EGinkgoAIBehavior ActiveBehavior = EGinkgoAIBehavior::Patrol;

	/** 已完成接管且树可运行；停止前先清零，使异步回调无法重新发起移动。 */
	bool bActive = false;

	/** 当前屏蔽截止的服务器游戏时间（秒），负数表示感知正常。 */
	double BlindUntil = -1.0;

	/** 本次恢复前发生的声音刺激即便延迟到达也丢弃；初始值允许开局刺激。 */
	double HearingSince = -1.0;

	/** 同时切换感知组件的两个 sense，清除引擎缓存并刷新监听器。 */
	void SetSensesEnabled(bool bEnabled);

	/** 巡逻点索引，接管时初始化；只在完成停留或不可达超时后前进，战斗中保持。 */
	int32 PatrolIndex = 0;

	/** 距离上次感知采样的累计秒数；接管时置为一个采样周期以便立即检测。 */
	float SightElapsed = 0.f;

	/** 当前任务累计秒数，进入任务时归零；用于搜索寿命与停留扫视的相位。 */
	float BehaviorElapsed = 0.f;

	/** 到达路线或搜索点后的累计停留秒数；移动中及进入任务时归零。 */
	float WaitElapsed = 0.f;

	/** 巡逻前进至少 25 cm 时记录的位置（世界 cm），进入任务时取当前角色位置。 */
	FVector ProgressLocation = FVector::ZeroVector;

	/** 巡逻尚未到点但连续没有推进的秒数；12 秒后跳过该点，长而可走通的路线不会因此超时。 */
	float StalledSeconds = 0.f;

	/** 下一次允许发起导航请求的世界时间（秒）；失败也推进，避免持续重试。 */
	double NextMoveTime = 0.0;

	/** 最近提交的导航终点（世界 cm），用于合并同目的地的移动请求。 */
	FVector MoveGoal = FVector::ZeroVector;

	/** MoveGoal 是否有效；退出任务时清除，与世界原点无关。 */
	bool bHasMoveGoal = false;

	/** 最近一次导航请求是否失败；搜索任务据此丢弃不可达的随机点，下一周期重新选择。 */
	bool bMoveFailed = false;

	/** 搜索的随机点或调查声源投影后的导航点（世界 cm）；只在当前任务有效。 */
	FVector SearchGoal = FVector::ZeroVector;

	/** 当前调查或搜索已选定有效导航点；新声音使调查点失效，无点时按导航周期重试。 */
	bool bHasSearchGoal = false;

	/** 当前控制器应看的位置（世界 cm），任务每帧写入；不是目标 Actor 绑定的焦点。 */
	FVector AimPoint = FVector::ZeroVector;

	/** 有任务瞄准点时平滑转向；巡逻或无目标时交给导航路径焦点。 */
	bool bHasAimPoint = false;
};

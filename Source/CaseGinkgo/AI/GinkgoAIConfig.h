#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GinkgoAIConfig.generated.h"

class UEnvQuery;
class UStateTree;
class UGinkgoWeaponDefinition;

/**
 * 敌人的行为参数与装备定义。关卡上的 AI 共享资产，感知记忆、路线进度和查询句柄留在各自控制器上。
 * 距离使用 cm、速度使用 cm/s、时间使用秒；运行时不会修改本资产。
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoAIConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 默认回退决策树，未持枪或武器没有可用的 DefaultAIStateTree 时使用；不是武器机构树。无可用树时停止行为。 */
	UPROPERTY(EditAnywhere, Category = "Behavior")
	TObjectPtr<UStateTree> StateTree;

	/** 开局武器；沿用武器定义的弹种、机构、AnimAction 与后坐力。为空时可以巡逻但不能射击。 */
	UPROPERTY(EditAnywhere, Category = "Equipment")
	TSoftObjectPtr<UGinkgoWeaponDefinition> Weapon;

	/** 开局默认弹种的后备数量；0 表示只使用枪内已有弹药。 */
	UPROPERTY(EditAnywhere, Category = "Equipment", meta = (ClampMin = "0"))
	int32 ReserveRounds = 180;

	/** 视野最远距离（cm）。超出后只能使用最后目击记忆。 */
	UPROPERTY(EditAnywhere, Category = "Sight", meta = (ClampMin = "100"))
	float SightRadius = 3000.f;

	/** 视锥半角（度）；0 朝正前，90 覆盖前方半球。 */
	UPROPERTY(EditAnywhere, Category = "Sight", meta = (ClampMin = "1", ClampMax = "89"))
	float SightHalfAngle = 70.f;

	/** 从感知组件采样并确认视线的周期（秒），避免每帧遍历全部目标。 */
	UPROPERTY(EditAnywhere, Category = "Sight", meta = (ClampMin = "0.03"))
	float SightInterval = 0.12f;

	/** 抵达最后目击位置后，连续看清且无人多久才开始搜索（秒）。离开到达范围或视线被挡会重新计时，驻守者在原地确认。 */
	UPROPERTY(EditAnywhere, Category = "Sight", meta = (ClampMin = "0.1"))
	float ClearPositionDelay = 0.6f;

	/** 丢失视野后的记忆最长寿命（秒）；不可达或始终被遮挡时也会最终放弃。 */
	UPROPERTY(EditAnywhere, Category = "Sight", meta = (ClampMin = "1"))
	float MemoryLifetime = 30.f;

	/** 听众接收噪声的最大半径（cm），与声源自身传播半径取较小值；当前不计算墙体声学衰减。 */
	UPROPERTY(EditAnywhere, Category = "Hearing", meta = (ClampMin = "100", Units = "cm"))
	float HearingRange = 6000.f;

	/** 最后声音线索的寿命（秒），只由新的有效声音刷新；不可达声源也会超时放弃。 */
	UPROPERTY(EditAnywhere, Category = "Hearing", meta = (ClampMin = "1"))
	float HearingMemoryLifetime = 30.f;

	/** 巡逻期望速度（cm/s），换算为模拟输入幅度；不能超过持枪、姿态允许的满幅速度。 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "10"))
	float PatrolSpeed = 150.f;

	/** 交战移动、推进到最后目击位置时的期望速度（cm/s），通过模拟输入驱动现有 locomotion。 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "10"))
	float CombatSpeed = 260.f;

	/** 搜索和声音调查的期望速度（cm/s）；通过降低输入幅度慢走，Walk/Run 仍由共用动画管线判断。 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "10"))
	float SearchSpeed = 90.f;

	/** 路线点、搜索点的到达半径（cm），另加胶囊半径。 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "5"))
	float AcceptanceRadius = 45.f;

	/** 导航请求最短间隔（秒）；无导航、堵路时按此退避，不每帧重建路径。 */
	UPROPERTY(EditAnywhere, Category = "Movement", meta = (ClampMin = "0.2"))
	float MoveRetryInterval = 1.f;

	/** 新发现或重新发现目标后，首次射击的反应时间（秒）。 */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0"))
	float ReactionDelay = 0.5f;

	/** 单次点射持续时间（秒）；武器本身决定射速及是否连发。 */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.05"))
	float BurstDuration = 0.5f;

	/** 两次点射间停顿（秒）。 */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.05"))
	float BurstPause = 0.75f;

	/** 常规武器丢失目标后向最后目击点压制的最长时间（秒），0 表示只举枪推进。确认空置后结束瞄准。 */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0"))
	float SuppressionDuration = 5.f;

	/** 控制器瞄准的最大转速（度/秒），防止发现身后的目标时瞬间转枪。 */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "1"))
	float AimTurnSpeed = 140.f;

	/** 控制器朝向与期望瞄准方向的最大开火偏差（度）；真实弹道仍从武器枪口发出。 */
	UPROPERTY(EditAnywhere, Category = "Combat", meta = (ClampMin = "0.1", ClampMax = "15"))
	float FireAimTolerance = 4.f;

	/** 确认目击点空置或抵达声源后，在其周围搜索的时间（秒），结束后回到原路线进度。 */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "1"))
	float SearchDuration = 15.f;

	/** 搜索点相对最后目击或听觉位置的半径（cm）；必须可以通过导航到达。 */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "50"))
	float SearchRadius = 650.f;

	/** 到达搜索点后停下观察的时间（秒）。 */
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "0"))
	float SearchPause = 1.5f;

	/** 掩体查询：保留低掩体并允许墙边上半身探出，两者无类型优先级；Approach 任务不使用此查询。空值不寻找掩体。 */
	UPROPERTY(EditAnywhere, Category = "Cover")
	TObjectPtr<UEnvQuery> CoverQuery;

	/** 战斗中重新评估掩体的最短间隔（秒），同一时刻最多一个 EQS 请求。 */
	UPROPERTY(EditAnywhere, Category = "Cover", meta = (ClampMin = "0.5"))
	float CoverQueryInterval = 3.f;
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GinkgoAITacticsComponent.generated.h"

class AGinkgoAIController;
struct FEnvQueryResult;

/** 状态树选择的移动与交战方式，视听记忆和武器机构仍由控制器与战斗组件维护。 */
UENUM()
enum class EGinkgoAIPositioning : uint8
{
	Cover,
	Approach,
	SprintToRange
};

/** 敌人 StateTree 的任务参数。资产定义偏好，组件持有选点、查询和距离滞回的运行状态。 */
USTRUCT()
struct CASEGINKGO_API FGinkgoAITactics
{
	GENERATED_BODY()

	/** Cover 选择掩体，Approach 边交战边靠近，SprintToRange 冲向感知位置且只在射程内交战。 */
	UPROPERTY(EditAnywhere, Category = "Tactics")
	EGinkgoAIPositioning Positioning = EGinkgoAIPositioning::Cover;

	/** Approach 的水平停止距离（cm）。冲刺任务进入时把已绑定的根参数换算为 cm 写入运行时副本。 */
	UPROPERTY(EditAnywhere, Category = "Tactics", meta = (ClampMin = "100", Units = "cm", EditCondition = "Positioning == EGinkgoAIPositioning::Approach", EditConditionHides))
	float ApproachDistance = 700.f;

	/** Approach 停步后重新开始推进的额外距离（cm），供边射击边走的任务使用。 */
	UPROPERTY(EditAnywhere, Category = "Tactics", meta = (ClampMin = "25", Units = "cm"))
	float ApproachHysteresis = 150.f;

	/** 墙边探身的可用横移距离（cm）；与现有 28° 上身叠加的头部位移相符，不移动脚下胶囊。 */
	UPROPERTY(EditAnywhere, Category = "Tactics", meta = (ClampMin = "0", ClampMax = "25", Units = "cm"))
	float PeekReach = 18.f;
};

/** 一次掩体几何判定的结果；仅由冻结威胁点和场景碰撞计算，不读取目标 Actor 的实时位置。 */
struct FGinkgoAICoverPose
{
	/** 脚下的世界位置（cm），导航的终点仍是这个位置，探身只作用于上半身。 */
	FVector Ground = FVector::ZeroVector;

	/** -1 左倾、+1 右倾、0 低掩体站姿射击；选点期间保持，不能由探出后的命中反向清零。 */
	float Peek = 0.f;
};

/**
 * 交战选位：主动逼近，或用 EQS 选择低掩体/墙边探身点。两类掩体平等参加距离评分。
 * 控制器在状态树中显式推进；退出战斗、换决策树和死亡时取消异步查询、撤销探身。
 * 导航仍由控制器拥有，射击/装填仍由 Combat 组件驱动，不在此修改武器机构或角色最大速度。
 */
UCLASS()
class CASEGINKGO_API UGinkgoAITacticsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	void Configure(const FGinkgoAITactics& InSettings);
	void TickEngagement();
	/** 向最后目击的世界位置导航；可见且进入射程时停止冲刺并返回 true，交给现有武器流程。 */
	bool TickSprintToRange();
	void Reset();

	/** 丢失视线的短暂过渡可继续在原墙边探出；超过一秒或射界失效就交回常规追踪。 */
	bool HoldCoverDuringSightLoss();

	/** EQS 的几何过滤与最终落点复核共享同一判定，空目标也可用冻结点查询。 */
	bool EvaluateCover(const FVector& Ground, const FVector& Threat, FGinkgoAICoverPose& OutPose) const;

	FVector GetQueryThreat() const { return QueryThreat; }
	EGinkgoAIPositioning GetPositioning() const { return Settings.Positioning; }
	bool HasCover() const { return bHasCover; }
	const FGinkgoAICoverPose& GetCoverPose() const { return CoverPose; }
	int32 GetPendingQuery() const { return QueryId; }

private:
	AGinkgoAIController* GetAI() const;
	void RequestCover();
	void FinishCover(TSharedPtr<FEnvQueryResult> Result);
	void ApplyCover();
	void SetPeek(float Amount);

	/** 本次交战任务的数据副本，进入任务时更新；不修改共享状态树资产。 */
	FGinkgoAITactics Settings;

	/** 主动逼近是否正在进行；进入任务或重置时清零，距离上下阈值负责启停。 */
	bool bApproaching = false;

	/** 当前 CoverPose 是否有效；目标改变使射界失效、退出任务或换武器时清除。 */
	bool bHasCover = false;

	/** 当前已确认的掩体落点和探身方向，只有 bHasCover 为真时有效。 */
	FGinkgoAICoverPose CoverPose;

	/** 正在执行的 EQS 编号，INDEX_NONE 表示空；取消前先失效，防止同步回调重入。 */
	int32 QueryId = INDEX_NONE;

	/** EQS 发起时的威胁点（世界 cm），在同一次查询完成前不改变。 */
	FVector QueryThreat = FVector::ZeroVector;

	/** 下次允许查询的世界秒数，失败也遵守间隔，Reset 后立即允许。 */
	double NextQueryTime = 0.0;
};

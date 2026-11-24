#pragma once

#include "CoreMinimal.h"
#include "GinkgoAIKnowledge.generated.h"

/** 决策树的进入条件读取这些事实；移动、射击和计时由任务驱动。 */
UENUM()
enum class EGinkgoAIBehavior : uint8
{
	Patrol,
	Engage,
	Pursue,
	Search,
	Investigate,
	/** 原地警戒任务。由状态树选入，目击与听觉仍通过正常的感知事实更新。 */
	StandGuard,
};

/**
 * 一名敌人的目击与听觉记忆。Observe 保存亲眼所见，Hear 保存声音发生处；两者互不冒充，也不持有声源 Actor。
 * 控制器在游戏线程更新；状态树、导航及 EQS 使用冻结快照。位置存在性由标志表示，世界原点也是有效线索。
 */
struct CASEGINKGO_API FGinkgoAIKnowledge
{
	/** 最近一次亲眼看到的目标脚下位置（世界 cm），供导航使用；无记忆时无效。 */
	FVector LastLocation = FVector::ZeroVector;

	/** 最近一次看到的躯干瞄准点（世界 cm），供视线确认、压制和掩体查询使用。 */
	FVector LastAimPoint = FVector::ZeroVector;

	/** 最近一次可见的世界时间（秒）；仅 Observe 更新。 */
	double LastSeenTime = 0.0;

	/** 本次连续可见的开始时间（秒），重获视野时更新，持续目击不会重复延迟开火。 */
	double AcquiredTime = 0.0;

	/** 连续确认最后目击点空置的累计时间（秒）；重新遮挡或目击目标即清零。 */
	float ClearElapsed = 0.f;

	/** 存在可使用的目击记忆；遗忘、死亡及解除控制时清零。 */
	bool bHasMemory = false;

	/** 最近一次感知采样确认目标可见；不代表永久持有目标的实时位置。 */
	bool bVisible = false;

	/** 最后目击位置已经被看清且目标不在那里；保持到重新目击或遗忘。 */
	bool bPositionCleared = false;

	/** 最近听到的敌对声音发生位置（世界 cm），可能在脚下或枪口高度；不随声源移动。 */
	FVector LastHeardLocation = FVector::ZeroVector;

	/** 最近一次被接受的听觉事件的世界时间（秒）；只刷新听觉寿命，不延长压制射击。 */
	double LastHeardTime = 0.0;

	/** 是否有听觉线索；重新目击、超时、搜索结束及停止 AI 时清除。 */
	bool bHasHearing = false;

	/** 已抵达声源附近，下一步在其周围搜索；新声音会清除此标志，重新调查。 */
	bool bHearingInvestigated = false;

	void Observe(const FVector& Location, const FVector& AimPoint, double Now);
	void LoseSight();
	void ConfirmEmpty(bool bPointVisible, float DeltaSeconds, float RequiredSeconds);

	/** 只接收比目击、已有听觉更新的线索；目标当前可见时不接受，返回是否更新。 */
	bool Hear(const FVector& Location, double Now);
	void FinishInvestigation() { bHearingInvestigated = bHasHearing; }
	void ForgetSight();
	void ForgetHearing();
	void Forget() { *this = FGinkgoAIKnowledge(); }
	bool IsExpired(double Now, float Lifetime) const;
	bool IsHearingExpired(double Now, float Lifetime) const;

	/** 搜索中心仍取冻结位置；声音高度由导航侧投影到地面，不改写原始感知事实。 */
	FVector GetSearchLocation() const { return bHasHearing ? LastHeardLocation : LastLocation; }
	EGinkgoAIBehavior GetBehavior() const;
};

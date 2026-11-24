#pragma once

#include "GinkgoTraversalTypes.h"
#include "GinkgoTraversalNetworking.generated.h"

/** 攀爬按键的持久输入；随每次移动上报，丢包后由后续计数补上，不能由客户端指定落点。 */
struct CASEGINKGO_API FGinkgoTraversalInput
{
	/** 本地跳跃请求序号，循环递增；服务器和重放按序号只处理一次。 */
	uint16 Sequence = 0;

	/** 按键时相机水平投影的偏航，压为 16 位；本地先量化，保证两端检测方向一致。 */
	uint16 Yaw = 0;

	/** 是否有移动意图，选择短翻越的移动出口；不是客户端声称的速度。 */
	bool bMovingExit = false;

	/** 本地动作前的武器槽，仅供收枪表现；服务器仍验证该槽存在武器，INDEX_NONE 表示徒手。 */
	int8 WeaponSlot = INDEX_NONE;

	/** 固定长度输入序列化，调用方负责包边界与错误检查。 */
	void Serialize(FArchive& Ar);
};

/** CMC 模拟阶段；Recovering 已归还普通移动，只留下动画淡出。 */
enum class EGinkgoTraversalPhase : uint8
{
	Idle,
	Moving,
	Recovering,
};

/**
 * 一次移动结束时的攀爬模拟状态。客户端存档、服务器校正和模拟代理共用；只有服务器能向其他端提供几何锚点。
 * 动画实例、AnimAction 和枪械副作用不进入存档，重放只推进值状态与碰撞位移，最后统一同步表现。
 */
USTRUCT()
struct CASEGINKGO_API FGinkgoTraversalSyncState
{
	GENERATED_BODY()

	/** 已处理的输入序号；拒绝也确认，校正后不反复尝试同一次按键。 */
	uint16 HandledSequence = 0;

	/** 当前/最近动作对应的请求序号，用于区分晚到的旧状态和新的动画实例。 */
	uint16 ActionSequence = 0;

	/** 当前物理阶段；Idle 不携带几何数据，但保留序号以确认拒绝或结束。 */
	EGinkgoTraversalPhase Phase = EGinkgoTraversalPhase::Idle;

	/** 从动作启动起累计的移动模拟时间（秒）；不使用帧末世界时间推进。 */
	float Time = 0.f;

	/** 归还普通移动时的素材时间（秒）；中断可早于素材的支撑事件。 */
	float ReleaseTime = 0.f;

	/** 本次是否正常落稳；false 的结束不补发取枪完成事件。 */
	bool bCompleted = false;

	/** 起步意图决定的翻越出口变体，执行中不再改变。 */
	bool bMovingExit = false;

	/** 动作前持枪槽的快照；表现层同时核对原武器，不能恢复已经丢弃的枪。 */
	int8 WeaponSlot = INDEX_NONE;

	/** 起步脚底的素材到世界变换（cm），包含网格基础旋转和缩放；校正重建计划不借用当前预测位置。 */
	FTransform Origin;

	/** 服务器或本地预测检测的静态支撑与世界锚点；不复制调试路径。 */
	FGinkgoTraversalResult Result;

	/** 前沿、左手、右手、落点的支撑变换快照，监测执行中移动或销毁的支撑。 */
	FTransform Supports[4];

	/** 网络读写；普通状态只写序号与阶段，执行/淡出阶段才写完整锚点。 */
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);

	/** 自定义序列化的值字段不通过反射比较；复制系统必须显式检测进度和锚点的变化。 */
	bool Identical(const FGinkgoTraversalSyncState* Other, uint32 PortFlags) const;

	/** 比对服务器确认与本地存档；锚点和进度均参与，不能只靠位置误差发现阶段分歧。 */
	bool Matches(const FGinkgoTraversalSyncState& Other) const;
};

template<> struct TStructOpsTypeTraits<FGinkgoTraversalSyncState> : TStructOpsTypeTraitsBase2<FGinkgoTraversalSyncState>
{
	enum { WithNetSerializer = true, WithIdentical = true };
};

/** 发给模拟代理的状态附带服务器采样时间，支持晚进入相关范围时从当前动作位置播放。 */
USTRUCT()
struct FGinkgoTraversalReplicatedState
{
	GENERATED_BODY()

	/** 服务器最近完成移动后的攀爬状态；拥有者通过 CMC 校正取得同类状态。 */
	UPROPERTY()
	FGinkgoTraversalSyncState State;

	/** 状态采样的服务器世界时间（秒），仅用于模拟代理表现追赶。 */
	UPROPERTY()
	float ServerTime = 0.f;
};

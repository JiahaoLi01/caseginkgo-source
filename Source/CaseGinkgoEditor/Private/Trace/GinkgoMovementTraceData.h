#pragma once

#include "CoreMinimal.h"
#include "Movement/Windows/GinkgoMovementWindowSegmenter.h"  // 切段规则，与窗口时间轴共用

/** @struct FGinkgoTraceWindow : 快照里的一个活动窗口 */
struct FGinkgoTraceWindow
{
	/** 定义在移动配置窗口列表里的下标 */
	uint8 Definition = 0;

	uint8 Phase = 0;

	float PhaseTime = 0.f;

	/** 当前阶段的时长；不定长的阶段为 0 */
	float PhaseDuration = 0.f;

	float Param = 0.f;

	/** 窗口标签与阶段标签的名字编号（FGinkgoMovementTraceData::GetName） */
	uint32 WindowTag = 0;

	uint32 PhaseTag = 0;
};

/** @struct FGinkgoTraceSnapshot : 一次移动结束时的窗口快照 */
struct FGinkgoTraceSnapshot
{
	/** 录制时间（秒）：世界从录制开始经过的时间 */
	double Time = 0.0;

	/** 引擎帧号：同一个进程里的服务器与客户端世界在同一帧里帧号相同 */
	uint64 Frame = 0;

	/** 客户端重放旧的移动时记的 */
	bool bReplay = false;

	TArray<FGinkgoTraceWindow> Windows;

	/** 活动窗口的倍率贡献涉及的类别，与这些类别上的倍率 */
	TArray<uint32> MultiplierCategories;

	TArray<float> MultiplierValues;

	TArray<uint32> BlockedCategories;

	TArray<uint32> Abilities;
};

/**
 * @struct FGinkgoTraceSegment : 一种窗口停在一个阶段的一段时间，时间轴上的一格
 * 按第一次模拟的快照切（FGinkgoWindowSegmenter）：开始于第一次看到这个阶段的那次移动，结束于第一次看不到它的那次移动。
 * 同一种窗口叠加了几个时只跟第一个。
 */
struct FGinkgoTraceSegment
{
	double StartTime = 0.0;

	/** 还没结束时为 double 的最大值 */
	double EndTime = TNumericLimits<double>::Max();

	uint64 StartFrame = 0;

	uint64 EndFrame = 0;

	uint8 Definition = 0;

	uint8 Phase = 0;

	uint32 WindowTag = 0;

	uint32 PhaseTag = 0;

	float PhaseDuration = 0.f;

	float Param = 0.f;

	/** 同一种窗口同时有几个，取这一段里最多的时候 */
	int32 Count = 1;

	/** 开始与结束有没有对应的窗口变化（发给了表现层的）。客户端上没有的是校正改的：预测错了的窗口 */
	bool bStartedByChange = false;

	bool bEndedByChange = false;

	bool IsOpen() const { return EndTime == TNumericLimits<double>::Max(); }
};

/** @enum EGinkgoTraceMarkerKind : 时间轴上的点 */
enum class EGinkgoTraceMarkerKind : uint8
{
	/** 窗口事件：起跳、落地等 */
	WindowEvent,
	/** 窗口变化：推入、换阶段、结束 */
	WindowChange,
	/** 客户端收到的校正 */
	Correction,
};

/** @struct FGinkgoTraceMarker : 时间轴上的一个点 */
struct FGinkgoTraceMarker
{
	double Time = 0.0;

	uint64 Frame = 0;

	EGinkgoTraceMarkerKind Kind = EGinkgoTraceMarkerKind::WindowEvent;

	/** 发生在客户端重放旧的移动时 */
	bool bReplay = false;

	// ===== 窗口事件 =====

	/** EGinkgoWindowEvent 的值 */
	uint8 EventType = 0;

	float Value = 0.f;

	bool bFlag = false;

	// ===== 窗口变化 =====

	/** 发给了表现层；客户端重放时为假 */
	bool bDispatched = false;

	uint8 Definition = 0;

	uint32 WindowTag = 0;

	/** 原来的阶段；推入时为 -1 */
	int32 FromPhase = INDEX_NONE;

	/** 新的阶段；结束时为 -1 */
	int32 ToPhase = INDEX_NONE;

	uint32 FromPhaseTag = 0;

	uint32 ToPhaseTag = 0;

	// ===== 校正 =====

	/** 位置差（cm）；基于底座的相对位置时为 -1 */
	float PositionError = 0.f;
};

/** @struct FGinkgoTraceObject : 一个移动组件的全部追踪数据，三个数组都按时间排好 */
struct FGinkgoTraceObject
{
	TArray<FGinkgoTraceSnapshot> Snapshots;

	TArray<FGinkgoTraceSegment> Segments;

	TArray<FGinkgoTraceMarker> Markers;

	/** 出现过的窗口定义（下标），按第一次出现的先后：每种一条轨道 */
	TArray<uint8> Definitions;

	/** 每种窗口当前开着的段（Segments 的下标） */
	TMap<uint8, int32> OpenSegments;

	/** 切段规则：开着的段与上一个第一次模拟的快照之后发给了表现层的变化 */
	FGinkgoWindowSegmenter Segmenter;
};

/**
 * @class FGinkgoMovementTraceData : 移动追踪的数据
 * 分析器按事件的先后写入，Rewind Debugger 的轨道读。不依赖分析会话，单元测试直接用；
 * 放进分析会话时由 FGinkgoMovementTraceProvider 包一层，读写都在会话的锁里。
 *
 * 时间轴上的段在写入快照时切好：只看第一次模拟的快照（客户端重放的快照记在别处，不改变段）。
 * 一种窗口在这次快照里换了阶段、阶段已过时间变小（刷新）、或者上次快照之后有发给表现层的变化，旧的段在这次快照的时刻结束、新的段开始；
 * 不在快照里了就结束。规则在运行时模块的 FGinkgoWindowSegmenter 里，游戏里的窗口时间轴用同一份。
 */
class FGinkgoMovementTraceData
{
public:
	void AppendName(uint32 Id, FName Name);

	void AppendSnapshot(uint64 ObjectId, FGinkgoTraceSnapshot&& Snapshot);

	void AppendMarker(uint64 ObjectId, const FGinkgoTraceMarker& Marker);

	bool HasData(uint64 ObjectId) const { return Objects.Contains(ObjectId); }

	const FGinkgoTraceObject* FindObject(uint64 ObjectId) const { return Objects.Find(ObjectId); }

	const TMap<uint64, FGinkgoTraceObject>& GetObjects() const { return Objects; }

	/** 名字编号对应的名字；0 或没见过时为 None */
	FName GetName(uint32 Id) const { return Names.FindRef(Id); }

	/** @brief 某个时刻之前（含）最后一个第一次模拟的快照的下标；没有时为 -1 */
	int32 FindSnapshotIndexAt(const FGinkgoTraceObject& Object, double Time) const;

	/** @brief 与这个快照同一帧、在它之前的重放快照有几个：客户端这一帧收到校正后重放了几次移动 */
	int32 CountReplaysBefore(const FGinkgoTraceObject& Object, int32 SnapshotIndex) const;

	/** @brief 时间在 [Start, End] 里的点的下标范围 [First, Last)；不受跨越边界的段影响 */
	void FindMarkerRange(const FGinkgoTraceObject& Object, double Start, double End, int32& OutFirst, int32& OutLast) const;

private:
	/** 用一个第一次模拟的快照切段 */
	void UpdateSegments(FGinkgoTraceObject& Object, const FGinkgoTraceSnapshot& Snapshot);

	TMap<uint64, FGinkgoTraceObject> Objects;

	TMap<uint32, FName> Names;
};

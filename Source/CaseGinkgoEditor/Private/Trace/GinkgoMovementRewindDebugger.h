#pragma once

#include "CoreMinimal.h"
#include "IRewindDebugger.h"  // FScrubTimeInformation
#include "IRewindDebuggerTrackCreator.h"
#include "RewindDebuggerRuntimeInterface/IRewindDebuggerRuntimeExtension.h"
#include "RewindDebuggerTrack.h"

class FGinkgoMovementTraceData;
struct FGinkgoTraceObject;

/**
 * @struct FGinkgoTraceTimelineData : 一条轨道在时间轴上画的东西
 * 轨道在 Update 里按当前可见的时间范围重建，时间轴控件只管画。
 */
struct FGinkgoTraceTimelineData
{
	/** 一段：窗口停在某个阶段 */
	struct FSpan
	{
		double Start = 0.0;
		double End = 0.0;
		FLinearColor Color = FLinearColor::White;
		/** 画在格子里的字，放不下时不画 */
		FText Label;
		FText ToolTip;
		/** 开始、结束没有对应的窗口变化（客户端上是校正改的）：画一道红线 */
		bool bUnexplainedStart = false;
		bool bUnexplainedEnd = false;
	};

	/** 一个点：窗口事件、窗口变化或校正 */
	struct FPoint
	{
		double Time = 0.0;
		FLinearColor Color = FLinearColor::White;
		FText ToolTip;
	};

	TArray<FSpan> Spans;

	TArray<FPoint> Points;
};

// ===== 轨道与导出图片共用的部分 =====

/** @brief 一种窗口在时间范围里的段：阶段一段一格 */
void BuildGinkgoWindowLane(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, uint8 Definition, double ViewStart, double ViewEnd,
	FGinkgoTraceTimelineData& OutTimeline);

/**
 * @brief 时间范围里的点
 * @param bOverviewOnly - 只要发给表现层的窗口变化与校正（根轨道折叠时的总览）
 */
void BuildGinkgoMovementPoints(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, double ViewStart, double ViewEnd, bool bOverviewOnly,
	FGinkgoTraceTimelineData& OutTimeline);

/** @brief 某个时刻的窗口快照、这一帧重放了几次移动与这一帧的事件，写给详情面板 */
FString DescribeGinkgoMovementAt(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, double Time);

/** @brief 一种窗口的显示名：窗口标签，没有标签时写下标 */
FText GetGinkgoWindowDisplayName(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, uint8 Definition);

/**
 * @brief 画段与点的时间轴控件，悬停时提示
 * @param CursorTime - 画一道竖线的时刻；Rewind Debugger 自己画拖动的位置，轨道不用传
 */
TSharedRef<SWidget> MakeGinkgoTraceTimeline(TSharedPtr<const FGinkgoTraceTimelineData> Timeline, TAttribute<TRange<double>> ViewRange, TOptional<double> CursorTime = {});

/**
 * @class FGinkgoMovementTrackBase : 移动组件下各条轨道的共同部分
 * 每条轨道有自己的时间轴数据与详情文字（与控件共享，轨道先没了控件也不会悬空），选中时详情面板显示拖到的时刻的窗口快照。
 * 选中时单步前进、后退跳到下一个、上一个有变化的时刻（由子类给出）。
 */
class FGinkgoMovementTrackBase : public RewindDebugger::FRewindDebuggerTrack
{
public:
	explicit FGinkgoMovementTrackBase(uint64 InObjectId);

protected:
	//~ Begin RewindDebugger::FRewindDebuggerTrack
	virtual bool UpdateInternal() override;
	virtual TSharedPtr<SWidget> GetTimelineViewInternal() override;
	virtual TSharedPtr<SWidget> GetDetailsViewInternal() override;
	virtual uint64 GetObjectIdInternal() const override { return ObjectId; }
	virtual TOptional<double> GetStepFrameTimeInternal(RewindDebugger::EStepMode StepMode, const FScrubTimeInformation& ScrubTime) const override;
	//~ End RewindDebugger::FRewindDebuggerTrack

	/** 按可见的时间范围重建时间轴数据 */
	virtual void BuildTimeline(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, double ViewStart, double ViewEnd, FGinkgoTraceTimelineData& OutTimeline) const = 0;

	/** 单步时可以停的时刻 */
	virtual void CollectStepTimes(const FGinkgoTraceObject& Object, TArray<double>& OutTimes) const = 0;

	/** 子轨道：只有根轨道有 */
	virtual void UpdateChildren(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object) {}

	uint64 ObjectId = 0;

	TSharedRef<FGinkgoTraceTimelineData> Timeline;

	TSharedRef<FText> DetailsText;
};

/** @class FGinkgoWindowLaneTrack : 一种窗口，阶段一段一格 */
class FGinkgoWindowLaneTrack : public FGinkgoMovementTrackBase
{
public:
	FGinkgoWindowLaneTrack(uint64 InObjectId, uint8 InDefinition, const FText& InDisplayName);

	uint8 GetDefinition() const { return Definition; }

private:
	virtual FSlateIcon GetIconInternal() override;
	virtual FName GetNameInternal() const override;
	virtual FText GetDisplayNameInternal() const override { return DisplayName; }
	virtual void BuildTimeline(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, double ViewStart, double ViewEnd, FGinkgoTraceTimelineData& OutTimeline) const override;
	virtual void CollectStepTimes(const FGinkgoTraceObject& Object, TArray<double>& OutTimes) const override;

	uint8 Definition = 0;

	FText DisplayName;
};

/** @class FGinkgoMovementEventsTrack : 窗口事件、窗口变化（重放里重算的灰色）、校正（红色） */
class FGinkgoMovementEventsTrack : public FGinkgoMovementTrackBase
{
public:
	explicit FGinkgoMovementEventsTrack(uint64 InObjectId);

private:
	virtual FSlateIcon GetIconInternal() override;
	virtual FName GetNameInternal() const override;
	virtual FText GetDisplayNameInternal() const override;
	virtual void BuildTimeline(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, double ViewStart, double ViewEnd, FGinkgoTraceTimelineData& OutTimeline) const override;
	virtual void CollectStepTimes(const FGinkgoTraceObject& Object, TArray<double>& OutTimes) const override;
};

/**
 * @class FGinkgoMovementTrack : 移动组件下的根轨道"Ginkgo 移动"
 * 折叠时一条总览：发给表现层的窗口变化与校正。展开后每种出现过的窗口一条，最后是事件。
 */
class FGinkgoMovementTrack : public FGinkgoMovementTrackBase
{
public:
	explicit FGinkgoMovementTrack(uint64 InObjectId);

private:
	/** 自己的时间轴与详情之外，还要更新子轨道；有新的子轨道时返回真 */
	virtual bool UpdateInternal() override;
	virtual FSlateIcon GetIconInternal() override;
	virtual FName GetNameInternal() const override;
	virtual FText GetDisplayNameInternal() const override;
	virtual TConstArrayView<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> GetChildrenInternal(TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>>& OutTracks) const override;
	virtual void BuildTimeline(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object, double ViewStart, double ViewEnd, FGinkgoTraceTimelineData& OutTimeline) const override;
	virtual void CollectStepTimes(const FGinkgoTraceObject& Object, TArray<double>& OutTimes) const override;
	virtual void UpdateChildren(const FGinkgoMovementTraceData& Data, const FGinkgoTraceObject& Object) override;

	TArray<TSharedPtr<RewindDebugger::FRewindDebuggerTrack>> Children;

	/** 已经建了轨道的窗口定义，与子轨道的先后一致 */
	TArray<uint8> LaneDefinitions;

	bool bChildrenChanged = false;
};

/** @class FGinkgoMovementTrackCreator : 给有移动追踪数据的 UGinkgoCharacterMovementComponent 建根轨道 */
class FGinkgoMovementTrackCreator : public RewindDebugger::IRewindDebuggerTrackCreator
{
private:
	virtual FName GetTargetTypeNameInternal() const override;
	virtual FName GetNameInternal() const override;
	virtual void GetTrackTypesInternal(TArray<RewindDebugger::FRewindDebuggerTrackType>& Types) const override;
	virtual TSharedPtr<RewindDebugger::FRewindDebuggerTrack> CreateTrackInternal(const RewindDebugger::FObjectId& InObjectId) const override;
	virtual bool HasDebugInfoInternal(const RewindDebugger::FObjectId& InObjectId) const override;
};

/** @class FGinkgoMovementRewindDebuggerRuntimeExtension : Rewind Debugger 开始录制时打开移动的追踪通道，停止时关上 */
class FGinkgoMovementRewindDebuggerRuntimeExtension : public IRewindDebuggerRuntimeExtension
{
public:
	virtual void RecordingStarted() override;
	virtual void RecordingStopped() override;
};

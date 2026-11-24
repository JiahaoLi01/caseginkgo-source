#pragma once

#include "CoreMinimal.h"

/**
 * @class FGinkgoWindowSegmenter : 窗口历史的切段规则
 * 时间轴上一种窗口一条轨道，一个阶段一段。编辑器里 Rewind Debugger 的轨道（按录下的追踪，CaseGinkgoEditor 的 FGinkgoMovementTraceData）
 * 与游戏里的窗口时间轴（ginkgo.showWindowTimeline，FGinkgoWindowTimelineModel）共用这一条规则，两处切出来的段一样。
 *
 * 切法：每次移动结束时（第一次模拟）给一份活动窗口的快照。一种窗口换了阶段、阶段已过时间变小（刷新），或者上一份快照之后有变化发给了表现层
 * （NotifyChanged），旧的段在这份快照上结束、新的段开始；不在快照里了就结束。同一种窗口叠加了几个时只跟第一个，另记个数。
 * 段的开始与结束有没有对应的变化也记下：客户端上没有的，是校正放回服务器的状态改的，即预测错了的窗口。
 *
 * 只管切，不管时刻：调用方按自己的时间轴（追踪的录制时间、页面的时钟）记开始与结束。
 * 开着的段至多与活动窗口一样多（FGinkgoMovementSimState::MaxWindows），用小数组而不是映射表。
 */
class CASEGINKGO_API FGinkgoWindowSegmenter
{
public:
	/** 快照里的一个窗口 */
	struct FWindow
	{
		/** 定义在移动配置窗口列表里的下标 */
		uint8 Definition = 0;

		uint8 Phase = 0;

		float PhaseTime = 0.f;
	};

	/** 在一份快照上结束的段 */
	struct FEnd
	{
		uint8 Definition = 0;

		/** 这一段的阶段 */
		uint8 Phase = 0;

		/** 这一段最后一次看到的阶段已过时间，即上一份快照里的 */
		float LastPhaseTime = 0.f;

		/** 有对应的变化（发给了表现层的） */
		bool bByChange = false;
	};

	/** 快照里的一种窗口：开了新的段，或者接着原来的段 */
	struct FPresence
	{
		uint8 Definition = 0;

		/** 这种窗口在快照里第一个的下标：段跟着它 */
		int32 Index = INDEX_NONE;

		/** 同时有几个 */
		int32 Count = 0;

		/** 这份快照上开了新的段 */
		bool bStarted = false;

		/** 新的段有对应的变化 */
		bool bByChange = false;
	};

	using FEnds = TArray<FEnd, TInlineAllocator<8>>;
	using FPresences = TArray<FPresence, TInlineAllocator<8>>;

	/**
	 * @brief 上一份快照之后，这种窗口有变化发给了表现层：它的段在下一份快照上重新开始
	 * @note 刷新时阶段不变、计时也可能没倒退，只看快照分不出来，所以要变化来分
	 */
	void NotifyChanged(uint8 Definition);

	/**
	 * @brief 用一份快照切段：先结束旧的段，再开新的段
	 * @param Windows - 这次移动结束时的活动窗口，按推入的先后
	 * @param OutEnds - 这份快照上结束的段，按开始的先后
	 * @param OutPresent - 快照里的每种窗口，按在快照里第一次出现的先后；同一种窗口换了阶段时，它在 OutEnds 里结束、在这里重新开始
	 */
	void Update(TConstArrayView<FWindow> Windows, FEnds& OutEnds, FPresences& OutPresent);

	int32 NumOpenSegments() const { return Open.Num(); }

	/** 清空开着的段与记下的变化：时间轴重新开始 */
	void Reset();

private:
	/** 一个开着的段 */
	struct FOpen
	{
		uint8 Definition = 0;

		uint8 Phase = 0;

		/** 最后一次看到的阶段已过时间：变小就是从头开始（刷新） */
		float LastPhaseTime = 0.f;
	};

	/** 开着的段，一种窗口至多一个，按开始的先后 */
	TArray<FOpen, TInlineAllocator<8>> Open;

	/** 上一份快照之后有变化发给了表现层的定义 */
	TArray<uint8, TInlineAllocator<8>> Changed;
};

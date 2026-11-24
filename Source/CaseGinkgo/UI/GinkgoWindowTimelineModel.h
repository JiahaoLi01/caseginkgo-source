#pragma once

#include "CoreMinimal.h"
#include "Movement/GinkgoMovementSimState.h"                   // FGinkgoMovementWindowState
#include "Movement/Windows/GinkgoMovementWindow.h"             // FGinkgoWindowEvent
#include "Movement/Windows/GinkgoMovementWindowManager.h"      // FChange、FDefinitions
#include "Movement/Windows/GinkgoMovementWindowSegmenter.h"    // 切段规则，与 Rewind Debugger 共用

/**
 * @class FGinkgoWindowTimelineModel : 窗口时间轴的数据
 * 把一个移动组件每帧的窗口状态、窗口事件与校正，换成时间轴上的段、事件与校正线，时刻在页面的时钟上（秒）。
 * 不碰 weft 与引擎对象（只读窗口定义里的阶段时长），单元测试直接喂数据；FGinkgoWindowTimeline 把每帧的结果写成 JSON 交给页面。
 * 切段用 FGinkgoWindowSegmenter，与 Rewind Debugger 的轨道同一条规则。
 *
 * 时刻：每帧在所有 Actor 更新之后取一次样（Now），上一次取样的时刻（FrameStart）就是这一帧的移动开始的时刻。
 * 1. 事件发生在这一帧的移动里，记在 FrameStart。它推入的窗口在这次移动结束时的已过时间是整个 Δt（管理器在移动结束时按 Δt 推进，
 *    这次移动里推入的窗口也算上这一整段），所以段也从 FrameStart 开始：事件与它推入的段在同一点上，连线落在段的开头
 * 2. 按时长换阶段：多出来的时间带进了新阶段，新的段从 Now − 已过时间开始，旧的段在同一点结束
 * 3. 按时长结束：FrameStart + (阶段时长 − 上一帧的已过时间)
 * 4. 由事件结束、由校正改的：FrameStart
 * 都限制在 [FrameStart, Now] 里；只有第一帧不限制开始，开时间轴之前就在的窗口从它真正开始的时刻画起。
 * 段上标的时长取模拟自己的时间（阶段时长、已过时间），不受取样时刻的影响。
 *
 * 颜色：每个引起了变化的事件开一条因果链（页面按链的序号轮流取三种颜色），被它推入、刷新、换阶段的段属于这条链；
 * 按时长换的阶段接着用原来那一段的链；没有引起变化的事件、开时间轴之前就在的段与校正加上的段不属于任何链。
 */
class CASEGINKGO_API FGinkgoWindowTimelineModel
{
public:
	/** 段开始 */
	struct FSegmentStart
	{
		/** 段的编号：结束时用它找回这一段 */
		int32 Id = 0;

		/** 定义在窗口列表里的下标，即第几条窗口轨道 */
		uint8 Definition = 0;

		uint8 Phase = 0;

		/** 开始的时刻（页面的时钟，秒） */
		double Time = 0.0;

		/** 阶段的时长（秒）；不定长的阶段为 0 */
		float Duration = 0.f;

		/** 同一种窗口同时有几个 */
		int32 Count = 1;

		/** 因果链的序号；不属于任何链时为 -1 */
		int32 Chain = INDEX_NONE;

		/** 有对应的窗口变化。客户端上没有的，是校正放回服务器的状态改的；开时间轴之前就在的也算有 */
		bool bExplained = true;
	};

	/** 段结束 */
	struct FSegmentEnd
	{
		int32 Id = 0;

		double Time = 0.0;

		/** 这一段在模拟里经过的时间（秒）：按时长结束的是阶段时长，其余是结束前最后一次看到的已过时间 */
		float Elapsed = 0.f;

		bool bExplained = true;
	};

	/** @enum ELinkKind : 事件对一种窗口做了什么 */
	enum class ELinkKind : uint8
	{
		/** 推入，或者结束后又推入（从头开始） */
		Push,
		/** 换阶段，或者再次推入时刷新 */
		Phase,
		/** 结束 */
		End,
	};

	/** 事件到它改变的一种窗口的连线 */
	struct FLink
	{
		uint8 Definition = 0;

		ELinkKind Kind = ELinkKind::Push;
	};

	/** 一次窗口事件 */
	struct FEvent
	{
		FGinkgoWindowEvent Event;

		/** 因果链；没有引起变化时为 -1 */
		int32 Chain = INDEX_NONE;

		/** 它改变的窗口，一种窗口一条，按第一次改变的先后 */
		TArray<FLink, TInlineAllocator<4>> Links;
	};

	/** 一帧的结果 */
	struct FFrame
	{
		/** 这一帧的事件与校正所在的时刻：这一帧的移动开始时 */
		double EventTime = 0.0;

		TArray<FSegmentEnd> Ends;

		TArray<FSegmentStart> Starts;

		/** 这一帧的窗口事件，按发生的先后 */
		TArray<FEvent> Events;

		/** 这一帧收到的校正的位置差（cm）；基于底座的相对位置时为 -1 */
		TArray<float> Corrections;

		bool IsEmpty() const { return Ends.IsEmpty() && Starts.IsEmpty() && Events.IsEmpty() && Corrections.IsEmpty(); }
	};

	// ===== 一帧之内：移动组件的表现层事件，按发生的先后 =====

	/** OnMovementWindowChanged：一次发给了表现层的窗口变化（包括按时长推进的） */
	void AddChange(int32 Definition);

	/** OnMovementWindowEvent：一次窗口事件与它引起的变化。没有引起变化的方向键按下不记：跑动时按个不停，只有用掉 lurch 的那一下有意义 */
	void AddEvent(const FGinkgoWindowEvent& Event, TConstArrayView<FGinkgoMovementWindowManager::FChange> Changes);

	/** OnMovementCorrected：客户端收到的一次校正 */
	void AddCorrection(float PositionError);

	// ===== 取样 =====

	/**
	 * @brief 一帧的取样，在所有 Actor 更新之后
	 * @param Now - 这一刻（页面的时钟，秒）
	 * @param FrameStart - 这一帧的移动开始的时刻：上一次取样的时刻；第一帧取 Now − Δt
	 * @param Windows - 这一刻的活动窗口
	 * @param Definitions - 窗口定义：取阶段的时长
	 * @param Out - 这一帧的结果，先清空
	 */
	void EndFrame(double Now, double FrameStart, TConstArrayView<FGinkgoMovementWindowState> Windows, FGinkgoMovementWindowManager::FDefinitions Definitions,
		FFrame& Out);

	/** 开着的段有几个 */
	int32 NumOpenSegments() const { return Open.Num(); }

	/** 从头开始：换了角色或窗口定义。下一帧当作第一帧 */
	void Reset();

private:
	/** 一个开着的段 */
	struct FOpen
	{
		uint8 Definition = 0;

		int32 Id = 0;

		int32 Chain = INDEX_NONE;

		/** 阶段的时长；不定长为 0 */
		float Duration = 0.f;
	};

	/** 这一帧里一种窗口被事件改了：属于哪条链 */
	struct FEventChange
	{
		uint8 Definition = 0;

		int32 Chain = INDEX_NONE;
	};

	FGinkgoWindowSegmenter Segmenter;

	/** 开着的段，一种窗口至多一个 */
	TArray<FOpen, TInlineAllocator<8>> Open;

	// ===== 这一帧里攒下的 =====

	/** 发给了表现层的变化涉及的定义 */
	TArray<uint8, TInlineAllocator<8>> Changed;

	/** 被事件改了的定义与改它的最后一个事件的链 */
	TArray<FEventChange, TInlineAllocator<8>> EventChanges;

	TArray<FEvent> Events;

	TArray<float> Corrections;

	int32 NextSegmentId = 1;

	int32 NextChain = 0;

	bool bFirstFrame = true;
};

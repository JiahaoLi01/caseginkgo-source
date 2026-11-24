#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"           // FTSTicker
#include "Engine/EngineBaseTypes.h"      // ELevelTick
#include "GinkgoWindowTimelineModel.h"
#include "InputCoreTypes.h"              // FKey
#include "UObject/StrongObjectPtr.h"     // 持有 weft 的 View：weft 的子系统只记弱引用

class APlayerController;
class SWeftViewport;
class UGameViewportClient;
class UGinkgoCharacterMovementComponent;
class UWeftView;
class UWorld;

/**
 * @class FGinkgoWindowTimeline : 窗口时间轴
 * 本地玩家的移动窗口画在屏幕下方的一条时间轴上（weft 页面 Content/UI/WindowTimeline）：最上面一条是窗口事件，下面每种窗口一条轨道、
 * 一个阶段一段；事件与它推入、结束、换阶段的窗口之间画连线，一条因果链一种颜色。样式照 weft 宣传页里的线程图。
 *
 * 为什么要它：Rewind Debugger 的轨道只在编辑器里看得到，面试看的是打包的游戏（开发版或发行版）。两处切段的规则相同（FGinkgoWindowSegmenter），
 * 切出来的段一样；时刻与颜色见 FGinkgoWindowTimelineModel。
 *
 * 不是调试工具：发行版里也有（发行版没有控制台）。属于一个本地玩家控制器，第一次打开时才创建；关着时不存在，移动组件的表现层事件没有人订阅。
 * 打开与关闭：控制台 ginkgo.showWindowTimeline [0|1]、F6（GetToggleKey），或命令行 -GinkgoWindowTimeline（启动就打开）。
 *
 * 每帧所有 Actor 更新之后（FWorldDelegates::OnWorldPostActorTick）取一次样：控制器现在控制的角色的窗口状态，加上这一帧收到的窗口变化、
 * 窗口事件与校正；有内容时写成一条 JSON 事件（"timeline"）交给页面。时刻都在页面的时钟上，页面自己按时钟每帧滚动，不用每帧发数据。
 * 世界暂停时（包括 PIE 的暂停）页面的时钟照走，这里记下暂停了多久，交给页面的时刻减去它，页面也停在暂停的那一刻。
 * weft 只在 Win64 的 D3D12 下画；不画的时候（-nullrhi、别的 RHI）打一条警告，什么也不做。
 */
class CASEGINKGO_API FGinkgoWindowTimeline
{
public:
	explicit FGinkgoWindowTimeline(APlayerController& InController);
	~FGinkgoWindowTimeline();

	FGinkgoWindowTimeline(const FGinkgoWindowTimeline&) = delete;
	FGinkgoWindowTimeline& operator=(const FGinkgoWindowTimeline&) = delete;

	/** 打开与关闭它的键 */
	static FKey GetToggleKey();

	/** 命令行里有 -GinkgoWindowTimeline：本地控制器开始时就打开 */
	static bool IsRequestedOnCommandLine();

	/**
	 * @brief 打开：建 weft 的 View 与视口上的控件，页面在视口有了大小之后载入
	 * @return 是否打开了；weft 不画时为假
	 */
	bool Show();

	/** 关闭：拿掉控件、放掉 View，不再订阅移动组件 */
	void Hide();

	bool IsShown() const { return View.IsValid(); }

	void Toggle();

	/**
	 * @brief 面板的顶离视口下沿多高（逻辑 px）；没打开时为 0。别的界面在时间轴打开时把底下的东西挪到它上面
	 *        （UI 美术："窗口时间轴打开时，底排整体上移到时间轴之上"；武器的界面这样挪武器栏）
	 * @note 按页面的排法算：标题行 22 + 4、事件轨 72、每种窗口一条 64 的轨道（没有窗口时也留一条）、横轴 18，面板离下沿 24。
	 *       常量同 page.js、page.css（window_timeline.py 也照着），改页面的排法时一起改
	 */
	float GetPanelTop() const;

private:
	/** 每帧所有 Actor 更新之后：载入页面、跟上控制的角色、取样、交给页面 */
	void OnWorldPostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);

	/** 每帧（世界暂停时也走）：暂停与继续 */
	bool OnTicker(float DeltaTime);

	/** 换成这个移动组件：订阅它的表现层事件，时间轴从头开始；空着时只解除旧的 */
	void BindMovement(UGinkgoCharacterMovementComponent* InMovement);

	/** 时间轴的时钟：页面的时钟减去暂停了的时间，暂停时停在暂停的那一刻 */
	double GetTimelineTime() const;

	/** 页面已经载入时把攒下的消息作为一条事件交给页面 */
	void FlushMessages();

	// ===== 交给页面的消息（JSON 对象） =====

	/** 键名、命令名与显示的时长 */
	void QueueConfig();

	/** 清空，换成这些窗口定义的轨道：每种窗口的名字、标签、推入与结束它的事件、各阶段 */
	void QueueLanes(FGinkgoMovementWindowManager::FDefinitions Definitions);

	void QueueClock();

	void QueueFrame(const FGinkgoWindowTimelineModel::FFrame& Frame);

	TWeakObjectPtr<APlayerController> Controller;

	/** 现在订阅的移动组件：控制器现在控制的角色的 */
	TWeakObjectPtr<UGinkgoCharacterMovementComponent> Movement;

	/** 发给页面的窗口定义：移动组件换了配置、或配置里增删了窗口时重新发轨道 */
	TArray<TWeakObjectPtr<const UObject>> SentDefinitions;

	/** 已经给页面发过这个角色的轨道：换了角色时重新发 */
	bool bLanesSent = false;

	TStrongObjectPtr<UWeftView> View;

	TSharedPtr<SWeftViewport> Widget;

	TWeakObjectPtr<UGameViewportClient> ViewportClient;

	FGinkgoWindowTimelineModel Model;

	/** 攒下的消息，每帧至多交一次 */
	TArray<FString> PendingMessages;

	bool bLoaded = false;

	/** 上一次取样的时刻（时间轴的时钟）；还没取过时为负 */
	double LastSampleTime = -1.0;

	bool bPaused = false;

	/** 这次暂停开始时页面的时钟 */
	double PausedAtPageTime = 0.0;

	/** 已经结束的暂停一共多久（秒） */
	double PausedTotal = 0.0;

	FDelegateHandle PostActorTickHandle;

	FTSTicker::FDelegateHandle TickerHandle;

	FDelegateHandle WindowChangedHandle;

	FDelegateHandle WindowEventHandle;

	FDelegateHandle CorrectedHandle;
};

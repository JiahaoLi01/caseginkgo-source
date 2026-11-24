#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"      // ELevelTick
#include "UObject/StrongObjectPtr.h"     // 持有 weft 的 View：weft 的子系统只记弱引用
#include "WeftTypes.h"                   // FWeftAnchor

class AGinkgoPlayerController;
class SWeftViewport;
class UGameViewportClient;
class UWeftView;
class UWorld;

/**
 * @class FGinkgoInteractionPrompt : 交互的提示
 * 本地玩家的角色附近能交互的目标，画在 weft 页面 Content/UI/Interaction 上，见 Docs/Design/Interaction.md「提示的界面」：
 * 提示距离以内、看得见的是空心圈；焦点可以交互时圈里写键、旁边一块墨牌写"拾取 98k"；不能交互时写原因；等服务器回复时键换成转动的弧。
 * 圈里的键是最近一次输入的设备上交互绑的键（AGinkgoPlayerController::GetActiveDevice，改过键的也跟着）：手柄的面键把徽章涂成那个键的颜色。
 *
 * 不是调试工具，发行版里也有。属于一个本地玩家控制器，开始时创建；weft 不画时（-nullrhi、别的 RHI）什么也不做。
 *
 * 每帧所有 Actor 更新之后（FWorldDelegates::OnWorldPostActorTick，相机已经更新过）读交互组件这一帧的候选：
 * - 位置每帧走锚点（UWeftView::SetAnchors）：目标的位置投影到屏幕，用的是这一帧的相机，转视角时圈贴着东西不拖后；
 * - 状态与文字变了才发一条事件（"interaction"），不每帧推 JSON。
 * - 出现与消失有动画（页面的 CSS）。目标不见了（拾起、走远、被挡住）以后还留 ExitSeconds，另作一项残影、标成"在消失"：
 *   页面放消失的动画，锚点照旧按它最后的世界位置投影（目标已经被销毁也一样），圈在消失时仍贴着那个地方。
 */
class CASEGINKGO_API FGinkgoInteractionPrompt
{
public:
	explicit FGinkgoInteractionPrompt(AGinkgoPlayerController& InController);
	~FGinkgoInteractionPrompt();

	FGinkgoInteractionPrompt(const FGinkgoInteractionPrompt&) = delete;
	FGinkgoInteractionPrompt& operator=(const FGinkgoInteractionPrompt&) = delete;

	/**
	 * @brief 建 weft 的 View 与视口上的控件，页面在视口有了大小之后载入
	 * @return 是否建了；weft 不画、没有游戏视口时为假
	 */
	bool Show();

	/** 拿掉控件、放掉 View */
	void Hide();

	bool IsShown() const { return View.IsValid(); }

	/** 本地工作台导航或安装指令交给页面当前选项；没有页面时返回假，由控制器沿用单配件快捷键 */
	bool NavigateWorkbench(FName Command);

private:
	/** 集成回归经实际 Slate 命中路径点击面板，覆盖跨帧与地点标注前后的输入。 */
	friend class FGinkgoHudLifecycleScenario;

	/** 一个显示着（或正在消失）的目标 */
	struct FShownItem
	{
		/** 目标的编号，也是锚点的 key */
		int64 Key = 0;

		/** 世界里的位置：目标被销毁以后，消失的动画还贴着这里 */
		FVector Location = FVector::ZeroVector;

		/** 当前交互状态的枚举名，交给页面选择提示样式。 */
		FString State;

		/** 目标给出的交互文案，离开候选后保留到消失动画结束。 */
		FString Text;

		/** 当前不可交互的原因，空值表示没有额外说明。 */
		FString Reason;

		/** 不见了的时刻（挂钟，秒，页面的动画也按挂钟走）；还显示着时为负 */
		double LeftAt = -1.0;
	};

	/** 每帧所有 Actor 更新之后：载入页面、读候选、写锚点，状态变了时发事件 */
	void OnWorldPostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);

	/** 所属本地控制器，提供普通 HUD 容器和工作台输入状态；不延长其生命周期。 */
	TWeakObjectPtr<AGinkgoPlayerController> Controller;

	/** 显示期间持有页面，Hide 释放；Weft 子系统只记录弱引用。 */
	TStrongObjectPtr<UWeftView> View;

	/** 页面的 Slate 控件；工作台输入管理局部命中状态，整体抑制由父 HUD 容器负责。 */
	TSharedPtr<SWeftViewport> Widget;

	/** 提供像素尺寸的视口弱引用；挂载与解除挂载统一经过控制器的 HUD 层。 */
	TWeakObjectPtr<UGameViewportClient> ViewportClient;

	/** Show 后订阅世界更新，Hide 在释放页面前注销。 */
	FDelegateHandle PostActorTickHandle;

	/** 进桌时立即把 Slate/Weft 焦点交给页面并显示鼠标，首击直接生效；离桌恢复原光标，设置页已接管时由它恢复。 */
	void SetWorkbenchInput(bool bOpen);

	/** 选枪、装配件、离桌请求交给交互组件；卡片携带绘制时的槽号，避免切枪后迟到的点击 */
	void OnPageMessage(const FString& Name, const FString& Json, uint64 CallId);

	/** 当前 View 的页面消息订阅；Show 创建，Hide 在释放 View 前移除。 */
	FDelegateHandle MessageHandle;

	/** 工作台是否已接管指针；仅进桌/离桌或临时抑制改变时更新，局部可命中状态由此独占。 */
	bool bWorkbenchInput = false;

	/** 进入工作台输入前的光标状态，离开输入模式时恢复；设置页已接管时交给设置页恢复。 */
	bool bPreviousCursor = false;

	/** 页面是否已按有效视口尺寸加载；Hide 重置。 */
	bool bLoaded = false;

	/** 上一次发给页面的事件：一样的不再发 */
	FString LastMessage;

	/** 上一帧显示着的目标，加上还在放消失动画的 */
	TArray<FShownItem> Items;

	/** 这一帧的锚点（复用，不每帧分配） */
	TArray<FWeftAnchor> Anchors;
};

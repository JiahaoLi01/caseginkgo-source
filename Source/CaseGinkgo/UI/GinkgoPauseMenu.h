#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "UObject/StrongObjectPtr.h"

class AGinkgoPlayerController;
class SBackgroundBlur;
class SOverlay;
class SWeftViewport;
class UGameViewportClient;
class UWeftView;
class UWorld;

/**
 * @class FGinkgoPauseMenu : 局内暂停菜单
 * 位于 Content/UI/PauseMenu，随包发布，采用左侧停靠暗黑云雾与现代扁平风格。
 * 属于一个本地玩家控制器（AGinkgoPlayerController），首次按下 Esc / F10 / 手柄菜单键时按需创建，关闭后释放视口控件与 Weft 视图。
 *
 * 行为约定：
 * - 单机模式（NM_Standalone）打开时暂停世界（SetPause(true)），关闭、读检查点或退出游戏前解除暂停；联网模式不暂停世界。
 * - 打开期间折叠普通玩法 HUD 层并独占 UI 输入（FInputModeUIOnly），底层以 Slate SBackgroundBlur 柔化游戏画面。
 * - 提供「继续游戏」「回到上一个检查点」「回到主菜单」「设置」「退出游戏」五个条目。
 * - 打开「设置」时隐藏自身根容器但保持暂停状态，待设置页关闭后自动恢复暂停菜单焦点。
 */
class CASEGINKGO_API FGinkgoPauseMenu
{
public:
	explicit FGinkgoPauseMenu(AGinkgoPlayerController& InController);
	~FGinkgoPauseMenu();

	FGinkgoPauseMenu(const FGinkgoPauseMenu&) = delete;
	FGinkgoPauseMenu& operator=(const FGinkgoPauseMenu&) = delete;

	/** 键盘上不与编辑器 PIE 停止键冲突的菜单切换键（F10；Esc 在打包版与非 PIE 同样有效） */
	static FKey GetToggleKey();

	/**
	 * @brief 打开暂停菜单：挂载背景模糊与 Weft 页面、按单机规则暂停世界并接管输入
	 * @param bInFromGamepad - 是否由手柄菜单键打开，用于页面初始按键提示风格与传给设置子页
	 * @return 打开成功返回 true；无游戏视口或 Weft 不可用时返回 false
	 */
	bool Show(bool bInFromGamepad = false);

	/** 关闭暂停菜单：释放子设置页（若由菜单打开）、解除单机暂停、还原普通 HUD 与输入模式 */
	void Hide();

	/** 暂停菜单当前是否处于打开状态（含子级设置页展开期间） */
	bool IsShown() const { return View.IsValid(); }

	/** 当前是否由本菜单主动置位了单机世界暂停 */
	bool HasPausedWorld() const { return bPausedWorld; }

	/** 设置子页关闭后由控制器回调，恢复暂停菜单显示与 UI 焦点 */
	void OnSettingsClosed();

	/** 调试与自动化测试用：返回页面 Weft 视图 */
	UWeftView* GetView() const { return View.Get(); }

	/** 调试与自动化测试用：返回承载页面的 SWeftViewport 控件 */
	TSharedPtr<SWeftViewport> GetWidget() const { return Widget; }

private:
	/** 页面消息触发的延迟动作：消息在 Slate 绘制/Tick 期间抵达，统一延后到下一帧 CoreTicker 执行 */
	enum class EPendingAction : uint8
	{
		None,
		Close,
		OpenSettings,
		RestartCheckpoint,
		ReturnToMainMenu,
		QuitGame,
	};

	/** 每帧（经 CoreTicker，单机世界暂停期间同样触发且避开 Slate 绘制重入）推进页面载入、焦点保护与延迟动作 */
	bool OnTick(float DeltaSeconds);

	/** 接收页面发来的 ready / sound / log / resume / restartCheckpoint / returnToMainMenu / openSettings / quitGame 消息 */
	void OnMessage(const FString& Name, const FString& JsonValue, uint64 CallId);

	/** 把输入模式切到 FInputModeUIOnly 并把 Slate 键盘焦点交给页面 */
	void ApplyInputMode();

	/** 把当前暂停状态、检查点可用性与地点名发给页面（pause-state 事件） */
	void SendMenuState() const;

	/** 启动关闭退场过渡：播关闭音、通知页面退场并在过渡结束后执行 Hide */
	void BeginClosing();

	/** 启动一次 Slate 背景模糊插值过渡（墙钟时间，暂停期间正常推进） */
	void StartBlurTransition(float FromStrength, float ToStrength, float DurationSeconds);

	/** 按墙钟进度推进当前背景模糊插值，返回是否已完成 */
	bool UpdateBlurTransition();

	/** 拥有本菜单的本地玩家控制器；跨关卡销毁时可能先失效 */
	TWeakObjectPtr<AGinkgoPlayerController> Controller;

	/** 挂载菜单控件的游戏视口；Hide 时用于卸载 RootWidget */
	TWeakObjectPtr<UGameViewportClient> ViewportClient;

	/** 强引用 Weft 视图（Weft 子系统仅保存弱引用）；仅在 Show 到 Hide 期间有效 */
	TStrongObjectPtr<UWeftView> View;

	/** 挂入游戏视口的根容器，底层为 SBackgroundBlur，顶层为 SWeftViewport */
	TSharedPtr<SOverlay> RootWidget;

	/** 底层 Slate 背景模糊控件；随开关过渡平滑推进 BlurStrength */
	TSharedPtr<SBackgroundBlur> BlurWidget;

	/** 实际承载暂停菜单页面的 Weft Slate 控件，生命周期与 RootWidget 一致 */
	TSharedPtr<SWeftViewport> Widget;

	/** 页面消息回调句柄；Show 注册、Hide 注销 */
	FDelegateHandle MessageHandle;

	/** CoreTicker 句柄（世界暂停期间不受 OnWorldPostActorTick 跳过影响）；Show 注册、Hide 注销 */
	FTSTicker::FDelegateHandle TickerHandle;

	/** 页面是否已按有效视口像素尺寸完成目录载入 */
	bool bLoaded = false;

	/** 本次是否由手柄菜单键触发打开，用于页面初始键位提示 */
	bool bFromGamepad = false;

	/** 是否由本菜单主动调用了 SetPause(true)；仅在此为真时才于 Hide 解除暂停 */
	bool bPausedWorld = false;

	/** 打开菜单之前鼠标指针是否处于显示状态（例如工作台或调参面板打开时），关闭时据此恢复 */
	bool bCursorWasShown = false;

	/** 是否正在等待由本菜单打开的设置子页关闭；期间隐藏自身根容器但维持暂停状态 */
	bool bWaitingForSettings = false;

	/** 进入设置子页时是否携带手柄输入模式标记 */
	bool bOpenSettingsFromGamepad = false;

	/** 关闭时是否跳过通用 Close 音效（如读检查点、退出游戏或退场动画开始时已播放音效） */
	bool bSuppressCloseSound = false;

	/** 是否正在播放关闭退场过渡；为真时屏蔽重复关闭请求并在模糊归零后执行 Hide */
	bool bClosing = false;

	/** 背景模糊过渡起始墙钟时间（秒，来自 FPlatformTime::Seconds） */
	double BlurTransitionStart = 0.0;

	/** 背景模糊过渡起始强度（Slate 逻辑模糊半径） */
	float BlurFromStrength = 0.f;

	/** 背景模糊过渡目标强度（Slate 逻辑模糊半径） */
	float BlurToStrength = 0.f;

	/** 当前已写入 SBackgroundBlur 的模糊强度（Slate 逻辑模糊半径），用于中途反向打断时无缝衔接 */
	float BlurCurrentStrength = 0.f;

	/** 背景模糊过渡总时长（秒）；0 表示无进行中的过渡 */
	float BlurTransitionDuration = 0.f;

	/** 待在下一帧 CoreTicker 执行的延迟动作；由 OnMessage 写入，执行前清零 */
	EPendingAction PendingAction = EPendingAction::None;
};

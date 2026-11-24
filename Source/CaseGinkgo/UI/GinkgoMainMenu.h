#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Framework/GinkgoDifficulty.h"
#include "UObject/StrongObjectPtr.h"

class AGinkgoPlayerController;
class SBackgroundBlur;
class SOverlay;
class SWeftViewport;
class UAudioComponent;
class UGameViewportClient;
class UWeftView;

/** 主菜单与开屏序列当前所处阶段 */
enum class EGinkgoMainMenuPhase : uint8
{
	/** 未显示，控件与 Weft 视图均已卸载 */
	Hidden,

	/** 全屏电影感开屏阶段（第 1 镜 Unreal Engine 5，第 2 镜 Weft UI Framework 与 MetaSound 同屏） */
	Splash,

	/** 实时游戏画面主菜单阶段（含特性画廊子视图与子设置页挂起期间） */
	Menu,
};

/**
 * @class FGinkgoMainMenu : 开屏动画与主菜单界面控制器
 * 页面位于 Content/UI/MainMenu，随包发布，直接叠放于实时 3D 游戏世界之上实现「主菜单即游戏画面」。
 * 隶属于单个本地玩家控制器（AGinkgoPlayerController），由 UGinkgoTitleSubsystem 决定首次进入是否先播两镜开屏动画。
 *
 * 数据流与生命周期：
 * - 开屏阶段（Splash）：全屏深炭动态云雾黑幕遮蔽场景，依次播放 Unreal Engine 5 与 Weft/MetaSound 两镜动态演出，
 *   每一镜切入与最终揭幕时通过 splashShot 消息通知本类播放裁剪后的 opening.wav 立体声转场音效。
 * - 主菜单阶段（Menu）：黑幕散开呈现实时 3D 场景，左侧暗黑云雾停靠「新游戏」「继续游戏」「设置」「特性画廊」「退出游戏」五项，
 *   并循环淡入播放 Brother's Keeper 配乐；进入设置子页或特性画廊时音乐无缝续播，进入玩法或退出时平滑淡出。
 * - 显示期间不暂停 3D 世界以保持天光、云层与水面鲜活，由控制器屏蔽玩法输入并折叠玩法 HUD 层。
 */
class CASEGINKGO_API FGinkgoMainMenu
{
public:
	explicit FGinkgoMainMenu(AGinkgoPlayerController& InController);
	~FGinkgoMainMenu();

	FGinkgoMainMenu(const FGinkgoMainMenu&) = delete;
	FGinkgoMainMenu& operator=(const FGinkgoMainMenu&) = delete;

	/**
	 * @brief 预加载开屏三段转场音效与主菜单背景音乐，避免首次切换镜头出现音画不同步
	 * @param WorldContext - 用于定位音频子系统的世界上下文对象
	 */
	static void PreloadAudio(const UObject* WorldContext);

	/**
	 * @brief 打开开屏画面或直接显示主菜单：挂载背景柔化层与 Weft 页面、接管 UI 输入并折叠玩法 HUD
	 * @param bStartWithSplash - true 从第 1 镜开屏动画起播；false 跳过开屏直接停留在主菜单选项（如从暂停菜单返回）
	 * @param bInFromGamepad - 是否以手柄模式初始化页面按键提示
	 * @return 打开成功返回 true；无游戏视口或 Weft 不可用时返回 false
	 */
	bool Show(bool bStartWithSplash = true, bool bInFromGamepad = false);

	/** 立即卸载主菜单与开屏视图、停止/淡出背景音乐、还原玩法 HUD 与游戏输入模式 */
	void Hide();

	/** 主菜单（含开屏阶段与设置子页展开期间）当前是否处于开启状态 */
	bool IsShown() const { return View.IsValid(); }

	/** 返回当前所处标题阶段（Hidden / Splash / Menu） */
	EGinkgoMainMenuPhase GetPhase() const { return Phase; }

	/** 当前是否正展开「特性画廊」子视图 */
	bool IsGalleryOpen() const { return bGalleryOpen; }

	/** 当前是否正在播放主菜单背景音乐（供自动化测试核验音频状态） */
	bool IsMusicPlaying() const;

	/** 返回最近一次由页面触发并尝试播放的开屏镜头序号（0 表示尚未触发，1–3 对应两镜与揭幕） */
	int32 GetLastSplashShotPlayed() const { return LastSplashShotPlayed; }

	/** 设置子页关闭后由玩家控制器回调，恢复主菜单根容器显示、UI 焦点与无重播回场过渡 */
	void OnSettingsClosed();

	/** 调试与自动化测试用：若仍在开屏阶段，立即跳过至主菜单阶段并启动背景音乐 */
	void SkipSplashToMenu();

	/** 调试与自动化测试用：返回页面 Weft 视图 */
	UWeftView* GetView() const { return View.Get(); }

	/** 调试与自动化测试用：返回承载页面的 SWeftViewport 控件 */
	TSharedPtr<SWeftViewport> GetWidget() const { return Widget; }

private:
	/** 页面消息触发的延迟动作，统一延后至下一帧 CoreTicker 执行，避免在 Slate 绘制遍历中卸载或切换视口 */
	enum class EPendingAction : uint8
	{
		None,
		StartNewGame,
		ContinueGame,
		OpenSettings,
		QuitGame,
	};

	/** 每帧（经 CoreTicker）推进背景模糊插值、页面延迟载入、UI 焦点保护与延迟动作 */
	bool OnTick(float DeltaSeconds);

	/** 接收页面发来的 ready / sound / log / splashShot / splashComplete / galleryState / newGame / continueGame / openSettings / quitGame 消息 */
	void OnMessage(const FString& Name, const FString& JsonValue, uint64 CallId);

	/** 将输入模式切到 FInputModeUIOnly 并把 Slate 键盘焦点交给页面 */
	void ApplyInputMode();

	/** 将当前阶段、检查点可用性、地点名与键鼠/手柄状态推送给页面（title-state 事件） */
	void SendMenuState() const;

	/** 按镜头序号（1/2/3）播放对应的裁剪后开屏过渡音效，并在前一段仍在响时做短暂淡出衔接 */
	void PlayOpeningShotSound(int32 ShotIndex);

	/** 启动主菜单循环背景音乐（Brother's Keeper）并按配置时长平滑淡入；已在播放时跳过重复创建 */
	void StartMusic();

	/** 平滑淡出或立即停止当前背景音乐并释放弱引用，由音频组件在淡出结束后自动销毁 */
	void StopMusic(float FadeOutSeconds);

	/** 进入主菜单阶段：更新阶段标记、同步主菜单虚化基准并淡入背景音乐 */
	void EnterMenuPhase(bool bImmediateBlur);

	/** 启动「新游戏」无缝进场退场过渡：淡出音乐与页面遮罩、归零虚化，过渡结束后卸载主菜单直入实时操控 */
	void BeginEnteringGameplay();

	/** 启动一次 Slate 背景模糊插值过渡（墙钟时间驱动） */
	void StartBlurTransition(float FromStrength, float ToStrength, float DurationSeconds);

	/** 按墙钟进度推进当前背景模糊插值，返回是否已完成 */
	bool UpdateBlurTransition();

	/** 拥有本界面的本地玩家控制器；关卡切换销毁时可能先失效 */
	TWeakObjectPtr<AGinkgoPlayerController> Controller;

	/** 挂载根控件的游戏视口；Hide 时用于卸载 RootWidget */
	TWeakObjectPtr<UGameViewportClient> ViewportClient;

	/** 强引用 Weft 视图（Weft 子系统仅持弱引用）；仅在 Show 到 Hide 期间有效 */
	TStrongObjectPtr<UWeftView> View;

	/** 挂入游戏视口的根容器，底层为 SBackgroundBlur，顶层为 SWeftViewport */
	TSharedPtr<SOverlay> RootWidget;

	/** 底层 Slate 背景模糊控件；开屏期与主菜单期保持 0 以呈现锐利实时场景，特性画廊期平滑加强景深 */
	TSharedPtr<SBackgroundBlur> BlurWidget;

	/** 实际承载主菜单与开屏页面的 Weft Slate 控件，生命周期与 RootWidget 一致 */
	TSharedPtr<SWeftViewport> Widget;

	/** 当前正在播放的开屏过渡音效组件；切入下一镜或跳过开屏时用于短促淡出避免叠糊 */
	TWeakObjectPtr<UAudioComponent> ActiveOpeningAudio;

	/** 当前正在播放的主菜单循环配乐组件（Brother's Keeper）；进出设置与画廊期间保持播放，进入游戏时平滑淡出 */
	TWeakObjectPtr<UAudioComponent> MusicAudio;

	/** 页面消息回调句柄；Show 注册、Hide 注销 */
	FDelegateHandle MessageHandle;

	/** CoreTicker 句柄；Show 注册、Hide 注销 */
	FTSTicker::FDelegateHandle TickerHandle;

	/** 当前所处标题阶段（Hidden / Splash / Menu） */
	EGinkgoMainMenuPhase Phase = EGinkgoMainMenuPhase::Hidden;

	/** 页面是否已按有效视口像素尺寸完成目录载入 */
	bool bLoaded = false;

	/** 本次 Show 是否要求从开屏两镜动画起步；页面 ready 时据此下发初始模式 */
	bool bStartWithSplash = true;

	/** 本次打开是否来源于手柄输入，决定初始按键提示风格 */
	bool bFromGamepad = false;

	/** 是否正在等待由主菜单打开的设置子页关闭；期间隐藏自身根容器但保留背景音乐继续播放 */
	bool bWaitingForSettings = false;

	/** 进入设置子页时携带的手柄输入标记 */
	bool bOpenSettingsFromGamepad = false;

	/** 当前是否处于「特性画廊」子视图展开状态 */
	bool bGalleryOpen = false;

	/** 是否正在播放「新游戏」无缝进入实时玩法的淡出过渡；为真时屏蔽交互并在过渡结束后调用 Hide */
	bool bEnteringGameplay = false;

	/** 最近一次触发播放的开屏镜头序号（0 未触发，1 UE5，2 Weft+MetaSound，3 揭幕入菜单） */
	int32 LastSplashShotPlayed = 0;

	/** 背景模糊过渡起始墙钟时间（秒，来自 FPlatformTime::Seconds） */
	double BlurTransitionStart = 0.0;

	/** 背景模糊过渡起始强度（Slate 逻辑模糊半径） */
	float BlurFromStrength = 0.f;

	/** 背景模糊过渡目标强度（Slate 逻辑模糊半径） */
	float BlurToStrength = 0.f;

	/** 当前已写入 SBackgroundBlur 的模糊强度（Slate 逻辑模糊半径），供中途反向切换平滑衔接 */
	float BlurCurrentStrength = 0.f;

	/** 背景模糊过渡总时长（秒）；0 表示无进行中的过渡 */
	float BlurTransitionDuration = 0.f;

	/** 待在下一帧 CoreTicker 执行的延迟动作；由 OnMessage 写入，执行前清零 */
	EPendingAction PendingAction = EPendingAction::None;

	/** 玩家在新游戏难度选择面板中确认的难度档位；由 OnMessage("newGame") 写入，执行 StartNewGame 时应用到 UGinkgoTitleSubsystem */
	EGinkgoDifficulty PendingNewGameDifficulty = EGinkgoDifficulty::Balanced;
};

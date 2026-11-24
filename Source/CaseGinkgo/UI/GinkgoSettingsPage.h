#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"              // FKey
#include "UI/GinkgoVideoSettingsEdit.h"
#include "UObject/StrongObjectPtr.h"     // 持有 weft 的 View：weft 的子系统只记弱引用

class APlayerController;
class FGinkgoKeyCapture;
class SBackgroundBlur;
class SOverlay;
class SWeftViewport;
class UGameViewportClient;
class UGinkgoInputSettingsSubsystem;
class UWeftView;
class UWorld;
struct FGinkgoInputSettingDef;

/**
 * @class FGinkgoSettingsPage : 设置界面
 * 现代扁平暗黑风格（与局内暂停菜单同源，底层叠加 Slate 背景模糊与暗黑烟雾效果，共用 UI/GinkgoUiTheme.h 的组件框架），页面在 Content/UI/Settings，随包；不是调试工具，发行版里也有。
 * 属于一个本地玩家控制器，第一次打开时才创建，关着时什么也没有。通常由暂停菜单（FGinkgoPauseMenu）中的「设置」条目进入并在关闭后返回，也可由调试命令与自动化测试直接打开。
 *
 * 开着时输入全给页面（FInputModeUIOnly：游戏视口不收键盘、鼠标与手柄，角色停下；视角与移动的修改器不跑）；是否暂停世界由父级暂停菜单控制（单机暂停、联网不暂停）。
 * 页面发 set、reset、resetDirectory、revert、confirm、close、state，这里改玩家的设置（UGinkgoInputSettingsSubsystem）、回变了的行。按钮的语义见 Docs/Design/Control.md「按钮」：
 * - 输入设置当场生效（Current），画质与显示暂存到确认时应用；
 * - 确认：写文件、关界面；写不进去时留在界面上，把原因告诉玩家；
 * - 撤销修改：所有目录回到上次确认时的状态，界面保持打开；
 * - 恢复默认：只重置本目录；输入预览随之更新，画质与显示仍待确认；
 * - 改键：页面发 capture，这里等玩家按下一个键（FGinkgoKeyCapture）、查过能不能绑再改；别的键位在用这个键时先问（页面发 assignKey 才互换）；
 * - 有改动时退出：页面问（确认并退出、放弃修改、继续修改）；这里的 close 就是放弃修改。关卡切换、退出游戏等没问就关的也算放弃。
 */
class CASEGINKGO_API FGinkgoSettingsPage
{
public:
	explicit FGinkgoSettingsPage(APlayerController& InController);
	~FGinkgoSettingsPage();

	FGinkgoSettingsPage(const FGinkgoSettingsPage&) = delete;
	FGinkgoSettingsPage& operator=(const FGinkgoSettingsPage&) = delete;

	/** 键盘上开关界面的键（Esc 另有；编辑器的 PIE 里 Esc 是停止运行，所以要有一个都能用的） */
	static FKey GetToggleKey();

	/**
	 * @brief 打开：建 weft 的 View 与视口上的控件、把输入交给页面，页面在视口有了大小之后载入
	 * @param bFromGamepad - 是手柄按开的：页面第一次打开时停在控制器这个目录，并显示手柄的按键提示
	 * @param InOnClosed - 关闭完成后的可选回调（由暂停菜单或主菜单传入，用于退回父菜单；世界销毁期间不会触发）
	 * @param InParentBlurStrength - 父级菜单当前的 Slate 背景模糊强度（逻辑半径）；小于 0 且存在回调时沿用暂停菜单默认模糊强度
	 * @return 打开了没有；weft 不画、没有游戏视口时为假
	 */
	bool Show(bool bFromGamepad = false, TFunction<void()> InOnClosed = nullptr, float InParentBlurStrength = -1.f);

	/** 关掉：还没确认的改动放弃，输入还给游戏或上层暂停菜单 */
	void Hide();

	/** 清除关闭回调；父级暂停菜单主动关闭时调用，避免析构重入 */
	void ClearOnClosedCallback() { OnClosedCallback.Reset(); }

	bool IsShown() const { return View.IsValid(); }

	/** 测试用（调试模块的 ginkgo.settings.input）：页面的 View 与控件 */
	UWeftView* GetView() const { return View.Get(); }
	TSharedPtr<SWeftViewport> GetWidget() const { return Widget; }

private:
	/** 每帧（经 CoreTicker，单机世界暂停期间同样触发且避开 Slate 绘制重入）推进页面载入、焦点保护与延迟关闭 */
	bool OnTick(float DeltaSeconds);
	void OnMessage(const FString& Name, const FString& JsonValue, uint64 CallId);

	/** 把输入交给页面：游戏视口不收输入、显示指针、键盘焦点在页面上 */
	void ApplyInputMode();

	UGinkgoInputSettingsSubsystem* GetSettings() const;

	/** 整张表：每一项的值、默认值、文件里的值、范围，加上这把枪的开镜缩放（预览用）与页面上次的状态。名字与说明在页面的 catalog.js */
	void SendTable() const;

	/** 变了的行与汇总（几项没确认、每个目录几项不是默认值、最近的结果） */
	void SendValues(TConstArrayView<const FGinkgoInputSettingDef*> Defs, const FString& Status, bool bWithKeys = false, bool bWithVideo = false) const;

	/** 开始等玩家按键：之后键盘、鼠标键与手柄的键先到这里，页面收不到 */
	void StartKeyCapture(int32 Index);
	void EndKeyCapture();
	/** 等到了一个键：Esc 或菜单键取消，Delete 或视图键清空，别的查过能绑就绑 */
	void OnCapturedKey(const FKey& Key);

	/** 启动关闭退场过渡：放弃未确认改动（若非确认关闭）、立即播关闭音、通知页面退场并推进背景模糊插值 */
	void BeginClosing();

	/** 启动一次 Slate 背景模糊插值过渡（墙钟时间，暂停期间正常推进） */
	void StartBlurTransition(float FromStrength, float ToStrength, float DurationSeconds);

	/** 按墙钟进度推进当前背景模糊插值，返回是否已完成 */
	bool UpdateBlurTransition();

	/** 本次打开设置页的机器级显示草稿，下次打开重新读取。 */
	FGinkgoVideoSettingsEdit Video;

	/** 拥有本设置页的本地玩家控制器；销毁时可能先失效 */
	TWeakObjectPtr<APlayerController> Controller;

	/** 挂载页面控件的游戏视口；Hide 时用于卸载 RootWidget */
	TWeakObjectPtr<UGameViewportClient> ViewportClient;

	/** 强引用 Weft 视图（Weft 子系统仅保存弱引用）；仅在 Show 到 Hide 期间有效 */
	TStrongObjectPtr<UWeftView> View;

	/** 挂入游戏视口的根容器，内含底层背景模糊与上层 SWeftViewport；隐藏时移除并清空 */
	TSharedPtr<SOverlay> RootWidget;

	/** 底层 Slate 背景模糊控件；随开关过渡平滑推进 BlurStrength，从暂停菜单进出时与父菜单无缝衔接 */
	TSharedPtr<SBackgroundBlur> BlurWidget;

	/** 实际承载设置页面的 Weft Slate 控件，位于 RootWidget 顶层，生命周期随 Show/Hide */
	TSharedPtr<SWeftViewport> Widget;

	/** 关闭完成后的可选回调，由暂停菜单在 Show 时注入，Hide 结束且非世界销毁时触发并清空 */
	TFunction<void()> OnClosedCallback;

	/** 页面是否已按有效视口像素尺寸完成载入 */
	bool bLoaded = false;

	/** 页面要关界面：下一帧 CoreTicker 启动退场过渡（消息是在 Slate 画控件的那一趟里来的，见调参面板的同一条） */
	bool bHideRequested = false;

	/** 确认存下来以后关的：放过确认的声音了，不再放关闭的（两声都长，叠在一起糊） */
	bool bClosedByConfirm = false;

	/** 是否正在播放关闭退场过渡；为真时屏蔽修改与重复关闭，待退场时长结束后执行 Video.Apply 与 Hide */
	bool bClosing = false;

	/** 关闭时是否跳过通用 Close 音效（退场过渡开始时已播过关闭音，避免 Hide 时重复播放） */
	bool bSuppressCloseSound = false;

	/** 本次打开是否来源于手柄输入，决定默认目录与底部提示风格 */
	bool bFromGamepad = false;

	/** 打开之前指针显示着没有（调参面板开着时）：关的时候还原 */
	bool bCursorWasShown = false;

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

	/** 父级菜单（暂停菜单或主菜单）的背景模糊基准强度（逻辑半径）；无父菜单时为 0，用于进出设置页无缝衔接 */
	float ParentBlurBaseline = 0.f;

	/** 页面上次的状态（目录、停在哪一行）：页面发来的 JSON 原样存着，关了再开回到原处。进程内有效 */
	FString PageState;

	/** 页面消息回调句柄；Show 注册、Hide 注销 */
	FDelegateHandle MessageHandle;

	/** CoreTicker 句柄（单机暂停期间不受 OnWorldPostActorTick 跳过影响）；Show 注册、Hide 注销 */
	FTSTicker::FDelegateHandle TickerHandle;

	/** 正在等键时才有的 Slate 输入预处理器，捕获结束或关闭页面时注销 */
	TSharedPtr<FGinkgoKeyCapture> KeyCapture;

	/** 在给哪个键位等键（GetKeyBindingDefs 的下标，INDEX_NONE 表示未捕获） */
	int32 KeyCaptureIndex = INDEX_NONE;
};

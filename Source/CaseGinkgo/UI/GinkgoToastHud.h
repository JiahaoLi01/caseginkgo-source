#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"      // ELevelTick
#include "UI/GinkgoToast.h"
#include "UObject/StrongObjectPtr.h"     // 持有 weft 的 View：weft 的子系统只记弱引用

class AGinkgoPlayerController;
class SWeftViewport;
class UGameViewportClient;
class UWeftView;
class UWorld;

/**
 * @class FGinkgoToastHud : 游戏里的 toast（Docs/Design/Control.md「Toast」）
 * 玩法、教程、已存档之类的信息，画在 weft 页面 Content/UI/Toast 上，在屏幕右边缘中间偏上；排队，一条一条显示，每条出现时放 notification 的声音。
 * 深色的框、不规则的左边和烟是着色器效果 Content/UI/Effects/toast_panel.hlsl。
 *
 * 不是调试工具，发行版里也有。属于一个本地玩家控制器，开始时创建；weft 不画时（-nullrhi、别的 RHI）什么也不做。
 * 队列在这里，页面只画这一条、放这一条的动画。游戏暂停、设置界面开着时队列不走：这时出的 toast 玩家看不到。
 */
class CASEGINKGO_API FGinkgoToastHud
{
public:
	explicit FGinkgoToastHud(AGinkgoPlayerController& InController);
	~FGinkgoToastHud();

	FGinkgoToastHud(const FGinkgoToastHud&) = delete;
	FGinkgoToastHud& operator=(const FGinkgoToastHud&) = delete;

	/**
	 * @brief 建 weft 的 View 与视口上的控件，页面在视口有了大小之后载入
	 * @return 是否建了；weft 不画、没有游戏视口时为假
	 */
	bool Show();

	/** 拿掉控件、放掉 View；没显示完的 toast 不要了 */
	void Hide();

	bool IsShown() const { return View.IsValid(); }

	/** @brief 排进队列，前面的都显示完才轮到它；weft 不画时不排 */
	void Push(const FGinkgoToast& Toast);

private:
	/** 每帧所有 Actor 更新之后：载入页面，队列往前走 */
	void OnWorldPostActorTick(UWorld* InWorld, ELevelTick TickType, float DeltaSeconds);

	void SendShow(const FGinkgoToast& Toast, const FGinkgoToastTimings& Timings);

	void SendHide(const FGinkgoToastTimings& Timings);

	TWeakObjectPtr<AGinkgoPlayerController> Controller;

	TStrongObjectPtr<UWeftView> View;

	TSharedPtr<SWeftViewport> Widget;

	TWeakObjectPtr<UGameViewportClient> ViewportClient;

	FDelegateHandle PostActorTickHandle;

	bool bLoaded = false;

	FGinkgoToastQueue Queue;

	/** 队列的时钟（秒）：游戏暂停、设置界面开着时不走，不受时间膨胀影响 */
	double Clock = 0.0;
};

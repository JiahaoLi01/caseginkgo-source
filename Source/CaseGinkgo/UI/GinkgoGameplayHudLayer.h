#pragma once

#include "CoreMinimal.h"

class SOverlay;
class SWidget;
class UGameViewportClient;

/**
 * 本地玩家的普通 HUD 容器：容纳武器、生命、交互、署名和 toast，地点标题与设置页在容器外。
 * 全局隐藏只作用于父容器，不改子控件的可见性或指针路由，避免覆盖工作台的交互状态。
 * 控制器在首次挂载 HUD 时创建，EndPlay 在各 HUD 清理后释放；没有 Tick。
 */
class CASEGINKGO_API FGinkgoGameplayHudLayer
{
public:
	FGinkgoGameplayHudLayer(UGameViewportClient& InViewport, bool bSuppressed);
	~FGinkgoGameplayHudLayer();

	FGinkgoGameplayHudLayer(const FGinkgoGameplayHudLayer&) = delete;
	FGinkgoGameplayHudLayer& operator=(const FGinkgoGameplayHudLayer&) = delete;

	/** 将子 HUD 按原有相对层级挂载，布局仍铺满玩家视口。 */
	void Add(const TSharedRef<SWidget>& Widget, int32 ZOrder);

	/** 仅移除指定子 HUD，不改变其它页面的状态。 */
	void Remove(const TSharedRef<SWidget>& Widget);

	/** 地点队列开始/结束时调用；恢复为仅子控件可命中，父容器本身不拦截指针。 */
	void SetSuppressed(bool bSuppressed);

private:
	/** 集成回归检查真实 Slate 父层，不绕过可见性进行页面输入。 */
	friend class FGinkgoHudLifecycleScenario;

	/** 所属游戏视口；退出时可能先失效，容器不延长其生命周期。 */
	TWeakObjectPtr<UGameViewportClient> Viewport;

	/** 普通 HUD 的共同父节点；局部显隐和点击状态仍由各子 HUD 自己持有。 */
	TSharedPtr<SOverlay> Root;
};

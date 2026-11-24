#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "UI/GinkgoCredits.h"
#include "UObject/StrongObjectPtr.h"

class AGinkgoPlayerController;
class SWeftViewport;
class UWeftView;

/** 本地署名表现：按需挂载于普通 HUD 层，不抢输入；暂停/地点标注时停表，空队列立即移除页面与 Tick。 */
class CASEGINKGO_API FGinkgoCreditsHud
{
public:
	explicit FGinkgoCreditsHud(AGinkgoPlayerController& InController);
	~FGinkgoCreditsHud();

	FGinkgoCreditsHud(const FGinkgoCreditsHud&) = delete;
	FGinkgoCreditsHud& operator=(const FGinkgoCreditsHud&) = delete;

	/** 入队只复制内容和设置；首次有效内容才创建页面和世界订阅。 */
	void Push(const TArray<FGinkgoCredit>& Credits);

private:
	/** 实际 PIE 回归检查体积触发、页面资源和暂停行为，不增加运行时调试入口。 */
	friend class FGinkgoCreditsWorldScenario;

	void BeginPresentation();
	void EndPresentation();
	void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void UpdatePage();

	/** 所属本地控制器，EndPlay 清理 HUD 时注销世界委托。 */
	TWeakObjectPtr<AGinkgoPlayerController> Controller;

	/** 仅播放期间持有的 Weft View，空队列释放强引用。 */
	TStrongObjectPtr<UWeftView> View;

	/** 不可交互的透明 Slate 容器，通过普通 HUD 父层隐藏，不影响准星或工作台输入。 */
	TSharedPtr<SWeftViewport> Widget;

	/** 只在队列非空时注册的世界更新，暂停时仍保留以等待恢复。 */
	FDelegateHandle TickHandle;

	/** 独立的按序播放状态，时钟只由本地游戏线程推进。 */
	FGinkgoCreditsQueue Queue;

	/** 已开始条目的递增编号，改变时才重建页面字符节点。 */
	uint32 Serial = 0;

	/** 最后一次发到当前 View 的条目编号，页面重建后清零。 */
	uint32 SentSerial = 0;

	/** 最近已发送的显现时刻（UI 秒）；同一条在停留与淡出期间固定，新条目强制发首帧。 */
	float SentRevealTime = -1.f;

	/** 最近已发送的整组透明度（0–1），只在实际改变时请求新页面帧。 */
	float SentOpacity = -1.f;

	/** 页面上次采样的逻辑尺寸（CSS px），视口改变时即使在停留阶段也要重新排版。 */
	FVector2D SentSize = FVector2D::ZeroVector;

	/** 页面是否已按有效视口尺寸加载；EndPresentation 清零。 */
	bool bLoaded = false;
};

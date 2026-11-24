#pragma once

#include "CoreMinimal.h"
#include "Checkpoint/GinkgoCheckpointState.h"
#include "Engine/EngineBaseTypes.h"
#include "UI/GinkgoLocationTitleSettings.h"
#include "UObject/StrongObjectPtr.h"

class AGinkgoPlayerController;
class SWeftViewport;
class UGameViewportClient;
class UWeftView;

/** 地点标注的纯时序。相邻检查点排队，HUD 在整段队列结束前保持隐藏；外部暂停时不给它推进时间。 */
class CASEGINKGO_API FGinkgoLocationTitleQueue
{
public:
	enum class EStep : uint8 { None, Begin, Finish };
	/** 入队时固定共用设置；地点数据只提供名称、声音和已确定的存档结果，全部时长来自项目设置。 */
	void Push(const FGinkgoCheckpointAnnouncement& Announcement, const FGinkgoLocationTitleSettings& Settings);
	EStep Advance(float DeltaSeconds);
	bool IsActive() const { return Current.IsSet() || !Pending.IsEmpty(); }
	const FGinkgoCheckpointAnnouncement* GetCurrent() const { return Current.IsSet() ? &Current->Announcement : nullptr; }
	const FGinkgoLocationTitleSettings* GetSettings() const { return Current.IsSet() ? &Current->Settings : nullptr; }
	float GetOpacity() const;

private:
	/** 一次标注的显示快照；共用配置属于项目设置，不回写到检查点 Actor 或存档。 */
	struct FEntry
	{
		/** 地点内容及保存结果，由拥有者 RPC 提供。 */
		FGinkgoCheckpointAnnouncement Announcement;

		/** 本次排版和时序，入队后不再受编辑器实时改配置影响。 */
		FGinkgoLocationTitleSettings Settings;
	};

	/** 尚未开始的标注；队列本身立即占用 HUD 显示权，避免触发帧漏出准星。 */
	TArray<FEntry> Pending;

	/** 当前标注，结束的同一帧清空；调用者在 Advance 前保存结束通知的存档结果。 */
	TOptional<FEntry> Current;

	/** 当前标注已经显示的 UI 秒数，只在外部允许推进时累加。 */
	float Elapsed = 0.f;
};

/** 本地控制器按需创建的地点队列；仅播放期间持有页面和 Tick，空队列立即释放展示资源。无渲染时仍推进反馈时序。 */
class CASEGINKGO_API FGinkgoLocationTitleHud
{
public:
	explicit FGinkgoLocationTitleHud(AGinkgoPlayerController& InController);
	~FGinkgoLocationTitleHud();

	FGinkgoLocationTitleHud(const FGinkgoLocationTitleHud&) = delete;
	FGinkgoLocationTitleHud& operator=(const FGinkgoLocationTitleHud&) = delete;

	void Push(const FGinkgoCheckpointAnnouncement& Announcement);
	bool IsActive() const { return Queue.IsActive(); }

private:
	/** 集成回归检查队列空闲时已释放页面、控件和世界订阅。 */
	friend class FGinkgoHudLifecycleScenario;

	/** 队列由空转为非空时创建展示资源，并订阅世界更新；无渲染时只订阅时序。 */
	void BeginPresentation();

	/** 最后一条结束或拥有者退出时移除控件、释放 View 并注销 Tick；可重复调用。 */
	void EndPresentation();

	void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void UpdatePage();

	/** 所属本地控制器；EndPlay 释放此对象时移除全局 Tick 委托。 */
	TWeakObjectPtr<AGinkgoPlayerController> Controller;

	/** 仅队列播放期间持有的 Weft 页面，不接收输入，位于普通 HUD 之上、设置界面之下。 */
	TStrongObjectPtr<UWeftView> View;

	/** 视口中的透明 Slate 容器；无渲染时为空。 */
	TSharedPtr<SWeftViewport> Widget;

	/** 移除控件时使用，避免持有跨地图的游戏视口。 */
	TWeakObjectPtr<UGameViewportClient> ViewportClient;

	/** 仅队列非空时订阅；即使 Weft 不可用也推进时序，队列结束立即注销。 */
	FDelegateHandle TickHandle;

	/** 页面在第一次得到有效视口尺寸后加载。 */
	bool bLoaded = false;

	/** 当前控制器独立的显示队列，不属于全局存档状态。 */
	FGinkgoLocationTitleQueue Queue;
};

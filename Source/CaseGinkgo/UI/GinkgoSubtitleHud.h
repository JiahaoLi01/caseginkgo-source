#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"

class AGinkgoPlayerController;
class SWeftViewport;
class UWeftView;
struct FGinkgoDialogueLine;

/** 本地玩家的环境台词，保留最近两名说话人，同一人续句时替换旧句。只在更新内容、设置和淡出边界时唤醒。 */
class FGinkgoSubtitleHud : public TSharedFromThis<FGinkgoSubtitleHud>
{
public:
	explicit FGinkgoSubtitleHud(AGinkgoPlayerController& InController);
	~FGinkgoSubtitleHud();

	void Push(uint32 SpeakerId, const FText& Speaker, const FGinkgoDialogueLine& Line);
	void Refresh();

private:
	struct FEntry
	{
		/** 权威端给定的说话对象标识，仅在当前世界的短时字幕窗口内使用。 */
		uint32 Id;

		/** 本地化说话人名称，可由本地设置隐藏。 */
		FText Speaker;

		/** 本句的本地化正文。 */
		FText Text;

		/** 开始淡出的世界时间（秒），暂停时与游戏一起停表。 */
		double FadeAt;
	};

	void Clear();
	bool OpenPage();

	/** 只在本地控制器生存期内使用，控制器先销毁本 HUD 再拆普通 HUD 层。 */
	TWeakObjectPtr<AGinkgoPlayerController> Controller;

	/** 按到达顺序保留的在屏条目，全部到期时卸下 View 和 Widget。 */
	TArray<FEntry> Entries;

	/** 指向下一次淡出或移除的单次计时器；新句到达时重新安排。 */
	FTimerHandle NextChange;

	/** 仅显示台词期间持有页面。 */
	TStrongObjectPtr<UWeftView> View;

	/** 普通 HUD 下只绘制、不接收输入的视口。 */
	TSharedPtr<SWeftViewport> Widget;
};

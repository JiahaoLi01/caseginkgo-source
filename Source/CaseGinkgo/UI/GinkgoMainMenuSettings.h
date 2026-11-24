#pragma once

#include "CoreMinimal.h"
#include "GinkgoMainMenuSettings.generated.h"

class USoundBase;

/**
 * @struct FGinkgoTitleSounds : 开屏动画与主菜单音频配置
 * 位于 项目设置 → Game → CaseGinkgo → UI（UGinkgoDeveloperSettings::TitleSounds）。
 * 开屏三段转场音效均裁剪自 opening.wav（/Game/Audio/Title/MS_Title_Opening_1..3），
 * 主菜单背景音乐为循环播放的 Brother's Keeper（/Game/Audio/Title/MS_Title_Music），由 FGinkgoMainMenu 负责预加载与淡入淡出控制。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoTitleSounds
{
	GENERATED_BODY()

	/** 开屏第 1 镜（Unreal Engine 5 徽标）进入时的立体声 2D 过渡音效；为空时不播放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Title")
	TSoftObjectPtr<USoundBase> OpeningShot1 = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Title/MS_Title_Opening_1.MS_Title_Opening_1")));

	/** 开屏第 2 镜（Weft UI Framework 与 MetaSound 同屏徽标）进入时的立体声 2D 过渡音效；为空时不播放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Title")
	TSoftObjectPtr<USoundBase> OpeningShot2 = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Title/MS_Title_Opening_2.MS_Title_Opening_2")));

	/** 开屏黑幕散开进入实时 3D 主菜单画面时的立体声 2D 过渡音效；为空时不播放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Title")
	TSoftObjectPtr<USoundBase> OpeningReveal = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Title/MS_Title_Opening_3.MS_Title_Opening_3")));

	/** 主菜单循环背景音乐（Brother's Keeper，2D 立体声循环 MetaSound）；为空时不播放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Title")
	TSoftObjectPtr<USoundBase> MenuMusic = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Audio/Title/MS_Title_Music.MS_Title_Music")));

	/** 开屏过渡音效的线性音量倍率（范围 ≥ 0，1 为资产原响度，0 为静音） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Title", meta = (ClampMin = 0))
	float OpeningVolume = 0.95f;

	/** 主菜单背景音乐的线性音量倍率（范围 ≥ 0，默认 0.72 兼顾沉浸氛围与界面点击反馈清晰度） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Title", meta = (ClampMin = 0))
	float MusicVolume = 0.72f;

	/** 主菜单背景音乐淡入时长（秒），进入主菜单阶段时生效；0 表示立即满音量起播 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Title", meta = (ClampMin = 0, Units = "s"))
	float MusicFadeInSeconds = 1.8f;

	/** 离开主菜单进入游戏或退出时背景音乐的淡出时长（秒）；0 表示立即停止 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI|Title", meta = (ClampMin = 0, Units = "s"))
	float MusicFadeOutSeconds = 1.15f;
};

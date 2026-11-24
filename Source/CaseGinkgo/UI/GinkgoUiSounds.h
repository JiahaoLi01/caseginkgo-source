#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GinkgoUiSounds.generated.h"

class UAudioComponent;
class USoundBase;

/** 菜单与工作台的界面反馈类型。工作台确认安装和设置菜单保存分别使用独立音色。 */
UENUM(BlueprintType)
enum class EGinkgoUiSound : uint8
{
	None		UMETA(DisplayName = "没有"),
	Open		UMETA(DisplayName = "打开菜单"),
	Close		UMETA(DisplayName = "关闭菜单"),
	Focus		UMETA(DisplayName = "焦点移动"),
	Select		UMETA(DisplayName = "选择、改值"),
	Confirm		UMETA(DisplayName = "确认"),
	Deny		UMETA(DisplayName = "取消、返回"),
	Invalid		UMETA(DisplayName = "不能这样做"),
	Notification	UMETA(DisplayName = "通知（toast）"),
	WorkbenchConfirm UMETA(DisplayName = "工作台确认安装"),
};

/** @struct FGinkgoUiSound : 一种菜单的声音与它的音量 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoUiSound
{
	GENERATED_BODY()

	FGinkgoUiSound() = default;
	explicit FGinkgoUiSound(const TCHAR* Path) : Sound(FSoftObjectPath(Path)) {}

	/** 2D 的声音。空的不放 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	TSoftObjectPtr<USoundBase> Sound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = 0))
	float Volume = 1.f;
};

/**
 * @struct FGinkgoUiSounds : 菜单的声音（你给的 audio_selected/UI，2026-11-09，Private/audio/source/UI，每种 4 条随机放、不连着重复）。
 * MetaSound 在 /Game/UI/Audio/Menu，样本切过开头的静音，响度同你给的文件（Private/audio/tools/split_ui_audio.py）
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoUiSounds
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound Open = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_Open.MS_UI_Open"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound Close = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_Close.MS_UI_Close"));

	/** 焦点移动：用的是 ui_focus_style（你说的） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound Focus = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_Focus.MS_UI_Focus"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound Select = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_Select.MS_UI_Select"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound Confirm = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_Confirm.MS_UI_Confirm"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound Deny = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_Deny.MS_UI_Deny"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound Invalid = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_Invalid.MS_UI_Invalid"));

	/** 每条 toast 出现时（Docs/Design/Control.md「Toast」） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound Notification = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_Notification.MS_UI_Notification"));

	/** 服务器接受安装时播放的短点击，四个变体由 MetaSound 随机选择。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	FGinkgoUiSound WorkbenchConfirm = FGinkgoUiSound(TEXT("/Game/UI/Audio/Menu/MS_UI_WorkbenchConfirm.MS_UI_WorkbenchConfirm"));

	/** 工作台界面反馈的线性音量倍数，乘在各提示音自身的音量上；0 为静音，1 为原音量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = 0, ClampMax = 1))
	float WorkbenchVolume = 0.45f;

	/** 同一种新的一声出来时前一声还在响，前一声这么快淡掉：按住方向键走、连着点时不叠成一片 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = 0, Units = "s"))
	float FadePrevious = 0.06f;

	/** 焦点的声音最短隔多久放一次：按住方向键时一秒走几十格，每格都放就成了一串杂音 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = 0, Units = "s"))
	float FocusMinInterval = 0.08f;

	/** @return 这一种的声音；None 是空 */
	const FGinkgoUiSound* Get(EGinkgoUiSound Kind) const;
};

namespace Ginkgo::UI
{
	/** @return 名字对的种类（页面的消息 "sound"，名字同枚举：Focus、Select……）；认不出是 None */
	CASEGINKGO_API EGinkgoUiSound ParseUiSound(const FString& Name);
}

/**
 * @class UGinkgoUiSoundSubsystem : 放菜单的声音（Docs/Design/Control.md「菜单的声音」）
 * 打开、关闭由界面的 C++ 放，确认存没存下来也由 C++ 放；别的由页面发一条消息 "sound"，放在页面的语义上（值真的变了、焦点真的动了）
 */
UCLASS()
class CASEGINKGO_API UGinkgoUiSoundSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * @brief 放一声：2D，游戏暂停时也响
	 * @return 放了没有（声音空着、没有声音设备、焦点的声音挨得太近时没放）
	 */
	static bool Play(const UObject* WorldContext, EGinkgoUiSound Kind);

	/** 本地玩家操作工作台时播放界面提示，使用工作台的音量倍数。 */
	static bool PlayWorkbench(const UObject* WorldContext, EGinkgoUiSound Kind);

	/** @brief 页面的消息 "sound"：JsonValue 是种类的名字（带引号的 JSON 字符串） */
	static bool PlayFromPage(const UObject* WorldContext, const FString& JsonValue);

	/** @brief 先读好、先建好 MetaSound：第一次打开菜单不多等 */
	static void Preload(const UObject* WorldContext);

	/** 预热单个界面声音并由 GameInstance 持有；地点标注等可配置音效复用菜单的 MetaSound 缓存路径。空值忽略。 */
	static void PreloadSound(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Sound);

private:
	static UGinkgoUiSoundSubsystem* Find(const UObject* WorldContext);

	bool PlayKind(EGinkgoUiSound Kind, float VolumeScale = 1.f);

	static constexpr int32 KindCount = static_cast<int32>(EGinkgoUiSound::WorkbenchConfirm) + 1;

	/** 每种上一声：FGinkgoUiSounds::FadePrevious */
	TWeakObjectPtr<UAudioComponent> Ringing[KindCount];

	/** 上一次放焦点的声音的时刻（不受暂停影响的世界时间）：FGinkgoUiSounds::FocusMinInterval */
	double LastFocusTime = -TNumericLimits<float>::Max();

	/** 读进来的声音：强引用，垃圾回收一次不用再读盘 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> Loaded;
};

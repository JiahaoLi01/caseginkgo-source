#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GinkgoCharacterAudioComponent.generated.h"

class UAudioComponent;
class USoundBase;

/**
 * @struct FGinkgoLowHealthBreathSettings : 本地玩家低血量急促喘息的阈值、缓入缓出与音量配置
 * 由 UGinkgoCharacterAudioComponent 持有并在游戏线程逐帧推进，配合血量暗角后处理提供连续的濒危听觉提示。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoLowHealthBreathSettings
{
	GENERATED_BODY()

	/** 血量比例低于该阈值时开始缓入急促喘息（无量纲 0–1，0.60 与血量暗角在 65% 起步压色形成视听衔接） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Breath", meta = (ClampMin = 0.05, ClampMax = 1.0))
	float StartHealthRatio = 0.60f;

	/** 血量比例降至该阈值及以下时喘息目标强度达到满载 1.0（无量纲 0–1，应小于 StartHealthRatio） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Breath", meta = (ClampMin = 0.0, ClampMax = 0.95))
	float FullHealthRatio = 0.18f;

	/** 归一化喘息包络从 0 缓入至 1 所需的时长（秒，正数），避免突发重伤时生硬弹出 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Breath", meta = (ClampMin = 0.05, Units = "s"))
	float FadeInDuration = 0.85f;

	/** 脱战回血离开低血量区间后，归一化喘息包络从 1 缓出至 0 所需的时长（秒，正数） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Breath", meta = (ClampMin = 0.05, Units = "s"))
	float FadeOutDuration = 1.25f;

	/** 角色死亡时喘息包络快速收尾淡出至 0 的时长（秒，正数），自然衔接死亡音效而不残留尸体呼吸 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Breath", meta = (ClampMin = 0.05, Units = "s"))
	float DeathFadeOutDuration = 0.35f;

	/** 满载喘息时施加到音频组件的线性音量倍率上限（无量纲，非负） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Audio|Breath", meta = (ClampMin = 0.0, UIMax = 2.0))
	float MaxVolume = 0.85f;
};

namespace Ginkgo::Character
{
	/**
	 * @brief 推进一帧低血量急促喘息的平滑包络并换算最终线性音量倍率（纯函数，供组件 Tick 与自动化测试共用）
	 * @param InOutEnvelope - 归一化喘息包络进度（0–1；0 为静默，1 为满载），按缓入或缓出时长限速平滑推进
	 * @param HealthRatio - 当前血量比例（0–1，由 Health / MaxHealth 计算）
	 * @param bAliveAndLocal - 角色是否存活且由本地玩家控制；失控或非本地玩家时目标归零
	 * @param bDead - 角色是否处于死亡状态；为真时切换至 DeathFadeOutDuration 加速收尾
	 * @param DeltaSeconds - 本帧步长（秒，非负）
	 * @param Settings - 低血量喘息阈值、缓入缓出时长与音量上限配置
	 * @return 经 SmoothStep 整形后的线性音量倍率（[0, Settings.MaxVolume]）
	 */
	CASEGINKGO_API float StepLowHealthBreath(float& InOutEnvelope, float HealthRatio, bool bAliveAndLocal, bool bDead,
		float DeltaSeconds, const FGinkgoLowHealthBreathSettings& Settings);
}

/**
 * @class UGinkgoCharacterAudioComponent : 角色受击与死亡音效组件
 * 挂在 AGinkgoCharacter 上，由玩家与 AGinkgoAICharacter 共用死亡空间音效，由本地玩家独占低血量 2D 喘息循环：
 * - 死亡音效（MS_Character_Death）：所有角色（玩家、远端玩家与敌人）进入布娃娃时在头部世界坐标以 3D 空间化方式独立播放，
 *   图内随机挑选 7 段死亡人声（不连重）并叠加随机音高与增益变化，绑定 SA_Character_Death 距离衰减与低通。
 * - 低血量喘息（MS_Character_BreathFast）：仅本地玩家启用，血量低于阈值时随伤势平滑缓入，回血或死亡时平滑缓出并在归零后停播释放声道；
 *   AI 角色默认关闭并禁用 Tick，保持零每帧开销。
 */
UCLASS(ClassGroup = (Ginkgo), meta = (BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoCharacterAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoCharacterAudioComponent();

	/**
	 * @brief 在角色头部世界位置生成一次独立的 3D 空间死亡音效（玩家与敌人通用，专用服务器跳过）
	 * @return 成功创建的音频组件指针；无音频设备、专用服务器或缺少资产时返回空
	 */
	UAudioComponent* PlayDeathSound();

	/** 配置是否启用本地玩家低血量急促喘息并同步组件 Tick 启停；AI 角色构造时关闭 */
	void SetLowHealthBreathEnabled(bool bEnabled);

	/** 当前是否启用了低血量急促喘息功能 */
	bool IsLowHealthBreathEnabled() const { return bLowHealthBreathEnabled; }

	/** 立即停止低血量喘息并清零包络状态；复活、读档复位、失去本地控制或 EndPlay 时调用 */
	void ResetBreathState();

	/** 推进一帧低血量喘息状态并同步音频组件启停与音量；供 TickComponent 调用及自动化测试按确定步长驱动 */
	void StepLowHealthBreathAudio(float DeltaSeconds);

	/** 死亡空间音效资产（默认指向 MS_Character_Death） */
	USoundBase* GetDeathSound() const { return DeathSound; }

	/** 低血量急促喘息循环音效资产（默认指向 MS_Character_BreathFast） */
	USoundBase* GetLowHealthBreathSound() const { return LowHealthBreathSound; }

	/** 低血量喘息阈值、缓入缓出与音量配置 */
	const FGinkgoLowHealthBreathSettings& GetLowHealthBreathSettings() const { return LowHealthBreath; }

	/** 当前归一化喘息包络值（0–1），用于调试与自动化核验 */
	float GetBreathEnvelope() const { return BreathEnvelope; }

	/** 当前低血量喘息音频组件（仅首次触发后按需创建，未播放或已清理时可能为空） */
	UAudioComponent* GetBreathAudioComponent() const { return BreathAudio; }

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

protected:
	/** 角色死亡时在头部世界坐标播放的 3D 空间随机死亡音效（MS_Character_Death，内嵌 SA_Character_Death 衰减） */
	UPROPERTY(EditAnywhere, Category = "Audio|Death")
	TObjectPtr<USoundBase> DeathSound;

	/** 本地玩家低血量时播放的 2D 连续循环急促喘息音效（MS_Character_BreathFast） */
	UPROPERTY(EditAnywhere, Category = "Audio|Breath")
	TObjectPtr<USoundBase> LowHealthBreathSound;

	/** 是否为本角色启用低血量急促喘息；玩家角色默认开启，AI 敌人构造时关闭 */
	UPROPERTY(EditAnywhere, Category = "Audio|Breath")
	bool bLowHealthBreathEnabled = true;

	/** 低血量急促喘息的触发阈值、缓入/缓出时长与音量上限配置 */
	UPROPERTY(EditAnywhere, Category = "Audio|Breath")
	FGinkgoLowHealthBreathSettings LowHealthBreath;

private:
	/** 解析死亡发声的世界空间坐标（优先角色骨骼 head 插槽，回退视点或 Actor 原点，单位 cm） */
	FVector ResolveDeathSoundOrigin() const;

	/** 按开关与网络角色刷新组件 Tick 启停，AI 与专用服务器保持零 Tick 开销 */
	void RefreshBreathTick();

	/** 本地玩家低血量喘息专用的非空间化 2D 音频组件；首次需要淡入时按需创建，EndPlay 时停止并释放 */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> BreathAudio;

	/** 当前归一化喘息包络进度（0–1），由游戏线程逐帧推进，复位或失去控制时清零 */
	float BreathEnvelope = 0.f;
};

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GinkgoFlashbangComponent.generated.h"

class AGinkgoCharacter;
class UAudioComponent;
class UGinkgoAnimAction;
class UAnimMontage;
class UAnimSequence;
class ULineBatchComponent;
class USkeletalMeshComponent;
class USoundBase;

UENUM()
enum class EGinkgoFlashPhase : uint8 { Empty, Belt, Draw, Hold, Throw, FollowThrough };

/** 同步整个阶段和起点；迟到的复制从当前动画时间接入。 */
USTRUCT()
struct FGinkgoFlashState
{
	GENERATED_BODY()

	/** 服务器确认的库存与左臂动作阶段。 */
	UPROPERTY()
	EGinkgoFlashPhase Phase = EGinkgoFlashPhase::Empty;

	/** 阶段开始的服务器世界时间，单位秒。 */
	UPROPERTY()
	double Start = 0.0;
};

/** 管理一个左腰槽。轻按与长按共享取出片段，左臂占用贯穿取出、等待和投后随动。 */
UCLASS()
class CASEGINKGO_API UGinkgoFlashbangComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoFlashbangComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick Type, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Press();
	void Release();
	void CancelThrow();
	void RestoreInventory(bool bHasItem);
	void SetLocalPickup(bool bPicking);

	bool HasItem() const;
	bool IsUsingArm() const;

	/** 拾取或投掷只占用左臂；武器仍需单独确认右手可用。 */
	bool AllowsOneHandFire() const { return bLocalPicking || IsUsingArm(); }
	EGinkgoFlashPhase GetPhase() const { return State.Phase; }

	/** 左臂投掷阶段绑定的三维空间音效资产；默认指向随机采样的 MS_Flashbang_Throw。 */
	USoundBase* GetThrowSound() const { return ThrowSound; }

	/** 当前正在播放的左手投掷语音组件；未处于挥臂发声或无音频设备时为空。 */
	UAudioComponent* GetThrowAudio() const { return ThrowAudio; }

	/** 当前取出动作对腰部抓取点的约束，0 表示自由跟随左臂片段。 */
	float GetBeltIK() const;

	/** 出手窗口中，给左腕 IK 的权重与世界空间脱手点（cm）。其余时间不查询轨迹。 */
	float GetReleaseIK(FVector& OutWorldPoint) const;

	/** 美术提供的物品相对 pelvis、hand_l 的挂点（厘米）。 */
	static FTransform BeltSocket();
	static FTransform GripSocket();

private:
	/** 库存与播放阶段由权威端写，客户端通过同一表现入口接收。 */
	UPROPERTY(ReplicatedUsing = OnRep_State)
	FGinkgoFlashState State;

	/** 占用左臂的 AnimAction，被高优先级动作抢占时取消尚未脱手的投掷。 */
	UPROPERTY(Transient)
	TObjectPtr<UGinkgoAnimAction> ArmAction;

	/** 当前 ArmL 槽的动态蒙太奇，只操作这个实例。 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> Montage;

	/** 腰部与左手共用的物品模型；首次拾取时才创建。 */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Item;

	/** 仅本地投掷玩家绘制的抛物线，离开持握阶段时清空。 */
	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> Guide;

	/** 取出时已经松键，取出完成后直接接投出；仅服务器消费。 */
	bool bReleaseQueued = false;

	/** 松开按键那一刻的视线单位向量（世界空间）。服务器保留到脱手，随后的转镜不改变投向。 */
	FVector ReleasedAim = FVector::ZeroVector;

	/** 拾取的本地手持替身尚未收纳时暂缓显示腰部模型。 */
	bool bLocalPicking = false;

	/** 可打包的左臂取出片段，最后一帧是胸前持握。 */
	UPROPERTY()
	TObjectPtr<UAnimSequence> DrawArm;

	/** 左臂投掷片段，从持握姿势连续接入。 */
	UPROPERTY()
	TObjectPtr<UAnimSequence> ThrowArm;

	/** 物品与两段角色动作同步的骨骼片段。 */
	UPROPERTY()
	TObjectPtr<UAnimSequence> DrawItem;

	/** 从胸前到脱手的物品动画，随动段仍保留最后几帧。 */
	UPROPERTY()
	TObjectPtr<UAnimSequence> ThrowItem;

	/** 左臂挥出闪光弹时的三维空间 MetaSound，主甩动瞬态对齐脱手第 9 帧（266.7 ms），自带风格化衰减。 */
	UPROPERTY()
	TObjectPtr<USoundBase> ThrowSound;

	/** 投掷挥臂期间附着在角色 hand_l 上的三维音频实例；正常脱手后自然放完，仅在脱手前取消或退出时淡出。 */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ThrowAudio;

	/** 上一次表现的部位与阶段，复制可直接跳过中间阶段。 */
	EGinkgoFlashPhase Presented = EGinkgoFlashPhase::Empty;

	/** 本地抛物线更新余量（秒）；角色动画与物品挂点逐帧同步。 */
	float GuideElapsed = 0.f;

	UFUNCTION(Server, Reliable)
	void ServerPress();

	UFUNCTION(Server, Reliable)
	void ServerRelease(FVector_NetQuantizeNormal AimDirection);

	UFUNCTION(Server, Reliable)
	void ServerCancel();

	UFUNCTION()
	void OnRep_State();

	AGinkgoCharacter* Character() const;
	double Now() const;
	void SetPhase(EGinkgoFlashPhase Phase);
	bool ClaimArm();
	void DropArm();
	void PlayPhase(bool bThrow, float Position, bool bPause);
	void UpdateItem(float Age);
	void UpdateGuide();
	void Launch();
};

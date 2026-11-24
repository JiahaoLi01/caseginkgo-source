#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Weapon/GinkgoScope.h"  // FGinkgoScope：瞄具的参数随枪写进来
#include "GinkgoCameraContextComponent.generated.h"

class USceneComponent;

/**
 * @enum EGinkgoCameraContextSource : 写状态快照的一方
 * 各写各的一份标签，相机读它们的并集：移动组件每次移动后覆盖写自己那份，不会擦掉武器写的开镜
 */
enum class EGinkgoCameraContextSource : uint8
{
	Movement,
	Weapon,
	Count,
};

/**
 * @struct FGinkgoCameraImpulse : 事件带来的一次冲量与它的弹簧（枪的后坐，见 UGinkgoCameraCarriedShake）
 */
struct FGinkgoCameraImpulse
{
	/** 加到角速度上（度/秒）：Pitch 为正是抬头，Yaw 为正是向右，Roll 为正是向右倾 */
	FRotator Rotation = FRotator::ZeroRotator;

	/** 加到 FOV 的速度上（度/秒） */
	float FieldOfView = 0.f;

	/** 无阻尼时的角频率（rad/s） */
	float Omega = 0.f;

	/** 阻尼比，0–1 */
	float Zeta = 1.f;

	/** 回弹的角频率（rad/s）：到峰值以后换成这一组 */
	float ReturnOmega = 0.f;

	/** 回弹的阻尼比，0–1 */
	float ReturnZeta = 1.f;
};

/**
 * @struct FGinkgoCameraEvent : 一个瞬时事件（滑铲开始、落地、开枪）
 * 只在本地传递，不进资产，不复制
 */
struct FGinkgoCameraEvent
{
	FGameplayTag Tag;

	/** 发生在哪一刻（世界时间，s）：叠加层按它把这一帧拆开推进，冲量加在这一刻，结果与帧率无关 */
	double Time = 0.0;

	/** 强度：滑铲开始是这次的起步加速（cm/s），落地是着地前的下落速度（cm/s） */
	float Magnitude = 0.f;

	/** 事件自己带的冲量：开枪的（每把枪不一样、每枪有随机，枪算好了带过来，相机不认识武器）。别的事件是空的 */
	TArray<FGinkgoCameraImpulse> Impulses;
};

/**
 * @class UGinkgoCameraContextComponent : 相机上下文
 * 修改器的激活条件与数值来源只读这里，不读任何别的组件（Docs/Design/CameraPipeline.md「修改器激活：相机上下文组件」）。
 * 依赖方向：移动组件、武器组件 → 这里 ← 相机管线，三方互不引用。挂在角色上，相机读当前视角目标上的这一个：
 * 换视角目标、重生、附身都不用重新订阅，下一帧就是新目标的状态。
 *
 * 三部分：
 * 1. 状态快照：标签（Movement.Slide、Weapon.ADS……）与连续量（水平速度、开镜进度……，按标签取），写的一方每帧（每次移动后）覆盖。
 *    用快照不用事件：晚加入、重生、换视角目标之后，修改器下一帧就回到正确状态；服务器校正后重放的移动也只是覆盖，留下的是最后一次的
 * 2. 事件队列：滑铲开始、落地这类瞬时事件，带时刻与强度。事件发生时入队，叠加层求值时一次取空。
 *    写的一方只在第一次模拟时发（客户端重放不发），每次滑铲只有一次
 * 3. 第一人称的瞄具（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：手上的枪带瞄具时，武器组件写瞄具装在哪个组件上（枪的网格）、是哪一具。
 *    组件的变换由相机在更新时读：那时这一帧的动画已经求完，枪在最终的位置上，眼睛（出瞳）就在它上面。相机写回一项：头藏不藏
 *
 * 只在本地控制的角色上写（相机只在本地算）。
 */
UCLASS(ClassGroup = (Ginkgo), meta = (BlueprintSpawnableComponent))
class CASEGINKGO_API UGinkgoCameraContextComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGinkgoCameraContextComponent();

	/** 是否收集相机瞬时事件；AI 没有相机消费者时关闭。关闭同时丢弃积压事件，状态快照仍可正常读取。 */
	void SetCollectEvents(bool bCollect);

	/** 队列最多存几个事件：相机每帧取空，正常情况下一帧只有一两个；满了丢最早的 */
	static constexpr int32 MaxEvents = 16;

	/** @brief Actor 上的相机上下文；没有时为空 */
	static UGinkgoCameraContextComponent* Find(const AActor* Actor);

	// ===== 状态快照 =====

	/**
	 * @brief 覆盖一方写的标签
	 * @param Source - 写的一方：只换它那一份，别的一方写的不动
	 */
	void SetTags(EGinkgoCameraContextSource Source, const FGameplayTagContainer& InTags);

	/** 各方写的标签的并集 */
	const FGameplayTagContainer& GetTags() const { return Tags; }

	/** @brief 写一个连续量（覆盖） */
	void SetScalar(const FGameplayTag& Key, float Value);

	/** @brief 读一个连续量；没写过时为空 */
	TOptional<float> FindScalar(const FGameplayTag& Key) const;

	/** 全部连续量（调试与回放用） */
	const TMap<FGameplayTag, float>& GetScalars() const { return Scalars; }

	// ===== 事件队列 =====

	/** @brief 事件入队：只在第一次模拟时调 */
	void PushEvent(const FGinkgoCameraEvent& Event);

	/** @brief 一次取空 */
	TArray<FGinkgoCameraEvent> TakeEvents();

	/** 入过队的事件一共多少个（累计，调试与回放用：每次滑铲一次） */
	int32 GetPushedEventCount() const { return PushedEventCount; }

	/** 还没被相机取走的事件（不取）：武器组件在这一帧相机更新之前推一份相机的后坐（AGinkgoPlayerCameraManager::PredictSightFollow） */
	TConstArrayView<FGinkgoCameraEvent> GetPendingEvents() const { return Events; }

	// ===== 第一人称的瞄具 =====

	/**
	 * @brief 手上的枪带瞄具时武器组件每帧写；没有瞄具、枪不在手上时写空的
	 * @param Component - 枪的网格：瞄具的安装位置（FGinkgoScopeMount）在它的坐标里
	 * @param Scope - 是哪一具；Component 为空时不看
	 * @param Offset - 这一帧枪口对准以后加在枪上的程序化偏移（枪的坐标：踢动、转镜头的落后、走路的偏与晃）：相机去掉它找眼睛的家
	 */
	void SetSight(const USceneComponent* Component, const FGinkgoScope* Scope, const FTransform& Offset);

	/** 瞄具装在哪个组件上；手上的枪没有瞄具时为空 */
	const USceneComponent* GetSightComponent() const { return SightComponent.Get(); }

	/** 枪的程序化偏移（SetSight 的 Offset） */
	const FTransform& GetSightOffset() const { return SightOffset; }

	/** 手上那具瞄具的参数（GetSightComponent 不为空时才有意义） */
	const FGinkgoScope& GetSightScope() const { return SightScope; }

	/**
	 * @brief 相机写：第一人称时把角色的头藏起来（相机滑到眼睛那里会穿过头，眼睛本来就在头里）。变了才动网格：
	 *        藏的是网格上的头骨骼（缩成一点，连同影子），下一次刷新骨骼时生效，所以相机提前一段距离要
	 * @note 发行版也有：这是第一人称开镜本身，不是调试
	 */
	void SetHeadHidden(bool bHidden);

	/** 头现在藏着没有（回放与测量） */
	bool IsHeadHidden() const { return bHeadHidden; }

	/** 藏的骨骼：小白人的头（下面没有别的骨骼） */
	static const FName HeadBoneName;

private:
	/** 由拥有者在初始化时决定是否需要瞬时事件，AI 默认关闭以免无消费者的队列积压。 */
	bool bCollectEvents = true;

	/** 每一方写的标签 */
	FGameplayTagContainer SourceTags[static_cast<int32>(EGinkgoCameraContextSource::Count)];

	/** 并集：有一方的变了才重建 */
	FGameplayTagContainer Tags;

	TMap<FGameplayTag, float> Scalars;

	TArray<FGinkgoCameraEvent> Events;

	int32 PushedEventCount = 0;

	/** 瞄具装在哪个组件上（枪的网格）：枪丢了、换了就是空的 */
	TWeakObjectPtr<const USceneComponent> SightComponent;

	/** 那具瞄具的参数（拷一份：项目设置在编辑器里改的时候表会重新分配） */
	FGinkgoScope SightScope;

	/** 枪的程序化偏移（GetSightOffset） */
	FTransform SightOffset = FTransform::Identity;

	bool bHeadHidden = false;
};

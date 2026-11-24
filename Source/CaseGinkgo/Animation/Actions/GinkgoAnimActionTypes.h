#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GinkgoAnimActionTypes.generated.h"

class UAnimMontage;
class USceneComponent;

/**
 * @enum EGinkgoAnimActionOutcome : 动画动作的结局
 * 每个动作只结束一次。见 Docs/Design/AnimActions.md「结局」
 */
UENUM(BlueprintType)
enum class EGinkgoAnimActionOutcome : uint8
{
	/** 还没结束 */
	None		UMETA(DisplayName = "未结束"),
	/** 蒙太奇开始自然淡出，或者放到了完成点 */
	Completed	UMETA(DisplayName = "完成"),
	/** 动作自己中止（中止条件、回调里判断），或者蒙太奇被别人停了、顶掉了 */
	Aborted		UMETA(DisplayName = "中止"),
	/** 拥有者叫停，或者同一个槽组里放了新动作的蒙太奇；链不再往下走 */
	Cancelled	UMETA(DisplayName = "取消"),
};

/** @enum EGinkgoMissedEvent : 结局时还没发过的事件怎么办 */
UENUM(BlueprintType)
enum class EGinkgoMissedEvent : uint8
{
	/** 结局时按在蒙太奇里的时刻先后补上：拾取的入包、换挂点 */
	CatchUp		UMETA(DisplayName = "补做"),
	/** 不做：开门、投掷的出手 */
	Skip		UMETA(DisplayName = "跳过"),
};

/** @enum EGinkgoHand : 手 */
UENUM(BlueprintType)
enum class EGinkgoHand : uint8
{
	Left	UMETA(DisplayName = "左手"),
	Right	UMETA(DisplayName = "右手"),
};

/**
 * @struct FGinkgoOccupancy : 占用的一条：占哪个部位、优先级（Docs/Design/AnimActions.md「占用」）
 * 标签是层级的，描述 gameplay 上的占用结构，不描述骨骼：拾取最后是手碰东西，占的却是整条胳膊（Occupancy.RightArm）；放松手指才只占手（Occupancy.RightArm.Hand）。
 * 拔枪的片段动了脊柱，也不占脊柱。
 */
struct FGinkgoOccupancy
{
	FGameplayTag Tag;
	int32 Priority = 0;
};

namespace Ginkgo::Occupancy
{
	// 优先级写在一处：放在一起才比得出大小
	/** 持枪：右臂被占着，比它大的都能用 */
	inline constexpr int32 PriorityHold = 0;
	/** 拾取：等于持枪，所以拿着枪时右手不去拾取 */
	inline constexpr int32 PriorityPickup = 0;
	/** 枪上的动作：开火、拉栓、以后的装填。小于拔枪、收枪：它们里面按收枪立刻收（你定的） */
	inline constexpr int32 PriorityGunAction = 3;

	/** 左臂投掷能打断换弹；切枪和攀爬仍能收回投掷物。 */
	inline constexpr int32 PriorityThrow = 4;

	/** 拔枪、收枪 */
	inline constexpr int32 PriorityWeaponSwap = 5;

	/** 翻越/攀爬占用支撑手（短翻越/低攀为左臂，高攀为双臂），优先级高于换弹和切枪；未播放到的武器机构事件按原动作策略跳过。 */
	inline constexpr int32 PriorityTraversal = 10;

	/**
	 * @brief 正占着的 Claim 挡不挡请求 Request
	 * - 同一个标签，或者请求在它下面（它占着右臂，请求右臂.右手）：请求的优先级严格大于才放行，等于也挡
	 * - 它在请求下面（它占着右臂.右手，请求右臂）：它的优先级严格大于请求才挡
	 * - 不是上下级：不挡
	 */
	CASEGINKGO_API bool Blocks(const FGinkgoOccupancy& Claim, const FGinkgoOccupancy& Request);

	/** @brief 两条占用是不是同一个部位或上下级 */
	CASEGINKGO_API bool Overlaps(const FGinkgoOccupancy& A, const FGinkgoOccupancy& B);
}

/** @struct FGinkgoAnimActionResult : 结局与原因 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoAnimActionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AnimAction")
	EGinkgoAnimActionOutcome Outcome = EGinkgoAnimActionOutcome::None;

	/** 原因（AnimAction.Abort.Interrupted 这类）；自然完成时为空 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AnimAction")
	FGameplayTag Reason;

	FGinkgoAnimActionResult() = default;

	explicit FGinkgoAnimActionResult(const EGinkgoAnimActionOutcome InOutcome, const FGameplayTag& InReason = FGameplayTag())
		: Outcome(InOutcome), Reason(InReason)
	{
	}

	static FGinkgoAnimActionResult Completed(const FGameplayTag& InReason = FGameplayTag())
	{
		return FGinkgoAnimActionResult(EGinkgoAnimActionOutcome::Completed, InReason);
	}

	static FGinkgoAnimActionResult Aborted(const FGameplayTag& InReason)
	{
		return FGinkgoAnimActionResult(EGinkgoAnimActionOutcome::Aborted, InReason);
	}

	static FGinkgoAnimActionResult Cancelled(const FGameplayTag& InReason)
	{
		return FGinkgoAnimActionResult(EGinkgoAnimActionOutcome::Cancelled, InReason);
	}

	bool IsFinished() const { return Outcome != EGinkgoAnimActionOutcome::None; }

	/** 日志与调试显示用，例如"Aborted (AnimAction.Abort.Interrupted)" */
	FString ToString() const;
};

/**
 * @struct FGinkgoIKBinding : 手的 IK 目标绑在哪
 * 组件有效时跟着它的插槽（物品的握把、门把手）；否则是角色网格组件空间里的固定变换。
 * 动画实例每帧在 NativeUpdateAnimation 里解析成这一帧的组件空间；权重来自蒙太奇的曲线（HandIK_L、HandIK_R）
 */
struct FGinkgoIKBinding
{
	TWeakObjectPtr<USceneComponent> Component;
	FName Socket;
	FTransform Transform = FTransform::Identity;
};

/**
 * @class IGinkgoMontageHost : 蒙太奇宿主
 * 动画动作的规则（FGinkgoAnimActionRunner）与引擎之间的一层：游戏里是角色的动画实例，测试里是假的。
 * 引擎的事件（通知、窗口、淡出、结束）由宿主转给执行器的 Handle* 函数，带上实例 ID。
 *
 * 实现注意：
 * - PlayMontage 照引擎的规则以打断的方式停掉同一组里原来的蒙太奇（StopAllMontagesByGroupName），
 *   被停掉的那个的淡出回调可能就在 PlayMontage 里面当场回来（在动画 tick 之外调用时引擎就是这样）。
 * - StopMontage 同样以打断的方式停，同样可能当场回调。
 */
class CASEGINKGO_API IGinkgoMontageHost
{
public:
	virtual ~IGinkgoMontageHost() = default;

	/**
	 * @param StartPosition - 从这一刻开始（秒）；0 是从头或从 StartSection
	 * @param BlendInTime - 淡入多久（秒），同一组里原来的蒙太奇同样快淡出；小于 0 用蒙太奇自己的
	 * @return 实例 ID；放不起来返回 INDEX_NONE
	 */
	virtual int32 PlayMontage(UAnimMontage* Montage, float PlayRate, FName StartSection, float StartPosition, float BlendInTime) = 0;

	/** 按淡出时长停掉一个实例（小于 0 时用蒙太奇自己的）；已经停了或不在了什么也不做 */
	virtual void StopMontage(int32 InstanceID, float BlendOutTime) = 0;

	virtual void JumpToSection(int32 InstanceID, FName Section) = 0;

	/** 改 From 这一段放完之后接哪一段 */
	virtual void SetNextSection(int32 InstanceID, FName From, FName To) = 0;

	/** 实例还在不在（淡出中也算在）；看门用 */
	virtual bool IsInstanceAlive(int32 InstanceID) const = 0;

	/** 实例在蒙太奇里的当前时刻（秒）；不在了返回 0 */
	virtual float GetPosition(int32 InstanceID) const = 0;

	/** 蒙太奇所在的槽组 */
	virtual FName GetGroup(const UAnimMontage* Montage) const = 0;

	/** 蒙太奇里一个事件（PlayMontageNotify 的 NotifyName）的时刻；没有这个事件返回负数。补做按它排先后 */
	virtual float GetEventTime(const UAnimMontage* Montage, FName Event) const = 0;
};

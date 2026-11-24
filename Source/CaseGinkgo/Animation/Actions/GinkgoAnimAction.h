#pragma once

#include "CoreMinimal.h"
#include "Animation/Actions/GinkgoAnimActionTypes.h"
#include "UObject/Object.h"
#include "GinkgoAnimAction.generated.h"

class UAnimMontage;
class UGinkgoAnimAction;
class UGinkgoAnimActionChain;
struct FGinkgoAnimActionRunner;

/**
 * @struct FGinkgoAnimActionContext : 动作在回调里能做的事
 * 事件回调、OnTick 与软停拿到可写的，中止条件拿到只读的。
 * 对蒙太奇的操作（跳段、停）先记下来，在执行器的应用阶段做：回调里不直接碰蒙太奇。
 */
struct CASEGINKGO_API FGinkgoAnimActionContext
{
	FGinkgoAnimActionContext(FGinkgoAnimActionRunner& InRunner, UGinkgoAnimAction& InAction)
		: Runner(InRunner), Action(InAction)
	{
	}

	UGinkgoAnimAction& GetAction() const { return Action; }

	/** 执行器的拥有者（角色）；测试里为空 */
	AActor* GetOwner() const;

	/** 蒙太奇里的当前时刻（秒）；没有蒙太奇时为 0 */
	float GetPosition() const;

	/** 蒙太奇里这个事件（PlayMontageNotify）的时刻（秒）；没有这个事件时为负 */
	float GetEventTime(FName Event) const;

	/** 蒙太奇里这个名字的窗口（PlayMontageNotifyWindow）开着 */
	bool IsWindowOpen(FName Window) const;

	/** @brief 中止。只在动作还在跑时有效；它的蒙太奇在应用阶段停掉 */
	void Abort(const FGameplayTag& Reason);

	/** @brief 完成，蒙太奇照常放完。例如事件里判断已经做完了 */
	void Complete(const FGameplayTag& Reason = FGameplayTag());

	/** @brief 跳到一段（应用阶段做）。软停时跳到收尾的一段用它 */
	void JumpToSection(FName Section);

	/** @brief 改一段放完之后接哪一段（应用阶段做）。换弹的循环段压满了改接闭栓 */
	void SetNextSection(FName From, FName To);

	/** @brief 手的 IK：设成新的绑定。自己已经持有这只手时就地换掉，不改在栈里的位置 */
	void SetHandIK(EGinkgoHand Hand, const FGinkgoIKBinding& Binding);

	/** @brief 冻结：不再跟着插槽，停在最后一次解析出来的位置。物品挂到手上的那个事件里用 */
	void FreezeHandIK(EGinkgoHand Hand);

	/** @brief 放掉自己持有的这只手 */
	void ReleaseHandIK(EGinkgoHand Hand);

	/** @brief 持有相机标签：执行器每帧把所有在跑的动作持有的标签并起来写进相机上下文 */
	void HoldCameraTag(const FGameplayTag& Tag);

	void ReleaseCameraTag(const FGameplayTag& Tag);

private:
	FGinkgoAnimActionRunner& Runner;
	UGinkgoAnimAction& Action;
};

/** @struct FGinkgoAnimActionEvent : 一次事件，交给事件回调 */
struct FGinkgoAnimActionEvent
{
	/** 通知的名字（PlayMontageNotify 的 NotifyName） */
	FName Name;

	/** 补做的：蒙太奇没放到这里，结局时补上。补做的拾取只入包，不往手上挂 */
	bool bCaughtUp = false;

	/** 补做时是哪个结局；正常发出时未结束 */
	FGinkgoAnimActionResult Result;

	FGinkgoAnimActionContext& Context;
};

/**
 * @struct FGinkgoAnimActionSetup : 开始时的声明
 * 只在 OnStart 里有效：放哪个蒙太奇，事件与策略，中止条件，完成点，持有的东西，软停怎么收
 */
struct CASEGINKGO_API FGinkgoAnimActionSetup
{
	FGinkgoAnimActionSetup(FGinkgoAnimActionRunner& InRunner, UGinkgoAnimAction& InAction)
		: Action(InAction), Context(InRunner, InAction)
	{
	}

	AActor* GetOwner() const { return Context.GetOwner(); }

	FGinkgoAnimActionContext& GetContext() { return Context; }

	/**
	 * @brief 放这个蒙太奇。不调就是没有蒙太奇的动作，开始后马上完成；调了却传空，算放不起来
	 * @param StartPosition - 从这一刻开始（秒）：比它早的事件按没到过算（Skip 的跳过，CatchUp 的结局时补）
	 * @param BlendInTime - 淡入多久（秒）；小于 0 用蒙太奇自己的
	 */
	void Play(UAnimMontage* Montage, float PlayRate = 1.f, FName StartSection = NAME_None, float StartPosition = 0.f, float BlendInTime = -1.f);

	/**
	 * @brief 事件：蒙太奇里这个名字的通知（PlayMontageNotify）发出时调用
	 * @param Policy - 结局时还没发过的补做还是跳过
	 */
	void On(FName Event, TFunction<void(FGinkgoAnimActionEvent&)> Handler, EGinkgoMissedEvent Policy);

	/**
	 * @brief 中止条件：执行器每帧推进时查，成立就以 Reason 中止
	 * @param Window - 只在这个窗口（PlayMontageNotifyWindow）开着时查；NAME_None 时整个过程都查
	 */
	void AbortIf(FName Window, TFunction<bool(const FGinkgoAnimActionContext&)> Condition, const FGameplayTag& Reason);

	/** @brief 完成点：这个通知发出时就算完成，蒙太奇照常放完。链在这里换下一步 */
	void CompleteAt(FName Event);

	/**
	 * @brief 没有蒙太奇、按时长的动作：占着部位、每帧 OnTick，过了 Seconds 完成（不调 Play 时用；捡枪以后把枪收拢到手上，姿势由别处按进度混）。
	 *        不调它、也不调 Play 的动作开始后马上完成
	 */
	void HoldFor(float Seconds);

	/** @brief 持有手的 IK 绑定：结局时冻结，蒙太奇实例结束才放 */
	void HoldHandIK(const EGinkgoHand Hand, const FGinkgoIKBinding& Binding) { Context.SetHandIK(Hand, Binding); }

	/** @brief 持有相机标签：结局时放 */
	void HoldCameraTag(const FGameplayTag& Tag) { Context.HoldCameraTag(Tag); }

	/** @brief 软停怎么收：返回 true 表示自己收（例如跳到收尾的一段），之后照常结束；不设时软停就是取消 */
	void OnStopRequested(TFunction<bool(FGinkgoAnimActionContext&, const FGameplayTag&)> Handler);

	/** @brief 结局之后（补做与释放之后）调一次，完成、中止、取消都算：动作自己的收尾，例如拔枪没拔出来时换回徒手 */
	void OnFinished(TFunction<void(const FGinkgoAnimActionResult&)> Handler);

	/** @brief 每帧，执行器推进时：要跟着蒙太奇的时刻走的，例如换枪时在手松开枪的那一刻接下一把 */
	void OnTick(TFunction<void(FGinkgoAnimActionContext&, float)> Handler);

private:
	UGinkgoAnimAction& Action;
	FGinkgoAnimActionContext Context;
};

/** @enum EGinkgoAnimActionPhase : 动作走到哪了（执行器内部用） */
enum class EGinkgoAnimActionPhase : uint8
{
	/** 交给执行器了，等应用阶段开始 */
	Pending,
	Running,
	Finished,
};

/** @struct FGinkgoAnimActionEventBinding : 一个事件的绑定 */
struct FGinkgoAnimActionEventBinding
{
	FName Name;
	TFunction<void(FGinkgoAnimActionEvent&)> Handler;
	EGinkgoMissedEvent Policy = EGinkgoMissedEvent::Skip;

	/** 发过几次（循环段里一个事件可以发多次）；补做只补一次都没发过的 */
	int32 FiredCount = 0;
};

/** @struct FGinkgoAnimActionGuard : 一个中止条件 */
struct FGinkgoAnimActionGuard
{
	/** NAME_None 时整个过程都查 */
	FName Window;
	TFunction<bool(const FGinkgoAnimActionContext&)> Condition;
	FGameplayTag Reason;
};

/**
 * @class UGinkgoAnimAction : 动画动作
 * 跟着一个蒙太奇的实例走完一次交互：开始时做决定、选蒙太奇（OnStart），放的过程中按名字收事件、按帧查中止条件。
 * 结局只有一个、只发一次（完成、中止、取消）；持有的东西（手的 IK 绑定、相机标签）在任何结局下都由执行器放掉；
 * 没播到的事件按声明的策略补做或跳过。见 Docs/Design/AnimActions.md。
 *
 * 用法：
 * - 可复用的交互写成子类，覆写 OnStart；一次性的粘合用 Make 传一个 lambda。
 * - 每次执行新建一个，交给执行器（FGinkgoAnimActionRunner::Start）。规则都在执行器里，动作只声明。
 *
 * 实现注意：
 * - lambda 可以捕获动作自己（this）：结局时执行器清掉动作所有的回调，之后不会再调。别的对象只捕获弱指针，动作可能比它们活得久。
 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoAnimAction : public UObject
{
	GENERATED_BODY()

public:
	using FStartFunction = TFunction<void(FGinkgoAnimActionSetup&)>;
	using FStopHandler = TFunction<bool(FGinkgoAnimActionContext&, const FGameplayTag&)>;
	using FFinishHandler = TFunction<void(const FGinkgoAnimActionResult&)>;
	using FTickHandler = TFunction<void(FGinkgoAnimActionContext&, float)>;

	/**
	 * @brief 用 lambda 定义一个动作
	 * @param Outer - 动作的外层，一般是执行器的拥有者；空时放进临时包
	 * @param Name - 调试显示与日志里的名字
	 * @param OnStart - 开始时的声明，同覆写 OnStart
	 */
	static UGinkgoAnimAction* Make(UObject* Outer, FName Name, FStartFunction OnStart);

	/** 调试显示与日志里的名字：设过就用它，否则用类名 */
	FName GetActionName() const;

	void SetActionName(const FName InName) { ActionName = InName; }

	/**
	 * @brief 这个动作占哪个部位（可以占几个，全部通过才开始；两条胳膊的动作写两条）。启动之前声明：执行器在 OnStart 之前看门
	 * @return 自己，接在 Make 后面写
	 */
	UGinkgoAnimAction* Occupy(const FGameplayTag& Tag, int32 Priority);

	TConstArrayView<FGinkgoOccupancy> GetOccupancy() const { return Occupancy; }

	const FGinkgoAnimActionResult& GetResult() const { return Result; }

	bool IsPending() const { return Phase == EGinkgoAnimActionPhase::Pending; }
	bool IsRunning() const { return Phase == EGinkgoAnimActionPhase::Running; }
	bool IsFinished() const { return Phase == EGinkgoAnimActionPhase::Finished; }

	/** 开始时选的蒙太奇；没有蒙太奇的动作为空 */
	UAnimMontage* GetMontage() const { return Montage; }

	/** 蒙太奇的实例 ID；还没放或放不起来时为 INDEX_NONE */
	int32 GetInstanceID() const { return InstanceID; }

	/** 蒙太奇所在的槽组 */
	FName GetGroup() const { return Group; }

	/** 所在的链；不在链里时为空 */
	UGinkgoAnimActionChain* GetParentChain() const { return ParentChain; }

protected:
	/**
	 * @brief 开始：做决定、选蒙太奇，声明事件、中止条件与持有的东西
	 * @note 声明只能在这里做。默认调 Make 给的 lambda
	 */
	virtual void OnStart(FGinkgoAnimActionSetup& Setup);

	/** @brief 每帧，执行器推进时；大多数动作用不上：IK 目标是绑定，动画实例每帧自己解析。默认调 Setup 里设的 */
	virtual void OnTick(FGinkgoAnimActionContext& Context, float DeltaSeconds);

	/**
	 * @brief 软停：拥有者请动作尽快收尾
	 * @return 自己收（例如跳到收尾的一段）返回 true，之后照常结束；返回 false 时执行器取消它。默认调 Setup 里设的，没设返回 false
	 */
	virtual bool OnStopRequested(FGinkgoAnimActionContext& Context, const FGameplayTag& Reason);

	/** @brief 结局之后（补做与释放之后）调用；子类收尾用，通常用不上。默认调 Setup 里设的，覆写时要用它就先调父类 */
	virtual void OnFinished(const FGinkgoAnimActionResult& InResult);

private:
	friend struct FGinkgoAnimActionRunner;
	friend struct FGinkgoAnimActionSetup;
	friend struct FGinkgoAnimActionContext;
	friend class UGinkgoAnimActionChain;

	UPROPERTY()
	FName ActionName;

	/** 开始时选的蒙太奇 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> Montage;

	/** 所在的链 */
	UPROPERTY()
	TObjectPtr<UGinkgoAnimActionChain> ParentChain;

	float PlayRate = 1.f;
	FName StartSection;
	float StartPosition = 0.f;

	/** 淡入多久（Play 给的）；小于 0 用蒙太奇自己的 */
	float BlendInTime = -1.f;

	/** OnStart 里调过 Play：这时蒙太奇是空的算放不起来，而不是没有蒙太奇的动作 */
	bool bPlayRequested = false;

	/** 没有蒙太奇时持续多久（HoldFor）；0 是开始后马上完成 */
	float HoldDuration = 0.f;

	/** 按时长的动作走了多久 */
	float HoldElapsed = 0.f;

	/** 完成点；NAME_None 时在蒙太奇开始自然淡出时完成 */
	FName CompletionEvent;

	FStartFunction StartFunction;
	FStopHandler StopHandler;
	FFinishHandler FinishHandler;
	FTickHandler TickHandler;
	TArray<FGinkgoAnimActionEventBinding> Events;
	TArray<FGinkgoAnimActionGuard> Guards;

	/** 持有的相机标签 */
	FGameplayTagContainer CameraTags;

	/** 占的部位 */
	TArray<FGinkgoOccupancy> Occupancy;

	/** 开着的窗口 */
	TSet<FName> OpenWindows;

	int32 InstanceID = INDEX_NONE;
	FName Group;
	EGinkgoAnimActionPhase Phase = EGinkgoAnimActionPhase::Pending;
	FGinkgoAnimActionResult Result;

	/** 拥有者提过软停：链放完这一步不再往下走 */
	bool bStopRequested = false;
	FGameplayTag StopReason;

	/** 结局时清掉所有的回调，放掉它们捕获的东西 */
	void ClearCallbacks();
};

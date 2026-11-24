#pragma once

#include "CoreMinimal.h"
#include "Animation/Actions/GinkgoAnimActionTypes.h"
#include "UObject/GCObject.h"  // 执行器向 GC 报告在跑的动作

class UGinkgoAnimAction;
class UGinkgoAnimActionChain;
struct FGinkgoAnimActionContext;

/**
 * @struct FGinkgoAnimActionRunner : 动画动作的规则
 * 结局、补做、释放、换段的规则只在这里写一份。执行器组件持有它、动画实例当宿主；测试里用假宿主直接驱动。
 * 见 Docs/Design/AnimActions.md「一个动作的一生」「时序」。
 *
 * 调用时机：
 * - 拥有者（动作状态的 StateTree、调试命令）随时 Start、Cancel、RequestStop：结局当场记下，对蒙太奇的操作留给应用阶段。
 * - Tick 每帧一次，在移动组件之后、网格体之前：先应用，再看门、查中止条件、调 OnTick，有了新结局再应用一次。
 * - 宿主把引擎的事件转给 Handle*：这里只记结局，不碰蒙太奇。
 *
 * 实现注意：
 * - 放与停只在应用阶段（Apply）。引擎在动画 tick 之外停蒙太奇时当场回调淡出事件，放新的会以打断的方式停掉同组旧的，
 *   所以先把上一步标成结束、再放下一步：同步回来的回调认出实例已经结束，忽略。
 * - 一个槽组一个动作（引擎一个组只留一个蒙太奇）：新动作的蒙太奇放进哪个组，就取消那个组里在跑的动作（Replaced），
 *   它的蒙太奇由新的顶掉；中止、取消要停的蒙太奇，这次应用里同组放了新的就不另停。完成的不停，让它自己放完。
 * - 占用（Ginkgo::Occupancy）看门在前面：Start 时就判断、就登记，还没应用的动作也算占着；通过了就把比它低的、盖得住的动作取消（Preempted）。
 *   外部（持枪状态、玩法逻辑）也能占。上面那条槽组的规则留在底下：能同时放的动作必须放在不同的组，占用不重叠的两个动作在同一组是配错了（ensure）。
 * - 动作是 UObject，这里是 FGCObject，向 GC 报告在跑的动作与还没应用的命令。
 */
struct CASEGINKGO_API FGinkgoAnimActionRunner : public FGCObject
{
	/** 结局时调一次（取消也算） */
	using FFinished = TFunction<void(UGinkgoAnimAction&, const FGinkgoAnimActionResult&)>;

	/** @struct FHandHold : 手的栈里的一层 */
	struct FHandHold
	{
		TWeakObjectPtr<UGinkgoAnimAction> Owner;
		FGinkgoIKBinding Binding;

		/** 冻结：动画实例用最后一次解析出来的组件空间位置，不再跟着插槽 */
		bool bFrozen = false;

		/** 结局之后等这个蒙太奇实例结束再放：曲线随蒙太奇淡出，权重降到 0 之前目标不能没有。INDEX_NONE 表示还在持有 */
		int32 ReleaseWithInstance = INDEX_NONE;
	};

	explicit FGinkgoAnimActionRunner(IGinkgoMontageHost& InHost, AActor* InOwner = nullptr);

	// ===== 拥有者 =====

	/**
	 * @brief 启动一个动作或链：记下来，在下一次应用时开始
	 * @param OnFinished - 结局时调一次（取消也算）
	 * @return 占用不让时假：不开始（OnStart 不调），当场结局，中止、原因 AnimAction.Abort.Occupied
	 */
	bool Start(UGinkgoAnimAction* Action, FFinished OnFinished = nullptr);

	/** @brief 取消（硬停）：结局当场记下，链不往下走；它的蒙太奇在应用阶段按蒙太奇自己的淡出停掉 */
	void Cancel(UGinkgoAnimAction* Action, const FGameplayTag& Reason);

	/** @brief 软停：在应用阶段问正在放的那一步怎么收，不自己收就取消；之后链不再往下走 */
	void RequestStop(UGinkgoAnimAction* Action, const FGameplayTag& Reason);

	/** @brief 每帧推进：应用，看门、中止条件、OnTick，再应用 */
	void Tick(float DeltaSeconds);

	/** 在移动模拟边界提交排队命令，不推进动作计时或重复 OnTick；Apply 自带重入保护。 */
	void FlushPending() { Apply(); }

	// ===== 占用 =====

	/** @brief 这个部位能不能占：占着的（动作的与外部的）都不挡才行 */
	bool CanOccupy(const FGameplayTag& Tag, int32 Priority) const;

	/**
	 * @brief 外部占一个部位（状态、玩法逻辑）：不判断能不能占，比它低、盖得住的动作取消。等于它的动作照常放完
	 * @return 句柄，ReleaseOccupancy 用
	 */
	int32 Occupy(const FGameplayTag& Tag, int32 Priority);

	void ReleaseOccupancy(int32 Handle);

	// ===== 引擎的事件（宿主转进来，带实例 ID） =====

	/** 蒙太奇里的通知（PlayMontageNotify）：事件，或者完成点 */
	void HandleNotify(int32 InstanceID, FName Name);

	/** 窗口（PlayMontageNotifyWindow）开始与结束 */
	void HandleWindowBegin(int32 InstanceID, FName Name);
	void HandleWindowEnd(int32 InstanceID, FName Name);

	/** 开始淡出：打断的算中止（Interrupted），自然淡出的算完成 */
	void HandleBlendingOut(int32 InstanceID, bool bInterrupted);

	/** 实例结束：没收到淡出就结束的，同样的结局；等它结束的手的 IK 在这里放 */
	void HandleEnded(int32 InstanceID, bool bInterrupted);

	// ===== 读 =====

	/** 手的 IK：栈顶那一层；没有返回空 */
	const FHandHold* GetHandIK(EGinkgoHand Hand) const;

	/** 所有在跑的动作持有的相机标签，并起来 */
	FGameplayTagContainer GetCameraTags() const;

	/** 拥有者启动、还没结束的动作（链算一个） */
	TArray<UGinkgoAnimAction*> GetActions() const;

	/** 这个槽组里正在放蒙太奇的动作；没有返回空 */
	UGinkgoAnimAction* GetActionInGroup(FName Group) const;

	/** 在跑的叶子动作：链往下找到正在放蒙太奇的那一步 */
	TArray<UGinkgoAnimAction*> GetRunningLeaves() const;

	AActor* GetOwner() const { return Owner.Get(); }

#if !UE_BUILD_SHIPPING
	/** 调试：上次取走以后发过的事件与结局，如"DrawWeapon:InHand"、"DrawWeapon=Completed"；回放轨迹与调试显示每帧取一次 */
	TArray<FString> TakeDebugLog();

	/** 调试：现在的占用，如"RightArm=0|RightArm=5:DrawWeapon|LeftArm=0:Pickup"；回放轨迹与调试显示用 */
	FString GetOccupancyText() const;
#endif

	// ===== FGCObject =====

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FGinkgoAnimActionRunner"); }

private:
	friend struct FGinkgoAnimActionContext;

	/** 应用阶段要做的事 */
	struct FCommand
	{
		enum class EType : uint8
		{
			Start,
			RequestStop,
			/** 链里一步结束了：按转换走下一步 */
			Advance,
			StopMontage,
			JumpToSection,
			SetNextSection,
		};

		EType Type = EType::Start;
		TObjectPtr<UGinkgoAnimAction> Action;

		/** Advance：结束的那一步的结局 */
		FGinkgoAnimActionResult Result;

		/** RequestStop：拥有者给的原因 */
		FGameplayTag Reason;

		int32 InstanceID = INDEX_NONE;
		FName Group;
		FName Section;
		FName NextSection;
	};

	/** 拥有者启动的一个动作 */
	struct FRoot
	{
		TObjectPtr<UGinkgoAnimAction> Action;
		FFinished OnFinished;
	};

	/** 结局时它的蒙太奇怎么办 */
	enum class EMontageEnd : uint8
	{
		/** 不动：自然淡出、被打断、被替换、实例丢了、完成点 */
		Keep,
		/** 应用阶段停掉，除非这次应用里同组放了新的：自己中止、拥有者取消 */
		Stop,
	};

	IGinkgoMontageHost& Host;
	TWeakObjectPtr<AActor> Owner;
	TArray<FRoot> Roots;
	TArray<FCommand> Commands;

	/** 两只手的栈，下标是 EGinkgoHand */
	TArray<FHandHold> Hands[2];

	/** 占着的一条：动作的（到它结局）或外部的（Action 为空，到 ReleaseOccupancy） */
	struct FOccupant
	{
		FGinkgoOccupancy Claim;
		TWeakObjectPtr<UGinkgoAnimAction> Action;
		int32 Handle = INDEX_NONE;
	};

	TArray<FOccupant> Occupants;
	int32 NextOccupancyHandle = 1;

	/** 这一次应用里放过蒙太奇的槽组：要停的蒙太奇在这些组里时已经被新的顶掉，不另停 */
	TSet<FName> PlayedGroups;

	bool bApplying = false;

#if !UE_BUILD_SHIPPING
	TArray<FString> DebugLog;
#endif

	/** 调试日志记一条；发行版里什么也不做 */
	void AddDebugLog(const UGinkgoAnimAction& Action, const TCHAR* Separator, const FString& What);

	/** 看门、登记、取消比它低的：同一棵（链与它的步）不互相挡。不通过时什么也不改 */
	bool TryOccupy(UGinkgoAnimAction& Action);

	/** 把比 Claims 低、盖得住的动作取消，Except 这一棵除外 */
	void TakeOver(TConstArrayView<FGinkgoOccupancy> Claims, const UGinkgoAnimAction* Except);

	void ReleaseOccupancy(const UGinkgoAnimAction& Action);

	void Apply();
	void Enter(UGinkgoAnimAction& Action);
	void EnterStep(UGinkgoAnimActionChain& Chain, int32 Index);
	void Advance(UGinkgoAnimActionChain& Chain, const FGinkgoAnimActionResult& StepResult);
	void HandleStopRequest(UGinkgoAnimAction& Root, const FGameplayTag& Reason);

	/** 叶子动作（放蒙太奇的）的结局：标记、补做、释放、记下要停的蒙太奇、报告。只发一次 */
	void FinishLeaf(UGinkgoAnimAction& Leaf, FGinkgoAnimActionResult InResult, EMontageEnd MontageEnd);

	/** 链的结局：路径上第一个没完成的步的结局，都完成了就是完成 */
	void FinishChain(UGinkgoAnimActionChain& Chain);

	/** 取消一整棵：先取消正在放的那一步（它要求的推进到时会被忽略），再结束链 */
	void CancelTree(UGinkgoAnimAction& Action, const FGinkgoAnimActionResult& InResult, EMontageEnd MontageEnd);

	/** 报告结局：链里的步交给链去推进，拥有者启动的调回调 */
	void Report(UGinkgoAnimAction& Action, const FGinkgoAnimActionResult& InResult);

	void FireEvent(UGinkgoAnimAction& Leaf, FName Name);
	void CatchUp(UGinkgoAnimAction& Leaf);
	void ReleaseHolds(UGinkgoAnimAction& Leaf);

	void QueueJumpToSection(UGinkgoAnimAction& Action, FName Section);
	void QueueSetNextSection(UGinkgoAnimAction& Action, FName From, FName To);

	void SetHandIK(UGinkgoAnimAction& Action, EGinkgoHand Hand, const FGinkgoIKBinding& Binding);
	void FreezeHandIK(UGinkgoAnimAction& Action, EGinkgoHand Hand);
	void ReleaseHandIK(UGinkgoAnimAction& Action, EGinkgoHand Hand);

	/** 正在放这个实例的叶子动作；结束了的不算 */
	UGinkgoAnimAction* FindRunningLeaf(int32 InstanceID) const;

	/** 一直往下找链的当前步，直到放蒙太奇的那一个 */
	static UGinkgoAnimAction* GetLeaf(UGinkgoAnimAction* Action);

	/** 一直往上找到拥有者启动的那一个 */
	static UGinkgoAnimAction& GetRoot(UGinkgoAnimAction& Action);

	/** 自己或上面的链提过软停 */
	static bool IsStopRequested(const UGinkgoAnimAction& Action, FGameplayTag* OutReason = nullptr);
};

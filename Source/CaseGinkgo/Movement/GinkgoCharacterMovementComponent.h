#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GinkgoMovementNetworking.h"  // FGinkgoMoveResponseDataContainer
#include "GinkgoMovementSimState.h"
#include "Movement/Windows/GinkgoMovementWindowManager.h"
#if !UE_BUILD_SHIPPING
#include "Utilities/Debug/DebugRollingCurve.h"  // ginkgo.showMovementCurve
#endif
#include "GinkgoCharacterMovementComponent.generated.h"

class UCanvas;
class UGinkgoCameraContextComponent;
class UGinkgoMovementConfig;

/**
 * @class UGinkgoCharacterMovementComponent : Source 式移动组件
 * 用 Source 的"投影加速"替换 CMC 默认的速度计算；二段跳、空中冲刺、滑铲、墙跑、钩锁（后续里程碑）共用这一套动量规则。
 * 设计见 Docs/Design/MovementComponent.md。
 *
 * 关键改动：
 * 1. CalcVelocity：Walking 走"摩擦 + 地面加速"（投影加速，且转向不让速度超过 wishspeed），Falling 走空中 Accelerate，
 *    其他模式交给父类；保留父类开头的根运动与模拟代理守卫
 * 2. GetFallingLateralAcceleration：原样返回水平输入加速度，不乘 AirControl
 * 3. GetMaxSpeed：按冲刺、蹲伏返回 wishspeed
 * 4. 落地免摩擦窗口：落地后一小段时间不施加地面摩擦，连跳不掉速
 * 5. 蹲着也能跳（同 Source；滑铲接跳也依赖这一点）
 * 6. 滑铲：Walking 里的子状态，不是自定义模式。地面跟随、台阶与斜坡照用 PhysWalking，只换速度计算：
 *    开始时给一次起步加速，之后两项减速（解析解推进，与帧率无关），输入去掉向前的分量，只能转向和刹车
 * 7. 窗口：起跳、落地、未起跳离地、滑铲开始与结束时发出事件，交给窗口管理器推入、切换与结束窗口；每次移动结束时按 Δt 推进。
 *    起步加速与跳跃高度乘窗口给的倍率（Movement.Window.Effect.SlideBoost、JumpPenalty）。
 *    键盘方向键按下是输入事件，在每次移动开始时发出，lurch 窗口在这里改速度
 * 8. 朝向：平时面向视角，在地面上冲刺时面向速度。偏航在 PhysicsRotation 里定（ComputeOrientToMovementRotation），
 *    每次移动之后一帧转到位，客户端预测、服务器执行与重放走同一份代码；角色不再按控制器转（bUseControllerRotationYaw 关掉）
 *
 * 9. 相机上下文（UGinkgoCameraContextComponent，本地控制的角色）：每次移动后覆盖写移动的标签与连续量；滑铲开始、落地在第一次模拟时入队，
 *    带上发生在帧里的哪一刻（移动的开头，落地时是着地的那一刻），相机的冲量弹簧按它拆开推进。客户端重放不发；发过相机事件的移动不合并
 *
 * 预测：冲刺输入走压缩标志位，方向键按下的计数走扩展的移动数据；模拟产生的状态（落地计时、滑铲、窗口、处理过的按下计数）
 * 收在 FGinkgoMovementSimState 里，存档记移动开始时的快照、
 * 客户端合并移动时放回、校正时随服务器的回包下发，等引擎应用完校正（包括切换移动模式）再整体放回；窗口有变化的移动不合并；
 * 胶囊在引擎应用校正之前先换成服务器那时的大小，校正不会跨过蹲下或站起的那一刻（见 GinkgoMovementNetworking.h）。
 * 客户端预测与服务器回放执行同一份代码。
 *
 * 参数全部来自 UGinkgoMovementConfig，每帧读取，PIE 中改资产立即生效。
 */
UCLASS()
class CASEGINKGO_API UGinkgoCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGinkgoCharacterMovementComponent();

	//~ Begin UCharacterMovementComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void Crouch(bool bClientSimulation = false) override;
	virtual void UnCrouch(bool bClientSimulation = false) override;
	virtual void PhysicsRotation(float DeltaTime) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual FVector GetFallingLateralAcceleration(float DeltaTime) override;
	virtual float GetMaxSpeed() const override;
	virtual float GetGravityZ() const override;
	virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
	virtual bool CanAttemptJump() const override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void UpdateCharacterStateAfterMovement(float DeltaSeconds) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;
	virtual FRotator ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const override;
protected:
	virtual void PerformMovement(float DeltaTime) override;
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations) override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity,
		FMovementBaseInterfaceData* NewMovementBaseInterfaceData, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode,
		FVector ServerGravityDirection) override;
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLocation,
		FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	//~ End UCharacterMovementComponent

public:
	/** 进入攀爬根运动模式，Facing 为起步世界朝向；站起失败时不缩小或禁用胶囊。 */
	bool BeginTraversalMovement(const FQuat& Facing);

	/** 动作结束后从当前位置恢复走路或下落；不将角色传送到目标或起点。 */
	void EndTraversalMovement();

	/** 当前由攀爬蒙太奇驱动，普通加速与朝向输入不参与。 */
	bool IsTraversing() const { return MovementMode == MOVE_Custom && CustomMovementMode == 1; }

	/** 本次根运动实际 Sweep 受阻；移动线程流程只记下结果，由动作组件在移动之后取消。 */
	bool IsTraversalBlocked() const { return bTraversalBlocked; }

	/** 服务器检查该次移动误差时锁定的攀爬状态，和同一回包的位置/速度严格对应。 */
	const FGinkgoTraversalSyncState& GetPendingAdjustmentTraversal() const { return PendingAdjustmentTraversal; }

	/**
	 * @brief 设置冲刺输入（按住为真）
	 * @note 只影响 wishspeed；联网时经压缩标志位 FLAG_Custom_0 上报（FGinkgoSavedMove）
	 */
	void SetWantsToSprint(const bool bInWantsToSprint) { bWantsToSprint = bInWantsToSprint; }

	bool WantsToSprint() const { return bWantsToSprint; }

	/**
	 * @brief 在地面上冲刺：按着冲刺、没有蹲下、在地面上。这时角色面向速度，不面向视角
	 * @note 武器系统据此挡住开镜（冲刺时不能开镜）。冲刺优先，开镜在武器侧挡住，不碰移动的预测。
	 *       模拟代理上恒为假：冲刺的输入不复制给它，它的朝向随复制的旋转来
	 */
	bool IsSprintFacing() const { return bWantsToSprint && IsMovingOnGround() && !IsCrouching(); }

	/**
	 * @brief 服务器校正（连同之后的重放）一共把胶囊挪了多少，累计值
	 * @note 引擎不平滑本地角色，校正的那一帧胶囊一下子跳过去。动画逐帧差分可见的根时按帧取它的差，从位移里扣掉（UGinkgoAnimInstance）。
	 *       只在本地控制的角色的客户端上变：收到校正时（引擎换位置之前）记下胶囊在哪里，重放完再看它到了哪里
	 */
	const FVector& GetCorrectionOffsetTotal() const { return CorrectionOffsetTotal; }

	/**
	 * @brief 键盘方向键按下（lurch 的输入），计数加上这么多次
	 * @note 发给服务器的是计数（一个字节，循环），不是"这次移动按下了"：按下的那次移动丢了，下一次移动带着同样的计数到达，
	 *       服务器在那次移动开始时照样处理。计数与处理过的计数（模拟状态）不同时，移动开始时发出"方向键按下"事件
	 */
	void AddDirectionPresses(const uint8 Presses) { DirectionPressCount += Presses; }

	/** 方向键按下的计数：输入，进扩展的移动数据（FGinkgoNetworkMoveData） */
	uint8 GetDirectionPressCount() const { return DirectionPressCount; }

	/**
	 * @brief 按着的键盘方向键合起来的朝向：X 向右、Y 向前，各为 −1、0、1；同一条轴上两个反方向的键都按着时后按的那个赢（控制器合好）
	 * @note lurch 的方向用它，不用移动输入：移动输入把反方向的两个键相加抵消，先按下新的键、后松开原来的键时，按下那一次移动的输入里
	 *       没有新的方向。打包成 4 位（Ginkgo::Movement::PackDirectionKeys）进扩展的移动数据
	 */
	void SetDirectionKeys(FIntPoint Keys);

	/** 方向键的朝向，打包的 4 位 */
	uint8 GetPackedDirectionKeys() const { return DirectionKeys; }

	/**
	 * @brief 持枪与开镜的地速倍率（Docs/Design/Weapons.md「移动：持枪与开镜的地速」）：武器组件每帧给，量化以后进扩展的移动数据
	 * @note 是输入，同冲刺：移动不读武器组件，也不认识枪；服务器与客户端重放时由那次移动的数据给出。不是枪的角色（AI、别的）一直是 1
	 */
	void SetSpeedScales(float HeldScale, float AdsScale);

	/** 持枪的地速倍率，量化过的字节 */
	uint8 GetPackedHeldSpeedScale() const { return HeldSpeedScale; }

	/** 开镜的地速倍率，量化过的字节 */
	uint8 GetPackedAdsSpeedScale() const { return AdsSpeedScale; }

	/** 现在的跑速：配置的跑速乘持枪倍率，不乘开镜倍率。滑铲 FOV 的"低于跑速"、动画的倾斜比重按它 */
	float GetHeldRunSpeed() const;

	/** 现在的冲刺速度：配置的冲刺速度乘持枪倍率 */
	float GetHeldSprintSpeed() const;

	/** 地面上跑速乘的总倍率（持枪乘开镜）：动画按它与手柄的推杆大小选走还是跑 */
	float GetGroundSpeedRatio() const;

	/** 当前生效的移动配置 */
	const UGinkgoMovementConfig& GetConfig() const;

	/** 模拟产生的状态：落地计时、滑铲、窗口 */
	const FGinkgoMovementSimState& GetSimState() const { return SimState; }

	/** 服务器上：父类把一次移动记成待发的确认或校正时，那次移动之后的模拟状态；回包带的是它（见 ServerMoveHandleClientError） */
	const FGinkgoMovementSimState& GetPendingAdjustmentSimState() const { return PendingAdjustmentSimState; }

	/** 服务器上：同一刻是否蹲着（胶囊是蹲下的大小）；校正时客户端先换成同样的胶囊 */
	bool IsPendingAdjustmentCrouched() const { return bPendingAdjustmentCrouched; }

	/**
	 * @brief 整体换掉模拟产生的状态，并按新的窗口重建汇总标签
	 * @note 只用来退回到某次移动开始时（客户端合并移动）、换成服务器给的状态（校正）或放回开始状态（调试回放），不要用来改模拟。
	 *       不算作窗口变化：不发表现层事件，也不改变化的累计次数
	 */
	void RestoreSimState(const FGinkgoMovementSimState& State);

	/** 落地后经过的时间（秒），超过 FGinkgoMovementSimState::LandingTimeLimit 后不再增长 */
	float GetTimeSinceLanded() const { return SimState.TimeSinceLanded; }

	/** 正在滑铲：Walking 里的子状态，碰撞体是下蹲的碰撞体 */
	bool IsSliding() const { return SimState.bSliding; }

	/** 这次滑铲已经滑了多久（秒） */
	float GetSlideTime() const { return SimState.SlideTime; }

	/** 这次滑铲的起步加速（cm/s），没拿到加速时为 0 */
	float GetSlideBoost() const { return SimState.SlideBoost; }

	// ===== 窗口 =====

	/** 窗口定义：移动配置里的窗口列表，模拟状态里的下标指向它 */
	FGinkgoMovementWindowManager::FDefinitions GetWindowDefinitions() const;

	/** 窗口的汇总标签：活动窗口与它们当前阶段的标签，窗口每次变化后重建 */
	const FGameplayTagContainer& GetWindowTags() const { return WindowManager.GetTags(); }

	/** 窗口给出的能力（Movement.Window.Ability.*） */
	bool HasWindowAbility(const FGameplayTag& Ability) const;

	/** 窗口在一个类别上的倍率（Movement.Window.Effect.*），没有贡献时为 1 */
	float GetWindowMultiplier(const FGameplayTag& Category) const;

	/**
	 * @brief 下落中的第一跳仍算第一跳（AGinkgoCharacter::CanJumpWhileFalling 转到这里）
	 * @note 窗口给出 Movement.Window.Ability.JumpWhileFalling（土狼时间）时为真；正在用它起跳时也为真，因为起跳事件已经结束了那个窗口，
	 *       父类的 DoJump 还要再查一次。父类默认把下落中的第一跳当成第二段跳，JumpMaxCount 为 1 时就跳不起来。
	 *       窗口是模拟状态，客户端预测、服务器回放与校正后的重放判断一样
	 */
	bool CanJumpWhileFalling() const;

	/** 窗口变化的累计次数：存档在移动开始与结束时各记一次，这次移动里窗口有变化就不与别的合并 */
	uint32 GetWindowChangeCount() const { return WindowManager.GetChangeCount(); }

	/**
	 * 发给相机上下文的事件的累计次数：存档在移动开始与结束时各记一次，这次移动里发过就不与别的合并。
	 * 合并后从被合并那次移动的开头重算，同一个事件会再发一遍；窗口有变化的移动本来就不合并，这里不靠窗口的定义（没有滑铲疲劳时滑铲开始也不改窗口）
	 */
	uint32 GetCameraEventCount() const { return CameraEventCount; }

	/**
	 * 表现层事件：窗口推入（FromPhase 为 -1）、换阶段、结束（ToPhase 为 -1），再次推入刷新时 ToPhase 为 0。
	 * 只在第一次模拟这次移动时发出，客户端重放时只改状态、不发；一次性的镜头、音效接这里，否则每次校正都会重播一遍。
	 * 校正整体换成服务器的状态时也不发：客户端预测推入、服务器却没有的窗口，不会收到结束。所以持续的表现（窗口期间的镜头效果）
	 * 每帧读 GetWindowTags，不要靠这里的推入与结束配对
	 */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnMovementWindowChanged, const UGinkgoMovementWindow* /*Definition*/, int32 /*FromPhase*/, int32 /*ToPhase*/);
	FOnMovementWindowChanged OnMovementWindowChanged;

	/**
	 * 表现层事件：一次窗口事件（起跳、落地等）与它引起的窗口变化，在 OnMovementWindowChanged 发完这些变化之后发出。
	 * 同样只在第一次模拟这次移动时发；没有引起变化的事件也发，按时长推进引起的变化不经过这里。
	 * 窗口时间轴（ginkgo.showWindowTimeline）用它把事件与它推入、结束的窗口连起来；落地音效这类一次性的表现也可以接这里
	 */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMovementWindowEvent, const FGinkgoWindowEvent& /*Event*/, TConstArrayView<FGinkgoMovementWindowManager::FChange> /*Changes*/);
	FOnMovementWindowEvent OnMovementWindowEvent;

	/**
	 * 客户端收到一次生效的服务器校正（本地控制的角色），发出时引擎还没换位置、模拟状态也还没放回。
	 * PositionError 是被校正那次移动结束时客户端与服务器的位置差（cm），基于底座的相对位置时为 -1。窗口时间轴在这一刻画一道校正线
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMovementCorrected, float /*PositionError*/);
	FOnMovementCorrected OnMovementCorrected;

#if !UE_BUILD_SHIPPING
	/** 客户端收到的服务器校正次数（本地控制的角色在客户端上才会增长），用来验证预测；发行版不统计 */
	int32 GetCorrectionCount() const { return CorrectionCount; }

	/** 客户端合并过的移动次数（存档把上一次还没发出的移动并进这一次），用来确认合并确实发生了；发行版不统计 */
	int32 GetCombinedMoveCount() const { return CombinedMoveCount; }

	/**
	 * @brief 存档合并时调用（FGinkgoSavedMove::CombineWith），只计数
	 * @param RecombinedWindowChanges - 被合并那次移动里的窗口变化次数：合并后重算时会再发生一遍。
	 *        平时总为 0（这样的移动不合并），只在 ginkgo.net.noCombineOnWindowChange 0 对照时不为 0
	 */
	void NotifyMovesCombined(const uint32 RecombinedWindowChanges)
	{
		++CombinedMoveCount;
		RecombinedWindowChangeCount += RecombinedWindowChanges;
	}

	/** 合并后重算时又发生一遍的窗口变化次数，见 NotifyMovesCombined */
	int32 GetRecombinedWindowChangeCount() const { return RecombinedWindowChangeCount; }

	/** 客户端拿服务器的状态比对过多少次（ginkgo.net.verifySimState，服务器在确认好移动的回包里带上模拟状态） */
	int32 GetSimStateCheckCount() const { return SimStateCheckCount; }

	/** 其中两端不一致的次数：滑铲与否不同，或计时差超过 1 ms，或起步加速差超过 1 cm/s，或蹲伏与否不同 */
	int32 GetSimStateMismatchCount() const { return SimStateMismatchCount; }

	/** 比对中见到的最大计时差（秒），容差内的也算 */
	float GetMaxSimStateTimeDifference() const { return MaxSimStateTimeDifference; }

	/** 发给表现层的窗口变化次数（OnMovementWindowChanged） */
	int32 GetDispatchedWindowChangeCount() const { return DispatchedWindowChangeCount; }

	/** 客户端重放时丢掉、没有发给表现层的窗口变化次数 */
	int32 GetReplayedWindowChangeCount() const { return ReplayedWindowChangeCount; }
#endif

protected:
	/** 移动配置资产。没有指定时用 项目设置 → Game → CaseGinkgo 里的；都没有时用 UGinkgoMovementConfig 的类默认值 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ginkgo|Movement")
	TObjectPtr<UGinkgoMovementConfig> MovementConfig;

private:
	/** 当前攀爬的身体路径发生阻挡；BeginTraversalMovement 清零，动作在移动完成后读取。 */
	bool bTraversalBlocked = false;

	/** 地面：先摩擦（落地免摩擦窗口内跳过），再按 wishspeed 做地面加速 */
	void CalcGroundVelocity(float DeltaTime);

	/** 空中：没有摩擦，只限制输入方向上的分量，所以没有速度上限，只有转向限制 */
	void CalcAirVelocity(float DeltaTime);

	/** 滑铲：先减速（落地免摩擦窗口内跳过），再按去掉向前分量的输入转向或刹车，速度大小不会因转向变大 */
	void CalcSlideVelocity(float DeltaTime, bool bInLandingGrace);

	/**
	 * @brief 能不能开始滑铲：按着蹲且已经蹲下、站在可走的地面上、速度够快、方向允许
	 * @param LandingFallSpeed - 落地时的下落速度；为空时是每次移动开始时的判断，按地面进入的速度门槛
	 */
	bool CanStartSlide(TOptional<float> LandingFallSpeed) const;

	/** 开始滑铲：起步加速，发出"滑铲开始" */
	void StartSlide();

	/**
	 * @brief 结束滑铲，速度不变，发出"滑铲结束"
	 * @note 松开蹲、离开地面、速度降到结束速度以下时调用；不在滑铲时什么也不做
	 */
	void EndSlide();

	/** 起步加速的倍率：窗口在 Movement.Window.Effect.SlideBoost 类别上给出的倍率，滑铲疲劳在这里给 0 */
	float GetSlideBoostScale() const;

	/**
	 * 发出窗口事件"滑铲开始"，带上这次的起步加速。
	 * 属于模拟，服务器回放与客户端重放都会走到这里；镜头、音效这类表现层反馈接 OnMovementWindowChanged，不要放在这里
	 */
	void NotifySlideStarted(float Boost);

	/** 这次移动的视角：客户端重放旧的移动时取那次移动存下的视角，而不是现在的（重放过的存档仍保留记录时的视角，见 FGinkgoSavedMove::PostUpdate） */
	FRotator GetMoveControlRotation() const;

	/** @return 模型的基准高度在世界里的位置；没有角色时 0 */
	double GetMeshBaseHeight() const;

	/**
	 * @brief 胶囊换完大小：模型的基准高度在世界里跳了多少，交给角色平滑（Docs/Design/MovementComponent.md「空中蹲下时的模型」）
	 * @param HeightBefore - 换之前的 GetMeshBaseHeight
	 * @note 模拟代理、监听服务器上的远端玩家不算：引擎自己把这一下记进网络平滑的偏移。校正前换胶囊（SetCrouchedInPlace）与重放也不算：
	 *       重放完胶囊还是原来的大小，算进去模型会无端晃一下
	 */
	void AbsorbMeshBaseJump(double HeightBefore, bool bClientSimulation);

	/**
	 * @brief 换成蹲下或站立的胶囊，不移动位置、不检查有没有空间
	 * @note 只用来在应用校正前，把胶囊换成服务器在被校正那次移动结束时的大小；平时蹲下与站起走父类的 Crouch 与 UnCrouch
	 */
	void SetCrouchedInPlace(bool bCrouched);

	/**
	 * @brief 发出一次窗口事件：交给管理器推入、切换与结束窗口，行为改过的速度写回
	 * @note 模拟代理不处理：它的状态来自复制
	 */
	void HandleWindowEvent(EGinkgoWindowEvent Type, float Value = 0.f, bool bFlag = false);

	/** 把从管理器取走的窗口变化交给表现层（OnMovementWindowChanged）；客户端重放时丢掉 */
	void DispatchWindowChanges(TConstArrayView<FGinkgoMovementWindowManager::FChange> Changes);

	/**
	 * @brief 方向键按下的计数与处理过的不同时，发出一次"方向键按下"事件，记下新的计数
	 * @param DeltaTime - 这次移动的 Δt，只进调试日志：服务器上比客户端的一帧长，就是前面丢了移动、那段时间并进了这一次
	 * @note 每次移动开始时调用（UpdateCharacterStateBeforeMovement）：起跳之后、按这次移动的输入算速度之前。
	 *       这次移动的加速度此时已经定好（客户端是发给服务器的取整值），两端与重放用的是同一个方向
	 */
	void HandleDirectionPresses(float DeltaTime);

	/**
	 * @brief 方向键的朝向转到世界的水平方向（单位向量，没按方向键时为零）
	 * @note 视角：服务器与客户端重放取这次移动的数据里的，客户端第一次模拟与单机取现在的；再按网络上的精度压一遍
	 *       （Ginkgo::Movement::DirectionKeysToWorld），几处算出来逐位相同
	 */
	FVector GetDirectionKeysWorld() const;

	/** 客户端正在重放旧的移动：只改状态，不发表现层事件 */
	bool IsClientReplaying() const;

	/**
	 * @brief 每次移动后覆盖写相机上下文：标签（在地面上、滑铲、冲刺，加上窗口的汇总标签）与连续量（水平速度、这次滑铲的起步加速、移动配置的跑速）
	 * @note 只在本地控制的角色上（相机只在本地算）。客户端校正后重放的移动也写，留下的是最后一次的
	 */
	void WriteCameraContext();

	/**
	 * @brief 一次窗口事件第一次模拟时，给相机上下文发对应的相机事件（滑铲开始、落地）
	 * @note 时刻是这次移动的开头；在着地的处理里（ProcessLanded）是着地的那一刻：世界时间减去这次移动剩下的时间
	 */
	void PushCameraEvent(const FGinkgoWindowEvent& Event);

#if !UE_BUILD_SHIPPING
	/** 一次发给表现层的窗口变化，调试面板列出最近几次 */
	struct FWindowChangeRecord
	{
		double Time = 0.0;
		FGameplayTag Tag;
		int32 FromPhase = INDEX_NONE;
		int32 ToPhase = INDEX_NONE;
	};

	/** 每帧移动之后：ginkgo.showMovementInfo 的箭头与文字，ginkgo.showMovementCurve 的采样（实现在 _Debug.cpp） */
	void TickMovementDebug();

	/** 画布上的调试显示：ginkgo.showMovementCurve 的水平速度曲线，ginkgo.showMovementWindows 的窗口面板 */
	void DrawMovementDebug(UCanvas* Canvas, APlayerController* PlayerController) const;

	void DrawMovementCurve(UCanvas& Canvas) const;

	/** 活动窗口的阶段与进度、汇总标签、最近几次发给表现层的变化 */
	void DrawMovementWindows(UCanvas& Canvas) const;

	/** 发给表现层的一次窗口变化，记给调试面板 */
	void RecordWindowChangeForDebug(const UGinkgoMovementWindow* Definition, const FGinkgoMovementWindowManager::FChange& Change);

	/** 服务器确认了一次好移动、并带上了它的模拟状态时：与存档里那次移动结束时的状态比对 */
	void VerifyAckedSimState();

	/** 水平速度（u/s）随时间的曲线，按移动模式着色 */
	FDebugRollingCurve SpeedCurve;

	FDelegateHandle DebugDrawHandle;

	int32 CorrectionCount = 0;

	int32 CombinedMoveCount = 0;

	int32 SimStateCheckCount = 0;

	int32 SimStateMismatchCount = 0;

	float MaxSimStateTimeDifference = 0.f;

	int32 DispatchedWindowChangeCount = 0;

	int32 ReplayedWindowChangeCount = 0;

	int32 RecombinedWindowChangeCount = 0;

	/** 最近几次发给表现层的窗口变化，旧的在前 */
	TArray<FWindowChangeRecord> RecentWindowChanges;

	/** MoveAutonomous 正在执行的那次移动的客户端时刻（服务器、客户端重放），[Press] 日志用它让两端对上同一次移动 */
	float AutonomousMoveTimeStamp = 0.f;
#endif

	/** 冲刺输入。影响模拟（决定 wishspeed），经压缩标志位上报 */
	bool bWantsToSprint = false;

	/** 角色上的相机上下文（BeginPlay 时找）；没有时不写 */
	TWeakObjectPtr<UGinkgoCameraContextComponent> CameraContext;

	/** 正在执行的这次移动的帧长（PerformMovement 里）：相机事件的时刻按它算 */
	float CurrentMoveDeltaTime = 0.f;

	/** 正在处理着地时（ProcessLanded 里）这次移动还剩多少时间；不在处理着地时为负 */
	float LandingRemainingTime = -1.f;

	/** 这次着地前的下落速度：落地的相机事件的强度 */
	float LastLandingFallSpeed = 0.f;

	/** 发给相机上下文的事件的累计次数，见 GetCameraEventCount */
	uint32 CameraEventCount = 0;

	/** 方向键按下的计数，见 AddDirectionPresses。经扩展的移动数据上报，服务器与客户端重放时由那次移动的数据给出 */
	uint8 DirectionPressCount = 0;

	/** 方向键的朝向，打包的 4 位，见 SetDirectionKeys。同计数，经扩展的移动数据上报 */
	uint8 DirectionKeys = 0;

	/** 持枪的地速倍率，量化过的（255 是 1），见 SetSpeedScales。同计数，经扩展的移动数据上报 */
	uint8 HeldSpeedScale = 255;

	/** 开镜的地速倍率，同上 */
	uint8 AdsSpeedScale = 255;

	/** 发给服务器的移动数据：多带方向键按下的计数 */
	FGinkgoNetworkMoveDataContainer NetworkMoveData;

	/** 模拟产生的状态：两端各自算，存档记快照，校正时下发 */
	FGinkgoMovementSimState SimState;

	/** 服务器上：待发的确认或校正对应的那次移动之后的模拟状态 */
	FGinkgoMovementSimState PendingAdjustmentSimState;

	/** 与 PendingAdjustmentSimState 同一移动末尾的攀爬状态，发送回包前不再重采样。 */
	FGinkgoTraversalSyncState PendingAdjustmentTraversal;

	/** 服务器上：同一刻是否蹲着 */
	bool bPendingAdjustmentCrouched = false;

	/** 服务器回包的存储：校正时多带模拟产生的状态 */
	FGinkgoMoveResponseDataContainer MoveResponseData;

	/**
	 * 父类正在处理服务器的回包。应用校正时父类先调 OnClientCorrectionReceived，再换位置、速度与移动模式，
	 * 换模式会调到 OnMovementModeChanged：这期间不按落地、离地处理，服务器的模拟状态里已经算过
	 */
	bool bHandlingMoveResponse = false;

	/** 这次回包里的校正已经生效（OnClientCorrectionReceived 被调到），父类处理完回包后整体换成服务器的模拟状态 */
	bool bServerSimStatePending = false;

	/** 校正累计把胶囊挪了多少，见 GetCorrectionOffsetTotal */
	FVector CorrectionOffsetTotal = FVector::ZeroVector;

	/** 收到了校正、还没重放：引擎换位置之前胶囊在哪里。一帧里收到几个校正时记第一个之前的 */
	TOptional<FVector> CorrectionStartLocation;

	/** 窗口管理器：规则与汇总标签；窗口的状态在 SimState.Windows */
	FGinkgoMovementWindowManager WindowManager;

	/** 正在 DoJump 里：DoJump 切到下落时，离地算起跳，不算未起跳离地 */
	bool bInDoJump = false;

	/** 正在 DoJump 里、这一跳是下落中靠窗口的"空中起跳"跳的（见 CanJumpWhileFalling） */
	bool bJumpingWhileFalling = false;
};

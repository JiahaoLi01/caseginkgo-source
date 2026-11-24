#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/CharacterMovementReplication.h"
#include "GinkgoMovementSimState.h"
#include "Traversal/GinkgoTraversalNetworking.h"

/**
 * @class FGinkgoSavedMove : 一次移动的存档
 * 客户端每次移动存一份，用来发给服务器、在服务器校正后重放。
 *
 * 影响模拟的东西分两类处理：
 * 1. 输入：冲刺进压缩标志位 FLAG_Custom_0，服务器与重放都经 UpdateFromCompressedFlags 拿到同一个值；
 *    方向键按下的计数与朝向、持枪与开镜的地速倍率进扩展的移动数据（FGinkgoNetworkMoveData），服务器与重放都经 MoveAutonomous 从那次移动的数据里取；
 *    视角在 SavedControlRotation 里，重放后仍保留记录时的值（父类会换成实时视角，见 PostUpdate）
 * 2. 模拟产生的状态（FGinkgoMovementSimState）：不由客户端上报，校正时随服务器的回包（FGinkgoMoveResponseDataContainer）下发。
 *    存档只记它在移动开始时的样子：客户端合并移动时，引擎只把位置、速度、地面与跳跃计数退回到被合并那次移动开始时，
 *    这份状态要在 CombineWith 里一起退回，否则合并后重算时，被合并那次移动的 Δt 会被多算一遍
 *
 * 窗口有变化（推入、换阶段、结束）的移动不合并，做法同引擎对重叠事件的处理：变化是离散的事件，表现层只该收到一次；
 * 合并后从被合并那次移动的开头重算，同一个变化会再发生一遍
 */
class FGinkgoSavedMove : public FSavedMove_Character
{
public:
	using Super = FSavedMove_Character;

	//~ Begin FSavedMove_Character
	virtual void Clear() override;
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override;
	virtual void SetInitialPosition(ACharacter* Character) override;
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override;
	virtual void CombineWith(const FSavedMove_Character* OldMove, ACharacter* InCharacter, APlayerController* PC, const FVector& OldStartLocation) override;
	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode) override;
	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override;
	virtual uint8 GetCompressedFlags() const override;
	//~ End FSavedMove_Character

	/** 这次移动带的方向键按下计数 */
	uint8 GetDirectionPressCount() const { return DirectionPressCount; }

	/** 这次移动带的方向键朝向，打包的 4 位 */
	uint8 GetDirectionKeys() const { return DirectionKeys; }

	/** 这次移动带的持枪地速倍率，量化过的 */
	uint8 GetHeldSpeedScale() const { return HeldSpeedScale; }

	/** 这次移动带的开镜地速倍率，量化过的 */
	uint8 GetAdsSpeedScale() const { return AdsSpeedScale; }

	/** 该次移动记录的攀爬按键，不随之后的实时镜头改变。 */
	const FGinkgoTraversalInput& GetTraversalInput() const { return TraversalInput; }

	/** 本次移动结束的攀爬状态；发给服务器的只取序号/阶段，完整几何只用于本地验证。 */
	const FGinkgoTraversalSyncState& GetEndTraversal() const { return EndTraversal; }

#if !UE_BUILD_SHIPPING
	/** 这次移动结束时的模拟状态，调试比对用；记录时与校正后重放时都会更新 */
	const FGinkgoMovementSimState& GetEndSimState() const { return EndSimState; }

	/** 这次移动结束时是否蹲着，调试比对用；更新时机同上 */
	bool IsEndCrouched() const { return bEndCrouched; }
#endif

private:
	/** 本次移动的冲刺意图，写入压缩标志位；校正重放按存档恢复。 */
	bool bWantsToSprint = false;

	/**
	 * 方向键按下的计数。计数不同的移动不合并：合并后按下会挪到被合并那次移动的开头，早了一次移动，同引擎对压缩标志位的处理。
	 * 计数变了的移动算重要的移动：引擎在之后的包里把还没确认的重要移动补发一次，按下落在原来那次移动上
	 */
	uint8 DirectionPressCount = 0;

	/**
	 * 方向键的朝向（打包的 4 位）。不同的移动照样合并：朝向只在处理按下时用，有按下的移动计数不同、本来就不合并
	 */
	uint8 DirectionKeys = 0;

	/**
	 * 持枪与开镜的地速倍率。不同的移动不合并，同压缩标志位：合并后两端按一个倍率算一整段；
	 * 与最后确认的不同的算重要的移动：丢了的那次补发，服务器不会用错的倍率走那一段
	 */
	uint8 HeldSpeedScale = 255;

	/** 同次移动的开镜地速倍率，量化为 0–255；与持枪倍率一起决定是否可合并移动。 */
	uint8 AdsSpeedScale = 255;

	/** 这次移动开始时的模拟状态：合并移动时放回 */
	FGinkgoMovementSimState StartSimState;

	/** 本次输入快照；在 SetMoveFor 记录，重要移动和不合并判定均使用请求序号。 */
	FGinkgoTraversalInput TraversalInput;

	/** 本次模拟开始时的攀爬状态，合并回退时连同普通移动状态恢复。 */
	FGinkgoTraversalSyncState StartTraversal;

	/** 本次模拟结束时的攀爬状态，校正比较与阶段分歧检查使用。 */
	FGinkgoTraversalSyncState EndTraversal;

	/**
	 * 这次移动开始时在地面上、且还在落地免摩擦窗口里。
	 * 这样的移动不与别的合并：合并后两端都按一整段算，窗口在中间结束时，后半段该有的摩擦就没了。
	 */
	bool bStartedInLandingGrace = false;

	/** 移动开始与结束时窗口变化的累计次数（UGinkgoCharacterMovementComponent::GetWindowChangeCount） */
	uint32 StartWindowChangeCount = 0;

	/** 本次移动结束时窗口变化的累计次数；与开始值比较，避免合并导致事件重复。 */
	uint32 EndWindowChangeCount = 0;

	/**
	 * 移动开始与结束时发给相机上下文的事件的累计次数（UGinkgoCharacterMovementComponent::GetCameraEventCount）：发过相机事件的移动不合并，
	 * 合并后重算时滑铲开始会再发一遍、相机多震一次
	 */
	uint32 StartCameraEventCount = 0;

	/** 本次移动结束时已派发的相机事件计数；有新增事件时禁止合并。 */
	uint32 EndCameraEventCount = 0;

#if !UE_BUILD_SHIPPING
	/** 记录/重放结束后的普通移动状态，仅在开发版用于与服务器确认快照比对。 */
	FGinkgoMovementSimState EndSimState;

	/** 同次移动结束时胶囊是否为蹲姿，供网络调试区分形状差异。 */
	bool bEndCrouched = false;
#endif
};

/**
 * @class FGinkgoNetworkPredictionData_Client : 客户端预测数据
 * 只为让 CMC 分配 FGinkgoSavedMove。
 */
class FGinkgoNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
public:
	using Super = FNetworkPredictionData_Client_Character;

	explicit FGinkgoNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}

	//~ Begin FNetworkPredictionData_Client_Character
	virtual FSavedMovePtr AllocateNewMove() override;
	//~ End FNetworkPredictionData_Client_Character
};

/**
 * @struct FGinkgoNetworkMoveData : 发给服务器的一次移动
 * 引擎的移动数据之外多带新的输入。压缩标志位只剩三位，新的输入不再占它们，都进这里（设计文档"网络与预测"）。
 * lurch 输入是方向键按下计数（8 位）与按着的方向键朝向（4 位，后按的赢）。
 * 还有持枪与开镜的地速倍率（各 8 位，Docs/Design/Weapons.md「移动：持枪与开镜的地速」）：客户端算好放进去，服务器照用。移动不读武器组件，
 * 服务器不用查那一刻手上是哪把枪；量化时夹到不超过 1，改过的客户端只能让自己更慢。
 * 攀爬另外发送持久请求（41 位）及预测动作序号/阶段（18 位）；几何锚点不由客户端上报。
 * 引擎每个包里的新移动、双移动里的前一次与补发的旧移动各带一份。
 *
 * 为什么发计数而不是"这次移动按下了"：按下只出现在一次移动里，那个包丢了、引擎又没补发（每个包只补发一次旧移动，
 * 最后存下的那次不补发），服务器就永远收不到。计数是状态，之后每次移动都带着它，服务器在下一次收到的移动开始时处理；
 * 引擎把丢掉的那段时间并进下一次移动，所以那一刻服务器的状态正是客户端按下时的状态。
 */
struct FGinkgoNetworkMoveData : public FCharacterNetworkMoveData
{
	using Super = FCharacterNetworkMoveData;

	/** 请求序号和量化后的相机方向；没有目标位置，服务器独立检测。 */
	FGinkgoTraversalInput TraversalInput;

	/** 客户端该次移动结束时的动作序号，仅用于要求服务器发送校正，不作为权威状态。 */
	uint16 TraversalAction = 0;

	/** 客户端该次移动结束时的阶段，确保原地拒绝等位置误差很小时也能纠正。 */
	EGinkgoTraversalPhase TraversalPhase = EGinkgoTraversalPhase::Idle;

	//~ Begin FCharacterNetworkMoveData
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;
	//~ End FCharacterNetworkMoveData

	/** 方向键按下的计数（UGinkgoCharacterMovementComponent::AddDirectionPresses） */
	uint8 DirectionPressCount = 0;

	/**
	 * 按着的方向键的朝向，打包的 4 位（UGinkgoCharacterMovementComponent::SetDirectionKeys）。服务器用它与这次移动的视角算 lurch 的方向：
	 * 移动输入里反方向的两个键相加抵消，从加速度里看不出后按的是哪个
	 */
	uint8 DirectionKeys = 0;

	/** 持枪的地速倍率，量化过的（UGinkgoCharacterMovementComponent::SetSpeedScales） */
	uint8 HeldSpeedScale = 255;

	/** 开镜的地速倍率，同上 */
	uint8 AdsSpeedScale = 255;
};

/**
 * @struct FGinkgoNetworkMoveDataContainer : 发给服务器的移动数据的存储
 * 只把父类的三份移动数据换成 FGinkgoNetworkMoveData；移动组件在构造时交给 SetNetworkMoveDataContainer。
 * 引擎 5.8 默认走打包的移动 RPC（p.NetUsePackedMovementRPCs 1），关掉时扩展的数据发不出去
 */
struct FGinkgoNetworkMoveDataContainer : public FCharacterNetworkMoveDataContainer
{
	FGinkgoNetworkMoveDataContainer()
	{
		NewMoveData = &MoveData[0];
		PendingMoveData = &MoveData[1];
		OldMoveData = &MoveData[2];
	}

private:
	/** 本包的新移动、待合并移动及旧移动各自的扩展数据，由构造函数交给父类。 */
	FGinkgoNetworkMoveData MoveData[3];
};

/**
 * @struct FGinkgoMoveResponseDataContainer : 服务器的回包
 * 校正时除了引擎自带的位置、速度与移动模式，再带上普通移动状态、攀爬快照与是否蹲着；
 * 客户端先换成同样的胶囊、应用完校正再放回模拟状态，然后重放之后的移动。
 * 确认好移动的回包平时不带；ginkgo.net.verifySimState 打开时也带上，客户端拿来与存档比对，验证两端算得一样。
 */
struct FGinkgoMoveResponseDataContainer : public FCharacterMoveResponseDataContainer
{
	using Super = FCharacterMoveResponseDataContainer;

	//~ Begin FCharacterMoveResponseDataContainer
	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment) override;
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap) override;
	//~ End FCharacterMoveResponseDataContainer

	/** 服务器处理完被确认或被校正的那次移动后的模拟状态：父类记下待发的确认或校正时存下，不是发送时的（见 UGinkgoCharacterMovementComponent::ServerMoveHandleClientError） */
	FGinkgoMovementSimState SimState;

	/** 同次移动末尾的攀爬状态；只由服务器下发，客户端重放不使用旧的预测锚点覆盖它。 */
	FGinkgoTraversalSyncState Traversal;

	/**
	 * 同一刻服务器是否蹲着，与模拟状态一起带。引擎的校正不带蹲伏状态，给的位置却是服务器那个胶囊的中心：
	 * 客户端刚蹲下、收到的是蹲下之前那次移动的校正时，不先换成站立的胶囊，蹲下的胶囊就悬在半空
	 */
	bool bServerCrouched = false;

	/** 这次回包带了模拟状态与是否蹲着：校正总带，确认好移动时只在调试比对时带 */
	bool bHasSimState = false;
};

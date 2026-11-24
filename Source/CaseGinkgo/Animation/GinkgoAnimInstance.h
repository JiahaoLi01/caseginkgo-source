#pragma once

#include "CoreMinimal.h"
#include "Movement/Traversal/GinkgoTraversalAnimations.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/Actions/GinkgoAnimActionTypes.h"  // EGinkgoHand：伸手的是哪只手
#include "Animation/GinkgoAnimMath.h"
#include "Animation/GinkgoAnimTypes.h"
#include "Animation/GinkgoPeek.h"
#include "Camera/GinkgoSpring.h"
#include "Weapon/GinkgoWeaponTypes.h"
#include "GinkgoAnimInstance.generated.h"

class AGinkgoCharacter;
class UAnimMontage;
class UGinkgoWeaponDefinition;

/**
 * @struct FGinkgoWeaponAnimData : 给武器层的一帧
 * 主实例在工作线程上算好；层（UGinkgoWeaponAnimLayers）在自己的 NativeThreadSafeUpdateAnimation 里读，那时主实例这一帧的已经算过了
 */
struct FGinkgoWeaponAnimData
{
	/** 手上的枪；徒手时为空 */
	const UGinkgoWeaponDefinition* Weapon = nullptr;
	EGinkgoHoldStance Stance = EGinkgoHoldStance::Unarmed;
	/** 两头缓过的抬枪进度（低持 → 抵肩）与开镜进度（抵肩 → 开镜） */
	float RaiseAlpha = 0.f;
	float AdsAlpha = 0.f;
	/**
	 * 持枪的层盖多少：平时 1。拔枪、收枪放在右臂的槽上，盖不住左臂与上半身，层跟着片段的 Weapon_HoldPose 曲线让开，这段时间里它们照移动的姿势（跑着切枪上半身不绷直）。
	 * 片段的曲线在第 4 到 13 帧走，同片段里左臂从持枪混到徒手待机，所以站着时左臂的路与片段一样
	 */
	float HoldPoseAlpha = 1.f;
	/** 冲刺持枪的权重：两头缓过的冲刺进度；枪没有冲刺的循环、持枪循环关着时为 0（冲刺照低持放） */
	float SprintAlpha = 0.f;
	/** 地速：在地面上（不含滑铲）可见的根的水平速度（cm/s），别的时候为 0。层按它把低持跑动的循环混进来 */
	float GroundSpeed = 0.f;
	/** 这一步两只手都在枪上（换弹）：单手跑的枪这时不放跑的循环 */
	bool bBothHandsOnGun = false;
	/** 持枪循环开着（ginkgo.anim.holdLoops，只用来对照；发行版里总是开） */
	bool bHoldLoops = true;
	/**
	 * 持枪循环的时钟（s）：动画开始或上次切（RequestCut：传送、回放放回开始状态）以来走过的时间。开镜的循环按它取时刻，一直走、不随开镜从头放；
	 * 回放从切的那一刻起计时，各个帧率下同一个时刻取到同一帧
	 */
	double LoopClock = 0.0;
	/** 视角相对网格根的偏航与俯仰（度，向右、向上为正）：瞄准偏移的输入。站着时偏航就是偏航补偿的反向 */
	float AimYaw = 0.f;
	float AimPitch = 0.f;
};

/**
 * @struct FGinkgoHandReach : 一只手伸去拿一件东西（3C 的接口，拾取用；Docs/Design/Interaction.md「拾取子弹盒」）
 * 被交互物开始拾取的动作时设上（AGinkgoCharacter::SetHandReach），动画实例每帧把东西上的点换到这一帧，按蒙太奇里两条曲线把手拉过去：
 * 去抓（手里那个点到东西上）、收回（东西到胸前的收纳处）。曲线在蒙太奇这一刻的值直接从它的序列求，不经过图；
 * 权重是这个蒙太奇实例的混合权重：胳膊的槽淡入淡出时，手也从原来的目标（枪上的握法，或动画的手）换过来、换回去。
 * 蒙太奇淡出完（实例没了）就自己清掉；开始淡出时还留着，手跟着淡出的权重换回去。
 * 同一只手连着拾取时，新的蒙太奇顶掉旧的（同一个蒙太奇的两个实例），旧的伸手随旧的实例淡出，两个目标按实例的权重混，不跳
 */
struct FGinkgoHandReach
{
	/** 放着的蒙太奇：曲线从它求，权重是它的 */
	TWeakObjectPtr<const UAnimMontage> Montage;

	/** 东西：它的组件，与组件坐标里手要去的点。东西没了（被销毁）时停在最后一次的位置 */
	TWeakObjectPtr<const USceneComponent> Target;
	FVector TargetPoint = FVector::ZeroVector;

	/** 手骨（hand_l / hand_r）的坐标里，东西被握住时那个点在哪：片段的挂点 */
	FVector ItemInHand = FVector::ZeroVector;

	/** 拾取物指定的收纳骨骼。 */
	FName StowBone = TEXT("spine_05");

	/** 物品收纳点，StowBone 局部坐标（cm）。 */
	FVector PocketInChest = FVector::ZeroVector;

	/** 去抓、收回两条曲线：只标出斜坡从哪帧到哪帧（0→1、1→0），形状是这边给的（缓进缓出） */
	FName GrabCurve = TEXT("Pickup_GrabIK");
	FName PlaceCurve = TEXT("Pickup_PlaceIK");

	/**
	 * 方位约束（度，Ginkgo::Animation::ConstrainReachBearing）：东西相对角色朝向的水平方位，从伸手那边的肩关节量，往外（这只手那一侧，含身后）
	 * MaxOutward、往身体中间（另一只肩膀那边）MaxInward 以内照常伸；超出的只朝扇形的边上伸，超出 FadeOutside 以外不再伸过去。
	 * 往里 60°：够得着的范围里身前往右那一圈在 45° 上（肩关节的位置差一点就过了 45°，手对着转过的点、Grab 时吸过来 2.8 cm）。
	 * 跑得快、转身快时东西跑到身后或另一只肩膀那边，上半身不往后仰、不拧过去；够不着的照样在 Grab 时直接挂上
	 */
	float MaxOutwardDegrees = 100.f;
	float MaxInwardDegrees = 60.f;
	float FadeOutsideDegrees = 45.f;

	/**
	 * 够不着时手的目标拉回到胳膊能到的地方（cm，从前倾过的肩关节量；0 是不拉回）：ArmLimit 以内不变，往外平滑地趋近 ArmLimit + ArmLimitMargin（Ginkgo::Animation::SoftLimitReach）。
	 * 不拉回时全身 IK 为够不到的目标把脊柱往下拽，地上的东西把背压弯。够得着的前倾以后肩关节离东西 48 cm，碰不到这个限制
	 */
	float ArmLimit = 0.f;
	float ArmLimitMargin = 5.f;
};

namespace Ginkgo::Animation
{
	/**
	 * @brief 拔枪片段里持枪的层盖满的时刻（Weapon_HoldPose 到 1 的最早一刻）：捡枪抓住以后从这里接上。枪从地上直接到低持；从层开始盖的时刻接的话，手还在肩膀上，枪先竖到肩上再落下来
	 * @return 片段上没有这条曲线时为 0
	 */
	CASEGINKGO_API float GetHoldPoseFullTime(const UAnimMontage& Montage);

	/**
	 * @brief 手的目标离肩关节的距离，够不着时拉回（FGinkgoHandReach::ArmLimit）
	 * @param Distance - 东西离肩关节多远
	 * @param Limit - 这以内不变
	 * @param Margin - 往外平滑地趋近 Limit + Margin：过 Limit 时斜率还是 1，不折
	 */
	CASEGINKGO_API double SoftLimitReach(double Distance, double Limit, double Margin);

	/**
	 * @brief 伸手的东西的方位约束（见 FGinkgoHandReach::MaxOutwardDegrees）：超出扇形的绕过肩关节的竖直轴转回近的那条边，距离、高度不变
	 * @param Item - 东西（世界）
	 * @param Shoulder - 伸手那只手的肩关节（世界）：方位从这里量
	 * @param Forward - 角色的朝向（只取水平的）
	 * @param OutItem - 转回扇形边上的东西
	 * @return 伸过去的系数（乘在去抓的曲线上）：扇形里为 1，出了扇形 FadeOutside 度以外为 0，中间缓过（smoothstep）。身后两条边一样远的地方已经是 0，
	 *         东西扫过身后时手不会从一条边跳到另一条
	 */
	CASEGINKGO_API float ConstrainReachBearing(const FVector& Item, const FVector& Shoulder, const FVector& Forward, EGinkgoHand Hand, float MaxOutward, float MaxInward,
		float FadeOutside, FVector& OutItem);
}

/** @struct FGinkgoHandReachRigData : 一只手伸手的一帧，给 Control Rig（FRigUnit_GinkgoWeaponAim） */
struct FGinkgoHandReachRigData
{
	bool bActive = false;

	/** 东西上手要去的点（世界） */
	FVector Item = FVector::ZeroVector;

	/** 同 FGinkgoHandReach */
	FVector ItemInHand = FVector::ZeroVector;

	/** 当前动作的收纳骨骼。 */
	FName StowBone = TEXT("spine_05");

	/** StowBone 局部空间收纳点（cm）。 */
	FVector PocketInChest = FVector::ZeroVector;

	/** 两条曲线在蒙太奇这一刻的值（0–1），缓进缓出过的 */
	float Grab = 0.f;
	float Place = 0.f;

	/** 方位约束给的伸过去的系数（0–1，乘在 Grab 上；前倾、手转向、两遍 IK 都用乘过的）。Item 已经转回扇形的边上 */
	float Bearing = 1.f;

	/** 蒙太奇实例的混合权重：这只手从原来的目标换到伸手的目标 */
	float Weight = 0.f;

	/** 同 FGinkgoHandReach */
	float ArmLimit = 0.f;
	float ArmLimitMargin = 0.f;
};

/** 游戏线程采样投掷与遮眼，动画求值只读取这三个归一化权重。 */
struct FGinkgoFlashArmPose
{
	/** 左臂交给投掷动画的权重，0–1。 */
	float Throw = 0.f;

	/** 取出时左腕对当前帧骨盆挂点的 IK 权重，抓住时为 1。 */
	float Belt = 0.f;

	/** 最后三帧向脱手点对齐，脱手后在随动段释放。 */
	float Release = 0.f;

	/** 出手瞬间物品中心的世界位置（cm），仅在 Release 非零时读取。 */
	FVector ReleasePoint = FVector::ZeroVector;

	/** NPC 左臂遮眼的权重，失明时升到 1，恢复后淡出。 */
	float Cover = 0.f;
};

/**
 * @struct FGinkgoWeaponRigData : 给 Control Rig 里瞄准与双手的一帧（FRigUnit_GinkgoWeaponAim 读）
 * 主实例在更新时写好，Control Rig 在求姿势时读（那时这一帧的更新已经完了）。见 Docs/Design/Weapons.md「手与瞄准」
 */
struct FGinkgoWeaponRigData
{
	/** 攀爬姿势覆盖及双手世界空间接触快照；按全身权重让出持枪修正，单手动作保留右手握枪，末端仍由双臂 IK 求解。 */
	FGinkgoTraversalHandPose Traversal;

	/** 手上有枪：没有时瞄准与双手都不做 */
	bool bArmed = false;
	/** 瞄准点（世界）：本地控制的角色从推算的这一帧的相机沿视角打线的命中点；模拟代理没有 */
	bool bHasAimPoint = false;
	FVector AimPoint = FVector::ZeroVector;
	/**
	 * 第一人称开镜（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：对准的是瞄具的光轴，不是枪管。眼睛（相机）就在光轴的出瞳上，
	 * 所以光轴平行于瞄准方向就对准了，不用瞄准点。按这个比重从"枪口 → 瞄准点"混过去（有瞄具时就是开镜进度）；越肩时是 0
	 */
	float SightAlpha = 0.f;
	/** 瞄准方向（世界，单位向量）：第一人称时枪的 X 轴（瞄具的光轴平行于它）对准它 */
	FVector AimDirection = FVector::ForwardVector;
	/** 枪口对准的权重：两头缓过的抬枪进度（腰射、开镜为 1，低持、冲刺为 0） */
	float AimAlpha = 0.f;
	/** 两只手在枪上的目标（枪的坐标）：低持、冲刺、腰射、开镜的按三个进度混过 */
	FGinkgoWeaponHandTargets Hands;
	/**
	 * 左手握着枪的比例：单手冲刺（定义的 bSprintTwoHanded 为假）时是 1 − 冲刺持枪的权重，别的时候为 1。
	 * 乘在左手照常握枪的那一份上（蒙太奇的 HandIK_L 曲线另加）：举枪冲刺时左手照循环里的摆臂
	 */
	float LeftHandGrip = 1.f;
	/**
	 * 左手的目标沿枪往后挪多少（cm，枪的 −X）：左手在跟着枪机走的护木上（泵动霰弹枪，定义的 bLeftHandOnForend），
	 * 是枪机的行程 × 枪机现在走了多少，手一直在护木上。别的枪为 0
	 */
	float LeftHandSlide = 0.f;
	/** 枪口在枪的坐标里的位置，枪管方向是枪的 X 轴 */
	FVector MuzzleOffset = FVector::ZeroVector;
	/** 枪的程序化偏移（枪的坐标，UGinkgoWeaponComponent::GetGunOffsetTransform：踢动，第一人称开镜时再加枪相对眼睛的偏移）：乘在枪口对准以后的枪上，手的目标跟着它 */
	FTransform GunKick = FTransform::Identity;
	/**
	 * 枪上的蒙太奇的权重（拔枪、收枪在右臂的槽上，以后换弹在上半身的槽上）：双手的权重是 1 − 它 + 蒙太奇的 HandIK_L、HandIK_R 曲线（曲线 Control Rig 从姿势里读）。
	 * 右手的拾取也在右臂的槽上，但只在枪不在手上时放，那时手的权重本来就只听曲线
	 */
	float GunMontageWeight = 0.f;
	/**
	 * 枪挂在手上（不在背上）：不在手上时双手只听蒙太奇的曲线。拔枪开始时就链上了层（手上换成这把枪），枪还在背上，
	 * 照常握着的话两只手被拉到背上去（weapon 路线一帧跳 75 cm）
	 */
	bool bGunInHand = false;

	/** 从手持姿势混到桌面枪位的进度（0–1）。枪到接触点以后停在 BenchRest，双手按 BenchGrip 收回 */
	float BenchGunAlpha = 0.f;

	/** 放枪、取枪时双手的 IK 权重（0–1），枪到桌面后按动作进度收回 */
	float BenchGrip = 1.f;

	/** 装配件时的世界空间手腕目标与两侧接触权重，由安装动作统一采样 */
	FGinkgoAttachmentHandPose BenchInstall;

	/** 投掷、腰部抓取和受闪遮眼的左臂姿势。 */
	FGinkgoFlashArmPose FlashArm;

	/** 桌上枪网格的世界变换，同一个目标供枪骨和双手求解 */
	FTransform BenchRest = FTransform::Identity;

	/**
	 * 捡枪抓住以后的收拢（UGinkgoWeaponComponent::GetPickupSettleAlpha，两头缓过）：小于 1 时枪从右手里（右手照空手时伸手、提起来的位置）
	 * 按它混到持枪的位置，两只手的目标都跟着这把枪，右手一直握着。1 是不在收拢
	 */
	float PickupSettle = 1.f;
	/** 抓住的那一刻（UGinkgoWeaponComponent::GetPickupSettleStart，角色网格的坐标） */
	FGinkgoPickupSettleStart PickupStart;
	/** 角色的朝向（角色网格的坐标）：前是 X、上是 Z。收拢时枪口从身前扫过去（Ginkgo::Weapon::BlendTurnInFront） */
	FQuat BodyRotation = FQuat::Identity;

	/** 伸手（拾取）：下标是 EGinkgoHand。空手时也算：伸手的那只手照它，另一只不动 */
	FGinkgoHandReachRigData Reach[2];

	/**
	 * 同一只手被新的拾取顶掉、还在随自己的蒙太奇实例淡出的伸手（连着拾取时；先顶掉的在前）。胳膊的槽里新旧两个实例按权重混，
	 * 手的目标也按权重混（Ginkgo::Animation::ComputeHandReachBlend），不在新的开始那一帧跳回原来的目标
	 */
	TArray<FGinkgoHandReachRigData, TInlineAllocator<2>> FadingReach[2];
};

class ACharacter;
class UCharacterMovementComponent;
struct FSequencePlayerReference;

/**
 * @class UGinkgoAnimInstance : 角色的动画实例，动画蓝图（ABP_GinkgoCharacter）的父类
 * 设计见 Docs/Design/AnimationBlueprint.md。每帧分两步：
 * 1. NativeUpdateAnimation（游戏线程）：读网格、移动组件与角色，存成一份快照；
 * 2. NativeThreadSafeUpdateAnimation（工作线程）：只用快照算三类量（运动、意图、状态），再算移动状态机要的转换条件、
 *    根骨骼的偏航补偿与修正节点的值，写进图要读的成员。
 * 网格的 tick 排在移动组件之后（ACharacter::PostInitializeComponents 加的前置），读到的是这一帧移动并平滑之后的位置。
 *
 * 动画蓝图由命令 GinkgoAnimBlueprint 生成一次，之后在编辑器里改。图只读这里的成员，不写事件图：
 * - 状态机的转换规则读 bShould* 这些条件；
 * - 每个状态的节点绑定这里的节点函数（On Become Relevant 选片段，On Update 推进时间、报自己是哪个状态）；
 * - 状态机之后的修正节点读 OrientationWarpingAngle、StrideScale，再之后的混合节点（Dead Blending）接住换片段、换状态时的跳变，
 *   最后转根骨骼读 RootYawOffset。
 * 读这些量时不分网络角色：网络角色的分支只在读快照的地方（滑铲、冲刺取复制的还是模拟的、意图来自哪里）。
 */
UCLASS()
class CASEGINKGO_API UGinkgoAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	explicit UGinkgoAnimInstance(const FObjectInitializer& ObjectInitializer);

	/**
	 * @brief 清掉差分的历史：下一次更新没有上一帧可比，速度取移动组件的
	 * @note 角色被放到别处时调用（输入回放放回开始状态，以后的重生与传送），与相机的 RequestCameraCut 同时。
	 *       不能靠 bJustTeleported 判断：模拟代理每次网络更新只要位置变了就会设上它
	 */
	void RequestCut() { bCutRequested = true; }

	const FGinkgoAnimMotion& GetMotion() const { return Motion; }
	const FGinkgoAnimIntentState& GetIntent() const { return Intent; }
	const FGinkgoAnimMoveState& GetMoveState() const { return MoveState; }
	EGinkgoLocomotionState GetLocomotionState() const { return LocomotionState; }
	EGinkgoAnimGait GetGait() const { return Gait; }
	EGinkgoAnimDirection GetDirection() const { return Direction; }
	float GetRootYawOffset() const { return RootYawOffset; }
	float GetOrientationWarpingAngle() const { return OrientationWarpingAngle; }
	float GetStrideScale() const { return StrideScale; }
	float GetLeanAmount() const { return LeanAmount; }
	float GetLeanSprintWeight() const { return LeanSprintWeight; }
	float GetLegIKAlpha() const { return LegIKAlpha; }

	/** 给武器层的一帧（工作线程：层在它们的 NativeThreadSafeUpdateAnimation 里读，主实例这一帧的已经算过） */
	const FGinkgoWeaponAnimData& GetWeaponAnimData() const { return WeaponAnim; }

	/** 给 Control Rig 里瞄准与双手的一帧（求姿势时读，这一帧的更新已经完了） */
	const FGinkgoWeaponRigData& GetWeaponRigData() const { return WeaponRig; }

	/** 当前帧是否需要玩家反遮挡参考枪口；仅本地玩家常规抬枪姿态为真，AI 战术请求不依赖此参考射线 */
	bool IsPeekProbeRequested() const { return bPeekProbeRequested; }

	/** 同一次姿势求值里，捕获节点写、下游 WeaponAim 读，不跨线程并发访问 */
	void RecordPeekReference(const FGinkgoPeekReference& Reference) { PeekReference = Reference; }

	/** 本次求值在探身前捕获的骨骼；无效时不生成参考枪口 */
	const FGinkgoPeekReference& GetPeekReference() const { return PeekReference; }

	/** WeaponAim 在求值线程报未探身的已对准枪口，下一次游戏线程更新消费；不含 Gun Kick */
	void RecordPeekGun(const FTransform& Gun) { PeekGun = Gun; bPeekGunReady = true; }

	/** 调试与测量：带符号权重，负为左倾、正为右倾 */
	double GetPeekWeight() const { return PeekState.Blend.Value; }

	/** 调试与测量：参考命中相对准星的水平夹角（度），正值偏右 */
	double GetPeekErrorDegrees() const { return PeekErrorDegrees; }

	/** 最近一次未探身参考枪的组件空间变换，只有已求值过参考分支后有效 */
	const FTransform& GetPeekGun() const { return PeekGun; }

	/** 捡枪收拢时持枪的层盖上来、拾取的片段淡出，都在收拢的前多少（按时间的比例）里做完 */
	static constexpr float PickupHoldShare = 0.4f;

	/** @brief 层报这一帧跑的循环的权重（层的工作线程上，同主实例一起更新）：单手跑的枪下一帧按它放开左手 */
	void ReportRunAlpha(const float RunAlpha) { LayerRunAlpha = RunAlpha; }

	/**
	 * @brief 一只手伸去拿东西：从下一次更新起按蒙太奇的两条曲线把手拉过去（见 FGinkgoHandReach）。这只手上一个还在时（连着拾取），
	 *        上一个随它的蒙太奇实例淡出
	 * @note 3C 的接口，拾取用（Docs/Design/Interaction.md「拾取子弹盒」）。只在本地控制的角色上有意义：动作只在本地放。
	 *       在放蒙太奇之前调（动作的 OnStart 里）：那时上一个的实例还是在放的那个
	 */
	void SetHandReach(EGinkgoHand Hand, const FGinkgoHandReach& Reach);

	/** @brief 不再伸手（在淡出的也清掉）；蒙太奇放完时也会自己清掉 */
	void ClearHandReach(EGinkgoHand Hand);

	/**
	 * @brief 伸着的东西冻在这一刻相对身体的位置，之后不再跟着它在世界里的位置走：手抓住了东西（东西已经在手里）。
	 *        拾取结局（蒙太奇实例开始淡出）时自己会冻
	 */
	void FreezeHandReach(EGinkgoHand Hand);

	/** 这只手伸着的；没有时为空 */
	const FGinkgoHandReach* GetHandReach(EGinkgoHand Hand) const;

	/**
	 * 移动状态机这一帧求姿势时放的片段与时刻：节点函数记下，求值器是给的时刻，播放器是这一帧推进以后的（RecordPlayerTime）。
	 * 武器层的持枪循环在自己的节点函数里读（工作线程）：层的分层混合先更新输入的姿势（bUpdateBasePoseFirst），那时状态机这一帧的节点函数已经跑过
	 */
	const UAnimSequence* GetLocomotionSequence() const { return PoseSequence; }
	float GetLocomotionTime() const { return LocomotionTime; }

	// ===== 节点函数 =====
	// 动画蓝图里各状态的节点绑定它们：Setup* 绑 On Become Relevant（进入状态时选片段），Update* 绑 On Update。
	// 签名照引擎的原型（AnimExecutionContextLibrary::Prototype_ThreadSafeAnimUpdateCall），在动画的工作线程上调

	/** Idle：站着或蹲着的循环，蹲下、站起时惯性化地换 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateIdleAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 原地转身：按偏航补偿的方向与大小选 45°–180° 的片段 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void SetupTurnInPlaceAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 原地转身：按原速播放，片段的剩余转角把偏航补偿还回去（在 NativeThreadSafeUpdateAnimation 里） */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateTurnInPlaceAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 起步：按意图的方向选片段，从头放 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void SetupStartAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 起步：按这一帧走过的距离推进（距离匹配），走过 StartBlendOutDistance 之后转到循环 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateStartAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 循环：按方向与步态选片段，从现在着地的那只脚落地的时刻放起 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void SetupCycleAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 循环：方向、步态变了就惯性化地换片段（步伐对上），播放速率与步幅分担速度比 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateCycleAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 急停：按速度的方向选片段，左右脚两套里挑进入时着地的脚对得上的，对到预测的停止点 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void SetupStopAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 急停：停下之前每帧对到预测的停止点（距离匹配），停下之后按原速放完收势 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateStopAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 反向：按原来的方向选片段（Pivot_F_B……），同样挑着地的脚 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void SetupPivotAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 反向：过零之前对到预测的反向点，过零之后按走过的距离推进 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdatePivotAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/**
	 * 空中与落地（动画蓝图的 Air 状态）：起跳、走下边缘、下落循环、落地四个阶段，按阶段换片段（序列播放器的播放速率设 0、每帧给时刻）。
	 * 着地以后接着放落地的片段，交出去之前 Air 的三个出口先不成立（bLandHold），见 Docs/Design/AnimationBlueprint.md 的"空中"
	 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateAirAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 滑铲的进入：按倒下去的那只脚选 Into，从片段里那只脚最后一次落地放起（之前是跑过来的几步） */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void SetupSlideStartAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 滑铲的进入：按原速放，放到 SlideLoopTime（已经滑稳了）转到循环 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateSlideStartAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 滑铲的滑行：循环，不按地速（滑行的姿势没有步子，放快了只是晃得快） */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateSlideLoopAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 滑铲结束：按还蹲不蹲、有没有意图（推得满不满）选起身的片段，从 SlideExitStartTime 放起 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void SetupSlideExitAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

	/** 滑铲结束：起身接着走的按走过的距离推进，停下来的按原速放；放到 SlideExitEndTime（已经站起来、走起来了）交给平常的状态 */
	UFUNCTION(BlueprintCallable, Category = "Ginkgo|Locomotion", meta = (BlueprintThreadSafe))
	void UpdateSlideExitAnim(const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

#if !UE_BUILD_SHIPPING
	/** 调试显示（ginkgo.showAnim）与回放轨迹要的中间结果 */
	struct FDebugFrame
	{
		/** 网格的根（脚底）的世界位置：箭头从这里画 */
		FVector Feet = FVector::ZeroVector;
		/** 移动组件的速度：本地角色在地面上应与差分出来的相同 */
		FVector MovementVelocity = FVector::ZeroVector;
		/** 这一帧从位移里扣掉的校正挪动量 */
		FVector CorrectionOffset = FVector::ZeroVector;
		/** 相邻两次差分速度之差除以帧长，过弹簧之前 */
		FVector RawAcceleration = FVector::ZeroVector;
		/** 视角的偏航：本地角色是控制器的，模拟代理是角色的（它收不到视角的偏航） */
		double ViewYaw = 0.0;
		/** 这一帧没有上一帧可比（第一次，或清掉了历史） */
		bool bReset = false;
		bool bLocallyControlled = false;
		/** 移动状态机正在放的片段与它的时刻、播放速率（循环的） */
		const UAnimSequence* Sequence = nullptr;
		float SequenceTime = 0.f;
		float PlayRate = 1.f;
		/** 顺着轨迹预测的着地：还有多久（s，没扫到为负）与着地点 */
		double TimeToImpact = -1.0;
		FVector ImpactLocation = FVector::ZeroVector;
		/** 落地片段的着地时刻（Impact 标记）；没在放落地片段时为负 */
		float LandImpactTime = -1.f;
		/**
		 * 腿部 IK（Control Rig 里的 FRigUnit_GinkgoLegIK）每只脚报的：锁的状态（EGinkgoLegIKLock）、着没着地、锁住的位置、
		 * 动画想要的脚掌位置、加给脚的修正（cm）、预测还有多久落下（s，着地时为负）与落脚点（都是世界）
		 */
		struct FLegIKFoot
		{
			uint8 Lock = 0;
			bool bPlanted = false;
			FVector LockLocation = FVector::ZeroVector;
			FVector AnimBall = FVector::ZeroVector;
			float Correction = 0.f;
			float TimeToLand = -1.f;
			FVector PredictedLanding = FVector::ZeroVector;
			/**
			 * 地形：脚下的地面高度（世界；没扫到时 bHasTerrain 为假）与加给脚的竖直修正（cm）；
			 * 摆动的脚的路径（上一次着地的点、上凸包的拐点、预测的落脚点，世界），没有时为 0 个
			 */
			bool bHasTerrain = false;
			float TerrainZ = 0.f;
			float VerticalCorrection = 0.f;
			static constexpr int32 MaxPath = 12;
			int32 PathCount = 0;
			FVector Path[MaxPath];
			/**
			 * 脚下的地面从哪来（0 没有，1 着地的脚掌下，2 摆动的路径，3 照脚下扫）与过弹簧之前的高度（世界）；
			 * 两骨骼 IK 求解之前动画里的脚踝、膝盖（骨盆挪过以后）与脚踝要去的位置（世界）
			 */
			uint8 TerrainSource = 0;
			float RawTerrainZ = 0.f;
			/** 脚下的地面（或落脚点）在高台的边外，放在了边上那一侧的高度 */
			bool bLedge = false;
			FVector AnimFoot = FVector::ZeroVector;
			FVector AnimKnee = FVector::ZeroVector;
			FVector FootTarget = FVector::ZeroVector;
		};
		/** 左脚、右脚（节点里 Feet 的顺序） */
		FLegIKFoot LegIK[2];
		/** 腿部 IK 加给骨盆的竖直偏移（cm）与这一帧往下扫地面的次数 */
		float LegIKPelvisOffset = 0.f;
		int32 LegIKTraces = 0;
		/** 骨盆的偏移是怎么来的（cm，相对根骨骼）：两只脚的锚连成的线、够得着的上限、不折断的下限（没有时为 ±999）、在线以外另挪的量 */
		float LegIKPelvisLine = 0.f;
		float LegIKPelvisUpper = 0.f;
		float LegIKPelvisLower = 0.f;
		float LegIKPelvisAdjust = 0.f;

		/** Control Rig 里的瞄准与双手（FRigUnit_GinkgoWeaponAim 报） */
		struct FWeaponAim
		{
			/** 手上有枪、开着（ginkgo.anim.weaponAim） */
			bool bArmed = false;
			/** 枪口对准的权重（两头缓过的抬枪进度） */
			float AimAlpha = 0.f;
			/** 对准之前、之后枪口方向与"枪口 → 瞄准点"差多少（度） */
			float ResidualBefore = 0.f;
			float ResidualAfter = 0.f;
			/** 按满的算时几根脊柱一共转了多少、枪自己转了多少（度，混之前的） */
			float SpineAngle = 0.f;
			float GunAngle = 0.f;
			/** 对准以后枪口的位置与方向（世界） */
			FVector Muzzle = FVector::ZeroVector;
			FVector Bore = FVector::ZeroVector;
			/** 两只手的目标（世界）与权重 */
			FVector LeftTarget = FVector::ZeroVector;
			FVector RightTarget = FVector::ZeroVector;
			float LeftAlpha = 0.f;
			float RightAlpha = 0.f;
		};
		FWeaponAim WeaponAim;

		/** 武器层的持枪循环（UGinkgoWeaponAnimLayers 报）：三个循环的权重与这一帧放到的时刻（这一帧没放的为负） */
		struct FWeaponLayer
		{
			float RunAlpha = 0.f;
			float SprintAlpha = 0.f;
			float AimAlpha = 0.f;
			float UpperBodyAlpha = 0.f;
			float RunTime = -1.f;
			float SprintTime = -1.f;
			float AimTime = -1.f;
		};
		FWeaponLayer WeaponLayer;
	};

	const FDebugFrame& GetDebugFrame() const { return DebugFrame; }

	/** 腿部 IK 在 Control Rig 里执行时报一只脚（工作线程；游戏线程在求值结束以后才读） */
	void ReportLegIKFoot(const int32 Index, const FDebugFrame::FLegIKFoot& Foot)
	{
		if (Index >= 0 && Index < UE_ARRAY_COUNT(DebugFrame.LegIK))
		{
			DebugFrame.LegIK[Index] = Foot;
		}
	}

	/** 同上：骨盆的偏移与往下扫地面的次数，偏移的来历（线、上限、下限、另挪的量） */
	void ReportLegIKPelvis(const float Offset, const int32 Traces, const float Line, const float Upper, const float Lower, const float Adjust)
	{
		DebugFrame.LegIKPelvisOffset = Offset;
		DebugFrame.LegIKTraces = Traces;
		DebugFrame.LegIKPelvisLine = Line;
		DebugFrame.LegIKPelvisUpper = Upper;
		DebugFrame.LegIKPelvisLower = Lower;
		DebugFrame.LegIKPelvisAdjust = Adjust;
	}

	/** 瞄准与双手在 Control Rig 里执行时报（工作线程；游戏线程在求值结束以后才读） */
	void ReportWeaponAim(const FDebugFrame::FWeaponAim& Aim) { DebugFrame.WeaponAim = Aim; }

	/** 武器层更新时报（工作线程，在这一帧的 NativeThreadSafeUpdateAnimation 之后） */
	void ReportWeaponLayer(const FDebugFrame::FWeaponLayer& Layer) { DebugFrame.WeaponLayer = Layer; }

	/** 调试：枪自己放的片段的比重。回放记下 */
	float GetGunClipWeight() const { return GunClip.Weight; }
#endif

protected:
	/** 探身：判定阈值、退出滞回和权重过渡；在角色 AnimBP 默认值中调节 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon|Peek")
	FGinkgoPeekSettings PeekSettings;

	/** 左倾叠加权重（0–1），游戏线程由独立参考判定更新，图在转根前读取 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon|Peek")
	float PeekLeftAlpha = 0.f;

	/** 右倾叠加权重（0–1），与左倾互斥，过中心时两边均为零 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Weapon|Peek")
	float PeekRightAlpha = 0.f;

	//~ Begin UAnimInstance
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativePostEvaluateAnimation() override;
	//~ End UAnimInstance

	/** 运动：可见的根逐帧差分 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo")
	FGinkgoAnimMotion Motion;

	/** 意图：移动组件的输入加速度与由它推出的停止点、反向点 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo")
	FGinkgoAnimIntentState Intent;

	/** 状态：移动模式、蹲、滑铲、冲刺 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo")
	FGinkgoAnimMoveState MoveState;

	// ===== 移动状态机的转换条件 =====
	// 每帧在 NativeThreadSafeUpdateAnimation 里算好，转换规则只读它们（读成员变量走快速路径）。*Finished 由节点函数写，晚一帧

	/** 不在地面上：空中或别的移动模式 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bInAir = false;

	/** 在地面上、有意图 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bShouldStart = false;

	/** 急停中又按了方向、速度还快：直接回循环，不从起步放 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bShouldCycleFromStop = false;

	/** 在地面上、没有意图、还在动（快于 StopMinSpeed） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bShouldStop = false;

	/** 在地面上、没有意图、几乎不动 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bShouldIdle = false;

	/** 在地面上、意图与速度的夹角大于 PivotAngle、速度快于 PivotMinSpeed */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bShouldPivot = false;

	/** 反向过零了：速度已经转到意图的方向（夹角小于 PivotEndAngle），或片段快放完了 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bPivotFinished = false;

	/** 起步走过了 StartBlendOutDistance */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bStartFinished = false;

	/** 急停的片段快放完了（剩下不到 StateEndTime） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bStopFinished = false;

	/** 站着不动、根骨骼的偏航补偿超过 TurnInPlaceThreshold */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bShouldTurnInPlace = false;

	/** 原地转身的片段快放完了 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bTurnInPlaceFinished = false;

	/** 在滑铲（移动组件里是 Walking 的子状态）：各个地面状态与空中最先看它 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bShouldSlide = false;

	/** 滑铲结束了、还在地面上（离开地面的走空中） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bSlideEnded = false;

	/** 滑铲的进入放到 SlideLoopTime 了 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bSlideStartFinished = false;

	/** 起身放完了、有意图、在动：接循环 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bSlideExitToCycle = false;

	/** 起身放完了、有意图、几乎没动（起身时才按方向）：从起步放 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bSlideExitToStart = false;

	/** 起身放完了、没有意图、还在动 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bSlideExitToStop = false;

	/** 起身放完了、没有意图、几乎不动 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Transitions")
	bool bSlideExitToIdle = false;

	// ===== 状态机之后的节点读的 =====

	/**
	 * 根骨骼的偏航补偿（度，向右为正）：站着时胶囊跟着视角转，网格反着转回去，脚留在原地；动起来以后用弹簧收回 0。
	 * 动画蓝图里的 Rotate Root Bone 读它
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Locomotion")
	float RootYawOffset = 0.f;

	/**
	 * 朝向修正的角度（度）：移动方向相对身体、再相对所放片段的方向还差多少，起步、循环、急停时才有。
	 * 由这三个状态的节点函数在选完片段之后写，与片段同一帧；修正节点排在混合节点之前，所以换片段、换状态时的跳变由混合接住，
	 * 同一个片段里却不能跳：方向不可信时（急停停下以后）保持上一帧的
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Locomotion")
	float OrientationWarpingAngle = 0.f;

	/** 步幅修正：速度比里播放速率夹不下的那部分，循环时才不为 1 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Locomotion")
	float StrideScale = 1.f;

	/** 步幅修正的方向：速度在网格组件空间里的方向（小白人朝 +Y） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Locomotion")
	FVector StrideDirection = FVector(0.0, 1.0, 0.0);

	/**
	 * 左右倾斜（−1 向左倒满，1 向右倒满）：运动的加速度在身体左右方向上的分量除以 LeanFullAcceleration，乘上速度的比例，
	 * 只在循环与空中（含落地）时有，过弹簧。图里不直接读它，读拆开的两边的权重与冲刺的比重
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Lean")
	float LeanAmount = 0.f;

	/**
	 * 向左、向右倾斜的叠加权重：动画蓝图在根骨骼的偏航补偿之前，按网格空间叠加 GASP 的倾斜姿势（左、右各一个节点）。
	 * 同一时刻只有一边不为 0，过零时两边都几乎是 0，换边不跳
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Lean")
	float LeanLeftAlpha = 0.f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Lean")
	float LeanRightAlpha = 0.f;

	/** 倾斜姿势里冲刺那套的比重：水平速度从跑速到冲刺速度，0 到 1（跑的倾斜 13–16°，冲刺的 20–32°） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Lean")
	float LeanSprintWeight = 0.f;

	/**
	 * 腿部 IK 的权重（动画蓝图里 Control Rig 节点的 LegIKAlpha 引脚读它）：在地面上（不含滑铲）为 1，空中、滑铲为 0，
	 * 过弹簧（离地、开始滑铲时锁着的脚的修正平滑地收掉）。权重为 0 时腿部 IK 整个跳过，再执行时它自己清掉之前的锁
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|LegIK")
	float LegIKAlpha = 0.f;

	/** 移动状态机现在在哪个状态（节点函数报的，晚一帧） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Locomotion")
	EGinkgoLocomotionState LocomotionState = EGinkgoLocomotionState::Idle;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Locomotion")
	EGinkgoAnimGait Gait = EGinkgoAnimGait::Run;

	/** 所放片段的方向（起步、循环、急停、反向） */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ginkgo|Locomotion")
	EGinkgoAnimDirection Direction = EGinkgoAnimDirection::F;

	// ===== 片段 =====
	// 生成动画蓝图的命令按 GASP 的名字填好（Neutral 的那套），之后在动画蓝图的类默认值里改

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	FGinkgoGaitAnims WalkAnims;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	FGinkgoGaitAnims RunAnims;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	FGinkgoGaitAnims SprintAnims;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	FGinkgoGaitAnims CrouchAnims;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	FGinkgoStanceAnims StandAnims;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	FGinkgoStanceAnims CrouchedAnims;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	TObjectPtr<UAnimSequence> FallLoop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	FGinkgoSlideAnims SlideAnims;

	/**
	 * 起跳、走下边缘、落地（GASP 的 Jump）：构造时按 GASP 的名字填好（Neutral 的那套），动画蓝图不用动；要换在动画蓝图的类默认值里改。
	 * 动画蓝图只存与父类不同的值，这些片段不在它的依赖里：打包时 Jump 目录在 DefaultGame.ini 的 DirectoriesToAlwaysCook 里
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ginkgo|Anims")
	FGinkgoJumpAnims JumpAnims;

	// ===== 参数 =====

	/**
	 * 运动的加速度过的临界阻尼弹簧的角频率：越大越跟手，也越抖；≤ 0 不平滑。
	 * 模拟代理的可见位置每次网络更新都拐一下，未平滑的加速度在那一帧有尖刺
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo", meta = (ClampMin = 0, Units = "rad/s"))
	float AccelerationOmega = 12.f;

	/** 水平速度低于它时方向不可信：速度相对面向的角度、意图与速度的夹角取 0 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo", meta = (ClampMin = 0, Units = "cm/s"))
	float MinDirectionSpeed = 10.f;

	/** 手柄推得不满（输入的大小低于它）时用走路的片段 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, ClampMax = 1))
	float WalkInputScale = 0.6f;

	/** 松手时快于它进急停，否则直接回 Idle */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "cm/s"))
	float StopMinSpeed = 30.f;

	/** 急停中又按了方向、快于它时直接回循环 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "cm/s"))
	float CycleFromStopSpeed = 200.f;

	/** 起步只用开头这么远（距离匹配），之后转到循环（动画文档：移动比起步片段快 2–9 倍，只用得上开头的蹬地） */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "cm"))
	float StartBlendOutDistance = 40.f;

	/** 起步按距离推进时播放速率的范围：起步比片段快得多，上限放宽 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion")
	FVector2D StartPlayRateClamp = FVector2D(0.75, 3.0);

	/** 循环的播放速率的范围，超出的部分交给步幅修正（Lyra 的 0.75–1.25） */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion")
	FVector2D CyclePlayRateClamp = FVector2D(0.75, 1.25);

	/** 反向过零之后按距离推进时播放速率的范围 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion")
	FVector2D PivotPlayRateClamp = FVector2D(0.75, 2.0);

	/** 意图与速度的夹角大于它算反向 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 90, ClampMax = 180, Units = "Degrees"))
	float PivotAngle = 120.f;

	/** 快于它才做反向；更慢时起步、循环自己换方向 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "cm/s"))
	float PivotMinSpeed = 150.f;

	/** 速度转到与意图的夹角小于它，反向就做完了 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float PivotEndAngle = 60.f;

	/** 换方向的滞回：越过扇区的边界这么多度才换片段 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, ClampMax = 45, Units = "Degrees"))
	float DirectionDeadZone = 10.f;

	/** 循环里换方向、换步态时惯性化的时长 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "s"))
	float CycleBlendTime = 0.2f;

	/** 片段剩下不到这么久时算放完（急停、反向、原地转身） */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "s"))
	float StateEndTime = 0.2f;

	/** 站着时偏航补偿超过它就原地转身 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, ClampMax = 180, Units = "Degrees"))
	float TurnInPlaceThreshold = 90.f;

	/** 瞄准点：沿视角最远打多远，打不着就取这么远处 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon", meta = (ClampMin = 0, Units = "cm"))
	float AimTraceDistance = 10000.f;

	/** 瞄准点离枪口最近多远（沿视角量）：贴着墙时往外推，不把枪扭过去 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon", meta = (ClampMin = 0, Units = "cm"))
	float MinAimDistance = 150.f;

	/**
	 * 瞄准点的远近过的临界阻尼弹簧的角频率：准星扫过近处的边时命中的远近一下子变（3 m 的墙到 100 m 外），视差跟着跳几度。
	 * 弹簧追的是距离的倒数（视差与它成正比）
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon", meta = (ClampMin = 0, Units = "rad/s"))
	float AimDistanceOmega = 20.f;

	/**
	 * 瞄准点远近的倒数（1/cm）一秒最多跟着变多少：弹簧前馈连续的变化到这么快（走近墙、扫过地面），超出的留在误差里缓过去
	 * （准星扫过近处的边，一下子变了很多）。在倒数里分：视差与它成正比，贴着地面扫时距离变得飞快、倒数却变得慢。
	 * 地面上俯仰 80°/s 扫过时约 0.006，走向 5 m 外的墙约 0.002；3 m 的墙到 25 m 的墙一帧变 0.0013，30 fps 时就是 0.04
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Weapon", meta = (ClampMin = 0))
	float MaxAimInverseDistanceRate = 0.01f;

	/** 偏航补偿的上限：视角转得更多时网格被带着走 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, ClampMax = 180, Units = "Degrees"))
	float RootYawOffsetLimit = 135.f;

	/** 动起来以后偏航补偿收回 0 的临界阻尼弹簧的角频率 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "rad/s"))
	float RootYawOffsetBlendOmega = 10.f;

	/**
	 * 胶囊转得比它快的部分算"一下子转过去"（进出冲刺时朝向在一帧里换成速度或视角的方向，最多 180°；鼠标甩得很快时也是），
	 * 任何状态下都先由偏航补偿接住，网格不跟着跳，再按各状态的方式收回
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "DegreesPerSecond"))
	float SnapYawRate = 720.f;

	/** 朝向修正的上限：差得更多说明方向变了，等状态机换片段 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, ClampMax = 90, Units = "Degrees"))
	float MaxOrientationWarpingAngle = 60.f;

	/** 步幅修正的值过的临界阻尼弹簧的角频率：换片段时速度比一下子变 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Locomotion", meta = (ClampMin = 0, Units = "rad/s"))
	float StrideScaleOmega = 20.f;

	/** 滑铲的进入放到片段的这个时刻转到循环：GASP 的 Into 在约 0.6 s 最后一次落脚，到约 1.03 s 倒下去、滑稳 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Slide", meta = (ClampMin = 0, Units = "s"))
	float SlideLoopTime = 1.3f;

	/** 起身的片段从这个时刻放起：GASP 的 Out 开头约 0.2 s 还在滑 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Slide", meta = (ClampMin = 0, Units = "s"))
	float SlideExitStartTime = 0.2f;

	/** 起身的片段放到这个时刻算起来了（Out_Idle_Stand 约 0.83 s 站直，Out_Moving_Run 约 1 s 跑起来），交给平常的状态 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Slide", meta = (ClampMin = 0, Units = "s"))
	float SlideExitEndTime = 0.9f;

	/** 起身接着走时按距离推进的播放速率范围：冲刺速度下要放到约 3 倍（片段起身时只有 225–300 cm/s） */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Slide")
	FVector2D SlideExitPlayRateClamp = FVector2D(0.75, 3.0);

	/** 滑铲时网格的根转向速度的临界阻尼弹簧的角频率（转向时速度的方向慢慢变） */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Slide", meta = (ClampMin = 0, Units = "rad/s"))
	float SlideFacingOmega = 12.f;

	/** 跳跃片段按水平速度分站、走、跑、冲刺：快于这三个（cm/s）依次用走、跑、冲刺的（我们跑 476、冲刺 762，GASP 走 200、跑 500、冲刺 700） */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Air")
	FVector JumpGaitSpeeds = FVector(100.0, 350.0, 600.0);

	/** 预测的着地速度快于它用重的落地：约从 2.5 m 落下；平地起跳着地 498 cm/s */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Air", meta = (ClampMin = 0, Units = "cm/s"))
	float HeavyLandSpeed = 750.f;

	/** 站着落地、没有意图时，放到着地后这么久（冲击吸收完）才交给 Idle */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Air", meta = (ClampMin = 0, Units = "s"))
	float LandSettleTime = 0.6f;

	/** 在动的落地有意图时放到着地的那只脚再离地（下一步）才交给循环，最多放到着地后这么久 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Air", meta = (ClampMin = 0, Units = "s"))
	float LandMoveMaxTime = 0.8f;

	/** 在动的落地着地以后按走过的距离推进，播放速率的范围 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Air")
	FVector2D LandPlayRateClamp = FVector2D(0.75, 2.0);

	/** 空中竖直速度一帧里变大超过它算又起跳了（土狼起跳） */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Air", meta = (ClampMin = 0, Units = "cm/s"))
	float JumpImpulseSpeed = 100.f;

	/** 着地预测顺着轨迹最远看多久：重的落地片段着地前有 1.0 s */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Air", meta = (ClampMin = 0, Units = "s"))
	float ImpactPredictionTime = 1.5f;

	/** 着地预测把抛物线每秒分几段扫：30 段时弦与抛物线最多差 g·Δt²/8 ≈ 0.16 cm */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Air", meta = (ClampMin = 1))
	float ImpactPredictionFrequency = 30.f;

	/**
	 * 身体左右方向的加速度到这么大时倒满。实测（平滑后的加速度）：冲刺中转弯（约 160°/s）中位数约 1140、p90 约 2050；
	 * 跑着转弯 p90 约 900；空中扫射 p90 约 800
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Lean", meta = (ClampMin = 1, Units = "cm/s^2"))
	float LeanFullAcceleration = 1500.f;

	/** 水平速度低于 X 不倾斜，到 Y 时按加速度倒满：走得慢时转弯不倒 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Lean")
	FVector2D LeanSpeedRange = FVector2D(100.0, 350.0);

	/** 倾斜过的临界阻尼弹簧的角频率：进出循环、空中时目标一下子变，弹簧接住 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|Lean", meta = (ClampMin = 0, Units = "rad/s"))
	float LeanOmega = 10.f;

	/** 腿部 IK 的权重过的临界阻尼弹簧的角频率：约 0.1 s 收掉 */
	UPROPERTY(EditDefaultsOnly, Category = "Ginkgo|LegIK", meta = (ClampMin = 0, Units = "rad/s"))
	float LegIKAlphaOmega = 30.f;

private:
	/** 游戏线程读到的一帧，工作线程只读它 */
	struct FSnapshot
	{
		/** 有角色可读：编辑器的预览里没有，什么也不算 */
		bool bValid = false;

		/** 游戏线程从本次攀爬读取的纯数据；工作线程不访问动作或墙体组件。 */
		FGinkgoTraversalHandPose Traversal;

		/** 差分的输入：可见的位置与朝向、校正累计挪的量、移动组件的速度、是否清掉历史（帧长在工作线程填） */
		Ginkgo::Animation::FGinkgoMotionDifferencer::FInput Motion;
		/** 移动组件的输入加速度（世界） */
		FVector InputAcceleration = FVector::ZeroVector;
		/** 输入的大小（0–1） */
		float InputScale = 0.f;
		EGinkgoAnimMode Mode = EGinkgoAnimMode::Ground;
		bool bCrouched = false;
		bool bSprinting = false;
		/** 地面摩擦与停止速度：停止距离、反向距离用 */
		double Friction = 0.0;
		double StopSpeed = 0.0;
		/** 反向时的加速 = 地面加速系数 × wishspeed（cm/s²） */
		double PivotDeceleration = 0.0;
		/** 跑速与冲刺速度（乘过持枪倍率）：倾斜姿势里冲刺那套的比重按水平速度在这两者之间 */
		double RunSpeed = 0.0;
		double SprintSpeed = 0.0;
		/** 地面上跑速乘的总倍率（持枪乘开镜）：选走还是跑时乘在推杆大小上，开镜走得慢就放走的片段 */
		float GroundSpeedRatio = 1.f;
		/** 网格组件的世界朝向：把速度转到组件空间（步幅修正的方向） */
		FQuat MeshRotation = FQuat::Identity;
		/** 移动组件的速度（世界）：起跳、着地用。差分出来的竖直速度是整帧的平均 */
		FVector MovementVelocity = FVector::ZeroVector;
		/** 重力（cm/s²，向下为正） */
		double Gravity = 0.0;
		/** 顺着轨迹预测的着地：还有多久（s，没扫到为负）与那时的下落速度（cm/s，向下为正） */
		double TimeToImpact = -1.0;
		double ImpactSpeed = 0.0;
		/** 上一帧的姿势里左脚掌比右脚掌低（组件空间）：落地时它先着地 */
		bool bLeftFootLower = true;
		/** 持枪状态：本地控制的角色读武器组件的；别的角色第 3 批随移动数据复制，现在是徒手 */
		FGinkgoHoldState Hold;

		/** 当前装配的枪口位置（枪空间 cm），游戏线程随持枪状态采集，动画线程只读 */
		FVector MuzzleOffset = FVector::ZeroVector;

		/** 当前装配的左手接触修正（枪空间 cm），换模块或切枪时从武器组件重新采集 */
		FVector SupportHandOffset = FVector::ZeroVector;

		/** 视角（度）：本地控制的角色取控制器的（不带相机的抖动、踢动），别的角色取复制的俯仰与角色的偏航 */
		double ViewYaw = 0.0;
		double ViewPitch = 0.0;
		/** 瞄准点（世界）：本地控制、手上有枪时才打线 */
		bool bHasAimPoint = false;
		FVector AimPoint = FVector::ZeroVector;
		/** 第一人称开镜：瞄具的光轴对准瞄准方向的比重（FGinkgoWeaponRigData::SightAlpha）与瞄准方向（世界，单位向量） */
		float SightAlpha = 0.f;
		FVector AimDirection = FVector::ForwardVector;
		/** 枪上的蒙太奇的权重（这一帧的，蒙太奇在 NativeUpdateAnimation 之前推进过了） */
		float GunMontageWeight = 0.f;
		/** 枪的踢动（FGinkgoWeaponRigData::GunKick），这一帧的 */
		FTransform GunKick = FTransform::Identity;
		/** 右手跟着片段走的比重（拉栓：蒙太奇的权重）与这一刻手相对枪的变换（FGinkgoWeaponStep::RightHandTrack） */
		float RightHandTrackWeight = 0.f;
		FTransform RightHandTrack = FTransform::Identity;
		/** 片段让左手握枪的比重：曲线 HandIK_L 按片段的比重混过，没有这条曲线的片段不管 */
		float LeftHandClipGrip = 1.f;
		/** 护木现在往后走了多远（cm，FGinkgoWeaponRigData::LeftHandSlide） */
		float LeftHandSlide = 0.f;
		float HoldPoseAlpha = 1.f;
		/** 枪挂在手上（不在背上） */
		bool bGunInHand = false;

		/** 游戏线程采样的放枪进度（0–1），动画线程据此把枪骨混到 BenchRest */
		float BenchGunAlpha = 0.f;

		/** 放枪、取枪时双手的 IK 权重（0–1），无工作台时为 1 */
		float BenchGrip = 1.f;

		/** 游戏线程采样的安装动作姿势，世界坐标和权重语义见 FGinkgoAttachmentHandPose */
		FGinkgoAttachmentHandPose BenchInstall;

		/** 投掷、腰部抓取和受闪遮眼的左臂姿势。 */
		FGinkgoFlashArmPose FlashArm;

		/** 桌上枪位的世界变换，仅 BenchGunAlpha 大于零时参与姿势求解 */
		FTransform BenchRest = FTransform::Identity;

		/** 捡枪抓住以后的收拢（FGinkgoWeaponRigData::PickupSettle），1 是不在收拢；同 FGinkgoWeaponRigData 的朝向与挂点 */
		float PickupSettle = 1.f;
		FGinkgoPickupSettleStart PickupStart;
		FQuat BodyRotation = FQuat::Identity;
		/** 伸手：东西换到这一帧，曲线与权重是蒙太奇这一刻的（UpdateHandReach） */
		FGinkgoHandReachRigData Reach[2];
		/** 被顶掉、还在淡出的伸手，同上 */
		TArray<FGinkgoHandReachRigData, TInlineAllocator<2>> FadingReach[2];
	};

	/** @brief 游戏线程上：伸手的东西换到这一帧，求两条曲线与权重；蒙太奇实例没了就清掉（被顶掉的也一样） */
	void UpdateHandReach();

	/** 持枪的层盖多少（FGinkgoWeaponAnimData::HoldPoseAlpha）：游戏线程上，蒙太奇这一刻的曲线 */
	float ComputeHoldPoseAlpha() const;

	/**
	 * @brief 枪上的蒙太奇的权重：盖住枪的持法的那些。叠加的（开火、拉栓）不算：枪照姿态走，手照常在枪上，见 Docs/Design/ActionStates.md「开火与拉栓：实现」
	 */
	float ComputeGunMontageWeight() const;

	/** @brief 叠加的枪上的动作这一刻右手的轨迹写进快照、枪栓的曲线记下来，到了时刻的声音放掉（游戏线程上） */
	void UpdateGunActionClips(AGinkgoCharacter& Character);

	/**
	 * @brief 枪上的声音：片段的曲线走到一个时刻（枪栓到头、压进去一发）减去声音的提前量时放，撞击点正好落在那一帧。
	 *        只看在放的片段，开始淡出以后不放：拉到一半收枪不会再响"推到底"
	 * @param Position - 片段放到哪里（s）
	 */
	void UpdateGunCues(AGinkgoCharacter& Character, const UGinkgoWeaponDefinition& Weapon, const UAnimMontage& Montage, int32 InstanceID, float Position);

	/** 上一帧在算声音的蒙太奇实例与它放到哪里：换了实例从这一帧算起 */
	int32 GunCueInstance = INDEX_NONE;
	float GunCuePosition = 0.f;

	/** 带枪栓曲线的片段的比重与片段上枪栓的姿势（淡出时停在淡出开始那一帧的）：摆枪栓用（AGinkgoCharacter::ApplyWeaponBolts） */
	float BoltClipWeight = 0.f;
	FGinkgoBoltPose BoltClip;

	/** 枪自己放的片段（配了 FGinkgoWeaponStep::GunTrack 的步）：摆枪用（AGinkgoCharacter::ApplyWeaponBolts） */
	FGinkgoGunClip GunClip;

	/** 层上一帧跑的循环的权重：单手跑的枪左手放开多少（ReportRunAlpha） */
	float LayerRunAlpha = 0.f;

	/**
	 * @brief 一只手伸着的那个绑的蒙太奇实例：绑过的按 ID 找；没绑的绑上这个蒙太奇在放的实例（没有在放的用淡出中的），被顶掉、在淡出的那几个不算
	 * @return 实例没了（放完、淡出完）为空
	 */
	FAnimMontageInstance* FindHandReachInstance(int32 Index);

	/**
	 * @brief 一个伸手在这一帧给 Control Rig 的：东西换到这一帧（冻住的跟着身体，没了的停在最后的位置），方位约束，两条曲线在实例这一刻的值（缓过的），
	 *        实例的混合权重。实例开始淡出（拾取结局了）时把东西冻在那一刻
	 */
	FGinkgoHandReachRigData MakeHandReachRigData(const FGinkgoHandReach& Reach, const FAnimMontageInstance& Instance, EGinkgoHand Hand, const FVector& ShoulderInComponent,
		FVector& InOutLastItem, TOptional<FVector>& InOutFrozen) const;

	/** 两只手伸着的（下标是 EGinkgoHand） */
	TOptional<FGinkgoHandReach> HandReach[2];

	/**
	 * 伸着的那个冻住的东西（网格组件的坐标）：手抓住了东西（FreezeHandReach）、拾取结局了（蒙太奇实例开始淡出）以后，东西不再跟着它在世界里的位置走，
	 * 冻在那一刻相对身体的位置。跑得快时不然手被拖向身后的那一点（抓住以后东西已经在手里；结局以后淡出的 0.25 s 里追着跑过的东西，胸口拧 39°、头转 72°）
	 */
	TOptional<FVector> HandReachFrozen[2];

	/**
	 * 设上伸手时那只手的肩关节（网格组件的坐标）：方位约束从这里量，跟着网格组件走。不用这一帧的肩关节：前倾与全身 IK 把它拉向东西，
	 * 量出来的方位又反过来改伸手（同拾取判走开的那处）
	 */
	FVector HandReachShoulder[2] = { FVector::ZeroVector, FVector::ZeroVector };

	/** 伸着的那个绑的蒙太奇实例的 ID：第一次找时绑上（INDEX_NONE 是还没绑）。同一个蒙太奇连着放时有两个实例，按资源找会找错 */
	int32 HandReachInstance[2] = { INDEX_NONE, INDEX_NONE };

	/** 东西最后一次的位置（世界）：东西没了时停在这里 */
	FVector HandReachLastItem[2] = { FVector::ZeroVector, FVector::ZeroVector };

	/** 被新的伸手顶掉、还在随自己的蒙太奇实例淡出的（同一只手连着拾取时） */
	struct FFadingHandReach
	{
		FGinkgoHandReach Reach;
		int32 InstanceID = INDEX_NONE;
		FVector LastItem = FVector::ZeroVector;
		TOptional<FVector> Frozen;
		FVector Shoulder = FVector::ZeroVector;
	};
	TArray<FFadingHandReach, TInlineAllocator<2>> FadingHandReach[2];

	/** 根骨骼的偏航补偿这一帧怎么变：各状态的节点函数报，下一帧用。胶囊一下子转过去的部分三种都接住 */
	enum class ERootYawMode : uint8
	{
		/** 动起来了：用弹簧收回 0 */
		BlendOut,
		/** 急停还没停下：胶囊怎么转都接住，身体保持朝着跑的方向停下来，停下以后再原地转身 */
		Hold,
		/** 站着：跟着胶囊反着转，超过上限时网格被带着走 */
		Accumulate,
		/**
		 * 滑铲：胶囊照常面向视角，网格的根转向速度（滑铲的片段只有向前的）。胶囊怎么转都接住（同 Hold），
		 * 再用弹簧追速度的方向；几乎不动时方向不可信，只接住
		 */
		FaceVelocity,
	};

	/**
	 * @brief 游戏线程：在空中时顺着轨迹预测着地，写进快照
	 * 从胶囊下半球的球心出发，按移动组件的速度与重力分段扫一个胶囊半径的球（PredictProjectilePath），扫到能站的地面就是着地。
	 * 不算接下来的空中加速，每帧重算
	 */
	void PredictImpact(const ACharacter& Character, const UCharacterMovementComponent& Movement);

	/**
	 * @brief 游戏线程：瞄准点。相机在所有 Actor 更新完以后才算，这里推算这一帧的相机（AGinkgoPlayerCameraManager::PredictViewLocation），
	 *        AI 没有玩家相机时改从角色眼位取视线，沿控制器的平滑瞄准方向求交，仍交给同一套 Control Rig 对准枪口。
	 *        从那里沿瞄准方向（AimRotation，腰射时是视线后面拖的弹簧）打线；最远 AimTraceDistance，离枪口不到 MinAimDistance 的往外推。见 Docs/Design/Weapons.md「手与瞄准」
	 */
	void UpdateAimPoint(const ACharacter& Character, const FRotator& View, const FRotator& AimRotation, float DeltaSeconds);

	/** 瞄准点远近的弹簧：距离的倒数（1/cm）与它的速度，上一帧打线（夹过）的距离；手上没枪、不是本地控制时作废，下次直接放到位 */
	double AimInverseDistance = 0.0;
	double AimInverseDistanceVelocity = 0.0;
	double LastAimDistance = 0.0;
	bool bAimDistanceValid = false;

	/** 工作线程：差分、加速度的弹簧 */
	void UpdateMotion(float DeltaSeconds);

	/** 正常移动时推进偏航补偿；攀爬进入时从上一帧可见值随全身覆盖渐退，脚部交接后恢复正常转向。 */
	void UpdateRootYaw(float DeltaSeconds);

	/** 工作线程：意图与停止点、反向点（读这一帧的运动） */
	void UpdateIntent();

	/** 工作线程：偏航补偿、转换条件、步幅修正与倾斜（读上一帧节点函数报的状态）；朝向修正由节点函数写 */
	void UpdateLocomotion(float DeltaSeconds);

	/**
	 * 工作线程：左右倾斜。加速度转到身体的方向上（同步幅修正：网格组件空间，再按偏航补偿反着转，倾斜节点在转根骨骼之前），
	 * 取左右的分量；蹲着、滑铲、起步、急停、反向、原地转身时不倾斜（这些片段自己带着身体的动作）
	 */
	void UpdateLean(float DeltaSeconds);

	/** 工作线程：给武器层的一帧（读这一帧的偏航补偿，所以在 UpdateLocomotion 之后） */
	void UpdateWeaponAnim();

	/** 节点函数报这一帧在哪个状态、偏航补偿怎么变 */
	void ReportState(EGinkgoLocomotionState State, ERootYawMode RootYawMode);

	EGinkgoAnimGait SelectGait() const;

	/**
	 * @brief 起步、急停的片段按进入时的步态选，蹲下、站起以后步态的站姿与胶囊对不上：按现在的输入重选步态
	 * @return 步态换了，要换成新站姿的片段
	 */
	bool FollowStance();

	/**
	 * @brief 急停用的 FollowStance：站起来时用走的急停。速度还是蹲着走的（最多 159 cm/s，GASP 走的片段 150–200 cm/s），
	 *        跑的急停停下时脚跨得远（身前 52 cm），从蹲着走混过去脚被带着走（slide 路线松开蹲的同时松手，820 cm/s）
	 */
	bool FollowStanceToStop();

	/** 急停：按步态与方向选片段（左右脚两套里挑着地的脚对得上的），OutEntryTime 是对到预测的停止点的时刻 */
	UAnimSequence* SelectStopAnim(float& OutEntryTime) const;

	/** 原地转身：现在的站姿那一套里，进入时选的那一边、那个角度的片段 */
	UAnimSequence* GetTurnInPlaceAnim() const;

	/** 滑铲的起身：按现在的站姿选，接着走的（bMoving）站着时按推得满不满选走或跑 */
	UAnimSequence* SelectSlideExitAnim(bool bMoving) const;

	/** Air 状态的节点函数报的四种 */
	static bool IsAirState(EGinkgoLocomotionState State);

	/** 跳跃片段的步态：按水平速度 */
	EGinkgoJumpGait SelectJumpGait(double HorizontalSpeed) const;

	/**
	 * @brief 落地片段：方向取 Direction，步态按水平速度，轻重按着地速度，左右脚取先着地的那只（片段着地时接触曲线先起来的）
	 * @param bLeftFirst - 左脚先着地
	 */
	UAnimSequence* SelectLandAnim(double ImpactSpeed, bool bLeftFirst) const;

	/** 选好落地片段以后：记下着地的时刻、在不在动、什么时候交出去 */
	void BeginLand(const UAnimSequence& Land);

	/** @brief 左右脚两个里挑一个：片段在 Time 时左脚着地（接触曲线大）与 bLeft 对得上的；两个都对得上或都对不上时取左脚那个 */
	static UAnimSequence* ChooseByContact(const FGinkgoFootAnims& Pair, TFunctionRef<float(const UAnimSequence&)> Time, bool bLeft);

	/** 片段里第一个这个名字的同步标记的时刻；没有时为负 */
	static float MarkerTime(const UAnimSequence& Sequence, FName MarkerName);

	const FGinkgoGaitAnims& GetGaitAnims(EGinkgoAnimGait InGait) const;

	/** 某个方向用哪种步态的片段：冲刺只有向前，身体还没转过来、或者往回时用跑步的 */
	const FGinkgoGaitAnims& GetGaitAnims(EGinkgoAnimGait InGait, EGinkgoAnimDirection InDirection) const;

	/** 相对面向的角度换成相对身体的：网格还按偏航补偿转着，腿要对着身体选方向（Lyra 的 LocalVelocityDirectionAngleWithOffset） */
	float ToBodyAngle(float AngleToFacing) const;

	/**
	 * @brief 起步、循环、急停的节点函数在选完片段之后调：按移动方向与所放片段的方向写朝向修正的角度
	 * @param bUseIntentWithoutVelocity - 速度还没起来时看意图（起步、落地接循环），与选片段时看的一致。
	 *        不看时（急停）方向不可信就保持上一帧的，腿以停下时的朝向放完收势
	 */
	void UpdateOrientationWarping(bool bUseIntentWithoutVelocity);

	const FGinkgoStanceAnims& GetStanceAnims() const;

	/** 起步看的方向：意图相对身体的角度（度，向右为正）；没有意图时取速度的 */
	float GetMoveAngle() const;

	/**
	 * @brief 左右脚两套片段里挑一个：进入时（距离曲线等于 −Distance 的时刻）着地的脚与现在着地的脚对得上的
	 * @note 现在着地的是哪只脚读上一帧的姿势的接触曲线；两套都对得上或都对不上时取左脚那套
	 */
	UAnimSequence* ChooseByPlantedFoot(UAnimSequence* LeftFootAnim, UAnimSequence* RightFootAnim, float Distance) const;

	/** 片段在某个时刻的曲线值；没有这条曲线时为 Default */
	static float EvaluateCurve(const UAnimSequence* Sequence, FName CurveName, float Time, float Default = 0.f);

	/**
	 * @brief 现在着地的、或者下一步要落地的是不是左脚
	 * @note 先看上一帧姿势的接触曲线；两只脚都在空中（跑步腾空）时看所放片段的下一个同步标记；都没有时算左脚
	 */
	bool IsLeftFootNext() const;

	/**
	 * 记下所放的片段：滑铲的进入要知道下一步落哪只脚；武器层的持枪循环跟着它；调试显示与回放轨迹也用。
	 * Time 是节点函数这时读到、给的时刻：求值器就在这个时刻求姿势，播放器的还没推进（之后 RecordPlayerTime）
	 */
	void RecordSequence(const UAnimSequence* Sequence, float Time, float PlayRate);

	/**
	 * 播放器的节点函数在 RecordSequence 之后调：求姿势时的时刻是这一帧推进以后的。照引擎推进不在同步组里的播放器
	 * （UAnimSequenceBase::TickAssetPlayer：播放速率 × 片段的 RateScale × 帧长，FAnimationRuntime::AdvanceTime 绕回），
	 * 推进在整张图更新完以后才做，这里先算出来。播放速率的基数、缩放与夹取取默认（不改）
	 */
	void RecordPlayerTime(const FAnimUpdateContext& Context, const FSequencePlayerReference& Player);

	FSnapshot Snapshot;

	/** 给武器层的一帧：UpdateWeaponAnim 写 */
	FGinkgoWeaponAnimData WeaponAnim;

	/** 给 Control Rig 里瞄准与双手的一帧：UpdateWeaponAnim 写 */
	FGinkgoWeaponRigData WeaponRig;

	/** 游戏线程读取上一轮求值的参考枪口并射线检测，更新当前帧的探身权重 */
	void UpdatePeek(const AGinkgoCharacter& Character, const FRotator& View, const FRotator& AimRotation, float DeltaSeconds);

	/** 探身方向、退出计时和权重弹簧；游戏线程拥有 */
	FGinkgoPeekState PeekState;

	/** 当前求值的未探身输入，线程安全更新先置为无效，捕获节点随后填入 */
	FGinkgoPeekReference PeekReference;

	/** 最近一次求值完成的参考枪组件空间变换；游戏线程用当前网格变换带到世界 */
	FTransform PeekGun = FTransform::Identity;

	/** 上一次参考查询对应的武器；换武器立即清掉旧姿势，避免用旧枪口驱动新武器 */
	TWeakObjectPtr<const UGinkgoWeaponDefinition> PeekWeapon;

	/** 上次求值已生成参考枪口，游戏线程每消费一次便清零，避免跳过求值时沿用旧数据 */
	bool bPeekGunReady = false;

	/** 当前游戏线程快照允许捕获参考姿势；工作线程仅在为真时多做一次小范围瞄准求解 */
	bool bPeekProbeRequested = false;

	/** 最近一次有效参考射线的水平偏差（度）；查询失效时清零，供调试和自动化检查 */
	double PeekErrorDegrees = 0.0;

	Ginkgo::Animation::FGinkgoMotionDifferencer Differencer;

	/** 运动的加速度：值是平滑后的加速度 */
	TGinkgoCriticalSpring<FVector> AccelerationSpring;

	/** 上次动画更新是否受攀爬根运动驱动；动画线程在更新末尾写入，用于保存入场偏航并滤掉交接帧最后一段攀爬位移。 */
	bool bTraversalWasDriving = false;

	/** RequestCut 之后还没有被快照取走 */
	bool bCutRequested = false;

	/** 节点函数这一帧报的状态与偏航补偿的方式，下一帧生效 */
	EGinkgoLocomotionState ReportedState = EGinkgoLocomotionState::Idle;

	ERootYawMode ReportedRootYawMode = ERootYawMode::Accumulate;

	/** 偏航补偿收回 0 时的速度（度/秒） */
	double RootYawOffsetVelocity = 0.0;

	/** 本次攀爬第一帧之前的根偏航补偿（度）；动画线程在根运动进入时捕获，直到下一次攀爬覆盖。 */
	float TraversalEntryRootYaw = 0.f;

	/** 正在放的原地转身的片段与它的时刻（节点函数记下），偏航补偿按它的剩余转角还回去 */
	const UAnimSequence* TurnInPlaceSequence = nullptr;

	float TurnInPlaceTime = 0.f;

	/** 原地转身进入时选的那一边（向右）与角度（45°–180° 的第几个）：蹲下、站起时换成另一套的同一个 */
	bool bTurnInPlaceRight = false;

	int32 TurnInPlaceAngleIndex = INDEX_NONE;

	/** 正在放的原地转身是蹲着的那一套 */
	bool bTurnInPlaceCrouched = false;

	/** 起步以来走过的距离 */
	float StartDistanceTraveled = 0.f;

	/** 步幅修正的目标（循环的节点函数给），StrideScale 用弹簧追它 */
	float StrideScaleTarget = 1.f;

	double StrideScaleVelocity = 0.0;

	/** 倾斜的弹簧的速度（每秒） */
	double LeanVelocity = 0.0;

	/** 腿部 IK 的权重的弹簧的速度（每秒） */
	double LegIKAlphaVelocity = 0.0;

	/** 意图相对面向的角度，这一帧的 */
	float IntentLocalAngle = 0.f;

	/** 起身的片段放到 SlideExitEndTime 了（节点函数写；离开起身的状态时清掉） */
	bool bSlideExitFinished = false;

	/** 起跳离地时的竖直速度：上升时按现在的竖直速度与它之比对到片段的最高点 */
	double JumpTakeoffSpeed = 0.0;

	/** 落地片段的着地时刻（Impact 标记）；还没选落地片段时为负 */
	float LandImpactTime = -1.f;

	/** 落地片段在动（走、跑、冲刺）：着地后按走过的距离推进，有意图时放到下一步才交出去 */
	bool bLandMoving = false;

	/**
	 * 落地片段放到这里交出去：在动的是先着地的那只脚再离地（下一步，有意图时），站着的是着地后 LandSettleTime（没有意图时）。
	 * 选落地片段时算好（BeginLand）
	 */
	float LandHandoffTime = -1.f;

	/** 已经着地（落地片段放过了着地时刻） */
	bool bLanded = false;

	/** 落地还没交出去：Air 的三个出口（循环、急停、Idle）先不成立。节点函数写，下一帧 UpdateLocomotion 读 */
	bool bLandHold = false;

	/** 上一帧移动组件的竖直速度：空中一下子变大是又起跳了 */
	double PreviousVerticalSpeed = 0.0;

	/** 所放的片段与它的时刻（RecordSequence 记下） */
	const UAnimSequence* PoseSequence = nullptr;

	float PoseSequenceTime = 0.f;

	/** 所放的片段这一帧求姿势时的时刻（RecordSequence、RecordPlayerTime 记下） */
	float LocomotionTime = 0.f;

	/**
	 * @brief 评估之后（游戏线程）：所放的片段这一帧跨过的脚的标记，给本地角色的相机上下文发落脚的事件（Movement.Event.Footstep），
	 * 时刻按跨过的几成插在这一帧里。换了片段的那一帧不比：新片段的时刻是按步伐对上的，不是走过来的
	 */
	void PushFootsteps();

	/** 这一帧更新的帧长：落脚的事件按它把片段的时刻换成世界时间 */
	float UpdateDeltaSeconds = 0.f;

	/** 落脚：上一帧所放的片段、它的时刻，上一次落的脚（连着同名的只算第一个） */
	const UAnimSequence* FootstepSequence = nullptr;

	float FootstepTime = 0.f;

	FName LastFootstep;

#if !UE_BUILD_SHIPPING
	/** 评估之后：ginkgo.showAnim 开着时画 DrawAnimDebug */
	void PostEvaluateDebug();

	/** ginkgo.showAnim：脚下的箭头与停止点、反向点，本地角色再加几行屏幕文字（实现在 _Debug.cpp） */
	void DrawAnimDebug() const;

	FDebugFrame DebugFrame;
#endif
};

#pragma once

#include "CoreMinimal.h"
#include "Rigs/RigHierarchyCache.h"
#include "Units/RigUnit.h"
#include "RigUnit_GinkgoLegIK.generated.h"

/**
 * @struct FRigUnit_GinkgoLegIK_Foot : 腿部 IK 的一只脚
 * 两骨骼 IK 的末端（脚踝，大腿、小腿取它往上两级）、着地时锁在世界里的脚掌、跟着脚踝挪的 IK 骨骼，与这只脚读的曲线。
 * 曲线要在 Control Rig 的层级里有同名的元素，动画蓝图的 Control Rig 节点才会把姿势里的值传进来
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FRigUnit_GinkgoLegIK_Foot
{
	GENERATED_BODY()

	/** 脚踝：两骨骼 IK 的末端 */
	UPROPERTY(EditAnywhere, Category = "Foot")
	FRigElementKey Foot = FRigElementKey(NAME_None, ERigElementType::Bone);

	/** 脚掌：着地时锁在世界里的那一点 */
	UPROPERTY(EditAnywhere, Category = "Foot")
	FRigElementKey Ball = FRigElementKey(NAME_None, ERigElementType::Bone);

	/** 跟着脚踝一起挪的 IK 骨骼（小白人的 ik_foot_l/r）：之后的节点读到的与脚一致 */
	UPROPERTY(EditAnywhere, Category = "Foot")
	FRigElementKey IKFoot = FRigElementKey(NAME_None, ERigElementType::Bone);

	/** 接触曲线（GASP 的 contact_l/r） */
	UPROPERTY(EditAnywhere, Category = "Foot")
	FName ContactCurve;

	/** 导入时烘焙的预测落脚：还有多久落下（片段里的时间，s） */
	UPROPERTY(EditAnywhere, Category = "Foot")
	FName LandInCurve;

	/** 导入时烘焙的预测落脚：落下那一刻脚掌在根骨骼空间里的位置（cm） */
	UPROPERTY(EditAnywhere, Category = "Foot")
	FName LandXCurve;

	UPROPERTY(EditAnywhere, Category = "Foot")
	FName LandYCurve;

	/** 关掉这只脚的曲线（GASP 跳跃片段里的 disableleftfootik）：高过 0.5 时这只脚不锁 */
	UPROPERTY(EditAnywhere, Category = "Foot")
	FName DisableCurve;
};

/** 一只脚锁没锁：育碧的锁住、滑，加上离地或差得太多以后修正收回 0 的那一段，与着地了还没锁上的那一段 */
UENUM()
enum class EGinkgoLegIKLock : uint8
{
	/** 照动画 */
	Free,
	/** 着地：脚掌锁在世界里，朝向照动画 */
	Locked,
	/** 着地：动画把脚往外拽超过 SlideDistance，锁住的位置往动画那边滑（限速） */
	Sliding,
	/** 离地，或动画差得超过 UnlockDistance：修正在 ReleaseTime 里收回 0 */
	Releasing,
	/**
	 * 着地了，但动画的脚掌在世界里还快过 LockSpeed、还在减速（换片段的混合把脚扫过去，接触曲线先到了）：先不锁、照动画走，
	 * 修正照 Releasing 收回；慢下来、不再减速或等满 MaxSettleTime 再锁。排在最后，前四种的值不变（回放轨迹、测量脚本按值认）
	 */
	Settling,
};

/**
 * 一个追目标的高度：临界阻尼弹簧追的是与目标之差，并带着目标自己连续变化的速率（前馈）。坡、楼梯上连续地变的照样跟上、不落后，
 * 换了来源、预测变了这类跳变按 ω 缓过去
 */
USTRUCT()
struct CASEGINKGO_API FRigUnit_GinkgoLegIK_Track
{
	GENERATED_BODY()

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	double Value = 0.0;

	UPROPERTY()
	double Velocity = 0.0;
};

/** 一只脚跨帧的状态（RigUnit 的隐藏成员，每个 Control Rig 实例一份） */
USTRUCT()
struct CASEGINKGO_API FRigUnit_GinkgoLegIK_FootState
{
	GENERATED_BODY()

	/** 脚踝、脚掌、IK 骨骼在层级里的下标（大腿、小腿取脚踝往上两级） */
	UPROPERTY()
	FCachedRigElement CachedFoot;

	UPROPERTY()
	FCachedRigElement CachedBall;

	UPROPERTY()
	FCachedRigElement CachedIKFoot;

	UPROPERTY()
	bool bPlanted = false;

	UPROPERTY()
	EGinkgoLegIKLock Lock = EGinkgoLegIKLock::Free;

	/** 锁住的脚掌在世界里的位置 */
	UPROPERTY()
	FVector LockLocation = FVector::ZeroVector;

	/** 上一帧动画想要的脚掌在世界里的位置：滑的时候不比动画自己挪得快 */
	UPROPERTY()
	FVector PreviousAnimBall = FVector::ZeroVector;

	UPROPERTY()
	bool bHasAnimBall = false;

	/** 上一帧动画想要的脚掌在世界里的水平速度（cm/s）：着地以后看它还在不在减速（Settling） */
	UPROPERTY()
	float PreviousAnimBallSpeed = 0.f;

	/** 着地以后等了多久还没锁上（Settling，s） */
	UPROPERTY()
	float SettleElapsed = 0.f;

	/** 这一帧加给脚的修正（世界，水平） */
	UPROPERTY()
	FVector Correction = FVector::ZeroVector;

	/** 开始收回时的修正与已经收了多久 */
	UPROPERTY()
	FVector ReleaseFrom = FVector::ZeroVector;

	UPROPERTY()
	float ReleaseElapsed = 0.f;

	/** 上一帧的"还有多久落下"与它往下走的速率（片段时间 / 真实时间） */
	UPROPERTY()
	float PreviousLandIn = -1.f;

	UPROPERTY()
	float LandRate = 1.f;

	/**
	 * 这一步的"还有多久落下"可信了：离地那一刻曲线在一个键里从 0 升到整步的时长，升的时候的值不对；
	 * 曲线开始往下走（这一步的第二帧起）才用，速率也是那时量的
	 */
	UPROPERTY()
	bool bLandInTrusted = false;

	/** 预测：还有多久落下（真实时间，s；着地时为负）与落在世界里的哪里 */
	UPROPERTY()
	float TimeToLand = -1.f;

	UPROPERTY()
	FVector PredictedLanding = FVector::ZeroVector;

	/** 地形（第三批）：上一次着地的点（地面上，锁住的脚掌正下方）与那里的法线，摆动时脚的路径从这里起 */
	UPROPERTY()
	bool bHasAnchor = false;

	UPROPERTY()
	FVector Anchor = FVector::ZeroVector;

	UPROPERTY()
	FVector AnchorNormal = FVector::UpVector;

	/** 预测的落脚点那里的地面高度与法线 */
	UPROPERTY()
	bool bHasLanding = false;

	UPROPERTY()
	double LandingZ = 0.0;

	UPROPERTY()
	FVector LandingNormal = FVector::UpVector;

	/** 这一帧脚下的地面高度（世界）：着地时是锁住的点正下方，摆动时是路径上的；跳变缓过去以后的 */
	UPROPERTY()
	bool bHasTerrain = false;

	UPROPERTY()
	double TerrainZ = 0.0;

	/**
	 * 脚下的地面高度跳变时（换了来源、路径变了、着地落的高度与预测不同）缓过去；摆动时前馈的是脚沿着这一帧的路径走带来的变化。
	 * 上一帧脚掌在路径上走到哪了（没有时为负）
	 */
	UPROPERTY()
	FRigUnit_GinkgoLegIK_Track TerrainTrack;

	UPROPERTY()
	double PreviousAlong = -1.0;

	/** 上一帧照脚下扫到的地面高度（没有时 bHasPreviousSupport 为假）与脚掌要去的水平位置：脚下扫的那几种也有前馈 */
	UPROPERTY()
	bool bHasPreviousSupport = false;

	UPROPERTY()
	double PreviousSupport = 0.0;

	UPROPERTY()
	bool bHasPreviousBall = false;

	UPROPERTY()
	FVector2D PreviousBall = FVector2D::ZeroVector;

	/** 着地时脚跟、脚尖下面的地面高出脚掌那个面（台阶的边），脚绕脚掌俯仰多少；离地以后按 FitWeight 收掉 */
	UPROPERTY()
	FQuat FitPitch = FQuat::Identity;

	UPROPERTY()
	float FitWeight = 0.f;

	/** 软 IK 用多少（0–1）：离地以后 SoftBlendTime 里升到 1，着地以后降回 0，不在离地、着地那一帧突然变 */
	UPROPERTY()
	float SoftWeight = 0.f;
};

/** 腿部 IK 跨帧的状态：每只脚一份，加上根骨骼的差分 */
USTRUCT()
struct CASEGINKGO_API FRigUnit_GinkgoLegIK_WorkData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FRigUnit_GinkgoLegIK_FootState> Feet;

	UPROPERTY()
	FCachedRigElement CachedRoot;

	/** 上一次执行的帧：隔了帧（权重为 0 的那些帧不算）就不作数 */
	UPROPERTY()
	uint64 LastFrame = 0;

	UPROPERTY()
	bool bHasRoot = false;

	UPROPERTY()
	FTransform PreviousRoot = FTransform::Identity;

	UPROPERTY()
	FVector RootVelocity = FVector::ZeroVector;

	/** 速度方向的转速与根骨骼的偏航速率（度/秒） */
	UPROPERTY()
	float VelocityTurnRate = 0.f;

	UPROPERTY()
	float RootYawRate = 0.f;

	/** 骨盆、手的 IK 根在层级里的下标 */
	UPROPERTY()
	FCachedRigElement CachedPelvis;

	UPROPERTY()
	FCachedRigElement CachedHandIKRoot;

	/**
	 * 骨盆下面的地面（世界里的高度，两只脚的锚连成的线在骨盆正下方）与它的速度：临界阻尼弹簧追线，带着线自己的变化率（前馈），
	 * 坡、楼梯上不落后，换锚时的跳变缓过去
	 */
	UPROPERTY()
	FRigUnit_GinkgoLegIK_Track PelvisBase;

	/** 骨盆在那条线以外另挪的量（cm）与它的速度：腿够不着时往下（负），腿要折断时往上（正） */
	UPROPERTY()
	double PelvisAdjust = 0.0;

	UPROPERTY()
	double PelvisAdjustVelocity = 0.0;

	/** 加给骨盆的竖直偏移（cm，相对动画） */
	UPROPERTY()
	double PelvisOffset = 0.0;
};

/**
 * @struct FRigUnit_GinkgoLegIK : 预测式的腿部 IK（第四步），Control Rig 里的节点，设计见 Docs/Design/AnimationBlueprint.md 的"脚：预测式的腿部 IK"
 * 2026-10-29 你说"改成RigUnit吧"：脚、手（以后的全身 IK）、瞄准都收进同一个 Control Rig。逻辑与原来的动画图节点相同：
 * - 着地按接触曲线的回差（高过 PlantOn 着地、低过 PlantOff 离地）；着地那一刻记下脚掌在世界里的位置，之后锁住，
 *   动画把它拽开超过 SlideDistance 时锁往动画那边滑（每秒最多 SlideSpeed，也不比动画这一帧挪得多），超过 UnlockDistance 放开；
 *   离地以后修正在 ReleaseTime 里收回 0。修正只在水平面上（平地），脚整个平移，两骨骼 IK 把腿接上，脚的朝向照动画。
 *   着地那一刻动画的脚掌还快过 LockSpeed 的（换片段的混合把脚扫过去）先照动画走，等它慢下来再锁（Settling），不锁在半路上。
 *   膝盖往哪边弯照动画：动画里的膝盖跟着整条腿转过去（"大腿根 → 动画的脚踝"转到"大腿根 → 目标"的那个旋转），目标挪得多时膝盖也不翻边。
 * - 摆动时按导入时烘焙的曲线预测这一步落在哪：还有多久落下（片段里的时间，按曲线自己往下走的速率换成真实时间），
 *   根骨骼按现在的速度、速度的转向与偏航速率走到那时，再加上落脚在根骨骼空间里的位置。
 * - 地形（第三批，育碧的做法）：动画的地面是根骨骼（胶囊底）的高度，脚在动画里高出它多少，放到地面上就高出地面多少。
 *   着地的脚放在脚掌正下方的地面上，按那个面的法线倾斜；脚跟、脚尖下面的地面高出那个面（踩在台阶边上）时绕脚掌俯仰，合起来有上限。
 *   摆动的脚走上一次着地的点到预测落脚点的路径：路径是沿途往下扫到的点（台阶的边）的上凸包，在另一只脚的着地点处分段（两段各取上凸包），
 *   脚跟、脚尖那里也不低于它。没着地的腿伸到腿长的 SoftReach 以后软着伸直（软 IK），不一帧弹直。
 *   骨盆跟着脚步链（前后两步的锚，始终在两者之间内插、不向外推；腾空期沿用刚离地的脚的着地点到下一只落脚点，单脚着地前半段沿用刚离地的脚到当前支撑脚，
 *   过支撑脚后切到支撑脚到下一只落脚点，离地与着地都不跳），线在骨盆正下方的高度加上动画里骨盆高出地面多少；
 *   哪条腿伸到最长（ReachRatio）也够不着时再往下放（快落地的脚在 LandingReachTime 内平滑介入）；脚相对大腿根被抬得比腿能折的还多（FoldLimit、AnkleBelowHip）时往上抬，
 *   够得着优先。抬不够的，脚的目标往下压到能折的地方：冲刺上陡楼梯时宁可摆动的脚蹭一下台阶，不让腿折断、膝盖翻边。
 *   手的 IK 根跟着骨盆一起挪，枪与手不脱开。
 *   脚下的地面、骨盆的目标都在世界里算，不随根骨骼：胶囊上下台阶一帧跳一级，看得见的身体不跟着跳。
 *   它们各自过带前馈的临界阻尼弹簧（FRigUnit_GinkgoLegIK_Track）：沿着坡、楼梯连续地变的照样跟上，换了锚、预测变了、着地落的高度与预测不同这类跳变缓过去。
 *   只扫世界里的东西（WorldStatic、WorldDynamic），忽略角色自己
 * 节点自己差分根骨骼在世界里的位置与朝向；调试的中间结果交给角色的动画实例（UGinkgoAnimInstance，发行版不编译）
 */
USTRUCT(meta = (DisplayName = "Ginkgo Leg IK", Category = "Ginkgo", Keywords = "IK,Foot,Leg,Lock,Plant,Predictive", NodeColor = "0.3 0.6 0.4"))
struct CASEGINKGO_API FRigUnit_GinkgoLegIK : public FRigUnitMutable
{
	GENERATED_BODY()

	/** 默认是小白人（UEFN_Mannequin）的两只脚与导入命令生成的曲线 */
	FRigUnit_GinkgoLegIK();

	RIGVM_METHOD()
	virtual void Execute() override;

	UPROPERTY(meta = (Input))
	TArray<FRigUnit_GinkgoLegIK_Foot> Feet;

	/** 根骨骼：差分它在世界里的运动来预测落脚 */
	UPROPERTY(meta = (Input))
	FRigElementKey Root = FRigElementKey(TEXT("root"), ERigElementType::Bone);

	/**
	 * 权重：动画实例的 LegIKAlpha（地面上为 1，空中、滑铲为 0），乘在修正上。
	 * 为 0 时整个跳过，下一次执行时隔了帧，锁与预测从头算（与原来的动画图节点权重为 0 时不求值一样）
	 */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "1"))
	float Alpha = 1.f;

	/** 接触曲线高过它着地 */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "1"))
	float PlantOn = 0.6f;

	/** 接触曲线低过它离地（GASP 的曲线偶尔在着地中间掉到 0.41–0.45） */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "1"))
	float PlantOff = 0.4f;

	/** 动画想要的脚掌离锁住的位置超过它，锁住的位置往动画那边滑（育碧的"滑"，cm） */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float SlideDistance = 5.f;

	/**
	 * 滑的时候锁住的位置每秒最多挪多远（cm/s）：只"滑一点"，动画拖得更快时差越拉越大，到 UnlockDistance 放开。
	 * 同时不比动画想要的脚掌这一帧挪得快：差是之前攒下的、动画已经慢下来时，脚不该为了追回来滑
	 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float SlideSpeed = 50.f;

	/** 超过它放开（育碧的"放开"，cm）：动画与锁住的位置差得太多，硬锁会把腿拉直、拧坏 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float UnlockDistance = 30.f;

	/** 离地或放开以后，修正收回 0 用多久（s，缓入缓出） */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float ReleaseTime = 0.15f;

	/**
	 * 着地那一刻动画想要的脚掌在世界里还快过它（cm/s）时先不锁，照动画走；之后慢到它以下、不再减速或等满 MaxSettleTime 再锁。
	 * 换片段的混合把脚扫过去时接触曲线先到了：起步接走的循环那一下，右脚着地时脚掌还以七百多 cm/s 往前扫，锁在那里就落在动画后面二十九厘米，
	 * 整个着地都扣着，蹬地时腿够不着，骨盆往下放十五厘米。稳定的循环里着地那一帧的 p99 是 155、132 cm/s（60、144 fps），照旧马上锁
	 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float LockSpeed = 150.f;

	/** 等动画的脚掌慢下来最多等多久（s）：一直慢不下来（动画自己拖着脚滑）的照样锁住 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MaxSettleTime = 0.2f;

	/** 根骨骼一帧挪得比它远算瞬移（回放放回开始状态、重生，cm）：锁与预测都清掉 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float TeleportDistance = 200.f;

	/** 预测落脚最远看多久（s）：不循环的片段最后一步没落下时，曲线按片段末尾落下算，可能是好几秒以后 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MaxTimeToLand = 1.f;

	/** 骨盆：地形上整体上下挪它（连同脊柱与两条大腿） */
	UPROPERTY(meta = (Input))
	FRigElementKey Pelvis = FRigElementKey(TEXT("pelvis"), ERigElementType::Bone);

	/** 手的 IK 根（小白人的 ik_hand_root，挂在根骨骼下）：跟着骨盆挪，枪（挂在 ik_hand_gun）与手不脱开 */
	UPROPERTY(meta = (Input))
	FRigElementKey HandIKRoot = FRigElementKey(TEXT("ik_hand_root"), ERigElementType::Bone);

	/**
	 * 往下扫地面相对基准高度往上多高开始、往下多深（cm）：上面要高过能跨上的台阶（移动组件的 MaxStepHeight 45 cm），
	 * 远处的预测落脚点与摆动路径会沿途逐级抬高基准，上多级楼梯不撞穿透也不误打天花板；
	 * 下面要够到下一步的落脚点：冲刺下 25 cm 的楼梯一步跨四级，落脚点比胶囊底低约 1 m
	 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float TraceUp = 50.f;

	UPROPERTY(meta = (Input, ClampMin = "0"))
	float TraceDown = 150.f;

	/** 往下扫的小球的半径（cm）：着地的点、预测的落脚点 */
	UPROPERTY(meta = (Input, ClampMin = "0.1"))
	float TraceRadius = 4.f;

	/** 摆动的路径上每隔多远往下扫一次（cm，最多 MaxPathSamples 次）：小球的半径取间隔的一半，台阶的边落在两次之间也扫得到 */
	UPROPERTY(meta = (Input, ClampMin = "1"))
	float PathSpacing = 15.f;

	/** 脚尖在脚掌前面多远（cm）：摆动时脚掌前面这么远也不低于路径，上台阶时脚尖不踢进台阶 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float ToeLength = 12.f;

	/** 摆动的脚下面的地面高度追目标的临界阻尼弹簧的 ω（1/s）：路径因预测变了而跳时缓过去 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float FootSettleOmega = 25.f;

	/** 着地的脚的同上：着地落的高度与预测不同、滑过台阶的边时，脚悬空或陷着的那几帧最显眼，要快（约 4/ω 秒） */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float PlantSettleOmega = 90.f;

	/**
	 * 脚掌（骨骼）至少高出脚下的地面多少（cm），着地、摆动都是（育碧的"脚永远不低于路径"）：片段里着地时脚掌离地 2–3 cm，那时脚底贴地；
	 * 片段自己有时低下去，原来胶囊离地约 2 cm 把它盖住了，脚放到地面上以后就是脚底陷进地里
	 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MinBallHeight = 2.f;

	/** 脚按地面的法线倾斜、按台阶的边俯仰，合起来最多多少度：再陡的坡脚也只倾这么多，不然拉着髋 */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "90"))
	float FootTiltLimit = 30.f;

	/** 着地后（离地后）脚按台阶的边俯仰的权重升到 1（降到 0）用多久（s） */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float FitBlendTime = 0.1f;

	/** 骨盆下面的地面（两只脚的锚连成的线）追目标的临界阻尼弹簧的 ω（1/s） */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float PelvisOmega = 12.f;

	/**
	 * 骨盆在线以外另挪的量（够不着往下、要折断往上）追目标的 ω（1/s）：摆动只有零点几秒，慢了抬不及，脚就往下压、蹭进台阶；
	 * 抬着的骨盆落地时降不及，着地的脚悬空
	 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float PelvisAdjustOmega = 20.f;

	/** 摆动的脚预测还有多久落下（s）以内，骨盆就要让它够得着（着地的脚一直要）：落地前先放下来，不等着地才降 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float LandingReachTime = 0.15f;

	/**
	 * 腿够不着时骨盆最多比那条线再低多少（cm）：冲刺上陡坡、楼梯时，锁着的后脚到蹬地时比胶囊底低四五十厘米、在身后七十厘米，
	 * 全靠放低骨盆就是深蹲。再够不着的让脚差一点（蹬地那一下脚跟本来就离地）
	 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MaxReachDrop = 15.f;

	/** 腿最多伸到（大腿 + 小腿）的多少：再伸就放低骨盆 */
	UPROPERTY(meta = (Input, ClampMin = "0.5", ClampMax = "1"))
	float ReachRatio = 0.98f;

	/**
	 * 腿最多折到大腿根离脚踝是（大腿 + 小腿）的多少：脚相对大腿根最多比动画抬高"动画里这条腿比它长出的那一截"，
	 * 动画里本来就折得更紧的（冲刺收腿时脚跟贴到屁股，只剩二三十厘米）一点也不抬。冲刺上 25 cm 的楼梯时骨盆跟着身后的锚放低，
	 * 摆动的脚又按前面台阶的高度抬起，不限的话脚的目标跑到大腿根上面，腿折断、膝盖翻边
	 */
	UPROPERTY(meta = (Input, ClampMin = "0", ClampMax = "1"))
	float FoldLimit = 0.45f;

	/** 脚踝最高到大腿根以下多少（cm）；动画里本来就更高的以动画的为准 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float AnkleBelowHip = 25.f;

	/** 为了不折断腿骨盆最多比那条线高多少（cm）；够得着优先，抬不够的把脚往下压 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MaxFoldRise = 40.f;

	/**
	 * 脚下的地面比胶囊底低过这么多（cm）时查它是不是在高台的边外：从胶囊底朝那只脚沿路往下打线（间隔 PathSpacing），相邻两处落下超过它的是边，
	 * 过了边的地面走不到（胶囊会掉下去），脚放在边上那一侧的高度。比移动组件能跨的台阶（MaxStepHeight 45 cm）高一点
	 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MaxStepDown = 50.f;

	/** 摆动的腿伸到腿长的多少以后软着伸直（软 IK）：超出的部分按指数收进腿长以内，不一帧弹直 */
	UPROPERTY(meta = (Input, ClampMin = "0.5", ClampMax = "1"))
	float SoftReach = 0.9f;

	/**
	 * 骨盆最多上下挪多少（cm），只是兜底：骨盆本来就跟着两只脚。冲刺上 25 cm 的楼梯时胶囊的前沿一碰台阶就上去，比后脚高出一米左右，
	 * 骨盆跟着脚比胶囊该在的位置低七八十厘米；限得太紧时贴着上限，胶囊跳一级身体也跟着跳
	 */
	UPROPERTY(meta = (Input, ClampMin = "0"))
	float MaxPelvisOffset = 120.f;

	UPROPERTY()
	FRigUnit_GinkgoLegIK_WorkData WorkData;
};

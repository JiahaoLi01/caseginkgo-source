#pragma once

#include "CoreMinimal.h"
#include "GinkgoAnimTypes.generated.h"

class UAnimSequence;

/**
 * 动画只读三类量：运动（可见的根逐帧差分）、意图（移动组件的输入加速度）、状态（移动模式、蹲、滑铲）。
 * 每一类对本地角色、模拟代理、服务器意思相同，动画蓝图里不分网络角色。设计见 Docs/Design/AnimationBlueprint.md 的"数据更新"。
 */

/**
 * @struct FGinkgoAttachmentHandPose : 桌面操作动作（开关台灯与配件拆装）给双臂的一帧，游戏线程采样后整份交给 Control Rig
 * 施力手使用世界坐标手腕位置与相对基础握姿的世界空间旋转增量，扶枪手关闭对应目标开关后直接沿用枪上握点。
 * 默认配置与既有左手拆装、左手拨灯一致；右手拆装枪托、主握把与扳机时改为左手扶枪、右手跟随配件移动。
 */
struct FGinkgoAttachmentHandPose
{
	/** 左手腕的世界位置（cm），仅 bHasLeftTarget 为真且 LeftGrip 大于零时作为左手 IK 位置目标 */
	FVector WristLocation = FVector::ZeroVector;

	/** 左手相对基础握姿的世界空间旋转增量，仅 bHasLeftTarget 为真时叠加；零增量为单位四元数 */
	FQuat WristRotationDelta = FQuat::Identity;

	/** 左手位置和旋转的 IK 权重（0–1），伸手与收手分别淡入、淡出；bHasLeftTarget 为假时沿用枪上左手握点 */
	float LeftGrip = 0.f;

	/** 左手是否覆盖枪上握点而使用 WristLocation / WristRotationDelta；默认为真以兼容既有左手动作 */
	bool bHasLeftTarget = true;

	/** 右手腕的世界位置（cm），仅 bHasRightTarget 为真且 RightGrip 大于零时作为右手 IK 位置目标 */
	FVector RightWristLocation = FVector::ZeroVector;

	/** 右手相对基础握姿的世界空间旋转增量，仅 bHasRightTarget 为真时叠加；零增量为单位四元数 */
	FQuat RightWristRotationDelta = FQuat::Identity;

	/** 右手位置和旋转的 IK 权重（0–1）；bHasRightTarget 为假时沿用枪上右手握点稳枪 */
	float RightGrip = 0.f;

	/** 右手是否覆盖枪上握点而使用 RightWristLocation / RightWristRotationDelta；默认为假，表示右手留在枪上 */
	bool bHasRightTarget = false;

	/** 躯干前倾所朝向的世界目标点（cm），仅 bHasLeanTarget 为真时生效，避免手部快速收放腰侧时脊柱左右摆动 */
	FVector LeanTarget = FVector::ZeroVector;

	/** 是否使用 LeanTarget 决定躯干弯腰方向；为假时沿用当前施力手腕位置作为前倾方向 */
	bool bHasLeanTarget = false;

	/** 躯干前倾的倍率，0 为直立；取物时可超过 1 以加强身体遮挡 */
	float BodyLean = 0.f;
};

/** 动画要分的移动状态 */
UENUM(BlueprintType)
enum class EGinkgoAnimMode : uint8
{
	Ground,
	Air,
	/** 滑铲：移动组件里是 Walking 的子状态 */
	Slide,
	/** 飞行、游泳与自定义模式（墙跑以后单列） */
	Other,
};

/**
 * @struct FGinkgoAnimMotion : 运动，可见的根逐帧差分出来的
 * 可见的根是网格组件的世界位置：模拟代理上它经过网络平滑，胶囊却一跳一跳；本地角色被校正时胶囊一下子挪开，挪的量扣掉。
 * 本地角色在地面上，差分出来的水平速度就是移动组件的速度（PhysWalking 每一步结束时把速度改成位移除以步长）；
 * 空中位置按前后两个速度的平均推进，差分得到的是帧中间的速度
 */
USTRUCT(BlueprintType)
struct FGinkgoAnimMotion
{
	GENERATED_BODY()

	/** 水平速度（cm/s，世界） */
	UPROPERTY(BlueprintReadOnly, Category = "Motion")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Motion")
	float Speed = 0.f;

	/** 速度相对面向的角度（度，−180–180，向右为正）；几乎不动时为 0 */
	UPROPERTY(BlueprintReadOnly, Category = "Motion")
	float LocalAngle = 0.f;

	/** 竖直速度（cm/s），空中用 */
	UPROPERTY(BlueprintReadOnly, Category = "Motion")
	float VerticalSpeed = 0.f;

	/** 水平加速度（cm/s²）：相邻两次速度之差过一个临界阻尼弹簧，倾斜用 */
	UPROPERTY(BlueprintReadOnly, Category = "Motion")
	FVector Acceleration = FVector::ZeroVector;

	/** 这一帧的水平位移（cm，扣掉校正之后），起步的距离匹配用 */
	UPROPERTY(BlueprintReadOnly, Category = "Motion")
	float FrameDistance = 0.f;

	/** 面向的偏航（度）：网格的世界朝向去掉网格的基准旋转，模拟代理上含网络平滑 */
	UPROPERTY(BlueprintReadOnly, Category = "Motion")
	float FacingYaw = 0.f;

	/** 面向的转速（度/秒） */
	UPROPERTY(BlueprintReadOnly, Category = "Motion")
	float YawRate = 0.f;
};

/**
 * @struct FGinkgoAnimIntentState : 意图与由它推出的距离
 * 意图是移动组件的输入加速度：本地角色是这一帧的输入，服务器从客户端发来的移动里取，模拟代理靠引擎的加速度复制。
 * 逐帧差分得不到它：匀速冲刺时速度不变，意图却是满的；松手与反向都在减速，只差大小
 */
USTRUCT(BlueprintType)
struct FGinkgoAnimIntentState
{
	GENERATED_BODY()

	/** 在按方向键（或推摇杆） */
	UPROPERTY(BlueprintReadOnly, Category = "Intent")
	bool bHasIntent = false;

	/** 输入的方向（世界、水平，单位向量） */
	UPROPERTY(BlueprintReadOnly, Category = "Intent")
	FVector Direction = FVector::ZeroVector;

	/** 输入的大小（0–1，手柄的模拟量） */
	UPROPERTY(BlueprintReadOnly, Category = "Intent")
	float Scale = 0.f;

	/** 意图与速度的夹角（度，0–180）：反向的判断用；没有意图或几乎不动时为 0 */
	UPROPERTY(BlueprintReadOnly, Category = "Intent")
	float AngleToVelocity = 0.f;

	/** 没有意图时还要滑多远（cm）：本项目的摩擦模型，急停的距离匹配用 */
	UPROPERTY(BlueprintReadOnly, Category = "Intent")
	float StopDistance = 0.f;

	/** 意图与速度相反时，速度还要多远过零（cm）：摩擦与反向的加速一起，反向的距离匹配用 */
	UPROPERTY(BlueprintReadOnly, Category = "Intent")
	float PivotDistance = 0.f;
};

/** @struct FGinkgoAnimMoveState : 状态，状态机的大分支读它 */
USTRUCT(BlueprintType)
struct FGinkgoAnimMoveState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "State")
	EGinkgoAnimMode Mode = EGinkgoAnimMode::Ground;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bCrouched = false;

	/** 在地面上冲刺（角色面向速度）：本地角色与服务器读移动组件，模拟代理读复制下来的 */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bSprinting = false;
};

/**
 * 移动状态机的状态。状态机在动画蓝图里，这里只是它的名字：每个状态的节点函数每帧报一次自己，动画实例下一帧据此决定
 * 根骨骼的偏航补偿怎么变、修正节点用什么值（晚一帧）。回放轨迹与 ginkgo.showAnim 也用它
 */
UENUM(BlueprintType)
enum class EGinkgoLocomotionState : uint8
{
	Idle,
	TurnInPlace,
	Start,
	Cycle,
	Stop,
	Pivot,
	/** 空中：起跳，从片段离地的时刻放起，上升时按竖直速度对到片段的最高点（这四种都在动画蓝图的 Air 状态里） */
	Jump,
	/** 空中：走下边缘（离开地面时竖直速度不向上） */
	LedgeOff,
	/** 空中：下落循环 */
	Fall,
	/** 落地：着地之前按顺着轨迹预测的着地时间对上，着地之后接着放，到交出去为止 */
	Land,
	/** 滑铲的进入：GASP 的 Into 从最后一次落脚放起，人倒下去 */
	SlideStart,
	/** 滑铲的滑行：循环，不按地速 */
	SlideLoop,
	/** 滑铲结束：按结束时还蹲不蹲、有没有意图选起身的片段 */
	SlideExit,
};

/** 步态：选哪一套片段 */
UENUM(BlueprintType)
enum class EGinkgoAnimGait : uint8
{
	/** 手柄推得不满、开镜走得慢（输入的大小乘地面的速度倍率，低于 WalkInputScale） */
	Walk,
	Run,
	/** 在地面上冲刺：角色面向速度，只用向前的片段 */
	Sprint,
	Crouch,
};

/**
 * 移动的方向，左右各分两种髋的朝向（GASP 的命名）：LL 向左走、髋朝左，LR 向左走、髋朝右；RL、RR 同理。
 * 从前方转到侧向时髋朝向移动的方向（LL、RR），从后方转过来时朝相反的一侧（LR、RL）；左右之间反向时髋不转（Pivot_LL_RL 之后是 RL）
 */
UENUM(BlueprintType)
enum class EGinkgoAnimDirection : uint8
{
	F,
	B,
	LL,
	LR,
	RL,
	RR,
};

/** @struct FGinkgoDirectionalAnims : 一组片段，每个方向一个；缺的方向（冲刺只有向前）用 F */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoDirectionalAnims
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> F;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> B;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> LL;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> LR;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> RL;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> RR;

	/** 这个方向的片段，没有时用 F */
	UAnimSequence* Get(EGinkgoAnimDirection Direction) const;
};

/**
 * @struct FGinkgoGaitAnims : 一种步态的片段
 * 急停与反向各两套，GASP 的 _Lfoot、_Rfoot：进入时按哪只脚着地选（UGinkgoAnimInstance::SetupStopAnim）。
 * 反向按原来的方向取：F 里放 Pivot_F_B，B 里放 Pivot_B_F，LL 里放 Pivot_LL_RL……
 */
USTRUCT(BlueprintType)
struct FGinkgoGaitAnims
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoDirectionalAnims Cycle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoDirectionalAnims Start;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoDirectionalAnims StopLeftFoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoDirectionalAnims StopRightFoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoDirectionalAnims PivotLeftFoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoDirectionalAnims PivotRightFoot;
};

/**
 * @struct FGinkgoSlideAnims : 滑铲的一套片段（GASP 的 FootOut 或 KneesOut）
 * 进入分左右脚，按倒下去前最后落地的那只脚选（UGinkgoAnimInstance::SetupSlideStartAnim）；起身按结束时的状态选
 */
USTRUCT(BlueprintType)
struct FGinkgoSlideAnims
{
	GENERATED_BODY()

	/** 进入，最后一步落左脚（Into_Lfoot） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> IntoLeftFoot;

	/** 进入，最后一步落右脚（Into_Rfoot） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> IntoRightFoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> Loop;

	/** 结束时站起来、没有意图（Out_Idle_Stand） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> ExitIdleStand;

	/** 结束时还按着蹲、没有意图（Out_Idle_Crouch） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> ExitIdleCrouch;

	/** 结束时站起来、有意图、推得不满（Out_Moving_Walk） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> ExitMovingWalk;

	/** 结束时站起来、有意图（Out_Moving_Run） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> ExitMovingRun;

	/** 结束时还按着蹲、有意图（Out_Moving_Crouch） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> ExitMovingCrouch;
};

/** 跳跃片段的步态，按离地、着地时的水平速度分；FGinkgoJumpDirectionAnims 里按它排 */
enum class EGinkgoJumpGait : uint8
{
	Stand,
	Walk,
	Run,
	Sprint,
};

/** @struct FGinkgoFootAnims : 同一种片段的左右脚两个（GASP 的 _Lfoot、_Rfoot）；不分左右脚的，两个都是它 */
USTRUCT(BlueprintType)
struct FGinkgoFootAnims
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> LeftFoot;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> RightFoot;
};

/**
 * @struct FGinkgoJumpDirectionAnims : 一个方向（F、B、LL、RL）的起跳、走下边缘、落地（GASP 的 Jump）
 * 按步态分的依次是站、走、跑、冲刺（EGinkgoJumpGait）。B、LL、RL 的起跳不分步态，四个都是它；没有冲刺落地的用跑的
 */
USTRUCT(BlueprintType)
struct FGinkgoJumpDirectionAnims
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Anims")
	FGinkgoFootAnims Start[4];

	/** 走下边缘：走、跑 */
	UPROPERTY(EditAnywhere, Category = "Anims")
	FGinkgoFootAnims Off[2];

	UPROPERTY(EditAnywhere, Category = "Anims")
	FGinkgoFootAnims LandLight[4];

	UPROPERTY(EditAnywhere, Category = "Anims")
	FGinkgoFootAnims LandHeavy[4];
};

/** @struct FGinkgoJumpAnims : 四个方向的跳跃片段 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoJumpAnims
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoJumpDirectionAnims F;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoJumpDirectionAnims B;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoJumpDirectionAnims LL;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	FGinkgoJumpDirectionAnims RL;

	/** 这个方向的：向左的两种髋朝向（LL、LR）都用 LL，向右的都用 RL */
	const FGinkgoJumpDirectionAnims& Get(EGinkgoAnimDirection Direction) const;
};

/** @struct FGinkgoStanceAnims : 站着或蹲着时的 Idle 与原地转身；转身按 45°、90°、135°、180° 的顺序各放一个 */
USTRUCT(BlueprintType)
struct FGinkgoStanceAnims
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TObjectPtr<UAnimSequence> Idle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TArray<TObjectPtr<UAnimSequence>> TurnLeft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Anims")
	TArray<TObjectPtr<UAnimSequence>> TurnRight;
};

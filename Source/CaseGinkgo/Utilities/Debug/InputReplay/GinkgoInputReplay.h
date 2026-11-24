#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Combat/GinkgoCombatTypes.h"  // FGinkgoHitConfirm
#include "Engine/EngineTypes.h"  // ELevelTick、EMovementMode
#include "GinkgoInputRecording.h"

class AGinkgoCharacter;
class AGinkgoPlayerController;
class UWorld;

/**
 * @struct FGinkgoReplayOptions : 回放选项
 */
struct FGinkgoReplayOptions
{
	/** 轨迹输出路径；为空时写到 Saved/Ginkgo/Replays/<名字>.csv */
	FString OutputPath;

	/** 回放结束后退出程序（无界面测量用） */
	bool bQuitWhenDone = false;

	/** 不把角色放回开始状态：联网时只在客户端放回会被服务器当成错误校正掉，从角色现在的状态开始回放 */
	bool bNoReset = false;

	/** 角色就绪后再等这么久才开始（秒）：联网时等连接与地形流送稳定 */
	double Delay = 0.0;

	/**
	 * 回放到这些时刻（秒，从早到晚）时各截一张图，带 UI 与调试绘制，存在轨迹旁边：一个时刻是同名的 .png，几个时刻是 <名字>_0.png、_1.png……（按先后）；空的不截。
	 * 要真的渲染（不能 -nullrhi，可以 -RenderOffscreen），时刻要早于回放结束：截图在下一次绘制视口时才存下来，所以一帧最多截一张，挤在同一帧的往后推一帧
	 */
	TArray<double> ShotTimes;

	/** shot= 的写法："1.2" 或 "0.8+1.0+1.6"（加号隔开，-ExecCmds 里逗号是命令的分隔） */
	static TArray<double> ParseShotTimes(const FString& Text);
};

/**
 * @class FGinkgoInputReplay : 输入录制与回放
 * 调试工具，发行版不编译。属于一个玩家控制器，第一次用到时才创建（控制台或命令行），游戏代码不依赖它。
 * 录制时每帧记下控制器解析好的输入意图；回放时用录制的意图代替实时输入，并在每帧所有 Actor 更新之后
 * （FWorldDelegates::OnWorldPostActorTick）记下角色的轨迹、是否在滑铲、收到的服务器校正次数、客户端合并的移动次数、模拟状态的比对结果
 * 与窗口（汇总标签、发给表现层与重放时丢掉的变化次数），结束时写成 CSV。放回开始状态时模拟状态整体换成初始值。
 *
 * 用途：
 * 1. 在不同帧率下复现同一段操作（M1b，配合 -benchmark -fps=N 的固定帧长）
 * 2. 在网络模拟下回放同一条路线、统计校正（M1d）
 *
 * 控制台：ginkgo.record <名字>、ginkgo.record.stop、ginkgo.replay <名字或路径> [out=<轨迹路径>] [quit] [noreset] [delay=<秒>] [shot=<秒>]、
 * ginkgo.replay.stop。命令行（联网的客户端连上服务器、拿到角色之后才开始）：-GinkgoReplay=<名字或路径> [-GinkgoReplayOut=<轨迹路径>]
 * [-GinkgoReplayQuit] [-GinkgoReplayNoReset] [-GinkgoReplayDelay=<秒>] [-GinkgoReplayShot=<秒>]。
 * shot 在回放到那一刻时截一张带调试绘制的图，固定帧长下每次截到的都是同一帧，用来给调试显示留证据。
 * 录制存到 Saved/Ginkgo/Recordings/<名字>.csv，轨迹默认写到 Saved/Ginkgo/Replays/<名字>.csv。
 */
class FGinkgoInputReplay
{
public:
	explicit FGinkgoInputReplay(AGinkgoPlayerController& InController);
	~FGinkgoInputReplay();

	FGinkgoInputReplay(const FGinkgoInputReplay&) = delete;
	FGinkgoInputReplay& operator=(const FGinkgoInputReplay&) = delete;

	/** @brief 命令行里有 -GinkgoReplay= 时为这个控制器请求回放；每个进程只做一次，只对本地控制器 */
	static void HandleCommandLine(AGinkgoPlayerController& Controller);

	/** @brief 在这一帧所有 Actor 更新之后开始录制，开始状态取那时角色的位置、速度、视角与移动模式 */
	void StartRecording(const FString& Name);

	/** @brief 停止录制并写成 CSV */
	void StopRecording();

	/**
	 * @brief 请求回放：角色就绪（并等完 Delay）后，在那一帧所有 Actor 更新之后放回开始状态，下一帧起按录制的输入回放
	 * @param Source - 录制的名字（Saved/Ginkgo/Recordings/<名字>.csv）或 CSV 路径
	 * @param Options - 输出路径、结束后退出、不重置、延迟
	 * @param OutError - 录制读不出时的原因
	 * @return 录制能否读出
	 */
	bool RequestPlayback(const FString& Source, const FGinkgoReplayOptions& Options, FString& OutError);

	void StopPlayback();

	bool IsRecording() const { return bRecording; }

	/** 回放中：这一帧的输入来自录制，实时输入要忽略 */
	bool IsPlaying() const { return PlaybackState == EPlaybackState::Playing; }

	/**
	 * @brief 取这一帧的回放输入
	 * @note 控制器在处理实时输入之前调用；时刻是回放开始后经过的游戏时间，跳跃按下沿与方向键按下覆盖上一次调用到现在之间
	 */
	FGinkgoInputIntent TakePlaybackInput();

	/** @brief 记下这一帧交给角色的输入（控制器处理完实时输入之后调用） */
	void RecordFrame(const FGinkgoInputIntent& Input);

private:
	enum class EPlaybackState : uint8
	{
		None,
		/** 已请求，等角色就绪、等完延迟，再在那一帧所有 Actor 更新之后开始 */
		Pending,
		Playing,
	};

	/** 每帧所有 Actor 更新之后的一行轨迹 */
	struct FTrajectoryRow
	{
		double Time;
		/** 引擎帧号（GFrameCounter）：与移动追踪里的帧号对得上 */
		uint64 Frame;
		FVector Location;
		FVector Velocity;
		EMovementMode MovementMode;
		/** 正在滑铲（Walking 里的子状态） */
		bool bSliding;
		/** 回放开始以来收到的服务器校正次数 */
		int32 Corrections;
		/** 回放开始以来客户端合并的移动次数 */
		int32 Combined;
		/** 回放开始以来拿服务器的模拟状态比对过的次数，与其中不一致的次数（ginkgo.net.verifySimState） */
		int32 SimChecks;
		int32 SimMismatches;
		/** 这个进程里比对见到的最大计时差（秒），容差内的也算 */
		float SimMaxTimeDifference;
		/** 窗口的汇总标签，用 | 隔开 */
		FString Windows;
		/** 回放开始以来发给表现层的窗口变化次数，与客户端重放时丢掉的次数 */
		int32 WindowEvents;
		int32 WindowReplayed;
		/** 回放开始以来合并后重算时又发生一遍的窗口变化次数（只在 ginkgo.net.noCombineOnWindowChange 0 对照时不为 0） */
		int32 WindowRecombined;
		/** 这一帧相机输出的位置、旋转与水平视场（相机管理器的缓存）。第一行的相机还是放回开始状态之前的 */
		FVector CameraLocation = FVector::ZeroVector;
		FRotator CameraRotation = FRotator::ZeroRotator;
		float CameraFOV = 0.f;
		/** 相机管线的中间结果：平滑后的跟随点，碰撞第一段扫掠把它拉回了多少，第二段要求的压缩与实际的压缩（cm） */
		FVector CameraPivot = FVector::ZeroVector;
		double CameraPivotPull = 0.0;
		double CameraRequiredCompression = 0.0;
		double CameraCompression = 0.0;
		/** 角色的偏航与控制器的偏航：平时两者相同，在地面上冲刺时角色面向速度 */
		double ActorYaw = 0.0;
		double ControlYaw = 0.0;
		/** 在地面上冲刺、面向速度（UGinkgoCharacterMovementComponent::IsSprintFacing） */
		bool bSprintFacing = false;
		/**
		 * 动画实例算出的量（UGinkgoAnimInstance）：差分出来的水平速度与竖直速度、面向、平滑后的加速度，这一帧从位移里扣掉的校正挪动量（cm），
		 * 有没有意图、停止距离与反向距离，这一帧有没有清掉差分的历史。第一行还是放回开始状态之前的
		 */
		FVector AnimVelocity = FVector::ZeroVector;
		double AnimVerticalSpeed = 0.0;
		double AnimFacingYaw = 0.0;
		FVector AnimAcceleration = FVector::ZeroVector;
		double AnimCorrection = 0.0;
		bool bAnimHasIntent = false;
		double AnimStopDistance = 0.0;
		double AnimPivotDistance = 0.0;
		bool bAnimReset = false;
		/**
		 * 移动状态机：状态、步态、方向、所放的片段与它的时刻、播放速率（循环的）、步幅修正、朝向修正的角度、根骨骼的偏航补偿；
		 * 两只脚（foot_l/r）与脚掌（ball_l/r）的世界位置，接触曲线。量脚滑用：接触曲线大于 0.5 时脚不该在世界里动
		 */
		FString AnimState;
		FString AnimGait;
		FString AnimDirection;
		FString AnimClip;
		double AnimClipTime = 0.0;
		double AnimPlayRate = 1.0;
		double AnimStride = 1.0;
		double AnimWarp = 0.0;
		double AnimRootYaw = 0.0;
		FVector FootLeft = FVector::ZeroVector;
		FVector FootRight = FVector::ZeroVector;
		FVector BallLeft = FVector::ZeroVector;
		FVector BallRight = FVector::ZeroVector;
		double ContactLeft = 0.0;
		double ContactRight = 0.0;
		/** 原地转身的两条曲线（剩余转角与它的权重），这一帧求值的姿势里的 */
		double TurnRemaining = 0.0;
		double TurnWeight = 0.0;
		/** 空中：顺着轨迹预测的还有多久着地（s，没扫到为负），在放的落地片段的着地时刻（Impact 标记，没在放时为负） */
		double AnimTimeToImpact = -1.0;
		double AnimLandImpact = -1.0;
		/** 左右倾斜（向右为正，−1 到 1）与倾斜姿势里冲刺那套的比重 */
		double AnimLean = 0.0;
		double AnimLeanSprint = 0.0;
		/**
		 * 腿部 IK 的权重，与节点报的每只脚（左、右）：锁的状态（EGinkgoLegIKLock）、着没着地、加给脚的修正（cm）、
		 * 预测还有多久落下（s，着地时为负）与落脚点、动画想要的脚掌（世界）
		 */
		double LegIKAlpha = 0.0;
		int32 LegIKLock[2] = {};
		bool bLegIKPlanted[2] = {};
		double LegIKCorrection[2] = {};
		double LegIKLandIn[2] = { -1.0, -1.0 };
		FVector LegIKLanding[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector LegIKAnimBall[2] = { FVector::ZeroVector, FVector::ZeroVector };
		/**
		 * 量地形上的腿部 IK 用的真值：骨盆的世界高度；两只脚掌正下方的地面高度（从脚掌往下打一条线，只碰世界里静态、动态的东西，
		 * 碰不到时为 NaN），与节点自己扫的无关
		 */
		double PelvisZ = 0.0;
		double GroundUnderBall[2] = { 0.0, 0.0 };
		/** 节点报的：每只脚下的地面高度（没扫到时为 NaN）与加给脚的竖直修正，骨盆的偏移，这一帧往下扫地面的次数 */
		double LegIKTerrain[2] = { 0.0, 0.0 };
		double LegIKVerticalCorrection[2] = { 0.0, 0.0 };
		double LegIKPelvisOffset = 0.0;
		int32 LegIKTraces = 0;
		/**
		 * 量腿的角度：两条腿的大腿根（thigh_l/r）与膝盖（calf_l/r）的世界位置；节点报的每只脚下地面的来源（0 没有，1 着地的脚掌下，
		 * 2 摆动的路径，3 照脚下扫）与过弹簧之前的高度，求解之前动画里的脚踝、膝盖（骨盆挪过以后）与脚踝要去的位置
		 */
		FVector Thigh[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector Calf[2] = { FVector::ZeroVector, FVector::ZeroVector };
		int32 LegIKTerrainSource[2] = {};
		double LegIKRawTerrain[2] = { 0.0, 0.0 };
		FVector LegIKAnimFoot[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector LegIKAnimKnee[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector LegIKFootTarget[2] = { FVector::ZeroVector, FVector::ZeroVector };
		/** 节点报的骨盆偏移的来历（cm，相对根骨骼）：两只脚的锚连成的线、够得着的上限、不折断的下限、在线以外另挪的量 */
		double LegIKPelvisLine = 0.0;
		double LegIKPelvisUpper = 0.0;
		double LegIKPelvisLower = 0.0;
		double LegIKPelvisAdjust = 0.0;
		/** 节点报的：脚下的地面（或落脚点）在高台的边外，放在了边上那一侧的高度 */
		bool bLegIKLedge[2] = {};
		/**
		 * 持枪（Docs/Design/Weapons.md）：姿态（EGinkgoHoldStance）、抬枪与开镜进度（线性的）、主实例给武器层的瞄准偏移的偏航与俯仰（度），
		 * 枪的世界朝向（位置是上面的 Weapon）。手离握把、护木的距离按枪的变换在脚本里算。
		 * 网格的世界位置：枪一帧挪多少按它算（蹲下时胶囊的中心一下子往下掉，网格补回去、不动）
		 */
		int32 HoldStance = 0;
		double HoldRaise = 0.0;
		double HoldAds = 0.0;
		double AimYaw = 0.0;
		double AimPitch = 0.0;
		FQuat WeaponRotation = FQuat::Identity;
		FVector MeshLocation = FVector::ZeroVector;
		/**
		 * 持枪第 2 批（Docs/Design/Weapons.md「手与瞄准」）：给 Control Rig 的瞄准点（从推算的这一帧相机打线），这一帧真正的相机沿瞄准
		 * （叠加之前的视角，相机的叠加层只晃画面）打线命中多远（cm，打不着为最远）；瞄准节点报的枪口对准的权重、对准之前与之后差多少（度）、枪口的位置与方向、两只手的目标与权重
		 */
		bool bHasAimPoint = false;
		FVector AimPoint = FVector::ZeroVector;
		double CrosshairDistance = 0.0;
		float AimAlpha = 0.f;
		float AimResidualBefore = 0.f;
		float AimResidualAfter = 0.f;
		float AimSpineAngle = 0.f;
		float AimGunAngle = 0.f;
		FVector Muzzle = FVector::ZeroVector;
		FVector Bore = FVector::ZeroVector;
		FVector HandTarget[2] = { FVector::ZeroVector, FVector::ZeroVector };
		float HandAlpha[2] = {};
		/**
		 * 持枪循环（Docs/Design/Weapons.md「持枪循环」）：冲刺进度（线性的）、移动状态机所放的片段这一帧求姿势时的时刻，
		 * 武器层报的三个循环的权重与这一帧放到的时刻（没放的为负）、主实例的持枪循环的时钟；上半身几个关节的世界位置（侧面的火柴人逐帧图用）
		 */
		double HoldSprint = 0.0;
		double LocomotionTime = 0.0;
		float LayerRunAlpha = 0.f;
		float LayerSprintAlpha = 0.f;
		float LayerRunTime = -1.f;
		float LayerSprintTime = -1.f;
		float LayerAimAlpha = 0.f;
		float LayerAimTime = -1.f;
		double LoopClock = 0.0;
		static constexpr int32 UpperBodyJointCount = 6;
		FVector UpperBody[UpperBodyJointCount] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector,
			FVector::ZeroVector };
		/** 骨盆、胸口（spine_05）、头、其余四根脊柱的世界旋转：量上半身相对骨盆拧了多少、头转了多少（拾取时东西跑到身后、另一侧），弯在哪几根脊柱上 */
		static constexpr int32 PoseRotationJointCount = 7;
		FQuat PoseRotation[PoseRotationJointCount] = { FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity };
		/**
		 * 动画动作：在跑的叶子动作（用 + 隔开）与第一个的蒙太奇时刻，这一帧发过的事件与结局（执行器的调试日志，用 ; 隔开）；
		 * 武器挂在哪根骨头上与它的世界位置，两只手的世界位置，枪上的蒙太奇的权重（上半身槽与右臂槽大的那个）。量换挂点与换段时跳不跳
		 */
		FString Action;
		double ActionTime = 0.0;
		FString ActionLog;
		FString WeaponParent;
		FVector Weapon = FVector::ZeroVector;
		FVector HandLeft = FVector::ZeroVector;
		FVector HandRight = FVector::ZeroVector;
		double UpperBodyWeight = 0.0;
		/**
		 * 交互：显示着的目标里离得最近的那个（名字，没有时为空），它的状态、距离与离准星的角度；在等服务器回复的请求的目标；
		 * 最近一次请求的结果与它的时刻（回放的时刻，还没有过时为负）；两个武器槽里的枪（网格名，用 | 隔开，空槽为空）。
		 * 量提示与可交互在哪出现、按下到枪进槽要多久
		 */
		FString InteractionTarget;
		FString InteractionState;
		double InteractionDistance = -1.0;
		double InteractionAngle = -1.0;
		FString InteractionPending;
		FString InteractionResult;
		double InteractionResultTime = -1.0;
		FString WeaponSlots;
		/**
		 * 武器的界面（Docs/Design/Weapons.md「界面：武器栏与准星」）：这一帧瞄准的记号（FGinkgoWeaponHud::FindAim）——
		 * 不开镜的点跟着谁（0 瞄准方向、1 屏幕中心）、世界里的点、投影到视口的像素，开镜进度（点与开镜准星的缩放），
		 * 红 X（算没算、世界里的点、投影到视口的像素、离准星多远、缩放：0 是不画），腰射操作性的偏差（度，开镜进度乘之前的）
		 */
		int32 HudAimSource = 2;
		FVector HudAimPoint = FVector::ZeroVector;
		FVector2D HudAimScreen = FVector2D::ZeroVector;
		float HudAdsAlpha = 0.f;
		bool bHudHit = false;
		FVector HudHitPoint = FVector::ZeroVector;
		FVector2D HudHitScreen = FVector2D::ZeroVector;
		double HudHitGap = 0.0;
		float HudHitScale = 0.f;
		FVector2D HipAimLag = FVector2D::ZeroVector;
		/** 手上这把枪的腰射操作性：ω、ζ、最多落后多少度 */
		FVector HipHandling = FVector::ZeroVector;
		/**
		 * 伸手（拾取，Docs/Design/Interaction.md「拾取子弹盒」），下标是 EGinkgoHand：给 Control Rig 的权重与两条曲线，东西上手要去的点、
		 * 胸前的收纳处（世界），最终姿势里手里的挂点（手骨 ⊗ 片段的挂点，世界；没在伸手时为零），手里的东西显示着没有。
		 * 量抓的那一下手里的点离东西多远、收回时离收纳处多远
		 */
		float ReachWeight[2] = {};
		float ReachGrab[2] = {};
		float ReachPlace[2] = {};
		FVector ReachItem[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector ReachPocket[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector ReachHeld[2] = { FVector::ZeroVector, FVector::ZeroVector };
		bool bHandItem[2] = {};
		/** 被顶掉、还在随自己的蒙太奇实例淡出的伸手（连着拾取时）权重加起来 */
		float ReachFading[2] = {};
		/** 方位约束给的伸过去的系数（东西到了身后、另一只肩膀那边时小于 1） */
		float ReachBearing[2] = { 1.f, 1.f };
		/**
		 * 相机的参数层与叠加层（Docs/Design/CameraPipeline.md）：参数层这一帧的输出（弹簧之后的臂长、两个肩部偏移、跟随点的高度、视场），
		 * 叠加之前的视角（ControlRotation 加参数层的侧倾），叠加层的输出（视角空间的旋转与视场，度），相机的时钟。
		 * 文字列（CSV 里不能有逗号）：修改器"名字:权重:值:屏蔽"、叠加效果"名字:俯仰:偏航:侧倾:视场:包络:累计冲量"，用 | 隔开；
		 * 这一帧取出的事件"标签@离帧开头的时刻:强度"，用 ; 隔开；相机上下文的标签（用 | 隔开）、连续量"标签=值"（用 | 隔开），入过队的事件累计个数
		 */
		double CameraArm = 0.0;
		double CameraShoulderRight = 0.0;
		double CameraShoulderUp = 0.0;
		double CameraPivotUp = 0.0;
		double CameraParamFov = 0.0;
		FRotator CameraView = FRotator::ZeroRotator;
		FRotator CameraOverlay = FRotator::ZeroRotator;
		double CameraOverlayFov = 0.0;
		double CameraClock = 0.0;
		FString CameraModifiers;
		FString CameraEffects;
		FString CameraEvents;
		FString CameraContextTags;
		FString CameraContextScalars;
		int32 CameraContextEvents = 0;
		/**
		 * 动作状态（Docs/Design/ActionStates.md）：树的状态路径，手上的槽、目标槽、上一次的槽（从 0 数，徒手 -1），这一帧能不能开镜；
		 * 第二个槽的枪的挂点与世界位置（第一个槽的是上面的 WeaponParent、Weapon）；每个槽的机构状态（供弹发数、EGinkgoChamber、EGinkgoAction，槽空着时 -1）与第一把弹种的后备
		 */
		FString WeaponState;
		int32 HeldSlot = -1;
		int32 TargetSlot = -1;
		int32 LastSlot = -1;
		bool bAimAllowed = true;
		FString SecondWeaponParent;
		FVector SecondWeapon = FVector::ZeroVector;
		/** 每个槽（主武器两个、手枪、重武器）：列名从 1 数 */
		int32 MechFeed[4] = {-1, -1, -1, -1};
		int32 MechChamber[4] = {-1, -1, -1, -1};
		int32 MechAction[4] = {-1, -1, -1, -1};
		/** 手枪的槽：弹匣在不在枪上、枪在哪、挂在哪，套筒的骨骼在枪的坐标里退了多少（cm） */
		int32 SidearmMagazineIn = -1;
		FVector Sidearm = FVector::ZeroVector;
		FQuat SidearmRotation = FQuat::Identity;
		FString SidearmParent;
		float SidearmSlide = 0.f;
		/** 这把枪的后备（手枪的弹种） */
		int32 SidearmReserve = 0;
		/** 准星两边的横线、这一枪恢复到哪了、左手握枪的比重、枪自己放的片段的比重 */
		float HudDash = 0.f;
		float FireRecovery = 0.f;
		float LeftHandGrip = 1.f;
		float GunClipWeight = 0.f;
		/** 散布：这一刻的扩散与连射累积（度），圆锥在画面上的半径与开镜准星的横柱多张开多少（1080p 高的像素）；最后一枪没偏的那条线打到哪、这一枪的扩散与实际偏了多少（度） */
		float Spread = 0.f;
		float SpreadBloom = 0.f;
		/** 速度系数、姿态系数（空中、蹲着、滑铲乘起来） */
		float SpreadSpeedFactor = 1.f;
		float SpreadPosture = 1.f;
		/** 瞄准后坐：开火键按着没有，打到第几发，还没回复的（度），一共动了视线多少（度） */
		bool bTriggerHeld = false;
		float RecoilIndex = 0.f;
		FVector2D RecoilAccumulated = FVector2D::ZeroVector;
		FVector2D RecoilApplied = FVector2D::ZeroVector;
		/** 手上那把枪（哪个槽都算：捡起来的重武器、手枪）在哪、朝哪；捡枪收拢的进度（1 是不在收拢） */
		FVector HeldGun = FVector::ZeroVector;
		FQuat HeldGunRotation = FQuat::Identity;
		float PickupSettle = 1.f;
		/** 手上那把枪的枪管比环境高出多少（°C）、热浪有多强（0–1）；徒手时 0 */
		float BarrelHeat = 0.f;
		float HeatHaze = 0.f;
		/** 命中（Docs/Design/Damage.md「调试」）：最近一枪打中目标的那一次（UGinkgoWeaponComponent::GetHitConfirm），Serial 变了就是这一帧打中了 */
		FGinkgoHitConfirm HitConfirm;
		/**
		 * 弹种（Docs/Design/Weapons.md「切换弹种」）：装填键按下了在等（reload_pending）、按了多久，轮盘开着、选第几项、指针、开关了几次、上一次怎么关的，
		 * 还想从抛壳口压膛；手上那把枪装填压哪种、膛里那一发、供弹从下往上每一发、要压的那种的后备、同一口径每种的后备（文字列）
		 */
		bool bReloadPending = false;
		float ReloadHeldTime = 0.f;
		bool bWheelOpen = false;
		int32 WheelSelected = INDEX_NONE;
		FVector2D WheelMotion = FVector2D::ZeroVector;
		int32 WheelSerial = 0;
		FString WheelResult;
		bool bWantsChamberSwap = false;
		FString LoadAmmo;
		FString ChamberAmmo;
		FString FeedAmmo;
		int32 LoadReserve = 0;
		FString AmmoReserves;
		/** 弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：放过几道、还在画的几道（没有画面的进程里是 0），最后一道多长（cm）、几个点、长到头的快慢 */
		int32 Trails = 0;
		int32 TrailsAlive = 0;
		float TrailLength = 0.f;
		int32 TrailPoints = 0;
		float TrailReveal = 0.f;
		/**
		 * 第一人称开镜（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：相机这一帧的进度，瞄具的出瞳与光轴（相机记下的，同一帧的枪），头藏着没有，
		 * 镜内的画面拍了多大（没拍是 0），晃枪的那一份（武器组件的），相机的近裁剪面（没设是 −1），
		 * 去掉程序化偏移的出瞳（眼睛的家），枪相对眼睛转了多少（转镜头的落后加走路的偏与晃）与其中走路的那一份
		 */
		float ScopeFirstPerson = 0.f;
		FVector ScopeEye = FVector::ZeroVector;
		FVector ScopeAxis = FVector::ZeroVector;
		bool bScopeHeadHidden = false;
		int32 ScopeCapture = 0;
		FRotator ScopeSway = FRotator::ZeroRotator;
		float CameraNearClip = -1.f;
		FVector ScopeRestEye = FVector::ZeroVector;
		FVector ScopeRestAxis = FVector::ZeroVector;
		FRotator ScopeOffset = FRotator::ZeroRotator;
		FRotator ScopeMoveSway = FRotator::ZeroRotator;
		FVector ScopeMoveShift = FVector::ZeroVector;
		/** 第一人称时枪跟着相机后坐转的那一份（武器组件推的，同相机这一帧的） */
		FRotator ScopeRecoil = FRotator::ZeroRotator;
		double HudSpreadPixels = 0.0;
		float HudReticleOpen = 0.f;
		FVector ShotAimed = FVector::ZeroVector;
		float ShotSpread = 0.f;
		float ShotDeviation = 0.f;
		int32 Reserve = 0;
		/** 现在的占用（FGinkgoAnimActionRunner::GetOccupancyText） */
		FString Occupancy;
		/** 还记着想开火、想装填 */
		bool bWantsFire = false;
		bool bWantsReload = false;
		/** 开过几枪、最后一枪的线（UGinkgoWeaponComponent::FDebugShots） */
		int32 Shots = 0;
		FVector ShotMuzzle = FVector::ZeroVector;
		FVector ShotPoint = FVector::ZeroVector;
		/** 枪的踢动：往后顶（cm）、枪口上扬（度） */
		double GunKickBack = 0.0;
		double GunKickClimb = 0.0;
		/** 第一个槽的枪栓：滑动、抬柄（0–1），拉机柄球头的位置 */
		int32 Cues[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
		/** 第一个槽枪口的细烟还剩多少 */
		float MuzzleWisp = 0.f;
		/** 抛壳：抛过几个、其中几个是活弹，最新那个在哪、多快；第一个槽弹仓最上面那一发看不看得见、在哪 */
		int32 Ejects = 0;
		int32 EjectsLive = 0;
		FVector EjectPoint = FVector::ZeroVector;
		float EjectSpeed = 0.f;
		bool bFeedRound = false;
		FVector FeedRoundPoint = FVector::ZeroVector;
		/** 弹壳撞出过几声、其中几声是重的，最后一声撞的速度、音量 */
		int32 ShellHits = 0;
		int32 ShellHitsHard = 0;
		float ShellHitSpeed = 0.f;
		float ShellHitVolume = 0.f;
		float BoltTravel = 0.f;
		float BoltLift = 0.f;
		FVector BoltKnob = FVector::ZeroVector;
		/** 持枪的层盖多少：换层的淡入淡出乘拔枪、收枪时让开的曲线 */
		float LayerUpperAlpha = 0.f;
	};

	/** 每帧所有 Actor 更新之后：放好开始状态、记下轨迹 */
	void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

	/** @brief 记下这一帧武器界面的准星画在哪里：同界面一样在姿势求完、相机更新过以后算 */
	void RecordHud(FTrajectoryRow& Row) const;

	/** @brief 记下这一帧的动作状态：树的状态路径、三个槽号、第二把枪、两个槽的机构状态 */
	void RecordActionStates(FTrajectoryRow& Row) const;

	/** @brief 记下这一帧的交互与两个武器槽（交互组件在 TG_PostUpdateWork 更新过，这里读到的是这一帧的） */
	void RecordInteraction(FTrajectoryRow& Row) const;

	/**
	 * @brief 记下这一帧相机的输出与管线的中间结果
	 * @note 引擎先更新相机（LevelTick 里所有 Actor 更新完之后的 UpdateCameraManager），再广播 OnWorldPostActorTick，所以记下的是这一帧的相机
	 */
	void RecordCamera(FTrajectoryRow& Row) const;

	/**
	 * @brief 记下这一帧角色与控制器的偏航，以及动画实例算出的量
	 * @note 网格的更新（连同工作线程上的那一半）在这一帧所有 Actor 更新完之前已经结束，记下的是这一帧的
	 */
	void RecordAnimation(FTrajectoryRow& Row) const;

	AGinkgoCharacter* GetCharacter() const;
	double GetTime() const;

	void BeginRecording();
	void BeginPlayback();
	void FinishPlayback();

	/** 回放开始以来收到的服务器校正次数 */
	int32 GetCorrectionsSinceStart() const;

	/** 回放开始以来客户端合并的移动次数 */
	int32 GetCombinedSinceStart() const;

	/** 回放开始以来比对模拟状态的次数与不一致的次数 */
	int32 GetSimChecksSinceStart() const;
	int32 GetSimMismatchesSinceStart() const;

	TWeakObjectPtr<AGinkgoPlayerController> Controller;
	FDelegateHandle PostActorTickHandle;

	bool bRecordingPending = false;
	bool bRecording = false;
	FString RecordingName;
	double RecordingStartTime = 0.0;
	FGinkgoInputRecording Recording;

	EPlaybackState PlaybackState = EPlaybackState::None;
	FString PlaybackName;
	FGinkgoReplayOptions PlaybackOptions;
	FGinkgoInputRecording Playback;
	/** 角色就绪的时刻，延迟从这里算起；还没就绪时为负 */
	double CharacterReadyTime = -1.0;
	double PlaybackStartTime = 0.0;
	/** 上一次取回放输入的时刻，跳跃按下沿从这里之后算起 */
	double LastPlaybackInputTime = -1.0;
	/** 这次回放已经请求过几张截图（ShotTimes 的前几个） */
	int32 ShotsRequested = 0;
	/** 回放结束、要退出，等截图存下来再退 */
	bool bQuitPending = false;
	int32 CorrectionsAtStart = 0;
	int32 CombinedAtStart = 0;
	int32 SimChecksAtStart = 0;
	int32 SimMismatchesAtStart = 0;
	int32 WindowEventsAtStart = 0;
	int32 WindowReplayedAtStart = 0;
	int32 WindowRecombinedAtStart = 0;
	TArray<FTrajectoryRow> Trajectory;
};

#endif // !UE_BUILD_SHIPPING

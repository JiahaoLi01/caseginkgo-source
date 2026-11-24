#include "GinkgoAnimInstance.h"

#include "Algo/AnyOf.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "AI/GinkgoAICharacter.h"
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Interaction/GinkgoWorkbench.h"
#include "CollisionQueryParams.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/GinkgoMovementConfig.h"
#include "Movement/Traversal/GinkgoTraversalComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"

namespace
{
	/** GASP 的跳跃片段：目录与名字的前缀（Neutral 的那套） */
	const TCHAR* const JumpAnimFolder = TEXT("/Game/Characters/UEFN_Mannequin/Animations/Jump/");
	const TCHAR* const JumpAnimPrefix = TEXT("M_Neutral_Jump_");

	UAnimSequence* FindJumpAnim(const FString& Name)
	{
		const FString Asset = JumpAnimPrefix + Name;
		ConstructorHelpers::FObjectFinder<UAnimSequence> Finder(*FString::Printf(TEXT("%s%s.%s"), JumpAnimFolder, *Asset, *Asset));
		return Finder.Object;
	}

	/** 分左右脚的取 Name_Lfoot、Name_Rfoot，不分的两个都是 Name */
	FGinkgoFootAnims FindFootAnims(const FString& Name, const bool bByFoot)
	{
		FGinkgoFootAnims Pair;
		Pair.LeftFoot = FindJumpAnim(bByFoot ? Name + TEXT("_Lfoot") : Name);
		Pair.RightFoot = bByFoot ? FindJumpAnim(Name + TEXT("_Rfoot")) : Pair.LeftFoot.Get();
		return Pair;
	}

	/**
	 * GASP 的片段：F 的起跳与落地按步态分、都分左右脚；B、LL、RL 的起跳不分步态，落地只有站着的分左右脚，没有冲刺的落地（用跑的）；
	 * 走下边缘四个方向都是走、跑，分左右脚
	 */
	FGinkgoJumpAnims FindJumpAnims()
	{
		FGinkgoJumpAnims Anims;
		const TCHAR* const Gaits[] = { TEXT("Stand"), TEXT("Walk"), TEXT("Run"), TEXT("Sprint") };
		const TPair<const TCHAR*, FGinkgoJumpDirectionAnims*> Directions[] = { { TEXT("F"), &Anims.F }, { TEXT("B"), &Anims.B }, { TEXT("LL"), &Anims.LL },
			{ TEXT("RL"), &Anims.RL } };
		for (const TPair<const TCHAR*, FGinkgoJumpDirectionAnims*>& Direction : Directions)
		{
			const bool bForward = Direction.Value == &Anims.F;
			FGinkgoJumpDirectionAnims& Clips = *Direction.Value;
			for (int32 Gait = 0; Gait < UE_ARRAY_COUNT(Gaits); ++Gait)
			{
				Clips.Start[Gait] = bForward ? FindFootAnims(FString::Printf(TEXT("F_Start_%s"), Gaits[Gait]), true)
					: FindFootAnims(FString::Printf(TEXT("%s_Start"), Direction.Key), true);
				const int32 LandGait = bForward ? Gait : FMath::Min(Gait, 2);
				Clips.LandLight[Gait] = FindFootAnims(FString::Printf(TEXT("%s_Land_%s_Light"), Direction.Key, Gaits[LandGait]), bForward || LandGait == 0);
				Clips.LandHeavy[Gait] = FindFootAnims(FString::Printf(TEXT("%s_Land_%s_Heavy"), Direction.Key, Gaits[LandGait]), bForward || LandGait == 0);
			}
			Clips.Off[0] = FindFootAnims(FString::Printf(TEXT("%s_Off_Walk"), Direction.Key), true);
			Clips.Off[1] = FindFootAnims(FString::Printf(TEXT("%s_Off_Run"), Direction.Key), true);
		}
		return Anims;
	}

	const FName LeftBallBoneName(TEXT("ball_l"));
	const FName RightBallBoneName(TEXT("ball_r"));

	/** 枪上的蒙太奇放在这两个槽上（拔枪、收枪在右臂，以后换弹在上半身）：双手的权重按它们的蒙太奇权重与曲线算 */
	const FName UpperBodySlotName(TEXT("UpperBody"));
	const FName ArmRightSlotName(TEXT("ArmR"));
	const FName HoldPoseCurveName(TEXT("Weapon_HoldPose"));
	const FName RightHandTrackBoneName(TEXT("ik_hand_r"));
	const FName BoltTravelCurveName(TEXT("Bolt_Travel"));
	const FName BoltLiftCurveName(TEXT("Bolt_Lift"));
	const FName RoundInCurveName(TEXT("Weapon_RoundIn"));
	/** 抛壳口装填（霰弹枪）：手里那一发落进膛。声音借压一发的（Weapon.Cue.RoundIn：一发落进枪里那一下） */
	const FName RoundChamberCurveName(TEXT("Weapon_RoundChamber"));
	const FName MagOutCurveName(TEXT("Weapon_MagOut"));
	const FName MagInCurveName(TEXT("Weapon_MagIn"));
	const FName BoltOpenCurveName(TEXT("Weapon_BoltOpen"));
	const FName RoundInHandCurveName(TEXT("Round_InHand"));
	const FName LeftHandIkCurveName(TEXT("HandIK_L"));

#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarHoldLoops(
		TEXT("ginkgo.anim.holdLoops"),
		1,
		TEXT("武器层的持枪循环（低持跑动、冲刺）：1 开，0 关（跑、冲刺都照低持的姿势放，双手照第 2 批的握法；只用来对照）。"));

	TAutoConsoleVariable<int32> CVarReachEase(
		TEXT("ginkgo.anim.reachEase"),
		1,
		TEXT("伸手的两条曲线缓进缓出（smoothstep）：1 开，0 关（照曲线的直线斜坡，两头速度折一下；只用来对照）。"));

	TAutoConsoleVariable<int32> CVarReachFade(
		TEXT("ginkgo.anim.reachFade"),
		1,
		TEXT("同一只手连着拾取时，被顶掉的伸手随它的蒙太奇实例淡出：1 开，0 关（新的设上时旧的马上没了，手跳回原来的目标；只用来对照）。"));

	TAutoConsoleVariable<int32> CVarReachFreeze(
		TEXT("ginkgo.anim.reachFreeze"),
		1,
		TEXT("伸手的东西在手抓住时、拾取结局时冻在相对身体的位置：1 开，0 关（一直跟着东西在世界里的位置；只用来对照）。"));

	TAutoConsoleVariable<int32> CVarReachBearing(
		TEXT("ginkgo.anim.reachBearing"),
		1,
		TEXT("伸手的方位约束（东西到了身后、另一只肩膀那边时只朝扇形的边上伸，再远就不伸）：1 开，0 关（只用来对照）。"));

	TAutoConsoleVariable<float> CVarReachMaxOutward(
		TEXT("ginkgo.anim.reachMaxOutward"),
		-1.f,
		TEXT("方位约束：往外（伸手那只手那一侧，含身后）最多多少度，负的用伸手的设置。调试用。"));

	TAutoConsoleVariable<float> CVarReachMaxInward(
		TEXT("ginkgo.anim.reachMaxInward"),
		-1.f,
		TEXT("方位约束：往身体中间（另一只肩膀那边）最多多少度，负的用伸手的设置。调试用。"));

	TAutoConsoleVariable<float> CVarReachFadeOutside(
		TEXT("ginkgo.anim.reachFadeOutside"),
		-1.f,
		TEXT("方位约束：出了扇形多少度以外不再伸过去，负的用伸手的设置。调试用。"));
#endif

	/** ginkgo.anim.reachFreeze、ginkgo.anim.reachBearing：开没开（发行版里总是开） */
	bool IsReachFreezeEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarReachFreeze.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	bool IsReachBearingEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarReachBearing.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

#if !UE_BUILD_SHIPPING
	/** 方位约束的一个角：调试时控制台变量覆盖（负的不覆盖） */
	float OverrideReachBearingLimit(const TAutoConsoleVariable<float>& Override, const float Value)
	{
		const float Debug = Override.GetValueOnAnyThread();
		return Debug >= 0.f ? Debug : Value;
	}
#endif

	/** ginkgo.anim.reachFade：被顶掉的伸手淡出（发行版里总是开） */
	bool IsReachFadeEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarReachFade.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	/** ginkgo.anim.holdLoops：开没开（发行版里总是开） */
	bool AreHoldLoopsEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarHoldLoops.GetValueOnAnyThread() != 0;
#else
		return true;
#endif
	}

	/**
	 * 伸手的曲线（0–1 的斜坡）缓进缓出：smoothstep，斜坡两头的斜率是 0。曲线只标出斜坡从哪帧到哪帧：导入时线性，
	 * ACL 也按 30 Hz 采样、样本之间走直线（资源里写成三次的到这里还是折线），斜坡两头手的目标速度折一下，
	 * 折多少正比于东西离动画里的手多远（盒子在左边远处，Grab 回到 0 时手一帧从 5.3 m/s 停到 0.6 m/s）
	 */
	float EaseReachCurve(const float Value)
	{
		const float Clamped = FMath::Clamp(Value, 0.f, 1.f);
#if !UE_BUILD_SHIPPING
		if (CVarReachEase.GetValueOnAnyThread() == 0)
		{
			return Clamped;
		}
#endif
		return FMath::SmoothStep(0.f, 1.f, Clamped);
	}
}

UGinkgoAnimInstance::UGinkgoAnimInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 跳跃的片段：照 ConstructorHelpers 的惯例，第一次构造（原生类的默认对象）时找一次，之后每个对象都给同样的值。
	// 每个对象都要给：动画蓝图生成的类只给实例拷与原生类默认对象不同的属性（CustomPropertyListForPostConstruction），
	// 只填默认对象的话两边相同、不拷，实例是空的；动画蓝图里改过的随后照常拷进来。
	// 命令行（导入命令等）里不找：导入命令要覆盖这些包，先载进内存的话它读到、存回去的是旧的那份
	if (!IsRunningCommandlet())
	{
		static const FGinkgoJumpAnims Defaults = FindJumpAnims();
		JumpAnims = Defaults;
	}
}

void UGinkgoAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	UpdateDeltaSeconds = DeltaSeconds;
	AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(TryGetPawnOwner());
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	Snapshot.bValid = Character && Mesh && Mesh == Character->GetMesh();
	if (!Snapshot.bValid)
	{
		PeekState.Reset();
		PeekLeftAlpha = PeekRightAlpha = 0.f;
		bPeekProbeRequested = bPeekGunReady = false;
		return;
	}
	const UGinkgoCharacterMovementComponent* Movement = Character->GetGinkgoMovement();
	const UGinkgoMovementConfig& Config = Movement->GetConfig();

	// 看得见的位置与朝向：胶囊加上网络平滑的偏移。网格的相对变换去掉基准（构造时摆的位置与朝向、蹲下时的抬高），剩下的就是这个偏移，本地角色上为零。
	// 不直接取网格的世界位置：水平上两者相同，竖直上网格多了蹲下的抬高，空中蹲下时胶囊不动、网格上移 48 cm（本地角色上平滑跟上，那段偏移也在基准里），那是姿势的变化，不是运动；
	// 另外 OnStartCrouch 只改网格的相对位置、不更新世界变换，这一帧网格的世界位置可能还是旧的
	const FTransform& CapsuleTransform = Character->GetCapsuleComponent()->GetComponentTransform();
	Ginkgo::Animation::FGinkgoMotionDifferencer::FInput& MotionInput = Snapshot.Motion;
	MotionInput.Location = CapsuleTransform.TransformPosition(Mesh->GetRelativeLocation() - Character->GetBaseTranslationOffset());
	MotionInput.FacingYaw = (CapsuleTransform.GetRotation() * Mesh->GetRelativeRotation().Quaternion() * Character->GetBaseRotationOffset().Inverse()).Rotator().Yaw;
	MotionInput.CorrectionOffsetTotal = Movement->GetCorrectionOffsetTotal();
	MotionInput.FallbackVelocity = Movement->Velocity;
	MotionInput.bCut = bCutRequested;
	bCutRequested = false;

	// 意图：本地角色是这一帧的输入，服务器是客户端移动数据里的，模拟代理是复制下来的（AGinkgoCharacter::ShouldReplicateAcceleration）
	Snapshot.InputAcceleration = Movement->GetCurrentAcceleration();
	Snapshot.InputScale = Movement->GetAnalogInputModifier();

	const auto* Bench = Character->GetWorkbench();
	Snapshot.Traversal = Character->GetTraversal()->GetHandPose();
	if (Character->IsSlidingForAnimation() && !Bench)
	{
		Snapshot.Mode = EGinkgoAnimMode::Slide;
	}
	else if (Bench || Snapshot.Traversal.bDrivingRootMotion || Movement->IsMovingOnGround())
	{
		Snapshot.Mode = EGinkgoAnimMode::Ground;
	}
	else if (Movement->IsFalling())
	{
		Snapshot.Mode = EGinkgoAnimMode::Air;
	}
	else
	{
		Snapshot.Mode = EGinkgoAnimMode::Other;
	}
	Snapshot.bCrouched = Character->bIsCrouched;
	Snapshot.bSprinting = Character->IsSprintingForAnimation();
	Snapshot.MeshRotation = Mesh->GetComponentQuat();

	// 反向时的加速与移动组件的地面加速相同（CalcGroundVelocity）。模拟代理收不到冲刺的输入，wishspeed 按跑速算；冲刺没有反向动画
	Snapshot.Friction = Config.GroundFriction;
	Snapshot.StopSpeed = Config.StopSpeed;
	Snapshot.PivotDeceleration = Config.GroundAccel * Movement->GetMaxSpeed() * Movement->GetAnalogInputModifier();
	Snapshot.RunSpeed = Movement->GetHeldRunSpeed();
	Snapshot.SprintSpeed = Movement->GetHeldSprintSpeed();
	Snapshot.GroundSpeedRatio = Movement->GetGroundSpeedRatio();

	// 空中：起跳、着地用移动组件的速度；顺着轨迹预测着地；落地时先着地的脚看上一帧的姿势
	Snapshot.MovementVelocity = Movement->Velocity;
	Snapshot.Gravity = -Movement->GetGravityZ();
	PredictImpact(*Character, *Movement);
	Snapshot.bLeftFootLower = Mesh->GetBoneLocation(LeftBallBoneName, EBoneSpaces::ComponentSpace).Z
		<= Mesh->GetBoneLocation(RightBallBoneName, EBoneSpaces::ComponentSpace).Z;

	// 持枪：武器组件的快照（本地控制的角色在控制器的 PlayerTick 里、移动之前写好了）。视角给瞄准偏移：本地控制的角色取控制器的，
	// 不带相机的抖动与踢动；别的角色没有控制器，取复制的俯仰（GetBaseAimRotation 里的 RemoteViewPitch，一个字节）
	const UGinkgoWeaponComponent* Weapons = Character->GetWeapons();
	Snapshot.Hold = Weapons ? Weapons->GetHoldState() : FGinkgoHoldState();
	Snapshot.MuzzleOffset = Weapons ? Weapons->GetMuzzleOffset(Weapons->GetHeldSlot()) : FVector::ZeroVector;
	Snapshot.SupportHandOffset = Weapons ? Weapons->GetSupportHandOffset(Weapons->GetHeldSlot()) : FVector::ZeroVector;
	Snapshot.BenchGunAlpha = Bench ? Bench->GetGunAlpha() : 0.f;
	Snapshot.BenchGrip = Bench ? Bench->GetGripAlpha() : 1.f;
	const auto* Flash = Character->GetFlashbang();
	const auto* Enemy = Cast<AGinkgoAICharacter>(Character);
	Snapshot.FlashArm.Throw = FMath::FInterpConstantTo(Snapshot.FlashArm.Throw, Flash->IsUsingArm() ? 1.f : 0.f, DeltaSeconds, 10.f);
	Snapshot.FlashArm.Belt = Flash->GetBeltIK();
	Snapshot.FlashArm.Release = Flash->GetReleaseIK(Snapshot.FlashArm.ReleasePoint);
	Snapshot.FlashArm.Cover = FMath::FInterpConstantTo(Snapshot.FlashArm.Cover, Enemy && Enemy->bCoverEyes ? 1.f : 0.f, DeltaSeconds, 6.f);
	Snapshot.BenchInstall = Bench ? Bench->GetInstallHandPose() : FGinkgoAttachmentHandPose{};
	if (Bench)
	{
		Snapshot.BenchRest = Bench->GetRestPose();
	}
	const AController* Controller = Character->IsLocallyControlled() ? Character->GetController() : nullptr;
	const FRotator View = Controller ? Controller->GetControlRotation() : Character->GetBaseAimRotation();
	// 瞄准偏移、枪口对准都对着瞄准方向（腰射操作性）：枪跟着准星一起落后，不是只有准星在动。相机的位置还是按视线算
	const FRotator AimRotation = Controller && Weapons ? Weapons->GetAimRotation(View) : View;
	Snapshot.ViewYaw = AimRotation.Yaw;
	Snapshot.ViewPitch = FRotator::NormalizeAxis(AimRotation.Pitch);
	Snapshot.bHasAimPoint = false;
	if (Controller && Snapshot.Hold.Weapon && !Bench)
	{
		UpdateAimPoint(*Character, View, AimRotation, DeltaSeconds);
	}
	else
	{
		bAimDistanceValid = false;
	}
	// 第一人称开镜：瞄具的光轴对准瞄准方向（带着开镜的晃动），相机在光轴的出瞳上看出去
	Snapshot.SightAlpha = Controller && Weapons ? Weapons->GetFirstPersonAlpha() : 0.f;
	Snapshot.AimDirection = AimRotation.Vector();
	Snapshot.GunMontageWeight = ComputeGunMontageWeight();
	// 枪口对准以后的程序化偏移：踢动，第一人称开镜时再加枪相对眼睛的偏移（转镜头的落后、走路的偏与晃）
	Snapshot.GunKick = Weapons ? Weapons->GetGunOffsetTransform() : FTransform::Identity;
	UpdateGunActionClips(*Character);
	Character->ApplyWeaponBolts(BoltClipWeight, BoltClip, GunClip);
	// 左手在护木上、护木跟着枪机走（泵动霰弹枪）：护木现在走了多远，同枪上摆的一样（片段的曲线混机构状态）
	{
		const UGinkgoWeaponDefinition* Held = Snapshot.Hold.Weapon;
		const FGinkgoMechanismState* HeldState = Held && Weapons ? Weapons->GetMechanismState(Weapons->GetHeldSlot()) : nullptr;
		Snapshot.LeftHandSlide = Held && Held->bLeftHandOnForend
			? Held->Bolt.Stroke * Ginkgo::Weapon::BlendBoltPose(HeldState && HeldState->Action == EGinkgoAction::Open, BoltClipWeight, BoltClip).Travel
			: 0.f;
	}
	// 捡枪抓住以后的收拢：持枪的层按它盖上来（同拔枪时片段的 Weapon_HoldPose），Control Rig 按它把枪从右手带到持枪的位置
	Snapshot.PickupSettle = Weapons ? Weapons->GetPickupSettleAlpha() : 1.f;
	Snapshot.PickupStart = Weapons ? Weapons->GetPickupSettleStart() : FGinkgoPickupSettleStart();
	// 网格挂在胶囊上（转了 −90°）：胶囊的前、上在网格的坐标里
	Snapshot.BodyRotation = GetSkelMeshComponent()->GetRelativeRotation().Quaternion().Inverse();
	// 收拢时持枪的层在前 PickupHoldShare 里盖满：枪往持枪的位置转的时候，挂点（虚拟骨骼）已经是持枪时的。空手时它在胯边、朝上，
	// 跟着收拢一起慢慢盖的话混进来的那一份让枪口先往上翘（MG3 的 pickup 路线翘到 21°）
	const float HoldIn = Weapons ? Ginkgo::Weapon::EaseProgress(Weapons->GetPickupSettleProgress() / PickupHoldShare) : 1.f;
	Snapshot.HoldPoseAlpha = FMath::Min(ComputeHoldPoseAlpha(), HoldIn);
	Snapshot.bGunInHand = Character->IsHeldWeaponInHand();
	UpdateHandReach();
	UpdatePeek(*Character, View, AimRotation, DeltaSeconds);

#if !UE_BUILD_SHIPPING
	DebugFrame.Feet = Mesh->GetComponentLocation();
	DebugFrame.MovementVelocity = Movement->Velocity;
	DebugFrame.ViewYaw = Character->GetBaseAimRotation().Yaw;
	DebugFrame.bLocallyControlled = Character->IsLocallyControlled();
#endif
}

void UGinkgoAnimInstance::PredictImpact(const ACharacter& Character, const UCharacterMovementComponent& Movement)
{
	Snapshot.TimeToImpact = -1.0;
	Snapshot.ImpactSpeed = 0.0;
#if !UE_BUILD_SHIPPING
	DebugFrame.TimeToImpact = -1.0;
#endif
	if (Snapshot.Mode != EGinkgoAnimMode::Air || Snapshot.Gravity <= 0.0)
	{
		return;
	}
	const UCapsuleComponent* Capsule = Character.GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FVector Start = Capsule->GetComponentLocation() - FVector(0.0, 0.0, Capsule->GetScaledCapsuleHalfHeight() - Radius);
	FPredictProjectilePathParams Params(Radius, Start, Snapshot.MovementVelocity, ImpactPredictionTime, Capsule->GetCollisionObjectType(),
		const_cast<ACharacter*>(&Character));
	Params.SimFrequency = ImpactPredictionFrequency;
	Params.OverrideGravityZ = static_cast<float>(-Snapshot.Gravity);
	FPredictProjectilePathResult Result;
	// 扫到墙、斜坡这些站不住的不算着地
	if (!UGameplayStatics::PredictProjectilePath(&Character, Params, Result) || Result.HitResult.ImpactNormal.Z < Movement.GetWalkableFloorZ())
	{
		return;
	}
	const FPredictProjectilePathPointData& Hit = Result.PathData.Last();
	Snapshot.TimeToImpact = Hit.Time;
	Snapshot.ImpactSpeed = -Hit.Velocity.Z;
#if !UE_BUILD_SHIPPING
	DebugFrame.TimeToImpact = Hit.Time;
	DebugFrame.ImpactLocation = Hit.Location - FVector(0.0, 0.0, Radius);
#endif
}

void UGinkgoAnimInstance::NativeThreadSafeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	PeekReference.bValid = false;
	if (!Snapshot.bValid)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	// 落地片段的着地时刻由 Air 状态的节点函数每帧写，别的状态里没有；武器层的循环由层报，没链上层时是空的
	DebugFrame.LandImpactTime = -1.f;
	DebugFrame.WeaponLayer = FDebugFrame::FWeaponLayer();
#endif
	UpdateMotion(DeltaSeconds);
	UpdateIntent();
	MoveState.Mode = Snapshot.Mode;
	MoveState.bCrouched = Snapshot.bCrouched;
	MoveState.bSprinting = Snapshot.bSprinting;
	UpdateLocomotion(DeltaSeconds);

	WeaponAnim.LoopClock = Ginkgo::Animation::StepLoopClock(WeaponAnim.LoopClock, DeltaSeconds, Snapshot.Motion.bCut);
	UpdateWeaponAnim();
	bTraversalWasDriving = Snapshot.Traversal.bDrivingRootMotion;
}

void UGinkgoAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	// 评估之后回到游戏线程：这一帧工作线程算的结果已经写好（节点函数记下的片段与时刻）；相机在所有 Actor 更新之后才取事件
	PushFootsteps();
#if !UE_BUILD_SHIPPING
	PostEvaluateDebug();
#endif
}

void UGinkgoAnimInstance::UpdateWeaponAnim()
{
	WeaponRig.Traversal = Snapshot.Traversal;
	const FGinkgoHoldState& Hold = Snapshot.Hold;
	const UGinkgoWeaponDefinition* Weapon = Hold.Weapon;
	WeaponAnim.Weapon = Weapon;
	WeaponAnim.Stance = Hold.Stance;
	WeaponAnim.RaiseAlpha = Hold.GetRaiseAlpha();
	WeaponAnim.AdsAlpha = Hold.GetAdsAlpha();
	WeaponAnim.HoldPoseAlpha = Snapshot.HoldPoseAlpha * Snapshot.BenchGrip;
	// 持枪循环（Docs/Design/Weapons.md「持枪循环」）：冲刺的循环按冲刺进度换过去，跑的循环由层按地速混；滑铲、空中不算地速
	WeaponAnim.bHoldLoops = AreHoldLoopsEnabled();
	WeaponAnim.SprintAlpha = WeaponAnim.bHoldLoops && Weapon && Weapon->SprintLoop ? Hold.GetSprintAlpha() : 0.f;
	WeaponAnim.GroundSpeed = Snapshot.Mode == EGinkgoAnimMode::Ground ? static_cast<float>(Motion.Speed) : 0.f;
	WeaponAnim.bBothHandsOnGun = Hold.bBothHandsOnGun;
	// 网格根的偏航 = 网格的朝向加上转根骨骼转的那一下（偏航补偿）：站着时就是偏航补偿的反向，滑铲时网格的根朝速度也一样算
	WeaponAnim.AimYaw = static_cast<float>(FRotator::NormalizeAxis(Snapshot.ViewYaw - Snapshot.Motion.FacingYaw - RootYawOffset));
	WeaponAnim.AimPitch = static_cast<float>(Snapshot.ViewPitch);

	// Control Rig 里的瞄准与双手：两只手的目标同层里的姿势一样按三个进度混；枪口对准只在抬起来时做。
	// 单手的举枪冲刺左手离开枪，照循环里的摆臂：左手照常握枪的那一份随冲刺持枪的权重放到 0
	WeaponRig.bArmed = Weapon != nullptr;
	WeaponRig.bHasAimPoint = Snapshot.bHasAimPoint;
	WeaponRig.AimPoint = Snapshot.AimPoint;
	WeaponRig.SightAlpha = Snapshot.SightAlpha;
	WeaponRig.AimDirection = Snapshot.AimDirection;
	WeaponRig.AimAlpha = Weapon ? WeaponAnim.RaiseAlpha * (1.f - Snapshot.Traversal.Weight) : 0.f;
	WeaponRig.GunKick = Snapshot.GunKick;
	WeaponRig.GunMontageWeight = Snapshot.GunMontageWeight;
	WeaponRig.bGunInHand = Snapshot.bGunInHand;
	WeaponRig.LeftHandGrip = Weapon
		? Ginkgo::Weapon::MakeLeftHandGrip(Weapon->bSprintTwoHanded, WeaponAnim.SprintAlpha, Weapon->bRunTwoHanded, LayerRunAlpha, WeaponAnim.RaiseAlpha, Snapshot.LeftHandClipGrip)
		: 1.f;
	WeaponRig.BenchGunAlpha = Snapshot.BenchGunAlpha;
	WeaponRig.BenchGrip = Snapshot.BenchGrip;
	WeaponRig.BenchRest = Snapshot.BenchRest;
	WeaponRig.FlashArm = Snapshot.FlashArm;
	WeaponRig.BenchInstall = Snapshot.BenchInstall;
	// 捡枪的收拢：枪先在右手里提起来，左手后半段才握上去（进度过了 30% 起，两头缓过）
	WeaponRig.PickupSettle = Snapshot.PickupSettle;
	WeaponRig.PickupStart = Snapshot.PickupStart;
	WeaponRig.BodyRotation = Snapshot.BodyRotation;
	if (Snapshot.PickupSettle < 1.f)
	{
		WeaponRig.LeftHandGrip *= Ginkgo::Weapon::EaseProgress(FMath::Clamp((Snapshot.PickupSettle - 0.3f) / 0.7f, 0.f, 1.f));
	}
	if (Weapon)
	{
		WeaponRig.Hands = Ginkgo::Weapon::BlendHandTargets(Weapon->LoweredHands, Weapon->SprintHands, Weapon->HipHands, Weapon->AimHands, WeaponAnim.SprintAlpha,
			WeaponAnim.RaiseAlpha, WeaponAnim.AdsAlpha);
		WeaponRig.MuzzleOffset = Snapshot.MuzzleOffset;
		WeaponRig.Hands.Left.AddToTranslation(Snapshot.SupportHandOffset);
		// 左手在护木上、护木跟着枪机走（泵动霰弹枪）：手的目标沿枪往后挪护木现在走了的距离
		WeaponRig.LeftHandSlide = Snapshot.LeftHandSlide;
		// 拉栓、装填：右手从握把到片段里的轨迹（枪的坐标），枪照姿态走，手就一直在枪上的拉柄那里
		if (const float Track = Snapshot.RightHandTrackWeight; Track > 0.f)
		{
			FTransform Right;
			Right.Blend(WeaponRig.Hands.Right, Snapshot.RightHandTrack, Track);
			WeaponRig.Hands.Right = Right;
		}
	}
	WeaponRig.Reach[0] = Snapshot.Reach[0];
	WeaponRig.Reach[1] = Snapshot.Reach[1];
	WeaponRig.FadingReach[0] = Snapshot.FadingReach[0];
	WeaponRig.FadingReach[1] = Snapshot.FadingReach[1];
}

// ===== 伸手 =====

namespace
{
	/** 蒙太奇里某条曲线在这一刻的值：找到这一刻所在的段，在它的序列上求（第一条有段的槽轨道） */
	bool HasMontageCurve(const UAnimMontage& Montage, const FName Curve)
	{
		return Montage.SlotAnimTracks.ContainsByPredicate([Curve](const FSlotAnimationTrack& Track)
		{
			return Track.AnimTrack.AnimSegments.ContainsByPredicate([Curve](const FAnimSegment& Segment)
			{
				const UAnimSequenceBase* Sequence = Segment.GetAnimReference().Get();
				return Sequence && Sequence->HasCurveData(Curve);
			});
		});
	}

	float EvaluateMontageCurve(const UAnimMontage& Montage, const FName Curve, const float Position)
	{
		for (const FSlotAnimationTrack& Track : Montage.SlotAnimTracks)
		{
			const FAnimSegment* Segment = Track.AnimTrack.GetSegmentAtTime(Position);
			const UAnimSequenceBase* Sequence = Segment ? Segment->GetAnimReference().Get() : nullptr;
			if (Sequence)
			{
				return Sequence->EvaluateCurveData(Curve, FAnimExtractContext(static_cast<double>(Segment->ConvertTrackPosToAnimPos(Position))));
			}
		}
		return 0.f;
	}

	/**
	 * 这个蒙太奇的实例：在放的优先，没有时用正在淡出的；Skip 说不算的跳过。开始淡出（自动淡出、被停掉）时实例就不算在放了
	 * （GetActiveInstanceForMontage 找不到），那时清掉的话手一帧跳回原来的目标（左手从胸前回到枪上 38 cm）；淡出时照它的权重换回去
	 */
	FAnimMontageInstance* FindMontageInstance(const TArray<FAnimMontageInstance*>& Instances, const UAnimMontage& Montage, const TFunctionRef<bool(int32)> Skip)
	{
		FAnimMontageInstance* BlendingOut = nullptr;
		for (FAnimMontageInstance* Instance : Instances)
		{
			if (Instance && Instance->Montage == &Montage && !Skip(Instance->GetInstanceID()))
			{
				if (Instance->IsActive())
				{
					return Instance;
				}
				BlendingOut = Instance;
			}
		}
		return BlendingOut;
	}

	/** 伸手那只手的肩关节（upperarm_l / upperarm_r）：方位从这里量 */
	FName ReachShoulderBone(const EGinkgoHand Hand)
	{
		static const FName Bones[2] = { TEXT("upperarm_l"), TEXT("upperarm_r") };
		return Bones[static_cast<int32>(Hand)];
	}
}

float Ginkgo::Animation::ConstrainReachBearing(const FVector& Item, const FVector& Shoulder, const FVector& Forward, const EGinkgoHand Hand, const float MaxOutward,
	const float MaxInward, const float FadeOutside, FVector& OutItem)
{
	OutItem = Item;
	const FVector Ahead = Forward.GetSafeNormal2D();
	const FVector Flat(Item.X - Shoulder.X, Item.Y - Shoulder.Y, 0.0);
	if (Ahead.IsZero() || Flat.IsNearlyZero())
	{
		return 1.f;
	}
	// 方位：往身体中间为正（左手往右，右手往左）
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
	const double Inward = Hand == EGinkgoHand::Left ? 1.0 : -1.0;
	const double Bearing = Inward * FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Flat, Right), FVector::DotProduct(Flat, Ahead)));
	if (Bearing >= -MaxOutward && Bearing <= MaxInward)
	{
		return 1.f;
	}
	// 转回近的那条边（身后两条边一样远的地方系数已经是 0）
	const double ToInward = FMath::Abs(FMath::UnwindDegrees(Bearing - MaxInward));
	const double ToOutward = FMath::Abs(FMath::UnwindDegrees(Bearing + MaxOutward));
	const double Edge = ToInward <= ToOutward ? MaxInward : -MaxOutward;
	const double Turn = Inward * FMath::UnwindDegrees(Edge - Bearing);
	OutItem = Shoulder + FQuat(FVector::UpVector, FMath::DegreesToRadians(Turn)).RotateVector(Flat) + FVector(0.0, 0.0, Item.Z - Shoulder.Z);
	return FadeOutside > 0.f ? 1.f - FMath::SmoothStep(0.f, FadeOutside, static_cast<float>(FMath::Min(ToInward, ToOutward))) : 0.f;
}

FGinkgoHandReachRigData UGinkgoAnimInstance::MakeHandReachRigData(const FGinkgoHandReach& Reach, const FAnimMontageInstance& Instance, const EGinkgoHand Hand,
	const FVector& ShoulderInComponent, FVector& InOutLastItem, TOptional<FVector>& InOutFrozen) const
{
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	// 东西：冻住的跟着身体；没冻住的是它此刻在世界里的位置（没了停在最后的位置）
	if (InOutFrozen.IsSet() && Mesh)
	{
		InOutLastItem = Mesh->GetComponentTransform().TransformPosition(InOutFrozen.GetValue());
	}
	else if (const USceneComponent* Target = Reach.Target.Get())
	{
		InOutLastItem = Target->GetComponentTransform().TransformPosition(Reach.TargetPoint);
	}
	// 实例开始淡出：拾取结局了（中止、取消、被下一个顶掉），东西冻在这一刻相对身体的位置，淡出的 0.25 s 里不再追着它
	if (!InOutFrozen.IsSet() && !Instance.IsActive() && Mesh && IsReachFreezeEnabled())
	{
		InOutFrozen = Mesh->GetComponentTransform().InverseTransformPosition(InOutLastItem);
	}

	FGinkgoHandReachRigData Out;
	Out.bActive = true;
	Out.Item = InOutLastItem;
	Out.ItemInHand = Reach.ItemInHand;
	Out.StowBone = Reach.StowBone;
	Out.PocketInChest = Reach.PocketInChest;
	Out.ArmLimit = Reach.ArmLimit;
	Out.ArmLimitMargin = Reach.ArmLimitMargin;
	const AActor* Owner = GetOwningActor();
	if (Mesh && Owner && IsReachBearingEnabled())
	{
		float MaxOutward = Reach.MaxOutwardDegrees;
		float MaxInward = Reach.MaxInwardDegrees;
		float FadeOutside = Reach.FadeOutsideDegrees;
#if !UE_BUILD_SHIPPING
		MaxOutward = OverrideReachBearingLimit(CVarReachMaxOutward, MaxOutward);
		MaxInward = OverrideReachBearingLimit(CVarReachMaxInward, MaxInward);
		FadeOutside = OverrideReachBearingLimit(CVarReachFadeOutside, FadeOutside);
#endif
		Out.Bearing = Ginkgo::Animation::ConstrainReachBearing(InOutLastItem, Mesh->GetComponentTransform().TransformPosition(ShoulderInComponent),
			Owner->GetActorForwardVector(), Hand, MaxOutward, MaxInward, FadeOutside, Out.Item);
	}
	if (const UAnimMontage* Montage = Instance.Montage)
	{
		const float Position = Instance.GetPosition();
		Out.Grab = EaseReachCurve(EvaluateMontageCurve(*Montage, Reach.GrabCurve, Position));
		Out.Place = EaseReachCurve(EvaluateMontageCurve(*Montage, Reach.PlaceCurve, Position));
	}
	Out.Weight = Instance.GetWeight();
	return Out;
}

void UGinkgoAnimInstance::SetHandReach(const EGinkgoHand Hand, const FGinkgoHandReach& Reach)
{
	const int32 Index = static_cast<int32>(Hand);
	// 这只手上一个还在（连着拾取）：新的蒙太奇马上以打断的方式顶掉它，它按新的淡入时长淡出。它的伸手随它的实例淡出，
	// 不然这一帧手就跳回原来的目标（实例的权重还是满的）
	if (HandReach[Index].IsSet() && IsReachFadeEnabled())
	{
		if (const FAnimMontageInstance* Instance = FindHandReachInstance(Index))
		{
			FadingHandReach[Index].Add({ HandReach[Index].GetValue(), Instance->GetInstanceID(), HandReachLastItem[Index], HandReachFrozen[Index], HandReachShoulder[Index] });
		}
	}
	HandReach[Index] = Reach;
	HandReachInstance[Index] = INDEX_NONE;
	HandReachFrozen[Index].Reset();
	if (const USkeletalMeshComponent* Mesh = GetSkelMeshComponent())
	{
		HandReachShoulder[Index] = Mesh->GetBoneLocation(ReachShoulderBone(Hand), EBoneSpaces::ComponentSpace);
	}
	const USceneComponent* Target = Reach.Target.Get();
	HandReachLastItem[Index] = Target ? Target->GetComponentTransform().TransformPosition(Reach.TargetPoint) : FVector::ZeroVector;
}

void UGinkgoAnimInstance::ClearHandReach(const EGinkgoHand Hand)
{
	const int32 Index = static_cast<int32>(Hand);
	HandReach[Index].Reset();
	HandReachInstance[Index] = INDEX_NONE;
	HandReachFrozen[Index].Reset();
	FadingHandReach[Index].Reset();
}

void UGinkgoAnimInstance::FreezeHandReach(const EGinkgoHand Hand)
{
	const int32 Index = static_cast<int32>(Hand);
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (HandReach[Index].IsSet() && !HandReachFrozen[Index].IsSet() && Mesh && IsReachFreezeEnabled())
	{
		HandReachFrozen[Index] = Mesh->GetComponentTransform().InverseTransformPosition(HandReachLastItem[Index]);
	}
}

float UGinkgoAnimInstance::ComputeHoldPoseAlpha() const
{
	float Alpha = 1.f;
	for (const FAnimMontageInstance* Instance : MontageInstances)
	{
		const UAnimMontage* Montage = Instance ? Instance->Montage.Get() : nullptr;
		if (Montage && HasMontageCurve(*Montage, HoldPoseCurveName))
		{
			const float Curve = FMath::Clamp(EvaluateMontageCurve(*Montage, HoldPoseCurveName, Instance->GetPosition()), 0.f, 1.f);
			// 淡入时直接听曲线：拔枪一开始层就让开，不跟着槽的权重先盖一下。开始淡出以后按权重回到 1：半路被打断时枪还在手上，层要盖回来
			Alpha = FMath::Min(Alpha, Instance->IsActive() ? Curve : FMath::Lerp(1.f, Curve, Instance->GetWeight()));
		}
	}
	return Alpha;
}

double Ginkgo::Animation::SoftLimitReach(const double Distance, const double Limit, const double Margin)
{
	if (Distance <= Limit)
	{
		return Distance;
	}
	return Margin > 0.0 ? Limit + Margin * (1.0 - FMath::Exp(-(Distance - Limit) / Margin)) : Limit;
}

float Ginkgo::Animation::GetHoldPoseFullTime(const UAnimMontage& Montage)
{
	if (!HasMontageCurve(Montage, HoldPoseCurveName))
	{
		return 0.f;
	}
	// 从尾巴往前找：曲线只升一次，按片段的 30 fps 走两倍细就够
	constexpr float Step = 1.f / 60.f;
	float Time = Montage.GetPlayLength();
	while (Time > 0.f && EvaluateMontageCurve(Montage, HoldPoseCurveName, Time - Step) >= 0.999f)
	{
		Time -= Step;
	}
	return FMath::Max(Time, 0.f);
}

float UGinkgoAnimInstance::ComputeGunMontageWeight() const
{
	const float ArmRight = GetSlotMontageGlobalWeight(ArmRightSlotName);
	const auto IsAdditive = [](const FAnimMontageInstance* Instance)
	{
		return Instance && Instance->Montage && Instance->Montage->IsValidSlot(UpperBodySlotName) && Instance->Montage->IsValidAdditive();
	};
	if (!MontageInstances.ContainsByPredicate(IsAdditive))
	{
		return FMath::Max(GetSlotMontageGlobalWeight(UpperBodySlotName), ArmRight);
	}
	// 槽的权重把叠加的也加进去了（FAnimInstanceProxy::GetSlotWeight）：有叠加的在放时只加不叠加的
	float UpperBody = 0.f;
	for (const FAnimMontageInstance* Instance : MontageInstances)
	{
		if (Instance && Instance->Montage && Instance->Montage->IsValidSlot(UpperBodySlotName) && !IsAdditive(Instance))
		{
			UpperBody += Instance->GetWeight();
		}
	}
	return FMath::Max(FMath::Min(UpperBody, 1.f), ArmRight);
}

void UGinkgoAnimInstance::UpdateGunCues(AGinkgoCharacter& Character, const UGinkgoWeaponDefinition& Weapon, const UAnimMontage& Montage, const int32 InstanceID, const float Position)
{
	struct FCueEdge
	{
		FGameplayTag Cue;
		FName Curve;
		float Threshold;
		bool bRising;
		/** 片段上有这条曲线才算：手枪开枪的片段上也有 Bolt_Travel（套筒每枪回来一次，声音在枪声里），放挂住的套筒的片段才有 Weapon_BoltOpen */
		FName RequiredCurve;
	};
	static const FCueEdge Edges[] = {
		{FWeaponTags::Weapon_Cue_BoltLift, BoltLiftCurveName, 0.02f, true},
		{FWeaponTags::Weapon_Cue_BoltPull, BoltTravelCurveName, 0.98f, true},
		{FWeaponTags::Weapon_Cue_BoltPush, BoltTravelCurveName, 0.02f, false},
		{FWeaponTags::Weapon_Cue_BoltPress, BoltLiftCurveName, 0.02f, false},
		{FWeaponTags::Weapon_Cue_RoundIn, RoundInCurveName, 0.5f, true},
		{FWeaponTags::Weapon_Cue_RoundIn, RoundChamberCurveName, 0.5f, true},
		{FWeaponTags::Weapon_Cue_MagOut, MagOutCurveName, 0.98f, true},
		{FWeaponTags::Weapon_Cue_MagIn, MagInCurveName, 0.98f, true},
		{FWeaponTags::Weapon_Cue_SlideHome, BoltTravelCurveName, 0.02f, false, BoltOpenCurveName},
	};
	const float Last = InstanceID == GunCueInstance ? GunCuePosition : Position;
	GunCueInstance = InstanceID;
	GunCuePosition = Position;
	// 看到片段后面不能超过结尾：过了结尾读到的是 0，停在拉开的片段（开栓）会被当成推到底
	const float End = Montage.GetPlayLength() - UE_KINDA_SMALL_NUMBER;
	for (const FCueEdge& Edge : Edges)
	{
		const FGinkgoWeaponCue* Cue = Weapon.Cues.Find(Edge.Cue);
		if (!Cue || (!Edge.RequiredCurve.IsNone() && !HasMontageCurve(Montage, Edge.RequiredCurve)))
		{
			continue;
		}
		const float Before = EvaluateMontageCurve(Montage, Edge.Curve, FMath::Min(Last + Cue->Lead, End));
		const float Now = EvaluateMontageCurve(Montage, Edge.Curve, FMath::Min(Position + Cue->Lead, End));
		if (Edge.bRising ? (Before < Edge.Threshold && Now >= Edge.Threshold) : (Before > Edge.Threshold && Now <= Edge.Threshold))
		{
			Character.GetWeapons()->PlayCue(Edge.Cue);
		}
	}
}

void UGinkgoAnimInstance::UpdateGunActionClips(AGinkgoCharacter& Character)
{
	Snapshot.RightHandTrackWeight = 0.f;
	float TrackSource = 0.f;
	BoltClipWeight = 0.f;
	float BoltSource = 0.f;
	Snapshot.LeftHandClipGrip = 1.f;
	float LeftHandRelease = 0.f;
	GunClip.Weight = 0.f;
	GunClip.bNextRoundInClip = false;
	float GunSource = 0.f;
	const UGinkgoWeaponDefinition* Weapon = Snapshot.Hold.Weapon;
	if (!Weapon)
	{
		Character.SetRoundInHand(false);
		return;
	}
	// 枪自己的动画：哪个槽的蒙太奇都算（拔枪、收枪在右臂的槽）。同枪栓：比重加起来；开始淡出以后不再读，停在那一刻
	for (const FAnimMontageInstance* Instance : MontageInstances)
	{
		const UAnimMontage* Montage = Instance ? Instance->Montage.Get() : nullptr;
		for (const TPair<FGameplayTag, FGinkgoWeaponStep>& Step : Weapon->Steps)
		{
			if (Montage && Step.Value.Montage == Montage && Step.Value.GunTrack)
			{
				const float Weight = Instance->GetWeight();
				GunClip.Weight = FMath::Min(GunClip.Weight + Weight, 1.f);
				if (Instance->IsActive() && Weight >= GunSource)
				{
					GunClip.Track = Step.Value.GunTrack;
					GunClip.Time = Instance->GetPosition();
					GunClip.bNextRoundInClip = Step.Value.bNextRoundInClip;
					GunSource = Weight;
				}
				break;
			}
		}
	}
	bool bRoundInHand = false;
	for (const FAnimMontageInstance* Instance : MontageInstances)
	{
		const UAnimMontage* Montage = Instance ? Instance->Montage.Get() : nullptr;
		if (!Montage || !Montage->IsValidSlot(UpperBodySlotName) || !Montage->IsValidAdditive())
		{
			continue;
		}
		const float Weight = Instance->GetWeight();
		if (Instance->IsActive())
		{
			UpdateGunCues(Character, *Weapon, *Montage, Instance->GetInstanceID(), Instance->GetPosition());
		}
		// 片段里左手离开枪（换弹匣、压弹）：叠加片段里的左手已经走了，IK 放开。比重淡下去时回到枪上。
		// 放开的那一份是比重乘（1 − 曲线），几个片段加起来：同右手的轨迹，两步交接时（压弹接压弹，接缝上左手都不在枪上）手不半路回到枪上。
		// 整段不在枪上的片段读步上的 bLeftHandOff（那条全 0 的曲线压缩时被丢掉了）。
		// 左手在跟着枪机走的护木上（泵动霰弹枪）时泵动的片段不看：手的目标跟着护木挪（FGinkgoWeaponRigData::LeftHandSlide），IK 一直握着
		const auto IsForendStep = [Weapon, Montage](const FGameplayTag& Step)
		{
			const FGinkgoWeaponStep* Clip = Weapon->Steps.Find(Step);
			return Clip && Clip->Montage == Montage;
		};
		const bool bForendRide = Weapon->bLeftHandOnForend && (IsForendStep(FWeaponTags::Weapon_Step_CycleBolt) || IsForendStep(FWeaponTags::Weapon_Step_CycleBoltLowered)
			|| IsForendStep(FWeaponTags::Weapon_Step_CloseBolt));
		if (!bForendRide)
		{
			const bool bLeftHandOff = Algo::AnyOf(Weapon->Steps, [Montage](const TPair<FGameplayTag, FGinkgoWeaponStep>& Step)
			{
				return Step.Value.Montage == Montage && Step.Value.bLeftHandOff;
			});
			if (bLeftHandOff)
			{
				LeftHandRelease += Weight;
			}
			else if (HasMontageCurve(*Montage, LeftHandIkCurveName))
			{
				const float Curve = FMath::Clamp(EvaluateMontageCurve(*Montage, LeftHandIkCurveName, Instance->GetPosition()), 0.f, 1.f);
				LeftHandRelease += Weight * (1.f - Curve);
			}
		}
		// 压弹时手里的那一发：片段被打断（开始淡出）就没有了，放回弹药盒
		if (Instance->IsActive() && HasMontageCurve(*Montage, RoundInHandCurveName))
		{
			bRoundInHand |= EvaluateMontageCurve(*Montage, RoundInHandCurveName, Instance->GetPosition()) > 0.5f;
		}
		// 枪栓：片段上的两条曲线，同手的轨迹一样比重加起来、取比重大的那个（接缝是同一个姿势）。开火的片段没有曲线，枪栓读机构状态。
		// 开始淡出以后不再读（片段还在往下放）：拉到一半被收枪打断时枪栓从那个位置直接去机构状态的位置，不先跟着往前走一段再回来
		if (HasMontageCurve(*Montage, BoltTravelCurveName))
		{
			BoltClipWeight = FMath::Min(BoltClipWeight + Weight, 1.f);
			if (Instance->IsActive() && Weight > BoltSource)
			{
				BoltClip.Travel = FMath::Clamp(EvaluateMontageCurve(*Montage, BoltTravelCurveName, Instance->GetPosition()), 0.f, 1.f);
				BoltClip.Lift = FMath::Clamp(EvaluateMontageCurve(*Montage, BoltLiftCurveName, Instance->GetPosition()), 0.f, 1.f);
				BoltSource = Weight;
			}
		}
		for (const TPair<FGameplayTag, FGinkgoWeaponStep>& Step : Weapon->Steps)
		{
			const UAnimSequence* Track = Step.Value.RightHandTrack;
			const USkeleton* Skeleton = Track ? Track->GetSkeleton() : nullptr;
			if (Step.Value.Montage != Montage || !Skeleton)
			{
				continue;
			}
			// 两个片段交接时（一个淡出、一个淡入，接缝是同一个姿势）比重加起来，手不掉回握把；轨迹取比重大的那个
			Snapshot.RightHandTrackWeight = FMath::Min(Snapshot.RightHandTrackWeight + Weight, 1.f);
			const int32 Bone = Skeleton->GetReferenceSkeleton().FindBoneIndex(RightHandTrackBoneName);
			if (Bone != INDEX_NONE && Weight > TrackSource)
			{
				Track->GetBoneTransform(Snapshot.RightHandTrack, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(static_cast<double>(Instance->GetPosition())), /*bUseRawData*/ false);
				TrackSource = Weight;
			}
			break;
		}
	}
	Snapshot.LeftHandClipGrip = 1.f - FMath::Min(LeftHandRelease, 1.f);
	Character.SetRoundInHand(bRoundInHand);
}

FAnimMontageInstance* UGinkgoAnimInstance::FindHandReachInstance(const int32 Index)
{
	if (HandReachInstance[Index] != INDEX_NONE)
	{
		return GetMontageInstanceForID(HandReachInstance[Index]);
	}
	const UAnimMontage* Montage = HandReach[Index].IsSet() ? HandReach[Index]->Montage.Get() : nullptr;
	const TArray<FFadingHandReach, TInlineAllocator<2>>& Fading = FadingHandReach[Index];
	FAnimMontageInstance* Instance = Montage ? FindMontageInstance(MontageInstances, *Montage, [&Fading](const int32 InstanceID)
	{
		return Fading.ContainsByPredicate([InstanceID](const FFadingHandReach& Entry) { return Entry.InstanceID == InstanceID; });
	}) : nullptr;
	if (Instance)
	{
		HandReachInstance[Index] = Instance->GetInstanceID();
	}
	return Instance;
}

const FGinkgoHandReach* UGinkgoAnimInstance::GetHandReach(const EGinkgoHand Hand) const
{
	const TOptional<FGinkgoHandReach>& Reach = HandReach[static_cast<int32>(Hand)];
	return Reach.IsSet() ? &Reach.GetValue() : nullptr;
}

void UGinkgoAnimInstance::UpdateHandReach()
{
	// 蒙太奇已经推进到这一帧（UpdateMontage 在 NativeUpdateAnimation 之前）；实例没了就是放完了（淡出完），清掉
	for (int32 Index = 0; Index < 2; ++Index)
	{
		// 被顶掉的：随自己的实例淡出，先顶掉的在前
		TArray<FFadingHandReach, TInlineAllocator<2>>& Fading = FadingHandReach[Index];
		Fading.RemoveAll([this](const FFadingHandReach& Entry) { return GetMontageInstanceForID(Entry.InstanceID) == nullptr; });
		Snapshot.FadingReach[Index].Reset();
		for (FFadingHandReach& Entry : Fading)
		{
			Snapshot.FadingReach[Index].Add(MakeHandReachRigData(Entry.Reach, *GetMontageInstanceForID(Entry.InstanceID), static_cast<EGinkgoHand>(Index), Entry.Shoulder,
				Entry.LastItem, Entry.Frozen));
		}

		FGinkgoHandReachRigData& Out = Snapshot.Reach[Index];
		Out = FGinkgoHandReachRigData();
		if (!HandReach[Index].IsSet())
		{
			continue;
		}
		const FAnimMontageInstance* Instance = FindHandReachInstance(Index);
		if (!Instance)
		{
			HandReach[Index].Reset();
			HandReachInstance[Index] = INDEX_NONE;
			HandReachFrozen[Index].Reset();
			continue;
		}
		Out = MakeHandReachRigData(HandReach[Index].GetValue(), *Instance, static_cast<EGinkgoHand>(Index), HandReachShoulder[Index], HandReachLastItem[Index],
			HandReachFrozen[Index]);
	}
}

void UGinkgoAnimInstance::UpdateAimPoint(const ACharacter& Character, const FRotator& View, const FRotator& AimRotation, const float DeltaSeconds)
{
	const APlayerController* PlayerController = Cast<APlayerController>(Character.GetController());
	const AGinkgoPlayerCameraManager* Camera = PlayerController ? Cast<AGinkgoPlayerCameraManager>(PlayerController->PlayerCameraManager) : nullptr;
	const UWorld* World = GetWorld();
	if (!World || !Character.GetController())
	{
		return;
	}
	// 战术控制源只给当前目击或冻结记忆中的点。探身前眼位可能仍被墙遮住，不能据此把枪口目标缩到墙上。
	FVector TacticalPoint;
	if (const AGinkgoCharacter* TacticalCharacter = Cast<AGinkgoCharacter>(&Character);
		TacticalCharacter && TacticalCharacter->GetTacticalAimPoint(TacticalPoint))
	{
		const FVector Eye = Character.GetPawnViewLocation();
		Snapshot.AimPoint = Eye + (AimRotation.Quaternion() * View.Quaternion().Inverse()).RotateVector(TacticalPoint - Eye);
		Snapshot.bHasAimPoint = true;
		return;
	}
	// 玩家从预测相机取视线，AI 从角色眼位取视线；后面的距离平滑与 Control Rig 枪口对准共用。
	const FVector Origin = Camera ? Camera->PredictViewLocation(Character, View) : Character.GetPawnViewLocation();
	const FVector ViewDirection = AimRotation.Vector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoAimPoint), /*bTraceComplex*/ false, &Character);
	FHitResult Hit;
	double Distance = AimTraceDistance;
	// 同开枪的那条线一个通道（Docs/Design/Damage.md「命中」）：人形靶挡子弹，不挡 Visibility
	if (World->LineTraceSingleByChannel(Hit, Origin, Origin + ViewDirection * AimTraceDistance, Ginkgo::Combat::WeaponTraceChannel, Params))
	{
		Distance = Hit.Distance;
	}
	// 离枪口太近的往外推：沿视角量，枪口取上一帧的（这一帧的姿势还没求）。
	// 探身有效时用未探身参考枪口定这个下限，避免最终枪口的位置通过瞄准距离反过来影响参考判定。
	const AGinkgoCharacter* GinkgoCharacter = Cast<AGinkgoCharacter>(&Character);
	const UPoseableMeshComponent* Gun = GinkgoCharacter ? GinkgoCharacter->GetHeldWeaponMesh() : nullptr;
	double MinDistance = 1.0;
	if (Gun && Snapshot.Hold.Weapon)
	{
		const FTransform ReferenceGun = bPeekGunReady && PeekWeapon.Get() == Snapshot.Hold.Weapon && !Snapshot.Motion.bCut
			? PeekGun * GetSkelMeshComponent()->GetComponentTransform() : Gun->GetComponentTransform();
		const FVector Muzzle = ReferenceGun.TransformPosition(Snapshot.MuzzleOffset);
		MinDistance = FMath::Max(MinDistance, FVector::DotProduct(Muzzle - Origin, ViewDirection) + MinAimDistance);
	}
	// 远近过弹簧：追距离的倒数。连续地变的（走近、走远、扫过地面）前馈它的速率、不落后，前馈最多 MaxAimInverseDistanceRate；
	// 一下子变的（准星扫过边）超出的部分留在误差里缓过去。同腿部 IK 的 Track：误差从这一帧开始时的目标量起
	const double Clamped = FMath::Max(Distance, MinDistance);
	const double TargetInverse = 1.0 / Clamped;
	if (!bAimDistanceValid || DeltaSeconds <= 0.f)
	{
		AimInverseDistance = TargetInverse;
		AimInverseDistanceVelocity = 0.0;
		bAimDistanceValid = true;
	}
	else
	{
		const double Rate = FMath::Clamp((TargetInverse - 1.0 / LastAimDistance) / DeltaSeconds, -static_cast<double>(MaxAimInverseDistanceRate),
			static_cast<double>(MaxAimInverseDistanceRate));
		double Error = AimInverseDistance - (TargetInverse - Rate * DeltaSeconds);
		double ErrorVelocity = AimInverseDistanceVelocity - Rate;
		Ginkgo::Spring::StepCritical(Error, ErrorVelocity, AimDistanceOmega, DeltaSeconds);
		AimInverseDistance = TargetInverse + Error;
		AimInverseDistanceVelocity = Rate + ErrorVelocity;
	}
	LastAimDistance = Clamped;
	Snapshot.bHasAimPoint = true;
	Snapshot.AimPoint = Origin + ViewDirection * FMath::Max(1.0 / FMath::Max(AimInverseDistance, 1e-6), MinDistance);
}

void UGinkgoAnimInstance::UpdateMotion(const float DeltaSeconds)
{
	Snapshot.Motion.DeltaSeconds = DeltaSeconds;
	Ginkgo::Animation::FGinkgoMotionDifferencer::FOutput Output = Differencer.Update(Snapshot.Motion);
	if (Snapshot.Traversal.bDrivingRootMotion || bTraversalWasDriving)
	{
		// 差分器仍记录真实位置，以便下一帧正常接续；墙上的根运动不驱动底层跑步、急停或落地状态。
		// 交接帧过滤最后一段 Warp 的距离，但用移动组件的出口速度立刻准备走跑；偏航差分继续接住胶囊转向。
		Output.Velocity = Snapshot.Traversal.bDrivingRootMotion ? FVector::ZeroVector
			: FVector(Snapshot.MovementVelocity.X, Snapshot.MovementVelocity.Y, 0.);
		Output.VerticalSpeed = 0.;
		Output.FrameDistance = 0.;
		Output.bReset = true;
	}

	// 加速度：相邻两次差分速度之差，过弹簧。没有上一帧时从零开始；帧长为零时差分返回上一次的结果，弹簧也不动
	FVector RawAcceleration = FVector::ZeroVector;
	if (Output.bReset)
	{
		AccelerationSpring.Reset(FVector::ZeroVector);
	}
	else if (DeltaSeconds > 0.f)
	{
		RawAcceleration = (Output.Velocity - Motion.Velocity) / DeltaSeconds;
		AccelerationSpring.Step(RawAcceleration, AccelerationOmega, DeltaSeconds);
	}

	Motion.Velocity = Output.Velocity;
	Motion.Speed = Output.Velocity.Size2D();
	Motion.LocalAngle = Ginkgo::Animation::LocalAngle(Output.Velocity, Snapshot.Motion.FacingYaw, MinDirectionSpeed);
	Motion.VerticalSpeed = Output.VerticalSpeed;
	Motion.Acceleration = AccelerationSpring.Value;
	Motion.FrameDistance = Output.FrameDistance;
	Motion.FacingYaw = Snapshot.Motion.FacingYaw;
	Motion.YawRate = Output.YawRate;

#if !UE_BUILD_SHIPPING
	DebugFrame.CorrectionOffset = Output.CorrectionOffset;
	DebugFrame.RawAcceleration = RawAcceleration;
	DebugFrame.bReset = Output.bReset;
#endif
}

void UGinkgoAnimInstance::UpdateIntent()
{
	const FVector Input(Snapshot.InputAcceleration.X, Snapshot.InputAcceleration.Y, 0.0);
	Intent.bHasIntent = !Input.IsNearlyZero();
	Intent.Direction = Intent.bHasIntent ? Input.GetSafeNormal() : FVector::ZeroVector;
	Intent.Scale = Intent.bHasIntent ? Snapshot.InputScale : 0.f;
	Intent.AngleToVelocity = Intent.bHasIntent && Motion.Speed > MinDirectionSpeed
		? FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Intent.Direction | Motion.Velocity / Motion.Speed, -1.0, 1.0)))
		: 0.f;

	// 停止点与反向点只在地面上有意义：空中没有摩擦，滑铲的减速是另一套
	if (Snapshot.Mode != EGinkgoAnimMode::Ground)
	{
		Intent.StopDistance = 0.f;
		Intent.PivotDistance = 0.f;
		return;
	}
	Intent.StopDistance = Ginkgo::Animation::StopDistance(Motion.Speed, Snapshot.Friction, Snapshot.StopSpeed);
	// 反向点：速度在意图反方向上的分量过零的地方。速度高于停止速度时摩擦对每个分量按同一比例衰减，分量单独推进是精确的；
	// 低于它之后斜着反向时略有偏差。意图与速度夹角不到 90° 时这个分量为零，没有反向
	const double ReverseSpeed = Intent.bHasIntent ? FMath::Max(-(Motion.Velocity | Intent.Direction), 0.0) : 0.0;
	Intent.PivotDistance = Ginkgo::Animation::PivotDistance(ReverseSpeed, Snapshot.Friction, Snapshot.StopSpeed, Snapshot.PivotDeceleration);
}

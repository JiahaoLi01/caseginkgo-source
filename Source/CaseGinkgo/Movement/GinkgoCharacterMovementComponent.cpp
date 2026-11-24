#include "GinkgoCharacterMovementComponent.h"

#include "Camera/GinkgoCameraContextComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Debug/DebugDrawService.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "GameFramework/Character.h"
#include "GinkgoMovementConfig.h"
#include "GinkgoMovementMath.h"
#include "GinkgoMovementTrace.h"
#include "Traversal/GinkgoTraversalComponent.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/MovementTags.h"  // 窗口的倍率类别与能力

UGinkgoCharacterMovementComponent::UGinkgoCharacterMovementComponent()
{
	// 角色的偏航全由 PhysicsRotation 定：平时面向视角（射击游戏的常态，移动是平移），在地面上冲刺时面向速度（ComputeOrientToMovementRotation）。
	// 借父类"朝向运动"的那条路，只换朝向的目标；转速为负即每次移动一帧转到位，客户端预测、服务器执行与重放结果相同
	bOrientRotationToMovement = true;
	RotationRate = FRotator(0.f, -1.f, 0.f);
	// 蹲着也能走下台阶边缘（同 Source）
	bCanWalkOffLedgesWhenCrouching = true;
	GetNavAgentPropertiesRef().bCanCrouch = true;
	// 服务器的回包换成带模拟状态的版本，发给服务器的移动多带方向键按下的计数
	SetMoveResponseDataContainer(MoveResponseData);
	SetNetworkMoveDataContainer(NetworkMoveData);
}

void UGinkgoCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	CameraContext = UGinkgoCameraContextComponent::Find(GetOwner());
	if (!MovementConfig)
	{
		MovementConfig = UGinkgoDeveloperSettings::Get()->MovementConfig.LoadSynchronous();
	}
#if !NO_LOGGING
	// 窗口也列出来：配置资产里的列表可能与类默认值不同
	TArray<FString> WindowNames;
	for (const TObjectPtr<UGinkgoMovementWindow>& Definition : GetWindowDefinitions())
	{
		WindowNames.Add(Definition ? Definition->Tag.ToString() : TEXT("空"));
	}
	UE_LOG(LogGinkgoMovement, Log, TEXT("[Movement] %s 使用移动配置：%s；窗口 %d 种：%s"), *GetNameSafe(GetOwner()),
		MovementConfig ? *MovementConfig->GetPathName() : TEXT("类默认值（未指定资产）"), WindowNames.Num(),
		WindowNames.IsEmpty() ? TEXT("无") : *FString::Join(WindowNames, TEXT("、")));
	// 配置资产存过一次，窗口列表就存进了资产（存盘时窗口子对象总算作与类默认值的不同），类默认值后来加的窗口不会自动进去。
	// 按标签的层级匹配：资产里的窗口标签是默认窗口的标签或它的子标签（换成了一个变体）就算有
	if (MovementConfig)
	{
		TArray<FString> Missing;
		for (const TObjectPtr<UGinkgoMovementWindow>& Default : GetDefault<UGinkgoMovementConfig>()->Windows)
		{
			if (Default && !MovementConfig->Windows.ContainsByPredicate([&Default](const TObjectPtr<UGinkgoMovementWindow>& Window) { return Window && Window->Tag.MatchesTag(Default->Tag); }))
			{
				Missing.Add(Default->Tag.ToString());
			}
		}
		UE_CLOG(!Missing.IsEmpty(), LogGinkgoMovement, Warning, TEXT("[Movement] 移动配置 %s 没有类默认值里的窗口：%s。要用的话，在资产里把 Windows 重置为默认值（或手动加上）再保存"),
			*MovementConfig->GetPathName(), *FString::Join(Missing, TEXT("、")));
	}
#endif
#if !UE_BUILD_SHIPPING
	DebugDrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &ThisClass::DrawMovementDebug));
#endif
}

void UGinkgoCharacterMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
#if !UE_BUILD_SHIPPING
	UDebugDrawService::Unregister(DebugDrawHandle);
#endif
	Super::EndPlay(EndPlayReason);
}

void UGinkgoCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(CharacterOwner))
	{
		Character->StepCrouchMeshBlend(DeltaTime);
	}
#if !UE_BUILD_SHIPPING
	TickMovementDebug();
#endif
}

void UGinkgoCharacterMovementComponent::Crouch(const bool bClientSimulation)
{
	const double HeightBefore = GetMeshBaseHeight();
	Super::Crouch(bClientSimulation);
	AbsorbMeshBaseJump(HeightBefore, bClientSimulation);
}

void UGinkgoCharacterMovementComponent::UnCrouch(const bool bClientSimulation)
{
	const double HeightBefore = GetMeshBaseHeight();
	Super::UnCrouch(bClientSimulation);
	AbsorbMeshBaseJump(HeightBefore, bClientSimulation);
}

double UGinkgoCharacterMovementComponent::GetMeshBaseHeight() const
{
	const AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(CharacterOwner);
	return Character ? Character->GetMeshBaseHeight() : 0.0;
}

void UGinkgoCharacterMovementComponent::AbsorbMeshBaseJump(const double HeightBefore, const bool bClientSimulation)
{
	AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(CharacterOwner);
	if (Character && Character->IsLocallyControlled() && !bClientSimulation && !Character->bClientUpdating)
	{
		Character->AbsorbMeshBaseJump(Character->GetMeshBaseHeight() - HeightBefore);
	}
}

const UGinkgoMovementConfig& UGinkgoCharacterMovementComponent::GetConfig() const
{
	return MovementConfig ? *MovementConfig : *GetDefault<UGinkgoMovementConfig>();
}

// ===== 速度模型 =====

void UGinkgoCharacterMovementComponent::CalcVelocity(const float DeltaTime, const float Friction, const bool bFluid, const float BrakingDeceleration)
{
	// 与父类相同的守卫：根运动期间、模拟代理（速度由复制得到）、过小的步长都不计算
	if (!HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME
		|| (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy && !bWasSimulatingRootMotion))
	{
		return;
	}

	switch (MovementMode)
	{
	case MOVE_Walking:
		[[fallthrough]];
	case MOVE_NavWalking:
		CalcGroundVelocity(DeltaTime);
		break;
	case MOVE_Falling:
		CalcAirVelocity(DeltaTime);
		break;
	default:
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		break;
	}
}

void UGinkgoCharacterMovementComponent::CalcGroundVelocity(const float DeltaTime)
{
	// PhysWalking 调到这里之前已把 Velocity 与 Acceleration 压到水平面
	const UGinkgoMovementConfig& Config = GetConfig();
	const bool bInLandingGrace = SimState.TimeSinceLanded < Config.LandingFrictionGrace;
	SimState.TimeSinceLanded = FMath::Min(SimState.TimeSinceLanded + DeltaTime, FGinkgoMovementSimState::LandingTimeLimit);
	if (SimState.bSliding)
	{
		CalcSlideVelocity(DeltaTime, bInLandingGrace);
		return;
	}

	if (!bInLandingGrace)
	{
		Ginkgo::Movement::ApplyFriction(Velocity, Config.GroundFriction, Config.StopSpeed, DeltaTime);
	}

	if (!Acceleration.IsNearlyZero())
	{
		// wishspeed 保留手柄的模拟量。速度平衡在 wishspeed：转向不会让速度超过它，更快的来速也不会被截断
		const float WishSpeed = GetMaxSpeed() * AnalogInputModifier;
		Ginkgo::Movement::AccelerateOnGround(Velocity, Acceleration.GetSafeNormal(), WishSpeed, Config.GroundAccel * FMath::Max(WishSpeed, Config.GroundAccelMinWishSpeed) * DeltaTime);
	}
}

void UGinkgoCharacterMovementComponent::CalcSlideVelocity(const float DeltaTime, const bool bInLandingGrace)
{
	const UGinkgoMovementConfig& Config = GetConfig();
	SimState.SlideTime += DeltaTime;

	// 落地免摩擦窗口里也不减速：滑铲接跳落在这个范围里获得最大速度，但滑铲跳只要在速度降为奔跑速度前使用就有收益。
	if (!bInLandingGrace)
	{
		const double Speed = Velocity.Size();
		if (Speed > UE_KINDA_SMALL_NUMBER)
		{
			Velocity *= Ginkgo::Movement::SlideDecelerate(Speed, Config.SlideDecel, Config.SlideDecay, DeltaTime) / Speed;
		}
	}

	// 只能转向和刹车：输入去掉沿速度方向的正分量，转向后速度大小不超过转向前
	const FVector Steer = Ginkgo::Movement::RemoveForwardInput(Acceleration.GetSafeNormal() * AnalogInputModifier, Velocity);
	Ginkgo::Movement::SteerSlide(Velocity, Steer, Config.SlideSteerAccel * DeltaTime);
}

void UGinkgoCharacterMovementComponent::CalcAirVelocity(const float DeltaTime)
{
	// PhysFalling 调到这里之前已把竖直速度置 0、Acceleration 换成 GetFallingLateralAcceleration 的结果，结束后恢复竖直速度
	if (Acceleration.IsNearlyZero())
	{
		return;
	}
	const UGinkgoMovementConfig& Config = GetConfig();
	const float WishSpeed = GetMaxSpeed() * AnalogInputModifier;
	// SpeedCap 很小、AccelStep 按完整 wishspeed 算：直接往前加不了多少，与速度接近垂直的输入却总能加满——扫射增益与转向都来自这里
	Ginkgo::Movement::Accelerate(Velocity, Acceleration.GetSafeNormal(), FMath::Min(WishSpeed, Config.AirSpeedCap), Config.AirAccel * WishSpeed * DeltaTime);
}

FVector UGinkgoCharacterMovementComponent::GetFallingLateralAcceleration(float DeltaTime)
{
	// 原样返回水平输入加速度，不乘 AirControl、不走 BoostAirControl：空中手感完全由 CalcAirVelocity 的投影规则决定
	return FVector(Acceleration.X, Acceleration.Y, 0.f);
}

float UGinkgoCharacterMovementComponent::GetMaxSpeed() const
{
	const UGinkgoMovementConfig& Config = GetConfig();
	switch (MovementMode)
	{
	case MOVE_Walking:
		[[fallthrough]];
	case MOVE_NavWalking:
		return Ginkgo::Movement::GroundWishSpeed(Config.RunSpeed, Config.SprintSpeed, Config.CrouchSpeed, IsCrouching(), bWantsToSprint,
			Ginkgo::Movement::UnquantizeSpeedScale(HeldSpeedScale), Ginkgo::Movement::UnquantizeSpeedScale(AdsSpeedScale));
	case MOVE_Falling:
		// 空中蹲下不减速：Source 的蹲伏减速只在地面生效。持枪、开镜的倍率也不乘：空中加速的一步按 wishspeed 算，乘了会削掉空中扫射
		return bWantsToSprint ? Config.SprintSpeed : Config.RunSpeed;
	default:
		return Super::GetMaxSpeed();
	}
}

FRotator UGinkgoCharacterMovementComponent::ComputeOrientToMovementRotation(const FRotator& CurrentRotation, const float DeltaTime, FRotator& DeltaRotation) const
{
	// 父类朝的是输入加速度的方向：空中扫射时输入与速度几乎垂直，冲刺的朝向要的是速度。
	// 视角取这次移动的：服务器执行时是客户端发来的，客户端重放时是存档里记录时的
	const double Yaw = Ginkgo::Movement::ComputeFacingYaw(IsSprintFacing(), Velocity, GetMoveControlRotation().Yaw, GetConfig().SprintFacingMinSpeed);
	return FRotator(0.0, Yaw, 0.0);
}

float UGinkgoCharacterMovementComponent::GetGravityZ() const
{
	// 角色用自己的重力，不跟世界设置走；GravityScale 仍然生效，留给能力（墙跑的重力回升）调
	return -GetConfig().Gravity * GravityScale;
}

// ===== 跳跃与落地 =====

bool UGinkgoCharacterMovementComponent::DoJump(const bool bReplayingMoves, const float DeltaTime)
{
	// 与父类相同的守卫：不能跳时不跳，也就不发起跳事件
	if (!CharacterOwner || !CharacterOwner->CanJump())
	{
		return false;
	}
	// 起跳速度由跳跃高度和当前重力推出；父类按 max(当前竖直速度, JumpZVelocity) 设置，不叠加。
	// 跳跃高度乘窗口的跳跃惩罚倍率（起跳速度因此乘它的平方根），在起跳事件之前读：土狼时间这类窗口会被起跳结束
	const float HeightScale = GetWindowMultiplier(FMovementTags::Movement_Window_Effect_JumpPenalty);
	JumpZVelocity = Ginkgo::Movement::JumpVelocityForHeight(GetConfig().JumpHeight * HeightScale, -GetGravityZ());
	// 下落中的第一跳靠窗口给的"空中起跳"才算第一跳（土狼时间）。起跳事件会结束那个窗口，父类的 DoJump 还要再查一次 CanJump：
	// 这一跳期间沿用起跳前的判断，否则土狼起跳在父类那里被拒
	TGuardValue<bool> JumpingWhileFalling(bJumpingWhileFalling, IsFalling() && HasWindowAbility(FMovementTags::Movement_Window_Ability_JumpWhileFalling));
	// 起跳事件先经过窗口再交给父类：行为可以改起跳前的水平速度（死滑扣回起步加速）。不能放到父类之后：父类切到下落时先发滑铲结束，
	// 死滑这类到滑铲结束就结束的窗口就等不到起跳了。事件带上这次的跳跃高度倍率，小于 1 就是疲劳的起跳（lurch 只在不疲劳的起跳时推入）
	HandleWindowEvent(EGinkgoWindowEvent::Jumped, HeightScale, HeightScale < 1.f);
	TGuardValue<bool> InDoJump(bInDoJump, true);
	return Super::DoJump(bReplayingMoves, DeltaTime);
}

bool UGinkgoCharacterMovementComponent::CanJumpWhileFalling() const
{
	return bJumpingWhileFalling || HasWindowAbility(FMovementTags::Movement_Window_Ability_JumpWhileFalling);
}

bool UGinkgoCharacterMovementComponent::CanAttemptJump() const
{
	// 父类在按住蹲时不让跳；Source 里蹲着也能跳
	return IsJumpAllowed() && (IsMovingOnGround() || IsFalling());
}

void UGinkgoCharacterMovementComponent::OnMovementModeChanged(const EMovementMode PreviousMovementMode, const uint8 PreviousCustomMode)
{
	// 父类切到行走时会去掉竖直速度，落地接滑铲要用的下落速度先记下来
	const float FallSpeed = -Velocity.Z;
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	if (bHandlingMoveResponse)
	{
		// 应用校正时换成服务器的移动模式：服务器那边的落地、离地已经算在它的模拟状态里，回包处理完整体放回，这里不再处理一次
		return;
	}
	if (!IsMovingOnGround())
	{
		EndSlide();
		// 起跳的事件在 DoJump 里已经发过；没起跳就离开地面的是走下了平台边缘
		if ((PreviousMovementMode == MOVE_Walking || PreviousMovementMode == MOVE_NavWalking) && IsFalling() && !bInDoJump)
		{
			HandleWindowEvent(EGinkgoWindowEvent::WalkedOffLedge);
		}
		return;
	}
	if (PreviousMovementMode == MOVE_Falling)
	{
		SimState.TimeSinceLanded = 0.f;
		LastLandingFallSpeed = FallSpeed;
		HandleWindowEvent(EGinkgoWindowEvent::Landed);
		if (CanStartSlide(FallSpeed))
		{
			StartSlide();
		}
	}
}

// ===== 滑铲 =====

void UGinkgoCharacterMovementComponent::UpdateCharacterStateBeforeMovement(const float DeltaSeconds)
{
	if (IsTraversing()) { Acceleration = FVector::ZeroVector; return; }
	// 父类先按蹲的输入蹲下或站起；松开蹲时头顶有遮挡站不起来，就保持下蹲
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds);
	HandleDirectionPresses(DeltaSeconds);
	if (SimState.bSliding)
	{
		if (!bWantsToCrouch || !IsMovingOnGround() || Velocity.Size() < GetConfig().SlideEndSpeed)
		{
			EndSlide();
		}
	}
	else if (CanStartSlide({}))
	{
		StartSlide();
	}
}

bool UGinkgoCharacterMovementComponent::BeginTraversalMovement(const FQuat& Facing)
{
	if (!HasValidData() || !IsMovingOnGround()) { return false; }
	bWantsToCrouch = false;
	UnCrouch(false);
	if (CharacterOwner->bIsCrouched) { return false; }
	StopMovementImmediately();
	bTraversalBlocked = false;
	MoveUpdatedComponent(FVector::ZeroVector, Facing, false);
	SetMovementMode(MOVE_Custom, 1);
	// 几何与姿势代理已接受才产生事件；清除规则属于窗口定义，沿用状态存档、校正和时间轴因果链。
	HandleWindowEvent(EGinkgoWindowEvent::TraversalStarted);
	return true;
}

void UGinkgoCharacterMovementComponent::EndTraversalMovement()
{
	if (!IsTraversing()) { return; }
	// 正常落稳保留动画出口速度，下一帧由正常地面摩擦/输入接管；受阻则已在 PhysCustom 清零。
	// 中段取消时恢复站立代理可能发生重叠，使用 CMC 的 MTD 解穿透，不回跳起点或传送到落点。
	FHitResult Overlap;
	const auto* Capsule = CharacterOwner->GetCapsuleComponent();
	const FVector Center = UpdatedComponent->GetComponentLocation();
	const FQuat Rotation = UpdatedComponent->GetComponentQuat();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalRestoreBody), false, CharacterOwner);
	GetWorld()->SweepSingleByChannel(Overlap, Center, Center + FVector::UpVector * .1, Rotation, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()), Params,
		FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
	if (Overlap.bStartPenetrating) { ResolvePenetration(GetPenetrationAdjustment(Overlap), Overlap, Rotation); }
	FFindFloorResult Floor;
	FindFloor(UpdatedComponent->GetComponentLocation(), Floor, false);
	SetMovementMode(Floor.IsWalkableFloor() && Floor.FloorDist <= 5.f ? MOVE_Walking : MOVE_Falling);
}

void UGinkgoCharacterMovementComponent::PhysicsRotation(const float DeltaTime)
{
	// 攀爬在本次移动物理计算前开始，朝向由原根轨迹及入场修正共同拥有。
	if (!IsTraversing()) { Super::PhysicsRotation(DeltaTime); }
}

void UGinkgoCharacterMovementComponent::PhysCustom(const float DeltaTime, const int32 Iterations)
{
	if (!IsTraversing()) { Super::PhysCustom(DeltaTime, Iterations); return; }
	// 模拟代理只接受标准角色移动复制与网格平滑，不能再执行本地抓沿或把同一根位移走第二次。
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy) { return; }
	if (!HasValidData() || DeltaTime < MIN_TICK_TIME) { return; }
	auto* Character = CastChecked<AGinkgoCharacter>(CharacterOwner);
	auto* Traversal = Character->GetTraversal();
	if (!Traversal->CanContinue())
	{
		Traversal->FinishMotion(false);
		EndTraversalMovement();
		StartNewPhysics(DeltaTime, Iterations + 1);
		return;
	}
	FVector Delta;
	FQuat Facing;
	const float UsedTime = Traversal->AdvanceMotion(DeltaTime, Delta, Facing);
	const FVector Start = UpdatedComponent->GetComponentLocation();
	FHitResult Hit;
	if (!Traversal->SweepMotion(Start, Start + Delta, Hit))
	{
		bTraversalBlocked = true;
		Velocity = FVector::ZeroVector;
		Traversal->FinishMotion(false);
	}
	else
	{
		// 已由独立姿势胶囊查过路径；保持原动画增量，不再让站立胶囊二次裁剪。
		MoveUpdatedComponent(Delta, Facing, false);
		Velocity = UsedTime > MIN_TICK_TIME ? Delta / UsedTime : FVector::ZeroVector;
		if (Traversal->GetMotionTime() >= Traversal->GetMotionEndTime() - UE_KINDA_SMALL_NUMBER)
		{
			Traversal->FinishMotion(true);
		}
	}
	if (Traversal->GetSyncState().Phase != EGinkgoTraversalPhase::Moving)
	{
		EndTraversalMovement();
		StartNewPhysics(FMath::Max(DeltaTime - UsedTime, 0.f), Iterations + 1);
	}
}

void UGinkgoCharacterMovementComponent::UpdateCharacterStateAfterMovement(const float DeltaSeconds)
{
	Super::UpdateCharacterStateAfterMovement(DeltaSeconds);
	WriteCameraContext();
	// 窗口只按每次移动的 Δt 推进，两端在同一次移动上跨过时长边界；这次移动里推入的窗口也算上这一整段
	if (CharacterOwner && CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		WindowManager.Advance(GetWindowDefinitions(), SimState.Windows, DeltaSeconds);
		DispatchWindowChanges(WindowManager.TakeChanges());
#if GINKGO_MOVEMENT_TRACE_ENABLED
		FGinkgoMovementTrace::TraceWindowState(*this);
#endif
	}
}

bool UGinkgoCharacterMovementComponent::CanStartSlide(const TOptional<float> LandingFallSpeed) const
{
	// 模拟代理的状态来自复制，不在这里推导；滑铲用下蹲的碰撞体，蹲不下去就不铲
	if (SimState.bSliding || !CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy || !bWantsToCrouch || !IsCrouching()
		|| !IsMovingOnGround() || !CurrentFloor.IsWalkableFloor())
	{
		return false;
	}
	const UGinkgoMovementConfig& Config = GetConfig();
	const float HorizontalSpeed = Velocity.Size2D();
	const bool bFastEnough = LandingFallSpeed.IsSet()
		? HorizontalSpeed > Config.SlideLandHorizontalSpeed && LandingFallSpeed.GetValue() > Config.SlideLandFallSpeed
		: HorizontalSpeed >= Config.SlideStartSpeed;
	const FVector ViewForward = FRotator(0.f, GetMoveControlRotation().Yaw, 0.f).Vector();
	return bFastEnough
		&& Ginkgo::Movement::IsSlideDirectionAllowed(Acceleration, ViewForward, FMath::Cos(FMath::DegreesToRadians(Config.SlideMaxStartAngle)));
}

void UGinkgoCharacterMovementComponent::StartSlide()
{
	const UGinkgoMovementConfig& Config = GetConfig();
	SimState.bSliding = true;
	SimState.SlideTime = 0.f;
	const float HeldScale = Ginkgo::Movement::UnquantizeSpeedScale(HeldSpeedScale);
	SimState.SlideBoost = Ginkgo::Movement::ApplySlideBoost(Velocity, Config.SlideBoost * GetSlideBoostScale() * HeldScale, Config.SlideBoostCap * HeldScale);
	NotifySlideStarted(SimState.SlideBoost);
}

void UGinkgoCharacterMovementComponent::EndSlide()
{
	if (!SimState.bSliding)
	{
		return;
	}
	SimState.bSliding = false;
	HandleWindowEvent(EGinkgoWindowEvent::SlideEnded);
}

float UGinkgoCharacterMovementComponent::GetSlideBoostScale() const
{
	return GetWindowMultiplier(FMovementTags::Movement_Window_Effect_SlideBoost);
}

void UGinkgoCharacterMovementComponent::NotifySlideStarted(const float Boost)
{
	UE_LOG(LogGinkgoMovement, Verbose, TEXT("[Slide] %s 开始滑铲：起步加速 %.1f cm/s，速度 %.1f cm/s"), *GetNameSafe(CharacterOwner), Boost, Velocity.Size());
	HandleWindowEvent(EGinkgoWindowEvent::SlideStarted, Boost, Boost > 0.f);
}

FRotator UGinkgoCharacterMovementComponent::GetMoveControlRotation() const
{
	if (const FSavedMove_Character* ReplayedMove = GetCurrentReplayedSavedMove())
	{
		return ReplayedMove->SavedControlRotation;
	}
	return CharacterOwner->GetControlRotation();
}

// ===== 窗口 =====

void UGinkgoCharacterMovementComponent::RestoreSimState(const FGinkgoMovementSimState& State)
{
	SimState = State;
	WindowManager.RebuildTags(GetWindowDefinitions(), SimState.Windows);
}

FGinkgoMovementWindowManager::FDefinitions UGinkgoCharacterMovementComponent::GetWindowDefinitions() const
{
	return GetConfig().Windows;
}

bool UGinkgoCharacterMovementComponent::HasWindowAbility(const FGameplayTag& Ability) const
{
	return WindowManager.HasAbility(GetWindowDefinitions(), SimState.Windows, Ability);
}

float UGinkgoCharacterMovementComponent::GetWindowMultiplier(const FGameplayTag& Category) const
{
	return WindowManager.GetMultiplier(GetWindowDefinitions(), SimState.Windows, Category);
}

void UGinkgoCharacterMovementComponent::HandleWindowEvent(const EGinkgoWindowEvent Type, const float Value, const bool bFlag)
{
	if (!CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}
	const FGinkgoWindowEvent Event{Type, Value, bFlag};
#if GINKGO_MOVEMENT_TRACE_ENABLED
	FGinkgoMovementTrace::TraceWindowEvent(*this, Event);
#endif
	FGinkgoWindowEventContext Context;
	Context.Velocity = Velocity;
	Context.Acceleration = Acceleration;
	Context.KeyDirection = GetDirectionKeysWorld();
	WindowManager.HandleEvent(GetWindowDefinitions(), SimState.Windows, Event, Context);
	Velocity = Context.Velocity;
	const TArray<FGinkgoMovementWindowManager::FChange, TInlineAllocator<8>> Changes = WindowManager.TakeChanges();
	DispatchWindowChanges(Changes);
	if (!IsClientReplaying())
	{
		OnMovementWindowEvent.Broadcast(Event, Changes);
		PushCameraEvent(Event);
	}
}

void UGinkgoCharacterMovementComponent::DispatchWindowChanges(const TConstArrayView<FGinkgoMovementWindowManager::FChange> Changes)
{
	if (Changes.IsEmpty())
	{
		return;
	}
	// 客户端重放旧的移动时只改状态：这些变化第一次模拟时已经发过，或者是校正后才发现的，补发也已经晚了
	const bool bReplaying = IsClientReplaying();
	const FGinkgoMovementWindowManager::FDefinitions Definitions = GetWindowDefinitions();
	for (const FGinkgoMovementWindowManager::FChange& Change : Changes)
	{
		const UGinkgoMovementWindow* Definition = Definitions.IsValidIndex(Change.Definition) ? Definitions[Change.Definition].Get() : nullptr;
#if GINKGO_MOVEMENT_TRACE_ENABLED
		FGinkgoMovementTrace::TraceWindowChange(*this, Change.Definition, Definition, Change.FromPhase, Change.ToPhase, !bReplaying);
#endif
		if (bReplaying)
		{
#if !UE_BUILD_SHIPPING
			++ReplayedWindowChangeCount;
#endif
			continue;
		}
		UE_LOG(LogGinkgoMovement, Verbose, TEXT("[Window] %s %s：阶段 %d → %d"), *GetNameSafe(CharacterOwner),
			Definition ? *Definition->Tag.ToString() : TEXT("?"), Change.FromPhase, Change.ToPhase);
#if !UE_BUILD_SHIPPING
		RecordWindowChangeForDebug(Definition, Change);
#endif
		OnMovementWindowChanged.Broadcast(Definition, Change.FromPhase, Change.ToPhase);
	}
}

bool UGinkgoCharacterMovementComponent::IsClientReplaying() const
{
	return CharacterOwner && CharacterOwner->bClientUpdating;
}

// ===== 相机上下文 =====

void UGinkgoCharacterMovementComponent::PerformMovement(const float DeltaTime)
{
	TGuardValue<float> MoveDeltaTime(CurrentMoveDeltaTime, DeltaTime);
	auto* Character = Cast<AGinkgoCharacter>(CharacterOwner);
	auto* Traversal = Character ? Character->GetTraversal() : nullptr;
	if (Traversal) { Traversal->BeginMovement(DeltaTime); }
	const float TraversalStart = Traversal ? Traversal->GetSyncState().Time : 0.f;
	Super::PerformMovement(DeltaTime);
	if (Traversal) { Traversal->EndMovement(TraversalStart, DeltaTime); }
}

void UGinkgoCharacterMovementComponent::ProcessLanded(const FHitResult& Hit, const float remainingTime, const int32 Iterations)
{
	// 着地的处理里切到行走、发落地与落地接滑铲：这时这次移动还剩 remainingTime（着地的那一刻之后）
	TGuardValue<float> Remaining(LandingRemainingTime, remainingTime);
	Super::ProcessLanded(Hit, remainingTime, Iterations);
}

void UGinkgoCharacterMovementComponent::WriteCameraContext()
{
	UGinkgoCameraContextComponent* Context = CameraContext.Get();
	if (!Context || !CharacterOwner || !CharacterOwner->IsLocallyControlled())
	{
		return;
	}
	FGameplayTagContainer Tags = WindowManager.GetTags();
	if (IsMovingOnGround())
	{
		Tags.AddTag(FMovementTags::Movement_Grounded);
	}
	if (SimState.bSliding)
	{
		Tags.AddTag(FMovementTags::Movement_Slide);
	}
	if (IsSprintFacing())
	{
		Tags.AddTag(FMovementTags::Movement_Sprint);
	}
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags);
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, Velocity.Size2D());
	Context->SetScalar(FMovementTags::Movement_Value_SlideBoost, SimState.bSliding ? SimState.SlideBoost : 0.f);
	Context->SetScalar(FMovementTags::Movement_Value_RunSpeed, GetHeldRunSpeed());
}

void UGinkgoCharacterMovementComponent::PushCameraEvent(const FGinkgoWindowEvent& Event)
{
	UGinkgoCameraContextComponent* Context = CameraContext.Get();
	if (!Context || !CharacterOwner || !CharacterOwner->IsLocallyControlled())
	{
		return;
	}
	FGinkgoCameraEvent CameraEvent;
	switch (Event.Type)
	{
	case EGinkgoWindowEvent::SlideStarted:
		CameraEvent.Tag = FMovementTags::Movement_Event_SlideStarted;
		CameraEvent.Magnitude = Event.Value;
		break;
	case EGinkgoWindowEvent::Landed:
		CameraEvent.Tag = FMovementTags::Movement_Event_Landed;
		CameraEvent.Magnitude = LastLandingFallSpeed;
		break;
	default:
		return;
	}
	// 世界时间在这一帧的开头就推进到了帧末，这次移动覆盖 [帧末 − 帧长, 帧末]；着地的处理里取着地的那一刻
	const double Remaining = LandingRemainingTime >= 0.f ? LandingRemainingTime : CurrentMoveDeltaTime;
	CameraEvent.Time = GetWorld()->GetTimeSeconds() - FMath::Clamp(static_cast<double>(Remaining), 0.0, static_cast<double>(CurrentMoveDeltaTime));
	Context->PushEvent(CameraEvent);
	++CameraEventCount;
}

void UGinkgoCharacterMovementComponent::SetDirectionKeys(const FIntPoint Keys)
{
	DirectionKeys = Ginkgo::Movement::PackDirectionKeys(Keys);
}

void UGinkgoCharacterMovementComponent::SetSpeedScales(const float HeldScale, const float AdsScale)
{
	HeldSpeedScale = Ginkgo::Movement::QuantizeSpeedScale(HeldScale);
	AdsSpeedScale = Ginkgo::Movement::QuantizeSpeedScale(AdsScale);
}

float UGinkgoCharacterMovementComponent::GetHeldRunSpeed() const
{
	return GetConfig().RunSpeed * Ginkgo::Movement::UnquantizeSpeedScale(HeldSpeedScale);
}

float UGinkgoCharacterMovementComponent::GetHeldSprintSpeed() const
{
	return GetConfig().SprintSpeed * Ginkgo::Movement::UnquantizeSpeedScale(HeldSpeedScale);
}

float UGinkgoCharacterMovementComponent::GetGroundSpeedRatio() const
{
	return Ginkgo::Movement::UnquantizeSpeedScale(HeldSpeedScale) * Ginkgo::Movement::UnquantizeSpeedScale(AdsSpeedScale);
}

FVector UGinkgoCharacterMovementComponent::GetDirectionKeysWorld() const
{
	// 服务器收到的视角已经压成 16 位；客户端重放时引擎用存档填的这份数据还没压过，DirectionKeysToWorld 里统一再压一遍
	const FCharacterNetworkMoveData* MoveData = GetCurrentNetworkMoveData();
	const double Yaw = MoveData ? MoveData->ControlRotation.Yaw : (CharacterOwner ? CharacterOwner->GetControlRotation().Clamp().Yaw : 0.0);
	return Ginkgo::Movement::DirectionKeysToWorld(Ginkgo::Movement::UnpackDirectionKeys(DirectionKeys), Yaw);
}

void UGinkgoCharacterMovementComponent::HandleDirectionPresses(const float DeltaTime)
{
	if (DirectionPressCount == SimState.HandledDirectionPressCount || !CharacterOwner || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		return;
	}
	// 处理过的计数是模拟状态：校正时随服务器的状态放回，重放时已经处理过的按下不会再处理一次。
	// 数值是两次之间按了几次（一般是 1；丢了几次移动、或一帧里按了几个键时更多），计数按字节循环，差也按字节算
	const uint8 Presses = DirectionPressCount - SimState.HandledDirectionPressCount;
	SimState.HandledDirectionPressCount = DirectionPressCount;
#if !NO_LOGGING && !UE_BUILD_SHIPPING
	const FVector VelocityBefore = Velocity;
#endif
	HandleWindowEvent(EGinkgoWindowEvent::DirectionPressed, Presses);
#if !NO_LOGGING && !UE_BUILD_SHIPPING
	// 联网测量用（net_corrections.py 的 lurch 路线）：两端按计数对上同一次按下，比处理它的那次移动的时刻、Δt 与有没有转向。
	// 客户端第一次模拟时取这次移动存档的时刻，服务器与客户端重放取 MoveAutonomous 收到的；单机没有，记 0
	float MoveTimeStamp = AutonomousMoveTimeStamp;
	if (CharacterOwner->GetLocalRole() == ROLE_AutonomousProxy && !IsClientReplaying())
	{
		const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
		MoveTimeStamp = ClientData ? ClientData->CurrentTimeStamp : 0.f;
	}
	// 转向时另给出水平速度转过的角度（从上往下看，向右转为正）与大小的变化，看得出转向哪边、转了多少
	const FString Turn = Velocity.Equals(VelocityBefore, 0.0) ? FString(TEXT("没转")) : FString::Printf(TEXT("转向 %.1f°，水平速度 %.0f → %.0f cm/s"),
		FMath::RadiansToDegrees(FMath::Atan2(VelocityBefore.X * Velocity.Y - VelocityBefore.Y * Velocity.X, VelocityBefore.X * Velocity.X + VelocityBefore.Y * Velocity.Y)),
		VelocityBefore.Size2D(), Velocity.Size2D());
	UE_LOG(LogGinkgoMovement, Verbose, TEXT("[Press] %s 计数 %d，这次 %d 次，移动时刻 %.4f，Δt %.4f，%s%s"), *GetNameSafe(CharacterOwner), DirectionPressCount, Presses,
		MoveTimeStamp, DeltaTime, *Turn, IsClientReplaying() ? TEXT("（重放）") : TEXT(""));
#endif
}

// ===== 网络 =====

FNetworkPredictionData_Client* UGinkgoCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		// 与父类相同的惰性创建：接口是 const 的，父类也是这样去掉 const
		UGinkgoCharacterMovementComponent* MutableThis = const_cast<UGinkgoCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FGinkgoNetworkPredictionData_Client(*this);
	}
	return ClientPredictionData;
}

void UGinkgoCharacterMovementComponent::MoveAutonomous(const float ClientTimeStamp, const float DeltaTime, const uint8 CompressedFlags, const FVector& NewAccel)
{
	// 服务器执行收到的移动、客户端重放存档时，父类调到这里之前都把那次移动的数据设成了当前的移动数据（重放时由存档填好）；
	// 输入从那次移动的数据里取，同压缩标志位。旧移动、双移动里的前一次也各带各的计数
	if (const FGinkgoNetworkMoveData* MoveData = static_cast<const FGinkgoNetworkMoveData*>(GetCurrentNetworkMoveData()))
	{
		DirectionPressCount = MoveData->DirectionPressCount;
		DirectionKeys = MoveData->DirectionKeys;
		HeldSpeedScale = MoveData->HeldSpeedScale;
		AdsSpeedScale = MoveData->AdsSpeedScale;
		if (auto* Character = Cast<AGinkgoCharacter>(CharacterOwner)) { Character->GetTraversal()->SetInput(MoveData->TraversalInput); }
	}
#if !UE_BUILD_SHIPPING
	AutonomousMoveTimeStamp = ClientTimeStamp;
#endif
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, NewAccel);
}

void UGinkgoCharacterMovementComponent::UpdateFromCompressedFlags(const uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

bool UGinkgoCharacterMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	// 重放时冲刺状态、方向键按下的计数与朝向来自各次移动的存档，重放完交还这一帧的实时输入，否则新的一次移动会带上上一帧的输入
	// （引擎对蹲与跳跃也是这样做的）
	const bool bRealWantsToSprint = bWantsToSprint;
	const uint8 RealDirectionPressCount = DirectionPressCount;
	const uint8 RealDirectionKeys = DirectionKeys;
	const uint8 RealHeldSpeedScale = HeldSpeedScale;
	const uint8 RealAdsSpeedScale = AdsSpeedScale;
	auto* Traversal = CastChecked<AGinkgoCharacter>(CharacterOwner)->GetTraversal();
	const auto RealTraversalInput = Traversal->GetInput();
	const bool bResult = Super::ClientUpdatePositionAfterServerUpdate();
	bWantsToSprint = bRealWantsToSprint;
	DirectionPressCount = RealDirectionPressCount;
	DirectionKeys = RealDirectionKeys;
	HeldSpeedScale = RealHeldSpeedScale;
	AdsSpeedScale = RealAdsSpeedScale;
	Traversal->SetInput(RealTraversalInput);
	Traversal->SyncPresentation();
	// 校正把胶囊从预测的位置放到服务器那时的位置，重放又把它推到现在：两步合起来挪了多少，累计给动画扣掉。
	// 这一帧的移动还没做，所以这里的位置就是"校正之后的上一帧末"
	if (CorrectionStartLocation.IsSet())
	{
		CorrectionOffsetTotal += UpdatedComponent->GetComponentLocation() - CorrectionStartLocation.GetValue();
		CorrectionStartLocation.Reset();
	}
	return bResult;
}

void UGinkgoCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, const float TimeStamp, const FVector NewLocation,
	const FVector NewVelocity, FMovementBaseInterfaceData* NewMovementBaseInterfaceData, const FName NewBaseBoneName, const bool bHasBase,
	const bool bBaseRelativePosition, const uint8 ServerMovementMode, const FVector ServerGravityDirection)
{
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewMovementBaseInterfaceData, NewBaseBoneName, bHasBase,
		bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);

	// 只在校正真正生效时才会走到这里（过期的校正被引擎丢掉时不会）。引擎在这之后才换位置、速度与移动模式，
	// 模拟状态等它全部换完，在 ClientHandleMoveResponse 里父类返回后再放回
	bServerSimStatePending = true;

	// 动画扣校正用：引擎换位置之前胶囊在哪里（还是预测的上一帧末）；重放完在 ClientUpdatePositionAfterServerUpdate 里算挪了多少
	if (!CorrectionStartLocation.IsSet())
	{
		CorrectionStartLocation = UpdatedComponent->GetComponentLocation();
	}

#if !UE_BUILD_SHIPPING
	const bool bClientWasCrouched = IsCrouching();
#endif
	// 胶囊则要先换成服务器在那次移动结束时的大小：服务器给的位置是那个胶囊的中心，引擎接下来放到这个位置、找地面、切移动模式
	// 都按现在的胶囊算。客户端此刻可能已经在那之后蹲下或站起，不换的话，蹲下的胶囊放在站立的中心，脚底悬空，直接下落
	SetCrouchedInPlace(MoveResponseData.bServerCrouched);

	// 与引擎的 p.NetShowCorrections 同一种算法：被校正的那次移动结束时客户端的位置，与服务器给的位置之差
	const FVector ClientLocation = ClientData.LastAckedMove.IsValid() ? ClientData.LastAckedMove->SavedLocation : UpdatedComponent->GetComponentLocation();
	const float PositionError = bBaseRelativePosition ? -1.f : static_cast<float>((ClientLocation - NewLocation).Size());
	OnMovementCorrected.Broadcast(PositionError);

#if !UE_BUILD_SHIPPING
	// 两端在这次移动结束时的移动模式与模拟状态也打出来。模式相同、位置差不到 √3 cm 的校正不是这次移动本身的误差：
	// 或是 p.NetForceClientAdjustmentPercent 强制的，或是之前一次校正的重放已经把这次移动改对了
	++CorrectionCount;
	const FVector ClientVelocity = ClientData.LastAckedMove.IsValid() ? ClientData.LastAckedMove->SavedVelocity : Velocity;
	const uint8 ClientMovementMode = ClientData.LastAckedMove.IsValid() ? ClientData.LastAckedMove->EndPackedMovementMode : PackNetworkMovementMode();
	const FString ClientSimState = ClientData.LastAckedMove.IsValid() ? static_cast<const FGinkgoSavedMove*>(ClientData.LastAckedMove.Get())->GetEndSimState().ToString() : TEXT("-");
#if GINKGO_MOVEMENT_TRACE_ENABLED
	FGinkgoMovementTrace::TraceCorrection(*this, PositionError);
#endif
	UE_LOG(LogGinkgoMovement, Log, TEXT("[Net] 第 %d 次校正：时刻 %.3f，位置差 %.2f cm，速度 客户端 %.1f / 服务器 %.1f cm/s，服务器移动模式 %d，客户端移动模式 %d；模拟状态 客户端 %s / 服务器 %s；胶囊 收到时客户端%s / 服务器%s"),
		CorrectionCount, TimeStamp, PositionError, ClientVelocity.Size(), NewVelocity.Size(), ServerMovementMode,
		ClientMovementMode, *ClientSimState, *MoveResponseData.SimState.ToString(), bClientWasCrouched ? TEXT("蹲着") : TEXT("站着"),
		MoveResponseData.bServerCrouched ? TEXT("蹲着") : TEXT("站着"));
#endif
}

void UGinkgoCharacterMovementComponent::SetCrouchedInPlace(const bool bCrouched)
{
	if (bCrouched == IsCrouching())
	{
		return;
	}
	// 父类的客户端模拟版本只换胶囊大小与网格的偏移，不移动位置、不检查有没有空间，也不改 bIsCrouched
	constexpr bool bClientSimulation = true;
	if (bCrouched)
	{
		Crouch(bClientSimulation);
	}
	else
	{
		UnCrouch(bClientSimulation);
	}
	CharacterOwner->SetIsCrouched(bCrouched);
}

void UGinkgoCharacterMovementComponent::ServerMoveHandleClientError(const float ClientTimeStamp, const float DeltaTime, const FVector& Accel,
	const FVector& RelativeClientLocation, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, const FName ClientBaseBoneName, const uint8 ClientMovementMode)
{
	const auto* Traversal = CastChecked<AGinkgoCharacter>(CharacterOwner)->GetTraversal();
	if (const auto* Move = static_cast<const FGinkgoNetworkMoveData*>(GetCurrentNetworkMoveData()); Move
		&& (Move->TraversalAction != Traversal->GetSyncState().ActionSequence || Move->TraversalPhase != Traversal->GetSyncState().Phase))
	{
		GetPredictionData_Server_Character()->bForceClientUpdate = true;
	}
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation, ClientMovementBaseInterfaceData, ClientBaseBoneName, ClientMovementMode);

	// 父类刚把这次移动记成待发的确认或校正时，记下此刻的模拟状态与是否蹲着。回包要到帧末才发，同一帧里之后的移动照样模拟，
	// 却不一定再检查：记下校正后，父类用 LastUpdateTime（float）与世界时间（double）比较是不是同一帧，几乎总不相等，
	// 之后的移动就被当成更新太频繁、跳过检查。帧末的状态因此可能已经走过了被校正的那次移动
	const FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character();
	if (ServerData && ServerData->PendingAdjustment.TimeStamp == ClientTimeStamp)
	{
		PendingAdjustmentSimState = SimState;
		PendingAdjustmentTraversal = Traversal->GetSyncState();
		bPendingAdjustmentCrouched = IsCrouching();
	}
}

void UGinkgoCharacterMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	{
		// 父类应用校正时先调 OnClientCorrectionReceived，再换位置、速度与移动模式；换模式会调到 OnMovementModeChanged，这期间不按落地、离地处理
		TGuardValue<bool> HandlingMoveResponse(bHandlingMoveResponse, true);
		Super::ClientHandleMoveResponse(MoveResponse);
	}
	if (bServerSimStatePending)
	{
		// 放回服务器在被校正那次移动之后的模拟状态，下一次移动前的重放从它开始。MoveResponseData 就是刚解析出这次校正的回包，校正总带着模拟状态
		bServerSimStatePending = false;
		RestoreSimState(MoveResponseData.SimState);
		CastChecked<AGinkgoCharacter>(CharacterOwner)->GetTraversal()->RestoreSyncState(MoveResponseData.Traversal);
	}
#if !UE_BUILD_SHIPPING
	if (MoveResponse.IsGoodMove() && MoveResponseData.bHasSimState)
	{
		VerifyAckedSimState();
	}
#endif
}

#if !UE_BUILD_SHIPPING
void UGinkgoCharacterMovementComponent::VerifyAckedSimState()
{
	// 父类处理好移动的确认时，已把 LastAckedMove 换成被确认的那次移动；找不到那次移动时不换，时刻对不上就不比
	const FNetworkPredictionData_Client_Character* ClientData = GetPredictionData_Client_Character();
	const FGinkgoSavedMove* AckedMove = ClientData ? static_cast<const FGinkgoSavedMove*>(ClientData->LastAckedMove.Get()) : nullptr;
	if (!AckedMove || AckedMove->TimeStamp != MoveResponseData.ClientAdjustment.TimeStamp)
	{
		return;
	}
	// 容差见 FGinkgoMovementSimState::NearlyEquals：零点几毫秒的落地时刻差是两端位置误差带来的，合并时多算的 Δt 至少一帧
	constexpr float TimeTolerance = 1e-3f;
	constexpr float SpeedTolerance = 1.f;
	++SimStateCheckCount;
	const FGinkgoMovementSimState& ClientState = AckedMove->GetEndSimState();
	const FGinkgoMovementSimState& ServerState = MoveResponseData.SimState;
	if (ClientState.bSliding == ServerState.bSliding)
	{
		MaxSimStateTimeDifference = FMath::Max(MaxSimStateTimeDifference, ClientState.MaxTimeDifference(ServerState));
	}
	const bool bClientCrouched = AckedMove->IsEndCrouched();
	const auto& ClientTraversal = AckedMove->GetEndTraversal();
	const auto& ServerTraversal = MoveResponseData.Traversal;
	if (bClientCrouched != MoveResponseData.bServerCrouched || !ClientState.NearlyEquals(ServerState, TimeTolerance, SpeedTolerance)
		|| !ClientTraversal.Matches(ServerTraversal))
	{
		++SimStateMismatchCount;
		UE_LOG(LogGinkgoMovement, Log, TEXT("[Net] 第 %d 次模拟状态不一致：时刻 %.3f；客户端 %s，%s；服务器 %s，%s"), SimStateMismatchCount, AckedMove->TimeStamp,
			*ClientState.ToString(), bClientCrouched ? TEXT("蹲着") : TEXT("站着"), *ServerState.ToString(), MoveResponseData.bServerCrouched ? TEXT("蹲着") : TEXT("站着"));
		UE_LOG(LogGinkgoMovement, Log, TEXT("[Net] Traversal client=%u/%u phase=%u time=%.6f server=%u/%u phase=%u time=%.6f"),
			ClientTraversal.HandledSequence, ClientTraversal.ActionSequence, ClientTraversal.Phase, ClientTraversal.Time,
			ServerTraversal.HandledSequence, ServerTraversal.ActionSequence, ServerTraversal.Phase, ServerTraversal.Time);
	}
}
#endif

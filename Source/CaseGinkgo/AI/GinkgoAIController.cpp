#include "AI/GinkgoAIController.h"

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAICharacterMovementComponent.h"
#include "AI/GinkgoAICombatComponent.h"
#include "AI/GinkgoAITacticsComponent.h"
#include "AI/GinkgoAIStateTree.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoPatrolRoute.h"
#include "AI/GinkgoNoiseEmitterComponent.h"
#include "AI/GinkgoPerceptionEvents.h"
#include "Framework/GinkgoEventHub.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoAI, Log, All);

namespace
{
	/** 非 Shipping 的可视化开关：状态、最后目击点、声音发生点与当前导航目标。 */
	TAutoConsoleVariable<int32> CVarAIDebug(TEXT("ginkgo.ai.debug"), 0, TEXT("Draw enemy state, memory and navigation (0/1)."), ECVF_Cheat);
}

AGinkgoAIController::AGinkgoAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	SetPerceptionComponent(*CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception")));
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
	SightConfig->SetMaxAge(1.f);
	GetPerceptionComponent()->ConfigureSense(*SightConfig);
	GetPerceptionComponent()->SetDominantSense(UAISense_Sight::StaticClass());
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("Hearing"));
	// Hearing 按声源 Actor 的 TeamId 过滤，而本项目阵营由 Controller 判断；先接收，再在回调统一复核。
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->SetMaxAge(1.f);
	GetPerceptionComponent()->ConfigureSense(*HearingConfig);
	GetPerceptionComponent()->OnTargetPerceptionUpdated.AddDynamic(this, &ThisClass::HandlePerception);
	Combat = CreateDefaultSubobject<UGinkgoAICombatComponent>(TEXT("Combat"));
	Tactics = CreateDefaultSubobject<UGinkgoAITacticsComponent>(TEXT("Tactics"));
	SetGenericTeamId(FGenericTeamId(1));
}

ETeamAttitude::Type AGinkgoAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
	if (const APawn* SensedPawn = Cast<APawn>(&Other))
	{
		if (SensedPawn->IsPlayerControlled()) return ETeamAttitude::Hostile;
		if (Cast<AGinkgoAICharacter>(SensedPawn)) return ETeamAttitude::Friendly;
	}
	return ETeamAttitude::Neutral;
}

void AGinkgoAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	AGinkgoAICharacter* ControlledCharacter = Cast<AGinkgoAICharacter>(InPawn);
	Config = ControlledCharacter ? ControlledCharacter->Definition.LoadSynchronous() : nullptr;
	if (!Config)
	{
		UE_LOG(LogGinkgoAI, Error, TEXT("%s 缺少已编译的 AI 状态树，请检查 Definition 或运行 GinkgoAIAssets。"), *GetNameSafe(InPawn));
		return;
	}
	// OnPossess 可早于 BeginPlay 的装备初始化；第一帧再按实际手持武器解析树。
	PatrolIndex = ControlledCharacter->PatrolRoute && !ControlledCharacter->PatrolRoute->Points.IsEmpty()
		? FMath::Clamp(ControlledCharacter->StartingPatrolPoint, 0, ControlledCharacter->PatrolRoute->Points.Num() - 1) : 0;
	SightConfig->SightRadius = Config->SightRadius;
	SightConfig->LoseSightRadius = Config->SightRadius + 200.f;
	SightConfig->PeripheralVisionAngleDegrees = Config->SightHalfAngle;
	GetPerceptionComponent()->ConfigureSense(*SightConfig);
	HearingConfig->HearingRange = Config->HearingRange;
	GetPerceptionComponent()->ConfigureSense(*HearingConfig);
	GetPerceptionComponent()->RequestStimuliListenerUpdate();
	Combat->Initialize(ControlledCharacter, Config);
	ControlledCharacter->GetCharacterMovement()->AddTickPrerequisiteActor(this);
	SightElapsed = Config->SightInterval;
	bActive = true;
}

void AGinkgoAIController::OnUnPossess()
{
	StopAI();
	Super::OnUnPossess();
}

void AGinkgoAIController::EndPlay(const EEndPlayReason::Type Reason)
{
	StopAI();
	Super::EndPlay(Reason);
}

void AGinkgoAIController::SetSensesEnabled(const bool bEnabled)
{
	UAIPerceptionComponent* Perception = GetPerceptionComponent();
	Perception->SetSenseEnabled(UAISense_Sight::StaticClass(), bEnabled);
	Perception->SetSenseEnabled(UAISense_Hearing::StaticClass(), bEnabled);
	Perception->ForgetAll();
	Perception->RequestStimuliListenerUpdate();
}

void AGinkgoAIController::ApplyFlash(const float Duration)
{
	auto* Enemy = Cast<AGinkgoAICharacter>(GetPawn());
	if (!bActive || !Enemy || Enemy->GetDamageable()->IsDead() || Duration <= 0.f) return;
	const bool bFirstHit = BlindUntil < 0.0;
	BlindUntil = FMath::Max(BlindUntil, GetWorld()->GetTimeSeconds() + Duration);
	if (!bFirstHit) return;

	Knowledge.Forget();
	TargetActor.Reset();
	bHasSearchGoal = false;
	StopTree();
	Combat->Stop();
	Enemy->GetCharacterMovement()->StopMovementImmediately();
	SetSensesEnabled(false);
	Enemy->bCoverEyes = true;
	Enemy->ForceNetUpdate();
}

void AGinkgoAIController::StopAI()
{
	bActive = false;
	if (BlindUntil >= 0.0)
	{
		BlindUntil = -1.0;
		SetSensesEnabled(true);
		if (auto* Enemy = Cast<AGinkgoAICharacter>(GetPawn())) Enemy->bCoverEyes = false;
	}
	HearingSince = -1.0;
	StopTree();
	Combat->Stop();
	if (AGinkgoAICharacter* ControlledCharacter = Cast<AGinkgoAICharacter>(GetPawn()))
	{
		ControlledCharacter->GetCharacterMovement()->RemoveTickPrerequisiteActor(this);
	}
	Knowledge.Forget();
	bHasSeenHostile = false;
	TargetActor.Reset();
	BehaviorWeapon.Reset();
	Config = nullptr;
	GetPerceptionComponent()->ForgetAll();
}

void AGinkgoAIController::StopTree()
{
	if (BehaviorTree && TreeData.Num() > 0)
	{
		FStateTreeExecutionContext Context(*this, *BehaviorTree, TreeData);
		Context.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateUObject(this, &ThisClass::CollectTreeData));
		Context.Stop();
	}
	EndBehavior();
	Tactics->Reset();
	TreeData.Reset();
	BehaviorTree = nullptr;
}

bool AGinkgoAIController::RefreshWeaponTree()
{
	const AGinkgoAICharacter* ControlledCharacter = Cast<AGinkgoAICharacter>(GetPawn());
	const UGinkgoWeaponDefinition* Weapon = ControlledCharacter ? ControlledCharacter->GetWeapons()->GetHeldWeapon() : nullptr;
	const UStateTree* Candidates[] = {
		ControlledCharacter ? ControlledCharacter->BehaviorOverrides.Reference.GetStateTree() : nullptr,
		Weapon ? Weapon->DefaultAIStateTree.Get() : nullptr,
		Config->StateTree.Get()
	};
	const UStateTree* Selected = nullptr;
	for (const UStateTree* Candidate : Candidates)
	{
		const auto* Schema = Candidate && Candidate->IsReadyToRun() ? Cast<UGinkgoAIStateTreeSchema>(Candidate->GetSchema()) : nullptr;
		if (!Schema) continue;
		// 若所有候选都要求交接，保留最高优先级的树，让它执行自己的目击后分支。
		if (!Selected) Selected = Candidate;
		if (!bHasSeenHostile || !Schema->bUseDefaultBehaviorAfterSight)
		{
			Selected = Candidate;
			break;
		}
	}
	if (!Selected) return false;
	if (BehaviorWeapon.Get() != Weapon)
	{
		Combat->ResetWeaponInput();
		BehaviorWeapon = Weapon;
	}
	if (Selected != BehaviorTree)
	{
		// 只停止决策任务，不调用 StopAI：视听记忆、反应起点与巡逻索引继续属于同一个敌人。
		StopTree();
		BehaviorTree = Selected;
	}
	return true;
}

void AGinkgoAIController::Tick(const float DeltaSeconds)
{
	AGinkgoAICharacter* ControlledCharacter = Cast<AGinkgoAICharacter>(GetPawn());
	if (!bActive || !ControlledCharacter || !ControlledCharacter->HasActorBegunPlay())
	{
		Super::Tick(DeltaSeconds);
		return;
	}
	if (ControlledCharacter->GetDamageable()->IsDead())
	{
		StopAI();
		return;
	}
	if (BlindUntil >= 0.0)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		if (Now < BlindUntil)
		{
			Super::Tick(DeltaSeconds);
			return;
		}
		HearingSince = Now;
		BlindUntil = -1.0;
		SetSensesEnabled(true);
		ControlledCharacter->bCoverEyes = false;
		ControlledCharacter->ForceNetUpdate();
		Combat->Initialize(ControlledCharacter, Config);
		SightElapsed = Config->SightInterval;
	}
	UpdateSight(DeltaSeconds);
	if (!RefreshWeaponTree() || !TickTree(DeltaSeconds))
	{
		UE_LOG(LogGinkgoAI, Error, TEXT("%s 的决策树执行失败，停止 AI，检查树的 schema 和任务。"), *GetNameSafe(GetPawn()));
		StopAI();
		return;
	}
	// AAIController::Tick 内部调用 UpdateControlRotation，一帧只转一次。
	Super::Tick(DeltaSeconds);
	const FVector Desired = bHasAimPoint ? (AimPoint - GetPawn()->GetPawnViewLocation()).GetSafeNormal() : FVector::ZeroVector;
	const bool bAligned = bHasAimPoint && FVector::DotProduct(GetControlRotation().Vector(), Desired)
		>= FMath::Cos(FMath::DegreesToRadians(Config->FireAimTolerance));
	Combat->TickCombat(DeltaSeconds, bAligned);
	DrawDebug();
}

void AGinkgoAIController::UpdateControlRotation(const float DeltaTime, const bool bUpdatePawn)
{
	if (bHasAimPoint && GetPawn() && Config)
	{
		const FRotator Desired = (AimPoint - GetPawn()->GetPawnViewLocation()).Rotation();
		SetControlRotation(FMath::RInterpConstantTo(GetControlRotation(), Desired, DeltaTime, Config->AimTurnSpeed));
	}
	else
	{
		Super::UpdateControlRotation(DeltaTime, bUpdatePawn);
	}
}

bool AGinkgoAIController::CanSeePoint(const FVector& Point, const AActor* Target) const
{
	return BlindUntil < 0.0 && Config && HasUnobstructedViewOf(Point, Config->SightRadius, Target);
}

bool AGinkgoAIController::HasUnobstructedViewOf(const FVector& Point, const float Radius, const AActor* Target) const
{
	if (!GetPawn() || !Config || Radius <= 0.f) return false;
	const FVector Eye = GetPawn()->GetPawnViewLocation();
	const FVector Offset = Point - Eye;
	if (Offset.SizeSquared() > FMath::Square(Radius)
		|| FVector::DotProduct(GetControlRotation().Vector(), Offset.GetSafeNormal()) < FMath::Cos(FMath::DegreesToRadians(Config->SightHalfAngle))) return false;
	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility,
		FCollisionQueryParams(SCENE_QUERY_STAT(GinkgoAISight), false, GetPawn()));
	return !bBlocked || (Target && Hit.GetActor() == Target);
}

void AGinkgoAIController::HandlePerception(AActor* Source, const FAIStimulus Stimulus)
{
	if (!bActive || BlindUntil >= 0.0 || GetWorld()->GetTimeSeconds() - Stimulus.GetAge() < HearingSince
		|| !Source || Source == GetPawn() || Stimulus.Type != UAISense::GetSenseID<UAISense_Hearing>()
		|| !Stimulus.WasSuccessfullySensed() || GetTeamAttitudeTowards(*Source) != ETeamAttitude::Hostile) return;
	// 刺激只给当时的世界位置；不把声音当目击，也不从 Source 读取持续移动的位置。
	if (Knowledge.Hear(Stimulus.StimulusLocation, GetWorld()->GetTimeSeconds() - Stimulus.GetAge())
		&& ActiveBehavior == EGinkgoAIBehavior::Investigate)
	{
		bHasSearchGoal = false;
	}
	if (Stimulus.Tag == UGinkgoNoiseEmitterComponent::GunshotTag)
	{
		if (auto* Hub = UGinkgoEventHub::Find(GetPawn()))
		{
			Hub->Publish({Ginkgo::Perception::GunshotHeard, Source, Stimulus.StimulusLocation});
		}
	}
}

void AGinkgoAIController::UpdateSight(const float DeltaSeconds)
{
	SightElapsed += DeltaSeconds;
	if (SightElapsed < Config->SightInterval) return;
	const float SampleTime = SightElapsed;
	SightElapsed = 0.f;
	TArray<AActor*> Candidates;
	GetPerceptionComponent()->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Candidates);
	AActor* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	FVector BestLocation = FVector::ZeroVector;
	FVector BestAim = FVector::ZeroVector;
	for (AActor* Candidate : Candidates)
	{
		const APawn* SensedPawn = Cast<APawn>(Candidate);
		if (!SensedPawn || !SensedPawn->IsPlayerControlled()) continue;
		const UGinkgoDamageableComponent* Damageable = Candidate->FindComponentByClass<UGinkgoDamageableComponent>();
		if (Damageable && Damageable->IsDead()) continue;
		// 候选来自当前感知，仍需确认躯干视线；只在此分支读取实时位置。
		const ACharacter* ControlledCharacter = Cast<ACharacter>(SensedPawn);
		const float HalfHeight = ControlledCharacter ? ControlledCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 80.f;
		const FVector Location = SensedPawn->GetActorLocation() - FVector(0.0, 0.0, HalfHeight);
		const FVector Aim = Location + FVector(0.0, 0.0, HalfHeight * 1.35f);
		if (!CanSeePoint(Aim, Candidate)) continue;
		const double Distance = Candidate == TargetActor.Get() ? -1.0 : FVector::DistSquared(GetPawn()->GetActorLocation(), Location);
		if (Distance < BestDistance)
		{
			Best = Candidate;
			BestLocation = Location;
			BestAim = Aim;
			BestDistance = Distance;
		}
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bWasVisible = Knowledge.bVisible;
	const auto PreviousTarget = TargetActor;
	const FVector PreviousLocation = Knowledge.LastLocation;
	if (Best)
	{
		if (Best != TargetActor.Get()) Knowledge.LoseSight();
		TargetActor = Best;
		Knowledge.Observe(BestLocation, BestAim, Now);
		bHasSeenHostile = true;
	}
	else
	{
		Knowledge.LoseSight();
		// 移动任务要先抵达目击点，再连续看清该处。原地警戒只能从岗点观察。
		if (Knowledge.bHasMemory && !Knowledge.bPositionCleared)
		{
			const bool bCanInspect = ActiveBehavior == EGinkgoAIBehavior::StandGuard || HasReached(Knowledge.LastLocation);
			Knowledge.ConfirmEmpty(bCanInspect && CanSeePoint(Knowledge.LastAimPoint), SampleTime, Config->ClearPositionDelay);
		}
		if (Knowledge.IsExpired(Now, Config->MemoryLifetime) || !TargetActor.IsValid())
		{
			Knowledge.ForgetSight();
			TargetActor.Reset();
		}
	}
	if (Knowledge.IsHearingExpired(Now, Config->HearingMemoryLifetime)) Knowledge.ForgetHearing();
	const bool bChangedTarget = Best != PreviousTarget.Get();
	if ((bWasVisible != Knowledge.bVisible || bChangedTarget) && (bWasVisible || Best))
	{
		if (auto* Hub = UGinkgoEventHub::Find(GetPawn()))
		{
			if (bWasVisible) { Hub->Publish({Ginkgo::Perception::TargetLost, PreviousTarget.Get(), PreviousLocation}); }
			if (Best) { Hub->Publish({Ginkgo::Perception::TargetSeen, Best, BestLocation}); }
		}
	}
}

bool AGinkgoAIController::CollectTreeData(const FStateTreeExecutionContext&, const UStateTree*,
	const TArrayView<const FStateTreeExternalDataDesc> Descs, const TArrayView<FStateTreeDataView> Views)
{
	for (int32 Index = 0; Index < Descs.Num(); ++Index)
	{
		if (Descs[Index].Struct && Descs[Index].Struct->IsChildOf(StaticClass())) Views[Index] = FStateTreeDataView(this);
		else if (Descs[Index].Requirement == EStateTreeExternalDataRequirement::Required) return false;
	}
	return true;
}

bool AGinkgoAIController::TickTree(const float DeltaSeconds)
{
	FStateTreeExecutionContext Context(*this, *BehaviorTree, TreeData);
	if (!Context.IsValid()) return false;
	Context.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateUObject(this, &ThisClass::CollectTreeData));
	if (Context.GetStateTreeRunStatus() != EStateTreeRunStatus::Running)
	{
		const auto* ControlledCharacter = CastChecked<AGinkgoAICharacter>(GetPawn());
		const auto* Reference = ControlledCharacter->BehaviorOverrides.GetParametersFor(BehaviorTree);
		if (Context.Start(Reference ? Reference->GetGlobalParameters() : FConstStructView()) != EStateTreeRunStatus::Running) return false;
	}
	return Context.Tick(DeltaSeconds) == EStateTreeRunStatus::Running;
}

void AGinkgoAIController::BeginBehavior(const EGinkgoAIBehavior Behavior, const FGinkgoAITactics& Settings)
{
	ActiveBehavior = Behavior;
	BehaviorElapsed = 0.f;
	WaitElapsed = 0.f;
	ProgressLocation = GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
	StalledSeconds = 0.f;
	NextMoveTime = 0.0;
	Tactics->Configure(Settings);
	if (Settings.Positioning != EGinkgoAIPositioning::SprintToRange
		&& (Behavior == EGinkgoAIBehavior::Engage || Behavior == EGinkgoAIBehavior::Pursue || Behavior == EGinkgoAIBehavior::Investigate))
	{
		// 树在旧任务 Tick 后切换。进入当帧就接住姿势与瞄点，丢失视野或声音更新线索时也能连续举枪。
		AimPoint = Behavior == EGinkgoAIBehavior::Investigate ? Knowledge.LastHeardLocation : Knowledge.LastAimPoint;
		bHasAimPoint = true;
		Combat->SetWeaponIntent(EGinkgoAIWeaponIntent::Aim);
	}
	bHasSearchGoal = false;
	if (AGinkgoAICharacter* ControlledCharacter = Cast<AGinkgoAICharacter>(GetPawn()))
	{
		if (UGinkgoAICharacterMovementComponent* Movement = Cast<UGinkgoAICharacterMovementComponent>(ControlledCharacter->GetCharacterMovement()))
		{
			float Speed = Config->CombatSpeed;
			if (Behavior == EGinkgoAIBehavior::Patrol) { Speed = Config->PatrolSpeed; }
			else if (Behavior == EGinkgoAIBehavior::Search) { Speed = Config->SearchSpeed; }
			else if (Settings.Positioning == EGinkgoAIPositioning::SprintToRange) { Speed = 0.f; }
			else if (Behavior == EGinkgoAIBehavior::Investigate) { Speed = Config->SearchSpeed; }
			Movement->SetBehaviorSpeed(Speed);
		}
	}
	UE_LOG(LogGinkgoAI, Log, TEXT("%s -> %s"), *GetNameSafe(GetPawn()), *UEnum::GetValueAsString(Behavior));
}

void AGinkgoAIController::EndBehavior()
{
	Combat->SetWeaponIntent(EGinkgoAIWeaponIntent::Lowered);
	if (auto* ControlledCharacter = Cast<AGinkgoAICharacter>(GetPawn())) { ControlledCharacter->SetWantsToSprint(false); }
	// 墙边姿势过渡短暂遮挡眼位时允许保留选点，不能因自己的探身使 Engage/Pursue 反复撤销姿势。
	const EGinkgoAIBehavior Next = Knowledge.GetBehavior();
	const bool bCombatTransition = bActive && (ActiveBehavior == EGinkgoAIBehavior::Engage || ActiveBehavior == EGinkgoAIBehavior::Pursue)
		&& (Next == EGinkgoAIBehavior::Engage || Next == EGinkgoAIBehavior::Pursue);
	if (!bCombatTransition) Tactics->Reset();
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	bHasMoveGoal = false;
	bMoveFailed = false;
	bHasAimPoint = false;
}

bool AGinkgoAIController::HasReached(const FVector& Point) const
{
	const ACharacter* ControlledCharacter = Cast<ACharacter>(GetPawn());
	if (!ControlledCharacter) return false;
	const FVector Feet = ControlledCharacter->GetActorLocation() - FVector(0.0, 0.0, ControlledCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	return FVector::DistSquared2D(Feet, Point) <= FMath::Square(Config->AcceptanceRadius + ControlledCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius())
		&& FMath::Abs(Feet.Z - Point.Z) < 100.0;
}

void AGinkgoAIController::MoveToPoint(const FVector& Point, const float Acceptance)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextMoveTime || (bHasMoveGoal && FVector::DistSquared(MoveGoal, Point) < FMath::Square(80.0)
		&& GetMoveStatus() == EPathFollowingStatus::Moving)) return;
	NextMoveTime = Now + Config->MoveRetryInterval;
	MoveGoal = Point;
	bHasMoveGoal = true;
	bMoveFailed = MoveToLocation(Point, Acceptance >= 0.f ? Acceptance : Config->AcceptanceRadius, Acceptance < 0.f,
		true, true, false, nullptr, false) == EPathFollowingRequestResult::Failed;
}

EStateTreeRunStatus AGinkgoAIController::TickBehavior(const EGinkgoAIBehavior Behavior, const float DeltaSeconds)
{
	if (!bActive || !GetPawn() || !Config) return EStateTreeRunStatus::Failed;
	BehaviorElapsed += DeltaSeconds;
	const double Now = GetWorld()->GetTimeSeconds();
	// 树在 Tick 后才评估转换，任务不能在这一帧使用已经失效的战斗事实。
	const EGinkgoAIBehavior PerceivedBehavior = Knowledge.GetBehavior();
	const bool bReadyToFire = (PerceivedBehavior == EGinkgoAIBehavior::Engage && Now - Knowledge.AcquiredTime >= Config->ReactionDelay)
		|| (PerceivedBehavior == EGinkgoAIBehavior::Pursue && Now - Knowledge.LastSeenTime < Config->SuppressionDuration);
	if (Behavior == EGinkgoAIBehavior::StandGuard)
	{
		// 只转向有效线索。警戒任务没有导航、巡逻进度或掩体查询，仍走正常的反应计时与武器输入。
		bHasAimPoint = Knowledge.bHasHearing || Knowledge.bHasMemory;
		AimPoint = Knowledge.bHasHearing ? Knowledge.LastHeardLocation : Knowledge.LastAimPoint;
		Combat->SetWeaponIntent(bReadyToFire ? EGinkgoAIWeaponIntent::Fire : EGinkgoAIWeaponIntent::Lowered);
		return EStateTreeRunStatus::Running;
	}
	if (PerceivedBehavior != Behavior)
	{
		Combat->SetWeaponIntent(EGinkgoAIWeaponIntent::Lowered);
		return EStateTreeRunStatus::Running;
	}
	if (Behavior == EGinkgoAIBehavior::Patrol)
	{
		const AGinkgoAICharacter* ControlledCharacter = Cast<AGinkgoAICharacter>(GetPawn());
		const AGinkgoPatrolRoute* Route = ControlledCharacter ? ControlledCharacter->PatrolRoute.Get() : nullptr;
		if (!Route || Route->Points.IsEmpty()) return EStateTreeRunStatus::Running;
		PatrolIndex %= Route->Points.Num();
		const FVector Point = Route->GetPointLocation(PatrolIndex);
		if (HasReached(Point))
		{
			StopMovement();
			WaitElapsed += DeltaSeconds;
			if (WaitElapsed >= Route->Points[PatrolIndex].WaitSeconds)
			{
				PatrolIndex = (PatrolIndex + 1) % Route->Points.Num();
				return EStateTreeRunStatus::Succeeded;
			}
		}
		else
		{
			WaitElapsed = 0.f;
			MoveToPoint(Point);
			StalledSeconds += DeltaSeconds;
			if (FVector::DistSquared2D(ProgressLocation, GetPawn()->GetActorLocation()) > FMath::Square(25.0))
			{
				ProgressLocation = GetPawn()->GetActorLocation();
				StalledSeconds = 0.f;
			}
			if (StalledSeconds > 12.f)
			{
				UE_LOG(LogGinkgoAI, Warning, TEXT("%s 路线点 %d 不可达，跳到下一点。"), *GetNameSafe(GetPawn()), PatrolIndex);
				PatrolIndex = (PatrolIndex + 1) % Route->Points.Num();
				return EStateTreeRunStatus::Succeeded;
			}
		}
		return EStateTreeRunStatus::Running;
	}
	AimPoint = Knowledge.bHasHearing ? Knowledge.LastHeardLocation : Knowledge.LastAimPoint;
	bHasAimPoint = true;
	const bool bRush = Tactics->GetPositioning() == EGinkgoAIPositioning::SprintToRange;
	if (bRush && (Behavior == EGinkgoAIBehavior::Engage || Behavior == EGinkgoAIBehavior::Pursue))
	{
		const bool bInSightAndRange = Tactics->TickSprintToRange();
		Combat->SetWeaponIntent(bInSightAndRange && bReadyToFire ? EGinkgoAIWeaponIntent::Fire : EGinkgoAIWeaponIntent::Lowered);
	}
	else if (Behavior == EGinkgoAIBehavior::Engage)
	{
		Combat->SetWeaponIntent(bReadyToFire ? EGinkgoAIWeaponIntent::Fire : EGinkgoAIWeaponIntent::Aim);
		Tactics->TickEngagement();
	}
	else if (Behavior == EGinkgoAIBehavior::Pursue)
	{
		Combat->SetWeaponIntent(bReadyToFire ? EGinkgoAIWeaponIntent::Fire : EGinkgoAIWeaponIntent::Aim);
		if (!Tactics->HoldCoverDuringSightLoss() && !HasReached(Knowledge.LastLocation)) MoveToPoint(Knowledge.LastLocation);
	}
	else if (Behavior == EGinkgoAIBehavior::Investigate)
	{
		// 声音可以更新前往的地点，但不能单独授权压制射击。
		Combat->SetWeaponIntent(bRush ? EGinkgoAIWeaponIntent::Lowered : EGinkgoAIWeaponIntent::Aim);
		AimPoint = Knowledge.LastHeardLocation;
		if (!bHasSearchGoal && Now >= NextMoveTime)
		{
			UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			FNavLocation Point;
			// 枪声可能发生在离地一米多的位置；只把导航目的地投影到地面，原始听觉位置保持不变。
			bHasSearchGoal = Navigation && Navigation->ProjectPointToNavigation(Knowledge.LastHeardLocation, Point, FVector(100.0, 100.0, 250.0));
			SearchGoal = Point.Location;
			if (!bHasSearchGoal) NextMoveTime = Now + Config->MoveRetryInterval;
		}
		if (bHasSearchGoal)
		{
			if (HasReached(SearchGoal))
			{
				StopMovement();
				Knowledge.FinishInvestigation();
				return EStateTreeRunStatus::Succeeded;
			}
			if (bRush) { CastChecked<AGinkgoAICharacter>(GetPawn())->SetWantsToSprint(true); }
			MoveToPoint(SearchGoal);
		}
	}
	else if (Behavior == EGinkgoAIBehavior::Search)
	{
		Combat->SetWeaponIntent(EGinkgoAIWeaponIntent::Lowered);
		if (BehaviorElapsed >= Config->SearchDuration)
		{
			Knowledge.Forget();
			TargetActor.Reset();
			return EStateTreeRunStatus::Succeeded;
		}
		if (!bHasSearchGoal && Now >= NextMoveTime)
		{
			FNavLocation Point;
			UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			FNavLocation Center;
			bHasSearchGoal = Navigation && Navigation->ProjectPointToNavigation(Knowledge.GetSearchLocation(), Center, FVector(100.0, 100.0, 250.0))
				&& Navigation->GetRandomReachablePointInRadius(Center.Location, Config->SearchRadius, Point);
			SearchGoal = Point.Location;
			WaitElapsed = 0.f;
			if (!bHasSearchGoal) NextMoveTime = Now + Config->MoveRetryInterval;
		}
		if (bHasSearchGoal)
		{
			if (HasReached(SearchGoal))
			{
				StopMovement();
				WaitElapsed += DeltaSeconds;
				const FVector Direction = FRotator(0.0, GetPawn()->GetActorRotation().Yaw + FMath::Sin(BehaviorElapsed) * 45.0, 0.0).Vector();
				AimPoint = GetPawn()->GetPawnViewLocation() + Direction * 500.0;
				if (WaitElapsed >= Config->SearchPause) bHasSearchGoal = false;
			}
			else
			{
				AimPoint = SearchGoal + FVector(0.0, 0.0, 120.0);
				MoveToPoint(SearchGoal);
				if (bMoveFailed) bHasSearchGoal = false;
			}
		}
	}
	return EStateTreeRunStatus::Running;
}

void AGinkgoAIController::DrawDebug() const
{
#if !UE_BUILD_SHIPPING
	if (CVarAIDebug.GetValueOnGameThread() == 0 || !GetPawn()) return;
	const FVector Origin = GetPawn()->GetActorLocation();
	DrawDebugString(GetWorld(), Origin + FVector(0.0, 0.0, 120.0), FString::Printf(TEXT("%s | %s | %s"),
		*UEnum::GetValueAsString(ActiveBehavior), *GetNameSafe(BehaviorTree), *UEnum::GetValueAsString(Tactics->GetPositioning())), nullptr, FColor::White, 0.f, true);
	if (Knowledge.bHasMemory)
	{
		const FColor Color = Knowledge.bVisible ? FColor::Red : FColor::Orange;
		DrawDebugSphere(GetWorld(), Knowledge.LastAimPoint, 16.f, 12, Color);
		DrawDebugLine(GetWorld(), GetPawn()->GetPawnViewLocation(), Knowledge.LastAimPoint, Color);
	}
	if (bHasMoveGoal) DrawDebugLine(GetWorld(), Origin, MoveGoal, FColor::Cyan);
	if (Knowledge.bHasHearing)
	{
		DrawDebugSphere(GetWorld(), Knowledge.LastHeardLocation, 20.f, 12, FColor::Yellow);
		DrawDebugLine(GetWorld(), GetPawn()->GetPawnViewLocation(), Knowledge.LastHeardLocation, FColor::Yellow);
	}
#endif
}

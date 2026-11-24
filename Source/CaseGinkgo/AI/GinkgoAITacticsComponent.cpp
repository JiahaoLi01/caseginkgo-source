#include "AI/GinkgoAITacticsComponent.h"

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoAIController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "NavigationSystem.h"

AGinkgoAIController* UGinkgoAITacticsComponent::GetAI() const
{
	return Cast<AGinkgoAIController>(GetOwner());
}

void UGinkgoAITacticsComponent::Configure(const FGinkgoAITactics& InSettings)
{
	if (Settings.Positioning != InSettings.Positioning) Reset();
	Settings = InSettings;
	bApproaching = true;
}

void UGinkgoAITacticsComponent::SetPeek(const float Amount)
{
	const AGinkgoAIController* AI = GetAI();
	if (AGinkgoAICharacter* Character = AI ? Cast<AGinkgoAICharacter>(AI->GetPawn()) : nullptr)
	{
		Character->SetTacticalPeek(Amount);
	}
}

void UGinkgoAITacticsComponent::Reset()
{
	const int32 OldQuery = QueryId;
	QueryId = INDEX_NONE;
	if (OldQuery != INDEX_NONE)
	{
		if (UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(GetWorld())) Manager->AbortQuery(OldQuery);
	}
	bHasCover = false;
	bApproaching = false;
	NextQueryTime = 0.0;
	SetPeek(0.f);
}

bool UGinkgoAITacticsComponent::EvaluateCover(const FVector& Ground, const FVector& Threat, FGinkgoAICoverPose& OutPose) const
{
	const AGinkgoAIController* AI = GetAI();
	if (!AI || !AI->GetPawn() || !GetWorld()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoAICover), false, AI->GetPawn());
	Params.AddIgnoredActor(AI->GetKnownTarget());
	const auto Blocked = [&](const FVector& From)
	{
		return GetWorld()->LineTraceTestByChannel(From, Threat, ECC_Visibility, Params);
	};
	if (!Blocked(Ground + FVector(0.0, 0.0, 65.0))) return false;
	OutPose.Ground = Ground;
	OutPose.Peek = 0.f;
	const FVector FiringPoint = Ground + FVector(0.0, 0.0, 145.0);
	// 保留现有低掩体。墙边和低掩体都只是合格候选，统一由 EQS 距离评分，不人为提高墙体优先级。
	if (!Blocked(FiringPoint)) return true;
	const FVector Right = FVector::CrossProduct(FVector::UpVector, (Threat - Ground).GetSafeNormal2D());
	for (const float Side : { -1.f, 1.f })
	{
		const FVector Offset = Right * Side * Settings.PeekReach;
		if (Offset.IsNearlyZero() || Blocked(FiringPoint + Offset) || Blocked(Ground + FVector(0.0, 0.0, 165.0) + Offset)) continue;
		FHitResult Sweep;
		// 横向探出过程也必须畅通，不能只因为终点能看见，就让头或枪横穿另一块墙。
		if (GetWorld()->SweepSingleByChannel(Sweep, FiringPoint, FiringPoint + Offset, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(4.f), Params)) continue;
		OutPose.Peek = Side;
		return true;
	}
	return false;
}

void UGinkgoAITacticsComponent::TickEngagement()
{
	AGinkgoAIController* AI = GetAI();
	const ACharacter* Character = AI ? Cast<ACharacter>(AI->GetPawn()) : nullptr;
	if (!Character || !AI->GetDefinition()) return;
	if (Settings.Positioning == EGinkgoAIPositioning::Approach)
	{
		SetPeek(0.f);
		const FVector Target = AI->GetKnowledge().LastLocation;
		const FVector Feet = Character->GetActorLocation() - FVector(0.0, 0.0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const double Distance = FVector::Dist2D(Feet, Target);
		const double Desired = FMath::Max(100.f, Settings.ApproachDistance);
		if (Distance <= Desired) bApproaching = false;
		else if (Distance > Desired + FMath::Max(25.f, Settings.ApproachHysteresis)) bApproaching = true;
		if (!bApproaching)
		{
			AI->StopMovement();
			return;
		}
		// 目的地停在目标之前，不导航进玩家胶囊；通过现有导航节流和模拟输入，保留 Walk/Run 的共用判断。
		const FVector Goal = Target + (Feet - Target).GetSafeNormal2D() * FMath::Max(0.0, Desired - 30.0);
		AI->MoveToPoint(Goal, 10.f);
		return;
	}
	RequestCover();
	ApplyCover();
}

bool UGinkgoAITacticsComponent::TickSprintToRange()
{
	auto* AI = GetAI();
	auto* Character = AI ? Cast<AGinkgoAICharacter>(AI->GetPawn()) : nullptr;
	if (!Character) { return false; }
	const FGinkgoAIKnowledge& Knowledge = AI->GetKnowledge();
	const double Range = Settings.ApproachDistance;
	const bool bCanEngage = Knowledge.bVisible && FVector::DistSquared(Character->GetCharacterMovement()->GetActorFeetLocation(), Knowledge.LastLocation) <= FMath::Square(Range);
	Character->SetWantsToSprint(!bCanEngage);
	if (bCanEngage)
	{
		if (AI->GetMoveStatus() != EPathFollowingStatus::Idle) { AI->StopMovement(); }
	}
	else
	{
		// 导航到冻结的目击位置；仅在有射界时才按射程停步，墙后也能继续找路。
		AI->MoveToPoint(Knowledge.LastLocation, 5.f);
	}
	return bCanEngage;
}

void UGinkgoAITacticsComponent::ApplyCover()
{
	AGinkgoAIController* AI = GetAI();
	if (!AI || !AI->GetPawn() || !bHasCover) { SetPeek(0.f); return; }
	const double Distance = FVector::Dist2D(AI->GetPawn()->GetActorLocation(), CoverPose.Ground);
	SetPeek(Distance < 90.0 ? CoverPose.Peek : 0.f);
	// 墙边有效射界可能只有十几厘米宽，不能沿用巡逻的胶囊半径 + 45 cm 到达容差。
	if (Distance > 10.0) AI->MoveToPoint(CoverPose.Ground, 5.f);
	else AI->StopMovement();
}

bool UGinkgoAITacticsComponent::HoldCoverDuringSightLoss()
{
	const AGinkgoAIController* AI = GetAI();
	FGinkgoAICoverPose Updated;
	if (!AI || !bHasCover || CoverPose.Peek == 0.f
		|| GetWorld()->GetTimeSeconds() - AI->GetKnowledge().LastSeenTime > 1.0
		|| !EvaluateCover(CoverPose.Ground, AI->GetKnowledge().LastAimPoint, Updated))
	{
		Reset();
		return false;
	}
	ApplyCover();
	return true;
}

void UGinkgoAITacticsComponent::RequestCover()
{
	AGinkgoAIController* AI = GetAI();
	const UGinkgoAIConfig* Config = AI ? AI->GetDefinition() : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Config || !Config->CoverQuery || QueryId != INDEX_NONE || Now < NextQueryTime) return;
	NextQueryTime = Now + Config->CoverQueryInterval;
	FGinkgoAICoverPose Updated;
	if (bHasCover && EvaluateCover(CoverPose.Ground, AI->GetKnowledge().LastAimPoint, Updated))
	{
		CoverPose = Updated;
		return;
	}
	if (bHasCover) AI->StopMovement();
	bHasCover = false;
	SetPeek(0.f);
	QueryThreat = AI->GetKnowledge().LastAimPoint;
	// Querier 的位置被网格生成器、距离评分与寻路测试共用。Controller 默认不随 Pawn 移动，必须传实际角色。
	QueryId = FEnvQueryRequest(Config->CoverQuery, AI->GetPawn()).Execute(EEnvQueryRunMode::SingleResult, this, &ThisClass::FinishCover);
}

void UGinkgoAITacticsComponent::FinishCover(TSharedPtr<FEnvQueryResult> Result)
{
	if (!Result.IsValid() || Result->QueryID != QueryId) return;
	QueryId = INDEX_NONE;
	const AGinkgoAIController* AI = GetAI();
	if (!AI || !AI->IsAIActive() || AI->GetActiveBehavior() != EGinkgoAIBehavior::Engage || !AI->GetKnowledge().bVisible
		|| Settings.Positioning != EGinkgoAIPositioning::Cover || !Result->IsSuccessful() || Result->Items.IsEmpty()
		|| FVector::DistSquared(QueryThreat, AI->GetKnowledge().LastAimPoint) > FMath::Square(300.0)) return;
	bHasCover = EvaluateCover(Result->GetItemAsLocation(0), AI->GetKnowledge().LastAimPoint, CoverPose);
}

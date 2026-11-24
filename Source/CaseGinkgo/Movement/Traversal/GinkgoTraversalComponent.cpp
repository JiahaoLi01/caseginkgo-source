#include "GinkgoTraversalComponent.h"

#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GinkgoTraversalDetection.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/GinkgoMovementConfig.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Utilities/Debug/DebugCategory.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShowTraversal(TEXT("ginkgo.showTraversal"), 0,
		TEXT("攀爬检测灰盒：0 关闭；1 每 0.1 秒实时检测；2 按跳跃键记录并冻结结果。\n")
		TEXT("方向是相机前向的 XY 投影，起点为角色胶囊。跳跃优先尝试攀爬，失败时普通跳跃；调试开关不改变动作逻辑。\n")
		TEXT("细线/小球是候选查询，白色折线是保守胶囊路径，橙/青为左右手，绿色大胶囊为目标站立体积。"));
}
#endif

UGinkgoTraversalComponent::UGinkgoTraversalComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

FGinkgoTraversalResult UGinkgoTraversalComponent::Evaluate(const FRotator& CameraRotation, TArray<FGinkgoTraversalProbe>* Probes) const
{
	const auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	const auto* Movement = Character ? Character->GetGinkgoMovement() : nullptr;
	const float Reach = Movement ? Movement->GetConfig().GetTraversalReach(Movement->Velocity.Size2D()) : 0.f;
	return EvaluateWithReach(CameraRotation, Reach, Probes);
}

FGinkgoTraversalResult UGinkgoTraversalComponent::EvaluateWithReach(const FRotator& CameraRotation, const float Reach, TArray<FGinkgoTraversalProbe>* Probes) const
{
	if (Probes) { Probes->Reset(); }
	FGinkgoTraversalResult Result;
	Result.Failure = EGinkgoTraversalFailure::InvalidState;
	const auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	const auto* Movement = Character ? Character->GetGinkgoMovement() : nullptr;
	if (!Character || !Movement || !GetWorld() || !Movement->IsMovingOnGround() || Character->GetWorkbench()
		|| Character->GetDamageable()->IsDead() || !Character->GetActorUpVector().Equals(FVector::UpVector, .001)) { return Result; }
	const auto* Capsule = Character->GetCapsuleComponent();
	FGinkgoTraversalQuery Query;
	Query.Owner = Character;
	Query.CurrentHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	Query.Feet = Capsule->GetComponentLocation() - FVector::UpVector * Query.CurrentHalfHeight;
	Query.Forward = Ginkgo::Traversal::CameraDirection(CameraRotation);
	Query.Radius = Capsule->GetScaledCapsuleRadius();
	Query.StandingHalfHeight = Character->GetClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() * Capsule->GetShapeScale();
	Query.StepHeight = Movement->MaxStepHeight;
	Query.WalkableFloorZ = Movement->GetWalkableFloorZ();
	Query.BodyChannel = Capsule->GetCollisionObjectType();
	Query.BodyResponses = Capsule->GetCollisionResponseToChannels();
	FGinkgoTraversalSettings Settings = Movement->GetConfig().Traversal;
	Settings.Reach = Reach;
	return Ginkgo::Traversal::Detect(*GetWorld(), Query, Settings, Probes);
}

void UGinkgoTraversalComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	TickAction();
#if !UE_BUILD_SHIPPING
	const auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	const auto* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Controller || !Controller->IsLocalController()) { return; }
	const int32 Mode = CVarShowTraversal.GetValueOnGameThread();
	if (Mode != LastDebugMode)
	{
		LastDebugMode = Mode;
		bHasDebugSample = false;
		DebugProbes.Reset();
		NextDebugTime = 0.;
	}
	if (Mode <= 0 || IsActive()) { return; }
	if (!IsActive() && Mode == 1 && GetWorld()->GetTimeSeconds() >= NextDebugTime)
	{
		FVector UnusedLocation;
		FRotator Rotation;
		Controller->GetPlayerViewPoint(UnusedLocation, Rotation);
		SampleDebug(Rotation);
		NextDebugTime = GetWorld()->GetTimeSeconds() + .1;
	}
	if (bHasDebugSample) { DrawDebug(); }
#endif
}

#if !UE_BUILD_SHIPPING
void UGinkgoTraversalComponent::PreviewJump(const FRotator& CameraRotation)
{
	const int32 Mode = CVarShowTraversal.GetValueOnGameThread();
	if (Mode <= 0) { return; }
	LastDebugMode = Mode;
	SampleDebug(CameraRotation);
	UE_LOG(LogGinkgoMovement, Log, TEXT("[Traversal preview] %s: %s; reach %.1f cm, height %.1f cm, depth %.1f cm, queries %d, %.3f ms"),
		Ginkgo::Traversal::Describe(DebugResult.Kind), Ginkgo::Traversal::Describe(DebugResult.Failure),
		DebugResult.Reach, DebugResult.Height, DebugResult.Depth, DebugResult.QueryCount, DebugMilliseconds);
}

void UGinkgoTraversalComponent::SampleDebug(const FRotator& Rotation)
{
	const double Started = FPlatformTime::Seconds();
	DebugResult = Evaluate(Rotation, &DebugProbes);
	DebugMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.;
	const auto* Character = Cast<AGinkgoCharacter>(GetOwner());
	DebugFeet = Character->GetCapsuleComponent()->GetComponentLocation()
		- FVector::UpVector * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	bHasDebugSample = true;
}

void UGinkgoTraversalComponent::DrawDebug() const
{
	const auto* World = GetWorld();
	for (const auto& Probe : DebugProbes)
	{
		const FColor Color = Probe.bHit ? FColor(170, 80, 70) : FColor(80, 150, 130);
		DrawDebugLine(World, Probe.Start, Probe.End, Color, false, -1.f, 0, .5f);
		if (Probe.HalfHeight > 0.f)
		{
			DrawDebugCapsule(World, Probe.End, Probe.HalfHeight, Probe.Radius, FQuat::Identity, Color, false, -1.f);
		}
		else if (Probe.Radius > 0.f) { DrawDebugSphere(World, Probe.End, Probe.Radius, 8, Color, false, -1.f); }
	}
	DrawDebugDirectionalArrow(World, DebugFeet + FVector::UpVector * 5.f,
		DebugFeet + FVector::UpVector * 5.f + DebugResult.Forward * 120.f, 12.f, FColor::Yellow, false, -1.f, 0, 2.f);
	const auto DrawContact = [World](const FGinkgoTraversalContact& Contact, const FColor& Color)
	{
		if (!Contact.Component.IsValid()) { return; }
		DrawDebugSphere(World, Contact.Position, 4.f, 10, Color, false, -1.f, 0, 2.f);
		DrawDebugDirectionalArrow(World, Contact.Position, Contact.Position + Contact.Normal * 18.f, 5.f, Color, false, -1.f);
	};
	DrawContact(DebugResult.Ledge, FColor::White);
	DrawContact(DebugResult.LeftHand, FColor::Orange);
	DrawContact(DebugResult.RightHand, FColor::Cyan);
	DrawContact(DebugResult.Landing, FColor::Green);
	for (int32 Index = 1; Index < DebugResult.Path.Num(); ++Index)
	{
		DrawDebugLine(World, DebugResult.Path[Index - 1], DebugResult.Path[Index], FColor::White, false, -1.f, 0, 3.f);
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.f, DebugResult.IsValid() ? FColor::Green : FColor::Orange,
			FString::Printf(TEXT("Traversal: %s | %s\nReach %.1f  Height %.1f  Depth %s%.1f cm | %d queries / %.3f ms | camera XY, Jump to traverse"),
				Ginkgo::Traversal::Describe(DebugResult.Kind), Ginkgo::Traversal::Describe(DebugResult.Failure),
				DebugResult.Reach, DebugResult.Height, DebugResult.bFoundBackEdge ? TEXT("") : TEXT(">="), DebugResult.Depth,
				DebugResult.QueryCount, DebugMilliseconds));
	}
}
#endif

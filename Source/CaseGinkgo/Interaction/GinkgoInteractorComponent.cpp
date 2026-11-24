#include "Interaction/GinkgoInteractorComponent.h"
#include "Interaction/GinkgoWorkbench.h"
#include "Weapon/GinkgoWeaponComponent.h"

#include "Camera/GinkgoPlayerCameraManager.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/GinkgoInteractionMath.h"
#include "Interaction/GinkgoInteractionSubsystem.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"

DECLARE_CYCLE_STAT(TEXT("Interaction Update"), STAT_GinkgoInteractionUpdate, STATGROUP_Ginkgo3C);

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarServerReject(
		TEXT("ginkgo.interaction.serverReject"),
		0,
		TEXT("服务器拒绝所有交互请求（回 Unavailable，不执行）：1 开，0 关。在服务器上设，测客户端被拒绝的那条路（像别人先拿走了）。"));
#endif

	/** ginkgo.interaction.serverReject：服务器拒绝所有请求（发行版里总是不拒绝） */
	bool IsServerRejectingAll()
	{
#if !UE_BUILD_SHIPPING
		return CVarServerReject.GetValueOnGameThread() != 0;
#else
		return false;
#endif
	}

	FString ResultName(const EGinkgoInteractionResult Result)
	{
		return StaticEnum<EGinkgoInteractionResult>()->GetNameStringByValue(static_cast<int64>(Result));
	}

	/** 从 From 看得见 To：Visibility 通道上没有挡住的，忽略角色自己与目标所在的 Actor */
	bool HasLineOfSight(const UWorld& World, const FVector& From, const FVector& To, const AActor* Character, const AActor* TargetActor)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoInteractionSight), false);
		Params.AddIgnoredActor(Character);
		Params.AddIgnoredActor(TargetActor);
		return !World.LineTraceTestByChannel(From, To, ECC_Visibility, Params);
	}
}

UGinkgoInteractorComponent::UGinkgoInteractorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 引擎在 TG_PostPhysics 之后、TG_PostUpdateWork 之前更新相机（LevelTick.cpp 的 "Update cameras last"）：焦点用这一帧最后的相机
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	// 只为 RPC：没有要复制的属性
	SetIsReplicatedByDefault(true);
}

const UGinkgoInteractorComponent::FCandidate* UGinkgoInteractorComponent::GetFocus() const
{
	return Candidates.IsValidIndex(FocusIndex) ? &Candidates[FocusIndex] : nullptr;
}

UObject* UGinkgoInteractorComponent::GetPendingTarget() const
{
	return PendingRequestId != 0 ? PendingTarget.Get() : nullptr;
}

void UGinkgoInteractorComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsLocallyControlled())
	{
		Candidates.Reset();
		FocusIndex = INDEX_NONE;
		return;
	}
	if (PendingRequestId != 0 && GetWorld()->GetTimeSeconds() - PendingSince > UGinkgoDeveloperSettings::Get()->InteractionRequestTimeout)
	{
		FinishRequest(PendingTarget.Get(), EGinkgoInteractionResult::TimedOut);
	}
	UpdateCandidates();
#if !UE_BUILD_SHIPPING
	DrawDebug();
#endif
}

// ===== 候选与焦点 =====

void UGinkgoInteractorComponent::UpdateCandidates()
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoInteractionUpdate);
	using namespace Ginkgo::Interaction;

	const AGinkgoCharacter* Character = GetGinkgoCharacter();
	const APlayerController* PlayerController = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	const UGinkgoInteractionSubsystem* Subsystem = UWorld::GetSubsystem<UGinkgoInteractionSubsystem>(GetWorld());
	if (!PlayerController || !PlayerController->PlayerCameraManager || !Subsystem)
	{
		Candidates.Reset();
		FocusIndex = INDEX_NONE;
		return;
	}
	if (AGinkgoWorkbench* Bench = Character->GetWorkbench())
	{
		Candidates.Reset();
		FocusIndex = INDEX_NONE;
		if (!Bench->IsExiting())
		{
			auto& Exit = Candidates.AddDefaulted_GetRef();
			Exit.Target = TWeakInterfacePtr<IGinkgoInteractable>(Bench);
			Exit.State = EGinkgoInteractionState::Ready;
			Exit.Text = Bench->GetInteractionText();
			Exit.bPromptVisible = true;
			FocusIndex = 0;
		}
		return;
	}
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	const UWorld& World = *GetWorld();
	// 焦点按叠加之前的视角选：相机的叠加层只晃画面（滑铲与冲刺落脚的震动、开镜的晃动），晃的时候焦点不跟着跳
	const AGinkgoPlayerCameraManager* GinkgoCamera = Cast<AGinkgoPlayerCameraManager>(PlayerController->PlayerCameraManager);
	const FMinimalViewInfo& View = GinkgoCamera ? GinkgoCamera->GetAimView() : PlayerController->PlayerCameraManager->GetCameraCacheView();
	const FVector ViewForward = View.Rotation.Vector();
	const FVector Eye = Character->GetPawnViewLocation();
	FVector Bottom, Top;
	GetCapsuleSegment(Bottom, Top);
	const FGinkgoInteractionContext Context = MakeContext();
	const UObject* PreviousFocus = FocusIndex != INDEX_NONE ? Candidates[FocusIndex].Target.GetObject() : nullptr;

	// 上一帧的候选留着做滞回：已经在里面的目标多放 InteractionRangeHysteresis 才算出去
	const TArray<FCandidate> Previous = MoveTemp(Candidates);
	Candidates.Reset();
	TArray<FFocusCandidate> FocusCandidates;
	for (const TWeakInterfacePtr<IGinkgoInteractable>& Entry : Subsystem->GetInteractables())
	{
		IGinkgoInteractable* Target = Entry.Get();
		if (!Target)
		{
			continue;
		}
		const UObject* TargetObject = Entry.GetObject();
		const FCandidate* Before = Previous.FindByPredicate([TargetObject](const FCandidate& Candidate) { return Candidate.Target.GetObject() == TargetObject; });
		const FVector Location = Target->GetInteractionLocation();
		const FGinkgoInteractionRanges Ranges = Target->GetInteractionRanges();
		const double Distance = DistanceToSegment(Location, Bottom, Top);
		if (!IsWithinRange(Distance, Ranges.Prompt, Before != nullptr, Settings->InteractionRangeHysteresis))
		{
			continue;
		}

		const AActor* TargetActor = GetInteractableActor(TargetObject);
		FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Target = Entry;
		Candidate.Distance = Distance;
		Candidate.ViewAngle = ViewAngleDegrees(View.Location, ViewForward, Location);
		// 提示圈要相机看得见；能不能交互总要角色的眼睛看得见，隔墙显示的目标也一样
		Candidate.bPromptVisible = Target->ShowsPromptThroughWalls() || HasLineOfSight(World, View.Location, Location, Character, TargetActor);
		// 交互距离可以量到胶囊中心（拾取的球）；提示距离与 Candidate.Distance 照旧量到中轴线段
		const double Interact = InteractDistance(Location, Bottom, Top, Ranges.bInteractFromCenter);
		Candidate.bInInteractRange = IsWithinRange(Interact, Ranges.Interact, Before && Before->bInInteractRange, Settings->InteractionRangeHysteresis);
		Candidate.bReachable = Candidate.bInInteractRange && HasLineOfSight(World, Eye, Location, Character, TargetActor);
		Candidate.Availability = Target->GetAvailability(Context, Candidate.Reason);
		if (Candidate.Availability == EGinkgoInteractionAvailability::Available)
		{
			Candidate.Availability = Target->GetLocalAvailability(Context, Candidate.Reason);
		}
		Candidate.Text = Target->GetInteractionText();
		const bool bShown = Candidate.bPromptVisible && Candidate.Availability != EGinkgoInteractionAvailability::Hidden;
		Candidate.State = bShown ? EGinkgoInteractionState::Hint : EGinkgoInteractionState::Hidden;
		// 屏幕上没有提示的不当焦点：看到哪个，交互的就是哪个
		FocusCandidates.Add({Candidate.ViewAngle, bShown && Candidate.bReachable, TargetObject == PreviousFocus});
	}

	FocusIndex = SelectFocus(FocusCandidates, Settings->InteractionMaxFocusAngle, Settings->InteractionFocusSwitchMargin);
	if (FocusIndex != INDEX_NONE)
	{
		FCandidate& Focus = Candidates[FocusIndex];
		Focus.State = Focus.Availability == EGinkgoInteractionAvailability::Available ? EGinkgoInteractionState::Ready : EGinkgoInteractionState::Blocked;
	}
}

// ===== 请求 =====

void UGinkgoInteractorComponent::RequestInteract()
{
	if (!IsLocallyControlled())
	{
		return;
	}
	if (auto* Bench = GetGinkgoCharacter()->GetWorkbench())
	{
		ServerLeaveWorkbench(Bench);
		return;
	}
	if (PendingRequestId != 0)
	{
		UE_LOG(LogGinkgoInteraction, Log, TEXT("[Interaction] 按下：还在等请求 %d 的结果，这次不发"), PendingRequestId);
		return;
	}
	const FCandidate* Focus = GetFocus();
	UObject* Target = Focus ? Focus->Target.GetObject() : nullptr;
	if (!Target)
	{
		UE_LOG(LogGinkgoInteraction, Log, TEXT("[Interaction] 按下：没有焦点"));
		return;
	}
	if (Focus->State != EGinkgoInteractionState::Ready)
	{
		UE_LOG(LogGinkgoInteraction, Log, TEXT("[Interaction] 按下：%s 不能交互（%s）"), *Focus->Text.ToString(), *Focus->Reason.ToString());
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	UE_LOG(LogGinkgoInteraction, Log, TEXT("[Interaction] 按下：%s（%s），距离 %.1f cm，离准星 %.1f°，世界时间 %.4f s"), *Focus->Text.ToString(), *Ginkgo::Interaction::DescribeInteractable(Target),
		Focus->Distance, Focus->ViewAngle, Now);

	// 本地先开始（要放动画的交互在这里伸手），不等服务器
	if (IGinkgoInteractable* Interactable = Focus->Target.Get())
	{
		Interactable->BeginLocalInteract(MakeContext());
	}

	// 单机与主机：本地就是服务器
	if (GetOwner()->HasAuthority())
	{
		double Distance = 0.0;
		FinishRequest(Target, ExecuteInteraction(Target, Distance, MakeContext().WeaponSlot));
		return;
	}
	LastRequestId = LastRequestId == MAX_uint8 ? 1 : LastRequestId + 1;
	PendingRequestId = LastRequestId;
	PendingTarget = Target;
	PendingSince = Now;
	PendingSincePlatform = FPlatformTime::Seconds();
	ServerInteract(Target, PendingRequestId, MakeContext().WeaponSlot);
}

void UGinkgoInteractorComponent::SelectWorkbenchSlot(int32 Slot)
{
	auto* Character = GetGinkgoCharacter();
	auto* Bench = Character ? Character->GetWorkbench() : nullptr;
	if (IsLocallyControlled() && Bench && Bench->CanSelectWeapon() && Bench->IsSlotAvailable(Slot))
	{
		ServerSelectWorkbenchSlot(Bench, Slot);
	}
}

void UGinkgoInteractorComponent::ServerSelectWorkbenchSlot_Implementation(AGinkgoWorkbench* Bench, int32 Slot)
{
	if (auto* Character = GetGinkgoCharacter(); Bench && Character && Character->GetWorkbench() == Bench)
	{
		Bench->SelectWeapon(*Character, Slot);
	}
}

void UGinkgoInteractorComponent::InstallWorkbenchAttachment(UGinkgoWeaponAttachment* Part)
{
	auto* Character = GetGinkgoCharacter();
	auto* Bench = Character ? Character->GetWorkbench() : nullptr;
	if (IsLocallyControlled() && Bench && Bench->CanInstallAttachment(Part))
	{
		ServerInstallWorkbenchAttachment(Bench, Bench->GetSelectedSlot(), Part);
	}
}

void UGinkgoInteractorComponent::ServerInstallWorkbenchAttachment_Implementation(AGinkgoWorkbench* Bench, int32 Slot, UGinkgoWeaponAttachment* Part)
{
	auto* Character = GetGinkgoCharacter();
	if (Character && Bench && Character->GetWorkbench() == Bench && Bench->GetSelectedSlot() == Slot)
	{
		Bench->InstallAttachment(*Character, Part);
	}
}

void UGinkgoInteractorComponent::ServerLeaveWorkbench_Implementation(AGinkgoWorkbench* Bench)
{
	if (auto* Character = GetGinkgoCharacter(); Bench && Character && Character->GetWorkbench() == Bench)
	{
		Bench->RequestExit(*Character);
	}
}

void UGinkgoInteractorComponent::ServerInteract_Implementation(UObject* Target, const uint8 RequestId, const int32 WeaponSlot)
{
	double Distance = 0.0;
	const EGinkgoInteractionResult Result = IsServerRejectingAll() ? EGinkgoInteractionResult::Unavailable : ExecuteInteraction(Target, Distance, WeaponSlot);
	UE_LOG(LogGinkgoInteraction, Log, TEXT("[Interaction] 服务器收到请求 %d：%s，服务器上的距离 %.1f cm，结果 %s"), RequestId, *Ginkgo::Interaction::DescribeInteractable(Target), Distance,
		*ResultName(Result));
	ClientInteractResult(RequestId, Result);
}

void UGinkgoInteractorComponent::ClientInteractResult_Implementation(const uint8 RequestId, const EGinkgoInteractionResult Result)
{
	if (RequestId != PendingRequestId)
	{
		UE_LOG(LogGinkgoInteraction, Log, TEXT("[Interaction] 请求 %d 的结果 %s 来得太晚，已经不等了"), RequestId, *ResultName(Result));
		return;
	}
	FinishRequest(PendingTarget.Get(), Result);
}

EGinkgoInteractionResult UGinkgoInteractorComponent::ExecuteInteraction(UObject* Target, double& OutDistance, const int32 WeaponSlot)
{
	using namespace Ginkgo::Interaction;
	OutDistance = -1.0;
	const AGinkgoCharacter* Character = GetGinkgoCharacter();
	if (!Character || !Character->GetController() || Character->GetDamageable()->IsDead())
	{
		return EGinkgoInteractionResult::NoCharacter;
	}
	const UGinkgoInteractionSubsystem* Subsystem = UWorld::GetSubsystem<UGinkgoInteractionSubsystem>(GetWorld());
	IGinkgoInteractable* Interactable = Cast<IGinkgoInteractable>(Target);
	if (!Interactable || !Subsystem || !Subsystem->IsRegistered(Target))
	{
		return EGinkgoInteractionResult::Gone;
	}
	FVector Bottom, Top;
	GetCapsuleSegment(Bottom, Top);
	const FGinkgoInteractionRanges Ranges = Interactable->GetInteractionRanges();
	OutDistance = InteractDistance(Interactable->GetInteractionLocation(), Bottom, Top, Ranges.bInteractFromCenter);
	// 服务器上角色的位置与客户端按下时不同：当作已经在交互距离以内，多放的是容差
	if (!IsWithinRange(OutDistance, Ranges.Interact, true, UGinkgoDeveloperSettings::Get()->InteractionServerTolerance))
	{
		return EGinkgoInteractionResult::OutOfRange;
	}
	// 视线服务器不再查：作弊不在范围内
	if (Character->GetWorkbench())
	{
		return EGinkgoInteractionResult::Unavailable;
	}
	FGinkgoInteractionContext Context = MakeContext();
	Context.WeaponSlot = WeaponSlot;
	FText Reason;
	if (Interactable->GetAvailability(Context, Reason) != EGinkgoInteractionAvailability::Available)
	{
		return EGinkgoInteractionResult::Unavailable;
	}
	Interactable->Interact(Context);
	return EGinkgoInteractionResult::Accepted;
}

void UGinkgoInteractorComponent::FinishRequest(UObject* Target, const EGinkgoInteractionResult Result)
{
	if (PendingRequestId != 0)
	{
		UE_LOG(LogGinkgoInteraction, Log, TEXT("[Interaction] 请求 %d 的结果 %s，等了 %.1f ms"), PendingRequestId, *ResultName(Result),
			(FPlatformTime::Seconds() - PendingSincePlatform) * 1000.0);
	}
	else
	{
		UE_LOG(LogGinkgoInteraction, Log, TEXT("[Interaction] 本地执行：%s"), *ResultName(Result));
	}
	PendingRequestId = 0;
	PendingTarget.Reset();
	LastResult = Result;
	LastResultTime = GetWorld()->GetTimeSeconds();
	// 目标收尾本地开始的东西；已经销毁的不用管
	if (IGinkgoInteractable* Interactable = Cast<IGinkgoInteractable>(Target))
	{
		Interactable->OnInteractResult(MakeContext(), Result);
	}
}

// ===== 角色 =====

AGinkgoCharacter* UGinkgoInteractorComponent::GetGinkgoCharacter() const
{
	return Cast<AGinkgoCharacter>(GetOwner());
}

bool UGinkgoInteractorComponent::IsLocallyControlled() const
{
	const AGinkgoCharacter* Character = GetGinkgoCharacter();
	return Character && Character->IsLocallyControlled();
}

FGinkgoInteractionContext UGinkgoInteractorComponent::MakeContext()
{
	auto* Character = GetGinkgoCharacter();
	return {Character, this, Character ? Character->GetWeapons()->GetHeldSlot() : INDEX_NONE};
}

void UGinkgoInteractorComponent::GetCapsuleSegment(FVector& OutBottom, FVector& OutTop) const
{
	const AGinkgoCharacter* Character = GetGinkgoCharacter();
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		OutBottom = OutTop = GetOwner()->GetActorLocation();
		return;
	}
	Ginkgo::Interaction::GetCapsuleSegment(Capsule->GetComponentLocation(), Capsule->GetUpVector(), Capsule->GetScaledCapsuleHalfHeight(),
		Capsule->GetScaledCapsuleRadius(), OutBottom, OutTop);
}

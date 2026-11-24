#include "Interaction/GinkgoPickup.h"

#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/GinkgoAnimInstance.h"     // FGinkgoHandReach：伸手
#include "Character/GinkgoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "HAL/IConsoleManager.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Utilities/Enumeration/Tags/PickupTags.h"
#include "Weapon/GinkgoWeaponComponent.h"

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarPickupAnimation(
		TEXT("ginkgo.pickup.anim"),
		1,
		TEXT("拾取放不放动画：1 放（够不着也放，Grab 时东西直接挂到手上），0 不放，按下就给（拾取子弹盒第 1 批的样子；只用来对照）。"));
#endif

	/** ginkgo.pickup.anim：放不放动画（发行版里总是放） */
	bool IsPickupAnimationEnabled()
	{
#if !UE_BUILD_SHIPPING
		return CVarPickupAnimation.GetValueOnGameThread() != 0;
#else
		return true;
#endif
	}

	const TCHAR* HandName(const EGinkgoHand Hand)
	{
		return Hand == EGinkgoHand::Left ? TEXT("左手") : TEXT("右手");
	}

	FName HandBone(const EGinkgoHand Hand)
	{
		static const FName Left(TEXT("hand_l"));
		static const FName Right(TEXT("hand_r"));
		return Hand == EGinkgoHand::Left ? Left : Right;
	}

	/** 拾取占的部位：gameplay 上占整条胳膊，不只是手 */
	FGameplayTag ArmOccupancy(const EGinkgoHand Hand)
	{
		return Hand == EGinkgoHand::Left ? FAnimActionTags::Occupancy_LeftArm : FAnimActionTags::Occupancy_RightArm;
	}

	const FName PickupActionName(TEXT("Pickup"));

	/** 最早开始、还没结束的拾取；没有返回空 */
	UGinkgoAnimAction* FindEarliestPickup(const UGinkgoAnimActionComponent& Actions)
	{
		const FGinkgoAnimActionRunner* Runner = Actions.GetRunner();
		if (!Runner)
		{
			return nullptr;
		}
		for (UGinkgoAnimAction* Action : Runner->GetActions())
		{
			if (Action->GetActionName() == PickupActionName && !Action->IsFinished())
			{
				return Action;
			}
		}
		return nullptr;
	}
}

// ===== 规则 =====

TOptional<EGinkgoHand> Ginkgo::Pickup::ChooseHand(const bool bLeftFree, const bool bRightFree, const FVector& LeftShoulder, const FVector& RightShoulder, const FVector& GraspPoint)
{
	if (!bLeftFree || !bRightFree)
	{
		return bLeftFree ? TOptional<EGinkgoHand>(EGinkgoHand::Left) : bRightFree ? TOptional<EGinkgoHand>(EGinkgoHand::Right) : TOptional<EGinkgoHand>();
	}
	return FVector::DistSquared(LeftShoulder, GraspPoint) < FVector::DistSquared(RightShoulder, GraspPoint) ? EGinkgoHand::Left : EGinkgoHand::Right;
}

FVector Ginkgo::Pickup::ReachCentre(const FVector& Shoulder, const FVector& CharacterForward, const EGinkgoHand Hand, const FVector& Offset)
{
	const FVector Forward = CharacterForward.GetSafeNormal2D();
	const FVector Inward = FVector::CrossProduct(FVector::UpVector, Forward) * (Hand == EGinkgoHand::Left ? 1.0 : -1.0);
	return Shoulder + Forward * Offset.X + Inward * Offset.Y + FVector::UpVector * Offset.Z;
}

double Ginkgo::Pickup::ArmReachRatio(const FVector& Centre, const FVector& GraspPoint, const FVector& CharacterForward, const EGinkgoHand Hand, const FVector& Radii,
	const double Margin)
{
	const FVector Forward = CharacterForward.GetSafeNormal2D();
	const FVector Inward = FVector::CrossProduct(FVector::UpVector, Forward) * (Hand == EGinkgoHand::Left ? 1.0 : -1.0);
	const FVector Offset = GraspPoint - Centre;
	const FVector Local(FVector::DotProduct(Offset, Forward), FVector::DotProduct(Offset, Inward), Offset.Z);
	const FVector Axes(FMath::Max(Radii.X + Margin, 1.0), FMath::Max(Radii.Y + Margin, 1.0), FMath::Max(Radii.Z + Margin, 1.0));
	return FMath::Sqrt(FMath::Square(Local.X / Axes.X) + FMath::Square(Local.Y / Axes.Y) + FMath::Square(Local.Z / Axes.Z));
}

bool Ginkgo::Pickup::IsWithinArmReach(const FVector& Centre, const FVector& GraspPoint, const FVector& CharacterForward, const EGinkgoHand Hand, const FVector& Radii,
	const double Margin)
{
	return ArmReachRatio(Centre, GraspPoint, CharacterForward, Hand, Radii, Margin) <= 1.0;
}

FVector Ginkgo::Pickup::WalkAwayRadii(const FVector& Radii, const double StartRatio)
{
	return Radii * FMath::Max(1.0, StartRatio);
}

FTransform Ginkgo::Pickup::SnapToHand(const FTransform& ItemToHand, const FVector& GraspPoint, const FVector& ItemInHand)
{
	FTransform Snapped = ItemToHand;
	Snapped.AddToTranslation(ItemInHand - ItemToHand.TransformPosition(GraspPoint));
	return Snapped;
}

bool Ginkgo::Pickup::FGrant::MarkDue()
{
	bDue = true;
	return TryGrant();
}

bool Ginkgo::Pickup::FGrant::MarkAccepted()
{
	bAccepted = true;
	return TryGrant();
}

void Ginkgo::Pickup::FGrant::MarkRejected()
{
	bRejected = true;
}

bool Ginkgo::Pickup::FGrant::TryGrant()
{
	if (!bDue || !bAccepted || bRejected || bGranted)
	{
		return false;
	}
	bGranted = true;
	return true;
}

// ===== 被交互组件 =====

UGinkgoPickupComponent::UGinkgoPickupComponent()
{
	ActionText = NSLOCTEXT("Ginkgo", "PickupAction", "拾取");
}

AGinkgoPickup* UGinkgoPickupComponent::GetPickup() const
{
	return Cast<AGinkgoPickup>(GetOwner());
}

FGinkgoInteractionRanges UGinkgoPickupComponent::GetInteractionRanges() const
{
	// 拾取的范围是以角色为中心的球：交互距离量到胶囊中心，半径在拾取上
	FGinkgoInteractionRanges Ranges = Super::GetInteractionRanges();
	if (const AGinkgoPickup* Pickup = GetPickup())
	{
		Ranges.Interact = Pickup->GetPickupRadius();
	}
	Ranges.bInteractFromCenter = true;
	return Ranges;
}

EGinkgoInteractionAvailability UGinkgoPickupComponent::GetAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
{
	const AGinkgoPickup* Pickup = GetPickup();
	return Pickup ? Pickup->GetAvailability(Context, OutReason) : EGinkgoInteractionAvailability::Hidden;
}

EGinkgoInteractionAvailability UGinkgoPickupComponent::GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
{
	const AGinkgoPickup* Pickup = GetPickup();
	return Pickup ? Pickup->GetLocalAvailability(Context, OutReason) : EGinkgoInteractionAvailability::Available;
}

void UGinkgoPickupComponent::Interact(const FGinkgoInteractionContext& Context)
{
	if (AGinkgoPickup* Pickup = GetPickup())
	{
		Pickup->Interact(Context);
	}
}

void UGinkgoPickupComponent::BeginLocalInteract(const FGinkgoInteractionContext& Context)
{
	if (AGinkgoPickup* Pickup = GetPickup())
	{
		Pickup->BeginLocalInteract(Context);
	}
}

void UGinkgoPickupComponent::OnInteractResult(const FGinkgoInteractionContext& Context, const EGinkgoInteractionResult Result)
{
	if (AGinkgoPickup* Pickup = GetPickup())
	{
		Pickup->OnInteractResult(Context, Result);
	}
}

// ===== 拾取 =====

AGinkgoPickup::AGinkgoPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// 只看不碰：交互的视线忽略目标自己的 Actor，东西也不挡人
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	RootComponent = Mesh;

	Interactable = CreateDefaultSubobject<UGinkgoPickupComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(Mesh);
}

void AGinkgoPickup::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// 提示圈画在手要抓的点上（子类在构造函数里定这个点）
	Interactable->SetRelativeLocation(GraspPoint);
}

void AGinkgoPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bClaimed);
}

FVector AGinkgoPickup::GetGraspLocation() const
{
	return GetActorTransform().TransformPosition(GraspPoint);
}

FVector AGinkgoPickup::GetReachCentre(const AGinkgoCharacter& Character, const EGinkgoHand Hand) const
{
	return Ginkgo::Pickup::ReachCentre(Character.GetShoulderLocation(Hand), Character.GetActorForwardVector(), Hand, ArmReachCentre);
}

FVector AGinkgoPickup::GetWalkAwayShoulder(const AGinkgoCharacter& Character) const
{
	return Character.GetActorTransform().TransformPosition(LocalShoulderInActor);
}

EGinkgoInteractionAvailability AGinkgoPickup::GetAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
{
	// 被占住了别人看不到；本地已经被接受的也不再提示：占住的复制可能比回复晚到一两帧
	if (bClaimed || (LocalPicker.IsValid() && LocalGrant.IsAccepted()))
	{
		return EGinkgoInteractionAvailability::Hidden;
	}
	const UGinkgoCharacterMovementComponent* Movement = Context.Character ? Context.Character->GetGinkgoMovement() : nullptr;
	if (!Movement)
	{
		return EGinkgoInteractionAvailability::Hidden;
	}
	if (Movement->IsFalling())
	{
		OutReason = NSLOCTEXT("Ginkgo", "PickupInAir", "在空中不能拾取");
		return EGinkgoInteractionAvailability::Blocked;
	}
	if (Movement->IsSliding())
	{
		OutReason = NSLOCTEXT("Ginkgo", "PickupSliding", "滑铲中不能拾取");
		return EGinkgoInteractionAvailability::Blocked;
	}
	return GetPickupAvailability(Context, OutReason);
}

EGinkgoInteractionAvailability AGinkgoPickup::GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
{
	const UGinkgoAnimActionComponent* Actions = Context.Character ? Context.Character->GetAnimActions() : nullptr;
	if (!Actions || !IsPickupAnimationEnabled() || Actions->CanOccupy(ArmOccupancy(EGinkgoHand::Left), GetPickupPriority())
		|| Actions->CanOccupy(ArmOccupancy(EGinkgoHand::Right), GetPickupPriority()) || FindEarliestPickup(*Actions))
	{
		return EGinkgoInteractionAvailability::Available;
	}
	OutReason = NSLOCTEXT("Ginkgo", "PickupHandsBusy", "手上没空");
	return EGinkgoInteractionAvailability::Blocked;
}

EGinkgoInteractionAvailability AGinkgoPickup::GetPickupAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
{
	return EGinkgoInteractionAvailability::Available;
}

void AGinkgoPickup::Interact(const FGinkgoInteractionContext& Context)
{
	bClaimed = true;
	ForceNetUpdate();
	// 拾取的人的动作放完再走：远端客户端的手可能还在伸
	SetLifeSpan(ClaimedLifeSpan);
	OnServerClaimed(Context);
	UpdateVisibility();
	UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 服务器：%s 被 %s 占住，%.1f s 后销毁，世界时间 %.4f s"), *GetName(), *GetNameSafe(Context.Character), ClaimedLifeSpan,
		GetWorld()->GetTimeSeconds());
}

void AGinkgoPickup::PrepareLocalInteract(AGinkgoCharacter& Character)
{
	LocalPicker = &Character;
	LocalAction.Reset();
	bLocalGrabbed = false;
	LocalGrant = Ginkgo::Pickup::FGrant();
}

bool AGinkgoPickup::IsHandAvailable(const UGinkgoAnimActionComponent& Actions, const EGinkgoHand Hand) const
{
	return Actions.CanOccupy(ArmOccupancy(Hand), GetPickupPriority()) || FindEarliestPickup(Actions);
}

void AGinkgoPickup::BeginLocalInteract(const FGinkgoInteractionContext& Context)
{
	if (AGinkgoCharacter* Character = Context.Character)
	{
		PrepareLocalInteract(*Character);
		StartLocalReach(*Character);
	}
}

void AGinkgoPickup::StartLocalReach(AGinkgoCharacter& InCharacter)
{
	AGinkgoCharacter* Character = &InCharacter;

	// 选手：能占的胳膊里近的那只（有的东西只能用一只手）。都占着时顶掉自己最早的那个拾取（中止即完成，东西不丢），腾出来的胳膊接着用，连按 E 不会被吃掉
	UGinkgoAnimActionComponent* Actions = Character->GetAnimActions();
	const FVector Grasp = GetGraspLocation();
	const TOptional<EGinkgoHand> Required = GetRequiredHand();
	const auto Choose = [this, Actions, Character, &Grasp, &Required]
	{
		const bool bLeftFree = (!Required.IsSet() || Required.GetValue() == EGinkgoHand::Left) && (!Actions || Actions->CanOccupy(ArmOccupancy(EGinkgoHand::Left), GetPickupPriority()));
		const bool bRightFree = (!Required.IsSet() || Required.GetValue() == EGinkgoHand::Right) && (!Actions || Actions->CanOccupy(ArmOccupancy(EGinkgoHand::Right), GetPickupPriority()));
		return Ginkgo::Pickup::ChooseHand(bLeftFree, bRightFree, Character->GetShoulderLocation(EGinkgoHand::Left), Character->GetShoulderLocation(EGinkgoHand::Right), Grasp);
	};
	TOptional<EGinkgoHand> Chosen = Choose();
	while (!Chosen.IsSet() && Actions)
	{
		UGinkgoAnimAction* Earliest = FindEarliestPickup(*Actions);
		if (!Earliest)
		{
			break;
		}
		Actions->Cancel(Earliest, FAnimActionTags::AnimAction_Cancel_Replaced);
		Chosen = Choose();
	}
	const EGinkgoHand Hand = Chosen.Get(Required.Get(EGinkgoHand::Left));

	// 看手够不够得着：够不着也放动画（同 TLOU2 的保底），手伸到最远，Grab 那一帧东西直接挂到手上
	const FVector Centre = GetReachCentre(*Character, Hand);
	const double ReachRatio = Ginkgo::Pickup::ArmReachRatio(Centre, Grasp, Character->GetActorForwardVector(), Hand, ArmReachRadii, 0.0);
	const bool bInReach = ReachRatio <= 1.0;
	bLocalBeyondReach = !bInReach;
	LocalShoulderInActor = Character->GetActorTransform().InverseTransformPosition(Character->GetShoulderLocation(Hand));
	const UGinkgoWeaponComponent* Weapons = Character->GetWeapons();
	UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 本地开始：%s，%s%s，肩关节到抓的点 %.1f cm，在够得着的椭球的 %.2f 倍处（%s），离胶囊中心 %.1f cm，世界时间 %.4f s"),
		*GetName(), HandName(Hand), Weapons && Weapons->GetHeldWeapon() ? TEXT("（拿着枪）") : TEXT(""), FVector::Dist(Character->GetShoulderLocation(Hand), Grasp), ReachRatio,
		bInReach ? TEXT("够得着") : TEXT("够不着"), FVector::Dist(Character->GetActorLocation(), Grasp), GetWorld()->GetTimeSeconds());

	UAnimMontage* Montage = GetMontage(Hand);
	UGinkgoAnimAction* Action = Montage && Actions && Chosen.IsSet() && IsPickupAnimationEnabled() ? MakePickupAction(*Character, Hand, *Montage, ReachRatio) : nullptr;
	if (!Action || !Actions->Start(Action))
	{
		// 不放动画：马上到了给的时刻，就是"中止即完成"那条路
		UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 不放动画（%s），直接完成"), !Montage ? TEXT("没有蒙太奇") : !Actions ? TEXT("没有执行器") : !Chosen.IsSet() || Action ? TEXT("手上没空") : TEXT("ginkgo.pickup.anim 0"));
		if (LocalGrant.MarkDue())
		{
			GrantNow();
		}
		return;
	}
	LocalAction = Action;
}

UAnimMontage* AGinkgoPickup::GetMontage(const EGinkgoHand Hand) const
{
	UAnimMontage* Own = Hand == EGinkgoHand::Left ? MontageLeft : MontageRight;
	if (Own)
	{
		return Own;
	}
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	const TSoftObjectPtr<UAnimMontage>& Default = Hand == EGinkgoHand::Left ? Settings->PickupMontageLeft : Settings->PickupMontageRight;
	return Default.IsNull() ? nullptr : Default.LoadSynchronous();
}

UGinkgoAnimAction* AGinkgoPickup::MakePickupAction(AGinkgoCharacter& Character, const EGinkgoHand Hand, UAnimMontage& Montage, const double StartReachRatio)
{
	const TWeakObjectPtr<AGinkgoPickup> WeakThis(this);
	const TWeakObjectPtr<AGinkgoCharacter> WeakCharacter(&Character);
	const TWeakObjectPtr<UAnimMontage> WeakMontage(&Montage);
	const double StartTime = GetWorld()->GetTimeSeconds();
	return UGinkgoAnimAction::Make(&Character, PickupActionName, [WeakThis, WeakCharacter, WeakMontage, Hand, StartTime, StartReachRatio](FGinkgoAnimActionSetup& Setup)
	{
		AGinkgoPickup* Self = WeakThis.Get();
		AGinkgoCharacter* Picker = WeakCharacter.Get();
		UAnimMontage* PickupMontage = WeakMontage.Get();
		if (!Self || !Picker || !PickupMontage)
		{
			// 不放蒙太奇：开始后马上完成，结局里照常给
			Setup.OnFinished([WeakThis, WeakCharacter, Hand](const FGinkgoAnimActionResult&)
			{
				if (AGinkgoPickup* Pickup = WeakThis.Get(); Pickup && WeakCharacter.IsValid())
				{
					Pickup->OnDue(*WeakCharacter.Get(), Hand, TEXT("东西或角色没了"));
				}
			});
			return;
		}
		Setup.Play(PickupMontage);

		// 伸手：东西上抓的点（Mesh 是根，它的坐标就是 Actor 的），片段的挂点与收纳处
		FGinkgoHandReach Reach;
		Reach.Montage = PickupMontage;
		Reach.Target = Self->Mesh;
		Reach.TargetPoint = Self->GraspPoint;
		Reach.ItemInHand = Hand == EGinkgoHand::Left ? Self->ItemInHandLeft : Self->ItemInHandRight;
		Reach.StowBone = Self->StowBone;
		Reach.PocketInChest = Hand == EGinkgoHand::Left ? Self->PocketInChestLeft : Self->PocketInChestRight;
		Reach.ArmLimit = Self->ArmLimit;
		Picker->SetHandReach(Hand, Reach);

		// 中止就是马上完成：手还没抓住时人走开了（东西出了够得着的椭球加余量；开始时就够不着的，比开始时远出余量）、保底超时、离地或滑铲
		const FVector Radii = Ginkgo::Pickup::WalkAwayRadii(Self->ArmReachRadii, StartReachRatio);
		const double Margin = Self->ArmReachMargin;
		const double MaxDuration = Self->MaxDuration;
		if (Self->AbortsWhenInterrupted())
		{
			Setup.AbortIf(NAME_None, [WeakThis, WeakCharacter, Hand, Radii, Margin](const FGinkgoAnimActionContext&)
			{
				const AGinkgoPickup* Pickup = WeakThis.Get();
				const AGinkgoCharacter* Character = WeakCharacter.Get();
				if (!Pickup || !Character || Pickup->bLocalGrabbed)
				{
					return false;
				}
				const FVector Forward = Character->GetActorForwardVector();
				const FVector Centre = Ginkgo::Pickup::ReachCentre(Pickup->GetWalkAwayShoulder(*Character), Forward, Hand, Pickup->ArmReachCentre);
				return !Ginkgo::Pickup::IsWithinArmReach(Centre, Pickup->GetGraspLocation(), Forward, Hand, Radii, Margin);
			}, FPickupTags::AnimAction_Abort_OutOfReach);
		}
		Setup.AbortIf(NAME_None, [WeakCharacter, StartTime, MaxDuration](const FGinkgoAnimActionContext&)
		{
			const AGinkgoCharacter* Character = WeakCharacter.Get();
			return Character && Character->GetWorld()->GetTimeSeconds() - StartTime > MaxDuration;
		}, FPickupTags::AnimAction_Abort_Timeout);
		if (Self->AbortsWhenInterrupted())
		{
			Setup.AbortIf(NAME_None, [WeakCharacter](const FGinkgoAnimActionContext&)
			{
				const AGinkgoCharacter* Character = WeakCharacter.Get();
				const UGinkgoCharacterMovementComponent* Movement = Character ? Character->GetGinkgoMovement() : nullptr;
				return Movement && (Movement->IsFalling() || Movement->IsSliding());
			}, FPickupTags::AnimAction_Abort_Posture);
		}

		Setup.On(Ginkgo::PickupEvents::Grab, [WeakThis, WeakCharacter, Hand](FGinkgoAnimActionEvent& Event)
		{
			if (AGinkgoPickup* Pickup = WeakThis.Get(); Pickup && WeakCharacter.IsValid() && !Event.bCaughtUp)
			{
				Pickup->OnGrab(*WeakCharacter.Get(), Hand);
			}
		}, EGinkgoMissedEvent::Skip);
		Setup.On(Ginkgo::PickupEvents::Stow, [WeakThis, WeakCharacter, Hand](FGinkgoAnimActionEvent& Event)
		{
			if (AGinkgoPickup* Pickup = WeakThis.Get(); Pickup && WeakCharacter.IsValid())
			{
				Pickup->OnDue(*WeakCharacter.Get(), Hand, Event.bCaughtUp ? TEXT("Stow（补做）") : TEXT("Stow"));
			}
		}, EGinkgoMissedEvent::CatchUp);

		// 结局：手里的马上清掉（完成时 Stow 已经清了）；被拒绝的不给，别的结局都保证给过（Stow 补做时已经给了，这里只是兜底）
		Setup.OnFinished([WeakThis, WeakCharacter, Hand](const FGinkgoAnimActionResult& Result)
		{
			AGinkgoPickup* Pickup = WeakThis.Get();
			AGinkgoCharacter* Character = WeakCharacter.Get();
			if (Character)
			{
				Character->HideHandItem(Hand);
			}
			UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 动作结局：%s，原因 %s，世界时间 %.4f s"),
				*StaticEnum<EGinkgoAnimActionOutcome>()->GetNameStringByValue(static_cast<int64>(Result.Outcome)), *Result.Reason.ToString(),
				Character ? Character->GetWorld()->GetTimeSeconds() : -1.0);
			if (Pickup && Character && Result.Reason != FPickupTags::AnimAction_Cancel_Rejected)
			{
				Pickup->OnDue(*Character, Hand, TEXT("结局"));
			}
		});
	})->Occupy(ArmOccupancy(Hand), GetPickupPriority());
}

void AGinkgoPickup::OnGrab(AGinkgoCharacter& Character, const EGinkgoHand Hand)
{
	// 朝向照东西此刻相对手骨的，位置挪到东西上抓的点落在手里的挂点上：够得着时手的 IK 已经把挂点拉到了东西上，挂上去不跳；
	// 够不着时手伸到了最远，东西在这一帧直接挂到手上（同 TLOU2 的保底）
	const FTransform Bone = Character.GetMesh()->GetSocketTransform(HandBone(Hand));
	const FVector ItemInHand = Hand == EGinkgoHand::Left ? ItemInHandLeft : ItemInHandRight;
	const FTransform InHand = Ginkgo::Pickup::SnapToHand(Mesh->GetComponentTransform().GetRelativeTransform(Bone), GraspPoint, ItemInHand);
	const double Gap = FVector::Dist(Bone.TransformPosition(ItemInHand), GetGraspLocation());
	Character.ShowHandItem(Hand, HandMesh, InHand);
	// 东西已经在手里：伸手不再跟着它在世界里的那一点（跑得快时手被拖向身后），冻在这一刻相对身体的位置
	Character.FreezeHandReach(Hand);
	bLocalGrabbed = true;
	UpdateVisibility();
	UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 抓住：%s，%s的挂点离抓的点 %.2f cm%s，世界时间 %.4f s"), *GetName(), HandName(Hand), Gap,
		bLocalBeyondReach ? TEXT("（够不着，东西直接挂到手上）") : TEXT(""), GetWorld()->GetTimeSeconds());
	if (GrantsAtGrab())
	{
		OnDue(Character, Hand, TEXT("Grab"));
	}
}

void AGinkgoPickup::OnDue(AGinkgoCharacter& Character, const EGinkgoHand Hand, const TCHAR* Why)
{
	Character.HideHandItem(Hand);
	if (LocalPicker.Get() != &Character)
	{
		return;
	}
	UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 到了给的时刻：%s，%s，世界时间 %.4f s"), *GetName(), Why, GetWorld()->GetTimeSeconds());
	if (LocalGrant.MarkDue())
	{
		GrantNow();
	}
}

void AGinkgoPickup::OnInteractResult(const FGinkgoInteractionContext& Context, const EGinkgoInteractionResult Result)
{
	if (!LocalPicker.IsValid() || LocalPicker.Get() != Context.Character)
	{
		return;
	}
	if (Result == EGinkgoInteractionResult::Accepted)
	{
		if (LocalGrant.MarkAccepted())
		{
			GrantNow();
		}
		return;
	}
	// 被拒绝、等不到：不给，取消动作（手里的清掉），地上的显示回来（被别人占住的照样藏着）。
	// 先放开本地的拾取再取消：取消时补做的 Stow 只清手里的，不再算"到了给的时刻"
	LocalGrant.MarkRejected();
	UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 本地：%s 没拿到（%s），不给"), *GetName(), *StaticEnum<EGinkgoInteractionResult>()->GetNameStringByValue(static_cast<int64>(Result)));
	UGinkgoAnimAction* Action = LocalAction.Get();
	LocalPicker.Reset();
	LocalAction.Reset();
	bLocalGrabbed = false;
	if (Action && Context.Character->GetAnimActions())
	{
		Context.Character->GetAnimActions()->Cancel(Action, FPickupTags::AnimAction_Cancel_Rejected);
	}
	UpdateVisibility();
}

void AGinkgoPickup::GrantNow()
{
	AGinkgoCharacter* Character = LocalPicker.Get();
	if (!Character)
	{
		return;
	}
	UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 本地给效果：%s 给 %s，世界时间 %.4f s"), *GetName(), *Character->GetName(), GetWorld()->GetTimeSeconds());
	GrantLocal(*Character);
	UpdateVisibility();
}

void AGinkgoPickup::OnRep_Claimed()
{
	UpdateVisibility();
}

void AGinkgoPickup::UpdateVisibility()
{
	// 拾取的人：手碰到它、或者给了效果就藏（它还没被销毁）；别人：被占住就藏
	const bool bPickingHere = LocalPicker.IsValid();
	const bool bHideMesh = bLocalGrabbed || LocalGrant.IsGranted() || (bClaimed && !bPickingHere);
	if (bHideMesh != static_cast<bool>(Mesh->bHiddenInGame))
	{
		UE_LOG(LogGinkgoInteraction, Log, TEXT("[Pickup] 地上的 %s：%s，世界时间 %.4f s"), *GetName(), bHideMesh ? TEXT("藏起") : TEXT("显示"), GetWorld()->GetTimeSeconds());
	}
	Mesh->SetHiddenInGame(bHideMesh, true);
}

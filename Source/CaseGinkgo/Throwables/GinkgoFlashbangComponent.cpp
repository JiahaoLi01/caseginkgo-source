#include "Throwables/GinkgoFlashbangComponent.h"
#include "Throwables/GinkgoFlashProjectile.h"
#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "AudioDevice.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/AudioComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "MetasoundOperatorCacheSubsystem.h"
#include "MetasoundSource.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Weapon/GinkgoWeaponComponent.h"

namespace
{
	// 两个 30 fps 片段中，抓住在取出第 4 帧，脱手在投掷第 9 帧（首帧为 1）。
	constexpr float GrabTime = 3.f / 30.f;
	constexpr float ReleaseTime = 8.f / 30.f;
}

UGinkgoFlashbangComponent::UGinkgoFlashbangComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Draw(TEXT("/Game/Weapons/Throwables/Anims/M_M84_Prepare"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Toss(TEXT("/Game/Weapons/Throwables/Anims/M_M84_Throw"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PropDraw(TEXT("/Game/Weapons/Throwables/Anims/A_M84_Prepare"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> PropThrow(TEXT("/Game/Weapons/Throwables/Anims/A_M84_Throw"));
	static ConstructorHelpers::FObjectFinder<USoundBase> TossSound(TEXT("/Game/Weapons/Throwables/Audio/MS_Flashbang_Throw"));
	DrawArm = Draw.Object;
	ThrowArm = Toss.Object;
	DrawItem = PropDraw.Object;
	ThrowItem = PropThrow.Object;
	ThrowSound = TossSound.Object;
}

void UGinkgoFlashbangComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer) return;
	const FAudioDeviceHandle Device = GetWorld() ? GetWorld()->GetAudioDevice() : FAudioDeviceHandle();
	UMetaSoundCacheSubsystem* Cache = Device.IsValid() ? Device->GetSubsystem<UMetaSoundCacheSubsystem>() : nullptr;
	if (!Cache) return;
	const auto* ProjectileDefaults = GetDefault<AGinkgoFlashProjectile>();
	for (USoundBase* Sound : {ThrowSound.Get(), ProjectileDefaults->GetImpactSound(), ProjectileDefaults->GetExplodeSound()})
	{
		if (UMetaSoundSource* MetaSound = Cast<UMetaSoundSource>(Sound))
		{
			Cache->PrecacheMetaSound(MetaSound);
		}
	}
}

AGinkgoCharacter* UGinkgoFlashbangComponent::Character() const { return CastChecked<AGinkgoCharacter>(GetOwner()); }

FTransform UGinkgoFlashbangComponent::BeltSocket()
{
	return FTransform(FQuat(.67105513, .19967544, .67822097, -.22322869).GetNormalized(), FVector(3.320819, -2.708278, -16.358255));
}

FTransform UGinkgoFlashbangComponent::GripSocket()
{
	return FTransform(FQuat(-.70315971, .70770420, .00943040, .06806072).GetNormalized(), FVector(4.53563, -5.64140, 7.22978));
}

double UGinkgoFlashbangComponent::Now() const
{
	const auto* Clock = GetWorld()->GetGameState();
	return Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

bool UGinkgoFlashbangComponent::HasItem() const
{
	return State.Phase != EGinkgoFlashPhase::Empty && State.Phase != EGinkgoFlashPhase::FollowThrough;
}

bool UGinkgoFlashbangComponent::IsUsingArm() const
{
	return State.Phase != EGinkgoFlashPhase::Empty && State.Phase != EGinkgoFlashPhase::Belt;
}

float UGinkgoFlashbangComponent::GetBeltIK() const
{
	if (State.Phase != EGinkgoFlashPhase::Draw) return 0.f;
	const float Distance = FMath::Abs(static_cast<float>(Now() - State.Start) - GrabTime);
	return 1.f - FMath::SmoothStep(0.f, GrabTime, Distance);
}

float UGinkgoFlashbangComponent::GetReleaseIK(FVector& OutWorldPoint) const
{
	if (State.Phase != EGinkgoFlashPhase::Throw && State.Phase != EGinkgoFlashPhase::FollowThrough) return 0.f;
	const float Age = FMath::Max(0.0, Now() - State.Start);
	const float Distance = State.Phase == EGinkgoFlashPhase::Throw ? FMath::Abs(Age - ReleaseTime) : Age;
	const float Weight = 1.f - FMath::SmoothStep(0.f, .1f, Distance);
	if (Weight > 0.f)
	{
		FVector Velocity;
		AGinkgoFlashProjectile::GetLaunch(*Character(), OutWorldPoint, Velocity);
	}
	return Weight;
}

void UGinkgoFlashbangComponent::Press()
{
	if (!bLocalPicking && State.Phase == EGinkgoFlashPhase::Belt) ServerPress();
}

void UGinkgoFlashbangComponent::Release()
{
	ServerRelease(Character()->GetControlRotation().Vector());
}

void UGinkgoFlashbangComponent::ServerPress_Implementation()
{
	if (State.Phase != EGinkgoFlashPhase::Belt || !Character()->GetController() || Character()->GetWorkbench()
		|| Character()->GetDamageable()->IsDead() || !DrawArm || !ThrowArm || !ClaimArm()) return;
	bReleaseQueued = false;
	ReleasedAim = FVector::ZeroVector;
	SetPhase(EGinkgoFlashPhase::Draw);
}

void UGinkgoFlashbangComponent::ServerRelease_Implementation(FVector_NetQuantizeNormal AimDirection)
{
	if (State.Phase != EGinkgoFlashPhase::Draw && State.Phase != EGinkgoFlashPhase::Hold) return;
	if (bReleaseQueued) return;
	ReleasedAim = AimDirection.ContainsNaN() ? FVector::ZeroVector : AimDirection.GetSafeNormal();
	if (State.Phase == EGinkgoFlashPhase::Draw) bReleaseQueued = true;
	else if (State.Phase == EGinkgoFlashPhase::Hold) SetPhase(EGinkgoFlashPhase::Throw);
}

bool UGinkgoFlashbangComponent::ClaimArm()
{
	if (ArmAction) return true;
	auto* Actions = Character()->GetAnimActions();
	auto* Action = UGinkgoAnimAction::Make(this, TEXT("FlashThrow"), [](FGinkgoAnimActionSetup& Setup) { Setup.HoldFor(MAX_flt); });
	Action->Occupy(FAnimActionTags::Occupancy_LeftArm, Ginkgo::Occupancy::PriorityThrow);
	if (!Actions->Start(Action, [Weak = TWeakObjectPtr(this)](UGinkgoAnimAction& Finished, const FGinkgoAnimActionResult&)
	{
		if (auto* Self = Weak.Get(); Self && Self->ArmAction == &Finished)
		{
			Self->ArmAction = nullptr;
			Self->CancelThrow();
		}
	})) return false;
	ArmAction = Action;
	Character()->GetWeapons()->CancelReloadForThrow();
	return true;
}

void UGinkgoFlashbangComponent::DropArm()
{
	if (auto* Action = ArmAction.Get())
	{
		ArmAction = nullptr;
		Character()->GetAnimActions()->Cancel(Action, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	}
}

void UGinkgoFlashbangComponent::CancelThrow()
{
	if (GetOwner()->HasAuthority()) ServerCancel_Implementation();
	else if (Character()->IsLocallyControlled()) ServerCancel();
}

void UGinkgoFlashbangComponent::ServerCancel_Implementation()
{
	if (IsUsingArm()) RestoreInventory(HasItem());
}

void UGinkgoFlashbangComponent::RestoreInventory(bool bHasItem)
{
	if (!GetOwner()->HasAuthority()) return;
	bReleaseQueued = false;
	SetPhase(bHasItem ? EGinkgoFlashPhase::Belt : EGinkgoFlashPhase::Empty);
}

void UGinkgoFlashbangComponent::SetLocalPickup(bool bPicking)
{
	bLocalPicking = bPicking;
	if (bPicking) Character()->GetWeapons()->CancelReloadForThrow();
	UpdateItem(FMath::Max(0.0, Now() - State.Start));
}

void UGinkgoFlashbangComponent::SetPhase(EGinkgoFlashPhase Phase)
{
	State.Phase = Phase;
	State.Start = Now();
	OnRep_State();
	GetOwner()->ForceNetUpdate();
}

void UGinkgoFlashbangComponent::PlayPhase(bool bThrow, float Position, bool bPause)
{
	UAnimInstance* Anim = Character()->GetMesh()->GetAnimInstance();
	auto* Sequence = bThrow ? ThrowArm.Get() : DrawArm.Get();
	if (!Anim || !Sequence) return;
	const bool bSameClip = Montage && ((bThrow && Presented == EGinkgoFlashPhase::Throw)
		|| (!bThrow && Presented == EGinkgoFlashPhase::Draw));
	if (!bSameClip)
	{
		Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(Sequence, TEXT("ArmL"), bPause ? 0.f : .06f, .08f);
		if (!Montage) return;
		Montage->bEnableAutoBlendOut = bThrow;
		Anim->Montage_Play(Montage, 1.f, EMontagePlayReturnType::MontageLength, Position);
	}
	else Anim->Montage_SetPosition(Montage, Position);
	if (bPause) Anim->Montage_Pause(Montage);
}

void UGinkgoFlashbangComponent::OnRep_State()
{
	const float Age = FMath::Max(0.0, Now() - State.Start);
	if (IsUsingArm() && !GetOwner()->HasAuthority() && Character()->IsLocallyControlled() && !ClaimArm())
	{
		ServerCancel();
	}
	if (!IsUsingArm()) DropArm();
	if (Guide && State.Phase != EGinkgoFlashPhase::Hold) Guide->Flush();
	if (GetNetMode() != NM_DedicatedServer)
	{
		if (State.Phase == EGinkgoFlashPhase::Draw) PlayPhase(false, Age, false);
		else if (State.Phase == EGinkgoFlashPhase::Hold && DrawArm) PlayPhase(false, DrawArm->GetPlayLength() - KINDA_SMALL_NUMBER, true);
		else if (State.Phase == EGinkgoFlashPhase::Throw) PlayPhase(true, Age, false);
		else if (State.Phase == EGinkgoFlashPhase::FollowThrough && Presented != EGinkgoFlashPhase::Throw) PlayPhase(true, ReleaseTime + Age, false);
		else if (!IsUsingArm() && Montage)
		{
			if (auto* Anim = Character()->GetMesh()->GetAnimInstance()) Anim->Montage_Stop(.08f, Montage);
			Montage = nullptr;
		}
		if (State.Phase == EGinkgoFlashPhase::Throw && Presented != EGinkgoFlashPhase::Throw)
		{
			if (ThrowAudio && ThrowAudio->IsPlaying()) ThrowAudio->FadeOut(.04f, 0.f);
			ThrowAudio = (ThrowSound && Age < .15f)
				? UGameplayStatics::SpawnSoundAttached(ThrowSound, Character()->GetMesh(), TEXT("hand_l"), FVector::ZeroVector,
					EAttachLocation::SnapToTarget, false)
				: nullptr;
		}
		else if (Presented == EGinkgoFlashPhase::Throw && State.Phase != EGinkgoFlashPhase::Throw && State.Phase != EGinkgoFlashPhase::FollowThrough)
		{
			if (ThrowAudio && ThrowAudio->IsPlaying()) ThrowAudio->FadeOut(.06f, 0.f);
			ThrowAudio = nullptr;
		}
		UpdateItem(Age);
	}
	Presented = State.Phase;
	GuideElapsed = 1.f;
	SetComponentTickEnabled(IsUsingArm());
}

void UGinkgoFlashbangComponent::UpdateItem(float Age)
{
	if (GetNetMode() == NM_DedicatedServer) return;
	if (State.Phase == EGinkgoFlashPhase::Empty || bLocalPicking)
	{
		if (Item) Item->SetVisibility(false);
		return;
	}
	if (!Item)
	{
		Item = NewObject<USkeletalMeshComponent>(GetOwner());
		Item->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Item->SetCanEverAffectNavigation(false);
		Item->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Weapons/Throwables/SK_M84.SK_M84")));
		Item->SetupAttachment(Character()->GetMesh(), TEXT("pelvis"));
		Item->SetRelativeTransform(BeltSocket());
		Item->RegisterComponent();
	}
	const bool bOnBelt = State.Phase == EGinkgoFlashPhase::Belt || (State.Phase == EGinkgoFlashPhase::Draw && Age < GrabTime);
	const FName Bone = bOnBelt ? TEXT("pelvis") : TEXT("hand_l");
	if (Item->GetAttachSocketName() != Bone || Presented != State.Phase || !Item->IsVisible())
	{
		Item->AttachToComponent(Character()->GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, Bone);
		Item->SetRelativeTransform(bOnBelt ? BeltSocket() : GripSocket());
	}
	Item->SetVisibility(true);
	const bool bThrow = State.Phase == EGinkgoFlashPhase::Throw || State.Phase == EGinkgoFlashPhase::FollowThrough;
	auto* Clip = bThrow ? ThrowItem.Get() : DrawItem.Get();
	if (!Clip) return;
	if (!Item->GetSingleNodeInstance() || Item->GetSingleNodeInstance()->GetCurrentAsset() != Clip)
	{
		Item->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Item->SetAnimation(Clip);
		Item->Stop();
	}
	const float Time = bOnBelt ? 0.f : State.Phase == EGinkgoFlashPhase::Hold ? Clip->GetPlayLength()
		: State.Phase == EGinkgoFlashPhase::FollowThrough ? ReleaseTime + Age : Age;
	Item->SetPosition(FMath::Min(Time, Clip->GetPlayLength()), false);
}

void UGinkgoFlashbangComponent::Launch()
{
	FVector Start, Velocity;
	AGinkgoFlashProjectile::GetLaunch(*Character(), Start, Velocity, ReleasedAim);
	const FTransform Transform(Velocity.Rotation(), Start);
	auto* Projectile = GetWorld()->SpawnActorDeferred<AGinkgoFlashProjectile>(
		AGinkgoFlashProjectile::StaticClass(), Transform, GetOwner(), Character(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Projectile) { RestoreInventory(true); return; }
	Projectile->LaunchVelocity = Velocity;
	Projectile->FinishSpawning(Transform);
	SetPhase(EGinkgoFlashPhase::FollowThrough);
}

void UGinkgoFlashbangComponent::UpdateGuide()
{
	if (!Guide)
	{
		Guide = NewObject<ULineBatchComponent>(GetOwner());
		Guide->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Guide->SetCanEverAffectNavigation(false);
		Guide->RegisterComponent();
	}
	FVector Start, Velocity;
	AGinkgoFlashProjectile::GetLaunch(*Character(), Start, Velocity);
	TArray<FVector> Path;
	AGinkgoFlashProjectile::PredictPath(*GetWorld(), Character(), Start, Velocity, Path);
	TArray<FBatchedLine, TInlineAllocator<96>> Lines;
	Lines.Reserve(Path.Num() - 1);
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		Lines.Emplace(Path[Index - 1], Path[Index], FLinearColor(.9f, .87f, .78f), 0.f, 1.4f, SDPG_World);
	}
	Guide->Flush();
	Guide->DrawLines(Lines);
	Guide->DrawPoint(Path.Last(), FLinearColor::White, 5.f, SDPG_World);
}

void UGinkgoFlashbangComponent::TickComponent(float DeltaSeconds, ELevelTick Type, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaSeconds, Type, TickFunction);
	const float Age = FMath::Max(0.0, Now() - State.Start);
	if (GetOwner()->HasAuthority())
	{
		if (!Character()->GetController() || Character()->GetDamageable()->IsDead() || Character()->GetWorkbench()) { CancelThrow(); return; }
		if (State.Phase == EGinkgoFlashPhase::Draw && Age >= DrawArm->GetPlayLength())
		{
			SetPhase(bReleaseQueued ? EGinkgoFlashPhase::Throw : EGinkgoFlashPhase::Hold);
			return;
		}
		if (State.Phase == EGinkgoFlashPhase::Throw && Age >= ReleaseTime) { Launch(); return; }
		if (State.Phase == EGinkgoFlashPhase::FollowThrough && Age >= ThrowArm->GetPlayLength() - ReleaseTime) { SetPhase(EGinkgoFlashPhase::Empty); return; }
	}
	if (State.Phase != EGinkgoFlashPhase::Hold) UpdateItem(Age);
	else if (Character()->IsLocallyControlled() && (GuideElapsed += DeltaSeconds) >= 1.f / 30.f)
	{
		GuideElapsed = 0.f;
		UpdateGuide();
	}
}

void UGinkgoFlashbangComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	DropArm();
	if (ThrowAudio)
	{
		ThrowAudio->Stop();
		ThrowAudio = nullptr;
	}
	if (Guide) Guide->DestroyComponent();
	if (Item) Item->DestroyComponent();
	Super::EndPlay(Reason);
}

void UGinkgoFlashbangComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, State);
}

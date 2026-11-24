#include "Interaction/GinkgoWorkbench.h"

#include "Animation/Actions/GinkgoAnimActionChain.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/GinkgoInteractionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "UI/GinkgoUiSounds.h"
#include "UObject/ConstructorHelpers.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Weapon/GinkgoInstallAttachmentAction.h"
#include "Weapon/GinkgoMagazineAttachmentAction.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace
{
	/** 放枪的归一化接触进度（0–1）；走到此处时枪从手挂到桌面，双手开始离开。 */
	constexpr float WorkbenchContactProgress = 0.76f;

	/** 拨台灯动作中手指拨过开关卡点的归一化时刻（0–1）；跨过此处时切换台灯状态并触发点击音。 */
	constexpr float LampSwitchContactProgress = 0.46f;

	/** 拨杆开关在局部空间绕此单位轴在关（负角）与开（正角）之间摆动，与导出网格转轴一致。 */
	const FVector LampSwitchHingeAxis(0.4068, -0.85153, -0.33077);

	float Ease(const float Value)
	{
		return FMath::SmoothStep(0.f, 1.f, Value);
	}

	/**
	 * 非公度三谐波手持漂移噪声（范围约 [-1, 1]），频率无周期重复，模拟摄影师呼吸与轻微手持晃动。
	 * 与 UGinkgoCameraNoiseSway 沿用相同思路，解析按连续时钟求值，与帧率无关。
	 */
	double SampleHandheldNoise(const double Time, const double BaseHz, const double Phase)
	{
		static constexpr double Ratios[3] = {1.0, 1.73, 2.81};
		static constexpr double Weights[3] = {0.58, 0.28, 0.14};
		double Sum = 0.0;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Sum += Weights[Index] * FMath::Sin(UE_DOUBLE_TWO_PI * BaseHz * Ratios[Index] * Time + Phase * (1.0 + 0.61 * Index));
		}
		return Sum;
	}
}

AGinkgoWorkbench::AGinkgoWorkbench()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> BenchAsset(TEXT("/Game/Interaction/Workbench/SM_Workbench.SM_Workbench"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> LampAsset(TEXT("/Game/Interaction/Workbench/SM_WorkbenchLamp.SM_WorkbenchLamp"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> SwitchAsset(TEXT("/Game/Interaction/Workbench/SM_WorkbenchLampSwitch.SM_WorkbenchLampSwitch"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CubeFallback(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> LampOnAsset(TEXT("/Game/Interaction/Workbench/MS_Workbench_LampOn.MS_Workbench_LampOn"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> LampOffAsset(TEXT("/Game/Interaction/Workbench/MS_Workbench_LampOff.MS_Workbench_LampOff"));

	WorkbenchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WorkbenchMesh"));
	WorkbenchMesh->SetupAttachment(RootComponent);
	WorkbenchMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	if (BenchAsset.Succeeded())
	{
		WorkbenchMesh->SetStaticMesh(BenchAsset.Get());
	}
	else if (CubeFallback.Succeeded())
	{
		WorkbenchMesh->SetStaticMesh(CubeFallback.Get());
		WorkbenchMesh->SetRelativeLocation(FVector(13.0, 0.0, 45.2));
		WorkbenchMesh->SetRelativeScale3D(FVector(1.06, 2.24, 0.904));
	}

	LampMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LampMesh"));
	LampMesh->SetupAttachment(RootComponent);
	LampMesh->SetRelativeLocation(FVector(20.0, -40.0, 90.0));
	LampMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LampMesh->SetCanEverAffectNavigation(false);
	LampMesh->SetCastShadow(false);
	if (LampAsset.Succeeded())
	{
		LampMesh->SetStaticMesh(LampAsset.Get());
	}

	LampSwitchMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LampSwitchMesh"));
	LampSwitchMesh->SetupAttachment(RootComponent);
	LampSwitchMesh->SetRelativeLocation(FVector(-17.726, -16.919, 138.354));
	LampSwitchMesh->SetRelativeRotation(FQuat(LampSwitchHingeAxis.GetSafeNormal(), FMath::DegreesToRadians(-15.0)).Rotator());
	LampSwitchMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LampSwitchMesh->SetCanEverAffectNavigation(false);
	LampSwitchMesh->SetCastShadow(false);
	if (SwitchAsset.Succeeded())
	{
		LampSwitchMesh->SetStaticMesh(SwitchAsset.Get());
	}

	LampSwitchTarget = CreateDefaultSubobject<USceneComponent>(TEXT("LampSwitchTarget"));
	LampSwitchTarget->SetupAttachment(RootComponent);
	LampSwitchTarget->SetRelativeLocation(FVector(-19.714, -17.860, 138.331));

	const FVector BulbLocation(-14.645, -12.241, 130.099);
	const FVector AimTarget(-20.0, 0.0, 92.0);
	LampLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("LampLight"));
	LampLight->SetupAttachment(RootComponent);
	LampLight->SetMobility(EComponentMobility::Movable);
	LampLight->SetRelativeLocationAndRotation(BulbLocation, (AimTarget - BulbLocation).Rotation());
	LampLight->SetIntensityUnits(ELightUnits::Candelas);
	LampLight->bUseTemperature = true;
	LampLight->SetTemperature(4300.f);
	LampLight->SetInnerConeAngle(44.f);
	LampLight->SetOuterConeAngle(72.f);
	LampLight->SetAttenuationRadius(265.f);
	LampLight->SetSourceRadius(4.2f);
	LampLight->SetSourceLength(7.5f);
	LampLight->SetSoftSourceRadius(2.5f);
	LampLight->SetCastShadows(true);
	LampLight->SetIntensity(0.f);
	LampLight->SetVisibility(false);

	LampFillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("LampFillLight"));
	LampFillLight->SetupAttachment(RootComponent);
	LampFillLight->SetMobility(EComponentMobility::Movable);
	LampFillLight->SetRelativeLocation(FVector(-48.0, 10.0, 118.0));
	LampFillLight->SetIntensityUnits(ELightUnits::Candelas);
	LampFillLight->bUseTemperature = true;
	LampFillLight->SetTemperature(4500.f);
	LampFillLight->SetAttenuationRadius(175.f);
	LampFillLight->SetSourceRadius(24.f);
	LampFillLight->SetCastShadows(false);
	LampFillLight->SetIntensity(0.f);
	LampFillLight->SetVisibility(false);

	LampOnSound = LampOnAsset.Get();
	LampOffSound = LampOffAsset.Get();

	Stand = CreateDefaultSubobject<USceneComponent>(TEXT("Stand"));
	Stand->SetupAttachment(RootComponent);
	Stand->SetRelativeLocation(FVector(-82.0, 0.0, 0.0));

	WeaponRest = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponRest"));
	WeaponRest->SetupAttachment(RootComponent);
	WeaponRest->SetRelativeLocationAndRotation(FVector(-20.0, 0.0, 90.4), FRotator(0.0, -90.0, 0.0));

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("BenchCamera"));
	Camera->SetupAttachment(RootComponent);
	Camera->SetRelativeLocationAndRotation(ActionCameraLocation, (ActionCameraLookAt - ActionCameraLocation).Rotation());
	Camera->SetFieldOfView(ActionCameraFov);
	Camera->bConstrainAspectRatio = false;
	CameraSpanSpring.Reset(1.0);
}

void AGinkgoWorkbench::BeginPlay()
{
	Super::BeginPlay();
	RefreshLampVisuals();
	ResetCamera();
	if (auto* Registry = GetWorld()->GetSubsystem<UGinkgoInteractionSubsystem>())
	{
		Registry->Register(this);
	}
}

void AGinkgoWorkbench::EndPlay(const EEndPlayReason::Type Reason)
{
	Release();
	if (auto* Registry = GetWorld()->GetSubsystem<UGinkgoInteractionSubsystem>())
	{
		Registry->Unregister(this);
	}
	Super::EndPlay(Reason);
}

void AGinkgoWorkbench::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Visit);
}

FVector AGinkgoWorkbench::GetInteractionLocation() const
{
	return WeaponRest->GetComponentLocation();
}

FGinkgoInteractionRanges AGinkgoWorkbench::GetInteractionRanges() const
{
	return {520.f, 160.f};
}

FText AGinkgoWorkbench::GetInteractionText() const
{
	return Visit.Character ? NSLOCTEXT("Ginkgo", "BenchExit", "取枪并离开") : NSLOCTEXT("Ginkgo", "BenchEnter", "使用改装桌");
}

EGinkgoInteractionAvailability AGinkgoWorkbench::GetAvailability(const FGinkgoInteractionContext& Context, FText& Reason) const
{
	const AGinkgoCharacter* Character = Context.Character;
	if (!Character || Visit.Character || Character->GetWorkbench())
	{
		return EGinkgoInteractionAvailability::Hidden;
	}
	bool bHasWeapon = false;
	for (int32 Slot = 0; Slot < UGinkgoWeaponComponent::SlotCount; ++Slot)
	{
		const auto* Weapon = Character->GetWeapons()->GetCarried(Slot);
		bHasWeapon |= Weapon && Weapon->Mesh;
	}
	if (!bHasWeapon)
	{
		Reason = NSLOCTEXT("Ginkgo", "BenchWeapon", "需要携带武器");
		return EGinkgoInteractionAvailability::Blocked;
	}
	const FVector Local = Stand->GetComponentTransform().InverseTransformPosition(Character->GetActorLocation());
	if (Local.X > 28.0 || FMath::Abs(Local.Y) > 112.0 || !Character->GetCharacterMovement()->IsMovingOnGround() || Character->bIsCrouched || Character->IsSlidingForAnimation())
	{
		Reason = NSLOCTEXT("Ginkgo", "BenchStand", "站到桌前");
		return EGinkgoInteractionAvailability::Blocked;
	}
	return EGinkgoInteractionAvailability::Available;
}

EGinkgoInteractionAvailability AGinkgoWorkbench::GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& Reason) const
{
	const auto* Weapons = Context.Character->GetWeapons();
	if (!Weapons->CanPlaceOnWorkbench() || (Weapons->GetHeldWeapon() && !Context.Character->IsHeldWeaponInHand()))
	{
		Reason = NSLOCTEXT("Ginkgo", "BenchBusy", "等手上的动作结束");
		return EGinkgoInteractionAvailability::Blocked;
	}
	return EGinkgoInteractionAvailability::Available;
}

double AGinkgoWorkbench::Now() const
{
	const auto* GameState = GetWorld()->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

bool AGinkgoWorkbench::IsSlotAvailable(const int32 Slot) const
{
	const auto* Weapon = Visit.Character ? Visit.Character->GetWeapons()->GetCarried(Slot) : nullptr;
	return Weapon && Weapon->Mesh;
}

bool AGinkgoWorkbench::CanSelectWeapon() const
{
	return Visit.Character && !Visit.bExiting && !Visit.InstallingPart && bReady;
}

UGinkgoWeaponAttachment* AGinkgoWorkbench::GetNextAttachment() const
{
	if (const UGinkgoWeaponDefinition* Weapon = PresentedWeapon.Get())
	{
		for (const FGinkgoWeaponAttachmentMount& Mount : Weapon->Attachments)
		{
			if (CanInstallAttachment(Mount.Part))
			{
				return Mount.Part;
			}
		}
	}
	return nullptr;
}

bool AGinkgoWorkbench::CanInstallAttachment(const UGinkgoWeaponAttachment* Part) const
{
	return CanSelectWeapon() && bOnTable && PresentedSlot == Visit.Slot
		&& Visit.Character->GetWeapons()->CanInstallAttachment(PresentedSlot, Part);
}

void AGinkgoWorkbench::InstallAttachment(AGinkgoCharacter& Character, UGinkgoWeaponAttachment* Part)
{
	if (!HasAuthority() || Visit.Character != &Character || !CanInstallAttachment(Part))
	{
		return;
	}
	Visit.InstallingPart = Part;
	ApplyVisit();
	ForceNetUpdate();
}

FGinkgoAttachmentHandPose AGinkgoWorkbench::GetInstallHandPose() const
{
	if (const auto* Action = Cast<UGinkgoInstallAttachmentAction>(Sequence.Get()))
	{
		return Action->GetHandPose();
	}
	return LampHandPose;
}

void AGinkgoWorkbench::StartInstall()
{
	SetLampReach(0.f);
	const UGinkgoWeaponDefinition* Weapon = PresentedWeapon.Get();
	const FGinkgoWeaponAttachmentMount* Mount = Weapon ? Weapon->FindAttachment(Visit.InstallingPart) : nullptr;
	if (!Mount)
	{
		return;
	}
	const auto* Weapons = PresentedCharacter->GetWeapons();
	const UGinkgoWeaponAttachment* Previous = nullptr;
	for (const UGinkgoWeaponAttachment* Candidate : Weapons->GetAttachments(PresentedSlot).Parts)
	{
		const auto* Other = Weapon->FindAttachment(Candidate);
		if (Other && Other->Mount == Mount->Mount) { Previous = Candidate; break; }
	}
	FTransform Placement = Mount->RelativeTransform;
	if (Weapon->Modularity && Mount->Mount == TEXT("muzzle"))
	{
		Placement.AddToTranslation(Weapons->GetMuzzleOffset(PresentedSlot) - Weapon->MuzzleOffset);
	}
	UGinkgoAnimAction* Action;
	if (Mount->Part->bInstallWithReload)
	{
		// 弹匣使用武器自己的换弹动画：取枪、换件、放回。接触事件由换件动作处理，桌子不处理弹药。
		const auto Weak = TWeakObjectPtr(this);
		Action = UGinkgoAnimActionChain::MakeChain(this, TEXT("ReplaceMagazineAtBench"), [Weak](FGinkgoAnimActionChainBuilder& Steps)
		{
			Steps.Step(TEXT("Take"), [Weak] { return Weak->MakeTransfer(1.f, 0.f); }).Next(TEXT("Reload"));
			Steps.Step(TEXT("Reload"), [Weak]
			{
				return UGinkgoMagazineAttachmentAction::Make(*Weak->PresentedCharacter, Weak->PresentedSlot, *Weak->Visit.InstallingPart);
			}).Next(TEXT("Place"));
			Steps.Step(TEXT("Place"), [Weak] { return Weak->MakeTransfer(0.f, 1.f); });
		});
		Action->Occupy(FAnimActionTags::Occupancy_LeftArm, Ginkgo::Occupancy::PriorityWeaponSwap)
			->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityWeaponSwap);
	}
	else
	{
		Action = UGinkgoInstallAttachmentAction::Make(*WeaponRest, *Mount->Part,
			Placement * RestPose, Stand->GetComponentTransform(), PresentedCharacter->GetWeaponSlotComponent(PresentedSlot), Previous, Mount->ModuleBone,
			Weapons->GetModularVisual(PresentedSlot));
	}
	Sequence = Action;
	PresentedCharacter->GetAnimActions()->Start(Action, [Weak = TWeakObjectPtr(this)](UGinkgoAnimAction& Finished, const FGinkgoAnimActionResult& Result)
	{
		auto* Bench = Weak.Get();
		if (!Bench || Bench->Sequence.Get() != &Finished)
		{
			return;
		}
		Bench->Sequence.Reset();
		if (Bench->HasAuthority())
		{
			UGinkgoWeaponComponent* Weapons = Bench->Visit.Character ? Bench->Visit.Character->GetWeapons() : nullptr;
			if (Result.Outcome == EGinkgoAnimActionOutcome::Completed && Weapons && Bench->Visit.InstallingPart
				&& !Bench->Visit.InstallingPart->bInstallWithReload && Weapons->GetCarried(Bench->PresentedSlot) == Bench->PresentedWeapon.Get())
			{
				Weapons->InstallAttachment(Bench->PresentedSlot, Bench->Visit.InstallingPart);
			}
			Bench->Visit.InstallingPart = nullptr;
			Bench->ApplyVisit();
			Bench->ForceNetUpdate();
		}
	});
}

int32 AGinkgoWorkbench::GetAdjacentSlot(bool bNext) const
{
	constexpr int32 Count = UGinkgoWeaponComponent::SlotCount;
	const int32 Start = Visit.Slot == INDEX_NONE ? (bNext ? Count - 1 : 0) : Visit.Slot;
	for (int32 Offset = 1; Offset <= Count; ++Offset)
	{
		const int32 Slot = (Start + (bNext ? Offset : -Offset) + Count) % Count;
		if (Slot != Visit.Slot && IsSlotAvailable(Slot))
		{
			return Slot;
		}
	}
	return INDEX_NONE;
}

void AGinkgoWorkbench::SelectWeapon(AGinkgoCharacter& Character, const int32 Slot)
{
	if (!HasAuthority() || Visit.Character != &Character || !CanSelectWeapon() || !IsSlotAvailable(Slot) || Slot == Visit.Slot)
	{
		return;
	}
	Visit.PreviousSlot = Visit.Slot;
	Visit.Slot = Slot;
	ApplyVisit();
	ForceNetUpdate();
}

float AGinkgoWorkbench::GetGunAlpha() const { return Ease((TransferProgress - 0.18f) / (WorkbenchContactProgress - 0.18f)); }
float AGinkgoWorkbench::GetGripAlpha() const { return 1.f - Ease((TransferProgress - WorkbenchContactProgress) / (1.f - WorkbenchContactProgress)); }

void AGinkgoWorkbench::Interact(const FGinkgoInteractionContext& Context)
{
	FText Reason;
	if (!HasAuthority() || GetAvailability(Context, Reason) != EGinkgoInteractionAvailability::Available ||
		(Context.Character->IsLocallyControlled() && GetLocalAvailability(Context, Reason) != EGinkgoInteractionAvailability::Available))
	{
		return;
	}
	Visit.Character = Context.Character;
	Visit.Slot = IsSlotAvailable(Context.WeaponSlot) ? Context.WeaponSlot : INDEX_NONE;
	Visit.StartTime = Now();
	ApplyVisit();
	ForceNetUpdate();
}

void AGinkgoWorkbench::RequestExit(AGinkgoCharacter& Character)
{
	if (!HasAuthority() || Visit.Character != &Character || Visit.bExiting)
	{
		return;
	}
	Visit.PreviousSlot = PresentedSlot;
	Visit.InstallingPart = nullptr;
	Visit.bExiting = true;
	ApplyVisit();
	ForceNetUpdate();
}

void AGinkgoWorkbench::OnRep_Visit()
{
	ApplyVisit();
}

void AGinkgoWorkbench::ApplyVisit()
{
	if (PresentedCharacter != Visit.Character)
	{
		Release();
	}
	AGinkgoCharacter* Character = Visit.Character;
	if (!Character)
	{
		return;
	}
	const bool bEntering = !PresentedCharacter.IsValid();
	if (bEntering)
	{
		PresentedCharacter = Character;
		EnteringController = Character->GetController();
		ApproachStart = Character->GetActorTransform();
		Character->SetWorkbench(this);
		// 桌上由 AnimAction 换枪，退出旧的 Linked StateTree；离桌恢复推进时会进入取走那把枪的子树。
		Character->GetWeapons()->StopActionStates();
		Character->GetWeapons()->SetTriggerHeld(false);
		Character->GetWeapons()->SetWantsToAim(false);
		Character->GetMesh()->AddTickPrerequisiteActor(this);
		if (Character->GetLocalRole() != ROLE_SimulatedProxy)
		{
			auto* Movement = Character->GetCharacterMovement();
			Character->StopJumping();
			Character->SetWantsToSprint(false);
			Character->ConsumeMovementInputVector();
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
			bMovementLocked = true;
		}
		if (auto* PC = Cast<APlayerController>(Character->GetController()); PC && PC->IsLocalController())
		{
			Viewer = PC;
			PC->SetIgnoreLookInput(true);
		}
	}
	const bool bExitStarted = Visit.bExiting && !bPresentedExit;
	if (bEntering || bExitStarted)
	{
		if (auto* PC = Viewer.Get())
		{
			if (auto* Manager = Cast<AGinkgoPlayerCameraManager>(PC->PlayerCameraManager))
			{
				Manager->BlendTo(Visit.bExiting ? static_cast<AActor&>(*Character) : *this, CameraBlendTime);
			}
		}
	}
	const bool bInstallChanged = PresentedInstall.Get() != Visit.InstallingPart;
	// 键鼠和手柄共用服务器确认的访问状态；只有操作这张桌子的本地玩家听到菜单反馈。
	if (auto* PC = Viewer.Get(); PC && !bEntering)
	{
		if (bExitStarted) { UGinkgoUiSoundSubsystem::PlayWorkbench(PC, EGinkgoUiSound::Close); }
		else if (bInstallChanged && Visit.InstallingPart) { UGinkgoUiSoundSubsystem::PlayWorkbench(PC, EGinkgoUiSound::WorkbenchConfirm); }
		else if (LocalSelection != Visit.Slot) { UGinkgoUiSoundSubsystem::PlayWorkbench(PC, EGinkgoUiSound::Select); }
	}
	PresentedInstall = Visit.InstallingPart;
	if (bEntering || bExitStarted || LocalSelection != Visit.Slot || bInstallChanged)
	{
		CancelSequence();
		bPresentedExit = Visit.bExiting;
		LocalSelection = Visit.Slot;
		if (bEntering)
		{
			PrepareSlot(Visit.PreviousSlot != INDEX_NONE ? Visit.PreviousSlot : Visit.Slot);
			CameraSpanSpring.Reset(ComputeFocusDistanceScale());
		}
		if (Visit.InstallingPart)
		{
			// 服务器可能先完成开灯和放枪。客户端在安装动作开始前先把台灯和同一把枪对齐到就绪状态。
			if (PresentedSlot != Visit.Slot)
			{
				PrepareSlot(Visit.Slot);
			}
			SetLampState(true, true);
			Character->GetWeapons()->SetHeldSlot(Visit.Slot);
			SetProgress(1.f);
			StartInstall();
		}
		else if (bInstallChanged && !bExitStarted && !bEntering && PresentedSlot == Visit.Slot)
		{
			bReady = true;
		}
		else
		{
			StartSequence();
		}
	}
	SetActorTickEnabled(Visit.Character != nullptr);
	UpdatePresentation(0.f);
}

void AGinkgoWorkbench::PrepareSlot(int32 Slot)
{
	if (PresentedSlot != Slot)
	{
		FocusedAttachment.Reset();
	}
	PresentedSlot = Slot;
	PresentedWeapon = PresentedCharacter->GetWeapons()->GetCarried(Slot);
	if (const auto* Weapon = PresentedWeapon.Get(); Weapon && Weapon->Mesh)
	{
		// 枪的局部 +X 朝枪口、+Z 朝瞄具。沿桌边摆枪，让右手握点在左手右边、扳机一侧靠近站位。
		FVector Barrel = WeaponRest->GetForwardVector();
		const double GripSpacing = Weapon->LoweredHands.Right.GetLocation().X - Weapon->LoweredHands.Left.GetLocation().X;
		if (GripSpacing * FVector::DotProduct(Barrel, Stand->GetRightVector()) < 0.0)
		{
			Barrel = -Barrel;
		}
		const FVector TableNormal = WeaponRest->GetUpVector();
		const FVector TowardFarEdge = FVector::VectorPlaneProject(Stand->GetForwardVector(), TableNormal);
		const FQuat Rotation = FRotationMatrix::MakeFromXZ(Barrel, TowardFarEdge).ToQuat();
		const FBox Bounds = Weapon->Mesh->GetBoundingBox();
		const FVector Center = WeaponRest->GetComponentLocation() + TableNormal * Bounds.GetExtent().Y;
		RestPose = FTransform(Rotation, Center - Rotation.RotateVector(Bounds.GetCenter()));
	}
}

void AGinkgoWorkbench::SetLampReach(const float Progress, const float PressPulse)
{
	LampReachProgress = FMath::Clamp(Progress, 0.f, 1.f);
	if (LampReachProgress <= UE_KINDA_SMALL_NUMBER || !LampSwitchTarget || !Stand)
	{
		LampReachProgress = 0.f;
		LampHandPose = {};
		return;
	}
	const FVector Up = Stand->GetUpVector();
	const FVector Forward = Stand->GetForwardVector();
	const FVector Right = Stand->GetRightVector();
	const FVector SwitchWorld = LampSwitchTarget->GetComponentLocation();

	// 腰部适度前探送肩，让左手自然伸到灯头黄铜拨杆处并在卡点完成轻按动作。
	LampHandPose.BodyLean = 0.62f * LampReachProgress;
	LampHandPose.LeftGrip = LampReachProgress;
	LampHandPose.RightGrip = 0.f;
	LampHandPose.WristLocation = SwitchWorld - Forward * (4.4 - 1.6 * PressPulse) + Right * 1.2 - Up * (1.8 + 1.0 * PressPulse);
	const FQuat FlickPitch(Right, FMath::DegreesToRadians(-12.0 * LampReachProgress + 10.0 * PressPulse));
	const FQuat FlickYaw(Up, FMath::DegreesToRadians(-8.0 * LampReachProgress));
	LampHandPose.WristRotationDelta = (FlickPitch * FlickYaw).GetNormalized();
}

void AGinkgoWorkbench::SetLampState(const bool bInOn, const bool bImmediate)
{
	const bool bChanged = bLampOn != bInOn;
	bLampOn = bInOn;
	if (bImmediate)
	{
		LampAlpha = bLampOn ? 1.f : 0.f;
		RefreshLampVisuals();
		return;
	}
	if (bChanged && GetWorld() && GetNetMode() != NM_DedicatedServer)
	{
		if (USoundBase* Cue = bLampOn ? LampOnSound.Get() : LampOffSound.Get(); Cue && LampSwitchTarget)
		{
			UGameplayStatics::PlaySoundAtLocation(this, Cue, LampSwitchTarget->GetComponentLocation(), 0.72f);
		}
	}
	RefreshLampVisuals();
}

void AGinkgoWorkbench::RefreshLampVisuals()
{
	const float SmoothAlpha = Ease(LampAlpha);
	const bool bLit = SmoothAlpha > UE_KINDA_SMALL_NUMBER;
	if (LampLight)
	{
		LampLight->SetIntensity(LampIntensity * SmoothAlpha);
		LampLight->SetVisibility(bLit);
	}
	if (LampFillLight)
	{
		const float Fill = LampFillIntensity * SmoothAlpha;
		LampFillLight->SetIntensity(Fill);
		LampFillLight->SetVisibility(Fill > UE_KINDA_SMALL_NUMBER);
	}
	if (LampSwitchMesh)
	{
		const double AngleDeg = FMath::Lerp(-15.0, 15.0, bLampOn ? 1.0 : 0.0);
		LampSwitchMesh->SetRelativeRotation(FQuat(LampSwitchHingeAxis.GetSafeNormal(), FMath::DegreesToRadians(AngleDeg)).Rotator());
	}
	if (LampMesh && LampMesh->GetStaticMesh())
	{
		if (!LampMaterial)
		{
			if (UMaterialInterface* BaseMat = LampMesh->GetMaterial(0))
			{
				LampMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
				LampMesh->SetMaterial(0, LampMaterial);
				if (LampSwitchMesh && LampSwitchMesh->GetStaticMesh())
				{
					LampSwitchMesh->SetMaterial(0, LampMaterial);
				}
			}
		}
		if (LampMaterial)
		{
			LampMaterial->SetScalarParameterValue(TEXT("LampGlow"), LampEmissiveStrength * SmoothAlpha);
		}
	}
}

UGinkgoAnimAction* AGinkgoWorkbench::MakeLampAction(const bool bTurnOn)
{
	const float StartReach = LampReachProgress;
	// 进桌尚未拨开开关便取消时，台灯本就处于熄灭状态，只需把抬到半途的左手平滑收回。
	if (!bTurnOn && !bLampOn)
	{
		const float RetractDuration = StartReach * 0.24f;
		return UGinkgoAnimAction::Make(this, TEXT("RetractWorkbenchLampHand"),
			[Weak = TWeakObjectPtr(this), StartReach, RetractDuration](FGinkgoAnimActionSetup& Setup)
		{
			if (RetractDuration <= UE_SMALL_NUMBER)
			{
				if (auto* Bench = Weak.Get()) { Bench->SetLampReach(0.f); }
				return;
			}
			Setup.HoldFor(RetractDuration);
			Setup.OnTick([Weak, StartReach, RetractDuration, Elapsed = 0.f](FGinkgoAnimActionContext&, float Dt) mutable
			{
				Elapsed += Dt;
				if (auto* Bench = Weak.Get())
				{
					Bench->SetLampReach(FMath::Lerp(StartReach, 0.f, Ease(FMath::Min(Elapsed / RetractDuration, 1.f))));
				}
			});
		});
	}

	const float TotalDuration = FMath::Max(0.15f, LampToggleDuration * (1.f - 0.35f * StartReach));
	return UGinkgoAnimAction::Make(this, bTurnOn ? TEXT("TurnOnWorkbenchLamp") : TEXT("TurnOffWorkbenchLamp"),
		[Weak = TWeakObjectPtr(this), bTurnOn, StartReach, TotalDuration](FGinkgoAnimActionSetup& Setup)
	{
		Setup.HoldFor(TotalDuration);
		Setup.OnTick([Weak, bTurnOn, StartReach, TotalDuration, Elapsed = 0.f, bSwitched = false](FGinkgoAnimActionContext&, float Dt) mutable
		{
			Elapsed += Dt;
			auto* Bench = Weak.Get();
			if (!Bench)
			{
				return;
			}
			const float Normalized = FMath::Clamp(Elapsed / TotalDuration, 0.f, 1.f);
			if (!bSwitched && Normalized >= LampSwitchContactProgress)
			{
				bSwitched = true;
				Bench->SetLampState(bTurnOn, false);
			}
			const float Reach = Normalized < LampSwitchContactProgress
				? FMath::Lerp(StartReach, 1.f, Ease(Normalized / LampSwitchContactProgress))
				: 1.f - Ease((Normalized - LampSwitchContactProgress) / (1.f - LampSwitchContactProgress));
			const float PressWindow = FMath::Clamp((Normalized - (LampSwitchContactProgress - 0.12f)) / 0.24f, 0.f, 1.f);
			const float PressPulse = FMath::Sin(PressWindow * UE_PI);
			Bench->SetLampReach(Reach, PressPulse);
		});
	});
}

void AGinkgoWorkbench::SetProgress(float Value)
{
	TransferProgress = FMath::Clamp(Value, 0.f, 1.f);
	auto* Character = PresentedCharacter.Get();
	auto* Gun = Character ? Character->GetWeaponSlotComponent(PresentedSlot) : nullptr;
	if (!Gun || !Gun->GetSkinnedAsset() || Character->GetWeapons()->GetCarried(PresentedSlot) != PresentedWeapon.Get())
	{
		return;
	}
	if (!bOnTable && TransferProgress >= WorkbenchContactProgress)
	{
		Gun->AttachToComponent(WeaponRest, FAttachmentTransformRules::KeepWorldTransform);
		Gun->SetWorldTransform(RestPose);
		bOnTable = true;
	}
	else if (bOnTable && TransferProgress < WorkbenchContactProgress)
	{
		Character->AttachWeaponToHand(PresentedSlot);
		bOnTable = false;
	}
}

UGinkgoAnimAction* AGinkgoWorkbench::MakeTransfer(float From, float To)
{
	const float Duration = FMath::Abs(To - From) * (To > From ? PlaceDuration : TakeDuration);
	return UGinkgoAnimAction::Make(this, To > From ? TEXT("PlaceOnBench") : TEXT("TakeFromBench"),
		[Weak = TWeakObjectPtr(this), From, To, Duration](FGinkgoAnimActionSetup& Setup)
	{
		if (auto* Bench = Weak.Get())
		{
			Bench->SetLampReach(0.f);
		}
		if (Duration <= UE_SMALL_NUMBER)
		{
			if (auto* Bench = Weak.Get())
			{
				Bench->SetProgress(To);
			}
			return;
		}
		Setup.HoldFor(Duration);
		Setup.OnTick([Weak, From, To, Duration, Elapsed = 0.f](FGinkgoAnimActionContext&, float Dt) mutable
		{
			Elapsed += Dt;
			if (auto* Bench = Weak.Get())
			{
				Bench->SetProgress(FMath::Lerp(From, To, FMath::Min(Elapsed / Duration, 1.f)));
			}
		});
	});
}

UGinkgoAnimAction* AGinkgoWorkbench::MakeEquipStep(int32 Slot, bool bDraw)
{
	auto* Character = PresentedCharacter.Get();
	auto* Weapon = Character ? Character->GetWeapons()->GetCarried(Slot) : nullptr;
	if (Weapon && (bDraw ? Weapon->DrawMontage : Weapon->StowMontage) && Character->GetMesh()->GetAnimInstance())
	{
		return bDraw ? Character->MakeDrawWeaponAction(Slot) : Character->MakeStowWeaponAction(Slot);
	}
	// 没配收拔枪片段的枪（例如 MG3 收枪）直接换挂点，仍然占用这一步的时间。
	return UGinkgoAnimAction::Make(this, bDraw ? TEXT("DrawAtBench") : TEXT("StowAtBench"),
		[Weak = TWeakObjectPtr(Character), Slot, bDraw](FGinkgoAnimActionSetup& Setup)
	{
		if (auto* Owner = Weak.Get())
		{
			if (bDraw)
			{
				Owner->AttachWeaponToHand(Slot, true);
			}
			else
			{
				Owner->AttachWeaponToStow(Slot, true);
			}
			Owner->GetWeapons()->SetHeldSlot(bDraw ? Slot : INDEX_NONE);
			Setup.HoldFor(0.16f);
		}
	});
}

void AGinkgoWorkbench::StartSequence()
{
	bReady = false;
	auto* Character = PresentedCharacter.Get();
	const bool bExit = Visit.bExiting;
	const int32 Target = Visit.Slot;
	const int32 Old = Visit.PreviousSlot;
	const float From = TransferProgress;
	const bool bNeedLampOn = !bExit && !bLampOn;
	const bool bNeedTake = bExit && From > UE_SMALL_NUMBER;
	const bool bNeedLampOff = bExit && (bLampOn || LampReachProgress > UE_SMALL_NUMBER);

	if (!bExit && Target == INDEX_NONE && !bNeedLampOn)
	{
		bReady = true;
		return;
	}
	if (bExit && !bNeedTake && !bNeedLampOff)
	{
		if (HasAuthority())
		{
			FinishVisit();
		}
		return;
	}

	const auto Weak = TWeakObjectPtr(this);
	auto* Chain = UGinkgoAnimActionChain::MakeChain(this, bExit ? TEXT("LeaveWorkbench") : TEXT("SetWorkbenchWeapon"),
		[Weak, Target, Old, From, bExit, bNeedLampOn, bNeedTake, bNeedLampOff](FGinkgoAnimActionChainBuilder& Steps)
	{
		auto* Bench = Weak.Get();
		if (!Bench)
		{
			return;
		}
		if (bExit)
		{
			// 离桌顺序：先从亮着的台面取回武器，确认武器回到手上后再拨关台灯。
			if (bNeedTake)
			{
				auto TakeStep = Steps.Step(TEXT("Take"), [Weak, From] { return Weak->MakeTransfer(From, 0.f); });
				if (bNeedLampOff)
				{
					TakeStep.Next(TEXT("LampOff"));
				}
			}
			if (bNeedLampOff)
			{
				Steps.Step(TEXT("LampOff"), [Weak] { return Weak->MakeLampAction(false); });
			}
			return;
		}

		// 进桌顺序：先打开台灯照亮工作区，再执行取放或拔枪动作。
		const bool bHasWeaponStep = Target != INDEX_NONE;
		const bool bNeedDraw = bHasWeaponStep && (Old != INDEX_NONE || Bench->PresentedCharacter->GetWeapons()->GetHeldSlot() != Target);
		const FName FirstWeaponStep = Old != INDEX_NONE ? TEXT("Take") : (bNeedDraw ? TEXT("Draw") : TEXT("Place"));
		if (bNeedLampOn)
		{
			auto LampStep = Steps.Step(TEXT("LampOn"), [Weak] { return Weak->MakeLampAction(true); });
			if (bHasWeaponStep)
			{
				LampStep.Next(FirstWeaponStep);
			}
		}
		if (!bHasWeaponStep)
		{
			return;
		}
		if (Old != INDEX_NONE)
		{
			Steps.Step(TEXT("Take"), [Weak, From] { return Weak->MakeTransfer(From, 0.f); }).Next(TEXT("Stow"));
			Steps.Step(TEXT("Stow"), [Weak, Old] { return Weak->MakeEquipStep(Old, false); }).Next(TEXT("Draw"));
		}
		if (bNeedDraw)
		{
			Steps.Step(TEXT("Draw"), [Weak, Target] { return Weak->MakeEquipStep(Target, true); }).Next(TEXT("Place"));
		}
		Steps.Step(TEXT("Place"), [Weak, Target]
		{
			Weak->PrepareSlot(Target);
			auto* Owner = Weak->PresentedCharacter.Get();
			Owner->GetWeapons()->SetHeldSlot(Target);
			if (!Owner->IsHeldWeaponInHand())
			{
				Owner->AttachWeaponToHand(Target);
			}
			return Weak->MakeTransfer(0.f, 1.f);
		});
	});
	Sequence = Chain;
	Chain->Occupy(FAnimActionTags::Occupancy_LeftArm, Ginkgo::Occupancy::PriorityWeaponSwap)
	     ->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityWeaponSwap);
	Character->GetAnimActions()->Start(Chain, [Weak, bExit](UGinkgoAnimAction& Action, const FGinkgoAnimActionResult& Result)
	{
		auto* Bench = Weak.Get();
		if (!Bench || Bench->Sequence.Get() != &Action)
		{
			return;
		}
		Bench->Sequence.Reset();
		Bench->SetLampReach(0.f);
		if (Result.Outcome == EGinkgoAnimActionOutcome::Completed && !bExit)
		{
			Bench->bReady = true;
		}
		else if (Bench->HasAuthority())
		{
			Bench->FinishVisit();
		}
	});
}

void AGinkgoWorkbench::CancelSequence()
{
	auto* Action = Sequence.Get();
	Sequence.Reset();
	if (Action && PresentedCharacter.IsValid())
	{
		PresentedCharacter->GetAnimActions()->Cancel(Action, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	}
}

void AGinkgoWorkbench::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdatePresentation(DeltaSeconds);
}

void AGinkgoWorkbench::UpdatePresentation(const float DeltaSeconds)
{
	const float TargetLamp = bLampOn ? 1.f : 0.f;
	if (!FMath::IsNearlyEqual(LampAlpha, TargetLamp, 0.0001f) && DeltaSeconds > 0.f)
	{
		LampAlpha = FMath::FInterpConstantTo(LampAlpha, TargetLamp, DeltaSeconds, 1.f / FMath::Max(LampFadeDuration, 0.02f));
		RefreshLampVisuals();
	}

	auto* Character = PresentedCharacter.Get();
	if (!IsValid(Character) || Character->GetDamageable()->IsDead() || (HasAuthority() && (!Character->GetController() || EnteringController.Get() != Character->GetController())))
	{
		if (HasAuthority())
		{
			FinishVisit();
		}
		return;
	}
	if (HasAuthority() && ((Visit.Slot != INDEX_NONE && !IsSlotAvailable(Visit.Slot)) ||
		(PresentedWeapon.IsValid() && Character->GetWeapons()->GetCarried(PresentedSlot) != PresentedWeapon.Get())))
	{
		FinishVisit();
		return;
	}
	if (!Visit.bExiting && bMovementLocked && !bAligned)
	{
		FVector StandCenter = Stand->GetComponentLocation();
		StandCenter.Z += Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
		const float Alpha = Ease(static_cast<float>(Now() - Visit.StartTime) / (0.36f * PlaceDuration));
		bAligned = Alpha >= 1.f;
		FHitResult Hit;
		Character->SetActorLocationAndRotation(FMath::Lerp(ApproachStart.GetLocation(), StandCenter, Alpha),
			FQuat::Slerp(ApproachStart.GetRotation(), Stand->GetComponentQuat(), Alpha), true, &Hit);
		if (HasAuthority() && Hit.bBlockingHit && FVector::Dist2D(Character->GetActorLocation(), StandCenter) > 6.f)
		{
			RequestExit(*Character);
			return;
		}
	}
	UpdateCamera(DeltaSeconds);
}

bool AGinkgoWorkbench::WantsFocusCamera() const
{
	return Visit.Character != nullptr
		&& !Visit.bExiting
		&& !Visit.InstallingPart
		&& bReady
		&& bOnTable
		&& PresentedSlot != INDEX_NONE
		&& !Sequence.IsValid();
}

double AGinkgoWorkbench::ComputeFocusDistanceScale() const
{
	const UGinkgoWeaponDefinition* Weapon = PresentedWeapon.Get();
	if (!Weapon || !Weapon->Mesh)
	{
		return 1.0;
	}
	double EffectiveSpan = Weapon->Mesh->GetBoundingBox().GetSize().X;
	if (const AGinkgoCharacter* Character = PresentedCharacter.Get(); Character && PresentedSlot != INDEX_NONE)
	{
		EffectiveSpan += FMath::Max(0.0, Character->GetWeapons()->GetMuzzleOffset(PresentedSlot).X - Weapon->MuzzleOffset.X);
	}
	// 由基准特写机位到 WeaponRest 的投影深度与 FocusCameraFov 求基准水平可视宽，使任意长度武器稳定落在左侧约 56% 画面内。
	const FVector Anchor = WeaponRest ? WeaponRest->GetRelativeLocation() : FVector(-20.0, 0.0, 90.4);
	const FVector BaseDir = (FocusCameraLookAt - FocusCameraLocation).GetSafeNormal();
	const double BaseDepth = FMath::Max(FVector::DotProduct(Anchor - FocusCameraLocation, BaseDir), 40.0);
	const double BaseCoverage = 2.0 * BaseDepth * FMath::Tan(FMath::DegreesToRadians(static_cast<double>(FocusCameraFov) * 0.5));
	const double DesiredScale = (EffectiveSpan / 0.56) / FMath::Max(BaseCoverage, 1.0);
	return FMath::Clamp(DesiredScale, 0.84, 2.40);
}

void AGinkgoWorkbench::SetFocusedAttachment(const UGinkgoWeaponAttachment* Part)
{
	if (FocusedAttachment.Get() == Part)
	{
		return;
	}
	FocusedAttachment = Part;
	if (!Part)
	{
		return;
	}
	// 仅在切换高亮配件瞬间按需载入网格以获取精确包围盒中心，避免在每帧 UpdateCamera 里触碰同步加载。
	if (!Part->bInstallWithReload && !Part->bEmptyModule)
	{
		if (!Part->ModuleMesh.IsNull() && !Part->ModuleMesh.IsValid())
		{
			Part->ModuleMesh.LoadSynchronous();
		}
		else if (!Part->Mesh.IsNull() && !Part->Mesh.IsValid())
		{
			Part->Mesh.LoadSynchronous();
		}
	}
	else if (Part->bEmptyModule)
	{
		if (const UGinkgoWeaponDefinition* Weapon = PresentedWeapon.Get())
		{
			if (const FGinkgoWeaponAttachmentMount* Mount = Weapon->FindAttachment(Part))
			{
				for (const FGinkgoWeaponAttachmentMount& Sibling : Weapon->Attachments)
				{
					if (Sibling.Mount == Mount->Mount && Sibling.Part && !Sibling.Part->bEmptyModule && !Sibling.Part->ModuleMesh.IsNull() && !Sibling.Part->ModuleMesh.IsValid())
					{
						Sibling.Part->ModuleMesh.LoadSynchronous();
						break;
					}
				}
			}
		}
	}
}

FVector AGinkgoWorkbench::ComputeFocusedPartOffset() const
{
	if (!WantsFocusCamera())
	{
		return FVector::ZeroVector;
	}
	const UGinkgoWeaponAttachment* Part = FocusedAttachment.Get();
	const UGinkgoWeaponDefinition* Weapon = PresentedWeapon.Get();
	const AGinkgoCharacter* Character = PresentedCharacter.Get();
	if (!Part || !Weapon || !Character || PresentedSlot == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	const FGinkgoWeaponAttachmentMount* Mount = Weapon->FindAttachment(Part);
	if (!Mount)
	{
		return FVector::ZeroVector;
	}

	const UGinkgoWeaponComponent* Weapons = Character->GetWeapons();
	FTransform Placement = Mount->RelativeTransform;
	if (Weapon->Modularity && Mount->Mount == TEXT("muzzle"))
	{
		Placement.AddToTranslation(Weapons->GetMuzzleOffset(PresentedSlot) - Weapon->MuzzleOffset);
	}

	FVector PartCenterInGun = Placement.GetLocation();
	bool bResolved = false;
	if (Part->bInstallWithReload || Part->bEmptyModule)
	{
		if (const UPoseableMeshComponent* Gun = Character->GetWeaponSlotComponent(PresentedSlot))
		{
			const FName BoneName = !Mount->ModuleBone.IsNone() ? Mount->ModuleBone : Weapon->MagazineBone;
			const int32 BoneIndex = !BoneName.IsNone() ? Gun->GetBoneIndex(BoneName) : INDEX_NONE;
			if (BoneIndex != INDEX_NONE)
			{
				PartCenterInGun = Gun->GetBoneTransform(BoneIndex, FTransform::Identity).GetLocation();
				bResolved = true;
			}
		}
		if (!bResolved && Part->bEmptyModule)
		{
			for (const FGinkgoWeaponAttachmentMount& Sibling : Weapon->Attachments)
			{
				if (Sibling.Mount == Mount->Mount && Sibling.Part && !Sibling.Part->bEmptyModule)
				{
					if (const USkeletalMesh* SiblingMesh = Sibling.Part->ModuleMesh.Get())
					{
						PartCenterInGun = Placement.TransformPosition(SiblingMesh->GetBounds().Origin);
						bResolved = true;
						break;
					}
				}
			}
		}
	}
	if (!bResolved)
	{
		if (const USkeletalMesh* ModuleMesh = Part->ModuleMesh.Get())
		{
			PartCenterInGun = Placement.TransformPosition(ModuleMesh->GetBounds().Origin);
		}
		else if (const UStaticMesh* StaticMesh = Part->Mesh.Get())
		{
			PartCenterInGun = Placement.TransformPosition(StaticMesh->GetBoundingBox().GetCenter());
		}
	}

	const FVector PartWorld = RestPose.TransformPosition(PartCenterInGun);
	const FVector PartLocal = GetActorTransform().InverseTransformPosition(PartWorld);
	const FVector Anchor = WeaponRest ? WeaponRest->GetRelativeLocation() : FVector(-20.0, 0.0, 90.4);
	// 限幅在 48 cm 以内，保证超长消音器或极端挂点也不会把镜头拉离枪匠垫或靠近台灯遮挡。
	return (PartLocal - Anchor).GetClampedToMaxSize(48.0);
}

void AGinkgoWorkbench::UpdateCamera(const float DeltaSeconds)
{
	if (!Camera)
	{
		return;
	}
	const double Dt = FMath::Max(static_cast<double>(DeltaSeconds), 0.0);
	const bool bFocusNow = WantsFocusCamera();
	const double TargetFocus = bFocusNow ? 1.0 : 0.0;
	const double FocusOmega = TargetFocus > CameraFocusSpring.Value
		? static_cast<double>(CameraFocusInOmega)
		: static_cast<double>(CameraPullBackOmega);
	CameraFocusSpring.Step(TargetFocus, FocusOmega, Dt);

	if (PresentedWeapon.IsValid())
	{
		CameraSpanSpring.Step(ComputeFocusDistanceScale(), 6.0, Dt);
	}

	const FVector TargetPartOffset = ComputeFocusedPartOffset();
	const double TargetPartZoom = (bFocusNow && FocusedAttachment.IsValid() && PresentedWeapon.IsValid()
		&& PresentedWeapon->FindAttachment(FocusedAttachment.Get()) != nullptr) ? 1.0 : 0.0;
	const double PartOmega = static_cast<double>(CameraPartFocusOmega);
	CameraPartOffsetSpring.Step(TargetPartOffset, PartOmega, Dt);
	CameraPartZoomSpring.Step(TargetPartZoom, PartOmega, Dt);

	const double TargetSway = (Visit.Character && !Visit.bExiting && HandheldSwayScale > 0.f) ? 1.0 : 0.0;
	CameraSwayEnvelopeSpring.Step(TargetSway, 3.5, Dt);
	CameraSwayClock += Dt;

	const double FocusAlpha = FMath::Clamp(CameraFocusSpring.Value, 0.0, 1.0);
	const double DesiredScale = FMath::Clamp(CameraSpanSpring.Value, 0.84, 2.40);
	// 机位退距上限设为 1.50 以避开室内低天花板与角色遮挡；超长武器将剩余视野倍率平滑折算为微增特写 FOV。
	constexpr double MaxDistanceScale = 1.50;
	const double SpanScale = FMath::Min(DesiredScale, MaxDistanceScale);
	const double BaseTanHalf = FMath::Tan(FMath::DegreesToRadians(static_cast<double>(FocusCameraFov) * 0.5));
	const double EffectiveFocusFov = DesiredScale > MaxDistanceScale
		? FMath::Clamp(FMath::RadiansToDegrees(2.0 * FMath::Atan(BaseTanHalf * (DesiredScale / MaxDistanceScale))),
			static_cast<double>(FocusCameraFov), 84.0)
		: static_cast<double>(FocusCameraFov);

	const FVector Anchor = WeaponRest ? WeaponRest->GetRelativeLocation() : FVector(-20.0, 0.0, 90.4);
	const double PartZoomAlpha = FMath::Clamp(CameraPartZoomSpring.Value, 0.0, 1.0);
	const double ZoomFactor = 1.0 - static_cast<double>(FocusPartZoomIn) * PartZoomAlpha;
	const FVector PartOffset = CameraPartOffsetSpring.Value;
	const FVector EyePartShift(PartOffset.X * static_cast<double>(FocusPartEyeBias), PartOffset.Y * static_cast<double>(FocusPartEyeBias), 0.0);
	const FVector LookPartShift = PartOffset * static_cast<double>(FocusPartLookBias);

	// 围绕 WeaponRest 径向缩放特写机位与注视点，再叠加选中部件的柔和偏向与微推近，既突出该部位又不丢失整枪全貌。
	const FVector ScaledFocusEye = Anchor + (FocusCameraLocation - Anchor) * (SpanScale * ZoomFactor) + EyePartShift;
	const FVector ScaledFocusLook = Anchor + (FocusCameraLookAt - Anchor) * SpanScale + LookPartShift;

	// 推拉途中微抬弧线（+4 cm），绕开角色肩部轮廓，呈现沉稳的手持推轨质感。
	const FVector BaseEye = FMath::Lerp(ActionCameraLocation, ScaledFocusEye, FocusAlpha)
		+ FVector(0.0, 0.0, 4.0 * FMath::Sin(FocusAlpha * UE_DOUBLE_PI));
	const FVector BaseLook = FMath::Lerp(ActionCameraLookAt, ScaledFocusLook, FocusAlpha);
	const FRotator BaseRot = (BaseLook - BaseEye).Rotation();
	const float BaseFov = static_cast<float>(FMath::Lerp(static_cast<double>(ActionCameraFov), EffectiveFocusFov, FocusAlpha));

	const double SwayGain = static_cast<double>(HandheldSwayScale) * FMath::Clamp(CameraSwayEnvelopeSpring.Value, 0.0, 1.0)
		* FMath::Lerp(1.22, 1.0, FocusAlpha);
	const double PitchDeg = 0.26 * SwayGain * SampleHandheldNoise(CameraSwayClock, 0.19, 0.4);
	const double YawDeg = 0.32 * SwayGain * SampleHandheldNoise(CameraSwayClock, 0.14, 1.9);
	const double RollDeg = 0.18 * SwayGain * SampleHandheldNoise(CameraSwayClock, 0.11, 3.5);
	const double ShiftRightCm = 0.42 * SwayGain * SampleHandheldNoise(CameraSwayClock, 0.16, 5.1);
	const double ShiftUpCm = 0.34 * SwayGain * SampleHandheldNoise(CameraSwayClock, 0.21, 6.7);

	const FQuat BaseQuat = BaseRot.Quaternion();
	const FVector CamRight = BaseQuat.GetAxisY();
	const FVector CamUp = BaseQuat.GetAxisZ();
	const FVector FinalEye = BaseEye + CamRight * ShiftRightCm + CamUp * ShiftUpCm;
	const FQuat FinalQuat = (BaseQuat * FQuat(FRotator(PitchDeg, YawDeg, RollDeg))).GetNormalized();

	Camera->SetRelativeLocationAndRotation(FinalEye, FinalQuat.Rotator());
	Camera->SetFieldOfView(BaseFov);
}

void AGinkgoWorkbench::ResetCamera()
{
	FocusedAttachment.Reset();
	CameraFocusSpring.Reset(0.0);
	CameraSpanSpring.Reset(1.0);
	CameraPartOffsetSpring.Reset(FVector::ZeroVector);
	CameraPartZoomSpring.Reset(0.0);
	CameraSwayEnvelopeSpring.Reset(0.0);
	CameraSwayClock = 0.0;
	if (Camera)
	{
		Camera->SetRelativeLocationAndRotation(ActionCameraLocation, (ActionCameraLookAt - ActionCameraLocation).Rotation());
		Camera->SetFieldOfView(ActionCameraFov);
	}
}

void AGinkgoWorkbench::FinishVisit()
{
	Release();
	Visit = {};
	ForceNetUpdate();
}

void AGinkgoWorkbench::Release()
{
	CancelSequence();
	SetLampReach(0.f);
	SetLampState(false, true);
	if (auto* Character = PresentedCharacter.Get())
	{
		if (bOnTable && PresentedWeapon.IsValid() && Character->GetWeapons()->GetCarried(PresentedSlot) == PresentedWeapon.Get())
		{
			Character->AttachWeaponToHand(PresentedSlot);
			Character->GetWeapons()->SetHeldSlot(PresentedSlot);
		}
		Character->GetMesh()->RemoveTickPrerequisiteActor(this);
		Character->SetWorkbench(nullptr);
		if (bMovementLocked && !Character->GetDamageable()->IsDead())
		{
			Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
	if (auto* PC = Viewer.Get())
	{
		PC->SetIgnoreLookInput(false);
		if (PC->GetPawn() && PC->GetViewTarget() == this)
		{
			if (auto* Manager = Cast<AGinkgoPlayerCameraManager>(PC->PlayerCameraManager))
			{
				Manager->BlendTo(*PC->GetPawn(), CameraBlendTime);
			}
		}
	}
	Viewer.Reset();
	EnteringController.Reset();
	PresentedCharacter.Reset();
	PresentedWeapon.Reset();
	PresentedSlot = INDEX_NONE;
	bMovementLocked = false;
	bOnTable = false;
	bAligned = false;
	bReady = false;
	bPresentedExit = false;
	PresentedInstall.Reset();
	TransferProgress = 0.f;
	LocalSelection = INDEX_NONE;
	ResetCamera();
	SetActorTickEnabled(false);
}

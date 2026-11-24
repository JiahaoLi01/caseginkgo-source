#include "GinkgoCharacter.h"
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Character/GinkgoCharacterAudioComponent.h"
#include "Character/GinkgoCharacterModel.h"

#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Camera/GinkgoCameraContextComponent.h"
#include "Camera/GinkgoPlayerCameraComponent.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "AI/GinkgoNoiseEmitterComponent.h"
#include "Combat/GinkgoPenetrableComponent.h"
#include "Combat/GinkgoPracticeTarget.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Animation/AnimSequence.h"
#include "Interaction/GinkgoInteractorComponent.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/Traversal/GinkgoTraversalComponent.h"
#include "Movement/Traversal/GinkgoMotionWarpingComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponTypes.h"

namespace
{
	/** 捡枪收拢时枪先挂的右手骨（同 Control Rig 里右手的目标那根） */
	const FName PickupRightHandBone(TEXT("hand_r"));

	/** 新建一个只看不碰的占位网格 */
	UStaticMeshComponent* CreatePlaceholder(ACharacter& Owner, const FName Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Component = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Owner.GetCapsuleComponent());
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		return Component;
	}

	/** 新建一个武器槽的网格组件：只看不碰，网格与挂点在槽里有枪时才换上。不放动画，活动部件由 ApplyWeaponBolts 摆 */
	UPoseableMeshComponent* CreateWeaponSlot(ACharacter& Owner, const FName Name)
	{
		UPoseableMeshComponent* Component = Owner.CreateDefaultSubobject<UPoseableMeshComponent>(Name);
		Component->SetupAttachment(Owner.GetMesh());
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		return Component;
	}

	/** 新建一只手上拿东西的网格组件：只看不碰，平时没有网格 */
	UStaticMeshComponent* CreateHandItem(ACharacter& Owner, const FName Name)
	{
		UStaticMeshComponent* Component = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Owner.GetMesh());
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		return Component;
	}
}

AGinkgoCharacter::AGinkgoCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGinkgoCharacterMovementComponent>(CharacterMovementComponentName))
{
	// 偏航由移动组件在每次移动之后定（平时面向视角，在地面上冲刺时面向速度），不按控制器转；Pitch 与 Roll 只给相机
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// 子弹打的是网格上的形体（部位），胶囊不挡子弹的线（Docs/Design/Damage.md「命中」）
	GetCapsuleComponent()->SetCollisionResponseToChannel(Ginkgo::Combat::WeaponTraceChannel, ECR_Ignore);

	// 定高跳：按住跳跃不延长上升；二段跳在 M2 打开
	JumpMaxHoldTime = 0.f;
	JumpMaxCount = 1;

	// 网格的脚底对在胶囊底上；小白人朝 +Y，转 −90° 对上角色的前方。必须在构造时摆好：父类在 PostInitializeComponents 里记下这个相对变换，
	// 蹲下时的网格偏移、模拟代理的网络平滑都以它为基准。网格资产在 ApplyCharacterMesh 里换上
	USkeletalMeshComponent* MeshComponent = GetMesh();
	MeshComponent->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()), FRotator(0.f, -90.f, 0.f));
	MeshComponent->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	MeshComponent->AnimClass = UGinkgoAnimInstance::StaticClass();

	PresentationMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterModel"));
	PresentationMesh->SetupAttachment(MeshComponent);
	PresentationMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PresentationMesh->SetGenerateOverlapEvents(false);
	PresentationMesh->SetCanEverAffectNavigation(false);
	PresentationMesh->SetDisablePostProcessBlueprint(true);

	AnimActions = CreateDefaultSubobject<UGinkgoAnimActionComponent>(TEXT("AnimActions"));
	Interactor = CreateDefaultSubobject<UGinkgoInteractorComponent>(TEXT("Interactor"));
	Traversal = CreateDefaultSubobject<UGinkgoTraversalComponent>(TEXT("Traversal"));
	MotionWarping = CreateDefaultSubobject<UGinkgoMotionWarpingComponent>(TEXT("MotionWarping"));
	PlayerCamera = CreateDefaultSubobject<UGinkgoPlayerCameraComponent>(TEXT("PlayerCamera"));
	PlayerCamera->SetupAttachment(GetCapsuleComponent());
	CameraContext = CreateDefaultSubobject<UGinkgoCameraContextComponent>(TEXT("CameraContext"));
	// 子弹穿得过人（Docs/Design/Damage.md「穿射」）：厚度用组件的默认值，人 0.3
	Penetrable = CreateDefaultSubobject<UGinkgoPenetrableComponent>(TEXT("Penetrable"));
	Damageable = CreateDefaultSubobject<UGinkgoDamageableComponent>(TEXT("Damageable"));
	Damageable->SetIsReplicated(true);
	Damageable->SetFullBodyArmor(true);
	Damageable->SetHealthRegenEnabled(true);
	CharacterAudio = CreateDefaultSubobject<UGinkgoCharacterAudioComponent>(TEXT("CharacterAudio"));
	NoiseEmitter = CreateDefaultSubobject<UGinkgoNoiseEmitterComponent>(TEXT("NoiseEmitter"));

	// 两个武器槽在武器组件里；网格在槽里有枪时换上、挂到背上（ApplyCarriedWeapons）
	Flashbang = CreateDefaultSubobject<UGinkgoFlashbangComponent>(TEXT("Flashbang"));
	Weapons = CreateDefaultSubobject<UGinkgoWeaponComponent>(TEXT("Weapons"));
	WeaponMesh = CreateWeaponSlot(*this, TEXT("WeaponMesh"));
	SecondWeaponMesh = CreateWeaponSlot(*this, TEXT("SecondWeaponMesh"));
	SidearmMesh = CreateWeaponSlot(*this, TEXT("SidearmMesh"));
	HeavyWeaponMesh = CreateWeaponSlot(*this, TEXT("HeavyWeaponMesh"));
	LeftHandItem = CreateHandItem(*this, TEXT("LeftHandItem"));
	RightHandItem = CreateHandItem(*this, TEXT("RightHandItem"));
	FeedRound = CreateHandItem(*this, TEXT("FeedRound"));
	FeedRound->SetupAttachment(WeaponMesh);
	FeedRound->SetVisibility(false);
	SecondFeedRound = CreateHandItem(*this, TEXT("SecondFeedRound"));
	SecondFeedRound->SetupAttachment(SecondWeaponMesh);
	SecondFeedRound->SetVisibility(false);
	SidearmFeedRound = CreateHandItem(*this, TEXT("SidearmFeedRound"));
	SidearmFeedRound->SetupAttachment(SidearmMesh);
	SidearmFeedRound->SetVisibility(false);
	HeavyFeedRound = CreateHandItem(*this, TEXT("HeavyFeedRound"));
	HeavyFeedRound->SetupAttachment(HeavyWeaponMesh);
	HeavyFeedRound->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	PlaceholderBody = CreatePlaceholder(*this, TEXT("PlaceholderBody"), CylinderMesh.Object);
	PlaceholderVisor = CreatePlaceholder(*this, TEXT("PlaceholderVisor"), CubeMesh.Object);
	FitPlaceholderToCapsule();
}

UGinkgoCharacterMovementComponent* AGinkgoCharacter::GetGinkgoMovement() const
{
	return CastChecked<UGinkgoCharacterMovementComponent>(GetCharacterMovement());
}

bool AGinkgoCharacter::IsSlidingForAnimation() const
{
	return GetLocalRole() == ROLE_SimulatedProxy ? ReplicatedAnimState.bSliding : GetGinkgoMovement()->IsSliding();
}

bool AGinkgoCharacter::IsSprintingForAnimation() const
{
	return GetLocalRole() == ROLE_SimulatedProxy ? ReplicatedAnimState.bSprinting : GetGinkgoMovement()->IsSprintFacing();
}

void AGinkgoCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	MeshBaseRelativeZ = BaseTranslationOffset.Z;
	ApplyCharacterMesh();
	if (Damageable->GetIsReplicated())
	{
		Damageable->OnLifeStateChanged.AddUObject(this, &ThisClass::HandlePlayerLifeStateChanged);
	}
}

void AGinkgoCharacter::ApplyCharacterMesh()
{
	// 项目动作、命中和挂点共享这个源网格。加载失败时保留构造阶段的占位体。
	const TSoftObjectPtr<USkeletalMesh>& MeshPath = UGinkgoDeveloperSettings::Get()->CharacterMesh;
	USkeletalMesh* MeshAsset = MeshPath.IsNull() ? nullptr : MeshPath.LoadSynchronous();
	if (!MeshAsset)
	{
		static bool bWarned = false;
		UE_CLOG(!bWarned, LogGinkgoAnimation, Warning, TEXT("[Animation] 角色网格 %s 读不到，用占位的圆柱体。网格由导入命令生成，见 Docs/status.md 的\"怎么试\""),
			*MeshPath.ToString());
		bWarned = true;
		return;
	}
	GetMesh()->SetSkeletalMeshAsset(MeshAsset);
	LeftHandItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("hand_l"));
	RightHandItem->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("hand_r"));
	PlaceholderBody->SetVisibility(false);
	PlaceholderVisor->SetVisibility(false);

	// 蓝图读不到时留着 UGinkgoAnimInstance 本身，逻辑数据继续更新，源网格使用参考姿势。
	const TSoftClassPtr<UAnimInstance>& AnimClassPath = UGinkgoDeveloperSettings::Get()->CharacterAnimClass;
	UClass* AnimClass = AnimClassPath.IsNull() ? nullptr : AnimClassPath.LoadSynchronous();
	if (!AnimClass || !AnimClass->IsChildOf<UGinkgoAnimInstance>())
	{
		static bool bWarnedAnimClass = false;
		UE_CLOG(!bWarnedAnimClass, LogGinkgoAnimation, Warning, TEXT("[Animation] 动画蓝图 %s 读不到或不是 UGinkgoAnimInstance 的子类，网格摆着参考姿势。动画蓝图由命令 GinkgoAnimBlueprint 生成"),
			*AnimClassPath.ToString());
		bWarnedAnimClass = true;
		return;
	}
	GetMesh()->SetAnimInstanceClass(AnimClass);
}

const UGinkgoCharacterModel* AGinkgoCharacter::ResolveCharacterModel() const
{
	return UGinkgoDeveloperSettings::Get()->PlayerModel.LoadSynchronous();
}

USkeletalMeshComponent* AGinkgoCharacter::GetPresentationMesh() const
{
	return PresentationMesh->GetSkeletalMeshAsset() ? PresentationMesh.Get() : GetMesh();
}

void AGinkgoCharacter::ApplyCharacterModel()
{
	if (GetNetMode() == NM_DedicatedServer) { PresentationMesh->SetComponentTickEnabled(false); return; }
	const auto* Model = ResolveCharacterModel();
	const bool bHasModel = Model && Model->Mesh && Model->RetargetClass && GetMesh()->GetSkeletalMeshAsset();
	PresentationMesh->SetSkeletalMeshAsset(bHasModel ? Model->Mesh.Get() : nullptr);
	PresentationMesh->SetAnimInstanceClass(bHasModel ? Model->RetargetClass.Get() : nullptr);
	PresentationMesh->SetComponentTickEnabled(bHasModel);
	if (bHasModel)
	{
		// 切换外观时同一个显示组件会复用，先清掉上一套着装的遮罩。
		for (int32 LOD = 0; LOD < Model->Mesh->GetLODNum(); ++LOD)
		{
			PresentationMesh->ShowAllMaterialSections(LOD);
			for (FName Slot : Model->HiddenMaterialSlots)
			{
				const int32 Material = PresentationMesh->GetMaterialIndex(Slot);
				if (Material != INDEX_NONE)
				{
					// 已按名称取得最终材质索引，跳过按 SectionIndex 的 LOD 重映射。
					PresentationMesh->ShowMaterialSection(Material, INDEX_NONE, false, LOD);
				}
			}
		}
		// 源网格隐藏后仍须刷新骨骼，目标姿势、武器挂点和命中物理需要同一帧的结果。
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		PresentationMesh->AddTickPrerequisiteComponent(GetMesh());
	}
	GetMesh()->SetVisibility(!bHasModel, false);
}

void AGinkgoCharacter::BeginPlay()
{
	Super::BeginPlay();
	ApplyCharacterModel();
	// 开局的枪由服务器放进它那一种的槽，复制给各端；客户端上槽可能已经复制下来（OnRep），这里再对一遍
	if (HasAuthority())
	{
		// 落脚听觉来自动画标记；权威端离开画面后仍需评估姿势，不能由是否渲染决定 AI 能否听到。
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		InitializeStartingEquipment();
	}
	ApplyCarriedWeapons();
	if (Damageable->GetIsReplicated()) { ApplyPlayerLifeState(/*bPlayDeathAudio*/ false); }
}

void AGinkgoCharacter::EnterDeathRagdoll(const bool bPlayDeathAudio)
{
	Flashbang->CancelThrow();
	if (MeshBeforeDeath.IsSet()) { return; }
	if (bPlayDeathAudio && CharacterAudio)
	{
		CharacterAudio->PlayDeathSound();
	}
	MeshBeforeDeath = GetMesh()->GetRelativeTransform();
	MeshProfileBeforeDeath = GetMesh()->GetCollisionProfileName();
	CapsuleCollisionBeforeDeath = GetCapsuleComponent()->GetCollisionEnabled();
	const FVector Velocity = GetVelocity();
	if (Traversal->IsActive()) { Traversal->Cancel(); }
	Weapons->InterruptForDeath();
	StopJumping();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetMesh()->SetAllBodiesSimulatePhysics(true);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetAllPhysicsLinearVelocity(Velocity);
	GetMesh()->WakeAllRigidBodies();
	// 布娃娃在物理阶段写回源骨骼，显示网格应读取本帧的最终姿势。
	PresentationMesh->SetTickGroup(TG_PostPhysics);
}

void AGinkgoCharacter::HandlePlayerLifeStateChanged()
{
	ApplyPlayerLifeState(/*bPlayDeathAudio*/ true);
}

void AGinkgoCharacter::ApplyPlayerLifeState(const bool bPlayDeathAudio)
{
	if (Damageable->IsDead())
	{
		EnterDeathRagdoll(bPlayDeathAudio);
	}
	else if (MeshBeforeDeath.IsSet())
	{
		GetMesh()->SetAllBodiesSimulatePhysics(false);
		GetMesh()->SetSimulatePhysics(false);
		PresentationMesh->SetTickGroup(TG_PrePhysics);
		GetMesh()->SetCollisionProfileName(MeshProfileBeforeDeath);
		GetMesh()->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepWorldTransform);
		GetMesh()->SetRelativeTransform(MeshBeforeDeath.GetValue(), false, nullptr, ETeleportType::TeleportPhysics);
		MeshBeforeDeath.Reset();
		GetCapsuleComponent()->SetCollisionEnabled(CapsuleCollisionBeforeDeath);
		GetCharacterMovement()->SetDefaultMovementMode();
		if (CharacterAudio) { CharacterAudio->ResetBreathState(); }
	}
}

void AGinkgoCharacter::InitializeStartingEquipment()
{
	// 正式玩家开局配装由 AGinkgoGameMode::GrantStartingLoadout 统一发放，
	// 角色基类不再自动生成调试靶子、地面武器或预览改装桌；AI 角色由 AGinkgoAICharacter 重写本方法。
}

AGinkgoPracticeTarget* AGinkgoCharacter::SpawnPracticeTarget(const FName Name, const FGinkgoTargetSpawn& Spawn)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const FVector Right = GetActorRightVector().GetSafeNormal2D();
	const double HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Above = GetActorLocation() + Forward * Spawn.Forward + Right * Spawn.Right;
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Above + FVector(0.0, 0.0, HalfHeight), Above - FVector(0.0, 0.0, HalfHeight + 500.0), ECC_Visibility,
		FCollisionQueryParams(SCENE_QUERY_STAT(GinkgoPracticeTarget), false, this));
	const FVector Ground = bHit ? FVector(Hit.ImpactPoint) : Above - FVector(0.0, 0.0, HalfHeight);
	// 胶囊高 180 cm：中心在地上 90 cm；面朝角色
	const FTransform Transform((-Forward).Rotation(), Ground + FVector(0.0, 0.0, 90.5));
	// 重名时引擎另起一个
	FActorSpawnParameters Params;
	Params.Name = Name;
	Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.bDeferConstruction = true;
	AGinkgoPracticeTarget* Target = World->SpawnActor<AGinkgoPracticeTarget>(AGinkgoPracticeTarget::StaticClass(), Transform, Params);
	if (!Target)
	{
		return nullptr;
	}
	Target->Configure(Spawn);
	Target->FinishSpawning(Transform);
	return Target;
}

UPoseableMeshComponent* AGinkgoCharacter::GetWeaponSlotComponent(const int32 Slot) const
{
	switch (Slot)
	{
	case 0:
		return WeaponMesh.Get();
	case 1:
		return SecondWeaponMesh.Get();
	case Ginkgo::Weapon::SidearmSlot:
		return SidearmMesh.Get();
	case Ginkgo::Weapon::HeavySlot:
		return HeavyWeaponMesh.Get();
	default:
		return nullptr;
	}
}

UStaticMeshComponent* AGinkgoCharacter::GetFeedRoundComponent(const int32 Slot) const
{
	switch (Slot)
	{
	case 0:
		return FeedRound.Get();
	case 1:
		return SecondFeedRound.Get();
	case Ginkgo::Weapon::SidearmSlot:
		return SidearmFeedRound.Get();
	case Ginkgo::Weapon::HeavySlot:
		return HeavyFeedRound.Get();
	default:
		return nullptr;
	}
}

UPoseableMeshComponent* AGinkgoCharacter::GetHeldWeaponMesh() const
{
	UPoseableMeshComponent* Component = GetWeaponSlotComponent(Weapons->GetHeldSlot());
	return Component && Component->GetSkinnedAsset() ? Component : nullptr;
}

bool AGinkgoCharacter::IsHeldWeaponInHand() const
{
	const auto* Gun = GetHeldWeaponMesh();
	return Gun && Gun->GetAttachParent() == GetMesh() && !IsWeaponStowed(Weapons->GetHeldSlot());
}

void AGinkgoCharacter::ApplyCarriedWeapons()
{
	if (!GetMesh()->GetSkeletalMeshAsset())
	{
		return;
	}
	AppliedWeapons.SetNum(UGinkgoWeaponComponent::SlotCount);
	AppliedBolts.SetNum(UGinkgoWeaponComponent::SlotCount);
	AppliedGunPoses.SetNum(UGinkgoWeaponComponent::SlotCount);
	for (int32 Slot = 0; Slot < UGinkgoWeaponComponent::SlotCount; ++Slot)
	{
		UGinkgoWeaponDefinition* Carried = Weapons->GetCarried(Slot);
		if (AppliedWeapons[Slot].Get() == Carried)
		{
			continue;
		}
		AppliedWeapons[Slot] = Carried;
		UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot);
		USkeletalMesh* GunMesh = Carried ? Carried->SkeletalMesh.LoadSynchronous() : nullptr;
		UE_CLOG(Carried && !GunMesh, LogGinkgoWeapon, Warning, TEXT("[Weapon] %s 的骨骼网格读不到，这把枪看不见"), *GetNameSafe(Carried));
		Component->SetSkinnedAssetAndUpdate(GunMesh);
		// 回到参考姿势（骨骼数相同时换网格不会自己回）：枪栓这一帧由 ApplyWeaponBolts 摆
		if (GunMesh)
		{
			Component->BoneSpaceTransforms = GunMesh->GetRefSkeleton().GetRefBonePose();
			Component->RefreshBoneTransforms();
		}
		AppliedBolts[Slot] = FGinkgoBoltPose();
		AppliedGunPoses[Slot] = FAppliedGunPose();
		// 换挂点收到一半枪没了（丢枪）：下一把不接着收
		GunAttachBlends[Slot].Remaining = 0.f;
		GunAttachBlends[Slot].bSettleRebase = false;
		UStaticMeshComponent* Feed = GetFeedRoundComponent(Slot);
		Feed->SetStaticMesh(Carried ? Carried->Rounds.LiveRound.LoadSynchronous() : nullptr);
		Feed->SetVisibility(false);
		if (Carried)
		{
			Weapons->PreloadFireAssets(*this, Slot, *Carried);
		}
		// 新放进槽的枪收着；要拿到手上的（开局、拔枪）由武器组件、拔枪的动作再换
		if (Carried)
		{
			AttachWeaponToStow(Slot);
		}
		Component->SetVisibility(Slot != HiddenWeaponSlot);
	}
}

void AGinkgoCharacter::ApplyGunPose(const int32 Slot, const UGinkgoWeaponDefinition& Carried, const USkeletalMesh& GunMesh, const FGinkgoMechanismState* State, const FGinkgoGunClip& Clip)
{
	const bool bActionOpen = State && State->Action == EGinkgoAction::Open;
	const bool bMagazineIn = !State || State->bMagazineIn;
	const bool bClip = Clip.Track && Clip.Weight > 0.f;
	// 片段里从供弹出来进膛的那一发：供弹空了、膛里也没有活弹时没有它（霰弹枪打空了再泵）。随枪机退出来的空壳：膛里是空壳时才有（压完膛空着时的泵动没有）。
	// 抛壳口装填的片段里这一发是手拿来的（bNextRoundInClip），照片段
	const bool bNextRoundShown = !State || !State->Feed.IsEmpty() || State->Chamber == EGinkgoChamber::Live || (bClip && Clip.bNextRoundInClip);
	const bool bSpentCaseShown = !State || State->Chamber == EGinkgoChamber::Spent;
	// 露在外面的弹链：剩下的不够时从后往前缩（MG3）
	const int32 FeedShown = State ? FMath::Min(State->Feed.Num(), Carried.VisibleFeedBones.Num()) : Carried.VisibleFeedBones.Num();
	const FGameplayTag ActiveRoundAmmo = State
		? (State->Chamber == EGinkgoChamber::Live && State->ChamberAmmo.IsValid() ? State->ChamberAmmo : Weapons->GetLoadAmmo(Slot))
		: Carried.AmmoType;
	FAppliedGunPose& Applied = AppliedGunPoses[Slot];
	// 不放片段时样子只跟着枪机、弹匣、那两发、露在外面的弹链及弹头涂装变；片段刚放完的这一帧（bClip 还是真）要摆回来
	if (Applied.Mesh.Get() == &GunMesh && Applied.MuzzleOffset == Weapons->GetMuzzleOffset(Slot) && Applied.RoundAmmo == ActiveRoundAmmo && Applied.bValid && !bClip && !Applied.bClip && Applied.bActionOpen == bActionOpen && Applied.bMagazineIn == bMagazineIn
		&& Applied.bNextRoundShown == bNextRoundShown && Applied.bSpentCaseShown == bSpentCaseShown && Applied.FeedShown == FeedShown)
	{
		return;
	}
	const bool bRoundAmmoChanged = !Applied.bValid || Applied.Mesh.Get() != &GunMesh || Applied.RoundAmmo != ActiveRoundAmmo;
	Applied = {true, bClip, bActionOpen, bMagazineIn, bNextRoundShown, bSpentCaseShown, FeedShown};
	Applied.Mesh = &GunMesh;
	Applied.MuzzleOffset = Weapons->GetMuzzleOffset(Slot);
	Applied.RoundAmmo = ActiveRoundAmmo;
	const UAnimSequence* Rest = bActionOpen && Carried.GunRestOpen ? Carried.GunRestOpen.Get() : Carried.GunRestClosed.Get();
	const FAnimExtractContext RestContext(bActionOpen && Carried.GunRestOpen ? static_cast<double>(Rest->GetPlayLength()) : 0.0);
	const FAnimExtractContext ClipContext(static_cast<double>(Clip.Time));
	const FReferenceSkeleton& RefSkeleton = GunMesh.GetRefSkeleton();
	const int32 Magazine = bMagazineIn ? INDEX_NONE : RefSkeleton.FindBoneIndex(Carried.MagazineBone);
	const int32 NextRound = bNextRoundShown || Carried.NextRoundBone.IsNone() ? INDEX_NONE : RefSkeleton.FindBoneIndex(Carried.NextRoundBone);
	const int32 SpentCase = bSpentCaseShown || Carried.SpentCaseBone.IsNone() ? INDEX_NONE : RefSkeleton.FindBoneIndex(Carried.SpentCaseBone);
	// 缩看得见的最后一发后面那一发就够（后面的挂在它上面），全缩一遍也一样
	TArray<int32, TInlineAllocator<8>> HiddenFeed;
	for (int32 Index = FeedShown; Index < Carried.VisibleFeedBones.Num(); ++Index)
	{
		HiddenFeed.Add(RefSkeleton.FindBoneIndex(Carried.VisibleFeedBones[Index]));
	}
	UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot);
	for (int32 Bone = 0; Bone < RefSkeleton.GetNum() && Bone < Component->BoneSpaceTransforms.Num(); ++Bone)
	{
		// Mutable 组合可能调整网格骨骼顺序，动画索引必须从序列骨架按名字查询。
		FTransform Transform = RefSkeleton.GetRefBonePose()[Bone];
		const int32 RestBone = Rest->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(RefSkeleton.GetBoneName(Bone));
		if (RestBone != INDEX_NONE) { Rest->GetBoneTransform(Transform, FSkeletonPoseBoneIndex(RestBone), RestContext, /*bUseRawData*/ false); }
		// 弹匣在片段之前缩：换弹的片段里新弹匣照片段出来
		if (Bone == Magazine)
		{
			Transform.SetScale3D(FVector::ZeroVector);
		}
		if (bClip)
		{
			FTransform InClip = Transform;
			const int32 ClipBone = Clip.Track->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(RefSkeleton.GetBoneName(Bone));
			if (ClipBone != INDEX_NONE) { Clip.Track->GetBoneTransform(InClip, FSkeletonPoseBoneIndex(ClipBone), ClipContext, /*bUseRawData*/ false); }
			Transform.Blend(Transform, InClip, Clip.Weight);
		}
		// 那两发、弹链在片段之后缩：泵动的片段里总有，没有的就是没有
		if (Bone == NextRound || Bone == SpentCase || HiddenFeed.Contains(Bone))
		{
			Transform.SetScale3D(FVector::ZeroVector);
		}
		Component->BoneSpaceTransforms[Bone] = Transform;
	}
	UGinkgoWeaponComponent::ApplyModulePose(Carried, Weapons->GetAttachments(Slot), *Component);
	if (bRoundAmmoChanged && Carried.RoundMaterialSlot >= 0 && Carried.RoundMaterialSlot < Component->GetNumMaterials())
	{
		const UGinkgoAmmoDefinition* AmmoDef = ActiveRoundAmmo.IsValid() ? UGinkgoAmmoDefinition::Find(ActiveRoundAmmo) : nullptr;
		UMaterialInterface* OverrideMat = AmmoDef && !AmmoDef->RoundMaterial.IsNull() ? AmmoDef->RoundMaterial.LoadSynchronous() : nullptr;
		if (!OverrideMat && GunMesh.GetMaterials().IsValidIndex(Carried.RoundMaterialSlot))
		{
			OverrideMat = GunMesh.GetMaterials()[Carried.RoundMaterialSlot].MaterialInterface;
		}
		Component->SetMaterial(Carried.RoundMaterialSlot, OverrideMat);
	}
	// 这一帧就要：组件自己的 Tick 可能已经过了
	Component->RefreshBoneTransforms();
}

void AGinkgoCharacter::ApplyWeaponBolts(const float ClipWeight, const FGinkgoBoltPose& Clip, const FGinkgoGunClip& GunClip)
{
	if (IsNetMode(NM_DedicatedServer))
	{
		return;
	}
	for (int32 Slot = 0; Slot < AppliedWeapons.Num(); ++Slot)
	{
		const UGinkgoWeaponDefinition* Carried = AppliedWeapons[Slot].Get();
		UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot);
		const USkeletalMesh* GunMesh = Component ? Cast<USkeletalMesh>(Component->GetSkinnedAsset()) : nullptr;
		if (!Carried || !GunMesh)
		{
			continue;
		}
		if (FGunAttachBlend& Blend = GunAttachBlends[Slot]; Blend.Remaining > 0.f)
		{
			// 捡枪的收拢：Control Rig 要等持枪的快照换成这把枪才把挂点（虚拟骨骼）摆到收拢开始的地方（抓住以后两帧：快照在控制器里先写，
			// 手上的槽后换）。在那之前枪停在地上那里，不往挂点（还在空手时的胯边）收；轮到的这一帧（同 Control Rig 一样看快照）枪还在上一帧画的
			// 地方，偏移按挂点这一帧要去的地方重算
			const bool bSettling = Weapons->GetPickupSettleAlpha() < 1.f;
			if (Blend.bSettleRebase && bSettling && Slot == Weapons->GetHeldSlot() && Weapons->GetHoldState().Weapon == Carried)
			{
				Blend.bSettleRebase = false;
				Blend.From = Component->GetComponentTransform().GetRelativeTransform(Blend.Base * MakePickupSettleAttach(*Carried));
				Blend.Remaining = Blend.Duration;
				Component->SetRelativeTransform(Blend.From * Blend.Base);
			}
			else if (!Blend.bSettleRebase || !bSettling)
			{
				Blend.bSettleRebase = false;
				Blend.Remaining = FMath::Max(Blend.Remaining - GetWorld()->GetDeltaSeconds(), 0.f);
				FTransform Delta;
				Delta.Blend(FTransform::Identity, Blend.From, Ginkgo::Weapon::EaseProgress(Blend.Remaining / Blend.Duration));
				Component->SetRelativeTransform(Delta * Blend.Base);
			}
		}
		const FGinkgoMechanismState* State = Weapons->GetMechanismState(Slot);
		const FGinkgoBoltPose Pose = Ginkgo::Weapon::BlendBoltPose(State && State->Action == EGinkgoAction::Open, Slot == Weapons->GetHeldSlot() ? ClipWeight : 0.f, Clip);
		// 弹仓最上面那一发：枪机开着才看得见，枪栓推回来时被推着走；推到底（ActionClose）就进了膛。弹匣自己有那一发的枪不画
		UStaticMeshComponent* Feed = GetFeedRoundComponent(Slot);
		const bool bFeedShown = State && State->Action == EGinkgoAction::Open && !State->Feed.IsEmpty() && !Carried->Rounds.FeedTop.IsZero() && Slot != HiddenWeaponSlot;
		if (bFeedShown)
		{
			Feed->SetRelativeTransform(Ginkgo::Weapon::MakeFeedRoundTransform(Carried->Rounds, Carried->Bolt, Pose.Travel));
		}
		if (Feed->IsVisible() != bFeedShown)
		{
			Feed->SetVisibility(bFeedShown);
		}
		if (Carried->GunRestClosed)
		{
			AppliedBolts[Slot] = Pose;
			ApplyGunPose(Slot, *Carried, *GunMesh, State, Slot == Weapons->GetHeldSlot() ? GunClip : FGinkgoGunClip());
			continue;
		}
		if (Pose == AppliedBolts[Slot])
		{
			continue;
		}
		AppliedBolts[Slot] = Pose;
		const FGinkgoBoltRig& Rig = Carried->Bolt;
		const FReferenceSkeleton& RefSkeleton = GunMesh->GetRefSkeleton();
		// 先摆父骨骼：SetBoneTransformByName 按父骨骼现在的位置换算
		if (const int32 Carrier = RefSkeleton.FindBoneIndex(Rig.CarrierBone); Carrier != INDEX_NONE)
		{
			Component->SetBoneTransformByName(Rig.CarrierBone, Ginkgo::Weapon::MakeBoltCarrierTransform(Ginkgo::Weapon::GetRefPoseComponentTransform(RefSkeleton, Carrier), Rig, Pose),
				EBoneSpaces::ComponentSpace);
		}
		if (const int32 Handle = RefSkeleton.FindBoneIndex(Rig.HandleBone); Handle != INDEX_NONE)
		{
			Component->SetBoneTransformByName(Rig.HandleBone, Ginkgo::Weapon::MakeBoltHandleTransform(Ginkgo::Weapon::GetRefPoseComponentTransform(RefSkeleton, Handle), Rig, Pose),
				EBoneSpaces::ComponentSpace);
		}
		// 这一帧就要：组件自己的 Tick 可能已经过了
		Component->RefreshBoneTransforms();
	}
	// 第一人称开镜（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：拉栓时拉机柄扳起来、往后拉，手到眼前几厘米。相机按它让眼睛离开瞄具；
	// 写这一帧的样子（相机在所有 Actor 更新之后才读）。只有本地控制的角色有相机
	if (IsLocallyControlled())
	{
		if (UGinkgoCameraContextComponent* Context = UGinkgoCameraContextComponent::Find(this))
		{
			const FGinkgoBoltPose Bolt = GetWeaponBolt(Weapons->GetHeldSlot());
			Context->SetScalar(FWeaponTags::Weapon_Value_SightClearance, FMath::Max(Bolt.Lift, Bolt.Travel));
		}
	}
}

void AGinkgoCharacter::BeginGunAttachBlend(const int32 Slot, const FTransform& WorldBefore)
{
	UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot);
	if (!Component)
	{
		return;
	}
	FGunAttachBlend& Blend = GunAttachBlends[Slot];
	Blend.Base = Component->GetRelativeTransform();
	Blend.From = WorldBefore.GetRelativeTransform(Component->GetComponentTransform());
	Blend.Remaining = GunAttachBlendTime;
	Blend.Duration = GunAttachBlendTime;
	Blend.bSettleRebase = false;
	Component->SetRelativeTransform(Blend.From * Blend.Base);
}

void AGinkgoCharacter::AttachPickedWeapon(const int32 Slot, const FTransform& WorldBefore, const float BlendTime)
{
	UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot);
	if (!Component)
	{
		return;
	}
	AttachWeaponToHand(Slot);
	FGunAttachBlend& Blend = GunAttachBlends[Slot];
	Blend.Base = Component->GetRelativeTransform();
	Blend.From = WorldBefore.GetRelativeTransform(Component->GetComponentTransform());
	Blend.Remaining = BlendTime;
	Blend.Duration = FMath::Max(BlendTime, UE_KINDA_SMALL_NUMBER);
	// 收拢（UGinkgoWeaponComponent::GetPickupSettleAlpha）：挂点还在空手时的地方，等 Control Rig 接手再收（ApplyWeaponBolts）
	Blend.bSettleRebase = Weapons->GetPickupSettleAlpha() < 1.f && Weapons->GetCarried(Slot) && GetMesh()->GetBoneIndex(PickupRightHandBone) != INDEX_NONE;
	Component->SetRelativeTransform(Blend.From * Blend.Base);
}

FGinkgoPickupSettleStart AGinkgoCharacter::CapturePickupSettleStart(const FTransform& GunWorld, const FVector& ItemInHand) const
{
	const FQuat ToComponent = GetMesh()->GetComponentQuat().Inverse();
	FGinkgoPickupSettleStart Start;
	Start.GunRotation = ToComponent * GunWorld.GetRotation();
	Start.HandRotation = GetMesh()->GetBoneIndex(PickupRightHandBone) != INDEX_NONE
		? GetMesh()->GetBoneQuaternion(PickupRightHandBone, EBoneSpaces::ComponentSpace)
		: Start.GunRotation;
	Start.ItemInHand = ItemInHand;
	return Start;
}

FTransform AGinkgoCharacter::MakePickupSettleAttach(const UGinkgoWeaponDefinition& Carried) const
{
	// 收拢开始的地方与 Control Rig 同一个算法（世界）：抓住的那一刻记在网格的坐标里，换回世界；角色的朝向就是胶囊的
	const FQuat ToWorld = GetMesh()->GetComponentQuat();
	FGinkgoPickupSettleStart Start = Weapons->GetPickupSettleStart();
	Start.GunRotation = ToWorld * Start.GunRotation;
	Start.HandRotation = ToWorld * Start.HandRotation;
	return Ginkgo::Weapon::BlendPickupSettle(Start, GetActorQuat(), GetMesh()->GetSocketTransform(PickupRightHandBone), FTransform::Identity, Carried.LoweredHands.Right, 0.f).Gun;
}

void AGinkgoCharacter::SetWeaponSlotHidden(const int32 Slot, const bool bHide)
{
	if (bHide)
	{
		HiddenWeaponSlot = Slot;
	}
	else if (HiddenWeaponSlot == Slot)
	{
		HiddenWeaponSlot = INDEX_NONE;
	}
	if (UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot))
	{
		Component->SetVisibility(!bHide);
		Weapons->RefreshAttachmentMeshes();
	}
}

void AGinkgoCharacter::AttachWeaponToHand(const int32 Slot, const bool bBlend)
{
	UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot);
	if (!Component)
	{
		return;
	}
	// 虚拟骨骼 VB spine_05_ik_hand_gun 是 spine_05 的子骨骼，跟着上半身走：上半身按网格空间分层混合以后，ik_hand_gun 还停在
	// 层的姿势（站着的）那个高度，蹲着时比手高约 45 cm（Docs/Design/Weapons.md「枪跟着身体」）
	FName Bone = UGinkgoDeveloperSettings::Get()->WeaponHandBone;
	if (GetMesh()->GetBoneIndex(Bone) == INDEX_NONE)
	{
		static bool bWarned = false;
		UE_CLOG(!bWarned, LogGinkgoWeapon, Warning, TEXT("[Weapon] 网格上没有 %s（导入命令 -VirtualBones 加），枪挂 ik_hand_gun"), *Bone.ToString());
		bWarned = true;
		Bone = TEXT("ik_hand_gun");
	}
	const FTransform Before = Component->GetComponentTransform();
	const bool bFromStow = IsWeaponStowed(Slot);
	Component->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
	Component->SetRelativeTransform(FTransform::Identity);
	if (bBlend)
	{
		BeginGunAttachBlend(Slot, Before);
		if (bFromStow && Component->GetAttachSocketName() == Bone)
		{
			Weapons->PlayContactShake(Slot, Ginkgo::WeaponEvents::InHand);
		}
	}
}

void AGinkgoCharacter::AttachWeaponToStow(const int32 Slot, const bool bBlend)
{
	UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot);
	if (!Component)
	{
		return;
	}
	const FTransform Before = Component->GetComponentTransform();
	const bool bFromHand = Weapons->GetHeldSlot() == Slot && !IsWeaponStowed(Slot);
	Component->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, GetWeaponStowBone(Slot));
	Component->SetRelativeTransform(GetWeaponStowOffset(Slot));
	if (bBlend)
	{
		BeginGunAttachBlend(Slot, Before);
		if (bFromHand && IsWeaponStowed(Slot))
		{
			Weapons->PlayContactShake(Slot, Ginkgo::WeaponEvents::OnBack);
		}
	}
}

FName AGinkgoCharacter::GetWeaponStowBone(const int32 Slot) const
{
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	return Ginkgo::Weapon::GetSlotKind(Slot) == EGinkgoWeaponSlotKind::Sidearm ? Settings->SidearmStowBone : Settings->WeaponBackBone;
}

FTransform AGinkgoCharacter::GetWeaponStowOffset(const int32 Slot) const
{
	return Ginkgo::Weapon::GetSlotKind(Slot) == EGinkgoWeaponSlotKind::Sidearm ? UGinkgoDeveloperSettings::Get()->SidearmStowOffset : GetWeaponBackOffset(Slot);
}

FTransform AGinkgoCharacter::GetWeaponBackOffset(const int32 Slot) const
{
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	return Slot == 0 ? Settings->WeaponBackOffset : Settings->SecondWeaponBackShift * Settings->WeaponBackOffset;
}

bool AGinkgoCharacter::IsWeaponStowed(const int32 Slot) const
{
	const UPoseableMeshComponent* Component = GetWeaponSlotComponent(Slot);
	return !Component || Component->GetAttachSocketName() == GetWeaponStowBone(Slot);
}

FVector AGinkgoCharacter::GetShoulderLocation(const EGinkgoHand Hand) const
{
	static const FName LeftShoulder(TEXT("upperarm_l"));
	static const FName RightShoulder(TEXT("upperarm_r"));
	return GetMesh()->GetBoneLocation(Hand == EGinkgoHand::Left ? LeftShoulder : RightShoulder);
}

void AGinkgoCharacter::SetRoundInHand(const bool bInHand)
{
	if (bInHand == bRoundInHand)
	{
		return;
	}
	bRoundInHand = bInHand;
	const UGinkgoWeaponDefinition* Held = Weapons->GetHeldWeapon();
	if (bInHand && Held)
	{
		ShowHandItem(EGinkgoHand::Right, Held->Rounds.LiveRound.LoadSynchronous(), Held->Rounds.InHand);
	}
	else
	{
		HideHandItem(EGinkgoHand::Right);
	}
}

bool AGinkgoCharacter::IsFeedRoundShown(const int32 Slot) const
{
	return GetFeedRoundComponent(Slot)->IsVisible();
}

FVector AGinkgoCharacter::GetFeedRoundLocation(const int32 Slot) const
{
	return GetFeedRoundComponent(Slot)->GetComponentLocation();
}

void AGinkgoCharacter::ShowHandItem(const EGinkgoHand Hand, UStaticMesh* ItemMesh, const FTransform& InHand)
{
	UStaticMeshComponent* Item = Hand == EGinkgoHand::Left ? LeftHandItem : RightHandItem;
	Item->SetStaticMesh(ItemMesh);
	Item->SetRelativeTransform(InHand);
	Item->SetVisibility(ItemMesh != nullptr);
}

void AGinkgoCharacter::HideHandItem(const EGinkgoHand Hand)
{
	UStaticMeshComponent* Item = Hand == EGinkgoHand::Left ? LeftHandItem : RightHandItem;
	Item->SetVisibility(false);
	Item->SetStaticMesh(nullptr);
}

bool AGinkgoCharacter::IsHandItemShown(const EGinkgoHand Hand) const
{
	const UStaticMeshComponent* Item = Hand == EGinkgoHand::Left ? LeftHandItem : RightHandItem;
	return Item->GetStaticMesh() && Item->IsVisible();
}

void AGinkgoCharacter::SetHandReach(const EGinkgoHand Hand, const FGinkgoHandReach& Reach)
{
	if (UGinkgoAnimInstance* AnimInstance = Cast<UGinkgoAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AnimInstance->SetHandReach(Hand, Reach);
	}
}

void AGinkgoCharacter::ClearHandReach(const EGinkgoHand Hand)
{
	if (UGinkgoAnimInstance* AnimInstance = Cast<UGinkgoAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AnimInstance->ClearHandReach(Hand);
	}
}

void AGinkgoCharacter::FreezeHandReach(const EGinkgoHand Hand)
{
	if (UGinkgoAnimInstance* AnimInstance = Cast<UGinkgoAnimInstance>(GetMesh()->GetAnimInstance()))
	{
		AnimInstance->FreezeHandReach(Hand);
	}
}

UGinkgoAnimAction* AGinkgoCharacter::MakeStowWeaponAction(const int32 Slot)
{
	const TWeakObjectPtr<AGinkgoCharacter> WeakThis(this);
	return UGinkgoAnimAction::Make(this, TEXT("StowWeapon"), [WeakThis, Slot](FGinkgoAnimActionSetup& Setup)
	{
		const AGinkgoCharacter* Owner = WeakThis.Get();
		const UGinkgoWeaponDefinition* Carried = Owner ? Owner->Weapons->GetCarried(Slot) : nullptr;
		UAnimMontage* Montage = Carried ? Carried->StowMontage.Get() : nullptr;
		Setup.Play(Montage);
		Setup.On(Ginkgo::WeaponEvents::OnBack, [WeakThis, Slot](FGinkgoAnimActionEvent&)
		{
			if (AGinkgoCharacter* Character = WeakThis.Get())
			{
				Character->AttachWeaponToStow(Slot, /*bBlend*/ true);
				// 手上没枪了：层在收枪的第 4 到 13 帧已经让开（Weapon_HoldPose），这里淡完撤掉。收枪的最后一帧就是 GASP 的徒手待机
				Character->Weapons->SetHeldSlot(INDEX_NONE);
			}
		}, EGinkgoMissedEvent::Skip);
		// 接下来拔背上的另一把：手在肩后松开枪的那一刻就是接下一把的地方，收枪到这里就算完，不放手落回腰边的那一段。枪自己落到背上（挂点的接缝收过去）
		Setup.OnTick([WeakThis, Slot, bDecided = false](FGinkgoAnimActionContext& Context, float) mutable
		{
			AGinkgoCharacter* Character = WeakThis.Get();
			if (!Character || bDecided || Context.GetPosition() < UGinkgoDeveloperSettings::Get()->WeaponSwap.StowRelease)
			{
				return;
			}
			bDecided = true;
			if (Character->Weapons->BeginDrawFromBack(Slot))
			{
				Character->AttachWeaponToStow(Slot, /*bBlend*/ true);
				Character->Weapons->SetHeldSlot(INDEX_NONE);
				Context.Complete();
			}
		});
	})->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityWeaponSwap);
}

UGinkgoAnimAction* AGinkgoCharacter::MakeDrawWeaponAction(const int32 Slot)
{
	const TWeakObjectPtr<AGinkgoCharacter> WeakThis(this);
	return UGinkgoAnimAction::Make(this, TEXT("DrawWeapon"), [WeakThis, Slot](FGinkgoAnimActionSetup& Setup)
	{
		AGinkgoCharacter* Owner = WeakThis.Get();
		const UGinkgoWeaponDefinition* Carried = Owner ? Owner->Weapons->GetCarried(Slot) : nullptr;
		UAnimMontage* DrawMontage = Carried ? Carried->DrawMontage.Get() : nullptr;
		// 捡枪抓住了：枪已经在手里，从持枪的层盖满的那一刻接上，枪从地上直接到低持（Docs/Design/Interaction.md「丢枪与捡枪」）
		const bool bFromPickup = Owner && Owner->Weapons->ConsumeDrawFromPickup();
		// 捡枪抓住了、枪在右手里：不放拔枪的片段（它的最后几帧是枪从肩上下来），按收拢剩下的时间占着右臂。姿势按收拢的进度混：
		// 拾取的片段同样淡出，持枪的层盖上来，Control Rig 把枪从右手带到持枪的位置（FGinkgoWeaponRigData::PickupSettle）
		if (const float Settle = bFromPickup && Carried ? Owner->Weapons->GetPickupSettleRemaining() : 0.f; Settle > 0.f)
		{
			Setup.HoldFor(Settle);
			Owner->Weapons->SetHeldSlot(Slot);
			const UAnimMontage* Pickup = UGinkgoDeveloperSettings::Get()->PickupMontageRight.Get();
			if (UAnimInstance* Anim = Owner->GetMesh()->GetAnimInstance(); Anim && Pickup)
			{
				// 先停：执行器在应用阶段最后再停它时它已经在淡出，不再改淡出的时长。同持枪的层盖上来一样快（UGinkgoAnimInstance::PickupHoldShare）：
				// 右臂的槽也管枪挂的虚拟骨骼，片段里它停在胯边、朝上；淡得慢的话收拢要去的持枪位置一直混着它，枪口先往上翘（11°）
				Anim->Montage_Stop(UGinkgoAnimInstance::PickupHoldShare * Settle, Pickup);
			}
			Setup.OnFinished([WeakThis](const FGinkgoAnimActionResult&)
			{
				if (AGinkgoCharacter* Character = WeakThis.Get())
				{
					Character->Weapons->EndPickupSettle();
				}
			});
			return;
		}
		// 接在收枪后面（BeginDrawFromBack）：手已经在肩后接枪的地方，不放手伸上来的那一段，枪从背上直接到手里（挂点的接缝收过去，片段的 InHand 在开始的时刻之前）
		const bool bFromBack = Owner && !bFromPickup && DrawMontage && Owner->Weapons->ConsumeDrawFromBack(Slot);
		const FGinkgoWeaponSwapTiming& Swap = UGinkgoDeveloperSettings::Get()->WeaponSwap;
		const float StartPosition = bFromBack ? Swap.DrawStart : bFromPickup && DrawMontage ? Ginkgo::Animation::GetHoldPoseFullTime(*DrawMontage) : 0.f;
		Setup.Play(DrawMontage, 1.f, NAME_None, StartPosition, bFromBack ? Swap.Blend : -1.f);
		// 开始时就链上层，层跟着片段的曲线让开，第 16 帧才开始盖（Weapon_HoldPose，蒙太奇只动右臂，盖不住别的）；拔枪的最后一帧就是低持，淡出时接得上
		if (Owner && Carried)
		{
			Owner->Weapons->SetHeldSlot(Slot);
			if (bFromBack)
			{
				Owner->AttachWeaponToHand(Slot, /*bBlend*/ true);
			}
		}
		Setup.On(Ginkgo::WeaponEvents::InHand, [WeakThis, Slot](FGinkgoAnimActionEvent&)
		{
			if (AGinkgoCharacter* Character = WeakThis.Get())
			{
				Character->AttachWeaponToHand(Slot, /*bBlend*/ true);
			}
		}, EGinkgoMissedEvent::Skip);
		// 没拔出来（InHand 之前被打断、取消）：枪还在背上，换回徒手
		Setup.OnFinished([WeakThis, Slot](const FGinkgoAnimActionResult&)
		{
			if (AGinkgoCharacter* Character = WeakThis.Get(); Character && Character->IsWeaponStowed(Slot))
			{
				Character->Weapons->SetHeldSlot(INDEX_NONE);
			}
		});
	})->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityWeaponSwap);
}

void AGinkgoCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ThisClass, ReplicatedAnimState, COND_SimulatedOnly);
}

void AGinkgoCharacter::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	ReplicatedAnimState.bSliding = GetGinkgoMovement()->IsSliding();
	ReplicatedAnimState.bSprinting = GetGinkgoMovement()->IsSprintFacing();
}

void AGinkgoCharacter::Move(const FVector2D& Input)
{
	if (!Controller)
	{
		return;
	}
	const FRotationMatrix YawMatrix(FRotator(0.f, Controller->GetControlRotation().Yaw, 0.f));
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::X), Input.Y);
	AddMovementInput(YawMatrix.GetUnitAxis(EAxis::Y), Input.X);
}

void AGinkgoCharacter::SetWantsToSprint(const bool bWantsToSprint)
{
	GetGinkgoMovement()->SetWantsToSprint(bWantsToSprint);
}

void AGinkgoCharacter::AddDirectionPresses(const uint8 Presses)
{
	GetGinkgoMovement()->AddDirectionPresses(Presses);
}

void AGinkgoCharacter::SetDirectionKeys(const FIntPoint Keys)
{
	GetGinkgoMovement()->SetDirectionKeys(Keys);
}

void AGinkgoCharacter::OnStartCrouch(const float HalfHeightAdjust, const float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	MeshBaseRelativeZ = BaseTranslationOffset.Z;
	if (CrouchMeshBlend.IsActive())
	{
		ApplyCrouchMeshBlend();
	}
	FitPlaceholderToCapsule();
}

void AGinkgoCharacter::OnEndCrouch(const float HalfHeightAdjust, const float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	MeshBaseRelativeZ = BaseTranslationOffset.Z;
	if (CrouchMeshBlend.IsActive())
	{
		ApplyCrouchMeshBlend();
	}
	FitPlaceholderToCapsule();
}

double AGinkgoCharacter::GetMeshBaseHeight() const
{
	return GetCapsuleComponent()->GetComponentLocation().Z + MeshBaseRelativeZ;
}

void AGinkgoCharacter::AbsorbMeshBaseJump(const double Jump)
{
	CrouchMeshBlend.Absorb(Jump);
	if (CrouchMeshBlend.IsActive())
	{
		ApplyCrouchMeshBlend();
	}
}

void AGinkgoCharacter::StepCrouchMeshBlend(const float DeltaTime)
{
	if (CrouchMeshBlend.IsActive() && !MeshBeforeDeath.IsSet())
	{
		CrouchMeshBlend.Step(UGinkgoDeveloperSettings::Get()->CrouchMeshBlendOmega, DeltaTime);
		ApplyCrouchMeshBlend();
	}
}

void AGinkgoCharacter::ApplyCrouchMeshBlend()
{
	// 布娃娃期间网格由物理接管，空中蹲起残余的平滑偏移不能把它拉回胶囊。
	if (MeshBeforeDeath.IsSet()) { return; }
	const double Height = MeshBaseRelativeZ + CrouchMeshBlend.GetOffset();
	BaseTranslationOffset.Z = Height;
	FVector Location = GetMesh()->GetRelativeLocation();
	Location.Z = Height;
	GetMesh()->SetRelativeLocation(Location);
}

bool AGinkgoCharacter::CanJumpInternal_Implementation() const
{
	// 父类蹲着时不让跳；Source 里蹲着也能跳（与 UGinkgoCharacterMovementComponent::CanAttemptJump 对应）
	return JumpIsAllowedInternal();
}

bool AGinkgoCharacter::CanJumpWhileFalling() const
{
	const UGinkgoCharacterMovementComponent* Movement = GetGinkgoMovement();
	return Movement && Movement->CanJumpWhileFalling();
}

void AGinkgoCharacter::FitPlaceholderToCapsule()
{
	// 引擎的基础形状是 100 cm 见方、轴心在中心
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float Radius = Capsule->GetUnscaledCapsuleRadius();
	const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight();
	PlaceholderBody->SetRelativeScale3D(FVector(Radius * 2.f, Radius * 2.f, HalfHeight * 2.f) / 100.f);
	PlaceholderVisor->SetRelativeLocation(FVector(Radius, 0.f, HalfHeight * 0.6f));
	PlaceholderVisor->SetRelativeScale3D(FVector(0.2f, 0.5f, 0.15f));
}

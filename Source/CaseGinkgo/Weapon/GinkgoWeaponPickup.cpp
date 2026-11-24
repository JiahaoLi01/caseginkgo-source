#include "Weapon/GinkgoWeaponPickup.h"

#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimSequence.h"
#include "Character/GinkgoCharacter.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

AGinkgoWeaponPickup::AGinkgoWeaponPickup()
{
	// 丢出去的枪物理落地：服务器算，位置复制给各端
	SetReplicatingMovement(true);

	GunPose = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("GunPose"));
	GunPose->SetupAttachment(Mesh);
	GunPose->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GunPose->SetCanEverAffectNavigation(false);

	FeedRound = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FeedRound"));
	FeedRound->SetupAttachment(GunPose);
	FeedRound->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FeedRound->SetCanEverAffectNavigation(false);
	FeedRound->SetCastShadow(false);
	FeedRound->SetVisibility(false);

	// 身体创建之前就要定：落定了要收到通知
	Mesh->BodyInstance.bGenerateWakeEvents = true;

	// 枪常常躺在脚边的地上，球心在胶囊中间：比子弹盒大一些，水平方向约 2 m 内能捡
	PickupRadius = 220.f;
	// 先收枪再伸手，服务器占住以后要等得更久
	ClaimedLifeSpan = 6.f;
}

void AGinkgoWeaponPickup::SetWeapon(UGinkgoWeaponDefinition* InWeapon)
{
	if (Weapon != InWeapon) { InstalledAttachments = InWeapon ? InWeapon->GetDefaultAttachments() : FGinkgoInstalledAttachments(); }
	Weapon = InWeapon;
	ApplyWeapon();
}

void AGinkgoWeaponPickup::SetAttachments(const FGinkgoInstalledAttachments& Installed)
{
	InstalledAttachments = Installed;
	RefreshAttachmentMeshes();
}

void AGinkgoWeaponPickup::RefreshAttachmentMeshes()
{
	if (Weapon && Weapon->Modularity && InstalledAttachments.Parts.IsEmpty()) { InstalledAttachments = Weapon->GetDefaultAttachments(); }
	AttachmentMeshes.Update(Weapon, InstalledAttachments, *GunPose);
}

void AGinkgoWeaponPickup::SetMechanismState(const FGinkgoMechanismState& State)
{
	MechanismState = State;
	bHasMechanismState = true;
	ApplyMechanismPose();
}

void AGinkgoWeaponPickup::Launch(const FVector& Velocity, const FVector& AngularVelocity)
{
	bLaunched = true;
	ApplyLaunchedCollision();
	const UBodySetup* Body = Mesh->GetBodySetup();
	if (!Body || Body->AggGeom.GetElementCount() == 0)
	{
		// 静态网格没有碰撞（没跑过 Scripts/Assets/gun_collision.py）：不抛，直接放到脚下的地上
		UStaticMesh* WeaponMesh = Mesh->GetStaticMesh();
		const FVector Start = GetActorLocation();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoWeaponLand), false, this);
		const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0, 0.0, 1000.0), ECC_Visibility, Params);
		if (WeaponMesh && bHit)
		{
			SetActorTransform(MakeLyingTransform(*WeaponMesh, Hit.ImpactPoint, GetActorForwardVector()));
		}
		UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] %s 的静态网格没有碰撞，不抛，放到地上"), *GetNameSafe(WeaponMesh));
		return;
	}
	LaunchTime = GetWorld()->GetTimeSeconds();
	Mesh->OnComponentSleep.AddUniqueDynamic(this, &ThisClass::OnMeshSettled);
	Mesh->SetSimulatePhysics(true);
	Mesh->SetPhysicsLinearVelocity(Velocity);
	Mesh->SetPhysicsAngularVelocityInDegrees(AngularVelocity);
}

FTransform AGinkgoWeaponPickup::MakeLyingTransform(const UStaticMesh& InMesh, const FVector& Ground, const FVector& Direction)
{
	const FBox Bounds = InMesh.GetBoundingBox();
	const FVector Extent = Bounds.GetExtent();
	// 网格自己的三条轴：最长的（枪身）、最薄的（枪侧躺时朝上），剩下的一条
	const int32 Longest = Extent.X >= Extent.Y ? (Extent.X >= Extent.Z ? 0 : 2) : (Extent.Y >= Extent.Z ? 1 : 2);
	int32 Thinnest = Extent.X <= Extent.Y ? (Extent.X <= Extent.Z ? 0 : 2) : (Extent.Y <= Extent.Z ? 1 : 2);
	if (Thinnest == Longest)
	{
		Thinnest = (Longest + 1) % 3;
	}
	const int32 Middle = 3 - Longest - Thinnest;

	// 最长的一边对准 Direction、最薄的一边朝上；中间那条按网格三条轴的左右手补上，保证是旋转（行列式为 1）
	const FVector Along = FVector(Direction.X, Direction.Y, 0.0).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	const FVector Up = FVector::UpVector;
	const bool bEvenPermutation = (Longest + 1) % 3 == Middle;
	const FVector Side = bEvenPermutation ? FVector::CrossProduct(Up, Along) : FVector::CrossProduct(Along, Up);
	FVector Rows[3];
	Rows[Longest] = Along;
	Rows[Middle] = Side;
	Rows[Thinnest] = Up;
	const FQuat Rotation(FMatrix(Rows[0], Rows[1], Rows[2], FVector::ZeroVector));

	// 包围盒的中心在地面上方最薄的一边的半宽处：最低点贴地
	const FVector Center = Ground + Up * Extent[Thinnest];
	return FTransform(Rotation, Center - Rotation.RotateVector(Bounds.GetCenter()));
}

EGinkgoInteractionAvailability AGinkgoWeaponPickup::GetLocalAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
{
	const UGinkgoWeaponComponent* Weapons = Context.Character ? Context.Character->GetWeapons() : nullptr;
	const UGinkgoAnimActionComponent* Actions = Context.Character ? Context.Character->GetAnimActions() : nullptr;
	if (Weapons && Weapons->IsPickingUpWeapon())
	{
		OutReason = NSLOCTEXT("Ginkgo", "WeaponPickupBusy", "正在捡枪");
		return EGinkgoInteractionAvailability::Blocked;
	}
	// 手上的枪会自动收起来（或者丢掉），不算占着；别的占着右手的（比如以后爬梯子）才不能捡
	if (!Actions || (Weapons && Weapons->GetHeldWeapon()) || IsHandAvailable(*Actions, EGinkgoHand::Right))
	{
		return EGinkgoInteractionAvailability::Available;
	}
	OutReason = NSLOCTEXT("Ginkgo", "WeaponPickupHandBusy", "右手没空");
	return EGinkgoInteractionAvailability::Blocked;
}

void AGinkgoWeaponPickup::BeginLocalInteract(const FGinkgoInteractionContext& Context)
{
	AGinkgoCharacter* Character = Context.Character;
	UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
	if (!Weapons || !Weapon)
	{
		return;
	}
	PrepareLocalInteract(*Character);
	// 腾地方、收枪都在武器组件里；右手空了它再回来叫 StartLocalReach
	Weapons->BeginWeaponPickup(*this);
}

void AGinkgoWeaponPickup::OnInteractResult(const FGinkgoInteractionContext& Context, const EGinkgoInteractionResult Result)
{
	Super::OnInteractResult(Context, Result);
	if (Result != EGinkgoInteractionResult::Accepted && Context.Character)
	{
		Context.Character->GetWeapons()->CancelWeaponPickup(*this);
	}
}

void AGinkgoWeaponPickup::GrantLocal(AGinkgoCharacter& Character)
{
	Character.GetWeapons()->FinishWeaponPickup(*this, Mesh->GetComponentTransform());
}

EGinkgoInteractionAvailability AGinkgoWeaponPickup::GetPickupAvailability(const FGinkgoInteractionContext& Context, FText& OutReason) const
{
	// 槽满了也能捡：占着那个槽的枪掉到地上（Ginkgo::Weapon::PlanWeaponPickup）
	return Weapon ? EGinkgoInteractionAvailability::Available : EGinkgoInteractionAvailability::Hidden;
}

void AGinkgoWeaponPickup::OnServerClaimed(const FGinkgoInteractionContext& Context)
{
	UGinkgoWeaponComponent* Weapons = Context.Character ? Context.Character->GetWeapons() : nullptr;
	const int32 Slot = Weapons && Weapon ? Weapons->FindFreeSlot(Weapon->SlotKind) : INDEX_NONE;
	UE_CLOG(Slot == INDEX_NONE, LogGinkgoWeapon, Warning, TEXT("[Weapon] %s 被捡起，服务器上没有空槽：本地腾地方的请求没到？"), *GetName());
	if (Slot != INDEX_NONE)
	{
		Weapons->SetCarried(Slot, Weapon);
		for (UGinkgoWeaponAttachment* Part : InstalledAttachments.Parts)
		{
			Weapons->InstallAttachment(Slot, Part);
		}
	}
}

void AGinkgoWeaponPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyWeapon();
}

void AGinkgoWeaponPickup::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyWeapon();
}

void AGinkgoWeaponPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Weapon);
	DOREPLIFETIME(ThisClass, InstalledAttachments);
	DOREPLIFETIME(ThisClass, MechanismState);
	DOREPLIFETIME(ThisClass, bHasMechanismState);
	DOREPLIFETIME(ThisClass, bLaunched);
}

void AGinkgoWeaponPickup::OnRep_Weapon()
{
	ApplyWeapon();
}

void AGinkgoWeaponPickup::OnRep_MechanismState()
{
	ApplyMechanismPose();
}

void AGinkgoWeaponPickup::OnRep_Launched()
{
	ApplyLaunchedCollision();
}

void AGinkgoWeaponPickup::OnMeshSettled(UPrimitiveComponent* Component, FName BoneName)
{
	const FVector Location = GetActorLocation();
	const FVector Grasp = GetGraspLocation();
	const FVector Up = GetActorRotation().UnrotateVector(FVector::UpVector);
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 地上的枪落定：%s，丢出去 %.2f s，x=%.1f y=%.1f z=%.1f，抓的点 x=%.1f y=%.1f z=%.1f，枪自己朝上的方向 (%.2f, %.2f, %.2f)，世界时间 %.4f s"), *GetNameSafe(Weapon),
		GetWorld()->GetTimeSeconds() - LaunchTime, Location.X, Location.Y, Location.Z, Grasp.X, Grasp.Y, Grasp.Z, Up.X, Up.Y, Up.Z, GetWorld()->GetTimeSeconds());
}

void AGinkgoWeaponPickup::ApplyWeapon()
{
	// 没指定时用设置里开局的那把；资产不在时地上什么也没有，也不能交互
	if (!Weapon)
	{
		const TSoftObjectPtr<UGinkgoWeaponDefinition>& DefaultWeapon = UGinkgoDeveloperSettings::Get()->StartingWeapon;
		Weapon = DefaultWeapon.IsNull() ? nullptr : DefaultWeapon.LoadSynchronous();
	}
	UStaticMesh* WeaponMesh = Weapon ? Weapon->Mesh.Get() : nullptr;
	RefreshAttachmentMeshes();
	Mesh->SetStaticMesh(WeaponMesh);
	USkeletalMesh* GunMesh = Weapon ? Weapon->SkeletalMesh.LoadSynchronous() : nullptr;
	GunPose->SetSkinnedAssetAndUpdate(GunMesh);
	if (GunMesh)
	{
		GunPose->BoneSpaceTransforms = GunMesh->GetRefSkeleton().GetRefBonePose();
	}
	// 有骨骼网格时画它，静态网格只管碰撞；隐藏不影响物理
	Mesh->SetVisibility(GunMesh == nullptr);
	FeedRound->SetStaticMesh(Weapon ? Weapon->Rounds.LiveRound.LoadSynchronous() : nullptr);

	// 手要到的点：持枪时右手在枪上的位置（你说的）。手骨的变换乘片段里东西在手里的挂点
	if (Weapon)
	{
		const FTransform& RightHand = Weapon->LoweredHands.Right;
		const FVector Center = WeaponMesh ? WeaponMesh->GetBoundingBox().GetCenter() : FVector::ZeroVector;
		GraspPoint = RightHand.Equals(FTransform::Identity) ? Center : RightHand.TransformPosition(ItemInHandRight);
		// 挪到枪身的中面：手心那一点偏在枪的一侧，枪朝那边侧躺时比地面还低，视线被地挡住，提示、焦点都没了（98k 低 1.1 cm）。中面上的点在凸包里面，离地至少半个枪厚
		GraspPoint.Y = Center.Y;
	}
	Interactable->SetRelativeLocation(GraspPoint);
	if (Weapon && !Weapon->DisplayName.IsEmpty())
	{
		Interactable->DisplayName = Weapon->DisplayName;
	}
	ApplyMechanismPose();
}

void AGinkgoWeaponPickup::ApplyMechanismPose()
{
	const USkeletalMesh* GunMesh = Cast<USkeletalMesh>(GunPose->GetSkinnedAsset());
	if (!Weapon || !GunMesh)
	{
		return;
	}
	const FGinkgoMechanismState* State = GetMechanismState();
	const bool bActionOpen = State && State->Action == EGinkgoAction::Open;
	const bool bMagazineIn = !State || State->bMagazineIn;
	const FReferenceSkeleton& RefSkeleton = GunMesh->GetRefSkeleton();
	const FGinkgoBoltPose Pose = Ginkgo::Weapon::BlendBoltPose(bActionOpen, 0.f, FGinkgoBoltPose());
	if (Weapon->GunRestClosed)
	{
		// 同 AGinkgoCharacter::ApplyGunPose：枪机开着、关着的静止姿势各一张，弹匣不在时缩没
		const UAnimSequence* Rest = bActionOpen && Weapon->GunRestOpen ? Weapon->GunRestOpen.Get() : Weapon->GunRestClosed.Get();
		const FAnimExtractContext RestContext(bActionOpen && Weapon->GunRestOpen ? static_cast<double>(Rest->GetPlayLength()) : 0.0);
		const int32 Magazine = bMagazineIn ? INDEX_NONE : RefSkeleton.FindBoneIndex(Weapon->MagazineBone);
		for (int32 Bone = 0; Bone < RefSkeleton.GetNum() && Bone < GunPose->BoneSpaceTransforms.Num(); ++Bone)
		{
			FTransform Transform = RefSkeleton.GetRefBonePose()[Bone];
			const int32 RestBone = Rest->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(RefSkeleton.GetBoneName(Bone));
			if (RestBone != INDEX_NONE) { Rest->GetBoneTransform(Transform, FSkeletonPoseBoneIndex(RestBone), RestContext, /*bUseRawData*/ false); }
			if (Bone == Magazine)
			{
				Transform.SetScale3D(FVector::ZeroVector);
			}
			GunPose->BoneSpaceTransforms[Bone] = Transform;
		}
	}
	else
	{
		// 枪机是两根骨头的枪（98k）：先摆父骨骼
		const FGinkgoBoltRig& Rig = Weapon->Bolt;
		if (const int32 Carrier = RefSkeleton.FindBoneIndex(Rig.CarrierBone); Carrier != INDEX_NONE)
		{
			GunPose->SetBoneTransformByName(Rig.CarrierBone, Ginkgo::Weapon::MakeBoltCarrierTransform(Ginkgo::Weapon::GetRefPoseComponentTransform(RefSkeleton, Carrier), Rig, Pose), EBoneSpaces::ComponentSpace);
		}
		if (const int32 Handle = RefSkeleton.FindBoneIndex(Rig.HandleBone); Handle != INDEX_NONE)
		{
			GunPose->SetBoneTransformByName(Rig.HandleBone, Ginkgo::Weapon::MakeBoltHandleTransform(Ginkgo::Weapon::GetRefPoseComponentTransform(RefSkeleton, Handle), Rig, Pose), EBoneSpaces::ComponentSpace);
		}
	}
	UGinkgoWeaponComponent::ApplyModulePose(*Weapon, InstalledAttachments, *GunPose);
	GunPose->RefreshBoneTransforms();

	// 弹仓顶上那一发：枪机开着才看得见（同 AGinkgoCharacter::ApplyWeaponBolts）
	const bool bFeedShown = State && bActionOpen && !State->Feed.IsEmpty() && !Weapon->Rounds.FeedTop.IsZero();
	if (bFeedShown)
	{
		FeedRound->SetRelativeTransform(Ginkgo::Weapon::MakeFeedRoundTransform(Weapon->Rounds, Weapon->Bolt, Pose.Travel));
	}
	FeedRound->SetVisibility(bFeedShown);
}

void AGinkgoWeaponPickup::ApplyLaunchedCollision()
{
	if (!bLaunched)
	{
		return;
	}
	// 只同世界碰：不挡人，壳也穿过去。交互的视线忽略目标自己的 Actor
	Mesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	Mesh->SetCollisionObjectType(ECC_PhysicsBody);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->CanCharacterStepUpOn = ECB_No;
	// 细长，转着落下来一帧能走过自己的厚度
	Mesh->SetUseCCD(true);
	Mesh->SetLinearDamping(0.1f);
	Mesh->SetAngularDamping(1.f);
	Mesh->SetMassOverrideInKg(NAME_None, LaunchedMass, true);
}

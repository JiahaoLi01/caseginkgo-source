#include "Weapon/GinkgoWeaponComponent.h"

#include "Movement/Traversal/GinkgoTraversalComponent.h"
#include "AI/GinkgoNoiseEmitterComponent.h"

#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/GinkgoWeaponAnimLayers.h"
#include "Camera/GinkgoCameraContextComponent.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Combat/GinkgoGrenadeProjectile.h"
#include "Combat/GinkgoPenetrableComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "HAL/IConsoleManager.h"
#include "AudioDevice.h"
#include "Kismet/GameplayStatics.h"
#include "MetasoundOperatorCacheSubsystem.h"
#include "MetasoundSource.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/GinkgoMovementConfig.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeReference.h"
#include "TimerManager.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoRecoilPattern.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoModularWeaponData.h"
#include "Weapon/GinkgoWeaponPickup.h"

DECLARE_CYCLE_STAT(TEXT("Weapon action states"), STAT_GinkgoWeaponActionStates, STATGROUP_Ginkgo3C);

#if !UE_BUILD_SHIPPING
namespace
{
	int32 GForcedStance = -1;
	FAutoConsoleVariableRef CVarForcedStance(TEXT("ginkgo.weapon.stance"), GForcedStance,
		TEXT("强制持枪的姿态（调试，回放与截图用）：-1 照规则选（默认），0 低持，1 腰射，2 开镜，3 冲刺。手上没枪时不起作用。发行版不编译"));

	int32 GSpeedScaleEnabled = 1;
	FAutoConsoleVariableRef CVarSpeedScaleEnabled(TEXT("ginkgo.weapon.speedScale"), GSpeedScaleEnabled,
		TEXT("持枪、开镜的地速倍率（调试，测量用）：1 照枪的倍率（默认），0 不乘：拔枪、收枪时腿要与枪一直在手上逐帧比的路线用。发行版不编译"));

	int32 GAutoActions = 1;
	FAutoConsoleVariableRef CVarAutoActions(TEXT("ginkgo.weapon.autoActions"), GAutoActions,
		TEXT("枪的子树按事实选状态（调试）：1 照规则（默认），0 一直是就绪，枪停在现在的状态：截图看膛里空壳、栓开着的样子用。发行版不编译"));

	float GRelaxDelay = -1.f;
	FAutoConsoleVariableRef CVarRelaxDelay(TEXT("ginkgo.weapon.relaxDelay"), GRelaxDelay,
		TEXT("站着不动多久放一次放松（调试，测量用）：负的照枪上的范围（默认）。发行版不编译"));

	int32 GShowShots = 0;
	FAutoConsoleVariableRef CVarShowShots(TEXT("ginkgo.weapon.showShots"), GShowShots,
		TEXT("开火时画出弹道与打到的点（调试）：0 不画（默认：命中有白 X 了，红线和黄点挡着它；原来默认画，是命中还没做时先看这条线），1 画。发行版不编译"));

	float GSpreadScale = 1.f;
	FAutoConsoleVariableRef CVarSpreadScale(TEXT("ginkgo.weapon.spreadScale"), GSpreadScale,
		TEXT("开枪时按散布偏的那一下乘这个数（调试，测量用）：1 照散布（默认），0 不偏，子弹正好打在准星上（Scripts/Measurements/combat.py 打指定的部位用）。")
		TEXT("扩散、准星的大小照常。发行版不编译"));

	int32 GAimThroughCover = 1;
	FAutoConsoleVariableRef CVarAimThroughCover(TEXT("ginkgo.weapon.aimThroughCover"), GAimThroughCover,
		TEXT("开镜隔着墙瞄墙后的人（调试，对照用，Docs/Design/Damage.md「穿射」）：1 准星的线穿过去，子弹朝后面穿得到的第一个活着的目标去（默认），")
		TEXT("0 照原来朝准星打到的第一个东西去（过了墙按枪口与相机的视差偏开）。发行版不编译"));

	int32 GHeatHaze = 1;
	FAutoConsoleVariableRef CVarHeatHaze(TEXT("ginkgo.weapon.heatHaze"), GHeatHaze,
		TEXT("枪口的热浪（调试，对照用）：1 冒（默认），0 不冒（枪管照样热、照样凉，回放照记）。发行版不编译"));

	int32 GBulletTrails = 1;
	FAutoConsoleVariableRef CVarBulletTrails(TEXT("ginkgo.weapon.trails"), GBulletTrails,
		TEXT("弹道的烟（调试，对照用）：1 画（默认），0 不画（回放照记几道、多长）。发行版不编译"));

	FString GBulletTrailTune;
	FAutoConsoleVariableRef CVarBulletTrailTune(TEXT("ginkgo.weapon.trailTune"), GBulletTrailTune,
		TEXT("弹道的烟调样子（调试，截图比用）：\"浓淡的倍数 宽度的倍数 灰度\"，乘在子弹的数上，灰度为负时照子弹的颜色；空的不改（默认）。发行版不编译"));

	int32 GScopeEnabled = 1;
	FAutoConsoleVariableRef CVarScopeEnabled(TEXT("ginkgo.weapon.scope"), GScopeEnabled,
		TEXT("第一人称开镜（调试，对照用，Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：1 有瞄具的枪开镜进第一人称、只有镜内放大（默认），")
		TEXT("0 照原来越肩拉近（98k 的 1.73 倍）。发行版不编译"));
}
#endif

namespace
{
	/** ginkgo.weapon.aimThroughCover：开镜隔着墙瞄墙后的人（发行版里总是） */
	bool IsAimThroughCoverEnabled()
	{
#if !UE_BUILD_SHIPPING
		return GAimThroughCover != 0;
#else
		return true;
#endif
	}

	/** ginkgo.weapon.scope：有瞄具的枪开镜进第一人称（发行版里总是） */
	bool IsScopeEnabled()
	{
#if !UE_BUILD_SHIPPING
		return GScopeEnabled != 0;
#else
		return true;
#endif
	}

	/**
	 * 一颗子弹从 First 起往 End 穿（Docs/Design/Damage.md「穿射」）：一个东西只算一次，穿过的整个 actor 不再挡（一个人的身子与胳膊、一面墙的几块网格）
	 * @param Shooter - 开枪的人，不挡
	 */
	void TraceBulletPath(const UWorld& World, const FHitResult& First, const FVector& End, const float CoverPenetration, const AActor& Shooter, FGinkgoPenetrationPath& OutPath)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoShot), /*bTraceComplex*/ false, &Shooter);
		Ginkgo::Combat::TracePenetration(First, CoverPenetration, [](const FHitResult& Hit)
		{
			return UGinkgoPenetrableComponent::FindThickness(Hit.GetActor());
		}, [&World, &End, &Params](const FHitResult& Through, FHitResult& OutNext)
		{
			Params.AddIgnoredActor(Through.GetActor());
			return World.LineTraceSingleByChannel(OutNext, Through.ImpactPoint, End, Ginkgo::Combat::WeaponTraceChannel, Params);
		}, OutPath);
	}

	/**
	 * 开镜时准星那条线上、这一发穿得到的第一个活着的目标（Docs/Design/Damage.md「穿射」：隔着墙瞄墙后的人）；没有时是准星打到的第一个东西（First）
	 * @param End - 准星那条线的最远处
	 */
	FHitResult FindAimedTarget(const UWorld& World, const FHitResult& First, const FVector& End, const float CoverPenetration, const AActor& Shooter)
	{
		FGinkgoPenetrationPath Path;
		TraceBulletPath(World, First, End, CoverPenetration, Shooter, Path);
		for (const FGinkgoPenetrationHit& Entry : Path)
		{
			const AActor* Actor = Entry.Hit.GetActor();
			const UGinkgoDamageableComponent* Target = Actor ? Actor->FindComponentByClass<UGinkgoDamageableComponent>() : nullptr;
			if (Target && !Target->IsDead())
			{
				return Entry.Hit;
			}
		}
		return First;
	}

	/** 一颗子弹的路写成一行日志（测量的路线按它对每个东西挨的倍数）：每个东西 "名字 厚 剩 ×倍数 结果" */
	FString DescribePenetrationPath(const FGinkgoPenetrationPath& Path)
	{
		TStringBuilder<512> Text;
		for (int32 Index = 0; Index < Path.Num(); ++Index)
		{
			const FGinkgoPenetrationHit& Entry = Path[Index];
			const TCHAR* Outcome = Entry.bPassed ? TEXT("穿过")
				: (Entry.Thickness < 0.f ? TEXT("穿不过") : (Ginkgo::Combat::GetPenetrationLeft(Entry.Remaining, Entry.Thickness) > 0.f ? TEXT("到上限") : TEXT("停在里面")));
			Text.Appendf(TEXT("%s%s 厚 %.4f 剩 %.4f ×%.4f %s"), Index > 0 ? TEXT("；") : TEXT(""), *GetNameSafe(Entry.Hit.GetActor()), Entry.Thickness, Entry.Remaining,
				Entry.DamageScale, Outcome);
		}
		return Text.ToString();
	}

	/** ginkgo.weapon.heatHaze：冒不冒热浪（发行版里总是冒） */
	bool IsHeatHazeEnabled()
	{
#if !UE_BUILD_SHIPPING
		return GHeatHaze != 0;
#else
		return true;
#endif
	}

	/** ginkgo.weapon.trails：画不画弹道的烟（发行版里总是画） */
	bool AreBulletTrailsEnabled()
	{
#if !UE_BUILD_SHIPPING
		return GBulletTrails != 0;
#else
		return true;
#endif
	}

	/** 弹道的烟的用户参数（GinkgoWeaponFx -Trail 建的 NS_BulletTrail）：终点是相对起点的（系统放在枪口、不转），颜色的透明度是浓淡 */
	const FName TrailEndParameter(TEXT("User.TrailEnd"));
	const FName TrailPointsParameter(TEXT("User.TrailPoints"));
	const FName TrailLifetimeParameter(TEXT("User.TrailLifetime"));
	const FName TrailWidthParameter(TEXT("User.TrailWidth"));
	const FName TrailRevealParameter(TEXT("User.TrailReveal"));
	const FName TrailColorParameter(TEXT("User.TrailColor"));
}

UGinkgoWeaponComponent::UGinkgoWeaponComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	CarriedWeapons.SetNum(SlotCount);
	InstalledAttachments.SetNum(SlotCount);
	AttachmentMeshes.SetNum(SlotCount);
	AttachmentEffects.SetNum(SlotCount);
	EffectiveMechanisms.SetNum(SlotCount);
	EffectiveMechanismSources.SetNum(SlotCount);
	Mechanisms.SetNum(SlotCount);
	MechanismWeapons.SetNum(SlotCount);
}

AGinkgoCharacter* UGinkgoWeaponComponent::GetCharacter() const
{
	return Cast<AGinkgoCharacter>(GetOwner());
}

UGinkgoWeaponDefinition* UGinkgoWeaponComponent::GetCarried(const int32 Slot) const
{
	return CarriedWeapons.IsValidIndex(Slot) ? CarriedWeapons[Slot].Get() : nullptr;
}

int32 UGinkgoWeaponComponent::FindFreeSlot(const EGinkgoWeaponSlotKind Kind) const
{
	return Ginkgo::Weapon::FindFreeSlot(Kind, GetCarriedFlags());
}

void UGinkgoWeaponComponent::SetCarried(const int32 Slot, UGinkgoWeaponDefinition* Weapon)
{
	if (!ensureMsgf(GetOwner() && GetOwner()->HasAuthority(), TEXT("武器槽只在服务器上改")) || !CarriedWeapons.IsValidIndex(Slot))
	{
		return;
	}
	if (CarriedWeapons[Slot] != Weapon)
	{
		InstalledAttachments[Slot] = Weapon ? Weapon->GetDefaultAttachments() : FGinkgoInstalledAttachments();
	}
	CarriedWeapons[Slot] = Weapon;
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 的第 %d 个槽：%s"), *GetNameSafe(GetOwner()), Slot + 1, Weapon ? *Weapon->GetName() : TEXT("空"));
	// 服务器上没有 OnRep
	OnRep_CarriedWeapons();
}

void UGinkgoWeaponComponent::OnRep_CarriedWeapons()
{
	SyncMechanisms();
	TryApplyStartingReserve();
	// 手上那把被拿走了（放到地上）：回到徒手
	if (HeldSlot != INDEX_NONE && !GetCarried(HeldSlot))
	{
		SetHeldSlot(INDEX_NONE);
	}
	// 要去的那把被拿走了：不去了
	if (TargetSlot != INDEX_NONE && !GetCarried(TargetSlot))
	{
		TargetSlot = HeldSlot;
	}
	if (AGinkgoCharacter* Character = GetCharacter())
	{
		Character->ApplyCarriedWeapons();
	}
	RefreshAttachmentMeshes();
}

const FGinkgoInstalledAttachments& UGinkgoWeaponComponent::GetAttachments(const int32 Slot) const
{
	static const FGinkgoInstalledAttachments Empty;
	return GetCarried(Slot) && InstalledAttachments.IsValidIndex(Slot) ? InstalledAttachments[Slot] : Empty;
}

void UGinkgoWeaponComponent::SetHeldSlot(const int32 Slot)
{
	const int32 NewSlot = GetCarried(Slot) ? Slot : INDEX_NONE;
	if (NewSlot == HeldSlot)
	{
		return;
	}
	if (!CurrentStep.IsValid())
	{
		TargetSlot = NewSlot;
	}
	HeldSlot = NewSlot;
	// 重武器不记成上一次拿的：它离手就在地上了，收枪键、切枪键回到之前那把
	if (HeldSlot != INDEX_NONE && Ginkgo::Weapon::CanStow(Ginkgo::Weapon::GetSlotKind(HeldSlot)))
	{
		LastHeldSlot = HeldSlot;
	}
	LinkAnimLayers(GetHeldWeapon());
	UE_LOG(LogGinkgoWeapon, Verbose, TEXT("[Weapon] %s 手上：%s"), *GetNameSafe(GetOwner()), HeldSlot == INDEX_NONE ? TEXT("徒手") : *GetHeldWeapon()->GetName());
}

void UGinkgoWeaponComponent::EquipStartingWeapon(const int32 Slot)
{
	auto* Character = GetCharacter();
	if (!Character || !GetCarried(Slot)) { return; }
	bStartHoldDone = true;
	SetHeldSlot(Slot);
	Character->AttachWeaponToHand(Slot);
	HoldState.Weapon = GetHeldWeapon();
	HoldState.Stance = EGinkgoHoldStance::Lowered;
}

void UGinkgoWeaponComponent::ApplyReplicatedHoldPose(const FGinkgoReplicatedHoldPose& Pose)
{
	auto* Character = GetCharacter();
	if (!Character || Character->GetLocalRole() != ROLE_SimulatedProxy) { return; }

	auto* Weapon = GetCarried(Pose.Slot);
	const int32 NewSlot = Weapon ? Pose.Slot : INDEX_NONE;
	if (NewSlot != HeldSlot)
	{
		if (GetHeldWeapon()) { Character->AttachWeaponToStow(HeldSlot); }
		SetHeldSlot(NewSlot);
	}
	else if (Weapon != HoldState.Weapon)
	{
		// 同一槽可换成另一把枪，槽号不变时也要切换武器专用动画层。
		LinkAnimLayers(Weapon);
	}
	if (Weapon && !Character->IsHeldWeaponInHand()) { Character->AttachWeaponToHand(NewSlot); }
	HoldState.Weapon = Weapon;
	HoldState.Stance = Weapon ? Pose.Stance : EGinkgoHoldStance::Unarmed;
	HoldState.RaiseProgress = Weapon ? Pose.Raise / 255.f : 0.f;
	HoldState.AdsProgress = Weapon ? Pose.Ads / 255.f : 0.f;
	HoldState.SprintProgress = Weapon ? Pose.Sprint / 255.f : 0.f;
	HoldState.bBothHandsOnGun = Weapon && Pose.bBothHandsOnGun;
}

void UGinkgoWeaponComponent::LinkAnimLayers(const UGinkgoWeaponDefinition* Weapon)
{
	const AGinkgoCharacter* Character = GetCharacter();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	UWorld* World = GetWorld();
	if (!Mesh || !World)
	{
		return;
	}
	UClass* NewClass = Weapon ? Weapon->AnimLayerClass.Get() : nullptr;
	// 淡出还没完又拿起同一种层的枪：不撤了，层从现在的权重淡回去
	if (NewClass && NewClass == LinkedLayerClass)
	{
		World->GetTimerManager().ClearTimer(UnlinkTimer);
		return;
	}
	if (!NewClass)
	{
		// 徒手：层看到手上没枪会自己淡到 0，淡完再撤（撤掉时回到层节点的默认，主蓝图自己那份原样输出），撤的那一帧看不出来。
		// 多等一点：层按动画的帧长走，定时器按世界的，差一帧时还没淡完
		const UGinkgoWeaponAnimLayers* Layers = LinkedLayerClass ? Cast<UGinkgoWeaponAnimLayers>(LinkedLayerClass->GetDefaultObject()) : nullptr;
		if (Layers && !World->GetTimerManager().IsTimerActive(UnlinkTimer))
		{
			World->GetTimerManager().SetTimer(UnlinkTimer, this, &ThisClass::UnlinkAnimLayers, Layers->GetLayerBlendTime() + UnlinkMargin);
			return;
		}
		if (!Layers)
		{
			UnlinkAnimLayers();
		}
		return;
	}
	// 换成另一种层：旧的直接撤（以后要混两种层时再做，现在只有步枪的层）
	if (LinkedLayerClass)
	{
		UnlinkAnimLayers();
	}
	Mesh->LinkAnimClassLayers(NewClass);
	LinkedLayerClass = NewClass;
}

void UGinkgoWeaponComponent::UnlinkAnimLayers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UnlinkTimer);
	}
	const AGinkgoCharacter* Character = GetCharacter();
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (Mesh && LinkedLayerClass)
	{
		Mesh->UnlinkAnimClassLayers(LinkedLayerClass);
	}
	LinkedLayerClass = nullptr;
}

TArray<bool, TInlineAllocator<UGinkgoWeaponComponent::SlotCount>> UGinkgoWeaponComponent::GetCarriedFlags() const
{
	TArray<bool, TInlineAllocator<SlotCount>> Carried;
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		Carried.Add(GetCarried(Slot) != nullptr);
	}
	return Carried;
}

int32 UGinkgoWeaponComponent::FindDrawSlot() const
{
	return Ginkgo::Weapon::FindDrawSlot(LastHeldSlot, GetCarriedFlags());
}

namespace
{
	/** 丢枪的日志：状态原样带走，路线（weapon_drop.py）照这一行比 */
	FString DescribeMechanism(const FGinkgoMechanismState& State)
	{
		return FString::Printf(TEXT("feed=%d chamber=%d action=%d magazine=%d"), State.Feed.Num(), static_cast<int32>(State.Chamber), static_cast<int32>(State.Action), State.bMagazineIn ? 1 : 0);
	}

	// 从手里丢出去：往前上方（cm/s），绕枪身滚一点、水平转一点（度/秒），落下来才躺得平。我定的，手测再调
	constexpr double DropTossForward = 170.0;
	constexpr double DropTossUp = 130.0;
	constexpr double DropRollSpeed = 180.0;
	constexpr double DropYawSpeed = 70.0;
	// 身上掉下来的：没人扔它，只是松开了，带着人的速度，往外一点
	constexpr double FallOutward = 40.0;
	constexpr double FallRollSpeed = 60.0;
	// 贴墙丢时枪离墙留多少（cm）：比枪的半个厚度大，物理不用再推
	constexpr double DropWallMargin = 6.0;
	// 捡枪抓住以后枪收到手里要多久：够不着时枪从地上飞过来，太快像瞬移，太慢手里空一下
	constexpr float PickupGunBlendTime = 0.2f;
}

void UGinkgoWeaponComponent::DropWeapon(const int32 Slot, const bool bTossFromHand)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Ginkgo_Weapon_DropWeapon);

	AGinkgoCharacter* Character = GetCharacter();
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	const UPoseableMeshComponent* Gun = Character && Weapon ? Character->GetWeaponSlotComponent(Slot) : nullptr;
	if (!Gun)
	{
		return;
	}
	FTransform Transform = Gun->GetComponentTransform();
	// 贴着墙丢：枪管伸到了墙那边，物理会把它推到墙外。整把挪回人这边
	if (const UStaticMesh* WeaponMesh = Weapon->Mesh.Get())
	{
		const FBox Bounds = WeaponMesh->GetBoundingBox();
		const FVector Body = Character->GetActorLocation();
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		const FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoWeaponDrop), /*bTraceComplex*/ false, Character);
		for (const double EndX : {Bounds.Min.X, Bounds.Max.X})
		{
			const FVector End = Transform.TransformPosition(FVector(EndX, 0.0, 0.0));
			FHitResult Hit;
			if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(Body.X, Body.Y, End.Z), End, Objects, Params))
			{
				Transform.AddToTranslation(FVector(Hit.ImpactNormal) * (FVector::DotProduct(FVector(Hit.ImpactPoint) - End, FVector(Hit.ImpactNormal)) + DropWallMargin));
			}
		}
	}
	const FVector Along = Transform.GetUnitAxis(EAxis::X);
	const FVector CharacterVelocity = Character->GetVelocity();
	FVector Velocity;
	FVector AngularVelocity;
	if (bTossFromHand && Slot == HeldSlot && !Character->IsWeaponStowed(Slot))
	{
		Velocity = CharacterVelocity + Character->GetActorForwardVector() * DropTossForward + FVector::UpVector * DropTossUp;
		AngularVelocity = Along * DropRollSpeed + FVector::UpVector * DropRandom.FRandRange(-DropYawSpeed, DropYawSpeed);
	}
	else
	{
		const double Side = Ginkgo::Weapon::GetSlotKind(Slot) == EGinkgoWeaponSlotKind::Sidearm ? 1.0 : -0.5;
		Velocity = CharacterVelocity + Character->GetActorRightVector() * Side * FallOutward;
		AngularVelocity = Along * DropRandom.FRandRange(-FallRollSpeed, FallRollSpeed);
	}
	const FGinkgoMechanismState* State = GetMechanismState(Slot);
	const FGinkgoMechanismState Snapshot = State ? *State : FGinkgoMechanismState();
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 丢枪：第 %d 个槽的 %s，%s，%s，世界时间 %.4f s"), Slot + 1, *Weapon->GetName(), Slot == HeldSlot ? TEXT("从手里") : TEXT("从身上"), *DescribeMechanism(Snapshot),
		GetWorld()->GetTimeSeconds());
	const bool bFromHand = Slot == HeldSlot;
	// 收拢到一半扔了：枪不在手上了，不再换挂点
	if (Slot == PickupSettleSlot)
	{
		PickupSettleSlot = INDEX_NONE;
	}
	if (GetOwner()->HasAuthority())
	{
		SpawnDroppedWeapon(Slot, Snapshot, Transform, Velocity, AngularVelocity);
	}
	else
	{
		// 客户端：槽先空，手才空得出来。同一把枪丢了又捡时服务器的值可能看起来没变，这边抓住时再填回去（FinishWeaponPickup）
		ServerDropWeapon(Slot, Snapshot, Transform, Velocity, AngularVelocity);
		CarriedWeapons[Slot] = nullptr;
		OnRep_CarriedWeapons();
	}
	// 手上的枪没了，正在跑的一步也没了：开火、换弹的树下一帧自己会退，拔枪、收枪是"放的时候不打断"的，手不再对着空气放完。树不存事实，下一帧重新选（Docs/Design/ActionStates.md）
	if (bFromHand)
	{
		StopActionStates();
	}
}

void UGinkgoWeaponComponent::ServerDropWeapon_Implementation(const int32 Slot, const FGinkgoMechanismState& State, const FTransform& Transform, const FVector& Velocity, const FVector& AngularVelocity)
{
	SpawnDroppedWeapon(Slot, State, Transform, Velocity, AngularVelocity);
}

void UGinkgoWeaponComponent::SpawnDroppedWeapon(const int32 Slot, const FGinkgoMechanismState& State, const FTransform& Transform, const FVector& Velocity, const FVector& AngularVelocity)
{
	// 包含拾取物创建、组件注册、物理启动及持枪状态清理；用于区分掉枪与后续布娃娃的开销。
	TRACE_CPUPROFILER_EVENT_SCOPE(Ginkgo_Weapon_SpawnDroppedWeapon);

	UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	UWorld* World = GetWorld();
	if (!Weapon || !World)
	{
		return;
	}
	AGinkgoWeaponPickup* Pickup = World->SpawnActorDeferred<AGinkgoWeaponPickup>(AGinkgoWeaponPickup::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Pickup->SetWeapon(Weapon);
	Pickup->SetAttachments(GetAttachments(Slot));
	Pickup->SetMechanismState(State);
	Pickup->FinishSpawning(Transform);
	Pickup->Launch(Velocity, AngularVelocity);
	SetCarried(Slot, nullptr);
}

void UGinkgoWeaponComponent::BeginWeaponPickup(AGinkgoWeaponPickup& Pickup)
{
	AGinkgoCharacter* Character = GetCharacter();
	UGinkgoWeaponDefinition* Weapon = Pickup.GetWeapon();
	if (!Character || !Weapon)
	{
		return;
	}
	const Ginkgo::Weapon::FGinkgoWeaponPickupPlan Plan = Ginkgo::Weapon::PlanWeaponPickup(Weapon->SlotKind, GetCarriedFlags(), HeldSlot, LastHeldSlot);
	const FGinkgoMechanismState* State = Pickup.GetMechanismState();
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 捡枪开始：%s，放进第 %d 个槽，%s%s，地上的状态 %s，世界时间 %.4f s"), *Weapon->GetName(), Plan.Slot + 1,
		Plan.DropSlot != INDEX_NONE ? *FString::Printf(TEXT("第 %d 个槽的枪掉地上"), Plan.DropSlot + 1) : TEXT("有空槽"),
		Plan.bStowHeld ? TEXT("，手上的先收") : (Plan.bDropHeld ? TEXT("，手上的收不起来，扔掉") : TEXT("")), State ? *DescribeMechanism(*State) : TEXT("新枪"), GetWorld()->GetTimeSeconds());
	// 手上的收不起来（重武器）：先扔，右手空了就伸手
	if (Plan.bDropHeld)
	{
		DropWeapon(HeldSlot);
	}
	if (Plan.DropSlot != INDEX_NONE)
	{
		DropWeapon(Plan.DropSlot);
	}
	PendingPickup = &Pickup;
	PendingPickupSlot = Plan.Slot;
	PendingPickupWeapon = Weapon;
	bPickupReachStarted = false;
	bPendingPickupHasState = State != nullptr;
	PendingPickupState = State ? *State : FGinkgoMechanismState();
	Character->SetWeaponSlotHidden(Plan.Slot, true);
	// 捡枪时弹种轮盘取消
	CancelReloadKey();
	if (Plan.bStowHeld)
	{
		TargetSlot = INDEX_NONE;
		bWantsFire = false;
		bWantsReload = false;
		bWantsChamberSwap = false;
	}
	UpdatePendingPickup();
}

void UGinkgoWeaponComponent::UpdatePendingPickup()
{
	if (PendingPickupSlot == INDEX_NONE)
	{
		return;
	}
	AGinkgoCharacter* Character = GetCharacter();
	AGinkgoWeaponPickup* Pickup = PendingPickup.Get();
	if (!Pickup || !Character)
	{
		// 地上那把没了（等收枪时、手还没到时）：不捡了，武器键也不能一直不响应
		if (Character)
		{
			Character->SetWeaponSlotHidden(PendingPickupSlot, false);
		}
		PendingPickupSlot = INDEX_NONE;
		PendingPickupWeapon = nullptr;
		bPendingPickupHasState = false;
		return;
	}
	// 右手空了：手上没枪、持枪的占用放开了（丢手上那把的这一帧还没放，优先级同拾取，抢不过）、树没在跑一步（收枪放完）。别的拾取占着右手时 StartLocalReach 顶掉它
	if (!bPickupReachStarted && HeldSlot == INDEX_NONE && HoldOccupancy == INDEX_NONE && !CurrentStep.IsValid())
	{
		bPickupReachStarted = true;
		Pickup->StartLocalReach(*Character);
	}
}

void UGinkgoWeaponComponent::FinishWeaponPickup(const AGinkgoWeaponPickup& Pickup, const FTransform& WorldBefore)
{
	AGinkgoCharacter* Character = GetCharacter();
	if (!Character || PendingPickupSlot == INDEX_NONE || PendingPickup.Get() != &Pickup)
	{
		return;
	}
	const int32 Slot = PendingPickupSlot;
	// 客户端：服务器放进槽的复制可能还没到，或者同一把枪丢了又捡，服务器的值没变，不会再复制：这边先填上
	if (GetCarried(Slot) != PendingPickupWeapon && CarriedWeapons.IsValidIndex(Slot))
	{
		CarriedWeapons[Slot] = PendingPickupWeapon;
		OnRep_CarriedWeapons();
	}
	// 状态是地上那把的，原样（SyncMechanisms 放进槽时已经照它，这里再保证一次：同一把枪丢了又捡时槽里的那把没换过）
	if (bPendingPickupHasState && MechanismWeapons.IsValidIndex(Slot))
	{
		MechanismWeapons[Slot] = PendingPickupWeapon;
		Mechanisms[Slot] = PendingPickupState;
	}
	const FGinkgoMechanismState* State = GetMechanismState(Slot);
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 捡枪抓住：第 %d 个槽的 %s，%s，后备 %d，地上 x=%.1f y=%.1f z=%.1f，世界时间 %.4f s"), Slot + 1, *GetNameSafe(PendingPickupWeapon), State ? *DescribeMechanism(*State) : TEXT("没有机构"),
		PendingPickupWeapon ? AmmoReserve.Count(PendingPickupWeapon->AmmoType) : 0, WorldBefore.GetLocation().X, WorldBefore.GetLocation().Y, WorldBefore.GetLocation().Z, GetWorld()->GetTimeSeconds());
	PendingPickup.Reset();
	PendingPickupSlot = INDEX_NONE;
	PendingPickupWeapon = nullptr;
	bPendingPickupHasState = false;
	Character->SetWeaponSlotHidden(Slot, false);
	// 收拢（设置的 PickupSettleTime）：枪先在右手里，按进度带到持枪的位置。从抓住这一刻算，Control Rig 这一帧起就从右手里出发：
	// 枪保持在地上的朝向、右手保持抓住时的握法（记在角色网格的坐标里，人转身时跟着转），被握的那一点在右手的挂点上
	PickupSettleSlot = Slot;
	PickupSettleStart = GetWorld()->GetTimeSeconds();
	PickupSettleDuration = UGinkgoDeveloperSettings::Get()->PickupSettleTime;
	PickupSettleStartPose = Character->CapturePickupSettleStart(WorldBefore, Pickup.GetItemInHand(EGinkgoHand::Right));
	Character->AttachPickedWeapon(Slot, WorldBefore, PickupGunBlendTime);
	// 树下一帧看到目标槽变了，放拔枪那一步：捡起来的不放拔枪的片段，按收拢剩下的时间占着右臂（AGinkgoCharacter::MakeDrawWeaponAction）
	bDrawFromPickup = true;
	TargetSlot = Slot;
}

float UGinkgoWeaponComponent::GetPickupSettleAlpha() const
{
	return Ginkgo::Weapon::EaseProgress(GetPickupSettleProgress());
}

float UGinkgoWeaponComponent::GetPickupSettleProgress() const
{
	if (PickupSettleSlot == INDEX_NONE || PickupSettleDuration <= 0.f)
	{
		return 1.f;
	}
	const double Linear = (GetWorld()->GetTimeSeconds() - PickupSettleStart) / PickupSettleDuration;
	return static_cast<float>(FMath::Clamp(Linear, 0.0, 1.0));
}

float UGinkgoWeaponComponent::GetPickupSettleRemaining() const
{
	return PickupSettleSlot == INDEX_NONE ? 0.f : FMath::Max(PickupSettleDuration - static_cast<float>(GetWorld()->GetTimeSeconds() - PickupSettleStart), 0.f);
}

void UGinkgoWeaponComponent::EndPickupSettle()
{
	if (PickupSettleSlot == INDEX_NONE)
	{
		return;
	}
	const int32 Slot = PickupSettleSlot;
	PickupSettleSlot = INDEX_NONE;
	// 枪从右手换回持枪的挂点：收拢做完时右手正握在持枪位置的握把上，两个挂法是同一个地方（只差全身 IK 没解到的那一点）
	AGinkgoCharacter* Character = GetCharacter();
	if (Character && GetCarried(Slot) && !Character->IsWeaponStowed(Slot))
	{
		Character->AttachWeaponToHand(Slot);
	}
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 捡枪收拢完：第 %d 个槽，世界时间 %.4f s"), Slot + 1, GetWorld()->GetTimeSeconds());
}

void UGinkgoWeaponComponent::CancelWeaponPickup(const AGinkgoWeaponPickup& Pickup)
{
	if (PendingPickupSlot == INDEX_NONE || PendingPickup.Get() != &Pickup)
	{
		return;
	}
	if (AGinkgoCharacter* Character = GetCharacter())
	{
		Character->SetWeaponSlotHidden(PendingPickupSlot, false);
	}
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] 捡枪取消：%s"), *Pickup.GetName());
	PendingPickup.Reset();
	PendingPickupSlot = INDEX_NONE;
	PendingPickupWeapon = nullptr;
	bPendingPickupHasState = false;
}

bool UGinkgoWeaponComponent::RequestDrawStowedWeapon(const int32 Slot)
{
	auto* Character = GetCharacter();
	if (!Character || Slot == INDEX_NONE || HeldSlot != Slot || !GetCarried(Slot) || !Character->IsWeaponStowed(Slot)) { return false; }
	StopActionStates();
	InterruptHandRequests();
	// 已经有过手持武器；本次是显式取回请求，不能再走开局直接挂手的初始化捷径。
	bStartHoldDone = true;
	bDrawFromPickup = false;
	DrawFromBackSlot = INDEX_NONE;
	// 远端玩家没有本地输入驱动武器状态树；只复用同一取枪 AnimAction 的表现和 InHand 挂点事件。
	// 不能为了播放一次取枪而推进远端整棵武器树，也不能把它的生命周期绑在更短的攀爬淡出上。
	if (!Character->IsLocallyControlled() && (Character->GetLocalRole() == ROLE_SimulatedProxy || Character->IsPlayerControlled()))
	{
		return Character->GetAnimActions()->Start(Character->MakeDrawWeaponAction(Slot));
	}
	SetHeldSlot(INDEX_NONE);
	TargetSlot = Slot;
	return true;
}

bool UGinkgoWeaponComponent::BeginDrawFromBack(const int32 FromSlot)
{
	const AGinkgoCharacter* Character = GetCharacter();
	const UWorld* World = GetWorld();
	if (!Character || !World || !Ginkgo::Weapon::IsBackToBackSwap(FromSlot, TargetSlot) || !GetCarried(TargetSlot) || !Character->IsWeaponStowed(TargetSlot))
	{
		DrawFromBackSlot = INDEX_NONE;
		return false;
	}
	DrawFromBackSlot = TargetSlot;
	DrawFromBackTime = World->GetTimeSeconds();
	return true;
}

bool UGinkgoWeaponComponent::ConsumeDrawFromBack(const int32 Slot)
{
	// 拔枪在收枪完成后的下一帧开始
	constexpr double MaxWait = 0.25;
	const UWorld* World = GetWorld();
	const bool bResult = World && DrawFromBackSlot == Slot && World->GetTimeSeconds() - DrawFromBackTime <= MaxWait;
	DrawFromBackSlot = INDEX_NONE;
	return bResult;
}

bool UGinkgoWeaponComponent::ConsumeDrawFromPickup()
{
	const bool bResult = bDrawFromPickup;
	bDrawFromPickup = false;
	return bResult;
}

void UGinkgoWeaponComponent::HandleWeaponKeys(const EGinkgoWeaponKey Keys)
{
	if (Keys == EGinkgoWeaponKey::None || !GetActionTree())
	{
		return;
	}
	// 捡枪的这一段不换枪：手已经定了要去哪
	if (IsPickingUpWeapon())
	{
		return;
	}
	// 丢枪：只丢拿在手里的。拔枪还没出背、收枪已经上背时不响应
	if (EnumHasAnyFlags(Keys, EGinkgoWeaponKey::Drop) && GetHeldWeapon() && GetCharacter() && !GetCharacter()->IsWeaponStowed(HeldSlot))
	{
		DropWeapon(HeldSlot);
	}
	const TArray<bool, TInlineAllocator<SlotCount>> Carried = GetCarriedFlags();
	const int32 PreviousTarget = TargetSlot;
	int32 NewTarget = TargetSlot;
	for (const EGinkgoWeaponKey Key : {EGinkgoWeaponKey::Slot1, EGinkgoWeaponKey::Slot2, EGinkgoWeaponKey::Sidearm, EGinkgoWeaponKey::Holster, EGinkgoWeaponKey::Switch})
	{
		if (EnumHasAnyFlags(Keys, Key))
		{
			NewTarget = Ginkgo::Weapon::ApplyWeaponKey(Key, NewTarget, LastHeldSlot, Carried);
		}
	}
	// 手上的收不起来（重武器，MG3）：换枪、收枪都直接扔在地上（你说的），手空了接着拔要的那把，收枪键就是空手。
	// 扔掉时手上变空，没在跑一步时目标跟着变成空手（SetHeldSlot），所以目标在扔完以后再定
	if (NewTarget != PreviousTarget && HeldSlot != INDEX_NONE && HeldSlot == PreviousTarget && !Ginkgo::Weapon::CanStow(Ginkgo::Weapon::GetSlotKind(HeldSlot))
		&& GetCharacter() && !GetCharacter()->IsWeaponStowed(HeldSlot))
	{
		UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 收不起来，换枪时扔在地上，接着%s"), *GetNameSafe(GetHeldWeapon()),
			NewTarget == INDEX_NONE ? TEXT("空手") : *FString::Printf(TEXT("拔第 %d 个槽"), NewTarget + 1));
		DropWeapon(HeldSlot);
	}
	TargetSlot = NewTarget;
	// 收枪、换枪立刻收（你定的）：装填、压膛不接着做了，轮盘取消
	if (TargetSlot != PreviousTarget)
	{
		bWantsReload = false;
		bWantsChamberSwap = false;
		CancelReloadKey();
	}
	const UGinkgoWeaponDefinition* TargetWeapon = GetCarried(TargetSlot);
	if (TargetWeapon && TargetWeapon->ActionTree)
	{
		if (EnumHasAnyFlags(Keys, EGinkgoWeaponKey::Fire))
		{
			bWantsFire = true;
			bWantsReload = false;
			bWantsChamberSwap = false;
			CancelReloadKey();
		}
		// 装填键的按下交给 UpdateAmmoWheel：有轮盘的枪等松开或按够（Docs/Design/Weapons.md「切换弹种」）
		if (EnumHasAnyFlags(Keys, EGinkgoWeaponKey::Reload))
		{
			bReloadPressed = true;
			bWantsFire = false;
		}
	}
	UE_LOG(LogGinkgoWeapon, Verbose, TEXT("[Weapon] 武器的键 %d：手上 %d，目标 %d，想开火 %d"), static_cast<int32>(Keys), HeldSlot, TargetSlot, bWantsFire ? 1 : 0);
}

EGinkgoWeaponAction UGinkgoWeaponComponent::SelectWeaponAction() const
{
#if !UE_BUILD_SHIPPING
	if (GAutoActions == 0)
	{
		return EGinkgoWeaponAction::Ready;
	}
#endif
	const FGinkgoMechanismState* State = GetMechanismState(HeldSlot);
	const AGinkgoCharacter* Character = GetCharacter();
	const UGinkgoAnimActionComponent* Actions = Character ? Character->GetAnimActions() : nullptr;
	if (!State || !Actions)
	{
		return EGinkgoWeaponAction::Ready;
	}
	FGinkgoWeaponActionFacts Facts;
	Facts.bActionOpen = State->Action == EGinkgoAction::Open;
	Facts.bChamberSpent = State->Chamber == EGinkgoChamber::Spent;
	Facts.bChamberEmpty = State->Chamber == EGinkgoChamber::Empty;
	const UGinkgoWeaponMechanism* Mechanism = GetMechanism(HeldSlot);
	Facts.bCanFire = Mechanism ? Mechanism->CanFire(*State) : State->CanFire();
	Facts.bOpenBolt = Mechanism && Mechanism->IsOpenBolt();
	Facts.bFeedHasRounds = State->bMagazineIn && !State->Feed.IsEmpty();
	Facts.bWantsFire = WantsToShoot();
	Facts.bWantsReload = bWantsReload;
	const UGinkgoWeaponDefinition* Weapon = GetHeldWeapon();
	const FGameplayTag Load = GetLoadAmmo(HeldSlot);
	Facts.bCanLoad = Weapon && GetMechanism(HeldSlot) && GetMechanism(HeldSlot)->CanLoadRound(*State, AmmoReserve, Load);
	// 从抛壳口压膛（霰弹枪）：轮盘确认的那一种还不是下一枪，或者想装填而枪里一发也没有（紧急装填，2026-11-06 你定的：膛空时走这条路）
	const bool bDry = State->Chamber != EGinkgoChamber::Live && (!State->bMagazineIn || State->Feed.IsEmpty());
	Facts.bWantsLoadChamber = Mechanism && Mechanism->AllowsChamberLoad() && (bWantsChamberSwap || (bWantsReload && bDry));
	Facts.bCanLoadChamber = Mechanism && Mechanism->CanLoadChamber(*State, AmmoReserve, Load);
	Facts.bDetachableFeed = Weapon && GetMechanism(HeldSlot) && GetMechanism(HeldSlot)->GetFeed() == EGinkgoFeed::DetachableMagazine;
	Facts.bLoadsWithActionClosed = Weapon && GetMechanism(HeldSlot) && GetMechanism(HeldSlot)->GetFeed() == EGinkgoFeed::TubeMagazine;
	Facts.bSingleShot = Mechanism && Mechanism->GetFeed() == EGinkgoFeed::SingleShot;
	Facts.bHandAtAction = bHandAtAction;
	Facts.bIdleRelaxDue = bIdleRelaxDue;
	Facts.bHandsFree = Actions->CanOccupy(FAnimActionTags::Occupancy_LeftArm, Ginkgo::Occupancy::PriorityGunAction)
		&& Actions->CanOccupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityGunAction);
	Facts.bOneHandFire = Character->GetFlashbang()->AllowsOneHandFire() && Actions->CanOccupy(FAnimActionTags::Occupancy_RightArm_Hand, Ginkgo::Occupancy::PriorityGunAction);
	return Ginkgo::Weapon::SelectWeaponAction(Facts);
}

bool UGinkgoWeaponComponent::CanLoadTargetWeapon() const
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(TargetSlot);
	const FGinkgoMechanismState* State = GetMechanismState(TargetSlot);
	return Weapon && State && GetMechanism(TargetSlot) && GetMechanism(TargetSlot)->CanLoadRound(*State, AmmoReserve, GetLoadAmmo(TargetSlot));
}

bool UGinkgoWeaponComponent::CanLoadTargetWeaponAfterCycle() const
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(TargetSlot);
	const FGinkgoMechanismState* State = GetMechanismState(TargetSlot);
	return Weapon && State && GetMechanism(TargetSlot) && GetMechanism(TargetSlot)->CanLoadAfterCycle(*State, AmmoReserve, GetLoadAmmo(TargetSlot));
}

bool UGinkgoWeaponComponent::CanLoadTargetWeaponChamber() const
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(TargetSlot);
	const FGinkgoMechanismState* State = GetMechanismState(TargetSlot);
	return Weapon && State && GetMechanism(TargetSlot) && GetMechanism(TargetSlot)->CanLoadChamber(*State, AmmoReserve, GetLoadAmmo(TargetSlot));
}

void UGinkgoWeaponComponent::CancelReloadForThrow()
{
	CancelReloadKey();
	bReloadPressed = false;
	bWantsReload = false;
	bWantsChamberSwap = false;
}

void UGinkgoWeaponComponent::SetWantsToAim(const bool bInWantsToAim)
{
	// 按下开镜：装填这一发压完就停（你定的），压膛不做了；弹种轮盘取消（你说的：开镜键对应取消选择），按着的装填键作废
	if (bInWantsToAim && !bWantsToAim)
	{
		bWantsReload = false;
		bWantsChamberSwap = false;
		CancelReloadKey();
	}
	bWantsToAim = bInWantsToAim;
}

// ===== 弹种与轮盘（Docs/Design/Weapons.md「切换弹种」） =====

bool UGinkgoWeaponComponent::HasAmmoWheel(const UGinkgoWeaponDefinition* Weapon)
{
	return Weapon && Weapon->Mechanism && Weapon->Mechanism->IsReloadable() && Weapon->AmmoType.IsValid() && UGinkgoAmmoDefinition::GetChoices(Weapon->AmmoType).Num() >= 2;
}

FGameplayTag UGinkgoWeaponComponent::GetLoadAmmo(const int32 Slot) const
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	if (!Weapon)
	{
		return FGameplayTag();
	}
	return Slot >= 0 && Slot < SlotCount && LoadAmmo[Slot].IsValid() ? LoadAmmo[Slot] : Weapon->AmmoType;
}

void UGinkgoWeaponComponent::SetLoadAmmo(const int32 Slot, const FGameplayTag& Ammo)
{
	if (Slot >= 0 && Slot < SlotCount && GetCarried(Slot))
	{
		LoadAmmo[Slot] = Ammo;
	}
}

bool UGinkgoWeaponComponent::ResolveLoadAmmo(const int32 Slot)
{
	// 只往枪的弹种退（特殊弹打完了接着装普通弹）；普通弹没了不自动用特殊弹：特殊弹要自己在轮盘里选（霰弹枪的 reserve_one 路线量出来的：
	// 鹿弹只剩一发时，压完它接着把独头弹压进去了）
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	const FGameplayTag Current = GetLoadAmmo(Slot);
	if (!Weapon || Current == Weapon->AmmoType || AmmoReserve.Count(Current) > 0 || AmmoReserve.Count(Weapon->AmmoType) <= 0)
	{
		return false;
	}
	LoadAmmo[Slot] = Weapon->AmmoType;
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 第 %d 个槽：%s 后备没了，装填换成 %s"), *GetNameSafe(GetOwner()), Slot + 1, *Current.ToString(), *Weapon->AmmoType.ToString());
	return true;
}

void UGinkgoWeaponComponent::StartReload()
{
	bWantsReload = true;
	bWantsFire = false;
	ResolveLoadAmmo(TargetSlot);
}

bool UGinkgoWeaponComponent::CanOpenAmmoWheel() const
{
	const AGinkgoCharacter* Character = GetCharacter();
	const UGinkgoWeaponDefinition* Held = GetHeldWeapon();
	return Held && Character && TargetSlot == HeldSlot && !Character->IsWeaponStowed(HeldSlot) && !IsPickingUpWeapon() && !bWantsToAim
		&& GetMechanismState(HeldSlot) && HasAmmoWheel(Held);
}

void UGinkgoWeaponComponent::OpenAmmoWheel()
{
	const UGinkgoWeaponDefinition* Held = GetHeldWeapon();
	if (!Held)
	{
		return;
	}
	AmmoWheel.bOpen = true;
	AmmoWheel.Slot = HeldSlot;
	AmmoWheel.Choices = UGinkgoAmmoDefinition::GetChoices(Held->AmmoType);
	AmmoWheel.Current = FMath::Max(AmmoWheel.Choices.IndexOfByKey(GetLoadAmmo(HeldSlot)), 0);
	AmmoWheel.Selected = AmmoWheel.Current;
	AmmoWheel.Motion = FVector2D::ZeroVector;
	AmmoWheel.Stick = FVector2D::ZeroVector;
	AmmoWheel.LastResult = EGinkgoAmmoWheelResult::None;
	++AmmoWheel.Serial;
	TStringBuilder<256> Choices;
	for (const FGameplayTag& Choice : AmmoWheel.Choices)
	{
		Choices.Appendf(TEXT("%s%s %d"), Choices.Len() > 0 ? TEXT("，") : TEXT(""), *Choice.ToString(), AmmoReserve.Count(Choice));
	}
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 弹种轮盘打开（第 %d 个槽 %s）：%s，正在装 %s"), *GetNameSafe(GetOwner()), HeldSlot + 1, *Held->GetName(), Choices.ToString(),
		*GetLoadAmmo(HeldSlot).ToString());
}

void UGinkgoWeaponComponent::ConfirmAmmoWheel()
{
	if (!AmmoWheel.bOpen)
	{
		return;
	}
	const int32 Slot = AmmoWheel.Slot;
	const FGameplayTag Chosen = AmmoWheel.Choices.IsValidIndex(AmmoWheel.Selected) ? AmmoWheel.Choices[AmmoWheel.Selected] : FGameplayTag();
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	const FGinkgoMechanismState* State = GetMechanismState(Slot);
	if (!Weapon || !State || !Chosen.IsValid() || AmmoReserve.Count(Chosen) <= 0)
	{
		UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 弹种轮盘：选的 %s 后备里没有，什么也不改"), *GetNameSafe(GetOwner()), *Chosen.ToString());
		CloseAmmoWheel(EGinkgoAmmoWheelResult::Unavailable);
		return;
	}
	const FGameplayTag Before = GetLoadAmmo(Slot);
	LoadAmmo[Slot] = Chosen;
	// 换了弹种就装填（你说的：松开即确认）。下一枪还不是它、机构能直接压膛的（霰弹枪）先从抛壳口压一发（紧急装填的片段，你说的）；
	// 98k 走平常的装填：开栓（膛里那一发回后备）、压这一种、闭栓，下一枪就是它
	const FGameplayTag Next = Ginkgo::Weapon::GetNextRoundAmmo(*State);
	const bool bSingleShotSameReady = Weapon->Mechanism && Weapon->Mechanism->GetFeed() == EGinkgoFeed::SingleShot
		&& State->Action == EGinkgoAction::Closed && State->Chamber == EGinkgoChamber::Live && State->ChamberAmmo == Chosen;
	bWantsChamberSwap = Weapon->Mechanism->AllowsChamberLoad() && Next != Chosen;
	bWantsReload = !bSingleShotSameReady;
	bWantsFire = false;
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 弹种轮盘确认：%s → %s，下一枪是 %s，%s"), *GetNameSafe(GetOwner()), *Before.ToString(), *Chosen.ToString(),
		Next.IsValid() ? *Next.ToString() : TEXT("没有"), bWantsChamberSwap ? TEXT("从抛壳口压膛，再装填") : TEXT("装填"));
	CloseAmmoWheel(EGinkgoAmmoWheelResult::Confirmed);
}

void UGinkgoWeaponComponent::CloseAmmoWheel(const EGinkgoAmmoWheelResult Result)
{
	if (!AmmoWheel.bOpen)
	{
		return;
	}
	AmmoWheel.bOpen = false;
	AmmoWheel.LastResult = Result;
	++AmmoWheel.Serial;
	ReloadKey = FGinkgoReloadKey();
	UE_CLOG(Result == EGinkgoAmmoWheelResult::Cancelled, LogGinkgoWeapon, Log, TEXT("[Weapon] %s 弹种轮盘取消"), *GetNameSafe(GetOwner()));
}

void UGinkgoWeaponComponent::CancelReloadKey()
{
	CloseAmmoWheel(EGinkgoAmmoWheelResult::Cancelled);
	ReloadKey = FGinkgoReloadKey();
}

const UGinkgoAmmoDefinition* UGinkgoWeaponComponent::GetNextRoundAmmoDefinition(const int32 Slot) const
{
	const FGinkgoMechanismState* State = GetMechanismState(Slot);
	const FGameplayTag Next = State ? Ginkgo::Weapon::GetNextRoundAmmo(*State) : FGameplayTag();
	return Next.IsValid() ? UGinkgoAmmoDefinition::Find(Next) : nullptr;
}

void UGinkgoWeaponComponent::UpdateAmmoWheel(const bool bReloadHeld, const FVector2D& WheelMouse, const FVector2D& WheelStick, const float DeltaSeconds)
{
	const bool bPressed = bReloadPressed;
	bReloadPressed = false;
	// 手上的枪换了、不在手上了、要去别的槽：轮盘关上
	if (AmmoWheel.bOpen && (AmmoWheel.Slot != HeldSlot || TargetSlot != HeldSlot || !GetHeldWeapon() || IsPickingUpWeapon()))
	{
		CancelReloadKey();
	}
	const UGinkgoDeveloperSettings* Settings = UGinkgoDeveloperSettings::Get();
	const bool bHasWheel = HasAmmoWheel(GetCarried(TargetSlot));
	switch (Ginkgo::Weapon::StepReloadKey(ReloadKey, bPressed, bReloadHeld, bHasWheel, bHasWheel && CanOpenAmmoWheel(), Settings->AmmoWheelHoldTime, DeltaSeconds))
	{
	case EGinkgoReloadKeyAction::Reload:
		StartReload();
		break;
	case EGinkgoReloadKeyAction::OpenWheel:
		OpenAmmoWheel();
		break;
	case EGinkgoReloadKeyAction::ConfirmWheel:
		ConfirmAmmoWheel();
		break;
	case EGinkgoReloadKeyAction::None:
		break;
	}
	if (!AmmoWheel.bOpen)
	{
		return;
	}
	// 鼠标相对运动的方向、摇杆的方向（你说的）：选的变了界面才更新
	AmmoWheel.Motion = Ginkgo::Weapon::StepWheelMotion(AmmoWheel.Motion, WheelMouse, Settings->AmmoWheelMotionRadius, Settings->AmmoWheelMotionDecay, DeltaSeconds);
	AmmoWheel.Stick = WheelStick;
	const int32 Selected = Ginkgo::Weapon::SelectWheelEntry(AmmoWheel.Motion, AmmoWheel.Stick, AmmoWheel.Choices.Num(), AmmoWheel.Selected,
		Settings->AmmoWheelMotionDeadZone, Settings->AmmoWheelStickDeadZone);
	if (Selected != AmmoWheel.Selected)
	{
		UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 弹种轮盘选 %s"), *GetNameSafe(GetOwner()),
			AmmoWheel.Choices.IsValidIndex(Selected) ? *AmmoWheel.Choices[Selected].ToString() : TEXT("（没有）"));
		AmmoWheel.Selected = Selected;
		++AmmoWheel.Serial;
	}
}

bool UGinkgoWeaponComponent::IsSprintBlocked() const
{
	return WantsToShoot() || CurrentStep == FWeaponTags::Weapon_Step_Fire;
}

bool UGinkgoWeaponComponent::WantsToShoot() const
{
	if (bWantsFire)
	{
		return true;
	}
	const UGinkgoWeaponDefinition* Weapon = bTriggerHeld ? GetHeldWeapon() : nullptr;
	const FGinkgoMechanismState* State = Weapon && Weapon->FireMode == EGinkgoFireMode::Automatic ? GetMechanismState(HeldSlot) : nullptr;
	return State && (Weapon->Mechanism ? Weapon->Mechanism->CanFire(*State) : State->CanFire());
}

bool UGinkgoWeaponComponent::TraceBore(FVector& OutMuzzle, FVector& OutBore, FVector& OutPoint, bool* bOutHit, FHitResult* OutHit) const
{
	const AGinkgoCharacter* Character = GetCharacter();
	const UGinkgoWeaponDefinition* Weapon = GetHeldWeapon();
	const UPoseableMeshComponent* Gun = Character && Weapon ? Character->GetWeaponSlotComponent(HeldSlot) : nullptr;
	if (!Gun || Character->IsWeaponStowed(HeldSlot))
	{
		return false;
	}
	// 去掉枪上的踢动（第一人称时是缩小了的那一份）：真的枪管
	const FTransform GunTransform = AppliedGunKickTransform.Inverse() * Gun->GetComponentTransform();
	OutMuzzle = GunTransform.TransformPosition(GetMuzzleOffset(HeldSlot));
	OutBore = GunTransform.GetUnitAxis(EAxis::X);
	OutPoint = OutMuzzle + OutBore * BoreTraceDistance;
	FHitResult Hit;
	const bool bHit = Character->GetWorld()->LineTraceSingleByChannel(Hit, OutMuzzle, OutPoint, Ginkgo::Combat::WeaponTraceChannel,
		FCollisionQueryParams(SCENE_QUERY_STAT(GinkgoTrueAim), /*bTraceComplex*/ false, Character));
	if (bHit)
	{
		OutPoint = Hit.ImpactPoint;
		if (OutHit)
		{
			*OutHit = Hit;
		}
	}
	if (bOutHit)
	{
		*bOutHit = bHit;
	}
	return true;
}

bool UGinkgoWeaponComponent::TraceShot(FVector& OutMuzzle, FVector& OutPoint, bool& bOutHit, FHitResult* OutHit, const float CoverPenetration) const
{
	FVector Bore;
	if (!TraceBore(OutMuzzle, Bore, OutPoint, &bOutHit, OutHit))
	{
		return false;
	}
	const AGinkgoCharacter* Character = GetCharacter();
	const APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (HoldState.GetAdsAlpha() < AdsShotAlpha || !Controller || !Controller->PlayerCameraManager)
	{
		return true;
	}
	// 准星那条线：越肩开镜是这一帧画面的中心。带瞄具的枪（第一人称开镜）从瞄具的出瞳沿光轴：分划的尖就在光轴上，镜内的画面也是沿光轴从物镜拍的，
	// 同一条线。枪的网格现在的变换是上一帧画出来的那个，所以打的就是看到分划指着的地方（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）
	FVector LineStart;
	FVector LineDirection;
	if (Ginkgo::Scope::FSightFrame Sight; GetSightFrame(Sight))
	{
		LineStart = Sight.Eye;
		LineDirection = Sight.GetAxis();
	}
	else
	{
		const FMinimalViewInfo View = Controller->PlayerCameraManager->GetCameraCacheView();
		LineStart = View.Location;
		LineDirection = View.Rotation.Vector();
	}
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(GinkgoShot), /*bTraceComplex*/ false, Character);
	const FVector End = LineStart + LineDirection * BoreTraceDistance;
	FHitResult Hit;
	const bool bCenterHit = Character->GetWorld()->LineTraceSingleByChannel(Hit, LineStart, End, Ginkgo::Combat::WeaponTraceChannel, Params);
	// 穿得过东西的这一发（Docs/Design/Damage.md「穿射」）：准星的线后面、这一发穿得到的第一个活着的目标就是要打的。隔着墙瞄着墙后的人时子弹从枪口朝他去，
	// 下面挡在前面的墙照样先打到；不然朝墙上准星那一点去，过了墙按枪口与相机的视差偏开
	if (bCenterHit && CoverPenetration > 0.f && IsAimThroughCoverEnabled())
	{
		Hit = FindAimedTarget(*Character->GetWorld(), Hit, End, CoverPenetration, *Character);
	}
	OutPoint = bCenterHit ? FVector(Hit.ImpactPoint) : End;
	bOutHit = bCenterHit;
	if (bCenterHit && OutHit)
	{
		*OutHit = Hit;
	}
	// 枪口到那里有东西挡着：打在挡着的上面（贴着墙开镜时准星对着墙后面）。留一点，不把瞄的那个面算成挡着的
	const FVector Line = OutPoint - OutMuzzle;
	if (Character->GetWorld()->LineTraceSingleByChannel(Hit, OutMuzzle, OutMuzzle + Line * 0.999, Ginkgo::Combat::WeaponTraceChannel, Params))
	{
		OutPoint = Hit.ImpactPoint;
		bOutHit = true;
		if (OutHit)
		{
			*OutHit = Hit;
		}
	}
	return true;
}

UGinkgoAnimAction* UGinkgoWeaponComponent::StartStep(const FGameplayTag& Step)
{
	AGinkgoCharacter* Character = GetCharacter();
	UGinkgoAnimActionComponent* Actions = Character ? Character->GetAnimActions() : nullptr;
	if (!Actions)
	{
		return nullptr;
	}
	UGinkgoAnimAction* Action = nullptr;
	if (Step == FWeaponTags::Weapon_Step_Draw)
	{
		Action = Character->MakeDrawWeaponAction(TargetSlot);
	}
	else if (Step == FWeaponTags::Weapon_Step_Stow)
	{
		Action = Character->MakeStowWeaponAction(HeldSlot);
	}
	else if (Step == FWeaponTags::Weapon_Step_Fire)
	{
		Action = MakeFireAction(*Character);
	}
	else if (Step == FWeaponTags::Weapon_Step_DryFire)
	{
		Action = MakeDryFireAction(*Character);
	}
	else if (Step == FWeaponTags::Weapon_Step_CycleBolt || Step == FWeaponTags::Weapon_Step_CloseBolt || Step == FWeaponTags::Weapon_Step_OpenBolt
		|| Step == FWeaponTags::Weapon_Step_ReloadStart || Step == FWeaponTags::Weapon_Step_LoadRound || Step == FWeaponTags::Weapon_Step_ReloadEnd
		|| Step == FWeaponTags::Weapon_Step_ReloadTactical || Step == FWeaponTags::Weapon_Step_ReloadEmpty || Step == FWeaponTags::Weapon_Step_IdleRelax
		|| Step == FWeaponTags::Weapon_Step_LoadChamber)
	{
		Action = MakeGunStepAction(*Character, Step);
	}
	UE_CLOG(!Action, LogGinkgoWeapon, Warning, TEXT("[Weapon] 动作状态：不认识的一步 %s"), *Step.ToString());
	if (Action)
	{
		CurrentStep = Step;
		Actions->Start(Action);
	}
	return Action;
}

UGinkgoAnimAction* UGinkgoWeaponComponent::MakeFireAction(AGinkgoCharacter& Character)
{
	const UGinkgoWeaponDefinition* Weapon = GetHeldWeapon();
	if (!Weapon)
	{
		return nullptr;
	}
	bWantsFire = false;
	// 打出去的是哪一发（Docs/Design/Damage.md「命中」）：机构记着每一发的弹种，开火会把它拿走，先读。没记的用枪的弹种
	FGameplayTag FiredAmmo;
	if (const FGinkgoMechanismState* Firing = GetMechanismState(HeldSlot))
	{
		FiredAmmo = GetMechanism(HeldSlot) ? GetMechanism(HeldSlot)->GetFiringAmmo(*Firing) : (Firing->Chamber == EGinkgoChamber::Live ? Firing->ChamberAmmo : FGameplayTag());
	}
	const UGinkgoAmmoDefinition* Ammo = UGinkgoAmmoDefinition::Find(FiredAmmo.IsValid() ? FiredAmmo : Weapon->AmmoType);
	ApplyMechanismEvent(HeldSlot, EGinkgoMechanismEvent::Fire);
	// 多个配件都换枪声时按武器定义中的顺序取第一项，模板系数叠加所有已装配件。
	USoundBase* PartFireSound = nullptr;
	float PatternScaleOffset = 0.f;
	float FireNoiseRange = Weapon->FireNoiseRange;
	const FGinkgoInstalledAttachments& Installed = GetAttachments(HeldSlot);
	for (const FGinkgoWeaponAttachmentMount& Mount : Weapon->Attachments)
	{
		const UGinkgoWeaponAttachment* Part = Mount.Part;
		if (Part && Installed.Parts.Contains(Part))
		{
			PatternScaleOffset += Part->PatternScaleOffset;
			FireNoiseRange *= FMath::Max(Part->FireNoiseRangeScale, 0.01f);
			if (!PartFireSound)
			{
				PartFireSound = Part->FireSound.LoadSynchronous();
			}
		}
	}
	USoundBase* AmmoFireSound = !PartFireSound && Ammo ? Ammo->FireSound.LoadSynchronous() : nullptr;
	PlayCue(FWeaponTags::Weapon_Cue_Fire, PartFireSound ? PartFireSound : AmmoFireSound);
	if (AmmoFireSound)
	{
		UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 这一枪用子弹自己的枪声：%s"), *GetNameSafe(GetOwner()), *AmmoFireSound->GetName());
	}
	// 开火以后留在腰射（开镜时松开开镜也是腰射），留的时间从拉完算（UpdateHoldState）
	HoldState.HipHoldRemaining = Weapon->HipHoldTime;
	const float ShotInterval = Weapon->GetShotInterval();
	ShotRecoveryRemaining = ShotInterval;
	ShotRecoveryDuration = ShotInterval;
	const bool bAutomatic = Weapon->FireMode == EGinkgoFireMode::Automatic;
	// 这一枪排的时刻（连发接着响的是该响的那个时刻）：弹道的烟按它数间隔，同帧率无关
	const double ScheduledShotTime = bAutomatic ? Ginkgo::Weapon::GetScheduledShotTime(NextShotTime, GetWorld()->GetTimeSeconds(), ShotInterval, bShotChained)
		: GetWorld()->GetTimeSeconds();
	if (bAutomatic)
	{
		NextShotTime = Ginkgo::Weapon::GetNextShotTime(NextShotTime, GetWorld()->GetTimeSeconds(), ShotInterval, bShotChained);
	}

	// 瞄准后坐（Docs/Design/Weapons.md「后坐模板」）：到下一发的增量开始摊到视线上，这一发打的还是现在瞄的地方。第一发最准
	if (const UGinkgoRecoilPattern* Pattern = Weapon->PatternRecoil.Pattern)
	{
		const FGinkgoPatternRecoil& PatternRecoil = Weapon->PatternRecoil;
		FVector2D Scale = PatternRecoil.GetScale(HoldState.GetAdsAlpha(), SpreadInput.CrouchAlpha, PatternScaleOffset);
		if (Weapon->Modularity)
		{
			const FVector2D Minimum = PatternRecoil.GetScale(HoldState.GetAdsAlpha(), SpreadInput.CrouchAlpha, 0.f) * Weapon->Modularity->MinimumRecoilFraction;
			Scale.X = FMath::Max(Scale.X, Minimum.X);
			Scale.Y = FMath::Max(Scale.Y, Minimum.Y);
		}
		// 没有扰动时不取随机数：开枪的随机流取数的次数只看配置
		const FVector2D Jitter = Pattern->Jitter.IsNearlyZero() ? FVector2D::ZeroVector
			: FVector2D(ShotRandom.FRandRange(-Pattern->Jitter.X, Pattern->Jitter.X), ShotRandom.FRandRange(-Pattern->Jitter.Y, Pattern->Jitter.Y));
		Ginkgo::Weapon::AddRecoilKick(AimRecoil, *Pattern, PatternRecoil, Scale, Jitter);
	}

	// 后坐（Docs/Design/ActionStates.md「后坐」）：枪的踢动、相机的晃动（这把枪配的，每枪取一次数）；片段只管肩膀
	Ginkgo::Weapon::AddGunKick(GunKick, Weapon->GunKick, ShotRandom.FRandRange(-1.f, 1.f), ShotRandom.FRandRange(-1.f, 1.f));
	if (UGinkgoCameraContextComponent* Context = CameraContext.Get())
	{
		FGinkgoCameraEvent Event;
		Event.Tag = FWeaponTags::Weapon_Event_Fire;
		// 这一帧的开头：按下的这一帧，踢动已经走了一帧的长度
		Event.Time = GetWorld()->GetTimeSeconds() - GetWorld()->GetDeltaSeconds();
		for (const FGinkgoCameraRecoil& Recoil : Weapon->CameraRecoil)
		{
			Event.Impulses.Add(Ginkgo::Weapon::SampleCameraRecoil(Recoil, ShotRandom));
		}
		Context->PushEvent(Event);
	}

	// 声源在真实枪口，独立于声音资产加载与弹道查询是否成功；一次发射只提交一次。
	const UPoseableMeshComponent* NoiseMesh = Character.GetHeldWeaponMesh();
	const FVector NoiseLocation = NoiseMesh ? NoiseMesh->GetComponentTransform().TransformPosition(GetMuzzleOffset(HeldSlot)) : Character.GetActorLocation();
	Character.GetNoiseEmitter()->ReportShot(NoiseLocation, FireNoiseRange);
	FVector Muzzle, Point;
	bool bHit = false;
	FHitResult AimedHit;
	// 掩体穿射（Docs/Design/Damage.md「穿射」）：这一发的系数，开镜时准星的线、每颗弹丸都按它往后穿
	const float CoverPenetration = Ammo ? Ammo->CoverPenetration : 0.f;
	if (TraceShot(Muzzle, Point, bHit, &AimedHit, CoverPenetration))
	{
		// 这一枪打中了几下目标（命中的白 X 一枪弹一次）
		FGinkgoHitConfirm Shot;
		// 散布（Docs/Design/Weapons.md「散布」）：这一枪按开枪这一刻的扩散偏，打完才长，所以第一枪最准。红 X 画的是没偏的那条线，圆锥的中心。
		// 霰弹枪一枪几颗弹丸（PelletCount，你说的）：每颗各自按散布偏、各自打一条线，只用一发子弹；日志的"开火"写第一颗，别的写"弹丸"。
		// 一颗弹丸的枪取随机数的次数同以前
		const FVector AimedPoint = Point;
		const FVector Aimed = (Point - Muzzle).GetSafeNormal();
		const bool bAimedHit = bHit;
		// 子弹改的弹丸数（独头弹一颗，Docs/Design/Weapons.md「弹种」）；没改的照枪的
		const int32 Pellets = FMath::Max(Ammo && Ammo->WeaponEffects.PelletCount > 0 ? Ammo->WeaponEffects.PelletCount : Weapon->PelletCount, 1);
		UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 这一枪是 %s：%d 颗弹丸，扩散 %.3f°，排在 %.4f s"), *GetNameSafe(GetOwner()),
			Ammo ? *Ammo->Ammo.ToString() : TEXT("（没有子弹的数据）"), Pellets, HoldState.Spread, ScheduledShotTime);
		float ShotSpread = HoldState.Spread;
#if !UE_BUILD_SHIPPING
		ShotSpread *= FMath::Max(GSpreadScale, 0.f);
#endif
		FVector Direction = Aimed;
		float Deviation = 0.f;
		// 每颗弹丸打到的地方：弹道的烟画到这里
		TArray<FVector, TInlineAllocator<16>> TrailEnds;
		TArray<FGinkgoBulletImpact> Impacts;
		const bool bProjectile = Ammo && Ammo->Delivery == EGinkgoAmmoDelivery::Projectile;
		for (int32 Pellet = 0; Pellet < Pellets; ++Pellet)
		{
			const FVector PelletDirection = Ginkgo::Weapon::SampleSpreadDirection(Aimed, ShotSpread, SpreadRandom);
			const float PelletDeviation = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Aimed | PelletDirection, -1.0, 1.0)));
			if (bProjectile)
			{
				const float MuzzleSpeed = FMath::Max(Ammo->Trail.Speed, 10.f) * 100.f;
				FActorSpawnParameters SpawnParams;
				SpawnParams.Owner = &Character;
				SpawnParams.Instigator = &Character;
				SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				AGinkgoGrenadeProjectile* Projectile = Character.GetWorld()->SpawnActor<AGinkgoGrenadeProjectile>(
					AGinkgoGrenadeProjectile::StaticClass(), FTransform(PelletDirection.ToOrientationQuat(), Muzzle), SpawnParams);
				if (Projectile)
				{
					Projectile->InitializeProjectile(PelletDirection * MuzzleSpeed, Ammo, Weapon->DamageMultiplier, this);
				}
				if (Pellet == 0)
				{
					Direction = PelletDirection;
					Deviation = PelletDeviation;
					UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 发射抛射物 %s：枪口 %s，方向 %s，初速 %.0f cm/s，扩散 %.2f°，偏了 %.2f°"),
						*GetNameSafe(GetOwner()), *Ammo->Ammo.ToString(), *Muzzle.ToCompactString(), *Direction.ToCompactString(),
						MuzzleSpeed, HoldState.Spread, Deviation);
				}
				continue;
			}
			FVector PelletPoint = AimedPoint;
			bool bPelletHit = bAimedHit;
			FHitResult PelletHit = AimedHit;
			if (PelletDeviation > 0.f)
			{
				PelletPoint = Muzzle + PelletDirection * BoreTraceDistance;
				bPelletHit = Character.GetWorld()->LineTraceSingleByChannel(PelletHit, Muzzle, PelletPoint, Ginkgo::Combat::WeaponTraceChannel,
					FCollisionQueryParams(SCENE_QUERY_STAT(GinkgoShot), /*bTraceComplex*/ false, &Character));
				if (bPelletHit)
				{
					PelletPoint = PelletHit.ImpactPoint;
				}
			}
			// 从打到的第一个东西起往后穿（Docs/Design/Damage.md「穿射」）：穿不过的、系数不够的停在它里面，最多碰 8 个。
			// 弹丸停下的地方是弹道的烟画到的地方：没打到东西、穿过最后一个以后没再打到东西时是射线的最远处
			FGinkgoPenetrationPath Path;
			FVector PelletEnd = PelletPoint;
			if (bPelletHit)
			{
				const FVector Far = Muzzle + PelletDirection * BoreTraceDistance;
				TraceBulletPath(*Character.GetWorld(), PelletHit, Far, CoverPenetration, Character, Path);
				PelletEnd = Path.Last().bPassed ? Far : FVector(Path.Last().Hit.ImpactPoint);
				UE_CLOG(CoverPenetration > 0.f, LogGinkgoWeapon, Log, TEXT("[Weapon] %s 穿射 %d/%d：系数 %.4f，%d 个：%s%s"), *GetNameSafe(GetOwner()), Pellet + 1, Pellets,
					CoverPenetration, Path.Num(), *DescribePenetrationPath(Path), Path.Last().bPassed ? TEXT("，后面没东西") : TEXT(""));
			}
			// 伤害是子弹的（Docs/Design/Damage.md）：每颗弹丸打到的每个可打击的目标各算一下，穿过东西以后乘剩的系数
			for (int32 Index = 0; Index < Path.Num(); ++Index)
			{
				// 先记录接触时的局部坐标，致命伤结算可能在本帧切换布娃娃。
				FGinkgoBulletImpact Impact;
				if (FGinkgoBulletImpact::FromHit(Path[Index].Hit, Impact)) Impacts.Add(Impact);
				if (Ammo) HitTarget(Path[Index].Hit, *Ammo, *Weapon, Muzzle, PelletDirection, Shot, Path[Index].DamageScale, Index);
			}
			TrailEnds.Add(PelletEnd);
			if (Pellet == 0)
			{
				Direction = PelletDirection;
				Deviation = PelletDeviation;
				Point = PelletPoint;
				bHit = bPelletHit;
				UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 开火：枪口 %s，方向 %s，扩散 %.2f°，偏了 %.2f°，%s %s"), *GetNameSafe(GetOwner()), *Muzzle.ToCompactString(), *Direction.ToCompactString(),
					HoldState.Spread, Deviation, bHit ? TEXT("打到") : TEXT("没打到东西，最远"), *Point.ToCompactString());
			}
			else
			{
				UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 弹丸 %d/%d：方向 %s，偏了 %.2f°，%s %s"), *GetNameSafe(GetOwner()), Pellet + 1, Pellets, *PelletDirection.ToCompactString(),
					PelletDeviation, bPelletHit ? TEXT("打到") : TEXT("没打到东西，最远"), *PelletPoint.ToCompactString());
			}
#if !UE_BUILD_SHIPPING && ENABLE_DRAW_DEBUG
			if (GShowShots != 0)
			{
				// 弹道与打到的点（伤害的数字是 ginkgo.combat.showDamage）：穿过去的那几个橙色，停在里面的那个黄色
				DrawDebugLine(GetWorld(), Muzzle, PelletEnd, FColor::Red, false, 3.f, 0, 0.6f);
				for (const FGinkgoPenetrationHit& Entry : Path)
				{
					DrawDebugSphere(GetWorld(), Entry.Hit.ImpactPoint, Pellets > 1 ? 3.f : 6.f, 12, Entry.bPassed ? FColor::Orange : FColor::Yellow, false, 3.f);
				}
			}
#endif
		}
		if (!Impacts.IsEmpty())
		{
			// 本地模拟玩家武器的客户端只表现自己的命中，权威端模拟的射击同时通知旁观客户端。
			if (Character.HasAuthority()) MulticastBulletImpacts(Impacts);
			else MulticastBulletImpacts_Implementation(Impacts);
		}
		if (Shot.Hits > 0)
		{
			NotifyProjectileHitConfirm(Shot);
		}
		// 枪管热一点，枪口喷一下热气（Docs/Design/Weapons.md「枪管的热」）。槽里刚换的枪从凉的算
		if (HeatWeapons[HeldSlot].Get() != Weapon)
		{
			HeatWeapons[HeldSlot] = Weapon;
			BarrelHeat[HeldSlot] = 0.f;
		}
		BarrelHeat[HeldSlot] += Weapon->HeatPerShot;
		HeatPuff[HeldSlot] = 1.f;
		// 连发时隔一阵才冒一次硝烟（MuzzleSmokeInterval），中间的枪只有火光
		const double ShotTime = GetWorld()->GetTimeSeconds();
		const bool bSmoke = Weapon->MuzzleSmokeInterval <= 0.f || Weapon->MuzzleFlashOnly.IsNull() || ShotTime - LastMuzzleSmokeTime >= Weapon->MuzzleSmokeInterval - UE_KINDA_SMALL_NUMBER;
		if (UNiagaraSystem* Effect = bSmoke ? Weapon->MuzzleEffect.LoadSynchronous() : Weapon->MuzzleFlashOnly.LoadSynchronous())
		{
			if (bSmoke)
			{
				LastMuzzleSmokeTime = ShotTime;
			}
			// 朝枪的朝向放：火光沿枪管喷，不朝瞄的点
			const UPoseableMeshComponent* Gun = Character.GetWeaponSlotComponent(HeldSlot);
			const FRotator Rotation = Gun ? (AppliedGunKickTransform.Inverse() * Gun->GetComponentTransform()).Rotator() : Direction.Rotation();
			StartMuzzleWisp(Character, HeldSlot);
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Effect, Muzzle, Rotation, FVector(Weapon->MuzzleSmokeScale), /*bAutoDestroy*/ true, /*bAutoActivate*/ true, ENCPoolMethod::AutoRelease);
		}
		// 弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：配在子弹上；最短隔 BulletTrailMinInterval 一道（你说的：AK 的射速刚好每枪都留，MG3 对齐到它），
		// 按这一枪排的时刻数。枪口的硝烟另算（MuzzleSmokeInterval）
		if (Ammo && Ginkgo::Weapon::ShouldLeaveTrail(ScheduledShotTime, LastTrailShotTime, GetDefault<UGinkgoDeveloperSettings>()->BulletTrailMinInterval))
		{
			LastTrailShotTime = ScheduledShotTime;
			SpawnBulletTrails(*Ammo, Muzzle, TrailEnds);
		}
#if !UE_BUILD_SHIPPING
		++DebugShots.Count;
		DebugShots.Muzzle = Muzzle;
		DebugShots.Point = Point;
		DebugShots.bHit = bHit;
		DebugShots.AimedPoint = AimedPoint;
		DebugShots.Spread = HoldState.Spread;
		DebugShots.Deviation = Deviation;
#endif
	}
	// 扩散打完才长，上限按这一刻的开镜进度（与这一枪的子弹改的扩散）。这一帧的准星就张开，按下一枪的子弹重算
	SpreadInput.Bloom = FMath::Min(SpreadInput.Bloom + Weapon->Spread.GrowthPerShot,
		Ginkgo::Weapon::GetMaxSpreadBloom(Weapon->Spread, HoldState.GetAdsAlpha(), SpreadInput.HipScale, SpreadInput.AdsScale, SpreadInput.HipBaseScale, SpreadInput.AdsBaseScale));
	UpdateSpread(Weapon, 0.f);

	// 打完最后一发枪机挂住：放枪机留在后面的那份（枪上配了才有）
	const FGinkgoMechanismState* State = GetMechanismState(HeldSlot);
	const FGinkgoWeaponStep* LastClip = State && State->Action == EGinkgoAction::Open ? Weapon->Steps.Find(FWeaponTags::Weapon_Step_FireLast) : nullptr;
	const FGinkgoWeaponStep* Clip = LastClip ? LastClip : Weapon->Steps.Find(FWeaponTags::Weapon_Step_Fire);
	UAnimMontage* Montage = Clip ? Clip->Montage.Get() : nullptr;
	UE_CLOG(!Montage, LogGinkgoWeapon, Warning, TEXT("[Weapon] %s 没有开火的片段"), *Weapon->GetName());
	// 有射速的枪要准：动作做完以后下一帧树才接下一枪，所以按"下一帧到了没有"算
	const float NextFrame = Weapon->FireRate > 0.f ? 1.f : 0.f;
	// 连发按世界时间：片段的时刻从开枪那一帧才走，一帧的迟到会累积，射速就跟帧率走了
	const TWeakObjectPtr<UWorld> World = bAutomatic ? GetWorld() : nullptr;
	const double ShotDue = NextShotTime;
	return UGinkgoAnimAction::Make(&Character, TEXT("Fire"), [Montage, ShotInterval, NextFrame, World, ShotDue](FGinkgoAnimActionSetup& Setup)
	{
		if (Montage)
		{
			Setup.Play(Montage);
			// 到时间就算做完，树接下一步；片段接着放，下一步（同一个槽组）淡入时它交叉淡出
			Setup.OnTick([ShotInterval, NextFrame, World, ShotDue](FGinkgoAnimActionContext& Context, const float DeltaSeconds)
			{
				const bool bDue = World.IsValid() ? World->GetTimeSeconds() + DeltaSeconds >= ShotDue : Context.GetPosition() + NextFrame * DeltaSeconds >= ShotInterval;
				if (bDue)
				{
					Context.Complete();
				}
			});
		}
	})->Occupy(FAnimActionTags::Occupancy_RightArm_Hand, Ginkgo::Occupancy::PriorityGunAction);
}

UGinkgoAnimAction* UGinkgoWeaponComponent::MakeDryFireAction(AGinkgoCharacter& Character)
{
	bWantsFire = false;
	// 没弹了转去装填（你定的）。要压的那一种没了换成还有的；霰弹枪枪里一发也没有，先从抛壳口压一发（紧急装填）
	ResolveLoadAmmo(TargetSlot);
	bWantsReload = CanLoadTargetWeapon();
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 想开火，不能响：咔%s"), *GetNameSafe(GetOwner()), bWantsReload ? TEXT("，转去装填") : TEXT(""));
	return UGinkgoAnimAction::Make(&Character, TEXT("DryFire"), [](FGinkgoAnimActionSetup&) {});
}

UGinkgoAnimAction* UGinkgoWeaponComponent::MakeGunStepAction(AGinkgoCharacter& Character, const FGameplayTag& Step)
{
	const UGinkgoWeaponDefinition* Weapon = GetHeldWeapon();
	// 同一步按情况换片段（枪配了才换，霰弹枪，Docs/Design/ActionStates.md「霰弹枪」）：管里空位、后备都够两发时一次压两发（你定的）；
	// 枪在低持时泵动用低持那条（手沿着枪走，低持时枪朝别的方向，叠加不能共用）
	FGameplayTag ClipStep = Step;
	const FGinkgoMechanismState* StateBefore = GetMechanismState(HeldSlot);
	// 压的是这个槽选的弹种（Docs/Design/Weapons.md「切换弹种」）：事件发生时再读，压到一半换了弹种的从下一发起换
	const FGameplayTag LoadBefore = GetLoadAmmo(HeldSlot);
	if (Weapon && Step == FWeaponTags::Weapon_Step_LoadRound && Weapon->Steps.Contains(FWeaponTags::Weapon_Step_LoadRoundTwin) && GetMechanism(HeldSlot) && StateBefore
		&& Ginkgo::Weapon::ShouldLoadTwo(GetMechanism(HeldSlot)->GetCapacity() - StateBefore->Feed.Num(), AmmoReserve.Count(LoadBefore)))
	{
		ClipStep = FWeaponTags::Weapon_Step_LoadRoundTwin;
	}
	else if (Weapon && Step == FWeaponTags::Weapon_Step_CycleBolt && HoldState.GetRaiseAlpha() < 0.5f && Weapon->Steps.Contains(FWeaponTags::Weapon_Step_CycleBoltLowered))
	{
		ClipStep = FWeaponTags::Weapon_Step_CycleBoltLowered;
	}
	UE_CLOG(ClipStep != Step, LogGinkgoWeapon, Log, TEXT("[Weapon] %s 这一步 %s 放 %s 的片段"), *GetNameSafe(GetOwner()), *Step.ToString(), *ClipStep.ToString());
	const FGinkgoWeaponStep* Clip = Weapon ? Weapon->Steps.Find(ClipStep) : nullptr;
	UAnimMontage* Montage = Clip ? Clip->Montage.Get() : nullptr;
	const int32 Slot = HeldSlot;
	const bool bCycle = Step == FWeaponTags::Weapon_Step_CycleBolt;
	// 98k 的托弹板在弹仓空了时挡着枪栓：拉开就停。管状弹仓（霰弹枪）没有这一下，打空了泵动照样推回去
	const bool bHoldOpenOnEmptyFeed = Weapon && GetMechanism(HeldSlot) && GetMechanism(HeldSlot)->GetFeed() == EGinkgoFeed::InternalMagazine;
	// 手已经在拉柄上（刚开栓、压完一发）：闭栓从推回去开始。这一步配了从哪一段开始的（M1911 拔出来放套筒）照它。
	// 抛壳口装填时护木已经在后面（做到一半收枪再拔）：从放一发开始
	const bool bActionAlreadyOpen = StateBefore && StateBefore->Action == EGinkgoAction::Open;
	const bool bSingleShot = GetMechanism(HeldSlot) && GetMechanism(HeldSlot)->GetFeed() == EGinkgoFeed::SingleShot;
	const FName SingleShotResume = bSingleShot && Step == FWeaponTags::Weapon_Step_ReloadEmpty && bActionAlreadyOpen
		? (StateBefore->Chamber == EGinkgoChamber::Spent ? Ginkgo::WeaponSections::Extract : Ginkgo::WeaponSections::PortLoad)
		: NAME_None;
	const FName StartSection = Clip && !Clip->StartSection.IsNone() ? Clip->StartSection
		: (!SingleShotResume.IsNone() ? SingleShotResume
			: (Step == FWeaponTags::Weapon_Step_CloseBolt && bHandAtAction ? Ginkgo::WeaponSections::BoltClose
				: (Step == FWeaponTags::Weapon_Step_LoadChamber && bActionAlreadyOpen ? Ginkgo::WeaponSections::PortLoad : NAME_None)));
	bHandAtAction = false;
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	const bool bReload = Step == FWeaponTags::Weapon_Step_ReloadTactical || Step == FWeaponTags::Weapon_Step_ReloadEmpty;
	const float PlayRate = bReload ? 1.f / FGinkgoAttachmentEffects::TimeScale(GetAttachmentEffects(Slot).ReloadTime) : 1.f;
	UGinkgoAnimAction* Action;
	if (Montage)
	{
		const bool bEndsAtAction = Step == FWeaponTags::Weapon_Step_OpenBolt || Step == FWeaponTags::Weapon_Step_ReloadStart || Step == FWeaponTags::Weapon_Step_LoadRound;
		const bool bHoldOpen = bCycle && bHoldOpenOnEmptyFeed;
		Action = UGinkgoAnimAction::Make(&Character, *ClipStep.GetTagName().ToString(), [WeakThis, Montage, Slot, bHoldOpen, bEndsAtAction, StartSection, Step, PlayRate](FGinkgoAnimActionSetup& Setup)
		{
			Setup.Play(Montage, PlayRate, StartSection);
			Setup.On(Ginkgo::WeaponEvents::ActionOpen, [WeakThis, Slot, bHoldOpen](FGinkgoAnimActionEvent& Event)
			{
				ThisClass* Weapons = WeakThis.Get();
				if (!Weapons)
				{
					return;
				}
				Weapons->ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::ActionOpen);
				// 拉栓时弹仓空了：98 的托弹板挡着枪栓，推不回去，拉开就停（Docs/Design/ActionStates.md「枪记住什么」）
				const FGinkgoMechanismState* State = Weapons->GetMechanismState(Slot);
				if (bHoldOpen && State && State->Feed.IsEmpty())
				{
					Event.Context.SetNextSection(Ginkgo::WeaponSections::BoltOpen, NAME_None);
				}
			}, EGinkgoMissedEvent::Skip);
			Setup.On(Ginkgo::WeaponEvents::ActionClose, [WeakThis, Slot](FGinkgoAnimActionEvent&)
			{
				if (ThisClass* Weapons = WeakThis.Get())
				{
					Weapons->ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::ActionClose);
				}
			}, EGinkgoMissedEvent::Skip);
			Setup.On(Ginkgo::WeaponEvents::RoundIn, [WeakThis, Slot](FGinkgoAnimActionEvent&)
			{
				if (ThisClass* Weapons = WeakThis.Get())
				{
					if (Weapons->ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::RoundIn, Weapons->GetLoadAmmo(Slot)))
					{
						Weapons->PlayContactShake(Slot, Ginkgo::WeaponEvents::RoundIn);
					}
				}
			}, EGinkgoMissedEvent::Skip);
			// 抛壳口装填：手里那一发落进膛（霰弹枪的紧急装填、换弹种；M79 的单发入膛）
			Setup.On(Ginkgo::WeaponEvents::RoundChamber, [WeakThis, Slot](FGinkgoAnimActionEvent&)
			{
				if (ThisClass* Weapons = WeakThis.Get())
				{
					if (Weapons->ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::RoundChamber, Weapons->GetLoadAmmo(Slot)))
					{
						if (Weapons->GetMechanism(Slot) && Weapons->GetMechanism(Slot)->GetFeed() == EGinkgoFeed::SingleShot)
						{
							Weapons->bWantsReload = false;
						}
						Weapons->PlayContactShake(Slot, Ginkgo::WeaponEvents::RoundChamber);
					}
				}
			}, EGinkgoMissedEvent::Skip);
			Setup.On(Ginkgo::WeaponEvents::MagOut, [WeakThis, Slot](FGinkgoAnimActionEvent&)
			{
				if (ThisClass* Weapons = WeakThis.Get())
				{
					if (Weapons->ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::MagOut))
					{
						Weapons->PlayContactShake(Slot, Ginkgo::WeaponEvents::MagOut);
					}
				}
			}, EGinkgoMissedEvent::Skip);
			Setup.On(Ginkgo::WeaponEvents::MagIn, [WeakThis, Slot](FGinkgoAnimActionEvent&)
			{
				if (ThisClass* Weapons = WeakThis.Get())
				{
					if (Weapons->ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::MagIn))
					{
						Weapons->PlayContactShake(Slot, Ginkgo::WeaponEvents::MagIn);
					}
				}
			}, EGinkgoMissedEvent::Skip);
			if (Step == FWeaponTags::Weapon_Step_IdleRelax)
			{
				// 放完了（或者被打断）：从头再等
				Setup.OnFinished([WeakThis](const FGinkgoAnimActionResult&)
				{
					if (ThisClass* Weapons = WeakThis.Get())
					{
						Weapons->IdleStillTime = 0.f;
						Weapons->IdleRelaxWait = -1.f;
						Weapons->bIdleRelaxDue = false;
					}
				});
			}
			if (bEndsAtAction)
			{
				Setup.OnFinished([WeakThis](const FGinkgoAnimActionResult& Result)
				{
					if (ThisClass* Weapons = WeakThis.Get())
					{
						Weapons->bHandAtAction = Result.Outcome == EGinkgoAnimActionOutcome::Completed;
					}
				});
			}
		});
	}
	else
	{
		// 没有片段时事件当场发，不然每一帧都重选到这一步
		UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Weapon] %s 没有 %s 的片段"), *GetNameSafe(Weapon), *Step.ToString());
		if (bCycle || Step == FWeaponTags::Weapon_Step_OpenBolt || Step == FWeaponTags::Weapon_Step_LoadChamber)
		{
			ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::ActionOpen);
		}
		if (Step == FWeaponTags::Weapon_Step_LoadRound)
		{
			ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::RoundIn, LoadBefore);
			if (ClipStep == FWeaponTags::Weapon_Step_LoadRoundTwin)
			{
				ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::RoundIn, LoadBefore);
			}
		}
		if (Step == FWeaponTags::Weapon_Step_LoadChamber)
		{
			ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::RoundChamber, LoadBefore);
			ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::ActionClose);
		}
		if (Step == FWeaponTags::Weapon_Step_ReloadTactical || Step == FWeaponTags::Weapon_Step_ReloadEmpty)
		{
			if (bSingleShot)
			{
				ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::ActionOpen);
				ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::MagOut);
				ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::RoundChamber, LoadBefore);
				ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::ActionClose);
				bWantsReload = false;
			}
			else
			{
				ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::MagOut);
				ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::MagIn);
			}
		}
		const FGinkgoMechanismState* State = GetMechanismState(Slot);
		if (Step == FWeaponTags::Weapon_Step_CloseBolt || Step == FWeaponTags::Weapon_Step_ReloadEmpty || (bCycle && State && (!State->Feed.IsEmpty() || !bHoldOpenOnEmptyFeed)))
		{
			ApplyMechanismEvent(Slot, EGinkgoMechanismEvent::ActionClose);
		}
		Action = UGinkgoAnimAction::Make(&Character, *Step.GetTagName().ToString(), [](FGinkgoAnimActionSetup&) {});
	}
	// 放松不占手：优先级最低，拾取照常（你按 E 就打断）
	if (Step == FWeaponTags::Weapon_Step_IdleRelax)
	{
		return Action;
	}
	return Action->Occupy(FAnimActionTags::Occupancy_LeftArm, Ginkgo::Occupancy::PriorityGunAction)->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityGunAction);
}

void UGinkgoWeaponComponent::NotifyProjectileHitConfirm(FGinkgoHitConfirm Shot)
{
	if (Shot.Hits <= 0 || !GetWorld())
	{
		return;
	}
	Shot.Serial = HitConfirm.Serial + 1;
	Shot.Time = GetWorld()->GetTimeSeconds();
	HitConfirm = Shot;
	const AGinkgoCharacter* Character = GetCharacter();
	const EGinkgoHitSound HitSound = Ginkgo::Combat::SelectHitSound(Shot);
	const bool bHitSoundPlayed = Character && Character->IsPlayerControlled() && Character->IsLocallyControlled() && PlayHitSound(HitSound);
	UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 第 %d 次命中（%d 下）：声音 %s%s"), Shot.Serial, Shot.Hits, Ginkgo::Combat::DescribeHitSound(HitSound),
		bHitSoundPlayed ? TEXT("") : TEXT("（没放）"));
}

void UGinkgoWeaponComponent::ServerApplyFlashBurst_Implementation(const FVector_NetQuantize Location)
{
	if (UWorld* World = GetWorld())
	{
		AGinkgoGrenadeProjectile::ApplyFlashBurstToAI(*World, FVector(Location), GetOwner());
	}
}

bool UGinkgoWeaponComponent::PlayHitSound(const EGinkgoHitSound Kind)
{
	const FGinkgoHitSounds& Sounds = UGinkgoDeveloperSettings::Get()->HitSounds;
	const FGinkgoHitSound* Item = Sounds.Get(Kind);
	USoundBase* Sound = Item ? Item->Sound.LoadSynchronous() : nullptr;
	if (!Sound)
	{
		return false;
	}
	// 2D：不在世界里的哪一点，不随相机转
	UAudioComponent* Audio = UGameplayStatics::SpawnSound2D(this, Sound, Item->Volume);
	if (!Audio)
	{
		// 没有声音设备（-NoSound、-benchmark）
		return false;
	}
	if (Ginkgo::Combat::IsPriorityHitSound(Kind))
	{
		// 打穿护甲、打死、爆头打死走自己的队列：下一次命中碰不到它们，放完为止；满了最老的让位
		Ginkgo::Combat::PushPriorityHitSound(PriorityHitAudio, TWeakObjectPtr<UAudioComponent>(Audio), Sounds.PriorityCapacity,
			[](const TWeakObjectPtr<UAudioComponent>& Voice)
			{
				return Voice.IsValid() && Voice->IsPlaying();
			},
			[&Sounds](const TWeakObjectPtr<UAudioComponent>& Oldest)
			{
				UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 命中的声音的队列满了，最老的一声淡掉"));
				Oldest->FadeOut(Sounds.FadePrevious, 0.f);
			});
		return true;
	}
	// 命中、打在护甲上、爆头：一次只响一声，连发时不叠成一片
	UAudioComponent* Ringing = LastHitAudio.Get();
	if (Ringing && Ringing->IsPlaying() && Sounds.FadePrevious > 0.f)
	{
		Ringing->FadeOut(Sounds.FadePrevious, 0.f);
	}
	LastHitAudio = Audio;
	return true;
}

UAudioComponent* UGinkgoWeaponComponent::PlayCue(const FGameplayTag& Cue, USoundBase* Override)
{
	const UGinkgoWeaponDefinition* Weapon = GetHeldWeapon();
	const FGinkgoWeaponCue* Item = Weapon ? Weapon->Cues.Find(Cue) : nullptr;
	// 这个时刻带的相机震动（拉栓的几下，2026-11-12 你说的）：枪配的，只晃画面。动画的提示按声音的提前量早发，
	// 震动等到撞上的那一刻（这一帧末再过提前量）才推给相机。相机上下文只有本地控制的角色找过，别的角色不记
	const FGinkgoCueCameraShake* Shake = Weapon ? Weapon->CueCameraShakes.Find(Cue) : nullptr;
	if (Shake && !Shake->Layers.IsEmpty() && CameraContext.IsValid())
	{
		PendingCueShakes.Add({Weapon, Cue, GetWorld()->GetTimeSeconds() + (Item ? FMath::Max(static_cast<double>(Item->Lead), 0.0) : 0.0)});
		// 没有提前量的这一帧就推：从这一帧末晃起
		PushDueCueShakes();
	}
	return Item ? PlayCueAudio(*Weapon, Cue, *Item, Override, 1.f) : nullptr;
}

void UGinkgoWeaponComponent::PlayAttachmentContact(const FGameplayTag Preferred, const FGameplayTag Fallback, const float VolumeScale)
{
	const auto* Weapon = GetHeldWeapon();
	if (!Weapon) { return; }
	for (const auto& Cue : {Preferred, Fallback})
	{
		if (const auto* Item = Weapon->Cues.Find(Cue); Item && !Item->Sound.IsNull())
		{
			PlayCueAudio(*Weapon, Cue, *Item, nullptr, VolumeScale);
			return;
		}
	}
}

UAudioComponent* UGinkgoWeaponComponent::PlayCueAudio(const UGinkgoWeaponDefinition& Weapon, const FGameplayTag& Cue,
	const FGinkgoWeaponCue& Item, USoundBase* Override, const float VolumeScale)
{
#if !UE_BUILD_SHIPPING
	++DebugCues.FindOrAdd(Cue);
#endif
	const AGinkgoCharacter* Character = GetCharacter();
	USoundBase* Sound = Override ? Override : Item.Sound.LoadSynchronous();
	const float Volume = Item.Volume * VolumeScale;
	UPoseableMeshComponent* Gun = Character ? Character->GetWeaponSlotComponent(HeldSlot) : nullptr;
	if (Sound && Gun)
	{
		UAudioComponent* Audio = nullptr;
		if (Cue == FWeaponTags::Weapon_Cue_Fire)
		{
			// AI 在服务器上也是 locally controlled；直接判定玩家控制器，避免依赖可能尚未到达的 PlayerState。
			const APlayerController* OwningPlayer = Cast<APlayerController>(Character->GetController());
			if (OwningPlayer && OwningPlayer->IsLocalController())
			{
				Audio = UGameplayStatics::CreateSound2D(this, Sound, Volume);
				if (Audio)
				{
					// 近身枪声仍是游戏声音，暂停时随世界暂停，不借用 UI 声音的继续播放语义。
					Audio->bIsUISound = false;
					Audio->Play();
				}
			}
			else if (USoundAttenuation* Attenuation = Weapon.FireAttenuation.LoadSynchronous())
			{
				// 尾音留在射击发生处，不随开枪后移动的枪口走；衰减资产也对替换的消音/弹种枪声生效。
				const FVector Muzzle = Gun->GetComponentTransform().TransformPosition(GetMuzzleOffset(HeldSlot));
				Audio = UGameplayStatics::SpawnSoundAtLocation(this, Sound, Muzzle, Gun->GetComponentRotation(), Volume, 1.f, 0.f, Attenuation);
			}
		}
		else
		{
			Audio = UGameplayStatics::SpawnSoundAttached(Sound, Gun, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset, /*bStopWhenAttachedToDestroyed*/ false, Volume);
		}
		if (Audio && Item.FadePrevious > 0.f)
		{
			TWeakObjectPtr<UAudioComponent>& Previous = LastCueAudio.FindOrAdd(Cue);
			if (UAudioComponent* Ringing = Previous.Get())
			{
				Ringing->FadeOut(Item.FadePrevious, 0.f);
			}
			Previous = Audio;
		}
		return Audio;
	}
	return nullptr;
}

void UGinkgoWeaponComponent::PlayContactShake(const int32 Slot, const FName Contact)
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	UGinkgoCameraContextComponent* Context = CameraContext.Get();
	const FGinkgoCameraRecoil* Layer = Weapon ? Weapon->ContactCameraShakes.Find(Contact) : nullptr;
	if (Layer && Context)
	{
		FGinkgoCameraEvent Event;
		Event.Tag = FWeaponTags::Weapon_Event_Handling;
		Event.Time = GetWorld()->GetTimeSeconds();
		Event.Impulses.Add(Ginkgo::Weapon::SampleCameraRecoil(*Layer, HandlingRandom));
		Context->PushEvent(Event);
		UE_LOG(LogGinkgoWeapon, Verbose, TEXT("[Weapon] 动画接触震动 %s %s"), *Weapon->GetName(), *Contact.ToString());
	}
}

void UGinkgoWeaponComponent::PushDueCueShakes()
{
	UGinkgoCameraContextComponent* Context = CameraContext.Get();
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = 0; Index < PendingCueShakes.Num();)
	{
		const FPendingCueShake& Pending = PendingCueShakes[Index];
		if (Pending.Time > Now)
		{
			++Index;
			continue;
		}
		// 记下以后换了枪也照推（声音也照样响完）。用自己的随机流：开枪的随机流取数的次数只看开枪
		const UGinkgoWeaponDefinition* Weapon = Pending.Weapon.Get();
		const FGinkgoCueCameraShake* Shake = Weapon ? Weapon->CueCameraShakes.Find(Pending.Cue) : nullptr;
		if (Shake && Context)
		{
			FGinkgoCameraEvent Event;
			Event.Tag = FWeaponTags::Weapon_Event_Handling;
			// 撞上的那一刻：每帧开头推，落在这一帧里（叠加层把冲量加在这一刻，同帧率无关）
			Event.Time = Pending.Time;
			for (const FGinkgoCameraRecoil& Layer : Shake->Layers)
			{
				Event.Impulses.Add(Ginkgo::Weapon::SampleCameraRecoil(Layer, HandlingRandom));
			}
			Context->PushEvent(Event);
		}
		PendingCueShakes.RemoveAt(Index, EAllowShrinking::No);
	}
}

UNiagaraComponent* UGinkgoWeaponComponent::GetWispComponent(AGinkgoCharacter& Character, const int32 Slot, const UGinkgoWeaponDefinition& Weapon)
{
	UNiagaraSystem* System = Weapon.MuzzleWisp.LoadSynchronous();
	UPoseableMeshComponent* Gun = Character.GetWeaponSlotComponent(Slot);
	if (!System || !Gun)
	{
		return nullptr;
	}
	WispComponents.SetNum(SlotCount);
	if (!WispComponents[Slot])
	{
		// 没有画面的进程（FApp::CanEverRender）这里是空的
		WispComponents[Slot] = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Gun, NAME_None, GetMuzzleOffset(Slot), FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
			/*bAutoDestroy*/ false, /*bAutoActivate*/ false);
	}
	UNiagaraComponent* Component = WispComponents[Slot];
	if (Component)
	{
		Component->SetAsset(System);
		Component->SetRelativeLocation(GetMuzzleOffset(Slot));
	}
	return Component;
}

void UGinkgoWeaponComponent::StartMuzzleWisp(AGinkgoCharacter& Character, const int32 Slot)
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	if (!Weapon || Weapon->MuzzleWisp.IsNull())
	{
		return;
	}
	// 上一阵冒完了就从头来：接着冒的话，新的第一段会连到半空里上一阵的尾巴上（它们已经看不见了），跑着开枪是一条长斜线
	const bool bRestart = WispRemaining[Slot] <= 0.f;
	WispRemaining[Slot] = 1.f;
	if (UNiagaraComponent* Component = GetWispComponent(Character, Slot, *Weapon))
	{
		Component->Activate(bRestart);
	}
}

void UGinkgoWeaponComponent::SpawnBulletTrails(const UGinkgoAmmoDefinition& Ammo, const FVector& Start, const TConstArrayView<FVector> Ends)
{
	const FGinkgoAmmoTrail& Trail = Ammo.Trail;
	// 没有画面的进程（-nullrhi 的测量）建不出组件：形状、道数照算照记
	UNiagaraSystem* System = !Trail.System.IsNull() && AreBulletTrailsEnabled() ? Trail.System.LoadSynchronous() : nullptr;
	int32 Count = 0;
	int32 Drawn = 0;
	double Longest = 0.0;
	FGinkgoTrailShape LongestShape;
	for (const FVector& End : Ends)
	{
		const FVector Offset = End - Start;
		const double Length = Offset.Size();
		const FGinkgoTrailShape Shape = Ginkgo::Combat::MakeTrailShape(Trail, Length);
		if (Shape.Points <= 0)
		{
			continue;
		}
		++Count;
		if (Length > Longest)
		{
			Longest = Length;
			LongestShape = Shape;
		}
#if !UE_BUILD_SHIPPING
		++DebugShots.Trails;
		DebugShots.TrailLength = static_cast<float>(Length);
		DebugShots.TrailPoints = Shape.Points;
		DebugShots.TrailReveal = Shape.Reveal;
#endif
		// 放在枪口、不转：终点是相对起点的世界方向。池里的组件按完成回池
		UNiagaraComponent* Component = System ? UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Start, FRotator::ZeroRotator, FVector::OneVector,
			/*bAutoDestroy*/ true, /*bAutoActivate*/ false, ENCPoolMethod::AutoRelease) : nullptr;
		if (!Component)
		{
			continue;
		}
		Component->SetVariableVec3(TrailEndParameter, Offset);
		Component->SetVariableInt(TrailPointsParameter, Shape.Points);
		Component->SetVariableFloat(TrailLifetimeParameter, Trail.Lifetime);
		float Opacity = Trail.Opacity;
		float Width = Trail.Width;
		FLinearColor Color = Trail.Color;
#if !UE_BUILD_SHIPPING
		TArray<FString> Tune;
		GBulletTrailTune.ParseIntoArrayWS(Tune);
		if (Tune.Num() >= 3)
		{
			Opacity = FMath::Clamp(Opacity * FCString::Atof(*Tune[0]), 0.f, 1.f);
			Width *= FCString::Atof(*Tune[1]);
			const float Gray = FCString::Atof(*Tune[2]);
			Color = Gray >= 0.f ? FLinearColor(Gray, Gray, Gray) : Color;
		}
#endif
		Component->SetVariableFloat(TrailWidthParameter, Width * Ginkgo::Combat::TrailGeometryScale);
		Component->SetVariableFloat(TrailRevealParameter, Shape.Reveal);
		Component->SetVariableLinearColor(TrailColorParameter, FLinearColor(Color.R, Color.G, Color.B, Opacity));
		Component->Activate(/*bReset*/ true);
		++Drawn;
#if !UE_BUILD_SHIPPING
		DebugTrails.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Item) { return !Item.IsValid() || !Item->IsActive(); });
		DebugTrails.AddUnique(Component);
#endif
	}
	// 没有画面的进程里画了 0 道（组件建不出来）；ginkgo.weapon.trails 0 时也是 0
	UE_CLOG(Count > 0, LogGinkgoWeapon, Log, TEXT("[Weapon] %s 弹道的烟：%s %d 道（画了 %d 道），最长 %.1f cm，%d 个点，%.1f ms 长到头"), *GetNameSafe(GetOwner()), *Ammo.Ammo.ToString(),
		Count, Drawn, Longest, LongestShape.Points, Longest / (Trail.Speed * 100.0) * 1000.0);
}

#if !UE_BUILD_SHIPPING
int32 UGinkgoWeaponComponent::GetDebugTrailsAlive() const
{
	int32 Alive = 0;
	for (const TWeakObjectPtr<UNiagaraComponent>& Trail : DebugTrails)
	{
		Alive += Trail.IsValid() && Trail->IsActive() ? 1 : 0;
	}
	return Alive;
}
#endif

void UGinkgoWeaponComponent::MulticastBulletImpacts_Implementation(const TArray<FGinkgoBulletImpact>& Impacts)
{
	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer) || !FApp::CanEverRender()) return;
	if (UGinkgoBulletImpactSubsystem* Effects = World->GetSubsystem<UGinkgoBulletImpactSubsystem>())
	{
		for (const FGinkgoBulletImpact& Impact : Impacts) Effects->Play(Impact);
	}
}

void UGinkgoWeaponComponent::HitTarget(const FHitResult& Hit, const UGinkgoAmmoDefinition& Ammo, const UGinkgoWeaponDefinition& Weapon, const FVector& Muzzle,
	const FVector& Direction, FGinkgoHitConfirm& Shot, const float PenetrationScale, const int32 Penetrated)
{
	AActor* Target = Hit.GetActor();
	UGinkgoDamageableComponent* Damageable = Target ? Target->FindComponentByClass<UGinkgoDamageableComponent>() : nullptr;
	if (!Damageable || Damageable->IsDead())
	{
		return;
	}
	const FGinkgoDamageEvent Event = Damageable->ApplyBulletHit(Ammo, Weapon.DamageMultiplier, Hit, Direction, FVector::Dist(Muzzle, Hit.ImpactPoint), GetOwner(),
		PenetrationScale, Penetrated);
	++Shot.Hits;
	Shot.bKill |= Event.bKilled;
	Shot.bHead |= Event.Part == EGinkgoBodyPart::Head;
	Shot.bKillHead |= Event.bKilled && Event.Part == EGinkgoBodyPart::Head;
	Shot.bArmor |= Event.Outcome.bArmorHit;
	Shot.bArmorBroken |= Event.Outcome.bArmorBroken;
	Shot.HealthDamage += Event.Outcome.HealthDamage;
	Shot.ArmorDamage += Event.Outcome.ArmorDamage;
	Shot.Part = Event.Part;
}

UNiagaraComponent* UGinkgoWeaponComponent::GetHeatComponent(AGinkgoCharacter& Character, const int32 Slot, const UGinkgoWeaponDefinition& Weapon)
{
	UNiagaraSystem* System = Weapon.MuzzleHeat.LoadSynchronous();
	UPoseableMeshComponent* Gun = Character.GetWeaponSlotComponent(Slot);
	if (!System || !Gun)
	{
		return nullptr;
	}
	HeatComponents.SetNum(SlotCount);
	const FVector Location = GetMuzzleOffset(Slot) - FVector(Weapon.MuzzleHeatOffset, 0.0, 0.0);
	if (!HeatComponents[Slot])
	{
		// 没有画面的进程（FApp::CanEverRender）这里是空的
		HeatComponents[Slot] = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Gun, NAME_None, Location, FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset,
			/*bAutoDestroy*/ false, /*bAutoActivate*/ false);
	}
	UNiagaraComponent* Component = HeatComponents[Slot];
	if (Component)
	{
		Component->SetAsset(System);
		Component->SetRelativeLocation(Location);
	}
	return Component;
}

void UGinkgoWeaponComponent::SetBarrelHeat(const int32 Slot, const float Heat)
{
	// 记下是哪把枪的：不然下一帧当成换了枪，从凉的算
	HeatWeapons[Slot] = GetCarried(Slot);
	BarrelHeat[Slot] = FMath::Max(Heat, 0.f);
}

float UGinkgoWeaponComponent::GetHeatHazeAlpha(const int32 Slot) const
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	return Weapon ? Ginkgo::Weapon::HeatHazeAlpha(BarrelHeat[Slot], Weapon->HeatHazeStart, Weapon->HeatHazeFull, HeatPuff[Slot] * Weapon->HeatShotPuff) : 0.f;
}

void UGinkgoWeaponComponent::UpdateBarrelHeat(const float DeltaSeconds)
{
	static const FName ColorParameter(TEXT("User.HeatColor"));
	static const FName RateParameter(TEXT("User.HeatRate"));
	// 最热的时候一秒冒多少团：精灵 0.7–1.1 s，同时在的约 50 团，沿枪管 20 cm 铺满
	constexpr float MaxSpawnRate = 55.f;
	AGinkgoCharacter* Character = GetCharacter();
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
		if (HeatWeapons[Slot].Get() != Weapon)
		{
			HeatWeapons[Slot] = Weapon;
			BarrelHeat[Slot] = 0.f;
			HeatPuff[Slot] = 0.f;
		}
		if (Weapon)
		{
			BarrelHeat[Slot] = Ginkgo::Weapon::CoolBarrel(BarrelHeat[Slot], Weapon->HeatCoolTime, DeltaSeconds);
			HeatPuff[Slot] = Ginkgo::Weapon::CoolBarrel(HeatPuff[Slot], Ginkgo::Weapon::HeatPuffTime, DeltaSeconds);
		}
		const float Alpha = GetHeatHazeAlpha(Slot);
		UNiagaraComponent* Component = HeatComponents.IsValidIndex(Slot) ? HeatComponents[Slot].Get() : nullptr;
		// 不热了：不再冒，已经在飘的飘完。小于 1% 的看不出来
		if (Alpha < 0.01f || !Weapon || Weapon->MuzzleHeat.IsNull() || !IsHeatHazeEnabled())
		{
			if (Component && Component->IsActive())
			{
				Component->Deactivate();
			}
			continue;
		}
		if (!Component && Character)
		{
			Component = GetHeatComponent(*Character, Slot, *Weapon);
		}
		if (!Component)
		{
			continue;
		}
		Component->SetVariableLinearColor(ColorParameter, FLinearColor(1.f, 1.f, 1.f, Alpha));
		Component->SetVariableFloat(RateParameter, MaxSpawnRate * Alpha);
		if (!Component->IsActive())
		{
			Component->Activate(/*bReset*/ true);
		}
	}
}

void UGinkgoWeaponComponent::UpdateMuzzleWisps(const float DeltaSeconds)
{
	static const FName ColorParameter(TEXT("User.WispColor"));
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		float& Remaining = WispRemaining[Slot];
		if (Remaining <= 0.f)
		{
			continue;
		}
		UNiagaraComponent* Component = WispComponents.IsValidIndex(Slot) ? WispComponents[Slot].Get() : nullptr;
		const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
		// 枪没了，或者冒完了：不再冒，已经在飘的飘完
		Remaining = Weapon && Weapon->MuzzleWispTime > 0.f ? FMath::Max(0.f, Remaining - DeltaSeconds / Weapon->MuzzleWispTime) : 0.f;
		if (!Component)
		{
			continue;
		}
		if (Remaining <= 0.f)
		{
			Component->Deactivate();
		}
		else
		{
			Component->SetVariableLinearColor(ColorParameter, FLinearColor(0.85f, 0.85f, 0.85f, Ginkgo::Weapon::MuzzleWispAlpha(Remaining, Weapon->MuzzleWispOpacity)));
		}
	}
}

void UGinkgoWeaponComponent::EjectRound(const int32 Slot, const EGinkgoChamber Chamber, const FGameplayTag EjectedAmmo)
{
	AGinkgoCharacter* Character = GetCharacter();
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	const UPoseableMeshComponent* Gun = Character && Weapon ? Character->GetWeaponSlotComponent(Slot) : nullptr;
	if (!Gun || Chamber == EGinkgoChamber::Empty)
	{
		return;
	}
	const FGinkgoRoundVisuals& Rounds = Weapon->Rounds;
	const bool bLive = Chamber == EGinkgoChamber::Live;
	const UGinkgoAmmoDefinition* AmmoDef = bLive && EjectedAmmo.IsValid() ? UGinkgoAmmoDefinition::Find(EjectedAmmo) : nullptr;
	UStaticMesh* Mesh = bLive && AmmoDef && !AmmoDef->LiveRoundMesh.IsNull()
		? AmmoDef->LiveRoundMesh.LoadSynchronous()
		: (bLive ? Rounds.LiveRound : Rounds.SpentCase).LoadSynchronous();
	if (!Mesh)
	{
		return;
	}
	if (EjectedRounds.Num() < MaxEjectedRounds)
	{
		UStaticMeshComponent* Created = NewObject<UStaticMeshComponent>(Character, NAME_None, RF_Transient);
		Created->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		Created->SetCollisionObjectType(ECC_PhysicsBody);
		Created->SetCollisionResponseToAllChannels(ECR_Ignore);
		Created->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
		Created->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
		Created->SetGenerateOverlapEvents(false);
		Created->SetCanEverAffectNavigation(false);
		Created->CanCharacterStepUpOn = ECB_No;
		Created->SetCastShadow(false);
		// 小而快：一帧走得比它自己还长
		Created->BodyInstance.bUseCCD = true;
		Created->SetLinearDamping(0.05f);
		// 胶囊在平地上一直滚：滚慢一点就停
		Created->SetAngularDamping(1.5f);
		Created->SetNotifyRigidBodyCollision(true);
		Created->OnComponentHit.AddDynamic(this, &ThisClass::OnEjectedRoundHit);
		Created->RegisterComponent();
		EjectedRounds.Add(Created);
		EjectedRoundSounds.AddDefaulted();
	}
	UStaticMeshComponent* Round = EjectedRounds[NextEjectedRound];
	EjectedRoundSounds[NextEjectedRound].Weapon = Weapon;
	NextEjectedRound = (NextEjectedRound + 1) % MaxEjectedRounds;

	const FTransform GunTransform = Gun->GetComponentTransform();
	const FQuat GunRotation = GunTransform.GetRotation();
	Round->SetSimulatePhysics(false);
	Round->SetStaticMesh(Mesh);
	Round->SetPhysMaterialOverride(Rounds.EjectMaterial.LoadSynchronous());
	// 事件在动画更新里，枪已经走完这一帧，物理还要走这一帧：往回退一帧，不然跑着开枪时壳从枪前面出来（30 fps 冲刺时差 20 cm）
	const FVector GunVelocity = Character->GetVelocity();
	const FVector Start = GunTransform.TransformPosition(Rounds.EjectOffset) - GunVelocity * Character->GetWorld()->GetDeltaSeconds();
	Round->SetWorldTransform(FTransform(GunRotation, Start), /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	Round->SetSimulatePhysics(true);
	Round->SetPhysicsLinearVelocity(Ginkgo::Weapon::SampleEjectVelocity(Rounds, GunRotation, GunVelocity, EjectRandom));
	Round->SetPhysicsAngularVelocityInDegrees(Ginkgo::Weapon::SampleEjectSpin(Rounds, GunRotation, EjectRandom));
#if !UE_BUILD_SHIPPING
	++DebugEjectCount;
	DebugEjectLiveCount += bLive ? 1 : 0;
#endif
}

void UGinkgoWeaponComponent::OnEjectedRoundHit(UPrimitiveComponent* HitComponent, AActor* /*OtherActor*/, UPrimitiveComponent* /*OtherComponent*/, const FVector NormalImpulse, const FHitResult& Hit)
{
	const int32 Index = EjectedRounds.IndexOfByKey(HitComponent);
	const UGinkgoWeaponDefinition* Weapon = Index != INDEX_NONE ? EjectedRoundSounds[Index].Weapon.Get() : nullptr;
	const UWorld* World = GetWorld();
	if (!Weapon || !World)
	{
		return;
	}
	const FGinkgoRoundVisuals& Rounds = Weapon->Rounds;
	const double Mass = HitComponent->GetMass();
	// 贴在地上时物理每帧都报接触，冲量就是这一帧的重力（30 fps 约 33 cm/s）：扣掉，滚的时候没有声音，也与帧率无关
	const double Speed = Mass > 0.0 ? FMath::Max(NormalImpulse.Size() / Mass - FMath::Abs(World->GetGravityZ()) * World->GetDeltaSeconds(), 0.0) : 0.0;
	const float Volume = Ginkgo::Weapon::ShellHitVolume(Rounds, Speed);
	const double Now = World->GetTimeSeconds();
	if (Volume <= 0.f || Now - EjectedRoundSounds[Index].LastTime < Rounds.HitSoundInterval)
	{
		return;
	}
	EjectedRoundSounds[Index].LastTime = Now;
	const bool bHard = Speed >= Rounds.HardHitSpeed;
	if (USoundBase* Sound = (bHard ? Rounds.HitSoundHard : Rounds.HitSoundSoft).Get())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Hit.ImpactPoint, FRotator::ZeroRotator, Volume);
	}
#if !UE_BUILD_SHIPPING
	++DebugHitSounds;
	DebugHardHitSounds += bHard ? 1 : 0;
	DebugLastHitSpeed = Speed;
	DebugLastHitVolume = Volume;
#endif
}

#if !UE_BUILD_SHIPPING
UGinkgoWeaponComponent::FDebugEject UGinkgoWeaponComponent::GetDebugEject() const
{
	FDebugEject Result;
	Result.Count = DebugEjectCount;
	Result.LiveCount = DebugEjectLiveCount;
	Result.HitSounds = DebugHitSounds;
	Result.HardHitSounds = DebugHardHitSounds;
	Result.LastHitSpeed = DebugLastHitSpeed;
	Result.LastHitVolume = DebugLastHitVolume;
	if (DebugEjectCount > 0 && EjectedRounds.Num() > 0)
	{
		const UStaticMeshComponent* Latest = EjectedRounds[(NextEjectedRound + MaxEjectedRounds - 1) % MaxEjectedRounds];
		Result.Location = Latest->GetComponentLocation();
		Result.Speed = Latest->GetPhysicsLinearVelocity().Size();
	}
	return Result;
}
#endif

void UGinkgoWeaponComponent::PreloadFireAssets(AGinkgoCharacter& Character, const int32 Slot, const UGinkgoWeaponDefinition& Weapon)
{
	// 和枪声一起预载并持有，第一次敌人开枪时不再同步读衰减资产；范围与 AI 听觉参数相互独立。
	if (USoundAttenuation* Attenuation = Weapon.FireAttenuation.LoadSynchronous()) LoadedFireAssets.AddUnique(Attenuation);
	for (const TSoftObjectPtr<UNiagaraSystem>& Effect : {Weapon.MuzzleEffect, Weapon.MuzzleFlashOnly, Weapon.MuzzleWisp})
	{
		if (UNiagaraSystem* System = Effect.LoadSynchronous())
		{
			LoadedFireAssets.AddUnique(System);
		}
	}
	for (UObject* Asset : TArray<UObject*>{Weapon.Rounds.LiveRound.LoadSynchronous(), Weapon.Rounds.SpentCase.LoadSynchronous(), Weapon.Rounds.EjectMaterial.LoadSynchronous()})
	{
		if (Asset)
		{
			LoadedFireAssets.AddUnique(Asset);
		}
	}
	if (Weapon.AmmoType.IsValid())
	{
		for (const FGameplayTag& Choice : UGinkgoAmmoDefinition::GetChoices(Weapon.AmmoType))
		{
			const UGinkgoAmmoDefinition* ChoiceAmmo = UGinkgoAmmoDefinition::Find(Choice);
			if (!ChoiceAmmo)
			{
				continue;
			}
			for (UObject* Asset : TArray<UObject*>{ChoiceAmmo->ProjectileMesh.LoadSynchronous(), ChoiceAmmo->LiveRoundMesh.LoadSynchronous(),
				ChoiceAmmo->RoundMaterial.LoadSynchronous(), ChoiceAmmo->FireSound.LoadSynchronous()})
			{
				if (Asset)
				{
					LoadedFireAssets.AddUnique(Asset);
				}
			}
		}
	}
	// 细烟是普通发射器，第一次激活要约 30 ms（实测，第一枪最长的一帧从 9 ms 到 33 ms）：放在枪进槽的时候
	WispRemaining[Slot] = 0.f;
	if (UNiagaraComponent* Wisp = GetWispComponent(Character, Slot, Weapon))
	{
		Wisp->Activate(true);
		Wisp->DeactivateImmediate();
	}
	// 热浪也是普通发射器：同样先激活一次
	if (UNiagaraComponent* Heat = GetHeatComponent(Character, Slot, Weapon))
	{
		Heat->Activate(true);
		Heat->DeactivateImmediate();
	}
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 第 %d 个槽读好开枪要用的：%s，连发时隔 %.3f s 才冒一次硝烟，弹道的烟最短隔 %.3f s"), *GetNameSafe(GetOwner()), Slot + 1,
		*Weapon.GetName(), Weapon.MuzzleSmokeInterval, GetDefault<UGinkgoDeveloperSettings>()->BulletTrailMinInterval);
	// 弹道的烟也是普通发射器：这把枪能用的每种子弹的系统先读进来，建一个、激活一次（0 个点，什么也不画），回池
	for (const FGameplayTag& Choice : UGinkgoAmmoDefinition::GetChoices(Weapon.AmmoType))
	{
		const UGinkgoAmmoDefinition* Ammo = UGinkgoAmmoDefinition::Find(Choice);
		UNiagaraSystem* System = Ammo && !Ammo->Trail.System.IsNull() ? Ammo->Trail.System.LoadSynchronous() : nullptr;
		if (!System || LoadedFireAssets.Contains(System))
		{
			continue;
		}
		LoadedFireAssets.Add(System);
		if (UNiagaraComponent* Warm = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Character.GetActorLocation(), FRotator::ZeroRotator, FVector::OneVector,
			/*bAutoDestroy*/ true, /*bAutoActivate*/ false, ENCPoolMethod::AutoRelease))
		{
			Warm->SetVariableInt(TrailPointsParameter, 0);
			Warm->Activate(true);
			Warm->DeactivateImmediate();
		}
	}
	const auto PreloadSound = [this](const TSoftObjectPtr<USoundBase>& Path)
	{
		USoundBase* Sound = Path.LoadSynchronous();
		if (!Sound || LoadedFireAssets.Contains(Sound))
		{
			return;
		}
		LoadedFireAssets.Add(Sound);
		// 第一次放现建 MetaSound 的图要多等约 25 ms：先建好一份
		UMetaSoundSource* MetaSound = Cast<UMetaSoundSource>(Sound);
		const FAudioDeviceHandle Device = GetWorld() ? GetWorld()->GetAudioDevice() : FAudioDeviceHandle();
		UMetaSoundCacheSubsystem* Cache = MetaSound && Device.IsValid() ? Device->GetSubsystem<UMetaSoundCacheSubsystem>() : nullptr;
		if (Cache)
		{
			Cache->PrecacheMetaSound(MetaSound);
		}
	};
	for (const TPair<FGameplayTag, FGinkgoWeaponCue>& Cue : Weapon.Cues)
	{
		PreloadSound(Cue.Value.Sound);
	}
	for (const FGinkgoWeaponAttachmentMount& Mount : Weapon.Attachments)
	{
		if (Mount.Part)
		{
			PreloadSound(Mount.Part->FireSound);
		}
	}
	PreloadSound(Weapon.Rounds.HitSoundHard);
	PreloadSound(Weapon.Rounds.HitSoundSoft);
	// 命中的声音（设置的 HitSounds）：第一次打中也不等。读过的不再读
	const FGinkgoHitSounds& HitSounds = UGinkgoDeveloperSettings::Get()->HitSounds;
	for (const EGinkgoHitSound Kind : {EGinkgoHitSound::Normal, EGinkgoHitSound::Armor, EGinkgoHitSound::ArmorBreak, EGinkgoHitSound::Head, EGinkgoHitSound::Kill, EGinkgoHitSound::HeadKill})
	{
		if (const FGinkgoHitSound* Item = HitSounds.Get(Kind))
		{
			PreloadSound(Item->Sound);
		}
	}
	// 这把枪能用的子弹自己的枪声（穿甲弹、独头弹）
	for (const FGameplayTag& Choice : UGinkgoAmmoDefinition::GetChoices(Weapon.AmmoType))
	{
		if (const UGinkgoAmmoDefinition* Ammo = UGinkgoAmmoDefinition::Find(Choice))
		{
			PreloadSound(Ammo->FireSound);
		}
	}
}

void UGinkgoWeaponComponent::EndStep(UGinkgoAnimAction* Action)
{
	bShotChained = CurrentStep == FWeaponTags::Weapon_Step_Fire && Action && Action->IsFinished() && Action->GetResult().Outcome == EGinkgoAnimActionOutcome::Completed;
	// 先放开目标槽：取消的拔枪换回徒手时目标槽跟着走，不然重启以后又拔一遍
	CurrentStep = FGameplayTag();
	const AGinkgoCharacter* Character = GetCharacter();
	UGinkgoAnimActionComponent* Actions = Character ? Character->GetAnimActions() : nullptr;
	if (Action && !Action->IsFinished() && Actions)
	{
		Actions->Cancel(Action, FAnimActionTags::AnimAction_Cancel_OwnerExit);
	}
}

const UStateTree* UGinkgoWeaponComponent::GetActionTree()
{
	if (!bActionTreeLoaded)
	{
		bActionTreeLoaded = true;
		const TSoftObjectPtr<UStateTree>& TreePath = UGinkgoDeveloperSettings::Get()->WeaponStateTree;
		const UStateTree* Tree = TreePath.IsNull() ? nullptr : TreePath.LoadSynchronous();
		ActionTree = Tree && Tree->IsReadyToRun() ? Tree : nullptr;
		UE_CLOG(!ActionTree, LogGinkgoWeapon, Warning, TEXT("[Weapon] 动作状态的树 %s 读不到或没编译，换枪的键不响应。树由命令 GinkgoWeaponStateTree 生成"), *TreePath.ToString());
	}
	return ActionTree;
}

bool UGinkgoWeaponComponent::SetupActionContext(FStateTreeExecutionContext& Context)
{
	if (!Context.IsValid())
	{
		return false;
	}
	Context.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateUObject(this, &ThisClass::CollectActionData));
	// 子树在选状态时读：手上的枪有自己的子树就换上，没有用根树链着的那棵
	if (const UGinkgoWeaponDefinition* Weapon = GetHeldWeapon(); Weapon && Weapon->ActionTree)
	{
		FStateTreeReference Reference;
		Reference.SetStateTree(Weapon->ActionTree);
		FStateTreeReferenceOverrides Overrides;
		Overrides.AddOverride(FWeaponTags::Weapon_State_Action, Reference);
		Context.SetLinkedStateTreeOverrides(MoveTemp(Overrides));
	}
	return Context.AreContextDataViewsValid();
}

bool UGinkgoWeaponComponent::CollectActionData(const FStateTreeExecutionContext& Context, const UStateTree* StateTree, const TArrayView<const FStateTreeExternalDataDesc> Descs,
	const TArrayView<FStateTreeDataView> OutDataViews)
{
	for (int32 Index = 0; Index < Descs.Num(); ++Index)
	{
		if (Descs[Index].Struct && Descs[Index].Struct->IsChildOf(ThisClass::StaticClass()))
		{
			OutDataViews[Index] = FStateTreeDataView(this);
		}
	}
	return true;
}

void UGinkgoWeaponComponent::TickActionStates(const float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoWeaponActionStates);
	const UStateTree* Tree = GetActionTree();
	if (!Tree)
	{
		return;
	}
	FStateTreeExecutionContext Context(*this, *Tree, ActionStateData);
	if (!SetupActionContext(Context))
	{
		return;
	}
	if (Context.GetStateTreeRunStatus() != EStateTreeRunStatus::Running)
	{
		const EStateTreeRunStatus Status = Context.Start();
		UE_CLOG(Status != EStateTreeRunStatus::Running, LogGinkgoWeapon, Warning, TEXT("[Weapon] 动作状态的树没跑起来：%s"), *UEnum::GetValueAsString(Status));
	}
	Context.Tick(DeltaSeconds);
	bShotChained = false;
	if (!CurrentStep.IsValid())
	{
		bHandAtAction = false;
	}
}

void UGinkgoWeaponComponent::StopActionStates()
{
	if (!ActionTree || ActionStateData.Num() == 0)
	{
		return;
	}
	FStateTreeExecutionContext Context(*this, *ActionTree, ActionStateData);
	if (SetupActionContext(Context))
	{
		Context.Stop();
	}
}

void UGinkgoWeaponComponent::RestartActionStates()
{
	TargetSlot = HeldSlot;
	bWantsFire = false;
	bWantsReload = false;
	bWantsChamberSwap = false;
	CancelReloadKey();
	AimRecoil = FGinkgoRecoilState();
	bRecoilViewValid = false;
	StopActionStates();
}

#if !UE_BUILD_SHIPPING
FString UGinkgoWeaponComponent::GetActionStatePath() const
{
	if (!ActionTree || ActionStateData.Num() == 0)
	{
		return FString();
	}
	const FStateTreeReadOnlyExecutionContext Context(const_cast<ThisClass*>(this), ActionTree.Get(), const_cast<FStateTreeInstanceData&>(ActionStateData));
	return FString::JoinBy(Context.GetActiveStateNames(), TEXT("/"), [](const FName Name) { return Name.ToString(); });
}
#endif

void UGinkgoWeaponComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopActionStates();
	for (auto& Meshes : AttachmentMeshes) { Meshes.Reset(); }
	Super::EndPlay(EndPlayReason);
}

void UGinkgoWeaponComponent::UpdateIdleRelax(const AGinkgoCharacter& Character, const UGinkgoWeaponDefinition* Weapon, const float DeltaSeconds)
{
	const UGinkgoCharacterMovementComponent* Movement = Character.GetGinkgoMovement();
	// 视线在转也算动：转镜头时左手不该放开枪（度/秒）
	constexpr double StillViewSpeed = 5.0;
	const bool bRelaxing = CurrentStep == FWeaponTags::Weapon_Step_IdleRelax;
	const bool bStill = Weapon && Weapon->Steps.Contains(FWeaponTags::Weapon_Step_IdleRelax) && Movement->IsMovingOnGround() && Movement->Velocity.SizeSquared2D() < 1.0
		&& Movement->GetCurrentAcceleration().IsNearlyZero() && HoldState.Stance == EGinkgoHoldStance::Lowered && HoldState.RaiseProgress <= 0.f && !bWantsToAim && !bWantsFire && !bWantsReload
		&& !bWantsChamberSwap && !AmmoWheel.bOpen
		&& (!CurrentStep.IsValid() || bRelaxing) && HipAim.Velocity.SizeSquared() < StillViewSpeed * StillViewSpeed;
	if (!bStill)
	{
		IdleStillTime = 0.f;
		IdleRelaxWait = -1.f;
		bIdleRelaxDue = false;
		return;
	}
	if (IdleRelaxWait < 0.f)
	{
		IdleRelaxWait = IdleRelaxRandom.FRandRange(Weapon->IdleRelaxDelay.Min, Weapon->IdleRelaxDelay.Max);
#if !UE_BUILD_SHIPPING
		IdleRelaxWait = GRelaxDelay >= 0.f ? GRelaxDelay : IdleRelaxWait;
#endif
	}
	IdleStillTime += DeltaSeconds;
	bIdleRelaxDue = IdleStillTime >= IdleRelaxWait;
}

void UGinkgoWeaponComponent::UpdateRecoil(const AGinkgoCharacter& Character, const float DeltaSeconds)
{
	RecoilMovedThisFrame = FVector2D::ZeroVector;
	AController* Controller = Character.GetController();
	if (!Controller)
	{
		return;
	}
	const FRotator View = Controller->GetControlRotation();
	const FVector2D PlayerDelta = bRecoilViewValid
		? FVector2D(FRotator::NormalizeAxis(View.Yaw - LastRecoilView.Yaw), FRotator::NormalizeAxis(View.Pitch - LastRecoilView.Pitch))
		: FVector2D::ZeroVector;
	const FVector2D Moved = Ginkgo::Weapon::StepRecoil(AimRecoil, PlayerDelta, DeltaSeconds);
	RecoilMovedThisFrame = Moved;
	LastRecoilView = View;
	bRecoilViewValid = true;
	if (!Moved.IsZero())
	{
		LastRecoilView.Yaw += Moved.X;
		LastRecoilView.Pitch += Moved.Y;
		Controller->SetControlRotation(LastRecoilView);
	}
}

void UGinkgoWeaponComponent::UpdateHipAim(const AGinkgoCharacter& Character, const UGinkgoWeaponDefinition* Weapon, const float DeltaSeconds)
{
	const AController* Controller = Character.GetController();
	if (!Controller)
	{
		return;
	}
	const FRotator View = Controller->GetControlRotation();
	// 枪同后坐一起走（是枪把视线顶上去的），手感弹簧只算玩家自己转的：腰射的落点同开镜一样走模板
	const FVector2D ViewDelta = bHipAimViewValid
		? FVector2D(FRotator::NormalizeAxis(View.Yaw - LastHipAimView.Yaw), FRotator::NormalizeAxis(View.Pitch - LastHipAimView.Pitch)) - RecoilMovedThisFrame
		: FVector2D::ZeroVector;
	LastHipAimView = View;
	bHipAimViewValid = true;
	if (!Weapon || Character.IsWeaponStowed(HeldSlot))
	{
		HipAim = FGinkgoHipAimState();
		return;
	}
	const float HandlingScale = FMath::Max(.1f, 1.f + GetAttachmentEffects(HeldSlot).HipHandling);
	Ginkgo::Weapon::StepHipAim(HipAim, ViewDelta, Weapon->HipHandlingOmega * HandlingScale, Weapon->HipHandlingZeta, Weapon->HipHandlingMaxLag, DeltaSeconds);
}

FRotator UGinkgoWeaponComponent::GetAimRotation(const FRotator& View) const
{
	const FRotator Aim = Ginkgo::Weapon::MakeHipAimRotation(View, HipAim.Lag, HoldState.GetAdsAlpha());
	// 第一人称开镜：开镜的晃动晃的是枪（视角空间里转，同叠加层晃画面的那一份），瞄具连同分划一起晃；
	// 开枪的相机后坐枪也跟着转（同相机叠加层这一帧的那一份），瞄具里的画面同画面一起跳
	if (SightSway.IsZero() && SightRecoil.IsZero())
	{
		return Aim;
	}
	return (Aim.Quaternion() * SightRecoil.Quaternion() * SightSway.Quaternion()).Rotator();
}

const FGinkgoScope* UGinkgoWeaponComponent::GetHeldScope() const
{
	const UGinkgoWeaponDefinition* Weapon = GetHeldWeapon();
	return Weapon && IsScopeEnabled() ? UGinkgoDeveloperSettings::Get()->FindScope(Weapon->AdsReticle) : nullptr;
}

bool UGinkgoWeaponComponent::GetSightFrame(Ginkgo::Scope::FSightFrame& OutSight) const
{
	const FGinkgoScope* Scope = GetHeldScope();
	const AGinkgoCharacter* Character = Scope ? GetCharacter() : nullptr;
	const UPoseableMeshComponent* Gun = Character ? Character->GetHeldWeaponMesh() : nullptr;
	if (!Gun || Character->IsWeaponStowed(HeldSlot))
	{
		return false;
	}
	OutSight = Ginkgo::Scope::MakeSightFrame(Gun->GetComponentTransform(), *Scope);
	return true;
}

float UGinkgoWeaponComponent::GetFirstPersonAlpha() const
{
	return Ginkgo::Scope::FirstPersonAlpha(HoldState.GetAdsAlpha(), GetHeldScope() != nullptr);
}

bool UGinkgoWeaponComponent::CanPlaceOnWorkbench() const
{
	return HeldSlot == TargetSlot && !CurrentStep.IsValid() && !IsPickingUpWeapon()
		&& !IsAmmoWheelOpen() && !bWantsFire && !bWantsReload && !bTriggerHeld;
}

bool UGinkgoWeaponComponent::IsShotStepClear() const
{
	if (const auto* Character = GetCharacter(); Character && Character->GetTraversal()->IsActive()) { return false; }
	return !CurrentStep.IsValid() || CurrentStep == FWeaponTags::Weapon_Step_Fire || CurrentStep == FWeaponTags::Weapon_Step_DryFire;
}

void UGinkgoWeaponComponent::InterruptHandRequests()
{
	CancelFireIntent();
	CancelReloadKey();
	bWantsReload = false;
	bWantsChamberSwap = false;
	bReloadPressed = false;
	bWantsToAim = false;
	AbandonTarget();
}

void UGinkgoWeaponComponent::InterruptForDeath()
{
	InterruptHandRequests();
	StopActionStates();
	HoldState = {};
	if (auto* Context = UGinkgoCameraContextComponent::Find(GetOwner()))
	{
		Context->SetTags(EGinkgoCameraContextSource::Weapon, {});
		Context->SetScalar(FWeaponTags::Weapon_Value_AdsAlpha, 0.f);
		Context->SetSight(nullptr, nullptr, FTransform::Identity);
		Context->SetHeadHidden(false);
	}
}

void UGinkgoWeaponComponent::UpdateHoldState(const float DeltaSeconds)
{
	AGinkgoCharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	// 拉栓那几下的相机震动：到了撞上那一刻的推给相机（相机在所有 Actor 更新之后取这一帧的事件）
	PushDueCueShakes();
	// 开局的枪由服务器放进第一个槽、复制下来；复制到的那一帧直接拿到手上（不放拔枪），之后就照常
	if (const int32 StartSlot = FindDrawSlot(); !bStartHoldDone && !Character->GetWorkbench() && HeldSlot == INDEX_NONE && StartSlot != INDEX_NONE)
	{
		bStartHoldDone = true;
		if (UGinkgoDeveloperSettings::Get()->bStartWeaponInHand)
		{
			EquipStartingWeapon(StartSlot);
		}
	}

	// 捡枪的收拢那一步没跑起来（树没进拔枪）：过了时长也换回持枪的挂点，枪不一直挂在右手骨上
	if (PickupSettleSlot != INDEX_NONE && GetWorld()->GetTimeSeconds() - PickupSettleStart > PickupSettleDuration + 0.5)
	{
		EndPickupSettle();
	}

	// 按装填却压不了（满的、没子弹）、压满了：装填到此为止，闭栓由树接着做。
	// 管状弹仓压满了膛还空着不算完：树收回来、泵一下上膛，管里空出一个再补一发（Docs/Design/ActionStates.md「霰弹枪」）。
	// 要压的那一种后备没了：换成同一口径里还有的（特殊弹打完了接着装普通弹，Docs/Design/Weapons.md「切换弹种」）
	if (bWantsReload)
	{
		ResolveLoadAmmo(TargetSlot);
	}
	if (bWantsReload && !CanLoadTargetWeapon() && !CanLoadTargetWeaponAfterCycle())
	{
		bWantsReload = false;
	}
	// 从抛壳口压膛：膛里已经是这一种的活弹（压完了）、后备没了、不能压时到此为止
	if (bWantsChamberSwap && !CanLoadTargetWeaponChamber())
	{
		bWantsChamberSwap = false;
	}

	// 在树之前走：开枪的这一帧恢复的进度是满的。壳等套筒退到后面才抛
	ShotRecoveryRemaining = FMath::Max(ShotRecoveryRemaining - DeltaSeconds, 0.f);
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		if (PendingEject[Slot] > 0.f)
		{
			PendingEject[Slot] -= DeltaSeconds;
			if (PendingEject[Slot] <= 0.f)
			{
				PendingEject[Slot] = 0.f;
				EjectRound(Slot, EGinkgoChamber::Spent);
			}
		}
	}

	// 相机上下文在开枪之前找好：开枪的这一帧就要发事件
	if (!CameraContext.IsValid())
	{
		CameraContext = UGinkgoCameraContextComponent::Find(Character);
	}

	// 树在姿态之前：这一帧进了拔枪、收枪的，这一帧就不开镜
	if (!Character->GetWorkbench() && (!Character->GetTraversal()->IsActive() || Character->GetTraversal()->IsRecovering()))
	{
		TickActionStates(DeltaSeconds);
	}
	// 这一帧开的枪，增量从这一帧开始摊
	UpdateRecoil(*Character, DeltaSeconds);

	const UGinkgoWeaponDefinition* Weapon = GetHeldWeapon();

	UpdateHipAim(*Character, Weapon, DeltaSeconds);
	UpdateIdleRelax(*Character, Weapon, DeltaSeconds);
	UpdateMuzzleWisps(DeltaSeconds);
	UpdateBarrelHeat(DeltaSeconds);

	// 枪的踢动：开枪在树里，所以开枪的这一帧已经走了一帧的长度。枪不在手上时清掉
	if (Weapon)
	{
		Ginkgo::Weapon::StepGunKick(GunKick, Weapon->GunKick, DeltaSeconds);
		GunKickTransform = Ginkgo::Weapon::MakeGunKickTransform(GunKick, Weapon->GunKick);
	}
	else
	{
		GunKick = FGinkgoGunKickState();
		GunKickTransform = FTransform::Identity;
	}

	// 枪在手上就占着右臂（优先级最低）：拔枪、收枪比它高，照常；拾取等于它，用不了右手
	if (UGinkgoAnimActionComponent* Actions = Character->GetAnimActions())
	{
		if (Weapon && HoldOccupancy == INDEX_NONE)
		{
			HoldOccupancy = Actions->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityHold);
		}
		else if (!Weapon && HoldOccupancy != INDEX_NONE)
		{
			Actions->ReleaseOccupancy(HoldOccupancy);
			HoldOccupancy = INDEX_NONE;
		}
	}
	// 持枪的占用放开以后：右手空了就在这一帧伸手
	UpdatePendingPickup();

	// 手上要拿的枪没了（收枪、丢枪）：想开火作废。想装填的在前面（CanLoadTargetWeapon）
	if (!GetCarried(TargetSlot))
	{
		bWantsFire = false;
	}
	if (Weapon)
	{
		if (!bRaiseAllowed)
		{
			// 拔枪、收枪、压弹：不抬枪。开完火接着收枪时不留在腰射，枪口对准不扯着收枪的片段
			HoldState.HipHoldRemaining = 0.f;
		}
		else if (WantsToShoot() || (CurrentStep.IsValid() && HoldState.HipHoldRemaining > 0.f))
		{
			// 想开火：先抬枪，抬好就开（树等 IsRaised）。开火、拉栓的时候不减，留在腰射的时间从拉完算：连着开不会每一枪都放下去再抬
			HoldState.HipHoldRemaining = Weapon->HipHoldTime;
		}
	}

	const UGinkgoCharacterMovementComponent* Movement = Character->GetGinkgoMovement();
	const FGinkgoScope* Scope = GetHeldScope();
	FGinkgoHoldInput Input;
	Input.bArmed = Weapon != nullptr;
	// 带瞄具的枪拉栓时留在镜后（2026-11-12 你选的）：拉栓的片段本来就是眼贴着瞄具做的，栓关上之前照旧开不了枪；装填照旧退出开镜
	const bool bScopeBoltAim = Scope && CurrentStep == FWeaponTags::Weapon_Step_CycleBolt;
	Input.bAimHeld = bWantsToAim && (bAimAllowed || bScopeBoltAim);
	// 冲刺按着却站着不动时不算冲刺：同冲刺的朝向，水平速度低于门槛时角色照常面向视角，枪也不放低
	Input.bGroundSprinting = Movement->IsSprintFacing() && Movement->Velocity.Size2D() >= Movement->GetConfig().SprintFacingMinSpeed;
	Input.bBothHandsOnGun = bBothHandsOnGun;
	Input.HipHoldRemaining = HoldState.HipHoldRemaining;
	EGinkgoHoldStance Stance = Ginkgo::Weapon::SelectStance(Input);
#if !UE_BUILD_SHIPPING
	if (GForcedStance >= 0 && Weapon)
	{
		// 枚举里 0 是徒手，命令的 0 是低持
		Stance = static_cast<EGinkgoHoldStance>(FMath::Clamp(GForcedStance + 1, 1, static_cast<int32>(EGinkgoHoldStance::Sprint)));
	}
#endif
	if (HoldState.Weapon != Weapon)
	{
		SpreadInput.Bloom = 0.f;
	}
	HoldState.Weapon = Weapon;
	HoldState.bBothHandsOnGun = bBothHandsOnGun;
	FGinkgoHoldTimings Timings = Weapon ? Weapon->GetHoldTimings() : FGinkgoHoldTimings();
	Timings.AdsTime *= FGinkgoAttachmentEffects::TimeScale(GetAttachmentEffects(HeldSlot).AdsTime);
	Ginkgo::Weapon::StepHoldState(HoldState, Stance, Timings, DeltaSeconds);
	UpdateSpread(Weapon, DeltaSeconds);

	// 地速倍率：枪在手上乘它的持枪倍率，开镜按开镜进度从 1 缓到它的开镜倍率。手上的槽拔枪开始时就换、枪落到背上时换回，倍率跟着
	// 腰射地速只影响未开镜的部分；ADS 单独用 AdsSpeed，过渡过程中连续插值。
	float HeldSpeedScale = Weapon ? Weapon->HeldSpeedScale
		* FMath::Lerp(FMath::Max(.1f, 1.f + GetAttachmentEffects(HeldSlot).HipSpeed), 1.f, HoldState.GetAdsAlpha()) : 1.f;
	float AdsSpeedScale = Weapon ? FMath::Lerp(1.f, FMath::Clamp(Weapon->AdsSpeedScale * (1.f + GetAttachmentEffects(HeldSlot).AdsSpeed), 0.f, 1.f), HoldState.GetAdsAlpha()) : 1.f;
#if !UE_BUILD_SHIPPING
	if (GSpeedScaleEnabled == 0)
	{
		HeldSpeedScale = AdsSpeedScale = 1.f;
	}
#endif
	Character->GetGinkgoMovement()->SetSpeedScales(HeldSpeedScale, AdsSpeedScale);

	// 第一人称开镜（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：开镜的晃动晃的是枪。取相机这一帧末的时钟上的噪声（同叠加层晃画面的那一份，
	// 相机在所有 Actor 更新之后才把时钟走到这一帧末），乘开镜进度（同画面那一份的包络）与第一人称的进度：画面那一份乘 1 − 第一人称的进度，两份加起来不跳
	SightSway = FRotator::ZeroRotator;
	SightRecoil = FRotator::ZeroRotator;
	SightOffset = FRotator::ZeroRotator;
	SightMoveSway = FRotator::ZeroRotator;
	if (const float FirstPerson = GetFirstPersonAlpha(); FirstPerson > 0.f)
	{
		const APlayerController* PlayerController = Cast<APlayerController>(Character->GetController());
		if (const AGinkgoPlayerCameraManager* Camera = PlayerController ? Cast<AGinkgoPlayerCameraManager>(PlayerController->PlayerCameraManager) : nullptr)
		{
			SightSway = Camera->SampleSightSway(Camera->GetClock() + DeltaSeconds) * (HoldState.GetAdsAlpha() * FirstPerson);
			// 开枪的相机后坐（view kick、camera shake）枪也跟着转（你说的：开镜时跳的角度同腰射一样）：相机这一帧末的那一份，按第一人称的进度混进来
			SightRecoil = Camera->PredictSightFollow(DeltaSeconds, FirstPerson) * FirstPerson;
		}
		// 枪相对眼睛的偏移（FGinkgoScopeHandling，2026-11-12 你手测以后说的：开火、移动、快速转镜头时枪离开准星、离开中心，镜子边上发黑），绕枪托转，
		// 同踢动一起由 Control Rig 乘在枪口对准以后的枪上；相机只跟视角转，眼睛只跟着走一部分。
		// 转镜头的落后就是腰射的手感弹簧：越肩时加在瞄准方向上（乘 1 − 开镜进度），第一人称时挪到这里（乘第一人称的进度），两份加起来不跳
		const FGinkgoScopeHandling& Handling = Scope->Handling;
		const double LagScale = static_cast<double>(FirstPerson) * Handling.AimLag;
		SightOffset = FRotator(HipAim.Lag.Y * LagScale, HipAim.Lag.X * LagScale, 0.0);
		// 走路：身体的水平速度换到视角坐标（前、右）。
		// 滑铲时当作站着（2026-11-12 你手测以后说的：滑铲过程中开镜不需要镜子抖动）：滑得比跑还快，照走路算一步一颠就成了一秒好几下的抖，
		// 侧着看时还偏、侧倾一大截。速度、地速都按 0 追，偏与颠由弹簧收回去，步的相位停住，不来回晃
		const bool bSliding = Movement->IsSliding();
		const AController* Controller = Character->GetController();
		const double ViewYaw = FMath::DegreesToRadians(Controller ? Controller->GetControlRotation().Yaw : Character->GetActorRotation().Yaw);
		const FVector Velocity = bSliding ? FVector::ZeroVector : Movement->Velocity;
		const FVector2D LocalVelocity(Velocity.X * FMath::Cos(ViewYaw) + Velocity.Y * FMath::Sin(ViewYaw), Velocity.Y * FMath::Cos(ViewYaw) - Velocity.X * FMath::Sin(ViewYaw));
		FVector MoveShift = FVector::ZeroVector;
		SightMoveSway = Ginkgo::Scope::StepMoveSway(MoveSway, Handling, LocalVelocity, Movement->IsMovingOnGround() && !bSliding, DeltaSeconds, &MoveShift) * FirstPerson;
		SightMoveShift = MoveShift * FirstPerson;
		SightOffset += SightMoveSway;
	}
	else
	{
		MoveSway = Ginkgo::Scope::FMoveSwayState();
		SightMoveShift = FVector::ZeroVector;
	}
	// 先绕枪托转，再挪（枪口对准以后的那一把的坐标）
	SightOffsetTransform = (SightOffset.IsZero() && SightMoveShift.IsZero()) || !Weapon ? FTransform::Identity
		: Ginkgo::Scope::MakePivotRotation(SightOffset, Weapon->GunKick.Pivot) * FTransform(SightMoveShift);
	// 枪上的踢动：第一人称时乘 KickScale，按第一人称的进度混过去（你说的：枪踢只是略微离开中心、轻微抖一下，画面的踢动、震动由相机那一层管）
	AppliedGunKickTransform = GunKickTransform;
	if (const float FirstPerson = GetFirstPersonAlpha(); FirstPerson > 0.f && Weapon)
	{
		const double Scale = FMath::Lerp(1.0, static_cast<double>(FMath::Clamp(Scope->Handling.KickScale, 0.f, 1.f)), static_cast<double>(FirstPerson));
		FGinkgoGunKickState Scaled = GunKick;
		Scaled.Back *= Scale;
		Scaled.Rotation = Scaled.Rotation * Scale;
		AppliedGunKickTransform = Ginkgo::Weapon::MakeGunKickTransform(Scaled, Weapon->GunKick);
	}

	// 相机上下文（本地控制的角色才走到这里）：开镜的标签、进度与这把枪的倍率。开镜的视场读标签与倍率，开镜晃动的包络读进度。
	// 手上没枪时倍率不写：开镜进度退回 0 的路上枪被拿走，视场不跳
	if (UGinkgoCameraContextComponent* Context = CameraContext.Get())
	{
		FGameplayTagContainer Tags;
		if (HoldState.Stance == EGinkgoHoldStance::Ads)
		{
			Tags.AddTag(FWeaponTags::Weapon_ADS);
		}
		Context->SetTags(EGinkgoCameraContextSource::Weapon, Tags);
		Context->SetScalar(FWeaponTags::Weapon_Value_AdsAlpha, HoldState.GetAdsAlpha());
		if (Weapon)
		{
			// 带瞄具的枪不拉近（仅镜内变焦，你说的）：倍率写 1，开镜的视场修改器照旧盖住滑铲的视场、臂长，只是不放大
			Context->SetScalar(FWeaponTags::Weapon_Value_AdsZoom, Scope ? 1.f : Weapon->AdsZoom);
		}
		// 瞄具装在手上那把枪的网格上：相机在更新时读它这一帧最终的变换（那时动画已经求完），去掉程序化的偏移就是眼睛的家。枪在背上时没有
		const bool bGunInHand = Weapon && !Character->GetWorkbench() && Character->IsHeldWeaponInHand();
		Context->SetSight(Scope && bGunInHand ? Character->GetHeldWeaponMesh() : nullptr, Scope, GetGunOffsetTransform());
	}
}

void UGinkgoWeaponComponent::UpdateSpread(const UGinkgoWeaponDefinition* Weapon, const float DeltaSeconds)
{
	AGinkgoCharacter* Character = GetCharacter();
	const UGinkgoCharacterMovementComponent* Movement = Character ? Character->GetGinkgoMovement() : nullptr;
	if (!Weapon || !Movement)
	{
		HoldState.Spread = 0.f;
		return;
	}
	const auto StepPosture = [DeltaSeconds](float& Progress, const bool bOn)
	{
		Progress = Ginkgo::Weapon::StepProgress(Progress, bOn, SpreadPostureBlendTime, SpreadPostureBlendTime, DeltaSeconds);
		return Ginkgo::Weapon::EaseProgress(Progress);
	};
	// 滑铲时胶囊也是蹲着的（蹲下才滑得起来）：只算滑铲。蹲着只算地面上的，空中蹲着不会更稳
	const bool bInAir = Movement->IsFalling();
	const bool bSliding = Movement->IsSliding();
	SpreadInput.AirAlpha = StepPosture(SpreadAirProgress, bInAir);
	SpreadInput.CrouchAlpha = StepPosture(SpreadCrouchProgress, Movement->IsCrouching() && !bSliding && !bInAir);
	SpreadInput.SlideAlpha = StepPosture(SpreadSlideProgress, bSliding);
	SpreadInput.AdsAlpha = HoldState.GetAdsAlpha();
	SpreadInput.Speed = Movement->Velocity.Size2D();
	// 下一枪的子弹改的扩散（独头弹开镜更准，你说的）：准星画的就是下一枪的圆锥
	const UGinkgoAmmoDefinition* Next = GetNextRoundAmmoDefinition(HeldSlot);
	SpreadInput.HipScale = Next ? Next->WeaponEffects.HipSpreadScale : 1.f;
	SpreadInput.AdsScale = Next ? Next->WeaponEffects.AdsSpreadScale : 1.f;
	// 上限按这一刻的开镜进度：开镜时比上限多的收掉，不然横线要等它收完才动
	const auto& Effects = GetAttachmentEffects(HeldSlot);
	SpreadInput.HipBaseScale = FMath::Max(.1f, 1.f + Effects.HipSpread);
	SpreadInput.AdsBaseScale = FMath::Max(.1f, 1.f + Effects.AdsSpread);
	SpreadInput.MovingScale = FMath::Max(0.f, 1.f + Effects.MovingSpread);
	SpreadInput.Bloom = FMath::Clamp(SpreadInput.Bloom - Weapon->Spread.RecoveryRate * DeltaSeconds, 0.f,
		Ginkgo::Weapon::GetMaxSpreadBloom(Weapon->Spread, SpreadInput.AdsAlpha, SpreadInput.HipScale, SpreadInput.AdsScale, SpreadInput.HipBaseScale, SpreadInput.AdsBaseScale));
	HoldState.Spread = Ginkgo::Weapon::ComputeSpread(Weapon->Spread, SpreadInput);
}

// ===== 机构与后备子弹 =====

void UGinkgoWeaponComponent::SyncMechanisms()
{
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
		if (MechanismWeapons[Slot] == Weapon)
		{
			continue;
		}
		MechanismWeapons[Slot] = Weapon;
		// 槽里换了枪：装填压枪的弹种（轮盘选的不跟着别的枪走）
		LoadAmmo[Slot] = Weapon ? Weapon->AmmoType : FGameplayTag();
		// 捡回来的枪：状态照地上那把的，不装满，后备也不再送（丢枪捡枪不能刷子弹）
		if (Weapon && Slot == PendingPickupSlot && Weapon == PendingPickupWeapon && bPendingPickupHasState)
		{
			Mechanisms[Slot] = PendingPickupState;
			continue;
		}
		Mechanisms[Slot] = Weapon && Weapon->Mechanism ? Weapon->Mechanism->MakeLoadedState(Weapon->AmmoType) : FGinkgoMechanismState();
		if (Weapon)
		{
			AmmoReserve.Add(Weapon->AmmoType, Weapon->StartingReserve);
			// 同一口径的特殊弹各带一些（写在子弹上：UGinkgoAmmoDefinition::StartingReserve，98k、AK 的数据资产不动）
			TStringBuilder<128> Extra;
			for (const FGameplayTag& Choice : UGinkgoAmmoDefinition::GetChoices(Weapon->AmmoType))
			{
				const UGinkgoAmmoDefinition* Definition = Choice != Weapon->AmmoType ? UGinkgoAmmoDefinition::Find(Choice) : nullptr;
				if (Definition && Definition->StartingReserve > 0)
				{
					AmmoReserve.Add(Choice, Definition->StartingReserve);
					Extra.Appendf(TEXT("，%s %d 发"), *Choice.ToString(), AmmoReserve.Count(Choice));
				}
			}
			UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 的第 %d 个槽拿到 %s：供弹 %d 发、膛里%s，后备 %s %d 发%s"), *GetNameSafe(GetOwner()), Slot + 1, *Weapon->GetName(),
				Mechanisms[Slot].Feed.Num(), Mechanisms[Slot].Chamber == EGinkgoChamber::Live ? TEXT("有弹") : TEXT("空"), *Weapon->AmmoType.ToString(),
				AmmoReserve.Count(Weapon->AmmoType), Extra.ToString());
		}
	}
}

const FGinkgoMechanismState* UGinkgoWeaponComponent::GetMechanismState(const int32 Slot) const
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	return Weapon && Weapon->Mechanism && MechanismWeapons[Slot] == Weapon ? &Mechanisms[Slot] : nullptr;
}

bool UGinkgoWeaponComponent::ApplyMechanismEvent(const int32 Slot, const EGinkgoMechanismEvent Event, FGameplayTag Ammo)
{
	const UGinkgoWeaponDefinition* Weapon = GetCarried(Slot);
	if (!Weapon || !GetMechanism(Slot) || MechanismWeapons[Slot] != Weapon)
	{
		return false;
	}
	if (!Ammo.IsValid())
	{
		Ammo = Weapon->AmmoType;
	}
	const EGinkgoChamber ChamberBefore = Mechanisms[Slot].Chamber;
	const FGameplayTag ChamberAmmoBefore = Mechanisms[Slot].ChamberAmmo;
	const bool bChanged = GetMechanism(Slot)->ApplyEvent(Mechanisms[Slot], AmmoReserve, Event, Ammo);
	if (bChanged && (Event == EGinkgoMechanismEvent::ActionOpen || Event == EGinkgoMechanismEvent::MagOut)
		&& ChamberBefore != EGinkgoChamber::Empty && Mechanisms[Slot].Chamber == EGinkgoChamber::Empty)
	{
		EjectRound(Slot, ChamberBefore, ChamberAmmoBefore);
	}
	// 自动装填：开火的同一个事件里膛就换了，壳等套筒退到后面才出来
	if (bChanged && Event == EGinkgoMechanismEvent::Fire && Mechanisms[Slot].Chamber != EGinkgoChamber::Spent)
	{
		if (PendingEject[Slot] > 0.f || Weapon->Rounds.EjectDelay <= 0.f)
		{
			EjectRound(Slot, EGinkgoChamber::Spent);
		}
		PendingEject[Slot] = Weapon->Rounds.EjectDelay;
	}
	UE_LOG(LogGinkgoWeapon, Verbose, TEXT("[Weapon] %s 第 %d 个槽 %s：%s"), *GetNameSafe(GetOwner()), Slot + 1, *StaticEnum<EGinkgoMechanismEvent>()->GetNameStringByValue(static_cast<int64>(Event)),
		bChanged ? TEXT("做了") : TEXT("前提不满足，没改"));
	return bChanged;
}

void UGinkgoWeaponComponent::SetMechanismState(const int32 Slot, const FGinkgoMechanismState& State)
{
	if (GetMechanismState(Slot))
	{
		Mechanisms[Slot] = State;
	}
}

void UGinkgoWeaponComponent::SetReserveRounds(const FGameplayTag& Ammo, const int32 Rounds)
{
	if (Ammo.IsValid())
	{
		AmmoReserve.Rounds.FindOrAdd(Ammo) = FMath::Max(Rounds, 0);
	}
}

void UGinkgoWeaponComponent::ApplyStartingReserve(const TConstArrayView<FGinkgoStartingAmmoEntry> Entries)
{
	if (!ensureMsgf(GetOwner() && GetOwner()->HasAuthority(), TEXT("开局后备弹药只在服务器上写入")))
	{
		return;
	}
	StartingReserveEntries.Reset(Entries.Num());
	AmmoReserve.Rounds.Reset();
	for (const FGinkgoStartingAmmoEntry& Entry : Entries)
	{
		if (!Entry.Ammo.IsValid())
		{
			continue;
		}
		FGinkgoStartingAmmoEntry Clamped;
		Clamped.Ammo = Entry.Ammo;
		Clamped.Rounds = FMath::Max(Entry.Rounds, 0);
		StartingReserveEntries.Add(Clamped);
		AmmoReserve.Rounds.FindOrAdd(Clamped.Ammo) = Clamped.Rounds;
	}
	++StartingReserveSerial;
	if (StartingReserveSerial == 0)
	{
		StartingReserveSerial = 1;
	}
	AppliedStartingReserveSerial = StartingReserveSerial;
	GetOwner()->ForceNetUpdate();
}

void UGinkgoWeaponComponent::OnRep_StartingReserve()
{
	TryApplyStartingReserve();
}

bool UGinkgoWeaponComponent::AreCarriedMechanismsSynced() const
{
	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		if (CarriedWeapons[Slot] != MechanismWeapons[Slot])
		{
			return false;
		}
	}
	return true;
}

void UGinkgoWeaponComponent::TryApplyStartingReserve()
{
	if (StartingReserveSerial == 0 || AppliedStartingReserveSerial == StartingReserveSerial || !AreCarriedMechanismsSynced())
	{
		return;
	}
	AmmoReserve.Rounds.Reset();
	for (const FGinkgoStartingAmmoEntry& Entry : StartingReserveEntries)
	{
		if (Entry.Ammo.IsValid())
		{
			AmmoReserve.Rounds.FindOrAdd(Entry.Ammo) = FMath::Max(Entry.Rounds, 0);
		}
	}
	AppliedStartingReserveSerial = StartingReserveSerial;
}

void UGinkgoWeaponComponent::AddReserveRounds(const FGameplayTag& Ammo, const int32 Rounds)
{
	if (!Ammo.IsValid() || Rounds <= 0)
	{
		return;
	}
	const int32 Before = AmmoReserve.Count(Ammo);
	AmmoReserve.Add(Ammo, Rounds);
	UE_LOG(LogGinkgoWeapon, Log, TEXT("[Weapon] %s 的后备 %s 加 %d 发：%d → %d"), *GetNameSafe(GetOwner()), *Ammo.ToString(), Rounds, Before, AmmoReserve.Count(Ammo));
}

void UGinkgoWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, CarriedWeapons);
	DOREPLIFETIME(ThisClass, InstalledAttachments);
	DOREPLIFETIME(ThisClass, StartingReserveEntries);
	DOREPLIFETIME(ThisClass, StartingReserveSerial);
}

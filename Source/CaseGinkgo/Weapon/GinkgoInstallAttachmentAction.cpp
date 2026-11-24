#include "Weapon/GinkgoInstallAttachmentAction.h"

#include "Character/GinkgoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Weapon/GinkgoWeaponAttachment.h"
#include "Weapon/GinkgoModularWeaponVisual.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"

namespace
{
	/** 配件在玩家探身遮住取物位置后出现的时间（秒）。 */
	constexpr float RevealTime = .60f;

	/** 送到挂点的片段时间（秒），与新件接触声共用。 */
	constexpr float PlaceEnd = 1.63f;

	/** 两次转腕及换握结束、开始收手的片段时间（秒）。 */
	constexpr float TurnEnd = 2.90f;

	/** 松手后身体和双手回到待机姿势所用的时间（秒）。 */
	constexpr float WithdrawTime = .63f;

	/** 松开旧件的时间（秒），同步驱动显隐和接触声。 */
	constexpr float UnseatTime = .45f;

	/** 旧件撤离挂点后，开始递送新件或空手撤回的时间（秒）。 */
	constexpr float RemovalEnd = 1.25f;

	/** 最后一次转腕到位的片段时间（秒），同时触发紧固声。 */
	constexpr float TightenTime = 2.73f;

	/** 拆旧件已完成前倾与伸手，新件阶段从该时刻接续（秒） */
	constexpr float ReplacementStart = .43f;

	/** 配件在起手位置相对正式挂点绕站位竖轴的偏航角（度）。 */
	constexpr double RestYaw = 32.0;

	/** M4A1 各模块专属利索动作的统一阶段时刻表（秒），由安装风格与是否拆旧/装新共同决定 */
	struct FModularSchedule
	{
		/** 真表示右手拆装、左手按稳护木；假表示左手拆装、右手按稳握把 */
		bool bRightHand = false;

		/** 本次是否需要先拆下旧模块 */
		bool bHasRemove = false;

		/** 本次是否需要装入新模块（空模块卸载时为假） */
		bool bHasInstall = true;

		/** 手到达旧件并开始脱位的时刻（秒） */
		float TReach = 0.f;

		/** 拆卸接触音（MagOut）触发时刻（秒） */
		float TUnseatAudio = 0.f;

		/** 旧件沿结构轴完全脱离挂点的时刻（秒） */
		float TExtract = 0.f;

		/** 旧件收入身体/固定相机盲区的时刻（秒） */
		float TPocketArrive = 0.f;

		/** 旧件在盲区内隐藏的时刻（秒），早于 TPocketArrive 避免穿帮 */
		float TVanish = 0.f;

		/** 新件在盲区内显形的时刻（秒） */
		float TReveal = 0.f;

		/** 手开始从盲区抽出新件的时刻（秒） */
		float TRevealDone = 0.f;

		/** 新件到达安装轴向对准预备点的时刻（秒） */
		float TApproach = 0.f;

		/** 新件推入/落入正式挂点的时刻（秒），触发 MagIn 接触音 */
		float TSeat = 0.f;

		/** 锁定/卡紧音效（SlideHome）触发时刻（秒） */
		float TLockAudio = 0.f;

		/** 锁定动作完成、配件完全静止在 MountWorld 的时刻（秒） */
		float TLockEnd = 0.f;

		/** 双手撤离、身体回正的完整动作时长（秒） */
		float TTotal = 0.f;
	};

	FModularSchedule BuildModularSchedule(const EGinkgoAttachmentInstallStyle Style, const bool bHasRemove, const bool bHasInstall)
	{
		FModularSchedule S;
		S.bHasRemove = bHasRemove;
		S.bHasInstall = bHasInstall;
		S.bRightHand = Style == EGinkgoAttachmentInstallStyle::StockSlide
			|| Style == EGinkgoAttachmentInstallStyle::PistolGrip
			|| Style == EGinkgoAttachmentInstallStyle::TriggerDropIn;

		float Reach = 0.31f;
		float UnseatAudioDelay = 0.f;
		float ExtractSpan = 0.16f;
		float StowSpan = 0.22f;
		float FreshCover = 0.34f;
		float SwapBeat = 0.06f;
		float ApproachSpan = 0.22f;
		float SeatSpan = 0.12f;
		float LockSpan = 0.14f;
		float WithdrawSpan = 0.36f;

		switch (Style)
		{
		case EGinkgoAttachmentInstallStyle::StockSlide:
			Reach = 0.32f; ExtractSpan = 0.16f; StowSpan = 0.22f; ApproachSpan = 0.22f; SeatSpan = 0.12f; LockSpan = 0.14f; WithdrawSpan = 0.36f;
			break;
		case EGinkgoAttachmentInstallStyle::PistolGrip:
			Reach = 0.31f; ExtractSpan = 0.16f; StowSpan = 0.22f; ApproachSpan = 0.22f; SeatSpan = 0.12f; LockSpan = 0.16f; WithdrawSpan = 0.36f;
			break;
		case EGinkgoAttachmentInstallStyle::TriggerDropIn:
			Reach = 0.30f; ExtractSpan = 0.13f; StowSpan = 0.21f; ApproachSpan = 0.20f; SeatSpan = 0.11f; LockSpan = 0.14f; WithdrawSpan = 0.35f;
			break;
		case EGinkgoAttachmentInstallStyle::ForegripRail:
			Reach = 0.31f; ExtractSpan = 0.16f; StowSpan = 0.20f; FreshCover = 0.34f; ApproachSpan = 0.22f; SeatSpan = 0.12f; LockSpan = 0.16f; WithdrawSpan = 0.35f;
			break;
		case EGinkgoAttachmentInstallStyle::HandguardSlide:
			Reach = 0.32f; ExtractSpan = 0.19f; StowSpan = 0.25f; ApproachSpan = 0.28f; SeatSpan = 0.16f; LockSpan = 0.14f; WithdrawSpan = 0.36f;
			break;
		case EGinkgoAttachmentInstallStyle::MuzzleQuickThread:
			Reach = 0.31f; UnseatAudioDelay = 0.10f; ExtractSpan = 0.20f; StowSpan = 0.26f; ApproachSpan = 0.24f; SeatSpan = 0.12f; LockSpan = 0.44f; WithdrawSpan = 0.36f;
			break;
		default:
			break;
		}

		if (bHasRemove)
		{
			S.TReach = Reach;
			S.TUnseatAudio = Reach + UnseatAudioDelay;
			S.TExtract = S.TReach + ExtractSpan;
			S.TPocketArrive = S.TExtract + StowSpan;
			S.TVanish = S.TPocketArrive - 0.025f;
		}
		if (!bHasInstall)
		{
			S.TTotal = S.TPocketArrive + WithdrawSpan;
			return S;
		}
		if (bHasRemove)
		{
			S.TReveal = S.TPocketArrive + 0.025f;
			S.TRevealDone = S.TPocketArrive + SwapBeat;
		}
		else
		{
			S.TReveal = FreshCover - 0.04f;
			S.TRevealDone = FreshCover;
		}
		S.TApproach = S.TRevealDone + ApproachSpan;
		S.TSeat = S.TApproach + SeatSpan;
		S.TLockEnd = S.TSeat + LockSpan;
		S.TLockAudio = S.TLockEnd - 0.02f;
		S.TTotal = S.TLockEnd + WithdrawSpan;
		return S;
	}
}

UGinkgoInstallAttachmentAction* UGinkgoInstallAttachmentAction::Make(USceneComponent& Table, const UGinkgoWeaponAttachment& Part,
	const FTransform& MountWorld, const FTransform& Stand, UPoseableMeshComponent* Gun,
	const UGinkgoWeaponAttachment* Previous, const FName ModuleBone, UGinkgoModularWeaponVisual* Visual)
{
	auto* Action = NewObject<UGinkgoInstallAttachmentAction>(Table.GetOwner());
	Action->SetActionName(TEXT("InstallAttachment"));
	Action->Table = &Table;
	Action->PartMesh = Part.Mesh.LoadSynchronous();
	Action->ModuleMesh = Part.ModuleMesh.LoadSynchronous();
	Action->PreviousMesh = Previous ? Previous->ModuleMesh.LoadSynchronous() : nullptr;
	Action->Gun = Gun;
	Action->Visual = Visual;
	Action->Part = &Part;
	Action->ModuleBone = ModuleBone;
	Action->bEmptyModule = Part.bEmptyModule;
	Action->WristTurn = Part.InstallWristTurn;
	Action->InstallStyle = Part.InstallStyle;
	Action->MountWorld = MountWorld;
	Action->Stand = Stand;
	Action->Occupy(FAnimActionTags::Occupancy_LeftArm, Ginkgo::Occupancy::PriorityWeaponSwap)
	      ->Occupy(FAnimActionTags::Occupancy_RightArm, Ginkgo::Occupancy::PriorityWeaponSwap);
	return Action;
}

void UGinkgoInstallAttachmentAction::OnStart(FGinkgoAnimActionSetup& Setup)
{
	USceneComponent* Rest = Table.Get();
	if (!Rest || (!PartMesh && !ModuleMesh && !bEmptyModule) || !Setup.GetOwner())
	{
		Setup.GetContext().Abort(FAnimActionTags::AnimAction_Cancel_OwnerExit);
		return;
	}
	bModuleWasHidden = Gun.IsValid() && Gun->IsBoneHiddenByName(ModuleBone);
	// 网格随执行动作的角色销毁，即使角色在动作中离开世界也不会留在桌上。
	if (ModuleMesh || bEmptyModule)
	{
		auto* Module = NewObject<UPoseableMeshComponent>(Setup.GetOwner());
		Module->SetSkinnedAssetAndUpdate(ModuleMesh);
		// 模块弹匣导出带有备用件；静止安装只展示原弹匣。
		Module->HideBoneByName(TEXT("mag_new"), EPhysBodyOp::PBO_None);
		Preview = Module;
	}
	else
	{
		auto* Static = NewObject<UStaticMeshComponent>(Setup.GetOwner());
		Static->SetStaticMesh(PartMesh);
		Preview = Static;
	}
	Preview->SetupAttachment(Rest);
	Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Preview->SetCanEverAffectNavigation(false);
	Preview->SetVisibility(false);
	Preview->RegisterComponent();
	if (PreviousMesh)
	{
		RemovedPreview = NewObject<UPoseableMeshComponent>(Setup.GetOwner());
		RemovedPreview->SetupAttachment(Rest);
		RemovedPreview->SetSkinnedAssetAndUpdate(PreviousMesh);
		RemovedPreview->HideBoneByName(TEXT("mag_new"), EPhysBodyOp::PBO_None);
		RemovedPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RemovedPreview->SetCanEverAffectNavigation(false);
		RemovedPreview->SetVisibility(false);
		RemovedPreview->RegisterComponent();
	}
	// 模块沿用共同枪空间，抓取点取模块中心；旧静态消音器保留已手测的握点。
	const FVector Center = ModuleMesh ? ModuleMesh->GetBounds().Origin : PartMesh ? PartMesh->GetBoundingBox().GetCenter() : FVector::ZeroVector;
	GripPoint = ModuleMesh ? Center : Center * .4;
	RemovedGripPoint = PreviousMesh ? PreviousMesh->GetBounds().Origin : GripPoint;

	if (InstallStyle != EGinkgoAttachmentInstallStyle::Default)
	{
		SampleModularStyle(0.f);
		Setup.HoldFor(ComputeModularDuration());
		return;
	}

	const FVector Up = Stand.GetUnitAxis(EAxis::Z);
	const FVector Forward = Stand.GetUnitAxis(EAxis::X);
	const FVector Right = Stand.GetUnitAxis(EAxis::Y);
	FVector Pocket = Stand.GetLocation() + Forward * 15.0 - Right * 18.0;
	Pocket.Z = Rest->GetComponentLocation().Z + 11.0;
	const FQuat StartRotation = FQuat(Up, FMath::DegreesToRadians(RestYaw)) * MountWorld.GetRotation();
	StartLocation = Pocket - StartRotation.RotateVector(MountWorld.GetScale3D() * Center);
	SamplePose(0.f);
	Setup.HoldFor(bEmptyModule ? RemovalEnd + WithdrawTime
		: (WristTurn != 0.f ? TurnEnd : PlaceEnd) + WithdrawTime + (PreviousMesh ? RemovalEnd - ReplacementStart : 0.f));
}

void UGinkgoInstallAttachmentAction::OnTick(FGinkgoAnimActionContext& Context, const float DeltaSeconds)
{
	const float Before = Elapsed;
	Elapsed += DeltaSeconds;
	if (InstallStyle != EGinkgoAttachmentInstallStyle::Default)
	{
		TickModularStyle(Context, Before, Elapsed);
		return;
	}
	const float Delay = PreviousMesh ? RemovalEnd - ReplacementStart : 0.f;
	auto Contact = [&](const float Time, const FGameplayTag& Preferred, const FGameplayTag& Fallback, const float Volume)
	{
		if (Before < Time && Elapsed >= Time)
		{
			if (const auto* Character = Cast<AGinkgoCharacter>(Context.GetOwner()))
			{
				Character->GetWeapons()->PlayAttachmentContact(Preferred, Fallback, Volume);
			}
		}
	};
	if (PreviousMesh) { Contact(UnseatTime, FWeaponTags::Weapon_Cue_MagOut, FWeaponTags::Weapon_Cue_BoltPull, .60f); }
	if (!bEmptyModule)
	{
		Contact(Delay + PlaceEnd, FWeaponTags::Weapon_Cue_MagIn, FWeaponTags::Weapon_Cue_BoltPush, .60f);
		if (WristTurn != 0.f) { Contact(Delay + TightenTime, FWeaponTags::Weapon_Cue_SlideHome, FWeaponTags::Weapon_Cue_BoltPush, .42f); }
	}
	if (PreviousMesh && Elapsed < RemovalEnd)
	{
		const float Reach = FMath::SmoothStep(0.f, .43f, Elapsed);
		const float Remove = FMath::SmoothStep(UnseatTime, 1.15f, Elapsed);
		FTransform Pose = MountWorld;
		const FVector Up = Stand.GetUnitAxis(EAxis::Z);
		const FVector Away = -Stand.GetUnitAxis(EAxis::X);
		Pose.AddToTranslation(Up * (12.f * FMath::Sin(Remove * UE_PI)) + Away * (32.f * Remove));
		RemovedPreview->SetWorldTransform(Pose);
		RemovedPreview->SetVisibility(Elapsed >= UnseatTime && Elapsed < 1.12f);
		if (Gun.IsValid() && Elapsed >= UnseatTime) { Gun->HideBoneByName(ModuleBone, EPhysBodyOp::PBO_None); }
		Hands.BodyLean = Reach;
		Hands.RightGrip = Reach;
		Hands.LeftGrip = Reach;
		Hands.WristLocation = Pose.TransformPosition(PreviousMesh->GetBounds().Origin) + Up * 3.5;
		Hands.WristRotationDelta = FQuat::Identity;
		RemovalHands = Hands;
		return;
	}
	if (RemovedPreview) { RemovedPreview->SetVisibility(false); }
	if (bEmptyModule)
	{
		const float Release = FMath::SmoothStep(0.f, WithdrawTime, Elapsed - RemovalEnd);
		Hands = RemovalHands;
		Hands.BodyLean *= 1.f - Release;
		Hands.LeftGrip *= 1.f - Release;
		Hands.RightGrip *= 1.f - Release;
		return;
	}
	SamplePose(Elapsed - Delay);
	if (PreviousMesh)
	{
		const float Join = FMath::SmoothStep(0.f, .25f, Elapsed - RemovalEnd);
		Hands.WristLocation = FMath::Lerp(RemovalHands.WristLocation, Hands.WristLocation, Join);
		Hands.WristRotationDelta = FQuat::Slerp(RemovalHands.WristRotationDelta, Hands.WristRotationDelta, Join).GetNormalized();
		Hands.LeftGrip = FMath::Lerp(RemovalHands.LeftGrip, Hands.LeftGrip, Join);
		Hands.RightGrip = FMath::Lerp(RemovalHands.RightGrip, Hands.RightGrip, Join);
		Hands.BodyLean = FMath::Lerp(RemovalHands.BodyLean, Hands.BodyLean, Join);
	}
}

void UGinkgoInstallAttachmentAction::SamplePose(const float Time)
{
	const FVector Up = Stand.GetUnitAxis(EAxis::Z);
	const FVector Forward = Stand.GetUnitAxis(EAxis::X);
	const FVector Right = Stand.GetUnitAxis(EAxis::Y);
	const float Withdraw = Time - (WristTurn != 0.f ? TurnEnd : PlaceEnd);

	// 先由肩背盖住腰侧，手探到以后才亮出网格；此时配件停在遮挡内，随后整件随手越过桌沿。
	const float Carry = FMath::SmoothStep(.68f, 1.54f, Time);
	const float Lift = FMath::SmoothStep(.64f, 1.03f, Time) * (1.f - FMath::SmoothStep(1.13f, PlaceEnd, Time));
	const FQuat Yaw(Up, FMath::DegreesToRadians(RestYaw * (1.0 - Carry)));
	const FQuat Tilt(Forward, FMath::DegreesToRadians(-19.0 * Lift));
	const FQuat CarryRotation = Tilt * Yaw;
	// 搬运的是实际握点，不能插值整枪原点再把远端模块的中心旋转过去。
	// M4 枪口模块的握点距原点约半米，旧路径会额外抬高十几厘米，手经过肩部时肘部求解翻转。
	const FVector ScaledGrip = MountWorld.GetScale3D() * GripPoint;
	const FQuat StartRotation = FQuat(Up, FMath::DegreesToRadians(RestYaw)) * MountWorld.GetRotation();
	const FVector StartGrip = StartLocation + StartRotation.RotateVector(ScaledGrip);
	const FVector EndGrip = MountWorld.TransformPosition(GripPoint);
	const FVector GripLocation = FMath::Lerp(StartGrip, EndGrip, Carry) + Up * (18.0 * Lift);

	// 落位后模型只在两次发力时沿局部轴转动；换握期间模型停住，手腕撤开一点再回到起势。
	// 两段长度和幅度不同。道具从负的表演角开始，到第二拍刚好归零，临时网格和正式挂点交接时朝向一致。
	const float First = FMath::SmoothStep(1.62f, 2.00f, Time);
	const float Regrip = FMath::SmoothStep(2.04f, 2.34f, Time);
	const float Second = FMath::SmoothStep(2.39f, TightenTime, Time);
	const float Release = FMath::SmoothStep(.0f, .29f, Withdraw);
	const double PartAngle = WristTurn * ((First - 1.0) + .65 * (Second - 1.0));
	const double WristAngle = WristTurn * (First - 1.12 * Regrip + .65 * Second) * (1.0 - Release);
	const FQuat PartRoll(FVector::ForwardVector, FMath::DegreesToRadians(PartAngle));
	FTransform Pose = MountWorld;
	Pose.SetRotation(CarryRotation * MountWorld.GetRotation() * PartRoll);
	// 转向与拧动都绕握点，反求模型原点；静态配件和骨骼模块使用相同的空间约定。
	Pose.SetLocation(GripLocation - Pose.GetRotation().RotateVector(ScaledGrip));
	Preview->SetWorldTransform(Pose);
	if (!bEmptyModule && Time >= RevealTime && !Preview->IsVisible())
	{
		Preview->SetVisibility(true);
	}

	// 腰和右手先开始，左手到腰侧时身体已探出。前倾放在独立曲线上，换握不会带着胸口反复点头。
	const float Cover = FMath::SmoothStep(.0f, .43f, Time);
	Hands.BodyLean = Cover * (1.0f + .26f * (1.f - FMath::SmoothStep(.75f, 1.28f, Time)))
		* (1.f - FMath::SmoothStep(.19f, WithdrawTime, Withdraw));
	Hands.RightGrip = FMath::SmoothStep(.04f, .40f, Time) * (1.f - FMath::SmoothStep(.30f, WithdrawTime, Withdraw));
	Hands.LeftGrip = FMath::SmoothStep(.22f, .56f, Time) * (1.f - FMath::SmoothStep(.14f, .53f, Withdraw));
	const FQuat WristRoll(MountWorld.GetUnitAxis(EAxis::X), FMath::DegreesToRadians(WristAngle));
	Hands.WristRotationDelta = CarryRotation * WristRoll;
	const float LiftPalm = WristTurn != 0.f ? 4.f * Regrip * (1.f - Regrip) : 0.f;
	const FVector WristOffset = Up * 3.5 - Forward * 4.0;
	Hands.WristLocation = Pose.TransformPosition(GripPoint) + Hands.WristRotationDelta.RotateVector(WristOffset)
		+ (Up * 2.5 - Forward * 3.0) * LiftPalm + (Up * 5.0 - Forward * 4.5) * Release;
}

float UGinkgoInstallAttachmentAction::ComputeModularDuration() const
{
	return BuildModularSchedule(InstallStyle, PreviousMesh != nullptr, !bEmptyModule).TTotal;
}

void UGinkgoInstallAttachmentAction::TickModularStyle(FGinkgoAnimActionContext& Context, const float Before, const float Current)
{
	const FModularSchedule S = BuildModularSchedule(InstallStyle, PreviousMesh != nullptr, !bEmptyModule);
	auto Contact = [&](const float Time, const FGameplayTag& Preferred, const FGameplayTag& Fallback, const float Volume)
	{
		if (Before < Time && Current >= Time)
		{
			if (const auto* Character = Cast<AGinkgoCharacter>(Context.GetOwner()))
			{
				Character->GetWeapons()->PlayAttachmentContact(Preferred, Fallback, Volume);
			}
		}
	};
	if (S.bHasRemove)
	{
		Contact(S.TUnseatAudio, FWeaponTags::Weapon_Cue_MagOut, FWeaponTags::Weapon_Cue_BoltPull, 0.58f);
	}
	if (S.bHasInstall)
	{
		Contact(S.TSeat, FWeaponTags::Weapon_Cue_MagIn, FWeaponTags::Weapon_Cue_BoltPush, 0.60f);
		Contact(S.TLockAudio, FWeaponTags::Weapon_Cue_SlideHome, FWeaponTags::Weapon_Cue_BoltPush, 0.42f);
	}
	SampleModularStyle(Current);
}

void UGinkgoInstallAttachmentAction::SampleModularStyle(const float Time)
{
	const USceneComponent* Rest = Table.Get();
	if (!Rest)
	{
		return;
	}
	const FModularSchedule S = BuildModularSchedule(InstallStyle, PreviousMesh != nullptr, !bEmptyModule);
	const FVector Up = Stand.GetUnitAxis(EAxis::Z);
	const FVector Forward = Stand.GetUnitAxis(EAxis::X);
	const FVector Right = Stand.GetUnitAxis(EAxis::Y);
	const FVector GunX = MountWorld.GetUnitAxis(EAxis::X);
	const FVector GunZ = MountWorld.GetUnitAxis(EAxis::Z);
	const double RestZ = Rest->GetComponentLocation().Z;

	// 右侧部件（枪托/主握把/扳机）收入右腰与固定相机底边下方盲区；左侧部件（前握把/前端/枪口）收入躯干左侧遮挡盲区。
	FVector Pocket = S.bRightHand
		? Stand.GetLocation() + Right * 14.0
		: Stand.GetLocation() + Forward * 10.0 - Right * 16.0;
	Pocket.Z = S.bRightHand ? (RestZ - 14.0) : (RestZ - 6.0);

	// 每种 M4 模块按真实结构轴向拆装，并在盲区内按竖向收拢，保证长前端与长枪托显隐瞬间也完全不穿帮。
	FVector RemoveOffset = FVector::ZeroVector;
	FVector ApproachOffset = FVector::ZeroVector;
	FVector TwistAxis = GunX;
	FQuat PocketRot = FQuat::Identity;
	double RemoveTwistDeg = 0.0;
	double LockTwistDeg = 0.0;
	float BaseReachLean = 0.56f;
	float CoverLean = 0.90f;
	FVector ActiveWristOffset = S.bRightHand ? (Up * 3.5 - Forward * 3.5 + Right * 2.2) : (Up * 3.5 - Forward * 3.8);
	FVector PocketWristOffset = S.bRightHand ? (Up * 4.5 + Forward * 2.0 + Right * 1.5) : (Up * 3.5 - Forward * 2.0);
	FVector RemoveArc = Up * 2.0;
	FVector DeliverArc = Up * 4.5;

	switch (InstallStyle)
	{
	case EGinkgoAttachmentInstallStyle::StockSlide:
		RemoveOffset = -GunX * 14.0 + Up * 1.0;
		ApproachOffset = -GunX * 14.0 + Up * 1.0;
		TwistAxis = GunX;
		PocketRot = (FQuat(Forward, FMath::DegreesToRadians(-66.0)) * FQuat(Up, FMath::DegreesToRadians(-18.0))).GetNormalized();
		RemoveTwistDeg = -6.0;
		BaseReachLean = 0.48f;
		CoverLean = 0.54f;
		RemoveArc = Up * 1.5;
		DeliverArc = Up * 3.0;
		break;
	case EGinkgoAttachmentInstallStyle::PistolGrip:
	{
		const FVector PullAxis = (-GunZ * 0.90 - GunX * 0.44).GetSafeNormal();
		RemoveOffset = PullAxis * 11.5 + Up * 1.2;
		ApproachOffset = PullAxis * 11.0 + Up * 1.0;
		TwistAxis = PullAxis;
		PocketRot = (FQuat(Forward, FMath::DegreesToRadians(-38.0)) * FQuat(Up, FMath::DegreesToRadians(-22.0))).GetNormalized();
		RemoveTwistDeg = -12.0;
		LockTwistDeg = 16.0;
		BaseReachLean = 0.52f;
		CoverLean = 0.56f;
		RemoveArc = Up * 2.0;
		DeliverArc = Up * 3.0;
		break;
	}
	case EGinkgoAttachmentInstallStyle::TriggerDropIn:
		RemoveOffset = Up * 7.5 - GunZ * 3.0;
		ApproachOffset = Up * 7.0 - GunZ * 1.5;
		TwistAxis = Right;
		PocketRot = FQuat(Up, FMath::DegreesToRadians(-20.0));
		RemoveTwistDeg = 8.0;
		BaseReachLean = 0.56f;
		CoverLean = 0.58f;
		ActiveWristOffset = Up * 4.0 - Forward * 3.0 + Right * 1.8;
		RemoveArc = Up * 2.5;
		DeliverArc = Up * 3.5;
		break;
	case EGinkgoAttachmentInstallStyle::ForegripRail:
		RemoveOffset = -GunZ * 9.0 + GunX * 4.0 + Up * 1.0;
		ApproachOffset = -GunZ * 8.5 + GunX * 4.5 + Up * 1.0;
		TwistAxis = -GunZ;
		PocketRot = (FQuat(Forward, FMath::DegreesToRadians(34.0)) * FQuat(Up, FMath::DegreesToRadians(22.0))).GetNormalized();
		RemoveTwistDeg = -10.0;
		LockTwistDeg = 14.0;
		BaseReachLean = 0.58f;
		CoverLean = 0.92f;
		RemoveArc = Up * 2.5;
		DeliverArc = Up * 4.0;
		break;
	case EGinkgoAttachmentInstallStyle::HandguardSlide:
		RemoveOffset = GunX * 14.0 + Up * 1.8;
		ApproachOffset = GunX * 14.0 + Up * 1.8;
		TwistAxis = GunX;
		PocketRot = (FQuat(Forward, FMath::DegreesToRadians(72.0)) * FQuat(Up, FMath::DegreesToRadians(18.0))).GetNormalized();
		RemoveTwistDeg = -6.0;
		LockTwistDeg = 10.0;
		BaseReachLean = 0.64f;
		CoverLean = 0.95f;
		RemoveArc = Up * 3.0;
		DeliverArc = Up * 4.5;
		break;
	case EGinkgoAttachmentInstallStyle::MuzzleQuickThread:
		RemoveOffset = GunX * 8.5 + Up * 0.8;
		ApproachOffset = GunX * 8.0 + Up * 0.8;
		TwistAxis = GunX;
		PocketRot = (FQuat(Forward, FMath::DegreesToRadians(40.0)) * FQuat(Up, FMath::DegreesToRadians(24.0))).GetNormalized();
		RemoveTwistDeg = -42.0;
		LockTwistDeg = WristTurn != 0.f ? WristTurn : 46.0;
		BaseReachLean = 0.72f;
		CoverLean = 0.94f;
		RemoveArc = Up * 3.0;
		DeliverArc = Up * 4.5;
		break;
	default:
		break;
	}

	const FVector EndGrip = MountWorld.TransformPosition(GripPoint);
	const FVector RemovedEndGrip = MountWorld.TransformPosition(RemovedGripPoint);
	const FVector ReferenceGrip = S.bHasInstall ? EndGrip : RemovedEndGrip;
	const double LateralSpan = FVector::Dist2D(Stand.GetLocation(), ReferenceGrip);
	const float ReachLean = FMath::Clamp(BaseReachLean + static_cast<float>(FMath::Max(0.0, LateralSpan - 70.0) * 0.008), BaseReachLean, 1.05f);

	const FVector ScaledGrip = MountWorld.GetScale3D() * GripPoint;
	const FVector ScaledRemovedGrip = MountWorld.GetScale3D() * RemovedGripPoint;
	auto SetPose = [&](UMeshComponent* MeshComp, const FVector& GripPos, const FQuat& WorldDeltaRot, const FVector& ScaledLocal)
	{
		if (!MeshComp) { return; }
		FTransform Pose = MountWorld;
		Pose.SetRotation((WorldDeltaRot * MountWorld.GetRotation()).GetNormalized());
		Pose.SetLocation(GripPos - Pose.GetRotation().RotateVector(ScaledLocal));
		MeshComp->SetWorldTransform(Pose);
	};

	const FQuat PocketWristRot = FQuat::Slerp(FQuat::Identity, PocketRot, 0.42f).GetNormalized();
	FVector ActiveWrist = Pocket + PocketWristRot.RotateVector(PocketWristOffset);
	FQuat ActiveWristRot = PocketWristRot;
	float ActiveGripAlpha = 0.f;
	float SteadyGripAlpha = 0.f;
	float LeanAlpha = 0.f;
	const FVector PocketLeanDir = Stand.GetLocation() + Forward * 58.0 + Right * (S.bRightHand ? 14.0 : -20.0);
	FVector LeanPoint = ReferenceGrip;

	if (S.bHasRemove && Time < S.TPocketArrive)
	{
		const float Reach = FMath::SmoothStep(0.f, S.TReach, Time);
		const float Extract = FMath::SmoothStep(S.TReach, S.TExtract, Time);
		const float Stow = FMath::SmoothStep(S.TExtract - 0.04f, S.TPocketArrive, Time);
		const FVector ExtractedPos = RemovedEndGrip + RemoveOffset;
		const FVector AfterExtract = FMath::Lerp(RemovedEndGrip, ExtractedPos, Extract);
		const FVector PartPos = FMath::Lerp(AfterExtract, Pocket, Stow) + RemoveArc * FMath::Sin(Stow * UE_PI);
		const FQuat ExtractRot(TwistAxis, FMath::DegreesToRadians(RemoveTwistDeg * Extract));
		const FQuat PartRot = FQuat::Slerp(ExtractRot, PocketRot, Stow).GetNormalized();

		SetPose(RemovedPreview, PartPos, PartRot, ScaledRemovedGrip);
		const bool bShowOld = Time >= S.TReach && Time < S.TVanish;
		if (RemovedPreview) { RemovedPreview->SetVisibility(bShowOld); }
		if (Gun.IsValid() && Time >= S.TReach && !ModuleBone.IsNone())
		{
			Gun->HideBoneByName(ModuleBone, EPhysBodyOp::PBO_None);
		}
		if (Preview) { Preview->SetVisibility(false); }

		const FQuat WristCarryRot = FQuat::Slerp(ExtractRot, PocketWristRot, Stow).GetNormalized();
		const FVector WristOff = FMath::Lerp(ActiveWristOffset, PocketWristOffset, Stow);
		ActiveWrist = PartPos + WristCarryRot.RotateVector(WristOff);
		ActiveWristRot = WristCarryRot;
		ActiveGripAlpha = Reach;
		SteadyGripAlpha = Reach;
		LeanAlpha = FMath::Lerp(ReachLean * Reach, CoverLean, Stow);
		LeanPoint = FMath::Lerp(RemovedEndGrip, PocketLeanDir, Stow);
	}
	else if (!S.bHasInstall)
	{
		// 仅拆旧件（如卸下前握把）：旧件已在盲区隐藏，手从盲区干脆收回待机。
		if (RemovedPreview) { RemovedPreview->SetVisibility(false); }
		if (Preview) { Preview->SetVisibility(false); }
		const float Withdraw = FMath::Clamp((Time - S.TPocketArrive) / FMath::Max(S.TTotal - S.TPocketArrive, 0.01f), 0.f, 1.f);
		const float SmoothWithdraw = FMath::SmoothStep(0.f, 1.f, Withdraw);
		ActiveWristRot = FQuat::Slerp(PocketWristRot, FQuat::Identity, SmoothWithdraw).GetNormalized();
		ActiveWrist = Pocket + ActiveWristRot.RotateVector(PocketWristOffset) + Up * (3.0 * SmoothWithdraw);
		ActiveGripAlpha = 1.f - FMath::SmoothStep(0.f, 0.86f, Withdraw);
		SteadyGripAlpha = 1.f - FMath::SmoothStep(0.10f, 1.f, Withdraw);
		LeanAlpha = CoverLean * (1.f - SmoothWithdraw);
		LeanPoint = PocketLeanDir;
	}
	else if (Time < S.TRevealDone)
	{
		// 盲区取件：有拆旧时手停在盲区完成替换；无拆旧时先探身遮挡再把手伸入盲区。
		if (RemovedPreview) { RemovedPreview->SetVisibility(false); }
		const FQuat InitLockRot = LockTwistDeg != 0.0
			? FQuat(TwistAxis, FMath::DegreesToRadians(-LockTwistDeg * (InstallStyle == EGinkgoAttachmentInstallStyle::MuzzleQuickThread ? 1.35 : 1.0)))
			: FQuat::Identity;
		const FQuat StartPartRot = (PocketRot * InitLockRot).GetNormalized();
		SetPose(Preview, Pocket, StartPartRot, ScaledGrip);
		if (Preview) { Preview->SetVisibility(Time >= S.TReveal); }

		const float EnterAlpha = S.bHasRemove ? 1.f : FMath::SmoothStep(0.f, S.TRevealDone, Time);
		ActiveWristRot = FQuat::Slerp(FQuat::Identity, PocketWristRot, EnterAlpha).GetNormalized();
		ActiveWrist = Pocket + ActiveWristRot.RotateVector(PocketWristOffset);
		ActiveGripAlpha = S.bHasRemove ? 1.f : EnterAlpha;
		SteadyGripAlpha = S.bHasRemove ? 1.f : EnterAlpha;
		LeanAlpha = CoverLean * EnterAlpha;
		LeanPoint = PocketLeanDir;
	}
	else
	{
		// 盲区抽出 -> 沿结构轴对准 -> 推入/落位 -> 专属锁止节拍 -> 干脆收手。
		if (RemovedPreview) { RemovedPreview->SetVisibility(false); }
		const float Approach = FMath::SmoothStep(S.TRevealDone, S.TApproach, Time);
		const float Seat = FMath::SmoothStep(S.TApproach - 0.03f, S.TSeat, Time);
		const float LockProgress = FMath::Clamp((Time - S.TSeat) / FMath::Max(S.TLockEnd - S.TSeat, 0.01f), 0.f, 1.f);
		const float Withdraw = FMath::Clamp((Time - S.TLockEnd) / FMath::Max(S.TTotal - S.TLockEnd, 0.01f), 0.f, 1.f);
		const float HandRelease = FMath::SmoothStep(0.f, 0.80f, Withdraw);

		const FVector ApproachPos = EndGrip + ApproachOffset;
		const FVector BringPos = FMath::Lerp(Pocket, ApproachPos, Approach) + DeliverArc * FMath::Sin(Approach * UE_PI);
		const FVector PartPos = FMath::Lerp(BringPos, EndGrip, Seat);

		double PartLockAngle = 0.0;
		double WristLockAngle = 0.0;
		FVector LockHandNudge = FVector::ZeroVector;
		FQuat ExtraWristFlick = FQuat::Identity;

		if (InstallStyle == EGinkgoAttachmentInstallStyle::MuzzleQuickThread)
		{
			// 枪口件：快节奏两连拧紧（先第一拍、微抬换握、第二拍旋死归零）。
			const float Beat1 = FMath::SmoothStep(0.00f, 0.34f, LockProgress);
			const float Regrip = FMath::SmoothStep(0.36f, 0.64f, LockProgress);
			const float Beat2 = FMath::SmoothStep(0.66f, 0.96f, LockProgress);
			PartLockAngle = LockTwistDeg * (0.75 * (Beat1 - 1.0) + 0.60 * (Beat2 - 1.0));
			WristLockAngle = LockTwistDeg * (0.75 * Beat1 - 0.85 * Regrip + 0.60 * Beat2) * (1.0 - HandRelease);
			const float LiftPalm = 4.f * Regrip * (1.f - Regrip);
			LockHandNudge = (Up * 2.2 - Forward * 2.4) * LiftPalm;
		}
		else if (InstallStyle == EGinkgoAttachmentInstallStyle::StockSlide)
		{
			// 枪托：推到位后一记干脆的轴向轻拍确认卡榫锁止。
			const float TapPulse = FMath::Sin(LockProgress * UE_PI);
			const float TapShift = FMath::Sin(FMath::Clamp((LockProgress - 0.15f) / 0.70f, 0.f, 1.f) * UE_PI);
			LockHandNudge = -GunX * (2.6 * TapShift) + Up * (1.4 * TapPulse);
			ExtraWristFlick = FQuat(Forward, FMath::DegreesToRadians(-8.0 * TapPulse * (1.0 - HandRelease)));
		}
		else if (InstallStyle == EGinkgoAttachmentInstallStyle::TriggerDropIn)
		{
			// 扳机：垂直落槽后一记利索向下按销轻点锁定。
			const float PressPulse = FMath::Sin(LockProgress * UE_PI);
			LockHandNudge = Up * (2.2 * FMath::Sin(FMath::Clamp(LockProgress * 2.f, 0.f, 1.f) * UE_PI) - 1.2 * PressPulse);
			ExtraWristFlick = FQuat(Right, FMath::DegreesToRadians(-11.0 * PressPulse * (1.0 - HandRelease)));
		}
		else if (LockTwistDeg != 0.0)
		{
			// 主握把 / 前握把 / 前端：落位后单拍干脆沿自身锁轴拧紧归零。
			const float Turn = FMath::SmoothStep(0.05f, 0.92f, LockProgress);
			PartLockAngle = LockTwistDeg * (Turn - 1.0);
			WristLockAngle = LockTwistDeg * Turn * (1.0 - HandRelease);
		}

		const FQuat LockPartRot(TwistAxis, FMath::DegreesToRadians(PartLockAngle));
		const FQuat CarryRot = FQuat::Slerp(PocketRot, FQuat::Identity, Approach).GetNormalized();
		const FQuat PartRot = (CarryRot * LockPartRot).GetNormalized();

		if (Time >= S.TLockEnd)
		{
			if (Preview) { Preview->SetWorldTransform(MountWorld); }
		}
		else
		{
			SetPose(Preview, PartPos, PartRot, ScaledGrip);
		}
		if (Preview && !Preview->IsVisible()) { Preview->SetVisibility(true); }

		const FQuat WristCarry = FQuat::Slerp(FQuat::Slerp(FQuat::Identity, PocketRot, 0.42f), FQuat::Identity, Approach).GetNormalized();
		const FQuat WristLockRot(TwistAxis, FMath::DegreesToRadians(WristLockAngle));
		ActiveWristRot = (WristCarry * WristLockRot * ExtraWristFlick).GetNormalized();
		const FVector WristOff = FMath::Lerp(PocketWristOffset, ActiveWristOffset, Approach);
		const FVector ReleaseLift = (Up * 4.5 - Forward * 3.8) * HandRelease;
		ActiveWrist = PartPos + ActiveWristRot.RotateVector(WristOff) + LockHandNudge + ReleaseLift;

		ActiveGripAlpha = 1.f - FMath::SmoothStep(0.00f, 0.86f, Withdraw);
		SteadyGripAlpha = 1.f - FMath::SmoothStep(0.10f, 1.00f, Withdraw);
		LeanAlpha = FMath::Lerp(CoverLean, ReachLean, Approach) * (1.f - FMath::SmoothStep(0.06f, 1.00f, Withdraw));
		LeanPoint = FMath::Lerp(PocketLeanDir, EndGrip, Approach);
	}

	Hands = {};
	Hands.BodyLean = LeanAlpha;
	Hands.bHasLeanTarget = true;
	Hands.LeanTarget = LeanPoint;
	if (S.bRightHand)
	{
		Hands.bHasLeftTarget = false;
		Hands.LeftGrip = SteadyGripAlpha;
		Hands.bHasRightTarget = true;
		Hands.RightGrip = ActiveGripAlpha;
		Hands.RightWristLocation = ActiveWrist;
		Hands.RightWristRotationDelta = ActiveWristRot;
	}
	else
	{
		Hands.bHasLeftTarget = true;
		Hands.LeftGrip = ActiveGripAlpha;
		Hands.WristLocation = ActiveWrist;
		Hands.WristRotationDelta = ActiveWristRot;
		Hands.bHasRightTarget = false;
		Hands.RightGrip = SteadyGripAlpha;
	}
}

void UGinkgoInstallAttachmentAction::OnFinished(const FGinkgoAnimActionResult& InResult)
{
	Super::OnFinished(InResult);
	Hands = {};
	bool bTransferred = false;
	if (InResult.Outcome == EGinkgoAnimActionOutcome::Completed && Visual.IsValid() && Gun.IsValid() && Part)
	{
		if (auto* Module = Cast<UPoseableMeshComponent>(Preview))
		{
			bTransferred = Visual->HoldPreview(*Part, *Gun, *Module, ModuleBone, bModuleWasHidden);
		}
	}
	if (!bTransferred && Gun.IsValid() && !ModuleBone.IsNone() && !bModuleWasHidden) { Gun->UnHideBoneByName(ModuleBone); }
	if (RemovedPreview) { RemovedPreview->DestroyComponent(); RemovedPreview = nullptr; }
	if (Preview)
	{
		if (!bTransferred) { Preview->DestroyComponent(); }
		Preview = nullptr;
	}
}

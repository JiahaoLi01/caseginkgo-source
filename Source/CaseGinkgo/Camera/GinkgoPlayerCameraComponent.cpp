#include "GinkgoPlayerCameraComponent.h"
#include "Camera/PlayerCameraManager.h"

#include "Engine/World.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoInputSettingsSubsystem.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GinkgoCameraConfig.h"
#include "GinkgoCameraContextComponent.h"
#include "GinkgoCameraMath.h"
#include "GinkgoScopeView.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"

DECLARE_CYCLE_STAT(TEXT("Camera pipeline"), STAT_GinkgoCameraPipeline, STATGROUP_Ginkgo3C);

void Ginkgo::Camera::ComputeHealthVignette(const float MissingHealthRatio, const FGinkgoHealthVignetteSettings& Settings,
	float& OutIntensity, FLinearColor& OutColor)
{
	const float Missing = FMath::Clamp(MissingHealthRatio, 0.f, 1.f);
	const float Exponent = FMath::Max(0.1f, Settings.ResponseExponent);
	const float Curve = Missing > 0.f ? FMath::Pow(Missing, Exponent) : 0.f;
	OutIntensity = FMath::Lerp(Settings.BaseIntensity, Settings.MaxIntensity, Curve);

	const float HealthRatio = 1.f - Missing;
	const float ColorStart = FMath::Clamp(Settings.ColorStartHealthRatio, 0.01f, 1.f);
	const float ColorAlpha = HealthRatio < ColorStart ? FMath::Clamp((ColorStart - HealthRatio) / ColorStart, 0.f, 1.f) : 0.f;
	const float SmoothColorAlpha = ColorAlpha * ColorAlpha * (3.f - 2.f * ColorAlpha);
	OutColor = FMath::Lerp(FLinearColor::Black, Settings.LowHealthColor, SmoothColorAlpha);
}

const UGinkgoCameraConfig& UGinkgoPlayerCameraComponent::GetConfig() const
{
	if (ViewerConfig.IsValid())
	{
		return *ViewerConfig.Get();
	}
	return CameraConfig ? *CameraConfig : *GetDefault<UGinkgoCameraConfig>();
}

void UGinkgoPlayerCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!CameraConfig)
	{
		CameraConfig = UGinkgoDeveloperSettings::Get()->CameraConfig.LoadSynchronous();
	}
	UE_LOG(LogGinkgoCamera, Log, TEXT("[Camera] %s 使用相机配置：%s"), *GetNameSafe(this),
		CameraConfig ? *CameraConfig->GetPathName() : TEXT("类默认值（未指定资产）"));
}

void UGinkgoPlayerCameraComponent::GetCameraView(const float DeltaTime, FMinimalViewInfo& DesiredView)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (APlayerController* Viewer = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
	{
		EvaluateView(*Viewer, DeltaTime, DesiredView);
		return;
	}
	Super::GetCameraView(DeltaTime, DesiredView);
}

void UGinkgoPlayerCameraComponent::ToggleShoulder()
{
	Shoulder.Toggle(GetWorld()->GetTimeSeconds(), GetConfig().Shoulder);
}

void UGinkgoPlayerCameraComponent::SuspendView()
{
	Shoulder.EndAds();
	if (UGinkgoCameraContextComponent* Context = FollowedContext.Get())
	{
		Context->SetHeadHidden(false);
	}
	if (ScopeView)
	{
		ScopeView->Deactivate();
	}
	FirstPersonAlpha = 0.f;
	ScopeShotTime.Reset();
	SmoothedHealthMissing = 0.f;
	bDiscardPendingEvents = true;
}

void UGinkgoPlayerCameraComponent::EvaluateView(APlayerController& Viewer, const float DeltaTime, FMinimalViewInfo& OutView, UGinkgoCameraConfig* Override)
{
	ViewerConfig = Override;
	SCOPE_CYCLE_COUNTER(STAT_GinkgoCameraPipeline);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		Super::GetCameraView(DeltaTime, OutView);
		return;
	}
	const UGinkgoCameraConfig& Config = GetConfig();
	const bool bCut = bCutRequested;
	if (bCut)
	{
		Follow.Reset();
		Collision.Reset();
		ParamLayer.Reset();
		Shoulder.Snap();
		OverlayLayer.Reset();
		FollowedTarget = Pawn;
		FollowedContext = UGinkgoCameraContextComponent::Find(Pawn);
		bCutRequested = false;
		if (Viewer.PlayerCameraManager)
		{
			Viewer.PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}
	UGinkgoCameraContextComponent* Context = FollowedContext.Get();

	// ① 基础参数：配置里的 P₀
	// ② 参数层：按上下文算出各修改器的权重，按优先级应用覆盖、累加增量，再交给参数弹簧
	const FGinkgoCameraParams RightShoulderParams = ParamLayer.Evaluate(Config, Context, DeltaTime, AreModifiersEnabled());
	FGinkgoCameraParams Params = RightShoulderParams;
	Shoulder.Apply(Params, DeltaTime, Config.Shoulder);

	// ③ 位姿解算：平滑的是胶囊中心（跟胶囊不跟骨骼），跟随点偏移随视角的 Yaw 转、在平滑之后加；旋转直接用 ControlRotation，不插值
	const FRotator ViewRotation = Viewer.GetControlRotation();
	// 死亡后胶囊停在原处，使用骨盆作为跟随中心，身体从台阶跌落也能留在画面里。
	const auto* Character = Cast<AGinkgoCharacter>(Pawn);
	const FVector CapsuleCenter = Character && Character->GetDamageable()->IsDead() && Character->GetMesh()->IsSimulatingPhysics(TEXT("pelvis"))
		? Character->GetMesh()->GetSocketLocation(TEXT("pelvis")) : Pawn->GetActorLocation();
	const FVector FollowCenter = Follow.Step(CapsuleCenter, Pawn->GetVelocity(), DeltaTime, Config.Follow);
	const FVector Pivot = Ginkgo::Camera::ComputePivot(FollowCenter, ViewRotation, Params);
	const FVector Boom = Ginkgo::Camera::ComputeCameraLocation(Pivot, ViewRotation, Params, &Config.Orbit) - Pivot;

	// ④ 碰撞：扫掠找出挡住的地方，拉近瞬时、推远走弹簧。之后不再大幅移动相机
	const FCollisionSweep Sweep = SweepCollision(*Pawn, CapsuleCenter, Pivot, Boom, Config.ProbeRadius);
	const double Compression = Collision.Step(Sweep.RequiredCompression, DeltaTime, Config.PushOutOmega);
	const FVector Location = Ginkgo::Camera::ApplyCompression(Sweep.SafePivot, Boom, Compression);
	// 侧倾绕相机前向，只转画面，不改上面算出的位置
	const FRotator BaseRotation(ViewRotation.Pitch, ViewRotation.Yaw, ViewRotation.Roll + Params.Roll);

	// ④½ 第一人称开镜（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：手上的枪带瞄具时，开镜让相机从越肩滑到眼睛（你选的：滑进去）。
	// 走多少就是开镜进度（两头缓过的）：到位时枪也抬到了开镜的位置。朝向照旧是视角的（2026-11-12 你手测以后说的：开火时枪要能离开准星、离开中心）：
	// 枪口对准以后枪上还有程序化的偏移（踢动、转镜头的落后、走路的偏与晃，FGinkgoScopeHandling），分划跟着枪离开屏幕中心。
	// 眼睛按枪这一帧最终的变换算（相机在所有 Actor 更新之后算，动画已经求完），不按头的骨骼（出瞳只有 8 mm）：去掉偏移的出瞳是眼睛的家，
	// 偏移只跟一部分（脸贴着枪托），出瞳离开眼睛时镜内照出瞳算的那一部分是黑的
	const USceneComponent* SightComponent = Context ? Context->GetSightComponent() : nullptr;
	const float AdsAlpha = Context ? Context->FindScalar(FWeaponTags::Weapon_Value_AdsAlpha).Get(0.f) : 0.f;
	const float FirstPerson = Ginkgo::Scope::FirstPersonAlpha(AdsAlpha, SightComponent != nullptr);
	Ginkgo::Scope::FSightFrame Sight;
	FVector ViewLocation = Location;
	const FRotator ViewBaseRotation = BaseRotation;
	// 拉栓时拉机柄扳起来、拉到底离出瞳只有 4 cm，抓着它的手正好在眼前：眼睛按枪栓这一帧的样子往左上让开（镜内照出瞳算，这时是黑的）
	FVector Eye = FVector::ZeroVector;
	Ginkgo::Scope::FSightFrame Rest;
	double Clearance = 0.0;
	if (FirstPerson > 0.f)
	{
		const FGinkgoScope& Scope = Context->GetSightScope();
		const FTransform Gun = SightComponent->GetComponentTransform();
		Sight = Ginkgo::Scope::MakeSightFrame(Gun, Scope);
		Clearance = FMath::Clamp(Context->FindScalar(FWeaponTags::Weapon_Value_SightClearance).Get(0.f), 0.f, 1.f);
		Eye = Ginkgo::Scope::ComputeFirstPersonEye(Gun, Context->GetSightOffset(), Scope, &Rest) + Rest.Rotation.RotateVector(Config.FirstPerson.BoltClearance) * Clearance;
		ViewLocation = FMath::Lerp(Location, Eye, static_cast<double>(FirstPerson));
	}

	// ⑤ 叠加层：冲量弹簧与噪声，只改旋转与 FOV。事件按时刻拆开推进：这一帧覆盖世界时间 [现在 − Δt, 现在]，
	// 移动组件给的事件时刻落在这一段里；切换之前入队的事件不算
	TArray<FGinkgoCameraEvent> Events;
	if (Context)
	{
		Events = Context->TakeEvents();
	}
	if (bCut || bDiscardPendingEvents)
	{
		bDiscardPendingEvents = false;
		Events.Reset();
	}
	Clock = bCut ? DeltaTime : Clock + DeltaTime;
	FGinkgoCameraOverlayStep OverlayStep;
	OverlayStep.Context = Context;
	OverlayStep.Events = Events;
	OverlayStep.DeltaTime = DeltaTime;
	OverlayStep.FrameStartTime = GetWorld()->GetTimeSeconds() - DeltaTime;
	OverlayStep.Clock = Clock;
	// 第一人称时开枪的相机后坐、开镜的晃动收掉（枪自己踢、自己晃，分划离开屏幕中心），别的照旧
	OverlayStep.FirstPerson = FirstPerson;
	const FGinkgoCameraOverlayOutput Overlay = OverlayLayer.Step(Config, OverlayStep, IsOverlayEnabled());
	const FRotator Rotation = FGinkgoCameraOverlayLayer::Compose(ViewBaseRotation, Overlay.Rotation);
	// 绕跟随点转时相机沿以安全的跟随点为心的球面挪（臂长 × 角度，碰撞之后挪的）；绕相机自身时位置不动。第一人称的那一份不绕跟随点：眼睛留在原地
	const auto ApplyOverlayPivot = [&](const FVector& SafePivot, const FVector& CameraLocation)
	{
		return Config.OverlayPivot == EGinkgoCameraOverlayPivot::Pivot
			? SafePivot + Rotation.RotateVector(BaseRotation.UnrotateVector(CameraLocation - SafePivot)) : CameraLocation;
	};
	const FVector ThirdPersonLocation = ApplyOverlayPivot(Sweep.SafePivot, Location);
	FVector FinalLocation = FirstPerson > 0.f ? FMath::Lerp(ThirdPersonLocation, Eye, static_cast<double>(FirstPerson)) : ThirdPersonLocation;
	if (bCut || FirstPerson <= 0.f)
	{
		ScopeShotTime.Reset();
	}
	if (FirstPerson > 0.f)
	{
		for (const FGinkgoCameraEvent& Event : Events)
		{
			if (Event.Tag == FWeaponTags::Weapon_Event_Fire)
			{
				ScopeShotTime = Event.Time;
			}
		}
		if (ScopeShotTime.IsSet())
		{
			const double Age = GetWorld()->GetTimeSeconds() - ScopeShotTime.GetValue();
			const FVector Shift = Ginkgo::Scope::SampleShotEyeShift(Context->GetSightScope().Handling, Age);
			// 只挪最后拍到画面的眼睛：动画推算用的眼位、枪和镜内捕获仍各走原来的轨迹。
			FinalLocation += Sight.Rotation.RotateVector(Shift) * (FirstPerson * (1.0 - Clearance));
		}
	}

	// ⑥ 输出。后处理权重等有了后处理效果（速度线、暗角）再输出
	OutView = FMinimalViewInfo();
	OutView.Location = FinalLocation;
	OutView.Rotation = Rotation;
	OutView.FOV = Params.FieldOfView + Overlay.FieldOfView;
	// 第一人称时眼睛离目镜只有几厘米：引擎默认的近裁剪面（10 cm）会把目镜切掉
	OutView.PerspectiveNearClipPlane = FirstPerson > 0.f ? Config.FirstPerson.NearClipPlane : -1.f;
	LastCapsuleCenter = CapsuleCenter;
	LastParams = Params;
	AimView = OutView;
	AimView.Location = ViewLocation;
	AimView.Rotation = ViewBaseRotation;
	AimView.FOV = Params.FieldOfView;
	FirstPersonAlpha = FirstPerson;
	SightFrame = Sight;
	FirstPersonEye = Eye;

	// 只在行为通道实际采样时解算候选肩位。已到位的一侧直接用成片位置；另一侧按同帧构图和自身碰撞求解，
	// 不推进当前相机的压缩弹簧。换肩途中也比较两个完整肩位，避免把途经中央的位置当作一侧。
	const auto GetShoulderOrigin = [&](bool bRight)
	{
		const double Side = bRight ? 1.0 : -1.0;
		FGinkgoCameraParams Candidate = RightShoulderParams;
		Candidate.PivotOffsetRight *= Side;
		Candidate.ShoulderOffsetRight *= Side;
		if (FirstPerson >= 1.f || (FMath::IsNearlyEqual(Candidate.PivotOffsetRight, Params.PivotOffsetRight)
			&& FMath::IsNearlyEqual(Candidate.ShoulderOffsetRight, Params.ShoulderOffsetRight)))
		{
			return FinalLocation;
		}
		const FVector CandidatePivot = Ginkgo::Camera::ComputePivot(FollowCenter, ViewRotation, Candidate);
		const FVector CandidateBoom = Ginkgo::Camera::ComputeCameraLocation(CandidatePivot, ViewRotation, Candidate, &Config.Orbit) - CandidatePivot;
		const FCollisionSweep CandidateSweep = SweepCollision(*Pawn, CapsuleCenter, CandidatePivot, CandidateBoom, Config.ProbeRadius);
		const FVector CandidateLocation = Ginkgo::Camera::ApplyCompression(CandidateSweep.SafePivot, CandidateBoom, CandidateSweep.RequiredCompression);
		const FVector CandidateThirdPerson = ApplyOverlayPivot(CandidateSweep.SafePivot, CandidateLocation);
		// 第一人称过渡的两端共用同一出瞳和镜头冲量，仅替换还在画面中占权重的越肩位移。
		return FinalLocation + (CandidateThirdPerson - ThirdPersonLocation) * (1.0 - FirstPerson);
	};
	const UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(&Viewer);
	static const FGinkgoInputSettings DefaultSettings;
	const bool bAutoShoulder = Settings ? Settings->GetCurrent().bAdsAutoShoulder : DefaultSettings.bAdsAutoShoulder;
	Shoulder.UpdateAuto(*GetWorld(), *Pawn, GetShoulderOrigin, OutView.Rotation, Context && Context->GetTags().HasTagExact(FWeaponTags::Weapon_ADS),
		bAutoShoulder, GetWorld()->GetTimeSeconds(), Config.Shoulder,
		Settings ? Settings->GetCurrent().bRestoreShoulderOnAdsExit : DefaultSettings.bRestoreShoulderOnAdsExit);

	// 头：相机滑到离眼睛不远时藏起来（眼睛本来就在头里）。藏骨骼下一次刷新才生效，所以离得比头大就藏
	if (Context)
	{
		Context->SetHeadHidden(FirstPerson > 0.f && FVector::Dist(FinalLocation, Eye) <= Config.FirstPerson.HeadHideDistance);
	}
	// 镜内的画面：拍、合成（不在第一人称时不拍）。手上的枪有瞄具时合成一直挂在后处理里、原样返回：开镜时后处理不换路（UGinkgoScopeView::PassThrough）
	if (!ScopeView && SightComponent)
	{
		ScopeView = NewObject<UGinkgoScopeView>(this);
	}
	if (FirstPerson > 0.f)
	{
		int32 ViewportX = 0;
		int32 ViewportY = 0;
		Viewer.GetViewportSize(ViewportX, ViewportY);
		// 拉栓时眼睛让开了多少（同上面眼睛让开的比例）：镜外的模糊按它收掉
		ScopeView->Update(*GetOwner(), Pawn, Sight, Context->GetSightScope(), FirstPerson, static_cast<float>(Clearance), FVector2D(ViewportX, ViewportY), OutView);
	}
	else if (ScopeView)
	{
		ScopeView->Deactivate();
		if (SightComponent)
		{
			ScopeView->PassThrough(Context->GetSightScope(), OutView);
		}
	}

	// 血量提示暗角后处理：实时追踪角色缺失生命比例并写入 Tonemapper 原生暗角参数；满血回落归零后释放覆写，保留关卡默认后处理
	float TargetHealthMissing = 0.f;
	if (const UGinkgoDamageableComponent* Damageable = Character ? Character->GetDamageable() : Pawn->FindComponentByClass<UGinkgoDamageableComponent>())
	{
		if (Damageable->IsDead())
		{
			TargetHealthMissing = 1.f;
		}
		else if (Damageable->GetMaxHealth() > 0.f)
		{
			TargetHealthMissing = FMath::Clamp(1.f - Damageable->GetHealth() / Damageable->GetMaxHealth(), 0.f, 1.f);
		}
	}
	if (bCut || Config.HealthVignette.SmoothSpeed <= 0.f)
	{
		SmoothedHealthMissing = TargetHealthMissing;
	}
	else
	{
		SmoothedHealthMissing = FMath::FInterpTo(SmoothedHealthMissing, TargetHealthMissing, DeltaTime, Config.HealthVignette.SmoothSpeed);
		if (TargetHealthMissing <= 0.f && SmoothedHealthMissing < 1e-3f)
		{
			SmoothedHealthMissing = 0.f;
		}
	}
	if (SmoothedHealthMissing > 0.f)
	{
		float VignetteIntensity = Config.HealthVignette.BaseIntensity;
		FLinearColor VignetteColor = FLinearColor::Black;
		Ginkgo::Camera::ComputeHealthVignette(SmoothedHealthMissing, Config.HealthVignette, VignetteIntensity, VignetteColor);
		OutView.PostProcessSettings.bOverride_VignetteIntensity = true;
		OutView.PostProcessSettings.VignetteIntensity = VignetteIntensity;
		OutView.PostProcessSettings.bOverride_VignetteColor = true;
		OutView.PostProcessSettings.VignetteColor = VignetteColor;
		OutView.PostProcessBlendWeight = FMath::Max(OutView.PostProcessBlendWeight, 1.f);
	}

#if !UE_BUILD_SHIPPING
	DebugFrame.CapsuleCenter = CapsuleCenter;
	DebugFrame.FollowCenter = FollowCenter;
	DebugFrame.FeedforwardTarget = Follow.GetFeedforwardTarget();
	DebugFrame.Pivot = Pivot;
	DebugFrame.SafePivot = Sweep.SafePivot;
	DebugFrame.Boom = Boom;
	DebugFrame.RequiredCompression = Sweep.RequiredCompression;
	DebugFrame.Compression = Compression;
	DebugFrame.CompressionVelocity = Collision.GetCompressionVelocity();
	DebugFrame.Location = OutView.Location;
	DebugFrame.Rotation = OutView.Rotation;
	DebugFrame.FieldOfView = OutView.FOV;
	DebugFrame.Overlay = Overlay;
	DebugFrame.Events = MoveTemp(Events);
	DebugFrame.ProbeRadius = Config.ProbeRadius;
	DebugFrame.bFollowClamped = Follow.WasClamped();
	DebugFrame.bPivotBlocked = Sweep.bPivotBlocked;
	DebugFrame.bBoomBlocked = Sweep.bBoomBlocked;
	DebugFrame.bCut = bCut;
	DebugFrame.FirstPerson = FirstPerson;
	DebugFrame.SightEye = FirstPerson > 0.f ? Sight.Eye : FVector::ZeroVector;
	DebugFrame.SightAxis = FirstPerson > 0.f ? Sight.GetAxis() : FVector::ZeroVector;
	DebugFrame.SightRestEye = FirstPerson > 0.f ? Rest.Eye : FVector::ZeroVector;
	DebugFrame.SightRestAxis = FirstPerson > 0.f ? Rest.GetAxis() : FVector::ZeroVector;
	DebugFrame.bHeadHidden = Context && Context->IsHeadHidden();
	DebugFrame.ScopeCaptureSize = ScopeView && ScopeView->IsActive() ? ScopeView->GetCaptureSize() : 0;
#endif
}

FVector UGinkgoPlayerCameraComponent::PredictViewLocation(const APawn& Pawn, const FRotator& ViewRotation) const
{
	const FVector CapsuleCenter = Pawn.GetActorLocation();
	const bool bFollowing = FollowedTarget.Get() == &Pawn && !bCutRequested;
	// 参数取参数层上一帧的输出（滑铲时臂长变了，跟着变）；还没跟过时取基础值
	FGinkgoCameraParams Params = bFollowing ? LastParams : GetConfig().BaseParams;
	if (!bFollowing)
	{
		Params.ShoulderOffsetRight *= Shoulder.GetBlend();
		Params.PivotOffsetRight *= Shoulder.GetBlend();
	}
	const FVector FollowCenter = bFollowing ? Follow.GetValue() + (CapsuleCenter - LastCapsuleCenter) : CapsuleCenter;
	const FVector Pivot = Ginkgo::Camera::ComputePivot(FollowCenter, ViewRotation, Params);
	const FVector Boom = Ginkgo::Camera::ComputeCameraLocation(Pivot, ViewRotation, Params, &GetConfig().Orbit) - Pivot;
	const FVector ThirdPerson = Ginkgo::Camera::ApplyCompression(Pivot, Boom, bFollowing ? Collision.GetCompression() : 0.0);
	// 第一人称开镜：眼睛取上一帧的加上胶囊这一帧挪的量（枪相对胶囊的那一点挪动当作不变，同跟随点）
	if (!bFollowing || FirstPersonAlpha <= 0.f)
	{
		return ThirdPerson;
	}
	return FMath::Lerp(ThirdPerson, FirstPersonEye + (CapsuleCenter - LastCapsuleCenter), static_cast<double>(FirstPersonAlpha));
}

FRotator UGinkgoPlayerCameraComponent::SampleSightSway(const double ClockTime) const
{
	FRotator Sway = FRotator::ZeroRotator;
	for (const UGinkgoCameraOverlayEffect* Effect : GetConfig().OverlayEffects)
	{
		const UGinkgoCameraNoiseSway* Noise = Cast<UGinkgoCameraNoiseSway>(Effect);
		if (!Noise || !Noise->bEnabled || !Noise->bMovesSight || !IsOverlayEnabled())
		{
			continue;
		}
		Sway.Pitch += Noise->EvaluateNoise(0, ClockTime);
		Sway.Yaw += Noise->EvaluateNoise(1, ClockTime);
		Sway.Roll += Noise->EvaluateNoise(2, ClockTime);
	}
	return Sway;
}

FRotator UGinkgoPlayerCameraComponent::PredictSightFollow(const double DeltaSeconds, const float FirstPerson) const
{
	FRotator Total = FRotator::ZeroRotator;
	const UWorld* World = GetWorld();
	if (!World || FirstPerson <= 0.f || !IsOverlayEnabled())
	{
		return Total;
	}
	// 相机这一帧推叠加层时用的输入：还没取走的事件（这一帧开的那一枪已经入队）、帧长、帧开头的时刻、帧末的时钟
	const UGinkgoCameraContextComponent* Context = FollowedContext.Get();
	FGinkgoCameraOverlayStep Step;
	Step.Context = Context;
	if (Context)
	{
		Step.Events = Context->GetPendingEvents();
	}
	Step.DeltaTime = DeltaSeconds;
	Step.FrameStartTime = World->GetTimeSeconds() - DeltaSeconds;
	Step.Clock = Clock + DeltaSeconds;
	Step.FirstPerson = FirstPerson;
	const TArray<TObjectPtr<UGinkgoCameraOverlayEffect>>& Effects = GetConfig().OverlayEffects;
	const TConstArrayView<FGinkgoCameraOverlayState> States = OverlayLayer.GetStates();
	for (int32 Index = 0; Index < Effects.Num(); ++Index)
	{
		const UGinkgoCameraOverlayEffect* Effect = Effects[Index];
		if (!Effect || !Effect->bEnabled || !Effect->bSightFollows)
		{
			continue;
		}
		// 拷一份状态推（相机的不动）。下标同叠加层；对不上时（第一帧、刚增删了效果）相机这一帧也从静止开始
		FGinkgoCameraOverlayState State = States.Num() == Effects.Num() ? States[Index] : FGinkgoCameraOverlayState();
		FRotator Rotation = Effect->Step(State, Step).Rotation;
		// 同叠加层：第一人称时乘 FirstPersonScale 的混合
		Rotation *= FMath::Lerp(1.f, Effect->FirstPersonScale, FMath::Clamp(FirstPerson, 0.f, 1.f));
		Total += Rotation;
	}
	return Total;
}

#if UE_BUILD_SHIPPING
bool UGinkgoPlayerCameraComponent::AreModifiersEnabled()
{
	return true;
}

bool UGinkgoPlayerCameraComponent::IsOverlayEnabled()
{
	return true;
}
#else
bool UGinkgoPlayerCameraComponent::bModifiersEnabled = true;
bool UGinkgoPlayerCameraComponent::bOverlayEnabled = true;

bool UGinkgoPlayerCameraComponent::AreModifiersEnabled()
{
	return bModifiersEnabled;
}

bool UGinkgoPlayerCameraComponent::IsOverlayEnabled()
{
	return bOverlayEnabled;
}

void UGinkgoPlayerCameraComponent::SetLayersEnabled(const bool bModifiers, const bool bOverlay)
{
	bModifiersEnabled = bModifiers;
	bOverlayEnabled = bOverlay;
}
#endif

UGinkgoPlayerCameraComponent::FCollisionSweep UGinkgoPlayerCameraComponent::SweepCollision(const APawn& Pawn, const FVector& CapsuleCenter, const FVector& Pivot,
	const FVector& Boom, const float ProbeRadius) const
{
	const UWorld* World = GetWorld();
	const FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GinkgoCameraCollision), false, &Pawn);
	const FCollisionShape Probe = FCollisionShape::MakeSphere(ProbeRadius);
	FCollisionSweep Result;
	FHitResult Hit;

	Result.SafePivot = Pivot;
	if (World->SweepSingleByChannel(Hit, CapsuleCenter, Pivot, FQuat::Identity, ECC_Camera, Probe, QueryParams))
	{
		Result.SafePivot = Hit.Location;
		Result.bPivotBlocked = true;
	}

	// 一开始就嵌在几何体里时 Time 为 0：整根臂都压缩，相机在安全的跟随点上
	if (World->SweepSingleByChannel(Hit, Result.SafePivot, Result.SafePivot + Boom, FQuat::Identity, ECC_Camera, Probe, QueryParams))
	{
		Result.RequiredCompression = Boom.Size() * (1.0 - Hit.Time);
		Result.bBoomBlocked = true;
	}
	return Result;
}

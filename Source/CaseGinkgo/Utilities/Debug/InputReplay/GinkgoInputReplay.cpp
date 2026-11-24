#include "GinkgoInputReplay.h"

#if !UE_BUILD_SHIPPING

#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraContextComponent.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "CollisionQueryParams.h"
#include "Combat/GinkgoAmmoDefinition.h"  // 同一口径的几种弹
#include "Combat/GinkgoCombatTypes.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/GinkgoInteractorComponent.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/GinkgoMovementMath.h"  // CentimetersPerSourceUnit
#include "UI/GinkgoWeaponHud.h"  // 准星画在哪里
#include "UnrealClient.h"  // FScreenshotRequest
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace
{
	/** 轨迹里记下世界位置的上半身关节（FTrajectoryRow::UpperBody 的顺序，列名同骨骼名）：侧面的火柴人逐帧图用 */
	const TCHAR* const UpperBodyJointNames[] = { TEXT("spine_05"), TEXT("head"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("upperarm_r"), TEXT("lowerarm_r") };

	/** 轨迹里记下世界旋转的骨骼（FTrajectoryRow::PoseRotation 的顺序，列名是骨骼名加 _qx、_qy、_qz、_qw） */
	const TCHAR* const PoseRotationJointNames[] = { TEXT("pelvis"), TEXT("spine_05"), TEXT("head"), TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03"), TEXT("spine_04") };

	FString RecordingDirectory()
	{
		return FPaths::ProjectSavedDir() / TEXT("Ginkgo") / TEXT("Recordings");
	}

	FString ReplayDirectory()
	{
		return FPaths::ProjectSavedDir() / TEXT("Ginkgo") / TEXT("Replays");
	}

	/** 相对路径从工程目录算起 */
	FString ResolvePath(const FString& Path)
	{
		return FPaths::IsRelative(Path) ? FPaths::ProjectDir() / Path : Path;
	}

	/** 带扩展名或目录分隔符的当作路径，否则在录制目录里找 <名字>.csv */
	FString ResolveRecordingPath(const FString& Source)
	{
		if (Source.EndsWith(TEXT(".csv")) || Source.Contains(TEXT("/")) || Source.Contains(TEXT("\\")))
		{
			return ResolvePath(Source);
		}
		return RecordingDirectory() / Source + TEXT(".csv");
	}

	bool SaveText(const FString& Path, const FString& Text)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString MovementModeName(const EMovementMode Mode)
	{
		return StaticEnum<EMovementMode>()->GetNameStringByValue(Mode);
	}

	/** 窗口的汇总标签，用 | 隔开（CSV 里不能有逗号）；没有窗口时为空 */
	FString JoinWindowTags(const FGameplayTagContainer& Tags)
	{
		TStringBuilder<256> Builder;
		for (const FGameplayTag& Tag : Tags)
		{
			Builder.Appendf(TEXT("%s%s"), Builder.Len() > 0 ? TEXT("|") : TEXT(""), *Tag.ToString());
		}
		return Builder.ToString();
	}
}

FGinkgoInputReplay::FGinkgoInputReplay(AGinkgoPlayerController& InController)
	: Controller(&InController)
{
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FGinkgoInputReplay::OnWorldPostActorTick);
}

FGinkgoInputReplay::~FGinkgoInputReplay()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
}

void FGinkgoInputReplay::HandleCommandLine(AGinkgoPlayerController& Controller)
{
	// 联网的客户端在连上服务器、本地控制器生成之后才会走到这里
	static bool bCommandLineReplayConsumed = false;
	FString Source;
	if (bCommandLineReplayConsumed || !Controller.IsLocalController() || !FParse::Value(FCommandLine::Get(), TEXT("GinkgoReplay="), Source))
	{
		return;
	}
	bCommandLineReplayConsumed = true;
	FGinkgoReplayOptions Options;
	FParse::Value(FCommandLine::Get(), TEXT("GinkgoReplayOut="), Options.OutputPath);
	FParse::Value(FCommandLine::Get(), TEXT("GinkgoReplayDelay="), Options.Delay);
	FString ShotText;
	if (FParse::Value(FCommandLine::Get(), TEXT("GinkgoReplayShot="), ShotText))
	{
		Options.ShotTimes = FGinkgoReplayOptions::ParseShotTimes(ShotText);
	}
	Options.bQuitWhenDone = FParse::Param(FCommandLine::Get(), TEXT("GinkgoReplayQuit"));
	Options.bNoReset = FParse::Param(FCommandLine::Get(), TEXT("GinkgoReplayNoReset"));
	FString Error;
	if (!Controller.GetOrCreateInputReplay().RequestPlayback(Source, Options, Error))
	{
		UE_LOG(LogGinkgoControl, Error, TEXT("[Replay] %s"), *Error);
		if (Options.bQuitWhenDone)
		{
			FPlatformMisc::RequestExit(false, TEXT("GinkgoInputReplay"));
		}
	}
}

void FGinkgoInputReplay::OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	// 每个世界每帧都会广播，暂停与只推进时间的帧也会：只看控制器所在的世界里 Actor 真的更新了的帧
	if (!Controller.IsValid() || World != Controller->GetWorld() || TickType == LEVELTICK_TimeOnly || World->IsPaused())
	{
		return;
	}

	if (bQuitPending)
	{
		if (!FScreenshotRequest::IsScreenshotRequested())
		{
			bQuitPending = false;
			FPlatformMisc::RequestExit(false, TEXT("GinkgoInputReplay"));
		}
		return;
	}

	if (bRecordingPending)
	{
		BeginRecording();
	}

	const double Now = GetTime();
	if (PlaybackState == EPlaybackState::Pending)
	{
		if (!GetCharacter())
		{
			CharacterReadyTime = -1.0;
			return;
		}
		if (CharacterReadyTime < 0.0)
		{
			CharacterReadyTime = Now;
		}
		if (Now - CharacterReadyTime >= PlaybackOptions.Delay)
		{
			BeginPlayback();
		}
		return;
	}
	if (PlaybackState != EPlaybackState::Playing)
	{
		return;
	}
	const AGinkgoCharacter* Character = GetCharacter();
	if (!Character)
	{
		UE_LOG(LogGinkgoControl, Warning, TEXT("[Replay] %s：角色不见了，停止回放"), *PlaybackName);
		StopPlayback();
		return;
	}
	const double Time = Now - PlaybackStartTime;
	const UGinkgoCharacterMovementComponent* Movement = Character->GetGinkgoMovement();
	FTrajectoryRow& Row = Trajectory.Add_GetRef({Time, GFrameCounter, Character->GetActorLocation(), Movement->Velocity, Movement->MovementMode, Movement->IsSliding(),
		GetCorrectionsSinceStart(), GetCombinedSinceStart(), GetSimChecksSinceStart(), GetSimMismatchesSinceStart(), Movement->GetMaxSimStateTimeDifference(),
		JoinWindowTags(Movement->GetWindowTags()), Movement->GetDispatchedWindowChangeCount() - WindowEventsAtStart,
		Movement->GetReplayedWindowChangeCount() - WindowReplayedAtStart, Movement->GetRecombinedWindowChangeCount() - WindowRecombinedAtStart});
	RecordCamera(Row);
	RecordAnimation(Row);
	RecordInteraction(Row);
	RecordHud(Row);
	RecordActionStates(Row);
	const TArray<double>& ShotTimes = PlaybackOptions.ShotTimes;
	if (ShotsRequested < ShotTimes.Num() && Time >= ShotTimes[ShotsRequested] - UE_KINDA_SMALL_NUMBER && !FScreenshotRequest::IsScreenshotRequested())
	{
		// 带 UI：调试绘制画在视口的画布上。截的是这一帧移动之后的样子，下一次绘制视口时存下来（一帧一张：上一张还没存下来时等下一帧）
		const FString ShotPath = ShotTimes.Num() == 1 ? FPaths::ChangeExtension(PlaybackOptions.OutputPath, TEXT("png"))
			: FPaths::Combine(FPaths::GetPath(PlaybackOptions.OutputPath), FString::Printf(TEXT("%s_%d.png"), *FPaths::GetBaseFilename(PlaybackOptions.OutputPath), ShotsRequested));
		++ShotsRequested;
		FScreenshotRequest::RequestScreenshot(ShotPath, true, false);
		UE_LOG(LogGinkgoControl, Log, TEXT("[Replay] %s：%.3f 秒时截图，存到 %s"), *PlaybackName, Time, *ShotPath);
	}
	if (Time >= Playback.GetDuration() - UE_KINDA_SMALL_NUMBER)
	{
		FinishPlayback();
	}
}

TArray<double> FGinkgoReplayOptions::ParseShotTimes(const FString& Text)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("+"));
	TArray<double> Times;
	for (const FString& Part : Parts)
	{
		Times.Add(FCString::Atod(*Part));
	}
	Times.Sort();
	return Times;
}

// ===== 录制 =====

void FGinkgoInputReplay::StartRecording(const FString& Name)
{
	if (PlaybackState != EPlaybackState::None)
	{
		UE_LOG(LogGinkgoControl, Warning, TEXT("[Replay] 回放中不能录制"));
		return;
	}
	if (bRecording)
	{
		StopRecording();
	}
	RecordingName = Name;
	bRecordingPending = true;
}

void FGinkgoInputReplay::BeginRecording()
{
	const AGinkgoCharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	bRecordingPending = false;
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Recording = FGinkgoInputRecording();
	Recording.Start.bRelative = false;
	Recording.Start.Location = Character->GetActorLocation();
	Recording.Start.Velocity = Movement->Velocity;
	Recording.Start.ControlRotation = Controller->GetControlRotation();
	Recording.Start.MovementMode = Movement->MovementMode;
	RecordingStartTime = GetTime();
	bRecording = true;
	UE_LOG(LogGinkgoControl, Log, TEXT("[Replay] 开始录制 %s（%s）"), *RecordingName, *Controller->GetWorld()->GetMapName());
}

void FGinkgoInputReplay::RecordFrame(const FGinkgoInputIntent& Input)
{
	if (!bRecording)
	{
		return;
	}
	Recording.Samples.Add({Input, GetTime() - RecordingStartTime});
}

void FGinkgoInputReplay::StopRecording()
{
	bRecordingPending = false;
	if (!bRecording)
	{
		return;
	}
	bRecording = false;
	const FString Path = RecordingDirectory() / RecordingName + TEXT(".csv");
	const bool bSaved = SaveText(Path, Recording.ToCsv());
	UE_LOG(LogGinkgoControl, Log, TEXT("[Replay] 录制 %s：%d 帧，%.2f 秒，%s %s"), *RecordingName, Recording.Samples.Num(), Recording.GetDuration(),
		bSaved ? TEXT("写到") : TEXT("写入失败："), *Path);
}

// ===== 回放 =====

bool FGinkgoInputReplay::RequestPlayback(const FString& Source, const FGinkgoReplayOptions& Options, FString& OutError)
{
	const FString Path = ResolveRecordingPath(Source);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("读不到 %s"), *Path);
		return false;
	}
	FGinkgoInputRecording Loaded;
	if (!FGinkgoInputRecording::FromCsv(Text, Loaded, OutError))
	{
		OutError = FString::Printf(TEXT("%s：%s"), *Path, *OutError);
		return false;
	}
	if (Loaded.Samples.IsEmpty())
	{
		OutError = FString::Printf(TEXT("%s 里没有采样"), *Path);
		return false;
	}
	// 回放会覆盖实时输入，同时录制没有意义
	StopRecording();
	Playback = MoveTemp(Loaded);
	PlaybackName = FPaths::GetBaseFilename(Path);
	PlaybackOptions = Options;
	PlaybackOptions.OutputPath = Options.OutputPath.IsEmpty() ? ReplayDirectory() / PlaybackName + TEXT(".csv") : ResolvePath(Options.OutputPath);
	CharacterReadyTime = -1.0;
	PlaybackState = EPlaybackState::Pending;
	return true;
}

void FGinkgoInputReplay::BeginPlayback()
{
	AGinkgoCharacter* Character = GetCharacter();
	UGinkgoCharacterMovementComponent* Movement = Character->GetGinkgoMovement();
	const FGinkgoInputRecordingStart& Start = Playback.Start;
	// 无界面运行时网格不渲染，引擎默认只推进动画、不求值骨骼（AlwaysTickPose），脚的位置与接触曲线都停在参考姿势：
	// 回放的角色总是求值，量得到脚滑。游戏里自己的角色总在画面里，本来就求值
	Character->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	if (!PlaybackOptions.bNoReset)
	{
		const FVector Location = Start.bRelative ? Character->GetActorLocation() + Start.Location : Start.Location;
		Character->UnCrouch();
		Character->SetWantsToSprint(false);
		Character->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		// 先切模式再给速度：切到行走时父类会把竖直速度清零
		Movement->SetMovementMode(Start.MovementMode);
		Movement->Velocity = Start.Velocity;
		// 切模式可能按落地、离地发出窗口事件；模拟状态最后整体换成初始值：落地已久、不在滑铲、没有窗口，开始状态不随之前发生过什么变。
		// 方向键按下的计数是输入，回放开始前按过的不算：处理过的计数对齐到现在的计数
		FGinkgoMovementSimState Initial;
		Initial.HandledDirectionPressCount = Movement->GetDirectionPressCount();
		Movement->RestoreSimState(Initial);
		Controller->SetControlRotation(Start.ControlRotation);
		// 角色是传送过去的：相机的弹簧不追过去，下一帧直接放到位；动画也不拿传送前的位置差分
		if (AGinkgoPlayerCameraManager* CameraManager = Cast<AGinkgoPlayerCameraManager>(Controller->PlayerCameraManager))
		{
			CameraManager->RequestCameraCut();
		}
		// 动作状态的树从事实重选：回放之前放到一半的不接着放
		if (UGinkgoWeaponComponent* Weapons = Character->GetWeapons())
		{
			Weapons->RestartActionStates();
		}
		if (UGinkgoAnimInstance* AnimInstance = Cast<UGinkgoAnimInstance>(Character->GetMesh()->GetAnimInstance()))
		{
			AnimInstance->RequestCut();
		}
	}

	PlaybackStartTime = GetTime();
	LastPlaybackInputTime = -1.0;
	ShotsRequested = 0;
	CorrectionsAtStart = Movement->GetCorrectionCount();
	CombinedAtStart = Movement->GetCombinedMoveCount();
	SimChecksAtStart = Movement->GetSimStateCheckCount();
	SimMismatchesAtStart = Movement->GetSimStateMismatchCount();
	WindowEventsAtStart = Movement->GetDispatchedWindowChangeCount();
	WindowReplayedAtStart = Movement->GetReplayedWindowChangeCount();
	WindowRecombinedAtStart = Movement->GetRecombinedWindowChangeCount();
	Trajectory.Reset();
	FTrajectoryRow& Row = Trajectory.Add_GetRef({0.0, GFrameCounter, Character->GetActorLocation(), Movement->Velocity, Movement->MovementMode, Movement->IsSliding(),
		0, 0, 0, 0, Movement->GetMaxSimStateTimeDifference(), JoinWindowTags(Movement->GetWindowTags()), 0, 0, 0});
	RecordCamera(Row);
	RecordAnimation(Row);
	RecordInteraction(Row);
	RecordHud(Row);
	RecordActionStates(Row);
	PlaybackState = EPlaybackState::Playing;
	UE_LOG(LogGinkgoControl, Log, TEXT("[Replay] 开始回放 %s：%d 个采样，%.3f 秒，帧长 %.4f 秒%s"), *PlaybackName, Playback.Samples.Num(),
		Playback.GetDuration(), FApp::GetDeltaTime(), PlaybackOptions.bNoReset ? TEXT("，不重置角色") : TEXT(""));
}

void FGinkgoInputReplay::RecordCamera(FTrajectoryRow& Row) const
{
	const APlayerCameraManager* CameraManager = Controller.IsValid() ? Controller->PlayerCameraManager.Get() : nullptr;
	if (!CameraManager)
	{
		return;
	}
	const FMinimalViewInfo& View = CameraManager->GetCameraCacheView();
	Row.CameraLocation = View.Location;
	Row.CameraRotation = View.Rotation;
	Row.CameraFOV = View.FOV;
	// 这一帧真正的相机沿瞄准打线，同动画实例推算瞄准点的那一条（子弹的通道 Weapon，忽略角色，最远 100 m）：量推算的误差与枪口对着哪。
	// 瞄准沿叠加之前的视角（相机的叠加层只晃画面，2026-11-01）：从叠加之前的相机打，不沿晃过的画面中心
	constexpr double CrosshairTraceDistance = 10000.0;
	Row.CrosshairDistance = CrosshairTraceDistance;
	const AGinkgoPlayerCameraManager* AimCamera = Cast<AGinkgoPlayerCameraManager>(CameraManager);
	const FMinimalViewInfo& AimView = AimCamera ? AimCamera->GetAimView() : View;
	const APawn* Pawn = Controller->GetPawn();
	FHitResult Hit;
	if (Pawn && Pawn->GetWorld()->LineTraceSingleByChannel(Hit, AimView.Location, AimView.Location + AimView.Rotation.Vector() * CrosshairTraceDistance, Ginkgo::Combat::WeaponTraceChannel,
		FCollisionQueryParams(SCENE_QUERY_STAT(GinkgoReplayCrosshair), /*bTraceComplex*/ false, Pawn)))
	{
		Row.CrosshairDistance = Hit.Distance;
	}
	if (const AGinkgoPlayerCameraManager* GinkgoCamera = Cast<AGinkgoPlayerCameraManager>(CameraManager))
	{
		const AGinkgoPlayerCameraManager::FDebugFrame& Frame = GinkgoCamera->GetDebugFrame();
		Row.CameraPivot = Frame.Pivot;
		Row.CameraPivotPull = FVector::Dist(Frame.Pivot, Frame.SafePivot);
		Row.CameraRequiredCompression = Frame.RequiredCompression;
		Row.CameraCompression = Frame.Compression;

		// 参数层与叠加层
		const FGinkgoCameraParams& Params = GinkgoCamera->GetParams();
		Row.CameraArm = Params.ArmLength;
		Row.CameraShoulderRight = Params.ShoulderOffsetRight;
		Row.CameraShoulderUp = Params.ShoulderOffsetUp;
		Row.CameraPivotUp = Params.PivotOffsetUp;
		Row.CameraParamFov = Params.FieldOfView;
		Row.CameraView = GinkgoCamera->GetAimView().Rotation;
		Row.CameraOverlay = Frame.Overlay.Rotation;
		Row.CameraOverlayFov = Frame.Overlay.FieldOfView;
		Row.CameraClock = GinkgoCamera->GetClock();
		const UGinkgoCameraConfig& Config = GinkgoCamera->GetConfig();
		const TConstArrayView<FGinkgoCameraParamLayer::FModifierState> Modifiers = GinkgoCamera->GetParamLayer().GetModifierStates();
		TArray<FString> Parts;
		for (int32 Index = 0; Index < Modifiers.Num() && Index < Config.Modifiers.Num(); ++Index)
		{
			Parts.Add(FString::Printf(TEXT("%s:%.6f:%.4f:%.6f"), *Config.Modifiers[Index].Name.ToString(), Modifiers[Index].Weight, Modifiers[Index].Value,
				Modifiers[Index].Block));
		}
		Row.CameraModifiers = FString::Join(Parts, TEXT("|"));
		Parts.Reset();
		const TConstArrayView<FGinkgoCameraOverlayState> Effects = GinkgoCamera->GetOverlayLayer().GetStates();
		for (int32 Index = 0; Index < Effects.Num() && Index < Config.OverlayEffects.Num(); ++Index)
		{
			const FGinkgoCameraOverlayState& State = Effects[Index];
			Parts.Add(FString::Printf(TEXT("%s:%.6f:%.6f:%.6f:%.6f:%.6f:%d"), Config.OverlayEffects[Index] ? *Config.OverlayEffects[Index]->Name.ToString() : TEXT("none"),
				State.Output.Rotation.Pitch, State.Output.Rotation.Yaw, State.Output.Rotation.Roll, State.Output.FieldOfView, State.Envelope.Value, State.ImpulseCount));
		}
		Row.CameraEffects = FString::Join(Parts, TEXT("|"));
		Parts.Reset();
		const double FrameStart = GinkgoCamera->GetWorld()->GetTimeSeconds() - GinkgoCamera->GetWorld()->GetDeltaSeconds();
		for (const FGinkgoCameraEvent& Event : Frame.Events)
		{
			Parts.Add(FString::Printf(TEXT("%s@%.6f:%.4f"), *Event.Tag.ToString(), Event.Time - FrameStart, Event.Magnitude));
		}
		Row.CameraEvents = FString::Join(Parts, TEXT(";"));
		// 第一人称开镜
		Row.ScopeFirstPerson = Frame.FirstPerson;
		Row.ScopeEye = Frame.SightEye;
		Row.ScopeAxis = Frame.SightAxis;
		Row.ScopeRestEye = Frame.SightRestEye;
		Row.ScopeRestAxis = Frame.SightRestAxis;
		Row.bScopeHeadHidden = Frame.bHeadHidden;
		Row.ScopeCapture = Frame.ScopeCaptureSize;
		Row.CameraNearClip = GinkgoCamera->GetCameraCacheView().PerspectiveNearClipPlane;
	}
	if (const UGinkgoCameraContextComponent* Context = UGinkgoCameraContextComponent::Find(Controller->GetPawn()))
	{
		TArray<FString> Parts;
		for (const FGameplayTag& Tag : Context->GetTags())
		{
			Parts.Add(Tag.ToString());
		}
		Parts.Sort();
		Row.CameraContextTags = FString::Join(Parts, TEXT("|"));
		Parts.Reset();
		for (const TPair<FGameplayTag, float>& Scalar : Context->GetScalars())
		{
			Parts.Add(FString::Printf(TEXT("%s=%.7f"), *Scalar.Key.ToString(), Scalar.Value));
		}
		Parts.Sort();
		Row.CameraContextScalars = FString::Join(Parts, TEXT("|"));
		Row.CameraContextEvents = Context->GetPushedEventCount();
	}
}

void FGinkgoInputReplay::RecordAnimation(FTrajectoryRow& Row) const
{
	const AGinkgoCharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	Row.ActorYaw = Character->GetActorRotation().Yaw;
	Row.ControlYaw = Controller->GetControlRotation().Yaw;
	Row.bSprintFacing = Character->GetGinkgoMovement()->IsSprintFacing();
	const UGinkgoAnimInstance* AnimInstance = Cast<UGinkgoAnimInstance>(Character->GetMesh()->GetAnimInstance());
	if (!AnimInstance)
	{
		return;
	}
	const FGinkgoAnimMotion& Motion = AnimInstance->GetMotion();
	const FGinkgoAnimIntentState& Intent = AnimInstance->GetIntent();
	Row.AnimVelocity = Motion.Velocity;
	Row.AnimVerticalSpeed = Motion.VerticalSpeed;
	Row.AnimFacingYaw = Motion.FacingYaw;
	Row.AnimAcceleration = Motion.Acceleration;
	Row.AnimCorrection = AnimInstance->GetDebugFrame().CorrectionOffset.Size();
	Row.bAnimHasIntent = Intent.bHasIntent;
	Row.AnimStopDistance = Intent.StopDistance;
	Row.AnimPivotDistance = Intent.PivotDistance;
	Row.bAnimReset = AnimInstance->GetDebugFrame().bReset;

	const UGinkgoAnimInstance::FDebugFrame& Frame = AnimInstance->GetDebugFrame();
	Row.AnimState = StaticEnum<EGinkgoLocomotionState>()->GetNameStringByValue(static_cast<int64>(AnimInstance->GetLocomotionState()));
	Row.AnimGait = StaticEnum<EGinkgoAnimGait>()->GetNameStringByValue(static_cast<int64>(AnimInstance->GetGait()));
	Row.AnimDirection = StaticEnum<EGinkgoAnimDirection>()->GetNameStringByValue(static_cast<int64>(AnimInstance->GetDirection()));
	Row.AnimClip = Frame.Sequence ? Frame.Sequence->GetName() : FString();
	Row.AnimClipTime = Frame.SequenceTime;
	Row.AnimPlayRate = Frame.PlayRate;
	Row.AnimStride = AnimInstance->GetStrideScale();
	Row.AnimWarp = AnimInstance->GetOrientationWarpingAngle();
	Row.AnimRootYaw = AnimInstance->GetRootYawOffset();
	// 骨骼的世界位置与接触曲线：网格的求值在这一帧所有 Actor 更新完之前已经结束
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	Row.FootLeft = Mesh->GetBoneLocation(TEXT("foot_l"));
	Row.FootRight = Mesh->GetBoneLocation(TEXT("foot_r"));
	Row.BallLeft = Mesh->GetBoneLocation(TEXT("ball_l"));
	Row.BallRight = Mesh->GetBoneLocation(TEXT("ball_r"));
	Row.PelvisZ = Mesh->GetBoneLocation(TEXT("pelvis")).Z;
	Row.Thigh[0] = Mesh->GetBoneLocation(TEXT("thigh_l"));
	Row.Thigh[1] = Mesh->GetBoneLocation(TEXT("thigh_r"));
	Row.Calf[0] = Mesh->GetBoneLocation(TEXT("calf_l"));
	Row.Calf[1] = Mesh->GetBoneLocation(TEXT("calf_r"));
	const FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(GinkgoReplayGround), false, Character);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FVector& Ball = Side == 0 ? Row.BallLeft : Row.BallRight;
		FHitResult Hit;
		FCollisionObjectQueryParams Objects(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		const bool bHit = Character->GetWorld()->LineTraceSingleByObjectType(Hit, Ball + FVector(0.0, 0.0, 60.0), Ball - FVector(0.0, 0.0, 150.0), Objects, GroundParams);
		Row.GroundUnderBall[Side] = bHit ? Hit.ImpactPoint.Z : std::numeric_limits<double>::quiet_NaN();
	}
	Row.ContactLeft = AnimInstance->GetCurveValue(TEXT("contact_l"));
	Row.ContactRight = AnimInstance->GetCurveValue(TEXT("contact_r"));
	Row.TurnRemaining = AnimInstance->GetCurveValue(TEXT("RemainingTurnYaw"));
	Row.TurnWeight = AnimInstance->GetCurveValue(TEXT("TurnYawWeight"));
	Row.AnimTimeToImpact = Frame.TimeToImpact;
	Row.AnimLandImpact = Frame.LandImpactTime;
	Row.AnimLean = AnimInstance->GetLeanAmount();
	Row.AnimLeanSprint = AnimInstance->GetLeanSprintWeight();
	Row.LegIKAlpha = AnimInstance->GetLegIKAlpha();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const UGinkgoAnimInstance::FDebugFrame::FLegIKFoot& Foot = Frame.LegIK[Side];
		Row.LegIKLock[Side] = Foot.Lock;
		Row.bLegIKPlanted[Side] = Foot.bPlanted;
		Row.LegIKCorrection[Side] = Foot.Correction;
		Row.LegIKLandIn[Side] = Foot.TimeToLand;
		Row.LegIKLanding[Side] = Foot.PredictedLanding;
		Row.LegIKAnimBall[Side] = Foot.AnimBall;
		Row.LegIKTerrain[Side] = Foot.bHasTerrain ? Foot.TerrainZ : std::numeric_limits<double>::quiet_NaN();
		Row.LegIKVerticalCorrection[Side] = Foot.VerticalCorrection;
		Row.LegIKTerrainSource[Side] = Foot.TerrainSource;
		Row.LegIKRawTerrain[Side] = Foot.TerrainSource != 0 ? Foot.RawTerrainZ : std::numeric_limits<double>::quiet_NaN();
		Row.LegIKAnimFoot[Side] = Foot.AnimFoot;
		Row.LegIKAnimKnee[Side] = Foot.AnimKnee;
		Row.LegIKFootTarget[Side] = Foot.FootTarget;
		Row.bLegIKLedge[Side] = Foot.bLedge;
	}
	Row.LegIKPelvisOffset = Frame.LegIKPelvisOffset;
	Row.LegIKTraces = Frame.LegIKTraces;
	Row.LegIKPelvisLine = Frame.LegIKPelvisLine;
	Row.LegIKPelvisUpper = Frame.LegIKPelvisUpper;
	Row.LegIKPelvisLower = Frame.LegIKPelvisLower;
	Row.LegIKPelvisAdjust = Frame.LegIKPelvisAdjust;

	// 动画动作与武器：调试日志每帧取走，记的是这一帧发过的事件与结局
	if (UGinkgoAnimActionComponent* Actions = GetCharacter()->GetAnimActions())
	{
		if (const FGinkgoAnimActionRunner* Runner = Actions->GetRunner())
		{
			const TArray<UGinkgoAnimAction*> Leaves = Runner->GetRunningLeaves();
			Row.Action = FString::JoinBy(Leaves, TEXT("+"), [](const UGinkgoAnimAction* Leaf) { return Leaf->GetActionName().ToString(); });
			Row.ActionTime = Leaves.IsEmpty() || !Leaves[0]->GetMontage() ? 0.0 : AnimInstance->Montage_GetPosition(Leaves[0]->GetMontage());
		}
		Row.ActionLog = FString::Join(Actions->TakeDebugLog(), TEXT(";"));
	}
	if (const UPoseableMeshComponent* Weapon = Character->GetWeapon(); Weapon && Weapon->GetSkinnedAsset())
	{
		Row.WeaponParent = Weapon->GetAttachSocketName().ToString();
		Row.Weapon = Weapon->GetComponentLocation();
		Row.WeaponRotation = Weapon->GetComponentQuat();
		Row.BoltTravel = Character->GetWeaponBolt(0).Travel;
		Row.BoltLift = Character->GetWeaponBolt(0).Lift;
		Row.BoltKnob = Weapon->GetSocketLocation(TEXT("bolt_knob"));
	}
	if (const UGinkgoWeaponComponent* Weapons = Character->GetWeapons())
	{
		const FGinkgoHoldState& Hold = Weapons->GetHoldState();
		Row.HoldStance = static_cast<int32>(Hold.Stance);
		Row.HoldRaise = Hold.RaiseProgress;
		Row.HoldAds = Hold.AdsProgress;
		Row.HoldSprint = Hold.SprintProgress;
	}
	Row.LocomotionTime = AnimInstance->GetLocomotionTime();
	const UGinkgoAnimInstance::FDebugFrame::FWeaponLayer& Layer = Frame.WeaponLayer;
	Row.LayerRunAlpha = Layer.RunAlpha;
	Row.LayerSprintAlpha = Layer.SprintAlpha;
	Row.LayerRunTime = Layer.RunTime;
	Row.LayerSprintTime = Layer.SprintTime;
	Row.LayerAimAlpha = Layer.AimAlpha;
	Row.LayerUpperAlpha = Layer.UpperBodyAlpha;
	Row.LayerAimTime = Layer.AimTime;
	Row.LoopClock = AnimInstance->GetWeaponAnimData().LoopClock;
	static_assert(UE_ARRAY_COUNT(UpperBodyJointNames) == FTrajectoryRow::UpperBodyJointCount);
	for (int32 Joint = 0; Joint < FTrajectoryRow::UpperBodyJointCount; ++Joint)
	{
		Row.UpperBody[Joint] = Mesh->GetBoneLocation(UpperBodyJointNames[Joint]);
	}
	static_assert(UE_ARRAY_COUNT(PoseRotationJointNames) == FTrajectoryRow::PoseRotationJointCount);
	for (int32 Joint = 0; Joint < FTrajectoryRow::PoseRotationJointCount; ++Joint)
	{
		Row.PoseRotation[Joint] = Mesh->GetBoneQuaternion(PoseRotationJointNames[Joint], EBoneSpaces::WorldSpace);
	}
	const FGinkgoWeaponAnimData& WeaponAnim = AnimInstance->GetWeaponAnimData();
	Row.AimYaw = WeaponAnim.AimYaw;
	Row.AimPitch = WeaponAnim.AimPitch;
	const FGinkgoWeaponRigData& WeaponRig = AnimInstance->GetWeaponRigData();
	Row.GunKickBack = -WeaponRig.GunKick.GetLocation().X;
	Row.GunKickClimb = WeaponRig.GunKick.Rotator().Pitch;
	Row.LeftHandGrip = WeaponRig.LeftHandGrip;
	Row.GunClipWeight = AnimInstance->GetGunClipWeight();
	Row.bHasAimPoint = WeaponRig.bHasAimPoint;
	Row.AimPoint = WeaponRig.AimPoint;
	const UGinkgoAnimInstance::FDebugFrame::FWeaponAim& Aim = AnimInstance->GetDebugFrame().WeaponAim;
	Row.AimAlpha = Aim.AimAlpha;
	Row.AimResidualBefore = Aim.ResidualBefore;
	Row.AimResidualAfter = Aim.ResidualAfter;
	Row.AimSpineAngle = Aim.SpineAngle;
	Row.AimGunAngle = Aim.GunAngle;
	Row.Muzzle = Aim.Muzzle;
	Row.Bore = Aim.Bore;
	Row.HandTarget[0] = Aim.LeftTarget;
	Row.HandTarget[1] = Aim.RightTarget;
	Row.HandAlpha[0] = Aim.LeftAlpha;
	Row.HandAlpha[1] = Aim.RightAlpha;
	Row.MeshLocation = Mesh->GetComponentLocation();
	Row.HandLeft = Mesh->GetBoneLocation(TEXT("hand_l"));
	Row.HandRight = Mesh->GetBoneLocation(TEXT("hand_r"));
	// 枪上的蒙太奇（拔枪、收枪在右臂的槽上）
	Row.UpperBodyWeight = FMath::Max(AnimInstance->GetSlotMontageGlobalWeight(TEXT("UpperBody")), AnimInstance->GetSlotMontageGlobalWeight(TEXT("ArmR")));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FGinkgoHandReachRigData& Reach = WeaponRig.Reach[Side];
		Row.bHandItem[Side] = Character->IsHandItemShown(static_cast<EGinkgoHand>(Side));
		for (const FGinkgoHandReachRigData& Fading : WeaponRig.FadingReach[Side])
		{
			Row.ReachFading[Side] += Fading.Weight;
		}
		if (!Reach.bActive)
		{
			continue;
		}
		Row.ReachWeight[Side] = Reach.Weight;
		Row.ReachBearing[Side] = Reach.Bearing;
		Row.ReachGrab[Side] = Reach.Grab;
		Row.ReachPlace[Side] = Reach.Place;
		Row.ReachItem[Side] = Reach.Item;
		Row.ReachPocket[Side] = Mesh->GetSocketTransform(Reach.StowBone).TransformPosition(Reach.PocketInChest);
		Row.ReachHeld[Side] = Mesh->GetSocketTransform(Side == 0 ? TEXT("hand_l") : TEXT("hand_r")).TransformPosition(Reach.ItemInHand);
	}
}

void FGinkgoInputReplay::RecordInteraction(FTrajectoryRow& Row) const
{
	const AGinkgoCharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	TStringBuilder<128> Slots;
	for (int32 Slot = 0; Slot < UGinkgoWeaponComponent::SlotCount; ++Slot)
	{
		// 槽里的是枪的数据资产，名字写它的网格（同以前的列）
		const UGinkgoWeaponDefinition* Weapon = Character->GetWeapons()->GetCarried(Slot);
		const UStaticMesh* WeaponMesh = Weapon ? Weapon->Mesh.Get() : nullptr;
		Slots.Appendf(TEXT("%s%s"), Slot > 0 ? TEXT("|") : TEXT(""), WeaponMesh ? *WeaponMesh->GetName() : TEXT(""));
	}
	Row.WeaponSlots = Slots.ToString();

	const UGinkgoInteractorComponent* Interactor = Character->GetInteractor();
	if (!Interactor)
	{
		return;
	}
	const UGinkgoInteractorComponent::FCandidate* Nearest = nullptr;
	for (const UGinkgoInteractorComponent::FCandidate& Candidate : Interactor->GetCandidates())
	{
		if (Candidate.State != EGinkgoInteractionState::Hidden && (!Nearest || Candidate.Distance < Nearest->Distance))
		{
			Nearest = &Candidate;
		}
	}
	if (Nearest)
	{
		Row.InteractionTarget = Ginkgo::Interaction::DescribeInteractable(Nearest->Target.GetObject());
		Row.InteractionState = StaticEnum<EGinkgoInteractionState>()->GetNameStringByValue(static_cast<int64>(Nearest->State));
		Row.InteractionDistance = Nearest->Distance;
		Row.InteractionAngle = Nearest->ViewAngle;
	}
	Row.InteractionPending = Interactor->GetPendingTarget() ? Ginkgo::Interaction::DescribeInteractable(Interactor->GetPendingTarget()) : FString();
	if (Interactor->GetLastResultTime() >= PlaybackStartTime)
	{
		Row.InteractionResult = StaticEnum<EGinkgoInteractionResult>()->GetNameStringByValue(static_cast<int64>(Interactor->GetLastResult()));
		Row.InteractionResultTime = Interactor->GetLastResultTime() - PlaybackStartTime;
	}
}

FGinkgoInputIntent FGinkgoInputReplay::TakePlaybackInput()
{
	const double Time = GetTime() - PlaybackStartTime;
	FGinkgoInputIntent Input = Playback.Evaluate(Time);
	Input.bJumpPressed = Playback.HasJumpPressBetween(LastPlaybackInputTime, Time);
	Input.bInteractPressed = Playback.HasInteractPressBetween(LastPlaybackInputTime, Time);
	Input.bSwitchShoulderPressed = Playback.ShouldSwitchShoulderBetween(LastPlaybackInputTime, Time);
	Input.DirectionPresses = Playback.CountDirectionPressesBetween(LastPlaybackInputTime, Time);
	Input.WeaponKeys = Playback.GetWeaponKeysBetween(LastPlaybackInputTime, Time);
	Input.WheelMouse = Playback.GetWheelMouseBetween(LastPlaybackInputTime, Time);
	LastPlaybackInputTime = Time;
	return Input;
}

void FGinkgoInputReplay::RecordHud(FTrajectoryRow& Row) const
{
	if (!Controller.IsValid())
	{
		return;
	}
	const FGinkgoHudAim Aim = FGinkgoWeaponHud::FindAim(*Controller);
	Row.HudAimSource = static_cast<int32>(Aim.Source);
	Row.HudAimPoint = Aim.Point;
	Row.HudAdsAlpha = Aim.AdsAlpha;
	FGinkgoWeaponHud::ProjectDot(*Controller, Aim, Row.HudAimScreen);
	Row.bHudHit = Aim.bHasHit;
	Row.HudHitPoint = Aim.HitPoint;
	Row.HudHitGap = Aim.HitGap;
	Row.HudHitScale = Aim.HitScale;
	Row.HudDash = Aim.DashOffset;
	Row.HudSpreadPixels = Aim.SpreadPixels;
	Row.HudReticleOpen = Aim.ReticleOpen;
	FGinkgoWeaponHud::ProjectHit(*Controller, Aim, Row.HudHitScreen);
	const AGinkgoCharacter* Character = GetCharacter();
	const UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
	Row.HipAimLag = Weapons ? Weapons->GetHipAimLag() : FVector2D::ZeroVector;
	if (const UGinkgoWeaponDefinition* Weapon = Weapons ? Weapons->GetHeldWeapon() : nullptr)
	{
		Row.HipHandling = FVector(Weapon->HipHandlingOmega, Weapon->HipHandlingZeta, Weapon->HipHandlingMaxLag);
	}
}

void FGinkgoInputReplay::RecordActionStates(FTrajectoryRow& Row) const
{
	const AGinkgoCharacter* Character = GetCharacter();
	const UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
	if (!Weapons)
	{
		return;
	}
	Row.WeaponState = Weapons->GetActionStatePath();
	if (const UGinkgoAnimActionComponent* Actions = Character->GetAnimActions(); Actions && Actions->GetRunner())
	{
		Row.Occupancy = Actions->GetRunner()->GetOccupancyText();
	}
	Row.HeldSlot = Weapons->GetHeldSlot();
	Row.TargetSlot = Weapons->GetTargetSlot();
	Row.LastSlot = Weapons->GetLastHeldSlot();
	Row.bAimAllowed = Weapons->IsAimAllowed();
	Row.bWantsFire = Weapons->WantsFire();
	Row.bWantsReload = Weapons->WantsReload();
	Row.Shots = Weapons->GetDebugShots().Count;
	const FGameplayTag CueTags[] = {FWeaponTags::Weapon_Cue_Fire, FWeaponTags::Weapon_Cue_BoltLift, FWeaponTags::Weapon_Cue_BoltPull, FWeaponTags::Weapon_Cue_BoltPush,
		FWeaponTags::Weapon_Cue_BoltPress, FWeaponTags::Weapon_Cue_RoundIn, FWeaponTags::Weapon_Cue_MagOut, FWeaponTags::Weapon_Cue_MagIn, FWeaponTags::Weapon_Cue_SlideHome};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Row.Cues); ++Index)
	{
		Row.Cues[Index] = Weapons->GetDebugCueCount(CueTags[Index]);
	}
	Row.MuzzleWisp = Weapons->GetMuzzleWispRemaining(0);
	const UGinkgoWeaponComponent::FDebugEject Eject = Weapons->GetDebugEject();
	Row.Ejects = Eject.Count;
	Row.EjectsLive = Eject.LiveCount;
	Row.EjectPoint = Eject.Location;
	Row.EjectSpeed = static_cast<float>(Eject.Speed);
	Row.ShellHits = Eject.HitSounds;
	Row.ShellHitsHard = Eject.HardHitSounds;
	Row.ShellHitSpeed = static_cast<float>(Eject.LastHitSpeed);
	Row.ShellHitVolume = Eject.LastHitVolume;
	Row.bFeedRound = Character->IsFeedRoundShown(0);
	Row.FeedRoundPoint = Character->GetFeedRoundLocation(0);
	Row.ShotMuzzle = Weapons->GetDebugShots().Muzzle;
	Row.ShotPoint = Weapons->GetDebugShots().Point;
	Row.ShotAimed = Weapons->GetDebugShots().AimedPoint;
	Row.ShotSpread = Weapons->GetDebugShots().Spread;
	Row.ShotDeviation = Weapons->GetDebugShots().Deviation;
	Row.Trails = Weapons->GetDebugShots().Trails;
	Row.TrailsAlive = Weapons->GetDebugTrailsAlive();
	Row.TrailLength = Weapons->GetDebugShots().TrailLength;
	Row.TrailPoints = Weapons->GetDebugShots().TrailPoints;
	Row.TrailReveal = Weapons->GetDebugShots().TrailReveal;
	Row.ScopeSway = Weapons->GetSightSway();
	Row.ScopeOffset = Weapons->GetSightOffset();
	Row.ScopeMoveSway = Weapons->GetSightMoveSway();
	Row.ScopeMoveShift = Weapons->GetSightMoveShift();
	Row.ScopeRecoil = Weapons->GetSightRecoil();
	if (const UPoseableMeshComponent* Held = Character->GetWeaponSlotComponent(Weapons->GetHeldSlot()); Held && Held->GetSkinnedAsset())
	{
		Row.HeldGun = Held->GetComponentLocation();
		Row.HeldGunRotation = Held->GetComponentQuat();
	}
	Row.PickupSettle = Weapons->GetPickupSettleAlpha();
	Row.HitConfirm = Weapons->GetHitConfirm();
	// 弹种：装填键、轮盘、手上那把枪装的是哪种
	Row.bReloadPending = Weapons->GetReloadKey().bPending;
	Row.ReloadHeldTime = Weapons->GetReloadKey().HeldTime;
	const FGinkgoAmmoWheel& Wheel = Weapons->GetAmmoWheel();
	Row.bWheelOpen = Wheel.bOpen;
	Row.WheelSelected = Wheel.Selected;
	Row.WheelMotion = Wheel.Motion;
	Row.WheelSerial = Wheel.Serial;
	{
		static const TCHAR* const ResultNames[] = {TEXT("none"), TEXT("confirmed"), TEXT("cancelled"), TEXT("unavailable")};
		Row.WheelResult = ResultNames[FMath::Clamp(static_cast<int32>(Wheel.LastResult), 0, 3)];
	}
	Row.bWantsChamberSwap = Weapons->WantsChamberSwap();
	if (const UGinkgoWeaponDefinition* Held = Weapons->GetHeldWeapon())
	{
		const int32 HeldSlot = Weapons->GetHeldSlot();
		const FGameplayTag Load = Weapons->GetLoadAmmo(HeldSlot);
		Row.LoadAmmo = Load.ToString();
		Row.LoadReserve = Weapons->GetAmmoReserve().Count(Load);
		if (const FGinkgoMechanismState* State = Weapons->GetMechanismState(HeldSlot))
		{
			Row.ChamberAmmo = State->Chamber == EGinkgoChamber::Empty ? FString() : State->ChamberAmmo.ToString();
			for (const FGameplayTag& Round : State->Feed)
			{
				Row.FeedAmmo += (Row.FeedAmmo.IsEmpty() ? TEXT("") : TEXT("|")) + Round.ToString();
			}
		}
		for (const FGameplayTag& Choice : UGinkgoAmmoDefinition::GetChoices(Held->AmmoType))
		{
			Row.AmmoReserves += FString::Printf(TEXT("%s%s:%d"), Row.AmmoReserves.IsEmpty() ? TEXT("") : TEXT("|"), *Choice.ToString(), Weapons->GetAmmoReserve().Count(Choice));
		}
	}
	if (Weapons->GetHeldSlot() != INDEX_NONE)
	{
		Row.BarrelHeat = Weapons->GetBarrelHeat(Weapons->GetHeldSlot());
		Row.HeatHaze = Weapons->GetHeatHazeAlpha(Weapons->GetHeldSlot());
	}
	if (const UPoseableMeshComponent* Second = Character->GetWeaponSlotComponent(1); Second && Second->GetSkinnedAsset())
	{
		Row.SecondWeaponParent = Second->GetAttachSocketName().ToString();
		Row.SecondWeapon = Second->GetComponentLocation();
	}
	for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Row.MechFeed); ++Slot)
	{
		if (const FGinkgoMechanismState* State = Weapons->GetMechanismState(Slot))
		{
			Row.MechFeed[Slot] = State->Feed.Num();
			Row.MechChamber[Slot] = static_cast<int32>(State->Chamber);
			Row.MechAction[Slot] = static_cast<int32>(State->Action);
		}
	}
	const UGinkgoWeaponDefinition* First = Weapons->GetCarried(0);
	Row.Reserve = First ? Weapons->GetAmmoReserve().Count(First->AmmoType) : 0;
	Row.FireRecovery = Weapons->GetFireRecovery();
	Row.Spread = Weapons->GetHoldState().Spread;
	Row.SpreadBloom = Weapons->GetSpreadInput().Bloom;
	Row.bTriggerHeld = Weapons->IsTriggerHeld();
	Row.RecoilIndex = Weapons->GetRecoilState().ShotIndex;
	Row.RecoilAccumulated = Weapons->GetRecoilState().Accumulated;
	Row.RecoilApplied = Weapons->GetRecoilState().Applied;
	if (const UGinkgoWeaponDefinition* SpreadWeapon = Weapons->GetHeldWeapon())
	{
		Row.SpreadSpeedFactor = SpreadWeapon->Spread.SpeedFactor.GetRichCurveConst()->Eval(Weapons->GetSpreadInput().Speed, 1.f);
		Row.SpreadPosture = Ginkgo::Weapon::GetSpreadPostureFactor(SpreadWeapon->Spread, Weapons->GetSpreadInput());
	}
	if (const UGinkgoWeaponDefinition* Pistol = Weapons->GetCarried(Ginkgo::Weapon::SidearmSlot))
	{
		const UPoseableMeshComponent* Gun = Character->GetWeaponSlotComponent(Ginkgo::Weapon::SidearmSlot);
		Row.SidearmReserve = Weapons->GetAmmoReserve().Count(Pistol->AmmoType);
		if (const FGinkgoMechanismState* State = Weapons->GetMechanismState(Ginkgo::Weapon::SidearmSlot))
		{
			Row.SidearmMagazineIn = State->bMagazineIn ? 1 : 0;
		}
		if (Gun && Gun->GetSkinnedAsset())
		{
			Row.Sidearm = Gun->GetComponentLocation();
			Row.SidearmRotation = Gun->GetComponentQuat();
			Row.SidearmParent = Gun->GetAttachSocketName().ToString();
			Row.SidearmSlide = static_cast<float>(-Gun->GetComponentTransform().InverseTransformPosition(Gun->GetBoneLocation(Pistol->Bolt.CarrierBone)).X);
		}
	}
}

void FGinkgoInputReplay::FinishPlayback()
{
	TStringBuilder<16384> Builder;
	Builder.Append(TEXT("time,frame,location_x,location_y,location_z,velocity_x,velocity_y,velocity_z,speed_2d,mode,sliding,corrections,combined,sim_checks,sim_mismatches,sim_max_dt,")
		TEXT("windows,window_events,window_replayed,window_recombined,camera_x,camera_y,camera_z,camera_pitch,camera_yaw,camera_roll,camera_fov,")
		TEXT("camera_pivot_x,camera_pivot_y,camera_pivot_z,camera_pivot_pull,camera_required_compression,camera_compression,")
		TEXT("actor_yaw,control_yaw,sprint_facing,anim_vx,anim_vy,anim_speed,anim_vz,anim_facing_yaw,anim_accel_x,anim_accel_y,anim_correction,anim_has_intent,")
		TEXT("anim_stop_distance,anim_pivot_distance,anim_reset,anim_state,anim_gait,anim_direction,anim_clip,anim_clip_time,anim_play_rate,anim_stride,anim_warp,")
		TEXT("anim_root_yaw,foot_l_x,foot_l_y,foot_l_z,foot_r_x,foot_r_y,foot_r_z,ball_l_x,ball_l_y,ball_l_z,ball_r_x,ball_r_y,ball_r_z,contact_l,contact_r,turn_remaining,turn_weight,")
		TEXT("action,action_time,action_log,weapon_parent,weapon_x,weapon_y,weapon_z,hand_l_x,hand_l_y,hand_l_z,hand_r_x,hand_r_y,hand_r_z,upper_body_weight,")
		TEXT("interaction_target,interaction_state,interaction_distance,interaction_angle,interaction_pending,interaction_result,interaction_result_time,weapon_slots,")
		TEXT("anim_tti,anim_land_impact,anim_lean,anim_lean_sprint,legik_alpha,")
		TEXT("legik_lock_l,legik_planted_l,legik_corr_l,legik_land_in_l,legik_land_x_l,legik_land_y_l,legik_anim_ball_x_l,legik_anim_ball_y_l,")
		TEXT("legik_lock_r,legik_planted_r,legik_corr_r,legik_land_in_r,legik_land_x_r,legik_land_y_r,legik_anim_ball_x_r,legik_anim_ball_y_r,")
		TEXT("pelvis_z,ground_l_z,ground_r_z,legik_terrain_l,legik_vcorr_l,legik_terrain_r,legik_vcorr_r,legik_pelvis,legik_traces,")
		TEXT("thigh_l_x,thigh_l_y,thigh_l_z,calf_l_x,calf_l_y,calf_l_z,thigh_r_x,thigh_r_y,thigh_r_z,calf_r_x,calf_r_y,calf_r_z,")
		TEXT("legik_source_l,legik_raw_terrain_l,legik_anim_foot_l_x,legik_anim_foot_l_y,legik_anim_foot_l_z,legik_anim_knee_l_x,legik_anim_knee_l_y,legik_anim_knee_l_z,")
		TEXT("legik_target_l_x,legik_target_l_y,legik_target_l_z,")
		TEXT("legik_source_r,legik_raw_terrain_r,legik_anim_foot_r_x,legik_anim_foot_r_y,legik_anim_foot_r_z,legik_anim_knee_r_x,legik_anim_knee_r_y,legik_anim_knee_r_z,")
		TEXT("legik_target_r_x,legik_target_r_y,legik_target_r_z,legik_pelvis_line,legik_pelvis_upper,legik_pelvis_lower,legik_pelvis_adjust,legik_ledge_l,legik_ledge_r,")
		TEXT("hold_stance,hold_raise,hold_ads,aim_yaw,aim_pitch,weapon_qx,weapon_qy,weapon_qz,weapon_qw,mesh_x,mesh_y,mesh_z,")
		TEXT("aim_has_point,aim_point_x,aim_point_y,aim_point_z,crosshair_dist,aim_alpha,aim_res_before,aim_res_after,aim_spine,aim_gun,muzzle_x,muzzle_y,muzzle_z,")
		TEXT("bore_x,bore_y,bore_z,")
		TEXT("hand_target_l_x,hand_target_l_y,hand_target_l_z,hand_target_r_x,hand_target_r_y,hand_target_r_z,hand_alpha_l,hand_alpha_r,")
		TEXT("hold_sprint,loco_time,layer_run_alpha,layer_sprint_alpha,layer_run_time,layer_sprint_time,layer_aim_alpha,layer_aim_time,loop_clock"));
	for (const TCHAR* const Joint : UpperBodyJointNames)
	{
		Builder.Appendf(TEXT(",%s_x,%s_y,%s_z"), Joint, Joint, Joint);
	}
	Builder.Append(TEXT(",hud_aim_source,hud_aim_x,hud_aim_y,hud_aim_z,hud_screen_x,hud_screen_y,hud_ads,hud_hit,hud_hit_x,hud_hit_y,hud_hit_z,hud_hit_screen_x,hud_hit_screen_y,hip_lag_yaw,hip_lag_pitch,hip_omega,hip_zeta,hip_max_lag,hud_hit_gap,hud_hit_scale"));
	for (const TCHAR* const Side : { TEXT("l"), TEXT("r") })
	{
		Builder.Appendf(TEXT(",reach_weight_%s,reach_grab_%s,reach_place_%s,reach_item_%s_x,reach_item_%s_y,reach_item_%s_z,reach_pocket_%s_x,reach_pocket_%s_y,reach_pocket_%s_z,")
			TEXT("reach_held_%s_x,reach_held_%s_y,reach_held_%s_z,hand_item_%s,reach_fading_%s,reach_bearing_%s"), Side, Side, Side, Side, Side, Side, Side, Side, Side, Side, Side, Side,
			Side, Side, Side);
	}
	for (const TCHAR* const Joint : PoseRotationJointNames)
	{
		Builder.Appendf(TEXT(",%s_qx,%s_qy,%s_qz,%s_qw"), Joint, Joint, Joint, Joint);
	}
	Builder.Append(TEXT(",camera_arm,camera_shoulder_right,camera_shoulder_up,camera_pivot_up,camera_param_fov,camera_view_pitch,camera_view_yaw,camera_view_roll,")
		TEXT("camera_overlay_pitch,camera_overlay_yaw,camera_overlay_roll,camera_overlay_fov,camera_clock,camera_modifiers,camera_effects,camera_events,")
		TEXT("camera_context_tags,camera_context_scalars,camera_context_events,")
		TEXT("weapon_state,held_slot,target_slot,last_slot,aim_allowed,weapon2_parent,weapon2_x,weapon2_y,weapon2_z,")
		TEXT("mech_feed_1,mech_chamber_1,mech_action_1,mech_feed_2,mech_chamber_2,mech_action_2,reserve,occupancy,layer_upper_alpha,")
		TEXT("wants_fire,wants_reload,shots,shot_muzzle_x,shot_muzzle_y,shot_muzzle_z,shot_point_x,shot_point_y,shot_point_z,gun_kick_back,gun_kick_climb,bolt_travel,bolt_lift,bolt_knob_x,bolt_knob_y,bolt_knob_z,cue_fire,cue_bolt_lift,cue_bolt_pull,cue_bolt_push,cue_bolt_press,cue_round_in,cue_mag_out,cue_mag_in,cue_slide_home,muzzle_wisp,ejects,ejects_live,eject_x,eject_y,eject_z,eject_speed,feed_round,feed_round_x,feed_round_y,feed_round_z,shell_hits,shell_hits_hard,shell_hit_speed,shell_hit_volume,mech_feed_3,mech_chamber_3,mech_action_3,sidearm_mag_in,sidearm_parent,sidearm_x,sidearm_y,sidearm_z,sidearm_qx,sidearm_qy,sidearm_qz,sidearm_qw,sidearm_slide,sidearm_reserve,hud_dash,fire_recovery,left_hand_grip,gun_clip_weight,spread,spread_bloom,hud_spread_px,hud_reticle_open,shot_aimed_x,shot_aimed_y,shot_aimed_z,shot_spread,shot_deviation,spread_speed_factor,spread_posture,trigger_held,recoil_index,recoil_accum_yaw,recoil_accum_pitch,recoil_applied_yaw,recoil_applied_pitch,mech_feed_4,mech_chamber_4,mech_action_4,held_gun_x,held_gun_y,held_gun_z,held_gun_qx,held_gun_qy,held_gun_qz,held_gun_qw,pickup_settle,barrel_heat,heat_haze,hit_serial,hit_hits,hit_kill,hit_head,hit_armor,hit_armor_broken,hit_health,hit_armor_damage,hit_part,")
		TEXT("reload_pending,reload_held_time,wheel_open,wheel_selected,wheel_motion_x,wheel_motion_y,wheel_serial,wheel_result,wants_chamber_swap,")
		TEXT("load_ammo,chamber_ammo,feed_ammo,load_reserve,ammo_reserves,trails,trails_alive,trail_length,trail_points,trail_reveal,")
		TEXT("scope_fp,scope_eye_x,scope_eye_y,scope_eye_z,scope_axis_x,scope_axis_y,scope_axis_z,scope_head_hidden,scope_capture,scope_sway_pitch,scope_sway_yaw,camera_near,")
		TEXT("scope_rest_eye_x,scope_rest_eye_y,scope_rest_eye_z,scope_offset_pitch,scope_offset_yaw,scope_offset_roll,scope_move_pitch,scope_move_yaw,scope_move_roll,")
		TEXT("scope_shift_x,scope_shift_y,scope_shift_z,scope_rest_axis_x,scope_rest_axis_y,scope_rest_axis_z,scope_recoil_pitch,scope_recoil_yaw,scope_recoil_roll"));
	Builder.Append(TEXT("\n"));
	for (const FTrajectoryRow& Row : Trajectory)
	{
		Builder.Appendf(TEXT("%.6f,%llu,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%s,%d,%d,%d,%d,%d,%.6f,%s,%d,%d,%d,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,")
			TEXT("%.4f,%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.4f,%d,%.3f,%.3f,%d,%s,%s,%s,%s,%.4f,%.4f,%.4f,%.3f,%.3f,")
			TEXT("%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.3f,%.4f,"),
			Row.Time, Row.Frame, Row.Location.X, Row.Location.Y, Row.Location.Z, Row.Velocity.X, Row.Velocity.Y, Row.Velocity.Z, Row.Velocity.Size2D(),
			*MovementModeName(Row.MovementMode), Row.bSliding ? 1 : 0, Row.Corrections, Row.Combined, Row.SimChecks, Row.SimMismatches, Row.SimMaxTimeDifference,
			*Row.Windows, Row.WindowEvents, Row.WindowReplayed, Row.WindowRecombined, Row.CameraLocation.X, Row.CameraLocation.Y, Row.CameraLocation.Z,
			Row.CameraRotation.Pitch, Row.CameraRotation.Yaw, Row.CameraRotation.Roll, Row.CameraFOV, Row.CameraPivot.X, Row.CameraPivot.Y, Row.CameraPivot.Z,
			Row.CameraPivotPull, Row.CameraRequiredCompression, Row.CameraCompression, Row.ActorYaw, Row.ControlYaw, Row.bSprintFacing ? 1 : 0,
			Row.AnimVelocity.X, Row.AnimVelocity.Y, Row.AnimVelocity.Size2D(), Row.AnimVerticalSpeed, Row.AnimFacingYaw, Row.AnimAcceleration.X, Row.AnimAcceleration.Y,
			Row.AnimCorrection, Row.bAnimHasIntent ? 1 : 0, Row.AnimStopDistance, Row.AnimPivotDistance, Row.bAnimReset ? 1 : 0, *Row.AnimState, *Row.AnimGait,
			*Row.AnimDirection, *Row.AnimClip, Row.AnimClipTime, Row.AnimPlayRate, Row.AnimStride, Row.AnimWarp, Row.AnimRootYaw, Row.FootLeft.X, Row.FootLeft.Y,
			Row.FootLeft.Z, Row.FootRight.X, Row.FootRight.Y, Row.FootRight.Z, Row.BallLeft.X, Row.BallLeft.Y, Row.BallLeft.Z, Row.BallRight.X, Row.BallRight.Y,
			Row.BallRight.Z, Row.ContactLeft, Row.ContactRight, Row.TurnRemaining, Row.TurnWeight);
		Builder.Appendf(TEXT("%s,%.4f,%s,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,"), *Row.Action, Row.ActionTime, *Row.ActionLog, *Row.WeaponParent,
			Row.Weapon.X, Row.Weapon.Y, Row.Weapon.Z, Row.HandLeft.X, Row.HandLeft.Y, Row.HandLeft.Z, Row.HandRight.X, Row.HandRight.Y, Row.HandRight.Z,
			Row.UpperBodyWeight);
		Builder.Appendf(TEXT("%s,%s,%.3f,%.3f,%s,%s,%.6f,%s,%.4f,%.4f,%.4f,%.4f,%.4f"), *Row.InteractionTarget, *Row.InteractionState, Row.InteractionDistance,
			Row.InteractionAngle, *Row.InteractionPending, *Row.InteractionResult, Row.InteractionResultTime, *Row.WeaponSlots, Row.AnimTimeToImpact,
			Row.AnimLandImpact, Row.AnimLean, Row.AnimLeanSprint, Row.LegIKAlpha);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			Builder.Appendf(TEXT(",%d,%d,%.3f,%.4f,%.3f,%.3f,%.3f,%.3f"), Row.LegIKLock[Side], Row.bLegIKPlanted[Side] ? 1 : 0, Row.LegIKCorrection[Side],
				Row.LegIKLandIn[Side], Row.LegIKLanding[Side].X, Row.LegIKLanding[Side].Y, Row.LegIKAnimBall[Side].X, Row.LegIKAnimBall[Side].Y);
		}
		// 碰不到地面时写 nan（printf 在 Windows 上写成 -nan(ind)，Python 读不了）
		const auto Ground = [](const double Z) { return FMath::IsNaN(Z) ? FString(TEXT("nan")) : FString::Printf(TEXT("%.3f"), Z); };
		Builder.Appendf(TEXT(",%.3f,%s,%s,%s,%.3f,%s,%.3f,%.3f,%d"), Row.PelvisZ, *Ground(Row.GroundUnderBall[0]), *Ground(Row.GroundUnderBall[1]),
			*Ground(Row.LegIKTerrain[0]), Row.LegIKVerticalCorrection[0], *Ground(Row.LegIKTerrain[1]), Row.LegIKVerticalCorrection[1], Row.LegIKPelvisOffset,
			Row.LegIKTraces);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			Builder.Appendf(TEXT(",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"), Row.Thigh[Side].X, Row.Thigh[Side].Y, Row.Thigh[Side].Z, Row.Calf[Side].X, Row.Calf[Side].Y,
				Row.Calf[Side].Z);
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector& AnimFoot = Row.LegIKAnimFoot[Side];
			const FVector& AnimKnee = Row.LegIKAnimKnee[Side];
			const FVector& Target = Row.LegIKFootTarget[Side];
			Builder.Appendf(TEXT(",%d,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"), Row.LegIKTerrainSource[Side], *Ground(Row.LegIKRawTerrain[Side]), AnimFoot.X,
				AnimFoot.Y, AnimFoot.Z, AnimKnee.X, AnimKnee.Y, AnimKnee.Z, Target.X, Target.Y, Target.Z);
		}
		Builder.Appendf(TEXT(",%.3f,%.3f,%.3f,%.3f,%d,%d"), Row.LegIKPelvisLine, Row.LegIKPelvisUpper, Row.LegIKPelvisLower, Row.LegIKPelvisAdjust,
			Row.bLegIKLedge[0] ? 1 : 0, Row.bLegIKLedge[1] ? 1 : 0);
		Builder.Appendf(TEXT(",%d,%.4f,%.4f,%.3f,%.3f,%.6f,%.6f,%.6f,%.6f,%.3f,%.3f,%.3f"), Row.HoldStance, Row.HoldRaise, Row.HoldAds, Row.AimYaw, Row.AimPitch,
			Row.WeaponRotation.X, Row.WeaponRotation.Y, Row.WeaponRotation.Z, Row.WeaponRotation.W, Row.MeshLocation.X, Row.MeshLocation.Y, Row.MeshLocation.Z);
		Builder.Appendf(TEXT(",%d,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.6f,%.6f,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f"),
			Row.bHasAimPoint ? 1 : 0, Row.AimPoint.X, Row.AimPoint.Y, Row.AimPoint.Z, Row.CrosshairDistance, Row.AimAlpha, Row.AimResidualBefore, Row.AimResidualAfter,
			Row.AimSpineAngle, Row.AimGunAngle, Row.Muzzle.X, Row.Muzzle.Y,
			Row.Muzzle.Z, Row.Bore.X, Row.Bore.Y, Row.Bore.Z, Row.HandTarget[0].X, Row.HandTarget[0].Y, Row.HandTarget[0].Z, Row.HandTarget[1].X, Row.HandTarget[1].Y,
			Row.HandTarget[1].Z, Row.HandAlpha[0], Row.HandAlpha[1]);
		// 持枪循环的时刻要逐帧比同一个时刻：写到 µs
		Builder.Appendf(TEXT(",%.4f,%.6f,%.4f,%.4f,%.6f,%.6f,%.4f,%.6f,%.6f"), Row.HoldSprint, Row.LocomotionTime, Row.LayerRunAlpha, Row.LayerSprintAlpha, Row.LayerRunTime,
			Row.LayerSprintTime, Row.LayerAimAlpha, Row.LayerAimTime, Row.LoopClock);
		for (const FVector& Joint : Row.UpperBody)
		{
			Builder.Appendf(TEXT(",%.3f,%.3f,%.3f"), Joint.X, Joint.Y, Joint.Z);
		}
		Builder.Appendf(TEXT(",%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f"), Row.HudAimSource, Row.HudAimPoint.X, Row.HudAimPoint.Y, Row.HudAimPoint.Z, Row.HudAimScreen.X,
			Row.HudAimScreen.Y, Row.HudAdsAlpha);
		Builder.Appendf(TEXT(",%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.5f,%.5f,%.3f,%.3f,%.3f,%.3f,%.4f"), Row.bHudHit ? 1 : 0, Row.HudHitPoint.X, Row.HudHitPoint.Y, Row.HudHitPoint.Z, Row.HudHitScreen.X, Row.HudHitScreen.Y,
			Row.HipAimLag.X, Row.HipAimLag.Y, Row.HipHandling.X, Row.HipHandling.Y, Row.HipHandling.Z, Row.HudHitGap, Row.HudHitScale);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector& Item = Row.ReachItem[Side];
			const FVector& Pocket = Row.ReachPocket[Side];
			const FVector& Held = Row.ReachHeld[Side];
			Builder.Appendf(TEXT(",%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%.4f"), Row.ReachWeight[Side], Row.ReachGrab[Side], Row.ReachPlace[Side],
				Item.X, Item.Y, Item.Z, Pocket.X, Pocket.Y, Pocket.Z, Held.X, Held.Y, Held.Z, Row.bHandItem[Side] ? 1 : 0, Row.ReachFading[Side]);
			Builder.Appendf(TEXT(",%.4f"), Row.ReachBearing[Side]);
		}
		for (const FQuat& Rotation : Row.PoseRotation)
		{
			Builder.Appendf(TEXT(",%.6f,%.6f,%.6f,%.6f"), Rotation.X, Rotation.Y, Rotation.Z, Rotation.W);
		}
		// 相机的两层：叠加的旋转、视场与时钟写到 µ 度、µs，模型逐帧对得上
		Builder.Appendf(TEXT(",%.4f,%.4f,%.4f,%.4f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%s,%s,%s,%s,%s,%d"), Row.CameraArm, Row.CameraShoulderRight,
			Row.CameraShoulderUp, Row.CameraPivotUp, Row.CameraParamFov, Row.CameraView.Pitch, Row.CameraView.Yaw, Row.CameraView.Roll, Row.CameraOverlay.Pitch,
			Row.CameraOverlay.Yaw, Row.CameraOverlay.Roll, Row.CameraOverlayFov, Row.CameraClock, *Row.CameraModifiers, *Row.CameraEffects, *Row.CameraEvents,
			*Row.CameraContextTags, *Row.CameraContextScalars, Row.CameraContextEvents);
		Builder.Appendf(TEXT(",%s,%d,%d,%d,%d,%s,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%s,%.4f"), *Row.WeaponState, Row.HeldSlot, Row.TargetSlot, Row.LastSlot, Row.bAimAllowed ? 1 : 0,
			*Row.SecondWeaponParent, Row.SecondWeapon.X, Row.SecondWeapon.Y, Row.SecondWeapon.Z, Row.MechFeed[0], Row.MechChamber[0], Row.MechAction[0], Row.MechFeed[1],
			Row.MechChamber[1], Row.MechAction[1], Row.Reserve, *Row.Occupancy, Row.LayerUpperAlpha);
		Builder.Appendf(TEXT(",%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f"), Row.bWantsFire ? 1 : 0, Row.bWantsReload ? 1 : 0, Row.Shots, Row.ShotMuzzle.X, Row.ShotMuzzle.Y, Row.ShotMuzzle.Z,
			Row.ShotPoint.X, Row.ShotPoint.Y, Row.ShotPoint.Z, Row.GunKickBack, Row.GunKickClimb);
		Builder.Appendf(TEXT(",%.4f,%.4f,%.3f,%.3f,%.3f"), Row.BoltTravel, Row.BoltLift, Row.BoltKnob.X, Row.BoltKnob.Y, Row.BoltKnob.Z);
		Builder.Appendf(TEXT(",%d,%d,%d,%d,%d,%d,%d,%d,%d,%.4f"), Row.Cues[0], Row.Cues[1], Row.Cues[2], Row.Cues[3], Row.Cues[4], Row.Cues[5], Row.Cues[6], Row.Cues[7], Row.Cues[8], Row.MuzzleWisp);
		Builder.Appendf(TEXT(",%d,%d,%.3f,%.3f,%.3f,%.2f,%d,%.3f,%.3f,%.3f"), Row.Ejects, Row.EjectsLive, Row.EjectPoint.X, Row.EjectPoint.Y, Row.EjectPoint.Z, Row.EjectSpeed,
			Row.bFeedRound ? 1 : 0, Row.FeedRoundPoint.X, Row.FeedRoundPoint.Y, Row.FeedRoundPoint.Z);
		Builder.Appendf(TEXT(",%d,%d,%.1f,%.3f"), Row.ShellHits, Row.ShellHitsHard, Row.ShellHitSpeed, Row.ShellHitVolume);
		Builder.Appendf(TEXT(",%d,%d,%d,%d,%s,%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%.4f,%d,%.3f,%.4f,%.4f,%.4f"), Row.MechFeed[2], Row.MechChamber[2], Row.MechAction[2], Row.SidearmMagazineIn, *Row.SidearmParent,
			Row.Sidearm.X, Row.Sidearm.Y, Row.Sidearm.Z, Row.SidearmRotation.X, Row.SidearmRotation.Y, Row.SidearmRotation.Z, Row.SidearmRotation.W, Row.SidearmSlide, Row.SidearmReserve,
			Row.HudDash, Row.FireRecovery, Row.LeftHandGrip, Row.GunClipWeight);
		Builder.Appendf(TEXT(",%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f"), Row.Spread, Row.SpreadBloom, Row.HudSpreadPixels, Row.HudReticleOpen, Row.ShotAimed.X, Row.ShotAimed.Y, Row.ShotAimed.Z, Row.ShotSpread, Row.ShotDeviation,
			Row.SpreadSpeedFactor, Row.SpreadPosture);
		Builder.Appendf(TEXT(",%d,%.3f,%.4f,%.4f,%.4f,%.4f"), Row.bTriggerHeld ? 1 : 0, Row.RecoilIndex, Row.RecoilAccumulated.X, Row.RecoilAccumulated.Y, Row.RecoilApplied.X, Row.RecoilApplied.Y);
		// 重武器的槽（MG3）：加在最后，前面的列不挪
		Builder.Appendf(TEXT(",%d,%d,%d"), Row.MechFeed[3], Row.MechChamber[3], Row.MechAction[3]);
		// 手上那把枪与捡枪的收拢
		Builder.Appendf(TEXT(",%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f,%.4f"), Row.HeldGun.X, Row.HeldGun.Y, Row.HeldGun.Z, Row.HeldGunRotation.X, Row.HeldGunRotation.Y,
			Row.HeldGunRotation.Z, Row.HeldGunRotation.W, Row.PickupSettle);
		// 枪管的热
		Builder.Appendf(TEXT(",%.3f,%.4f"), Row.BarrelHeat, Row.HeatHaze);
		// 命中：部位按 EGinkgoBodyPart 的序号（0 头、1 上胸、2 腹、3 上肢、4 腿、5 脚）
		const FGinkgoHitConfirm& Confirm = Row.HitConfirm;
		Builder.Appendf(TEXT(",%d,%d,%d,%d,%d,%d,%.3f,%.3f,%d"), Confirm.Serial, Confirm.Hits, Confirm.bKill ? 1 : 0, Confirm.bHead ? 1 : 0, Confirm.bArmor ? 1 : 0,
			Confirm.bArmorBroken ? 1 : 0, Confirm.HealthDamage, Confirm.ArmorDamage, static_cast<int32>(Confirm.Part));
		// 弹种：装填键、轮盘、装的是哪种（文字列：load_ammo、chamber_ammo、feed_ammo、ammo_reserves、wheel_result）
		Builder.Appendf(TEXT(",%d,%.4f,%d,%d,%.4f,%.4f,%d,%s,%d,%s,%s,%s,%d,%s"), Row.bReloadPending ? 1 : 0, Row.ReloadHeldTime, Row.bWheelOpen ? 1 : 0, Row.WheelSelected,
			Row.WheelMotion.X, Row.WheelMotion.Y, Row.WheelSerial, *Row.WheelResult, Row.bWantsChamberSwap ? 1 : 0, *Row.LoadAmmo, *Row.ChamberAmmo, *Row.FeedAmmo,
			Row.LoadReserve, *Row.AmmoReserves);
		// 弹道的烟
		Builder.Appendf(TEXT(",%d,%d,%.2f,%d,%.4f"), Row.Trails, Row.TrailsAlive, Row.TrailLength, Row.TrailPoints, Row.TrailReveal);
		// 第一人称开镜：出瞳、光轴要算对不对得上分划（毫弧度级），多留几位
		Builder.Appendf(TEXT(",%.6f,%.5f,%.5f,%.5f,%.8f,%.8f,%.8f,%d,%d,%.6f,%.6f,%.3f"), Row.ScopeFirstPerson, Row.ScopeEye.X, Row.ScopeEye.Y, Row.ScopeEye.Z,
			Row.ScopeAxis.X, Row.ScopeAxis.Y, Row.ScopeAxis.Z, Row.bScopeHeadHidden ? 1 : 0, Row.ScopeCapture, Row.ScopeSway.Pitch, Row.ScopeSway.Yaw, Row.CameraNearClip);
		Builder.Appendf(TEXT(",%.5f,%.5f,%.5f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.5f,%.5f,%.5f"), Row.ScopeRestEye.X, Row.ScopeRestEye.Y, Row.ScopeRestEye.Z,
			Row.ScopeOffset.Pitch, Row.ScopeOffset.Yaw, Row.ScopeOffset.Roll, Row.ScopeMoveSway.Pitch, Row.ScopeMoveSway.Yaw, Row.ScopeMoveSway.Roll,
			Row.ScopeMoveShift.X, Row.ScopeMoveShift.Y, Row.ScopeMoveShift.Z);
		Builder.Appendf(TEXT(",%.8f,%.8f,%.8f,%.6f,%.6f,%.6f"), Row.ScopeRestAxis.X, Row.ScopeRestAxis.Y, Row.ScopeRestAxis.Z,
			Row.ScopeRecoil.Pitch, Row.ScopeRecoil.Yaw, Row.ScopeRecoil.Roll);
		Builder.Append(TEXT("\n"));
	}
	const bool bSaved = SaveText(PlaybackOptions.OutputPath, Builder.ToString());
	const FTrajectoryRow& Last = Trajectory.Last();
	UE_LOG(LogGinkgoControl, Log, TEXT("[Replay] %s 回放结束：%d 帧，%.3f 秒，最终水平速度 %.1f u/s（%.2f m/s），收到服务器校正 %d 次，合并移动 %d 次，")
		TEXT("比对模拟状态 %d 次、不一致 %d 次、最大计时差 %.3f ms，窗口变化发给表现层 %d 次、重放时丢掉 %d 次，轨迹%s %s"), *PlaybackName, Trajectory.Num() - 1, Last.Time,
		Last.Velocity.Size2D() / Ginkgo::Movement::CentimetersPerSourceUnit, Last.Velocity.Size2D() / 100.f, Last.Corrections, Last.Combined, Last.SimChecks, Last.SimMismatches,
		Last.SimMaxTimeDifference * 1000.f, Last.WindowEvents, Last.WindowReplayed, bSaved ? TEXT("写到") : TEXT("写入失败："), *PlaybackOptions.OutputPath);
	PlaybackState = EPlaybackState::None;
	if (PlaybackOptions.bQuitWhenDone)
	{
		// 截图在下一次绘制视口时才存下来。同一帧里请求退出时，引擎卡在截带界面的图上，进程一直不退（帧慢时截图时刻与回放结束落进同一帧，
		// 2026-10-25 实测，带不带 weft 的界面都一样），所以等截图存下来再退
		if (FScreenshotRequest::IsScreenshotRequested())
		{
			bQuitPending = true;
		}
		else
		{
			FPlatformMisc::RequestExit(false, TEXT("GinkgoInputReplay"));
		}
	}
}

void FGinkgoInputReplay::StopPlayback()
{
	PlaybackState = EPlaybackState::None;
}

int32 FGinkgoInputReplay::GetCorrectionsSinceStart() const
{
	const AGinkgoCharacter* Character = GetCharacter();
	return Character ? Character->GetGinkgoMovement()->GetCorrectionCount() - CorrectionsAtStart : 0;
}

int32 FGinkgoInputReplay::GetCombinedSinceStart() const
{
	const AGinkgoCharacter* Character = GetCharacter();
	return Character ? Character->GetGinkgoMovement()->GetCombinedMoveCount() - CombinedAtStart : 0;
}

int32 FGinkgoInputReplay::GetSimChecksSinceStart() const
{
	const AGinkgoCharacter* Character = GetCharacter();
	return Character ? Character->GetGinkgoMovement()->GetSimStateCheckCount() - SimChecksAtStart : 0;
}

int32 FGinkgoInputReplay::GetSimMismatchesSinceStart() const
{
	const AGinkgoCharacter* Character = GetCharacter();
	return Character ? Character->GetGinkgoMovement()->GetSimStateMismatchCount() - SimMismatchesAtStart : 0;
}

AGinkgoCharacter* FGinkgoInputReplay::GetCharacter() const
{
	return Controller.IsValid() ? Cast<AGinkgoCharacter>(Controller->GetPawn()) : nullptr;
}

double FGinkgoInputReplay::GetTime() const
{
	const UWorld* World = Controller.IsValid() ? Controller->GetWorld() : nullptr;
	return World ? World->GetTimeSeconds() : 0.0;
}

// ===== 控制台 =====

namespace
{
	FGinkgoInputReplay* FindReplay(const UWorld* World)
	{
		AGinkgoPlayerController* PlayerController = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!PlayerController)
		{
			UE_LOG(LogGinkgoControl, Warning, TEXT("[Replay] 找不到本地的 GinkgoPlayerController"));
			return nullptr;
		}
		return &PlayerController->GetOrCreateInputReplay();
	}

	FAutoConsoleCommandWithWorldAndArgs CCmdRecord(
		TEXT("ginkgo.record"),
		TEXT("开始录制输入：ginkgo.record <名字>。\n")
		TEXT("在这一帧所有 Actor 更新之后开始，开始状态取那时角色的位置、速度、视角与移动模式；ginkgo.record.stop 停止并写到 Saved/Ginkgo/Recordings/<名字>.csv。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (FGinkgoInputReplay* Replay = FindReplay(World))
			{
				Replay->StartRecording(Args.IsEmpty() ? FString(TEXT("recording")) : Args[0]);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdRecordStop(
		TEXT("ginkgo.record.stop"),
		TEXT("停止录制输入并写成 CSV。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (FGinkgoInputReplay* Replay = FindReplay(World))
			{
				Replay->StopRecording();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdReplay(
		TEXT("ginkgo.replay"),
		TEXT("回放输入录制：ginkgo.replay <名字或路径> [out=<轨迹路径>] [quit] [noreset] [delay=<秒>] [shot=<秒>]。\n")
		TEXT("先把角色放回录制的开始状态，再按回放自己的时刻取输入，所以能在任意帧率下回放；每帧所有 Actor 更新之后记下轨迹与服务器校正次数，结束时写成 CSV。\n")
		TEXT("quit：结束后退出程序，配合 -game -nullrhi -benchmark -fps=N 做无界面测量；noreset：不放回开始状态（联网时用）；delay：角色就绪后再等几秒才开始；")
		TEXT("shot：回放到这一刻时截一张带调试绘制的图，存在轨迹旁边（要渲染，不能 -nullrhi）；几个时刻用加号隔开（shot=0.8+1.0+1.6），存成 <名字>_0.png、_1.png……"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.IsEmpty())
			{
				UE_LOG(LogGinkgoControl, Warning, TEXT("[Replay] 用法：ginkgo.replay <名字或路径> [out=<轨迹路径>] [quit] [noreset] [delay=<秒>] [shot=<秒>]"));
				return;
			}
			FGinkgoReplayOptions Options;
			for (int32 Index = 1; Index < Args.Num(); ++Index)
			{
				if (Args[Index] == TEXT("quit"))
				{
					Options.bQuitWhenDone = true;
				}
				else if (Args[Index] == TEXT("noreset"))
				{
					Options.bNoReset = true;
				}
				else if (Args[Index].StartsWith(TEXT("out=")))
				{
					Options.OutputPath = Args[Index].RightChop(4);
				}
				else if (Args[Index].StartsWith(TEXT("delay=")))
				{
					Options.Delay = FCString::Atod(*Args[Index].RightChop(6));
				}
				else if (Args[Index].StartsWith(TEXT("shot=")))
				{
					Options.ShotTimes = FGinkgoReplayOptions::ParseShotTimes(Args[Index].RightChop(5));
				}
			}
			if (FGinkgoInputReplay* Replay = FindReplay(World))
			{
				FString Error;
				if (!Replay->RequestPlayback(Args[0], Options, Error))
				{
					UE_LOG(LogGinkgoControl, Error, TEXT("[Replay] %s"), *Error);
					if (Options.bQuitWhenDone)
					{
						FPlatformMisc::RequestExit(false, TEXT("GinkgoInputReplay"));
					}
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CCmdReplayStop(
		TEXT("ginkgo.replay.stop"),
		TEXT("停止回放，交还实时输入（不写轨迹）。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (FGinkgoInputReplay* Replay = FindReplay(World))
			{
				Replay->StopPlayback();
			}
		}));
}

#endif // !UE_BUILD_SHIPPING

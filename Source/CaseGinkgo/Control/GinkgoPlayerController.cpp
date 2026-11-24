#include "GinkgoPlayerController.h"
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Settings/GinkgoUpscaling.h"

#include "Algo/IndexOf.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Camera/GinkgoPlayerCameraComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "UI/GinkgoLocationTitle.h"
#include "UI/GinkgoGameplayHudLayer.h"
#include "UI/GinkgoCreditsHud.h"
#include "UI/GinkgoSubtitleHud.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"           // 开火键按没按着（连发）
#include "Engine/LocalPlayer.h"
#include "GinkgoInputModifiers.h"
#include "GinkgoInputSettingsSubsystem.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Interaction/GinkgoInteractorComponent.h"
#include "Interaction/GinkgoWorkbench.h"
#include "Movement/Traversal/GinkgoTraversalComponent.h"
#include "PlayerMappableKeySettings.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "UI/GinkgoInteractionPrompt.h"
#include "UI/GinkgoMainMenu.h"
#include "UI/GinkgoPauseMenu.h"
#include "UI/GinkgoSettingsPage.h"
#include "UI/GinkgoTitleSubsystem.h"
#include "UI/GinkgoUiSounds.h"
#include "UI/GinkgoToastHud.h"
#include "UI/GinkgoWeaponHud.h"
#include "UI/GinkgoWindowTimeline.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/InputReplay/GinkgoInputReplay.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Framework/GinkgoDeveloperSettings.h"       // 辅助瞄准的配置

namespace
{
#if !UE_BUILD_SHIPPING
	float GAimAssistScale = 1.f;
	FAutoConsoleVariableRef CVarAimAssistScale(TEXT("ginkgo.aimAssist.scale"), GAimAssistScale,
		TEXT("辅助瞄准的强度乘这个（调试、测量用，Docs/Design/Control.md「辅助瞄准」）：1 照玩家的设置（默认），0 关掉。发行版不编译"));
#endif

	/** 打开设置界面的键（手柄的菜单键另外判断打开时的目录） */
	const FKey& SettingsPadKey()
	{
		return EKeys::Gamepad_Special_Right;
	}

	bool IsSettingsKey(const FKey& Key)
	{
		return Key == SettingsPadKey() || Key == EKeys::Escape || Key == FGinkgoSettingsPage::GetToggleKey();
	}

	/**
	 * 把一条映射变成可改键的：有了映射名，重建映射时键就从 Enhanced Input 的改键系统来。这两个成员受保护，本来是在映射资产的编辑器里填的，而这里的映射是代码建的，
	 * 引擎没有给代码用的接口，所以从派生类取成员指针（读不到时编译不过，不像反射那样到运行时才错）
	 */
	struct FMappableKeyMapping : FEnhancedActionKeyMapping
	{
		static void SetMappingName(FEnhancedActionKeyMapping& Mapping, UObject* Outer, const FName Name)
		{
			UPlayerMappableKeySettings* Settings = NewObject<UPlayerMappableKeySettings>(Outer);
			Settings->Name = Name;
			Mapping.*(&FMappableKeyMapping::SettingBehavior) = EPlayerMappableKeySettingBehaviors::OverrideSettings;
			Mapping.*(&FMappableKeyMapping::PlayerMappableKeySettings) = Settings;
		}
	};

	/** 武器的键：改键的动作与意图里的那一位 */
	const TPair<EGinkgoKeyAction, EGinkgoWeaponKey> WeaponKeyDefs[] = {
		{EGinkgoKeyAction::Slot1, EGinkgoWeaponKey::Slot1},
		{EGinkgoKeyAction::Slot2, EGinkgoWeaponKey::Slot2},
		{EGinkgoKeyAction::Sidearm, EGinkgoWeaponKey::Sidearm},
		{EGinkgoKeyAction::Holster, EGinkgoWeaponKey::Holster},
		{EGinkgoKeyAction::SwitchWeapon, EGinkgoWeaponKey::Switch},
		{EGinkgoKeyAction::Fire, EGinkgoWeaponKey::Fire},
		{EGinkgoKeyAction::Reload, EGinkgoWeaponKey::Reload},
		{EGinkgoKeyAction::DropWeapon, EGinkgoWeaponKey::Drop},
	};

	EGinkgoHoldAction HoldActionOf(const EGinkgoKeyAction Action)
	{
		return Action == EGinkgoKeyAction::Crouch ? EGinkgoHoldAction::Crouch : Action == EGinkgoKeyAction::Sprint ? EGinkgoHoldAction::Sprint : EGinkgoHoldAction::Aim;
	}

	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, const EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = ValueType;
		return Action;
	}

	template <typename TModifier>
	TModifier* AddModifier(FEnhancedActionKeyMapping& Mapping, UObject* Outer)
	{
		TModifier* Modifier = NewObject<TModifier>(Outer);
		Mapping.Modifiers.Add(Modifier);
		return Modifier;
	}
}

AGinkgoPlayerController::AGinkgoPlayerController()
{
	PlayerCameraManagerClass = AGinkgoPlayerCameraManager::StaticClass();
}

void AGinkgoPlayerController::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	FGinkgoInputReplay::HandleCommandLine(*this);
#endif
	if (IsLocalController() && FGinkgoWindowTimeline::IsRequestedOnCommandLine())
	{
		GetOrCreateWindowTimeline().Show();
	}
	// 交互的提示与武器的界面一直在（weft 不画时什么也不做）
	if (IsLocalController())
	{
		InteractionPrompt = MakeShared<FGinkgoInteractionPrompt>(*this);
		InteractionPrompt->Show();
		WeaponHud = MakeShared<FGinkgoWeaponHud>(*this);
		WeaponHud->Show();
		ToastHud = MakeShared<FGinkgoToastHud>(*this);
		ToastHud->Show();
		UGinkgoUiSoundSubsystem::Preload(this);
		UGinkgoUiSoundSubsystem::PreloadSound(this, FGinkgoCheckpointAnnouncement().Sound);
		FGinkgoMainMenu::PreloadAudio(this);
		if (UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(this))
		{
			bool bStartWithSplash = true;
			if (Title->ConsumeAutoShowDecision(GetWorld(), bStartWithSplash))
			{
				GetOrCreateMainMenu().Show(bStartWithSplash, false);
			}
		}
	}
}

void AGinkgoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetSlowMotionEnabled(false, false);
	if (EndPlayReason == EEndPlayReason::EndPlayInEditor && GetLocalPlayer() && IsPrimaryPlayer())
	{
		FGinkgoUpscaling::Apply(GetWorld(), TEXT("off"), TEXT("off"));
	}
	GetWorldTimerManager().ClearTimer(CheckpointRestartTimer);
	if (DeathSource.IsValid()) { DeathSource->OnDied.RemoveAll(this); }
	LocationTitle.Reset();
	CreditsHud.Reset();
	SubtitleHud.Reset();
#if !UE_BUILD_SHIPPING
	// 退出时还在录：写出来，不然这一段就没了
	if (InputReplay)
	{
		InputReplay->StopRecording();
	}
#endif
	// 控制器要等垃圾回收才析构；时间轴、提示、武器界面与设置界面的控件、订阅在这里就拿掉
	WindowTimeline.Reset();
	InteractionPrompt.Reset();
	WeaponHud.Reset();
	ToastHud.Reset();
	GameplayHudLayer.Reset();
	MainMenu.Reset();
	PauseMenu.Reset();
	SettingsPage.Reset();
	if (UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(this))
	{
		Settings->OnChanged.Remove(InputSettingsChangedHandle);
	}
	// 改键系统跟着本地玩家走，比控制器活得长：不拿掉的话它一直引用这份映射，映射的外层就是控制器
	const UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (UEnhancedInputUserSettings* UserSettings = Input ? Input->GetUserSettings() : nullptr; UserSettings && MappingContext)
	{
		UserSettings->UnregisterInputMappingContext(MappingContext);
	}
	Super::EndPlay(EndPlayReason);
}

FGinkgoWindowTimeline& AGinkgoPlayerController::GetOrCreateWindowTimeline()
{
	if (!WindowTimeline)
	{
		WindowTimeline = MakeShared<FGinkgoWindowTimeline>(*this);
	}
	return *WindowTimeline;
}

FGinkgoMainMenu& AGinkgoPlayerController::GetOrCreateMainMenu()
{
	if (!MainMenu)
	{
		MainMenu = MakeShared<FGinkgoMainMenu>(*this);
	}
	return *MainMenu;
}

bool AGinkgoPlayerController::IsMainMenuOpen() const
{
	return MainMenu && MainMenu->IsShown();
}

void AGinkgoPlayerController::ReturnToMainMenu()
{
	SetSlowMotionEnabled(false, false);
	if (PauseMenu && PauseMenu->IsShown())
	{
		PauseMenu->Hide();
	}
	if (SettingsPage && SettingsPage->IsShown())
	{
		SettingsPage->ClearOnClosedCallback();
		SettingsPage->Hide();
	}
	OnFlashCanceled();
	HoldActions.ReleaseAllHeld();
	if (UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(this))
	{
		Title->MarkReturnedFromGameplay();
	}
	GetOrCreateMainMenu().Show(false, ActiveDevice == EGinkgoInputDirectory::Gamepad);
}

FGinkgoPauseMenu& AGinkgoPlayerController::GetOrCreatePauseMenu()
{
	if (!PauseMenu)
	{
		PauseMenu = MakeShared<FGinkgoPauseMenu>(*this);
	}
	return *PauseMenu;
}

bool AGinkgoPlayerController::IsPauseMenuOpen() const
{
	return PauseMenu && PauseMenu->IsShown();
}

FGinkgoSettingsPage& AGinkgoPlayerController::GetOrCreateSettingsPage()
{
	if (!SettingsPage)
	{
		SettingsPage = MakeShared<FGinkgoSettingsPage>(*this);
	}
	return *SettingsPage;
}

bool AGinkgoPlayerController::IsSettingsPageOpen() const
{
	return SettingsPage && SettingsPage->IsShown();
}

bool AGinkgoPlayerController::IsMenuOpen() const
{
	return IsMainMenuOpen() || IsPauseMenuOpen() || IsSettingsPageOpen();
}

void AGinkgoPlayerController::HandleSettingsClosedFromPauseMenu()
{
	if (PauseMenu)
	{
		PauseMenu->OnSettingsClosed();
	}
}

void AGinkgoPlayerController::HandleSettingsClosedFromMainMenu()
{
	if (MainMenu)
	{
		MainMenu->OnSettingsClosed();
	}
}

bool AGinkgoPlayerController::IsGameplayHudSuppressed() const { return LocationTitle && LocationTitle->IsActive(); }

void AGinkgoPlayerController::ClientShowCredits_Implementation(const TArray<FGinkgoCredit>& Credits)
{
	if (!IsLocalController()) { return; }
	if (!CreditsHud) { CreditsHud = MakeShared<FGinkgoCreditsHud>(*this); }
	CreditsHud->Push(Credits);
}

void AGinkgoPlayerController::ClientHearDialogue_Implementation(uint32 SpeakerId, FVector_NetQuantize Origin, const FText& Speaker, const FGinkgoDialogueLine& Line)
{
	if (!IsLocalController()) { return; }
	if (Line.Voice) { UGameplayStatics::PlaySoundAtLocation(this, Line.Voice, Origin); }
	const auto* Settings = UGinkgoInputSettingsSubsystem::Get(this);
	if (Line.Text.IsEmpty() || !Settings || !Settings->GetCurrent().bSubtitles) { return; }
	if (!SubtitleHud) { SubtitleHud = MakeShared<FGinkgoSubtitleHud>(*this); }
	SubtitleHud->Push(SpeakerId, Speaker, Line);
}

void AGinkgoPlayerController::AddGameplayHudWidget(const TSharedRef<SWidget>& Widget, const int32 ZOrder)
{
	if (!GameplayHudLayer && GetWorld() && GetWorld()->GetGameViewport())
	{
		GameplayHudLayer = MakeShared<FGinkgoGameplayHudLayer>(*GetWorld()->GetGameViewport(), IsGameplayHudSuppressed() || IsPauseMenuOpen() || IsMainMenuOpen());
	}
	if (GameplayHudLayer) { GameplayHudLayer->Add(Widget, ZOrder); }
}

void AGinkgoPlayerController::RemoveGameplayHudWidget(const TSharedRef<SWidget>& Widget)
{
	if (GameplayHudLayer) { GameplayHudLayer->Remove(Widget); }
}

void AGinkgoPlayerController::RefreshGameplayHudVisibility()
{
	if (GameplayHudLayer) { GameplayHudLayer->SetSuppressed(IsGameplayHudSuppressed() || IsPauseMenuOpen() || IsMainMenuOpen()); }
}

void AGinkgoPlayerController::ClientAnnounceCheckpoint_Implementation(const FGinkgoCheckpointAnnouncement& Announcement)
{
	if (!IsLocalController()) { return; }
	if (!LocationTitle) { LocationTitle = MakeShared<FGinkgoLocationTitleHud>(*this); }
	LocationTitle->Push(Announcement);
}

void AGinkgoPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	GetWorldTimerManager().ClearTimer(CheckpointRestartTimer);
	if (DeathSource.IsValid()) { DeathSource->OnDied.RemoveAll(this); }
	DeathSource.Reset();
	if (auto* GinkgoPawn = Cast<AGinkgoCharacter>(InPawn))
	{
		DeathSource = GinkgoPawn->GetDamageable();
		DeathSource->OnDied.AddUObject(this, &ThisClass::HandlePlayerDied);
	}
}

void AGinkgoPlayerController::OnUnPossess()
{
	OnFlashCanceled();
	GetWorldTimerManager().ClearTimer(CheckpointRestartTimer);
	if (DeathSource.IsValid()) { DeathSource->OnDied.RemoveAll(this); }
	DeathSource.Reset();
	Super::OnUnPossess();
}

void AGinkgoPlayerController::HandlePlayerDied(UGinkgoDamageableComponent& Component, const FGinkgoDamageEvent&)
{
	const auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(this);
	if (HasAuthority() && Component.GetOwner() == GetPawn() && Checkpoints && Checkpoints->GetState().bHasCheckpoint)
	{
		GetWorldTimerManager().SetTimer(CheckpointRestartTimer, this, &ThisClass::RestartIfStillDead, 2.f, false);
	}
}

void AGinkgoPlayerController::RestartIfStillDead()
{
	const auto* Save = UGinkgoSaveGameSubsystem::Get(this);
	if (DeathSource.IsValid() && DeathSource->GetOwner() == GetPawn() && DeathSource->IsDead() && Save && !Save->IsLoadingSaveGame())
	{
		UE_LOG(LogGinkgoSave, Log, TEXT("[Checkpoint] 玩家 %s（%s）死亡后的等待结束，开始读取检查点"), *GetName(), *GetNameSafe(GetPawn()));
		RestartCheckpoint();
	}
}

void AGinkgoPlayerController::RestartCheckpoint()
{
	SetSlowMotionEnabled(false, false);
	if (PauseMenu && PauseMenu->IsShown())
	{
		PauseMenu->Hide();
	}
	if (UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(this))
	{
		Title->MarkEnteredGameplay();
		Title->RequestSkipTitleOnNextLoad();
	}
	if (auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(this); HasAuthority() && Checkpoints && !Checkpoints->RestartFromCheckpoint())
	{
		FGinkgoToast Toast;
		Toast.Title = NSLOCTEXT("Checkpoint", "LoadFailed", "无法读取检查点");
		PushToast(Toast);
	}
}

void AGinkgoPlayerController::PushToast(const FGinkgoToast& Toast)
{
	if (ToastHud)
	{
		ToastHud->Push(Toast);
	}
}

#if !UE_BUILD_SHIPPING
FGinkgoInputReplay& AGinkgoPlayerController::GetOrCreateInputReplay()
{
	if (!InputReplay)
	{
		InputReplay = MakeShared<FGinkgoInputReplay>(*this);
	}
	return *InputReplay;
}
#endif

void AGinkgoPlayerController::PlayerTick(const float DeltaTime)
{
	const FGinkgoInputIntent* Input = &LiveInput;
#if !UE_BUILD_SHIPPING
	// 回放：视角在 Super 的 UpdateRotation 之前定好，角色随后据此转身；实时输入的处理函数在回放中什么都不改
	FGinkgoInputIntent ReplayInput;
	if (IsReplaying())
	{
		ReplayInput = InputReplay->TakePlaybackInput();
		// 录下来的视角不带后坐（录的时候扣掉了），回放时后坐照常算，加回去
		const AGinkgoCharacter* ReplayCharacter = GetGinkgoCharacter();
		const UGinkgoWeaponComponent* ReplayWeapons = ReplayCharacter ? ReplayCharacter->GetWeapons() : nullptr;
		SetControlRotation(ReplayInput.ControlRotation + (ReplayWeapons ? ReplayWeapons->GetRecoilOffset() : FRotator::ZeroRotator));
		Input = &ReplayInput;
	}
#endif

	Super::PlayerTick(DeltaTime);

	LiveInput.ControlRotation = GetControlRotation();
	if (!IsReplaying())
	{
		UpdateDirectionKeys();
	}
	HoldActions.Tick(!LiveInput.Move.IsNearlyZero(), DeltaTime);
	LiveInput.bCrouchHeld = HoldActions.IsOn(EGinkgoHoldAction::Crouch);
	LiveInput.bSprintHeld = HoldActions.IsOn(EGinkgoHoldAction::Sprint);
	LiveInput.bAimHeld = HoldActions.IsOn(EGinkgoHoldAction::Aim);
	LiveInput.bFireHeld = IsFireKeyHeld();
	// 弹种轮盘（Docs/Design/Weapons.md「切换弹种」）：装填键按着没有、视角那根摇杆。鼠标动的量在 OnWheelMouse 里攒
	LiveInput.bReloadHeld = IsWeaponKeyHeld(EGinkgoWeaponKey::Reload);
	{
		const UEnhancedPlayerInput* Enhanced = Cast<UEnhancedPlayerInput>(PlayerInput);
		LiveInput.WheelStick = Enhanced && WheelStickAction ? Enhanced->GetActionValue(WheelStickAction).Get<FVector2D>() : FVector2D::ZeroVector;
	}
#if !UE_BUILD_SHIPPING
	// 录的是这一帧的输入，视角扣掉上一帧为止的后坐：回放时后坐是算出来的。要在 ApplyInput 之前，这一帧的后坐还没加
	const bool bRecordingInput = InputReplay && InputReplay->IsRecording();
	FGinkgoInputIntent RecordedInput = LiveInput;
	if (bRecordingInput)
	{
		const AGinkgoCharacter* RecordingCharacter = GetGinkgoCharacter();
		const UGinkgoWeaponComponent* RecordingWeapons = RecordingCharacter ? RecordingCharacter->GetWeapons() : nullptr;
		RecordedInput.ControlRotation -= RecordingWeapons ? RecordingWeapons->GetRecoilOffset() : FRotator::ZeroRotator;
	}
#endif
	ApplyInput(*Input, DeltaTime);
#if !UE_BUILD_SHIPPING
	if (bRecordingInput)
	{
		InputReplay->RecordFrame(RecordedInput);
	}
#endif
	LiveInput.Move = FVector2D::ZeroVector;
	LiveInput.bJumpPressed = false;
	LiveInput.DirectionPresses = 0;
	LiveInput.bInteractPressed = false;
	LiveInput.bSwitchShoulderPressed = false;
	LiveInput.WeaponKeys = EGinkgoWeaponKey::None;
	LiveInput.WheelMouse = FVector2D::ZeroVector;
}

void AGinkgoPlayerController::UpdateRotation(const float DeltaTime)
{
	if (IsMainMenuOpen())
	{
		RotationInput = FRotator::ZeroRotator;
		AimAssist.Reset();
		Super::UpdateRotation(DeltaTime);
		return;
	}
	// 辅助瞄准（Docs/Design/Control.md「辅助瞄准」）：这一帧的视角输入加进 ControlRotation 之前修一下。回放时视角是录下的，已经带着修正
	if (!IsReplaying())
	{
		RotationInput += UpdateAimAssist(DeltaTime);
	}
	else
	{
		AimAssist.Reset();
	}
	Super::UpdateRotation(DeltaTime);
}

FRotator AGinkgoPlayerController::UpdateAimAssist(const float DeltaTime)
{
	const AGinkgoCharacter* GinkgoCharacter = GetGinkgoCharacter();
	const AGinkgoPlayerCameraManager* Camera = Cast<AGinkgoPlayerCameraManager>(PlayerCameraManager);
	// 相机还没更新过一次时没有叠加之前的视角
	if (!IsLocalController() || !GinkgoCharacter || GinkgoCharacter->GetDamageable()->IsDead() || GinkgoCharacter->GetWorkbench() || !Camera || Camera->GetAimView().Location.IsNearlyZero())
	{
		AimAssist.Reset();
		return FRotator::ZeroRotator;
	}
	const UGinkgoWeaponComponent* Weapons = GinkgoCharacter->GetWeapons();
	FGinkgoAimAssist::FInputs In;
	In.World = GetWorld();
	In.Self = GinkgoCharacter;
	// 越肩的相机绕着角色转：视角不同相机的位置也不同（视差），按视角推算
	In.PredictCamera = [Camera, GinkgoCharacter](const FRotator& View) { return Camera->PredictViewLocation(*GinkgoCharacter, View); };
	In.CameraLocation = Camera->GetAimView().Location;
	In.View = GetControlRotation();
	In.LookInput = RotationInput;
	In.bMoveInput = !LiveInput.Move.IsNearlyZero();
	In.Device = ActiveDevice;
	In.Config = &UGinkgoDeveloperSettings::Get()->AimAssist;
	// 这种设备的强度、范围、跟随目标是玩家在设置界面里给的（强度 0 是关）
	static const FGinkgoInputSettings DefaultSettings;
	const UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(this);
	In.Profile = Ginkgo::AimAssist::MakeProfile(Settings ? Settings->GetCurrent() : DefaultSettings, ActiveDevice);
	In.AdsAlpha = Weapons ? Weapons->GetHoldState().GetAdsAlpha() : 0.f;
	// 手上有枪才辅助；冲刺时枪放下来，跟着淡掉
	In.StrengthScale = Weapons && Weapons->GetHeldWeapon() ? 1.f - Weapons->GetHoldState().GetSprintAlpha() : 0.f;
#if !UE_BUILD_SHIPPING
	In.StrengthScale *= GAimAssistScale;
#endif
	return AimAssist.Update(In, DeltaTime);
}

bool AGinkgoPlayerController::IsReplaying() const
{
#if !UE_BUILD_SHIPPING
	return InputReplay && InputReplay->IsPlaying();
#else
	return false;
#endif
}

void AGinkgoPlayerController::ApplyInput(const FGinkgoInputIntent& Input, const float DeltaTime) const
{
	AGinkgoCharacter* GinkgoCharacter = GetGinkgoCharacter();
	if (!GinkgoCharacter || GinkgoCharacter->GetDamageable()->IsDead())
	{
		return;
	}
	UGinkgoWeaponComponent* Weapons = GinkgoCharacter->GetWeapons();
	if (IsMainMenuOpen())
	{
		if (Weapons)
		{
			Weapons->SetTriggerHeld(false);
			Weapons->SetWantsToAim(false);
			Weapons->UpdateHoldState(DeltaTime);
		}
		GinkgoCharacter->SetWantsToSprint(false);
		return;
	}
	if (GinkgoCharacter->GetWorkbench())
	{
		Weapons->SetTriggerHeld(false);
		Weapons->SetWantsToAim(false);
		Weapons->UpdateHoldState(DeltaTime);
		if (Input.bInteractPressed)
		{
			GinkgoCharacter->GetInteractor()->RequestInteract();
		}
		return;
	}
	if (Input.bSwitchShoulderPressed)
	{
		if (UGinkgoPlayerCameraComponent* Camera = GinkgoCharacter->FindComponentByClass<UGinkgoPlayerCameraComponent>())
		{
			Camera->ToggleShoulder();
		}
	}
	// 武器的键在冲刺之前：冲刺中按开火，这一帧就松开冲刺
	if (Input.bJumpPressed)
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		GetPlayerViewPoint(CameraLocation, CameraRotation);
#if !UE_BUILD_SHIPPING
		GinkgoCharacter->GetTraversal()->PreviewJump(CameraRotation);
#endif
		GinkgoCharacter->GetTraversal()->TryStart(CameraRotation, !Input.Move.IsNearlyZero());
	}
	if (GinkgoCharacter->GetTraversal()->IsActive() && !GinkgoCharacter->GetTraversal()->IsRecovering())
	{
		Weapons->CancelFireIntent();
		Weapons->SetWantsToAim(false);
		Weapons->UpdateHoldState(DeltaTime);
		GinkgoCharacter->SetWantsToSprint(false);
		return;
	}
	if (Weapons)
	{
		Weapons->SetTriggerHeld(Input.bFireHeld);
		Weapons->HandleWeaponKeys(Input.WeaponKeys);
		// 装填键的长按与弹种轮盘：在按键之后（开火、换枪先取消它），在持枪状态之前（确认了这一帧就装）
		Weapons->UpdateAmmoWheel(Input.bReloadHeld, Input.WheelMouse, Input.WheelStick, DeltaTime);
	}
	GinkgoCharacter->Move(Input.Move);
	if (Input.bJumpPressed)
	{
		GinkgoCharacter->Jump();
	}
	if (Input.DirectionPresses > 0)
	{
		GinkgoCharacter->AddDirectionPresses(Input.DirectionPresses);
	}
	GinkgoCharacter->SetDirectionKeys(Input.DirectionKeys);
	if (Input.bCrouchHeld)
	{
		GinkgoCharacter->Crouch();
	}
	else
	{
		GinkgoCharacter->UnCrouch();
	}
	GinkgoCharacter->SetWantsToSprint(Input.bSprintHeld && !(Weapons && Weapons->IsSprintBlocked()));
	// 交互的焦点是交互组件上一帧算的（它在相机更新之后更新），也就是这一帧屏幕上画着的那个
	if (Input.bInteractPressed)
	{
		if (UGinkgoInteractorComponent* Interactor = GinkgoCharacter->GetInteractor())
		{
			Interactor->RequestInteract();
		}
	}
	if (Weapons)
	{
		Weapons->SetWantsToAim(!GinkgoCharacter->GetWorkbench() && Input.bAimHeld);
		Weapons->UpdateHoldState(DeltaTime);
	}
}

FKey AGinkgoPlayerController::GetWorkbenchInstallKey() const
{
	const bool bPad = ActiveDevice == EGinkgoInputDirectory::Gamepad;
	static const FGinkgoInputSettings Defaults;
	const auto* Settings = UGinkgoInputSettingsSubsystem::Get(this);
	const auto& Keys = (Settings ? Settings->GetCurrent() : Defaults).Keys;
	const FKey Candidates[] = {bPad ? EKeys::Gamepad_FaceButton_Bottom : EKeys::F,
		bPad ? EKeys::Gamepad_FaceButton_Top : EKeys::R, bPad ? EKeys::Gamepad_FaceButton_Right : EKeys::V};
	for (FKey Candidate : Candidates)
	{
		bool bUsedForExit = false;
		for (auto Slot : {EGinkgoKeySlot::Primary, EGinkgoKeySlot::Secondary})
		{
			const int32 Index = Ginkgo::Input::KeyBindingIndex(ActiveDevice, EGinkgoKeyAction::Interact, Slot);
			bUsedForExit |= Keys.IsValidIndex(Index) && Keys[Index].Key == Candidate;
		}
		if (!bUsedForExit) { return Candidate; }
	}
	return EKeys::Invalid;
}

bool AGinkgoPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// 辅助瞄准按最近用的设备选配置：按了手柄的键、摇杆推过一点（不回正的抖动不算）是手柄；动了鼠标、按了键盘或鼠标的键是键鼠
	if (Params.Key.IsGamepadKey())
	{
		if (!Params.Key.IsAnalog() || FMath::Abs(Params.AmountDepressed) > 0.25f)
		{
			ActiveDevice = EGinkgoInputDirectory::Gamepad;
		}
	}
	else if (!Params.Key.IsAnalog() || Params.AmountDepressed != 0.f)
	{
		ActiveDevice = EGinkgoInputDirectory::KeyboardMouse;
	}

	// 桌前先处理菜单按键，避免选枪时触发丢枪或开火。
	auto* BenchCharacter = GetGinkgoCharacter();
	auto* Bench = BenchCharacter ? BenchCharacter->GetWorkbench() : nullptr;
	if (Bench && !IsMenuOpen())
	{
		if (Params.Key == GetWorkbenchInstallKey())
		{
			if (Params.Event == IE_Pressed)
			{
				if (!InteractionPrompt || !InteractionPrompt->NavigateWorkbench(TEXT("install")))
				{
					BenchCharacter->GetInteractor()->InstallWorkbenchAttachment(Bench->GetNextAttachment());
				}
			}
			return true;
		}
		// LB / RB 保留横向选枪；十字键左右换插槽、上下选择配件，焦点由同一页面维护。
		FName Navigation;
		if (Params.Key == EKeys::Gamepad_DPad_Left) { Navigation = TEXT("previousMount"); }
		if (Params.Key == EKeys::Gamepad_DPad_Right) { Navigation = TEXT("nextMount"); }
		if (Params.Key == EKeys::Gamepad_DPad_Up) { Navigation = TEXT("previousPart"); }
		if (Params.Key == EKeys::Gamepad_DPad_Down) { Navigation = TEXT("nextPart"); }
		if (!Navigation.IsNone() && InteractionPrompt && InteractionPrompt->IsShown())
		{
			if (Params.Event == IE_Pressed) { InteractionPrompt->NavigateWorkbench(Navigation); }
			return true;
		}
		const FKey Numbers[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four};
		for (int32 Slot = 0; Slot < UE_ARRAY_COUNT(Numbers); ++Slot)
		{
			if (Params.Key == Numbers[Slot])
			{
				if (Params.Event == IE_Pressed) { BenchCharacter->GetInteractor()->SelectWorkbenchSlot(Slot); }
				return true;
			}
		}
		const bool bNext = Params.Key == EKeys::Gamepad_RightShoulder || Params.Key == EKeys::Right || Params.Key == EKeys::Gamepad_DPad_Down;
		if (bNext || Params.Key == EKeys::Gamepad_LeftShoulder || Params.Key == EKeys::Left || Params.Key == EKeys::Gamepad_DPad_Up)
		{
			if (Params.Event == IE_Pressed && Bench->CanSelectWeapon())
			{
				const int32 Slot = Bench->GetAdjacentSlot(bNext);
				if (Slot != INDEX_NONE)
				{
					BenchCharacter->GetInteractor()->SelectWorkbenchSlot(Slot);
				}
			}
			return true;
		}
	}
	// 按住 Esc 关掉设置界面以后，系统接着发按键重复。按下是页面收的，游戏没见过；PlayerInput 看到没有按下的重复会补一个按下
	// （bAutoReconcilePressedEventsOnFirstRepeat，给换关卡时按着键的情况用的），界面就又打开了。别的键不管：按着 W 关界面，接着走是对的
	if (Params.Event == IE_Repeat && IsSettingsKey(Params.Key) && !IsInputKeyDown(Params.Key))
	{
		return false;
	}
	return Super::InputKey(Params);
}

void AGinkgoPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	RegisterKeyMappings();
	// 本地控制器已经拿到游戏视口，可按当前交换链恢复本机的渲染偏好。
	if (GetLocalPlayer() && IsPrimaryPlayer()) { FGinkgoUpscaling::ApplySaved(GetWorld()); }
}

void AGinkgoPlayerController::RegisterKeyMappings()
{
	const UEnhancedInputLocalPlayerSubsystem* Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	UEnhancedInputUserSettings* UserSettings = Input ? Input->GetUserSettings() : nullptr;
	const UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(this);
	if (UserSettings && Settings && MappingContext)
	{
		UserSettings->RegisterInputMappingContext(MappingContext);
		Settings->ApplyKeys();
	}
}

void AGinkgoPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateInputMappings();
	RegisterKeyMappings();

	// 这里本地玩家一定绑上了（SetPlayer 才调到这里）；联网的客户端 BeginPlay 时不一定
	if (UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(this); Settings && !InputSettingsChangedHandle.IsValid())
	{
		InputSettingsChangedHandle = Settings->OnChanged.AddUObject(this, &ThisClass::OnInputSettingsChanged);
	}

	UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(InputComponent);
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ThisClass::OnMove);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ThisClass::OnLook);
	Input->BindAction(WheelMouseAction, ETriggerEvent::Triggered, this, &ThisClass::OnWheelMouse);
	// 只在按下时起跳：定高跳不需要松开事件；滚轮的按下与松开可能落在同一帧，绑松开会把这次跳跃清掉
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ThisClass::OnJump);
	Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ThisClass::OnInteract);
	Input->BindAction(FlashAction, ETriggerEvent::Started, this, &ThisClass::OnFlashPressed);
	Input->BindAction(FlashAction, ETriggerEvent::Completed, this, &ThisClass::OnFlashReleased);
	Input->BindAction(FlashAction, ETriggerEvent::Canceled, this, &ThisClass::OnFlashCanceled);
	Input->BindAction(SwitchShoulderAction, ETriggerEvent::Started, this, &ThisClass::OnSwitchShoulder);
	for (int32 Index = 0; Index < WeaponKeyActions.Num(); ++Index)
	{
		Input->BindAction(WeaponKeyActions[Index], ETriggerEvent::Started, this, &ThisClass::OnWeaponKey, WeaponKeyDefs[Index].Value);
	}
	Input->BindAction(ToggleWindowTimelineAction, ETriggerEvent::Started, this, &ThisClass::OnToggleWindowTimeline);
	Input->BindAction(OpenSettingsAction, ETriggerEvent::Started, this, &ThisClass::OnOpenSettings);
	Input->BindAction(ToggleSlowMotionAction, ETriggerEvent::Started, this, &ThisClass::OnToggleSlowMotion);
	for (int32 Binding = 0; Binding < HoldInputActions.Num(); ++Binding)
	{
		if (const UInputAction* Action = HoldInputActions[Binding])
		{
			Input->BindAction(Action, ETriggerEvent::Started, this, &ThisClass::OnHoldStarted, Binding);
			Input->BindAction(Action, ETriggerEvent::Completed, this, &ThisClass::OnHoldCompleted, Binding);
		}
	}
}

void AGinkgoPlayerController::CreateInputMappings()
{
	MoveAction = MakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	WheelMouseAction = MakeAction(this, TEXT("IA_WheelMouse"), EInputActionValueType::Axis2D);
	WheelStickAction = MakeAction(this, TEXT("IA_WheelStick"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(this, TEXT("IA_Jump"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(this, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	FlashAction = MakeAction(this, TEXT("IA_Flashbang"), EInputActionValueType::Boolean);
	SwitchShoulderAction = MakeAction(this, TEXT("IA_SwitchShoulder"), EInputActionValueType::Boolean);
	ToggleWindowTimelineAction = MakeAction(this, TEXT("IA_ToggleWindowTimeline"), EInputActionValueType::Boolean);
	OpenSettingsAction = MakeAction(this, TEXT("IA_OpenSettings"), EInputActionValueType::Boolean);
	ToggleSlowMotionAction = MakeAction(this, TEXT("IA_ToggleSlowMotion"), EInputActionValueType::Boolean);
	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Default"));

	// MapKey 返回的引用在下一次 MapKey 后可能失效，每条映射加完修饰器再映射下一个键

	// 玩家能改的键：一个键位一条映射、一个映射名（一行），不用同一行的多个槽：重建映射时一行的键会给这一行的每一条映射，而蹲、冲刺、开镜要分得清是哪个键位按的，
	// 动作也不同。空的键位也有映射（键是 Invalid），玩家绑了键才有用
	const TArray<FGinkgoKeyBindingDef>& KeyDefs = Ginkgo::Input::GetKeyBindingDefs();
	const auto MapBinding = [this, &KeyDefs](const UInputAction* Action, const int32 Binding) -> FEnhancedActionKeyMapping&
	{
		FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, KeyDefs[Binding].DefaultKey);
		FMappableKeyMapping::SetMappingName(Mapping, this, KeyDefs[Binding].MappingName);
		return Mapping;
	};
	/** 一个动作的所有键位：键盘的主键、辅助键，再是手柄的（手柄没有方向键的四个动作） */
	const auto BindingsOf = [](const EGinkgoKeyAction Action)
	{
		TArray<int32, TInlineAllocator<4>> Bindings;
		for (const EGinkgoInputDirectory Directory : {EGinkgoInputDirectory::KeyboardMouse, EGinkgoInputDirectory::Gamepad})
		{
			for (const EGinkgoKeySlot Slot : {EGinkgoKeySlot::Primary, EGinkgoKeySlot::Secondary})
			{
				if (const int32 Binding = Ginkgo::Input::KeyBindingIndex(Directory, Action, Slot); Binding != INDEX_NONE)
				{
					Bindings.Add(Binding);
				}
			}
		}
		return Bindings;
	};

	// 移动：拼成 X 右、Y 前的 2D 向量。各个键的值相加，同一条轴上两个反方向的键一起按着就抵消（同 Source 与 Apex）。
	// 引擎默认按分量取绝对值最大的，一样大时映射在后面的赢：A 总压过 D、S 总压过 W，与按下的先后无关，左右不对称。
	// lurch 的方向另算，后按的赢（FGinkgoDirectionKeys）
	MoveAction->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
	for (int32 Index = 0; Index < FGinkgoDirectionKeys::Count; ++Index)
	{
		for (const int32 Binding : BindingsOf(static_cast<EGinkgoKeyAction>(Index)))
		{
			// 第 0、1 个是 X 轴（右、左），第 2、3 个是 Y 轴（前、后）；单个按键的值在 X 上，Y 轴的先换到 Y，负方向的取反
			FEnhancedActionKeyMapping& Mapping = MapBinding(MoveAction, Binding);
			if (Index >= 2)
			{
				AddModifier<UInputModifierSwizzleAxis>(Mapping, this);
			}
			if (Index % 2 == 1)
			{
				AddModifier<UInputModifierNegate>(Mapping, this);
			}
		}
	}
	// 摇杆与鼠标都走玩家的设置（GinkgoInputModifiers）。两种摇杆摆法的映射都在，修改器按设置放行一边：换摆法不用重建映射
	AddModifier<UGinkgoInputModifierStickMove>(MappingContext->MapKey(MoveAction, EKeys::Gamepad_Left2D), this);
	AddModifier<UGinkgoInputModifierStickMove>(MappingContext->MapKey(MoveAction, EKeys::Gamepad_Right2D), this)->bForSwappedSticks = true;

	// 视角：IA_Look 的值就是这一帧要转的度数，X 偏航、Y 俯仰（向上为正），OnLook 直接加到 RotationInput
	AddModifier<UGinkgoInputModifierMouseLook>(MappingContext->MapKey(LookAction, EKeys::Mouse2D), this);
	AddModifier<UGinkgoInputModifierStickLook>(MappingContext->MapKey(LookAction, EKeys::Gamepad_Right2D), this);
	AddModifier<UGinkgoInputModifierStickLook>(MappingContext->MapKey(LookAction, EKeys::Gamepad_Left2D), this)->bForSwappedSticks = true;

	// 弹种轮盘（Docs/Design/Weapons.md「切换弹种」）：同样的鼠标与视角摇杆，换算不同（不反转、不加速、不乘开镜：拨轮盘按手往哪推）。
	// 一直在读，轮盘开着时视角不吃它们（OnLook）
	AddModifier<UGinkgoInputModifierWheelMouse>(MappingContext->MapKey(WheelMouseAction, EKeys::Mouse2D), this);
	AddModifier<UGinkgoInputModifierWheelStick>(MappingContext->MapKey(WheelStickAction, EKeys::Gamepad_Right2D), this);
	AddModifier<UGinkgoInputModifierWheelStick>(MappingContext->MapKey(WheelStickAction, EKeys::Gamepad_Left2D), this)->bForSwappedSticks = true;

	for (const int32 Binding : BindingsOf(EGinkgoKeyAction::Jump))
	{
		MapBinding(JumpAction, Binding);
	}
	for (const int32 Binding : BindingsOf(EGinkgoKeyAction::Flashbang))
	{
		MapBinding(FlashAction, Binding);
	}
	for (const int32 Binding : BindingsOf(EGinkgoKeyAction::SwitchShoulder))
	{
		MapBinding(SwitchShoulderAction, Binding);
	}
	HoldInputActions.SetNum(KeyDefs.Num());
	for (const EGinkgoKeyAction Action : {EGinkgoKeyAction::Crouch, EGinkgoKeyAction::Sprint, EGinkgoKeyAction::Interact, EGinkgoKeyAction::Aim})
	{
		for (const int32 Binding : BindingsOf(Action))
		{
			if (Action == EGinkgoKeyAction::Interact)
			{
				MapBinding(InteractAction, Binding);
			}
			else
			{
				HoldInputActions[Binding] = MakeAction(this, *(TEXT("IA_") + KeyDefs[Binding].Id.Replace(TEXT("."), TEXT("_"))), EInputActionValueType::Boolean);
				MapBinding(HoldInputActions[Binding], Binding);
			}
		}
	}

	WeaponKeyActions.Reset();
	for (const TPair<EGinkgoKeyAction, EGinkgoWeaponKey>& Def : WeaponKeyDefs)
	{
		UInputAction* Action = MakeAction(this, *FString::Printf(TEXT("IA_WeaponKey%d"), static_cast<int32>(Def.Value)), EInputActionValueType::Boolean);
		WeaponKeyActions.Add(Action);
		for (const int32 Binding : BindingsOf(Def.Key))
		{
			MapBinding(Action, Binding);
		}
	}

	// 发行版没有控制台，窗口时间轴用这个键开关
	MappingContext->MapKey(ToggleWindowTimelineAction, FGinkgoWindowTimeline::GetToggleKey());

	// 设置界面
	MappingContext->MapKey(OpenSettingsAction, FGinkgoSettingsPage::GetToggleKey());
	MappingContext->MapKey(OpenSettingsAction, EKeys::Escape);
	MappingContext->MapKey(OpenSettingsAction, SettingsPadKey());

	// 固定慢动作快捷键（不进入可改键配置表）：按 P 切换 0.2 倍慢动作，再按一次恢复常速
	MappingContext->MapKey(ToggleSlowMotionAction, EKeys::P);

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, 0);
	}
}

AGinkgoCharacter* AGinkgoPlayerController::GetGinkgoCharacter() const
{
	return Cast<AGinkgoCharacter>(GetPawn());
}

void AGinkgoPlayerController::UpdateDirectionKeys()
{
	const UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(this);
	if (!Settings)
	{
		return;
	}
	// 不经过输入动作：IA_Move 把各个键合成一个向量，按住 W 再按 D 不会再触发一次 Started，也分不出谁先谁后
	bool bPressed[FGinkgoDirectionKeys::Count] = {};
	bool bHeld[FGinkgoDirectionKeys::Count] = {};
	uint8 Presses = 0;
	for (int32 Index = 0; Index < FGinkgoDirectionKeys::Count; ++Index)
	{
		for (const EGinkgoKeySlot Slot : {EGinkgoKeySlot::Primary, EGinkgoKeySlot::Secondary})
		{
			const FKey& Key = Settings->GetCurrent().Keys[Ginkgo::Input::KeyBindingIndex(EGinkgoInputDirectory::KeyboardMouse, static_cast<EGinkgoKeyAction>(Index), Slot)].Key;
			// 只看按下的事件：松开与按住时的自动重复都不算。这一帧按下又松开（一帧里点按完）的也当作按着
			const bool bKeyPressed = WasInputKeyJustPressed(Key);
			bPressed[Index] |= bKeyPressed;
			bHeld[Index] |= bKeyPressed || IsInputKeyDown(Key);
			Presses += bKeyPressed ? 1 : 0;
		}
	}
	DirectionKeyState.Update(bPressed, bHeld);
	LiveInput.DirectionPresses = Presses;
	LiveInput.DirectionKeys = DirectionKeyState.Resolve();
}

// ===== 实时输入 =====

void AGinkgoPlayerController::OnMove(const FInputActionValue& Value)
{
	if (!IsReplaying())
	{
		// 各个键的值相加：一个方向的主键与辅助键一起按着是 2，不能比一个键快，也不能把斜着走的方向带偏（移动组件只按长度夹）
		const FVector2D Move = Value.Get<FVector2D>();
		LiveInput.Move = FVector2D(FMath::Clamp(Move.X, -1.0, 1.0), FMath::Clamp(Move.Y, -1.0, 1.0));
	}
}

void AGinkgoPlayerController::OnLook(const FInputActionValue& Value)
{
	if (IsReplaying())
	{
		return;
	}
	// IA_Look 的值就是这一帧要转的度数（修改器算好的）。不走 AddYawInput、AddPitchInput：工程开着 legacy 输入缩放，它们会再乘 ±2.5。
	// 这个开关是全局的，引擎的调试相机靠它（关了俯仰反向、变慢），所以留着，这里绕开。
	// 弹种轮盘开着时鼠标、摇杆拨的是轮盘（IA_WheelMouse、IA_WheelStick），视角不转（上一帧开的：这一帧的输入处理在武器组件推进之前）
	const AGinkgoCharacter* GinkgoCharacter = GetGinkgoCharacter();
	const UGinkgoWeaponComponent* Weapons = GinkgoCharacter ? GinkgoCharacter->GetWeapons() : nullptr;
	if (!IsLookInputIgnored() && !(Weapons && Weapons->IsAmmoWheelOpen()))
	{
		const FVector2D Look = Value.Get<FVector2D>();
		RotationInput.Yaw += Look.X;
		RotationInput.Pitch += Look.Y;
	}
}

void AGinkgoPlayerController::OnJump(const FInputActionValue& Value)
{
	LiveInput.bJumpPressed = true;
	HoldActions.OnJump();
}

void AGinkgoPlayerController::OnInteract(const FInputActionValue& Value)
{
	LiveInput.bInteractPressed = true;
}

void AGinkgoPlayerController::OnFlashPressed()
{
	if (auto* PawnOwner = GetGinkgoCharacter(); PawnOwner && !IsMenuOpen())
		PawnOwner->GetFlashbang()->Press();
}

void AGinkgoPlayerController::OnFlashReleased()
{
	if (auto* PawnOwner = GetGinkgoCharacter()) PawnOwner->GetFlashbang()->Release();
}

void AGinkgoPlayerController::OnFlashCanceled()
{
	if (auto* PawnOwner = GetGinkgoCharacter()) PawnOwner->GetFlashbang()->CancelThrow();
}

void AGinkgoPlayerController::OnSwitchShoulder(const FInputActionValue& Value)
{
	LiveInput.bSwitchShoulderPressed = !LiveInput.bSwitchShoulderPressed;
}

void AGinkgoPlayerController::OnWeaponKey(const FInputActionValue& Value, const EGinkgoWeaponKey Key)
{
	LiveInput.WeaponKeys |= Key;
	const AGinkgoCharacter* GinkgoCharacter = GetGinkgoCharacter();
	const UGinkgoWeaponComponent* Weapons = GinkgoCharacter ? GinkgoCharacter->GetWeapons() : nullptr;
	if (Key == EGinkgoWeaponKey::Fire && Weapons && Weapons->GetHeldWeapon())
	{
		HoldActions.OnFire();
	}
}

bool AGinkgoPlayerController::IsFireKeyHeld() const
{
	return IsWeaponKeyHeld(EGinkgoWeaponKey::Fire);
}

bool AGinkgoPlayerController::IsWeaponKeyHeld(const EGinkgoWeaponKey Key) const
{
	const UEnhancedPlayerInput* Enhanced = Cast<UEnhancedPlayerInput>(PlayerInput);
	const int32 Index = Algo::IndexOfByPredicate(WeaponKeyDefs, [Key](const TPair<EGinkgoKeyAction, EGinkgoWeaponKey>& Def) { return Def.Value == Key; });
	return Enhanced && WeaponKeyActions.IsValidIndex(Index) && Enhanced->GetActionValue(WeaponKeyActions[Index]).Get<bool>();
}

void AGinkgoPlayerController::OnWheelMouse(const FInputActionValue& Value)
{
	if (!IsReplaying())
	{
		LiveInput.WheelMouse += Value.Get<FVector2D>();
	}
}

void AGinkgoPlayerController::OnHoldStarted(const FInputActionValue& Value, const int32 Binding)
{
	const FGinkgoKeyBindingDef& Def = Ginkgo::Input::GetKeyBindingDefs()[Binding];
	const UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(this);
	HoldActions.Press(HoldActionOf(Def.Action), Def.Directory, Def.Slot, Settings ? Settings->GetCurrent().Keys[Binding].Mode : EGinkgoHoldMode::Hold);
}

void AGinkgoPlayerController::OnHoldCompleted(const FInputActionValue& Value, const int32 Binding)
{
	const FGinkgoKeyBindingDef& Def = Ginkgo::Input::GetKeyBindingDefs()[Binding];
	HoldActions.Release(HoldActionOf(Def.Action), Def.Directory, Def.Slot);
}

void AGinkgoPlayerController::OnInputSettingsChanged()
{
	if (SubtitleHud) { SubtitleHud->Refresh(); }
	HoldActions.Reset();
	if (const UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(this))
	{
		Settings->ApplyKeys();
	}
}

void AGinkgoPlayerController::OnToggleWindowTimeline(const FInputActionValue& Value)
{
	GetOrCreateWindowTimeline().Toggle();
}

void AGinkgoPlayerController::OnOpenSettings(const FInputActionValue& Value)
{
	if (IsMenuOpen())
	{
		return;
	}
	OnFlashCanceled();
	HoldActions.ReleaseAllHeld();
	GetOrCreatePauseMenu().Show(WasInputKeyJustPressed(SettingsPadKey()));
}

void AGinkgoPlayerController::SetSlowMotionEnabled(const bool bEnable, const bool bNotify)
{
	const bool bChanged = (bSlowMotionEnabled != bEnable);
	bSlowMotionEnabled = bEnable;
	if (UWorld* World = GetWorld(); World && !World->bIsTearingDown)
	{
		UGameplayStatics::SetGlobalTimeDilation(World, bSlowMotionEnabled ? SlowMotionTimeDilation : 1.0f);
	}
	if (!bChanged)
	{
		return;
	}
	UE_LOG(LogGinkgoControl, Log, TEXT("[SlowMotion] %s（全局时间倍率=%.2f）"),
		bSlowMotionEnabled ? TEXT("开启 0.2× 慢动作") : TEXT("恢复常速"),
		bSlowMotionEnabled ? SlowMotionTimeDilation : 1.0f);
	if (bNotify && IsLocalController())
	{
		FGinkgoToast Toast;
		Toast.Icon = EGinkgoToastIcon::Gameplay;
		Toast.Title = bSlowMotionEnabled
			? NSLOCTEXT("GinkgoControl", "SlowMoOnTitle", "慢动作 0.2×")
			: NSLOCTEXT("GinkgoControl", "SlowMoOffTitle", "已恢复常速");
		Toast.Body = bSlowMotionEnabled
			? NSLOCTEXT("GinkgoControl", "SlowMoOnBody", "再次按 P 取消慢动作。")
			: FText::GetEmpty();
		PushToast(Toast);
	}
}

void AGinkgoPlayerController::OnToggleSlowMotion(const FInputActionValue& Value)
{
	if (IsMenuOpen())
	{
		return;
	}
	SetSlowMotionEnabled(!bSlowMotionEnabled, true);
}

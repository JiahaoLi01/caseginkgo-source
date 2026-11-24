#include "UI/GinkgoPauseMenu.h"

#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Control/GinkgoPlayerController.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SWeftViewport.h"
#include "UI/GinkgoSettingsPage.h"
#include "UI/GinkgoTitleSubsystem.h"
#include "UI/GinkgoUiSounds.h"
#include "UI/GinkgoUiTheme.h"
#include "Utilities/Debug/DebugCategory.h"
#include "WeftSubsystem.h"
#include "WeftView.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/SOverlay.h"

namespace
{
	/** 页面目录：位于 Content/UI 随包发布目录内 */
	const TCHAR* PauseMenuPageDirectory = TEXT("UI/PauseMenu");

	/** 视口挂载层级：高于普通 HUD 与调参面板（30），低于设置子页（45） */
	constexpr int32 PauseWidgetZOrder = 40;

	/** 暂停菜单底层 Slate 背景模糊强度（逻辑半径）；右侧 60% 画面柔和虚化 */
	constexpr float PauseBlurStrength = 8.0f;

	/** 打开暂停菜单时的背景模糊入场时长（秒） */
	constexpr float PauseBlurEnterSeconds = 0.22f;

	/** 关闭暂停菜单时的背景模糊退场时长（秒），与页面 CSS 退场动画同步 */
	constexpr float PauseBlurExitSeconds = 0.16f;

	/** 三次缓出曲线：起步利落、收尾柔和，适配短时长 UI 开合过渡 */
	float PauseEaseOutCubic(const float Alpha)
	{
		const float Inv = 1.0f - FMath::Clamp(Alpha, 0.0f, 1.0f);
		return 1.0f - Inv * Inv * Inv;
	}

	FString PauseToJsonText(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
		return Json;
	}
}

FGinkgoPauseMenu::FGinkgoPauseMenu(AGinkgoPlayerController& InController)
	: Controller(&InController)
{
}

FGinkgoPauseMenu::~FGinkgoPauseMenu()
{
	Hide();
}

FKey FGinkgoPauseMenu::GetToggleKey()
{
	return FGinkgoSettingsPage::GetToggleKey();
}

bool FGinkgoPauseMenu::Show(const bool bInFromGamepad)
{
	if (IsShown())
	{
		return true;
	}
	AGinkgoPlayerController* PlayerController = Controller.Get();
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	UWeftSubsystem* Weft = UWeftSubsystem::Get();
	UWeftView* NewView = Viewport && Weft && Weft->IsAvailable() ? Weft->CreateView() : nullptr;
	if (!NewView)
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[PauseMenu] 打不开暂停菜单：%s"),
			!Viewport ? TEXT("没有游戏视口（无界面运行）") : TEXT("weft 在这里不运行（-nullrhi，或没有编出 weft 的运行时库）"));
		return false;
	}
	Weft->AddEffects(FPaths::ProjectContentDir() / TEXT("UI/Effects/effects.json"));
	View.Reset(NewView);
	ViewportClient = Viewport;
	bFromGamepad = bInFromGamepad;
	bCursorWasShown = PlayerController->ShouldShowMouseCursor();
	bWaitingForSettings = false;
	bSuppressCloseSound = false;
	bClosing = false;
	BlurTransitionDuration = 0.f;
	PendingAction = EPendingAction::None;

	MessageHandle = NewView->OnMessage.AddRaw(this, &FGinkgoPauseMenu::OnMessage);
	Widget = SNew(SWeftViewport).View(NewView);
	RootWidget = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(BlurWidget, SBackgroundBlur)
			.Padding(FMargin(0.f))
			.BlurStrength(0.f)
			.bApplyAlphaToBlur(true)
		]
		+ SOverlay::Slot()
		[
			Widget.ToSharedRef()
		];
	StartBlurTransition(0.f, PauseBlurStrength, PauseBlurEnterSeconds);
	Viewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), PauseWidgetZOrder);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FGinkgoPauseMenu::OnTick));

	// 仅单机模式真正暂停世界；联网对局保持世界推进
	if (World->GetNetMode() == NM_Standalone && !World->IsPaused())
	{
		bPausedWorld = PlayerController->SetPause(true);
	}
	else
	{
		bPausedWorld = false;
	}

	PlayerController->FlushPressedKeys();
	PlayerController->RefreshGameplayHudVisibility();
	ApplyInputMode();
	UE_LOG(LogGinkgoUI, Log, TEXT("[PauseMenu] 打开暂停菜单（%s，单机暂停=%d）"),
		bFromGamepad ? TEXT("手柄") : TEXT("键盘"), bPausedWorld ? 1 : 0);
	UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Open);
	return true;
}

void FGinkgoPauseMenu::Hide()
{
	if (!IsShown())
	{
		return;
	}
	if (TickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	View->OnMessage.Remove(MessageHandle);
	MessageHandle.Reset();

	AGinkgoPlayerController* PlayerController = Controller.Get();
	const bool bWorldTearingDown = !PlayerController || !PlayerController->GetWorld() || PlayerController->GetWorld()->bIsTearingDown;

	// 若由本菜单打开的设置子页仍处于显示状态，先解绑回调并一并关闭，避免退出/重载时残留
	if (bWaitingForSettings && PlayerController && PlayerController->IsSettingsPageOpen())
	{
		FGinkgoSettingsPage& SettingsPage = PlayerController->GetOrCreateSettingsPage();
		SettingsPage.ClearOnClosedCallback();
		SettingsPage.Hide();
	}
	bWaitingForSettings = false;

	if (!bSuppressCloseSound && !bWorldTearingDown)
	{
		UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Close);
	}
	bSuppressCloseSound = false;

	if (bPausedWorld && PlayerController && PlayerController->GetWorld())
	{
		PlayerController->SetPause(false);
	}
	bPausedWorld = false;

	if (UGameViewportClient* Viewport = ViewportClient.Get(); Viewport && RootWidget)
	{
		Viewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
	}

	RootWidget.Reset();
	BlurWidget.Reset();
	Widget.Reset();
	View.Reset();
	ViewportClient.Reset();
	bLoaded = false;
	bClosing = false;
	BlurTransitionStart = 0.0;
	BlurFromStrength = 0.f;
	BlurToStrength = 0.f;
	BlurCurrentStrength = 0.f;
	BlurTransitionDuration = 0.f;
	PendingAction = EPendingAction::None;

	if (PlayerController && PlayerController->GetWorld() && PlayerController->GetWorld()->GetGameViewport())
	{
		PlayerController->RefreshGameplayHudVisibility();
		if (bCursorWasShown)
		{
			FInputModeGameAndUI InputMode;
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			InputMode.SetHideCursorDuringCapture(false);
			PlayerController->SetInputMode(InputMode);
			PlayerController->SetShowMouseCursor(true);
		}
		else
		{
			PlayerController->SetInputMode(FInputModeGameOnly());
			PlayerController->SetShowMouseCursor(false);
		}
	}
	UE_LOG(LogGinkgoUI, Log, TEXT("[PauseMenu] 关闭暂停菜单"));
}

void FGinkgoPauseMenu::OnSettingsClosed()
{
	if (!IsShown() || !bWaitingForSettings)
	{
		return;
	}
	bWaitingForSettings = false;
	// 设置页退场时已将模糊平滑回到 PauseBlurStrength，此处直接保持该强度实现无闪烁衔接
	StartBlurTransition(PauseBlurStrength, PauseBlurStrength, 0.f);
	if (RootWidget)
	{
		RootWidget->SetVisibility(EVisibility::SelfHitTestInvisible);
	}
	ApplyInputMode();
	if (bLoaded && View)
	{
		SendMenuState();
		View->Emit(TEXT("pause-return"), TEXT("{}"));
	}
	UE_LOG(LogGinkgoUI, Log, TEXT("[PauseMenu] 设置子页关闭，返回暂停菜单"));
}

void FGinkgoPauseMenu::BeginClosing()
{
	if (!IsShown() || bClosing)
	{
		return;
	}
	bClosing = true;
	PendingAction = EPendingAction::None;
	bSuppressCloseSound = true;

	if (const AGinkgoPlayerController* PlayerController = Controller.Get();
		PlayerController && PlayerController->GetWorld() && !PlayerController->GetWorld()->bIsTearingDown)
	{
		UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Close);
	}
	if (bLoaded && View)
	{
		View->Emit(TEXT("pause-closing"), TEXT("{}"));
	}
	StartBlurTransition(BlurCurrentStrength, 0.f, PauseBlurExitSeconds);
}

void FGinkgoPauseMenu::StartBlurTransition(const float FromStrength, const float ToStrength, const float DurationSeconds)
{
	BlurFromStrength = FromStrength;
	BlurToStrength = ToStrength;
	BlurTransitionDuration = FMath::Max(0.f, DurationSeconds);
	BlurTransitionStart = FPlatformTime::Seconds();
	BlurCurrentStrength = BlurTransitionDuration > 0.f ? BlurFromStrength : BlurToStrength;
	if (BlurWidget)
	{
		BlurWidget->SetBlurStrength(BlurCurrentStrength);
	}
}

bool FGinkgoPauseMenu::UpdateBlurTransition()
{
	if (BlurTransitionDuration <= 0.f)
	{
		BlurCurrentStrength = BlurToStrength;
		if (BlurWidget)
		{
			BlurWidget->SetBlurStrength(BlurCurrentStrength);
		}
		return true;
	}
	const float Elapsed = static_cast<float>(FPlatformTime::Seconds() - BlurTransitionStart);
	const float Alpha = FMath::Clamp(Elapsed / BlurTransitionDuration, 0.0f, 1.0f);
	BlurCurrentStrength = FMath::Lerp(BlurFromStrength, BlurToStrength, PauseEaseOutCubic(Alpha));
	if (BlurWidget)
	{
		BlurWidget->SetBlurStrength(BlurCurrentStrength);
	}
	if (Alpha >= 1.0f)
	{
		BlurTransitionDuration = 0.f;
		return true;
	}
	return false;
}

void FGinkgoPauseMenu::ApplyInputMode()
{
	AGinkgoPlayerController* PlayerController = Controller.Get();
	if (!PlayerController || !Widget)
	{
		return;
	}
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Widget);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->SetShowMouseCursor(true);
	Widget->FocusView(View.Get());
}

bool FGinkgoPauseMenu::OnTick(const float DeltaSeconds)
{
	AGinkgoPlayerController* PlayerController = Controller.Get();
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!PlayerController || !World || !IsShown())
	{
		return false;
	}

	if (!bWaitingForSettings)
	{
		const bool bBlurDone = UpdateBlurTransition();
		if (bClosing)
		{
			if (bBlurDone)
			{
				Hide();
				return false;
			}
			return true;
		}
	}

	if (PendingAction != EPendingAction::None)
	{
		const EPendingAction Action = PendingAction;
		PendingAction = EPendingAction::None;

		if (Action == EPendingAction::Close)
		{
			BeginClosing();
			return true;
		}
		if (Action == EPendingAction::OpenSettings)
		{
			bWaitingForSettings = true;
			StartBlurTransition(PauseBlurStrength, PauseBlurStrength, 0.f);
			if (RootWidget)
			{
				RootWidget->SetVisibility(EVisibility::Collapsed);
			}
			const bool bOpened = PlayerController->GetOrCreateSettingsPage().Show(
				bOpenSettingsFromGamepad,
				[WeakController = Controller]()
				{
					if (AGinkgoPlayerController* Owner = WeakController.Get())
					{
						Owner->HandleSettingsClosedFromPauseMenu();
					}
				});
			if (!bOpened)
			{
				bWaitingForSettings = false;
				if (RootWidget)
				{
					RootWidget->SetVisibility(EVisibility::SelfHitTestInvisible);
				}
				ApplyInputMode();
			}
			return true;
		}
		if (Action == EPendingAction::RestartCheckpoint)
		{
			UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Confirm);
			bSuppressCloseSound = true;
			Hide();
			PlayerController->RestartCheckpoint();
			return false;
		}
		if (Action == EPendingAction::ReturnToMainMenu)
		{
			UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Confirm);
			bSuppressCloseSound = true;
			Hide();
			PlayerController->ReturnToMainMenu();
			return false;
		}
		if (Action == EPendingAction::QuitGame)
		{
			UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Confirm);
			bSuppressCloseSound = true;
			Hide();
			UKismetSystemLibrary::QuitGame(World, PlayerController, EQuitPreference::Quit, false);
			return false;
		}
	}

	if (bWaitingForSettings)
	{
		return true;
	}

	UGameViewportClient* Viewport = ViewportClient.Get();
	if (Viewport && !Viewport->IgnoreInput())
	{
		ApplyInputMode();
	}

	FVector2D Pixels = FVector2D::ZeroVector;
	if (Viewport)
	{
		Viewport->GetViewportSize(Pixels);
	}
	if (bLoaded || Pixels.X < 2.0 || Pixels.Y < 2.0)
	{
		return true;
	}

	const float DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(
		FIntPoint(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y)));
	View->Resize(Pixels / DPIScale, DPIScale);
	const FString Directory = FPaths::ProjectContentDir() / PauseMenuPageDirectory;
	if (!Ginkgo::UI::LoadPageWithKeycaps(*View, Directory))
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[PauseMenu] 页面载入失败：%s（原因见 LogGinkgoUI、LogWeft）"), *Directory);
		Hide();
		return false;
	}
	bLoaded = true;
	return true;
}

void FGinkgoPauseMenu::OnMessage(const FString& Name, const FString& JsonValue, const uint64 CallId)
{
	if (Name == TEXT("ready"))
	{
		SendMenuState();
	}
	else if (Name == TEXT("sound"))
	{
		UGinkgoUiSoundSubsystem::PlayFromPage(Controller.Get(), JsonValue);
	}
	else if (Name == TEXT("log"))
	{
		UE_LOG(LogGinkgoUI, Log, TEXT("[PauseMenu] 页面：%s"), *JsonValue);
	}
	else if (bClosing)
	{
		return;
	}
	else if (Name == TEXT("resume"))
	{
		PendingAction = EPendingAction::Close;
	}
	else if (Name == TEXT("openSettings"))
	{
		bOpenSettingsFromGamepad = bFromGamepad;
		TSharedPtr<FJsonObject> Object;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonValue), Object) && Object)
		{
			Object->TryGetBoolField(TEXT("fromGamepad"), bOpenSettingsFromGamepad);
		}
		PendingAction = EPendingAction::OpenSettings;
	}
	else if (Name == TEXT("restartCheckpoint"))
	{
		PendingAction = EPendingAction::RestartCheckpoint;
	}
	else if (Name == TEXT("returnToMainMenu"))
	{
		PendingAction = EPendingAction::ReturnToMainMenu;
	}
	else if (Name == TEXT("quitGame"))
	{
		PendingAction = EPendingAction::QuitGame;
	}
}

void FGinkgoPauseMenu::SendMenuState() const
{
	const AGinkgoPlayerController* PlayerController = Controller.Get();
	const UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!PlayerController || !World || !View)
	{
		return;
	}

	const UGinkgoCheckpointSubsystem* Checkpoints = UGinkgoCheckpointSubsystem::Get(PlayerController);
	const bool bHasCheckpoint = Checkpoints && Checkpoints->HasAvailableCheckpoint();
	const FString LocationName = Checkpoints ? Checkpoints->GetCurrentLocationName().ToString() : FString();
	const FString MapName = FPackageName::GetShortName(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));

	const UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(PlayerController);
	const FString DifficultyLabel = Title ? Ginkgo::Difficulty::GetDisplayName(Title->GetDifficulty()).ToString() : FString();

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("fromGamepad"), bFromGamepad);
	Payload->SetStringField(TEXT("toggleKey"), GetToggleKey().GetDisplayName().ToString());
	Payload->SetBoolField(TEXT("isPaused"), World->IsPaused());
	Payload->SetBoolField(TEXT("isStandalone"), World->GetNetMode() == NM_Standalone);
	Payload->SetBoolField(TEXT("hasCheckpoint"), bHasCheckpoint);
	Payload->SetStringField(TEXT("locationName"), LocationName);
	Payload->SetStringField(TEXT("mapName"), MapName);
	Payload->SetStringField(TEXT("difficultyLabel"), DifficultyLabel);
	View->Emit(TEXT("pause-state"), PauseToJsonText(Payload));
}

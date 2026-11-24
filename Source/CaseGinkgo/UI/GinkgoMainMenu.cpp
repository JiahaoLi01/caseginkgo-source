#include "UI/GinkgoMainMenu.h"

#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Components/AudioComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundBase.h"
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

namespace GinkgoMainMenuPrivate
{
	/** 页面目录：位于 Content/UI 随包发布目录内 */
	const TCHAR* MainMenuPageDirectory = TEXT("UI/MainMenu");

	/** 视口挂载层级：高于常规 HUD（5–30）与暂停菜单（40），低于设置子页（45） */
	constexpr int32 MainMenuWidgetZOrder = 42;

	/** 主菜单阶段对实时 3D 游戏画面的虚化强度（0 保持右侧实景画面完全锐利通透，文字区由左侧暗黑体积云雾保障对比度） */
	constexpr float MenuBlurStrength = 0.0f;

	/** 特性画廊展开时的全屏景深模糊强度（逻辑半径），提升图文展板可读性 */
	constexpr float GalleryBlurStrength = 7.2f;

	/** 开屏揭幕进入主菜单时的背景虚化过渡时长（秒） */
	constexpr float RevealBlurSeconds = 0.85f;

	/** 直接打开主菜单时的背景虚化入场时长（秒） */
	constexpr float DirectMenuBlurSeconds = 0.30f;

	/** 特性画廊展开与收起时的背景虚化切换时长（秒） */
	constexpr float GalleryBlurSeconds = 0.24f;

	/** 点击「新游戏」无缝进入实时场景时的退场淡出时长（秒），与页面 CSS 退场动画同步 */
	constexpr float EnterGameplayFadeSeconds = 0.44f;

	/** 切换下一镜开屏音效时前一段余韵的淡出时长（秒），防止快速跳过时两声叠糊 */
	constexpr float OpeningOverlapFadeSeconds = 0.20f;

	/** 三次缓出曲线：起步利落、收尾柔和，适配界面与景深过渡 */
	float EaseOutCubic(const float Alpha)
	{
		const float Inv = 1.0f - FMath::Clamp(Alpha, 0.0f, 1.0f);
		return 1.0f - Inv * Inv * Inv;
	}

	/** 紧凑 JSON 序列化工具，供 C++ 向 Weft 页面推送事件使用 */
	FString ToJsonText(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
		return Json;
	}

#if !UE_BUILD_SHIPPING
	/** 调试命令：ginkgo.title.show [splash|menu]，用于在运行期或 PIE 中随时预览开屏两镜或主菜单 */
	FAutoConsoleCommandWithWorldAndArgs CmdShowTitle(
		TEXT("ginkgo.title.show"),
		TEXT("打开开屏画面或主菜单：用法 ginkgo.title.show [splash|menu]，默认 splash"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AGinkgoPlayerController* PC = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
			if (!PC)
			{
				return;
			}
			const bool bSplash = Args.Num() == 0 || !Args[0].Equals(TEXT("menu"), ESearchCase::IgnoreCase);
			PC->GetOrCreateMainMenu().Hide();
			PC->GetOrCreateMainMenu().Show(bSplash, false);
		}));
#endif
}

FGinkgoMainMenu::FGinkgoMainMenu(AGinkgoPlayerController& InController)
	: Controller(&InController)
{
}

FGinkgoMainMenu::~FGinkgoMainMenu()
{
	Hide();
}

void FGinkgoMainMenu::PreloadAudio(const UObject* WorldContext)
{
	const FGinkgoTitleSounds& Sounds = UGinkgoDeveloperSettings::Get()->TitleSounds;
	UGinkgoUiSoundSubsystem::PreloadSound(WorldContext, Sounds.OpeningShot1);
	UGinkgoUiSoundSubsystem::PreloadSound(WorldContext, Sounds.OpeningShot2);
	UGinkgoUiSoundSubsystem::PreloadSound(WorldContext, Sounds.OpeningReveal);
	UGinkgoUiSoundSubsystem::PreloadSound(WorldContext, Sounds.MenuMusic);
}

bool FGinkgoMainMenu::Show(const bool bInStartWithSplash, const bool bInFromGamepad)
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
		UE_LOG(LogGinkgoUI, Warning, TEXT("[MainMenu] 打不开主菜单：%s"),
			!Viewport ? TEXT("没有游戏视口（无界面运行）") : TEXT("weft 在这里不运行（-nullrhi，或没有编出 weft 的运行时库）"));
		return false;
	}

	Weft->AddEffects(FPaths::ProjectContentDir() / TEXT("UI/Effects/effects.json"));
	PreloadAudio(PlayerController);

	View.Reset(NewView);
	ViewportClient = Viewport;
	bStartWithSplash = bInStartWithSplash;
	bFromGamepad = bInFromGamepad;
	bWaitingForSettings = false;
	bOpenSettingsFromGamepad = false;
	bGalleryOpen = false;
	bEnteringGameplay = false;
	bLoaded = false;
	LastSplashShotPlayed = 0;
	Phase = bStartWithSplash ? EGinkgoMainMenuPhase::Splash : EGinkgoMainMenuPhase::Menu;
	BlurTransitionDuration = 0.f;
	PendingAction = EPendingAction::None;

	MessageHandle = NewView->OnMessage.AddRaw(this, &FGinkgoMainMenu::OnMessage);
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

	if (bStartWithSplash)
	{
		StartBlurTransition(0.f, 0.f, 0.f);
	}
	else
	{
		StartBlurTransition(0.f, GinkgoMainMenuPrivate::MenuBlurStrength, GinkgoMainMenuPrivate::DirectMenuBlurSeconds);
	}

	Viewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), GinkgoMainMenuPrivate::MainMenuWidgetZOrder);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FGinkgoMainMenu::OnTick));

	PlayerController->FlushPressedKeys();
	PlayerController->RefreshGameplayHudVisibility();
	ApplyInputMode();

	if (!bStartWithSplash)
	{
		StartMusic();
	}

	UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 打开标题界面（阶段=%s，手柄=%d）"),
		bStartWithSplash ? TEXT("开屏动画") : TEXT("主菜单"), bFromGamepad ? 1 : 0);
	return true;
}

void FGinkgoMainMenu::Hide()
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

	if (bWaitingForSettings && PlayerController && PlayerController->IsSettingsPageOpen())
	{
		FGinkgoSettingsPage& SettingsPage = PlayerController->GetOrCreateSettingsPage();
		SettingsPage.ClearOnClosedCallback();
		SettingsPage.Hide();
	}
	bWaitingForSettings = false;

	if (UAudioComponent* OpeningAudio = ActiveOpeningAudio.Get())
	{
		if (bWorldTearingDown)
		{
			OpeningAudio->Stop();
		}
		else if (OpeningAudio->IsPlaying())
		{
			OpeningAudio->FadeOut(GinkgoMainMenuPrivate::OpeningOverlapFadeSeconds, 0.f);
		}
	}
	ActiveOpeningAudio.Reset();

	// 若尚未启动进入玩法的长淡出，此处按安全时长淡出或随世界销毁直接停播
	StopMusic(bWorldTearingDown ? 0.f : 0.25f);

	if (UGameViewportClient* Viewport = ViewportClient.Get(); Viewport && RootWidget)
	{
		Viewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
	}

	RootWidget.Reset();
	BlurWidget.Reset();
	Widget.Reset();
	View.Reset();
	ViewportClient.Reset();
	Phase = EGinkgoMainMenuPhase::Hidden;
	bLoaded = false;
	bGalleryOpen = false;
	bEnteringGameplay = false;
	BlurTransitionStart = 0.0;
	BlurFromStrength = 0.f;
	BlurToStrength = 0.f;
	BlurCurrentStrength = 0.f;
	BlurTransitionDuration = 0.f;
	PendingAction = EPendingAction::None;

	if (PlayerController && PlayerController->GetWorld() && PlayerController->GetWorld()->GetGameViewport() && !bWorldTearingDown)
	{
		PlayerController->RefreshGameplayHudVisibility();
		if (!PlayerController->IsMenuOpen())
		{
			PlayerController->SetInputMode(FInputModeGameOnly());
			PlayerController->SetShowMouseCursor(false);
		}
	}
	UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 关闭标题界面"));
}

bool FGinkgoMainMenu::IsMusicPlaying() const
{
	const UAudioComponent* Audio = MusicAudio.Get();
	return Audio && Audio->IsPlaying();
}

void FGinkgoMainMenu::OnSettingsClosed()
{
	if (!IsShown() || !bWaitingForSettings)
	{
		return;
	}
	bWaitingForSettings = false;
	const float TargetBlur = bGalleryOpen ? GinkgoMainMenuPrivate::GalleryBlurStrength : GinkgoMainMenuPrivate::MenuBlurStrength;
	StartBlurTransition(TargetBlur, TargetBlur, 0.f);
	if (RootWidget)
	{
		RootWidget->SetVisibility(EVisibility::SelfHitTestInvisible);
	}
	ApplyInputMode();
	if (bLoaded && View)
	{
		SendMenuState();
		View->Emit(TEXT("title-return"), TEXT("{}"));
	}
	UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 设置子页关闭，返回主菜单"));
}

void FGinkgoMainMenu::SkipSplashToMenu()
{
	if (!IsShown() || Phase != EGinkgoMainMenuPhase::Splash)
	{
		return;
	}
	EnterMenuPhase(false);
	if (bLoaded && View)
	{
		View->Emit(TEXT("title-skip-splash"), TEXT("{}"));
	}
}

void FGinkgoMainMenu::ApplyInputMode()
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

void FGinkgoMainMenu::PlayOpeningShotSound(const int32 ShotIndex)
{
	const AGinkgoPlayerController* PlayerController = Controller.Get();
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!World || World->bIsTearingDown)
	{
		return;
	}

	const FGinkgoTitleSounds& Sounds = UGinkgoDeveloperSettings::Get()->TitleSounds;
	const TSoftObjectPtr<USoundBase>* SoftSound = nullptr;
	if (ShotIndex == 1)
	{
		SoftSound = &Sounds.OpeningShot1;
	}
	else if (ShotIndex == 2)
	{
		SoftSound = &Sounds.OpeningShot2;
	}
	else if (ShotIndex == 3)
	{
		SoftSound = &Sounds.OpeningReveal;
	}
	if (!SoftSound)
	{
		return;
	}

	LastSplashShotPlayed = ShotIndex;
	if (UAudioComponent* Previous = ActiveOpeningAudio.Get(); Previous && Previous->IsPlaying())
	{
		Previous->FadeOut(GinkgoMainMenuPrivate::OpeningOverlapFadeSeconds, 0.f);
	}
	ActiveOpeningAudio.Reset();

	if (Sounds.OpeningVolume <= 0.f)
	{
		return;
	}
	USoundBase* Sound = SoftSound->LoadSynchronous();
	if (!Sound)
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[MainMenu] 开屏第 %d 镜转场音效未能加载：%s"), ShotIndex, *SoftSound->ToString());
		return;
	}
	UAudioComponent* Spawned = UGameplayStatics::SpawnSound2D(World, Sound, Sounds.OpeningVolume);
	ActiveOpeningAudio = Spawned;
	UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 播放开屏第 %d 镜过渡音效（音量 %.2f）%s"),
		ShotIndex, Sounds.OpeningVolume, Spawned ? TEXT("") : TEXT("（无音频设备）"));
}

void FGinkgoMainMenu::StartMusic()
{
	if (UAudioComponent* Existing = MusicAudio.Get(); Existing && Existing->IsPlaying())
	{
		return;
	}
	const AGinkgoPlayerController* PlayerController = Controller.Get();
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!World || World->bIsTearingDown)
	{
		return;
	}

	const FGinkgoTitleSounds& Sounds = UGinkgoDeveloperSettings::Get()->TitleSounds;
	if (Sounds.MusicVolume <= 0.f)
	{
		return;
	}
	USoundBase* Music = Sounds.MenuMusic.LoadSynchronous();
	if (!Music)
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[MainMenu] 主菜单背景音乐未能加载：%s"), *Sounds.MenuMusic.ToString());
		return;
	}

	// bIsUISound=true 保证暂停与界面切换期持续平滑播放，AutoDestroy=true 保证淡出结束后自动回收
	UAudioComponent* Spawned = UGameplayStatics::SpawnSound2D(World, Music, Sounds.MusicVolume, 1.f, 0.f, nullptr, false, true);
	if (!Spawned)
	{
		UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 主菜单背景音乐未创建音频组件（可能无音频设备）"));
		return;
	}
	Spawned->bIsUISound = true;
	Spawned->bAutoDestroy = true;
	if (Sounds.MusicFadeInSeconds > 0.f)
	{
		Spawned->FadeIn(Sounds.MusicFadeInSeconds, Sounds.MusicVolume);
	}
	MusicAudio = Spawned;
	UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 启动主菜单配乐 Brother's Keeper（音量 %.2f，淡入 %.2f s）"),
		Sounds.MusicVolume, Sounds.MusicFadeInSeconds);
}

void FGinkgoMainMenu::StopMusic(const float FadeOutSeconds)
{
	if (UAudioComponent* Audio = MusicAudio.Get())
	{
		if (Audio->IsPlaying())
		{
			if (FadeOutSeconds > 0.f)
			{
				Audio->FadeOut(FadeOutSeconds, 0.f);
			}
			else
			{
				Audio->Stop();
			}
		}
	}
	MusicAudio.Reset();
}

void FGinkgoMainMenu::EnterMenuPhase(const bool bImmediateBlur)
{
	if (Phase == EGinkgoMainMenuPhase::Menu)
	{
		StartMusic();
		return;
	}
	Phase = EGinkgoMainMenuPhase::Menu;
	StartBlurTransition(BlurCurrentStrength, GinkgoMainMenuPrivate::MenuBlurStrength,
		bImmediateBlur ? 0.f : GinkgoMainMenuPrivate::RevealBlurSeconds);
	StartMusic();
	UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 开屏完成，进入实时游戏画面主菜单"));
}

void FGinkgoMainMenu::BeginEnteringGameplay()
{
	if (!IsShown() || bEnteringGameplay)
	{
		return;
	}
	bEnteringGameplay = true;
	PendingAction = EPendingAction::None;

	const FGinkgoTitleSounds& Sounds = UGinkgoDeveloperSettings::Get()->TitleSounds;
	StopMusic(Sounds.MusicFadeOutSeconds);
	if (bLoaded && View)
	{
		View->Emit(TEXT("title-entering-game"), TEXT("{}"));
	}
	StartBlurTransition(BlurCurrentStrength, 0.f, GinkgoMainMenuPrivate::EnterGameplayFadeSeconds);
}

void FGinkgoMainMenu::StartBlurTransition(const float FromStrength, const float ToStrength, const float DurationSeconds)
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

bool FGinkgoMainMenu::UpdateBlurTransition()
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
	BlurCurrentStrength = FMath::Lerp(BlurFromStrength, BlurToStrength, GinkgoMainMenuPrivate::EaseOutCubic(Alpha));
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

bool FGinkgoMainMenu::OnTick(const float DeltaSeconds)
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
		if (bEnteringGameplay)
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

		if (Action == EPendingAction::StartNewGame)
		{
			UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Confirm);
			if (UGinkgoCheckpointSubsystem* Checkpoints = UGinkgoCheckpointSubsystem::Get(PlayerController))
			{
				Checkpoints->ClearCheckpointForNewGame();
			}
			UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(PlayerController);
			const bool bNeedReload = Title && Title->NeedsFreshReloadForNewGame();
			if (Title)
			{
				Title->SetDifficulty(PendingNewGameDifficulty);
				Title->MarkEnteredGameplay();
			}
			UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 开启新游戏（难度=%s，玩家承伤倍率=%.2f）"),
				*Ginkgo::Difficulty::GetDisplayName(PendingNewGameDifficulty).ToString(),
				Ginkgo::Difficulty::GetPlayerIncomingDamageScale(PendingNewGameDifficulty));
			if (bNeedReload)
			{
				if (Title)
				{
					Title->RequestSkipTitleOnNextLoad();
				}
				const FString MapPackage = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
				StopMusic(0.35f);
				Hide();
				UGameplayStatics::OpenLevel(World, FName(*MapPackage));
				return false;
			}
			BeginEnteringGameplay();
			return true;
		}

		if (Action == EPendingAction::ContinueGame)
		{
			UGinkgoCheckpointSubsystem* Checkpoints = UGinkgoCheckpointSubsystem::Get(PlayerController);
			if (!Checkpoints || !Checkpoints->HasAvailableCheckpoint())
			{
				UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Invalid);
				SendMenuState();
				return true;
			}
			UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Confirm);
			if (UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(PlayerController))
			{
				Title->MarkEnteredGameplay();
				Title->RequestSkipTitleOnNextLoad();
			}
			StopMusic(0.45f);
			Hide();
			PlayerController->RestartCheckpoint();
			return false;
		}

		if (Action == EPendingAction::OpenSettings)
		{
			bWaitingForSettings = true;
			const float CurrentBlur = bGalleryOpen ? GinkgoMainMenuPrivate::GalleryBlurStrength : GinkgoMainMenuPrivate::MenuBlurStrength;
			StartBlurTransition(CurrentBlur, CurrentBlur, 0.f);
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
						Owner->HandleSettingsClosedFromMainMenu();
					}
				},
				CurrentBlur);
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

		if (Action == EPendingAction::QuitGame)
		{
			UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Confirm);
			StopMusic(0.25f);
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
	const FString Directory = FPaths::ProjectContentDir() / GinkgoMainMenuPrivate::MainMenuPageDirectory;
	if (!Ginkgo::UI::LoadThemedPage(*View, Directory))
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[MainMenu] 页面载入失败：%s（原因见 LogGinkgoUI、LogWeft）"), *Directory);
		Hide();
		return false;
	}
	bLoaded = true;
	return true;
}

void FGinkgoMainMenu::OnMessage(const FString& Name, const FString& JsonValue, const uint64 CallId)
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
		UE_LOG(LogGinkgoUI, Log, TEXT("[MainMenu] 页面：%s"), *JsonValue);
	}
	else if (bEnteringGameplay)
	{
		return;
	}
	else if (Name == TEXT("splashShot"))
	{
		int32 ShotIndex = 0;
		TSharedPtr<FJsonObject> Object;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonValue), Object) && Object)
		{
			Object->TryGetNumberField(TEXT("shot"), ShotIndex);
		}
		if (ShotIndex >= 1 && ShotIndex <= 3)
		{
			PlayOpeningShotSound(ShotIndex);
			if (ShotIndex == 3)
			{
				EnterMenuPhase(false);
			}
		}
	}
	else if (Name == TEXT("splashComplete"))
	{
		EnterMenuPhase(false);
	}
	else if (Name == TEXT("galleryState"))
	{
		bool bOpen = false;
		TSharedPtr<FJsonObject> Object;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonValue), Object) && Object)
		{
			Object->TryGetBoolField(TEXT("open"), bOpen);
		}
		bGalleryOpen = bOpen;
		if (Phase == EGinkgoMainMenuPhase::Menu)
		{
			StartBlurTransition(
				BlurCurrentStrength,
				bGalleryOpen ? GinkgoMainMenuPrivate::GalleryBlurStrength : GinkgoMainMenuPrivate::MenuBlurStrength,
				GinkgoMainMenuPrivate::GalleryBlurSeconds);
		}
	}
	else if (Name == TEXT("newGame"))
	{
		PendingNewGameDifficulty = EGinkgoDifficulty::Balanced;
		TSharedPtr<FJsonObject> Object;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonValue), Object) && Object)
		{
			FString DiffId;
			if (Object->TryGetStringField(TEXT("difficulty"), DiffId))
			{
				Ginkgo::Difficulty::TryParseId(DiffId, PendingNewGameDifficulty);
			}
		}
		PendingAction = EPendingAction::StartNewGame;
	}
	else if (Name == TEXT("continueGame"))
	{
		PendingAction = EPendingAction::ContinueGame;
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
	else if (Name == TEXT("quitGame"))
	{
		PendingAction = EPendingAction::QuitGame;
	}
}

void FGinkgoMainMenu::SendMenuState() const
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
	const EGinkgoDifficulty CurrentDiff = Title ? Title->GetDifficulty() : EGinkgoDifficulty::Balanced;

	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("phase"), Phase == EGinkgoMainMenuPhase::Splash ? TEXT("splash") : TEXT("menu"));
	Payload->SetBoolField(TEXT("fromGamepad"), bFromGamepad);
	Payload->SetBoolField(TEXT("hasCheckpoint"), bHasCheckpoint);
	Payload->SetStringField(TEXT("locationName"), LocationName);
	Payload->SetStringField(TEXT("mapName"), MapName);
	Payload->SetStringField(TEXT("difficulty"), Ginkgo::Difficulty::ToId(CurrentDiff));
	View->Emit(TEXT("title-state"), GinkgoMainMenuPrivate::ToJsonText(Payload));
}

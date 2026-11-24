#include "UI/GinkgoLocationTitle.h"
#include "UI/GinkgoUiSounds.h"

#include "Control/GinkgoPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundBase.h"
#include "SWeftViewport.h"
#include "Utilities/Debug/DebugCategory.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

void FGinkgoLocationTitleQueue::Push(const FGinkgoCheckpointAnnouncement& Announcement, const FGinkgoLocationTitleSettings& Settings)
{
	FEntry Safe{Announcement, Settings};
	Safe.Settings.FadeIn = FMath::IsFinite(Settings.FadeIn) ? FMath::Max(0.f, Settings.FadeIn) : 0.f;
	Safe.Settings.FadeOut = FMath::IsFinite(Settings.FadeOut) ? FMath::Max(0.f, Settings.FadeOut) : 0.f;
	Safe.Settings.Hold = FMath::IsFinite(Settings.Hold) ? FMath::Max(0.f, Settings.Hold) : 5.f;
	Pending.Add(MoveTemp(Safe));
}

FGinkgoLocationTitleQueue::EStep FGinkgoLocationTitleQueue::Advance(const float DeltaSeconds)
{
	if (!Current.IsSet())
	{
		if (Pending.IsEmpty()) { return EStep::None; }
		Current = Pending[0];
		Pending.RemoveAt(0);
		Elapsed = 0.f;
		return EStep::Begin;
	}
	Elapsed += FMath::Max(0.f, DeltaSeconds);
	if (Elapsed >= Current->Settings.FadeIn + Current->Settings.Hold + Current->Settings.FadeOut)
	{
		Current.Reset();
		return EStep::Finish;
	}
	return EStep::None;
}

float FGinkgoLocationTitleQueue::GetOpacity() const
{
	if (!Current.IsSet()) { return 0.f; }
	const float FadeIn = Current->Settings.FadeIn;
	const float FadeOut = Current->Settings.FadeOut;
	const float Remaining = FadeIn + Current->Settings.Hold + FadeOut - Elapsed;
	// 零时长是直接切入/切出，而非极短淡变；显式分支，首帧 0/0 不会产生 NaN。
	const float Fade = FMath::Min(FadeIn > 0.f ? Elapsed / FadeIn : 1.f, FadeOut > 0.f ? Remaining / FadeOut : 1.f);
	return FMath::SmoothStep(0.f, 1.f, FMath::Clamp(Fade, 0.f, 1.f));
}

FGinkgoLocationTitleHud::FGinkgoLocationTitleHud(AGinkgoPlayerController& InController) : Controller(&InController)
{
}

void FGinkgoLocationTitleHud::BeginPresentation()
{
	const auto* PC = Controller.Get();
	UGameViewportClient* Viewport = PC && PC->GetWorld() ? PC->GetWorld()->GetGameViewport() : nullptr;
	UWeftSubsystem* Weft = UWeftSubsystem::Get();
	if (Viewport && Weft && Weft->IsAvailable())
	{
		View.Reset(Weft->CreateView());
		if (View)
		{
			ViewportClient = Viewport;
			Widget = SNew(SWeftViewport).View(View.Get()).Interactive(false);
			Viewport->AddViewportWidgetContent(Widget.ToSharedRef(), 25);
		}
	}
	TickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FGinkgoLocationTitleHud::OnWorldPostActorTick);
}

FGinkgoLocationTitleHud::~FGinkgoLocationTitleHud()
{
	EndPresentation();
}

void FGinkgoLocationTitleHud::EndPresentation()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
	TickHandle.Reset();
	if (auto* Viewport = ViewportClient.Get(); Viewport && Widget) { Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef()); }
	Widget.Reset();
	View.Reset();
	ViewportClient.Reset();
	bLoaded = false;
}

void FGinkgoLocationTitleHud::Push(const FGinkgoCheckpointAnnouncement& Announcement)
{
	UGinkgoUiSoundSubsystem::PreloadSound(Controller.Get(), Announcement.Sound);
	const bool bWasActive = Queue.IsActive();
	Queue.Push(Announcement, UGinkgoDeveloperSettings::Get()->LocationTitle);
	if (!bWasActive)
	{
		BeginPresentation();
		if (auto* PC = Controller.Get()) { PC->RefreshGameplayHudVisibility(); }
	}
}

void FGinkgoLocationTitleHud::OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	AGinkgoPlayerController* PC = Controller.Get();
	if (!PC || World != PC->GetWorld() || TickType == LEVELTICK_TimeOnly) { return; }
	if (!World->IsPaused() && !PC->IsMenuOpen())
	{
		const FGinkgoCheckpointAnnouncement Previous = Queue.GetCurrent() ? *Queue.GetCurrent() : FGinkgoCheckpointAnnouncement();
		const auto Step = Queue.Advance(static_cast<float>(FApp::GetDeltaTime()));
		if (Step == FGinkgoLocationTitleQueue::EStep::Begin)
		{
			const auto* Current = Queue.GetCurrent();
			if (USoundBase* Sound = Current->Sound.LoadSynchronous()) { UGameplayStatics::PlaySound2D(PC, Sound); }
			UE_LOG(LogGinkgoUI, Log, TEXT("[CheckpointTitle] 开始：%s"), *Current->LocationName.ToString());
		}
		else if (Step == FGinkgoLocationTitleQueue::EStep::Finish)
		{
			FGinkgoToast Toast;
			Toast.Icon = Previous.bSaved ? EGinkgoToastIcon::Saved : EGinkgoToastIcon::Gameplay;
			Toast.Title = Previous.bSaved ? NSLOCTEXT("Checkpoint", "Saved", "已存档") : NSLOCTEXT("Checkpoint", "Failed", "存档失败");
			Toast.Body = Previous.bSaved ? FText::GetEmpty() : NSLOCTEXT("Checkpoint", "Retry", "请离开检查点后重试。");
			PC->PushToast(Toast);
			UE_LOG(LogGinkgoUI, Log, TEXT("[CheckpointTitle] 结束：%s，随后提示=%s"), *Previous.LocationName.ToString(), *Toast.Title.ToString());
			if (!Queue.IsActive())
			{
				EndPresentation();
				PC->RefreshGameplayHudVisibility();
				return;
			}
		}
	}
	UpdatePage();
}

void FGinkgoLocationTitleHud::UpdatePage()
{
	if (!View || !Widget) { return; }
	FVector2D Pixels = FVector2D::ZeroVector;
	if (auto* Viewport = ViewportClient.Get()) { Viewport->GetViewportSize(Pixels); }
	if (Pixels.X < 2. || Pixels.Y < 2.) { return; }
	if (!bLoaded)
	{
		const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(Pixels.X, Pixels.Y));
		View->Resize(Pixels / Dpi, Dpi);
		bLoaded = View->LoadDirectory(FPaths::ProjectContentDir() / TEXT("UI/LocationTitle"));
	}
	// 播放期间只有暂停菜单或设置页开关会改变显隐；普通帧只更新标题内容和淡变，不反复写 Slate 状态。
	const EVisibility Visibility = Controller->IsMenuOpen() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
	if (Widget->GetVisibility() != Visibility) { Widget->SetVisibility(Visibility); }
	if (!bLoaded || !Queue.GetCurrent()) { return; }
	const FGinkgoLocationTitleSettings& Settings = *Queue.GetSettings();
	FString Message;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Message);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("title"), Queue.GetCurrent()->LocationName.ToString());
	Writer->WriteValue(TEXT("opacity"), Queue.GetOpacity());
	Writer->WriteValue(TEXT("width"), View->GetLogicalSize().X);
	Writer->WriteValue(TEXT("fontFamily"), Settings.FontFamily);
	Writer->WriteValue(TEXT("fontWeight"), Settings.FontWeight);
	Writer->WriteValue(TEXT("fontSize"), Settings.FontSize);
	Writer->WriteValue(TEXT("letterSpacing"), Settings.LetterSpacing);
	Writer->WriteValue(TEXT("maxWidthFraction"), Settings.MaxWidthFraction);
	Writer->WriteValue(TEXT("uppercase"), Settings.bUppercase);
	Writer->WriteValue(TEXT("color"), TEXT("#") + Settings.TextColor.ToHex());
	Writer->WriteObjectEnd();
	Writer->Close();
	View->Emit(TEXT("location-title"), Message, false);
}

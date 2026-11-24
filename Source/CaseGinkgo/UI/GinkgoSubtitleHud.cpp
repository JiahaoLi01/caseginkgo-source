#include "UI/GinkgoSubtitleHud.h"

#include "Control/GinkgoInputSettingsSubsystem.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Misc/Paths.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "SWeftViewport.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

namespace
{
	/** 淡出段（游戏秒）；发送给页面的 CSS 过渡也使用这个时长。 */
	constexpr double FadeSeconds = .20;
}

FGinkgoSubtitleHud::FGinkgoSubtitleHud(AGinkgoPlayerController& InController) : Controller(&InController) {}
FGinkgoSubtitleHud::~FGinkgoSubtitleHud() { Clear(); }

void FGinkgoSubtitleHud::Clear()
{
	if (auto* PC = Controller.Get())
	{
		if (PC->GetWorld()) { PC->GetWorldTimerManager().ClearTimer(NextChange); }
		if (Widget) { PC->RemoveGameplayHudWidget(Widget.ToSharedRef()); }
	}
	Entries.Reset();
	Widget.Reset();
	View.Reset();
}

bool FGinkgoSubtitleHud::OpenPage()
{
	auto* PC = Controller.Get();
	auto* Weft = UWeftSubsystem::Get();
	auto* Viewport = PC ? PC->GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport || !Weft || !Weft->IsAvailable()) { return false; }
	FVector2D Pixels;
	Viewport->GetViewportSize(Pixels);
	if (Pixels.X < 2 || Pixels.Y < 2) { return false; }
	const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(Pixels.X, Pixels.Y));
	View.Reset(Weft->CreateView());
	if (!View) { return false; }
	View->Resize(Pixels / Dpi, Dpi);
	if (!View->LoadDirectory(FPaths::ProjectContentDir() / TEXT("UI/Subtitles"))) { return false; }
	Widget = SNew(SWeftViewport).View(View.Get()).Interactive(false);
	PC->AddGameplayHudWidget(Widget.ToSharedRef(), 9);
	return true;
}

void FGinkgoSubtitleHud::Push(uint32 SpeakerId, const FText& Speaker, const FGinkgoDialogueLine& Line)
{
	auto* PC = Controller.Get();
	if (!PC) { return; }
	Entries.RemoveAll([SpeakerId](const FEntry& Entry) { return Entry.Id == SpeakerId; });
	if (Entries.Num() == 2) { Entries.RemoveAt(0); }
	Entries.Add({SpeakerId, Speaker, Line.Text, PC->GetWorld()->GetTimeSeconds() + FMath::Max(.2f, Line.Duration)});
	Refresh();
}

void FGinkgoSubtitleHud::Refresh()
{
	auto* PC = Controller.Get();
	if (Entries.IsEmpty() || !PC) { return; }
	const auto* Settings = UGinkgoInputSettingsSubsystem::Get(PC);
	const double Now = PC->GetWorld()->GetTimeSeconds();
	Entries.RemoveAll([Now](const FEntry& Entry) { return Now >= Entry.FadeAt + FadeSeconds; });
	if (Entries.IsEmpty() || !Settings || !Settings->GetCurrent().bSubtitles || (!View && !OpenPage()))
	{
		Clear();
		return;
	}
	const auto& Style = Settings->GetCurrent();
	FString Json;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("speaker"), Style.bSubtitleSpeaker);
	Writer->WriteValue(TEXT("size"), Style.SubtitleSize);
	Writer->WriteValue(TEXT("backdrop"), Style.SubtitleBackdrop);
	Writer->WriteValue(TEXT("fadeMs"), FMath::RoundToInt(FadeSeconds * 1000.));
	Writer->WriteArrayStart(TEXT("lines"));
	double NextTime = TNumericLimits<double>::Max();
	for (const auto& Entry : Entries)
	{
		const bool bFading = Now >= Entry.FadeAt;
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("name"), Entry.Speaker.ToString());
		Writer->WriteValue(TEXT("text"), Entry.Text.ToString());
		Writer->WriteValue(TEXT("fading"), bFading);
		Writer->WriteObjectEnd();
		NextTime = FMath::Min(NextTime, bFading ? Entry.FadeAt + FadeSeconds : Entry.FadeAt);
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	View->Emit(TEXT("subtitles"), Json, true);
	PC->GetWorldTimerManager().SetTimer(NextChange, FTimerDelegate::CreateSP(AsShared(), &FGinkgoSubtitleHud::Refresh),
		FMath::Max(.001f, static_cast<float>(NextTime - Now)), false);
}

#include "UI/GinkgoCreditsHud.h"

#include "Control/GinkgoPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "SWeftViewport.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

FGinkgoCreditsHud::FGinkgoCreditsHud(AGinkgoPlayerController& InController) : Controller(&InController) {}
FGinkgoCreditsHud::~FGinkgoCreditsHud() { EndPresentation(); }

void FGinkgoCreditsHud::Push(const TArray<FGinkgoCredit>& Credits)
{
	const bool bWasActive = Queue.IsActive();
	if (Queue.Push(Credits, UGinkgoDeveloperSettings::Get()->Credits, FMath::Rand()) && !bWasActive) { BeginPresentation(); }
}

void FGinkgoCreditsHud::BeginPresentation()
{
	auto* PC = Controller.Get();
	auto* Weft = UWeftSubsystem::Get();
	if (PC && PC->GetWorld()->GetGameViewport() && Weft && Weft->IsAvailable())
	{
		View.Reset(Weft->CreateView());
		if (View)
		{
			Widget = SNew(SWeftViewport).View(View.Get()).Interactive(false);
			PC->AddGameplayHudWidget(Widget.ToSharedRef(), 8);
		}
	}
	TickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FGinkgoCreditsHud::OnWorldPostActorTick);
}

void FGinkgoCreditsHud::EndPresentation()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle);
	TickHandle.Reset();
	if (auto* PC = Controller.Get(); PC && Widget) { PC->RemoveGameplayHudWidget(Widget.ToSharedRef()); }
	Widget.Reset();
	View.Reset();
	bLoaded = false;
	SentSerial = 0;
}

void FGinkgoCreditsHud::OnWorldPostActorTick(UWorld* World, const ELevelTick TickType, float DeltaSeconds)
{
	const auto* PC = Controller.Get();
	if (!PC || World != PC->GetWorld() || TickType == LEVELTICK_TimeOnly) { return; }
	if (!World->IsPaused() && !PC->IsMenuOpen() && !PC->IsGameplayHudSuppressed())
	{
		const auto Step = Queue.Advance(static_cast<float>(FApp::GetDeltaTime()));
		if (Step == FGinkgoCreditsQueue::EStep::Begin) { ++Serial; }
		if (Step == FGinkgoCreditsQueue::EStep::Finish && !Queue.IsActive()) { EndPresentation(); return; }
	}
	UpdatePage();
}

void FGinkgoCreditsHud::UpdatePage()
{
	auto* PC = Controller.Get();
	if (!PC || !View || !Widget) { return; }
	FVector2D Pixels = FVector2D::ZeroVector;
	if (auto* Viewport = PC->GetWorld()->GetGameViewport()) { Viewport->GetViewportSize(Pixels); }
	if (Pixels.X < 2. || Pixels.Y < 2.) { return; }
	if (!bLoaded)
	{
		const float Dpi = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(Pixels.X, Pixels.Y));
		View->Resize(Pixels / Dpi, Dpi);
		bLoaded = View->LoadDirectory(FPaths::ProjectContentDir() / TEXT("UI/Credits"));
	}
	const EVisibility Visibility = PC->IsMenuOpen() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
	if (Widget->GetVisibility() != Visibility) { Widget->SetVisibility(Visibility); }
	if (!bLoaded) { return; }
	const bool bNewCredit = Queue.HasCurrent() && Serial != SentSerial;
	if (bNewCredit)
	{
		const auto& Settings = *Queue.GetSettings();
		FString Json;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("font"), Settings.FontFamily);
		Writer->WriteValue(TEXT("weight"), Settings.FontWeight);
		Writer->WriteValue(TEXT("roleSize"), Settings.RoleSize);
		Writer->WriteValue(TEXT("nameSize"), Settings.NameSize);
		Writer->WriteValue(TEXT("spacing"), Settings.LetterSpacing);
		Writer->WriteValue(TEXT("gap"), Settings.LineGap);
		Writer->WriteValue(TEXT("left"), Settings.Left);
		Writer->WriteValue(TEXT("bottom"), Settings.Bottom);
		Writer->WriteValue(TEXT("width"), Settings.MaxWidth);
		Writer->WriteValue(TEXT("blur"), Settings.Blur);
		Writer->WriteValue(TEXT("reveal"), Settings.Reveal);
		Writer->WriteValue(TEXT("color"), TEXT("#") + Settings.TextColor.ToHex());
		Writer->WriteArrayStart(TEXT("glyphs"));
		for (const auto& Glyph : Queue.GetGlyphs())
		{
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("text"), Glyph.Text);
			Writer->WriteValue(TEXT("row"), Glyph.Row);
			Writer->WriteValue(TEXT("italic"), Glyph.bItalic);
			Writer->WriteValue(TEXT("delay"), Glyph.Delay);
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();
		View->Emit(TEXT("credit"), Json, false);
		SentSerial = Serial;
	}
	const float RevealTime = Queue.GetRevealTime();
	const float Opacity = Queue.GetOpacity();
	const FVector2D Size = View->GetLogicalSize();
	if (!bNewCredit && RevealTime == SentRevealTime && Opacity == SentOpacity && Size == SentSize) { return; }
	FString Json;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("opacity"), Opacity);
	Writer->WriteValue(TEXT("viewportWidth"), Size.X);
	Writer->WriteValue(TEXT("viewportHeight"), Size.Y);
	Writer->WriteValue(TEXT("time"), RevealTime);
	Writer->WriteObjectEnd();
	Writer->Close();
	View->Emit(TEXT("credit-frame"), Json, true);
	SentRevealTime = RevealTime;
	SentOpacity = Opacity;
	SentSize = Size;
}

#include "UI/GinkgoVideoSettingsEdit.h"

#include "Dom/JsonObject.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Settings/GinkgoUpscaling.h"

namespace
{
	const TArray<FString> QualityNames = {TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("epic"), TEXT("cinematic")};
	const TArray<FString> WindowModes = {TEXT("fullscreen"), TEXT("borderless"), TEXT("windowed")};
	const TArray<FString> OffOn = {TEXT("off"), TEXT("on")};

	FString PixelSize(FIntPoint Size) { return FString::Printf(TEXT("%d × %d"), Size.X, Size.Y); }

	/** 引擎的单档比较包含渲染比例；菜单把它单独列出，手动改比例后仍应显示原画质档。 */
	FString QualityName(const Scalability::FQualityLevels& Current)
	{
		for (int32 Index = 0; Index < QualityNames.Num(); ++Index)
		{
			Scalability::FQualityLevels Preset;
			Preset.SetFromSingleQualityLevel(Index);
			Preset.ResolutionQuality = Current.ResolutionQuality;
			if (Preset == Current) { return QualityNames[Index]; }
		}
		return TEXT("custom");
	}
}

void FGinkgoVideoSettingsEdit::Open(UGameUserSettings& Settings, const FGinkgoUpscaling& Support, bool bEditorWindow)
{
	Rows.Reset();
	Resolutions.Reset();
	bCanSyncGeneratedFrames = Support.bFrameGenerationWithVSync;
	const auto Add = [this](const TCHAR* Id, TArray<FString> Options, const FString& Saved, const FString& Default, const FString& Reason = {})
	{
		Options.AddUnique(Saved);
		Rows.Add({Id, MoveTemp(Options), Saved, Saved, Default, Reason});
	};
	const FString Quality = QualityName(Settings.ScalabilityQuality);
	Add(TEXT("video.quality"), QualityNames, Quality, TEXT("epic"));
	const float Scale = Settings.ScalabilityQuality.ResolutionQuality;
	Add(TEXT("video.scale"), {TEXT("autoScale"), TEXT("50%"), TEXT("60%"), TEXT("67%"), TEXT("75%"), TEXT("85%"), TEXT("100%")},
		Scale > 0.f ? FString::SanitizeFloat(Scale, 0) + TEXT("%") : TEXT("autoScale"), TEXT("autoScale"));

	const FString WindowReason = bEditorWindow ? TEXT("PIE 的窗口尺寸由编辑器管理。") : FString();
	Add(TEXT("video.window"), WindowModes, WindowModes[int32(Settings.GetFullscreenMode())], TEXT("borderless"), WindowReason);
	FIntPoint Desktop = Settings.GetDesktopResolution();
	FIntPoint Current = Settings.GetScreenResolution();
	TArray<FIntPoint> Sizes;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Sizes);
	if (Desktop.GetMin() <= 0) { Desktop = Current.GetMin() > 0 ? Current : FIntPoint(1280, 720); }
	if (Current.GetMin() <= 0) { Current = Desktop; }
	Sizes.AddUnique(Current);
	Sizes.AddUnique(Desktop);
	Sizes.Sort([](FIntPoint A, FIntPoint B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	TArray<FString> Labels;
	for (FIntPoint Size : Sizes)
	{
		if (Size.GetMin() <= 0) { continue; }
		const FString Label = PixelSize(Size);
		if (!Resolutions.Contains(Label)) { Resolutions.Add(Label, Size); Labels.Add(Label); }
	}
	Add(TEXT("video.resolution"), MoveTemp(Labels), PixelSize(Current), PixelSize(Desktop), WindowReason);
	const float Limit = Settings.GetFrameRateLimit();
	Add(TEXT("video.limit"), {TEXT("unlimited"), TEXT("30"), TEXT("45"), TEXT("60"), TEXT("90"), TEXT("120"), TEXT("144"), TEXT("165"), TEXT("240")},
		Limit > 0.f ? FString::SanitizeFloat(Limit, 0) : TEXT("unlimited"), TEXT("unlimited"));
	Add(TEXT("video.vsync"), OffOn, Settings.IsVSyncEnabled() ? TEXT("on") : TEXT("off"), TEXT("off"));
	const FString SR = FGinkgoUpscaling::ReadPreference(TEXT("SuperResolution"));
	const FString FG = FGinkgoUpscaling::ReadPreference(TEXT("FrameGeneration"));
	const FString DefaultSR = Support.SuperResolution.Contains(TEXT("quality")) ? TEXT("quality") : TEXT("off");
	Add(TEXT("video.dlss"), Support.SuperResolution, Support.SuperResolution.Contains(SR) ? SR : DefaultSR, DefaultSR, Support.SuperResolutionReason);
	Add(TEXT("video.frames"), Support.FrameGeneration,
		Support.FrameGeneration.Contains(FG) && (bCanSyncGeneratedFrames || !Settings.IsVSyncEnabled()) ? FG : TEXT("off"), TEXT("off"), Support.FrameGenerationReason);
}

const FGinkgoVideoSettingsEdit::FRow& FGinkgoVideoSettingsEdit::Row(const TCHAR* Id) const
{
	return *Rows.FindByPredicate([Id](const FRow& Entry) { return Entry.Id == Id; });
}

FString FGinkgoVideoSettingsEdit::DisabledReason(const FRow& Entry) const
{
	if (!Entry.Unavailable.IsEmpty()) { return Entry.Unavailable; }
	if (Entry.Id == TEXT("video.scale") && Row(TEXT("video.dlss")).Value != TEXT("off")) { return TEXT("渲染比例由 DLSS 档位决定。"); }
	if (Entry.Id == TEXT("video.resolution") && Row(TEXT("video.window")).Value == TEXT("borderless")) { return TEXT("无边框窗口使用桌面分辨率。"); }
	if (!bCanSyncGeneratedFrames)
	{
		if (Entry.Id == TEXT("video.frames") && Row(TEXT("video.vsync")).Value == TEXT("on")) { return TEXT("请先关闭垂直同步。"); }
		if (Entry.Id == TEXT("video.vsync") && Row(TEXT("video.frames")).Value != TEXT("off")) { return TEXT("当前驱动要求先关闭帧生成。"); }
	}
	return {};
}

bool FGinkgoVideoSettingsEdit::Set(const FString& Id, const FString& Value)
{
	FRow* Entry = Rows.FindByPredicate([&Id](const FRow& Candidate) { return Candidate.Id == Id; });
	if (!Entry || !DisabledReason(*Entry).IsEmpty() || !Entry->Options.Contains(Value)) { return false; }
	Entry->Value = Value;
	return true;
}

void FGinkgoVideoSettingsEdit::Reset(const FString& Id)
{
	for (FRow& Entry : Rows)
	{
		if (Entry.Id == Id && DisabledReason(Entry).IsEmpty()) { Entry.Value = Entry.Default; }
	}
}

void FGinkgoVideoSettingsEdit::ResetAll()
{
	for (FRow& Entry : Rows)
	{
		if (Entry.Unavailable.IsEmpty()) { Entry.Value = Entry.Default; }
	}
}

void FGinkgoVideoSettingsEdit::Revert()
{
	for (FRow& Entry : Rows) { Entry.Value = Entry.Saved; }
}

int32 FGinkgoVideoSettingsEdit::CountChanges() const
{
	int32 Count = 0;
	for (const FRow& Entry : Rows) { Count += Entry.Saved != Entry.Value; }
	return Count;
}

void FGinkgoVideoSettingsEdit::AppendRows(TArray<TSharedPtr<FJsonValue>>& Out, bool bIncludeDefinition) const
{
	for (const FRow& Entry : Rows)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("id"), Entry.Id);
		Object->SetStringField(TEXT("saved"), Entry.Saved);
		Object->SetStringField(TEXT("value"), Entry.Value);
		Object->SetStringField(TEXT("disabled"), DisabledReason(Entry));
		if (Entry.Id == TEXT("video.resolution"))
		{
			// 无边框始终铺满桌面；草稿保留窗口尺寸，切回窗口时继续用之前的选择。
			Object->SetStringField(TEXT("displayValue"), Row(TEXT("video.window")).Value == TEXT("borderless") ? Entry.Default : Entry.Value);
		}
		if (bIncludeDefinition)
		{
			Object->SetStringField(TEXT("dir"), TEXT("video"));
			Object->SetStringField(TEXT("kind"), TEXT("choice"));
			Object->SetStringField(TEXT("def"), Entry.Default);
			TArray<TSharedPtr<FJsonValue>> Options;
			for (const FString& Value : Entry.Options) { Options.Add(MakeShared<FJsonValueString>(Value)); }
			Object->SetArrayField(TEXT("options"), Options);
		}
		Out.Add(MakeShared<FJsonValueObject>(Object));
	}
}

void FGinkgoVideoSettingsEdit::Apply(UGameUserSettings& Settings, const UWorld* World) const
{
	if (!CountChanges()) { return; }
	const auto Changed = [this](const TCHAR* Id) { const FRow& Entry = Row(Id); return Entry.Saved != Entry.Value; };
	const bool bWindowChanged = Changed(TEXT("video.window")) || Changed(TEXT("video.resolution"));
	if (bWindowChanged)
	{
		Settings.SetFullscreenMode(EWindowMode::Type(WindowModes.IndexOfByKey(Row(TEXT("video.window")).Value)));
		Settings.SetScreenResolution(Resolutions.FindChecked(Row(TEXT("video.resolution")).Value));
		Settings.ApplyResolutionSettings(false);
		Settings.ConfirmVideoMode();
	}
	if (Changed(TEXT("video.quality")))
	{
		Settings.SetOverallScalabilityLevel(QualityNames.IndexOfByKey(Row(TEXT("video.quality")).Value));
	}
	if (Changed(TEXT("video.scale")) || Changed(TEXT("video.quality")) || bWindowChanged)
	{
		const FString& Scale = Row(TEXT("video.scale")).Value;
		if (Scale == TEXT("autoScale")) { Settings.ScalabilityQuality.ResolutionQuality = 0.f; }
		else { Settings.SetResolutionScaleValueEx(FCString::Atof(*Scale)); }
	}
	if (Changed(TEXT("video.vsync"))) { Settings.SetVSyncEnabled(Row(TEXT("video.vsync")).Value == TEXT("on")); }
	if (Changed(TEXT("video.limit"))) { Settings.SetFrameRateLimit(FCString::Atof(*Row(TEXT("video.limit")).Value)); }
	if (Changed(TEXT("video.dlss"))) { FGinkgoUpscaling::SetPreference(TEXT("SuperResolution"), Row(TEXT("video.dlss")).Value); }
	if (Changed(TEXT("video.frames"))) { FGinkgoUpscaling::SetPreference(TEXT("FrameGeneration"), Row(TEXT("video.frames")).Value); }
	Settings.ApplyNonResolutionSettings();
	if (Changed(TEXT("video.scale")) && Row(TEXT("video.scale")).Value == TEXT("autoScale"))
	{
		// UE 对 sg.ResolutionQuality=0 不写 r.ScreenPercentage。显式放回自动值，否则上一次的手动比例会继续生效。
		if (IConsoleVariable* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			Scale->Set(0.f, ECVF_SetByScalability);
		}
	}
	Settings.SaveSettings();
	FGinkgoUpscaling::ApplySaved(World);
}

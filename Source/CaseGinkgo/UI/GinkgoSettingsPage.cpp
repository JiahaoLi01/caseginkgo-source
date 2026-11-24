#include "UI/GinkgoSettingsPage.h"

#include "Character/GinkgoCharacter.h"
#include "Control/GinkgoInputSettings.h"
#include "Control/GinkgoInputSettingsSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Settings/GinkgoUpscaling.h"
#include "Engine/UserInterfaceSettings.h"                // 视口的 DPI 缩放：页面第一次布局用的大小
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UI/GinkgoUiSounds.h"
#include "SWeftViewport.h"
#include "UI/GinkgoKeyCapture.h"
#include "UI/GinkgoUiTheme.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "WeftSubsystem.h"
#include "WeftView.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/SOverlay.h"

namespace
{
	/** 页面的目录：Content/UI 随包（DefaultGame.ini 的 DirectoriesToAlwaysStageAsUFS），打包后从 pak 读 */
	const TCHAR* SettingsPageDirectory = TEXT("UI/Settings");

	/** 在别的界面之上（武器栏 5、交互的提示 10、窗口时间轴 20、调参面板 30、暂停菜单 40） */
	constexpr int32 SettingsWidgetZOrder = 45;

	/** 设置页底层的 Slate 背景模糊强度（逻辑半径）；为暗色半透明页面提供柔和景深 */
	constexpr float SettingsBlurStrength = 9.0f;

	/** 父级暂停菜单的 Slate 背景模糊强度（逻辑半径），用于进出设置子页时无缝衔接，避免模糊闪断 */
	constexpr float ParentPauseBlurStrength = 8.0f;

	/** 独立打开设置页时的背景模糊入场时长（秒） */
	constexpr float SettingsEnterSeconds = 0.22f;

	/** 由暂停菜单进入设置页时的背景模糊微调时长（秒） */
	constexpr float SettingsFromPauseEnterSeconds = 0.18f;

	/** 关闭设置页时的背景模糊退场时长（秒），与页面 CSS 退场动画同步 */
	constexpr float SettingsExitSeconds = 0.15f;

	/** 三次缓出曲线：起步利落、收尾柔和，适配短时长 UI 开合过渡 */
	float EaseOutCubic(const float Alpha)
	{
		const float Inv = 1.0f - FMath::Clamp(Alpha, 0.0f, 1.0f);
		return 1.0f - Inv * Inv * Inv;
	}

	/** 页面里目录的名字（catalog.js 的 id，也是存盘的名字的前缀） */
	const TCHAR* DirectoryName(const EGinkgoInputDirectory Directory)
	{
		switch (Directory)
		{
		case EGinkgoInputDirectory::Gamepad: return TEXT("gamepad");
		case EGinkgoInputDirectory::KeyboardMouse: return TEXT("mouse");
		case EGinkgoInputDirectory::Accessibility: return TEXT("accessibility");
		}
		return TEXT("");
	}

	/** 一项的值写成 JSON：数是数，开关是 true、false，选择是名字 */
	TSharedRef<FJsonValue> ValueToJson(const FGinkgoInputSettingDef& Def, const FGinkgoInputSettings& Settings)
	{
		const double Value = Def.Get(Settings);
		switch (Def.Kind)
		{
		case FGinkgoInputSettingDef::EKind::Toggle:
			return MakeShared<FJsonValueBoolean>(Value != 0.0);
		case FGinkgoInputSettingDef::EKind::Choice:
			return MakeShared<FJsonValueString>(Def.Options[FMath::RoundToInt32(Value)]);
		default:
			return MakeShared<FJsonValueNumber>(Value);
		}
	}

	/** 页面给的值换成 double；读不懂时为假 */
	bool ValueFromJson(const FGinkgoInputSettingDef& Def, const FJsonValue& Value, double& OutValue)
	{
		bool bFlag = false;
		FString Name;
		switch (Def.Kind)
		{
		case FGinkgoInputSettingDef::EKind::Toggle:
			if (Value.TryGetBool(bFlag))
			{
				OutValue = bFlag ? 1.0 : 0.0;
				return true;
			}
			return false;
		case FGinkgoInputSettingDef::EKind::Choice:
			if (Value.TryGetString(Name) && Def.Options.Contains(Name))
			{
				OutValue = Def.Options.IndexOfByKey(Name);
				return true;
			}
			return false;
		default:
			return Value.TryGetNumber(OutValue);
		}
	}

	FString ToJsonText(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json));
		return Json;
	}

	/** 键在文件与页面里的名字：EKeys 的名字，空的键是 "" */
	FString KeyName(const FKey& Key)
	{
		return Key.IsValid() ? Key.GetFName().ToString() : FString();
	}

	const TCHAR* ModeName(const EGinkgoHoldMode Mode)
	{
		return Mode == EGinkgoHoldMode::Toggle ? TEXT("toggle") : TEXT("hold");
	}

	/** 页面没有中文名的键（catalog.js 的 KEYS 没写的）显示引擎的名字 */
	void AddKeyLabel(FJsonObject& Labels, const FKey& Key)
	{
		if (Key.IsValid() && !Labels.HasField(KeyName(Key)))
		{
			Labels.SetStringField(KeyName(Key), Key.GetDisplayName(false).ToString());
		}
	}

	/** 键位的两行（键、方式）写进页面的表：键的行名就是键位的名字，方式的行名后面加 .mode */
	void AddKeyRows(TArray<TSharedPtr<FJsonValue>>& Rows, const FGinkgoKeyBindingDef& Def, const FGinkgoKeyBinding& Current, const FGinkgoKeyBinding& Saved, const FGinkgoKeyBinding* Default)
	{
		const auto Row = [&Rows, Default](const FString& Id, const FString& Value, const FString& SavedValue, const FString& DefaultValue, const TCHAR* Kind, const TCHAR* Directory)
		{
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("id"), Id);
			Object->SetStringField(TEXT("value"), Value);
			Object->SetStringField(TEXT("saved"), SavedValue);
			if (Default)
			{
				Object->SetStringField(TEXT("dir"), Directory);
				Object->SetStringField(TEXT("kind"), Kind);
				Object->SetStringField(TEXT("def"), DefaultValue);
			}
			Rows.Add(MakeShared<FJsonValueObject>(Object));
		};
		const TCHAR* Directory = DirectoryName(Def.Directory);
		Row(Def.Id, KeyName(Current.Key), KeyName(Saved.Key), Default ? KeyName(Default->Key) : FString(), TEXT("key"), Directory);
		if (Def.bHasMode)
		{
			Row(Def.Id + TEXT(".mode"), ModeName(Current.Mode), ModeName(Saved.Mode), Default ? ModeName(Default->Mode) : FString(), TEXT("mode"), Directory);
		}
	}

	TArray<const FGinkgoInputSettingDef*> AllDefs()
	{
		using namespace UE::Query;
		return Ginkgo::Input::GetSettingDefs()
			| Select([](const FGinkgoInputSettingDef& Def) { return &Def; })
			| ToArray();
	}
}

FGinkgoSettingsPage::FGinkgoSettingsPage(APlayerController& InController)
	: Controller(&InController)
{
}

FGinkgoSettingsPage::~FGinkgoSettingsPage()
{
	Hide();
}

FKey FGinkgoSettingsPage::GetToggleKey()
{
	// F1–F5、F9、F11 是引擎在开发版里的调试键（BaseInput.ini 的 DebugExecBindings），F6 窗口时间轴，F7 调参面板，F8 是编辑器里 PIE 的弹出
	return EKeys::F10;
}

UGinkgoInputSettingsSubsystem* FGinkgoSettingsPage::GetSettings() const
{
	return UGinkgoInputSettingsSubsystem::Get(Controller.Get());
}

bool FGinkgoSettingsPage::Show(const bool bInFromGamepad, TFunction<void()> InOnClosed, const float InParentBlurStrength)
{
	if (IsShown())
	{
		return true;
	}
	APlayerController* PlayerController = Controller.Get();
	const UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	UWeftSubsystem* Weft = UWeftSubsystem::Get();
	UWeftView* NewView = Viewport && Weft && Weft->IsAvailable() && GetSettings() ? Weft->CreateView() : nullptr;
	if (!NewView)
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[Settings] 打不开设置界面：%s"), !Viewport ? TEXT("没有游戏视口（无界面运行）") : TEXT("weft 在这里不运行（-nullrhi，或没有编出 weft 的运行时库）"));
		return false;
	}
	Weft->AddEffects(FPaths::ProjectContentDir() / TEXT("UI/Effects/effects.json"));
	View.Reset(NewView);
	if (UGameUserSettings* Machine = GEngine->GetGameUserSettings())
	{
		Video.Open(*Machine, FGinkgoUpscaling::Query(World), World->WorldType == EWorldType::PIE);
	}
	ViewportClient = Viewport;
	bFromGamepad = bInFromGamepad;
	bCursorWasShown = PlayerController->ShouldShowMouseCursor();
	bHideRequested = false;
	bClosedByConfirm = false;
	bClosing = false;
	bSuppressCloseSound = false;
	BlurTransitionDuration = 0.f;
	OnClosedCallback = MoveTemp(InOnClosed);
	const bool bFromParentMenu = static_cast<bool>(OnClosedCallback);
	ParentBlurBaseline = bFromParentMenu ? (InParentBlurStrength >= 0.f ? InParentBlurStrength : ParentPauseBlurStrength) : 0.f;
	const float InitialBlur = ParentBlurBaseline;
	const float EnterSeconds = bFromParentMenu ? SettingsFromPauseEnterSeconds : SettingsEnterSeconds;
	MessageHandle = NewView->OnMessage.AddRaw(this, &FGinkgoSettingsPage::OnMessage);
	Widget = SNew(SWeftViewport).View(NewView);
	RootWidget = SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(BlurWidget, SBackgroundBlur)
			.Padding(FMargin(0.f))
			.BlurStrength(InitialBlur)
			.bApplyAlphaToBlur(true)
		]
		+ SOverlay::Slot()
		[
			Widget.ToSharedRef()
		];
	StartBlurTransition(InitialBlur, SettingsBlurStrength, EnterSeconds);
	Viewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), SettingsWidgetZOrder);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FGinkgoSettingsPage::OnTick));
	// 按着的键放掉：之后这些键的事件都到页面，松开的事件游戏收不到。Enhanced Input 这一帧发 Completed
	PlayerController->FlushPressedKeys();
	ApplyInputMode();
	UE_LOG(LogGinkgoUI, Log, TEXT("[Settings] 打开设置界面（%s）"), bFromGamepad ? TEXT("手柄") : TEXT("键盘"));
	UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Open);
	return true;
}

void FGinkgoSettingsPage::Hide()
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
	EndKeyCapture();
	const bool bWorldTearingDown = !Controller.IsValid() || !Controller->GetWorld() || Controller->GetWorld()->bIsTearingDown;
	if (const APlayerController* Owner = Controller.Get(); !bClosedByConfirm && !bSuppressCloseSound && !bWorldTearingDown)
	{
		UGinkgoUiSoundSubsystem::Play(Owner, EGinkgoUiSound::Close);
	}
	bClosedByConfirm = false;
	bSuppressCloseSound = false;
	// 没确认的改动放弃：确认过的时候 Current 与 Saved 一样，这里什么也不变
	if (UGinkgoInputSettingsSubsystem* Settings = GetSettings(); Settings && Ginkgo::Input::CountDifferences(Settings->GetCurrent(), Settings->GetSaved()) > 0)
	{
		Settings->RevertToSaved();
	}
	if (UGameViewportClient* Viewport = ViewportClient.Get(); Viewport && RootWidget)
	{
		Viewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
	}
	TFunction<void()> ClosedCallback = MoveTemp(OnClosedCallback);
	OnClosedCallback.Reset();
	if (!ClosedCallback)
	{
		if (APlayerController* PlayerController = Controller.Get(); PlayerController && PlayerController->GetWorld() && PlayerController->GetWorld()->GetGameViewport())
		{
			if (bCursorWasShown)
			{
				// 调参面板开着：还给它（同它自己设的模式）
				FInputModeGameAndUI InputMode;
				InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
				InputMode.SetHideCursorDuringCapture(false);
				PlayerController->SetInputMode(InputMode);
			}
			else
			{
				PlayerController->SetInputMode(FInputModeGameOnly());
				PlayerController->SetShowMouseCursor(false);
			}
		}
	}
	RootWidget.Reset();
	BlurWidget.Reset();
	Widget.Reset();
	View.Reset();
	ViewportClient.Reset();
	bLoaded = false;
	bHideRequested = false;
	bClosing = false;
	BlurTransitionStart = 0.0;
	BlurFromStrength = 0.f;
	BlurToStrength = 0.f;
	BlurCurrentStrength = 0.f;
	BlurTransitionDuration = 0.f;
	ParentBlurBaseline = 0.f;
	UE_LOG(LogGinkgoUI, Log, TEXT("[Settings] 关闭设置界面"));
	if (ClosedCallback && !bWorldTearingDown)
	{
		ClosedCallback();
	}
}

void FGinkgoSettingsPage::BeginClosing()
{
	if (!IsShown() || bClosing)
	{
		return;
	}
	bClosing = true;
	bHideRequested = false;
	EndKeyCapture();

	const bool bWorldTearingDown = !Controller.IsValid() || !Controller->GetWorld() || Controller->GetWorld()->bIsTearingDown;
	if (!bClosedByConfirm)
	{
		if (UGinkgoInputSettingsSubsystem* Settings = GetSettings(); Settings && Ginkgo::Input::CountDifferences(Settings->GetCurrent(), Settings->GetSaved()) > 0)
		{
			Settings->RevertToSaved();
		}
		if (const APlayerController* Owner = Controller.Get(); !bWorldTearingDown)
		{
			UGinkgoUiSoundSubsystem::Play(Owner, EGinkgoUiSound::Close);
		}
	}
	bSuppressCloseSound = true;

	if (bLoaded && View)
	{
		View->Emit(TEXT("settings-closing"), TEXT("{}"));
	}
	const float TargetBlur = OnClosedCallback ? ParentBlurBaseline : 0.f;
	StartBlurTransition(BlurCurrentStrength, TargetBlur, SettingsExitSeconds);
}

void FGinkgoSettingsPage::StartBlurTransition(const float FromStrength, const float ToStrength, const float DurationSeconds)
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

bool FGinkgoSettingsPage::UpdateBlurTransition()
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
	BlurCurrentStrength = FMath::Lerp(BlurFromStrength, BlurToStrength, EaseOutCubic(Alpha));
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

void FGinkgoSettingsPage::ApplyInputMode()
{
	APlayerController* PlayerController = Controller.Get();
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

bool FGinkgoSettingsPage::OnTick(const float DeltaSeconds)
{
	const APlayerController* PlayerController = Controller.Get();
	UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	if (!PlayerController || !World || !IsShown())
	{
		return false;
	}
	const bool bBlurDone = UpdateBlurTransition();
	if (bHideRequested && !bClosing)
	{
		BeginClosing();
		return true;
	}
	if (bClosing)
	{
		if (bBlurDone)
		{
			if (bClosedByConfirm && GEngine && GEngine->GetGameUserSettings())
			{
				Video.Apply(*GEngine->GetGameUserSettings(), World);
			}
			Hide();
			return false;
		}
		return true;
	}
	UGameViewportClient* Viewport = ViewportClient.Get();
	// 别的界面（调参面板的 F7）改了输入模式时要回来：设置界面开着时输入是它的
	if (Viewport && !Viewport->IgnoreInput())
	{
		ApplyInputMode();
	}
	// 视口有了大小才载入：View 一开始是 1 × 1，页面第一次布局在那么窄的宽度上，文字会折成很长一列
	FVector2D Pixels = FVector2D::ZeroVector;
	if (Viewport)
	{
		Viewport->GetViewportSize(Pixels);
	}
	if (bLoaded || Pixels.X < 2.0 || Pixels.Y < 2.0)
	{
		return true;
	}
	const float DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y)));
	View->Resize(Pixels / DPIScale, DPIScale);
	const FString Directory = FPaths::ProjectContentDir() / SettingsPageDirectory;
	if (!Ginkgo::UI::LoadThemedPage(*View, Directory))
	{
		UE_LOG(LogGinkgoUI, Warning, TEXT("[Settings] 页面载入失败：%s（原因见 LogGinkgoUI、LogWeft）"), *Directory);
		Hide();
		return false;
	}
	bLoaded = true;
	return true;
}

void FGinkgoSettingsPage::OnMessage(const FString& Name, const FString& JsonValue, const uint64 CallId)
{
	UGinkgoInputSettingsSubsystem* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}
	TSharedPtr<FJsonValue> Value;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonValue), Value);
	const TSharedPtr<FJsonObject>* Object = nullptr;
	const bool bObject = Value.IsValid() && Value->TryGetObject(Object);
	FString Id;
	const FGinkgoInputSettingDef* Def = bObject && (*Object)->TryGetStringField(TEXT("id"), Id) ? Ginkgo::Input::FindSettingDef(Id) : nullptr;
	const int32 KeyIndex = Ginkgo::Input::FindKeyBinding(Id);
	const int32 ModeIndex = Id.EndsWith(TEXT(".mode")) ? Ginkgo::Input::FindKeyBinding(Id.LeftChop(5)) : INDEX_NONE;
	if (Name == TEXT("ready"))
	{
		SendTable();
	}
	else if (Name == TEXT("state"))
	{
		PageState = JsonValue;
	}
	else if (Name == TEXT("sound"))
	{
		UGinkgoUiSoundSubsystem::PlayFromPage(Controller.Get(), JsonValue);
	}
	else if (Name == TEXT("log"))
	{
		// 页面自己的诊断（发行版里的页面没有 console）
		UE_LOG(LogGinkgoUI, Log, TEXT("[Settings] 页面：%s"), *JsonValue);
	}
	else if (bClosing)
	{
		return;
	}
	else if (Name == TEXT("capture") && KeyIndex != INDEX_NONE)
	{
		StartKeyCapture(KeyIndex);
	}
	else if (Name == TEXT("captureCancel"))
	{
		EndKeyCapture();
	}
	else if (Name == TEXT("assignKey") && KeyIndex != INDEX_NONE)
	{
		// 撞了键以后玩家选了互换
		Settings->SetKey(KeyIndex, FKey(*(*Object)->GetStringField(TEXT("key"))));
		UE_LOG(LogGinkgoUI, Log, TEXT("[Settings] 互换：%s = %s"), *Id, *KeyName(Settings->GetCurrent().Keys[KeyIndex].Key));
		SendValues({}, FString(), true);
	}
	else if (Name == TEXT("set") && ModeIndex != INDEX_NONE)
	{
		Settings->SetKeyMode(ModeIndex, (*Object)->GetStringField(TEXT("value")) == TEXT("toggle") ? EGinkgoHoldMode::Toggle : EGinkgoHoldMode::Hold);
		SendValues({}, FString(), true);
	}
	else if (Id.StartsWith(TEXT("video.")) && (Name == TEXT("set") || Name == TEXT("reset")))
	{
		FString Choice;
		if (Name == TEXT("reset")) { Video.Reset(Id); }
		else if ((*Object)->TryGetStringField(TEXT("value"), Choice)) { Video.Set(Id, Choice); }
		SendValues({}, FString(), false, true);
	}
	else if (Name == TEXT("reset") && !Def && bObject)
	{
		if (Settings->ResetKeyRow(Id))
		{
			SendValues({}, FString(), true);
		}
	}
	else if (Name == TEXT("set") && Def)
	{
		const TSharedPtr<FJsonValue> NewValue = (*Object)->TryGetField(TEXT("value"));
		double Number = 0.0;
		if (NewValue && ValueFromJson(*Def, *NewValue, Number))
		{
			Settings->SetValue(Def->Id, Number);
		}
		else
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Settings] %s：读不懂 %s"), *Def->Id, *JsonValue);
		}
		SendValues({Def}, FString());
	}
	else if (Name == TEXT("reset") && Def)
	{
		Settings->ResetValue(Def->Id);
		SendValues({Def}, FString());
	}
	else if (Name == TEXT("resetDirectory") && bObject)
	{
		const FString DirectoryId = (*Object)->GetStringField(TEXT("dir"));
		if (DirectoryId == TEXT("video"))
		{
			Video.ResetAll();
			SendValues({}, FString(), false, true);
			return;
		}
		for (const EGinkgoInputDirectory Directory : {EGinkgoInputDirectory::Gamepad, EGinkgoInputDirectory::KeyboardMouse, EGinkgoInputDirectory::Accessibility})
		{
			if (DirectoryId == DirectoryName(Directory))
			{
				Settings->ResetDirectory(Directory);
				break;
			}
		}
		SendValues(AllDefs(), FString(), true);
	}
	else if (Name == TEXT("revert"))
	{
		Settings->RevertToSaved();
		Video.Revert();
		SendValues(AllDefs(), FString(), true, true);
	}
	else if (Name == TEXT("confirm"))
	{
		FString Error;
		if (Settings->Confirm(Error))
		{
			// 不在这里关：消息是页面在 SWeftViewport::Tick 里发的，而 Slate 是在画控件的那一趟里 Tick 它的（同调参面板，2026-11-01 崩过）
			bHideRequested = true;
			bClosedByConfirm = true;
			UGinkgoUiSoundSubsystem::Play(Controller.Get(), EGinkgoUiSound::Confirm);
		}
		else
		{
			UGinkgoUiSoundSubsystem::Play(Controller.Get(), EGinkgoUiSound::Invalid);
			// 按钮旁边只放得下一句，完整的路径在日志里（UGinkgoInputSettingsSubsystem::Confirm）
			SendValues(AllDefs(), TEXT("没存下来：写不进 InputSettings.json"), true);
		}
	}
	else if (Name == TEXT("close"))
	{
		bHideRequested = true;
	}
}

void FGinkgoSettingsPage::StartKeyCapture(const int32 Index)
{
	EndKeyCapture();
	KeyCaptureIndex = Index;
	KeyCapture = MakeShared<FGinkgoKeyCapture>(FGinkgoKeyCapture::FOnKey::CreateRaw(this, &FGinkgoSettingsPage::OnCapturedKey));
	// 排在最前面：weft 的手柄输入也是一个预处理器，它看到手柄的键就交给页面了
	FSlateApplication::Get().RegisterInputPreProcessor(KeyCapture, 0);
}

void FGinkgoSettingsPage::EndKeyCapture()
{
	if (KeyCapture)
	{
		// 在它自己的回调里拿掉也行：Slate 遍历时记下来，遍历完再拿。退出游戏时 Slate 可能已经没了
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(KeyCapture);
		}
		KeyCapture.Reset();
	}
	KeyCaptureIndex = INDEX_NONE;
}

void FGinkgoSettingsPage::OnCapturedKey(const FKey& Key)
{
	UGinkgoInputSettingsSubsystem* Settings = GetSettings();
	if (!Settings || !View || KeyCaptureIndex == INDEX_NONE)
	{
		return;
	}
	const int32 Index = KeyCaptureIndex;
	const FGinkgoKeyBindingDef& Def = Ginkgo::Input::GetKeyBindingDefs()[Index];
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("id"), Def.Id);
	Result->SetStringField(TEXT("key"), KeyName(Key));
	Result->SetStringField(TEXT("label"), Key.GetDisplayName(false).ToString());
	const auto Finish = [this, &Result](const TCHAR* Outcome)
	{
		Result->SetStringField(TEXT("result"), Outcome);
		EndKeyCapture();
		View->Emit(TEXT("capture"), ToJsonText(Result));
	};
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		Finish(TEXT("cancel"));
		return;
	}
	if (Key == EKeys::Delete || Key == EKeys::Gamepad_Special_Left)
	{
		Settings->SetKey(Index, EKeys::Invalid);
		UE_LOG(LogGinkgoUI, Log, TEXT("[Settings] 改键：%s 清空"), *Def.Id);
		Finish(TEXT("clear"));
		SendValues({}, FString(), true);
		return;
	}
	switch (Ginkgo::Input::CheckKey(Def.Directory, Key))
	{
	case EGinkgoKeyCheck::NotAButton:
		// 推一下摇杆不算，接着等
		return;
	case EGinkgoKeyCheck::WrongDevice:
		// 手柄的键位上点了鼠标，是不想改了；别的（按错了设备）说一声，接着等
		if (Key.IsMouseButton())
		{
			Finish(TEXT("cancel"));
		}
		else
		{
			Result->SetStringField(TEXT("result"), TEXT("wrongDevice"));
			View->Emit(TEXT("capture"), ToJsonText(Result));
		}
		return;
	case EGinkgoKeyCheck::Reserved:
		Result->SetStringField(TEXT("result"), TEXT("reserved"));
		View->Emit(TEXT("capture"), ToJsonText(Result));
		return;
	case EGinkgoKeyCheck::Ok:
		break;
	}
	if (Key == Settings->GetCurrent().Keys[Index].Key)
	{
		Finish(TEXT("cancel"));
		return;
	}
	if (const int32 Conflict = Ginkgo::Input::FindKeyConflict(Settings->GetCurrent(), Index, Key); Conflict != INDEX_NONE)
	{
		// 不直接互换：先问。页面点了互换再发 assignKey
		Result->SetStringField(TEXT("conflict"), Ginkgo::Input::GetKeyBindingDefs()[Conflict].Id);
		Finish(TEXT("conflict"));
		return;
	}
	Settings->SetKey(Index, Key);
	UE_LOG(LogGinkgoUI, Log, TEXT("[Settings] 改键：%s = %s"), *Def.Id, *KeyName(Key));
	Finish(TEXT("done"));
	SendValues({}, FString(), true);
}

void FGinkgoSettingsPage::SendTable() const
{
	const UGinkgoInputSettingsSubsystem* Settings = GetSettings();
	if (!Settings || !View)
	{
		return;
	}
	const FGinkgoInputSettings Defaults;
	TArray<TSharedPtr<FJsonValue>> Rows;
	Video.AppendRows(Rows, true);
	for (const FGinkgoInputSettingDef& Def : Ginkgo::Input::GetSettingDefs())
	{
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), Def.Id);
		Row->SetStringField(TEXT("dir"), DirectoryName(Def.Directory));
		Row->SetStringField(TEXT("kind"), Def.Kind == FGinkgoInputSettingDef::EKind::Toggle ? TEXT("bool") : Def.Kind == FGinkgoInputSettingDef::EKind::Choice ? TEXT("choice") : TEXT("number"));
		Row->SetField(TEXT("value"), ValueToJson(Def, Settings->GetCurrent()));
		Row->SetField(TEXT("def"), ValueToJson(Def, Defaults));
		Row->SetField(TEXT("saved"), ValueToJson(Def, Settings->GetSaved()));
		if (Def.Kind == FGinkgoInputSettingDef::EKind::Number)
		{
			Row->SetNumberField(TEXT("min"), Def.Min);
			Row->SetNumberField(TEXT("max"), Def.Max);
			Row->SetNumberField(TEXT("step"), Def.Step);
		}
		if (Def.Kind == FGinkgoInputSettingDef::EKind::Choice)
		{
			TArray<TSharedPtr<FJsonValue>> Options;
			for (const FString& Option : Def.Options)
			{
				Options.Add(MakeShared<FJsonValueString>(Option));
			}
			Row->SetArrayField(TEXT("options"), Options);
		}
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	const TSharedRef<FJsonObject> KeyLabels = MakeShared<FJsonObject>();
	const TArray<FGinkgoKeyBindingDef>& KeyDefs = Ginkgo::Input::GetKeyBindingDefs();
	for (int32 Index = 0; Index < KeyDefs.Num(); ++Index)
	{
		AddKeyRows(Rows, KeyDefs[Index], Settings->GetCurrent().Keys[Index], Settings->GetSaved().Keys[Index], &Defaults.Keys[Index]);
		AddKeyLabel(*KeyLabels, Settings->GetCurrent().Keys[Index].Key);
		AddKeyLabel(*KeyLabels, Settings->GetSaved().Keys[Index].Key);
		AddKeyLabel(*KeyLabels, Defaults.Keys[Index].Key);
	}
	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("rows"), Rows);
	Payload->SetObjectField(TEXT("keyLabels"), KeyLabels);
	Payload->SetStringField(TEXT("state"), PageState);
	Payload->SetBoolField(TEXT("fromGamepad"), bFromGamepad);
	Payload->SetStringField(TEXT("key"), GetToggleKey().GetDisplayName().ToString());
	Payload->SetStringField(TEXT("file"), Settings->GetFilePath());
	// 这把枪的开镜缩放：预览按它算开镜时画面上走多快；徒手时用 98k 的默认值（UGinkgoWeaponDefinition 的类默认值）
	const APlayerController* PlayerController = Controller.Get();
	const AGinkgoCharacter* Character = PlayerController ? Cast<AGinkgoCharacter>(PlayerController->GetPawn()) : nullptr;
	const UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
	const UGinkgoWeaponDefinition* Weapon = Weapons ? Weapons->GetHeldWeapon() : nullptr;
	Payload->SetNumberField(TEXT("adsZoom"), Weapon ? Weapon->AdsZoom : GetDefault<UGinkgoWeaponDefinition>()->AdsZoom);
	// 倍镜的倍率（第一人称开镜，Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：倍镜灵敏度的说明按它算。手上的枪没有瞄具时用 ZF39 的（类默认值）
	const FGinkgoScope* Scope = Weapons ? Weapons->GetHeldScope() : nullptr;
	Payload->SetNumberField(TEXT("scopeZoom"), Scope ? Scope->Optics.Magnification : FGinkgoScope().Optics.Magnification);
	Payload->SetNumberField(TEXT("dirty"), Ginkgo::Input::CountDifferences(Settings->GetCurrent(), Settings->GetSaved()) + Video.CountChanges());
	View->Emit(TEXT("settings"), ToJsonText(Payload));
}

void FGinkgoSettingsPage::SendValues(const TConstArrayView<const FGinkgoInputSettingDef*> Defs, const FString& Status, const bool bWithKeys, const bool bWithVideo) const
{
	const UGinkgoInputSettingsSubsystem* Settings = GetSettings();
	if (!Settings || !View)
	{
		return;
	}
	TArray<TSharedPtr<FJsonValue>> Rows;
	if (bWithVideo) { Video.AppendRows(Rows, false); }
	for (const FGinkgoInputSettingDef* Def : Defs)
	{
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), Def->Id);
		Row->SetField(TEXT("value"), ValueToJson(*Def, Settings->GetCurrent()));
		Row->SetField(TEXT("saved"), ValueToJson(*Def, Settings->GetSaved()));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	const TSharedRef<FJsonObject> KeyLabels = MakeShared<FJsonObject>();
	if (bWithKeys)
	{
		const TArray<FGinkgoKeyBindingDef>& KeyDefs = Ginkgo::Input::GetKeyBindingDefs();
		for (int32 Index = 0; Index < KeyDefs.Num(); ++Index)
		{
			AddKeyRows(Rows, KeyDefs[Index], Settings->GetCurrent().Keys[Index], Settings->GetSaved().Keys[Index], nullptr);
			AddKeyLabel(*KeyLabels, Settings->GetCurrent().Keys[Index].Key);
		}
	}
	const TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetArrayField(TEXT("rows"), Rows);
	Payload->SetObjectField(TEXT("keyLabels"), KeyLabels);
	Payload->SetNumberField(TEXT("dirty"), Ginkgo::Input::CountDifferences(Settings->GetCurrent(), Settings->GetSaved()) + Video.CountChanges());
	Payload->SetStringField(TEXT("status"), Status);
	View->Emit(TEXT("values"), ToJsonText(Payload));
}

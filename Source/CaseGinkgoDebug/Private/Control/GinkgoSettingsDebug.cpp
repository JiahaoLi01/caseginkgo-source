#include "Algo/BinarySearch.h"
#include "Containers/Ticker.h"
#include "Control/GinkgoAimAssist.h"
#include "Control/GinkgoInputMath.h"
#include "Control/GinkgoInputSettings.h"
#include "Control/GinkgoInputSettingsSubsystem.h"
#include "Control/GinkgoPlayerController.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SWeftViewport.h"
#include "UI/GinkgoPauseMenu.h"
#include "UI/GinkgoSettingsPage.h"
#include "UI/GinkgoUiSounds.h"
#include "UnrealClient.h"  // FScreenshotRequest
#include "Utilities/Debug/DebugCategory.h"
#include "WeftView.h"

/**
 * 设置界面的测试入口（发行版没有这个模块）：
 * - ginkgo.settings.set / get / list：不开界面改玩家的设置（测量设置的效果用）；
 * - ginkgo.settings [1|0]：开关界面（同 Esc、F10）；
 * - ginkgo.settings.setkey：不开界面改键；
 * - ginkgo.settings.input：给页面注入指针、按键与手柄，不经过系统的鼠标与键盘（同相机调参面板的 ginkgo.camera.tune.input），
 *   selfcheck 让页面把公式算一遍发回来，与 C++ 的比；slate 开头的走 Slate 的入口（改键时等键的预处理器在那里），手柄的键只能这样注入。
 */
namespace
{
	AGinkgoPlayerController* SettingsController(const UWorld* World)
	{
		return World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
	}

	UWorld* SettingsGameWorld()
	{
		UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
		return World && World->IsGameWorld() ? World : nullptr;
	}

	/** 手柄的状态（注入的）：按钮照 Gamepad API 的标准布局 */
	FWeftGamepadState SettingsPad;

	const TMap<FString, int32>& PadButtons()
	{
		static const TMap<FString, int32> Buttons = {
			{TEXT("a"), 0}, {TEXT("b"), 1}, {TEXT("x"), 2}, {TEXT("y"), 3}, {TEXT("lb"), 4}, {TEXT("rb"), 5}, {TEXT("lt"), 6}, {TEXT("rt"), 7},
			{TEXT("back"), 8}, {TEXT("start"), 9}, {TEXT("l3"), 10}, {TEXT("r3"), 11}, {TEXT("up"), 12}, {TEXT("down"), 13}, {TEXT("left"), 14}, {TEXT("right"), 15}, {TEXT("home"), 16},
		};
		return Buttons;
	}

	/** selfcheck：页面发来的样本 [{curve, strength, ...}]，与 C++ 的算法比 */
	void CheckPageSamples(const FString& JsonValue)
	{
		TSharedPtr<FJsonValue> Value;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonValue), Value);
		const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Samples))
		{
			UE_LOG(LogGinkgoControl, Warning, TEXT("[Settings] 自检：读不懂页面发来的样本"));
			return;
		}
		double WorstCurve = 0.0;
		double WorstZone = 0.0;
		double WorstGain = 0.0;
		double WorstRamp = 0.0;
		double WorstAssist = 0.0;
		int32 Count = 0;
		for (const TSharedPtr<FJsonValue>& Sample : *Samples)
		{
			const TSharedPtr<FJsonObject> Object = Sample->AsObject();
			const FString Kind = Object->GetStringField(TEXT("kind"));
			const double Out = Object->GetNumberField(TEXT("out"));
			++Count;
			if (Kind == TEXT("curve"))
			{
				const int32 Curve = static_cast<int32>(Object->GetNumberField(TEXT("curve")));
				const float In = static_cast<float>(Object->GetNumberField(TEXT("in")));
				const float Strength = static_cast<float>(Object->GetNumberField(TEXT("strength")));
				WorstCurve = FMath::Max(WorstCurve, FMath::Abs(Out - Ginkgo::Input::ApplyResponseCurve(In, static_cast<EGinkgoResponseCurve>(Curve), Strength)));
			}
			else if (Kind == TEXT("zone"))
			{
				const float In = static_cast<float>(Object->GetNumberField(TEXT("in")));
				const FVector2D Live = Ginkgo::Input::ApplyRadialDeadZone(FVector2D(In, 0.0), static_cast<float>(Object->GetNumberField(TEXT("inner"))), static_cast<float>(Object->GetNumberField(TEXT("outer"))));
				WorstZone = FMath::Max(WorstZone, FMath::Abs(Out - Live.X));
			}
			else if (Kind == TEXT("gain"))
			{
				WorstGain = FMath::Max(WorstGain, FMath::Abs(Out - Ginkgo::Input::MouseAccelerationGain(static_cast<float>(Object->GetNumberField(TEXT("in"))),
					static_cast<float>(Object->GetNumberField(TEXT("strength"))), static_cast<float>(Object->GetNumberField(TEXT("offset"))), static_cast<float>(Object->GetNumberField(TEXT("cap"))))));
			}
			else if (Kind == TEXT("ramp"))
			{
				// 页面算的是按住到 t 时的权重；C++ 按小步推进取最后一步的平均，差别在一步之内
				Ginkgo::Input::FGinkgoTurnRamp Ramp;
				const float Time = static_cast<float>(Object->GetNumberField(TEXT("in")));
				const float Delay = static_cast<float>(Object->GetNumberField(TEXT("delay")));
				const float RampTime = static_cast<float>(Object->GetNumberField(TEXT("ramp")));
				constexpr float Step = 0.0005f;
				float Weight = 0.f;
				for (float T = 0.f; T < Time; T += Step)
				{
					Weight = Ramp.Step(true, Step, Delay, RampTime);
				}
				WorstRamp = FMath::Max(WorstRamp, FMath::Abs(Out - Weight));
			}
			else if (Kind == TEXT("assist"))
			{
				// 辅助瞄准的强度随离中线的距离淡掉（预览画的那条线）
				WorstAssist = FMath::Max(WorstAssist, FMath::Abs(Out - Ginkgo::AimAssist::Falloff(static_cast<float>(Object->GetNumberField(TEXT("in"))),
					static_cast<float>(Object->GetNumberField(TEXT("inner"))), static_cast<float>(Object->GetNumberField(TEXT("outer"))))));
			}
		}
		UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] 自检：%d 个样本，页面与 C++ 最大差：响应曲线 %.2e，死区 %.2e，加速度 %.2e，转身加速 %.2e，辅助瞄准 %.2e"), Count, WorstCurve, WorstZone, WorstGain, WorstRamp, WorstAssist);
	}

	FDelegateHandle SelfCheckHandle;
	TWeakObjectPtr<UWeftView> SelfCheckView;

	/** 玩家的设置逐项写进日志：现在的、文件里的、默认的。值都按 double 写（开关 0、1，选择是下标），脚本读 */
	void LogSettings(const UGinkgoInputSettingsSubsystem& Settings)
	{
		const FGinkgoInputSettings Defaults;
		for (const FGinkgoInputSettingDef& Def : Ginkgo::Input::GetSettingDefs())
		{
			UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] %s = %g（文件 %g，默认 %g）"), *Def.Id, Def.Get(Settings.GetCurrent()), Def.Get(Settings.GetSaved()), Def.Get(Defaults));
		}
		const auto Binding = [](const FGinkgoKeyBinding& Key)
		{
			return FString::Printf(TEXT("%s %s"), Key.Key.IsValid() ? *Key.Key.GetFName().ToString() : TEXT("-"), Key.Mode == EGinkgoHoldMode::Toggle ? TEXT("toggle") : TEXT("hold"));
		};
		const TArray<FGinkgoKeyBindingDef>& KeyDefs = Ginkgo::Input::GetKeyBindingDefs();
		for (int32 Index = 0; Index < KeyDefs.Num(); ++Index)
		{
			UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] 键 %s = %s（文件 %s，默认 %s）"), *KeyDefs[Index].Id, *Binding(Settings.GetCurrent().Keys[Index]), *Binding(Settings.GetSaved().Keys[Index]), *Binding(Defaults.Keys[Index]));
		}
	}

	/** 走 Slate 的入口按键：slatedown、slateup <键名>、slatewheel <格数>。手柄的键与键盘的键是键事件，鼠标键是指针事件 */
	void RunSlateInput(const TArray<FString>& Args)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		const FString& Action = Args[0];
		if (Action == TEXT("slatewheel"))
		{
			Slate.OnMouseWheel(FCString::Atof(*Args[1]), FVector2D(200.0, 200.0));
			return;
		}
		const FKey Key(*Args[1]);
		const bool bDown = Action == TEXT("slatedown");
		if (Key.IsMouseButton())
		{
			const EMouseButtons::Type Button = Key == EKeys::RightMouseButton ? EMouseButtons::Right : Key == EKeys::MiddleMouseButton ? EMouseButtons::Middle
				: Key == EKeys::ThumbMouseButton ? EMouseButtons::Thumb01 : Key == EKeys::ThumbMouseButton2 ? EMouseButtons::Thumb02 : EMouseButtons::Left;
			if (bDown)
			{
				Slate.OnMouseDown(nullptr, Button, FVector2D(200.0, 200.0));
			}
			else
			{
				Slate.OnMouseUp(Button, FVector2D(200.0, 200.0));
			}
			return;
		}
		const FKeyEvent Event(Key, FModifierKeysState(), FInputDeviceId::CreateFromInternalId(0), false, 0, 0, 0);
		if (bDown)
		{
			Slate.ProcessKeyDownEvent(Event);
		}
		else
		{
			Slate.ProcessKeyUpEvent(Event);
		}
	}

	struct FScheduledSettingsInput
	{
		double At = 0.0;
		TArray<FString> Args;
	};
	TArray<FScheduledSettingsInput> ScheduledInputs;
	FTSTicker::FDelegateHandle InputTicker;

	bool TickSettingsInputs(float DeltaTime);

	void ScheduleInput(const double At, TArray<FString> Args)
	{
		const int32 Index = Algo::UpperBoundBy(ScheduledInputs, At, &FScheduledSettingsInput::At);
		ScheduledInputs.Insert({At, MoveTemp(Args)}, Index);
		if (!InputTicker.IsValid())
		{
			InputTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickSettingsInputs));
		}
	}

	void RunInput(const TArray<FString>& Args, UWorld* World)
	{
		AGinkgoPlayerController* Controller = SettingsController(World);
		const FString Action = Args.Num() > 0 ? Args[0] : FString();
		UE_LOG(LogGinkgoUI, Log, TEXT("[Settings] 测试输入：%s"), *FString::Join(Args, TEXT(" ")));
		if (Action == TEXT("sound") && Args.Num() >= 2)
		{
			// 放一声菜单的声音，不要页面（录音的测量：weapon_audio.py --ui）
			UGinkgoUiSoundSubsystem::Play(World, Ginkgo::UI::ParseUiSound(Args[1]));
			return;
		}
		if (Action == TEXT("open"))
		{
			if (Controller)
			{
				Controller->GetOrCreateSettingsPage().Show(Args.Num() > 1 && Args[1] == TEXT("pad"));
			}
			return;
		}
		if (Action == TEXT("close"))
		{
			if (Controller)
			{
				Controller->GetOrCreateSettingsPage().Hide();
			}
			return;
		}
		if (Action == TEXT("shot") && Args.Num() >= 2)
		{
			FScreenshotRequest::RequestScreenshot(Args[1], true, false);
			return;
		}
		if (Action == TEXT("quit"))
		{
			FPlatformMisc::RequestExit(false, TEXT("ginkgo.settings.input"));
			return;
		}
		if (Action == TEXT("list"))
		{
			if (const UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(Controller))
			{
				UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] 清单 %s"), Args.Num() > 1 ? *Args[1] : TEXT(""));
				LogSettings(*Settings);
			}
			return;
		}
		if (Action.StartsWith(TEXT("slate")) && Args.Num() >= 2)
		{
			RunSlateInput(Args);
			return;
		}
		const FGinkgoSettingsPage* Page = Controller ? &Controller->GetOrCreateSettingsPage() : nullptr;
		UWeftView* View = Page ? Page->GetView() : nullptr;
		if (!View)
		{
			// 手柄的菜单键确认以后界面就关了，松开要照样记下来，不然下次打开时它还按着
			if (const int32* Button = Action == TEXT("padup") && Args.Num() >= 2 ? PadButtons().Find(Args[1].ToLower()) : nullptr)
			{
				SettingsPad.Buttons[*Button] = 0.f;
				SettingsPad.Pressed &= ~(1u << *Button);
				return;
			}
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Settings] 测试输入 %s：界面没开着"), *Action);
			return;
		}
		// 测试输入一来，页面就不再接真的鼠标：Slate 会按真的指针位置给页面补发移动（同调参面板）
		if (const TSharedPtr<SWeftViewport> Widget = Page->GetWidget(); Widget && Widget->GetVisibility() != EVisibility::HitTestInvisible)
		{
			Widget->SetVisibility(EVisibility::HitTestInvisible);
		}
		if (Action == TEXT("key") && Args.Num() >= 2)
		{
			// key <Code>：按下加松开；键值就是 Code（ArrowUp、Escape、Enter、F10……）
			View->Key(true, Args[1], Args[1]);
			View->Key(false, Args[1], Args[1]);
		}
		else if (Action == TEXT("keydown") && Args.Num() >= 2)
		{
			View->Key(true, Args[1], Args[1], EWeftModifier::None, Args.Num() > 2 && Args[2] == TEXT("repeat"));
		}
		else if (Action == TEXT("keyup") && Args.Num() >= 2)
		{
			View->Key(false, Args[1], Args[1]);
		}
		else if (Action == TEXT("type") && Args.Num() >= 2)
		{
			View->Key(true, TEXT("KeyA"), TEXT("a"), EWeftModifier::Control);
			View->Key(false, TEXT("KeyA"), TEXT("a"), EWeftModifier::Control);
			View->CommitText(FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" ")));
			View->Key(true, TEXT("Enter"), TEXT("Enter"));
			View->Key(false, TEXT("Enter"), TEXT("Enter"));
		}
		else if ((Action == TEXT("pad") || Action == TEXT("paddown") || Action == TEXT("padup")) && Args.Num() >= 2)
		{
			// pad <按钮>：按下，0.08 s 后松开（页面的 LB、RB、X、Y、菜单键按帧读状态，一帧里按下又松开它看不见）；paddown、padup 分开
			const int32* Button = PadButtons().Find(Args[1].ToLower());
			if (!Button)
			{
				UE_LOG(LogGinkgoUI, Warning, TEXT("[Settings] 没有这个手柄按钮：%s"), *Args[1]);
				return;
			}
			const uint32 Bit = 1u << *Button;
			SettingsPad.Id = TEXT("Xbox 360 Controller (XInput STANDARD GAMEPAD)");
			if (Action != TEXT("padup"))
			{
				SettingsPad.Buttons[*Button] = 1.f;
				SettingsPad.Pressed |= Bit;
				View->Gamepad(SettingsPad);
			}
			if (Action == TEXT("padup"))
			{
				SettingsPad.Buttons[*Button] = 0.f;
				SettingsPad.Pressed &= ~Bit;
				View->Gamepad(SettingsPad);
			}
			else if (Action == TEXT("pad") && World)
			{
				ScheduleInput(World->GetTimeSeconds() + 0.08, {TEXT("padup"), Args[1]});
			}
		}
		else if (Action == TEXT("padstick") && Args.Num() >= 5)
		{
			// padstick <L|R> <x> <y>（y 向上为正，同游戏；Gamepad API 的 y 向下）
			const int32 Base = Args[1] == TEXT("L") ? 0 : 2;
			SettingsPad.Id = TEXT("Xbox 360 Controller (XInput STANDARD GAMEPAD)");
			SettingsPad.Axes[Base] = FCString::Atof(*Args[2]);
			SettingsPad.Axes[Base + 1] = -FCString::Atof(*Args[3]);
			View->Gamepad(SettingsPad);
		}
		else if (Action == TEXT("selfcheck"))
		{
			if (!SelfCheckHandle.IsValid() || SelfCheckView.Get() != View)
			{
				SelfCheckView = View;
				SelfCheckHandle = View->OnMessage.AddLambda([](const FString& Name, const FString& JsonValue, uint64)
				{
					if (Name == TEXT("selfcheck"))
					{
						CheckPageSamples(JsonValue);
					}
				});
			}
			View->Emit(TEXT("selfcheck"), TEXT("{}"));
		}
		else if (Args.Num() >= 3)
		{
			const FVector2D Position(FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
			if (Action == TEXT("click") || Action == TEXT("move"))
			{
				View->Pointer(EWeftPointer::Move, Position, 0);
			}
			if (Action == TEXT("drag"))
			{
				View->Pointer(EWeftPointer::Move, Position, 1);
			}
			if (Action == TEXT("click") || Action == TEXT("down"))
			{
				View->Pointer(EWeftPointer::Down, Position, 1);
			}
			if (Action == TEXT("click") || Action == TEXT("up"))
			{
				View->Pointer(EWeftPointer::Up, Position, 0);
			}
			if (Action == TEXT("wheel") && Args.Num() >= 4)
			{
				View->Pointer(EWeftPointer::Move, Position, 0);
				View->Pointer(EWeftPointer::Wheel, Position, 0, FVector2D(0.0, FCString::Atod(*Args[3])));
			}
		}
	}

	bool TickSettingsInputs(float DeltaTime)
	{
		UWorld* World = SettingsGameWorld();
		while (World && !ScheduledInputs.IsEmpty() && ScheduledInputs[0].At <= World->GetTimeSeconds())
		{
			const TArray<FString> Args = MoveTemp(ScheduledInputs[0].Args);
			ScheduledInputs.RemoveAt(0);
			RunInput(Args, World);
		}
		return true;
	}

	FAutoConsoleCommandWithWorldAndArgs CommandSettingsInput(
		TEXT("ginkgo.settings.input"),
		TEXT("测试设置界面用：ginkgo.settings.input [at=<世界时间>] open [pad] | close | click|down|up|move|drag <x> <y> | wheel <x> <y> <dy> | key|keydown|keyup <Code> | type <文字> | ")
		TEXT("pad|paddown|padup <a|b|x|y|lb|rb|lt|rt|back|start|l3|r3|up|down|left|right> | padstick <L|R> <x> <y> | slatedown|slateup <键名> | slatewheel <格数> | selfcheck | list [标签] | shot <文件> | quit。")
		TEXT("直接给页面，不经过系统的鼠标、键盘与手柄（第一条之后页面不再接真的鼠标）；带 at= 时排到那个世界时间再做"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			double At = -1.0;
			if (!Args.IsEmpty() && Args[0].StartsWith(TEXT("at=")))
			{
				At = FCString::Atod(*Args[0].RightChop(3));
				Args.RemoveAt(0);
			}
			if (At < 0.0)
			{
				RunInput(Args, World);
				return;
			}
			ScheduleInput(At, MoveTemp(Args));
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandPauseMenu(
		TEXT("ginkgo.pause"),
		TEXT("开关暂停菜单（同 Esc、F10）：ginkgo.pause [1|0|pad]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AGinkgoPlayerController* Controller = SettingsController(World))
			{
				FGinkgoPauseMenu& Menu = Controller->GetOrCreatePauseMenu();
				const bool bPad = Args.Num() > 0 && Args[0] == TEXT("pad");
				if (Args.Num() > 0 && Args[0] == TEXT("0"))
				{
					Menu.Hide();
				}
				else if (Args.Num() > 0 && (Args[0] == TEXT("1") || bPad))
				{
					Menu.Show(bPad);
				}
				else if (Menu.IsShown())
				{
					Menu.Hide();
				}
				else
				{
					Menu.Show(false);
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandSettings(
		TEXT("ginkgo.settings"),
		TEXT("直接开关设置界面（跳过暂停菜单，供调试与自动化测试用）：ginkgo.settings [1|0]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AGinkgoPlayerController* Controller = SettingsController(World))
			{
				FGinkgoSettingsPage& Page = Controller->GetOrCreateSettingsPage();
				if (Args.Num() > 0 && Args[0] == TEXT("0"))
				{
					Page.Hide();
				}
				else if (Args.Num() > 0 && Args[0] == TEXT("1"))
				{
					Page.Show();
				}
				else if (Page.IsShown())
				{
					Page.Hide();
				}
				else
				{
					Page.Show();
				}
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandSettingsSet(
		TEXT("ginkgo.settings.set"),
		TEXT("改玩家的一项输入设置（不写文件）：ginkgo.settings.set <名字> <值>。值：数；开关 0、1；选择的名字（linear、power、hold、toggle……）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(SettingsController(World));
			const FGinkgoInputSettingDef* Def = Args.Num() >= 2 ? Ginkgo::Input::FindSettingDef(Args[0]) : nullptr;
			if (!Settings || !Def)
			{
				UE_LOG(LogGinkgoControl, Warning, TEXT("[Settings] ginkgo.settings.set：没有本地玩家或没有这一项：%s"), *FString::Join(Args, TEXT(" ")));
				return;
			}
			const double Value = Def->Kind == FGinkgoInputSettingDef::EKind::Choice ? Def->Options.IndexOfByKey(Args[1]) : FCString::Atod(*Args[1]);
			Settings->SetValue(Def->Id, Value);
			UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] %s = %g"), *Def->Id, Def->Get(Settings->GetCurrent()));
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandSettingsSetKey(
		TEXT("ginkgo.settings.setkey"),
		TEXT("改一个键位（不写文件）：ginkgo.settings.setkey <键位，如 mouse.crouch.primary> <键名，none 是清空，keep 是不动> [hold|toggle]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(SettingsController(World));
			const int32 Index = Args.Num() >= 2 ? Ginkgo::Input::FindKeyBinding(Args[0]) : INDEX_NONE;
			if (!Settings || Index == INDEX_NONE)
			{
				UE_LOG(LogGinkgoControl, Warning, TEXT("[Settings] ginkgo.settings.setkey：没有本地玩家或没有这个键位：%s"), *FString::Join(Args, TEXT(" ")));
				return;
			}
			if (Args[1] != TEXT("keep"))
			{
				Settings->SetKey(Index, Args[1] == TEXT("none") ? EKeys::Invalid : FKey(*Args[1]));
			}
			if (Args.Num() >= 3)
			{
				Settings->SetKeyMode(Index, Args[2] == TEXT("toggle") ? EGinkgoHoldMode::Toggle : EGinkgoHoldMode::Hold);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandSettingsList(
		TEXT("ginkgo.settings.list"),
		TEXT("列出玩家的输入设置：名字、现在的值、文件里的值、默认值"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const UGinkgoInputSettingsSubsystem* Settings = UGinkgoInputSettingsSubsystem::Get(SettingsController(World));
			if (!Settings)
			{
				return;
			}
			UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] 文件 %s"), *Settings->GetFilePath());
			LogSettings(*Settings);
		}));
}

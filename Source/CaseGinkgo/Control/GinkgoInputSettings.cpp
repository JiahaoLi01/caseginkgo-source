#include "GinkgoInputSettings.h"

#include "Algo/Find.h"
#include "Control/GinkgoAimAssist.h"            // Ginkgo::AimAssist::MaxStrength：强度的上限
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"

namespace
{
	using FSettings = FGinkgoInputSettings;
	using FDef = FGinkgoInputSettingDef;
	using FKeyDef = FGinkgoKeyBindingDef;

	/** 文件格式的版本：现在只有 1。以后改了某一项的含义时加一，读的时候按版本换算 */
	constexpr int32 SettingsFileVersion = 1;

	template <auto Member>
	double GetField(const FSettings& Settings)
	{
		return static_cast<double>(Settings.*Member);
	}

	template <auto Member>
	void SetField(FSettings& Settings, const double Value)
	{
		using TField = std::remove_reference_t<decltype(Settings.*Member)>;
		if constexpr (std::is_same_v<TField, bool>)
		{
			Settings.*Member = Value != 0.0;
		}
		else
		{
			Settings.*Member = static_cast<TField>(Value);
		}
	}

	template <auto Member>
	FDef Number(const TCHAR* Id, const EGinkgoInputDirectory Directory, const double Min, const double Max, const double Step)
	{
		FDef Def;
		Def.Id = Id;
		Def.Directory = Directory;
		Def.Kind = FDef::EKind::Number;
		Def.Get = &GetField<Member>;
		Def.Set = &SetField<Member>;
		Def.Min = Min;
		Def.Max = Max;
		Def.Step = Step;
		return Def;
	}

	template <auto Member>
	FDef Toggle(const TCHAR* Id, const EGinkgoInputDirectory Directory)
	{
		FDef Def;
		Def.Id = Id;
		Def.Directory = Directory;
		Def.Kind = FDef::EKind::Toggle;
		Def.Get = &GetField<Member>;
		Def.Set = &SetField<Member>;
		return Def;
	}

	template <auto Member>
	FDef Choice(const TCHAR* Id, const EGinkgoInputDirectory Directory, TArray<FString> Options)
	{
		FDef Def;
		Def.Id = Id;
		Def.Directory = Directory;
		Def.Kind = FDef::EKind::Choice;
		Def.Get = &GetField<Member>;
		Def.Set = &SetField<Member>;
		Def.Options = MoveTemp(Options);
		return Def;
	}

	TArray<FKeyDef> BuildKeyBindingDefs()
	{
		constexpr EGinkgoInputDirectory Pad = EGinkgoInputDirectory::Gamepad;
		constexpr EGinkgoInputDirectory Mouse = EGinkgoInputDirectory::KeyboardMouse;
		TArray<FKeyDef> Defs;
		const auto Row = [&Defs](const EGinkgoInputDirectory Directory, const EGinkgoKeyAction Action, const TCHAR* Name, const FKey& Primary, const FKey& Secondary,
			const bool bHasMode = false, const EGinkgoHoldMode SecondaryMode = EGinkgoHoldMode::Hold, const EGinkgoHoldMode PrimaryMode = EGinkgoHoldMode::Hold)
		{
			const FString RowId = FString::Printf(TEXT("%s.%s"), Directory == EGinkgoInputDirectory::Gamepad ? TEXT("gamepad") : TEXT("mouse"), Name);
			for (const EGinkgoKeySlot Slot : {EGinkgoKeySlot::Primary, EGinkgoKeySlot::Secondary})
			{
				const bool bPrimary = Slot == EGinkgoKeySlot::Primary;
				FKeyDef Def;
				Def.Id = RowId + (bPrimary ? TEXT(".primary") : TEXT(".secondary"));
				Def.RowId = RowId;
				Def.MappingName = FName(*Def.Id);
				Def.Directory = Directory;
				Def.Action = Action;
				Def.Slot = Slot;
				Def.DefaultKey = bPrimary ? Primary : Secondary;
				Def.DefaultMode = bPrimary ? PrimaryMode : SecondaryMode;
				Def.bHasMode = bHasMode;
				Defs.Add(MoveTemp(Def));
			}
		};
		// 顺序同 EGinkgoKeyAction（KeyBindingIndex 按它算，测试查过）。Ctrl 按住蹲、C 切换蹲；Shift / 左摇杆按下切换冲刺；滚轮下滚是连跳用的
		Row(Mouse, EGinkgoKeyAction::MoveRight, TEXT("moveRight"), EKeys::D, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::MoveLeft, TEXT("moveLeft"), EKeys::A, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::MoveForward, TEXT("moveForward"), EKeys::W, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::MoveBack, TEXT("moveBack"), EKeys::S, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::Jump, TEXT("jump"), EKeys::SpaceBar, EKeys::MouseScrollDown);
		Row(Mouse, EGinkgoKeyAction::Crouch, TEXT("crouch"), EKeys::LeftControl, EKeys::C, true, EGinkgoHoldMode::Toggle);
		Row(Mouse, EGinkgoKeyAction::Sprint, TEXT("sprint"), EKeys::LeftShift, EKeys::Invalid, true, EGinkgoHoldMode::Hold, EGinkgoHoldMode::Toggle);
		Row(Mouse, EGinkgoKeyAction::Interact, TEXT("interact"), EKeys::E, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::Aim, TEXT("aim"), EKeys::RightMouseButton, EKeys::Invalid, true);
		Row(Mouse, EGinkgoKeyAction::Fire, TEXT("fire"), EKeys::LeftMouseButton, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::Reload, TEXT("reload"), EKeys::R, EKeys::Invalid);
		// 换枪（Docs/Design/ActionStates.md「收枪键」）：键盘按槽选（手枪主键 F、辅助键 4），手柄的键少，用切枪换另一个槽
		Row(Mouse, EGinkgoKeyAction::Slot1, TEXT("slot1"), EKeys::One, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::Slot2, TEXT("slot2"), EKeys::Two, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::Sidearm, TEXT("sidearm"), EKeys::F, EKeys::Four);
		Row(Mouse, EGinkgoKeyAction::Holster, TEXT("holster"), EKeys::Three, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::SwitchWeapon, TEXT("switchWeapon"), EKeys::Invalid, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::DropWeapon, TEXT("dropWeapon"), EKeys::G, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::SwitchShoulder, TEXT("switchShoulder"), EKeys::V, EKeys::Invalid);
		Row(Mouse, EGinkgoKeyAction::Flashbang, TEXT("flashbang"), EKeys::Q, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::Jump, TEXT("jump"), EKeys::Gamepad_FaceButton_Bottom, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::Crouch, TEXT("crouch"), EKeys::Gamepad_FaceButton_Right, EKeys::Invalid, true);
		Row(Pad, EGinkgoKeyAction::Sprint, TEXT("sprint"), EKeys::Gamepad_LeftThumbstick, EKeys::Invalid, true, EGinkgoHoldMode::Hold, EGinkgoHoldMode::Toggle);
		Row(Pad, EGinkgoKeyAction::Interact, TEXT("interact"), EKeys::Gamepad_FaceButton_Left, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::Aim, TEXT("aim"), EKeys::Gamepad_LeftTrigger, EKeys::Invalid, true);
		Row(Pad, EGinkgoKeyAction::Fire, TEXT("fire"), EKeys::Gamepad_RightTrigger, EKeys::Invalid);
		// X 已经是交互，所以装填放 RB（你定的）
		Row(Pad, EGinkgoKeyAction::Reload, TEXT("reload"), EKeys::Gamepad_RightShoulder, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::Slot1, TEXT("slot1"), EKeys::Invalid, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::Slot2, TEXT("slot2"), EKeys::Invalid, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::Sidearm, TEXT("sidearm"), EKeys::Invalid, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::Holster, TEXT("holster"), EKeys::Gamepad_DPad_Down, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::SwitchWeapon, TEXT("switchWeapon"), EKeys::Gamepad_FaceButton_Top, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::DropWeapon, TEXT("dropWeapon"), EKeys::Gamepad_DPad_Right, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::SwitchShoulder, TEXT("switchShoulder"), EKeys::Invalid, EKeys::Invalid);
		Row(Pad, EGinkgoKeyAction::Flashbang, TEXT("flashbang"), EKeys::Invalid, EKeys::Invalid);
		return Defs;
	}

	TArray<FDef> BuildSettingDefs()
	{
		constexpr EGinkgoInputDirectory Pad = EGinkgoInputDirectory::Gamepad;
		constexpr EGinkgoInputDirectory Mouse = EGinkgoInputDirectory::KeyboardMouse;
		return {
			Number<&FSettings::GamepadYawSpeed>(TEXT("gamepad.yawSpeed"), Pad, 30.0, 600.0, 5.0),
			Number<&FSettings::GamepadPitchSpeed>(TEXT("gamepad.pitchSpeed"), Pad, 30.0, 600.0, 5.0),
			Number<&FSettings::GamepadAdsScale>(TEXT("gamepad.adsScale"), Pad, 0.1, 2.0, 0.05),
			Toggle<&FSettings::bGamepadAdsMatchFov>(TEXT("gamepad.adsMatchFov"), Pad),
			Number<&FSettings::GamepadScopeScale>(TEXT("gamepad.scopeScale"), Pad, 0.1, 2.0, 0.05),
			Choice<&FSettings::GamepadCurve>(TEXT("gamepad.curve"), Pad, {TEXT("linear"), TEXT("power"), TEXT("scurve")}),
			Number<&FSettings::GamepadCurveStrength>(TEXT("gamepad.curveStrength"), Pad, 1.0, 4.0, 0.1),
			Toggle<&FSettings::bGamepadInvertPitch>(TEXT("gamepad.invertPitch"), Pad),
			Toggle<&FSettings::bGamepadInvertYaw>(TEXT("gamepad.invertYaw"), Pad),
			Toggle<&FSettings::bGamepadSwapSticks>(TEXT("gamepad.swapSticks"), Pad),
			// 内死区的上限与外圈的下限隔开 0.05：两个范围不重叠，外圈永远比内死区大，不用再做跨项的校验
			Number<&FSettings::GamepadLookInner>(TEXT("gamepad.lookInner"), Pad, 0.0, 0.5, 0.01),
			Number<&FSettings::GamepadLookOuter>(TEXT("gamepad.lookOuter"), Pad, 0.55, 1.0, 0.01),
			Number<&FSettings::GamepadMoveInner>(TEXT("gamepad.moveInner"), Pad, 0.0, 0.5, 0.01),
			Number<&FSettings::GamepadMoveOuter>(TEXT("gamepad.moveOuter"), Pad, 0.55, 1.0, 0.01),
			Number<&FSettings::GamepadExtraYaw>(TEXT("gamepad.extraYaw"), Pad, 0.0, 400.0, 10.0),
			Number<&FSettings::GamepadExtraPitch>(TEXT("gamepad.extraPitch"), Pad, 0.0, 200.0, 10.0),
			Number<&FSettings::GamepadExtraDelay>(TEXT("gamepad.extraDelay"), Pad, 0.0, 1.0, 0.01),
			Number<&FSettings::GamepadExtraRamp>(TEXT("gamepad.extraRamp"), Pad, 0.0, 1.5, 0.01),
			// 辅助瞄准：总开关；强度最多到算法的上限（再大就出不去了）；两个半径各自的范围，淡到零的比满强度的小时算法按满强度的算，不用跨项校验
			Toggle<&FSettings::bGamepadAimAssist>(TEXT("gamepad.aimAssistEnabled"), Pad),
			Number<&FSettings::GamepadAimAssistHip>(TEXT("gamepad.aimAssistHip"), Pad, 0.0, Ginkgo::AimAssist::MaxStrength, 0.05),
			Number<&FSettings::GamepadAimAssistAds>(TEXT("gamepad.aimAssistAds"), Pad, 0.0, Ginkgo::AimAssist::MaxStrength, 0.05),
			Number<&FSettings::GamepadAimAssistInner>(TEXT("gamepad.aimAssistInner"), Pad, 0.0, 100.0, 5.0),
			Number<&FSettings::GamepadAimAssistOuter>(TEXT("gamepad.aimAssistOuter"), Pad, 0.0, 300.0, 10.0),
			Number<&FSettings::GamepadAimAssistRange>(TEXT("gamepad.aimAssistRange"), Pad, 5.0, 100.0, 5.0),
			Number<&FSettings::GamepadAimAssistFollow>(TEXT("gamepad.aimAssistFollow"), Pad, 0.0, 1.0, 0.05),

			Number<&FSettings::MouseSensitivity>(TEXT("mouse.sensitivity"), Mouse, 0.1, 20.0, 0.05),
			Number<&FSettings::MousePitchRatio>(TEXT("mouse.pitchRatio"), Mouse, 0.25, 2.0, 0.05),
			Number<&FSettings::MouseAdsScale>(TEXT("mouse.adsScale"), Mouse, 0.1, 2.0, 0.05),
			Toggle<&FSettings::bMouseAdsMatchFov>(TEXT("mouse.adsMatchFov"), Mouse),
			Number<&FSettings::MouseScopeScale>(TEXT("mouse.scopeScale"), Mouse, 0.1, 2.0, 0.05),
			Toggle<&FSettings::bMouseInvertPitch>(TEXT("mouse.invertPitch"), Mouse),
			Toggle<&FSettings::bMouseInvertYaw>(TEXT("mouse.invertYaw"), Mouse),
			Toggle<&FSettings::bMouseAcceleration>(TEXT("mouse.accelEnabled"), Mouse),
			Number<&FSettings::MouseAccelStrength>(TEXT("mouse.accelStrength"), Mouse, 0.0, 0.2, 0.005),
			Number<&FSettings::MouseAccelOffset>(TEXT("mouse.accelOffset"), Mouse, 0.0, 20.0, 0.5),
			Number<&FSettings::MouseAccelCap>(TEXT("mouse.accelCap"), Mouse, 1.0, 4.0, 0.1),
			Toggle<&FSettings::bMouseAimAssist>(TEXT("mouse.aimAssistEnabled"), Mouse),
			Number<&FSettings::MouseAimAssistHip>(TEXT("mouse.aimAssistHip"), Mouse, 0.0, Ginkgo::AimAssist::MaxStrength, 0.05),
			Number<&FSettings::MouseAimAssistAds>(TEXT("mouse.aimAssistAds"), Mouse, 0.0, Ginkgo::AimAssist::MaxStrength, 0.05),
			Number<&FSettings::MouseAimAssistInner>(TEXT("mouse.aimAssistInner"), Mouse, 0.0, 100.0, 5.0),
			Number<&FSettings::MouseAimAssistOuter>(TEXT("mouse.aimAssistOuter"), Mouse, 0.0, 300.0, 10.0),
			Number<&FSettings::MouseAimAssistRange>(TEXT("mouse.aimAssistRange"), Mouse, 5.0, 100.0, 5.0),
			Number<&FSettings::MouseAimAssistFollow>(TEXT("mouse.aimAssistFollow"), Mouse, 0.0, 1.0, 0.05),

			Toggle<&FSettings::bAutoPeek>(TEXT("accessibility.autoPeek"), EGinkgoInputDirectory::Accessibility),
			Toggle<&FSettings::bAdsAutoShoulder>(TEXT("accessibility.adsAutoShoulder"), EGinkgoInputDirectory::Accessibility),
			Toggle<&FSettings::bRestoreShoulderOnAdsExit>(TEXT("accessibility.restoreShoulderOnAdsExit"), EGinkgoInputDirectory::Accessibility),
			Toggle<&FSettings::bSubtitles>(TEXT("accessibility.subtitles"), EGinkgoInputDirectory::Accessibility),
			Toggle<&FSettings::bSubtitleSpeaker>(TEXT("accessibility.subtitleSpeaker"), EGinkgoInputDirectory::Accessibility),
			Number<&FSettings::SubtitleSize>(TEXT("accessibility.subtitleSize"), EGinkgoInputDirectory::Accessibility, 16, 40, 1),
			Number<&FSettings::SubtitleBackdrop>(TEXT("accessibility.subtitleBackdrop"), EGinkgoInputDirectory::Accessibility, 0, 1, .05),
		};
	}

	/**
	 * 数的写法：能用 6 位有效数字写回同一个 float 就这样写（7.95，文件好读），不然用 9 位（一定写得回去）
	 */
	FString FormatNumber(const double Value)
	{
		const float Single = static_cast<float>(Value);
		FString Text = FString::Printf(TEXT("%.6g"), Value);
		if (static_cast<float>(FCString::Atod(*Text)) != Single)
		{
			Text = FString::Printf(TEXT("%.9g"), Value);
		}
		return Text;
	}
}

FGinkgoInputSettings::FGinkgoInputSettings()
{
	for (const FKeyDef& Def : Ginkgo::Input::GetKeyBindingDefs())
	{
		Keys.Add({Def.DefaultKey, Def.DefaultMode});
	}
}

const TArray<FGinkgoKeyBindingDef>& Ginkgo::Input::GetKeyBindingDefs()
{
	// 函数里的静态量第一次用到时才初始化：EKeys 是另一个模块的静态量，这时已经好了
	static const TArray<FKeyDef> Defs = BuildKeyBindingDefs();
	return Defs;
}

int32 Ginkgo::Input::KeyBindingIndex(const EGinkgoInputDirectory Directory, const EGinkgoKeyAction Action, const EGinkgoKeySlot Slot)
{
	constexpr int32 SlotCount = static_cast<int32>(EGinkgoKeySlot::Count);
	const int32 ActionIndex = static_cast<int32>(Action);
	if (Directory == EGinkgoInputDirectory::KeyboardMouse)
	{
		return ActionIndex * SlotCount + static_cast<int32>(Slot);
	}
	if (Directory != EGinkgoInputDirectory::Gamepad)
	{
		return INDEX_NONE;
	}
	// 手柄排在键盘后面，没有前面四个方向
	const int32 FirstOnPad = static_cast<int32>(EGinkgoKeyAction::Jump);
	return ActionIndex < FirstOnPad ? INDEX_NONE : (static_cast<int32>(EGinkgoKeyAction::Count) + ActionIndex - FirstOnPad) * SlotCount + static_cast<int32>(Slot);
}

int32 Ginkgo::Input::FindKeyBinding(const FString& Id)
{
	return GetKeyBindingDefs().IndexOfByPredicate([&Id](const FKeyDef& Def) { return Def.Id == Id; });
}

FKey Ginkgo::Input::FindBoundKey(const FGinkgoInputSettings& Settings, const EGinkgoInputDirectory Directory, const EGinkgoKeyAction Action)
{
	for (const EGinkgoKeySlot Slot : {EGinkgoKeySlot::Primary, EGinkgoKeySlot::Secondary})
	{
		const int32 Index = KeyBindingIndex(Directory, Action, Slot);
		if (Settings.Keys.IsValidIndex(Index) && Settings.Keys[Index].Key.IsValid())
		{
			return Settings.Keys[Index].Key;
		}
	}
	return EKeys::Invalid;
}

EGinkgoKeyCheck Ginkgo::Input::CheckKey(const EGinkgoInputDirectory Directory, const FKey& Key)
{
	if (!Key.IsValid() || Key.IsAxis1D() || Key.IsAxis2D() || Key.IsAxis3D() || Key.IsTouch() || Key.IsGesture())
	{
		return EGinkgoKeyCheck::NotAButton;
	}
	if (Key.IsGamepadKey() != (Directory == EGinkgoInputDirectory::Gamepad))
	{
		return EGinkgoKeyCheck::WrongDevice;
	}
	if (Key.IsGamepadKey())
	{
		// 摇杆的方向也是键（Gamepad_LeftStick_Up），但不是按钮：推一下摇杆不该绑上去
		static const FKey Buttons[] = {
			EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
			EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder, EKeys::Gamepad_LeftTrigger, EKeys::Gamepad_RightTrigger,
			EKeys::Gamepad_LeftThumbstick, EKeys::Gamepad_RightThumbstick,
			EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right,
		};
		if (Key == EKeys::Gamepad_Special_Right || Key == EKeys::Gamepad_Special_Left)
		{
			return EGinkgoKeyCheck::Reserved;
		}
		return Algo::Find(Buttons, Key) ? EGinkgoKeyCheck::Ok : EGinkgoKeyCheck::NotAButton;
	}
	// Esc、F10 打开设置；F1–F12 是开发版的调试键；Delete 在改键时是清空；` 是开发版的控制台
	static const FKey Reserved[] = {
		EKeys::Escape, EKeys::Delete, EKeys::Tilde, EKeys::LeftCommand, EKeys::RightCommand,
		EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5, EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9, EKeys::F10, EKeys::F11, EKeys::F12,
	};
	return Algo::Find(Reserved, Key) ? EGinkgoKeyCheck::Reserved : EGinkgoKeyCheck::Ok;
}

int32 Ginkgo::Input::FindKeyConflict(const FGinkgoInputSettings& Settings, const int32 Index, const FKey& Key)
{
	if (!Key.IsValid())
	{
		return INDEX_NONE;
	}
	const TArray<FKeyDef>& Defs = GetKeyBindingDefs();
	for (int32 Other = 0; Other < Defs.Num(); ++Other)
	{
		if (Other != Index && Defs[Other].Directory == Defs[Index].Directory && Settings.Keys[Other].Key == Key)
		{
			return Other;
		}
	}
	return INDEX_NONE;
}

void Ginkgo::Input::AssignKey(FGinkgoInputSettings& Settings, const int32 Index, const FKey& Key)
{
	const int32 Conflict = FindKeyConflict(Settings, Index, Key);
	if (Conflict != INDEX_NONE)
	{
		Settings.Keys[Conflict].Key = Settings.Keys[Index].Key;
	}
	Settings.Keys[Index].Key = Key;
}

const TArray<FGinkgoInputSettingDef>& Ginkgo::Input::GetSettingDefs()
{
	static const TArray<FDef> Defs = BuildSettingDefs();
	return Defs;
}

const FGinkgoInputSettingDef* Ginkgo::Input::FindSettingDef(const FString& Id)
{
	return GetSettingDefs().FindByPredicate([&Id](const FDef& Def) { return Def.Id == Id; });
}

void Ginkgo::Input::WriteSetting(FGinkgoInputSettings& Settings, const FGinkgoInputSettingDef& Def, const double Value)
{
	switch (Def.Kind)
	{
	case FDef::EKind::Number:
		if (FMath::IsFinite(Value))
		{
			Def.Set(Settings, FMath::Clamp(Value, Def.Min, Def.Max));
		}
		break;
	case FDef::EKind::Toggle:
		Def.Set(Settings, Value != 0.0 ? 1.0 : 0.0);
		break;
	case FDef::EKind::Choice:
		if (const int32 Index = FMath::RoundToInt32(Value); Def.Options.IsValidIndex(Index))
		{
			Def.Set(Settings, Index);
		}
		break;
	}
}

int32 Ginkgo::Input::CountDifferences(const FGinkgoInputSettings& A, const FGinkgoInputSettings& B, const EGinkgoInputDirectory* Directory)
{
	using namespace UE::Query;
	int32 Differences = GetSettingDefs()
		| Where([&](const FDef& Def) { return (!Directory || Def.Directory == *Directory) && Def.Get(A) != Def.Get(B); })
		| Count();
	const TArray<FKeyDef>& KeyDefs = GetKeyBindingDefs();
	for (int32 Index = 0; Index < KeyDefs.Num(); ++Index)
	{
		if (!Directory || KeyDefs[Index].Directory == *Directory)
		{
			Differences += (A.Keys[Index].Key != B.Keys[Index].Key ? 1 : 0) + (KeyDefs[Index].bHasMode && A.Keys[Index].Mode != B.Keys[Index].Mode ? 1 : 0);
		}
	}
	return Differences;
}

void Ginkgo::Input::CopyDirectory(FGinkgoInputSettings& To, const FGinkgoInputSettings& From, const EGinkgoInputDirectory Directory)
{
	for (const FDef& Def : GetSettingDefs())
	{
		if (Def.Directory == Directory)
		{
			Def.Set(To, Def.Get(From));
		}
	}
	const TArray<FKeyDef>& KeyDefs = GetKeyBindingDefs();
	for (int32 Index = 0; Index < KeyDefs.Num(); ++Index)
	{
		if (KeyDefs[Index].Directory == Directory)
		{
			To.Keys[Index] = From.Keys[Index];
		}
	}
}

FString Ginkgo::Input::SettingsToJson(const FGinkgoInputSettings& Settings)
{
	const FGinkgoInputSettings Defaults;
	const TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
	for (const FDef& Def : GetSettingDefs())
	{
		const double Value = Def.Get(Settings);
		if (Value == Def.Get(Defaults))
		{
			continue;
		}
		switch (Def.Kind)
		{
		case FDef::EKind::Number:
			Values->SetField(Def.Id, MakeShared<FJsonValueNumberString>(FormatNumber(Value)));
			break;
		case FDef::EKind::Toggle:
			Values->SetBoolField(Def.Id, Value != 0.0);
			break;
		case FDef::EKind::Choice:
			Values->SetStringField(Def.Id, Def.Options[FMath::RoundToInt32(Value)]);
			break;
		}
	}
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("note"), TEXT("玩家的输入设置（设置界面点确认时写的）：只记和默认值不同的项。删掉这个文件就回到默认"));
	Root->SetNumberField(TEXT("version"), SettingsFileVersion);
	Root->SetObjectField(TEXT("values"), Values);
	// 键位：只记和默认值不同的键位，空的键是 ""
	const TSharedRef<FJsonObject> KeyValues = MakeShared<FJsonObject>();
	const TArray<FKeyDef>& KeyDefs = GetKeyBindingDefs();
	for (int32 Index = 0; Index < KeyDefs.Num(); ++Index)
	{
		const FKeyDef& Def = KeyDefs[Index];
		const FGinkgoKeyBinding& Binding = Settings.Keys[Index];
		if (Binding.Key == Def.DefaultKey && (!Def.bHasMode || Binding.Mode == Def.DefaultMode))
		{
			continue;
		}
		const TSharedRef<FJsonObject> Slot = MakeShared<FJsonObject>();
		Slot->SetStringField(TEXT("key"), Binding.Key.IsValid() ? Binding.Key.GetFName().ToString() : FString());
		if (Def.bHasMode)
		{
			Slot->SetStringField(TEXT("mode"), Binding.Mode == EGinkgoHoldMode::Toggle ? TEXT("toggle") : TEXT("hold"));
		}
		KeyValues->SetObjectField(Def.Id, Slot);
	}
	if (KeyValues->Values.Num() > 0)
	{
		Root->SetObjectField(TEXT("keys"), KeyValues);
	}
	FString Text;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Text));
	return Text;
}

bool Ginkgo::Input::SettingsFromJson(const FString& Text, FGinkgoInputSettings& OutSettings, TArray<FString>* OutIssues)
{
	OutSettings = FGinkgoInputSettings();
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		return false;
	}
	const auto Issue = [OutIssues](FString Message)
	{
		if (OutIssues)
		{
			OutIssues->Add(MoveTemp(Message));
		}
	};
	const TSharedPtr<FJsonObject>* Values = nullptr;
	const bool bHasValues = Root->TryGetObjectField(TEXT("values"), Values);
	if (!bHasValues)
	{
		Issue(TEXT("没有 values"));
	}
	const TSharedPtr<FJsonObject> ValueObject = bHasValues ? *Values : MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ValueObject->Values)
	{
		const FDef* Def = FindSettingDef(Pair.Key);
		if (!Def)
		{
			Issue(FString::Printf(TEXT("不认识的项 %s"), *Pair.Key));
			continue;
		}
		double Number = 0.0;
		bool bFlag = false;
		FString Name;
		bool bRead = false;
		switch (Def->Kind)
		{
		case FDef::EKind::Number:
			if (Pair.Value->TryGetNumber(Number))
			{
				WriteSetting(OutSettings, *Def, Number);
				bRead = true;
			}
			break;
		case FDef::EKind::Toggle:
			if (Pair.Value->TryGetBool(bFlag))
			{
				WriteSetting(OutSettings, *Def, bFlag ? 1.0 : 0.0);
				bRead = true;
			}
			break;
		case FDef::EKind::Choice:
			if (Pair.Value->TryGetString(Name) && Def->Options.Contains(Name))
			{
				WriteSetting(OutSettings, *Def, Def->Options.IndexOfByKey(Name));
				bRead = true;
			}
			break;
		}
		if (!bRead)
		{
			Issue(FString::Printf(TEXT("%s 的值读不懂"), *Pair.Key));
		}
	}
	const TSharedPtr<FJsonObject>* KeyValues = nullptr;
	if (Root->TryGetObjectField(TEXT("keys"), KeyValues))
	{
		const TArray<FKeyDef>& KeyDefs = GetKeyBindingDefs();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*KeyValues)->Values)
		{
			const int32 Index = FindKeyBinding(Pair.Key);
			const TSharedPtr<FJsonObject>* Slot = nullptr;
			if (Index == INDEX_NONE || !Pair.Value->TryGetObject(Slot))
			{
				Issue(FString::Printf(TEXT("不认识的键位 %s"), *Pair.Key));
				continue;
			}
			if (FString KeyName; (*Slot)->TryGetStringField(TEXT("key"), KeyName))
			{
				const FKey Key = KeyName.IsEmpty() ? EKeys::Invalid : FKey(*KeyName);
				if (KeyName.IsEmpty() || CheckKey(KeyDefs[Index].Directory, Key) == EGinkgoKeyCheck::Ok)
				{
					OutSettings.Keys[Index].Key = Key;
				}
				else
				{
					Issue(FString::Printf(TEXT("%s 绑不了 %s"), *Pair.Key, *KeyName));
				}
			}
			if (FString ModeName; (*Slot)->TryGetStringField(TEXT("mode"), ModeName))
			{
				if (KeyDefs[Index].bHasMode && (ModeName == TEXT("hold") || ModeName == TEXT("toggle")))
				{
					OutSettings.Keys[Index].Mode = ModeName == TEXT("toggle") ? EGinkgoHoldMode::Toggle : EGinkgoHoldMode::Hold;
				}
				else
				{
					Issue(FString::Printf(TEXT("%s 的方式读不懂：%s"), *Pair.Key, *ModeName));
				}
			}
		}
		// 一个键只给一个键位：撞了时后面的空出来
		for (int32 Index = 0; Index < KeyDefs.Num(); ++Index)
		{
			const int32 Conflict = FindKeyConflict(OutSettings, Index, OutSettings.Keys[Index].Key);
			if (Conflict != INDEX_NONE && Conflict < Index)
			{
				Issue(FString::Printf(TEXT("%s 和 %s 都是 %s，%s 空出来"), *KeyDefs[Conflict].Id, *KeyDefs[Index].Id, *OutSettings.Keys[Index].Key.ToString(), *KeyDefs[Index].Id));
				OutSettings.Keys[Index].Key = EKeys::Invalid;
			}
		}
	}
	return true;
}

bool Ginkgo::Input::SaveSettingsFile(const FString& Path, const FGinkgoInputSettings& Settings, FString& OutError)
{
	IFileManager& Files = IFileManager::Get();
	Files.MakeDirectory(*FPaths::GetPath(Path), true);
	const FString Temporary = Path + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(SettingsToJson(Settings), *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("写不进 %s"), *Temporary);
		return false;
	}
	if (!Files.Move(*Path, *Temporary, /*bReplace*/ true, /*bEvenIfReadOnly*/ false, /*bAttributes*/ false, /*bDoNotRetryOrError*/ true))
	{
		Files.Delete(*Temporary, /*bRequireExists*/ false, /*bEvenReadOnly*/ false, /*bQuiet*/ true);
		OutError = FString::Printf(TEXT("换不过去：%s"), *Path);
		return false;
	}
	return true;
}

bool Ginkgo::Input::LoadSettingsFile(const FString& Path, FGinkgoInputSettings& OutSettings, TArray<FString>* OutIssues)
{
	OutSettings = FGinkgoInputSettings();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		return true;
	}
	if (SettingsFromJson(Text, OutSettings, OutIssues))
	{
		return true;
	}
	// 留着坏文件好查原因，下一次确认会写一份新的
	const FString Corrupt = Path + TEXT(".corrupt");
	IFileManager::Get().Move(*Corrupt, *Path, /*bReplace*/ true, /*bEvenIfReadOnly*/ false, /*bAttributes*/ false, /*bDoNotRetryOrError*/ true);
	return false;
}

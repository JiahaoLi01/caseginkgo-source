#include "GinkgoInputSettingsSubsystem.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "Utilities/Debug/DebugCategory.h"

UGinkgoInputSettingsSubsystem* UGinkgoInputSettingsSubsystem::Get(const APlayerController* Controller)
{
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UGinkgoInputSettingsSubsystem>() : nullptr;
}

void UGinkgoInputSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FilePath = FPaths::ProjectSavedDir() / TEXT("Ginkgo/InputSettings.json");
	bUseFile = !FApp::IsUnattended();
#if !UE_BUILD_SHIPPING
	if (FString Override; FParse::Value(FCommandLine::Get(), TEXT("GinkgoInputSettingsFile="), Override))
	{
		FilePath = Override;
		bUseFile = true;
	}
#endif
	if (!bUseFile)
	{
		UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] 自动化的运行：输入设置用默认值，不读不写 %s"), *FilePath);
		return;
	}
	TArray<FString> Issues;
	const bool bRead = Ginkgo::Input::LoadSettingsFile(FilePath, Saved, &Issues);
	Current = Saved;
	UE_CLOG(!bRead, LogGinkgoControl, Warning, TEXT("[Settings] %s 读不懂，用默认值（原文件改名成 .corrupt）"), *FilePath);
	for (const FString& Issue : Issues)
	{
		UE_LOG(LogGinkgoControl, Warning, TEXT("[Settings] %s：%s"), *FilePath, *Issue);
	}
	UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] 输入设置：%s，%d 项和默认值不同"), *FilePath, Ginkgo::Input::CountDifferences(Current, FGinkgoInputSettings()));
}

bool UGinkgoInputSettingsSubsystem::SetValue(const FString& Id, const double Value)
{
	const FGinkgoInputSettingDef* Def = Ginkgo::Input::FindSettingDef(Id);
	if (!Def)
	{
		return false;
	}
	Ginkgo::Input::WriteSetting(Current, *Def, Value);
	OnChanged.Broadcast();
	return true;
}

bool UGinkgoInputSettingsSubsystem::ResetValue(const FString& Id)
{
	const FGinkgoInputSettingDef* Def = Ginkgo::Input::FindSettingDef(Id);
	if (!Def)
	{
		return false;
	}
	Def->Set(Current, Def->Get(FGinkgoInputSettings()));
	OnChanged.Broadcast();
	return true;
}

bool UGinkgoInputSettingsSubsystem::SetKey(const int32 Index, const FKey& Key)
{
	if (!Current.Keys.IsValidIndex(Index))
	{
		return false;
	}
	Ginkgo::Input::AssignKey(Current, Index, Key);
	OnChanged.Broadcast();
	return true;
}

bool UGinkgoInputSettingsSubsystem::SetKeyMode(const int32 Index, const EGinkgoHoldMode Mode)
{
	if (!Current.Keys.IsValidIndex(Index) || !Ginkgo::Input::GetKeyBindingDefs()[Index].bHasMode)
	{
		return false;
	}
	Current.Keys[Index].Mode = Mode;
	OnChanged.Broadcast();
	return true;
}

bool UGinkgoInputSettingsSubsystem::ResetKeyRow(const FString& RowId)
{
	const FGinkgoInputSettings Defaults;
	const TArray<FGinkgoKeyBindingDef>& Defs = Ginkgo::Input::GetKeyBindingDefs();
	bool bFound = false;
	for (int32 Index = 0; Index < Defs.Num(); ++Index)
	{
		if (Defs[Index].RowId == RowId)
		{
			// 默认键别的键位正在用时互换，不会撞
			Ginkgo::Input::AssignKey(Current, Index, Defaults.Keys[Index].Key);
			Current.Keys[Index].Mode = Defaults.Keys[Index].Mode;
			bFound = true;
		}
	}
	if (bFound)
	{
		OnChanged.Broadcast();
	}
	return bFound;
}

void UGinkgoInputSettingsSubsystem::ApplyKeys() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer<ULocalPlayer>();
	const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	UEnhancedInputUserSettings* UserSettings = Input ? Input->GetUserSettings() : nullptr;
	const UEnhancedPlayerMappableKeyProfile* Profile = UserSettings ? UserSettings->GetActiveKeyProfile() : nullptr;
	if (!Profile)
	{
		return;
	}
	const TArray<FGinkgoKeyBindingDef>& Defs = Ginkgo::Input::GetKeyBindingDefs();
	for (int32 Index = 0; Index < Defs.Num(); ++Index)
	{
		FMapPlayerKeyArgs Args;
		Args.MappingName = Defs[Index].MappingName;
		// 一个键位一行映射（GinkgoPlayerController 说了为什么），每行只用第一个槽
		Args.Slot = EPlayerMappableKeySlot::First;
		Args.NewKey = Current.Keys[Index].Key;
		const FPlayerKeyMapping* Mapping = Profile->FindKeyMapping(Args);
		if (!Mapping)
		{
			UE_LOG(LogGinkgoControl, Warning, TEXT("[Settings] Enhanced Input 里没有映射 %s"), *Defs[Index].Id);
		}
		else if (Mapping->GetCurrentKey() != Args.NewKey)
		{
			FGameplayTagContainer Failure;
			UserSettings->MapPlayerKey(Args, Failure);
			UE_CLOG(!Failure.IsEmpty(), LogGinkgoControl, Warning, TEXT("[Settings] 改不了 %s：%s"), *Defs[Index].Id, *Failure.ToString());
		}
	}
}

void UGinkgoInputSettingsSubsystem::ResetDirectory(const EGinkgoInputDirectory Directory)
{
	Ginkgo::Input::CopyDirectory(Current, FGinkgoInputSettings(), Directory);
	OnChanged.Broadcast();
}

void UGinkgoInputSettingsSubsystem::RevertToSaved()
{
	SetCurrent(Saved);
}

bool UGinkgoInputSettingsSubsystem::Confirm(FString& OutError)
{
	// 没有改动时不碰文件
	if (Ginkgo::Input::CountDifferences(Current, Saved) == 0)
	{
		return true;
	}
	if (bUseFile && !Ginkgo::Input::SaveSettingsFile(FilePath, Current, OutError))
	{
		UE_LOG(LogGinkgoControl, Warning, TEXT("[Settings] 存盘失败：%s"), *OutError);
		return false;
	}
	Saved = Current;
	UE_LOG(LogGinkgoControl, Log, TEXT("[Settings] 确认：%s%s，%d 项和默认值不同"), bUseFile ? TEXT("存到 ") : TEXT("自动化的运行不写文件 "), *FilePath,
		Ginkgo::Input::CountDifferences(Current, FGinkgoInputSettings()));
	return true;
}

void UGinkgoInputSettingsSubsystem::SetCurrent(const FGinkgoInputSettings& Settings)
{
	Current = Settings;
	OnChanged.Broadcast();
}

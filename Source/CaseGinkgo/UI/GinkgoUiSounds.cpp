#include "UI/GinkgoUiSounds.h"

#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Kismet/GameplayStatics.h"
#include "MetasoundOperatorCacheSubsystem.h"
#include "MetasoundSource.h"
#include "Sound/SoundBase.h"
#include "Utilities/Debug/DebugCategory.h"

const FGinkgoUiSound* FGinkgoUiSounds::Get(const EGinkgoUiSound Kind) const
{
	switch (Kind)
	{
	case EGinkgoUiSound::Open:
		return &Open;
	case EGinkgoUiSound::Close:
		return &Close;
	case EGinkgoUiSound::Focus:
		return &Focus;
	case EGinkgoUiSound::Select:
		return &Select;
	case EGinkgoUiSound::Confirm:
		return &Confirm;
	case EGinkgoUiSound::Deny:
		return &Deny;
	case EGinkgoUiSound::Invalid:
		return &Invalid;
	case EGinkgoUiSound::Notification:
		return &Notification;
	case EGinkgoUiSound::WorkbenchConfirm:
		return &WorkbenchConfirm;
	default:
		return nullptr;
	}
}

EGinkgoUiSound Ginkgo::UI::ParseUiSound(const FString& Name)
{
	const int64 Value = StaticEnum<EGinkgoUiSound>()->GetValueByNameString(Name);
	return Value == INDEX_NONE ? EGinkgoUiSound::None : static_cast<EGinkgoUiSound>(Value);
}

UGinkgoUiSoundSubsystem* UGinkgoUiSoundSubsystem::Find(const UObject* WorldContext)
{
	const UWorld* World = GEngine && WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UGinkgoUiSoundSubsystem>() : nullptr;
}

bool UGinkgoUiSoundSubsystem::Play(const UObject* WorldContext, const EGinkgoUiSound Kind)
{
	UGinkgoUiSoundSubsystem* Sounds = Find(WorldContext);
	return Sounds && Sounds->PlayKind(Kind);
}

bool UGinkgoUiSoundSubsystem::PlayFromPage(const UObject* WorldContext, const FString& JsonValue)
{
	return Play(WorldContext, Ginkgo::UI::ParseUiSound(JsonValue.TrimQuotes()));
}

bool UGinkgoUiSoundSubsystem::PlayWorkbench(const UObject* WorldContext, const EGinkgoUiSound Kind)
{
	auto* Sounds = Find(WorldContext);
	const float VolumeScale = UGinkgoDeveloperSettings::Get()->UiSounds.WorkbenchVolume;
	return Sounds && VolumeScale > 0.f && Sounds->PlayKind(Kind, VolumeScale);
}

void UGinkgoUiSoundSubsystem::Preload(const UObject* WorldContext)
{
	const FGinkgoUiSounds& Sounds = UGinkgoDeveloperSettings::Get()->UiSounds;
	for (int32 Index = static_cast<int32>(EGinkgoUiSound::None) + 1; Index < KindCount; ++Index)
	{
		if (const FGinkgoUiSound* Item = Sounds.Get(static_cast<EGinkgoUiSound>(Index))) { PreloadSound(WorldContext, Item->Sound); }
	}
}

void UGinkgoUiSoundSubsystem::PreloadSound(const UObject* WorldContext, const TSoftObjectPtr<USoundBase>& Reference)
{
	UGinkgoUiSoundSubsystem* Self = Find(WorldContext);
	const UWorld* World = Self ? Self->GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const FAudioDeviceHandle Device = World->GetAudioDevice();
	UMetaSoundCacheSubsystem* Cache = Device.IsValid() ? Device->GetSubsystem<UMetaSoundCacheSubsystem>() : nullptr;
	USoundBase* Sound = Reference.LoadSynchronous();
	if (!Sound || Self->Loaded.Contains(Sound))
	{
		return;
	}
	Self->Loaded.Add(Sound);
	// 第一次放现建 MetaSound 的图要多等约 25 ms：先建好一份。
	if (UMetaSoundSource* MetaSound = Cast<UMetaSoundSource>(Sound); MetaSound && Cache)
	{
		Cache->PrecacheMetaSound(MetaSound);
	}
}

bool UGinkgoUiSoundSubsystem::PlayKind(const EGinkgoUiSound Kind, const float VolumeScale)
{
	const FGinkgoUiSounds& Sounds = UGinkgoDeveloperSettings::Get()->UiSounds;
	const FGinkgoUiSound* Item = Sounds.Get(Kind);
	USoundBase* Sound = Item ? Item->Sound.LoadSynchronous() : nullptr;
	UWorld* World = GetGameInstance()->GetWorld();
	if (!Sound || !World)
	{
		return false;
	}
	const double Now = World->GetUnpausedTimeSeconds();
	if (Kind == EGinkgoUiSound::Focus)
	{
		if (Now - LastFocusTime < Sounds.FocusMinInterval)
		{
			return false;
		}
		LastFocusTime = Now;
	}
	// SpawnSound2D 标了 bIsUISound：暂停时也响。没有声音设备（-NoSound、-benchmark）时是空的
	const float Volume = Item->Volume * VolumeScale;
	UAudioComponent* Audio = UGameplayStatics::SpawnSound2D(World, Sound, Volume);
	UE_LOG(LogGinkgoUI, Log, TEXT("[UI] 菜单的声音：%s，音量 %.2f，世界时间 %.4f s%s"), *StaticEnum<EGinkgoUiSound>()->GetNameStringByValue(static_cast<int64>(Kind)), Volume, Now,
		Audio ? TEXT("") : TEXT("（没放）"));
	if (!Audio)
	{
		return false;
	}
	TWeakObjectPtr<UAudioComponent>& Last = Ringing[static_cast<int32>(Kind)];
	if (UAudioComponent* Previous = Last.Get(); Previous && Previous->IsPlaying() && Sounds.FadePrevious > 0.f)
	{
		Previous->FadeOut(Sounds.FadePrevious, 0.f);
	}
	Last = Audio;
	return true;
}

#include "AudioDeviceManager.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AudioMixerDevice.h"
#include "AudioMixerSubmix.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Utilities/Debug/DebugCategory.h"

/**
 * 测量用的录音（发行版没有这个模块）：把这个进程放出来的声音录成 wav。Scripts/Measurements/weapon_audio.py 拿它和样本比，看真的响了哪个、晚了多少。
 * 配 -DeterministicAudio 时声音跟着游戏帧渲染：固定帧长下每次跑都一样。不配时是真的设备（量第一次放的延迟）：
 * 录的地方在送到设备之前，所以这里开 au.MuteAudio（只清送到设备的那一份）并关掉主混音的音量，录得到、扬声器里没有声音
 */
namespace
{
	FAutoConsoleCommandWithWorldAndArgs GRecordCommand(
		TEXT("ginkgo.audio.record"),
		TEXT("录下这个进程放出来的声音：ginkgo.audio.record <秒> <文件名（不带扩展名）> <目录>。到时间写成 wav"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 3 || !World)
			{
				UE_LOG(LogGinkgoWeapon, Warning, TEXT("[Audio] 用法：ginkgo.audio.record <秒> <文件名> <目录>"));
				return;
			}
			const float Seconds = FCString::Atof(*Args[0]);
			const FString Name = Args[1];
			const FString Path = Args[2];
			if (IConsoleVariable* Mute = IConsoleManager::Get().FindConsoleVariable(TEXT("au.MuteAudio")))
			{
				Mute->Set(1);
			}
			if (Audio::FMixerDevice* Device = FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World))
			{
				if (const Audio::FMixerSubmixPtr Main = Device->GetMainSubmix().Pin())
				{
					Main->SetOutputVolume(0.f);
				}
			}
			UAudioMixerBlueprintLibrary::StartRecordingOutput(World, Seconds);
			UE_LOG(LogGinkgoWeapon, Log, TEXT("[Audio] 开始录：世界时间 %.3f"), World->GetTimeSeconds());
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [World, Name, Path]
			{
				UAudioMixerBlueprintLibrary::StopRecordingOutput(World, EAudioRecordingExportType::WavFile, Name, Path);
				UE_LOG(LogGinkgoWeapon, Log, TEXT("[Audio] 录完：世界时间 %.3f，%s/%s.wav"), World->GetTimeSeconds(), *Path, *Name);
			}), Seconds, false);
		}));
}

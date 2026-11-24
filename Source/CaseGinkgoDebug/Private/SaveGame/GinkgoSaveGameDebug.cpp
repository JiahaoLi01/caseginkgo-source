#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "SaveGame/GinkgoSaveGameSubsystem.h"
#include "Utilities/Debug/DebugCategory.h"

/**
 * 存档的测试入口（发行版没有这个模块）：ginkgo.save [槽位] 存，ginkgo.load [槽位] 读，默认的槽位同 UGinkgoSaveGameSubsystem。
 * 结果写日志（Scripts/Measurements/save_game.py 读）。文件在 Saved/SaveGames/，编辑器里多一份 <槽位>.json.sav 可以直接看。
 */
namespace
{
	UGinkgoSaveGameSubsystem* FindSaveSubsystem(UWorld* World)
	{
		const UWorld* Target = World ? World : (GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr);
		const UGameInstance* GameInstance = Target ? Target->GetGameInstance() : nullptr;
		UGinkgoSaveGameSubsystem* Save = GameInstance ? GameInstance->GetSubsystem<UGinkgoSaveGameSubsystem>() : nullptr;
		if (!Save)
		{
			UE_LOG(LogGinkgoSave, Warning, TEXT("[Save] 没有存档子系统（没有游戏世界）"));
		}
		return Save;
	}

	FAutoConsoleCommandWithWorldAndArgs CommandSave(
		TEXT("ginkgo.save"),
		TEXT("存档：ginkgo.save [槽位]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UGinkgoSaveGameSubsystem* Save = FindSaveSubsystem(World))
			{
				const bool bSaved = Args.IsEmpty() ? Save->Save() : Save->Save(Args[0]);
				UE_LOG(LogGinkgoSave, Log, TEXT("[Save] 存档%s，Actor %d，%s"), bSaved ? TEXT("成功") : TEXT("失败"), Save->GetSaveGameActors().Num(), *Save->GetLastError());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandLoad(
		TEXT("ginkgo.load"),
		TEXT("读档：ginkgo.load [槽位]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UGinkgoSaveGameSubsystem* Save = FindSaveSubsystem(World);
			if (!Save)
			{
				return;
			}

			// 存档在别的地图时要先过去，完成的时候才知道结果
			const TSharedRef<FDelegateHandle> Handle = MakeShared<FDelegateHandle>();
			*Handle = Save->OnSaveGameLoaded.AddWeakLambda(Save, [Save, Handle]
			{
				UE_LOG(LogGinkgoSave, Log, TEXT("[Save] 读档完成，地图 %s，Actor %d"), *Save->GetWorld()->GetOutermost()->GetName(), Save->GetSaveGameActors().Num());
				Save->OnSaveGameLoaded.Remove(*Handle);
			});

			const bool bStarted = Args.IsEmpty() ? Save->Load() : Save->Load(Args[0]);
			if (!bStarted)
			{
				Save->OnSaveGameLoaded.Remove(*Handle);
				UE_LOG(LogGinkgoSave, Warning, TEXT("[Save] 读档失败：%s"), *Save->GetLastError());
			}
		}));
}

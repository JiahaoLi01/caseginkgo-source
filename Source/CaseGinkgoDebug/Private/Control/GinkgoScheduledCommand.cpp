#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

/**
 * 按世界时间执行控制台命令（发行版没有这个模块）：ginkgo.at <世界时间> <命令……>。
 * 测量的路线用：-ExecCmds 一次排好，到了那个世界时间才执行（同 ginkgo.input.feed 的 at=）。命令里不能有逗号，那是 -ExecCmds 的分隔
 */
namespace
{
	struct FScheduledCommand
	{
		double At = 0.0;
		FString Command;
	};

	TArray<FScheduledCommand> ScheduledCommands;
	FTSTicker::FDelegateHandle ScheduledHandle;

	bool RunDueCommands(float)
	{
		UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
		if (!World)
		{
			return true;
		}
		const double Now = World->GetTimeSeconds();
		// 先挑出到时候的再执行：执行的命令可能再排新的
		TArray<FString> Due;
		ScheduledCommands.RemoveAll([Now, &Due](const FScheduledCommand& Scheduled)
		{
			if (Scheduled.At <= Now)
			{
				Due.Add(Scheduled.Command);
				return true;
			}
			return false;
		});
		for (const FString& Command : Due)
		{
			GEngine->Exec(World, *Command);
		}
		if (ScheduledCommands.IsEmpty())
		{
			ScheduledHandle.Reset();
			return false;
		}
		return true;
	}

	FAutoConsoleCommandWithWorldAndArgs CommandAt(
		TEXT("ginkgo.at"),
		TEXT("按世界时间执行控制台命令（测量用）：ginkgo.at <世界时间> <命令……>；同一时刻的按排的先后执行"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			if (Args.Num() < 2)
			{
				return;
			}
			FString Command = Args[1];
			for (int32 Index = 2; Index < Args.Num(); ++Index)
			{
				Command += TEXT(" ") + Args[Index];
			}
			ScheduledCommands.Add({FCString::Atod(*Args[0]), MoveTemp(Command)});
			ScheduledCommands.StableSort([](const FScheduledCommand& A, const FScheduledCommand& B) { return A.At < B.At; });
			if (!ScheduledHandle.IsValid())
			{
				ScheduledHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&RunDueCommands));
			}
		}));
}

// 动画动作的调试命令：发行版不编译
#if !UE_BUILD_SHIPPING

#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionChain.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Parse.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"

namespace GinkgoAnimActionDebug
{
	/**
	 * 一次测试：等世界时间到了、本地角色就绪再启动；给了取消的时刻就到时取消（原因 OwnerExit）。
	 * 固定帧长（-benchmark -fps=N）下世界时间每帧走 1/N，同一条命令在各个帧率下都在同一个时刻附近开始
	 */
	struct FTest
	{
		FString Kind;
		double At = 1.0;
		double CancelAt = -1.0;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UGinkgoAnimAction> Action;
		bool bStarted = false;
	};

	AGinkgoCharacter* FindLocalCharacter(UWorld* World)
	{
		APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller ? Cast<AGinkgoCharacter>(Controller->GetPawn()) : nullptr;
	}

	/** 测试用的动作：draw、stow，switch 是拔枪接收枪的链（量换段），drawcancel 是拔枪到时取消 */
	UGinkgoAnimAction* MakeAction(AGinkgoCharacter& Character, const FString& Kind)
	{
		if (Kind == TEXT("draw") || Kind == TEXT("drawcancel"))
		{
			return Character.MakeDrawWeaponAction();
		}
		if (Kind == TEXT("stow"))
		{
			return Character.MakeStowWeaponAction();
		}
		if (Kind == TEXT("switch"))
		{
			const TWeakObjectPtr<AGinkgoCharacter> WeakCharacter(&Character);
			return UGinkgoAnimActionChain::MakeChain(&Character, TEXT("DrawThenStow"), [WeakCharacter](FGinkgoAnimActionChainBuilder& Chain)
			{
				Chain.Step(TEXT("Draw"), [WeakCharacter] { return WeakCharacter.IsValid() ? WeakCharacter->MakeDrawWeaponAction() : nullptr; }).Next(TEXT("Stow"));
				Chain.Step(TEXT("Stow"), [WeakCharacter] { return WeakCharacter.IsValid() ? WeakCharacter->MakeStowWeaponAction() : nullptr; });
			});
		}
		return nullptr;
	}

	/** @return 还要不要继续等 */
	bool Tick(FTest& Test)
	{
		UWorld* World = Test.World.Get();
		AGinkgoCharacter* Character = FindLocalCharacter(World);
		if (!World || !Character || !Character->GetAnimActions())
		{
			return World != nullptr;
		}
		const double Now = World->GetTimeSeconds();
		if (!Test.bStarted)
		{
			if (Now < Test.At)
			{
				return true;
			}
			UGinkgoAnimAction* Action = MakeAction(*Character, Test.Kind);
			if (!Action)
			{
				UE_LOG(LogGinkgoAnimAction, Warning, TEXT("[AnimAction] 不认识的测试 %s：draw、stow、switch、drawcancel"), *Test.Kind);
				return false;
			}
			Test.Action = Action;
			Test.bStarted = true;
			const FString Kind = Test.Kind;
			Character->GetAnimActions()->Start(Action, [Kind](UGinkgoAnimAction&, const FGinkgoAnimActionResult& Result)
			{
				UE_LOG(LogGinkgoAnimAction, Log, TEXT("[AnimAction] 测试 %s 结束：%s"), *Kind, *Result.ToString());
			});
			UE_LOG(LogGinkgoAnimAction, Log, TEXT("[AnimAction] 测试 %s 在 %.3f s 开始"), *Test.Kind, Now);
			return Test.CancelAt >= 0.0;
		}
		if (Now >= Test.CancelAt)
		{
			if (UGinkgoAnimAction* Action = Test.Action.Get(); Action && !Action->IsFinished())
			{
				UE_LOG(LogGinkgoAnimAction, Log, TEXT("[AnimAction] 测试 %s 在 %.3f s 取消"), *Test.Kind, Now);
				Character->GetAnimActions()->Cancel(Action, FAnimActionTags::AnimAction_Cancel_OwnerExit);
			}
			return false;
		}
		return true;
	}

	/** ginkgo.action.test <draw|stow|switch|drawcancel> [at=<秒>] [cancel=<秒>] */
	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimAction, Display, TEXT("ginkgo.action.test <draw|stow|switch|drawcancel> [at=<秒>] [cancel=<秒>]"));
			return;
		}
		TSharedRef<FTest> Test = MakeShared<FTest>();
		Test->Kind = Args[0].ToLower();
		Test->World = World;
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			FParse::Value(*Args[Index], TEXT("at="), Test->At);
			FParse::Value(*Args[Index], TEXT("cancel="), Test->CancelAt);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float)
		{
			return Tick(*Test);
		}));
	}

	/** ginkgo.action.occupy <LeftArm|RightArm|LeftArm.Hand|RightArm.Hand> <优先级> | release [at=<秒>]：外部占一个部位，试"手上没空"、以后的爬梯子 */
	TArray<int32> DebugOccupancies;

	void ApplyOccupy(const TArray<FString>& Words, UGinkgoAnimActionComponent& Actions)
	{
		if (Words[0].Equals(TEXT("release"), ESearchCase::IgnoreCase))
		{
			for (const int32 Handle : DebugOccupancies)
			{
				Actions.ReleaseOccupancy(Handle);
			}
			DebugOccupancies.Reset();
			UE_LOG(LogGinkgoAnimAction, Log, TEXT("[AnimAction] 占用：放掉命令占的"));
			return;
		}
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(*(TEXT("Occupancy.") + Words[0]), false);
		if (!Tag.IsValid())
		{
			UE_LOG(LogGinkgoAnimAction, Warning, TEXT("[AnimAction] 没有这个部位：Occupancy.%s"), *Words[0]);
			return;
		}
		const int32 Priority = Words.IsValidIndex(1) ? FCString::Atoi(*Words[1]) : 0;
		DebugOccupancies.Add(Actions.Occupy(Tag, Priority));
		UE_LOG(LogGinkgoAnimAction, Log, TEXT("[AnimAction] 占用：%s，优先级 %d"), *Tag.ToString(), Priority);
	}

	void RunOccupy(const TArray<FString>& Args, UWorld* World)
	{
		double At = -1.0;
		TArray<FString> Words;
		for (const FString& Arg : Args)
		{
			if (!FParse::Value(*Arg, TEXT("at="), At))
			{
				Words.Add(Arg);
			}
		}
		if (Words.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimAction, Display, TEXT("ginkgo.action.occupy <LeftArm|RightArm|LeftArm.Hand|RightArm.Hand> <优先级> | release [at=<秒>]"));
			return;
		}
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Words, At](float)
		{
			UWorld* Current = WeakWorld.Get();
			const AGinkgoCharacter* Character = FindLocalCharacter(Current);
			UGinkgoAnimActionComponent* Actions = Character ? Character->GetAnimActions() : nullptr;
			if (!Actions || !Actions->GetRunner() || Current->GetTimeSeconds() < At)
			{
				return Current != nullptr;
			}
			ApplyOccupy(Words, *Actions);
			return false;
		}));
	}

	FAutoConsoleCommandWithWorldAndArgs OccupyCommand(
		TEXT("ginkgo.action.occupy"),
		TEXT("外部占一个部位：ginkgo.action.occupy <LeftArm|RightArm|LeftArm.Hand|RightArm.Hand> <优先级>，release 放掉这个命令占的；at=<秒> 到世界时间再做。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunOccupy));

	FAutoConsoleCommandWithWorldAndArgs TestCommand(
		TEXT("ginkgo.action.test"),
		TEXT("动画动作的测试：ginkgo.action.test <draw|stow|switch|drawcancel> [at=<秒>] [cancel=<秒>]。")
		TEXT("世界时间到 at 时让本地角色拔枪、收枪，switch 是拔枪接收枪的链，给了 cancel 就到时取消。调试工具，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING

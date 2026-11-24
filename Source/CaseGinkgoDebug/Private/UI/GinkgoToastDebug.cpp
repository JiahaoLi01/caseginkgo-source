#include "Containers/Ticker.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UI/GinkgoToast.h"
#include "Utilities/Debug/DebugCategory.h"

/**
 * toast 的测试入口（发行版没有这个模块）：ginkgo.toast 排三条样例；带参数时排指定的一条。
 * 排程照世界时间（-ExecCmds 一次排好一串，Scripts/Measurements/toast.py）。
 */
namespace
{
	FGinkgoToast MakeToast(const EGinkgoToastIcon Icon, const FString& Title, const FString& Body, const float HoldSeconds = 0.f)
	{
		FGinkgoToast Toast;
		Toast.Icon = Icon;
		Toast.Title = FText::FromString(Title);
		Toast.Body = FText::FromString(Body);
		Toast.HoldSeconds = HoldSeconds;
		return Toast;
	}

	void RunToast(const TArray<FString>& Args)
	{
		const UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
		AGinkgoPlayerController* Controller = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller)
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Toast] ginkgo.toast：没有本地的玩家控制器"));
			return;
		}
		if (Args.Num() < 3)
		{
			Controller->PushToast(MakeToast(EGinkgoToastIcon::Gameplay, TEXT("滑铲"), TEXT("冲刺时按下蹲键滑铲，滑铲中可以开火。")));
			Controller->PushToast(MakeToast(EGinkgoToastIcon::Tutorial, TEXT("切换弹种"), TEXT("长按换弹键呼出轮盘，移动鼠标选择子弹，松开确认；按瞄准键取消。")));
			Controller->PushToast(MakeToast(EGinkgoToastIcon::Saved, TEXT("已存档"), TEXT("进度已保存。")));
			return;
		}
		const int64 Icon = StaticEnum<EGinkgoToastIcon>()->GetValueByNameString(Args[0]);
		if (Icon == INDEX_NONE)
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Toast] ginkgo.toast：没有这个图标 %s（Gameplay、Tutorial、Saved）"), *Args[0]);
			return;
		}
		Controller->PushToast(MakeToast(static_cast<EGinkgoToastIcon>(Icon), Args[1], Args[2], Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f));
	}

	FAutoConsoleCommandWithWorldAndArgs CommandToast(
		TEXT("ginkgo.toast"),
		TEXT("测试 toast 用：ginkgo.toast [at=<世界时间>] 排三条样例；ginkgo.toast [at=<世界时间>] <Gameplay|Tutorial|Saved> <标题> <内容> [停留的秒数] 排一条"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld*)
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
				RunToast(Args);
				return;
			}
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([At, Args](float)
			{
				const UWorld* World = GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
				if (!World || World->GetTimeSeconds() < At)
				{
					return true;
				}
				RunToast(Args);
				return false;
			}));
		}));
}

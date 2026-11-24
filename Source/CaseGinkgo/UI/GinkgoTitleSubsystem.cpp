#include "UI/GinkgoTitleSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

UGinkgoTitleSubsystem* UGinkgoTitleSubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UGinkgoTitleSubsystem>() : nullptr;
}

bool UGinkgoTitleSubsystem::ConsumeAutoShowDecision(const UWorld* World, bool& OutStartWithSplash)
{
	OutStartWithSplash = true;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const TCHAR* CmdLine = FCommandLine::Get();
	if (FParse::Param(CmdLine, TEXT("GinkgoSkipTitle")))
	{
		return false;
	}

	// 显式命令行开关允许在 -unattended 截图或专项测试中强制拉起开屏或直达主菜单
	const bool bForceDirectMenu = FParse::Param(CmdLine, TEXT("GinkgoMainMenuDirect"));
	const bool bForceSplash = FParse::Param(CmdLine, TEXT("GinkgoMainMenu"));
	if (bForceDirectMenu || bForceSplash)
	{
		if (bSkipTitleNextLoad)
		{
			bSkipTitleNextLoad = false;
			bNeedsFreshReloadForNewGame = false;
			return false;
		}
		if (bStartAtMainMenuNoSplash)
		{
			bStartAtMainMenuNoSplash = false;
			OutStartWithSplash = false;
			return true;
		}
		if (bHasEnteredGameplay)
		{
			return false;
		}
		OutStartWithSplash = !bForceDirectMenu;
		return true;
	}

	// 无人值守测试与自动化框架默认直接进入可操作玩法，避免阻挡既有移动/武器/AI/HUD 测试
	if (FApp::IsUnattended() || GIsAutomationTesting)
	{
		return false;
	}

	if (bSkipTitleNextLoad)
	{
		bSkipTitleNextLoad = false;
		bNeedsFreshReloadForNewGame = false;
		return false;
	}

	if (bStartAtMainMenuNoSplash)
	{
		bStartAtMainMenuNoSplash = false;
		OutStartWithSplash = false;
		return true;
	}

	if (!bHasEnteredGameplay)
	{
		OutStartWithSplash = true;
		return true;
	}

	return false;
}

void UGinkgoTitleSubsystem::MarkEnteredGameplay()
{
	bHasEnteredGameplay = true;
	bStartAtMainMenuNoSplash = false;
}

void UGinkgoTitleSubsystem::RequestSkipTitleOnNextLoad()
{
	bSkipTitleNextLoad = true;
}

void UGinkgoTitleSubsystem::MarkReturnedFromGameplay()
{
	bHasEnteredGameplay = true;
	bNeedsFreshReloadForNewGame = true;
	bStartAtMainMenuNoSplash = true;
}

void UGinkgoTitleSubsystem::ClearFreshReloadRequirement()
{
	bNeedsFreshReloadForNewGame = false;
}

void UGinkgoTitleSubsystem::SetDifficulty(const EGinkgoDifficulty NewDifficulty)
{
	CurrentDifficulty = NewDifficulty;
	bDifficultyExplicitlySet = true;
}

float UGinkgoTitleSubsystem::GetPlayerIncomingDamageScale() const
{
	if (!bDifficultyExplicitlySet && (FApp::IsUnattended() || GIsAutomationTesting))
	{
		return 1.f;
	}
	return Ginkgo::Difficulty::GetPlayerIncomingDamageScale(CurrentDifficulty);
}

void UGinkgoTitleSubsystem::ResetSessionState()
{
	bHasEnteredGameplay = false;
	bSkipTitleNextLoad = false;
	bStartAtMainMenuNoSplash = false;
	bNeedsFreshReloadForNewGame = false;
	CurrentDifficulty = EGinkgoDifficulty::Balanced;
	bDifficultyExplicitlySet = false;
}

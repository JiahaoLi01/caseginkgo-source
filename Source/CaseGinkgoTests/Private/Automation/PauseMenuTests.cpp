#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAIController.h"
#include "Character/GinkgoCharacter.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Control/GinkgoPlayerController.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "UI/GinkgoPauseMenu.h"
#include "UI/GinkgoSettingsPage.h"
#include "UnrealClient.h"
#include "WeftView.h"

namespace
{
	/** 向指定 Weft 视图发送一次完整按键按下与松开，用于无焦点依赖的页面流程验证。 */
	void TapWeftKey(UWeftView& View, const TCHAR* KeyName)
	{
		View.Key(true, KeyName, KeyName);
		View.Key(false, KeyName, KeyName);
	}

	/** 启用 -PausePreview 时把当前帧截到 Saved/PauseMenu/ 下供人工视觉核对。 */
	void RequestPreviewShot(const TCHAR* FileName)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("PausePreview")))
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("PauseMenu") / FileName, true, false);
		}
	}
}

/** 实际单机 PIE 场景：验证暂停菜单打开暂停、进出深色设置子页保持暂停、二次确认弹窗取消及恢复游戏解除暂停。 */
class FGinkgoPauseMenuScenario : public IAutomationLatentCommand
{
public:
	explicit FGinkgoPauseMenuScenario(FAutomationTestBase& InTest)
		: Test(InTest)
	{
	}

	virtual ~FGinkgoPauseMenuScenario() override
	{
		if (UWorld* World = GEditor ? GEditor->PlayWorld : nullptr)
		{
			if (auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(World); Checkpoints && !OriginalSlotName.IsEmpty())
			{
				Checkpoints->SaveSlotName = OriginalSlotName;
			}
		}
	}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 45.0)
		{
			Test.AddError(FString::Printf(TEXT("暂停菜单自动化测试超时，阶段 %d"), Stage));
			return true;
		}

		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		auto* PC = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
		auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
		if (!Player || !World->HasBegunPlay())
		{
			return false;
		}
		// 仅在未暂停的初始等待阶段等待游戏世界走过 0.5 秒
		if (Stage == 0 && World->GetTimeSeconds() < 0.5)
		{
			return false;
		}
		for (TActorIterator<AGinkgoAIController> It(World); It; ++It)
		{
			It->StopAI();
		}

		if (Stage == 0)
		{
			if (auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(World))
			{
				OriginalSlotName = Checkpoints->SaveSlotName;
				Checkpoints->SaveSlotName = TEXT("PauseMenuAutoTest_Empty");
				if (ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem())
				{
					SaveSystem->DeleteGame(false, *Checkpoints->SaveSlotName, 0);
				}
				Test.TestFalse(TEXT("无存档槽位时 HasAvailableCheckpoint 为假"), Checkpoints->HasAvailableCheckpoint());
			}
			Test.TestFalse(TEXT("打开前世界未暂停"), World->IsPaused());
			Test.TestFalse(TEXT("打开前菜单未开启"), PC->IsMenuOpen());

			FGinkgoPauseMenu& Menu = PC->GetOrCreatePauseMenu();
			if (!Test.TestTrue(TEXT("成功打开暂停菜单"), Menu.Show(false)))
			{
				return true;
			}
			Test.TestTrue(TEXT("单机打开暂停菜单后世界进入暂停"), World->IsPaused());
			Test.TestTrue(TEXT("菜单记录主动暂停标记"), Menu.HasPausedWorld());
			Test.TestTrue(TEXT("控制器报告暂停菜单与菜单总状态均为开启"), PC->IsPauseMenuOpen() && PC->IsMenuOpen());
			NextStage();
			return false;
		}

		if (Stage == 1)
		{
			if (SinceStage() < 0.8)
			{
				return false;
			}
			UWeftView* MenuView = PC->GetOrCreatePauseMenu().GetView();
			if (!Test.TestNotNull(TEXT("暂停菜单持有有效 Weft 视图"), MenuView))
			{
				return true;
			}
			if (!bCapturedMenuShot)
			{
				RequestPreviewShot(TEXT("pause_menu"));
				bCapturedMenuShot = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			// 焦点从「继续游戏」下移 3 项到「设置」并回车进入设置子页
			TapWeftKey(*MenuView, TEXT("ArrowDown"));
			TapWeftKey(*MenuView, TEXT("ArrowDown"));
			TapWeftKey(*MenuView, TEXT("ArrowDown"));
			TapWeftKey(*MenuView, TEXT("Enter"));
			NextStage();
			return false;
		}

		if (Stage == 2)
		{
			if (!PC->IsSettingsPageOpen())
			{
				return false;
			}
			if (SinceStage() < 1.0)
			{
				return false;
			}
			Test.TestTrue(TEXT("进入设置子页后世界保持单机暂停"), World->IsPaused());
			Test.TestTrue(TEXT("进入设置子页后父级暂停菜单保持挂起开启状态"), PC->IsPauseMenuOpen());
			UWeftView* SettingsView = PC->GetOrCreateSettingsPage().GetView();
			if (!Test.TestNotNull(TEXT("深色设置子页持有有效 Weft 视图"), SettingsView))
			{
				return true;
			}
			if (!bCapturedSettingsShot)
			{
				RequestPreviewShot(TEXT("settings_dark"));
				bCapturedSettingsShot = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			TapWeftKey(*SettingsView, TEXT("Escape"));
			NextStage();
			return false;
		}

		if (Stage == 3)
		{
			if (PC->IsSettingsPageOpen())
			{
				return false;
			}
			if (SinceStage() < 0.5)
			{
				return false;
			}
			Test.TestTrue(TEXT("关闭设置子页后自动回到暂停菜单"), PC->IsPauseMenuOpen());
			Test.TestTrue(TEXT("回到暂停菜单后世界依然保持暂停"), World->IsPaused());
			UWeftView* MenuView = PC->GetOrCreatePauseMenu().GetView();
			if (!Test.TestNotNull(TEXT("退回后暂停菜单视图仍然有效"), MenuView))
			{
				return true;
			}
			if (!bOpenedQuitModal)
			{
				// 从「设置」下移 1 项到「退出游戏」并回车打开二次确认弹窗
				TapWeftKey(*MenuView, TEXT("ArrowDown"));
				TapWeftKey(*MenuView, TEXT("Enter"));
				bOpenedQuitModal = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			if (!bCapturedModalShot)
			{
				RequestPreviewShot(TEXT("pause_modal"));
				bCapturedModalShot = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			// 第一次 Esc 取消退出弹窗，第二次 Esc 恢复游戏
			TapWeftKey(*MenuView, TEXT("Escape"));
			TapWeftKey(*MenuView, TEXT("Escape"));
			NextStage();
			return false;
		}

		if (PC->IsPauseMenuOpen())
		{
			return false;
		}
		Test.TestFalse(TEXT("退出暂停菜单后世界已解除暂停"), World->IsPaused());
		Test.TestFalse(TEXT("退出暂停菜单后控制器菜单总状态为关闭"), PC->IsMenuOpen());
		Test.TestFalse(TEXT("退出暂停菜单后清除主动暂停标记"), PC->GetOrCreatePauseMenu().HasPausedWorld());
		return true;
	}

private:
	void NextStage()
	{
		++Stage;
		StageStarted = FPlatformTime::Seconds();
	}

	double SinceStage() const
	{
		return FPlatformTime::Seconds() - StageStarted;
	}

	/** 自动化测试宿主，用于跨帧记录断言。 */
	FAutomationTestBase& Test;

	/** 整个场景的墙钟开始时间（秒），不受单机暂停影响。 */
	double Started = FPlatformTime::Seconds();

	/** 当前阶段的墙钟开始时间（秒）。 */
	double StageStarted = 0.0;

	/** 当前执行阶段：0 打开菜单，1 切入设置，2 退回菜单，3 弹出并取消退出确认，4 恢复游戏。 */
	int32 Stage = 0;

	/** 备份的原始检查点槽位名，析构时恢复避免污染默认槽。 */
	FString OriginalSlotName;

	/** 是否已请求暂停菜单主界面预览截图。 */
	bool bCapturedMenuShot = false;

	/** 是否已请求深色设置子页预览截图。 */
	bool bCapturedSettingsShot = false;

	/** 是否已触发退出二次确认弹窗。 */
	bool bOpenedQuitModal = false;

	/** 是否已请求退出二次确认弹窗预览截图。 */
	bool bCapturedModalShot = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPauseMenuFlowTest, "CaseGinkgo.UI.PauseMenu.FlowAndDarkSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoPauseMenuFlowTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/Checkpoint/L_CheckpointTest")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGinkgoPauseMenuScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

#include "GameFramework/GameUserSettings.h"
#include "Misc/AutomationTest.h"
#include "Settings/GinkgoUpscaling.h"
#include "UI/GinkgoVideoSettingsEdit.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoVideoDraftTest, "CaseGinkgo.Settings.Video.Draft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoVideoDraftTest::RunTest(const FString& Parameters)
{
	UGameUserSettings* Machine = NewObject<UGameUserSettings>();
	Machine->SetOverallScalabilityLevel(2);
	Machine->SetFullscreenMode(EWindowMode::WindowedFullscreen);
	Machine->SetVSyncEnabled(false);
	FGinkgoVideoSettingsEdit Edit;
	const FGinkgoUpscaling Native = FGinkgoUpscaling::Query(nullptr);
	Edit.Open(*Machine, Native, false);
	TestFalse(TEXT("无渲染视口不能强开 DLSS"), Edit.Set(TEXT("video.dlss"), TEXT("quality")));
	TestFalse(TEXT("无交换链不能强开插帧"), Edit.Set(TEXT("video.frames"), TEXT("2×")));
	TestFalse(TEXT("不接受任意画质名"), Edit.Set(TEXT("video.quality"), TEXT("invalid")));
	TestTrue(TEXT("无 RTX 仍可降低画质"), Edit.Set(TEXT("video.quality"), TEXT("low")));
	TestTrue(TEXT("可独立降低场景渲染比例"), Edit.Set(TEXT("video.scale"), TEXT("50%")));
	TestEqual(TEXT("确认前不改引擎设置"), Machine->ScalabilityQuality.ShadowQuality, 2);
	TestEqual(TEXT("两个待应用改动"), Edit.CountChanges(), 2);
	Edit.Revert();
	TestEqual(TEXT("撤销恢复整组快照"), Edit.CountChanges(), 0);

	FGinkgoUpscaling Available;
	Available.SuperResolution.Add(TEXT("quality"));
	Available.FrameGeneration.Add(TEXT("2×"));
	Available.bFrameGenerationWithVSync = false;
	Edit.Open(*Machine, Available, true);
	TestFalse(TEXT("PIE 不切换桌面窗口"), Edit.Set(TEXT("video.window"), TEXT("fullscreen")));
	TestTrue(TEXT("可选支持的 DLSS 档位"), Edit.Set(TEXT("video.dlss"), TEXT("quality")));
	TestFalse(TEXT("DLSS 接管比例"), Edit.Set(TEXT("video.scale"), TEXT("50%")));
	TestTrue(TEXT("可单独关闭超分"), Edit.Set(TEXT("video.dlss"), TEXT("off")));
	TestTrue(TEXT("超分关闭后比例可编辑"), Edit.Set(TEXT("video.scale"), TEXT("50%")));
	TestTrue(TEXT("帧生成可独立启用"), Edit.Set(TEXT("video.frames"), TEXT("2×")));
	TestFalse(TEXT("不支持同时启用的 VSync 不可选"), Edit.Set(TEXT("video.vsync"), TEXT("on")));
	Edit.Reset(TEXT("video.frames"));
	TestTrue(TEXT("关闭帧生成后可开 VSync"), Edit.Set(TEXT("video.vsync"), TEXT("on")));
	TestFalse(TEXT("不支持的组合也不能按相反顺序开启"), Edit.Set(TEXT("video.frames"), TEXT("2×")));
	Edit.ResetAll();
	Edit.Revert();
	TestEqual(TEXT("恢复默认也可撤销"), Edit.CountChanges(), 0);
	return true;
}
#endif

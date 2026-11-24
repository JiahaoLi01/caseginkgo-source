#include "Settings/GinkgoUpscaling.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

#if GINKGO_WITH_DLSS
#include "DLSSLibrary.h"
#include "HAL/IConsoleManager.h"
#include "StreamlineLibraryDLSSG.h"
#include "StreamlineRHI.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Widgets/SWindow.h"
#endif
#endif

namespace
{
	const TCHAR* VideoSection = TEXT("CaseGinkgo.Video");

#if GINKGO_WITH_DLSS
	const TPair<const TCHAR*, UDLSSMode> SuperResolutionModes[] = {
		{TEXT("quality"), UDLSSMode::Quality}, {TEXT("balanced"), UDLSSMode::Balanced}, {TEXT("performance"), UDLSSMode::Performance}
	};
	const TPair<const TCHAR*, EStreamlineDLSSGMode> FrameModes[] = {
		{TEXT("2×"), EStreamlineDLSSGMode::On2X}, {TEXT("3×"), EStreamlineDLSSGMode::On3X}, {TEXT("4×"), EStreamlineDLSSGMode::On4X},
		{TEXT("5×"), EStreamlineDLSSGMode::On5X}, {TEXT("6×"), EStreamlineDLSSGMode::On6X}
	};
#endif
}

FString FGinkgoUpscaling::ReadPreference(const TCHAR* Key)
{
	FString Value;
	if (GConfig->GetString(VideoSection, Key, Value, GGameUserSettingsIni))
	{
		return Value;
	}
	// 默认在支持 DLSS 的显卡上启用质量档超分辨率；不支持的设备在 Apply / Open 时自动回退 off。
	return FCString::Stricmp(Key, TEXT("SuperResolution")) == 0 ? TEXT("quality") : TEXT("off");
}

void FGinkgoUpscaling::SetPreference(const TCHAR* Key, const FString& Value)
{
	GConfig->SetString(VideoSection, Key, *Value, GGameUserSettingsIni);
}

FGinkgoUpscaling FGinkgoUpscaling::Query(const UWorld* World)
{
	FGinkgoUpscaling Result;
	Result.SuperResolutionReason = Result.FrameGenerationReason = TEXT("当前版本未包含 NVIDIA DLSS。");
#if GINKGO_WITH_DLSS
	if (!World || !World->IsGameWorld() || !World->GetGameViewport() || !FApp::CanEverRender())
	{
		Result.SuperResolutionReason = Result.FrameGenerationReason = TEXT("需要正在渲染的游戏视口。");
		return Result;
	}
	const UDLSSSupport SR = UDLSSLibrary::QueryDLSSSupport();
	if (SR == UDLSSSupport::Supported)
	{
		for (const auto& Mode : SuperResolutionModes)
		{
			if (UDLSSLibrary::IsDLSSModeSupported(Mode.Value)) { Result.SuperResolution.Add(Mode.Key); }
		}
	}
	Result.SuperResolutionReason = Result.SuperResolution.Num() > 1 ? TEXT("") :
		SR == UDLSSSupport::NotSupportedDriverOutOfDate ? TEXT("请更新 NVIDIA 显卡驱动。") : TEXT("当前显卡或渲染环境不支持 DLSS 超分辨率。");

	const EStreamlineFeatureSupport FG = UStreamlineLibraryDLSSG::QueryDLSSGSupport();
	if (FG != EStreamlineFeatureSupport::Supported)
	{
		switch (FG)
		{
		case EStreamlineFeatureSupport::NotSupportedHardewareSchedulingDisabled: Result.FrameGenerationReason = TEXT("请在 Windows 中开启硬件加速 GPU 计划并重新启动。"); break;
		case EStreamlineFeatureSupport::NotSupportedDriverOutOfDate: Result.FrameGenerationReason = TEXT("请更新 NVIDIA 显卡驱动。"); break;
		case EStreamlineFeatureSupport::NotSupportedByRHI: Result.FrameGenerationReason = TEXT("当前图形接口不支持帧生成。"); break;
		default: Result.FrameGenerationReason = TEXT("当前显卡或系统不支持 DLSS 帧生成。"); break;
		}
		return Result;
	}
	const FStreamlineRHI* RHI = GetPlatformStreamlineRHI();
	if (!RHI || !RHI->HasActiveSwapchainProxies()
#if WITH_EDITOR
		|| RHI->IsUnsupportedPIEActive()
#endif
	)
	{
		Result.FrameGenerationReason = TEXT("当前窗口无法呈现生成帧，请使用独立游戏窗口。");
		return Result;
	}
	for (const auto& Mode : FrameModes)
	{
		if (UStreamlineLibraryDLSSG::IsDLSSGModeSupported(Mode.Value)) { Result.FrameGeneration.Add(Mode.Key); }
	}
	Result.FrameGenerationReason = Result.FrameGeneration.Num() > 1 ? TEXT("") : TEXT("当前驱动未提供可用的帧生成倍率。");
	Result.bFrameGenerationWithVSync = UStreamlineLibraryDLSSG::GetDLSSGIsVsyncSupportAvailable();
#endif
	return Result;
}

void FGinkgoUpscaling::ApplySaved(const UWorld* World)
{
	Apply(World, ReadPreference(TEXT("SuperResolution")), ReadPreference(TEXT("FrameGeneration")));
}

void FGinkgoUpscaling::Apply(const UWorld* World, const FString& Resolution, const FString& FrameGenerationMode)
{
#if GINKGO_WITH_DLSS
	if (!World || !World->GetGameViewport() || !FApp::CanEverRender()) { return; }
	const FGinkgoUpscaling Support = Query(World);
	FVector2D Pixels = FVector2D::ZeroVector;
	World->GetGameViewport()->GetViewportSize(Pixels);
	bool bEnableSR = false;
	float Percentage = 100.f;
	for (const auto& Mode : SuperResolutionModes)
	{
		if (Resolution == Mode.Key && Support.SuperResolution.Contains(Resolution))
		{
			bool bFixed = false;
			float Min, Max, Sharpness;
			UDLSSLibrary::GetDLSSModeInformation(Mode.Value, Pixels, bEnableSR, Percentage, bFixed, Min, Max, Sharpness);
			break;
		}
	}
	static const FName ScaleTag(TEXT("Ginkgo.DLSS"));
	IConsoleVariable* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	if (Scale && bEnableSR)
	{
		Scale->Set(Percentage, EConsoleVariableFlags(ECVF_SetByPluginHighPriority | ECVF_Set_ReplaceExistingTag), ScaleTag);
	}
	else if (Scale)
	{
		// 释放超分的临时比例，回到当前画质组的 sg.ResolutionQuality（包括引擎自动值 0）。
		Scale->Unset(ECVF_SetByPluginHighPriority, ScaleTag);
	}
	if (Support.SuperResolution.Num() > 1) { UDLSSLibrary::EnableDLSS(bEnableSR); }
	if (UStreamlineLibraryDLSSG::IsDLSSGSupported())
	{
		EStreamlineDLSSGMode Selected = EStreamlineDLSSGMode::Off;
		if (Support.FrameGeneration.Contains(FrameGenerationMode) &&
			(Support.bFrameGenerationWithVSync || !GEngine->GetGameUserSettings()->IsVSyncEnabled()))
		{
			for (const auto& Mode : FrameModes)
			{
				if (FrameGenerationMode == Mode.Key) { Selected = Mode.Value; break; }
			}
		}
		UStreamlineLibraryDLSSG::SetDLSSGMode(Selected);
	}
#endif
}

#if WITH_DEV_AUTOMATION_TESTS && GINKGO_WITH_DLSS
/** 只在真实客户端运行：验证画质档的实际屏幕比例、交换链呈现的额外帧，以及切回原生后的状态。 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoVideoPresentationTest, "CaseGinkgo.Settings.Video.Presentation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)
bool FGinkgoVideoPresentationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
	if (!TestNotNull(TEXT("真实游戏视口"), World)) { return false; }
	const FGinkgoUpscaling Support = FGinkgoUpscaling::Query(World);
	AddInfo(FString::Printf(TEXT("DLSS: %s %s; FG: %s %s"), *FString::Join(Support.SuperResolution, TEXT("/")), *Support.SuperResolutionReason,
		*FString::Join(Support.FrameGeneration, TEXT("/")), *Support.FrameGenerationReason));
	const bool bSR = Support.SuperResolution.Contains(TEXT("quality"));
	const bool bFG = Support.FrameGeneration.Contains(TEXT("2×")) && (Support.bFrameGenerationWithVSync || !GEngine->GetGameUserSettings()->IsVSyncEnabled());
	if (bFG)
	{
		// Streamline 会在后台窗口停止插帧。呈现测试需要让操作系统激活本次运行的真实游戏窗口。
		if (const TSharedPtr<SWindow> Window = World->GetGameViewport()->GetWindow())
		{
			Window->BringToFront(true);
			Window->HACK_ForceToFront();
		}
	}
	IConsoleVariable* Scale = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
	FGinkgoUpscaling::Apply(World, TEXT("off"), TEXT("off"));
	const float NativeScale = Scale->GetFloat();
	FGinkgoUpscaling::Apply(World, TEXT("quality"), TEXT("2×"));
	if (bSR)
	{
		TestTrue(TEXT("DLSS 已启用"), UDLSSLibrary::IsDLSSEnabled());
		TestTrue(TEXT("质量档使用低于原生的内部比例"), Scale->GetFloat() > 0.f && Scale->GetFloat() < 100.f);
		AddInfo(FString::Printf(TEXT("DLSS quality screen percentage: %.2f"), Scale->GetFloat()));
	}
	const double Started = FPlatformTime::Seconds();
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, WeakWorld = TWeakObjectPtr<UWorld>(World), bSR, bFG, Scale, NativeScale, Started]()
	{
		float FrameRate = 0.f;
		int32 Presented = 0;
		if (bFG)
		{
			UStreamlineLibraryDLSSG::GetDLSSGFrameTiming(FrameRate, Presented);
			// 首次启用要创建 NGX 推理资源；首帧可能单独耗去数秒，等待交换链上报实际结果。
			if (Presented < 2 && FPlatformTime::Seconds() - Started < 60.) { return false; }
			TestTrue(TEXT("交换链实际呈现了生成帧"), Presented >= 2);
			AddInfo(FString::Printf(TEXT("DLSS-G 2x presented frames: %d"), Presented));
		}
		FGinkgoUpscaling::Apply(WeakWorld.Get(), TEXT("off"), TEXT("off"));
		TestEqual(TEXT("关闭后恢复之前的非 DLSS 渲染比例"), Scale->GetFloat(), NativeScale);
		if (bSR) { TestFalse(TEXT("超分已关闭"), UDLSSLibrary::IsDLSSEnabled()); }
		FGinkgoUpscaling::ApplySaved(WeakWorld.Get());
		return true;
	}));
	return true;
}
#endif

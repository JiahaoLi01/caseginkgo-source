#include "UI/GinkgoToastHud.h"

#include "Control/GinkgoPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"                // 视口的 DPI 缩放：页面第一次布局用的大小
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "SWeftViewport.h"
#include "UI/GinkgoUiSounds.h"
#include "Utilities/Debug/DebugCategory.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

namespace
{
	using FToastJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	using FToastJsonWriterFactory = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	/** 页面的目录：Content/UI 随包（DefaultGame.ini 的 DirectoriesToAlwaysStageAsUFS），打包后从 pak 读 */
	const TCHAR* ToastPageDirectory = TEXT("UI/Toast");

	/** 着色器效果的清单，同样随包。字节码由 Scripts/UI/build_ui_effects.py 编好放在旁边 */
	const TCHAR* ToastEffectsManifest = TEXT("UI/Effects/effects.json");

	/** 控件在视口上的层级：在武器的界面（5）、交互的提示（10）之上，窗口时间轴（20）、设置界面（40）之下 */
	constexpr int32 ToastWidgetZOrder = 12;

	/** 图标的枚举名，页面按它选图标：枚举的显示名是元数据，打包的游戏里没有 */
	FString IconName(const EGinkgoToastIcon Icon)
	{
		return StaticEnum<EGinkgoToastIcon>()->GetNameStringByValue(static_cast<int64>(Icon));
	}

	int32 ToMilliseconds(const float Seconds)
	{
		return FMath::RoundToInt(Seconds * 1000.f);
	}
}

FGinkgoToastHud::FGinkgoToastHud(AGinkgoPlayerController& InController)
	: Controller(&InController)
{
}

FGinkgoToastHud::~FGinkgoToastHud()
{
	Hide();
}

bool FGinkgoToastHud::Show()
{
	if (IsShown())
	{
		return true;
	}
	AGinkgoPlayerController* PlayerController = Controller.Get();
	const UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	UWeftSubsystem* Weft = UWeftSubsystem::Get();
	UWeftView* NewView = Viewport && Weft && Weft->IsAvailable() ? Weft->CreateView() : nullptr;
	if (!NewView)
	{
		// 无界面的测量里每次都会走到这里：不是问题，不报警告
		UE_LOG(LogGinkgoUI, Log, TEXT("[Toast] 不显示 toast：%s"),
			!Viewport ? TEXT("没有游戏视口（无界面运行）") : TEXT("weft 在这里不运行（-nullrhi，或没有编出 weft 的运行时库）"));
		return false;
	}
	Weft->AddEffects(FPaths::ProjectContentDir() / ToastEffectsManifest);
	View.Reset(NewView);
	ViewportClient = Viewport;
	// 只显示、不接指针：页面铺满视口，鼠标与键盘照常交给游戏
	Widget = SNew(SWeftViewport).View(NewView).Interactive(false);
	PlayerController->AddGameplayHudWidget(Widget.ToSharedRef(), ToastWidgetZOrder);
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FGinkgoToastHud::OnWorldPostActorTick);
	return true;
}

void FGinkgoToastHud::Hide()
{
	if (!IsShown())
	{
		return;
	}
	FWorldDelegates::OnWorldPostActorTick.Remove(PostActorTickHandle);
	PostActorTickHandle.Reset();
	if (auto* PC = Controller.Get(); PC && Widget)
	{
		PC->RemoveGameplayHudWidget(Widget.ToSharedRef());
	}
	Widget.Reset();
	View.Reset();
	ViewportClient.Reset();
	bLoaded = false;
	Queue = FGinkgoToastQueue();
}

void FGinkgoToastHud::Push(const FGinkgoToast& Toast)
{
	if (!IsShown())
	{
		return;
	}
	Queue.Push(Toast);
	UE_LOG(LogGinkgoUI, Log, TEXT("[Toast] 排队：%s（前面还有 %d 条等着）"), *Toast.Title.ToString(), Queue.GetNumPending() - 1);
}

void FGinkgoToastHud::OnWorldPostActorTick(UWorld* InWorld, const ELevelTick TickType, const float DeltaSeconds)
{
	const AGinkgoPlayerController* PlayerController = Controller.Get();
	if (!PlayerController || InWorld != PlayerController->GetWorld() || !View || TickType == LEVELTICK_TimeOnly)
	{
		return;
	}
	FVector2D Pixels = FVector2D::ZeroVector;
	if (const UGameViewportClient* Viewport = ViewportClient.Get())
	{
		Viewport->GetViewportSize(Pixels);
	}
	if (Pixels.X < 2.0 || Pixels.Y < 2.0)
	{
		return;
	}
	// 视口有了大小才载入：View 一开始是 1 × 1，页面第一次布局在那么窄的宽度上
	if (!bLoaded)
	{
		const float DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt(Pixels.X), FMath::RoundToInt(Pixels.Y)));
		View->Resize(Pixels / DPIScale, DPIScale);
		const FString Directory = FPaths::ProjectContentDir() / ToastPageDirectory;
		if (!View->LoadDirectory(Directory))
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Toast] toast 的页面载入失败：%s（原因见 LogWeft）"), *Directory);
			Hide();
			return;
		}
		bLoaded = true;
	}

	if (InWorld->IsPaused() || PlayerController->IsMenuOpen() || PlayerController->IsGameplayHudSuppressed())
	{
		return;
	}
	Clock += FApp::GetDeltaTime();
	const FGinkgoToastTimings& Timings = UGinkgoDeveloperSettings::Get()->ToastTimings;
	switch (Queue.Advance(Clock, Timings))
	{
	case FGinkgoToastQueue::EStep::Show:
		SendShow(*Queue.GetCurrent(), Timings);
		break;
	case FGinkgoToastQueue::EStep::Hide:
		SendHide(Timings);
		break;
	case FGinkgoToastQueue::EStep::None:
		break;
	}
}

void FGinkgoToastHud::SendShow(const FGinkgoToast& Toast, const FGinkgoToastTimings& Timings)
{
	FString Message;
	const TSharedRef<FToastJsonWriter> Writer = FToastJsonWriterFactory::Create(&Message);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("icon"), IconName(Toast.Icon));
	Writer->WriteValue(TEXT("title"), Toast.Title.ToString());
	Writer->WriteValue(TEXT("body"), Toast.Body.ToString());
	Writer->WriteValue(TEXT("inMs"), ToMilliseconds(Timings.In));
	Writer->WriteObjectEnd();
	Writer->Close();
	View->Emit(TEXT("toast-show"), Message, false);

	const AGinkgoPlayerController* PlayerController = Controller.Get();
	UGinkgoUiSoundSubsystem::Play(PlayerController, EGinkgoUiSound::Notification);
	const UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	UE_LOG(LogGinkgoUI, Log, TEXT("[Toast] 显示：%s，%s，队列时钟 %.3f s，世界时间 %.3f s，还有 %d 条等着"), *IconName(Toast.Icon), *Toast.Title.ToString(), Clock,
		World ? World->GetUnpausedTimeSeconds() : 0.0, Queue.GetNumPending());
}

void FGinkgoToastHud::SendHide(const FGinkgoToastTimings& Timings)
{
	FString Message;
	const TSharedRef<FToastJsonWriter> Writer = FToastJsonWriterFactory::Create(&Message);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("outMs"), ToMilliseconds(Timings.Out));
	Writer->WriteObjectEnd();
	Writer->Close();
	View->Emit(TEXT("toast-hide"), Message, false);
	const AGinkgoPlayerController* PlayerController = Controller.Get();
	const UWorld* World = PlayerController ? PlayerController->GetWorld() : nullptr;
	UE_LOG(LogGinkgoUI, Log, TEXT("[Toast] 消失，队列时钟 %.3f s，世界时间 %.3f s"), Clock, World ? World->GetUnpausedTimeSeconds() : 0.0);
}

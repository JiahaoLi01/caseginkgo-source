#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAIController.h"
#include "Algo/MaxElement.h"
#include "Character/GinkgoCharacter.h"
#include "Components/BoxComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HighResScreenshot.h"
#include "Misc/CommandLine.h"
#include "Presentation/GinkgoCreditVolume.h"
#include "SWeftViewport.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "UI/GinkgoCreditsHud.h"
#include "UI/GinkgoSettingsPage.h"
#include "WeftView.h"

/** 真实 PIE 胶囊进入、排队、遮挡暂停与资源释放；可额外输出实际 Weft 高斯模糊渲染帧。 */
class FGinkgoCreditsWorldScenario : public IAutomationLatentCommand
{
public:
	explicit FGinkgoCreditsWorldScenario(FAutomationTestBase& InTest) : Test(InTest), Original(UGinkgoDeveloperSettings::Get()->Credits) {}
	virtual ~FGinkgoCreditsWorldScenario() override { GetMutableDefault<UGinkgoDeveloperSettings>()->Credits = Original; }

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 45.)
		{
			Test.AddError(FString::Printf(TEXT("署名 PIE 回归超时，阶段 %d"), Stage));
			return true;
		}
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		auto* PC = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
		auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
		if (!Player || !World->HasBegunPlay() || World->GetTimeSeconds() < 1.) { return false; }
		for (TActorIterator<AGinkgoAIController> It(World); It; ++It) { It->StopAI(); }
		if (FParse::Param(FCommandLine::Get(), TEXT("CreditsProfile")))
		{
			return Profile(*PC);
		}
		if (Stage == 0)
		{
			Test.TestFalse(TEXT("未触发时没有署名页面或队列"), PC->CreditsHud.IsValid());
			auto& Settings = GetMutableDefault<UGinkgoDeveloperSettings>()->Credits;
			Settings.Stagger = .8f;
			Settings.Reveal = 1.2f;
			Settings.Hold = 1.5f;
			Settings.FadeOut = .8f;
			Volume = World->SpawnActor<AGinkgoCreditVolume>(Player->GetActorLocation() + FVector(800., 0., 0.), FRotator::ZeroRotator);
			Volume->Credits.Reset();
			Test.TestFalse(TEXT("空内容不激活"), Volume->TryActivate(Player));
			Test.TestFalse(TEXT("空内容没有创建 HUD"), PC->CreditsHud.IsValid());
			Test.TestFalse(TEXT("非玩家不激活"), Volume->TryActivate(nullptr));
			// 首个 Name 留空：从真实 Volume 经拥有者 RPC 展示多人及作品，验证旧的单姓名过滤已解除。
			FGinkgoCredit Group;
			Group.Role = FText::FromString(TEXT("SPECIAL THANKS"));
			Group.Name = FText::GetEmpty();
			Group.AdditionalContributors = {
				{FText::FromString(TEXT("JIAHAO LI")), false},
				{FText::FromString(TEXT("ALEX LIN")), false},
				{FText::FromString(TEXT("A STUDY IN LIGHT")), true}
			};
			Volume->Credits.Add(Group);
			FGinkgoCredit Second;
			Second.Role = FText::FromString(TEXT("GAME DESIGN"));
			Volume->Credits.Add(Second);
			const auto Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
			const bool bCursor = PC->ShouldShowMouseCursor();
			Move(*Player, Volume->GetActorLocation());
			if (!Test.TestTrue(TEXT("真实胶囊进入体积即创建本地署名"), PC->CreditsHud.IsValid())) { return true; }
			Test.TestFalse(TEXT("署名不隐藏普通 HUD"), PC->IsGameplayHudSuppressed());
			Test.TestTrue(TEXT("署名不改变焦点和光标"), Focus == FSlateApplication::Get().GetKeyboardFocusedWidget() && bCursor == PC->ShouldShowMouseCursor());
			Test.TestFalse(TEXT("停在体积内重复通知不重复排队"), Volume->TryActivate(Player));
			Next();
			return false;
		}
		auto& Hud = *PC->CreditsHud;
		if (Stage == 1)
		{
			if (!Hud.bLoaded || !Hud.Queue.HasCurrent() || SinceStage() < .15) { return false; }
			Test.TestTrue(TEXT("实际 Weft 页面产生绘制包"), Hud.View->MakePacket().IsValid());
			Test.TestFalse(TEXT("署名透明层不参与鼠标命中"), Hud.Widget->GetVisibility().IsHitTestVisible());
			Capture(Hud);
			Hud.OnWorldPostActorTick(GEditor->GetEditorWorldContext().World(), LEVELTICK_All, 1.f);
			CheckFrozen(Hud, TEXT("无关 World 不推进署名"));
			FGinkgoCheckpointAnnouncement Location;
			Location.LocationName = FText::FromString(TEXT("CREDITS PAUSE TEST"));
			Location.Sound.Reset();
			auto* Settings = GetMutableDefault<UGinkgoDeveloperSettings>();
			const auto Previous = Settings->LocationTitle;
			Settings->LocationTitle.FadeIn = Settings->LocationTitle.FadeOut = 0.f;
			Settings->LocationTitle.Hold = .4f;
			PC->ClientAnnounceCheckpoint(Location);
			Settings->LocationTitle = Previous;
			Next();
			return false;
		}
		if (Stage == 2)
		{
			if (PC->IsGameplayHudSuppressed()) { CheckFrozen(Hud, TEXT("地点标注期间保留逐字进度")); return false; }
			Next();
			return false;
		}
		if (Stage == 3)
		{
			float Minimum = 1.f;
			float Maximum = 0.f;
			for (int32 Index = 0; Index < Hud.Queue.GetGlyphs().Num(); ++Index)
			{
				if (Hud.Queue.GetGlyphs()[Index].Text.TrimStartAndEnd().IsEmpty()) { continue; }
				Minimum = FMath::Min(Minimum, Hud.Queue.GetGlyphAlpha(Index));
				Maximum = FMath::Max(Maximum, Hud.Queue.GetGlyphAlpha(Index));
			}
			if (!bBlurShot && Maximum > .5f && Minimum < .3f) { Shot(TEXT("reveal")); bBlurShot = true; }
			if (!bClearShot && Minimum == 1.f && Hud.Queue.GetOpacity() == 1.f) { Shot(TEXT("clear")); bClearShot = true; }
			if (!bFadeShot && Hud.Queue.GetOpacity() > .25f && Hud.Queue.GetOpacity() < .65f) { Shot(TEXT("fade")); bFadeShot = true; }
			if (Hud.Serial < 2) { return false; }
			Test.TestTrue(TEXT("第一条实际经过随机显现、停留和统一淡出"), bBlurShot && bClearShot && bFadeShot);
			Test.TestTrue(TEXT("第二条按序开始"), Hud.Queue.HasCurrent());
			Capture(Hud);
			Test.TestTrue(TEXT("打开实际设置页"), PC->GetOrCreateSettingsPage().Show());
			Next();
			return false;
		}
		if (Stage == 4)
		{
			CheckFrozen(Hud, TEXT("设置页期间暂停署名计时"));
			if (SinceStage() < .3) { return false; }
			Test.TestTrue(TEXT("设置页遮挡时署名页面隐藏"), Hud.Widget->GetVisibility() == EVisibility::Collapsed);
			PC->GetOrCreateSettingsPage().Hide();
			Next();
			return false;
		}
		if (Stage == 5)
		{
			if (Hud.Queue.IsActive()) { return false; }
			Test.TestFalse(TEXT("空队列释放 Weft View"), Hud.View.IsValid());
			Test.TestFalse(TEXT("空队列释放 Slate 控件"), Hud.Widget.IsValid());
			Test.TestFalse(TEXT("空队列不订阅世界 Tick"), Hud.TickHandle.IsValid());
			Move(*Player, Volume->GetActorLocation() + FVector(500., 0., 0.));
			Move(*Player, Volume->GetActorLocation());
			Test.TestFalse(TEXT("默认离开再进入不会重复播放"), Hud.Queue.IsActive());
			Volume->bRepeatable = true;
			Volume->Credits.SetNum(1);
			auto& Settings = GetMutableDefault<UGinkgoDeveloperSettings>()->Credits;
			Settings.Stagger = Settings.Reveal = Settings.FadeOut = 0.f;
			Settings.Hold = .1f;
			Move(*Player, Volume->GetActorLocation() + FVector(500., 0., 0.));
			Move(*Player, Volume->GetActorLocation());
			Test.TestTrue(TEXT("Repeatable 离开重进会重新挂载页面"), Hud.Queue.IsActive() && Hud.View.IsValid() && Hud.TickHandle.IsValid());
			Next();
			return false;
		}
		if (Hud.Queue.IsActive()) { return false; }
		Test.TestFalse(TEXT("复用后也完整释放展示资源"), Hud.View.IsValid() || Hud.Widget.IsValid() || Hud.TickHandle.IsValid());
		return true;
	}

private:
	/** 可选实机对照：同一 PIE 场景、相同时间参数，逐帧记录短署名和多人长署名的整帧墙钟耗时。 */
	bool Profile(AGinkgoPlayerController& PC)
	{
		const double Now = FPlatformTime::Seconds();
		if (Stage == 0)
		{
			auto& Settings = GetMutableDefault<UGinkgoDeveloperSettings>()->Credits;
			Settings.Stagger = .55f;
			Settings.Reveal = 2.f;
			Settings.Hold = 2.f;
			Settings.FadeOut = 1.f;
			if (FParse::Param(FCommandLine::Get(), TEXT("CreditsProfileNoBlur"))) { Settings.Blur = 0.f; }
			FGinkgoCredit Short;
			FGinkgoCredit Long = Short;
			for (const TCHAR* Name : {TEXT("CHARACTER AND ENVIRONMENT ART"), TEXT("ANIMATION AND CINEMATIC DIRECTION"),
				TEXT("ORIGINAL MUSIC AND ORCHESTRATION"), TEXT("AN ILLUSTRATED STUDY OF THE CITY")})
			{
				Long.AdditionalContributors.Add({FText::FromString(Name), true});
			}
			PC.ClientShowCredits({Short, Long, Short, Long});
			Stage = 1;
			PreviousTime = Now;
			return false;
		}
		auto& Hud = *PC.CreditsHud;
		if (PreviousPhase != NAME_None)
		{
			Timings.FindOrAdd(PreviousPhase).Add((Now - PreviousTime) * 1000.);
		}
		PreviousPhase = NAME_None;
		PreviousTime = Now;
		if (Hud.Queue.HasCurrent())
		{
			const auto& Glyphs = Hud.Queue.GetGlyphs();
			const auto* Latest = Algo::MaxElementBy(Glyphs, &FGinkgoCreditGlyph::Delay);
			const bool bRevealing = Latest && Hud.Queue.GetGlyphAlpha(Latest - Glyphs.GetData()) < 1.f;
			PreviousPhase = FName(*FString::Printf(TEXT("%d-%s"), Glyphs.Num(),
				bRevealing ? TEXT("Reveal") : Hud.Queue.GetOpacity() < 1.f ? TEXT("Fade") : TEXT("Hold")));
		}
		if (Hud.Queue.IsActive()) { return false; }
		for (auto& Pair : Timings)
		{
			Pair.Value.Sort();
			const int32 Count = Pair.Value.Num();
			Test.AddInfo(FString::Printf(TEXT("CreditsFrame %s n=%d p50=%.2f p95=%.2f ms"), *Pair.Key.ToString(),
				Count, Pair.Value[Count / 2], Pair.Value[FMath::FloorToInt((Count - 1) * .95)]));
		}
		Test.TestEqual(TEXT("长短署名均采到显现、停留、淡出三阶段"), Timings.Num(), 6);
		return true;
	}

	/** 上次采样的实际秒数，包含游戏、Slate 及渲染等待。 */
	double PreviousTime = 0.;

	/** 上次采样时正在显示的阶段，空值跳过条目切换。 */
	FName PreviousPhase;

	/** 性能模式使用的各阶段整帧耗时，单位毫秒。 */
	TMap<FName, TArray<double>> Timings;

	static void Move(AGinkgoCharacter& Player, const FVector& Location)
	{
		Player.GetCharacterMovement()->StopMovementImmediately();
		Player.SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}

	void Capture(const FGinkgoCreditsHud& Hud)
	{
		Progress.Reset();
		for (int32 Index = 0; Index < Hud.Queue.GetGlyphs().Num(); ++Index) { Progress.Add(Hud.Queue.GetGlyphAlpha(Index)); }
	}

	void CheckFrozen(const FGinkgoCreditsHud& Hud, const TCHAR* Label)
	{
		for (int32 Index = 0; Index < Progress.Num(); ++Index) { Test.TestEqual(Label, Hud.Queue.GetGlyphAlpha(Index), Progress[Index]); }
	}

	static void Shot(const TCHAR* Name)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("CreditsPreview")))
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Credits") / Name, true, false);
		}
	}

	void Next() { ++Stage; StageStarted = FPlatformTime::Seconds(); }
	double SinceStage() const { return FPlatformTime::Seconds() - StageStarted; }

	/** 跨帧断言的归属。 */
	FAutomationTestBase& Test;

	/** 测试只临时修改内存默认对象，退出恢复，绝不 SaveConfig。 */
	FGinkgoCreditsSettings Original;

	/** 超时保护采用实际秒数，不受游戏暂停影响。 */
	double Started = FPlatformTime::Seconds();

	/** 当前阶段的实际开始秒数。 */
	double StageStarted = 0.;

	/** 进入、地点暂停、展示、设置暂停、清理和再次进入的阶段。 */
	int32 Stage = 0;

	/** 仅在此 PIE 世界存在的触发体积。 */
	TWeakObjectPtr<AGinkgoCreditVolume> Volume;

	/** 暂停前的逐字采样，期间必须保持完全一致。 */
	TArray<float> Progress;

	/** 模糊阶段截图已经请求，避免每帧覆盖。 */
	bool bBlurShot = false;

	/** 完全清晰阶段截图已经请求。 */
	bool bClearShot = false;

	/** 整体淡出阶段截图已经请求。 */
	bool bFadeShot = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCreditsWorldTest, "CaseGinkgo.UI.Credits.VolumePresentationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoCreditsWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/Checkpoint/L_CheckpointTest")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGinkgoCreditsWorldScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

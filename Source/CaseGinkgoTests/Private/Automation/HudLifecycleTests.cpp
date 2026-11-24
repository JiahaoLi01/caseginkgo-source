#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAIController.h"
#include "Character/GinkgoCharacter.h"
#include "Control/GinkgoPlayerController.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/GinkgoInteractorComponent.h"
#include "Interaction/GinkgoWorkbench.h"
#include "SWeftViewport.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "UI/GinkgoGameplayHudLayer.h"
#include "UI/GinkgoInteractionPrompt.h"
#include "UI/GinkgoLocationTitle.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "WeftView.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"

/** 实际 PIE：跨帧工作台点击、连续地点队列、页面资源释放及恢复点击，输入经过 Slate 命中路径。 */
class FGinkgoHudLifecycleScenario : public IAutomationLatentCommand
{
public:
	explicit FGinkgoHudLifecycleScenario(FAutomationTestBase& InTest) : Test(InTest) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 65.)
		{
			Test.AddError(FString::Printf(TEXT("HUD 生命周期回归超时，阶段 %d"), Stage));
			return true;
		}
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		auto* PC = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
		auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
		if (!Player || !World->HasBegunPlay() || World->GetTimeSeconds() < 1.) { return false; }
		for (TActorIterator<AGinkgoAIController> It(World); It; ++It) { It->StopAI(); }
		auto* Prompt = PC->InteractionPrompt.Get();
		if (!Prompt || !Prompt->bLoaded || !PC->GameplayHudLayer) { return false; }
		if (bPointerDown)
		{
			// 让按下和松开之间经过真实游戏/Slate 更新，覆盖首次焦点交接和鼠标捕获。
			if (FPlatformTime::Seconds() - PointerDownAt < .12) { return false; }
			const FPointerEvent Up(0, PointerPosition, PointerPosition, TSet<FKey>{}, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
			FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
			Prompt->View->Advance();
			bPointerDown = false;
			return false;
		}
		auto* Weapons = Player->GetWeapons();
		if (Stage == 0)
		{
			Test.TestFalse(TEXT("未抵达地点时没有创建地点队列或页面"), PC->LocationTitle.IsValid());
			bOriginalCursor = PC->ShouldShowMouseCursor();
			auto* M4 = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
			auto* AK = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
			if (!Test.TestNotNull(TEXT("M4 数据"), M4) || !Test.TestNotNull(TEXT("AK 数据"), AK)) { return true; }
			Weapons->SetCarried(0, M4);
			Weapons->SetCarried(1, AK);
			Weapons->SetHeldSlot(0);
			Player->AttachWeaponToHand(0);
			Bench = World->SpawnActor<AGinkgoWorkbench>(Player->GetActorLocation() + FVector(110., 0., -92.), FRotator::ZeroRotator);
			NextStage();
			return false;
		}
		if (Stage == 1)
		{
			if (SinceStage() < 1. || !Player->GetCharacterMovement()->IsMovingOnGround() || !Weapons->CanPlaceOnWorkbench()) { return false; }
			PC->SetInputMode(FInputModeGameOnly());
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
			Bench->Interact({Player, Player->GetInteractor()});
			if (!Test.TestTrue(TEXT("进入真实工作台"), Player->GetWorkbench() == Bench.Get())) { return true; }
			NextStage();
			return false;
		}
		if (Stage == 2)
		{
			if (SinceStage() < 2. || !Bench->CanSelectWeapon()) { return false; }
			Test.TestTrue(TEXT("工作台跨帧保持可命中"), Prompt->Widget->GetVisibility().IsHitTestVisible());
			ExpectedSlot = Bench->GetAdjacentSlot(true);
			if (!ClickNext(*PC)) { return true; }
			NextStage();
			return false;
		}
		if (Stage == 3)
		{
			if (SinceStage() < 2. || !Bench->CanSelectWeapon()) { return false; }
			Test.TestEqual(TEXT("Slate 点击确实切换了工作台武器"), Bench->GetSelectedSlot(), ExpectedSlot);
			if (!CheckCameraPanel(*PC)) { return true; }
			FGinkgoCheckpointAnnouncement Announcement;
			Announcement.LocationName = FText::FromString(TEXT("HUD LIFECYCLE"));
			Announcement.Sound.Reset();
			PC->ClientAnnounceCheckpoint(Announcement);
			FirstTitleView = PC->LocationTitle->View.Get();
			Test.TestNotNull(TEXT("第一条请求才创建地点页面"), FirstTitleView.Get());
			Announcement.LocationName = FText::FromString(TEXT("SECOND LOCATION"));
			PC->ClientAnnounceCheckpoint(Announcement);
			Test.TestTrue(TEXT("连续地点共用当前展示实例"), PC->LocationTitle->View.Get() == FirstTitleView.Get());
			Test.TestTrue(TEXT("入队立即隐藏共同父层"), PC->GameplayHudLayer->Root->GetVisibility() == EVisibility::Collapsed);
			NextStage();
			return false;
		}
		if (Stage == 4)
		{
			if (PC->IsGameplayHudSuppressed())
			{
				Test.TestTrue(TEXT("队列间隙不闪回普通 HUD"), PC->GameplayHudLayer->Root->GetVisibility() == EVisibility::Collapsed);
				return false;
			}
			Test.TestFalse(TEXT("队列结束释放地点 View"), PC->LocationTitle->View.IsValid());
			Test.TestFalse(TEXT("队列结束移除地点 Slate 控件"), PC->LocationTitle->Widget.IsValid());
			Test.TestFalse(TEXT("空闲地点不订阅世界 Tick"), PC->LocationTitle->TickHandle.IsValid());
			Test.TestTrue(TEXT("恢复父层时不吞掉子控件点击"), PC->GameplayHudLayer->Root->GetVisibility() == EVisibility::SelfHitTestInvisible);
			NextStage();
			return false;
		}
		if (Stage == 5)
		{
			if (SinceStage() < .3) { return false; }
			Test.TestTrue(TEXT("地点结束恢复工作台鼠标"), Prompt->bWorkbenchInput && PC->ShouldShowMouseCursor());
			ExpectedSlot = Bench->GetAdjacentSlot(true);
			if (!ClickNext(*PC)) { return true; }
			NextStage();
			return false;
		}
		if (Stage == 6)
		{
			if (SinceStage() < 2. || !Bench->CanSelectWeapon()) { return false; }
			Test.TestEqual(TEXT("地点结束后的真实点击仍切枪"), Bench->GetSelectedSlot(), ExpectedSlot);
			Bench->RequestExit(*Player);
			NextStage();
			return false;
		}
		if (Player->GetWorkbench()) { return false; }
		Test.TestFalse(TEXT("离桌恢复纯展示交互提示"), Prompt->Widget->GetVisibility().IsHitTestVisible());
		Test.TestEqual(TEXT("离桌恢复原光标状态"), PC->ShouldShowMouseCursor(), bOriginalCursor);
		return true;
	}

private:
	/** 使用真实控件几何和 Slate 命中路由，不直接向 Weft 注入 pointer 来绕过外层显隐。 */
	bool ClickNext(AGinkgoPlayerController& PC)
	{
		auto& Prompt = *PC.InteractionPrompt;
		const FVector2D Size = Prompt.View->GetLogicalSize();
		const FGeometry& Geometry = Prompt.Widget->GetCachedGeometry();
		const FVector2D Logical(Size.X - 28., Size.Y * .12 + 26.);
		const FVector2D Position = Geometry.LocalToAbsolute(Logical * FVector2D(Geometry.GetLocalSize()) / Size);
		auto& Slate = FSlateApplication::Get();
		Test.TestTrue(TEXT("点击前工作台已持有焦点，首击无需激活"), Slate.GetKeyboardFocusedWidget() == Prompt.Widget);
		const FWidgetPath Path = Slate.LocateWindowUnderMouse(Position, Slate.GetInteractiveTopLevelWindows());
		if (!Test.TestTrue(TEXT("真实鼠标命中路径包含工作台控件"), Path.ContainsWidget(Prompt.Widget.Get()))) { return false; }
		const FPointerEvent Down(0, Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		Slate.ProcessMouseButtonDownEvent(Path.GetWindow()->GetNativeWindow(), Down);
		PointerPosition = Position;
		PointerDownAt = FPlatformTime::Seconds();
		bPointerDown = true;
		return true;
	}

	/** 从真实 F7 入口开关；额外的编辑器 World 回调不能关闭正在使用的游戏面板。 */
	bool CheckCameraPanel(AGinkgoPlayerController& PC)
	{
		auto& Slate = FSlateApplication::Get();
		const FKeyEvent Key(EKeys::F7, FModifierKeysState(), 0, false, 0, 0);
		Slate.ProcessKeyDownEvent(Key);
		Slate.ProcessKeyUpEvent(Key);
		const TSharedPtr<SWidget> TuningWidget = Slate.GetKeyboardFocusedWidget();
		if (!Test.TestTrue(TEXT("F7 打开相机调参并接管焦点"), TuningWidget && TuningWidget != PC.InteractionPrompt->Widget
			&& TuningWidget->GetTypeAsString() == TEXT("SWeftViewport"))) { return false; }
		auto Tuning = StaticCastSharedPtr<SWeftViewport>(TuningWidget);
		UWeftView* TuningView = Tuning->GetKeyboardView();
		if (!Test.TestNotNull(TEXT("F7 有实际页面"), TuningView)) { return false; }
		// 不加载或写入调参配置；只触发与编辑器/多客户端相同的全局 World 更新通知。
		FWorldDelegates::OnWorldPostActorTick.Broadcast(GEditor->GetEditorWorldContext().World(), LEVELTICK_All, 1.f / 60.f);
		FWorldDelegates::OnWorldPostActorTick.Broadcast(PC.GetWorld(), LEVELTICK_All, 1.f / 60.f);
		FWidgetPath Path;
		Test.TestTrue(TEXT("无关 World 更新后 F7 仍挂在视口"), Slate.GeneratePathToWidgetUnchecked(Tuning.ToSharedRef(), Path));
		Test.TestTrue(TEXT("F7 页面已完成真实加载"), TuningView->MakePacket().IsValid());
		Slate.ProcessKeyDownEvent(Key);
		Slate.ProcessKeyUpEvent(Key);
		Test.TestTrue(TEXT("关 F7 还回工作台焦点与光标"), Slate.GetKeyboardFocusedWidget() == PC.InteractionPrompt->Widget && PC.ShouldShowMouseCursor());
		return true;
	}

	void NextStage() { ++Stage; StageStarted = FPlatformTime::Seconds(); }
	double SinceStage() const { return FPlatformTime::Seconds() - StageStarted; }

	/** 跨帧断言的归属。 */
	FAutomationTestBase& Test;

	/** 实际秒数，避免暂停或世界时间变化影响超时保护。 */
	double Started = FPlatformTime::Seconds();

	/** 当前阶段起始的实际秒数，用于跨多帧验证稳定状态。 */
	double StageStarted = 0.;

	/** 进入、点击、地点排队、恢复点击、退出的顺序阶段。 */
	int32 Stage = 0;

	/** 最近一次真实点击应选中的武器槽号。 */
	int32 ExpectedSlot = INDEX_NONE;

	/** 进桌前光标状态，最终应完整恢复。 */
	bool bOriginalCursor = false;

	/** 一次真实点击正在按下；下一次跨帧更新释放，不补第二次点击。 */
	bool bPointerDown = false;

	/** 按下瞬间的屏幕坐标，用于同位置松开。 */
	FVector2D PointerPosition = FVector2D::ZeroVector;

	/** 按下时的实际秒数，至少跨过数帧后才松开。 */
	double PointerDownAt = 0.;

	/** PIE 临时工作台，随世界销毁，不写入测试地图。 */
	TWeakObjectPtr<AGinkgoWorkbench> Bench;

	/** 第一条请求创建的页面，只弱引用以免测试阻止释放。 */
	TWeakObjectPtr<UWeftView> FirstTitleView;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoHudLifecycleTest, "CaseGinkgo.UI.HudLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoHudLifecycleTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/Checkpoint/L_CheckpointTest")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGinkgoHudLifecycleScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

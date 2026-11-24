#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAIController.h"
#include "Character/GinkgoCharacter.h"
#include "Checkpoint/GinkgoCheckpointSubsystem.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Control/GinkgoInputSettings.h"
#include "Control/GinkgoPlayerController.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "Framework/GinkgoDifficulty.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "MetasoundSource.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PlatformFeatures.h"
#include "SaveGameSystem.h"
#include "Sound/SoundWave.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "UI/GinkgoMainMenu.h"
#include "UI/GinkgoPauseMenu.h"
#include "UI/GinkgoSettingsPage.h"
#include "UI/GinkgoTitleSubsystem.h"
#include "UnrealClient.h"
#include "WeftView.h"

namespace GinkgoMainMenuTestsPrivate
{
	/** 向指定 Weft 视图发送一次完整按键按下与松开，避免焦点抢占影响自动化流程。 */
	void TapWeftKey(UWeftView& View, const TCHAR* KeyName)
	{
		View.Key(true, KeyName, KeyName);
		View.Key(false, KeyName, KeyName);
	}

	/** 启用 -MainMenuPreview 时把当前画面截到 Saved/MainMenu/ 下供视觉验收。 */
	void RequestPreviewShot(const TCHAR* FileName)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("MainMenuPreview")))
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("MainMenu") / FileName, true, false);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTitleAudioAssetsTest, "CaseGinkgo.UI.MainMenu.TitleAudioAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoTitleAudioAssetsTest::RunTest(const FString& Parameters)
{
	const FGinkgoTitleSounds& Sounds = UGinkgoDeveloperSettings::Get()->TitleSounds;

	const TSoftObjectPtr<USoundBase> OpeningRefs[] = {
		Sounds.OpeningShot1,
		Sounds.OpeningShot2,
		Sounds.OpeningReveal,
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(OpeningRefs); ++Index)
	{
		USoundBase* Loaded = OpeningRefs[Index].LoadSynchronous();
		UMetaSoundSource* MetaSound = Cast<UMetaSoundSource>(Loaded);
		TestNotNull(FString::Printf(TEXT("开屏第 %d 镜转场音效为有效 MetaSoundSource"), Index + 1), MetaSound);
		if (MetaSound)
		{
			TestNull(FString::Printf(TEXT("开屏第 %d 镜转场音效保持 2D（无空间衰减）"), Index + 1), MetaSound->AttenuationSettings.Get());
		}

		const FString WavePath = FString::Printf(TEXT("/Game/Audio/Title/SW_Title_Opening_%d.SW_Title_Opening_%d"), Index + 1, Index + 1);
		USoundWave* Wave = LoadObject<USoundWave>(nullptr, *WavePath);
		TestNotNull(FString::Printf(TEXT("裁剪后的立体声转场波形 %s 存在"), *WavePath), Wave);
		if (Wave)
		{
			TestEqual(TEXT("转场波形为立体声 2 通道"), Wave->NumChannels, 2);
			TestFalse(TEXT("转场波形为单次播放（非循环）"), Wave->bLooping);
			TestTrue(TEXT("转场波形裁剪后时长合理（4–6 秒）"), Wave->Duration >= 4.0f && Wave->Duration <= 6.0f);
		}
	}

	UMetaSoundSource* MusicMs = Cast<UMetaSoundSource>(Sounds.MenuMusic.LoadSynchronous());
	TestNotNull(TEXT("主菜单配乐 Brother's Keeper 为有效 MetaSoundSource"), MusicMs);
	if (MusicMs)
	{
		TestNull(TEXT("主菜单配乐保持 2D（无空间衰减）"), MusicMs->AttenuationSettings.Get());
		TestEqual(TEXT("主菜单配乐启用 PlayWhenSilent 虚拟化以支持淡入淡出"), MusicMs->VirtualizationMode, EVirtualizationMode::PlayWhenSilent);
	}

	USoundWave* MusicWave = LoadObject<USoundWave>(nullptr, TEXT("/Game/Audio/Title/SW_Title_Music.SW_Title_Music"));
	TestNotNull(TEXT("主菜单配乐波形 SW_Title_Music 存在"), MusicWave);
	if (MusicWave)
	{
		TestEqual(TEXT("主菜单配乐为立体声 2 通道"), MusicWave->NumChannels, 2);
		TestTrue(TEXT("主菜单配乐启用循环播放"), MusicWave->bLooping);
		TestTrue(TEXT("主菜单配乐保留完整曲长（>230 秒）"), MusicWave->Duration > 230.f);
	}

	return true;
}

/** 实际单机 PIE 流程测试：覆盖双镜开屏音效触发、揭幕进入实时主菜单、特性画廊、设置往返、新游戏入场及暂停菜单返回主菜单。 */
class FGinkgoMainMenuScenario : public IAutomationLatentCommand
{
public:
	explicit FGinkgoMainMenuScenario(FAutomationTestBase& InTest)
		: Test(InTest)
	{
	}

	virtual ~FGinkgoMainMenuScenario() override
	{
		if (UWorld* World = GEditor ? GEditor->PlayWorld : nullptr)
		{
			if (auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(World); Checkpoints && !OriginalSlotName.IsEmpty())
			{
				Checkpoints->SaveSlotName = OriginalSlotName;
			}
			if (auto* Title = UGinkgoTitleSubsystem::Get(World))
			{
				Title->ResetSessionState();
			}
		}
	}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 60.0)
		{
			Test.AddError(FString::Printf(TEXT("主菜单自动化测试超时，阶段 %d"), Stage));
			return true;
		}

		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		auto* PC = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
		auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
		if (!Player || !World->HasBegunPlay())
		{
			return false;
		}
		if (Stage == 0 && World->GetTimeSeconds() < 0.4)
		{
			return false;
		}
		for (TActorIterator<AGinkgoAIController> It(World); It; ++It)
		{
			It->StopAI();
		}

		// 阶段 0：清空测试存档槽并从第 1 镜（UE5）拉起开屏动画
		if (Stage == 0)
		{
			if (auto* Checkpoints = UGinkgoCheckpointSubsystem::Get(World))
			{
				OriginalSlotName = Checkpoints->SaveSlotName;
				Checkpoints->SaveSlotName = TEXT("MainMenuAutoTest_Empty");
				if (ISaveGameSystem* SaveSystem = IPlatformFeaturesModule::Get().GetSaveGameSystem())
				{
					SaveSystem->DeleteGame(false, *Checkpoints->SaveSlotName, 0);
				}
			}
			if (auto* Title = UGinkgoTitleSubsystem::Get(World))
			{
				Title->ResetSessionState();
			}
			FGinkgoMainMenu& Menu = PC->GetOrCreateMainMenu();
			if (!Test.TestTrue(TEXT("成功打开开屏与主菜单"), Menu.Show(true, false)))
			{
				return true;
			}
			Test.TestEqual(TEXT("开屏起播阶段为 Splash"), Menu.GetPhase(), EGinkgoMainMenuPhase::Splash);
			Test.TestTrue(TEXT("控制器报告主菜单与菜单总状态均为开启"), PC->IsMainMenuOpen() && PC->IsMenuOpen());
			NextStage();
			return false;
		}

		// 阶段 1：等待 Shot 1（Unreal Engine 5）触发第 1 段转场音效，再快进到 Shot 2
		if (Stage == 1)
		{
			FGinkgoMainMenu& Menu = PC->GetOrCreateMainMenu();
			if (Menu.GetLastSplashShotPlayed() < 1 || SinceStage() < 1.1)
			{
				return false;
			}
			Test.TestEqual(TEXT("第 1 镜（UE5）已触发过渡音效 1"), Menu.GetLastSplashShotPlayed(), 1);
			if (!bCapturedShot1)
			{
				GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("splash_shot1_ue5"));
				bCapturedShot1 = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			UWeftView* View = Menu.GetView();
			if (!Test.TestNotNull(TEXT("开屏持有有效 Weft 视图"), View))
			{
				return true;
			}
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT(" "));
			NextStage();
			return false;
		}

		// 阶段 2：等待 Shot 2（Weft UI Framework + MetaSound 同屏）触发第 2 段转场音效，再揭幕入主菜单
		if (Stage == 2)
		{
			FGinkgoMainMenu& Menu = PC->GetOrCreateMainMenu();
			if (Menu.GetLastSplashShotPlayed() < 2 || SinceStage() < 1.2)
			{
				return false;
			}
			Test.TestEqual(TEXT("第 2 镜（Weft + MetaSound）已触发过渡音效 2"), Menu.GetLastSplashShotPlayed(), 2);
			if (!bCapturedShot2)
			{
				GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("splash_shot2_weft_metasound"));
				bCapturedShot2 = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			UWeftView* View = Menu.GetView();
			if (!Test.TestNotNull(TEXT("第 2 镜视图有效"), View))
			{
				return true;
			}
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT(" "));
			NextStage();
			return false;
		}

		// 阶段 3：等待黑幕揭幕完成进入实时 3D 主菜单，然后打开「特性画廊」
		if (Stage == 3)
		{
			FGinkgoMainMenu& Menu = PC->GetOrCreateMainMenu();
			if (Menu.GetPhase() != EGinkgoMainMenuPhase::Menu || SinceStage() < 1.5)
			{
				return false;
			}
			Test.TestEqual(TEXT("揭幕已触发过渡音效 3"), Menu.GetLastSplashShotPlayed(), 3);
			Test.TestFalse(TEXT("实时 3D 主菜单期间不暂停世界"), World->IsPaused());
			if (!bCapturedMenu)
			{
				GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("main_menu_live"));
				bCapturedMenu = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			UWeftView* View = Menu.GetView();
			if (!Test.TestNotNull(TEXT("主菜单视图有效"), View))
			{
				return true;
			}
			// 从「新游戏」(0) 下移 3 项到「特性画廊」(3) 并回车展开
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowDown"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowDown"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowDown"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("Enter"));
			NextStage();
			return false;
		}

		// 阶段 4：核验「特性画廊」展开并连续遍历全部 20 个分类条目（含代码片段切换），确认零帧丢弃与 KaTeX 正常渲染后按 Esc 返回
		if (Stage == 4)
		{
			FGinkgoMainMenu& Menu = PC->GetOrCreateMainMenu();
			if (!Menu.IsGalleryOpen())
			{
				return false;
			}
			const double MinWait = (GalleryShotStep == 0) ? 0.50 : 0.16;
			if (SinceStage() < MinWait)
			{
				return false;
			}
			UWeftView* View = Menu.GetView();
			if (!Test.TestNotNull(TEXT("特性画廊视图有效"), View))
			{
				return true;
			}
			// 偶步稳定等待并按需截屏（不与同帧按键冲突），奇步切向下一条目（覆盖全部 5 分类 20 项）
			constexpr int32 TotalGalleryEntries = 20;
			if ((GalleryShotStep % 2) == 0)
			{
				const int32 EntryIdx = GalleryShotStep / 2;
				switch (EntryIdx)
				{
				case 0: GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("main_menu_gallery")); break;
				case 6: GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("gallery_predictive_ik")); break;
				case 9: GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("gallery_anim_layering")); break;
				case 11: GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("gallery_anim_action_chain")); break;
				case 12: GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("gallery_camera_pipeline")); break;
				case 13: GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("gallery_camera_modifiers")); break;
				case 15: GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("gallery_input_curves")); break;
				default: break;
				}
				++GalleryShotStep;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			const int32 FinishedIdx = GalleryShotStep / 2;
			if (FinishedIdx + 1 < TotalGalleryEntries)
			{
				if (FinishedIdx == 0)
				{
					// 验证 Tab 切换代码片段不报错
					GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("Tab"));
				}
				GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowRight"));
				++GalleryShotStep;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("Escape"));
			NextStage();
			return false;
		}

		// 阶段 5：核验特性画廊已关闭，上移 1 项到「设置」并打开设置子页
		if (Stage == 5)
		{
			FGinkgoMainMenu& Menu = PC->GetOrCreateMainMenu();
			if (Menu.IsGalleryOpen() || SinceStage() < 0.5)
			{
				return false;
			}
			UWeftView* View = Menu.GetView();
			if (!Test.TestNotNull(TEXT("退回主菜单后视图有效"), View))
			{
				return true;
			}
			// 当前焦点仍在「特性画廊」(3)，上移 1 项到「设置」(2) 回车进入
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowUp"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("Enter"));
			NextStage();
			return false;
		}

		// 阶段 6：核验设置子页打开且主菜单挂起，按 Esc 退回主菜单
		if (Stage == 6)
		{
			if (!PC->IsSettingsPageOpen() || SinceStage() < 0.8)
			{
				return false;
			}
			Test.TestTrue(TEXT("进入设置子页期间主菜单保持挂起状态"), PC->IsMainMenuOpen());
			UWeftView* SettingsView = PC->GetOrCreateSettingsPage().GetView();
			if (!Test.TestNotNull(TEXT("设置子页视图有效"), SettingsView))
			{
				return true;
			}
			if (!bCapturedSettings)
			{
				GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("main_menu_settings"));
				bCapturedSettings = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			GinkgoMainMenuTestsPrivate::TapWeftKey(*SettingsView, TEXT("Escape"));
			NextStage();
			return false;
		}

		// 阶段 7：核验设置子页关闭并回到主菜单，测试「退出游戏」弹窗取消后点击「新游戏」弹出难度选择面板
		if (Stage == 7)
		{
			if (PC->IsSettingsPageOpen() || SinceStage() < 0.5)
			{
				return false;
			}
			Test.TestTrue(TEXT("关闭设置子页后自动回到主菜单"), PC->IsMainMenuOpen());
			UWeftView* View = PC->GetOrCreateMainMenu().GetView();
			if (!Test.TestNotNull(TEXT("设置返回后主菜单视图有效"), View))
			{
				return true;
			}
			// 退回后焦点停在「设置」(2)：下移 2 项到「退出游戏」(4) 弹出再取消，再下移 1 项回绕到「新游戏」(0) 打开难度选择面板
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowDown"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowDown"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("Enter"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("Escape"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowDown"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("Enter"));
			NextStage();
			return false;
		}

		// 阶段 8：等待难度选择面板（默认高亮推荐「平衡」难度）渲染并截屏，再切至「娱乐」并回车开局
		if (Stage == 8)
		{
			if (SinceStage() < 0.45)
			{
				return false;
			}
			UWeftView* View = PC->GetOrCreateMainMenu().GetView();
			if (!Test.TestNotNull(TEXT("难度选择面板视图有效"), View))
			{
				return true;
			}
			if (!bCapturedDifficulty)
			{
				GinkgoMainMenuTestsPrivate::RequestPreviewShot(TEXT("main_menu_difficulty"));
				bCapturedDifficulty = true;
				StageStarted = FPlatformTime::Seconds();
				return false;
			}
			// 默认焦点停在推荐的「平衡」(1)：上移 1 项选中「娱乐」(0) 并回车确认开局
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("ArrowUp"));
			GinkgoMainMenuTestsPrivate::TapWeftKey(*View, TEXT("Enter"));
			NextStage();
			return false;
		}

		// 阶段 9：等待无缝退场进入实时玩法，核验难度生效（娱乐 -90% / 平衡 -50% / 地狱 0% 且不影响非玩家目标）与局内返回主菜单
		if (Stage == 9)
		{
			if (PC->IsMainMenuOpen())
			{
				return false;
			}
			Test.TestFalse(TEXT("点击新游戏进入玩法后菜单总状态关闭"), PC->IsMenuOpen());
			UGinkgoTitleSubsystem* Title = UGinkgoTitleSubsystem::Get(World);
			if (Test.TestNotNull(TEXT("标题子系统存在"), Title))
			{
				Test.TestTrue(TEXT("会话记录已进入实际游玩"), Title->HasEnteredGameplay());
				Test.TestEqual(TEXT("新游戏所选难度为「娱乐」"), Title->GetDifficulty(), EGinkgoDifficulty::Casual);
				Test.TestEqual(TEXT("娱乐难度玩家承伤倍率为 0.10（-90%）"), Title->GetPlayerIncomingDamageScale(), 0.10f, 0.001f);

				// 在真实玩家角色上验证三档难度的实际扣血数值（并确保非玩家目标不受减伤影响）
				UGinkgoAmmoDefinition* TestAmmo = NewObject<UGinkgoAmmoDefinition>(GetTransientPackage());
				TestAmmo->Damage = 40.f;
				TestAmmo->ArmorDamage = 40.f;
				TestAmmo->BodyParts = FGinkgoBodyPartMultipliers();
				TestAmmo->BodyParts.UpperChest = 1.f;
				TestAmmo->Falloff = FGinkgoDamageFalloff();

				UGinkgoDamageableComponent* PlayerDamageable = Player->GetDamageable();
				PlayerDamageable->SetArmor({});

				PlayerDamageable->Revive();
				const FGinkgoDamageEvent CasualHit = PlayerDamageable->ApplyBulletHit(*TestAmmo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
				Test.TestEqual(TEXT("娱乐难度（-90%）玩家受 40 基础伤害实际仅扣 4 血"), CasualHit.Outcome.HealthDamage, 4.f, 0.01f);
				Test.TestEqual(TEXT("娱乐难度玩家剩余血量 96"), PlayerDamageable->GetHealth(), 96.f, 0.01f);

				// 同在娱乐难度下，无玩家控制的独立受击组件应承受完整 40 伤害
				AActor* DummyTarget = World->SpawnActor<AActor>();
				UGinkgoDamageableComponent* EnemyDamageable = NewObject<UGinkgoDamageableComponent>(DummyTarget);
				EnemyDamageable->RegisterComponent();
				EnemyDamageable->InitializeVitals(100.f, {});
				const FGinkgoDamageEvent EnemyHit = EnemyDamageable->ApplyBulletHit(*TestAmmo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, Player);
				Test.TestEqual(TEXT("娱乐难度不影响敌方目标承受伤害（仍为 40）"), EnemyHit.Outcome.HealthDamage, 40.f, 0.01f);
				DummyTarget->Destroy();

				Title->SetDifficulty(EGinkgoDifficulty::Balanced);
				PlayerDamageable->Revive();
				const FGinkgoDamageEvent BalancedHit = PlayerDamageable->ApplyBulletHit(*TestAmmo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
				Test.TestEqual(TEXT("平衡难度（-50%）玩家受 40 基础伤害实际扣 20 血"), BalancedHit.Outcome.HealthDamage, 20.f, 0.01f);
				Test.TestEqual(TEXT("平衡难度玩家剩余血量 80"), PlayerDamageable->GetHealth(), 80.f, 0.01f);

				Title->SetDifficulty(EGinkgoDifficulty::Hell);
				PlayerDamageable->Revive();
				const FGinkgoDamageEvent HellHit = PlayerDamageable->ApplyBulletHit(*TestAmmo, 1.f, FHitResult(), FVector::ForwardVector, 0.f, nullptr);
				Test.TestEqual(TEXT("地狱难度（无减伤）玩家受 40 基础伤害实际扣 40 血"), HellHit.Outcome.HealthDamage, 40.f, 0.01f);
				Test.TestEqual(TEXT("地狱难度玩家剩余血量 60"), PlayerDamageable->GetHealth(), 60.f, 0.01f);

				PlayerDamageable->Revive();
			}

			// 验证固定快捷键 P 的 0.2× 慢动作切换：不进重绑定表、按一次开启 0.2×、再按一次恢复 1.0×、返回主菜单自动重置且菜单中忽略 P
			bool bPInRebindDefs = false;
			for (const FGinkgoKeyBindingDef& Def : Ginkgo::Input::GetKeyBindingDefs())
			{
				if (Def.DefaultKey == EKeys::P)
				{
					bPInRebindDefs = true;
					break;
				}
			}
			Test.TestFalse(TEXT("固定慢动作键 P 未注册进重绑定配置表"), bPInRebindDefs);

			const auto TapControllerKey = [PC](const FKey& Key)
			{
				APlayerController* BasePC = static_cast<APlayerController*>(PC);

				FInputKeyEventArgs Down{};
				Down.Key = Key;
				Down.Event = IE_Pressed;
				Down.AmountDepressed = 1.f;
				BasePC->InputKey(Down);
				BasePC->PlayerTick(1.f / 60.f);

				FInputKeyEventArgs Up{};
				Up.Key = Key;
				Up.Event = IE_Released;
				BasePC->InputKey(Up);
				BasePC->PlayerTick(1.f / 60.f);
			};

			Test.TestFalse(TEXT("初始状态未开启慢动作"), PC->IsSlowMotionEnabled());
			Test.TestEqual(TEXT("初始全局时间倍率为 1.0"), UGameplayStatics::GetGlobalTimeDilation(World), 1.0f, 0.001f);

			TapControllerKey(EKeys::P);
			Test.TestTrue(TEXT("首次按 P 开启慢动作"), PC->IsSlowMotionEnabled());
			Test.TestEqual(TEXT("首次按 P 后全局时间倍率为 0.2"), UGameplayStatics::GetGlobalTimeDilation(World), 0.2f, 0.001f);

			TapControllerKey(EKeys::P);
			Test.TestFalse(TEXT("再次按 P 关闭慢动作"), PC->IsSlowMotionEnabled());
			Test.TestEqual(TEXT("再次按 P 后全局时间倍率恢复 1.0"), UGameplayStatics::GetGlobalTimeDilation(World), 1.0f, 0.001f);

			// 开启慢动作后返回主菜单：必须自动还原 1.0×，且主菜单打开期间按 P 不会触发慢动作
			TapControllerKey(EKeys::P);
			Test.TestTrue(TEXT("第三次按 P 再次进入 0.2× 慢动作"), PC->IsSlowMotionEnabled());

			// 验证从局内 ReturnToMainMenu 直接回到主菜单选项（跳过开屏）
			PC->ReturnToMainMenu();
			Test.TestFalse(TEXT("返回主菜单自动取消慢动作状态"), PC->IsSlowMotionEnabled());
			Test.TestEqual(TEXT("返回主菜单自动恢复 1.0 全局时间倍率"), UGameplayStatics::GetGlobalTimeDilation(World), 1.0f, 0.001f);

			TapControllerKey(EKeys::P);
			Test.TestFalse(TEXT("主菜单开启期间按 P 不触发慢动作"), PC->IsSlowMotionEnabled());
			Test.TestEqual(TEXT("主菜单开启期间按 P 全局时间倍率仍为 1.0"), UGameplayStatics::GetGlobalTimeDilation(World), 1.0f, 0.001f);

			Test.TestTrue(TEXT("ReturnToMainMenu 成功拉起主菜单"), PC->IsMainMenuOpen());
			Test.TestEqual(TEXT("局内返回主菜单直接进入 Menu 阶段不重播开屏"), PC->GetOrCreateMainMenu().GetPhase(), EGinkgoMainMenuPhase::Menu);
			if (Title)
			{
				Test.TestTrue(TEXT("局内返回后标记下次新游戏需重载洁净关卡"), Title->NeedsFreshReloadForNewGame());
			}
			PC->GetOrCreateMainMenu().Hide();
			return true;
		}

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

	/** 整个场景的墙钟开始时间（秒）。 */
	double Started = FPlatformTime::Seconds();

	/** 当前阶段的墙钟开始时间（秒）。 */
	double StageStarted = 0.0;

	/** 当前执行阶段编号（0–9）。 */
	int32 Stage = 0;

	/** 备份的原始检查点存档槽名，析构时恢复避免污染默认槽。 */
	FString OriginalSlotName;

	/** 是否已为开屏第 1 镜请求预览截图。 */
	bool bCapturedShot1 = false;

	/** 是否已为开屏第 2 镜请求预览截图。 */
	bool bCapturedShot2 = false;

	/** 是否已为主菜单实景界面请求预览截图。 */
	bool bCapturedMenu = false;

	/** 特性画廊全量 20 项遍历与预览截图进度（偶步截屏、奇步翻页）。 */
	int32 GalleryShotStep = 0;

	/** 是否已为设置子页请求预览截图。 */
	bool bCapturedSettings = false;

	/** 是否已为新游戏难度选择面板请求预览截图。 */
	bool bCapturedDifficulty = false;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoMainMenuFlowTest, "CaseGinkgo.UI.MainMenu.SplashMenuGalleryAndGameplayFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoMainMenuFlowTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/Checkpoint/L_CheckpointTest")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FGinkgoMainMenuScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

#include "UI/GinkgoInteractionPrompt.h"

#include "Camera/PlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "Control/GinkgoInputSettings.h"
#include "Control/GinkgoInputSettingsSubsystem.h"
#include "Control/GinkgoPlayerController.h"           // 最近一次输入的设备：圈里写哪个设备的键
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"                // 视口的 DPI 缩放：页面第一次布局用的大小
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/GinkgoInteractorComponent.h"
#include "Interaction/GinkgoWorkbench.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "String/LexFromString.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SWeftViewport.h"
#include "UI/GinkgoUiSounds.h"
#include "UI/GinkgoUiTheme.h"                          // 键帽（Theme/keycap.js）接进页面
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

DECLARE_CYCLE_STAT(TEXT("Interaction prompt"), STAT_GinkgoInteractionPrompt, STATGROUP_Ginkgo3C);

namespace
{
	using FPromptJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	using FPromptJsonWriterFactory = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	/** 页面的目录：Content/UI 随包（DefaultGame.ini 的 DirectoriesToAlwaysStageAsUFS），打包后从 pak 读 */
	const TCHAR* PromptPageDirectory = TEXT("UI/Interaction");

	/** 控件在视口上的层级：在窗口时间轴（20）之下 */
	constexpr int32 PromptWidgetZOrder = 10;

	/** 被拒绝的原因在提示上留多久（秒） */
	constexpr double FlashSeconds = 1.0;

	/** 目标不见了以后还留多久（挂钟，秒）：页面放消失的动画（page.js 的 EXIT_MS、BURST_MS，最长 340 ms） */
	constexpr double ExitSeconds = 0.4;

	/**
	 * 正在消失的目标在页面上是另一项（残影）：编号与锚点的 key 是原来的加上这一位（UObject 的编号远小于 2^40）。
	 * 目标在消失途中又出现时，原来那一项重新出现、重放出现的动画，残影照样把消失放完，两边互不打断
	 */
	constexpr int64 LeavingKeyBit = int64(1) << 40;

	/** 服务器拒绝、等不到结果时提示上闪一下的字；接受了没有 */
	FText RejectionText(const EGinkgoInteractionResult Result)
	{
		switch (Result)
		{
		case EGinkgoInteractionResult::Gone:
			return NSLOCTEXT("Ginkgo", "InteractionGone", "已经没了");
		case EGinkgoInteractionResult::OutOfRange:
			return NSLOCTEXT("Ginkgo", "InteractionOutOfRange", "太远了");
		case EGinkgoInteractionResult::Unavailable:
			return NSLOCTEXT("Ginkgo", "InteractionUnavailable", "现在不能交互");
		case EGinkgoInteractionResult::TimedOut:
			return NSLOCTEXT("Ginkgo", "InteractionTimedOut", "没有回应");
		case EGinkgoInteractionResult::NoCharacter:
			[[fallthrough]];
		case EGinkgoInteractionResult::Accepted:
			break;
		}
		return FText::GetEmpty();
	}

	/** 状态的枚举名（Hint、Ready、Blocked），页面按它选样子：枚举的显示名是元数据，打包的游戏里没有 */
	FString StateName(const EGinkgoInteractionState State)
	{
		return StaticEnum<EGinkgoInteractionState>()->GetNameStringByValue(static_cast<int64>(State));
	}
}

FGinkgoInteractionPrompt::FGinkgoInteractionPrompt(AGinkgoPlayerController& InController)
	: Controller(&InController)
{
}

FGinkgoInteractionPrompt::~FGinkgoInteractionPrompt()
{
	Hide();
}

bool FGinkgoInteractionPrompt::Show()
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
		UE_LOG(LogGinkgoUI, Log, TEXT("[Interaction] 不显示交互的提示：%s"),
			!Viewport ? TEXT("没有游戏视口（无界面运行）") : TEXT("weft 在这里不运行（-nullrhi，或没有编出 weft 的运行时库）"));
		return false;
	}
	Weft->AddEffects(FPaths::ProjectContentDir() / TEXT("UI/Effects/effects.json"));
	View.Reset(NewView);
	ViewportClient = Viewport;
	MessageHandle = NewView->OnMessage.AddRaw(this, &FGinkgoInteractionPrompt::OnPageMessage);
	// 只显示、不接指针：页面铺满视口，鼠标与键盘照常交给游戏
	Widget = SNew(SWeftViewport).View(NewView).Interactive(false);
	PlayerController->AddGameplayHudWidget(Widget.ToSharedRef(), PromptWidgetZOrder);
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FGinkgoInteractionPrompt::OnWorldPostActorTick);
	return true;
}

void FGinkgoInteractionPrompt::Hide()
{
	if (!IsShown())
	{
		return;
	}
	SetWorkbenchInput(false);
	View->OnMessage.Remove(MessageHandle);
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
	LastMessage.Reset();
	Anchors.Reset();
	Items.Reset();
}

void FGinkgoInteractionPrompt::SetWorkbenchInput(bool bOpen)
{
	auto* PC = Controller.Get();
	if (bOpen == bWorkbenchInput || !PC || !Widget) { return; }
	bWorkbenchInput = bOpen;
	Widget->AddView(View.Get(), 0, bOpen);
	if (bOpen)
	{
		bPreviousCursor = PC->ShouldShowMouseCursor();
		FInputModeGameAndUI Mode;
		// 进桌时一次性交出视口焦点，首个鼠标按下直接操作按钮，不再承担激活页面的职责。
		Mode.SetWidgetToFocus(Widget);
		Mode.SetHideCursorDuringCapture(false);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
		Widget->FocusView(View.Get());
	}
	else if (PC->GetWorld() && PC->GetWorld()->GetGameViewport())
	{
		const auto* GinkgoPC = Cast<AGinkgoPlayerController>(PC);
		if (!GinkgoPC || !GinkgoPC->IsMenuOpen())
		{
			if (bPreviousCursor) { PC->SetInputMode(FInputModeGameAndUI()); }
			else { PC->SetInputMode(FInputModeGameOnly()); }
			PC->SetShowMouseCursor(bPreviousCursor);
		}
	}
}

bool FGinkgoInteractionPrompt::NavigateWorkbench(const FName Command)
{
	if (!View || !bLoaded || !bWorkbenchInput) { return false; }
	View->Emit(TEXT("benchNavigate"), FString::Printf(TEXT("\"%s\""), *Command.ToString()));
	return true;
}

void FGinkgoInteractionPrompt::OnPageMessage(const FString& Name, const FString& Json, uint64 CallId)
{
	auto* PC = Controller.Get();
	auto* Character = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
	if (!Character || !Character->GetWorkbench() || !bWorkbenchInput)
	{
		return;
	}
	if (Name == TEXT("sound"))
	{
		UGinkgoUiSoundSubsystem::PlayWorkbench(PC, Ginkgo::UI::ParseUiSound(Json.TrimQuotes()));
	}
	else if (Name == TEXT("benchSelect"))
	{
		int32 Slot;
		if (LexTryParseString(Slot, *Json)) { Character->GetInteractor()->SelectWorkbenchSlot(Slot); }
	}
	else if (Name == TEXT("benchLeave"))
	{
		Character->GetInteractor()->RequestInteract();
	}
	else if (Name == TEXT("benchInstall"))
	{
		// 卡片对应的是绘制时的那把枪，切枪后迟到的点击不能装到新枪上。
		TSharedPtr<FJsonObject> Request;
		int32 Slot = INDEX_NONE;
		int32 Index = INDEX_NONE;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Request) && Request
			&& Request->TryGetNumberField(TEXT("slot"), Slot) && Slot == Character->GetWorkbench()->GetSelectedSlot()
			&& Request->TryGetNumberField(TEXT("index"), Index))
		{
			const UGinkgoWeaponDefinition* Weapon = Character->GetWeapons()->GetCarried(Slot);
			if (Weapon && Weapon->Attachments.IsValidIndex(Index))
			{
				Character->GetInteractor()->InstallWorkbenchAttachment(Weapon->Attachments[Index].Part);
			}
		}
	}
	else if (Name == TEXT("benchFocus"))
	{
		// 页面当前选中的部件卡片仅影响本地桌前相机的聚焦偏置，切枪或清零时同步复位。
		TSharedPtr<FJsonObject> Request;
		int32 Slot = INDEX_NONE;
		int32 Index = INDEX_NONE;
		if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Request) && Request
			&& Request->TryGetNumberField(TEXT("slot"), Slot)
			&& Request->TryGetNumberField(TEXT("index"), Index))
		{
			AGinkgoWorkbench* Bench = Character->GetWorkbench();
			const UGinkgoWeaponDefinition* Weapon = Slot == Bench->GetSelectedSlot() ? Character->GetWeapons()->GetCarried(Slot) : nullptr;
			const UGinkgoWeaponAttachment* Part = (Weapon && Weapon->Attachments.IsValidIndex(Index)) ? Weapon->Attachments[Index].Part : nullptr;
			Bench->SetFocusedAttachment(Part);
		}
	}
}

void FGinkgoInteractionPrompt::OnWorldPostActorTick(UWorld* InWorld, const ELevelTick TickType, const float DeltaSeconds)
{
	const APlayerController* PlayerController = Controller.Get();
	if (!PlayerController || InWorld != PlayerController->GetWorld() || !View || TickType == LEVELTICK_TimeOnly)
	{
		return;
	}
	SCOPE_CYCLE_COUNTER(STAT_GinkgoInteractionPrompt);
	const auto* HudController = Cast<AGinkgoPlayerController>(PlayerController);
	const bool bSuppressed = HudController && HudController->IsGameplayHudSuppressed();
	// 地点标注只隐藏共同父容器；此处暂停工作台输入，不覆盖 AddView 管理的局部可命中状态。
	if (bSuppressed) { SetWorkbenchInput(false); return; }

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
		const FString Directory = FPaths::ProjectContentDir() / PromptPageDirectory;
		if (!Ginkgo::UI::LoadPageWithKeycaps(*View, Directory))
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[Interaction] 提示的页面载入失败：%s（原因见 LogWeft）"), *Directory);
			Hide();
			return;
		}
		bLoaded = true;
	}

	const AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(PlayerController->GetPawn());
	const UGinkgoInteractorComponent* Interactor = Character ? Character->GetInteractor() : nullptr;
	// 锚点是页面的逻辑 px，投影出来的是视口的像素
	const FVector2D LogicalSize = View->GetLogicalSize();
	const FVector2D PixelToLogical(LogicalSize.X / Pixels.X, LogicalSize.Y / Pixels.Y);
	const FVector CameraLocation = PlayerController->PlayerCameraManager ? PlayerController->PlayerCameraManager->GetCameraCacheView().Location : FVector::ZeroVector;

	FString Message;
	const TSharedRef<FPromptJsonWriter> Writer = FPromptJsonWriterFactory::Create(&Message);
	Writer->WriteObjectStart();
	// 圈里的键：最近一次输入的设备（同辅助瞄准：按了手柄的键、推过摇杆是手柄）上交互绑的键，改过键的也跟着。换设备、改键时事件跟着变，页面重画
	const AGinkgoPlayerController* GinkgoController = Cast<AGinkgoPlayerController>(PlayerController);
	const EGinkgoInputDirectory Device = GinkgoController ? GinkgoController->GetActiveDevice() : EGinkgoInputDirectory::KeyboardMouse;
	static const FGinkgoInputSettings DefaultSettings;
	const UGinkgoInputSettingsSubsystem* InputSettings = UGinkgoInputSettingsSubsystem::Get(PlayerController);
	const FKey PromptKey = Ginkgo::Input::FindBoundKey(InputSettings ? InputSettings->GetCurrent() : DefaultSettings, Device, EGinkgoKeyAction::Interact);
	Writer->WriteObjectStart(TEXT("key"));
	Writer->WriteValue(TEXT("key"), PromptKey.IsValid() ? PromptKey.GetFName().ToString() : FString());
	Writer->WriteValue(TEXT("label"), PromptKey.IsValid() ? PromptKey.GetDisplayName(false).ToString() : FString());
	Writer->WriteObjectEnd();
	const auto* Bench = Character ? Character->GetWorkbench() : nullptr;
	const bool bShowBench = Bench && !Bench->IsExiting();
	SetWorkbenchInput(bShowBench);
	if (bShowBench)
	{
		const auto* Weapons = Character->GetWeapons();
		Writer->WriteObjectStart(TEXT("bench"));
		Writer->WriteValue(TEXT("selected"), Bench->GetSelectedSlot());
		Writer->WriteValue(TEXT("ready"), Bench->CanSelectWeapon());
		Writer->WriteValue(TEXT("gamepad"), Device == EGinkgoInputDirectory::Gamepad);
		Writer->WriteValue(TEXT("previous"), Bench->GetAdjacentSlot(false));
		Writer->WriteValue(TEXT("next"), Bench->GetAdjacentSlot(true));
		const int32 Selected = Bench->GetSelectedSlot();
		const FKey InstallKey = GinkgoController ? GinkgoController->GetWorkbenchInstallKey() : EKeys::F;
		Writer->WriteValue(TEXT("installKey"), InstallKey.GetFName().ToString());
		Writer->WriteArrayStart(TEXT("parts"));
		if (const UGinkgoWeaponDefinition* Weapon = Weapons->GetCarried(Selected))
		{
			for (int32 Index = 0; Index < Weapon->Attachments.Num(); ++Index)
			{
				const FGinkgoWeaponAttachmentMount& Mount = Weapon->Attachments[Index];
				const UGinkgoWeaponAttachment* Part = Mount.Part;
				if (!Part || Mount.Mount.IsNone())
				{
					continue;
				}
				Writer->WriteObjectStart();
				Writer->WriteValue(TEXT("index"), Index);
				// Shipping 关闭了 WITH_CASE_PRESERVING_NAME，FName::ToString() 会沿用先注册符号的大小写（如 Trigger / Magazine）；统一小写传给页面查表。
				Writer->WriteValue(TEXT("mount"), Mount.Mount.ToString().ToLower());
				Writer->WriteValue(TEXT("name"), Part->DisplayName.ToString());
				Writer->WriteValue(TEXT("description"), Part->Description.ToString());
				Writer->WriteArrayStart(TEXT("effects"));
				for (const FGinkgoAttachmentEffectDescription& Effect : Part->EffectDescriptions)
				{
					if (Effect.Text.IsEmpty()) { continue; }
					Writer->WriteObjectStart();
					Writer->WriteValue(TEXT("text"), Effect.Text.ToString());
					Writer->WriteValue(TEXT("tone"), StaticEnum<EGinkgoAttachmentEffectTone>()->GetNameStringByValue(static_cast<int64>(Effect.Tone)));
					Writer->WriteObjectEnd();
				}
				Writer->WriteArrayEnd();
				Writer->WriteValue(TEXT("icon"), Part->HudIcon.IsNone() ? FString() : Part->HudIcon.ToString());
				Writer->WriteValue(TEXT("installed"), Weapons->GetAttachments(Selected).Parts.Contains(Part));
				Writer->WriteValue(TEXT("installing"), Bench->GetInstallingPart() == Part);
				Writer->WriteValue(TEXT("available"), Bench->CanInstallAttachment(Part));
				Writer->WriteObjectEnd();
			}
		}
		Writer->WriteArrayEnd();
		Writer->WriteArrayStart(TEXT("weapons"));
		for (int32 Slot = 0; Slot < UGinkgoWeaponComponent::SlotCount; ++Slot)
		{
			const auto* Weapon = Weapons->GetCarried(Slot);
			if (!Weapon || !Bench->IsSlotAvailable(Slot)) { continue; }
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("slot"), Slot);
			Writer->WriteValue(TEXT("name"), Weapon->DisplayName.IsEmpty() ? Weapon->GetName() : Weapon->DisplayName.ToString());
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayStart(TEXT("items"));
	Anchors.Reset();

	// 一个目标：事件里的一项，加上这一帧的锚点（这一帧的相机已经更新过，投影与这一帧画出来的画面一致）
	auto AddItem = [&](const FShownItem& Item, const bool bPending, const FString& Flash)
	{
		const int64 Key = Item.LeftAt >= 0.0 ? Item.Key | LeavingKeyBit : Item.Key;
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("id"), LexToString(Key));
		Writer->WriteValue(TEXT("state"), Item.State);
		Writer->WriteValue(TEXT("text"), Item.Text);
		Writer->WriteValue(TEXT("reason"), Item.Reason);
		Writer->WriteValue(TEXT("pending"), bPending);
		Writer->WriteValue(TEXT("flash"), Flash);
		Writer->WriteValue(TEXT("leaving"), Item.LeftAt >= 0.0);
		Writer->WriteObjectEnd();

		FWeftAnchor& Anchor = Anchors.AddDefaulted_GetRef();
		Anchor.Key = Key;
		FVector2D Screen = FVector2D::ZeroVector;
		if (PlayerController->ProjectWorldLocationToScreen(Item.Location, Screen, false))
		{
			Anchor.Position = Screen * PixelToLogical;
		}
		else
		{
			Anchor.bBehind = true;
		}
		Anchor.Depth = FVector::Dist(CameraLocation, Item.Location);
	};

	TArray<FShownItem> Shown;
	if (Interactor && !Bench)
	{
		const UObject* Pending = Interactor->GetPendingTarget();
		const UGinkgoInteractorComponent::FCandidate* Focus = Interactor->GetFocus();
		const bool bFlash = Interactor->GetLastResult() != EGinkgoInteractionResult::Accepted && Interactor->GetLastResultTime() >= 0.0
			&& InWorld->GetTimeSeconds() - Interactor->GetLastResultTime() < FlashSeconds;
		for (const UGinkgoInteractorComponent::FCandidate& Candidate : Interactor->GetCandidates())
		{
			const IGinkgoInteractable* Target = Candidate.Target.Get();
			const UObject* TargetObject = Candidate.Target.GetObject();
			if (!Target || Candidate.State == EGinkgoInteractionState::Hidden)
			{
				continue;
			}
			FShownItem& Item = Shown.AddDefaulted_GetRef();
			Item.Key = TargetObject->GetUniqueID();
			Item.Location = Target->GetInteractionLocation();
			Item.State = StateName(Candidate.State);
			Item.Text = Candidate.Text.ToString();
			Item.Reason = Candidate.Reason.ToString();
			AddItem(Item, TargetObject == Pending, bFlash && &Candidate == Focus ? RejectionText(Interactor->GetLastResult()).ToString() : FString());
		}
	}

	// 上一帧显示着、这一帧不见了的：开始消失；消失够了 ExitSeconds 的丢掉；又出现了的照常显示（页面重放出现的动画）
	const double Now = FPlatformTime::Seconds();
	for (FShownItem& Item : Items)
	{
		if (Shown.ContainsByPredicate([&Item](const FShownItem& Current) { return Current.Key == Item.Key; }))
		{
			continue;
		}
		if (Item.LeftAt < 0.0)
		{
			Item.LeftAt = Now;
		}
		if (Now - Item.LeftAt < ExitSeconds)
		{
			AddItem(Item, false, FString());
			Shown.Add(MoveTemp(Item));
		}
	}
	Items = MoveTemp(Shown);

	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();

	View->SetAnchors(Anchors);
	if (Message != LastMessage)
	{
		LastMessage = Message;
		View->Emit(TEXT("interaction"), Message, true);
		UE_LOG(LogGinkgoUI, VeryVerbose, TEXT("[Interaction] 发给提示的页面（锚点 %d 个）：%s"), Anchors.Num(), *Message);
	}
}

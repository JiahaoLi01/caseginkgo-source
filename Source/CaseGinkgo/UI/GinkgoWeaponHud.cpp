#include "UI/GinkgoWeaponHud.h"

#include "Animation/GinkgoAnimInstance.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "CollisionQueryParams.h"
#include "Combat/GinkgoAmmoDefinition.h"                 // 弹种的名字、画哪种子弹
#include "Framework/GinkgoDeveloperSettings.h"           // 弹种轮盘的死区
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoInputSettings.h"                 // 轮盘的键位提示：装填、开镜是哪个键
#include "Control/GinkgoInputSettingsSubsystem.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"                // 视口的 DPI 缩放：页面第一次布局用的大小
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"
#include "SWeftViewport.h"
#include "UI/GinkgoUiTheme.h"                            // 键帽
#include "UI/GinkgoWindowTimeline.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "WeftSubsystem.h"
#include "WeftView.h"

DECLARE_CYCLE_STAT(TEXT("Weapon HUD"), STAT_GinkgoWeaponHud, STATGROUP_Ginkgo3C);

namespace
{
	using FHudJsonWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
	using FHudJsonWriterFactory = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

	/** 页面的目录：Content/UI 随包（DefaultGame.ini 的 DirectoriesToAlwaysStageAsUFS），打包后从 pak 读 */
	const TCHAR* HudPageDirectory = TEXT("UI/WeaponHud");

	/** 控件在视口上的层级：在交互的提示（10）与窗口时间轴（20）之下 */
	constexpr int32 HudWidgetZOrder = 5;

	/** 瞄准的锚点（页面的 data-anchor）：不开镜的点、开镜准星（中间的竖柱）、红 X、准星两边的横线、开镜准星的两根横柱 */
	constexpr int64 DotAnchorKey = 1;
	constexpr int64 ReticleAnchorKey = 2;
	constexpr int64 HitAnchorKey = 3;
	constexpr int64 DashLeftAnchorKey = 4;
	constexpr int64 DashRightAnchorKey = 5;
	constexpr int64 PostLeftAnchorKey = 6;
	constexpr int64 PostRightAnchorKey = 7;
	/** 命中的白 X：准星的中心 */
	constexpr int64 ConfirmAnchorKey = 8;
	/** 弹种轮盘（Docs/Design/Weapons.md「切换弹种」）：屏幕中心 */
	constexpr int64 WheelAnchorKey = 9;
	constexpr int64 SpreadRingAnchorKey = 10;
	const FName SpreadRingReticle(TEXT("SpreadRing"));
	/** 页面圆环的描边中线半径（逻辑 px）。 */
	constexpr double SpreadRingRadius = 64.0;

	/** 缩放比它小的锚点藏起来：两头缓过的进度离 0 还有一点时元素已经缩成看不见的一点 */
	constexpr float MinVisibleScale = 1e-3f;

	/**
	 * 世界里的点在这个视角的画面上离中心多少（1080p 高的像素）。视场是 4:3 的水平视场，引擎按竖直视场换算，所以与窗口的大小、宽高比无关
	 * @return 在相机前面
	 */
	bool ToViewPlane(const FMinimalViewInfo& View, const FVector& Point, FVector2D& OutPlane)
	{
		const FVector Local = View.Rotation.UnrotateVector(Point - View.Location);
		if (Local.X <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		OutPlane = FVector2D(Local.Y, Local.Z) / Local.X * (540.0 * (4.0 / 3.0) / FMath::Tan(FMath::DegreesToRadians(View.FOV * 0.5)));
		return true;
	}

	/** 窗口时间轴开着时武器栏挪到它上面，再空出这么多（逻辑 px） */
	constexpr float TimelineGap = 12.f;

	/** 枚举名转成页面认的小写（Live → live）：枚举的显示名是元数据，打包的游戏里没有 */
	template <typename TEnum>
	FString PageName(const TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value)).ToLower();
	}

	/**
	 * 一把枪的几种弹（同一口径，枪的弹种第一）写成页面的数组：短名字、武器栏写的口径、画哪种子弹、是不是特殊弹（不是枪的弹种）。
	 * 没有子弹数据的弹种照枪的口径与图
	 */
	void WriteAmmoChoices(FHudJsonWriter& Writer, const TCHAR* Name, const UGinkgoWeaponDefinition& Weapon, TConstArrayView<FGameplayTag> Choices)
	{
		Writer.WriteArrayStart(Name);
		for (const FGameplayTag& Choice : Choices)
		{
			const UGinkgoAmmoDefinition* Ammo = UGinkgoAmmoDefinition::Find(Choice);
			Writer.WriteObjectStart();
			Writer.WriteValue(TEXT("short"), Ammo ? Ammo->GetShortName().ToString() : Choice.ToString());
			Writer.WriteValue(TEXT("label"), Ammo && !Ammo->HudLabel.IsEmpty() ? Ammo->HudLabel.ToString() : Weapon.CaliberName.ToString());
			Writer.WriteValue(TEXT("round"), Ammo && !Ammo->HudRound.IsNone() ? Ammo->HudRound.ToString() : Weapon.HudIcon.ToString());
			Writer.WriteValue(TEXT("special"), Choice != Weapon.AmmoType);
			Writer.WriteObjectEnd();
		}
		Writer.WriteArrayEnd();
	}
}

FGinkgoWeaponHud::FGinkgoWeaponHud(AGinkgoPlayerController& InController)
	: Controller(&InController)
{
}

FGinkgoWeaponHud::~FGinkgoWeaponHud()
{
	Hide();
}

bool FGinkgoWeaponHud::Show()
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
		UE_LOG(LogGinkgoUI, Log, TEXT("[WeaponHud] 不显示武器的界面：%s"),
			!Viewport ? TEXT("没有游戏视口（无界面运行）") : TEXT("weft 在这里不运行（-nullrhi，或没有编出 weft 的运行时库）"));
		return false;
	}
	View.Reset(NewView);
	ViewportClient = Viewport;
	// 弹种轮盘的毛玻璃（backdrop-filter）读游戏画面：画面到 Slate 的输出时 alpha 是 0，要先写成不透明
	NewView->SetOpaqueBackdrop(true);
	// 只显示、不接指针：页面铺满视口，鼠标与键盘照常交给游戏
	Widget = SNew(SWeftViewport).View(NewView).Interactive(false);
	PlayerController->AddGameplayHudWidget(Widget.ToSharedRef(), HudWidgetZOrder);
	PostActorTickHandle = FWorldDelegates::OnWorldPostActorTick.AddRaw(this, &FGinkgoWeaponHud::OnWorldPostActorTick);
	return true;
}

void FGinkgoWeaponHud::Hide()
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
	LastMessage.Reset();
	LastConfirmSerial = INDEX_NONE;
	LastWheelSerial = INDEX_NONE;
	Anchors.Reset();
}

bool FGinkgoWeaponHud::ProjectDot(const APlayerController& Controller, const FGinkgoHudAim& Aim, FVector2D& OutScreen)
{
	FVector Point = Aim.Point;
	const AGinkgoPlayerCameraManager* Camera = Cast<AGinkgoPlayerCameraManager>(Controller.PlayerCameraManager);
	if (Camera && Aim.Source == EGinkgoAimSource::Aim)
	{
		// 这个方向在叠加之前的视角里是哪里，就放到最终的视角里同样的地方：画面晃的时候点不跟着世界走
		const FMinimalViewInfo Final = Camera->GetCameraCacheView();
		const FMinimalViewInfo& Before = Camera->GetAimView();
		const FQuat FromAimToFinal = Final.Rotation.Quaternion() * Before.Rotation.Quaternion().Inverse();
		Point = Final.Location + FromAimToFinal.RotateVector(Aim.Point - Before.Location);
	}
	return Controller.ProjectWorldLocationToScreen(Point, OutScreen, false);
}

bool FGinkgoWeaponHud::ProjectHit(const APlayerController& Controller, const FGinkgoHudAim& Aim, FVector2D& OutScreen)
{
	return Aim.bHasHit && Controller.ProjectWorldLocationToScreen(Aim.HitDrawPoint, OutScreen, false);
}

FGinkgoHudAim FGinkgoWeaponHud::FindAim(const APlayerController& Controller)
{
	FGinkgoHudAim Aim;
	const FMinimalViewInfo CameraView = Controller.PlayerCameraManager ? Controller.PlayerCameraManager->GetCameraCacheView() : FMinimalViewInfo();
	Aim.Point = CameraView.Location + CameraView.Rotation.Vector() * AimTraceDistance;

	const AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(Controller.GetPawn());
	const UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
	// 徒手，或拔枪开始时枪还在背上：屏幕中心
	if (!Weapons || !Weapons->GetHeldWeapon() || Character->IsWeaponStowed(Weapons->GetHeldSlot()))
	{
		return Aim;
	}
	const AGinkgoPlayerCameraManager* Camera = Cast<AGinkgoPlayerCameraManager>(Controller.PlayerCameraManager);
	const FMinimalViewInfo& AimView = Camera ? Camera->GetAimView() : CameraView;
	const FGinkgoHoldState& Hold = Weapons->GetHoldState();
	Aim.Source = EGinkgoAimSource::Aim;
	Aim.AdsAlpha = Hold.GetAdsAlpha();
	Aim.Point = AimView.Location + Weapons->GetAimRotation(AimView.Rotation).Vector() * AimTraceDistance;
	// 第一人称开镜（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：相机这一帧滑向了瞄具的后面，瞄具在相机记下的位置上（同一帧的枪）
	const FGinkgoScope* Scope = Weapons->GetHeldScope();
	Aim.bScoped = Scope && Camera && Camera->GetFirstPersonAlpha() > 0.f;
	if (Aim.bScoped)
	{
		Aim.SightPoint = Camera->GetSightFrame().Eye + Camera->GetSightFrame().GetAxis() * AimTraceDistance;
	}
	const UGinkgoWeaponDefinition* Held = Weapons->GetHeldWeapon();
	// 散布：两边的横线的内端、开镜准星的横柱都画在扩散的圆锥上。开枪用的是这一刻的扩散，打完才长，所以开枪之前看到的就是这一枪的圆锥
	Aim.SpreadPixels = Ginkgo::Weapon::SpreadToPixels(Hold.Spread, CameraView.FOV);
	Aim.bSpreadRing = Held->AdsReticle == SpreadRingReticle && !Aim.bScoped;
	if (!Aim.bSpreadRing)
	{
		Aim.DashOffset = Ginkgo::Weapon::CrosshairDashOffset(Held->CrosshairGap, static_cast<float>(Aim.SpreadPixels));
		Aim.ReticleOpen = FMath::Max(static_cast<float>(Aim.SpreadPixels) - Held->AdsReticleGap, 0.f);
	}
	// 枪抬到位了才开得了枪。枪挂在虚拟骨骼上，Control Rig 转过它，网格组件这一帧已经跟着动过；下一帧开枪时用的就是这个姿势
	if (Hold.IsRaised() && Weapons->IsShotStepClear())
	{
		FVector Muzzle;
		bool bHit = false;
		Aim.bHasHit = Weapons->TraceShot(Muzzle, Aim.HitPoint, bHit);
	}
	if (Aim.bHasHit)
	{
		Aim.HitDrawPoint = Aim.HitPoint;
		// 开镜时准星在这一帧画面的中心，子弹也打那里；腰射时点按叠加之前的视线画，枪也对着它，开枪那一下相机晃不算对不上
		const bool bAdsShot = Aim.AdsAlpha >= UGinkgoWeaponComponent::AdsShotAlpha;
		if (Aim.bScoped && bAdsShot)
		{
			// 第一人称开镜：子弹沿瞄具的光轴打（分划的尖），打到的点在镜内是放大了的，透过瞄具算它在画面上哪里，同分划的尖比。
			// 出了镜内的视场（枪口被镜子看不到的近处挡住）时画在它真的位置上
			FVector Direction;
			double Rho = 0.0;
			if (Ginkgo::Scope::ProjectThroughScope(Camera->GetSightFrame(), Scope->Optics, Aim.HitPoint, Direction, Rho))
			{
				Aim.HitDrawPoint = CameraView.Location + Direction * AimTraceDistance;
			}
			FVector2D HitPlane;
			FVector2D SightPlane;
			const bool bInFront = ToViewPlane(CameraView, Aim.HitDrawPoint, HitPlane) && ToViewPlane(CameraView, Aim.SightPoint, SightPlane);
			Aim.HitGap = bInFront ? FVector2D::Distance(HitPlane, SightPlane) : 10000.0;
		}
		else
		{
			const FMinimalViewInfo& GapView = bAdsShot ? CameraView : AimView;
			FVector2D HitPlane;
			FVector2D MarkPlane = FVector2D::ZeroVector;
			const bool bInFront = ToViewPlane(GapView, Aim.HitPoint, HitPlane) && (bAdsShot || ToViewPlane(GapView, Aim.Point, MarkPlane));
			// 在相机后面：差得不能再多了
			Aim.HitGap = bInFront ? FVector2D::Distance(HitPlane, MarkPlane) : 10000.0;
		}
		Aim.HitScale = Ginkgo::Weapon::HitMarkScale(Aim.HitGap);
	}
	return Aim;
}

FString FGinkgoWeaponHud::BuildWeaponMessage() const
{
	const AGinkgoCharacter* Character = Controller.IsValid() ? Cast<AGinkgoCharacter>(Controller->GetPawn()) : nullptr;
	const UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
	// 武器栏写手上那把；徒手时写收枪键会拔的那把（淡下去）
	int32 Slot = Weapons ? Weapons->GetHeldSlot() : INDEX_NONE;
	if (Weapons && Slot == INDEX_NONE)
	{
		Slot = Weapons->FindDrawSlot();
	}
	const UGinkgoWeaponDefinition* Weapon = Weapons ? Weapons->GetCarried(Slot) : nullptr;

	// 窗口时间轴开着时它占着屏幕下方：武器栏挪到它上面（UI 美术："窗口时间轴打开时，底排整体上移到时间轴之上"）
	const AGinkgoPlayerController* GinkgoController = Cast<AGinkgoPlayerController>(Controller.Get());
	const FGinkgoWindowTimeline* Timeline = GinkgoController ? GinkgoController->GetWindowTimeline() : nullptr;
	const float TimelineTop = Timeline ? Timeline->GetPanelTop() : 0.f;

	FString Message;
	const TSharedRef<FHudJsonWriter> Writer = FHudJsonWriterFactory::Create(&Message);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("lift"), TimelineTop > 0.f ? TimelineTop + TimelineGap : 0.f);
	Writer->WriteValue(TEXT("weapon"), Weapon != nullptr);
	if (Weapon)
	{
		const bool bInHand = Slot == Weapons->GetHeldSlot() && !Character->IsWeaponStowed(Slot);
		Writer->WriteValue(TEXT("name"), Weapon->DisplayName.ToString());
		Writer->WriteValue(TEXT("caliber"), Weapon->CaliberName.ToString());
		Writer->WriteValue(TEXT("icon"), Weapon->HudIcon.IsNone() ? FString() : Weapon->HudIcon.ToString());
		Writer->WriteObjectStart(TEXT("attachments"));
		for (const UGinkgoWeaponAttachment* Part : Weapons->GetAttachments(Slot).Parts)
		{
			const FGinkgoWeaponAttachmentMount* Mount = Weapon->FindAttachment(Part);
			if (Mount && !Part->HudIcon.IsNone())
			{
				// Shipping 下 FName 不保大小写，挂点名统一小写输出，避免 CSS 类名 .wh-mount-* / .wh-layer-* 失配。
				Writer->WriteValue(Mount->Mount.ToString().ToLower(), Part->HudIcon.ToString());
			}
		}
		Writer->WriteObjectEnd();
		Writer->WriteArrayStart(TEXT("layers"));
		FVector2D MuzzleAnchor = FVector2D::ZeroVector;
		for (const UGinkgoWeaponAttachment* Part : Weapons->GetAttachments(Slot).Parts)
		{
			const auto* Mount = Weapon->FindAttachment(Part);
			if (Mount && !Mount->MuzzleHudAnchor.IsZero()) { MuzzleAnchor = Mount->MuzzleHudAnchor; }
		}
		for (const auto& Mount : Weapon->Attachments)
		{
			const auto* Part = Mount.Part.Get();
			if (!Part || Part->bEmptyModule || Part->HudSize.IsZero() || !Weapons->GetAttachments(Slot).Parts.Contains(Part)) { continue; }
			const FVector2D Anchor = Mount.Mount == TEXT("muzzle") && !MuzzleAnchor.IsZero() ? MuzzleAnchor : Mount.HudAnchor;
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("mount"), Mount.Mount.ToString().ToLower());
			Writer->WriteValue(TEXT("icon"), Part->HudIcon.ToString());
			Writer->WriteValue(TEXT("x"), Anchor.X - Part->HudPivot.X);
			Writer->WriteValue(TEXT("y"), Anchor.Y - Part->HudPivot.Y);
			Writer->WriteValue(TEXT("w"), Part->HudSize.X);
			Writer->WriteValue(TEXT("h"), Part->HudSize.Y);
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteValue(TEXT("reticle"), Weapon->AdsReticle.IsNone() ? FString() : Weapon->AdsReticle.ToString());
		Writer->WriteValue(TEXT("inHand"), bInHand);
		// 枪抬起来了（腰射、开镜）：武器栏亮起来，平时更透明
		Writer->WriteValue(TEXT("raised"), bInHand && Ginkgo::Weapon::WantsRaised(Weapons->GetHoldState().Stance));
		if (const FGinkgoMechanismState* State = Weapons->GetMechanismState(Slot))
		{
			// 弹种（Docs/Design/Weapons.md「切换弹种」）：同一口径的几种，每一发、膛里那一发、装填压的那种按它们的下标写
			const TArray<FGameplayTag, TInlineAllocator<8>> Choices = UGinkgoAmmoDefinition::GetChoices(Weapon->AmmoType);
			const FGameplayTag Load = Weapons->GetLoadAmmo(Slot);
			WriteAmmoChoices(*Writer, TEXT("ammo"), *Weapon, Choices);
			Writer->WriteObjectStart(TEXT("mechanism"));
			Writer->WriteValue(TEXT("feed"), State->Feed.Num());
			Writer->WriteValue(TEXT("capacity"), Weapons->GetMechanism(Slot)->GetCapacity());
			Writer->WriteValue(TEXT("detachable"), Weapon->Mechanism->GetFeed() == EGinkgoFeed::DetachableMagazine);
			Writer->WriteValue(TEXT("magazineIn"), State->bMagazineIn);
			Writer->WriteValue(TEXT("chamber"), PageName(State->Chamber));
			Writer->WriteValue(TEXT("action"), PageName(State->Action));
			// 后备数的是装填要压的那一种（轮盘选的）
			Writer->WriteValue(TEXT("reserve"), Weapons->GetAmmoReserve().Count(Load));
			Writer->WriteValue(TEXT("load"), Choices.IndexOfByKey(Load));
			Writer->WriteValue(TEXT("chamberAmmo"), State->Chamber == EGinkgoChamber::Empty ? INDEX_NONE : Choices.IndexOfByKey(State->ChamberAmmo));
			// 开膛待击打的是弹链最上面那一发
			Writer->WriteValue(TEXT("nextAmmo"), Choices.IndexOfByKey(Ginkgo::Weapon::GetNextRoundAmmo(*State)));
			Writer->WriteArrayStart(TEXT("rounds"));
			for (const FGameplayTag& Round : State->Feed)
			{
				Writer->WriteValue(Choices.IndexOfByKey(Round));
			}
			Writer->WriteArrayEnd();
			// 开膛待击（MG3）：准备好时膛空、枪机在后面是常态，页面画弹链上的下一发、不写"栓开"；弹链不装填，不写后备
			Writer->WriteValue(TEXT("openBolt"), Weapon->Mechanism->IsOpenBolt());
			Writer->WriteValue(TEXT("reloadable"), Weapon->Mechanism->GetFeed() != EGinkgoFeed::Belt);
			Writer->WriteObjectEnd();
		}
	}
	Writer->WriteObjectEnd();
	Writer->Close();
	return Message;
}

void FGinkgoWeaponHud::OnWorldPostActorTick(UWorld* InWorld, const ELevelTick TickType, const float DeltaSeconds)
{
	const APlayerController* PlayerController = Controller.Get();
	if (!PlayerController || InWorld != PlayerController->GetWorld() || !View || TickType == LEVELTICK_TimeOnly)
	{
		return;
	}
	SCOPE_CYCLE_COUNTER(STAT_GinkgoWeaponHud);
	const auto* Pawn = Cast<AGinkgoCharacter>(PlayerController->GetPawn());
	const bool bAtBench = Pawn && Pawn->GetWorkbench();
	const auto* GinkgoPC = Cast<AGinkgoPlayerController>(PlayerController);
	const bool bSuppressed = GinkgoPC && GinkgoPC->IsGameplayHudSuppressed();
	const EVisibility Visibility = bAtBench ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
	if (Widget->GetVisibility() != Visibility) { Widget->SetVisibility(Visibility); }
	if (bAtBench || bSuppressed)
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
		const FString Directory = FPaths::ProjectContentDir() / HudPageDirectory;
		if (!Ginkgo::UI::LoadPageWithKeycaps(*View, Directory))
		{
			UE_LOG(LogGinkgoUI, Warning, TEXT("[WeaponHud] 武器界面的页面载入失败：%s（原因见 LogWeft）"), *Directory);
			Hide();
			return;
		}
		bLoaded = true;
	}

	// 瞄准的记号：投影到这一帧的画面（相机已经更新过），锚点是页面的逻辑 px
	const FGinkgoHudAim Aim = FindAim(*PlayerController);
	const FVector2D LogicalSize = View->GetLogicalSize();
	const FVector2D PixelToLogical(LogicalSize.X / Pixels.X, LogicalSize.Y / Pixels.Y);
	FVector2D Screen = Pixels * 0.5;
	const bool bOnScreen = ProjectDot(*PlayerController, Aim, Screen);
	const AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(PlayerController->GetPawn());
	const UGinkgoWeaponDefinition* Held = Character && Character->GetWeapons() ? Character->GetWeapons()->GetHeldWeapon() : nullptr;
	// 没有开镜准星的枪开镜时也是那个点。第一人称开镜（带瞄具的枪）时越肩的开镜准星不画：镜内有真的分划，点照样随开镜进度收拢
	const bool bHasReticle = Held && !Held->AdsReticle.IsNone();
	const float ReticleScale = bHasReticle && !Aim.bScoped && !Aim.bSpreadRing ? Aim.AdsAlpha : 0.f;
	const float DotScale = bHasReticle ? 1.f - Aim.AdsAlpha : 1.f;
	const TPair<int64, float> Marks[] = { {DotAnchorKey, DotScale}, {ReticleAnchorKey, ReticleScale} };
	Anchors.Reset();
	for (const TPair<int64, float>& Mark : Marks)
	{
		FWeftAnchor& Anchor = Anchors.AddDefaulted_GetRef();
		Anchor.Key = Mark.Key;
		// 开镜准星永远在屏幕正中心（你说的）：开镜时子弹也打这里（UGinkgoWeaponComponent::TraceShot）
		const bool bReticle = Mark.Key == ReticleAnchorKey;
		Anchor.Position = (bReticle ? Pixels * 0.5 : Screen) * PixelToLogical;
		Anchor.Scale = Mark.Value;
		Anchor.bHidden = Aim.bSpreadRing || Mark.Value < MinVisibleScale;
		Anchor.bBehind = !bReticle && !bOnScreen;
	}
	FVector2D HitScreen = Pixels * 0.5;
	const bool bHitOnScreen = ProjectHit(*PlayerController, Aim, HitScreen);
	FWeftAnchor& HitAnchor = Anchors.AddDefaulted_GetRef();
	HitAnchor.Key = HitAnchorKey;
	HitAnchor.Position = HitScreen * PixelToLogical;
	HitAnchor.Scale = Aim.HitScale;
	HitAnchor.bHidden = Aim.HitScale < MinVisibleScale;
	HitAnchor.bBehind = !bHitOnScreen;
	// 准星两边的横线（"- · -"）跟着点走，开镜准星的两根横柱跟着屏幕中心走：开镜时一个收拢、一个张开，同点与开镜准星。锚点是横线的内端（页面的框的中心）
	const double FromHeight1080 = LogicalSize.Y / 1080.0;
	const float DashScale = DotScale;
	for (const TPair<int64, double>& Side : {TPair<int64, double>(DashLeftAnchorKey, -1.0), TPair<int64, double>(DashRightAnchorKey, 1.0)})
	{
		FWeftAnchor& Anchor = Anchors.AddDefaulted_GetRef();
		Anchor.Key = Side.Key;
		Anchor.Position = Screen * PixelToLogical + FVector2D(Side.Value * Aim.DashOffset * DashScale * FromHeight1080, 0.0);
		Anchor.Scale = DashScale;
		Anchor.bHidden = Aim.DashOffset <= 0.f || DashScale < MinVisibleScale;
		Anchor.bBehind = !bOnScreen;
	}
	for (const TPair<int64, double>& Side : {TPair<int64, double>(PostLeftAnchorKey, -1.0), TPair<int64, double>(PostRightAnchorKey, 1.0)})
	{
		FWeftAnchor& Anchor = Anchors.AddDefaulted_GetRef();
		Anchor.Key = Side.Key;
		Anchor.Position = Pixels * 0.5 * PixelToLogical + FVector2D(Side.Value * Aim.ReticleOpen * ReticleScale * FromHeight1080, 0.0);
		Anchor.Scale = ReticleScale;
		Anchor.bHidden = ReticleScale < MinVisibleScale;
	}
	// 命中的白 X 在准星的中心（Docs/Design/Damage.md「命中的白 X」）：不开镜时是点，开镜时是屏幕中心（同开镜准星）；点不在画面里时是屏幕中心。缩放由页面自己弹。
	// 第一人称开镜时是分划的尖（瞄具的光轴，相机在光轴上时就是屏幕中心）
	const FVector2D Center = Pixels * 0.5;
	const FVector2D AimCenter = bOnScreen ? Screen : Center;
	const FVector2D RingCenter = Aim.AdsAlpha >= UGinkgoWeaponComponent::AdsShotAlpha ? Center : AimCenter;
	if (Aim.bSpreadRing)
	{
		FWeftAnchor& Ring = Anchors.AddDefaulted_GetRef();
		Ring.Key = SpreadRingAnchorKey;
		Ring.Position = RingCenter * PixelToLogical;
		Ring.Scale = Aim.SpreadPixels * FromHeight1080 / SpreadRingRadius;
		Ring.bHidden = Ring.Scale < MinVisibleScale;
		Ring.bBehind = Aim.AdsAlpha < UGinkgoWeaponComponent::AdsShotAlpha && !bOnScreen;
	}
	FVector2D ReticleCenter = Center;
	if (Aim.bScoped && !PlayerController->ProjectWorldLocationToScreen(Aim.SightPoint, ReticleCenter, false))
	{
		ReticleCenter = Center;
	}
	FWeftAnchor& ConfirmAnchor = Anchors.AddDefaulted_GetRef();
	ConfirmAnchor.Key = ConfirmAnchorKey;
	ConfirmAnchor.Position = (Aim.bSpreadRing ? RingCenter : FMath::Lerp(bOnScreen ? Screen : ReticleCenter, ReticleCenter, static_cast<double>(1.f - DotScale))) * PixelToLogical;
	ConfirmAnchor.Scale = 1.f;
	// 弹种轮盘在屏幕中心；指针的小点指着这一帧选的方向（摇杆推着时是摇杆，不然是鼠标攒的指针），在死区里时藏起来
	const UGinkgoWeaponComponent* WheelWeapons = Character ? Character->GetWeapons() : nullptr;
	const FGinkgoAmmoWheel* Wheel = WheelWeapons ? &WheelWeapons->GetAmmoWheel() : nullptr;
	FWeftAnchor& WheelAnchor = Anchors.AddDefaulted_GetRef();
	WheelAnchor.Key = WheelAnchorKey;
	WheelAnchor.Position = Center * PixelToLogical;
	WheelAnchor.Scale = 1.f;
	View->SetAnchors(Anchors);

	const FString Message = BuildWeaponMessage();
	if (Message != LastMessage)
	{
		LastMessage = Message;
		View->Emit(TEXT("weapon"), Message, true);
		UE_LOG(LogGinkgoUI, Verbose, TEXT("[WeaponHud] 发给武器栏：%s"), *Message);
	}

	// 弹种轮盘：开、关、选的变了（Serial）才发；页面刚载入时也发一次当前的
	if (Wheel && (Wheel->Serial != LastWheelSerial || LastWheelSerial == INDEX_NONE))
	{
		LastWheelSerial = Wheel->Serial;
		const FString WheelMessage = BuildWheelMessage(*WheelWeapons, *PlayerController);
		View->Emit(TEXT("wheel"), WheelMessage, true);
		UE_LOG(LogGinkgoUI, Verbose, TEXT("[WeaponHud] 弹种轮盘：%s"), *WheelMessage);
	}

	// 打中了目标：每一枪发一次，排着队不合并（连发时每一枪都弹）。页面刚载入时只记下现在是第几次
	if (const UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr)
	{
		const FGinkgoHitConfirm& Confirm = Weapons->GetHitConfirm();
		if (LastConfirmSerial == INDEX_NONE)
		{
			LastConfirmSerial = Confirm.Serial;
		}
		else if (Confirm.Serial != LastConfirmSerial)
		{
			LastConfirmSerial = Confirm.Serial;
			const FString ConfirmMessage = BuildConfirmMessage(Confirm);
			View->Emit(TEXT("confirm"), ConfirmMessage, false);
			UE_LOG(LogGinkgoUI, Verbose, TEXT("[WeaponHud] 命中：%s"), *ConfirmMessage);
		}
	}
}

namespace
{
	/**
	 * 轮盘底下的键位提示：这个动作在这个设备上的键
	 * @return 按着的那个（两个键位都绑了时，按的是哪个提示哪个）；都没按时主键，主键没绑时辅助键；都没绑时无效
	 */
	FKey FindPromptKey(const FGinkgoInputSettings& Settings, const APlayerController& Controller, const EGinkgoInputDirectory Directory, const EGinkgoKeyAction Action)
	{
		FKey First;
		for (const EGinkgoKeySlot Slot : {EGinkgoKeySlot::Primary, EGinkgoKeySlot::Secondary})
		{
			const int32 Index = Ginkgo::Input::KeyBindingIndex(Directory, Action, Slot);
			if (!Settings.Keys.IsValidIndex(Index) || !Settings.Keys[Index].Key.IsValid())
			{
				continue;
			}
			const FKey& Key = Settings.Keys[Index].Key;
			if (Controller.IsInputKeyDown(Key))
			{
				return Key;
			}
			if (!First.IsValid())
			{
				First = Key;
			}
		}
		return First;
	}

	void WritePromptKey(FHudJsonWriter& Writer, const TCHAR* Field, const FKey& Key)
	{
		if (!Key.IsValid())
		{
			return;
		}
		Writer.WriteObjectStart(Field);
		Writer.WriteValue(TEXT("key"), Key.GetFName().ToString());
		Writer.WriteValue(TEXT("label"), Key.GetDisplayName(false).ToString());
		Writer.WriteObjectEnd();
	}
}

FString FGinkgoWeaponHud::BuildWheelMessage(const UGinkgoWeaponComponent& Weapons, const APlayerController& PlayerController)
{
	const FGinkgoAmmoWheel& Wheel = Weapons.GetAmmoWheel();
	const UGinkgoWeaponDefinition* Weapon = Weapons.GetCarried(Wheel.Slot);
	FString Message;
	const TSharedRef<FHudJsonWriter> Writer = FHudJsonWriterFactory::Create(&Message);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("open"), Wheel.bOpen && Weapon != nullptr);
	static const TCHAR* const ResultNames[] = {TEXT("none"), TEXT("confirmed"), TEXT("cancelled"), TEXT("unavailable")};
	Writer->WriteValue(TEXT("result"), ResultNames[FMath::Clamp(static_cast<int32>(Wheel.LastResult), 0, 3)]);
	Writer->WriteValue(TEXT("selected"), Wheel.Selected);
	Writer->WriteValue(TEXT("current"), Wheel.Current);
	if (Weapon)
	{
		Writer->WriteValue(TEXT("weapon"), Weapon->DisplayName.ToString());
		WriteAmmoChoices(*Writer, TEXT("entries"), *Weapon, Wheel.Choices);
		// 每一项的说明与后备（轮盘中间写选着的那一项的说明，各项写后备，没有的灰掉）
		Writer->WriteArrayStart(TEXT("details"));
		for (const FGameplayTag& Choice : Wheel.Choices)
		{
			const UGinkgoAmmoDefinition* Ammo = UGinkgoAmmoDefinition::Find(Choice);
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("name"), Ammo ? Ammo->DisplayName.ToString() : Choice.ToString());
			Writer->WriteValue(TEXT("desc"), Ammo ? Ammo->Description.ToString() : FString());
			Writer->WriteValue(TEXT("count"), Weapons.GetAmmoReserve().Count(Choice));
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
	}
	// 键位提示（你说的）：打开轮盘的那个设备（装填键按着的是手柄的键就是手柄）上，松开确认的键、取消的键（开镜）
	if (const UGinkgoInputSettingsSubsystem* InputSettings = UGinkgoInputSettingsSubsystem::Get(&PlayerController))
	{
		const FGinkgoInputSettings& Keys = InputSettings->GetCurrent();
		const FKey PadReload = FindPromptKey(Keys, PlayerController, EGinkgoInputDirectory::Gamepad, EGinkgoKeyAction::Reload);
		const EGinkgoInputDirectory Device = PadReload.IsValid() && PlayerController.IsInputKeyDown(PadReload) ? EGinkgoInputDirectory::Gamepad : EGinkgoInputDirectory::KeyboardMouse;
		Writer->WriteObjectStart(TEXT("prompts"));
		Writer->WriteValue(TEXT("device"), Device == EGinkgoInputDirectory::Gamepad ? TEXT("pad") : TEXT("mouse"));
		WritePromptKey(*Writer, TEXT("confirm"), FindPromptKey(Keys, PlayerController, Device, EGinkgoKeyAction::Reload));
		WritePromptKey(*Writer, TEXT("cancel"), FindPromptKey(Keys, PlayerController, Device, EGinkgoKeyAction::Aim));
		Writer->WriteObjectEnd();
	}
	Writer->WriteObjectEnd();
	Writer->Close();
	return Message;
}

FString FGinkgoWeaponHud::BuildConfirmMessage(const FGinkgoHitConfirm& Confirm)
{
	FString Message;
	const TSharedRef<FHudJsonWriter> Writer = FHudJsonWriterFactory::Create(&Message);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("serial"), Confirm.Serial);
	Writer->WriteValue(TEXT("hits"), Confirm.Hits);
	Writer->WriteValue(TEXT("kill"), Confirm.bKill);
	Writer->WriteValue(TEXT("head"), Confirm.bHead);
	Writer->WriteValue(TEXT("armor"), Confirm.bArmor);
	Writer->WriteValue(TEXT("armorBroken"), Confirm.bArmorBroken);
	Writer->WriteValue(TEXT("health"), FMath::RoundToInt(Confirm.HealthDamage));
	Writer->WriteValue(TEXT("armorDamage"), FMath::RoundToInt(Confirm.ArmorDamage));
	Writer->WriteObjectEnd();
	Writer->Close();
	return Message;
}

#include "Camera/CameraComponent.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/GinkgoAnimTypes.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Components/PoseableMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Control/GinkgoInputIntent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/GinkgoInteractionSubsystem.h"
#include "Interaction/GinkgoInteractorComponent.h"
#include "Interaction/GinkgoWorkbench.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UI/GinkgoUiTheme.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "WeftSubsystem.h"
#include "WeftView.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponPickup.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ContentStreaming.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

/** 从交互键进桌，经相机混合、双手 IK、配件安装与拾取，验证服务器状态和角色最终的挂点 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWorkbenchFlowTest, "CaseGinkgo.Interaction.Workbench",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWorkbenchFlowTest::RunTest(const FString& Parameters)
{
	const FString Name = TEXT("BenchTest") + FGuid::NewGuid().ToString();
	UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		Instance->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		Instance->RemoveFromRoot();
	};

	auto* PC = World->SpawnActor<AGinkgoPlayerController>();
	auto* Character = World->SpawnActor<AGinkgoCharacter>(FVector(-110, 0, 92), FRotator::ZeroRotator);
	auto* Bench = World->SpawnActor<AGinkgoWorkbench>();
	auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
	if (!TestNotNull(TEXT("AK 武器定义"), Weapon) || !PC || !Character || !Bench) { return false; }
	PC->SetAsLocalPlayerController();
	PC->Possess(Character);
	PC->SetViewTarget(Character);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	auto* Camera = Cast<AGinkgoPlayerCameraManager>(PC->PlayerCameraManager);
	auto* Registry = World->GetSubsystem<UGinkgoInteractionSubsystem>();
	if (!TestNotNull(TEXT("玩家相机管理器"), Camera) || !TestNotNull(TEXT("交互目标登记表"), Registry)) { return false; }
	Character->DispatchBeginPlay();
	Bench->DispatchBeginPlay();
	TestTrue(TEXT("BeginPlay 把桌子登记为交互目标"), Registry->IsRegistered(Bench));
	// 独立测试世界没有视口和玩家连接，相机位置由本机更新。
	Camera->bUseClientSideCameraUpdates = false;
	auto* Interactor = Character->GetInteractor();
	FText Reason;
	auto* Weapons = Character->GetWeapons();
	for (int32 Slot = 0; Slot < Weapons->SlotCount; ++Slot) { Weapons->SetCarried(Slot, nullptr); }
	TestEqual(TEXT("空背包进桌被拒绝"), Bench->GetAvailability({Character, Interactor}, Reason), EGinkgoInteractionAvailability::Blocked);
	Weapons->SetCarried(0, Weapon);
	Weapons->UpdateHoldState(1.f / 60.f);
	Weapons->SetHeldSlot(0);
	Character->AttachWeaponToHand(0);
	auto* Gun = Character->GetHeldWeaponMesh();
	Character->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Character->GetMesh()->bEnableUpdateRateOptimizations = false;
	if (!TestNotNull(TEXT("手上的 AK 网格"), Gun)) { return false; }
	const auto* State = Weapons->GetMechanismState(0);
	const FName Grip = Gun->GetAttachSocketName();
	const auto* TableCamera = Bench->FindComponentByClass<UCameraComponent>();
	const auto* LampSpot = Bench->FindComponentByClass<USpotLightComponent>();
	if (!TestNotNull(TEXT("工作台台灯聚光灯"), LampSpot)) { return false; }
	const FBox BenchWorldBounds = Bench->GetComponentsBoundingBox(false);
	TestTrue(TEXT("正式改装桌模型尺寸大于旧灰盒且底部贴地"),
		BenchWorldBounds.GetSize().Y > 220.0 && BenchWorldBounds.GetSize().X > 100.0 && FMath::IsNearlyZero(BenchWorldBounds.Min.Z, 0.5));
	TestTrue(TEXT("未交互时台灯处于关闭状态"), !Bench->IsLampOn() && Bench->GetLampAlpha() == 0.f && !LampSpot->IsVisible());
	const FVector Eye = TableCamera->GetComponentLocation();
	auto Step = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			constexpr float Dt = 1.f / 60.f;
			World->TimeSeconds += Dt;
			if (Bench->IsActorTickEnabled()) { Bench->Tick(Dt); }
			Weapons->UpdateHoldState(Dt);
			Character->GetAnimActions()->TickComponent(Dt, LEVELTICK_All, nullptr);
			Character->GetMesh()->TickAnimation(Dt, false);
			// 这些模拟步都在同一个引擎帧里。动画代理每个 GFrameCounter 只更新一次原生 Rig 输入，测试里手动推进。
			Character->GetMesh()->GetAnimInstance()->NativeThreadSafeUpdateAnimation(Dt);
			Character->GetMesh()->RefreshBoneTransforms(nullptr);
			Camera->UpdateCamera(Dt);
			Interactor->TickComponent(Dt, LEVELTICK_All, nullptr);
		}
	};
	Step(1);
	auto Enter = [&]()
	{
		TestTrue(TEXT("常规交互焦点选中桌子"), Interactor->GetFocus() && Interactor->GetFocus()->Target.GetObject() == Bench);
		Interactor->RequestInteract();
		return TestTrue(TEXT("通过交互键占用工作台"), Character->GetWorkbench() == Bench);
	};
	const FVector PlayerEye = Camera->GetCameraLocation();
	if (!Enter()) { return false; }
	TestTrue(TEXT("进桌期间仍控制同一角色"), PC->GetPawn() == Character);
	Step(12);
	TestTrue(TEXT("相机已离开肩部且尚未到达桌前"), !Camera->GetCameraLocation().Equals(PlayerEye, 4) && !Camera->GetCameraLocation().Equals(Eye, 4));
	TestTrue(TEXT("拨到开关卡点前台灯尚未点亮"), !Bench->IsLampOn() && Bench->GetGunAlpha() == 0.f);
	const FVector BeforeCancel = Camera->GetCameraLocation();
	Interactor->RequestInteract();
	TestTrue(TEXT("中途退出让过渡反向"), Bench->IsExiting());
	Step(1);
	TestTrue(TEXT("反向过渡第一帧的位置连续"), FVector::Dist(BeforeCancel, Camera->GetCameraLocation()) < 2.0);
	Step(65);
	TestNull(TEXT("提前退出释放工作台"), Character->GetWorkbench());
	TestTrue(TEXT("未拨亮便提前退出时台灯保持关闭且枪仍在原握点"), !Bench->IsLampOn() && !LampSpot->IsVisible() && Gun->GetAttachSocketName() == Grip);
	if (!Enter()) { return false; }
	Step(14);
	const auto* Body = Character->GetMesh();
	const double LampHandDist = FVector::Distance(Body->GetSocketLocation(TEXT("hand_l")), Bench->GetInstallHandPose().WristLocation);
	AddInfo(FString::Printf(TEXT("拨灯时左手腕距开关目标 %.1f cm"), LampHandDist));
	TestTrue(TEXT("拨灯时左手伸达台灯开关目标"), Bench->GetInstallHandPose().LeftGrip > 0.9f && LampHandDist < 8.0);
	Step(4);
	TestTrue(TEXT("进桌先打开台灯，此时武器尚未开始向桌面移动"),
		Bench->IsLampOn() && Bench->GetGunAlpha() == 0.f && Gun->GetAttachParent() == Character->GetMesh());
	Step(48);
	const FVector HandSpan = Body->GetSocketLocation(TEXT("hand_r")) - Body->GetSocketLocation(TEXT("hand_l"));
	TestTrue(TEXT("AK 放到已点亮的桌面时左右手腕没有交叉"),
		Bench->IsLampOn() && LampSpot->IsVisible() && LampSpot->Intensity > 0.f && FVector::DotProduct(HandSpan, Character->GetActorRightVector()) > 0.f);
	TestTrue(TEXT("放枪动作期间保持拉远机位展示角色"), !Bench->IsCameraFocusingWeapon() && Bench->GetCameraFocusAlpha() < 0.05f);
	Step(42);
	TestTrue(TEXT("桌前镜头实时跟踪工作台动态相机"), TableCamera->GetComponentLocation().Equals(Camera->GetCameraLocation(), 0.25));
	TestTrue(TEXT("枪挂在桌上并到达放置目标"), Gun->GetAttachParent()->GetOwner() == Bench && Gun->GetComponentTransform().Equals(Bench->GetRestPose(), 0.01));
	TestTrue(TEXT("接触桌面后双手松开且台灯持续照明"), Bench->GetGripAlpha() == 0.f && !Character->IsHeldWeaponInHand() && Bench->IsLampOn() && Bench->GetLampAlpha() == 1.f);
	TestTrue(TEXT("工作台同一时间只由一个角色占用"), Bench->GetAvailability({Character, Interactor, 0}, Reason) != EGinkgoInteractionAvailability::Available);
	const FVector Across = Bench->GetRestPose().TransformVectorNoScale(Weapon->LoweredHands.Right.GetLocation() - Weapon->LoweredHands.Left.GetLocation());
	TestTrue(TEXT("AK 的右手握点仍在左手握点右侧"), FVector::DotProduct(Across, Character->GetActorRightVector()) > 0.f);
	auto ProjectToBenchViewport = [&](const FVector Point)
	{
		const FVector Local = TableCamera->GetComponentTransform().InverseTransformPosition(Point);
		if (Local.X <= 1.0)
		{
			return FVector2D(-1.0, -1.0);
		}
		const double TanHalfX = FMath::Tan(FMath::DegreesToRadians(TableCamera->FieldOfView * 0.5));
		const double TanHalfY = TanHalfX * (9.0 / 16.0);
		return FVector2D(
			0.5 + Local.Y / (2.0 * Local.X * TanHalfX),
			0.5 - Local.Z / (2.0 * Local.X * TanHalfY));
	};
	auto ScreenX = [&](FVector Point)
	{
		return ProjectToBenchViewport(Point).X;
	};
	auto IsInBenchViewport = [&](const FVector Point)
	{
		const FVector2D Screen = ProjectToBenchViewport(Point);
		return Screen.X >= 0.0 && Screen.X <= 1.0 && Screen.Y >= 0.0 && Screen.Y <= 1.0;
	};
	// 放枪结束进入无动作检视态：相机平滑推近聚焦枪械、让人物基本出画，并带有轻微手持晃动。
	Step(95);
	TestTrue(TEXT("无动作时相机推近聚焦桌上武器"),
		Bench->IsCameraFocusingWeapon() && Bench->GetCameraFocusAlpha() > 0.95f
		&& TableCamera->FieldOfView < 67.f
		&& FVector::Dist(TableCamera->GetComponentLocation(), Bench->GetInteractionLocation())
			< FVector::Dist(Eye, Bench->GetInteractionLocation()) - 12.0);
	TestTrue(TEXT("聚焦枪械时角色头胸已移出画面"),
		!IsInBenchViewport(Body->GetSocketLocation(TEXT("head")))
		&& !IsInBenchViewport(Body->GetSocketLocation(TEXT("spine_05"))));
	{
		const FVector SettledLoc = TableCamera->GetComponentLocation();
		const FQuat SettledRot = TableCamera->GetComponentQuat();
		double MaxSwayLoc = 0.0;
		double MaxSwayDeg = 0.0;
		for (int32 Frame = 0; Frame < 60; ++Frame)
		{
			Step(1);
			MaxSwayLoc = FMath::Max(MaxSwayLoc, FVector::Dist(SettledLoc, TableCamera->GetComponentLocation()));
			MaxSwayDeg = FMath::Max(MaxSwayDeg, FMath::RadiansToDegrees(SettledRot.AngularDistance(TableCamera->GetComponentQuat())));
		}
		AddInfo(FString::Printf(TEXT("特写待机 1 秒手持晃动幅度：位移 %.2f cm，转角 %.2f°"), MaxSwayLoc, MaxSwayDeg));
		TestTrue(TEXT("待机存在轻微克制的手持相机晃动"),
			MaxSwayLoc > 0.04 && MaxSwayLoc < 2.5 && MaxSwayDeg > 0.04 && MaxSwayDeg < 1.8);
	}
	TestTrue(TEXT("桌面中心落在画面左侧"), ScreenX(Bench->GetInteractionLocation()) > 0.22 && ScreenX(Bench->GetInteractionLocation()) < 0.42);
	auto CheckRest = [&](const UGinkgoWeaponDefinition& Asset)
	{
		const FTransform& Rest = Bench->GetRestPose();
		const FVector BothHands = Rest.TransformVectorNoScale(Asset.LoweredHands.Right.GetLocation() - Asset.LoweredHands.Left.GetLocation());
		TestTrue(TEXT("两只手的握点顺序正确"), FVector::DotProduct(BothHands, Character->GetActorRightVector()) >= 0);
		const FBox Box = Asset.Mesh->GetBoundingBox();
		const FVector TableCenter = Bench->GetInteractionLocation();
		const FVector TowardPlayer = (Character->GetActorLocation() - TableCenter).GetSafeNormal2D();
		const FVector GripPosition = Rest.GetLocation();
		const FVector Bore = Rest.TransformPosition(Asset.MuzzleOffset);
		TestTrue(*FString::Printf(TEXT("%s grip is on the player's side of the barrel"), *Asset.GetName()),
			FVector::DotProduct(GripPosition - Bore, TowardPlayer) > 0.1);
		if (const FGinkgoScope* Scope = UGinkgoDeveloperSettings::Get()->FindScope(Asset.AdsReticle))
		{
			const FVector Eyepiece = Rest.TransformPosition(Scope->Mount.EyepieceCenter);
			TestTrue(TEXT("从角色看瞄具位于枪管远侧"),
				FVector::DotProduct(Eyepiece - Bore, TowardPlayer) < -0.1);
		}
		const FBox PlacedBounds = Box.TransformBy(Rest);
		TestTrue(TEXT("网格下表面贴在桌面"), FMath::IsNearlyEqual(PlacedBounds.Min.Z, TableCenter.Z, 0.01));
		TestTrue(TEXT("网格中心落在放枪位置上方"), PlacedBounds.GetCenter().Equals(
			TableCenter + FVector::UpVector * PlacedBounds.GetExtent().Z, 0.01));
		double Left = 1.0, Right = 0.0;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Vertex((Corner & 1) ? Box.Max.X : Box.Min.X, (Corner & 2) ? Box.Max.Y : Box.Min.Y, (Corner & 4) ? Box.Max.Z : Box.Min.Z);
			const double X = ScreenX(Rest.TransformPosition(Vertex));
			Left = FMath::Min(Left, X);
			Right = FMath::Max(Right, X);
		}
		AddInfo(FString::Printf(TEXT("%s 的画面横向范围 %.3f 至 %.3f"), *Asset.GetName(), Left, Right));
		TestTrue(TEXT("整把枪入镜并避开右侧菜单"), Left > 0.005 && Right < 0.72);
	};
	CheckRest(*Weapon);
	TestEqual(TEXT("只有一个槽有枪时没有相邻选项"), Bench->GetAdjacentSlot(true), INDEX_NONE);
	const int32 Sidearm = Ginkgo::Weapon::SidearmSlot;
	auto* Pistol = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911"));
	if (!TestNotNull(TEXT("手枪定义"), Pistol)) { return false; }
	Weapons->SetCarried(Sidearm, Pistol);
	const auto PistolState = *Weapons->GetMechanismState(Sidearm);
	const auto RifleState = *Weapons->GetMechanismState(0);
	Interactor->SelectWorkbenchSlot(42);
	TestEqual(TEXT("拒绝无效槽号"), Bench->GetSelectedSlot(), 0);
	auto Press = [&](const FKey& Key, EInputEvent Event = IE_Pressed)
	{
		FInputKeyEventArgs Input{};
		Input.Key = Key;
		Input.Event = Event;
		return static_cast<APlayerController*>(PC)->InputKey(Input);
	};
	auto* Part = LoadObject<UGinkgoWeaponAttachment>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47Suppressor.DA_AK47Suppressor"));
	if (!TestTrue(TEXT("原装 AK 提供武器资产配置的配件和效果"), Part && Bench->GetNextAttachment() == Part
		&& Part->Mesh.LoadSynchronous() && Part->FireSound.LoadSynchronous() && !Part->DisplayName.IsEmpty()
		&& !Part->Description.IsEmpty() && Part->HudIcon == TEXT("AK47Suppressor"))) { return false; }
	const auto HasPart = [&] { return Weapons->GetAttachments(0).Parts.Contains(Part); };
	const auto PartMeshes = [&]
	{
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Character);
		return Meshes.FilterByPredicate([Part](const UStaticMeshComponent* Mesh)
		{
			return Mesh->GetStaticMesh() == Part->Mesh.Get();
		});
	};
	// 选中消音器配件时，特写相机柔和向枪口方向偏移并轻微推近，但不丢失整枪构图；清空焦点后平滑回中。
	{
		const FVector NeutralFocusLoc = TableCamera->GetComponentLocation();
		const double NeutralGunU = ScreenX(Bench->GetInteractionLocation());
		Bench->SetFocusedAttachment(Part);
		Step(75);
		const FVector BiasedLoc = TableCamera->GetComponentLocation();
		const FVector PartOffset = Bench->GetCameraPartFocusOffset();
		const double BiasedGunU = ScreenX(Bench->GetInteractionLocation());
		AddInfo(FString::Printf(TEXT("选中 AK 消音器部件偏置：局部偏移=(%.1f, %.1f, %.1f) cm，机位 ΔY=%.1f cm，枪心 U=%.3f->%.3f"),
			PartOffset.X, PartOffset.Y, PartOffset.Z, BiasedLoc.Y - NeutralFocusLoc.Y, NeutralGunU, BiasedGunU));
		TestTrue(TEXT("选中枪口配件时相机向枪口侧柔和偏移并微推近且仍保留整枪中心"),
			PartOffset.Y < -20.0
			&& BiasedLoc.Y < NeutralFocusLoc.Y - 4.0
			&& FVector::Dist(BiasedLoc, Bench->GetInteractionLocation()) < FVector::Dist(NeutralFocusLoc, Bench->GetInteractionLocation()) - 3.0
			&& BiasedGunU > NeutralGunU + 0.02 && BiasedGunU < 0.56);
		Bench->SetFocusedAttachment(nullptr);
		Step(75);
		TestTrue(TEXT("取消选中配件后镜头偏移平滑归零"), Bench->GetCameraPartFocusOffset().Size() < 1.5);
	}
	const int32 SeatBefore = Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagIn);
	const int32 TightenBefore = Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_SlideHome);
	Press(EKeys::Gamepad_FaceButton_Bottom);
	Step(2);  // 输入先交给交互组件，动作执行器在随后的模拟步进入 OnStart
	auto Meshes = PartMeshes();
	if (!TestEqual(TEXT("起手时只有一个临时配件"), Meshes.Num(), 1)) { return false; }
	TestFalse(TEXT("动作首帧不显示配件"), Meshes[0]->IsVisible());
	Step(26);
	TestTrue(TEXT("开始安装配件时相机拉远并露出角色上半身"),
		!Bench->IsCameraFocusingWeapon() && Bench->GetCameraFocusAlpha() < 0.25f
		&& TableCamera->FieldOfView > 73.f
		&& IsInBenchViewport(Body->GetSocketLocation(TEXT("spine_05"))));
	TestTrue(TEXT("身体已探到取物位置，模型仍藏在肩背后"), Bench->GetInstallHandPose().BodyLean > 1.f
		&& !Meshes[0]->IsVisible());
	Step(13);
	TestTrue(TEXT("取物后才显示临时模型"), Meshes[0]->IsVisible());
	TestTrue(TEXT("安装期间锁定选枪且左手跟随配件"),
		Bench->GetInstallingPart() == Part && !Bench->CanSelectWeapon() && Bench->GetInstallHandPose().LeftGrip > 0.98f);
	const auto WristDistance = FVector::Distance(Body->GetSocketLocation(TEXT("hand_l")), Bench->GetInstallHandPose().WristLocation);
	AddInfo(FString::Printf(TEXT("安装时左手腕距抓取目标 %.1f cm"), WristDistance));
	TestTrue(TEXT("左手腕到达配件的抓取目标"), WristDistance < 9.0);
	TestEqual(TEXT("安装动作只创建一个临时网格"), PartMeshes().Num(), 1);
	TestFalse(TEXT("临时网格移动时配件尚未生效"), HasPart());
	Interactor->SelectWorkbenchSlot(Sidearm);
	TestEqual(TEXT("安装期间请求换枪保持原槽"), Bench->GetSelectedSlot(), 0);
	Interactor->RequestInteract();
	Step(108);
	TestFalse(TEXT("安装完成前离桌不写入配件"), HasPart());
	TestFalse(TEXT("中途离桌收枪后关闭台灯"), Bench->IsLampOn());
	TestEqual(TEXT("携带途中离桌不会补播入位声"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagIn), SeatBefore);
	TestEqual(TEXT("提前取消不会补播紧固声"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_SlideHome), TightenBefore);
	TestEqual(TEXT("取消后临时网格已清理"), PartMeshes().Num(), 0);
	if (!Enter()) { return false; }
	Step(108);
	Press(EKeys::F);
	Step(8);
	const auto Reach = Bench->GetInstallHandPose();
	TestTrue(TEXT("起手时右手先落稳"), Reach.RightGrip > Reach.LeftGrip);
	Step(60);
	const auto Carry = Bench->GetInstallHandPose();
	TestTrue(TEXT("提起后左手离开桌面并带动腕部转向"), Carry.LeftGrip > .98f
		&& Carry.WristLocation.Z > Reach.WristLocation.Z + 6.0
		&& Carry.WristRotationDelta.AngularDistance(Reach.WristRotationDelta) > FMath::DegreesToRadians(5.0));
	const auto CarryWristDistance = FVector::Distance(Body->GetSocketLocation(TEXT("hand_l")), Carry.WristLocation);
	AddInfo(FString::Printf(TEXT("提起时左手腕距抓取目标 %.1f cm"), CarryWristDistance));
	TestTrue(TEXT("携带时左手腕跟随道具"), CarryWristDistance < 9.0);
	Step(30);
	const auto Contact = Bench->GetInstallHandPose();
	const auto ContactWristDistance = FVector::Distance(Body->GetSocketLocation(TEXT("hand_l")), Contact.WristLocation);
	AddInfo(FString::Printf(TEXT("落位时左手腕距抓取目标 %.1f cm"), ContactWristDistance));
	TestTrue(TEXT("落位前手腕仍跟随道具"), ContactWristDistance < 9.0);
	Meshes = PartMeshes();
	if (!TestEqual(TEXT("落位后仍由动作持有临时配件"), Meshes.Num(), 1)) { return false; }
	const UStaticMeshComponent* Preview = Meshes[0];
	const FQuat BeforeTurn = Preview->GetComponentQuat();
	Step(23);
	const FQuat FirstTurn = Preview->GetComponentQuat();
	TestEqual(TEXT("配件到位后只播放一次接触声"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagIn), SeatBefore + 1);
	TestEqual(TEXT("第一次转腕后仍未触发最后的紧固声"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_SlideHome), TightenBefore);
	const auto WristAtFirstTurn = Bench->GetInstallHandPose().WristRotationDelta;
	TestTrue(TEXT("第一拍配件和手腕一起转"), FirstTurn.AngularDistance(BeforeTurn) > FMath::DegreesToRadians(25.0)
		&& WristAtFirstTurn.AngularDistance(Contact.WristRotationDelta) > FMath::DegreesToRadians(25.0));
	Step(20);
	TestTrue(TEXT("换握时配件停住而手腕回摆"), FirstTurn.Equals(Preview->GetComponentQuat(), .001)
		&& WristAtFirstTurn.AngularDistance(Bench->GetInstallHandPose().WristRotationDelta) > FMath::DegreesToRadians(25.0));
	Step(24);
	const auto* InstalledMount = Weapon->FindAttachment(Part);
	TestTrue(TEXT("第二拍转到正式挂点，配件仍未提前生效"), InstalledMount && !HasPart()
		&& Preview->GetComponentTransform().Equals(InstalledMount->RelativeTransform * Bench->GetRestPose(), .01)
		&& FirstTurn.AngularDistance(Preview->GetComponentQuat()) > FMath::DegreesToRadians(15.0));
	Step(36);
	const auto Release = Bench->GetInstallHandPose();
	TestTrue(TEXT("落位后左手先离开，右手继续支撑"), Release.LeftGrip < Release.RightGrip
		&& Release.WristRotationDelta.Equals(FQuat::Identity, .001) && !HasPart());
	Step(22);
	TestTrue(TEXT("键盘确认将配件装入选中的那把枪"),
		HasPart() && Bench->CanSelectWeapon() && !Bench->CanInstallAttachment(Part));
	Step(65);
	TestTrue(TEXT("安装动作完成后相机重新推近聚焦武器"),
		Bench->IsCameraFocusingWeapon() && Bench->GetCameraFocusAlpha() > 0.85f);
	TestEqual(TEXT("完成后只留下枪上的配件网格"), PartMeshes().Num(), 1);
	TestEqual(TEXT("拧紧到位借用一次复进声"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_SlideHome), TightenBefore + 1);
	Press(EKeys::F);
	TestFalse(TEXT("已安装的配件不能重复启动动作"), Bench->GetInstallingPart() != nullptr);
	TestTrue(TEXT("安装保留弹量和枪机状态"), *Weapons->GetMechanismState(0) == RifleState);
	for (float Crouch : {0.f, 0.5f, 1.f})
	{
		const auto& Pattern = Weapon->PatternRecoil;
		const FVector2D Delta = Pattern.GetScale(0.f, Crouch, 0.f) - Pattern.GetScale(0.f, Crouch, Part->PatternScaleOffset);
		TestTrue(TEXT("姿态混合中模板的两个轴各减 0.2"), Delta.Equals(FVector2D(0.2, 0.2), 0.0001));
	}
	TestTrue(TEXT("桌前消耗手柄 RB 的按下"), Press(EKeys::Gamepad_RightShoulder));
	TestFalse(TEXT("换枪动作链执行时锁定选择"), Bench->CanSelectWeapon());
	Interactor->SelectWorkbenchSlot(0);
	TestEqual(TEXT("重复点击保持原动作链的目标"), Bench->GetSelectedSlot(), Sidearm);
	Step(360);
	TestTrue(TEXT("手枪放到同一张桌上且 AK 已收起"), Bench->CanSelectWeapon() && Weapons->GetHeldSlot() == Sidearm
		&& Character->GetWeaponSlotComponent(Sidearm)->GetAttachParent()->GetOwner() == Bench && Character->IsWeaponStowed(0));
	TestTrue(TEXT("换枪后两把枪各自保留弹量与机构状态"), *Weapons->GetMechanismState(Sidearm) == PistolState && *Weapons->GetMechanismState(0) == RifleState);
	CheckRest(*Pistol);
	TestFalse(TEXT("手枪不能安装 AK 的配件"), Bench->CanInstallAttachment(Part));
	Press(EKeys::Gamepad_RightShoulder, IE_Repeat);
	TestEqual(TEXT("按住 RB 不会连续换枪"), Bench->GetSelectedSlot(), Sidearm);
	Press(EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("RB 循环跳过空槽"), Bench->GetSelectedSlot(), 0);
	Step(360);
	Press(EKeys::Gamepad_LeftShoulder);
	TestEqual(TEXT("LB 可以向前循环"), Bench->GetSelectedSlot(), Sidearm);
	Step(360);
	// 逐一放上项目里的武器，包括没有收枪片段的重武器。
	struct FBenchCase { const TCHAR* Path; int32 Slot; };
	constexpr int32 BoltSlot = 1;
	const FBenchCase Cases[] = {
		{TEXT("/Game/Weapons/Shotgun/DA_Shotgun.DA_Shotgun"), 1},
		{TEXT("/Game/Weapons/Mauser98k/DA_Mauser98k.DA_Mauser98k"), BoltSlot},
		{TEXT("/Game/Weapons/MG3/DA_MG3.DA_MG3"), Ginkgo::Weapon::HeavySlot}
	};
	for (const auto& Example : Cases)
	{
		auto* Asset = LoadObject<UGinkgoWeaponDefinition>(nullptr, Example.Path);
		if (!TestNotNull(Example.Path, Asset)) { return false; }
		const int32 Slot = Example.Slot;
		if (Bench->GetSelectedSlot() == Slot)
		{
			Interactor->SelectWorkbenchSlot(0);
			Step(360);
		}
		Weapons->SetCarried(Slot, Asset);
		Interactor->SelectWorkbenchSlot(Slot);
		Step(360);
		TestTrue(*FString::Printf(TEXT("%s 已放到桌面"), *Asset->GetName()), Bench->CanSelectWeapon()
			&& Character->GetWeaponSlotComponent(Slot)->GetAttachParent()->GetOwner() == Bench);
		CheckRest(*Asset);
	}
	// 先带 MG3 离桌以激活它的动作树，再进桌取走 98k。
	Interactor->RequestInteract();
	Step(108);
	if (!Enter()) { return false; }
	Step(108);
	const auto* BoltRifle = Weapons->GetCarried(BoltSlot);
	if (!TestNotNull(TEXT("与 MG3 一起携带的 98k"), BoltRifle)) { return false; }
	Interactor->SelectWorkbenchSlot(BoltSlot);
	Step(360);
	const auto* Cycle = BoltRifle->Steps.Find(FWeaponTags::Weapon_Step_CycleBolt);
	if (!TestTrue(TEXT("98k 配置了拉栓蒙太奇"), Cycle && Cycle->Montage)) { return false; }
	for (int32 Visit = 0; Visit < 2; ++Visit)
	{
		const auto* BoltState = Weapons->GetMechanismState(BoltSlot);
		const auto BeforeExit = *BoltState;
		Interactor->RequestInteract();
		Step(108);
		if (!TestTrue(TEXT("98k 离桌时保持装填状态且在手上"), !Character->GetWorkbench() && Weapons->GetHeldWeapon() == BoltRifle
			&& Character->IsHeldWeaponInHand() && *BoltState == BeforeExit && BoltState->CanFire())) { return false; }
		Weapons->SetWantsToAim(true);
		Step(45);
		Weapons->HandleWeaponKeys(EGinkgoWeaponKey::Fire);
		bool bFired = false, bCycled = false;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			Step(1);
			bFired |= BoltState->Chamber == EGinkgoChamber::Spent;
			bCycled |= Body->GetAnimInstance()->Montage_IsPlaying(Cycle->Montage);
		}
		TestTrue(TEXT("98k 成功打出离桌时膛里的子弹"), bFired);
		TestTrue(TEXT("98k 开火以后播放拉栓动作"), bCycled);
		TestTrue(TEXT("98k 只把下一发子弹送入膛一次"), BoltState->CanFire() && BoltState->Feed.Num() == BeforeExit.Feed.Num() - 1);
		Weapons->SetWantsToAim(false);
		if (!Enter()) { return false; }
		Step(108);
	}
	Interactor->SelectWorkbenchSlot(0);
	Step(360);
	TestTrue(TEXT("可以再次选中步枪且手枪回到枪套"), Bench->CanSelectWeapon() && Character->IsWeaponStowed(Sidearm) && Weapons->GetHeldSlot() == 0);
	Interactor->RequestInteract();
	Step(56);
	TestTrue(TEXT("离桌时先取回武器到手中，此时台灯尚未关闭"),
		Bench->GetGripAlpha() == 1.f && Gun->GetAttachSocketName() == Grip && Character->IsHeldWeaponInHand() && Bench->IsLampOn());
	Step(18);
	TestTrue(TEXT("取回武器后拨关台灯"), !Bench->IsLampOn());
	Step(24);
	TestNull(TEXT("退出完成后释放角色"), Character->GetWorkbench());
	TestTrue(TEXT("退出完成后台灯熄灭"), !Bench->IsLampOn() && Bench->GetLampAlpha() == 0.f && !LampSpot->IsVisible());
	TestTrue(TEXT("离桌仍是同一槽的枪、机构和网格"), Weapons->GetCarried(0) == Weapon && Weapons->GetMechanismState(0) == State
		&& Character->GetHeldWeaponMesh() == Gun && Character->IsHeldWeaponInHand());
	TestTrue(TEXT("离桌恢复玩家镜头与移动输入"), PC->GetViewTarget() == Character && !PC->IsLookInputIgnored()
		&& Character->GetCharacterMovement()->IsMovingOnGround());
	if (!Enter()) { return false; }
	Step(108);
	Interactor->SelectWorkbenchSlot(Sidearm);
	Step(30);
	Interactor->RequestInteract();
	Step(108);
	TestTrue(TEXT("换枪中离桌取回正在拿起的枪并恢复输入与熄灯"),
		!Character->GetWorkbench() && !Bench->IsLampOn() && Weapons->GetHeldSlot() == 0 && Character->IsHeldWeaponInHand()
		&& Character->IsWeaponStowed(Sidearm) && !PC->IsLookInputIgnored());
	Character->AttachWeaponToStow(0);
	Weapons->SetHeldSlot(INDEX_NONE);
	Step(1);
	if (!Enter()) { return false; }
	TestTrue(TEXT("徒手进桌先打开台灯再允许选枪"), !Bench->CanSelectWeapon() && !Bench->IsLampOn());
	Step(36);
	TestTrue(TEXT("徒手进桌开灯完成后在空桌打开武器选择"),
		Bench->IsLampOn() && Bench->CanSelectWeapon() && Bench->GetSelectedSlot() == INDEX_NONE);
	TestFalse(TEXT("空桌未放枪时保持拉远视角不特写空桌面"), Bench->IsCameraFocusingWeapon());
	TestEqual(TEXT("空桌可以向前选到最后一个有枪的槽"), Bench->GetAdjacentSlot(false), Ginkgo::Weapon::HeavySlot);
	Press(EKeys::Right);
	Step(240);
	TestTrue(TEXT("从收起状态选中的步枪放到桌面"), Bench->CanSelectWeapon() && Gun->GetAttachParent()->GetOwner() == Bench);
	TestTrue(TEXT("空桌选枪放稳后进入特写聚焦态"), Bench->IsCameraFocusingWeapon() && Bench->GetCameraFocusAlpha() > 0.85f);
	Bench->Destroy();
	TestNull(TEXT("销毁占用中的桌子释放角色"), Character->GetWorkbench());
	TestTrue(TEXT("销毁桌子后枪网格回到原握点"), Gun->GetAttachSocketName() == Grip && PC->GetViewTarget() == Character);
	Weapons->DropWeapon(0);
	TestFalse(TEXT("丢枪后清空槽里的配件"), HasPart());
	TActorIterator<AGinkgoWeaponPickup> Ground(World);
	if (!TestTrue(TEXT("丢下武器生成地面拾取物"), bool(Ground))) { return false; }
	TInlineComponentArray<UStaticMeshComponent*> GroundMeshes(*Ground);
	TestTrue(TEXT("地上的枪显示已装配件的模型"), GroundMeshes.ContainsByPredicate([Part](const UStaticMeshComponent* Mesh)
	{
		return Mesh->GetStaticMesh() == Part->Mesh.Get();
	}));
	Ground->Interact({Character, Interactor});
	TestTrue(TEXT("捡回的枪保留已装配件"), HasPart());
	// 两个槽装同型号的枪，用临时定义验证新增挂点，不改项目里的武器资产。
	auto* TestWeapon = DuplicateObject<UGinkgoWeaponDefinition>(Weapon, GetTransientPackage());
	auto* GripPart = NewObject<UGinkgoWeaponAttachment>();
	auto* Alternative = NewObject<UGinkgoWeaponAttachment>();
	for (const auto& [Extra, Mount] : {TPair<UGinkgoWeaponAttachment*, FName>(GripPart, TEXT("grip")),
		TPair<UGinkgoWeaponAttachment*, FName>(Alternative, TEXT("muzzle"))})
	{
		FGinkgoWeaponAttachmentMount& Entry = TestWeapon->Attachments.AddDefaulted_GetRef();
		Entry.Part = Extra;
		Entry.Mount = Mount;
	}
	Weapons->SetCarried(0, TestWeapon);
	Weapons->SetCarried(1, TestWeapon);
	Weapons->InstallAttachment(0, GripPart);
	Weapons->InstallAttachment(0, Part);
	Weapons->InstallAttachment(0, Alternative);
	TestTrue(TEXT("不同挂点共存，同一挂点只占一次"),
		Weapons->GetAttachments(0).Parts.Num() == 2 && Weapons->GetAttachments(0).Parts.Contains(GripPart) && HasPart());
	TestTrue(TEXT("另一把同型号的枪有独立的配件状态"), Weapons->GetAttachments(1).Parts.IsEmpty());
	TestFalse(TEXT("手枪拒绝不兼容的配件"), Weapons->CanInstallAttachment(Sidearm, GripPart));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWorkbenchMenuTest, "CaseGinkgo.Interaction.WorkbenchMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWorkbenchMenuTest::RunTest(const FString& Parameters)
{
	auto* Weft = UWeftSubsystem::Get();
	if (!TestTrue(TEXT("输入测试可使用 Weft"), Weft && Weft->IsAvailable())) { return false; }
	TStrongObjectPtr<UWeftView> View(Weft->CreateView());
	if (!TestNotNull(TEXT("交互页面视图"), View.Get())) { return false; }
	View->Resize(FVector2D(1000, 620), 1.f);
	Weft->AddEffects(FPaths::ProjectContentDir() / TEXT("UI/Effects/effects.json"));
	if (!TestTrue(TEXT("实际交互页面成功载入"), Ginkgo::UI::LoadPageWithKeycaps(*View, FPaths::ProjectContentDir() / TEXT("UI/Interaction")))) { return false; }
	TArray<FString> Picks;
	bool bExited = false;
	TArray<FString> InstallRequests;
	TArray<FString> FocusRequests;
	TArray<FString> MenuSounds;
	View->OnMessage.AddLambda([&](const FString& Name, const FString& Json, uint64)
	{
		if (Name == TEXT("benchSelect")) { Picks.Add(Json); }
		bExited |= Name == TEXT("benchLeave");
		if (Name == TEXT("benchInstall")) { InstallRequests.Add(Json); }
		if (Name == TEXT("benchFocus")) { FocusRequests.Add(Json); }
		if (Name == TEXT("sound")) { MenuSounds.Add(Json.TrimQuotes()); }
	});
	View->Emit(TEXT("interaction"), TEXT(R"({"items":[],"key":{"key":"Gamepad_FaceButton_Left","label":"X"},"bench":{"ready":true,"gamepad":true,"selected":2,"previous":0,"next":3,"weapons":[{"slot":0,"name":"Karabiner 98 kurz 7.92mm"},{"slot":2,"name":"M1911"},{"slot":3,"name":"MG3"}]}})"));
	View->Advance();
	FString Cursor;
	TestFalse(TEXT("右侧交互栏不占用左侧武器区域"), View->HitTest(FVector2D(340, 300), Cursor));
	auto Click = [&](FVector2D Point)
	{
		View->Pointer(EWeftPointer::Down, Point, 1);
		View->Pointer(EWeftPointer::Up, Point, 0);
		View->Advance();
	};
	const FVector2D PreviousButton(735, 107), NextButton(963, 107);
	Click(PreviousButton);
	Click(NextButton);
	TestTrue(TEXT("上一把和下一把按钮与标题在同一行"), Picks == TArray<FString>{TEXT("0"), TEXT("3")});
	View->Key(true, TEXT("ArrowLeft"), TEXT("ArrowLeft"));
	View->Key(false, TEXT("ArrowLeft"), TEXT("ArrowLeft"));
	View->Advance();
	TestTrue(TEXT("焦点在页面时左方向键也能选枪"), Picks.Num() == 3 && Picks.Last() == TEXT("0"));
	View->Emit(TEXT("interaction"), TEXT(R"({"items":[],"key":{"key":"E","label":"E"},"bench":{"ready":false,"gamepad":false,"selected":0,"previous":3,"next":2,"weapons":[{"slot":0,"name":"Автомат Калашникова - 1947 7.62mm"},{"slot":2,"name":"M1911"},{"slot":3,"name":"MG3"}]}})"));
	View->Advance();
	const int32 Before = Picks.Num();
	Click(NextButton);
	TestEqual(TEXT("放枪期间点击不会切换"), Picks.Num(), Before);
	// 长标题折行以后，退出按钮跟着往下挪。
	for (int32 Y = 170; Y < 246 && !bExited; Y += 5) { Click(FVector2D(950, Y)); }
	TestTrue(TEXT("离桌按钮发出退出请求"), bExited);
	auto ShowPart = [&](bool bInstalled)
	{
		const FString Item = bInstalled ? TEXT(R"({"index":1,"name":"配件","description":"测试","effects":[{"text":"+后坐力控制","tone":"Positive"}],"icon":"AK47Suppressor","installed":true,"installing":false,"available":false})")
			: TEXT(R"({"index":1,"name":"配件","description":"测试","effects":[{"text":"+后坐力控制","tone":"Positive"}],"icon":"AK47Suppressor","installed":false,"installing":false,"available":true})");
		View->Emit(TEXT("interaction"), FString::Printf(TEXT(R"({"items":[],"key":{"key":"F","label":"F"},"bench":{"ready":true,"gamepad":false,"selected":0,"previous":-1,"next":-1,"weapons":[{"slot":0,"name":"AK47"}],"installKey":"F","parts":[%s]}})"), *Item));
		View->Advance();
	};
	ShowPart(false);
	Click(FVector2D(838, 194));
	TestTrue(TEXT("阅读配件卡片不会触发安装"), InstallRequests.IsEmpty());
	// 安装按钮在简介和效果之后，随文字换行下移；扫描其所在的左侧列，避免把文案高度写成固定布局。
	FVector2D InstallButton = FVector2D::ZeroVector;
	for (int32 Y = 230; Y < 450 && InstallRequests.IsEmpty(); Y += 4)
	{
		InstallButton = FVector2D(740, Y);
		Click(InstallButton);
	}
	TestTrue(TEXT("点击传回绘制时的武器槽和配件下标"), InstallRequests == TArray<FString>{TEXT(R"({"slot":0,"index":1})")});
	ShowPart(true);
	Click(InstallButton);
	TestEqual(TEXT("不能再次安装已装配件"), InstallRequests.Num(), 1);
	MenuSounds.Reset();
	View->Emit(TEXT("benchNavigate"), TEXT("\"install\""));
	View->Advance();
	TestEqual(TEXT("按一次安装快捷键只发一次无效反馈"), FString::Join(MenuSounds, TEXT(",")), FString(TEXT("Invalid")));
	MenuSounds.Reset();
	// 模块枪在页面里保留插槽/配件焦点；原生 F / A 与页面键盘走同一选择，不会误装其他插槽第一项。
	View->Emit(TEXT("interaction"), TEXT(R"({"items":[],"key":{"key":"E","label":"E"},"bench":{"ready":true,"selected":1,"previous":-1,"next":-1,"weapons":[{"slot":1,"name":"M4"}],"installKey":"F","parts":[{"index":0,"mount":"stock","name":"标准枪托","installed":true,"available":false},{"index":1,"mount":"stock","name":"轻型枪托","available":true},{"index":2,"mount":"muzzle","name":"制退器","available":true},{"index":3,"mount":"muzzle","name":"消音器","available":true}]}})"));
	View->Emit(TEXT("benchNavigate"), TEXT("\"nextMount\""));
	View->Emit(TEXT("benchNavigate"), TEXT("\"nextPart\""));
	View->Emit(TEXT("benchNavigate"), TEXT("\"install\""));
	View->Advance();
	TestEqual(TEXT("手柄可选择枪口插槽的第二个模块"), InstallRequests.Last(), FString(TEXT(R"({"slot":1,"index":3})")));
	TestEqual(TEXT("切部位与选配件同步上报相机焦点"), FocusRequests.Last(), FString(TEXT(R"({"slot":1,"index":3})")));
	TestTrue(TEXT("切分类和换配件各触发对应菜单音，页面刷新和安装请求不重复确认"), MenuSounds == TArray<FString>{TEXT("Select"), TEXT("Focus")});
	View->Emit(TEXT("benchNavigate"), TEXT("\"previousMount\""));
	View->Advance();
	View->Key(true, TEXT("KeyF"), TEXT("f"));
	View->Key(false, TEXT("KeyF"), TEXT("f"));
	View->Advance();
	TestEqual(TEXT("页面快捷键安装当前枪托选项"), InstallRequests.Last(), FString(TEXT(R"({"slot":1,"index":1})")));
	TestEqual(TEXT("切回枪托分类同步更新相机焦点"), FocusRequests.Last(), FString(TEXT(R"({"slot":1,"index":1})")));
	// 真正经过指针命中与 click 分发，不能只发导航事件绕过 CSS 的 pointer-events。
	const FVector2D MuzzleTab(910, 194);
	FString TabCursor;
	TestTrue(TEXT("配件分类具有鼠标命中区域"), View->HitTest(MuzzleTab, TabCursor));
	TestEqual(TEXT("配件分类显示可点击指针"), TabCursor, FString(TEXT("pointer")));
	Click(MuzzleTab);
	const int32 SoundsAfterTab = MenuSounds.Num();
	Click(MuzzleTab);
	TestEqual(TEXT("重复点击同一分类不重播声音"), MenuSounds.Num(), SoundsAfterTab);
	View->Key(true, TEXT("KeyF"), TEXT("f"));
	View->Key(false, TEXT("KeyF"), TEXT("f"));
	View->Advance();
	TestEqual(TEXT("鼠标点击枪口分类后快捷键对应枪口配件"), InstallRequests.Last(), FString(TEXT(R"({"slot":1,"index":2})")));
	TestEqual(TEXT("鼠标点击枪口分类同步上报相机焦点"), FocusRequests.Last(), FString(TEXT(R"({"slot":1,"index":2})")));
	View->Emit(TEXT("interaction"), TEXT(R"({"items":[]})"));
	View->Advance();
	TestEqual(TEXT("关闭菜单时清除相机部件焦点"), FocusRequests.Last(), FString(TEXT(R"({"slot":-1,"index":-1})")));
	TestFalse(TEXT("关闭菜单释放右侧指针区域"), View->HitTest(NextButton, Cursor));
	View->OnMessage.Clear();
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWorkbenchDarkReadabilityTest, "CaseGinkgo.Interaction.WorkbenchDarkReadability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWorkbenchDarkReadabilityTest::RunTest(const FString& Parameters)
{
	if (!FApp::CanEverRender())
	{
		AddInfo(TEXT("NullRHI 环境下跳过像素照度采样"));
		return true;
	}
	const FString Name = TEXT("BenchDark") + FGuid::NewGuid().ToString();
	auto* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		Instance->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		Instance->RemoveFromRoot();
	};

	auto* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	auto AddRoomSlab = [&](const FVector Location, const FVector Scale)
	{
		AActor* Wall = World->SpawnActor<AActor>(Location, FRotator::ZeroRotator);
		auto* Part = NewObject<UStaticMeshComponent>(Wall);
		Wall->SetRootComponent(Part);
		Part->SetStaticMesh(CubeMesh);
		Part->SetWorldScale3D(Scale);
		Part->RegisterComponent();
		Part->SetWorldLocation(Location);
	};
	// 构建无外部光源的封闭暗室地面与墙顶，测量台灯单独点亮前后桌面枪械的实际成像可读性。
	AddRoomSlab(FVector(0.0, 0.0, -10.0), FVector(9.0, 9.0, 0.2));
	AddRoomSlab(FVector(0.0, 0.0, 340.0), FVector(9.0, 9.0, 0.2));
	AddRoomSlab(FVector(240.0, 0.0, 165.0), FVector(0.2, 9.0, 3.5));
	AddRoomSlab(FVector(-340.0, 0.0, 165.0), FVector(0.2, 9.0, 3.5));
	AddRoomSlab(FVector(0.0, -340.0, 165.0), FVector(9.0, 0.2, 3.5));
	AddRoomSlab(FVector(0.0, 340.0, 165.0), FVector(9.0, 0.2, 3.5));

	auto* PC = World->SpawnActor<AGinkgoPlayerController>();
	auto* Character = World->SpawnActor<AGinkgoCharacter>(FVector(-110.0, 0.0, 92.0), FRotator::ZeroRotator);
	auto* Bench = World->SpawnActor<AGinkgoWorkbench>();
	auto* AK = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
	auto* M4 = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
	if (!TestTrue(TEXT("暗室测试角色与武器"), PC && Character && Bench && AK && M4)) { return false; }

	PC->SetAsLocalPlayerController();
	PC->Possess(Character);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Character->DispatchBeginPlay();
	Bench->DispatchBeginPlay();
	auto* Weapons = Character->GetWeapons();
	ON_SCOPE_EXIT
	{
		Weapons->StopActionStates();
		Character->Destroy();
	};
	for (int32 Slot = 0; Slot < Weapons->SlotCount; ++Slot) { Weapons->SetCarried(Slot, nullptr); }
	Weapons->SetCarried(0, AK);
	Weapons->SetCarried(1, M4);
	Weapons->SetHeldSlot(0);
	Character->AttachWeaponToHand(0);
	Character->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Character->GetMesh()->bEnableUpdateRateOptimizations = false;

	constexpr int32 Width = 1280;
	constexpr int32 Height = 720;
	auto* Target = NewObject<UTextureRenderTarget2D>(Bench);
	Target->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
	Target->UpdateResourceImmediate(true);

	auto* TableCamera = Bench->FindComponentByClass<UCameraComponent>();
	auto* Capture = NewObject<USceneCaptureComponent2D>(Bench);
	Capture->TextureTarget = Target;
	Capture->FOVAngle = TableCamera->FieldOfView;
	Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
	Capture->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
	Capture->PostProcessSettings.AutoExposureMinBrightness = 1.f;
	Capture->PostProcessSettings.AutoExposureMaxBrightness = 1.f;
	Capture->RegisterComponent();
	Capture->SetWorldTransform(TableCamera->GetComponentTransform());

	const bool bSaveImages = FParse::Param(FCommandLine::Get(), TEXT("BenchLampPreview"));
	const FString OutDir = FPaths::ProjectSavedDir() / TEXT("WorkbenchPreview");
	if (bSaveImages)
	{
		IFileManager::Get().MakeDirectory(*OutDir, true);
	}

	auto CaptureFrame = [&](const FTransform& ViewTransform, const float FovDeg, const FString& FileTag, TArray<FColor>& OutPixels)
	{
		Capture->FOVAngle = FovDeg;
		Capture->SetWorldTransform(ViewTransform);
		World->SendAllEndOfFrameUpdates();
		UMaterialInterface::SubmitRemainingJobsForWorld(World, EMaterialShaderPrecompileMode::Synchronous);
		FAssetCompilingManager::Get().FinishAllCompilation();
		IStreamingManager::Get().StreamAllResources(5.f);
		World->SendAllEndOfFrameUpdates();
		Capture->CaptureScene();
		FlushRenderingCommands();
		Target->GameThread_GetRenderTargetResource()->ReadPixels(OutPixels);
		for (FColor& Pixel : OutPixels) { Pixel.A = 255; }
		if (bSaveImages && !FileTag.IsEmpty())
		{
			FImageUtils::SaveImageByExtension(*(OutDir / (FileTag + TEXT(".png"))), FImageView(OutPixels.GetData(), Width, Height));
		}
	};

	auto MeasureWeaponBox = [](const TArray<FColor>& Pixels)
	{
		// 统计桌前相机下枪身所在画面区域（横向 16%–64%，纵向 42%–62%）的平均亮度与局部标准差。
		double Sum = 0.0;
		double SumSq = 0.0;
		int32 Count = 0;
		for (int32 Y = FMath::RoundToInt(Height * 0.42); Y < FMath::RoundToInt(Height * 0.62); ++Y)
		{
			for (int32 X = FMath::RoundToInt(Width * 0.16); X < FMath::RoundToInt(Width * 0.64); ++X)
			{
				const FColor& C = Pixels[Y * Width + X];
				const double Luma = 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B;
				Sum += Luma;
				SumSq += Luma * Luma;
				++Count;
			}
		}
		const double Mean = Count > 0 ? Sum / Count : 0.0;
		const double StdDev = Count > 0 ? FMath::Sqrt(FMath::Max(0.0, SumSq / Count - Mean * Mean)) : 0.0;
		return TPair<double, double>(Mean, StdDev);
	};

	auto Step = [&](const int32 Frames)
	{
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			constexpr float Dt = 1.f / 60.f;
			World->DeltaTimeSeconds = Dt;
			World->TimeSeconds += Dt;
			if (Bench->IsActorTickEnabled()) { Bench->Tick(Dt); }
			Weapons->UpdateHoldState(Dt);
			Character->GetAnimActions()->TickComponent(Dt, LEVELTICK_All, nullptr);
			Character->GetMesh()->TickAnimation(Dt, false);
			Character->GetMesh()->GetAnimInstance()->NativeThreadSafeUpdateAnimation(Dt);
			Character->GetMesh()->RefreshBoneTransforms(nullptr);
			Character->GetMesh()->UpdateChildTransforms();
			Character->GetMesh()->MarkRenderDynamicDataDirty();
		}
	};

	Step(2);

	TArray<FColor> DarkPixels;
	CaptureFrame(TableCamera->GetComponentTransform(), TableCamera->FieldOfView, TEXT("DarkRoom_BeforeInteract"), DarkPixels);
	const auto [MeanOff, StdOff] = MeasureWeaponBox(DarkPixels);

	Bench->Interact({Character, Character->GetInteractor(), 0});
	Step(14);
	TArray<FColor> ReachOnPixels;
	if (bSaveImages)
	{
		CaptureFrame(TableCamera->GetComponentTransform(), TableCamera->FieldOfView, TEXT("DarkRoom_EnterReachLamp"), ReachOnPixels);
	}
	Step(165);
	if (!TestTrue(TEXT("暗室完成开灯与放枪"), Bench->IsLampOn() && Bench->GetLampAlpha() == 1.f && Bench->CanSelectWeapon())) { return false; }

	TArray<FColor> LitAkPixels;
	CaptureFrame(TableCamera->GetComponentTransform(), TableCamera->FieldOfView, TEXT("DarkRoom_AK47_LampOn"), LitAkPixels);
	const auto [MeanOnAk, StdOnAk] = MeasureWeaponBox(LitAkPixels);

	// 对照如果台灯关闭时同一把桌上武器的暗室亮度：临时把聚光灯和补光设为不可见采样一帧再恢复。
	TInlineComponentArray<ULightComponent*> BenchLights(Bench);
	for (ULightComponent* Light : BenchLights) { Light->SetVisibility(false); }
	TArray<FColor> UnlitAkPixels;
	CaptureFrame(TableCamera->GetComponentTransform(), TableCamera->FieldOfView, TEXT("DarkRoom_AK47_LampForcedOff"), UnlitAkPixels);
	const auto [MeanForcedOff, StdForcedOff] = MeasureWeaponBox(UnlitAkPixels);
	for (ULightComponent* Light : BenchLights) { Light->SetVisibility(true); }

	AddInfo(FString::Printf(TEXT("暗室 AK47 枪区亮度：熄灯均值 %.2f（对比度 %.2f）-> 开灯均值 %.2f（对比度 %.2f）"),
		MeanForcedOff, StdForcedOff, MeanOnAk, StdOnAk));
	TestTrue(TEXT("暗室开灯显著提升桌上武器亮度与轮廓对比度且不过曝"),
		MeanOnAk > MeanForcedOff + 18.0 && StdOnAk > StdForcedOff + 8.0 && MeanOnAk < 215.0);

	// 切换到深黑色 M4 模块步枪，验证黑色机匣同样在暗室可读，并在预览模式下输出日间与第三人称全景图。
	Bench->SelectWeapon(*Character, 1);
	Step(300);
	if (TestTrue(TEXT("暗室切换 M4 放到桌面"), Bench->CanSelectWeapon() && Bench->GetSelectedSlot() == 1))
	{
		TArray<FColor> LitM4Pixels;
		CaptureFrame(TableCamera->GetComponentTransform(), TableCamera->FieldOfView, TEXT("DarkRoom_M4_LampOn"), LitM4Pixels);
		const auto [MeanOnM4, StdOnM4] = MeasureWeaponBox(LitM4Pixels);
		AddInfo(FString::Printf(TEXT("暗室 M4 枪区亮度：开灯均值 %.2f（对比度 %.2f）"), MeanOnM4, StdOnM4));
		TestTrue(TEXT("深色 M4 在暗室台灯下同样清晰可读"), MeanOnM4 > MeanOff + 14.0 && StdOnM4 > 8.0);
	}

	if (bSaveImages)
	{
		const FVector SideEye(-185.0, -165.0, 168.0);
		const FTransform OverviewTransform((FVector(5.0, 0.0, 95.0) - SideEye).Rotation(), SideEye);
		TArray<FColor> SideDark;
		CaptureFrame(OverviewTransform, 76.f, TEXT("DarkRoom_Overview_LampOn"), SideDark);

		Bench->RequestExit(*Character);
		Step(68);
		TArray<FColor> ExitReach;
		CaptureFrame(TableCamera->GetComponentTransform(), TableCamera->FieldOfView, TEXT("DarkRoom_ExitTurnOffLamp"), ExitReach);
		CaptureFrame(OverviewTransform, 76.f, TEXT("DarkRoom_Overview_ExitTurnOffLamp"), ExitReach);
		Step(40);

		// 加柔和环境光输出一张亮室全景，直观检查正式改装桌大尺寸比例与材质。
		auto* Key = NewObject<UDirectionalLightComponent>(Bench);
		Key->SetIntensity(3.2f);
		Key->RegisterComponent();
		Key->SetWorldRotation(FRotator(-38.0, 35.0, 0.0));
		auto* Fill = NewObject<UDirectionalLightComponent>(Bench);
		Fill->SetIntensity(1.1f);
		Fill->SetCastShadows(false);
		Fill->RegisterComponent();
		Fill->SetWorldRotation(FRotator(-25.0, -135.0, 0.0));
		Bench->Interact({Character, Character->GetInteractor(), 1});
		Step(15);
		TArray<FColor> LitOverview;
		CaptureFrame(OverviewTransform, 76.f, TEXT("LitRoom_Overview_ReachLamp"), LitOverview);
		CaptureFrame(TableCamera->GetComponentTransform(), TableCamera->FieldOfView, TEXT("LitRoom_BenchCamera_Action"), LitOverview);
		Step(165);
		CaptureFrame(OverviewTransform, 76.f, TEXT("LitRoom_Overview_LampOn"), LitOverview);
		CaptureFrame(TableCamera->GetComponentTransform(), TableCamera->FieldOfView, TEXT("LitRoom_BenchCamera_M4"), LitOverview);
	}
	return true;
}
#endif

#endif

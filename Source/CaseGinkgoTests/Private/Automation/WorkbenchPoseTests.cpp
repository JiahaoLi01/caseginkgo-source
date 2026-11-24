#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Interaction/GinkgoWorkbench.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "ContentStreaming.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoWorkbenchPoseTest, "CaseGinkgo.Interaction.WorkbenchPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoWorkbenchPoseTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* WeaponName : {TEXT("AK47"), TEXT("M4")})
	{
		const FString Name = TEXT("BenchPose") + FGuid::NewGuid().ToString();
		auto* Instance = NewObject<UGameInstance>(GEngine);
		Instance->AddToRoot();
		Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
		UWorld* World = Instance->GetWorld();
		World->InitializeActorsForPlay(FURL());
		ON_SCOPE_EXIT
		{
			Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); Instance->RemoveFromRoot();
		};
		auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, *FString::Printf(TEXT("/Game/Weapons/%s/DA_%s"), WeaponName, WeaponName));
		if (!TestNotNull(TEXT("武器定义"), Weapon)) { return false; }
		auto* Character = World->SpawnActor<AGinkgoCharacter>(FVector(-110, 0, 92), FRotator::ZeroRotator);
		auto* Controller = World->SpawnActor<AGinkgoPlayerController>();
		auto* Bench = World->SpawnActor<AGinkgoWorkbench>();
		Controller->SetAsLocalPlayerController();
		Controller->Possess(Character);
		Character->GetGinkgoMovement()->SetMovementMode(MOVE_Walking);
		Character->DispatchBeginPlay();
		Bench->DispatchBeginPlay();
		auto* Weapons = Character->GetWeapons();
		ON_SCOPE_EXIT { Weapons->StopActionStates(); Character->Destroy(); };
		for (int32 Slot = 0; Slot < Weapons->SlotCount; ++Slot) { Weapons->SetCarried(Slot, nullptr); }
		Weapons->SetCarried(0, Weapon);
		Weapons->SetHeldSlot(0);
		Character->AttachWeaponToHand(0);
		auto* Body = Character->GetMesh();
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Body->bEnableUpdateRateOptimizations = false;
		auto Step = [&]
		{
			constexpr float Dt = 1.f / 60.f;
			World->DeltaTimeSeconds = Dt;
			World->TimeSeconds += Dt;
			Bench->Tick(Dt);
			Weapons->UpdateHoldState(Dt);
			Character->GetAnimActions()->TickComponent(Dt, LEVELTICK_All, nullptr);
			Body->TickAnimation(Dt, false);
			Body->GetAnimInstance()->NativeThreadSafeUpdateAnimation(Dt);
			Body->RefreshBoneTransforms(nullptr);
			Body->UpdateChildTransforms();
			Body->MarkRenderDynamicDataDirty();
		};
		Bench->Interact({Character, Character->GetInteractor(), 0});
		for (int32 Frame = 0; Frame < 180; ++Frame) { Step(); }
		if (!TestTrue(TEXT("完成放枪并进入特写聚焦态"), Bench->CanSelectWeapon() && Bench->IsCameraFocusingWeapon() && Bench->GetCameraFocusAlpha() > 0.90f)) { return false; }
		const auto* TableCamera = Bench->FindComponentByClass<UCameraComponent>();
		if (!TestNotNull(TEXT("工作台固定相机"), TableCamera)) { return false; }

		// 用当前帧真实骨骼胶囊与固定相机视锥，校验配件显隐帧的 8 个包围盒角点是否均被身体或相机边界挡住。
		auto IsComponentHiddenFromBenchCamera = [&](const USkinnedMeshComponent* MeshComp, FString& OutDetail) -> bool
		{
			if (!MeshComp || !MeshComp->GetSkinnedAsset()) { return true; }
			const FBox LocalBox = MeshComp->GetSkinnedAsset()->GetBounds().GetBox();
			const FTransform MeshWorld = MeshComp->GetComponentTransform();
			const FTransform CamTransform = TableCamera->GetComponentTransform();
			const FVector Eye = CamTransform.GetLocation();
			const double TanHalfX = FMath::Tan(FMath::DegreesToRadians(TableCamera->FieldOfView * 0.5));
			const double TanHalfY = TanHalfX * (9.0 / 16.0);

			struct FBodyCapsule { FVector A; FVector B; double Radius; };
			const FVector Pelvis = Body->GetSocketLocation(TEXT("pelvis"));
			const FVector Spine = Body->GetSocketLocation(TEXT("spine_05"));
			const FVector UpperL = Body->GetSocketLocation(TEXT("upperarm_l"));
			const FVector LowerL = Body->GetSocketLocation(TEXT("lowerarm_l"));
			const FVector HandL = Body->GetSocketLocation(TEXT("hand_l"));
			const FVector UpperR = Body->GetSocketLocation(TEXT("upperarm_r"));
			const FVector LowerR = Body->GetSocketLocation(TEXT("lowerarm_r"));
			const FVector HandR = Body->GetSocketLocation(TEXT("hand_r"));
			const FVector ThighL = Body->GetSocketLocation(TEXT("thigh_l"));
			const FVector CalfL = Body->GetSocketLocation(TEXT("calf_l"));
			const FVector ThighR = Body->GetSocketLocation(TEXT("thigh_r"));
			const FVector CalfR = Body->GetSocketLocation(TEXT("calf_r"));
			const FBodyCapsule Capsules[] = {
				{Pelvis, Spine, 17.5},
				{UpperL, UpperR, 14.0},
				{UpperL, LowerL, 7.5},
				{LowerL, HandL, 5.5},
				{UpperR, LowerR, 7.5},
				{LowerR, HandR, 5.5},
				{ThighL, CalfL, 12.0},
				{ThighR, CalfR, 12.0},
			};

			int32 OffScreen = 0;
			int32 BodyBlocked = 0;
			int32 Exposed = 0;
			double MinExposedU = 1.0, MinExposedV = 1.0;
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector LocalCorner(
					(Corner & 1) ? LocalBox.Max.X : LocalBox.Min.X,
					(Corner & 2) ? LocalBox.Max.Y : LocalBox.Min.Y,
					(Corner & 4) ? LocalBox.Max.Z : LocalBox.Min.Z);
				const FVector WorldPoint = MeshWorld.TransformPosition(LocalCorner);
				const FVector LocalCam = CamTransform.InverseTransformPosition(WorldPoint);
				if (LocalCam.X <= 1.0)
				{
					++OffScreen;
					continue;
				}
				const double U = 0.5 + LocalCam.Y / (2.0 * LocalCam.X * TanHalfX);
				const double V = 0.5 - LocalCam.Z / (2.0 * LocalCam.X * TanHalfY);
				if (U < 0.0 || U > 1.0 || V < 0.0 || V > 1.0)
				{
					++OffScreen;
					continue;
				}
				// 射线从相机打向角点，在角点前穿过任一身体胶囊即被躯干或手臂遮挡。
				bool bBlocked = false;
				const FVector RayEnd = FMath::Lerp(Eye, WorldPoint, 0.96);
				for (const FBodyCapsule& Cap : Capsules)
				{
					FVector ClosestOnRay, ClosestOnCap;
					FMath::SegmentDistToSegmentSafe(Eye, RayEnd, Cap.A, Cap.B, ClosestOnRay, ClosestOnCap);
					if (FVector::Distance(ClosestOnRay, ClosestOnCap) <= Cap.Radius)
					{
						bBlocked = true;
						break;
					}
				}
				if (bBlocked) { ++BodyBlocked; }
				else
				{
					++Exposed;
					MinExposedU = FMath::Min(MinExposedU, U);
					MinExposedV = FMath::Min(MinExposedV, V);
				}
			}
			OutDetail = FString::Printf(TEXT("视外=%d 躯干遮挡=%d 暴露=%d(%.2f,%.2f)"), OffScreen, BodyBlocked, Exposed, MinExposedU, MinExposedV);
			return Exposed == 0;
		};

		TArray<UGinkgoWeaponAttachment*> Parts;
		for (const auto& Mount : Weapon->Attachments)
		{
			if (Mount.Part && !Mount.bDefault && !Mount.Part->bEmptyModule && !Mount.Part->bInstallWithReload)
			{
				Parts.Add(Mount.Part);
			}
		}
		// 空模块要在装好前握把后执行，覆盖拆卸后的收手分支。
		for (const auto& Mount : Weapon->Attachments) { if (Mount.Part && Mount.Part->bEmptyModule) { Parts.Add(Mount.Part); } }
#if WITH_EDITOR
		// 可选校图直接从桌前固定相机输出 M4 六类代表部位的关键帧，直观核查动作利索度与身体遮挡。
		USceneCaptureComponent2D* Capture = nullptr;
		UTextureRenderTarget2D* Target = nullptr;
		if (FParse::Param(FCommandLine::Get(), TEXT("BenchPosePreview")) && FApp::CanEverRender())
		{
			for (const double Yaw : {-40.0, 120.0})
			{
				auto* Light = NewObject<UDirectionalLightComponent>(Bench);
				Light->SetIntensity(3.f);
				Light->SetCastShadows(false);
				Light->RegisterComponent();
				Light->SetWorldRotation(FRotator(-38, Yaw, 0));
			}
			Target = NewObject<UTextureRenderTarget2D>(Bench);
			Target->InitCustomFormat(1280, 720, PF_B8G8R8A8, false);
			Target->UpdateResourceImmediate(true);
			Capture = NewObject<USceneCaptureComponent2D>(Bench);
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
		}
		auto SaveBenchFrame = [&](const FString& FileName)
		{
			if (!Capture) { return; }
			Capture->FOVAngle = TableCamera->FieldOfView;
			Capture->SetWorldTransform(TableCamera->GetComponentTransform());
			World->SendAllEndOfFrameUpdates();
			UMaterialInterface::SubmitRemainingJobsForWorld(World, EMaterialShaderPrecompileMode::Synchronous);
			FAssetCompilingManager::Get().FinishAllCompilation();
			IStreamingManager::Get().StreamAllResources(3.f);
			World->SendAllEndOfFrameUpdates();
			Capture->CaptureScene();
			FlushRenderingCommands();
			TArray<FColor> Pixels;
			Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
			for (auto& Pixel : Pixels) { Pixel.A = 255; }
			IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("WorkbenchPose")), true);
			FImageUtils::SaveImageByExtension(*(FPaths::ProjectSavedDir() / TEXT("WorkbenchPose") / FileName), FImageView(Pixels.GetData(), 1280, 720));
		};
		if (Capture && FString(WeaponName) == TEXT("M4"))
		{
			SaveBenchFrame(TEXT("M4_FocusIdle.png"));
		}
#endif
		// 校验 M4 不同部位（枪口、枪托、弹匣、空前握把）选中时相机向各自方向给出克制偏置，不丢失整枪构图。
		if (FString(WeaponName) == TEXT("M4"))
		{
			auto FindM4Part = [&](const FName Option) -> UGinkgoWeaponAttachment*
			{
				for (const auto& Mount : Weapon->Attachments)
				{
					if (Mount.Part && Mount.Part->ModularOption == Option) { return Mount.Part; }
				}
				return nullptr;
			};
			const FVector NeutralLoc = TableCamera->GetComponentLocation();
			auto CheckPartBias = [&](const FName Option, const TCHAR* ShotName, auto&& VerifyFn)
			{
				if (UGinkgoWeaponAttachment* TargetPart = FindM4Part(Option))
				{
					Bench->SetFocusedAttachment(TargetPart);
					for (int32 F = 0; F < 80; ++F) { Step(); }
					const FVector Offset = Bench->GetCameraPartFocusOffset();
					const FVector CamLoc = TableCamera->GetComponentLocation();
					AddInfo(FString::Printf(TEXT("M4 焦点[%s]：部件偏移=(%.1f, %.1f, %.1f) cm，机位增量=(%.1f, %.1f, %.1f) cm"),
						*Option.ToString(), Offset.X, Offset.Y, Offset.Z,
						CamLoc.X - NeutralLoc.X, CamLoc.Y - NeutralLoc.Y, CamLoc.Z - NeutralLoc.Z));
					VerifyFn(Offset, CamLoc);
#if WITH_EDITOR
					if (Capture && ShotName) { SaveBenchFrame(ShotName); }
#endif
				}
			};
			CheckPartBias(TEXT("Muzzle_Suppressor"), TEXT("M4_FocusPart_Muzzle.png"), [&](const FVector& Off, const FVector& Loc)
			{
				TestTrue(TEXT("M4 选中枪口时相机向枪口侧(-Y)柔和偏移"), Off.Y < -20.0 && Loc.Y < NeutralLoc.Y - 4.0);
			});
			CheckPartBias(TEXT("Stock_DMR"), TEXT("M4_FocusPart_Stock.png"), [&](const FVector& Off, const FVector& Loc)
			{
				TestTrue(TEXT("M4 选中枪托时相机向枪托侧(+Y)柔和偏移"), Off.Y > 12.0 && Loc.Y > NeutralLoc.Y + 2.0);
			});
			CheckPartBias(TEXT("Magazine_Large"), TEXT("M4_FocusPart_Magazine.png"), [&](const FVector& Off, const FVector& Loc)
			{
				TestTrue(TEXT("M4 选中弹匣时相机向弹匣前侧(-X)柔和偏移"), Off.X < -4.0 && Loc.X < NeutralLoc.X + 4.0);
			});
			CheckPartBias(TEXT("Foregrip_None"), nullptr, [&](const FVector& Off, const FVector&)
			{
				TestTrue(TEXT("M4 选中空前握把仍定位到护木下导轨方向(-Y)"), Off.Y < -10.0);
			});
			Bench->SetFocusedAttachment(nullptr);
			for (int32 F = 0; F < 80; ++F) { Step(); }
		}
		const TSet<FName> PreviewOptions = {
			TEXT("Stock_DMR"), TEXT("Grip_Elvi"), TEXT("Trigger_Classic"),
			TEXT("Foregrip_Vertical"), TEXT("Handguard_DMR"), TEXT("Muzzle_Suppressor")
		};
		for (auto* Part : Parts)
		{
			if (!TestTrue(TEXT("可开始待测安装"), Bench->CanInstallAttachment(Part))) { continue; }
			const FString Label = FString(WeaponName) + TEXT("_") + Part->GetName();
			const bool bModularStyle = Part->InstallStyle != EGinkgoAttachmentInstallStyle::Default;
			const bool bWantPreview = FString(WeaponName) == TEXT("M4") && PreviewOptions.Contains(Part->ModularOption);
			FString Csv = TEXT("frame,lgrip,rgrip,lean,lex,ley,lez,rex,rey,rez,lelbowstep,relbowstep,lboneturn,rboneturn\n");
			FVector PrevLeftElbow = Body->GetSocketLocation(TEXT("lowerarm_l"));
			FQuat PrevLeftUpper = Body->GetSocketQuaternion(TEXT("upperarm_l"));
			FQuat PrevLeftLower = Body->GetSocketQuaternion(TEXT("lowerarm_l"));
			FQuat PrevLeftHand = Body->GetSocketQuaternion(TEXT("hand_l"));
			FVector PrevRightElbow = Body->GetSocketLocation(TEXT("lowerarm_r"));
			FQuat PrevRightUpper = Body->GetSocketQuaternion(TEXT("upperarm_r"));
			FQuat PrevRightLower = Body->GetSocketQuaternion(TEXT("lowerarm_r"));
			FQuat PrevRightHand = Body->GetSocketQuaternion(TEXT("hand_r"));
			double MaxLeftElbow = 0.0, MaxLeftTurn = 0.0;
			double MaxRightElbow = 0.0, MaxRightTurn = 0.0;
			int32 CompletionFrame = INDEX_NONE;
			bool bPreviewWasVisible = false;
			bool bRemovedWasVisible = false;
			bool bCheckedReveal = false;
			bool bRevealHidden = true;
			FString RevealDetail;
			bool bCheckedVanish = false;
			bool bVanishHidden = true;
			FString VanishDetail;
			FString LastRemovedDetail;
			bool bLastRemovedHidden = true;

			TSet<UPoseableMeshComponent*> ExistingPoseables;
			{
				TArray<UPoseableMeshComponent*> Before;
				Character->GetComponents(Before);
				for (auto* Comp : Before) { ExistingPoseables.Add(Comp); }
			}

			Bench->InstallAttachment(*Character, Part);
			UPoseableMeshComponent* NewPreviewComp = nullptr;
			UPoseableMeshComponent* OldRemovedComp = nullptr;
			const USkeletalMesh* NewAsset = Part->ModuleMesh.LoadSynchronous();
			bool bPulledBackForAction = false;
			for (int32 Frame = 0; Frame < 360; ++Frame)
			{
				Step();
				if (Frame == 30 && CompletionFrame == INDEX_NONE)
				{
					bPulledBackForAction = !Bench->IsCameraFocusingWeapon() && Bench->GetCameraFocusAlpha() < 0.15f;
				}
				if (Frame == 0 && bModularStyle)
				{
					TArray<UPoseableMeshComponent*> Now;
					Character->GetComponents(Now);
					for (auto* Comp : Now)
					{
						if (ExistingPoseables.Contains(Comp)) { continue; }
						if (NewAsset && Comp->GetSkinnedAsset() == NewAsset && !NewPreviewComp) { NewPreviewComp = Comp; }
						else if (Comp->GetSkinnedAsset() && !OldRemovedComp) { OldRemovedComp = Comp; }
					}
				}
				const auto Pose = Bench->GetInstallHandPose();
				const FVector LeftElbow = Body->GetSocketLocation(TEXT("lowerarm_l"));
				const FQuat LeftUpper = Body->GetSocketQuaternion(TEXT("upperarm_l"));
				const FQuat LeftLower = Body->GetSocketQuaternion(TEXT("lowerarm_l"));
				const FQuat LeftHand = Body->GetSocketQuaternion(TEXT("hand_l"));
				const FVector RightElbow = Body->GetSocketLocation(TEXT("lowerarm_r"));
				const FQuat RightUpper = Body->GetSocketQuaternion(TEXT("upperarm_r"));
				const FQuat RightLower = Body->GetSocketQuaternion(TEXT("lowerarm_r"));
				const FQuat RightHand = Body->GetSocketQuaternion(TEXT("hand_r"));

				const double LElbowStep = FVector::Distance(PrevLeftElbow, LeftElbow);
				const double LBoneTurn = FMath::Max3(
					FMath::RadiansToDegrees(PrevLeftUpper.AngularDistance(LeftUpper)),
					FMath::RadiansToDegrees(PrevLeftLower.AngularDistance(LeftLower)),
					FMath::RadiansToDegrees(PrevLeftHand.AngularDistance(LeftHand)));
				const double RElbowStep = FVector::Distance(PrevRightElbow, RightElbow);
				const double RBoneTurn = FMath::Max3(
					FMath::RadiansToDegrees(PrevRightUpper.AngularDistance(RightUpper)),
					FMath::RadiansToDegrees(PrevRightLower.AngularDistance(RightLower)),
					FMath::RadiansToDegrees(PrevRightHand.AngularDistance(RightHand)));
				MaxLeftElbow = FMath::Max(MaxLeftElbow, LElbowStep);
				MaxLeftTurn = FMath::Max(MaxLeftTurn, LBoneTurn);
				MaxRightElbow = FMath::Max(MaxRightElbow, RElbowStep);
				MaxRightTurn = FMath::Max(MaxRightTurn, RBoneTurn);

				Csv += FString::Printf(TEXT("%d,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f\n"),
					Frame, Pose.LeftGrip, Pose.RightGrip, Pose.BodyLean,
					LeftElbow.X, LeftElbow.Y, LeftElbow.Z, RightElbow.X, RightElbow.Y, RightElbow.Z,
					LElbowStep, RElbowStep, LBoneTurn, RBoneTurn);
				PrevLeftElbow = LeftElbow; PrevLeftUpper = LeftUpper; PrevLeftLower = LeftLower; PrevLeftHand = LeftHand;
				PrevRightElbow = RightElbow; PrevRightUpper = RightUpper; PrevRightLower = RightLower; PrevRightHand = RightHand;

				if (CompletionFrame == INDEX_NONE && Bench->CanSelectWeapon())
				{
					CompletionFrame = Frame + 1;
				}
				if (bModularStyle && CompletionFrame == INDEX_NONE)
				{
					if (IsValid(NewPreviewComp))
					{
						const bool bVis = NewPreviewComp->IsVisible();
						if (!bPreviewWasVisible && bVis && !bCheckedReveal)
						{
							bCheckedReveal = true;
							bRevealHidden = IsComponentHiddenFromBenchCamera(NewPreviewComp, RevealDetail);
#if WITH_EDITOR
							if (bWantPreview) { SaveBenchFrame(FString::Printf(TEXT("%s_Reveal_%03d.png"), *Label, Frame)); }
#endif
						}
						bPreviewWasVisible = bVis;
					}
					if (IsValid(OldRemovedComp))
					{
						const bool bVis = OldRemovedComp->IsVisible();
						if (bVis)
						{
							bLastRemovedHidden = IsComponentHiddenFromBenchCamera(OldRemovedComp, LastRemovedDetail);
						}
						else if (bRemovedWasVisible && !bCheckedVanish)
						{
							bCheckedVanish = true;
							bVanishHidden = bLastRemovedHidden;
							VanishDetail = LastRemovedDetail;
						}
						bRemovedWasVisible = bVis;
					}
				}
#if WITH_EDITOR
				if (Capture && bWantPreview && CompletionFrame == INDEX_NONE && (Frame == 18 || Frame == 48 || Frame == 60))
				{
					SaveBenchFrame(FString::Printf(TEXT("%s_F%03d.png"), *Label, Frame));
				}
#endif
			}
			IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("WorkbenchPose")), true);
			FFileHelper::SaveStringToFile(Csv, *(FPaths::ProjectSavedDir() / TEXT("WorkbenchPose") / (Label + TEXT(".csv"))));
			const float DurationSec = CompletionFrame != INDEX_NONE ? CompletionFrame / 60.f : 6.f;
			AddInfo(FString::Printf(TEXT("%s：耗时 %.2f s，左臂 %.2f cm/%.2f°，右臂 %.2f cm/%.2f°%s%s"),
				*Label, DurationSec, MaxLeftElbow, MaxLeftTurn, MaxRightElbow, MaxRightTurn,
				bCheckedReveal ? *FString::Printf(TEXT("，显形[%s]"), *RevealDetail) : TEXT(""),
				bCheckedVanish ? *FString::Printf(TEXT("，收隐[%s]"), *VanishDetail) : TEXT("")));
			TestTrue(*FString::Printf(TEXT("%s 安装过程双臂不突然翻转"), *Label),
				MaxLeftTurn < 20.0 && MaxLeftElbow < 7.0 && MaxRightTurn < 20.0 && MaxRightElbow < 7.0);
			TestTrue(TEXT("安装完成"), Weapons->GetAttachments(0).Parts.Contains(Part) && Bench->CanSelectWeapon());
			TestTrue(*FString::Printf(TEXT("%s 安装播放期间拉远相机、完成后回推聚焦"), *Label),
				bPulledBackForAction && Bench->IsCameraFocusingWeapon() && Bench->GetCameraFocusAlpha() > 0.90f);
			if (bModularStyle)
			{
				TestTrue(*FString::Printf(TEXT("%s 动作利索耗时小于 2.1 秒"), *Label), DurationSec < 2.10f);
				if (bCheckedReveal)
				{
					TestTrue(*FString::Printf(TEXT("%s 新部件显形瞬间被身体或固定相机边界挡住（%s）"), *Label, *RevealDetail), bRevealHidden);
				}
				if (bCheckedVanish)
				{
					TestTrue(*FString::Printf(TEXT("%s 旧部件隐藏瞬间被身体或固定相机边界挡住（%s）"), *Label, *VanishDetail), bVanishHidden);
				}
			}
		}
	}
	return true;
}

#endif

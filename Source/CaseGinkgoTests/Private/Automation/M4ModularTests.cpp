#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/GinkgoCharacter.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/GinkgoAnimTypes.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/GinkgoWorkbench.h"
#include "Interaction/GinkgoInteractorComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Weapon/GinkgoModularWeaponData.h"
#include "Weapon/GinkgoModularWeaponVisual.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponPickup.h"
#include "Weapon/GinkgoMagazineAttachmentAction.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#if WITH_EDITOR
#include "Animation/AnimSequence.h"
#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "PreviewScene.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoM4AttachmentTest, "CaseGinkgo.Weapon.M4.Attachments",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoM4AttachmentTest::RunTest(const FString& Parameters)
{
	auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
	if (!TestNotNull(TEXT("M4 定义"), Weapon)) { return false; }
	TestEqual(TEXT("配件目录包含 23 项"), Weapon->Attachments.Num(), 23);
	TestEqual(TEXT("七个插槽各有一个默认选项"), Weapon->GetDefaultAttachments().Parts.Num(), 7);
	TSet<FName> DefaultMounts;
	for (const auto& Mount : Weapon->Attachments)
	{
		if (!TestNotNull(TEXT("配件资产"), Mount.Part.Get())) { return false; }
		if (Mount.bDefault) { TestFalse(TEXT("默认插槽不重复"), DefaultMounts.Contains(Mount.Mount)); DefaultMounts.Add(Mount.Mount); }
		TestFalse(TEXT("配件名称不为空"), Mount.Part->DisplayName.IsEmpty());
		TestFalse(TEXT("Mutable 选项非空"), Mount.Part->ModularOption.IsNone());
		if (!Mount.Part->bEmptyModule)
		{
			TestNotNull(TEXT("模块模型可加载"), Mount.Part->ModuleMesh.LoadSynchronous());
			TestTrue(TEXT("图层尺寸有效"), Mount.Part->HudSize.X > 0 && Mount.Part->HudSize.Y > 0);
		}
	}
	const FString Name = TEXT("M4AttachmentTest") + FGuid::NewGuid().ToString();
	auto* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); Instance->RemoveFromRoot();
	};
	auto* Character = World->SpawnActor<AGinkgoCharacter>();
	if (!TestNotNull(TEXT("测试角色"), Character)) { return false; }
	auto* Weapons = Character->GetWeapons();
	Weapons->SetCarried(0, Weapon);
	Weapons->SetCarried(1, Weapon);
	const int32 InitialFeed = Weapons->GetMechanismState(0)->Feed.Num();
	auto Part = [Weapon](const TCHAR* Option) -> UGinkgoWeaponAttachment*
	{
		for (const auto& Mount : Weapon->Attachments) { if (Mount.Part && Mount.Part->ModularOption == Option) { return Mount.Part; } }
		return nullptr;
	};
	Weapons->InstallAttachment(0, Part(TEXT("Muzzle_Suppressor")));
	Weapons->InstallAttachment(0, Part(TEXT("Stock_DMR")));
	TestTrue(TEXT("开镜时间增量按同类相加"), FMath::IsNearlyEqual(Weapons->GetAttachmentEffects(0).AdsTime, .15f));
	TestTrue(TEXT("同定义的另一实例保持中性"), FMath::IsNearlyZero(Weapons->GetAttachmentEffects(1).AdsTime));
	TestEqual(TEXT("替换后仍只有七个插槽"), Weapons->GetAttachments(0).Parts.Num(), 7);
	TestFalse(TEXT("当前已装项不可重复安装"), Weapons->CanInstallAttachment(0, Part(TEXT("Muzzle_Suppressor"))));
	Weapons->InstallAttachment(0, Part(TEXT("Foregrip_Vertical")));
	TestFalse(TEXT("有前握把时拒绝 Classic 前端"), Weapons->CanInstallAttachment(0, Part(TEXT("Handguard_Classic"))));
	Weapons->InstallAttachment(0, Part(TEXT("Foregrip_None")));
	Weapons->InstallAttachment(0, Part(TEXT("Handguard_Classic")));
	TestFalse(TEXT("Classic 前端反向拒绝加装前握把"), Weapons->CanInstallAttachment(0, Part(TEXT("Foregrip_Vertical"))));
	Weapons->InstallAttachment(0, Part(TEXT("Magazine_Large")));
	TestEqual(TEXT("实例容量增至 40"), Weapons->GetMechanism(0)->GetCapacity(), 40);
	TestEqual(TEXT("共享资产仍为 30"), Weapon->Mechanism->GetCapacity(), 30);
	TestEqual(TEXT("另一实例仍为 30"), Weapons->GetMechanism(1)->GetCapacity(), 30);
	TestEqual(TEXT("增容不凭空补弹"), Weapons->GetMechanismState(0)->Feed.Num(), InitialFeed);
	auto Full = Weapons->GetMechanism(0)->MakeLoadedState(Weapon->AmmoType);
	// MakeLoadedState 会从弹匣推一发进膛；再补满弹匣，明确覆盖 40+1 换为 30+1。
	while (Full.Feed.Num() < 40) { Full.Feed.Add(Weapon->AmmoType); }
	Weapons->SetMechanismState(0, Full);
	const int32 Before = Weapons->GetAmmoReserve().Count(Weapon->AmmoType);
	const auto Chamber = Weapons->GetMechanismState(0)->Chamber;
	Weapons->InstallAttachment(0, Part(TEXT("Magazine_Light")));
	TestEqual(TEXT("小弹匣保留 30 发"), Weapons->GetMechanismState(0)->Feed.Num(), 30);
	TestEqual(TEXT("多出的 10 发退回后备"), Weapons->GetAmmoReserve().Count(Weapon->AmmoType), Before + 10);
	TestEqual(TEXT("膛内状态保持"), Weapons->GetMechanismState(0)->Chamber, Chamber);
	Weapons->InstallAttachment(0, Part(TEXT("Handguard_DMR")));
	TestTrue(TEXT("长前端更新真实枪口"), Weapons->GetMuzzleOffset(0).X > Weapon->MuzzleOffset.X + 10);
	Weapons->SetCarried(0, nullptr);
	TestNull(TEXT("清槽释放机构覆盖"), Weapons->GetMechanism(0));
	TestTrue(TEXT("清槽重置配件效果"), FMath::IsNearlyZero(Weapons->GetAttachmentEffects(0).AdsTime));
	return true;
}

namespace
{
#if WITH_EDITOR
	/** 对静止闭锁姿势做真实 CPU 蒙皮，捕捉参考姿势看不出来的错绑或悬空顶点。 */
	FBox RestBounds(USkeletalMesh& Mesh, const FName Front = NAME_None)
	{
		FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false));
		auto* Gun = NewObject<UPoseableMeshComponent>();
		Gun->SetSkinnedAssetAndUpdate(&Mesh);
		Scene.AddComponent(Gun, FTransform::Identity);
		const auto* Rest = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Weapons/M4/Anims/A_M4_Rest_Closed.A_M4_Rest_Closed"));
		const auto& Ref = Mesh.GetRefSkeleton();
		for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
		{
			const int32 Source = Rest->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(Bone));
			if (Source != INDEX_NONE) { Rest->GetBoneTransform(Gun->BoneSpaceTransforms[Bone], FSkeletonPoseBoneIndex(Source), FAnimExtractContext(0.0), false); }
		}
		if (!Front.IsNone())
		{
			const auto* Definition = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
			FGinkgoInstalledAttachments Installed;
			for (const auto& Mount : Definition->Attachments)
			{
				if (Mount.Part->ModularOption == Front) { Installed.Parts.Add(Mount.Part); }
			}
			UGinkgoWeaponComponent::ApplyModulePose(*Definition, Installed, *Gun);
		}
		Gun->RefreshBoneTransforms();
		auto& Lod = Mesh.GetResourceForRendering()->LODRenderData[0];
		FBox Bounds(ForceInit);
		for (uint32 Vertex = 0; Vertex < Lod.GetNumVertices(); ++Vertex)
		{
			Bounds += FVector(USkinnedMeshComponent::GetSkinnedVertexPosition(Gun, Vertex, Lod, *Gun->GetSkinWeightBuffer(0)));
		}
		return Bounds;
	}

	/** 可选离屏校图：真实 Mutable 网格、骨骼静止姿势与材质，输出到 Saved/M4Preview。 */
	void RenderM4(USkeletalMesh& Mesh, int32 Phase)
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("M4Preview")) || !FApp::CanEverRender()) { return; }
		FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false));
		auto* Gun = NewObject<UPoseableMeshComponent>();
		Gun->SetSkinnedAssetAndUpdate(&Mesh);
		Scene.AddComponent(Gun, FTransform::Identity);
		const auto* Rest = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Weapons/M4/Anims/A_M4_Rest_Closed.A_M4_Rest_Closed"));
		const auto& Ref = Mesh.GetRefSkeleton();
		for (int32 Bone = 0; Rest && Bone < Ref.GetNum(); ++Bone)
		{
			const int32 Source = Rest->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(Bone));
			if (Source != INDEX_NONE) { Rest->GetBoneTransform(Gun->BoneSpaceTransforms[Bone], FSkeletonPoseBoneIndex(Source), FAnimExtractContext(0.0), false); }
		}
		if (Phase > 0)
		{
			const auto* Definition = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
			const FName Fronts[] = {TEXT("Handguard_DMR"), TEXT("Handguard_Classic"), TEXT("Handguard_MK18")};
			for (const auto& Mount : Definition->Attachments)
			{
				if (Mount.Part->ModularOption == Fronts[Phase - 1])
				{
					FGinkgoInstalledAttachments Installed;
					Installed.Parts.Add(Mount.Part);
					UGinkgoWeaponComponent::ApplyModulePose(*Definition, Installed, *Gun);
				}
			}
		}
		Gun->RefreshBoneTransforms();
		auto* Fill = NewObject<UDirectionalLightComponent>();
		Fill->SetIntensity(3.f);
		Fill->SetCastShadows(false);
		Scene.AddComponent(Fill, FTransform(FRotator(-35, -70, 0)));
		auto* Target = NewObject<UTextureRenderTarget2D>();
		Target->InitCustomFormat(1280, 480, PF_B8G8R8A8, false);
		Target->UpdateResourceImmediate(true);
		auto* Capture = NewObject<USceneCaptureComponent2D>();
		Capture->TextureTarget = Target;
		Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->ProjectionType = ECameraProjectionMode::Orthographic;
		Capture->OrthoWidth = 150.f;
		Capture->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
		Capture->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
		Capture->PostProcessSettings.AutoExposureMinBrightness = 1.f;
		Capture->PostProcessSettings.AutoExposureMaxBrightness = 1.f;
		Scene.AddComponent(Capture, FTransform(FRotator(-8, 90, 0), FVector(23, -180, 28)));
		FAssetCompilingManager::Get().FinishAllCompilation();
		Scene.GetWorld()->SendAllEndOfFrameUpdates();
		Capture->CaptureScene();
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels);
		for (auto& Pixel : Pixels) { Pixel.A = 255; }
		IFileManager::Get().MakeDirectory(*(FPaths::ProjectSavedDir() / TEXT("M4Preview")), true);
		FImageUtils::SaveImageByExtension(*(FPaths::ProjectSavedDir() / FString::Printf(TEXT("M4Preview/Mutable%d.png"), Phase)), FImageView(Pixels.GetData(), 1280, 480));
	}
#endif

	/** 跨帧等待 Mutable 的真实生成，独立于角色 Tick，检查保存后的组合图可用于运行时。 */
	struct FM4Generation
	{
		/** 测试执行器，仅在潜伏命令 Update 内使用，异步回调不访问 */
		FAutomationTestBase* Test = nullptr;

		/** 测试期间手动 Root 的实例，结束或超时都会释放 */
		UCustomizableObjectInstance* Instance = nullptr;

		/** 回调是否已到达，游戏线程读写 */
		bool bDone = false;

		/** 最后一次生成的结果，由回调记录 */
		EUpdateResult Result = EUpdateResult::Error;

		/** 当前组合编号：默认、DMR 加扩容弹匣与握把、Classic、MK18；后三组应用实际枪口位移 */
		int32 Phase = 0;

		/** 默认组合的顶点数，用于确认第二套组合实际改变几何 */
		uint32 Vertices = 0;

		/** 本次请求的真实开始时间（秒），60 秒超时 */
		double Started = 0;
	};

	void Request(const TSharedPtr<FM4Generation>& State)
	{
		State->bDone = false;
		State->Started = FPlatformTime::Seconds();
		FInstanceUpdateNativeDelegate Done;
		Done.AddLambda([State](const FUpdateContext& Result) { State->Result = Result.UpdateResult; State->bDone = true; });
		State->Instance->UpdateSkeletalMeshAsyncResult(Done, true, true);
	}
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FWaitM4Generation, TSharedPtr<FM4Generation>, State);
bool FWaitM4Generation::Update()
{
	if (!State->bDone && FPlatformTime::Seconds() - State->Started < 60) { return false; }
	if (!State->Test->TestTrue(TEXT("Mutable 生成成功且未超时"), State->bDone && State->Result == EUpdateResult::Success))
	{
		State->Instance->RemoveFromRoot(); return true;
	}
	auto* Mesh = State->Instance->GetSkeletalMeshComponentSkeletalMesh(TEXT("Weapon"));
	if (!State->Test->TestNotNull(TEXT("组合输出骨骼网格"), Mesh)) { State->Instance->RemoveFromRoot(); return true; }
	State->Test->TestTrue(TEXT("机械骨骼保留"), Mesh->GetRefSkeleton().FindBoneIndex(TEXT("bolt_carrier")) != INDEX_NONE
		&& Mesh->GetRefSkeleton().FindBoneIndex(TEXT("mag_new")) != INDEX_NONE);
	const uint32 Vertices = Mesh->GetResourceForRendering()->LODRenderData[0].GetNumVertices();
#if WITH_EDITOR
	const FName Fronts[] = {NAME_None, TEXT("Handguard_DMR"), TEXT("Handguard_Classic"), TEXT("Handguard_MK18")};
	const FBox Bounds = RestBounds(*Mesh, Fronts[State->Phase]);
	State->Test->AddInfo(FString::Printf(TEXT("Mutable 组合 %d 静止蒙皮范围：%s"), State->Phase, *Bounds.ToString()));
	State->Test->TestTrue(TEXT("Mutable 静止组合没有远离枪身的几何"), Bounds.Min.X > -45 && Bounds.Max.X < 85 && Bounds.Min.Y > -10 && Bounds.Max.Y < 10 && Bounds.Min.Z > -20 && Bounds.Max.Z < 25);
	RenderM4(*Mesh, State->Phase);
#endif
	if (State->Phase == 0)
	{
		++State->Phase;
		State->Vertices = Vertices;
		State->Instance->SetIntParameterSelectedOption(TEXT("handguard"), TEXT("Handguard_DMR"));
		State->Instance->SetIntParameterSelectedOption(TEXT("magazine"), TEXT("Magazine_Large"));
		State->Instance->SetIntParameterSelectedOption(TEXT("foregrip"), TEXT("Foregrip_Vertical"));
		Request(State);
		return false;
	}
	State->Test->TestTrue(TEXT("改变参数实际生成不同几何"), Vertices != State->Vertices && Vertices > 0);
	if (State->Phase < 3)
	{
		State->Instance->SetIntParameterSelectedOption(TEXT("handguard"), State->Phase == 1 ? TEXT("Handguard_Classic") : TEXT("Handguard_MK18"));
		State->Instance->SetIntParameterSelectedOption(TEXT("foregrip"), TEXT("Foregrip_None"));
		++State->Phase;
		Request(State);
		return false;
	}
	State->Instance->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoM4MutableTest, "CaseGinkgo.Weapon.M4.Mutable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoM4MutableTest::RunTest(const FString& Parameters)
{
	auto* Object = LoadObject<UCustomizableObject>(nullptr, TEXT("/Game/Weapons/M4/CO_M4.CO_M4"));
	if (!TestNotNull(TEXT("持久化 Mutable 资产"), Object)) { return false; }
	FCompileParams Compile;
	Compile.bAsync = false;
	Object->Compile(Compile);
	auto State = MakeShared<FM4Generation>();
	State->Test = this;
	State->Instance = Object->CreateInstance();
	if (!TestNotNull(TEXT("创建 Mutable 实例"), State->Instance)) { return false; }
	State->Instance->AddToRoot();
	Request(State);
	ADD_LATENT_AUTOMATION_COMMAND(FWaitM4Generation(State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoM4WorkbenchTest, "CaseGinkgo.Weapon.M4.Workbench",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoM4WorkbenchTest::RunTest(const FString& Parameters)
{
	auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
	if (!TestNotNull(TEXT("M4 定义"), Weapon)) { return false; }
	const FString Name = TEXT("M4BenchTest") + FGuid::NewGuid().ToString();
	auto* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); Instance->RemoveFromRoot();
	};
	auto* Character = World->SpawnActor<AGinkgoCharacter>(FVector(-110, 0, 92), FRotator::ZeroRotator);
	auto* Controller = World->SpawnActor<AGinkgoPlayerController>();
	auto* Bench = World->SpawnActor<AGinkgoWorkbench>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Character->DispatchBeginPlay();
	Bench->DispatchBeginPlay();
	auto* Weapons = Character->GetWeapons();
	ON_SCOPE_EXIT
	{
		// 手动驱动的测试世界不进入 World::BeginPlay；先停树，避免 GC 时才丢失外部数据上下文。
		Weapons->StopActionStates();
		Character->Destroy();
	};
	for (int32 Slot = 0; Slot < Weapons->SlotCount; ++Slot) { Weapons->SetCarried(Slot, nullptr); }
	Weapons->SetCarried(0, Weapon);
	Weapons->SetHeldSlot(0);
	Character->AttachWeaponToHand(0);
	auto* Gun = Character->GetHeldWeaponMesh();
	auto* Interactor = Character->GetInteractor();
	auto Step = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			constexpr float Dt = 1.f / 60.f;
			World->TimeSeconds += Dt;
			if (IsValid(Bench) && Bench->IsActorTickEnabled()) { Bench->Tick(Dt); }
			Weapons->UpdateHoldState(Dt);
			Character->GetAnimActions()->TickComponent(Dt, LEVELTICK_All, nullptr);
			Character->GetMesh()->TickAnimation(Dt, false);
			Character->GetMesh()->GetAnimInstance()->NativeThreadSafeUpdateAnimation(Dt);
			Character->GetMesh()->RefreshBoneTransforms(nullptr);
		}
	};
	auto Part = [Weapon](const TCHAR* Option) -> UGinkgoWeaponAttachment*
	{
		for (const auto& Mount : Weapon->Attachments) { if (Mount.Part && Mount.Part->ModularOption == Option) { return Mount.Part; } }
		return nullptr;
	};
	auto Enter = [&]
	{
		Bench->Interact({Character, Interactor, 0});
		Step(150);
		return TestTrue(TEXT("M4 完成放枪并可改装"), Bench->CanSelectWeapon() && Character->GetWorkbench() == Bench && Bench->GetSelectedSlot() == 0);
	};
	auto PreviewCount = [&]
	{
		TArray<UPoseableMeshComponent*> Components;
		Character->GetComponents(Components);
		// 已结束动作的占位可移交给外观管理器，等真实 Mutable 回调再销毁；这里仅检查动作没有遗留组件。
		const auto* Visual = Weapons->GetModularVisual(0);
		return Components.Num() - (Visual ? Visual->GetHandoffCount() : 0);
	};
	const int32 OriginalMeshes = PreviewCount();
	const auto OriginalAmmo = *Weapons->GetMechanismState(0);
	const int32 OriginalReserve = Weapons->GetAmmoReserve().Count(Weapon->AmmoType);
	if (!Enter()) { return false; }
	auto* Large = Part(TEXT("Magazine_Large"));
	Bench->InstallAttachment(*Character, Large);
	Step(110);
	TestTrue(TEXT("取枪后复用现有战术换弹蒙太奇"), Character->GetMesh()->GetAnimInstance()->Montage_IsPlaying(
		Weapon->Steps.FindChecked(FGameplayTag::RequestGameplayTag(TEXT("Weapon.Step.ReloadTactical"))).Montage));
	TestTrue(TEXT("新弹匣由临时预览展示，隐藏原模型的备用件"), Gun->IsBoneHiddenByName(TEXT("mag_new")));
	TestEqual(TEXT("动作尚未完成时容量不提前改变"), Weapons->GetMechanism(0)->GetCapacity(), 30);
	Bench->RequestExit(*Character);
	Step(150);
	TestFalse(TEXT("取消改装恢复旧件可见性"), Gun->IsBoneHiddenByName(TEXT("mag")));
	TestFalse(TEXT("取消改装不提交新配件"), Weapons->GetAttachments(0).Parts.Contains(Large));
	TestTrue(TEXT("入位前取消保留原匣余弹和膛内状态"), *Weapons->GetMechanismState(0) == OriginalAmmo);
	TestEqual(TEXT("入位前取消不扣后备"), Weapons->GetAmmoReserve().Count(Weapon->AmmoType), OriginalReserve);
	TestEqual(TEXT("入位前取消清理临时弹匣"), PreviewCount(), OriginalMeshes);
	if (!Enter()) { return false; }
	const int32 MagOutBefore = Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagOut);
	const int32 MagInBefore = Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagIn);
	Bench->InstallAttachment(*Character, Large);
	Step(225);
	TestEqual(TEXT("弹匣入位时提交新容量"), Weapons->GetMechanism(0)->GetCapacity(), 40);
	TestEqual(TEXT("入位时按扩容后的容量装满"), Weapons->GetMechanismState(0)->Feed.Num(), 40);
	TestEqual(TEXT("更换弹匣保留膛内状态"), Weapons->GetMechanismState(0)->Chamber, OriginalAmmo.Chamber);
	TestEqual(TEXT("更换弹匣保留膛内弹种"), Weapons->GetMechanismState(0)->ChamberAmmo, OriginalAmmo.ChamberAmmo);
	const int32 ReserveAfterLarge = OriginalReserve - (40 - OriginalAmmo.Feed.Num());
	TestEqual(TEXT("后备只扣新旧匣的差额"), Weapons->GetAmmoReserve().Count(Weapon->AmmoType), ReserveAfterLarge);
	TestEqual(TEXT("战术换匣只触发一次取匣音"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagOut), MagOutBefore + 1);
	TestEqual(TEXT("战术换匣只触发一次入匣音"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagIn), MagInBefore + 1);
	Bench->RequestExit(*Character);
	Step(150);
	TestTrue(TEXT("入位后取消保留已安装的新弹匣"), Weapons->GetAttachments(0).Parts.Contains(Large));
	TestEqual(TEXT("入位后离桌保留已装弹药"), Weapons->GetMechanismState(0)->Feed.Num(), 40);
	TestEqual(TEXT("入位后离桌不二次结算"), Weapons->GetAmmoReserve().Count(Weapon->AmmoType), ReserveAfterLarge);
	TestEqual(TEXT("入位后取消仅在新外观尚未就绪时隐藏旧弹匣"), Gun->IsBoneHiddenByName(TEXT("mag")),
		Weapons->GetModularVisual(0)->HasHandoff(TEXT("mag")));
	TestFalse(TEXT("入位后取消恢复备用骨骼隐藏标记"), Gun->IsBoneHiddenByName(TEXT("mag_new")));
	TestEqual(TEXT("入位后取消清理临时弹匣"), PreviewCount(), OriginalMeshes);
	if (!Enter()) { return false; }
	// 真实动作路线缩容：满 40+1 换为 30+1，多出的十发只退回一次。
	auto Full = Weapons->GetMechanism(0)->MakeLoadedState(Weapon->AmmoType);
	while (Full.Feed.Num() < 40) { Full.Feed.Add(Weapon->AmmoType); }
	Weapons->SetMechanismState(0, Full);
	Bench->InstallAttachment(*Character, Part(TEXT("Magazine_Default")));
	Step(400);
	TestEqual(TEXT("换小弹匣动作保留三十发"), Weapons->GetMechanismState(0)->Feed.Num(), 30);
	TestEqual(TEXT("换小弹匣净归还十发"), Weapons->GetAmmoReserve().Count(Weapon->AmmoType), ReserveAfterLarge + 10);
	TestEqual(TEXT("换小弹匣不动膛内状态"), Weapons->GetMechanismState(0)->Chamber, Full.Chamber);
	auto MissingMagazine = *Weapons->GetMechanismState(0);
	MissingMagazine.bMagazineIn = false;
	MissingMagazine.Feed.Reset();
	Weapons->SetMechanismState(0, MissingMagazine);
	Weapons->SetReserveRounds(Weapon->AmmoType, 3);
	Bench->InstallAttachment(*Character, Large);
	Step(400);
	TestEqual(TEXT("动作成功后容量变为 40"), Weapons->GetMechanism(0)->GetCapacity(), 40);
	TestTrue(TEXT("原本缺匣也能装上新的实体弹匣"), Weapons->GetMechanismState(0)->bMagazineIn);
	TestEqual(TEXT("后备不足时只装入现有三发"), Weapons->GetMechanismState(0)->Feed.Num(), 3);
	TestEqual(TEXT("装入三发后后备耗尽"), Weapons->GetAmmoReserve().Count(Weapon->AmmoType), 0);
	TestEqual(TEXT("完整动作清理临时弹匣"), PreviewCount(), OriginalMeshes);
	TestTrue(TEXT("完成后允许继续改装"), Bench->CanSelectWeapon());
	const int32 SeatBefore = Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagIn);
	Bench->InstallAttachment(*Character, Part(TEXT("Foregrip_Vertical")));
	Step(260);
	TestTrue(TEXT("安装前握把后记录独立状态"), Weapons->GetAttachments(0).Parts.Contains(Part(TEXT("Foregrip_Vertical"))));
	TestEqual(TEXT("前握把入位借用一次装匣音"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagIn), SeatBefore + 1);
	const int32 RemoveBefore = Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagOut);
	Bench->InstallAttachment(*Character, Part(TEXT("Foregrip_None")));
	Step(150);
	TestTrue(TEXT("空模块经拆卸动作移除前握把"), Weapons->GetAttachments(0).Parts.Contains(Part(TEXT("Foregrip_None"))));
	TestEqual(TEXT("只拆卸时触发一次取件音"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagOut), RemoveBefore + 1);
	TestEqual(TEXT("只拆卸时不播放安装音"), Weapons->GetDebugCueCount(FWeaponTags::Weapon_Cue_MagIn), SeatBefore + 1);
	Bench->InstallAttachment(*Character, Part(TEXT("Stock_DMR")));
	Step(55);
	Bench->Destroy();
	TestNull(TEXT("桌子退出清理占用"), Character->GetWorkbench());
	TestFalse(TEXT("拥有者退出不提交中途模块"), Weapons->GetAttachments(0).Parts.Contains(Part(TEXT("Stock_DMR"))));
	TestFalse(TEXT("拥有者退出恢复模块可见性"), Gun->IsBoneHiddenByName(TEXT("mount_stock")));
	Weapons->SetMechanismState(0, Weapons->GetMechanism(0)->MakeLoadedState(Weapon->AmmoType));
	const auto State = *Weapons->GetMechanismState(0);
	const auto Parts = Weapons->GetAttachments(0).Parts;
	const int32 ReserveBeforeDrop = Weapons->GetAmmoReserve().Count(Weapon->AmmoType);
	Weapons->DropWeapon(0);
	TActorIterator<AGinkgoWeaponPickup> Ground(World);
	if (!TestTrue(TEXT("M4 掉落生成拾取物"), bool(Ground))) { return false; }
	TestTrue(TEXT("拾取物保存弹药"), Ground->GetMechanismState() && *Ground->GetMechanismState() == State);
	Ground->BeginLocalInteract({Character, Interactor});
	Ground->Interact({Character, Interactor});
	Step(240);
	TestTrue(TEXT("拾回 M4 恢复全部模块"), Weapons->GetAttachments(0).Parts.Num() == Parts.Num()
		&& Parts.ContainsByPredicate([&](const UGinkgoWeaponAttachment* Part) { return !Weapons->GetAttachments(0).Parts.Contains(Part); }) == false);
	TestEqual(TEXT("拾回后有效容量仍为 40"), Weapons->GetMechanism(0)->GetCapacity(), 40);
	TestTrue(TEXT("拾回保持膛内与弹匣状态"), *Weapons->GetMechanismState(0) == State);
	TestEqual(TEXT("拾取恢复不误退弹或赠送后备弹药"), Weapons->GetAmmoReserve().Count(Weapon->AmmoType), ReserveBeforeDrop);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoM4HandlingTest, "CaseGinkgo.Weapon.M4.Handling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoM4HandlingTest::RunTest(const FString& Parameters)
{
	auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
	if (!TestNotNull(TEXT("M4 定义"), Weapon)) { return false; }
	const FString Name = TEXT("M4HandlingTest") + FGuid::NewGuid().ToString();
	auto* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); Instance->RemoveFromRoot();
	};
	auto* Character = World->SpawnActor<AGinkgoCharacter>();
	auto* Controller = World->SpawnActor<AGinkgoPlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Character);
	Character->DispatchBeginPlay();
	Character->GetGinkgoMovement()->SetMovementMode(MOVE_Walking);
	auto* Weapons = Character->GetWeapons();
	ON_SCOPE_EXIT
	{
		Weapons->StopActionStates();
		Character->Destroy();
	};
	Weapons->SetCarried(0, Weapon);
	Weapons->SetHeldSlot(0);
	Character->AttachWeaponToHand(0);
	auto Step = [&](int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame) { Weapons->UpdateHoldState(1.f / 60.f); }
	};
	auto Install = [&](const TCHAR* Option)
	{
		for (const auto& Mount : Weapon->Attachments)
		{
			if (Mount.Part->ModularOption == Option) { Weapons->InstallAttachment(0, Mount.Part); return; }
		}
		AddError(FString::Printf(TEXT("缺少配件 %s"), Option));
	};
	// 对同一条转向输入测准星滞后，避免只断言配置字段而漏掉实际消费路径。
	auto TurnLag = [&]
	{
		Controller->SetControlRotation(FRotator::ZeroRotator);
		Step(120);
		for (int32 Frame = 1; Frame <= 12; ++Frame)
		{
			Controller->SetControlRotation(FRotator(0, Frame, 0));
			Step(1);
		}
		return FMath::Abs(Weapons->GetHipAimLag().X);
	};
	const double DefaultLag = TurnLag();
	const float DefaultHipSpeed = Character->GetGinkgoMovement()->GetMaxSpeed();
	const float DefaultHipSpread = Weapons->GetHoldState().Spread;
	Install(TEXT("Stock_Light"));
	TestTrue(TEXT("轻型枪托实际减少腰射转向滞后"), TurnLag() < DefaultLag);
	Install(TEXT("Stock_Tube"));
	TestTrue(TEXT("无肩托实际减少腰射转向滞后"), TurnLag() < DefaultLag);
	TestTrue(TEXT("无肩托提高未开镜地速"), Character->GetGinkgoMovement()->GetMaxSpeed() > DefaultHipSpeed);
	TestTrue(TEXT("无肩托降低腰射精度"), Weapons->GetHoldState().Spread > DefaultHipSpread);
	Install(TEXT("Stock_DMR"));
	Install(TEXT("Magazine_Large"));
	TestTrue(TEXT("扩容弹匣实际增加腰射转向滞后"), TurnLag() > DefaultLag);
	Install(TEXT("Magazine_Light"));
	Install(TEXT("Handguard_DMR"));
	Step(60);
	TestTrue(TEXT("DMR 前端降低未开镜地速"), Character->GetGinkgoMovement()->GetMaxSpeed() < DefaultHipSpeed);
	TestTrue(TEXT("DMR 前端降低腰射精度"), Weapons->GetHoldState().Spread > DefaultHipSpread);
	Weapons->SetWantsToAim(true);
	Step(60);
	TestTrue(TEXT("实际进入 ADS"), Weapons->GetHoldState().GetAdsAlpha() > .99f);
	const float DmrAdsSpeed = Character->GetGinkgoMovement()->GetMaxSpeed();
	const float DmrAdsSpread = Weapons->GetHoldState().Spread;
	// 清除两个精度来源，分别验证前端和枪托。保留枪本体的 ADS 地速基线。
	Install(TEXT("Handguard_Kmode"));
	Step(60);
	TestTrue(TEXT("DMR 前端提高 ADS 精度"), Weapons->GetHoldState().Spread > DmrAdsSpread);
	const float StockAdsSpread = Weapons->GetHoldState().Spread;
	Install(TEXT("Stock_Light"));
	Step(60);
	TestTrue(TEXT("DMR 枪托提高 ADS 精度"), Weapons->GetHoldState().Spread > StockAdsSpread);
	TestTrue(TEXT("DMR 组合降低 ADS 移速"), Character->GetGinkgoMovement()->GetMaxSpeed() > DmrAdsSpeed);
	const float DefaultAdsSpeed = Character->GetGinkgoMovement()->GetMaxSpeed();
	Install(TEXT("Stock_Tube"));
	Step(60);
	TestTrue(TEXT("无肩托提高 ADS 移速"), Character->GetGinkgoMovement()->GetMaxSpeed() > DefaultAdsSpeed);
	const float AdsHeldSpeed = Character->GetGinkgoMovement()->GetHeldRunSpeed();
	Weapons->SetWantsToAim(false);
	Step(60);
	TestTrue(TEXT("退出 ADS 后恢复非开镜地速加成，ADS 不重复乘腰射倍率"), Character->GetGinkgoMovement()->GetHeldRunSpeed() > AdsHeldSpeed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoM4ModuleGeometryTest, "CaseGinkgo.Weapon.M4.ModuleGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoM4ModuleGeometryTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	const auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M4/DA_M4.DA_M4"));
	if (!Weapon) { return false; }
	for (const auto& Mount : Weapon->Attachments)
	{
		if (!Mount.Part || Mount.Part->bEmptyModule) { continue; }
		auto* Mesh = Mount.Part->ModuleMesh.LoadSynchronous();
		if (!TestNotNull(TEXT("模块模型"), Mesh)) { continue; }
		const FBox Bounds = RestBounds(*Mesh);
		AddInfo(FString::Printf(TEXT("%s 静止蒙皮范围：%s"), *Mount.Part->ModularOption.ToString(), *Bounds.ToString()));
		TestTrue(*Mount.Part->ModularOption.ToString(), Bounds.Min.X > -45 && Bounds.Max.X < 85 && Bounds.Min.Y > -10 && Bounds.Max.Y < 10 && Bounds.Min.Z > -20 && Bounds.Max.Z < 25);
	}
#endif
	return true;
}

#endif

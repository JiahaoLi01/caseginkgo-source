#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AssetCompilingManager.h"
#include "Combat/GinkgoBulletImpact.h"
#include "Components/DecalComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/WorldSettings.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ScopeExit.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoBulletImpactTest, "CaseGinkgo.Combat.BulletImpact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoBulletImpactTest::RunTest(const FString& Parameters)
{
	const auto* Dust = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Weapons/FX/NS_BulletImpactDust"));
	const auto* Mist = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Weapons/FX/NS_BulletImpactBlood"));
	if (!TestNotNull(TEXT("墙面粒子资产"), Dust) || !TestNotNull(TEXT("身体粒子资产"), Mist)) { return false; }
	TestTrue(TEXT("扬尘与碎片分别发射"), Dust->GetEmitterHandles().Num() == 2 && Dust->GetEmitterHandle(1).GetName() == TEXT("Fragments"));
	TestTrue(TEXT("身体只喷血雾"), Mist->GetEmitterHandles().Num() == 1 && Mist->GetEmitterHandle(0).GetName() == TEXT("BloodMist"));
	if (!FApp::CanEverRender()) { AddInfo(TEXT("当前运行无渲染，射击贴花与画面对照需在可渲染世界执行。")); return true; }

	const FString Name = TEXT("BulletImpact") + FGuid::NewGuid().ToString();
	auto* Instance = NewObject<UGameInstance>(GEngine);
	Instance->AddToRoot();
	Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
	UWorld* World = Instance->GetWorld();
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->BeginTearingDown();
		Instance->Shutdown();
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		Instance->RemoveFromRoot();
	};
	auto* Effects = World->GetSubsystem<UGinkgoBulletImpactSubsystem>();
	if (!TestNotNull(TEXT("弹着表现子系统"), Effects)) { return false; }
	Effects->OnWorldBeginPlay(*World);

	auto* Enemy = World->SpawnActor<AGinkgoAICharacter>(FVector(0, 0, 96), FRotator::ZeroRotator);
	auto* Player = World->SpawnActor<AGinkgoCharacter>(FVector(400, -350, 96), FRotator::ZeroRotator);
	Enemy->DispatchBeginPlay();
	Player->DispatchBeginPlay();
	ON_SCOPE_EXIT
	{
		Enemy->GetWeapons()->StopActionStates();
		Player->GetWeapons()->StopActionStates();
		Enemy->Destroy();
		Player->Destroy();
	};
	const FVector Chest = Enemy->GetMesh()->GetSocketLocation(TEXT("spine_04"));
	const FTransform WallPose(FQuat::Identity, Chest - FVector(160, 0, 0), FVector(.15, 4, 4));
	auto* Wall = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WallPose);
	Wall->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube")));
	Wall->FinishSpawning(WallPose);
	auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47"));
	if (!TestNotNull(TEXT("开火动作使用实际武器"), Weapon)) { return false; }
	auto* Weapons = Player->GetWeapons();
	Weapons->SetCarried(0, Weapon);
	Weapons->SetHeldSlot(0);
	Player->AttachWeaponToHand(0);
	auto FireFrom = [&](const FVector& Muzzle)
	{
		const FRotator Aim(0, 180, 0);
		Player->GetHeldWeaponMesh()->SetWorldLocationAndRotation(Muzzle - Aim.RotateVector(Weapons->GetMuzzleOffset(0)), Aim);
		Weapons->StartStep(FWeaponTags::Weapon_Step_Fire);
	};
	auto FindMark = [](AActor* Actor, FName MaterialName) -> UDecalComponent*
	{
		TInlineComponentArray<UDecalComponent*> Decals(Actor);
		for (auto* Decal : Decals)
		{
			if (Decal->GetDecalMaterial() && Decal->GetDecalMaterial()->GetFName() == MaterialName) { return Decal; }
		}
		return nullptr;
	};
	FireFrom(Chest - FVector(60, 0, 0));
	auto* Hole = FindMark(World->GetWorldSettings(), TEXT("M_BulletImpactChip"));
	if (!TestNotNull(TEXT("向静态墙开火产生弹孔"), Hole)) { return false; }
	FireFrom(Chest + FVector(120, 0, 0));
	auto* Blood = FindMark(Enemy, TEXT("M_BulletImpactStain"));
	if (!TestNotNull(TEXT("向实际敌人开火产生血迹"), Blood)) { return false; }
	bool bSpawnedDust = false;
	bool bSpawnedMist = false;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		if (It->GetWorld() != World) { continue; }
		bSpawnedDust |= It->GetAsset() == Dust && It->IsActive();
		bSpawnedMist |= It->GetAsset() == Mist && It->IsActive();
	}
	TestTrue(TEXT("墙面击中启动扬尘与碎片系统"), bSpawnedDust);
	TestTrue(TEXT("角色击中启动血雾系统"), bSpawnedMist);
	TestTrue(TEXT("血迹挂在受击骨骼"), Blood->GetAttachParent() == Enemy->GetMesh() && !Blood->GetAttachSocketName().IsNone());
	const FVector Before = Blood->GetComponentLocation();
	Enemy->AddActorWorldOffset(FVector(0, 50, 0), false, nullptr, ETeleportType::TeleportPhysics);
	TestTrue(TEXT("移动后血迹保持在同一身体位置"), Blood->GetComponentLocation().Equals(Before + FVector(0, 50, 0), .1));

	if (FParse::Param(FCommandLine::Get(), TEXT("GinkgoCaptureImpacts")))
	{
		// 保持相机、光照与姿势固定，只切换贴花可见性，在实际重定向外观上检查投影。
		auto* Light = NewObject<UDirectionalLightComponent>(Wall);
		Light->SetIntensity(4.f);
		Light->RegisterComponent();
		Light->SetWorldRotation(FRotator(-25, 165, 0));
		auto* Target = NewObject<UTextureRenderTarget2D>(Wall);
		Target->InitCustomFormat(640, 480, PF_B8G8R8A8, false);
		Target->UpdateResourceImmediate(true);
		auto* Camera = NewObject<USceneCaptureComponent2D>(Wall);
		Camera->TextureTarget = Target;
		Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Camera->bCaptureEveryFrame = Camera->bCaptureOnMovement = false;
		Camera->ShowFlags.SetTemporalAA(false);
		Camera->ShowFlags.SetMotionBlur(false);
		Camera->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
		Camera->PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
		Camera->PostProcessSettings.AutoExposureMinBrightness = Camera->PostProcessSettings.AutoExposureMaxBrightness = 1.f;
		Camera->RegisterComponent();
		World->SendAllEndOfFrameUpdates();
		UMaterialInterface::SubmitRemainingJobsForWorld(World, EMaterialShaderPrecompileMode::Synchronous);
		FAssetCompilingManager::Get().FinishAllCompilation();
		const FString Dir = FPaths::ProjectSavedDir() / TEXT("Automation/BulletImpactFrames");
		IFileManager::Get().MakeDirectory(*Dir, true);
		auto Compare = [&](UDecalComponent* Mark, const FString& Label)
		{
			Camera->SetWorldLocationAndRotation(Mark->GetComponentLocation() + FVector(85, 0, 0), FRotator(0, 180, 0));
			TArray<FColor> Frames[2];
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Mark->SetVisibility(Index == 1);
				World->SendAllEndOfFrameUpdates();
				Camera->CaptureScene();
				FlushRenderingCommands();
				if (!TestTrue(TEXT("读取实际渲染帧"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Frames[Index])
					&& Frames[Index].Num() == 640 * 480)) { return; }
				for (FColor& Pixel : Frames[Index]) { Pixel.A = 255; }
				FImageUtils::SaveImageByExtension(*(Dir / (Label + (Index ? TEXT("_After.png") : TEXT("_Before.png")))), FImageView(Frames[Index].GetData(), 640, 480));
			}
			int32 Changed = 0;
			int32 Reddened = 0;
			for (int32 Y = 190; Y < 290; ++Y)
			{
				for (int32 X = 270; X < 370; ++X)
				{
					const auto& A = Frames[0][Y * 640 + X];
					const auto& B = Frames[1][Y * 640 + X];
					if (FMath::Max3(FMath::Abs(int(A.R) - B.R), FMath::Abs(int(A.G) - B.G), FMath::Abs(int(A.B) - B.B)) > 8) { ++Changed; }
					if ((int(B.R) - B.G) - (int(A.R) - A.G) > 12) { ++Reddened; }
				}
			}
			TestTrue(*FString::Printf(TEXT("%s 在命中位置实际改变像素（%d）"), *Label, Changed), Changed > 20);
			if (Mark == Blood) { TestTrue(*FString::Printf(TEXT("角色血迹呈红色（%d 像素）"), Reddened), Reddened > 20); }
		};
		Compare(Hole, TEXT("Wall"));
		Compare(Blood, TEXT("Character"));
	}
	return true;
}
#endif

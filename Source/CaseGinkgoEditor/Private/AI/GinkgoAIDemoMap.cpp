#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoPatrolRoute.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/PackageName.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"

/** 新建独立灰盒：中央高墙负责丢失视线，两侧矮墙用于 EQS；默认地图与现有 Untitled 不受影响。 */
bool BuildGinkgoAIDemoMap(const bool bForce, const bool bWeaponVariants)
{
	const FString Path(bWeaponVariants ? TEXT("/Game/AI/Maps/L_AIWeaponTypes") : TEXT("/Game/AI/Maps/L_AIPrototype"));
	if (!bForce && FPackageName::DoesPackageExist(Path)) return true;
	UWorld* World = GEditor->NewMap();
	if (!World) return false;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube) return false;
	auto Box = [World, Cube](const TCHAR* Label, const FVector& Location, const FVector& Size)
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
		Actor->SetActorLabel(Label);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Actor->SetActorScale3D(Size / 100.0);
		Actor->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		return Actor;
	};
	Box(TEXT("AI_Floor"), FVector(0.0, 0.0, -25.0), FVector(3600.0, 3200.0, 50.0));
	Box(TEXT("AI_Occluder"), FVector(0.0, 0.0, 160.0), FVector(600.0, 80.0, 320.0));
	Box(TEXT("AI_CoverLeft"), FVector(-550.0, 350.0, 47.5), FVector(300.0, 60.0, 95.0));
	Box(TEXT("AI_CoverRight"), FVector(550.0, 350.0, 47.5), FVector(300.0, 60.0, 95.0));
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0.0, 0.0, 1000.0), FRotator(-50.0, -30.0, 0.0));
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sun->GetLightComponent()->SetIntensity(5.f);
	ASkyLight* Sky = World->SpawnActor<ASkyLight>();
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->SetIntensity(0.8f);
	Sky->GetLightComponent()->bRealTimeCapture = true;
	World->SpawnActor<APlayerStart>(FVector(0.0, -1050.0, 100.0), FRotator(0.0, 90.0, 0.0));
	AGinkgoPatrolRoute* Route = World->SpawnActor<AGinkgoPatrolRoute>();
	Route->SetActorLabel(TEXT("Patrol_OuterLoop"));
	Route->Points.SetNum(4);
	Route->Points[0].Location = FVector(0.0, 700.0, 0.0);
	Route->Points[0].WaitSeconds = 3.f;
	Route->Points[1].Location = FVector(800.0, 700.0, 0.0);
	Route->Points[1].WaitSeconds = 1.f;
	Route->Points[2].Location = FVector(800.0, -300.0, 0.0);
	Route->Points[3].Location = FVector(0.0, 700.0, 0.0);
	Route->OnConstruction(Route->GetActorTransform());
	AGinkgoAICharacter* Enemy = World->SpawnActor<AGinkgoAICharacter>(FVector(0.0, 700.0, 100.0), FRotator(0.0, -90.0, 0.0));
	Enemy->SetActorLabel(TEXT("Enemy_Patrol"));
	Enemy->PatrolRoute = Route;
	if (bWeaponVariants)
	{
		Enemy->SetActorLabel(TEXT("Enemy_AK47"));
		for (int32 Index = 0; Index < 2; ++Index)
		{
			AGinkgoAICharacter* Variant = World->SpawnActor<AGinkgoAICharacter>(FVector(Index == 0 ? -1050.0 : 1050.0, 700.0, 100.0), FRotator(0.0, -90.0, 0.0));
			Variant->Definition = TSoftObjectPtr<UGinkgoAIConfig>(FSoftObjectPath(Index == 0 ? TEXT("/Game/AI/DA_Enemy_Pistol.DA_Enemy_Pistol") : TEXT("/Game/AI/DA_Enemy_Shotgun.DA_Enemy_Shotgun")));
			Variant->SetActorLabel(Index == 0 ? TEXT("Enemy_M1911") : TEXT("Enemy_Shotgun"));
		}
		Box(TEXT("AI_WallCoverLeft"), FVector(-300.0, 500.0, 140.0), FVector(100.0, 60.0, 280.0));
		Box(TEXT("AI_WallCoverRight"), FVector(300.0, 500.0, 140.0), FVector(100.0, 60.0, 280.0));
	}

	ANavMeshBoundsVolume* Bounds = World->SpawnActor<ANavMeshBoundsVolume>(FVector(0.0, 0.0, 200.0), FRotator::ZeroRotator);
	UCubeBuilder* Builder = NewObject<UCubeBuilder>();
	Builder->X = 3500.f;
	Builder->Y = 3100.f;
	Builder->Z = 600.f;
	UActorFactory::CreateBrushForVolumeActor(Bounds, Builder);
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Navigation) return false;
	// NewMap 在 commandlet 没有下一帧编辑器 tick 来释放异步装图锁；几何已同步生成完成。
	Navigation->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock, UNavigationSystemV1::ELockRemovalRebuildAction::NoRebuild);
	World->UpdateWorldComponents(false, false);
	Navigation->OnWorldInitDone(FNavigationSystemRunMode::EditorMode);
	Navigation->OnNavigationBoundsUpdated(Bounds);
	Navigation->Tick(0.1f);
	Navigation->Build();
	for (ANavigationData* Data : Navigation->NavDataSet)
	{
		if (Data) Data->EnsureBuildCompletion();
	}
	FNavLocation NavPoint;
	if (!Navigation->ProjectPointToNavigation(FVector(800.0, 700.0, 0.0), NavPoint, FVector(100.0, 100.0, 200.0))) return false;
	const FString Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetMapPackageExtension());
	return FEditorFileUtils::SaveMap(World, Filename);
}

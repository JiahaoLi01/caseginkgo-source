#pragma once

#include "Components/BoxComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/Traversal/GinkgoTraversalComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Movement/Traversal/GinkgoTraversalDetection.h"

namespace GinkgoTraversalTests
{
	constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** 每个场景拥有独立物理世界，使用真实碰撞查询，不修改打开的编辑器地图或资产。 */
	struct FScene
	{
		/** 游戏实例拥有世界上下文；析构时按项目其他场景测试的顺序释放。 */
		UGameInstance* Instance = nullptr;

		/** 只用于本场景的同步物理查询。 */
		UWorld* World = nullptr;

		/** 默认脚底在原点、相机朝 +X、站立胶囊 34×88 cm 的查询快照。 */
		FGinkgoTraversalQuery Query;

		/** 每个测试可单独调整的检测参数，不改资产。 */
		FGinkgoTraversalSettings Settings;

		FScene()
		{
			const FString Name = TEXT("TraversalTest") + FGuid::NewGuid().ToString();
			Instance = NewObject<UGameInstance>(GEngine);
			Instance->AddToRoot();
			Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
			World = Instance->GetWorld();
			World->InitializeActorsForPlay(FURL());
		}

		~FScene()
		{
			World->BeginTearingDown();
			for (TActorIterator<AActor> Actor(World); Actor; ++Actor) { Actor->RouteEndPlay(EEndPlayReason::Quit); }
			Instance->Shutdown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			Instance->RemoveFromRoot();
		}

		UBoxComponent* Box(const FVector& Center, const FVector& Extent, const EComponentMobility::Type Mobility = EComponentMobility::Static,
			const FRotator& Rotation = FRotator::ZeroRotator)
		{
			auto* Actor = World->SpawnActor<AActor>();
			auto* Component = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Component);
			Component->SetMobility(Mobility);
			Component->SetBoxExtent(Extent);
			Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Component->SetCollisionObjectType(ECC_WorldStatic);
			Component->SetCollisionResponseToAllChannels(ECR_Block);
			Component->SetWorldLocationAndRotation(Center, Rotation);
			Component->RegisterComponent();
			return Component;
		}

		void Floor() { Box(FVector(0., 0., -10.), FVector(1000., 1000., 10.)); }

		UBoxComponent* Wall(const double Height, const double Depth, const double HalfWidth = 150.)
		{
			return Box(FVector(70. + Depth * .5, 0., Height * .5), FVector(Depth * .5, HalfWidth, Height * .5));
		}

		/** 三层水平横条，中间留空；顶条高度 100 cm，不能要求墙面从地面到顶面实心。 */
		void Louvers(const double Pitch = 0.)
		{
			for (const double Height : {55., 75., 95.})
			{
				Box(FVector(90., 0., Height), FVector(20., 150., 5.), EComponentMobility::Static, FRotator(Pitch, 0., 0.));
			}
		}

		/** 两条纵向空缝位于中心与外侧支撑环之间，用来检查稀疏采样是否误认完整脚底。 */
		void SlottedSurface(const double CenterX, const double HalfDepth, const double Top)
		{
			Box(FVector(CenterX, 0., Top - 5.), FVector(HalfDepth, 6., 5.));
			for (const double Side : {-1., 1.})
			{
				Box(FVector(CenterX, Side * 84., Top - 5.), FVector(HalfDepth, 66., 5.));
			}
		}

		FGinkgoTraversalResult Detect() { return Ginkgo::Traversal::Detect(*World, Query, Settings); }
	};
}

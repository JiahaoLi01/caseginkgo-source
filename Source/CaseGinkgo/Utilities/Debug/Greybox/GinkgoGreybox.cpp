// 灰盒测试场景（腿部 IK 与移动用）：运行时用方块拼出来，不存成地图；发行版不编译
#if !UE_BUILD_SHIPPING

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Utilities/Debug/DebugCategory.h"

namespace GinkgoGreybox
{
	/** 场景里的 Actor 都打这个标签：重新生成、清掉时按它找 */
	const FName Tag(TEXT("GinkgoGreybox"));

	/** 默认的位置：平地模板的地板（±20 m）旁边，场景自带地板，顶面 z = 0。测量的路线按这里写绝对的开始位置 */
	const FVector DefaultOrigin(0.0, 3000.0, 0.0);

	/** 五条道沿 +X，各自的 Y（场景内的坐标）；前四条从 X = 0 起跑，3 m 以后才有东西；高台道从平台上起跑 */
	constexpr double StairsLane = 0.0;
	constexpr double StepsLane = 600.0;
	constexpr double SlopesLane = 1200.0;
	constexpr double RoughLane = 1800.0;
	constexpr double LedgeLane = 2400.0;
	constexpr double TargetLane = 3000.0;
	constexpr double LaneWidth = 200.0;

	/** 一块方块：中心、尺寸（cm）与朝向，都在场景内的坐标里 */
	struct FBoxSpec
	{
		FVector Center;
		FVector Size;
		FRotator Rotation = FRotator::ZeroRotator;
	};

	/**
	 * 一段楼梯：上 Count 级、平台 Landing、对称地下 Count 级。第 k 级是一块从这一级的踏面铺到下行那一侧同一级的板，
	 * 板叠起来，碰撞是它们的并
	 */
	void AddStairs(TArray<FBoxSpec>& Boxes, const double X, const double Y, const double Rise, const double Tread, const int32 Count, const double Landing)
	{
		for (int32 Step = 1; Step <= Count; ++Step)
		{
			const double Start = X + (Step - 1) * Tread;
			const double End = X + (2 * Count - Step + 1) * Tread + Landing;
			Boxes.Add({FVector((Start + End) * 0.5, Y, (Step - 0.5) * Rise), FVector(End - Start, LaneWidth, Rise)});
		}
	}

	/** 单级台阶：从 X 起长 Length、高 Height 的一块 */
	void AddBlock(TArray<FBoxSpec>& Boxes, const double X, const double Y, const double Length, const double Height)
	{
		Boxes.Add({FVector(X + Length * 0.5, Y, Height * 0.5), FVector(Length, LaneWidth, Height)});
	}

	/**
	 * 坡：从 X 起以 Angle 度往上、水平走 Run，平台 Plateau，再对称地下来。坡面是转过的厚板，顶面的下沿正好接地面、上沿接平台；
	 * 板厚到上沿下面也是实的
	 */
	void AddRamp(TArray<FBoxSpec>& Boxes, const double X, const double Y, const double Angle, const double Run, const double Plateau)
	{
		const double Radians = FMath::DegreesToRadians(Angle);
		const double Sin = FMath::Sin(Radians);
		const double Cos = FMath::Cos(Radians);
		const double Rise = Run * Sin / Cos;
		const double Length = Run / Cos;
		const double Thickness = Rise + 50.0;
		// 上坡（抬头 Angle）：顶面低端 (X, 0)；下坡（低头 Angle）：顶面高端 (X + Run + Plateau, Rise)
		Boxes.Add({FVector(X + Length * 0.5 * Cos + Thickness * 0.5 * Sin, Y, Length * 0.5 * Sin - Thickness * 0.5 * Cos), FVector(Length, LaneWidth, Thickness),
			FRotator(Angle, 0.0, 0.0)});
		const double Top = X + Run + Plateau;
		Boxes.Add({FVector(Top + Length * 0.5 * Cos - Thickness * 0.5 * Sin, Y, Rise - Length * 0.5 * Sin - Thickness * 0.5 * Cos), FVector(Length, LaneWidth, Thickness),
			FRotator(-Angle, 0.0, 0.0)});
		Boxes.Add({FVector(X + Run + Plateau * 0.5, Y, Rise * 0.5), FVector(Plateau, LaneWidth, Rise)});
	}

	/** 高台：从 X 起长 Length、宽 Width、高 Height 的平台，后面接一根同样高、宽 BeamWidth、长 BeamLength 的窄梁，梁的尽头直接掉到地面 */
	void AddLedge(TArray<FBoxSpec>& Boxes, const double X, const double Y, const double Length, const double Width, const double Height, const double BeamLength,
		const double BeamWidth)
	{
		Boxes.Add({FVector(X + Length * 0.5, Y, Height * 0.5), FVector(Length, Width, Height)});
		Boxes.Add({FVector(X + Length + BeamLength * 0.5, Y, Height * 0.5), FVector(BeamLength, BeamWidth, Height)});
	}

	/** 凹凸不平的地面：从 X 起 Length 长的一片矮块，高 2–10 cm，有的歪 6° 以内；种子固定，每次一样 */
	void AddRough(TArray<FBoxSpec>& Boxes, const double X, const double Y, const double Length)
	{
		FRandomStream Random(20261029);
		for (int32 Index = 0; Index < 48; ++Index)
		{
			const double Height = Random.FRandRange(2.0, 10.0);
			const FVector Size(Random.FRandRange(25.0, 80.0), Random.FRandRange(25.0, 80.0), Height + 20.0);
			const FVector Center(X + Random.FRandRange(0.0, Length), Y + Random.FRandRange(-0.45, 0.45) * LaneWidth, Height - Size.Z * 0.5);
			Boxes.Add({Center, Size, FRotator(Random.FRandRange(-6.0, 6.0), Random.FRandRange(0.0, 90.0), Random.FRandRange(-6.0, 6.0))});
		}
	}

	/** 一面竖着的墙：前面在 X，朝 −X（对着起点），中心偏 Y、宽 Width、高 Height */
	void AddWall(TArray<FBoxSpec>& Boxes, const double X, const double Y, const double Width, const double Height)
	{
		Boxes.Add({FVector(X + 10.0, Y, Height * 0.5), FVector(20.0, Width, Height)});
	}

	/**
	 * 整个场景（场景内的坐标）：
	 * - 楼梯道：3 m 起，15 cm × 30 cm 上 8 级、2 m 平台、下 8 级；平地 3 m；25 cm × 35 cm 上 5 级、2 m 平台、下 5 级
	 * - 单级台阶道：3 m 起，10、20、30 cm 高的三块，各长 2.5 m，之间隔 2 m
	 * - 坡道：3 m 起，10°、20°、30° 的坡，各上 3 m（水平）、平台 1.5 m、下 3 m，之间隔 2 m
	 * - 凹凸道：3 m 起，12 m 长的一片矮块
	 * - 高台道：1 m 高的平台（X −3 m 到 6 m，宽 3 m），接一根 40 cm 宽、6 m 长的窄梁，梁的尽头掉到地面；起点在平台上
	 * - 靶道（持枪第 2 批量枪口对准）：3 m 处偏左 1.5 m、8 m 处正前、25 m 处偏右 6 m 三面墙，自己一块地板（不动别的道的）
	 */
	TArray<FBoxSpec> BuildCourse()
	{
		TArray<FBoxSpec> Boxes;
		Boxes.Add({FVector(1400.0, 1100.0, -25.0), FVector(4000.0, 3000.0, 50.0)});
		AddStairs(Boxes, 300.0, StairsLane, 15.0, 30.0, 8, 200.0);
		AddStairs(Boxes, 1280.0, StairsLane, 25.0, 35.0, 5, 200.0);
		AddBlock(Boxes, 300.0, StepsLane, 250.0, 10.0);
		AddBlock(Boxes, 750.0, StepsLane, 250.0, 20.0);
		AddBlock(Boxes, 1200.0, StepsLane, 250.0, 30.0);
		AddRamp(Boxes, 300.0, SlopesLane, 10.0, 300.0, 150.0);
		AddRamp(Boxes, 1250.0, SlopesLane, 20.0, 300.0, 150.0);
		AddRamp(Boxes, 2200.0, SlopesLane, 30.0, 300.0, 150.0);
		AddRough(Boxes, 300.0, RoughLane, 1200.0);
		AddLedge(Boxes, -300.0, LedgeLane, 900.0, 300.0, 100.0, 600.0, 40.0);
		Boxes.Add({FVector(1400.0, TargetLane + 100.0, -25.0), FVector(4000.0, 1000.0, 50.0)});
		AddWall(Boxes, 300.0, TargetLane - 150.0, 150.0, 300.0);
		AddWall(Boxes, 800.0, TargetLane, 200.0, 300.0);
		AddWall(Boxes, 2500.0, TargetLane + 600.0, 400.0, 400.0);
		return Boxes;
	}

	void Clear(UWorld& World)
	{
		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			if (It->Tags.Contains(Tag))
			{
				It->Destroy();
			}
		}
	}

	/** 按 Origin（场景内坐标到世界的变换）生成；之前生成的先清掉 */
	void Spawn(UWorld& World, const FTransform& Origin)
	{
		Clear(World);
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
		if (!Cube)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[Greybox] 读不到 /Engine/BasicShapes/Cube"));
			return;
		}
		const TArray<FBoxSpec> Boxes = BuildCourse();
		for (const FBoxSpec& Box : Boxes)
		{
			// 引擎的方块边长 100 cm、中心在原点
			const FTransform Transform = FTransform(Box.Rotation, Box.Center, Box.Size / 100.0) * Origin;
			AStaticMeshActor* Actor = World.SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Actor->Tags.Add(Tag);
			UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Cube);
			Mesh->SetMaterial(0, Material);
			Actor->FinishSpawning(Transform);
		}
		const auto LaneStart = [&Origin](const double Lane) { return Origin.TransformPosition(FVector(0.0, Lane, 0.0)); };
		const FVector Stairs = LaneStart(StairsLane);
		const FVector Steps = LaneStart(StepsLane);
		const FVector Slopes = LaneStart(SlopesLane);
		const FVector Rough = LaneStart(RoughLane);
		const FVector Ledge = Origin.TransformPosition(FVector(0.0, LedgeLane, 100.0));
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[Greybox] 生成了 %d 块，沿 (%.0f, %.0f) 方向；各条道的起点（地面）：楼梯 (%.0f, %.0f, %.0f)、单级台阶 (%.0f, %.0f, %.0f)、")
			TEXT("坡 (%.0f, %.0f, %.0f)、凹凸 (%.0f, %.0f, %.0f)、高台（平台上）(%.0f, %.0f, %.0f)"), Boxes.Num(), Origin.GetUnitAxis(EAxis::X).X,
			Origin.GetUnitAxis(EAxis::X).Y, Stairs.X, Stairs.Y, Stairs.Z, Steps.X, Steps.Y, Steps.Z, Slopes.X, Slopes.Y, Slopes.Z, Rough.X, Rough.Y, Rough.Z,
			Ledge.X, Ledge.Y, Ledge.Z);
	}

	FAutoConsoleCommandWithWorldAndArgs CCmdGreybox(
		TEXT("ginkgo.greybox"),
		TEXT("灰盒测试场景（腿部 IK、移动与枪口对准用）：ginkgo.greybox [here] [clear]。六条并排的道沿 +X：楼梯（15 cm、25 cm 高）、单级台阶（10、20、30 cm）、")
		TEXT("坡（10°、20°、30°）、凹凸不平的地面、1 m 高的平台接窄梁（梁的尽头掉下去）、靶道（3 m 偏左、8 m 正前、25 m 偏右三面墙）；")
		TEXT("前四条起跑 3 m，场景自带地板。\n")
		TEXT("默认放在世界 (0, 3000, 0)（平地模板的地板旁边，测量的路线从这里开始）；here：放在第一个玩家前方 3 m，楼梯道对着玩家；clear：清掉。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			if (Args.Contains(TEXT("clear")))
			{
				Clear(*World);
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[Greybox] 清掉了"));
				return;
			}
			FTransform Origin(DefaultOrigin);
			if (Args.Contains(TEXT("here")))
			{
				const APlayerController* Controller = World->GetFirstPlayerController();
				const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
				if (!Pawn)
				{
					UE_LOG(LogGinkgoAnimation, Warning, TEXT("[Greybox] here：还没有玩家的角色"));
					return;
				}
				// 原点放在角色脚下（胶囊底）、朝角色水平的面向：楼梯道从脚下起跑，3 m 后是第一级
				const FRotator Yaw(0.0, Pawn->GetActorRotation().Yaw, 0.0);
				const FVector Feet = Pawn->GetActorLocation() - FVector(0.0, 0.0, Pawn->GetSimpleCollisionHalfHeight());
				Origin = FTransform(Yaw, Feet);
			}
			Spawn(*World, Origin);
		}));
}

#endif

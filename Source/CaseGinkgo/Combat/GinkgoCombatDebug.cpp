// 伤害的调试命令（Docs/Design/Damage.md「调试」「穿射」）：发行版不编译
#if !UE_BUILD_SHIPPING

#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Combat/GinkgoPenetrableComponent.h"
#include "Combat/GinkgoPracticeTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Framework/GinkgoDeveloperSettings.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Parse.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Weapon/GinkgoWeaponComponent.h"

namespace GinkgoCombatDebug
{
	/** 一次记下：等第一个玩家的角色出现，再等 Delay 秒（世界时间） */
	struct FAimLog
	{
		TWeakObjectPtr<UWorld> World;
		double Delay = 0.0;
		/** 第一次见到角色的世界时间；还没见到时为负 */
		double PawnSeenAt = -1.0;
	};

	/**
	 * 相机中心的线穿过 Point 要的视角：相机随视角绕跟随点转（肩后的偏移），所以按推算的相机位置迭代几次
	 * @return 视角；OutFrom 是那个视角下相机在哪
	 */
	FRotator SolveAim(const AGinkgoPlayerCameraManager& Camera, const APawn& Pawn, const FVector& Point, FVector& OutFrom)
	{
		FRotator Rotation = (Point - Camera.PredictViewLocation(Pawn, Pawn.GetControlRotation())).Rotation();
		for (int32 Iteration = 0; Iteration < 12; ++Iteration)
		{
			OutFrom = Camera.PredictViewLocation(Pawn, Rotation);
			const FRotator Next = (Point - OutFrom).Rotation();
			const bool bSettled = Next.Equals(Rotation, 1e-5);
			Rotation = Next;
			if (bSettled)
			{
				break;
			}
		}
		return Rotation;
	}

	/** 每个靶子每块物理形体：中心在哪、算哪个部位、相机中心对准它要的视角 */
	void LogAim(UWorld& World, const APawn& Pawn, const AGinkgoPlayerCameraManager& Camera)
	{
		int32 Lines = 0;
		for (TActorIterator<AGinkgoPracticeTarget> It(&World); It; ++It)
		{
			USkeletalMeshComponent* Mesh = It->GetMesh();
			const UPhysicsAsset* Physics = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
			if (!Physics)
			{
				continue;
			}
			for (const USkeletalBodySetup* Setup : Physics->SkeletalBodySetups)
			{
				const FName Bone = Setup ? Setup->BoneName : NAME_None;
				const FBodyInstance* Body = Bone.IsNone() ? nullptr : Mesh->GetBodyInstance(Bone);
				if (!Body || !Body->IsValidBodyInstance())
				{
					continue;
				}
				// 形体的包围盒中心：胶囊、球都在骨骼段的中间，不在关节上
				const FVector Point = Body->GetBodyBounds().GetCenter();
				FHitResult Hit;
				Hit.Component = Mesh;
				Hit.BoneName = Bone;
				const EGinkgoBodyPart Part = It->GetDamageable()->FindBodyPart(Hit);
				FVector From;
				const FRotator Aim = SolveAim(Camera, Pawn, Point, From);
				UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 瞄准 %s %s %s：pitch=%.4f yaw=%.4f dist=%.1f point=(%.2f,%.2f,%.2f)"), *It->GetName(), *Bone.ToString(),
					Ginkgo::Combat::DescribeBodyPart(Part), Aim.Pitch, Aim.Yaw, FVector::Dist(From, Point), Point.X, Point.Y, Point.Z);
				++Lines;
			}
		}
		UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] ginkgo.combat.aimLog：%d 块形体，世界时间 %.4f s"), Lines, World.GetTimeSeconds());
	}

	/** @return 还要不要继续等 */
	bool Tick(FAimLog& Request)
	{
		UWorld* World = Request.World.Get();
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		const AGinkgoPlayerCameraManager* Camera = Controller ? Cast<AGinkgoPlayerCameraManager>(Controller->PlayerCameraManager) : nullptr;
		if (!World || !Pawn || !Camera)
		{
			return World != nullptr;
		}
		const double Now = World->GetTimeSeconds();
		if (Request.PawnSeenAt < 0.0)
		{
			Request.PawnSeenAt = Now;
		}
		if (Now - Request.PawnSeenAt < Request.Delay)
		{
			return true;
		}
		LogAim(*World, *Pawn, *Camera);
		return false;
	}

	FAutoConsoleCommandWithWorldAndArgs AimLogCommand(
		TEXT("ginkgo.combat.aimLog"),
		TEXT("ginkgo.combat.aimLog [<秒>]：角色出现后再等这么久（世界时间），把每个人形靶每块物理形体的中心、部位与相机中心对准它要的视角（pitch、yaw）写进日志。")
		TEXT("测量的路线（Scripts/Measurements/combat.py）照着瞄：要在开枪时的姿态下记（开镜与否相机不一样）"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			TSharedRef<FAimLog> Request = MakeShared<FAimLog>();
			Request->World = World;
			if (!Args.IsEmpty())
			{
				Request->Delay = FCString::Atod(*Args[0]);
			}
			// 角色还没出来（命令行里的 -ExecCmds）或要等一会儿时，每帧看一次
			if (Tick(*Request))
			{
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Request](float)
				{
					return Tick(*Request);
				}));
			}
		}));

	/** 试听命中的声音：等角色出现，按世界时间一声一声放 */
	struct FHitSoundTest
	{
		TWeakObjectPtr<UWorld> World;
		/** 离命令多久放（世界时间，s）、放哪种 */
		TArray<TPair<double, EGinkgoHitSound>> Plays;
		int32 Next = 0;
		/** 命令那一刻的世界时间 */
		double StartedAt = 0.0;
	};

	/** @return 还要不要继续 */
	bool Tick(FHitSoundTest& Test);

	/** 排好了就开始：每帧看一次到时候的 */
	void StartHitSoundTest(const TSharedRef<FHitSoundTest>& Test)
	{
		UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 试听命中的声音：%d 声，世界时间 %.4f s"), Test->Plays.Num(), Test->StartedAt);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float)
		{
			return Tick(*Test);
		}));
	}

	bool Tick(FHitSoundTest& Test)
	{
		UWorld* World = Test.World.Get();
		if (!World)
		{
			return false;
		}
		const APlayerController* Controller = World->GetFirstPlayerController();
		const AGinkgoCharacter* Character = Controller ? Cast<AGinkgoCharacter>(Controller->GetPawn()) : nullptr;
		UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
		if (!Weapons)
		{
			return true;
		}
		const double Elapsed = World->GetTimeSeconds() - Test.StartedAt;
		while (Test.Next < Test.Plays.Num() && Test.Plays[Test.Next].Key <= Elapsed)
		{
			const EGinkgoHitSound Kind = Test.Plays[Test.Next].Value;
			const bool bPlayed = Weapons->PlayHitSound(Kind);
			++Test.Next;
			// 音量是设置里这一种的（你调过）：录音的测量按它换算该多响
			const FGinkgoHitSound* Item = UGinkgoDeveloperSettings::Get()->HitSounds.Get(Kind);
			UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 试听命中的声音 %d/%d：%s，音量 %.2f，离命令 %.4f s%s"), Test.Next, Test.Plays.Num(), Ginkgo::Combat::DescribeHitSound(Kind),
				Item ? Item->Volume : 0.f, Elapsed, bPlayed ? TEXT("") : TEXT("（没放）"));
		}
		return Test.Next < Test.Plays.Num();
	}

	FAutoConsoleCommandWithWorldAndArgs HitSoundTestCommand(
		TEXT("ginkgo.combat.hitSoundTest"),
		TEXT("ginkgo.combat.hitSoundTest [<每种几次> [<间隔秒> [<连发几下> [<连发间隔秒> [<开始秒>]]]]]：试听命中的声音（Docs/Design/Damage.md「命中的声音」）。")
		TEXT("离命令 0.5 s（开始秒）起，命中、打在护甲上、打穿护甲、爆头、打死、爆头打死依次各放几次（默认 6 次，隔 1.6 s），最后按连发放一串命中（默认 10 下，隔 0.05 s：MG3 的射速），")
		TEXT("同开枪打中时一样走武器组件（两条队列）。每一声写一行日志，录音的测量（weapon_audio.py --hits）照着对"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			const int32 Each = Args.IsValidIndex(0) ? FMath::Max(FCString::Atoi(*Args[0]), 0) : 6;
			const double Interval = Args.IsValidIndex(1) ? FMath::Max(FCString::Atod(*Args[1]), 0.01) : 1.6;
			const int32 Burst = Args.IsValidIndex(2) ? FMath::Max(FCString::Atoi(*Args[2]), 0) : 10;
			const double BurstInterval = Args.IsValidIndex(3) ? FMath::Max(FCString::Atod(*Args[3]), 0.01) : 0.05;
			const double Start = Args.IsValidIndex(4) ? FMath::Max(FCString::Atod(*Args[4]), 0.0) : 0.5;
			TSharedRef<FHitSoundTest> Test = MakeShared<FHitSoundTest>();
			Test->World = World;
			Test->StartedAt = World->GetTimeSeconds();
			double At = Start;
			for (const EGinkgoHitSound Kind : {EGinkgoHitSound::Normal, EGinkgoHitSound::Armor, EGinkgoHitSound::ArmorBreak, EGinkgoHitSound::Head, EGinkgoHitSound::Kill,
				EGinkgoHitSound::HeadKill})
			{
				for (int32 Index = 0; Index < Each; ++Index)
				{
					Test->Plays.Emplace(At, Kind);
					At += Interval;
				}
			}
			for (int32 Index = 0; Index < Burst; ++Index)
			{
				Test->Plays.Emplace(At + Index * BurstInterval, EGinkgoHitSound::Normal);
			}
			StartHitSoundTest(Test);
		}));

	FAutoConsoleCommandWithWorldAndArgs HitSoundSequenceCommand(
		TEXT("ginkgo.combat.hitSoundSeq"),
		TEXT("ginkgo.combat.hitSoundSeq <秒>:<种类> [<秒>:<种类> ...]：离命令这么多秒放这一种（Normal、Armor、ArmorBreak、Head、Kill、HeadKill），同开枪打中时一样走武器组件，")
		TEXT("试命中的声音的两条队列（Docs/Design/Damage.md「命中的声音」）。例：ginkgo.combat.hitSoundSeq 0:ArmorBreak 0.1:Normal 0.2:Normal"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			TSharedRef<FHitSoundTest> Test = MakeShared<FHitSoundTest>();
			Test->World = World;
			Test->StartedAt = World->GetTimeSeconds();
			for (const FString& Arg : Args)
			{
				FString At, Name;
				const int64 Kind = Arg.Split(TEXT(":"), &At, &Name) ? StaticEnum<EGinkgoHitSound>()->GetValueByNameString(Name) : INDEX_NONE;
				if (Kind == INDEX_NONE)
				{
					UE_LOG(LogGinkgoCombat, Warning, TEXT("[Combat] 认不出 %s（要 <秒>:<种类>）"), *Arg);
					return;
				}
				Test->Plays.Emplace(FCString::Atod(*At), static_cast<EGinkgoHitSound>(Kind));
			}
			Test->Plays.StableSort([](const TPair<double, EGinkgoHitSound>& A, const TPair<double, EGinkgoHitSound>& B)
			{
				return A.Key < B.Key;
			});
			StartHitSoundTest(Test);
		}));

	// ===== 穿射的场地（Docs/Design/Damage.md「穿射」） =====

	/** 等第一个玩家的角色出现再做（命令行里的 -ExecCmds 跑的时候角色还没出来）；已经在了就当场做 */
	void WhenCharacterReady(UWorld& World, TFunction<void(AGinkgoCharacter&)> Action)
	{
		// 返回还要不要等
		auto TryRun = [WeakWorld = TWeakObjectPtr<UWorld>(&World), Action = MoveTemp(Action)]()
		{
			UWorld* Current = WeakWorld.Get();
			const APlayerController* Controller = Current ? Current->GetFirstPlayerController() : nullptr;
			AGinkgoCharacter* Character = Controller ? Cast<AGinkgoCharacter>(Controller->GetPawn()) : nullptr;
			if (Character)
			{
				Action(*Character);
			}
			return Current && !Character;
		};
		if (TryRun())
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([TryRun = MoveTemp(TryRun)](float) mutable
			{
				return TryRun();
			}));
		}
	}

	/**
	 * 在角色面前立一面子弹穿得过的墙：引擎的立方体（挡子弹、挡人、挡相机），挂 UGinkgoPenetrableComponent。前、右按角色水平的朝向，薄的一面朝角色，底在地上
	 * @param Depth - 墙真的有多厚（cm，只是样子；子弹减多少看 Thickness）
	 */
	void SpawnPenetrableWall(AGinkgoCharacter& Character, const double Forward, const double Right, const float Thickness, const double Width, const double Height,
		const double Depth)
	{
		UWorld* World = Character.GetWorld();
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!World || !Cube)
		{
			return;
		}
		const FVector Ahead = Character.GetActorForwardVector().GetSafeNormal2D();
		const FVector Side = Character.GetActorRightVector().GetSafeNormal2D();
		const double HalfHeight = Character.GetSimpleCollisionHalfHeight();
		const FVector Above = Character.GetActorLocation() + Ahead * Forward + Side * Right;
		FHitResult Ground;
		const bool bGround = World->LineTraceSingleByChannel(Ground, Above + FVector(0.0, 0.0, HalfHeight), Above - FVector(0.0, 0.0, HalfHeight + 500.0), ECC_Visibility,
			FCollisionQueryParams(SCENE_QUERY_STAT(GinkgoPenetrableWall), false, &Character));
		const FVector Bottom = bGround ? FVector(Ground.ImpactPoint) : Above - FVector(0.0, 0.0, HalfHeight);
		// 立方体边长 100 cm、中心在中间
		const FTransform Transform((-Ahead).Rotation(), Bottom + FVector(0.0, 0.0, Height * 0.5), FVector(Depth, Width, Height) / 100.0);
		// 按这个世界里第几面起名（日志里穿射的那一行写它）
		int32 Index = 0;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			Index += It->FindComponentByClass<UGinkgoPenetrableComponent>() ? 1 : 0;
		}
		FActorSpawnParameters Params;
		Params.Name = *FString::Printf(TEXT("PenetrableWall%d"), Index);
		Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Wall = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, Params);
		if (!Wall)
		{
			return;
		}
		// 运行时换网格要能动的
		Wall->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* Mesh = Wall->GetStaticMeshComponent();
		Mesh->SetStaticMesh(Cube);
		Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		UGinkgoPenetrableComponent* Penetrable = NewObject<UGinkgoPenetrableComponent>(Wall, TEXT("Penetrable"));
		Penetrable->Thickness = Thickness;
		Wall->AddInstanceComponent(Penetrable);
		Penetrable->RegisterComponent();
		const FVector Location = Wall->GetActorLocation();
		UE_LOG(LogGinkgoCombat, Log, TEXT("[Combat] 可穿射的墙 %s：(%.1f, %.1f, %.1f)，厚度 %.4f，宽 %.0f 高 %.0f 厚 %.0f cm"), *Wall->GetName(), Location.X, Location.Y, Location.Z,
			UGinkgoPenetrableComponent::FindThickness(Wall), Width, Height, Depth);
	}

	FAutoConsoleCommandWithWorldAndArgs SpawnWallCommand(
		TEXT("ginkgo.combat.spawnWall"),
		TEXT("ginkgo.combat.spawnWall <前 cm> <右 cm> [<厚度> [<宽 cm> [<高 cm> [<真的厚 cm>]]]]：在角色面前立一面子弹穿得过的墙（Docs/Design/Damage.md「穿射」）：")
		TEXT("引擎的立方体，挂 UGinkgoPenetrableComponent，厚度默认 0.25（木板墙），默认宽 150、高 250、厚 10 cm。角色还没出来时等它出来。试穿射、测量的路线用"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() < 2)
			{
				UE_LOG(LogGinkgoCombat, Warning, TEXT("[Combat] 要 ginkgo.combat.spawnWall <前 cm> <右 cm> [<厚度> [<宽 cm> [<高 cm> [<真的厚 cm>]]]]"));
				return;
			}
			const double Forward = FCString::Atod(*Args[0]);
			const double Right = FCString::Atod(*Args[1]);
			const float Thickness = Args.IsValidIndex(2) ? FMath::Max(FCString::Atof(*Args[2]), 0.f) : 0.25f;
			const double Width = Args.IsValidIndex(3) ? FMath::Max(FCString::Atod(*Args[3]), 1.0) : 150.0;
			const double Height = Args.IsValidIndex(4) ? FMath::Max(FCString::Atod(*Args[4]), 1.0) : 250.0;
			const double Depth = Args.IsValidIndex(5) ? FMath::Max(FCString::Atod(*Args[5]), 0.1) : 10.0;
			WhenCharacterReady(*World, [=](AGinkgoCharacter& Character)
			{
				SpawnPenetrableWall(Character, Forward, Right, Thickness, Width, Height, Depth);
			});
		}));

	FAutoConsoleCommandWithWorldAndArgs SpawnTargetCommand(
		TEXT("ginkgo.combat.spawnTarget"),
		TEXT("ginkgo.combat.spawnTarget <名字> <前 cm> <右 cm> [vest] [helmet]：在角色面前多放一个人形靶（同开局的一排一样：脚踩在地上、面朝角色），")
		TEXT("可以穿背心、戴头盔。角色还没出来时等它出来。试穿射（一列站在一条线上）、测量的路线用"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.Num() < 3)
			{
				UE_LOG(LogGinkgoCombat, Warning, TEXT("[Combat] 要 ginkgo.combat.spawnTarget <名字> <前 cm> <右 cm> [vest] [helmet]"));
				return;
			}
			const FName Name(*Args[0]);
			const FGinkgoTargetSpawn Spawn(FCString::Atof(*Args[1]), FCString::Atof(*Args[2]), Args.Contains(TEXT("vest")), Args.Contains(TEXT("helmet")));
			WhenCharacterReady(*World, [Name, Spawn](AGinkgoCharacter& Character)
			{
				Character.SpawnPracticeTarget(Name, Spawn);
			});
		}));
}

#endif // !UE_BUILD_SHIPPING

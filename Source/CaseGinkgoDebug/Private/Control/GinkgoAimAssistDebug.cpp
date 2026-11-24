#include "Combat/GinkgoCombatTypes.h"
#include "Combat/GinkgoPracticeTarget.h"
#include "Containers/Ticker.h"
#include "Control/GinkgoPlayerController.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Utilities/Debug/DebugCategory.h"

/**
 * 辅助瞄准的调试（发行版没有这个模块，Docs/Design/Control.md「辅助瞄准」）：
 * - ginkgo.aimAssist.show [0|1]：画出选中的目标的中线、满强度与淡到零的范围，屏幕左上写设备、强度、修正
 * - ginkgo.aimAssist.target clear | add <前 cm> <右 cm> [左右来回的速度 cm/s] [来回的范围 cm]：去掉所有人形靶；在面前放一个，可以左右来回走（测量的路线用）
 */
namespace
{
	FTSTicker::FDelegateHandle ShowHandle;

	/** 左右来回走的靶子：在 Origin 两边各 Range / 2，匀速 */
	struct FMovingTarget
	{
		TWeakObjectPtr<AActor> Actor;
		FVector Origin = FVector::ZeroVector;
		FVector Axis = FVector::RightVector;
		double Speed = 0.0;
		double Range = 0.0;
		double Start = 0.0;
	};

	TArray<FMovingTarget> MovingTargets;
	FTSTicker::FDelegateHandle MoveHandle;

	UWorld* GameWorld(UWorld* World)
	{
		return World ? World : (GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr);
	}

	bool DrawAimAssist(float)
	{
		UWorld* World = GameWorld(nullptr);
		const AGinkgoPlayerController* Controller = World ? Cast<AGinkgoPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Controller || !Controller->PlayerCameraManager)
		{
			return true;
		}
		const FGinkgoAimAssistFrame& Frame = Controller->GetAimAssist().GetFrame();
		const TCHAR* Device = Frame.Device == EGinkgoInputDirectory::Gamepad ? TEXT("手柄") : TEXT("键鼠");
		if (!Frame.bEnabled)
		{
			GEngine->AddOnScreenDebugMessage(0x41A1, 0.f, FColor::Silver, FString::Printf(TEXT("辅助瞄准（%s）：总开关关着"), Device));
			return true;
		}
		if (!Frame.Target.IsValid())
		{
			GEngine->AddOnScreenDebugMessage(0x41A1, 0.f, FColor::Silver, FString::Printf(TEXT("辅助瞄准（%s）：没有目标"), Device));
			return true;
		}
		const FVector Camera = Controller->PlayerCameraManager->GetCameraLocation();
		FVector Y, Z;
		(Frame.Nearest - Camera).GetSafeNormal().FindBestAxisVectors(Y, Z);
		const auto ToCm = [&Frame](const float Degrees) { return FMath::Tan(FMath::DegreesToRadians(Degrees)) * Frame.Distance; };
		const FColor Color = Frame.Correction.IsNearlyZero(1.e-5) ? FColor::Yellow : FColor::Green;
		DrawDebugLine(World, Frame.SegmentA, Frame.SegmentB, Color, false, -1.f, SDPG_Foreground, 1.5f);
		DrawDebugPoint(World, Frame.Nearest, 6.f, Color, false, -1.f, SDPG_Foreground);
		DrawDebugCircle(World, Frame.Nearest, ToCm(Frame.InnerRadius), 32, Color, false, -1.f, SDPG_Foreground, 1.f, Y, Z, false);
		DrawDebugCircle(World, Frame.Nearest, ToCm(Frame.OuterRadius), 48, FColor(Color.R, Color.G, Color.B, 96), false, -1.f, SDPG_Foreground, 0.5f, Y, Z, false);
		GEngine->AddOnScreenDebugMessage(0x41A1, 0.f, Color, FString::Printf(
			TEXT("辅助瞄准（%s）：%s %.1f m，离中线 %.2f°（满 %.2f°、到零 %.2f°），强度 %.2f / %.2f，修正 %+.4f° %+.4f°，目标 %+.1f %+.1f °/s%s"),
			Device, *Frame.Target->GetName(), Frame.Distance / 100.f, Frame.Offset.Size(), Frame.InnerRadius, Frame.OuterRadius, Frame.K, Frame.Strength,
			Frame.Correction.Yaw, Frame.Correction.Pitch, Frame.TargetVelocity.X, Frame.TargetVelocity.Y, Frame.bHasInput ? TEXT("") : TEXT("，没有输入")));
		return true;
	}

	bool MoveTargets(float)
	{
		MovingTargets.RemoveAll([](const FMovingTarget& Moving) { return !Moving.Actor.IsValid(); });
		for (const FMovingTarget& Moving : MovingTargets)
		{
			AActor* Actor = Moving.Actor.Get();
			const double Travel = FMath::Fmod((Actor->GetWorld()->GetTimeSeconds() - Moving.Start) * Moving.Speed, 2.0 * Moving.Range);
			const double Along = (Travel <= Moving.Range ? Travel : 2.0 * Moving.Range - Travel) - 0.5 * Moving.Range;
			Actor->SetActorLocation(Moving.Origin + Moving.Axis * Along);
		}
		if (MovingTargets.IsEmpty())
		{
			MoveHandle.Reset();
			return false;
		}
		return true;
	}

	void ClearTargets(UWorld& World)
	{
		for (TActorIterator<AGinkgoPracticeTarget> It(&World); It; ++It)
		{
			It->Destroy();
		}
		MovingTargets.Reset();
	}

	void AddTarget(UWorld& World, const double Forward, const double Right, const double Speed, const double Range)
	{
		const APlayerController* Controller = World.GetFirstPlayerController();
		const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogGinkgoControl, Warning, TEXT("[AimAssist] ginkgo.aimAssist.target：没有角色"));
			return;
		}
		const FRotator Yaw(0.0, Controller->GetControlRotation().Yaw, 0.0);
		const FVector ForwardAxis = Yaw.Vector();
		const FVector RightAxis = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
		const FVector Above = Pawn->GetActorLocation() + ForwardAxis * Forward + RightAxis * Right;
		FHitResult Hit;
		const bool bHit = World.LineTraceSingleByChannel(Hit, Above + FVector(0.0, 0.0, 300.0), Above - FVector(0.0, 0.0, 800.0), ECC_Visibility,
			FCollisionQueryParams(SCENE_QUERY_STAT(GinkgoAimAssistTarget), false, Pawn));
		const FVector Ground = bHit ? FVector(Hit.ImpactPoint) : Above - FVector(0.0, 0.0, 90.0);
		// 同开局的人形靶：胶囊中心在地上 90 cm，面朝角色
		const FTransform Transform((-ForwardAxis).Rotation(), Ground + FVector(0.0, 0.0, 90.5));
		FActorSpawnParameters Params;
		Params.Name = TEXT("AimAssistTarget");
		Params.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.bDeferConstruction = true;
		AGinkgoPracticeTarget* Target = World.SpawnActor<AGinkgoPracticeTarget>(AGinkgoPracticeTarget::StaticClass(), Transform, Params);
		if (!Target)
		{
			return;
		}
		Target->Configure(FGinkgoTargetSpawn(Forward, Right, false, false));
		Target->FinishSpawning(Transform);
		UE_LOG(LogGinkgoControl, Log, TEXT("[AimAssist] 放了 %s：前 %.0f cm、右 %.0f cm%s"), *Target->GetName(), Forward, Right,
			Speed > 0.0 ? *FString::Printf(TEXT("，左右来回 %.0f cm/s、范围 %.0f cm"), Speed, Range) : TEXT(""));
		if (Speed > 0.0 && Range > 0.0)
		{
			MovingTargets.Add({Target, Transform.GetLocation(), RightAxis, Speed, Range, World.GetTimeSeconds()});
			if (!MoveHandle.IsValid())
			{
				MoveHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&MoveTargets));
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CommandShow(
		TEXT("ginkgo.aimAssist.show"),
		TEXT("辅助瞄准的调试：ginkgo.aimAssist.show [0|1] 画出选中的目标的中线、满强度与淡到零的范围，屏幕左上写设备、强度、修正"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const bool bShow = Args.IsEmpty() ? !ShowHandle.IsValid() : FCString::Atoi(*Args[0]) != 0;
			if (bShow && !ShowHandle.IsValid())
			{
				ShowHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&DrawAimAssist));
			}
			else if (!bShow && ShowHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(ShowHandle);
				ShowHandle.Reset();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs CommandTarget(
		TEXT("ginkgo.aimAssist.target"),
		TEXT("辅助瞄准的测试靶：ginkgo.aimAssist.target clear 去掉所有人形靶；ginkgo.aimAssist.target add <前 cm> <右 cm> [左右来回的速度 cm/s] [来回的范围 cm] 在面前放一个"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
		{
			UWorld* World = GameWorld(InWorld);
			if (!World || Args.IsEmpty())
			{
				return;
			}
			if (Args[0] == TEXT("clear"))
			{
				ClearTargets(*World);
			}
			else if (Args[0] == TEXT("add") && Args.Num() >= 3)
			{
				AddTarget(*World, FCString::Atod(*Args[1]), FCString::Atod(*Args[2]), Args.Num() > 3 ? FCString::Atod(*Args[3]) : 0.0, Args.Num() > 4 ? FCString::Atod(*Args[4]) : 0.0);
			}
		}));
}

#include "Character/GinkgoCharacter.h"
#include "Containers/Ticker.h"
#include "Control/GinkgoPlayerController.h"      // 辅助瞄准这一帧的样子
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Weapon/GinkgoWeaponComponent.h"

/**
 * 测量用的输入（发行版没有这个模块）：把"真的设备给的量"直接交给本地玩家的 PlayerInput（APlayerController::InputKey，同视口交给它的那一步），
 * 之后走 Enhanced Input 的映射与修改器、处理函数与 PlayerTick，同真的鼠标与手柄一样；不碰系统的鼠标，也不要游戏窗口有焦点。
 * 每帧记下视角与移动（CSV），Scripts/Measurements/input_settings.py 按它算同样的输入在不同设置下转了多少。
 *
 * 排程照世界时间（-ExecCmds 一次排好一串）；固定帧长（-benchmark -fps=N）下每次跑到的帧都一样。
 */
namespace
{
	/** 正在给的一路模拟量 */
	struct FFeedStream
	{
		enum class EKind : uint8 { Mouse, LeftStick, RightStick };
		EKind Kind = EKind::Mouse;
		FVector2D Value = FVector2D::ZeroVector;
		double EndTime = 0.0;
	};

	struct FFeedCommand
	{
		double At = 0.0;
		TArray<FString> Args;
	};

	TArray<FFeedCommand> FeedCommands;
	TArray<FFeedStream> FeedStreams;
	FString FeedCsvPath;
	FString FeedCsv;
	FDelegateHandle FeedPreTickHandle;
	FDelegateHandle FeedPostTickHandle;

	APlayerController* FeedController(const UWorld* World)
	{
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	void FeedAxis(APlayerController& Controller, const FKey& Key, const float Delta, const float DeltaSeconds)
	{
		// 工程没开 bFilterInputByPlatformUser，设备号不看
		const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(0);
		FInputKeyEventArgs Args(nullptr, Device, Key, Delta, DeltaSeconds, 1, FPlatformTime::Cycles64());
		Controller.InputKey(Args);
	}

	void FeedButton(APlayerController& Controller, const FKey& Key, const EInputEvent Event)
	{
		// 工程没开 bFilterInputByPlatformUser，设备号不看
		const FInputDeviceId Device = FInputDeviceId::CreateFromInternalId(0);
		FInputKeyEventArgs Args(nullptr, Device, Key, Event, FPlatformTime::Cycles64());
		Controller.InputKey(Args);
	}

	FString FeedLine(const UWorld& World)
	{
		const APlayerController* Controller = FeedController(&World);
		const FRotator View = Controller ? Controller->GetControlRotation() : FRotator::ZeroRotator;
		const AGinkgoCharacter* Character = Controller ? Cast<AGinkgoCharacter>(Controller->GetPawn()) : nullptr;
		const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		const FVector Acceleration = Movement ? Movement->GetCurrentAcceleration() : FVector::ZeroVector;
		const FVector Velocity = Movement ? Movement->Velocity : FVector::ZeroVector;
		const UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr;
		const float MaxAcceleration = Movement ? Movement->GetMaxAcceleration() : 1.f;
		const UGinkgoCharacterMovementComponent* GinkgoMovement = Cast<UGinkgoCharacterMovementComponent>(Movement);
		// 辅助瞄准（Docs/Design/Control.md「辅助瞄准」）：这一帧选中的目标、准星离它的中线多远（这一帧开始时）、强度、修正、目标离相机多远（cm）、总开关
		const AGinkgoPlayerController* GinkgoController = Cast<AGinkgoPlayerController>(Controller);
		const FGinkgoAimAssistFrame Assist = GinkgoController ? GinkgoController->GetAimAssist().GetFrame() : FGinkgoAimAssistFrame();
		return FString::Printf(TEXT("%.6f,%.6f,%.7f,%.7f,%.5f,%.5f,%.4f,%.4f,%.5f,%d,%d,%.4f,%d,%d,%d,%d,%.6f,%.6f,%.5f,%.5f,%.5f,%.5f,%.7f,%.7f,%.5f,%.5f,%.5f,%.5f,%.2f,%d"),
			World.GetTimeSeconds(), World.GetDeltaSeconds(), View.Pitch, View.Yaw,
			Acceleration.X / FMath::Max(MaxAcceleration, 1.f), Acceleration.Y / FMath::Max(MaxAcceleration, 1.f), Velocity.X, Velocity.Y,
			Weapons ? Weapons->GetHoldState().GetAdsAlpha() : 0.f, Character && Character->bIsCrouched ? 1 : 0,
			Movement && Movement->MaxWalkSpeed > 0.f ? FMath::RoundToInt(Movement->GetMaxSpeed()) : 0,
			Velocity.Z, GinkgoMovement ? GinkgoMovement->GetPackedDirectionKeys() : 0,
			Assist.Device == EGinkgoInputDirectory::Gamepad ? 0 : 1, Assist.Target.IsValid() ? 1 : 0, Assist.bHasInput ? 1 : 0,
			Assist.Offset.X, Assist.Offset.Y, Assist.InnerRadius, Assist.OuterRadius, Assist.Strength, Assist.K,
			Assist.Correction.Yaw, Assist.Correction.Pitch, Assist.TargetVelocity.X, Assist.TargetVelocity.Y, Assist.ScreenX.X, Assist.ScreenY.Y, Assist.Distance, Assist.bEnabled ? 1 : 0);
	}

	void FeedRun(const TArray<FString>& Args, UWorld& World)
	{
		APlayerController* Controller = FeedController(&World);
		const FString Action = Args.Num() > 0 ? Args[0] : FString();
		const double Now = World.GetTimeSeconds();
		if (Action == TEXT("mouse") && Args.Num() >= 4)
		{
			FeedStreams.Add({FFeedStream::EKind::Mouse, FVector2D(FCString::Atod(*Args[1]), FCString::Atod(*Args[2])), Now + FCString::Atod(*Args[3])});
		}
		else if (Action == TEXT("stick") && Args.Num() >= 5)
		{
			FeedStreams.Add({Args[1] == TEXT("L") ? FFeedStream::EKind::LeftStick : FFeedStream::EKind::RightStick, FVector2D(FCString::Atod(*Args[2]), FCString::Atod(*Args[3])), Now + FCString::Atod(*Args[4])});
		}
		else if ((Action == TEXT("press") || Action == TEXT("release") || Action == TEXT("repeat")) && Args.Num() >= 2 && Controller)
		{
			FeedButton(*Controller, FKey(*Args[1]), Action == TEXT("press") ? IE_Pressed : Action == TEXT("repeat") ? IE_Repeat : IE_Released);
		}
		else if (Action == TEXT("mark") && Args.Num() >= 2)
		{
			UE_LOG(LogGinkgoControl, Log, TEXT("[Feed] %s：%s"), *Args[1], *FeedLine(World));
		}
		else if (Action == TEXT("record") && Args.Num() >= 2)
		{
			FeedCsvPath = Args[1];
			FeedCsv = TEXT("time,dt,pitch,yaw,accel_x,accel_y,vel_x,vel_y,ads,crouched,max_speed,vel_z,dir_keys,")
				TEXT("aa_mouse,aa_target,aa_input,aa_off_x,aa_off_y,aa_inner,aa_outer,aa_strength,aa_k,aa_corr_yaw,aa_corr_pitch,aa_tvel_x,aa_tvel_y,aa_screen_x,aa_screen_y,aa_dist,aa_on\n");
		}
		else if (Action == TEXT("stop") || Action == TEXT("quit"))
		{
			if (!FeedCsvPath.IsEmpty())
			{
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(FeedCsvPath), true);
				const bool bSaved = FFileHelper::SaveStringToFile(FeedCsv, *FeedCsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				UE_LOG(LogGinkgoControl, Log, TEXT("[Feed] 记录%s %s"), bSaved ? TEXT("存到") : TEXT("存不进"), *FeedCsvPath);
				FeedCsvPath.Reset();
			}
			if (Action == TEXT("quit"))
			{
				FPlatformMisc::RequestExit(false, TEXT("ginkgo.input.feed"));
			}
		}
		else
		{
			UE_LOG(LogGinkgoControl, Warning, TEXT("[Feed] 看不懂：%s"), *FString::Join(Args, TEXT(" ")));
		}
	}

	void FeedPreTick(UWorld* World, const ELevelTick TickType, const float DeltaSeconds)
	{
		if (!World || !World->IsGameWorld() || TickType == LEVELTICK_TimeOnly)
		{
			return;
		}
		// 世界时间在这之前已经走到这一帧。到时的命令先做，模拟量算这一帧的
		const double Now = World->GetTimeSeconds();
		while (!FeedCommands.IsEmpty() && FeedCommands[0].At <= Now)
		{
			const TArray<FString> Args = MoveTemp(FeedCommands[0].Args);
			FeedCommands.RemoveAt(0);
			FeedRun(Args, *World);
		}
		APlayerController* Controller = FeedController(World);
		if (!Controller)
		{
			return;
		}
		for (int32 Index = FeedStreams.Num() - 1; Index >= 0; --Index)
		{
			const FFeedStream& Stream = FeedStreams[Index];
			const bool bEnded = Now >= Stream.EndTime - 1e-6;
			switch (Stream.Kind)
			{
			case FFeedStream::EKind::Mouse:
				if (!bEnded)
				{
					FeedAxis(*Controller, EKeys::MouseX, Stream.Value.X * DeltaSeconds, DeltaSeconds);
					FeedAxis(*Controller, EKeys::MouseY, Stream.Value.Y * DeltaSeconds, DeltaSeconds);
				}
				break;
			case FFeedStream::EKind::LeftStick:
				FeedAxis(*Controller, EKeys::Gamepad_LeftX, bEnded ? 0.f : Stream.Value.X, DeltaSeconds);
				FeedAxis(*Controller, EKeys::Gamepad_LeftY, bEnded ? 0.f : Stream.Value.Y, DeltaSeconds);
				break;
			case FFeedStream::EKind::RightStick:
				FeedAxis(*Controller, EKeys::Gamepad_RightX, bEnded ? 0.f : Stream.Value.X, DeltaSeconds);
				FeedAxis(*Controller, EKeys::Gamepad_RightY, bEnded ? 0.f : Stream.Value.Y, DeltaSeconds);
				break;
			}
			if (bEnded)
			{
				FeedStreams.RemoveAt(Index);
			}
		}
	}

	void FeedPostTick(UWorld* World, const ELevelTick TickType, const float DeltaSeconds)
	{
		if (World && World->IsGameWorld() && !FeedCsvPath.IsEmpty())
		{
			FeedCsv += FeedLine(*World) + TEXT("\n");
		}
	}

	FAutoConsoleCommandWithWorldAndArgs CommandInputFeed(
		TEXT("ginkgo.input.feed"),
		TEXT("测量用：ginkgo.input.feed [at=<世界时间>] mouse <每秒 x counts> <每秒 y counts> <秒> | stick <L|R> <x> <y> <秒> | press|release|repeat <键名> | mark <标签> | record <csv> | stop | quit。")
		TEXT("直接交给本地玩家的 PlayerInput，后面的映射、修改器与处理函数同真的设备"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& InArgs, UWorld* World)
		{
			TArray<FString> Args = InArgs;
			double At = -1.0;
			if (!Args.IsEmpty() && Args[0].StartsWith(TEXT("at=")))
			{
				At = FCString::Atod(*Args[0].RightChop(3));
				Args.RemoveAt(0);
			}
			if (!FeedPreTickHandle.IsValid())
			{
				FeedPreTickHandle = FWorldDelegates::OnWorldPreActorTick.AddStatic(&FeedPreTick);
				FeedPostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddStatic(&FeedPostTick);
			}
			if (At < 0.0 && World)
			{
				FeedRun(Args, *World);
				return;
			}
			const int32 Index = FeedCommands.IndexOfByPredicate([At](const FFeedCommand& Command) { return Command.At > At; });
			FeedCommands.Insert({At, MoveTemp(Args)}, Index == INDEX_NONE ? FeedCommands.Num() : Index);
		}));
}

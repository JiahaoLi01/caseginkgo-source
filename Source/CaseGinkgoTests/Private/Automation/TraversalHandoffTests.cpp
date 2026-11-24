#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animation/GinkgoAnimInstance.h"
#include "Character/GinkgoCharacter.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/Traversal/GinkgoTraversalComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"
#include "Utilities/Debug/InputReplay/GinkgoInputRecording.h"

namespace
{
	/** 回放经过 PlayerController 的真实输入分支，检查世界朝向与可见步态，而不只验证胶囊移动。 */
	class FTraversalHandoffScenario : public IAutomationLatentCommand
	{
	public:
		explicit FTraversalHandoffScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 130.) { Test.AddError(TEXT("攀爬交接回放超时")); return true; }
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			auto* PC = World ? World->GetFirstPlayerController() : nullptr;
			auto* Character = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!Character || World->GetTimeSeconds() < 1.) { return false; }
			auto* Traversal = Character->GetTraversal();
			auto* Movement = Character->GetGinkgoMovement();
			auto* Anim = CastChecked<UGinkgoAnimInstance>(Character->GetMesh()->GetAnimInstance());
			const double Now = World->GetTimeSeconds();
			const int32 Kind = Case < 6 || Case == 10 ? 0 : Case < 8 ? 1 : 2;
			const bool bMove = Case == 3 || Case == 4 || Case == 5 || Case == 7 || Case == 9 || Case == 10;
			const double Yaw = Case == 1 ? -120. : Case == 2 ? 170. : Case == 3 || Case == 4 || Case == 10 ? 0. : 120.;
			if (Stage == 0)
			{
				// 只在本次 PIE 实例隔离其他工位；正常出口惯性会让转向中的角色跑进相邻墙，
				// 随后的贴墙换步属于另一个场景，不能误算为攀爬交还时的转身瞬跳。保留所有地面与当前目标墙。
				for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
				{
					It->SetActorEnableCollision(It->GetActorLocation().Z <= 0. || FMath::Abs(It->GetActorLocation().Y - Kind * 400.) < 150.);
				}
				Character->SetActorLocationAndRotation(FVector(130., Kind * 400., 90.), FRotator::ZeroRotator);
				PC->SetControlRotation(FRotator::ZeroRotator);
				Movement->StopMovementImmediately();
				Movement->SetMovementMode(MOVE_Walking);
				Anim->RequestCut();
				StageStarted = Now;
				Stage = 1;
				return false;
			}
			if (Stage == 1)
			{
				if (Now - StageStarted < .4) { return false; }
				FGinkgoInputRecording Recording;
				const double LandTime = .2 + (Kind == 0 ? .8 : Kind == 1 ? 1. : 1.8);
				for (int32 Frame = 0; Frame <= 252; ++Frame)
				{
					FGinkgoInputSample Input;
					Input.Time = Frame / 60.;
					Input.bJumpPressed = Frame == 12;
					Input.ControlRotation.Yaw = Yaw * FMath::Clamp((Input.Time - .35) / .3, 0., 1.);
					Input.Move.Y = bMove && Input.Time >= LandTime + (Case == 10 ? .4 : Case == 4 ? .15 : -.1) ? 1. : 0.;
					Recording.Samples.Add(Input);
				}
				const FString Directory = FPaths::ProjectSavedDir() / TEXT("Ginkgo/Recordings");
				IFileManager::Get().MakeDirectory(*Directory, true);
				const FString Path = Directory / FString::Printf(TEXT("TraversalHandoff%d.csv"), Case);
				if (!FFileHelper::SaveStringToFile(Recording.ToCsv(), *Path)) { Test.AddError(TEXT("不能保存回放输入")); return true; }
				PC->ConsoleCommand(FString::Printf(TEXT("ginkgo.replay %s noreset"), *Path), false);
				StageStarted = PreviousTime = Now;
				PreviousYaw = Character->GetMesh()->GetSocketRotation(TEXT("root")).Yaw;
				MaxYawRate = MaskedMovement = FirstMove = 0.;
				FootLow = DBL_MAX;
				FootHigh = -DBL_MAX;
				bSawTraversal = bSawTurn = bSawGait = false;
				bWasDrivingRootMotion = false;
				Stage = 2;
				return false;
			}
			const double Dt = Now - PreviousTime;
			const double RootYaw = Character->GetMesh()->GetSocketRotation(TEXT("root")).Yaw;
			const auto State = Anim->GetLocomotionState();
			bSawTraversal |= Traversal->IsActive();
			if (bSawTraversal && !Movement->IsTraversing() && Dt > 0.)
			{
				if (FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousYaw, RootYaw)) / Dt >= 1000.)
				{
					Test.AddInfo(FString::Printf(TEXT("朝向超限 %d t=%.4f dt=%.4f root %.2f → %.2f actor %.2f weight %.3f"),
						Case, Now - StageStarted, Dt, PreviousYaw, RootYaw, Character->GetActorRotation().Yaw, Traversal->GetHandPose().Weight));
				}
				MaxYawRate = FMath::Max(MaxYawRate, FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousYaw, RootYaw)) / Dt);
				bSawTurn |= State == EGinkgoLocomotionState::TurnInPlace && Traversal->GetHandPose().Weight < .1f;
				if (bMove && Movement->Velocity.Size2D() > 30.)
				{
					if (FirstMove == 0.) { FirstMove = Now; }
					// 释放当帧的位移仍由根运动执行，不能把整段帧长算作普通移动的姿势遮盖。
					// 特别是跨过落稳通知的长帧，其中大部分时间实际上还在墙上。
					if (!bWasDrivingRootMotion && Traversal->GetHandPose().Weight > .5f) { MaskedMovement += Dt; }
					if (Now - FirstMove > .25 && Traversal->GetHandPose().Weight < .1f)
					{
						bSawGait |= State == EGinkgoLocomotionState::Start || State == EGinkgoLocomotionState::Cycle;
						const double FootZ = Character->GetMesh()->GetSocketTransform(TEXT("foot_l"), RTS_Component).GetLocation().Z;
						FootLow = FMath::Min(FootLow, FootZ);
						FootHigh = FMath::Max(FootHigh, FootZ);
					}
				}
			}
			PreviousTime = Now;
			PreviousYaw = RootYaw;
			bWasDrivingRootMotion = Movement->IsTraversing();
			if (Now - StageStarted < 4.4) { return false; }
			const FString Context = FString::Printf(TEXT("交接 %d yaw %.0f move %d：最大根旋转速度 %.1f deg/s，移动被全身姿势覆盖 %.3f s，脚高变化 %.1f cm"),
				Case, Yaw, bMove, MaxYawRate, MaskedMovement, bMove ? FootHigh - FootLow : 0.);
			Test.AddInfo(Context);
			Test.TestTrue(TEXT("真实跳跃输入确实触发攀爬"), bSawTraversal);
			Test.TestFalse(TEXT("动作正常交还"), Traversal->IsActive());
			Test.TestTrue(Context + TEXT(" 身体朝向没有瞬跳"), MaxYawRate < 1000.);
			if (bMove)
			{
				Test.TestTrue(TEXT("恢复移动时及时让出腿部动画"), FirstMove > 0. && MaskedMovement < .16);
				Test.TestTrue(TEXT("实际进入起步/循环并产生迈步"), bSawGait && FootHigh - FootLow > 5.);
			}
			else { Test.TestTrue(TEXT("转镜头后通过正常原地转身承接"), bSawTurn); }
			Stage = 0;
			return ++Case >= 11;
		}

	private:
		/** 框架拥有的报告接收者，潜伏命令期间有效。 */
		FAutomationTestBase& Test;

		/** 实时超时基准，避免未创建 PIE 时永久等待。 */
		double Started = FPlatformTime::Seconds();

		/** 翻越六种操作、低攀/高攀各两种操作，末尾补自然淡出中起步，逐次回放。 */
		int32 Case = 0;

		/** 放置、稳定、回放三个阶段；状态只由 Update 推进。 */
		int32 Stage = 0;

		/** 当前阶段开始的世界时间（秒）。 */
		double StageStarted = 0.;

		/** 上次采样的世界时间（秒），用于计算真实帧间转角。 */
		double PreviousTime = 0.;

		/** 上次求值后根骨骼的世界偏航（度），包含胶囊转动。 */
		double PreviousYaw = 0.;

		/** 本次交接后最大世界偏航速度（度/秒），每次回放清零。 */
		double MaxYawRate = 0.;

		/** 已移动但全身动作仍占大半权重的累计时间（秒）。 */
		double MaskedMovement = 0.;

		/** 落稳后首次产生移动速度的世界时间，零表示尚未移动。 */
		double FirstMove = 0.;

		/** 可见 Locomotion 阶段左脚的最小组件空间高度（cm）。 */
		double FootLow = DBL_MAX;

		/** 同阶段左脚的最大组件空间高度（cm），与最小值一起排除静止姿势滑行。 */
		double FootHigh = -DBL_MAX;

		/** 当前输入路线确实播放过攀爬。 */
		bool bSawTraversal = false;

		/** 交接后实际进入过原地转身状态。 */
		bool bSawTurn = false;

		/** 交接后全身覆盖退出时实际进入过起步或循环。 */
		bool bSawGait = false;

		/** 上次采样时是否仍由根运动驱动；跨越落稳的帧不计入普通移动的全身遮盖时间。 */
		bool bWasDrivingRootMotion = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalHandoffTest, "CaseGinkgo.Movement.Traversal.World.InputHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoTraversalHandoffTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/JiahaoLi/Traversal/L_TraversalDetection")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FTraversalHandoffScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

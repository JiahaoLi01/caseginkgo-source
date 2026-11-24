#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Character/GinkgoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Editor.h"
#include "Movement/GinkgoCharacterMovementComponent.h"
#include "Movement/Traversal/GinkgoTraversalComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "TraversalPIEScene.h"

namespace
{
	/** 在用户手测地图运行完整 PIE Tick，保留控制器、武器状态树和正常骨骼更新顺序。 */
	class FTraversalWorldScenario : public IAutomationLatentCommand
	{
	public:
		explicit FTraversalWorldScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 65.) { Test.AddError(TEXT("攀爬 PIE 超时")); return true; }
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
			auto* Character = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!World || !World->HasBegunPlay() || !Character || World->GetTimeSeconds() < 1.) { return false; }
			auto* Traversal = Character->GetTraversal();
			auto* Movement = Character->GetGinkgoMovement();
			const double Now = World->GetTimeSeconds();
			const FVector Feet = Character->GetActorLocation() - FVector::UpVector * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			if (Stage == 0)
			{
				const double Gaps[] = {84., 59., 34.};
				Character->SetActorLocation(FVector(200. - Gaps[Case % 3], Case == 9 ? 0. : (Case / 3) * 400., 90.));
				MaxExitSpeed = 0.;
				PreviousWeight = 0.f;
				bKeptGun = true;
				bCapturedLanding = false;
				PC->SetControlRotation(FRotator::ZeroRotator);
				Movement->StopMovementImmediately();
				Movement->SetMovementMode(MOVE_Walking);
				StageStarted = Now;
				Stage = 1;
				return false;
			}
			if (Stage == 1)
			{
				if (Now - StageStarted < .3) { return false; }
				const auto Candidate = Traversal->Evaluate(FRotator::ZeroRotator);
				if (!Test.TestTrue(FString::Printf(TEXT("工位 %d / 距离 %d 检测通过"), Case / 3 + 1, Case % 3), Candidate.IsValid())) { return true; }
				Expected = Candidate.Landing.Position;
				if (!Test.TestTrue(TEXT("真实 PIE 发起动作"), Traversal->TryStart(FRotator::ZeroRotator, Case == 9))) { return true; }
				StageStarted = Now;
				Stage = 2;
				return false;
			}
			if (!bCapturedLanding && !Movement->IsTraversing()) { LandedFeet = Feet; bCapturedLanding = true; }
			const FVector Wrist = Character->GetMesh()->GetSocketTransform(TEXT("hand_l"), RTS_Component).GetLocation();
			const FVector Pelvis = Character->GetMesh()->GetSocketTransform(TEXT("pelvis"), RTS_Component).GetLocation();
			if (Now - StageStarted > .5 && PreviousWeight > 0.f && PreviousWeight < .25f && Now > PreviousTime)
			{
				MaxExitSpeed = FMath::Max3(MaxExitSpeed, FVector::Dist(Wrist, PreviousWrist) / (Now - PreviousTime),
					FVector::Dist(Pelvis, PreviousPelvis) / (Now - PreviousTime));
				if (FVector::Dist(Wrist, PreviousWrist) / (Now - PreviousTime) >= 480.
					|| FVector::Dist(Pelvis, PreviousPelvis) / (Now - PreviousTime) >= 480.)
				{
					Test.AddInfo(FString::Printf(TEXT("退出跳位用例 %d，t %.3f，权重 %.3f → %.3f，手腕 %s → %s，骨盆 %s → %s"),
						Case, Now - StageStarted, PreviousWeight, Traversal->GetHandPose().Weight,
						*PreviousWrist.ToCompactString(), *Wrist.ToCompactString(), *PreviousPelvis.ToCompactString(), *Pelvis.ToCompactString()));
				}
			}
			PreviousWrist = Wrist;
			PreviousPelvis = Pelvis;
			PreviousTime = Now;
			PreviousWeight = Traversal->GetHandPose().Weight;
			if (Traversal->IsActive() && (Case < 6 || Case == 9)) { bKeptGun &= Character->IsHeldWeaponInHand(); }
			if (Case == 9 && Traversal->IsRecovering()) { Character->Move(FVector2D(0., 1.)); }
			if (Traversal->IsActive() && Now - StageStarted < 4.) { return false; }
			const FString Context = FString::Printf(TEXT("PIE 工位 %d / 距离 %d: 用时 %.3f，实际 %s，目标 %s"),
				Case / 3 + 1, Case % 3, Now - StageStarted, *Feet.ToCompactString(), *Expected.ToCompactString());
			Test.TestFalse(Context + TEXT(" 无路径阻挡"), Movement->IsTraversalBlocked());
			Test.TestFalse(Context + TEXT(" 动作已释放"), Traversal->IsActive());
			if (Case == 9) { Test.TestTrue(TEXT("移动版落稳后收势期间已能前进"), Feet.X > Expected.X + 20.); }
			else { Test.TestTrue(Context + TEXT(" 到达落点"), bCapturedLanding && LandedFeet.Equals(Expected, 6.)); }
			Test.TestTrue(TEXT("短翻越与低攀全程保持武器在手"), bKeptGun);
			Test.TestTrue(FString::Printf(TEXT("真实 PIE 退出手腕/骨盆速度 %.1f cm/s，无单帧跳位"), MaxExitSpeed), MaxExitSpeed < 480.);
			Test.AddInfo(FString::Printf(TEXT("PIE 用例 %d 收势退出速度 %.1f cm/s"), Case, MaxExitSpeed));
			Test.AddInfo(Context);
			Stage = 0;
			return ++Case >= 10;
		}

	private:
		/** 框架拥有的报告接收者，持续到潜伏命令结束。 */
		FAutomationTestBase& Test;

		/** 实时时间超时基准，避免 PIE 未启动时永久等待。 */
		double Started = FPlatformTime::Seconds();

		/** 三类工位 × 三种接近距离，最后补一次移动出口；由每次实际动作结束推进。 */
		int32 Case = 0;

		/** 当前阶段：放置、稳定、运行；移动和动画始终由世界自行 Tick。 */
		int32 Stage = 0;

		/** 当前阶段开始的世界时间（秒）。 */
		double StageStarted = 0.;

		/** 启动前的安全世界脚底落点（cm），结束后用于核对实际位移。 */
		FVector Expected = FVector::ZeroVector;

		/** 根运动归还时的脚底位置，避免把收势阶段的惯性移动算成根运动落点误差。 */
		FVector LandedFeet = FVector::ZeroVector;

		/** 本用例是否已采样根运动出口，每次放置角色时重置。 */
		bool bCapturedLanding = false;

		/** 上次采样的组件空间手腕位置（cm），排除角色整体移动对姿势连续性检查的干扰。 */
		FVector PreviousWrist = FVector::ZeroVector;

		/** 上次采样的组件空间骨盆位置（cm），用于发现退出瞬间站直。 */
		FVector PreviousPelvis = FVector::ZeroVector;

		/** 上次采样的全身权重，仅在淡出尾部比较跳位。 */
		float PreviousWeight = 0.f;

		/** 上次采样的世界时间（秒），按实际 PIE 步长归一化帧间位移。 */
		double PreviousTime = 0.;

		/** 本次淡出末尾最大的手腕或骨盆速度（cm/s），每次放置角色时清零。 */
		double MaxExitSpeed = 0.;

		/** 本次单手动作是否每帧都保留手持武器；高攀不检查此约束。 */
		bool bKeptGun = true;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalWorldTest, "CaseGinkgo.Movement.Traversal.World.MapFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoTraversalWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Developers/JiahaoLi/Traversal/L_TraversalDetection")));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<GinkgoTraversalTests::FStartStandalonePIE>());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FTraversalWorldScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif

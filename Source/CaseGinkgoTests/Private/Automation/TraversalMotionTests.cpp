#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Movement/Traversal/GinkgoTraversalAnimations.h"
#include "Animation/AnimMontage.h"
#include "MotionWarpingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalTrajectoryTest, "CaseGinkgo.Movement.Traversal.Motion.AuthoredTrajectory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoTraversalTrajectoryTest::RunTest(const FString& Parameters)
{
	const auto* Assets = LoadObject<UGinkgoTraversalAnimations>(nullptr, TEXT("/Game/Characters/Traversal/DA_TraversalAnimations.DA_TraversalAnimations"));
	if (!TestNotNull(TEXT("素材定义"), Assets)) { return false; }
	for (const auto* Clip : {&Assets->Vault, &Assets->VaultMoving, &Assets->Mantle, &Assets->Climb})
	{
		FGinkgoTraversalMotion Motion;
		Motion.Montage = Clip->Montage;
		Motion.Duration = Clip->MotionEndTime;
		for (int32 Frame = 0; Frame <= 120; ++Frame)
		{
			const float T = Motion.Duration * Frame / 120.f;
			const auto Raw = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Clip->Montage, 0.f, T);
			TestTrue(TEXT("无适配时每个采样保持完整原根运动，包括回摆和根旋转"), Motion.Evaluate(T).Equals(Raw, .001));
		}
		Motion.Scale = FVector(.7, .7, 1.1);
		Motion.HeightCorrection = 8.f;
		for (const int32 FPS : {30, 60, 120})
		{
			FTransform Accumulated = FTransform::Identity;
			float Previous = 0.f;
			for (float T = 1.f / FPS; Previous < Motion.Duration; T += 1.f / FPS)
			{
				const float End = FMath::Min(T, Motion.Duration);
				Accumulated = Motion.Delta(Previous, End) * Accumulated;
				Previous = End;
			}
			TestTrue(TEXT("不同帧率的根增量组合仍落在同一曲线上，不靠终点强拉"), Accumulated.Equals(Motion.Evaluate(Motion.Duration), .01));
		}
		for (const float Fraction : {.2f, .5f, .7f, .9f})
		{
			const FVector Raw = UMotionWarpingUtilities::ExtractRootMotionFromAnimation(Clip->Montage, 0.f, Motion.Duration * Fraction).GetTranslation();
			const FVector Adapted = Motion.Evaluate(Motion.Duration * Fraction).GetTranslation();
			TestTrue(TEXT("水平进度只整体适配，抬升和下落期间仍保留原来的推进"), FVector(Adapted.X, Adapted.Y, 0.).Equals(FVector(Raw.X, Raw.Y, 0.) * .7, .001));
		}
		if (Clip == &Assets->Vault)
		{
			const auto BeforeLand = Motion.Evaluate(Motion.Duration - .2f).GetTranslation();
			TestTrue(TEXT("短翻越最后 0.2 秒仍向前，不是原地下降"), Motion.Evaluate(Motion.Duration).GetTranslation().Y - BeforeLand.Y > 30.);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalContactCurveTest, "CaseGinkgo.Movement.Traversal.Motion.ContactMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoTraversalContactCurveTest::RunTest(const FString& Parameters)
{
	FGinkgoTraversalMotion Motion;
	Motion.Duration = 1.f;
	Motion.RiseEndTime = .4f;
	Motion.SourceContact = 40.;
	Motion.SourceEnd = 100.;
	Motion.ContactDistance = 0.;
	Motion.EndDistance = 80.;
	TestEqual(TEXT("贴墙起步触边前不向墙里冲"), Motion.WarpForward(20., .2f), 0.);
	TestEqual(TEXT("触边位置独立于落点"), Motion.WarpForward(40., .4f), 0.);
	TestTrue(TEXT("触边后继续推进"), Motion.WarpForward(60., .6f) > 0.);
	TestEqual(TEXT("落点准确"), Motion.WarpForward(100., 1.f), 80.);
	Motion.ContactDistance = 25.;
	const double LeftSlope = (Motion.WarpForward(40., .4f) - Motion.WarpForward(39.99, .3999f)) / .01;
	const double RightSlope = (Motion.WarpForward(40.01, .4001f) - Motion.WarpForward(40., .4f)) / .01;
	TestTrue(TEXT("触边前后水平切线连续"), FMath::IsNearlyEqual(LeftSlope, RightSlope, .001));
	Motion.ContactDistance = 125.;
	Motion.EndDistance = 205.;
	TestTrue(TEXT("远距离接近不令触边后的支撑段提前前冲"), FMath::IsNearlyEqual(Motion.WarpForward(70., .7f) - Motion.ContactDistance, 40., .001));
	const double LongLeftSlope = (Motion.WarpForward(40., .4f) - Motion.WarpForward(39.99, .3999f)) / .01;
	const double LongRightSlope = (Motion.WarpForward(40.01, .4001f) - Motion.WarpForward(40., .4f)) / .01;
	TestTrue(TEXT("远距离接近的触边切线也连续"), FMath::IsNearlyEqual(LongLeftSlope, LongRightSlope, .001));
	Motion.ContactDistance = 25.;
	Motion.EndDistance = 80.;
	Motion.SourceContact = Motion.SourceEnd = 0.;
	TestEqual(TEXT("没有 XY 根运动也可对齐触边位置"), Motion.WarpForward(0., .4f), 25.);
	TestEqual(TEXT("没有 XY 根运动仍能对齐落点"), Motion.WarpForward(0., 1.f), 80.);
	TestTrue(TEXT("零 XY 素材的补充位移连续"), Motion.WarpForward(0., .2f) > 0. && Motion.WarpForward(0., .2f) < 25.);
	return true;
}

#endif

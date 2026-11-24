#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "TraversalTestScene.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/Actions/GinkgoAnimActionRunner.h"
#include "Animation/AnimMontage.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "MotionWarpingComponent.h"
#include "Movement/GinkgoMovementConfig.h"
#include "Movement/Traversal/GinkgoTraversalAction.h"
#include "Movement/Traversal/GinkgoTraversalAnimations.h"
#include "Utilities/Enumeration/Tags/AnimActionTags.h"
#include "Utilities/Enumeration/Tags/MovementTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "UI/GinkgoWindowTimelineModel.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace GinkgoTraversalTests
{
	/** 在独立物理世界中使用真实角色、AnimBP、蒙太奇及 CharacterMovement，不模拟根运动结果。 */
	struct FActionScene : FScene
	{
		/** 世界拥有的角色，析构时由 FScene 的 EndPlay 路径释放。 */
		AGinkgoCharacter* Character = nullptr;

		/** 创建本地权威角色并初始化真实动画和动作执行器；所有尺寸来自角色胶囊。 */
		void SpawnCharacter()
		{
			Character = World->SpawnActor<AGinkgoCharacter>(FVector(0., 0., 90.), FRotator::ZeroRotator);
			auto* PC = World->SpawnActor<AGinkgoPlayerController>();
			PC->SetAsLocalPlayerController();
			PC->Possess(Character);
			Character->GetGinkgoMovement()->SetMovementMode(MOVE_Walking);
			Character->DispatchBeginPlay();
			Character->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Character->GetMesh()->bEnableUpdateRateOptimizations = false;
			auto* Weapons = Character->GetWeapons();
			Weapons->StopActionStates();
			for (int32 Slot = 0; Slot < Weapons->SlotCount; ++Slot) { Weapons->SetCarried(Slot, nullptr); }
		}

		/** 同一次引擎帧可推进多次自主根运动 Tick；NativeThreadSafeUpdate 显式更新 Rig 快照。 */
		void Step(const float Dt)
		{
			World->TimeSeconds += Dt;
			auto* Mesh = Character->GetMesh();
			Character->GetWeapons()->UpdateHoldState(Dt);
			const bool bRootMotionBefore = Character->IsPlayingRootMotion();
			Character->GetGinkgoMovement()->TickComponent(Dt, LEVELTICK_All, nullptr);
			Character->GetAnimActions()->TickComponent(Dt, LEVELTICK_All, nullptr);
			static_cast<UActorComponent*>(Character->GetTraversal())->TickComponent(Dt, LEVELTICK_All, nullptr);
			if (!bRootMotionBefore && !Character->IsPlayingRootMotion()) { Mesh->TickAnimation(Dt, false); }
			Mesh->GetAnimInstance()->NativeThreadSafeUpdateAnimation(Dt);
			Mesh->RefreshBoneTransforms(nullptr);
		}

		FVector Feet() const
		{
			return Character->GetActorLocation() - FVector::UpVector * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalAssetTest, "CaseGinkgo.Movement.Traversal.Action.AssetsAndWindows", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalAssetTest::RunTest(const FString& Parameters)
{
	const auto* Data = LoadObject<UGinkgoTraversalAnimations>(nullptr, TEXT("/Game/Characters/Traversal/DA_TraversalAnimations.DA_TraversalAnimations"));
	if (!TestNotNull(TEXT("攀爬素材定义已保存且可加载"), Data)) { return false; }
	for (const auto Kind : {EGinkgoTraversalKind::Vault, EGinkgoTraversalKind::Mantle, EGinkgoTraversalKind::Climb})
	{
		const auto* Clip = Data->Find(Kind);
		if (!TestTrue(TEXT("素材、根运动和必需支撑手窗口有效"), Clip && Clip->IsValid())) { continue; }
		TestEqual(TEXT("一个整段根运动窗口及一个落稳完成事件"), Clip->Montage->Notifies.Num(), 2);
		TestEqual(TEXT("使用已接入管线的全身槽"), Clip->Montage->SlotAnimTracks[0].SlotName, FName(TEXT("DefaultSlot")));
		TestEqual(TEXT("收势尾段提前淡出，不在素材终点切姿势"), Clip->Montage->BlendOutTriggerTime, Clip->BlendOutDuration);
		TestEqual(TEXT("仅高攀需要双手"), Clip->bTwoHanded, Kind == EGinkgoTraversalKind::Climb);
		FGinkgoTraversalClip Broken = *Clip;
		Broken.Montage = DuplicateObject<UAnimMontage>(Clip->Montage, GetTransientPackage());
		Broken.Montage->Notifies.RemoveAt(1);
		TestFalse(TEXT("缺少落稳事件时拒绝执行"), Broken.IsValid());
		Broken.Montage = DuplicateObject<UAnimMontage>(Clip->Montage, GetTransientPackage());
		Broken.Montage->Notifies.RemoveAt(0);
		TestFalse(TEXT("缺少整段 Warp 窗口时拒绝执行"), Broken.IsValid());
		Broken.Montage = DuplicateObject<UAnimMontage>(Clip->Montage, GetTransientPackage());
		Broken.Montage->Notifies[0].TriggerTimeOffset = .0001f;
		TestFalse(TEXT("窗口晚于零时刻会漏掉第一帧适配，必须拒绝"), Broken.IsValid());
		for (const auto* Window : {&Clip->LeftHand, &Clip->RightHand})
		{
			if (Window == &Clip->RightHand && !Clip->bTwoHanded) { continue; }
			TestEqual(TEXT("接触前没有硬吸附"), Window->Weight(Window->Begin), 0.f);
			TestEqual(TEXT("接触中权重完全贴合"), Window->Weight((Window->Contact + Window->Release) * .5f), 1.f);
			TestEqual(TEXT("脱手后归还动画"), Window->Weight(Window->End), 0.f);
		}
	}
	TestTrue(TEXT("移动出口也是完整有效片段"), Data->VaultMoving.IsValid());
	TestNotEqual(TEXT("停步和移动分别选择落地恢复素材"), Data->Find(EGinkgoTraversalKind::Vault, false)->Montage.Get(), Data->Find(EGinkgoTraversalKind::Vault, true)->Montage.Get());
	TestTrue(TEXT("翻越包括触地缓冲"), Data->Vault.Montage->GetPlayLength() >= 1.39f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalFlowTest, "CaseGinkgo.Movement.Traversal.Action.RootMotionFlow", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalFlowTest::RunTest(const FString& Parameters)
{
	for (const float Dt : {1.f / 30.f, 1.f / 60.f, 1.f / 120.f})
	{
		for (const auto& Shape : {FVector2D(60., 40.), FVector2D(100., 40.), FVector2D(125., 40.),
			FVector2D(60., 200.), FVector2D(100., 200.), FVector2D(125., 200.),
			FVector2D(130., 200.), FVector2D(220., 200.), FVector2D(250., 200.)})
		{
			GinkgoTraversalTests::FActionScene Scene;
			Scene.Floor();
			Scene.Wall(Shape.X, Shape.Y);
			Scene.SpawnCharacter();
			auto* Traversal = Scene.Character->GetTraversal();
			auto* Movement = Scene.Character->GetGinkgoMovement();
			auto* Warp = Scene.Character->FindComponentByClass<UMotionWarpingComponent>();
			const auto Expected = Traversal->Evaluate(FRotator::ZeroRotator);
			FGinkgoTraversalMotion ExpectedMotion;
			const auto* Assets = LoadObject<UGinkgoTraversalAnimations>(nullptr, TEXT("/Game/Characters/Traversal/DA_TraversalAnimations.DA_TraversalAnimations"));
			if (!TestTrue(TEXT("原动画适配方案有效"), Assets && Ginkgo::Traversal::PrepareMotion(*Assets->Find(Expected.Kind), Expected, *Scene.Character, ExpectedMotion))) { continue; }
			const FString Context = FString::Printf(TEXT("%.0f FPS 高 %.0f 深 %.0f"), 1.f / Dt, Shape.X, Shape.Y);
			if (!TestTrue(Context + TEXT(" 发起"), Traversal->TryStart(FRotator::ZeroRotator))) { continue; }
			const auto OriginalMode = Scene.Character->GetMesh()->GetAnimInstance()->RootMotionMode;
			const double Radius = Scene.Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
			const double HalfHeight = Scene.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			FVector LandedFeet = FVector::ZeroVector;
			bool bCapturedLanding = false;
			bool bMoved = false;
			bool bSawContact = false;
			double MaxHandError = 0.;
			double MaxTrajectoryError = 0.;
			for (int32 Frame = 0; Frame < FMath::CeilToInt(4.f / Dt); ++Frame)
			{
				Scene.Step(Dt);
				bMoved |= Scene.Feet().X > 10.;
				if (!bCapturedLanding && !Movement->IsTraversing()) { LandedFeet = Scene.Feet(); bCapturedLanding = true; }
				if (Movement->IsTraversing())
				{
					const FVector Authored = ExpectedMotion.Origin.TransformPosition(ExpectedMotion.Evaluate(Traversal->GetMotionTime()).GetTranslation());
					MaxTrajectoryError = FMath::Max(MaxTrajectoryError, FVector::Dist(Scene.Feet(), Authored));
				}
				const auto Hands = Traversal->GetHandPose();
				bSawContact |= Hands.LeftAlpha > .9f && (Hands.bTwoHanded ? Hands.RightAlpha > .9f : Hands.RightAlpha == 0.f);
				if (Hands.LeftAlpha * Hands.Weight > .99f) { MaxHandError = FMath::Max(MaxHandError, FVector::Dist(Scene.Character->GetMesh()->GetSocketLocation(TEXT("hand_l")), Hands.Left)); }
				if (Hands.RightAlpha * Hands.Weight > .99f) { MaxHandError = FMath::Max(MaxHandError, FVector::Dist(Scene.Character->GetMesh()->GetSocketLocation(TEXT("hand_r")), Hands.Right)); }

				if (!Traversal->IsActive()) { break; }
			}
			const FString Position = FString::Printf(TEXT(" 实际 %s 目标 %s"), *Scene.Feet().ToCompactString(), *Expected.Landing.Position.ToCompactString());
			TestTrue(Context + TEXT(" 使用根运动实际移动"), bMoved);
			TestFalse(Context + TEXT(" 清理动作"), Traversal->IsActive());
			TestFalse(Context + TEXT(" 身体路径未撞墙") + Position, Movement->IsTraversalBlocked());
			TestTrue(Context + TEXT(" 落在已验证目标附近") + Position, bCapturedLanding && LandedFeet.Equals(Expected.Landing.Position, 6.));
			TestTrue(Context + TEXT(" 对应的单手/双手窗口实际生效"), bSawContact);
			TestTrue(Context + FString::Printf(TEXT(" 实际胶囊每帧沿适配后的原曲线移动，最大偏差 %.3f cm"), MaxTrajectoryError), MaxTrajectoryError < .1);
			TestTrue(Context + FString::Printf(TEXT(" 实际手腕贴合误差 %.1f cm"), MaxHandError), MaxHandError < 6.);
			AddInfo(Context + FString::Printf(TEXT(" 手腕最大误差 %.2f cm"), MaxHandError));
			TestFalse(Context + TEXT(" 退出自定义移动模式"), Movement->IsTraversing());
			TestEqual(Context + TEXT(" 移除所有本动作 Warp Target"), Warp->GetWarpTargets().Num(), 0);
			TestEqual(Context + TEXT(" 移除旧窗口，允许重复播放同一素材"), Warp->GetModifiers().Num(), 0);
			TestEqual(Context + TEXT(" 恢复根运动提取模式"), Scene.Character->GetMesh()->GetAnimInstance()->RootMotionMode, OriginalMode);
			TestEqual(Context + TEXT(" 保留胶囊半径"), double(Scene.Character->GetCapsuleComponent()->GetScaledCapsuleRadius()), Radius);
			TestEqual(Context + TEXT(" 保留胶囊半高"), double(Scene.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()), HalfHeight);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalCancelTest, "CaseGinkgo.Movement.Traversal.Action.Interruption", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalCancelTest::RunTest(const FString& Parameters)
{
	for (int32 Case = 0; Case < 3; ++Case)
	{
		GinkgoTraversalTests::FActionScene Scene;
		Scene.Floor();
		auto* Wall = Scene.Wall(220., 200.);
		Scene.SpawnCharacter();
		auto* Traversal = Scene.Character->GetTraversal();
		if (!TestTrue(TEXT("取消用例先启动真实高攀"), Traversal->TryStart(FRotator::ZeroRotator))) { continue; }
		for (int32 Frame = 0; Frame < 25; ++Frame) { Scene.Step(1.f / 60.f); }
		if (Case == 0) { Traversal->Cancel(); }
		else if (Case == 1) { Wall->DestroyComponent(); }
		else { Scene.Box(FVector(20., 0., 205.), FVector(50., 100., 5.)); }
		for (int32 Frame = 0; Frame < 180; ++Frame) { Scene.Step(1.f / 60.f); }
		TestFalse(TEXT("手动取消、支撑销毁或中途加梁均结束动作"), Traversal->IsActive());
		TestFalse(TEXT("中止不遗留根运动模式"), Scene.Character->GetGinkgoMovement()->IsTraversing());
		TestEqual(TEXT("中止不遗留 Warp Target"), Scene.Character->FindComponentByClass<UMotionWarpingComponent>()->GetWarpTargets().Num(), 0);
		TestEqual(TEXT("中止也移除旧修正窗口"), Scene.Character->FindComponentByClass<UMotionWarpingComponent>()->GetModifiers().Num(), 0);
		TestTrue(TEXT("中止不会传送到高台落点"), Scene.Feet().X < 70.);
		if (Case == 0)
		{
			Scene.Character->SetActorLocation(FVector(0., 0., 90.));
			Scene.Character->GetGinkgoMovement()->SetMovementMode(MOVE_Walking);
			TestTrue(TEXT("取消后可以再次启动同一素材"), Traversal->TryStart(FRotator::ZeroRotator));
			for (int32 Frame = 0; Frame < 180; ++Frame) { Scene.Step(1.f / 60.f); }
			TestFalse(TEXT("重试后释放动作"), Traversal->IsActive());
			TestTrue(TEXT("重试保留完整根运动窗口，实际登上高台"), Scene.Feet().Z > 220. && Scene.Feet().X > 100.);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalBodyTest, "CaseGinkgo.Movement.Traversal.Action.IndependentCollision", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalBodyTest::RunTest(const FString& Parameters)
{
	GinkgoTraversalTests::FActionScene Scene;
	Scene.Floor();
	Scene.Wall(100., 40.);
	Scene.SpawnCharacter();
	auto* Traversal = Scene.Character->GetTraversal();
	const auto Candidate = Traversal->Evaluate(FRotator::ZeroRotator);
	const auto* Assets = LoadObject<UGinkgoTraversalAnimations>(nullptr, TEXT("/Game/Characters/Traversal/DA_TraversalAnimations.DA_TraversalAnimations"));
	FGinkgoTraversalMotion Motion;
	if (!TestTrue(TEXT("生成同一根轨迹"), Assets && Ginkgo::Traversal::PrepareMotion(Assets->Vault, Candidate, *Scene.Character, Motion))) { return false; }
	FHitResult Hit;
	TestTrue(TEXT("收腿代理允许原动画轨迹通过墙沿"), Ginkgo::Traversal::CheckMotion(Motion, *Scene.Character, Hit));
	const auto OriginalMidpoint = Motion.Evaluate(.4f);
	for (auto& Body : Motion.Body) { Body.Offset = FVector::ZeroVector; Body.HeightScale = Body.RadiusScale = 1.f; }
	TestFalse(TEXT("同一轨迹使用站立胶囊会被拒绝，不能投影改轨迹绕过去"), Ginkgo::Traversal::CheckMotion(Motion, *Scene.Character, Hit));
	TestTrue(TEXT("碰撞代理的替换完全不改变动画轨迹"), Motion.Evaluate(.4f).Equals(OriginalMidpoint, .001));
	if (!TestTrue(TEXT("启动后在收腿中段取消"), Traversal->TryStart(FRotator::ZeroRotator))) { return false; }
	for (int32 Frame = 0; Frame < 15; ++Frame) { Scene.Step(1.f / 60.f); }
	Traversal->Cancel();
	for (int32 Frame = 0; Frame < 90; ++Frame) { Scene.Step(1.f / 60.f); }
	TestFalse(TEXT("中段取消清理动作"), Traversal->IsActive());
	const auto* Capsule = Scene.Character->GetCapsuleComponent();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TraversalRestoreTest), false, Scene.Character);
	TestFalse(TEXT("取消后站立胶囊不遗留墙内重叠"), Scene.World->OverlapBlockingTestByChannel(Scene.Character->GetActorLocation(), FQuat::Identity,
		Capsule->GetCollisionObjectType(), FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()),
		Params, FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())));
	TestTrue(TEXT("取消只作附近解穿透，不传送到墙后落点"), Scene.Feet().X < Candidate.Landing.Position.X - 30.);
	GinkgoTraversalTests::FActionScene Crouched;
	Crouched.Floor();
	Crouched.Wall(100., 40.);
	Crouched.SpawnCharacter();
	Crouched.Character->Crouch();
	Crouched.Step(1.f / 60.f);
	TestTrue(TEXT("确实以蹲姿进入预检"), Crouched.Character->bIsCrouched);
	if (TestTrue(TEXT("蹲姿预检采用站立执行尺寸"), Crouched.Character->GetTraversal()->TryStart(FRotator::ZeroRotator)))
	{
		for (int32 Frame = 0; Frame < 110; ++Frame) { Crouched.Step(1.f / 60.f); }
		TestFalse(TEXT("站起后执行不因预检尺寸不同而撞墙"), Crouched.Character->GetGinkgoMovement()->IsTraversalBlocked());
		TestFalse(TEXT("蹲姿发起的动作正常释放"), Crouched.Character->GetTraversal()->IsActive());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalReloadTest, "CaseGinkgo.Movement.Traversal.Action.ReloadPreemption", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalReloadTest::RunTest(const FString& Parameters)
{
	for (const FVector2D Case : {FVector2D(0., 100.), FVector2D(1., 100.), FVector2D(0., 220.), FVector2D(1., 220.)})
	{
		GinkgoTraversalTests::FActionScene Scene;
		Scene.Floor();
		Scene.SpawnCharacter();
		auto* Weapons = Scene.Character->GetWeapons();
		auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
		if (!TestNotNull(TEXT("真实 AK 定义"), Weapon)) { return false; }
		Weapons->SetCarried(0, Weapon);
		Weapons->SetHeldSlot(0);
		Weapons->AbandonTarget();
		Scene.Character->AttachWeaponToHand(0);
		auto* Reload = Weapons->StartStep(FWeaponTags::Weapon_Step_ReloadTactical);
		if (!TestNotNull(TEXT("真实换弹 AnimAction"), Reload)) { return false; }
		Scene.Step(1.f / 60.f);
		TestFalse(TEXT("没有可攀爬墙面时不会消费跳跃"), Scene.Character->GetTraversal()->TryStart(FRotator::ZeroRotator));
		TestFalse(TEXT("失败检测保留正在播放的换弹"), Reload->IsFinished());
		if (Case.X > 0.)
		{
			for (int32 Frame = 0; Frame < 120 && Weapons->GetMechanismState(0)->bMagazineIn; ++Frame) { Scene.Step(1.f / 60.f); }
			TestFalse(TEXT("中断前真实 MagOut 通知已取下弹匣"), Weapons->GetMechanismState(0)->bMagazineIn);
			TestFalse(TEXT("取下弹匣后仍处于换弹过程"), Reload->IsFinished());
		}
		Scene.Wall(Case.Y, 200.);
		const FGinkgoMechanismState Before = *Weapons->GetMechanismState(0);
		TestTrue(TEXT("攀爬可抢占换弹"), Scene.Character->GetTraversal()->TryStart(FRotator::ZeroRotator));
		Scene.Step(1.f / 60.f);
		TestTrue(TEXT("换弹按取消路径结束"), Reload->IsFinished());
		TestEqual(TEXT("取消原因是手臂被高优先级抢占"), Reload->GetResult().Reason, FGameplayTag(FAnimActionTags::AnimAction_Cancel_Preempted));
		TestEqual(TEXT("低攀保留右手持枪，仅高攀收枪"), Scene.Character->IsWeaponStowed(0), Case.Y > 125.);
		for (int32 Frame = 0; Frame < 210; ++Frame) { Scene.Step(1.f / 60.f); }
		const auto* After = Weapons->GetMechanismState(0);
		TestEqual(TEXT("未发生的装弹不会补做"), After->Feed.Num(), Before.Feed.Num());
		TestEqual(TEXT("保留真实弹匣状态"), After->bMagazineIn, Before.bMagazineIn);
		TestEqual(TEXT("保留真实枪机状态"), After->Action, Before.Action);
		TestTrue(TEXT("结束后恢复原来的手持武器"), Scene.Character->IsHeldWeaponInHand());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalSingleHandTest, "CaseGinkgo.Movement.Traversal.Action.SingleHandRecovery", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalSingleHandTest::RunTest(const FString& Parameters)
{
	for (int32 Case = 0; Case < 3; ++Case)
	{
		GinkgoTraversalTests::FActionScene Scene;
		Scene.Floor();
		Scene.Wall(100., Case == 2 ? 200. : 40.);
		Scene.SpawnCharacter();
		auto* Weapons = Scene.Character->GetWeapons();
		auto* Definition = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
		if (!TestNotNull(TEXT("持枪用例的武器定义"), Definition)) { return false; }
		Weapons->SetCarried(0, Definition);
		Weapons->SetHeldSlot(0);
		Weapons->AbandonTarget();
		Scene.Character->AttachWeaponToHand(0);
		for (int32 Frame = 0; Frame < 30; ++Frame) { Scene.Step(1.f / 60.f); }
		auto* Traversal = Scene.Character->GetTraversal();
		auto* Mesh = Scene.Character->GetMesh();
		if (!TestTrue(TEXT("持枪短翻越/低攀启动"), Traversal->TryStart(FRotator::ZeroRotator, Case == 1))) { continue; }
		bool bAlwaysHeld = true;
		bool bOnlyLeft = true;
		bool bSawFade = false;
		bool bSawRecovery = false;
		double MaxGripError = 0.;
		double ExitJump = 0.;
		FVector PreviousLeft = Mesh->GetSocketTransform(TEXT("hand_l"), RTS_Component).GetLocation();
		FVector PreviousPelvis = Mesh->GetSocketTransform(TEXT("pelvis"), RTS_Component).GetLocation();
		float PreviousWeight = 0.f;
		for (int32 Frame = 0; Frame < 210; ++Frame)
		{
			const bool bWasActive = Traversal->IsActive();
			Scene.Step(1.f / 60.f);
			const auto Pose = Traversal->GetHandPose();
			const FVector Left = Mesh->GetSocketTransform(TEXT("hand_l"), RTS_Component).GetLocation();
			const FVector Pelvis = Mesh->GetSocketTransform(TEXT("pelvis"), RTS_Component).GetLocation();
			bAlwaysHeld &= Scene.Character->IsHeldWeaponInHand();
			bOnlyLeft &= Pose.RightAlpha == 0.f;
			bSawRecovery |= Traversal->IsRecovering();
			bSawFade |= Traversal->IsRecovering() && Pose.Weight > .05f && Pose.Weight < .9f;
			if (bSawRecovery && PreviousWeight > 0.f && PreviousWeight < .25f)
			{
				ExitJump = FMath::Max3(ExitJump, FVector::Dist(PreviousLeft, Left), FVector::Dist(PreviousPelvis, Pelvis));
			}
			if (Pose.Weight > .99f)
			{
				const auto& Rig = CastChecked<UGinkgoAnimInstance>(Mesh->GetAnimInstance())->GetWeaponRigData();
				const FTransform Grip = Rig.Hands.Right * Mesh->GetSocketTransform(TEXT("VB spine_05_ik_hand_gun"));
				const double Error = FVector::Dist(Grip.GetLocation(), Mesh->GetSocketLocation(TEXT("hand_r")));
				MaxGripError = FMath::Max(MaxGripError, Error);
			}
			PreviousLeft = Left;
			PreviousPelvis = Pelvis;
			PreviousWeight = Pose.Weight;
			if (bWasActive && !Traversal->IsActive()) { break; }
		}
		TestTrue(TEXT("全程保留右手武器"), bAlwaysHeld);
		TestTrue(TEXT("右手从不进入墙沿 IK"), bOnlyLeft);
		TestTrue(TEXT("落稳后先归还移动，再平滑退出姿势"), bSawRecovery && bSawFade);
		TestFalse(TEXT("收势最终释放"), Traversal->IsActive());
		TestTrue(FString::Printf(TEXT("右手握把误差 %.2f cm"), MaxGripError), MaxGripError < 6.);
		TestTrue(FString::Printf(TEXT("退出时实际手腕/骨盆帧间位移 %.2f cm"), ExitJump), ExitJump < 8.);
		AddInfo(FString::Printf(TEXT("单手用例 %d：握把误差 %.2f cm，退出帧间位移 %.2f cm"), Case, MaxGripError, ExitJump));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalEntryCorrectionsTest, "CaseGinkgo.Movement.Traversal.Action.EntryCorrections", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalEntryCorrectionsTest::RunTest(const FString& Parameters)
{
	for (const float Dt : {1.f / 30.f, 1.f / 60.f, 1.f / 120.f})
	{
		GinkgoTraversalTests::FActionScene Scene;
		Scene.Floor();
		Scene.Wall(100., 200.);
		Scene.SpawnCharacter();
		auto* Anim = CastChecked<UGinkgoAnimInstance>(Scene.Character->GetMesh()->GetAnimInstance());
		for (int32 Frame = 0; Frame < FMath::CeilToInt(.4f / Dt); ++Frame) { Scene.Step(Dt); }
		Scene.Character->GetController()->SetControlRotation(FRotator(0., 45., 0.));
		Scene.Step(Dt);
		const float BeforeYaw = Anim->GetRootYawOffset();
		const float BeforeIK = Anim->GetLegIKAlpha();
		TestTrue(TEXT("进入前存在真实偏航补偿和地面脚锁"), FMath::Abs(BeforeYaw) > 20.f && BeforeIK > .9f);
		auto* Traversal = Scene.Character->GetTraversal();
		if (!TestTrue(TEXT("带偏航补偿进入低攀"), Traversal->TryStart(FRotator::ZeroRotator))) { continue; }
		Scene.Step(Dt);
		TestTrue(TEXT("进入第一帧不清空偏航补偿"), FMath::Abs(Anim->GetRootYawOffset()) > FMath::Abs(BeforeYaw) * .8f);
		TestTrue(TEXT("进入第一帧不清空 foot IK"), Anim->GetLegIKAlpha() > BeforeIK * .8f);
		for (int32 Frame = 0; Frame < FMath::CeilToInt(.3f / Dt); ++Frame) { Scene.Step(Dt); }
		TestTrue(TEXT("全身接管后脚锁与偏航补偿完全让出"), Anim->GetLegIKAlpha() < .001f && FMath::Abs(Anim->GetRootYawOffset()) < .01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalFootHandoffTest, "CaseGinkgo.Movement.Traversal.Action.FootSupportHandoff", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalFootHandoffTest::RunTest(const FString& Parameters)
{
	for (const float Dt : {1.f / 30.f, 1.f / 60.f, 1.f / 120.f})
	{
		for (const double Height : {100., 220.})
		{
			GinkgoTraversalTests::FActionScene Scene;
			Scene.Floor();
			Scene.Wall(Height, 200.);
			Scene.SpawnCharacter();
			auto* Weapons = Scene.Character->GetWeapons();
			auto* Definition = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
			Weapons->SetCarried(0, Definition);
			Weapons->SetHeldSlot(0);
			Weapons->AbandonTarget();
			Scene.Character->AttachWeaponToHand(0);
			for (int32 Frame = 0; Frame < FMath::CeilToInt(.4f / Dt); ++Frame) { Scene.Step(Dt); }
			auto* Traversal = Scene.Character->GetTraversal();
			auto* Movement = Scene.Character->GetGinkgoMovement();
			auto* Anim = CastChecked<UGinkgoAnimInstance>(Scene.Character->GetMesh()->GetAnimInstance());
			if (!TestTrue(TEXT("足部交接用例发起"), Traversal->TryStart(FRotator::ZeroRotator))) { continue; }
			float ReleasedAt = -1.f;
			FVector ReleasedFeet = FVector::ZeroVector;
			bool bSawDrawWhileFading = false;
			bool bMovedWhileDrawing = false;
			bool bGunStayedOnBackAtRelease = false;
			bool bSawGait = false;
			for (int32 Frame = 0; Frame < FMath::CeilToInt(3.5f / Dt); ++Frame)
			{
				const float Elapsed = Frame * Dt;
				if (ReleasedAt >= 0.f && Elapsed - ReleasedAt < .25f) { Scene.Character->Move(FVector2D(0., 1.)); }
				Scene.Step(Dt);
				if (ReleasedAt < 0.f && Traversal->IsRecovering())
				{
					ReleasedAt = Elapsed;
					ReleasedFeet = Scene.Feet();
					bGunStayedOnBackAtRelease = Scene.Character->IsWeaponStowed(0);
					TestTrue(TEXT("交还时普通胶囊已有可行走支撑"), Movement->IsMovingOnGround());
				}
				bool bDrawing = false;
				for (const auto* Action : Scene.Character->GetAnimActions()->GetRunner()->GetRunningLeaves())
				{
					bDrawing |= Action->GetActionName() == FName(TEXT("DrawWeapon"));
				}
				bSawDrawWhileFading |= bDrawing && Traversal->IsRecovering();
				bMovedWhileDrawing |= bDrawing && ReleasedAt >= 0.f && Scene.Feet().X > ReleasedFeet.X + 20.;
				bSawGait |= ReleasedAt >= 0.f && (Anim->GetLocomotionState() == EGinkgoLocomotionState::Start || Anim->GetLocomotionState() == EGinkgoLocomotionState::Cycle);
			}
			TestTrue(FString::Printf(TEXT("高 %.0f 在站直以前交还，实测 %.3f s"), Height, ReleasedAt), ReleasedAt > 0.f && ReleasedAt < (Height > 125. ? 1.88f : 1.08f));
			TestTrue(TEXT("下半身实际进入走跑"), bSawGait);
			if (Height > 125.)
			{
				TestTrue(TEXT("脚部交接不把背上的枪瞬间挂回手"), bGunStayedOnBackAtRelease);
				TestTrue(TEXT("全身尚在淡出时右臂已经取枪"), bSawDrawWhileFading);
				TestTrue(TEXT("取枪过程中脚部可以前进"), bMovedWhileDrawing);
			}
			TestTrue(TEXT("正常取枪事件最终恢复手持"), Scene.Character->IsHeldWeaponInHand());
			TestFalse(TEXT("全身蒙太奇不继续等待站直"), Traversal->IsActive());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalDrawCancelTest, "CaseGinkgo.Movement.Traversal.Action.DrawInterruption", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalDrawCancelTest::RunTest(const FString& Parameters)
{
	for (const bool bDrop : {false, true})
	{
		GinkgoTraversalTests::FActionScene Scene;
		Scene.Floor();
		Scene.Wall(220., 200.);
		Scene.SpawnCharacter();
		auto* Weapons = Scene.Character->GetWeapons();
		Weapons->SetCarried(0, LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47")));
		Weapons->SetHeldSlot(0);
		Weapons->AbandonTarget();
		Scene.Character->AttachWeaponToHand(0);
		auto* Traversal = Scene.Character->GetTraversal();
		if (!TestTrue(TEXT("取枪中断用例发起高攀"), Traversal->TryStart(FRotator::ZeroRotator))) { continue; }
		bool bInterrupted = false;
		for (int32 Frame = 0; Frame < 240; ++Frame)
		{
			Scene.Step(1.f / 60.f);
			if (bInterrupted || !Traversal->IsRecovering()) { continue; }
			for (auto* Action : Scene.Character->GetAnimActions()->GetRunner()->GetRunningLeaves())
			{
				if (Action->GetActionName() != FName(TEXT("DrawWeapon"))) { continue; }
				TestTrue(TEXT("取枪中断发生在枪离背之前"), Scene.Character->IsWeaponStowed(0));
				// 攀爬还未清理，但手部占用已经释放；随后取消/丢弃由正常武器流程拥有。
				Scene.Character->GetAnimActions()->Cancel(Action, FAnimActionTags::AnimAction_Cancel_OwnerExit);
				if (bDrop) { Weapons->DropWeapon(0, false); }
				bInterrupted = true;
				break;
			}
		}
		TestTrue(TEXT("提前交接实际启动了可中断的取枪"), bInterrupted);
		TestFalse(TEXT("中断取枪不阻塞攀爬清理"), Traversal->IsActive());
		TestFalse(TEXT("攀爬清理不会重新把已取消的枪挂回手"), Scene.Character->IsHeldWeaponInHand());
		if (bDrop) { TestNull(TEXT("丢弃后不恢复旧武器数据"), Weapons->GetCarried(0)); }
		else { TestTrue(TEXT("取消取枪仍保留背上的原武器"), Weapons->GetCarried(0) && Scene.Character->IsWeaponStowed(0)); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalRangeTest, "CaseGinkgo.Movement.Traversal.Action.EntryRange", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalRangeTest::RunTest(const FString& Parameters)
{
	const auto* Assets = LoadObject<UGinkgoTraversalAnimations>(nullptr, TEXT("/Game/Characters/Traversal/DA_TraversalAnimations.DA_TraversalAnimations"));
	if (!TestNotNull(TEXT("范围验收素材"), Assets)) { return false; }
	for (const auto& Shape : {FVector2D(60., 40.), FVector2D(100., 40.), FVector2D(125., 40.),
		FVector2D(60., 200.), FVector2D(100., 200.), FVector2D(125., 200.),
		FVector2D(130., 200.), FVector2D(220., 200.), FVector2D(250., 200.)})
	{
		GinkgoTraversalTests::FActionScene Scene;
		Scene.Floor();
		Scene.Wall(Shape.X, Shape.Y);
		Scene.SpawnCharacter();
		const double Radius = Scene.Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		auto* Movement = Scene.Character->GetGinkgoMovement();
		const auto& Config = Movement->GetConfig();
		for (const FVector2D& Sample : {FVector2D(0., 50.), FVector2D(Config.RunSpeed * .5, 75.), FVector2D(Config.RunSpeed, 100.),
			FVector2D((Config.RunSpeed + Config.SprintSpeed) * .5, 112.5), FVector2D(Config.SprintSpeed, 125.), FVector2D(Config.SprintSpeed * 2., 125.)})
		{
			Movement->Velocity = FVector(Sample.X, 0., 0.);
			for (int32 Index = 0; Index <= FMath::CeilToInt(Sample.Y / 2.); ++Index)
			{
				const double Gap = FMath::Min(Index * 2., Sample.Y);
				Scene.Character->SetActorLocation(FVector(70. - Radius - Gap, 0., 90.));
				const auto Result = Scene.Character->GetTraversal()->Evaluate(FRotator::ZeroRotator);
				FGinkgoTraversalMotion Motion;
				FHitResult Hit;
				const auto* Clip = Assets->Find(Result.Kind);
				const bool bFits = Result.IsValid() && Clip && Ginkgo::Traversal::PrepareMotion(*Clip, Result, *Scene.Character, Motion)
					&& Ginkgo::Traversal::CheckMotion(Motion, *Scene.Character, Hit);
				TestTrue(FString::Printf(TEXT("速度 %.1f cm/s，前缘 %.1f cm，高 %.0f 深 %.0f：整个操作区间可用，阻挡 %s at %s"),
					Sample.X, Gap, Shape.X, Shape.Y, *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString()), bFits);
				TestTrue(TEXT("几何结果保存本次接受距离"), FMath::IsNearlyEqual(Result.Reach, Sample.Y, .01));
			}
			Scene.Character->SetActorLocation(FVector(70. - Radius - Sample.Y - 1., 0., 90.));
			TestFalse(TEXT("探测球半径不能扩大速度对应的操作边界"), Scene.Character->GetTraversal()->Evaluate(FRotator::ZeroRotator).IsValid());
		}
		Movement->SetWantsToSprint(true);
		Movement->Velocity = FVector(0., 0., 700.);
		Scene.Character->SetActorLocation(FVector(70. - Radius - 51., 0., 90.));
		TestFalse(TEXT("只按冲刺或有竖直速度，不扩大静止水平接受距离"), Scene.Character->GetTraversal()->Evaluate(FRotator::ZeroRotator).IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalEntryFlowTest, "CaseGinkgo.Movement.Traversal.Action.EntryFlow", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalEntryFlowTest::RunTest(const FString& Parameters)
{
	for (const double Gap : {0., .1, 25., 50., 75., 100., 112.5, 125.})
	{
		for (const auto& Shape : {FVector2D(100., 40.), FVector2D(100., 200.), FVector2D(125., 200.), FVector2D(220., 200.)})
		{
			for (const double Yaw : {-45., 0., 45.})
			{
				GinkgoTraversalTests::FActionScene Scene;
				Scene.Floor();
				Scene.Wall(Shape.X, Shape.Y);
				Scene.SpawnCharacter();
				Scene.Character->SetActorLocationAndRotation(FVector(36. - Gap, 0., 90.), FRotator(0., Yaw, 0.));
				auto* Traversal = Scene.Character->GetTraversal();
				auto* Movement = Scene.Character->GetGinkgoMovement();
				const auto& Config = Movement->GetConfig();
				if (Gap == 25.) { Movement->Velocity = FVector(500., 0., 0.); }
				if (Gap == 75.) { Movement->Velocity = FVector(Config.RunSpeed * .5, 0., 0.); }
				if (Gap == 100.) { Movement->Velocity = FVector(Config.RunSpeed, 0., 0.); }
				if (Gap == 112.5) { Movement->Velocity = FVector((Config.RunSpeed + Config.SprintSpeed) * .5, 0., 0.); }
				if (Gap == 125.) { Movement->Velocity = FVector(Config.SprintSpeed, 0., 0.); }
				const auto Expected = Traversal->Evaluate(FRotator::ZeroRotator);
				const FString Context = FString::Printf(TEXT("前缘 %.1f cm，高 %.0f 深 %.0f，起步朝向 %.0f"), Gap, Shape.X, Shape.Y, Yaw);
				if (!TestTrue(Context + TEXT(" 可启动"), Traversal->TryStart(FRotator::ZeroRotator))) { continue; }
				TestTrue(Context + TEXT(" 启动不瞬间转身"), FMath::IsNearlyEqual(Scene.Character->GetActorRotation().Yaw, Yaw, .01));
				// 请求在下一次 CMC 物理计算前消费，不能先清零速度，把奔跑接受距离缩回 50 cm。
				TestEqual(Context + TEXT(" 请求尚未推进物理时钟"), Traversal->GetSyncState().Time, 0.f);
				double MaxHandError = 0.;
				bool bLanded = false;
				FVector LandedFeet = FVector::ZeroVector;
				const float Dt = Yaw < 0. ? 1.f / 30.f : Yaw > 0. ? 1.f / 120.f : 1.f / 60.f;
				for (int32 Frame = 0; Frame < FMath::CeilToInt(4.f / Dt); ++Frame)
				{
					Scene.Step(Dt);
					const auto Hands = Traversal->GetHandPose();
					if (Hands.LeftAlpha * Hands.Weight > .99f) { MaxHandError = FMath::Max(MaxHandError, FVector::Dist(Scene.Character->GetMesh()->GetSocketLocation(TEXT("hand_l")), Hands.Left)); }
					if (Hands.RightAlpha * Hands.Weight > .99f) { MaxHandError = FMath::Max(MaxHandError, FVector::Dist(Scene.Character->GetMesh()->GetSocketLocation(TEXT("hand_r")), Hands.Right)); }
					if (!bLanded && !Movement->IsTraversing()) { LandedFeet = Scene.Feet(); bLanded = true; }
					if (!Traversal->IsActive()) { break; }
				}
				TestFalse(Context + TEXT(" 执行中不碰墙取消"), Movement->IsTraversalBlocked());
				TestFalse(Context + TEXT(" 完成并清理"), Traversal->IsActive());
				TestTrue(Context + FString::Printf(TEXT(" 落点正确：实际 %s，目标 %s，朝向 %s"), *LandedFeet.ToCompactString(), *Expected.Landing.Position.ToCompactString(),
					*Scene.Character->GetActorRotation().ToCompactString()), bLanded && LandedFeet.Equals(Expected.Landing.Position, 6.));
				TestTrue(Context + FString::Printf(TEXT(" 真实手腕贴边 %.2f cm"), MaxHandError), MaxHandError < 6.);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalReplayTest, "CaseGinkgo.Movement.Traversal.Net.CorrectionReplay", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalReplayTest::RunTest(const FString& Parameters)
{
	GinkgoTraversalTests::FActionScene Scene;
	Scene.Floor();
	Scene.Wall(220., 200.);
	Scene.SpawnCharacter();
	auto* Traversal = Scene.Character->GetTraversal();
	auto* Movement = Scene.Character->GetGinkgoMovement();
	constexpr float Dt = 1.f / 60.f;
	auto* Weapons = Scene.Character->GetWeapons();
	Weapons->SetCarried(0, LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47")));
	Weapons->SetHeldSlot(0);
	Weapons->AbandonTarget();
	Scene.Character->AttachWeaponToHand(0);
	for (int32 Frame = 0; Frame < 24; ++Frame) { Scene.Step(Dt); }
	TestTrue(TEXT("本地立即接受高攀输入"), Traversal->TryStart(FRotator::ZeroRotator));
	for (int32 Frame = 0; Frame < 30; ++Frame) { Scene.Step(Dt); }
	const auto Saved = Traversal->GetSyncState();
	const FVector Position = Scene.Character->GetActorLocation();
	const FVector Velocity = Movement->Velocity;
	const auto Actions = Scene.Character->GetAnimActions()->GetRunner()->GetActions();
	for (int32 Frame = 0; Frame < 20; ++Frame) { Scene.Step(Dt); }
	const FVector Expected = Scene.Character->GetActorLocation();
	const auto ExpectedState = Traversal->GetSyncState();
	Scene.Character->SetActorLocation(Position);
	Movement->Velocity = Velocity;
	Traversal->RestoreSyncState(Saved);
	Scene.Character->bClientUpdating = true;
	for (int32 Frame = 0; Frame < 20; ++Frame) { Movement->TickComponent(Dt, LEVELTICK_All, nullptr); }
	Scene.Character->bClientUpdating = false;
	Traversal->SyncPresentation();
	TestTrue(TEXT("校正从历史起点重建同一根轨迹"), Scene.Character->GetActorLocation().Equals(Expected, .01));
	TestTrue(TEXT("重放进度与原预测一致"), Traversal->GetSyncState().Matches(ExpectedState));
	TestEqual(TEXT("重放不重复创建手部动作"), Scene.Character->GetAnimActions()->GetRunner()->GetActions(), Actions);

	// 预测已交还 locomotion 后，服务器把进度校正回抓沿；表现也应恢复，不能继续播放取枪或保持旧淡出。
	for (int32 Frame = 0; Frame < 63; ++Frame) { Scene.Step(Dt); }
	TestFalse(TEXT("客户端先到达支撑点"), Movement->IsTraversing());
	Scene.Character->SetActorLocation(Position);
	Movement->Velocity = Velocity;
	Movement->SetMovementMode(MOVE_Custom, 1);
	Traversal->RestoreSyncState(Saved);
	Traversal->SyncPresentation();
	Scene.Step(Dt);
	TestTrue(TEXT("晚到校正能恢复抓沿动作及碰撞计划"), Movement->IsTraversing() && Traversal->GetHandPose().bDrivingRootMotion);
	TestTrue(TEXT("交接后回退抓沿时武器仍回到背部"), Scene.Character->IsWeaponStowed(0));

	FGinkgoTraversalSyncState Rejected;
	Rejected.HandledSequence = Traversal->GetInput().Sequence;
	Movement->SetMovementMode(MOVE_Walking);
	Traversal->RestoreSyncState(Rejected);
	Traversal->SyncPresentation();
	for (int32 Frame = 0; Frame < 4; ++Frame) { Scene.Step(Dt); }
	TestFalse(TEXT("服务器拒绝后同一请求不重新启动"), Traversal->IsActive());
	TestEqual(TEXT("拒绝仍确认输入序号"), Traversal->GetSyncState().HandledSequence, Rejected.HandledSequence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalServerValidationTest, "CaseGinkgo.Movement.Traversal.Net.AuthorityValidation", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalServerValidationTest::RunTest(const FString& Parameters)
{
	GinkgoTraversalTests::FActionScene Client;
	Client.Floor();
	Client.Wall(100., 40.);
	Client.SpawnCharacter();
	TestTrue(TEXT("客户端几何预检允许短翻越"), Client.Character->GetTraversal()->TryStart(FRotator::ZeroRotator));
	const auto Input = Client.Character->GetTraversal()->GetInput();

	GinkgoTraversalTests::FActionScene Server;
	Server.Floor();
	Server.SpawnCharacter();
	auto* Traversal = Server.Character->GetTraversal();
	Traversal->SetInput(Input);
	Server.Step(1.f / 60.f);
	TestEqual(TEXT("服务器没有同一面墙时拒绝客户端预测"), Traversal->GetSyncState().Phase, EGinkgoTraversalPhase::Idle);
	TestEqual(TEXT("拒绝也确认该次按键"), Traversal->GetSyncState().HandledSequence, Input.Sequence);
	TestTrue(TEXT("拒绝时回到普通跳跃语义"), Server.Character->GetGinkgoMovement()->IsFalling());
	TestTrue(TEXT("几何拒绝不抢占手部动作"), Server.Character->GetAnimActions()->GetRunner()->GetActions().IsEmpty());

	Server.Wall(100., 40.);
	Server.Character->SetActorLocation(FVector(0., 0., 90.));
	Server.Character->GetGinkgoMovement()->StopMovementImmediately();
	Server.Character->GetGinkgoMovement()->SetMovementMode(MOVE_Walking);
	Server.Character->StopJumping();
	auto LaterInput = Input;
	LaterInput.Sequence += 3;
	Traversal->SetInput(LaterInput);
	Server.Step(1.f / 60.f);
	TestEqual(TEXT("缺失的请求由后续移动携带的序号补上"), Traversal->GetSyncState().Phase, EGinkgoTraversalPhase::Moving);
	const auto Actions = Server.Character->GetAnimActions()->GetRunner()->GetActions();
	for (int32 Frame = 0; Frame < 5; ++Frame) { Traversal->SetInput(LaterInput); Server.Step(1.f / 60.f); }
	TestTrue(TEXT("重复携带请求不重播起手"), Server.Character->GetAnimActions()->GetRunner()->GetActions() == Actions);
	TestTrue(TEXT("时间连续前进"), Traversal->GetSyncState().Time > .09f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalFatigueTest, "CaseGinkgo.Movement.Traversal.Action.ClearsJumpFatigue", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalFatigueTest::RunTest(const FString& Parameters)
{
	using FTimeline = FGinkgoWindowTimelineModel;
	constexpr float Dt = 1.f / 60.f;
	for (const FVector2D Shape : {FVector2D(100., 40.), FVector2D(100., 200.), FVector2D(220., 200.), FVector2D::ZeroVector})
	{
		for (const uint8 Phase : {uint8(0), uint8(1)})
		{
			GinkgoTraversalTests::FActionScene Scene;
			Scene.Floor();
			const bool bValidWall = Shape.X > 0.;
			if (bValidWall) { Scene.Wall(Shape.X, Shape.Y); }
			Scene.SpawnCharacter();
			auto* Movement = Scene.Character->GetGinkgoMovement();
			auto* Traversal = Scene.Character->GetTraversal();
			const auto Definitions = Movement->GetWindowDefinitions();
			const int32 FatigueIndex = Definitions.IndexOfByPredicate([](const auto& Definition)
			{
				return Definition && Definition->Tag == FMovementTags::Movement_Window_JumpFatigue;
			});
			const int32 SlideIndex = Definitions.IndexOfByPredicate([](const auto& Definition)
			{
				return Definition && Definition->Tag == FMovementTags::Movement_Window_SlideFatigue;
			});
			if (!TestTrue(TEXT("实际角色配置有跳跃和滑铲疲劳"), FatigueIndex != INDEX_NONE && SlideIndex != INDEX_NONE)) { return false; }
			auto Before = Movement->GetSimState();
			Before.Windows.Add({static_cast<uint8>(FatigueIndex), Phase, .1f, 1.f});
			Before.Windows.Add({static_cast<uint8>(SlideIndex), 0, .1f, 0.f});
			Movement->RestoreSimState(Before);
			const FVector Start = Scene.Character->GetActorLocation();
			FTimeline Timeline;
			FTimeline::FFrame Initial;
			Timeline.EndFrame(1., 1. - Dt, Before.Windows, Definitions, Initial);
			int32 StartedEvents = 0;
			const auto EventHandle = Movement->OnMovementWindowEvent.AddLambda([&](const FGinkgoWindowEvent& Event, const auto Changes)
			{
				if (Event.Type == EGinkgoWindowEvent::TraversalStarted) { ++StartedEvents; }
				Timeline.AddEvent(Event, Changes);
			});
			TestEqual(TEXT("几何检测是否接受"), Traversal->TryStart(FRotator::ZeroRotator), bValidWall);
			TestTrue(TEXT("仅提交请求不清除疲劳"), Movement->GetWindowTags().HasTag(FMovementTags::Movement_Window_JumpFatigue));
			TestEqual(TEXT("预检不发攀爬开始事件"), StartedEvents, 0);
			Scene.Step(Dt);
			if (bValidWall)
			{
				TestTrue(TEXT("实际进入攀爬物理阶段"), Movement->IsTraversing());
				TestFalse(TEXT("翻越、低攀与高攀均清除 Armed/Recovering"), Movement->GetWindowTags().HasTag(FMovementTags::Movement_Window_JumpFatigue));
				TestTrue(TEXT("滑铲疲劳不受影响"), Movement->GetWindowTags().HasTag(FMovementTags::Movement_Window_SlideFatigue));
				TestEqual(TEXT("下一跳不再受高度惩罚"), Movement->GetWindowMultiplier(FMovementTags::Movement_Window_Effect_JumpPenalty), 1.f);
				TestEqual(TEXT("开始事件只发一次"), StartedEvents, 1);
				FTimeline::FFrame Frame;
				Timeline.EndFrame(1. + Dt, 1., Movement->GetSimState().Windows, Definitions, Frame);
				if (TestTrue(TEXT("时间轴显示一个事件与一段结束"), Frame.Events.Num() == 1 && Frame.Ends.Num() == 1))
				{
					TestEqual(TEXT("时间轴使用攀爬事件"), Frame.Events[0].Event.Type, EGinkgoWindowEvent::TraversalStarted);
					TestTrue(TEXT("攀爬到疲劳结束的因果连线"), Frame.Events[0].Links.ContainsByPredicate([&](const auto& Link)
					{
						return Link.Definition == FatigueIndex && Link.Kind == FTimeline::ELinkKind::End;
					}));
					TestTrue(TEXT("疲劳结束有事件解释，不显示为校正"), Frame.Ends[0].bExplained);
					TestEqual(TEXT("事件与窗口结束在同一时刻"), Frame.Ends[0].Time, Frame.EventTime);
				}
				// 模拟校正退到起步前再重放相同输入：值状态仍清除，时间轴/相机不再重复派发事件。
				Scene.Character->bClientUpdating = true;
				Scene.Character->SetActorLocation(Start);
				Movement->SetMovementMode(MOVE_Walking);
				Movement->Velocity = FVector::ZeroVector;
				Movement->RestoreSimState(Before);
				Traversal->RestoreSyncState({});
				Movement->TickComponent(Dt, LEVELTICK_All, nullptr);
				Scene.Character->bClientUpdating = false;
				Traversal->SyncPresentation();
				TestFalse(TEXT("校正重放重新清除疲劳状态"), Movement->GetWindowTags().HasTag(FMovementTags::Movement_Window_JumpFatigue));
				TestEqual(TEXT("重放不重复时间轴事件"), StartedEvents, 1);
				Traversal->Cancel();
				TestFalse(TEXT("开始后中断不会恢复已清除的疲劳"), Movement->GetWindowTags().HasTag(FMovementTags::Movement_Window_JumpFatigue));
			}
			else
			{
				TestTrue(TEXT("拒绝攀爬后的普通跳跃仍有疲劳"), Movement->GetWindowTags().HasTag(FMovementTags::Movement_Window_JumpFatigue));
				TestEqual(TEXT("拒绝不产生清除事件"), StartedEvents, 0);
			}
			Movement->OnMovementWindowEvent.Remove(EventHandle);
		}
	}
	return true;
}

#endif

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "AI/GinkgoAICharacter.h"
#include "AI/GinkgoAIController.h"
#include "AI/GinkgoNoiseEmitterComponent.h"
#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Components/LineBatchComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Tests/AutomationEditorCommon.h"
#include "Throwables/GinkgoFlashbangComponent.h"
#include "Throwables/GinkgoFlashPickup.h"
#include "Throwables/GinkgoFlashProjectile.h"
#include "TraversalPIEScene.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace
{
	/** 读取运行时动画的骨骼并逐级合成到网格空间（厘米），包含父骨骼在这一帧的运动。 */
	FTransform SampleMeshPose(const UAnimSequence& Sequence, FName Name, double Time)
	{
		const FReferenceSkeleton& Skeleton = Sequence.GetSkeleton()->GetReferenceSkeleton();
		FTransform Pose = FTransform::Identity;
		for (int32 Bone = Skeleton.FindBoneIndex(Name); Bone != INDEX_NONE; Bone = Skeleton.GetParentIndex(Bone))
		{
			FTransform Local;
			Sequence.GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(Time), false);
			Pose *= Local;
		}
		return Pose;
	}

	/** 用真实飞行、场景遮挡、感知和持枪状态机覆盖从拾取到恢复的完整周期。 */
	class FFlashScenario final : public IAutomationLatentCommand
	{
	public:
		explicit FFlashScenario(FAutomationTestBase& InTest) : Test(InTest) {}

		bool Update() override
		{
			if (FPlatformTime::Seconds() - Started > 60.)
			{
				Test.AddError(FString::Printf(TEXT("闪光弹 PIE 超时，阶段 %d"), Stage));
				return true;
			}
			UWorld* World = GEditor->PlayWorld;
			auto* PC = World ? World->GetFirstPlayerController() : nullptr;
			auto* Player = PC ? Cast<AGinkgoCharacter>(PC->GetPawn()) : nullptr;
			if (!Player || !World->HasBegunPlay()) return false;
			if (!Enemy.IsValid())
			{
				for (TActorIterator<AGinkgoAICharacter> It(World); It; ++It) { Enemy = *It; break; }
			}
			auto* AI = Enemy.IsValid() ? Cast<AGinkgoAIController>(Enemy->GetController()) : nullptr;
			if (!AI) return false;
			auto* Flash = Player->GetFlashbang();
			auto* Guns = Player->GetWeapons();
			const double Now = World->GetTimeSeconds();
			const double Age = Now - StageStart;
			auto Next = [&]() { StageStart = Now; ++Stage; };
			if (Stage > 0) Guns->UpdateHoldState(World->GetDeltaSeconds());
			switch (Stage)
			{
			case 0:
			{
				if (Now < .8) break;
				PC->SetActorTickEnabled(false);
				// 让声源在持握、延时飞行和恢复期间一直存活，避免测试被击杀读档打断。
				Player->GetDamageable()->InitializeVitals(100000.f, {});
				Player->SetActorLocation(FVector(0, -500, 96));
				PC->SetControlRotation(FRotator(-52, 90, 0));
				Enemy->SetActorLocation(FVector(0, 700, 96));
				auto* Rifle = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
				if (!Test.TestNotNull(TEXT("可实际射击的测试武器"), Rifle)) return true;
				Guns->SetCarried(0, Rifle);
				Guns->SetHeldSlot(0);
				Player->AttachWeaponToHand(0);
				auto* Pickup = World->SpawnActor<AGinkgoFlashPickup>(Player->GetActorLocation() + FVector(40, 0, 0), FRotator::ZeroRotator);
				if (!Test.TestNotNull(TEXT("可放置闪光弹"), Pickup)) return true;
				FGinkgoInteractionContext Context;
				Context.Character = Player;
				Pickup->BeginLocalInteract(Context);
				Pickup->Interact(Context);
				Pickup->OnInteractResult(Context, EGinkgoInteractionResult::Accepted);
				Test.TestTrue(TEXT("拾取进入腰槽"), Flash->GetPhase() == EGinkgoFlashPhase::Belt);
				Next();
				break;
			}
			case 1:
			{
				if (Age < 1.7) break;
				auto* Actions = Player->GetAnimActions();
				auto* Reload = Guns->StartStep(FWeaponTags::Weapon_Step_ReloadTactical);
				if (!Test.TestNotNull(TEXT("实际战术换弹动作"), Reload)) return true;
				Actions->FlushPending();
				Test.TestTrue(TEXT("换弹蒙太奇已启动"), Reload->IsRunning());
				Flash->Press();
				Test.TestTrue(TEXT("Q 开始取出"), Flash->GetPhase() == EGinkgoFlashPhase::Draw);
				Test.TestTrue(TEXT("实际换弹被左手投掷抢占"), Reload->IsFinished());
				Guns->EndStep(Reload);
				Next();
				break;
			}
			case 2:
				if (Age < AGinkgoFlashProjectile::FuseSeconds + .8) break;
				Test.TestTrue(TEXT("持握超过引信时长仍停在胸口"), Flash->GetPhase() == EGinkgoFlashPhase::Hold);
				Test.TestFalse(TEXT("未脱手时没有飞行物"), static_cast<bool>(TActorIterator<AGinkgoFlashProjectile>(World)));
				Test.TestTrue(TEXT("本地显示轨迹"), Player->FindComponentByClass<ULineBatchComponent>() != nullptr);
				Rounds = Guns->GetMechanismState(0)->CountLiveRounds();
				Guns->SetWantsToAim(true);
				Guns->HandleWeaponKeys(EGinkgoWeaponKey::Fire);
				Next();
				break;
			case 3:
				if (Age < .55) break;
				Test.TestTrue(TEXT("左手持物期间右手实际开火"), Guns->GetMechanismState(0)->CountLiveRounds() < Rounds);
				Flash->CancelThrow();
				Test.TestTrue(TEXT("脱手前取消仍在腰槽"), Flash->GetPhase() == EGinkgoFlashPhase::Belt);
				Player->GetNoiseEmitter()->ReportShot(Player->GetActorLocation(), 6000.f);
				Next();
				break;
			case 4:
			{
				if (Age < .4) break;
				Test.TestTrue(TEXT("爆闪前已有听觉或视觉记忆"), AI->GetKnowledge().bHasHearing || AI->GetKnowledge().bHasMemory);
				FVector Origin, Velocity;
				AGinkgoFlashProjectile::GetLaunch(*Player, Origin, Velocity);
				const auto* Throw = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Weapons/Throwables/Anims/M_M84_Throw.M_M84_Throw"));
				if (!Test.TestNotNull(TEXT("游戏投掷片段"), Throw)) return true;
				const FVector Release = SampleMeshPose(*Throw, TEXT("ik_hand_gun"), 8.0 / 30.0).GetLocation();
				Test.TestTrue(TEXT("空旷站姿的抛物线起点贴合脱手帧弹体原点"),
					Origin.Equals(Player->GetMesh()->GetComponentTransform().TransformPosition(Release), 1.0));
				TArray<FVector> Path;
				AGinkgoFlashProjectile::PredictPath(*World, Player, Origin, Velocity, Path);
				Enemy->SetActorLocation(Path.Last() + FVector(140, 0, 94));
				Flash->Press();
				Flash->Release();
				// 松开后立刻转镜，实际落点应仍是刚刚预览的方向。
				PC->SetControlRotation(FRotator(25, -90, 0));
				Next();
				break;
			}
			case 5:
				if (!Enemy->bCoverEyes) break;
				Test.TestFalse(TEXT("爆闪清除目击"), AI->GetKnowledge().bHasMemory);
				Test.TestFalse(TEXT("爆闪清除声音"), AI->GetKnowledge().bHasHearing);
				Test.TestFalse(TEXT("轻按一次消耗一枚"), Flash->HasItem());
				Frozen = Enemy->GetActorLocation();
				Next();
				break;
			case 6:
				Player->GetNoiseEmitter()->ReportShot(Player->GetActorLocation(), 6000.f);
				if (Age < 2.65) break;
				Test.TestTrue(TEXT("默认三秒内保持遮眼"), Enemy->bCoverEyes);
				Test.TestTrue(TEXT("受闪期间双感知无记忆"), !AI->GetKnowledge().bHasHearing && !AI->GetKnowledge().bHasMemory);
				Test.TestTrue(TEXT("受闪期间不移动"), FVector::Dist2D(Frozen, Enemy->GetActorLocation()) < 2.f);
				if (auto* Anim = Cast<UGinkgoAnimInstance>(Enemy->GetMesh()->GetAnimInstance()))
				{
					Test.TestTrue(TEXT("动画收到完整遮眼权重"), Anim->GetWeaponRigData().FlashArm.Cover > .99f);
					const FVector Head = Enemy->GetMesh()->GetSocketLocation(TEXT("head"));
					const FVector Wrist = Enemy->GetMesh()->GetSocketLocation(TEXT("hand_l"));
					Test.TestTrue(FString::Printf(TEXT("左腕实际到达头部，距离 %.1f cm"), FVector::Distance(Head, Wrist)), FVector::Distance(Head, Wrist) < 24.f);
				}
				AI->ApplyFlash(.9f);
				Next();
				break;
			case 7:
				if (Age < .55) break;
				Test.TestTrue(TEXT("重复受闪延长原截止时间"), Enemy->bCoverEyes);
				Next();
				break;
			case 8:
				if (Age < .65) break;
				Test.TestFalse(TEXT("期满放下手臂"), Enemy->bCoverEyes);
				Player->GetNoiseEmitter()->ReportShot(Player->GetActorLocation(), 6000.f);
				Next();
				break;
			case 9:
				if (Age < .45) break;
				Test.TestTrue(TEXT("恢复后重新感知新刺激"), AI->GetKnowledge().bHasHearing || AI->GetKnowledge().bHasMemory);
				return true;
			}
			return false;
		}

	private:
		/** 当前自动化结果接收器。 */
		FAutomationTestBase& Test;

		/** 实时时钟超时起点，独立于 PIE 世界。 */
		double Started = FPlatformTime::Seconds();

		/** 当前阶段的世界时间起点（秒）。 */
		double StageStart = 0.0;

		/** 下一次检查所处的步骤。 */
		int32 Stage = 0;

		/** 开火前实际总弹数。 */
		int32 Rounds = 0;

		/** 爆闪发生时的敌人世界位置。 */
		FVector Frozen = FVector::ZeroVector;

		/** 使用测试地图现有的受控敌人。 */
		TWeakObjectPtr<AGinkgoAICharacter> Enemy;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFlashbangWorldTest, "CaseGinkgo.Flashbang.PickupThrowAndRecover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFlashbangWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	ADD_LATENT_AUTOMATION_COMMAND(GinkgoTraversalTests::FStartStandalonePIE());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFlashScenario>(*this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFlashbangExposureTest, "CaseGinkgo.Flashbang.ExposureRanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFlashbangExposureTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/AI/Maps/L_AIPrototype")));
	ADD_LATENT_AUTOMATION_COMMAND(GinkgoTraversalTests::FStartStandalonePIE());
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FUntilCommand>([this]()
	{
		UWorld* World = GEditor->PlayWorld;
		if (!World || !World->HasBegunPlay()) return false;

		// 在地图上方搭出独立的墙和地板。弹体从负 X 侧撞墙，落地后仍与正 X 侧的敌人隔墙。
		const FVector Origin(0., 0., 20000.);
		auto AddSurface = [&](FVector Offset, FVector Extent)
		{
			AActor* Surface = World->SpawnActor<AActor>();
			auto* Box = NewObject<UBoxComponent>(Surface);
			Surface->SetRootComponent(Box);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Box->SetCanEverAffectNavigation(false);
			Box->RegisterComponent();
			Box->SetWorldLocation(Origin + Offset);
		};
		AddSurface(FVector(100., 0., 0.), FVector(100., 450., 300.));
		AddSurface(FVector(0., 0., -120.), FVector(1500., 450., 20.));

		const FVector Start = Origin - FVector(20., 0., 0.);
		auto* Projectile = World->SpawnActor<AGinkgoFlashProjectile>(Start, FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("生成用于范围检测的弹体"), Projectile)) return true;
		Projectile->LaunchVelocity = FVector(600., 0., 0.);
		TArray<FVector> Path;
		AGinkgoFlashProjectile::PredictPath(*World, nullptr, Start, Projectile->LaunchVelocity, Path);
		const FVector Burst = Path.Last();
		TestTrue(TEXT("指示线先抵达墙面，再回到来向一侧的地板"), Path.ContainsByPredicate([](const FVector& P) { return P.X > -6. && P.X < 0.; })
			&& Burst.X < Start.X - 50. && FMath::IsNearlyEqual(Burst.Z, Origin.Z - 96., 1.));

		struct FObserver
		{
			/** 断言名称，描述这一站位的条件。 */
			const TCHAR* Label;

			/** 眼位相对爆点的世界空间偏移（cm）。 */
			FVector Offset;

			/** 为真时正对爆点，否则背对。 */
			bool bFacesFlash;

			/** 引爆后应有的遮眼状态。 */
			bool bShouldCover;

			/** 仅在本次 PIE 世界的同一帧内保存。 */
			AGinkgoAICharacter* Enemy = nullptr;
		};
		FObserver Observers[] = {
			{TEXT("近处隔墙且背对"), FVector(1200., 0., 0.), false, true},
			{TEXT("眼位在 15 m 外但胶囊触及近身球"), FVector(0., -1515., 0.), false, true},
			{TEXT("刚离开近身球且背对"), FVector(0., 1560., 0.), false, false},
			{TEXT("远处正对但隔墙"), FVector(5000., 0., 0.), true, false},
			{TEXT("远处无遮挡但背对"), FVector(-4000., 3000., 0.), false, false},
			{TEXT("恰好 100 m 且正对无遮挡"), FVector(-10000., 0., 0.), true, true},
			{TEXT("超过 100 m 且正对无遮挡"), FVector(-8008., -6006., 0.), true, false},
		};
		// 同一帧设置朝向并引爆，排除 AI 自行转向和行走的影响。
		for (FObserver& Observer : Observers)
		{
			const FRotator Facing = (Observer.bFacesFlash ? -Observer.Offset : Observer.Offset).Rotation();
			Observer.Enemy = World->SpawnActor<AGinkgoAICharacter>(Burst + Observer.Offset, Facing);
			auto* AI = Observer.Enemy ? Cast<AGinkgoAIController>(Observer.Enemy->GetController()) : nullptr;
			if (!TestTrue(TEXT("观测敌人有活跃的控制器"), AI && AI->IsAIActive())) return true;
			Observer.Enemy->AddActorWorldOffset(Burst + Observer.Offset - Observer.Enemy->GetPawnViewLocation());
			AI->SetControlRotation(Facing);
		}
		constexpr float LastSlice = 1.f / 128.f;
		const float Fuse = static_cast<float>(AGinkgoFlashProjectile::FuseSeconds);
		Projectile->Tick(.125f);
		TestTrue(TEXT("碰墙后反弹"), Projectile->LaunchVelocity.X < 0. && Projectile->GetActorLocation().X < Start.X);
		Projectile->Tick(Fuse - .125f - LastSlice);
		TestTrue(TEXT("地面数次弹跳后已停稳"), Projectile->LaunchVelocity.IsNearlyZero());
		TestFalse(TEXT("引信到期前最后一个时间片，近身敌人仍未受闪"), Observers[0].Enemy->bCoverEyes);
		TestTrue(TEXT("实物静止位置与轨迹终点一致"), Projectile->GetActorLocation().Equals(Burst, .05));
		Projectile->Tick(LastSlice);
		for (const FObserver& Observer : Observers)
		{
			TestEqual(Observer.Label, Observer.Enemy->bCoverEyes, Observer.bShouldCover);
		}

		// 高处没有地面：一次长帧越过引信时限也应在引信到期的空中位置触发。
		const FVector AirStart = Origin + FVector(0., 5000., 20000.);
		auto* Air = World->SpawnActor<AGinkgoFlashProjectile>(AirStart, FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("空中弹体"), Air)) return true;
		Air->LaunchVelocity = FVector(400., 0., 100.);
		const double FuseTime = AGinkgoFlashProjectile::FuseSeconds;
		const FVector AirBurst = AirStart + Air->LaunchVelocity * FuseTime + FVector::UpVector * (.5 * FuseTime * FuseTime * World->GetGravityZ());
		AGinkgoFlashProjectile::PredictPath(*World, nullptr, AirStart, Air->LaunchVelocity, Path);
		TestTrue(TEXT("悬空爆点在预览轨迹末端"), Path.Last().Equals(AirBurst, .1));
		Air->Tick(5.f);
		TestTrue(TEXT("长帧只推进到引信时限"), Air->GetActorLocation().Equals(AirBurst, .1));
		TestTrue(TEXT("空中到期同样触发场景闪光"), Air->FindComponentByClass<UPointLightComponent>()->IsVisible());
		return true;
	}, [this]() { AddError(TEXT("等待范围测试 PIE 世界超时")); return true; }, 60.f));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFlashbangChestThrowTest, "CaseGinkgo.Flashbang.ChestThrowPose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFlashbangChestThrowTest::RunTest(const FString& Parameters)
{
	const auto* Hold = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Weapons/Throwables/Anims/M_M84_Prepare.M_M84_Prepare"));
	const auto* Throw = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Weapons/Throwables/Anims/M_M84_Throw.M_M84_Throw"));
	if (!TestNotNull(TEXT("胸前持握片段"), Hold) || !TestNotNull(TEXT("左臂投出片段"), Throw)) return false;
	for (FName Bone : {FName(TEXT("clavicle_l")), FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")), FName(TEXT("hand_l"))})
	{
		const FTransform Held = SampleMeshPose(*Hold, Bone, Hold->GetPlayLength());
		const FTransform First = SampleMeshPose(*Throw, Bone, 0.);
		TestTrue(FString::Printf(TEXT("%s 从持握姿势连续接入"), *Bone.ToString()),
			Held.GetLocation().Equals(First.GetLocation(), .25) && Held.GetRotation().AngularDistance(First.GetRotation()) < FMath::DegreesToRadians(1.0));
	}
	const FVector Start = SampleMeshPose(*Throw, TEXT("hand_l"), 0.).GetLocation();
	FVector Previous = Start;
	// 小白人网格的 +Y 朝前。逐帧检查到脱手，防止中途绕到耳侧再落回正确的终点。
	for (int32 Frame = 1; Frame <= 8; ++Frame)
	{
		const FVector Wrist = SampleMeshPose(*Throw, TEXT("hand_l"), Frame / 30.0).GetLocation();
		TestTrue(FString::Printf(TEXT("第 %d 帧在胸高范围持续前伸"), Frame + 1),
			Wrist.Y >= Previous.Y - .1 && FMath::Abs(Wrist.Z - Start.Z) < 8.0);
		Previous = Wrist;
	}
	TestTrue(TEXT("脱手前手腕有足够的前向行程"), Previous.Y - Start.Y > 22.0);
	return true;
}

#endif

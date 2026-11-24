#include "Animation/GinkgoAnimInstance.h"
#include "Animation/GinkgoPeek.h"
#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoAmmoDefinition.h"
#include "Combat/GinkgoCombatTypes.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Control/GinkgoInputSettingsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoPeekTests
{
	constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** 独立物理世界，供射线与完整动画管线复用；不改动编辑器当前地图。 */
	struct FPeekTestScene
	{
		/** 测试世界拥有者，结束时连同世界上下文释放 */
		UGameInstance* Instance = nullptr;

		/** 仅供本测试使用的游戏世界 */
		UWorld* World = nullptr;

		FPeekTestScene()
		{
			const FString Name = TEXT("PeekTest") + FGuid::NewGuid().ToString();
			Instance = NewObject<UGameInstance>(GEngine);
			Instance->AddToRoot();
			Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
			World = Instance->GetWorld();
			World->InitializeActorsForPlay(FURL());
		}

		~FPeekTestScene()
		{
			// 在世界与组件尚有效时结束武器状态树，不能拖到别的测试的垃圾回收才退出。
			World->BeginTearingDown();
			for (TActorIterator<AActor> Actor(World); Actor; ++Actor)
			{
				Actor->RouteEndPlay(EEndPlayReason::Quit);
			}
			Instance->Shutdown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			Instance->RemoveFromRoot();
		}

		UBoxComponent* Block(const FVector& Location, const FVector& Extent)
		{
			auto* Owner = World->SpawnActor<AActor>();
			auto* Box = NewObject<UBoxComponent>(Owner);
			Owner->SetRootComponent(Box);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(Ginkgo::Combat::WeaponTraceChannel, ECR_Block);
			Box->RegisterComponent();
			Box->SetWorldLocation(Location);
			return Box;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPeekStateTest, "CaseGinkgo.Animation.Peek.DirectionAndHysteresis", GinkgoPeekTests::Flags)
bool FGinkgoPeekStateTest::RunTest(const FString& Parameters)
{
	FGinkgoPeekSettings Settings;
	for (const int32 Rate : { 30, 60, 144 })
	{
		const double Dt = 1.0 / Rate;
		FGinkgoPeekState State;
		State.Step(true, 0.4, Dt, Settings);
		TestEqual(TEXT("进入阈值内不探身"), State.Side, 0);
		for (int32 Frame = 0; Frame < Rate; ++Frame) { State.Step(true, 4.0, Dt, Settings); }
		TestEqual(TEXT("命中偏右时左倾"), State.Side, -1);
		TestTrue(TEXT("各帧率均平滑到完整左倾"), State.Blend.Value < -0.999);
		for (int32 Frame = 0; Frame < Rate; ++Frame) { State.Step(true, 0.4, Dt, Settings); }
		TestEqual(TEXT("降到进入/退出阈值之间仍保持原侧"), State.Side, -1);
		State.Step(true, 0.0, 0.1, Settings);
		TestEqual(TEXT("短暂解除不回正"), State.Side, -1);
		State.Step(true, 0.4, Dt, Settings);
		TestEqual(TEXT("重新遮挡清掉退出计时"), State.ReleaseTime, 0.0);
		for (int32 Frame = 0; Frame < Rate * 2; ++Frame) { State.Step(true, -4.0, Dt, Settings); }
		TestTrue(TEXT("命中偏左可连续转为右倾"), State.Side == 1 && State.Blend.Value > 0.999);
		for (int32 Frame = 0; Frame < Rate * 2; ++Frame) { State.Step(true, 0.0, Dt, Settings); }
		TestTrue(TEXT("持续解除后回正"), State.Side == 0 && FMath::Abs(State.Blend.Value) < 0.001);
		State.Step(true, 4.0, 1.0, Settings);
		State.Step(false, 4.0, 2.0, Settings);
		TestTrue(TEXT("失效/收枪淡出，不保留方向"), State.Side == 0 && FMath::Abs(State.Blend.Value) < 0.001);
		State.Reset();
		TestTrue(TEXT("切换与传送立即清空状态"), State.Blend.Value == 0.0 && State.Blend.Velocity == 0.0);
		for (int32 Frame = 0; Frame < Rate; ++Frame) { State.Step(true, 4.0, Dt, Settings, false); }
		TestTrue(TEXT("实命中有效目标时不发起探身"), State.Side == 0 && State.Blend.Value == 0.0);
		State.Step(true, 4.0, 2.0, Settings);
		for (int32 Frame = 0; Frame < Rate * 2; ++Frame) { State.Step(true, 4.0, Dt, Settings, false); }
		TestTrue(TEXT("探出后打得到目标，仍保持必要姿势"), State.Side == -1 && State.Blend.Value < -0.999);
		for (int32 Frame = 0; Frame < Rate * 2; ++Frame) { State.Step(true, -4.0, Dt, Settings, false); }
		TestTrue(TEXT("命中有效目标时不会发起反向探身"), State.Side == 0 && FMath::Abs(State.Blend.Value) < 0.001);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPeekTraceTest, "CaseGinkgo.Animation.Peek.ReferenceTrace", GinkgoPeekTests::Flags)
bool FGinkgoPeekTraceTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoPeekTests;
	FPeekTestScene Scene;
	auto* Self = Scene.World->SpawnActor<AActor>();
	const FVector Eye(0, 0, 100);
	auto* Cover = Scene.Block(FVector(150, 34, 100), FVector(5, 8, 80));
	Scene.Block(FVector(1000, 0, 100), FVector(5, 500, 500));
	const FTransform Reference(FQuat::Identity, FVector(70, 35, 100));
	const FTransform Corrected(FQuat::Identity, FVector(70, 15, 100));
	FVector Hit;
	double RefError = 0.0;
	double ActualError = 0.0;
	TestTrue(TEXT("未探身参考命中可投影"), Ginkgo::Animation::TracePeek(*Scene.World, *Self, Eye, FQuat::Identity, Reference, FVector::ZeroVector, true, 10000, Hit, RefError));
	TestTrue(TEXT("右侧遮挡产生正偏差"), RefError > 3.0);
	Ginkgo::Animation::TracePeek(*Scene.World, *Self, Eye, FQuat::Identity, Corrected, FVector::ZeroVector, true, 10000, Hit, ActualError);
	TestTrue(TEXT("探身后的实际枪口已绕过遮挡"), FMath::Abs(ActualError) < 0.01);
	FGinkgoPeekState State;
	FGinkgoPeekSettings Settings;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		Ginkgo::Animation::TracePeek(*Scene.World, *Self, Eye, FQuat::Identity, Reference, FVector::ZeroVector, true, 10000, Hit, RefError);
		State.Step(true, RefError, 1.0 / 60.0, Settings);
	}
	TestTrue(TEXT("实际命中已修正仍持续左倾，没有回正反馈"), State.Side == -1 && State.Blend.Value < -0.999);
	Cover->SetWorldLocation(FVector(150, -34, 100));
	Ginkgo::Animation::TracePeek(*Scene.World, *Self, Eye, FQuat::Identity, FTransform(FQuat::Identity, FVector(70, -35, 100)), FVector::ZeroVector, true, 10000, Hit, RefError);
	TestTrue(TEXT("左侧遮挡产生负偏差"), RefError < -3.0);
	const FQuat Rotated = FRotator(23, 137, 0).Quaternion();
	double RotatedError = 0.0;
	Ginkgo::Animation::PeekError(Eye, Rotated, Eye + Rotated.RotateVector(FVector(100, 10, 90)), RotatedError);
	TestTrue(TEXT("视角旋转与竖直误差不改变水平判据"), FMath::IsNearlyEqual(RotatedError, FMath::RadiansToDegrees(FMath::Atan(0.1)), 0.00001));
	TestFalse(TEXT("视点后方不产生反向倾斜"), Ginkgo::Animation::PeekError(Eye, FQuat::Identity, Eye - FVector(100, 0, 0), RotatedError));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPeekPipelineTest, "CaseGinkgo.Animation.Peek.AnimBlueprintPipeline", GinkgoPeekTests::Flags)
bool FGinkgoPeekPipelineTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoPeekTests;
	FPeekTestScene Scene;
	auto* PC = Scene.World->SpawnActor<AGinkgoPlayerController>();
	auto* Character = Scene.World->SpawnActor<AGinkgoCharacter>(FVector(0, 0, 92), FRotator::ZeroRotator);
	auto* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
	if (!PC || !Character || !Weapon) { return false; }
	auto* Player = NewObject<ULocalPlayer>(GEngine);
	Scene.Instance->AddLocalPlayer(Player, FPlatformUserId::CreateFromInternalId(0));
	PC->SetPlayer(Player);
	auto* Settings = UGinkgoInputSettingsSubsystem::Get(PC);
	if (!TestNotNull(TEXT("角色读取真实本地玩家设置"), Settings)) { return false; }
	PC->SetAsLocalPlayerController();
	PC->Possess(Character);
	PC->SetViewTarget(Character);
	PC->SetControlRotation(FRotator::ZeroRotator);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Character->DispatchBeginPlay();
	auto* Camera = Cast<AGinkgoPlayerCameraManager>(PC->PlayerCameraManager);
	auto* Mesh = Character->GetMesh();
	auto* Anim = Cast<UGinkgoAnimInstance>(Mesh->GetAnimInstance());
	if (!TestNotNull(TEXT("实际角色动画实例"), Anim) || !Camera) { return false; }
	Camera->bUseClientSideCameraUpdates = false;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	auto* Weapons = Character->GetWeapons();
	for (int32 Slot = 0; Slot < Weapons->SlotCount; ++Slot) { Weapons->SetCarried(Slot, nullptr); }
	Weapons->SetCarried(0, Weapon);
	Weapons->UpdateHoldState(1.f / 60.f);
	Weapons->SetHeldSlot(0);
	Character->AttachWeaponToHand(0);
	Weapons->SetWantsToAim(true);
	auto Step = [&](const int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			constexpr float Dt = 1.f / 60.f;
			Scene.World->TimeSeconds += Dt;
			Weapons->UpdateHoldState(Dt);
			Mesh->TickAnimation(Dt, false);
			// 同一个自动化引擎帧内推进多个模拟帧，需要显式更新原生 Rig 输入。
			Mesh->GetAnimInstance()->NativeThreadSafeUpdateAnimation(Dt);
			Mesh->RefreshBoneTransforms(nullptr);
			Camera->UpdateCamera(Dt);
		}
	};
	// 先固定准星的收敛距离，后面只给同一阻挡体添加受伤组件，避免目标距离变化改变掩体交点。
	auto* FarBox = Scene.Block(FVector(1000, 0, 100), FVector(5, 500, 500));
	Step(120);
	if (!TestTrue(TEXT("参考捕获节点已在实际 AnimBP 求值"), Anim->GetPeekReference().bValid)) { return false; }
	TestTrue(TEXT("无阻挡时保持中立"), FMath::Abs(Anim->GetPeekWeight()) < 0.01);
	const FVector Eye = Camera->PredictViewLocation(*Character, PC->GetControlRotation());
	const FTransform Gun = Anim->GetPeekGun() * Mesh->GetComponentTransform();
	const FVector Muzzle = Gun.TransformPosition(Weapon->MuzzleOffset);
	FVector CheckMuzzle;
	FVector End;
	bool bCheckHit = false;
	FHitResult CheckHit;
	Weapons->TraceShot(CheckMuzzle, End, bCheckHit, &CheckHit);
	const FVector CoverPosition = Muzzle + (End - Muzzle).GetSafeNormal() * 80;
	auto* Cover = Scene.Block(CoverPosition, FVector(4, 4, 20));
	Settings->SetValue(TEXT("accessibility.autoPeek"), 0);
	Step(90);
	TestTrue(TEXT("辅助功能关闭时真实遮挡也不触发探身"), FMath::Abs(Anim->GetPeekWeight()) < 0.01);
	Settings->SetValue(TEXT("accessibility.autoPeek"), 1);
	auto* NearTarget = NewObject<UGinkgoDamageableComponent>(Cover->GetOwner());
	NearTarget->RegisterComponent();
	NearTarget->Revive();
	FVector CheckPoint;
	Weapons->TraceShot(CheckMuzzle, CheckPoint, bCheckHit, &CheckHit);
	TestTrue(TEXT("实命中指针确实指向近处目标"), bCheckHit && CheckHit.GetActor() == Cover->GetOwner());
	Step(90);
	TestTrue(TEXT("开启自动探头但已能伤害目标时保持中立"), FMath::Abs(Anim->GetPeekWeight()) < 0.01);
	auto* Ammo = NewObject<UGinkgoAmmoDefinition>();
	Ammo->Damage = 1000.f;
	NearTarget->ApplyBulletHit(*Ammo, 1.f, CheckHit, FVector::ForwardVector, 80.f, Character);
	TestTrue(TEXT("近处目标已死亡，不再属于可伤害目标"), NearTarget->IsDead());
	const FVector FootBefore = Mesh->GetSocketLocation(TEXT("foot_l"));
	Step(90);
	const double Weight = Anim->GetPeekWeight();
	const double Error = Anim->GetPeekErrorDegrees();
	AddInfo(FString::Printf(TEXT("探身权重 %.3f，未探身参考偏差 %.3f°"), Weight, Error));
	TestTrue(TEXT("真实障碍驱动与偏差反向的探身"), FMath::Abs(Weight) > 0.2 && Weight * Error < 0.0);
	TestTrue(TEXT("叠加不移动脚的落点"), FVector::Dist(FootBefore, Mesh->GetSocketLocation(TEXT("foot_l"))) < 0.5);
	const FTransform StableReference = Anim->GetPeekGun();
	const FTransform FinalGun = Mesh->GetSocketTransform(TEXT("VB spine_05_ik_hand_gun"), RTS_Component);
	TestTrue(TEXT("真实枪口随探身移动，参考枪保持独立"), FVector::Dist(StableReference.GetLocation(), FinalGun.GetLocation()) > 1.0);
	FVector ActualMuzzle;
	FVector ActualHit;
	bool bActualHit = false;
	double ActualError = 0.0;
	TestTrue(TEXT("最终射击仍使用真实枪口"), Weapons->TraceShot(ActualMuzzle, ActualHit, bActualHit));
	Ginkgo::Animation::PeekError(Camera->GetCameraLocation(), Camera->GetCameraRotation().Quaternion(), ActualHit, ActualError);
	AddInfo(FString::Printf(TEXT("探身后实际命中偏差 %.3f°，参考偏差 %.3f°"), ActualError, Error));
	TestTrue(TEXT("探身已经改善真实命中"), FMath::Abs(ActualError) < FMath::Abs(Error) * 0.25);
	auto* FarTarget = NewObject<UGinkgoDamageableComponent>(FarBox->GetOwner());
	FarTarget->RegisterComponent();
	FarTarget->Revive();
	Weapons->TraceShot(CheckMuzzle, CheckPoint, bCheckHit, &CheckHit);
	TestTrue(TEXT("探出后实命中指针指向远处有效目标"), bCheckHit && CheckHit.GetActor() == FarBox->GetOwner());
	for (int32 Sample = 0; Sample < 4; ++Sample)
	{
		Step(30);
		TestTrue(TEXT("命中有效目标后持续保持，不反复回正"), Anim->GetPeekWeight() * Weight > 0.04);
	}
	Settings->SetValue(TEXT("accessibility.autoPeek"), 0);
	Step(150);
	TestTrue(TEXT("设置关闭立即生效并平滑收回现有探身"), FMath::Abs(Anim->GetPeekWeight()) < 0.01);
	Settings->SetValue(TEXT("accessibility.autoPeek"), 1);
	Step(90);
	TestTrue(TEXT("重新启用后仍能对环境遮挡探身"), Anim->GetPeekWeight() * Weight > 0.04);
	Cover->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Step(150);
	TestTrue(TEXT("遮挡真正解除后平滑回正"), FMath::Abs(Anim->GetPeekWeight()) < 0.01);
	return true;
}

#endif

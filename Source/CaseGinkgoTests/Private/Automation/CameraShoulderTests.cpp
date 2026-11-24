#include "Camera/GinkgoCameraShoulder.h"
#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraContextComponent.h"
#include "Camera/GinkgoCameraMath.h"
#include "Camera/GinkgoPlayerCameraComponent.h"
#include "Character/GinkgoCharacter.h"
#include "Components/BoxComponent.h"
#include "Control/GinkgoPlayerController.h"
#include "Control/GinkgoInputSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoCameraShoulderTests
{
	const EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	/** 独立物理世界；小方块只挡一条射线，验证真实查询的方向、长度和碰撞响应 */
	struct FShoulderTestScene
	{
		/** 测试世界的拥有者，构造时初始化，析构时连同世界上下文释放 */
		UGameInstance* Instance = nullptr;

		/** 独立游戏世界，不与当前编辑器地图共享物理场景 */
		UWorld* World = nullptr;

		/** 被忽略的观察者，特意放在远离相机的位置，防止把角色位置误用作射线起点 */
		AActor* Self = nullptr;

		FShoulderTestScene()
		{
			const FString Name = TEXT("ShoulderTest") + FGuid::NewGuid().ToString();
			Instance = NewObject<UGameInstance>(GEngine);
			Instance->AddToRoot();
			Instance->InitializeStandalone(*Name, CreatePackage(*(TEXT("/Temp/") + Name)));
			World = Instance->GetWorld();
			World->InitializeActorsForPlay(FURL());
			Self = World->SpawnActor<AActor>();
		}

		~FShoulderTestScene()
		{
			Instance->Shutdown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
			Instance->RemoveFromRoot();
		}

		UBoxComponent* Block(const FVector& Location, AActor* Owner = nullptr)
		{
			if (!Owner) { Owner = World->SpawnActor<AActor>(); }
			auto* Box = NewObject<UBoxComponent>(Owner);
			Owner->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(4.0));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore);
			Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
			Box->RegisterComponent();
			Box->SetWorldLocation(Location);
			return Box;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraShoulderDetectionTest, "CaseGinkgo.Camera.Shoulder.DetectionAndCooldown", GinkgoCameraShoulderTests::Flags)
bool FGinkgoCameraShoulderDetectionTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraShoulderTests;
	FShoulderTestScene Scene;
	FGinkgoCameraShoulder Channel;
	FGinkgoCameraShoulderSettings Settings;
	const FVector Eye(1000, 500, 300);
	const FRotator View(12, 70, 0);
	const FVector LeftEye = Eye - FRotator(0, View.Yaw, 0).RotateVector(FVector(0, 300, 0));
	int32 OriginQueries = 0;
	const auto Origin = [&](bool bRight) { ++OriginQueries; return bRight ? Eye : LeftEye; };
	auto Place = [&](double Angle, bool bRight = true)
	{
		return Scene.Block((bRight ? Eye : LeftEye) + FRotator(View.Pitch, View.Yaw + Angle, 0).Vector() * 150.0);
	};
	auto Scan = [&](double Time, bool bAds = true, bool bEnabled = true)
	{
		return Channel.UpdateAuto(*Scene.World, *Scene.Self, Origin, View, bAds, bEnabled, Time, Settings);
	};
	UBoxComponent* Right45 = Place(-45);
	TestFalse(TEXT("非 ADS 不检测"), Scan(9, false));
	TestTrue(TEXT("进入 ADS 立即采样"), Scan(10));
	TestEqual(TEXT("右肩 -45° 射线命中计入右肩 1.0 分"), Channel.GetScores(), FVector2D(0, 1));
	TestTrue(TEXT("分差等于默认容差 1.0 不换肩"), Channel.IsRightShoulder());
	UBoxComponent* RightFront = Place(0);
	TestFalse(TEXT("间隔未到不重新计分"), Scan(10.1));
	TestEqual(TEXT("等待时仍是上次分数"), Channel.GetScores(), FVector2D(0, 1));
	TestTrue(TEXT("间隔到达才检测正前方"), Scan(10.25));
	TestEqual(TEXT("右肩 -45°(1.0) 与正前方(2.0) 合计 3.0 分"), Channel.GetScores(), FVector2D(0, 3));
#if !UE_BUILD_SHIPPING
	const auto& Sample = Channel.GetDebugSample();
	TestEqual(TEXT("调试保存每肩九条共十八条射线"), Sample.Rays.Num(), 18);
	TestEqual(TEXT("右肩采样原点"), Sample.RightOrigin, Eye);
	TestEqual(TEXT("左肩采样原点"), Sample.LeftOrigin, LeftEye);
	TestTrue(TEXT("记录换肩前的选定侧"), Sample.bOnRight);
	int32 BlockedCount = 0;
	for (const auto& Ray : Sample.Rays)
	{
		BlockedCount += Ray.bBlocked ? 1 : 0;
		const FVector Start = Ray.bRightOrigin ? Eye : LeftEye;
		TestTrue(TEXT("两肩每条射线使用同一世界方向和完整射程"), (Ray.End - Start).Equals(FRotator(View.Pitch, View.Yaw + Ray.YawOffset, 0).Vector() * 350, 0.001));
		if (Ray.bBlocked)
		{
			TestTrue(TEXT("命中点位于射程内"), FVector::Dist(Start, Ray.HitPoint) < 350.0);
		}
	}
	TestEqual(TEXT("调试命中条数与实际阻挡一致"), BlockedCount, 2);
#endif
	TestFalse(TEXT("超过容差后切左肩"), Channel.IsRightShoulder());
	TestEqual(TEXT("切换后冷却 15 秒"), Channel.GetCooldownRemaining(10.25), 15.0);
	Right45->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightFront->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Place(15, false); Place(45, false); Place(60, false); Place(0, false);
	FGinkgoCameraParams Params;
	Channel.Apply(Params, 2.f, Settings);
	TestFalse(TEXT("冷却结束前不检测改变了的环境"), Scan(25.249));
	TestEqual(TEXT("非 ADS、间隔和冷却期间均不额外解算候选镜头"), OriginQueries, 4);
	TestEqual(TEXT("冷却期间不重新计分"), Channel.GetScores(), FVector2D(0, 3));
#if !UE_BUILD_SHIPPING
	TestEqual(TEXT("冷却期间调试快照冻结在实际采样时刻"), Channel.GetDebugSample().Time, 10.25);
	TestEqual(TEXT("冻结快照保留真实采样原点"), Channel.GetDebugSample().RightOrigin, Eye);
#endif
	TestTrue(TEXT("冷却到期才再次检测"), Scan(25.25));
	TestTrue(TEXT("只有左肩向前和向右的四条射线命中（1.4 + 1 + 1 + 2 = 5.4）"), Channel.GetScores().Equals(FVector2D(5.4, 0), 1e-4));
	TestTrue(TEXT("左侧受阻也对称地切回右侧"), Channel.IsRightShoulder());
	TestFalse(TEXT("退出 ADS 不做射线检测"), Scan(25.26, false));
	TestEqual(TEXT("退出 ADS 立即清冷却"), Channel.GetCooldownRemaining(25.26), 0.0);
#if !UE_BUILD_SHIPPING
	TestTrue(TEXT("退出 ADS 清空调试射线"), Channel.GetDebugSample().Rays.IsEmpty());
#endif
	TestTrue(TEXT("重新 ADS 不等旧冷却或间隔"), Scan(25.27));
	TestFalse(TEXT("关闭辅助功能立即停止检测"), Scan(25.28, true, false));
	Channel.Toggle(25.29, Settings);
	TestFalse(TEXT("关闭辅助功能仍可手动换肩"), Channel.IsRightShoulder());
	TestEqual(TEXT("手动换肩也保护玩家选择"), Channel.GetCooldownRemaining(25.29), 15.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraShoulderTraceTest, "CaseGinkgo.Camera.Shoulder.TraceOriginRangeAndChannel", GinkgoCameraShoulderTests::Flags)
bool FGinkgoCameraShoulderTraceTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraShoulderTests;
	FShoulderTestScene Scene;
	FGinkgoCameraShoulder Channel;
	FGinkgoCameraShoulderSettings Settings;
	Settings.ScoreTolerance = 12;
	const FVector Eye(700, -500, 200);
	const auto Origin = [&](bool bRight) { return bRight ? Eye : Eye - FVector(0, 1000, 0); };
	// 自身物体占住起点仍不计分；另外九个物体各放在一条射线上，长度 400 cm。
	Scene.Block(Eye, Scene.Self);
	TArray<UBoxComponent*> Boxes;
	for (double Angle : {-60.0, -45.0, -30.0, -15.0, 0.0, 15.0, 30.0, 45.0, 60.0})
	{
		Boxes.Add(Scene.Block(Eye + FRotator(0, Angle, 0).Vector() * 400.0));
	}
	auto Scan = [&](double Time) { return Channel.UpdateAuto(*Scene.World, *Scene.Self, Origin, FRotator::ZeroRotator, true, true, Time, Settings); };
	Scan(1);
	TestEqual(TEXT("默认 3.5 米以外及角色自身不计分"), Channel.GetScores(), FVector2D::ZeroVector);
	for (auto& Ray : Settings.Rays) { Ray.Length = 450.f; }
	Scan(2);
	TestTrue(TEXT("同一肩的整组九条各计一次（总分 11.2）"), Channel.GetScores().Equals(FVector2D(0, 11.2), 1e-4));
	Settings.ScoreTolerance = 0;
	Channel.UpdateAuto(*Scene.World, *Scene.Self, [&](bool) { return Eye; }, FRotator::ZeroRotator, true, true, 2.5, Settings);
	TestTrue(TEXT("视点重合时两肩全被挡住（各 11.2 分）"), Channel.GetScores().Equals(FVector2D(11.2, 11.2), 1e-4));
	TestTrue(TEXT("两肩同分即使零容差也保持当前肩"), Channel.IsRightShoulder());
	Settings.ScoreTolerance = 12;
	Settings.TraceChannel = ECC_Visibility;
	Scan(3);
	TestEqual(TEXT("不符合通道的目标不计分"), Channel.GetScores(), FVector2D::ZeroVector);
	Boxes[0]->SetCollisionResponseToChannel(ECC_Visibility, ECR_Overlap);
	Boxes.Last()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Scan(4);
	TestEqual(TEXT("Overlap 不算遮挡，只有 Block 加分"), Channel.GetScores(), FVector2D(0, 1));
	Scene.Block(Eye + FRotator(0, 60, 0).Vector() * 100)->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Scan(5);
	TestEqual(TEXT("同一射线上多个目标仍只加一分"), Channel.GetScores(), FVector2D(0, 1));
	Channel.UpdateAuto(*Scene.World, *Scene.Self, [](bool) { return FVector::ZeroVector; }, FRotator::ZeroRotator, true, true, 6, Settings);
	TestEqual(TEXT("跟随相机起点移动，不使用角色位置"), Channel.GetScores(), FVector2D::ZeroVector);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraShoulderBlendTest, "CaseGinkgo.Camera.Shoulder.BlendAndPrediction", GinkgoCameraShoulderTests::Flags)
bool FGinkgoCameraShoulderBlendTest::RunTest(const FString& Parameters)
{
	FGinkgoCameraShoulderSettings Settings;
	double PreviousResult = 0;
	for (int32 Fps : {30, 60, 144})
	{
		FGinkgoCameraShoulder Channel;
		Channel.Toggle(0, Settings);
		FGinkgoCameraParams Params;
		for (int32 Frame = 0; Frame < Fps; ++Frame)
		{
			Params = FGinkgoCameraParams();
			Params.PivotOffsetRight = 8;
			Channel.Apply(Params, 1.f / Fps, Settings);
			TestTrue(TEXT("平滑过渡不超出两个肩位"), FMath::Abs(Channel.GetBlend()) <= 1.0);
		}
		TestTrue(TEXT("左肩落位"), Params.ShoulderOffsetRight < -67.9f && Params.PivotOffsetRight < -7.99f);
		TestEqual(TEXT("不改变臂长和垂直构图"), Params.ArmLength, 150.f);
		if (PreviousResult != 0) { TestEqual(TEXT("帧率不影响同一时刻肩位"), Channel.GetBlend(), PreviousResult, 1e-6); }
		PreviousResult = Channel.GetBlend();
	}
	GinkgoCameraShoulderTests::FShoulderTestScene Scene;
	auto* Pawn = Scene.World->SpawnActor<AGinkgoCharacter>();
	auto* PC = Scene.World->SpawnActor<AGinkgoPlayerController>();
	PC->Possess(Pawn);
	auto* Camera = Pawn->FindComponentByClass<UGinkgoPlayerCameraComponent>();
	auto* Config = NewObject<UGinkgoCameraConfig>();
	Config->Modifiers.Reset();
	Config->ParamSprings.Reset();
	Config->OverlayEffects.Reset();
	FMinimalViewInfo View;
	Camera->EvaluateView(*PC, 1.f / 60.f, View, Config);
	const FVector RightEye = View.Location;
	Camera->ToggleShoulder();
	for (int32 Frame = 0; Frame < 90; ++Frame) { Camera->EvaluateView(*PC, 1.f / 60.f, View, Config); }
	TestTrue(TEXT("组件实际输出左肩相机"), RightEye.Y > 0 && View.Location.Y < 0);
	TestTrue(TEXT("瞄准预测沿同一肩位"), Camera->PredictViewLocation(*Pawn, PC->GetControlRotation()).Equals(View.Location, 0.01));
	Camera->SuspendView();
	TestEqual(TEXT("进其他相机清空自动行为冷却"), Camera->GetShoulder().GetCooldownRemaining(0), 0.0);
	TestFalse(TEXT("回玩家视角保留左肩选择"), Camera->GetShoulder().IsRightShoulder());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraShoulderViewpointsTest, "CaseGinkgo.Camera.Shoulder.CandidateViewpoints", GinkgoCameraShoulderTests::Flags)
bool FGinkgoCameraShoulderViewpointsTest::RunTest(const FString& Parameters)
{
	GinkgoCameraShoulderTests::FShoulderTestScene Scene;
	auto* Pawn = Scene.World->SpawnActor<AGinkgoCharacter>();
	auto* PC = Scene.World->SpawnActor<AGinkgoPlayerController>();
	PC->Possess(Pawn);
	auto* Camera = Pawn->FindComponentByClass<UGinkgoPlayerCameraComponent>();
	auto* Context = UGinkgoCameraContextComponent::Find(Pawn);
	auto* Config = NewObject<UGinkgoCameraConfig>();
	Config->Modifiers.Reset();
	Config->ParamSprings.Reset();
	Config->OverlayEffects.Reset();
	Config->Shoulder.Cooldown = 0.f;
	Config->Shoulder.ScoreTolerance = 20.f;
	Config->BaseParams.PivotOffsetRight = 18.f;
	const FRotator Rotation(-23, 57, 0);
	PC->SetControlRotation(Rotation);
	const FGameplayTagContainer Ads(FWeaponTags::Weapon_ADS);
	FMinimalViewInfo View;
	auto Evaluate = [&] { Camera->EvaluateView(*PC, 1.f / 60.f, View, Config); };
	// 世界时钟固定，用退出并重新进入 ADS 开启一次新采样，不跳过运行时的间隔和冷却判断。
	auto Rescan = [&]
	{
		Context->SetTags(EGinkgoCameraContextSource::Weapon, {});
		Evaluate();
		Context->SetTags(EGinkgoCameraContextSource::Weapon, Ads);
		Evaluate();
	};
	Rescan();
	const auto& Sample = Camera->GetShoulder().GetDebugSample();
	TestEqual(TEXT("正常相机入口实际采样两组射线"), Sample.Rays.Num(), 18);
	TestTrue(TEXT("当前右肩起点等于最终镜头"), Sample.RightOrigin.Equals(View.Location, 0.01));
	FGinkgoCameraParams LeftParams = Config->BaseParams;
	LeftParams.PivotOffsetRight *= -1;
	LeftParams.ShoulderOffsetRight *= -1;
	const FVector LeftPivot = Ginkgo::Camera::ComputePivot(Pawn->GetActorLocation(), Rotation, LeftParams);
	const FVector IdealLeft = Ginkgo::Camera::ComputeCameraLocation(LeftPivot, Rotation, LeftParams, &Config->Orbit);
	TestTrue(TEXT("另一肩镜像支点与肩偏移并保留三环轨道"), Sample.LeftOrigin.Equals(IdealLeft, 0.01));
	const FVector StableRight = View.Location;
	Scene.Block(FMath::Lerp(LeftPivot, IdealLeft, .6));
	Rescan();
	const FVector BlockedLeft = Sample.LeftOrigin;
	TestTrue(TEXT("只有左肩路径受阻时使用其碰撞压缩后的位置"), FVector::Dist(BlockedLeft, LeftPivot) < .65 * FVector::Dist(IdealLeft, LeftPivot));
	TestTrue(TEXT("候选侧的压缩不影响当前视点"), View.Location.Equals(StableRight, .01));

	Camera->ToggleShoulder();
	Rescan();
	TestTrue(TEXT("过渡中实际相机尚未抵达左肩"), !View.Location.Equals(BlockedLeft, .01));
	TestTrue(TEXT("过渡中比较两个完整肩位"), Sample.LeftOrigin.Equals(BlockedLeft, .01) && Sample.RightOrigin.Equals(StableRight, .01));
	Config->Shoulder.BlendOmega = 0.f;
	Rescan();
	TestTrue(TEXT("实际换肩落位与此前的候选位置吻合"), View.Location.Equals(BlockedLeft, .01));

	const FGinkgoScope Scope;
	Context->SetSight(Pawn->GetRootComponent(), &Scope, FTransform::Identity);
	Context->SetScalar(FWeaponTags::Weapon_Value_AdsAlpha, 1.f);
	Rescan();
	TestTrue(TEXT("完全进入瞄具时两组射线都从实际眼位发出"), Sample.LeftOrigin.Equals(View.Location, .01) && Sample.RightOrigin.Equals(View.Location, .01));
	TestEqual(TEXT("同一眼位的左右分数相等"), Camera->GetShoulder().GetScores().X, Camera->GetShoulder().GetScores().Y);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOrbitPredictionTest, "CaseGinkgo.Camera.Orbit.ViewAndPrediction", GinkgoCameraShoulderTests::Flags)
bool FGinkgoCameraOrbitPredictionTest::RunTest(const FString& Parameters)
{
	GinkgoCameraShoulderTests::FShoulderTestScene Scene;
	auto* Pawn = Scene.World->SpawnActor<AGinkgoCharacter>();
	auto* PC = Scene.World->SpawnActor<AGinkgoPlayerController>();
	PC->Possess(Pawn);
	auto* Camera = Pawn->FindComponentByClass<UGinkgoPlayerCameraComponent>();
	auto* Config = NewObject<UGinkgoCameraConfig>();
	Config->Modifiers.Reset();
	Config->ParamSprings.Reset();
	Config->OverlayEffects.Reset();
	FMinimalViewInfo View;
	for (const bool bRight : {true, false})
	{
		if (!bRight) { Camera->ToggleShoulder(); }
		for (const double Pitch : {-85.0, -60.0, -20.0, 0.0, 30.0, 45.0, 85.0})
		{
			PC->SetControlRotation(FRotator(Pitch, 35, 0));
			Camera->EvaluateView(*PC, 1.f / 60.f, View, Config);
			const FVector Pivot = Ginkgo::Camera::ComputePivot(Pawn->GetActorLocation(), PC->GetControlRotation(), Camera->GetParams());
			TestTrue(TEXT("实际管线应用三环与当帧肩位"), View.Location.Equals(Ginkgo::Camera::ComputeCameraLocation(Pivot,
				PC->GetControlRotation(), Camera->GetParams(), &Config->Orbit), 0.01));
			TestTrue(TEXT("旋转不延迟"), View.Rotation.Equals(PC->GetControlRotation(), 0.001));
			TestTrue(TEXT("连续转动与换肩时预测视点一致"), Camera->PredictViewLocation(*Pawn, PC->GetControlRotation()).Equals(View.Location, 0.01));
		}
	}
	// 在新轨道中途放一个真实阻挡，确认扫掠压缩沿新 Boom，而不是仍沿旧球面。
	PC->SetControlRotation(FRotator(-45, 0, 0));
	Camera->EvaluateView(*PC, 1.f / 60.f, View, Config);
	const FVector Pivot = Camera->GetDebugFrame().SafePivot;
	const FVector FreeLocation = View.Location;
	Scene.Block(FMath::Lerp(Pivot, FreeLocation, 0.55));
	Camera->EvaluateView(*PC, 1.f / 60.f, View, Config);
	TestTrue(TEXT("新轨道上的阻挡会缩短相机距离"), FVector::Dist(View.Location, Pivot) < FVector::Dist(FreeLocation, Pivot) - 10);
	TestTrue(TEXT("碰撞压缩后的瞄准预测一致"), Camera->PredictViewLocation(*Pawn, PC->GetControlRotation()).Equals(View.Location, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraShoulderRestoreTest, "CaseGinkgo.Camera.Shoulder.RestoreOnAdsExit", GinkgoCameraShoulderTests::Flags)
bool FGinkgoCameraShoulderRestoreTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraShoulderTests;
	FShoulderTestScene Scene;
	FGinkgoCameraShoulderSettings Settings;
	const FVector Eye(1000, 0, 200);
	const auto Origin = [&](bool bRight) { return Eye + FVector(0, bRight ? 68 : -68, 0); };
	TArray<UBoxComponent*> LeftBoxes;
	TArray<UBoxComponent*> RightBoxes;
	for (double Angle : {15.0, 30.0, 45.0})
	{
		LeftBoxes.Add(Scene.Block(Origin(false) + FRotator(0, -Angle, 0).Vector() * 150));
		RightBoxes.Add(Scene.Block(Origin(true) + FRotator(0, Angle, 0).Vector() * 150));
	}
	auto BlockSide = [&](bool bRight)
	{
		for (auto* Box : LeftBoxes) { Box->SetCollisionEnabled(bRight ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly); }
		for (auto* Box : RightBoxes) { Box->SetCollisionEnabled(bRight ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision); }
	};
	auto Scan = [&](FGinkgoCameraShoulder& Channel, double Time, bool bAds, bool bEnabled = true, bool bRestore = true)
	{
		return Channel.UpdateAuto(*Scene.World, *Scene.Self, Origin, FRotator::ZeroRotator, bAds, bEnabled, Time, Settings, bRestore);
	};
	auto Settle = [&](FGinkgoCameraShoulder& Channel)
	{
		FGinkgoCameraParams Params;
		Channel.Apply(Params, 2.f, Settings);
	};
	BlockSide(true);
	FGinkgoCameraShoulder Restore;
	Scan(Restore, 1, true);
	TestFalse(TEXT("本次 ADS 发生实际自动换肩"), Restore.IsRightShoulder());
	Settle(Restore);
	const double BeforeExit = Restore.GetBlend();
	Scan(Restore, 2, false);
	TestTrue(TEXT("冷却中退出也恢复进入时右肩"), Restore.IsRightShoulder());
	TestEqual(TEXT("复原只改目标，不瞬移镜头"), Restore.GetBlend(), BeforeExit);
	TestEqual(TEXT("复原同时清空冷却"), Restore.GetCooldownRemaining(2), 0.0);
	TestFalse(TEXT("退出释放会话基准"), Restore.IsAdsActive());
	Settle(Restore);
	TestTrue(TEXT("下一段过渡到达原肩位"), Restore.GetBlend() > 0.99);

	// 下一次从左肩进入，不能沿用上次的右肩快照。
	Restore.Toggle(3, Settings);
	Settle(Restore);
	Scan(Restore, 3, false);
	BlockSide(false);
	Scan(Restore, 4, true);
	TestTrue(TEXT("左肩受阻自动切右肩"), Restore.IsRightShoulder());
	Settle(Restore);
	Scan(Restore, 5, false);
	TestFalse(TEXT("下一次 ADS 恢复本次进入的左肩"), Restore.IsRightShoulder());
	Settle(Restore);

	// 同一次 ADS 可以多次自动换肩，恢复基准只能在进入时记录一次。
	Scan(Restore, 6, true);
	Settle(Restore);
	BlockSide(true);
	Scan(Restore, 22, true);
	TestFalse(TEXT("冷却到期后再次自动切左肩"), Restore.IsRightShoulder());
	Settle(Restore);
	BlockSide(false);
	Scan(Restore, 38, true);
	TestTrue(TEXT("第三次自动换肩切右肩"), Restore.IsRightShoulder());
	Restore.EndAds();
	TestFalse(TEXT("暂停玩家视角也恢复最初左肩"), Restore.IsRightShoulder());
	TestFalse(TEXT("暂停清理 ADS 会话"), Restore.IsAdsActive());

	FGinkgoCameraShoulder Disabled;
	BlockSide(true);
	Scan(Disabled, 1, true, true, false);
	Scan(Disabled, 2, false, true, false);
	TestFalse(TEXT("关闭复原选项时保持自动切换结果"), Disabled.IsRightShoulder());
	FGinkgoCameraShoulder ParentDisabled;
	Scan(ParentDisabled, 1, true, false);
	ParentDisabled.Toggle(1.1, Settings);
	Scan(ParentDisabled, 2, false, false);
	TestFalse(TEXT("总开关关闭时隐藏的复原值不生效"), ParentDisabled.IsRightShoulder());

	FGinkgoCameraShoulder LiveSettings;
	Scan(LiveSettings, 1, true, false);
	LiveSettings.Toggle(1.1, Settings);
	Settle(LiveSettings);
	Scan(LiveSettings, 2, true);
	Scan(LiveSettings, 3, false);
	TestTrue(TEXT("ADS 中启用辅助也使用真正进入时的肩位"), LiveSettings.IsRightShoulder());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraShoulderCustomRaysTest, "CaseGinkgo.Camera.Shoulder.CustomWeightedRays", GinkgoCameraShoulderTests::Flags)
bool FGinkgoCameraShoulderCustomRaysTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraShoulderTests;
	FShoulderTestScene Scene;
	FGinkgoCameraShoulder Channel;
	FGinkgoCameraShoulderSettings Settings;
	const FVector Eye(1000, 500, 300);
	const FRotator View(12, 70, 0);
	const FVector LeftEye = Eye - FRotator(0, View.Yaw, 0).RotateVector(FVector(0, 650, 0));
	const auto Origin = [&](bool bRight) { return bRight ? Eye : LeftEye; };
	auto Block = [&](float Angle, float Distance, bool bRight = true)
	{
		Scene.Block(Origin(bRight) + FRotator(View.Pitch, View.Yaw + Angle, 0).Vector() * Distance);
	};
	auto Scan = [&](double Time)
	{
		return Channel.UpdateAuto(*Scene.World, *Scene.Self, Origin, View, true, true, Time, Settings);
	};
	Settings.Rays = {{-22.5f, 250.f, .75f}, {37.5f, 150.f, 1.25f}, {0.f, 350.f, .5f}, {90.f, 200.f, 0.f}};
	Settings.ScoreTolerance = 1.f;
	Block(-22.5f, 200.f, false);
	Block(37.5f, 200.f);
	Block(0.f, 300.f);
	Block(90.f, 100.f);
	TestTrue(TEXT("使用自定义四条射线"), Scan(0.0));
	TestEqual(TEXT("每条独立长度：短右线未命中；零分线不加分"), Channel.GetScores(), FVector2D(.75, .5));
	Settings.Rays[1].Length = 300.f;
	TestTrue(TEXT("下次采样使用新长度"), Scan(1.0));
	TestEqual(TEXT("任意角度与小数权重正确累加"), Channel.GetScores(), FVector2D(.75, 1.75));
	TestTrue(TEXT("分差等于小数容差时不换肩"), Channel.IsRightShoulder());
	Settings.ScoreTolerance = .99f;
	Scan(2.0);
	TestFalse(TEXT("严格超过容差才换肩"), Channel.IsRightShoulder());
	const FVector2D BeforeCooldown = Channel.GetScores();
	Settings.Rays.Empty();
	TestFalse(TEXT("改配置不能绕过冷却"), Scan(3.0));
	TestEqual(TEXT("冷却保留上次评分"), Channel.GetScores(), BeforeCooldown);
	Channel.EndAds();
	TestFalse(TEXT("空列表没有物理查询"), Scan(4.0));
	TestEqual(TEXT("空列表清除旧分数"), Channel.GetScores(), FVector2D::ZeroVector);
	Settings.Rays = {{181.f}, {0.f, 0.f}, {0.f, 350.f, -1.f}, {0.f, 350.f, .25f}};
	Channel.ResetDetection();
	TestTrue(TEXT("混合非法配置时仍查询合法条目"), Scan(5.0));
	TestEqual(TEXT("仅右肩正前方命中，非法条目跳过"), Channel.GetScores(), FVector2D(0, .25));
#if !UE_BUILD_SHIPPING
	TestEqual(TEXT("一个合法条目对应两次实际查询"), Channel.GetDebugSample().Rays.Num(), 2);
	TestEqual(TEXT("调试保留原配置索引"), Channel.GetDebugSample().Rays[0].Index, 3);
	TestEqual(TEXT("左肩未命中"), Channel.GetDebugSample().Rays[0].Score, 0.f);
	TestEqual(TEXT("右肩记录实际贡献分"), Channel.GetDebugSample().Rays[1].Score, .25f);
#endif
	Settings.Rays.Init(FGinkgoCameraShoulderRay(0.f, 350.f, .25f), FGinkgoCameraShoulderSettings::MaxRays + 2);
	Settings.ScoreTolerance = 100.f;
	Channel.ResetDetection();
	Scan(6.0);
	TestEqual(TEXT("运行时也限制最大查询数"), Channel.GetScores(), FVector2D(0, FGinkgoCameraShoulderSettings::MaxRays * .25));
	return true;
}

#endif

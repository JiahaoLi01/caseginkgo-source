#include "Algo/AnyOf.h"
#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraMath.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectHash.h"  // GetDerivedClasses：实例化的子对象的各个子类
#include "Utilities/Enumeration/Tags/CameraTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraBlendCurveTest, "CaseGinkgo.Camera.Math.BlendCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraBlendCurveTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Camera;
	const EGinkgoCameraBlendCurve Curves[] = {EGinkgoCameraBlendCurve::Linear, EGinkgoCameraBlendCurve::Smooth, EGinkgoCameraBlendCurve::EaseOut, EGinkgoCameraBlendCurve::EaseIn};
	for (const EGinkgoCameraBlendCurve Curve : Curves)
	{
		for (const float Strength : {1.f, 2.f, 3.5f})
		{
			TestEqual(TEXT("进度 0 时是 0"), BlendCurveValue(0.0, Curve, Strength), 0.0, 1e-12);
			TestEqual(TEXT("进度 1 时是 1"), BlendCurveValue(1.0, Curve, Strength), 1.0, 1e-12);
			double Previous = -1.0;
			for (int32 Step = 0; Step <= 20; ++Step)
			{
				const double Progress = Step / 20.0;
				const double Value = BlendCurveValue(Progress, Curve, Strength);
				TestTrue(TEXT("单调"), Value > Previous);
				Previous = Value;
				TestEqual(TEXT("反函数"), BlendCurveProgress(Value, Curve, Strength), Progress, 1e-6);
			}

			// 与帧率无关：走一大段与走很多小段相同；半路换方向时权重不跳
			const float Coarse = StepCurvedWeight(0.f, true, 0.12, 0.3f, 0.2f, Curve, Curve, Strength);
			float Fine = 0.f;
			for (int32 Step = 0; Step < 36; ++Step)
			{
				Fine = StepCurvedWeight(Fine, true, 0.12 / 36.0, 0.3f, 0.2f, Curve, Curve, Strength);
			}
			TestEqual(TEXT("淡入：大步与小步相同"), Fine, Coarse, 1e-3f);
			TestEqual(TEXT("淡入：照曲线"), Coarse, static_cast<float>(BlendCurveValue(0.4, Curve, Strength)), 1e-5f);
			TestEqual(TEXT("换方向：时间为 0 时不动"), StepCurvedWeight(Coarse, false, 0.0, 0.3f, 0.2f, Curve, Curve, Strength), Coarse);
			TestTrue(TEXT("换方向：往回走，不跳"), FMath::Abs(StepCurvedWeight(Coarse, false, 1e-4, 0.3f, 0.2f, Curve, Curve, Strength) - Coarse) < 0.05f);
			TestEqual(TEXT("淡出：走完到 0"), StepCurvedWeight(1.f, false, 0.2, 0.3f, 0.2f, Curve, Curve, Strength), 0.f, 1e-5f);
		}
	}
	TestEqual(TEXT("强度 1 就是线性"), BlendCurveValue(0.3, EGinkgoCameraBlendCurve::EaseOut, 1.f), 0.3, 1e-9);
	TestEqual(TEXT("先快后慢"), BlendCurveValue(0.5, EGinkgoCameraBlendCurve::EaseOut, 2.f), 0.75, 1e-9);
	TestEqual(TEXT("先慢后快"), BlendCurveValue(0.5, EGinkgoCameraBlendCurve::EaseIn, 2.f), 0.25, 1e-9);
	TestEqual(TEXT("两头缓：一半时一半"), BlendCurveValue(0.5, EGinkgoCameraBlendCurve::Smooth, 3.f), 0.5, 1e-9);
	TestEqual(TEXT("两头缓：四分之一"), BlendCurveValue(0.25, EGinkgoCameraBlendCurve::Smooth, 2.f), 0.1, 1e-9);
	TestEqual(TEXT("时间为 0：一下子到"), StepCurvedWeight(0.f, true, 0.01, 0.f, 0.2f, EGinkgoCameraBlendCurve::EaseOut, EGinkgoCameraBlendCurve::EaseOut, 2.f), 1.f);
	TestEqual(TEXT("时间为 0：一下子回"), StepCurvedWeight(1.f, false, 0.01, 0.3f, 0.f, EGinkgoCameraBlendCurve::EaseOut, EGinkgoCameraBlendCurve::EaseOut, 2.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraPoseTest, "CaseGinkgo.Camera.Math.Pose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraPoseTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Camera;
	FGinkgoCameraParams Params;
	Params.ArmLength = 300.f;
	Params.ShoulderOffsetRight = 60.f;
	Params.ShoulderOffsetUp = 10.f;
	Params.PivotOffsetForward = 20.f;
	Params.PivotOffsetRight = 5.f;
	Params.PivotOffsetUp = 60.f;
	const FVector Capsule(100.0, 200.0, 50.0);
	// 旋转矩阵里的 FMath::SinCos 是多项式近似，相对误差约 1e-7，300 cm 的臂长上是 1e-5 cm 量级（FVector 版的 TestEqual 容差是 float）
	constexpr float VectorTolerance = 1e-3f;

	// 跟随点偏移只跟视角的 Yaw 转：抬头、低头不动跟随点，向上就是世界的上方
	TestEqual(TEXT("朝 +X 时跟随点偏移原样加上"), ComputePivot(Capsule, FRotator(0.0, 0.0, 0.0), Params), Capsule + FVector(20.0, 5.0, 60.0), VectorTolerance);
	TestEqual(TEXT("朝 +Y 时向前是 +Y、向右是 −X"), ComputePivot(Capsule, FRotator(0.0, 90.0, 0.0), Params), Capsule + FVector(-5.0, 20.0, 60.0), VectorTolerance);
	TestEqual(TEXT("抬头不动跟随点"), ComputePivot(Capsule, FRotator(40.0, 90.0, 0.0), Params), ComputePivot(Capsule, FRotator(0.0, 90.0, 0.0), Params), VectorTolerance);

	// 相机在跟随点后面臂长处，向右、向上挪肩部偏移，整体跟着视角转
	const FVector Pivot(0.0, 0.0, 100.0);
	TestEqual(TEXT("朝 +X 时相机在后、右、上"), ComputeCameraLocation(Pivot, FRotator(0.0, 0.0, 0.0), Params), Pivot + FVector(-300.0, 60.0, 10.0), VectorTolerance);
	TestEqual(TEXT("朝 +Y 时后方是 −Y、右方是 −X"), ComputeCameraLocation(Pivot, FRotator(0.0, 90.0, 0.0), Params), Pivot + FVector(-60.0, -300.0, 10.0), VectorTolerance);
	const double Pitch = FMath::DegreesToRadians(30.0);
	TestEqual(TEXT("抬头 30° 时相机绕跟随点转到下方"), ComputeCameraLocation(Pivot, FRotator(30.0, 0.0, 0.0), Params),
		Pivot + FVector(-300.0 * FMath::Cos(Pitch) - 10.0 * FMath::Sin(Pitch), 60.0, -300.0 * FMath::Sin(Pitch) + 10.0 * FMath::Cos(Pitch)), VectorTolerance);
	for (const FRotator& Rotation : {FRotator(-60.0, 33.0, 0.0), FRotator(10.0, -170.0, 0.0), FRotator(85.0, 400.0, 0.0)})
	{
		TestEqual(FString::Printf(TEXT("视角 %s 时离跟随点的距离不变"), *Rotation.ToString()), FVector::Dist(ComputeCameraLocation(Pivot, Rotation, Params), Pivot),
			FMath::Sqrt(300.0 * 300.0 + 60.0 * 60.0 + 10.0 * 10.0), 1e-9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraCompressionTest, "CaseGinkgo.Camera.Math.Compression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraCompressionTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Camera;
	// 碰撞压缩：沿跟随点到期望位置的连线退回，臂长与肩部偏移按同一比例缩短；压缩超过连线的长度时相机在跟随点上
	const FVector Pivot(0.0, 0.0, 100.0);
	const FVector Boom(-300.0, 60.0, 10.0);
	const double BoomLength = Boom.Size();
	TestTrue(TEXT("没有压缩时在期望位置"), ApplyCompression(Pivot, Boom, 0.0) == Pivot + Boom);
	TestEqual(TEXT("压缩一半时在连线的中点"), ApplyCompression(Pivot, Boom, BoomLength * 0.5), Pivot + Boom * 0.5, 1e-9f);
	TestEqual(TEXT("压缩 100 cm 时离跟随点少 100 cm"), FVector::Dist(ApplyCompression(Pivot, Boom, 100.0), Pivot), BoomLength - 100.0, 1e-9);
	const FVector Compressed = ApplyCompression(Pivot, Boom, 100.0) - Pivot;
	TestEqual(TEXT("肩部偏移按同一比例缩短"), Compressed.Y / Compressed.X, Boom.Y / Boom.X, 1e-12);
	TestTrue(TEXT("压缩超过连线的长度时在跟随点上"), ApplyCompression(Pivot, Boom, BoomLength + 50.0) == Pivot);
	TestTrue(TEXT("连线长度为 0 时在跟随点上"), ApplyCompression(Pivot, FVector::ZeroVector, 0.0) == Pivot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraZoomTest, "CaseGinkgo.Camera.Math.ZoomedFieldOfView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraZoomTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Camera;
	// tan(45°) / tan(30°) = √3：常态 90° 开镜 60°（相机文档的例子）
	TestEqual(TEXT("√3 倍时 90° → 60°"), ZoomedFieldOfView(90.f, FMath::Sqrt(3.f)), 60.f, 1e-3f);
	TestEqual(TEXT("1 倍不放大"), ZoomedFieldOfView(90.f, 1.f), 90.f);
	TestEqual(TEXT("小于 1 的倍率不缩小"), ZoomedFieldOfView(90.f, 0.5f), 90.f);
	TestEqual(TEXT("上下文里没有倍率（0）时不放大"), ZoomedFieldOfView(90.f, 0.f), 90.f);
	// 倍率不变时，换了基础视场，放大得一样多：两边 tan(视场 / 2) 之比都是倍率
	const float Wide = ZoomedFieldOfView(110.f, 2.f);
	TestEqual(TEXT("基础 110° 时 tan 之比还是 2"), FMath::Tan(FMath::DegreesToRadians(55.f)) / FMath::Tan(FMath::DegreesToRadians(0.5f * Wide)), 2.f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraConfigDefaultsTest, "CaseGinkgo.Camera.Config.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraConfigDefaultsTest::RunTest(const FString& Parameters)
{
	// 越肩构图（2026-11-01，照《最后生还者2》）：臂长 150、肩部偏移 68 / −3、跟随点在胶囊中心上方 50 cm（M3a 搬进配置时是 300、60 / 10、60）
	const UGinkgoCameraConfig& Config = *GetDefault<UGinkgoCameraConfig>();
	const FGinkgoCameraParams& Params = Config.BaseParams;
	TestEqual(TEXT("臂长 150 cm"), Params.ArmLength, 150.f);
	TestEqual(TEXT("肩部偏移向右 68 cm"), Params.ShoulderOffsetRight, 68.f);
	TestEqual(TEXT("肩部偏移向上 −3 cm"), Params.ShoulderOffsetUp, -3.f);
	TestTrue(TEXT("跟随点在胶囊中心上方 50 cm"), Params.PivotOffsetForward == 0.f && Params.PivotOffsetRight == 0.f && Params.PivotOffsetUp == 50.f);
	TestEqual(TEXT("视场 90°（4:3 画面的水平视场）"), Params.FieldOfView, 90.f);
	TestTrue(TEXT("没有侧倾与后处理"), Params.Roll == 0.f && Params.PostProcessWeight == 0.f);
	TestEqual(TEXT("探测球半径 12 cm"), Config.ProbeRadius, 12.f);

	// 跟随与推远：水平前馈 0.8、竖直不前馈，速度不过低通
	const FGinkgoFollowSettings& Follow = Config.Follow;
	TestTrue(TEXT("水平：ω 10、前馈 0.8、最大滞后 150 cm"), Follow.Omega == 10.f && FMath::IsNearlyEqual(Follow.Feedforward, 0.8f) && Follow.MaxLag == 150.f);
	TestTrue(TEXT("竖直：ω 10、不前馈、最大滞后 100 cm"), Follow.VerticalOmega == 10.f && Follow.VerticalFeedforward == 0.f && Follow.VerticalMaxLag == 100.f);
	TestEqual(TEXT("前馈的速度不过低通"), Follow.FeedforwardFilterOmega, 0.f);
	TestEqual(TEXT("推远的角频率 6"), Config.PushOutOmega, 6.f);

	// 换肩默认权重与容差
	TestEqual(TEXT("换肩分差容差默认 1.0"), Config.Shoulder.ScoreTolerance, 1.f);
	TestEqual(TEXT("换肩默认 9 条加权射线"), Config.Shoulder.Rays.Num(), 9);
	if (Config.Shoulder.Rays.Num() == 9)
	{
		TestTrue(TEXT("换肩射线权重：±15° 1.4、±30° 1.2、±45°/±60° 1.0、正前 2.0"),
			FMath::IsNearlyEqual(Config.Shoulder.Rays[0].Score, 1.4f)
			&& FMath::IsNearlyEqual(Config.Shoulder.Rays[2].Score, 1.2f)
			&& FMath::IsNearlyEqual(Config.Shoulder.Rays[4].Score, 1.f)
			&& FMath::IsNearlyEqual(Config.Shoulder.Rays[8].Score, 2.f));
	}

	// 参数层与叠加层：滑铲的 FOV、臂长与侧倾，三个参数弹簧；滑铲开始的两个冲量弹簧、冲刺落脚的冲量弹簧、开镜的晃动；叠加绕相机自身。
	// 滑铲的都只给拿到起步加速的（疲劳时不要）
	const auto ModifierNamed = [&Config](const TCHAR* Name) { return Config.Modifiers.FindByPredicate([Name](const FGinkgoCameraParamModifier& Modifier) { return Modifier.Name == Name; }); };
	const FGinkgoCameraParamModifier* SlideFov = ModifierNamed(TEXT("SlideFov"));
	const FGinkgoCameraParamModifier* SlideArm = ModifierNamed(TEXT("SlideArm"));
	const FGinkgoCameraParamModifier* SlideRoll = ModifierNamed(TEXT("SlideRoll"));
	TestTrue(TEXT("滑铲 FOV：+10、锁住、两个连续量条件"), SlideFov && SlideFov->Value && SlideFov->Value->Evaluate(nullptr, 0.f) == 10.f && SlideFov->bLatchOff
		&& SlideFov->Activation.Conditions.Num() == 2);
	TestTrue(TEXT("滑铲臂长：−50.2，与 FOV 同起同落"), SlideArm && SlideArm->Value && FMath::IsNearlyEqual(SlideArm->Value->Evaluate(nullptr, 0.f), -50.2f) && SlideArm->bLatchOff
		&& SlideArm->Activation.Conditions.Num() == 2 && SlideArm->BlendInTime == SlideFov->BlendInTime && SlideArm->BlendOutTime == SlideFov->BlendOutTime);
	TestTrue(TEXT("滑铲侧倾：−4°、整个滑铲（只看起步加速）"), SlideRoll && SlideRoll->Value && SlideRoll->Value->Evaluate(nullptr, 0.f) == -4.f && !SlideRoll->bLatchOff
		&& SlideRoll->Activation.Conditions.Num() == 1);
	TestTrue(TEXT("滑铲的视场、臂长是速度类，侧倾不是"), SlideFov && SlideArm && SlideRoll && SlideFov->Category == FCameraTags::Camera_Category_Speed
		&& SlideArm->Category == FCameraTags::Camera_Category_Speed && !SlideRoll->Category.IsValid());
	const FGinkgoCameraParamModifier* AdsFov = ModifierNamed(TEXT("AdsFov"));
	const UGinkgoCameraValueZoomedFov* AdsFovValue = AdsFov ? Cast<UGinkgoCameraValueZoomedFov>(AdsFov->Value) : nullptr;
	TestTrue(TEXT("开镜的视场：覆盖，盖在弹簧之后，开镜时激活，值按枪的倍率放大，屏蔽速度类"), AdsFov && AdsFov->IsAfterSprings()
		&& AdsFov->Activation.Query.Matches(FGameplayTagContainer(FWeaponTags::Weapon_ADS)) && AdsFov->BlendInCurve != EGinkgoCameraBlendCurve::Linear
		&& AdsFovValue && AdsFovValue->ZoomScalar == FWeaponTags::Weapon_Value_AdsZoom && AdsFov->BlockedCategories.HasTag(FCameraTags::Camera_Category_Speed));
	TestEqual(TEXT("三个参数弹簧"), Config.ParamSprings.Num(), 3);
	TArray<FName> Effects;
	for (const UGinkgoCameraOverlayEffect* Effect : Config.OverlayEffects)
	{
		Effects.Add(Effect ? Effect->Name : NAME_None);
	}
	TestTrue(TEXT("叠加效果：滑铲震动两个、开枪（参数在枪上）、冲刺落脚、开镜、操作枪（拉栓，参数在枪上）"), Effects == TArray<FName>({TEXT("SlideShake"), TEXT("SlideShakeRoll"),
		TEXT("WeaponRecoil"), TEXT("FootstepShake"), TEXT("AdsSway"), TEXT("WeaponHandling")}));
	const UGinkgoCameraImpulseShake* SlideShake = Cast<UGinkgoCameraImpulseShake>(Config.OverlayEffects[0]);
	TestTrue(TEXT("滑铲的震动不收疲劳的滑铲（起步加速为 0）"), SlideShake && SlideShake->MinMagnitude >= 1.f);
	TestTrue(TEXT("滑铲震动默认冲量 (-75, 14.4, 8.5)"), SlideShake && SlideShake->Impulse.Equals(FRotator(-75.0, 14.4, 8.5), 1e-3));
	const UGinkgoCameraImpulseShake* FootstepShake = Config.OverlayEffects.IsValidIndex(3) ? Cast<UGinkgoCameraImpulseShake>(Config.OverlayEffects[3]) : nullptr;
	TestTrue(TEXT("冲刺落脚震动默认冲量 (-4, -2, -4)、ω 25、ζ 0.45"), FootstepShake
		&& FootstepShake->Impulse.Equals(FRotator(-4.0, -2.0, -4.0), 1e-3)
		&& FMath::IsNearlyEqual(FootstepShake->Omega, 25.f)
		&& FMath::IsNearlyEqual(FootstepShake->Zeta, 0.45f));
	// 第一人称开镜（2026-11-12 你手测以后说的）：滑铲开始的震动收掉（滑铲过程中开镜不需要镜子抖动）；开枪的相机后坐留着、枪跟着转；
	// 拉栓的震动留着、枪不跟（只晃画面，不影响子弹）
	const UGinkgoCameraOverlayEffect* SlideShakeRoll = Config.OverlayEffects.IsValidIndex(1) ? Config.OverlayEffects[1].Get() : nullptr;
	TestTrue(TEXT("第一人称时滑铲开始的震动收掉"), SlideShake && SlideShakeRoll && SlideShake->FirstPersonScale == 0.f && SlideShakeRoll->FirstPersonScale == 0.f);
	const UGinkgoCameraCarriedShake* WeaponRecoil = Config.OverlayEffects.IsValidIndex(2) ? Cast<UGinkgoCameraCarriedShake>(Config.OverlayEffects[2]) : nullptr;
	TestTrue(TEXT("开枪的相机后坐：第一人称留着，枪跟着转"), WeaponRecoil && WeaponRecoil->Event == FWeaponTags::Weapon_Event_Fire
		&& WeaponRecoil->FirstPersonScale == 1.f && WeaponRecoil->bSightFollows);
	const UGinkgoCameraCarriedShake* WeaponHandling = Config.OverlayEffects.IsValidIndex(5) ? Cast<UGinkgoCameraCarriedShake>(Config.OverlayEffects[5]) : nullptr;
	TestTrue(TEXT("拉栓的震动：收操作枪的事件，第一人称留着，枪不跟"), WeaponHandling && WeaponHandling->Event == FWeaponTags::Weapon_Event_Handling
		&& WeaponHandling->FirstPersonScale == 1.f && !WeaponHandling->bSightFollows);
	TestTrue(TEXT("叠加绕相机自身"), Config.OverlayPivot == EGinkgoCameraOverlayPivot::Camera);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraConfigDisplayNamesTest, "CaseGinkgo.Camera.Config.DisplayNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraConfigDisplayNamesTest::RunTest(const FString& Parameters)
{
#if WITH_METADATA
	// 调参面板与细节面板是给策划看的（2026-11-01 你要的：所有参数都有中文的 DisplayName，而不是参数的名字）。
	// 相机配置里能编辑的每一项都要有：连同里面的结构、数组的元素、实例化的子对象（数值来源、叠加效果）的每个子类；引擎的结构（FRotator 等）不算
	TSet<const UStruct*> Visited;
	int32 Checked = 0;
	const auto IsOurs = [](const UStruct& Struct) { return Struct.GetOutermost()->GetFName() == TEXT("/Script/CaseGinkgo"); };
	const auto HasChinese = [](const FString& Text) { return Algo::AnyOf(Text, [](const TCHAR Char) { return Char >= 0x4E00 && Char <= 0x9FFF; }); };
	TFunction<void(const UStruct&)> Visit;
	TFunction<void(const FProperty&)> VisitValue = [&](const FProperty& Property)
	{
		if (const FStructProperty* Struct = CastField<FStructProperty>(&Property))
		{
			Visit(*Struct->Struct);
		}
		else if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
		{
			VisitValue(*Array->Inner);
		}
		else if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(&Property); Object && Object->HasAnyPropertyFlags(CPF_InstancedReference))
		{
			Visit(*Object->PropertyClass);
			TArray<UClass*> Derived;
			GetDerivedClasses(Object->PropertyClass, Derived);
			for (const UClass* Class : Derived)
			{
				Visit(*Class);
			}
		}
	};
	Visit = [&](const UStruct& Struct)
	{
		if (Visited.Contains(&Struct) || !IsOurs(Struct))
		{
			return;
		}
		Visited.Add(&Struct);
		// 只看这个结构自己声明的：父类（叠加效果的基类）自己会被走到，一项只查一次
		for (TFieldIterator<FProperty> It(&Struct, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Edit) || !It->GetOwnerStruct() || !IsOurs(*It->GetOwnerStruct()))
			{
				continue;
			}
			++Checked;
			TestTrue(FString::Printf(TEXT("%s.%s 有中文的 DisplayName（现在是\"%s\"）"), *It->GetOwnerStruct()->GetName(), *It->GetName(), *It->GetMetaData(TEXT("DisplayName"))),
				HasChinese(It->GetMetaData(TEXT("DisplayName"))));
			VisitValue(**It);
		}
	};
	Visit(*UGinkgoCameraConfig::StaticClass());
	// 配置 8、参数 9、跟随 7、修改器 12、激活 2、条件 4、选参 1、参数弹簧 2、两种数值来源 1 + 2、叠加效果 3、冲量弹簧 8、噪声 9、噪声的轴 2：共 70
	TestTrue(FString::Printf(TEXT("查过的项不少于 70（%d）"), Checked), Checked >= 70);
#endif
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

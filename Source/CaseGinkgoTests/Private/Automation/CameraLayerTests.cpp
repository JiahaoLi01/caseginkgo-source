#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraContextComponent.h"
#include "Camera/GinkgoCameraOverlay.h"
#include "Camera/GinkgoCameraParamLayer.h"
#include "Misc/AutomationTest.h"
#include "Utilities/Enumeration/Tags/CameraTags.h"
#include "Utilities/Enumeration/Tags/MovementTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"

#if WITH_DEV_AUTOMATION_TESTS

// 相机的上下文组件、参数层与叠加层（M3c、M3d、M3e，Docs/Design/CameraPipeline.md）

namespace GinkgoCameraLayerTests
{
	const TArray<int32> FrameRates = {30, 60, 144};

	/** 帧末恰好是 1/6 s 的整数倍（30、60、144 fps 共有的时刻）时返回它是第几个，否则 INDEX_NONE */
	int32 CommonSampleIndex(const double FrameEnd)
	{
		const double Sixths = FrameEnd * 6.0;
		return FMath::IsNearlyEqual(Sixths, FMath::RoundToDouble(Sixths), 1e-9) ? FMath::RoundToInt32(Sixths) : INDEX_NONE;
	}

	/** 只有测试里加的修改器、没有参数弹簧与叠加效果的配置 */
	UGinkgoCameraConfig* MakeEmptyConfig()
	{
		UGinkgoCameraConfig* Config = NewObject<UGinkgoCameraConfig>();
		Config->Modifiers.Reset();
		Config->ParamSprings.Reset();
		Config->OverlayEffects.Reset();
		return Config;
	}

	FGinkgoCameraParamModifier MakeModifier(UGinkgoCameraConfig& Config, const FName Name, const FName Member, const EGinkgoCameraBlend Blend, const float Value,
		const FGameplayTag& Tag, const float BlendTime)
	{
		FGinkgoCameraParamModifier Modifier;
		Modifier.Name = Name;
		Modifier.Param = FGinkgoCameraParamField::FromMember(Member);
		Modifier.Blend = Blend;
		Modifier.Activation.Query = FGameplayTagQuery::MakeQuery_MatchAllTags(FGameplayTagContainer(Tag));
		UGinkgoCameraValueConstant* Constant = NewObject<UGinkgoCameraValueConstant>(&Config);
		Constant->Value = Value;
		Modifier.Value = Constant;
		Modifier.BlendInTime = BlendTime;
		Modifier.BlendOutTime = BlendTime;
		return Modifier;
	}

	FGameplayTagContainer Tags(std::initializer_list<FGameplayTag> List)
	{
		FGameplayTagContainer Container;
		for (const FGameplayTag& Tag : List)
		{
			Container.AddTag(Tag);
		}
		return Container;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraContextTest, "CaseGinkgo.Camera.Context.Snapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraContextTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();

	// 两方各写各的一份，相机读并集；一方覆盖自己那份时不擦掉另一方的
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Grounded, FMovementTags::Movement_Slide}));
	Context->SetTags(EGinkgoCameraContextSource::Weapon, Tags({FWeaponTags::Weapon_ADS}));
	TestEqual(TEXT("并集里三个标签"), Context->GetTags().Num(), 3);
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Grounded}));
	TestTrue(TEXT("移动覆盖自己那份：滑铲没了，开镜还在"), !Context->GetTags().HasTagExact(FMovementTags::Movement_Slide)
		&& Context->GetTags().HasTagExact(FWeaponTags::Weapon_ADS) && Context->GetTags().HasTagExact(FMovementTags::Movement_Grounded));

	// 连续量：覆盖写，没写过的是空
	TestFalse(TEXT("没写过的连续量是空"), Context->FindScalar(FMovementTags::Movement_Value_HorizontalSpeed).IsSet());
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 500.f);
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 476.25f);
	TestEqual(TEXT("连续量是最后写的"), Context->FindScalar(FMovementTags::Movement_Value_HorizontalSpeed).Get(0.f), 476.25f);

	// 事件：按入队的先后一次取空
	Context->PushEvent({FMovementTags::Movement_Event_SlideStarted, 1.5, 438.f});
	Context->PushEvent({FMovementTags::Movement_Event_Landed, 1.6, 500.f});
	TArray<FGinkgoCameraEvent> Events = Context->TakeEvents();
	TestTrue(TEXT("两个事件，按先后"), Events.Num() == 2 && Events[0].Tag == FMovementTags::Movement_Event_SlideStarted && Events[1].Time == 1.6);
	TestEqual(TEXT("取空以后没有了"), Context->TakeEvents().Num(), 0);

	// 满了丢最早的（会打一条警告）
	AddExpectedError(TEXT("相机事件队列满了"), EAutomationExpectedErrorFlags::Contains, 3);
	for (int32 Index = 0; Index < UGinkgoCameraContextComponent::MaxEvents + 3; ++Index)
	{
		Context->PushEvent({FMovementTags::Movement_Event_Landed, static_cast<double>(Index), 0.f});
	}
	Events = Context->TakeEvents();
	TestTrue(TEXT("满了丢最早的三个"), Events.Num() == UGinkgoCameraContextComponent::MaxEvents && Events[0].Time == 3.0);
	TestEqual(TEXT("入过队的累计"), Context->GetPushedEventCount(), 2 + UGinkgoCameraContextComponent::MaxEvents + 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraParamFieldTest, "CaseGinkgo.Camera.ParamLayer.ParamField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraParamFieldTest::RunTest(const FString& Parameters)
{
	// 按反射选参：FGinkgoCameraParams 的每个 float 都能按"结构.属性"找到、读写
	const TArray<FGinkgoCameraParamFieldInfo>& Fields = Ginkgo::Camera::GetParamFieldInfoList(FGinkgoCameraParams::StaticStruct());
	TestEqual(TEXT("九个参数"), Fields.Num(), 9);
	TestTrue(TEXT("缓存：再取是同一份"), &Fields == &Ginkgo::Camera::GetParamFieldInfoList(FGinkgoCameraParams::StaticStruct()));
	const FGinkgoCameraParamField Arm = FGinkgoCameraParamField::FromMember(GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, ArmLength));
	TestEqual(TEXT("名字是 结构.属性"), Arm.Name.ToString(), FString(TEXT("GinkgoCameraParams.ArmLength")));
	FGinkgoCameraParams Params;
	for (const FGinkgoCameraParamFieldInfo& Info : Fields)
	{
		TestTrue(Info.Field.Name.ToString() + TEXT(" 找得到"), Ginkgo::Camera::FindParamProperty(Info.Field) == Info.Property);
		Ginkgo::Camera::SetParamValue(Params, *Info.Property, 12.5f);
		TestEqual(Info.Field.Name.ToString() + TEXT(" 写了读回来"), Ginkgo::Camera::GetParamValue(Params, *Info.Property), 12.5f);
	}
	TestEqual(TEXT("写的是那个成员"), Params.ArmLength, 12.5f);
	TestTrue(TEXT("没有的名字找不到"), Ginkgo::Camera::FindParamProperty(FGinkgoCameraParamField(TEXT("GinkgoCameraParams.Nope"))) == nullptr);
	TestEqual(TEXT("下拉框的选项就是这九个"), UGinkgoCameraConfig::GetParamFieldOptions().Num(), 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraParamLayerDesignTest, "CaseGinkgo.Camera.ParamLayer.DesignExample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraParamLayerDesignTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 相机文档的例子：冲刺 FOV +5、滑铲 +10、开镜覆盖为 60 且屏蔽速度类增量。开镜完全生效时 FOV = 60；开镜淡入到一半时，速度类增量也只剩一半
	UGinkgoCameraConfig* Config = MakeEmptyConfig();
	Config->BaseParams.FieldOfView = 90.f;
	const FName Fov = GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, FieldOfView);
	FGinkgoCameraParamModifier Sprint = MakeModifier(*Config, TEXT("Sprint"), Fov, EGinkgoCameraBlend::Delta, 5.f, FMovementTags::Movement_Sprint, 0.2f);
	Sprint.Category = FCameraTags::Camera_Category_Speed;
	FGinkgoCameraParamModifier Slide = MakeModifier(*Config, TEXT("Slide"), Fov, EGinkgoCameraBlend::Delta, 10.f, FMovementTags::Movement_Slide, 0.2f);
	Slide.Category = FCameraTags::Camera_Category_Speed;
	FGinkgoCameraParamModifier Ads = MakeModifier(*Config, TEXT("Ads"), Fov, EGinkgoCameraBlend::Override, 60.f, FWeaponTags::Weapon_ADS, 0.3f);
	Ads.BlockedCategories.AddTag(FCameraTags::Camera_Category_Speed);
	Config->Modifiers = {Sprint, Slide, Ads};

	UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Sprint, FMovementTags::Movement_Slide}));
	FGinkgoCameraParamLayer Layer;
	TestEqual(TEXT("第一次求值直接放到位：90 + 5 + 10"), Layer.Evaluate(*Config, Context, 1.0 / 60.0).FieldOfView, 105.f, 1e-4f);

	Context->SetTags(EGinkgoCameraContextSource::Weapon, Tags({FWeaponTags::Weapon_ADS}));
	const FGinkgoCameraParams Half = Layer.Evaluate(*Config, Context, 0.15);
	TestEqual(TEXT("开镜淡入一半：覆盖 90 → 75，速度类增量只剩一半（+7.5）"), Half.FieldOfView, 82.5f, 1e-4f);
	TestEqual(TEXT("增量被屏蔽的程度是开镜的权重"), Layer.GetModifierStates()[0].Block, 0.5f, 1e-6f);
	TestEqual(TEXT("开镜完全生效时 60°"), Layer.Evaluate(*Config, Context, 0.15).FieldOfView, 60.f, 1e-4f);

	// 关掉参数层：直接是基础值
	TestEqual(TEXT("关掉时是基础值"), Layer.Evaluate(*Config, Context, 1.0 / 60.0, false).FieldOfView, 90.f);
	TestEqual(TEXT("再开时直接放到位"), Layer.Evaluate(*Config, Context, 1.0 / 60.0).FieldOfView, 60.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraParamLayerOverridesTest, "CaseGinkgo.Camera.ParamLayer.OverlappingOverrides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraParamLayerOverridesTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 两个覆盖同时淡入淡出（低优先级的 60 淡出、高优先级的 70 淡入）：按优先级逐层插值，结果连续，两头是两个覆盖的值
	UGinkgoCameraConfig* Config = MakeEmptyConfig();
	const FName Fov = GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, FieldOfView);
	FGinkgoCameraParamModifier Low = MakeModifier(*Config, TEXT("Low"), Fov, EGinkgoCameraBlend::Override, 60.f, FWeaponTags::Weapon_ADS, 0.3f);
	FGinkgoCameraParamModifier High = MakeModifier(*Config, TEXT("High"), Fov, EGinkgoCameraBlend::Override, 70.f, FMovementTags::Movement_Slide, 0.3f);
	High.Priority = 1;
	// 列表里高优先级的在前：顺序按优先级，不按列表
	Config->Modifiers = {High, Low};
	UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
	Context->SetTags(EGinkgoCameraContextSource::Weapon, Tags({FWeaponTags::Weapon_ADS}));
	FGinkgoCameraParamLayer Layer;
	TestEqual(TEXT("只有低的：60"), Layer.Evaluate(*Config, Context, 1.0 / 144.0).FieldOfView, 60.f, 1e-4f);

	Context->SetTags(EGinkgoCameraContextSource::Weapon, FGameplayTagContainer());
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Slide}));
	float Previous = 60.f;
	float MaxStep = 0.f;
	for (int32 Frame = 0; Frame < 72; ++Frame)
	{
		const float Current = Layer.Evaluate(*Config, Context, 1.0 / 144.0).FieldOfView;
		MaxStep = FMath::Max(MaxStep, FMath::Abs(Current - Previous));
		Previous = Current;
	}
	// 每一步最多是两个权重各走 Δt / 0.3 带来的变化：|70 − 90|·Δt/0.3 + |60 − 90|·Δt/0.3 ≈ 1.16°
	AddInfo(FString::Printf(TEXT("两个覆盖同时淡入淡出：144 fps 下每帧最多变 %.4f°"), MaxStep));
	TestTrue(FString::Printf(TEXT("连续：每帧最多变 %.3f°"), MaxStep), MaxStep < 1.2f);
	TestEqual(TEXT("高的满了：70"), Previous, 70.f, 1e-4f);
	Context->SetTags(EGinkgoCameraContextSource::Weapon, Tags({FWeaponTags::Weapon_ADS}));
	TestEqual(TEXT("两个都满时高优先级的盖住低的"), Layer.Evaluate(*Config, Context, 1.0).FieldOfView, 70.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraParamLayerFlipTest, "CaseGinkgo.Camera.ParamLayer.FrameRateIndependence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraParamLayerFlipTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 滑铲 FOV（类默认值）：滑铲开始在共有的帧边界（0.5 s），之后水平速度线性地从 1000 降到 300 cm/s（每秒 700），在 0.5 + 0.748 s 跨过跑速，
	// 那一刻不在任何帧边界上。连续量的条件按前后两帧的余量插出跨过的时刻，权重从那一刻起淡出：三种帧率在共有的时刻权重相同，FOV 只差参数弹簧的二阶小量
	const UGinkgoCameraConfig* Config = GetDefault<UGinkgoCameraConfig>();
	const FGinkgoCameraParamModifier* SlideFov = Config->Modifiers.FindByPredicate([](const FGinkgoCameraParamModifier& Modifier) { return Modifier.Name == TEXT("SlideFov"); });
	if (!TestNotNull(TEXT("类默认值带滑铲 FOV"), SlideFov))
	{
		return false;
	}
	const auto Speed = [](const double Time) { return Time < 0.5 ? 762.0 : FMath::Max(300.0, 1000.0 - 700.0 * (Time - 0.5)); };
	TMap<int32, TMap<int32, TPair<float, float>>> Samples;
	TMap<int32, TPair<float, float>> WithoutFlip;
	for (const int32 Fps : FrameRates)
	{
		UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
		FGinkgoCameraParamLayer Layer;
		const double Step = 1.0 / Fps;
		for (int32 Frame = 0; Frame <= Fps * 2; ++Frame)
		{
			const double Time = Frame * Step;
			const bool bSliding = Time > 0.5 + 1e-9;
			Context->SetTags(EGinkgoCameraContextSource::Movement, bSliding ? Tags({FMovementTags::Movement_Grounded, FMovementTags::Movement_Slide}) : Tags({FMovementTags::Movement_Grounded}));
			Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, Speed(Time));
			Context->SetScalar(FMovementTags::Movement_Value_SlideBoost, bSliding ? 238.f : 0.f);
			Context->SetScalar(FMovementTags::Movement_Value_RunSpeed, 476.25f);
			const FGinkgoCameraParams Params = Layer.Evaluate(*Config, Context, Frame == 0 ? 0.0 : Step);
			const int32 Index = CommonSampleIndex(Time);
			if (Index != INDEX_NONE)
			{
				Samples.FindOrAdd(Fps).Add(Index, {Layer.GetModifierStates()[0].Weight, Params.FieldOfView});
			}
		}
	}
	float MaxWeight = 0.f;
	float MaxFov = 0.f;
	for (const int32 Fps : {30, 60})
	{
		for (const TPair<int32, TPair<float, float>>& Sample : Samples[Fps])
		{
			const TPair<float, float>* Reference = Samples[144].Find(Sample.Key);
			MaxWeight = FMath::Max(MaxWeight, FMath::Abs(Sample.Value.Key - Reference->Key));
			MaxFov = FMath::Max(MaxFov, FMath::Abs(Sample.Value.Value - Reference->Value));
		}
	}
	AddInfo(FString::Printf(TEXT("滑铲 FOV：共有时刻与 144 fps 相差：权重 %.2e，FOV %.6f°"), MaxWeight, MaxFov));
	TestTrue(FString::Printf(TEXT("共有时刻的权重与 144 fps 相差 %.2e"), MaxWeight), MaxWeight < 1e-5f);
	TestTrue(FString::Printf(TEXT("共有时刻的 FOV 与 144 fps 相差 %.4f°"), MaxFov), MaxFov < 0.05f);
	// 1.0 s 时（离跨过跑速还有 0.248 s）满权重、FOV 已到 +10 附近；2 s 时淡完
	TestEqual(TEXT("跨过跑速之前满权重"), Samples[144][6].Key, 1.f);
	TestEqual(TEXT("2 s 时淡完"), Samples[144][12].Key, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraParamLayerAfterSpringsTest, "CaseGinkgo.Camera.ParamLayer.AfterSprings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraParamLayerAfterSpringsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 开镜的视场：覆盖成基础视场按倍率放大，盖在参数弹簧之后，权重沿曲线走，时间到了就到位；速度类的增量按权重屏蔽
	UGinkgoCameraConfig* Config = MakeEmptyConfig();
	Config->BaseParams.FieldOfView = 90.f;
	Config->BaseParams.ArmLength = 150.f;
	const FName Fov = GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, FieldOfView);
	const FName Arm = GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, ArmLength);
	FGinkgoCameraParamModifier SlideFov = MakeModifier(*Config, TEXT("SlideFov"), Fov, EGinkgoCameraBlend::Delta, 10.f, FMovementTags::Movement_Slide, 0.f);
	SlideFov.Category = FCameraTags::Camera_Category_Speed;
	FGinkgoCameraParamModifier SlideArm = MakeModifier(*Config, TEXT("SlideArm"), Arm, EGinkgoCameraBlend::Delta, -24.f, FMovementTags::Movement_Slide, 0.f);
	SlideArm.Category = FCameraTags::Camera_Category_Speed;
	FGinkgoCameraParamModifier Ads;
	Ads.Name = TEXT("AdsFov");
	Ads.Param = FGinkgoCameraParamField::FromMember(Fov);
	Ads.Blend = EGinkgoCameraBlend::Override;
	Ads.BlockedCategories.AddTag(FCameraTags::Camera_Category_Speed);
	Ads.Activation.Query = FGameplayTagQuery::MakeQuery_MatchAllTags(FGameplayTagContainer(FWeaponTags::Weapon_ADS));
	Ads.bAfterSprings = true;
	Ads.BlendInTime = 0.3f;
	Ads.BlendOutTime = 0.2f;
	Ads.BlendInCurve = EGinkgoCameraBlendCurve::EaseOut;
	Ads.BlendOutCurve = EGinkgoCameraBlendCurve::Smooth;
	Ads.BlendCurveStrength = 2.f;
	UGinkgoCameraValueZoomedFov* Zoomed = NewObject<UGinkgoCameraValueZoomedFov>(Config);
	Zoomed->ZoomScalar = FWeaponTags::Weapon_Value_AdsZoom;
	Ads.Value = Zoomed;
	Config->Modifiers = {SlideFov, SlideArm, Ads};
	FGinkgoCameraParamSpring FovSpring;
	FovSpring.Param = SlideFov.Param;
	FovSpring.Omega = 15.f;
	FGinkgoCameraParamSpring ArmSpring;
	ArmSpring.Param = SlideArm.Param;
	ArmSpring.Omega = 15.f;
	Config->ParamSprings = {FovSpring, ArmSpring};
	const float ZoomedFov = 60.f;
	const FGameplayTagContainer Aiming = Tags({FWeaponTags::Weapon_ADS});

	// 开镜 0.3 s（先快后慢）、0.5 s 时放下（0.2 s，两头缓）：每一帧视场就在曲线上（弹簧在这个覆盖之前，追不追得上与它无关），三种帧率在共有时刻一样
	TMap<int32, TArray<float>> Samples;
	for (const int32 Rate : FrameRates)
	{
		UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
		Context->SetScalar(FWeaponTags::Weapon_Value_AdsZoom, FMath::Sqrt(3.f));
		FGinkgoCameraParamLayer Layer;
		Layer.Evaluate(*Config, Context, 0.0);
		float MaxError = 0.f;
		TArray<float>& Common = Samples.Add(Rate);
		Common.Add(90.f);
		const double Step = 1.0 / Rate;
		const int32 ReleaseFrame = FMath::RoundToInt32(0.5 * Rate);
		for (int32 Frame = 1; Frame <= Rate; ++Frame)
		{
			// 标签在帧的开头变：这一帧开头按下，这一帧末已经走了一帧
			const double End = Frame * Step;
			Context->SetTags(EGinkgoCameraContextSource::Weapon, Frame <= ReleaseFrame ? Aiming : FGameplayTagContainer());
			const float Field = Layer.Evaluate(*Config, Context, Step).FieldOfView;
			const double In = FMath::Clamp(End / 0.3, 0.0, 1.0);
			const double Out = FMath::Clamp((End - ReleaseFrame * Step) / 0.2, 0.0, 1.0);
			const double Weight = Frame <= ReleaseFrame ? 1.0 - FMath::Square(1.0 - In) : 1.0 - Out * Out / (Out * Out + (1.0 - Out) * (1.0 - Out));
			MaxError = FMath::Max(MaxError, FMath::Abs(Field - (90.f + static_cast<float>(Weight) * (ZoomedFov - 90.f))));
			if (CommonSampleIndex(End) != INDEX_NONE)
			{
				Common.Add(Field);
			}
		}
		TestTrue(FString::Printf(TEXT("%d fps：视场每一帧都在曲线上，最多差 %.4f°"), Rate, MaxError), MaxError < 2e-3f);
		TestEqual(TEXT("放下走完回到基础值"), Common.Last(), 90.f, 2e-3f);
	}
	TestEqual(TEXT("共有的时刻一样多"), Samples[30].Num(), Samples[144].Num());
	for (int32 Index = 0; Index < Samples[144].Num(); ++Index)
	{
		TestEqual(TEXT("共有时刻的视场与 144 fps 相同"), Samples[30][Index], Samples[144][Index], 2e-3f);
	}

	// 半路放下：权重不跳，从现在的地方往回走
	{
		UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
		Context->SetScalar(FWeaponTags::Weapon_Value_AdsZoom, FMath::Sqrt(3.f));
		FGinkgoCameraParamLayer Layer;
		Layer.Evaluate(*Config, Context, 0.0);
		Context->SetTags(EGinkgoCameraContextSource::Weapon, Aiming);
		const float Before = Layer.Evaluate(*Config, Context, 0.1).FieldOfView;
		Context->SetTags(EGinkgoCameraContextSource::Weapon, FGameplayTagContainer());
		const float After = Layer.Evaluate(*Config, Context, 1.0 / 240.0).FieldOfView;
		// 两头缓的中间最快：2 / 0.2 s × 30° × 1/240 s = 1.25°，就是这条曲线的速度，不是跳
		TestTrue(FString::Printf(TEXT("半路放下：视场不跳（%.2f → %.2f）"), Before, After), After > Before && After - Before < 1.5f);
		TestEqual(TEXT("目标变的时刻算在帧的开头"), Layer.GetModifierStates()[2].FlipTime, 0.0, 1e-9);
	}

	// 第一次求值直接放到位
	UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
	Context->SetScalar(FWeaponTags::Weapon_Value_AdsZoom, FMath::Sqrt(3.f));
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Slide}));
	FGinkgoCameraParamLayer Layer;
	FGinkgoCameraParams Params = Layer.Evaluate(*Config, Context, 0.0);
	TestEqual(TEXT("不开镜滑铲：90 + 10"), Params.FieldOfView, 100.f, 1e-4f);
	TestEqual(TEXT("不开镜滑铲：150 − 24"), Params.ArmLength, 126.f, 1e-4f);

	// 线性、淡入 4 s，走 2 s 时权重正好一半；停在这里等弹簧追上：滑铲的增量只剩一半（+5、−12），视场再往 60 盖一半
	Config->Modifiers[2].BlendInTime = 4.f;
	Config->Modifiers[2].BlendInCurve = EGinkgoCameraBlendCurve::Linear;
	Context->SetTags(EGinkgoCameraContextSource::Weapon, Aiming);
	Layer.Evaluate(*Config, Context, 2.0);
	Config->Modifiers[2].BlendInTime = 1e9f;
	Params = Layer.Evaluate(*Config, Context, 2.0);
	TestEqual(TEXT("开镜一半：屏蔽的程度就是权重"), Layer.GetModifierStates()[0].Block, 0.5f, 1e-6f);
	TestEqual(TEXT("开镜一半：95 往 60 盖一半"), Params.FieldOfView, 77.5f, 1e-3f);
	TestEqual(TEXT("开镜一半：臂长的增量也只剩一半"), Params.ArmLength, 138.f, 1e-3f);

	// 开镜到底：视场就是 60，滑铲的增量都没了
	Config->Modifiers[2].BlendInTime = 0.3f;
	Layer.Evaluate(*Config, Context, 2.0);
	Params = Layer.Evaluate(*Config, Context, 2.0);
	TestEqual(TEXT("开镜到底：60°"), Params.FieldOfView, ZoomedFov, 1e-3f);
	TestEqual(TEXT("开镜到底：臂长回到基础值"), Params.ArmLength, 150.f, 1e-3f);
	TestEqual(TEXT("目标里也算上这个覆盖"), Layer.GetTarget().FieldOfView, ZoomedFov, 1e-3f);
	TestEqual(TEXT("权重到 1"), Layer.GetModifierStates()[2].Weight, 1.f);

	// 没有倍率时不放大；淡入时间为 0 时一下子到；开关关掉时权重是 0
	UGinkgoCameraContextComponent* NoZoom = NewObject<UGinkgoCameraContextComponent>();
	NoZoom->SetTags(EGinkgoCameraContextSource::Weapon, Aiming);
	FGinkgoCameraParamLayer NoZoomLayer;
	TestEqual(TEXT("上下文里没有倍率：覆盖成基础视场"), NoZoomLayer.Evaluate(*Config, NoZoom, 0.0).FieldOfView, 90.f, 1e-4f);
	Config->Modifiers[2].BlendInTime = 0.f;
	{
		UGinkgoCameraContextComponent* Instant = NewObject<UGinkgoCameraContextComponent>();
		Instant->SetScalar(FWeaponTags::Weapon_Value_AdsZoom, FMath::Sqrt(3.f));
		FGinkgoCameraParamLayer InstantLayer;
		InstantLayer.Evaluate(*Config, Instant, 0.0);
		Instant->SetTags(EGinkgoCameraContextSource::Weapon, Aiming);
		TestEqual(TEXT("淡入时间为 0：一下子到"), InstantLayer.Evaluate(*Config, Instant, 1.0 / 60.0).FieldOfView, ZoomedFov, 1e-3f);
	}
	Config->Modifiers[2].bEnabled = false;
	FGinkgoCameraParamLayer OffLayer;
	TestEqual(TEXT("关掉：权重是 0，滑铲的增量不屏蔽"), OffLayer.Evaluate(*Config, Context, 0.0).FieldOfView, 100.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraParamLayerAdsKeepsRollTest, "CaseGinkgo.Camera.ParamLayer.AdsKeepsSlideRoll",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraParamLayerAdsKeepsRollTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 类默认值：拿到加速的滑铲里开镜，侧倾还在，开镜的过程中也不动；视场、臂长的增量按开镜的权重退掉（2026-11-03 你手测说开镜把侧倾吞掉了）
	const UGinkgoCameraConfig* Config = GetDefault<UGinkgoCameraConfig>();
	UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
	Context->SetScalar(FMovementTags::Movement_Value_SlideBoost, 100.f);
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 900.f);
	Context->SetScalar(FMovementTags::Movement_Value_RunSpeed, 476.f);
	Context->SetScalar(FWeaponTags::Weapon_Value_AdsZoom, FMath::Sqrt(3.f));
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Slide}));
	FGinkgoCameraParamLayer Layer;
	FGinkgoCameraParams Params = Layer.Evaluate(*Config, Context, 0.0);
	TestEqual(TEXT("滑铲：侧倾 −4°"), Params.Roll, -4.f, 1e-3f);
	TestEqual(TEXT("滑铲：视场 +10"), Params.FieldOfView, Config->BaseParams.FieldOfView + 10.f, 1e-3f);
	TestEqual(TEXT("滑铲：臂长 −50.2"), Params.ArmLength, Config->BaseParams.ArmLength - 50.2f, 1e-3f);

	Context->SetTags(EGinkgoCameraContextSource::Weapon, Tags({FWeaponTags::Weapon_ADS}));
	float MaxRollError = 0.f;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		Params = Layer.Evaluate(*Config, Context, 1.0 / 60.0);
		MaxRollError = FMath::Max(MaxRollError, FMath::Abs(Params.Roll + 4.f));
	}
	TestTrue(FString::Printf(TEXT("开镜的过程中侧倾不动（差 %.2e°）"), MaxRollError), MaxRollError < 1e-3f);
	const int32 RollIndex = Config->Modifiers.IndexOfByPredicate([](const FGinkgoCameraParamModifier& Modifier) { return Modifier.Name == TEXT("SlideRoll"); });
	TestEqual(TEXT("侧倾不被屏蔽"), Layer.GetModifierStates()[RollIndex].Block, 0.f);
	TestEqual(TEXT("开镜：视场按枪的倍率"), Params.FieldOfView, 60.f, 1e-2f);
	TestEqual(TEXT("开镜：臂长的增量退掉"), Params.ArmLength, Config->BaseParams.ArmLength, 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraParamLayerLatchTest, "CaseGinkgo.Camera.ParamLayer.Latch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraParamLayerLatchTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 锁住：滑铲中速度低于跑速一次以后，同一次滑铲里再快起来也不再加；滑铲结束、再铲时重新来
	const UGinkgoCameraConfig* Config = GetDefault<UGinkgoCameraConfig>();
	UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
	Context->SetScalar(FMovementTags::Movement_Value_RunSpeed, 476.25f);
	Context->SetScalar(FMovementTags::Movement_Value_SlideBoost, 400.f);
	FGinkgoCameraParamLayer Layer;
	const auto Frame = [&](const bool bSliding, const float Speed)
	{
		Context->SetTags(EGinkgoCameraContextSource::Movement, bSliding ? Tags({FMovementTags::Movement_Slide}) : FGameplayTagContainer());
		Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, Speed);
		Layer.Evaluate(*Config, Context, 0.1);
		return Layer.GetModifierStates()[0];
	};
	Frame(false, 700.f);
	TestTrue(TEXT("滑铲、快：激活"), Frame(true, 900.f).bTarget);
	TestTrue(TEXT("低于跑速：失效并锁住"), !Frame(true, 450.f).bTarget && Layer.GetModifierStates()[0].bLatched);
	TestFalse(TEXT("同一次滑铲里再快起来：不再激活"), Frame(true, 600.f).bTarget);
	TestTrue(TEXT("滑铲结束：解锁"), !Frame(false, 600.f).bLatched);
	TestTrue(TEXT("再铲：激活"), Frame(true, 900.f).bTarget);
	Context->SetScalar(FMovementTags::Movement_Value_SlideBoost, 0.f);
	TestFalse(TEXT("没拿到起步加速（滑铲疲劳）：不激活"), Frame(true, 900.f).bTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOverlayImpulseTest, "CaseGinkgo.Camera.Overlay.Impulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraOverlayImpulseTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 冲量弹簧按事件的时刻拆开推进：一个事件在共有的帧边界（0.5 s），一个在帧的中间（0.73 s，三种帧率都不在边界上）。
	// 共有的时刻三种帧率的输出相同（只差浮点舍入）；每个事件一次冲量，别的事件、强度不够的不算
	UGinkgoCameraConfig* Config = MakeEmptyConfig();
	UGinkgoCameraImpulseShake* Shake = NewObject<UGinkgoCameraImpulseShake>(Config);
	Shake->Name = TEXT("Shake");
	Shake->Event = FMovementTags::Movement_Event_SlideStarted;
	Shake->MinMagnitude = 1.f;
	Shake->Impulse = FRotator(-118.0, 18.0, 30.0);
	Shake->FieldOfViewImpulse = 20.f;
	Shake->Omega = 39.5f;
	Shake->Zeta = 0.3f;
	Config->OverlayEffects = {Shake};
	const TArray<FGinkgoCameraEvent> Timeline = {
		{FMovementTags::Movement_Event_SlideStarted, 0.5, 438.f},
		{FMovementTags::Movement_Event_Landed, 0.6, 500.f},
		{FMovementTags::Movement_Event_SlideStarted, 0.73, 438.f},
		{FMovementTags::Movement_Event_SlideStarted, 0.9, 0.f},
	};
	TMap<int32, TMap<int32, FGinkgoCameraOverlayOutput>> Samples;
	TMap<int32, int32> Impulses;
	for (const int32 Fps : FrameRates)
	{
		FGinkgoCameraOverlayLayer Layer;
		const double Step = 1.0 / Fps;
		for (int32 Frame = 1; Frame <= Fps * 2; ++Frame)
		{
			const double End = Frame * Step;
			const double Start = End - Step;
			TArray<FGinkgoCameraEvent> Events = Timeline.FilterByPredicate([Start, End](const FGinkgoCameraEvent& Event) { return Event.Time >= Start - 1e-9 && Event.Time < End - 1e-9; });
			FGinkgoCameraOverlayStep OverlayStep;
			OverlayStep.Events = Events;
			OverlayStep.DeltaTime = Step;
			OverlayStep.FrameStartTime = Start;
			OverlayStep.Clock = End;
			const FGinkgoCameraOverlayOutput Output = Layer.Step(*Config, OverlayStep);
			const int32 Index = CommonSampleIndex(End);
			if (Index != INDEX_NONE)
			{
				Samples.FindOrAdd(Fps).Add(Index, Output);
			}
		}
		Impulses.Add(Fps, Layer.GetStates()[0].ImpulseCount);
	}
	double MaxDifference = 0.0;
	double Peak = 0.0;
	for (const int32 Fps : {30, 60})
	{
		for (const TPair<int32, FGinkgoCameraOverlayOutput>& Sample : Samples[Fps])
		{
			const FGinkgoCameraOverlayOutput& Reference = Samples[144][Sample.Key];
			MaxDifference = FMath::Max(MaxDifference, static_cast<double>(FMath::Abs(Sample.Value.Rotation.Pitch - Reference.Rotation.Pitch)));
			MaxDifference = FMath::Max(MaxDifference, static_cast<double>(FMath::Abs(Sample.Value.Rotation.Roll - Reference.Rotation.Roll)));
			MaxDifference = FMath::Max(MaxDifference, static_cast<double>(FMath::Abs(Sample.Value.FieldOfView - Reference.FieldOfView)));
			Peak = FMath::Max(Peak, static_cast<double>(FMath::Abs(Reference.Rotation.Pitch)));
		}
	}
	AddInfo(FString::Printf(TEXT("冲量弹簧：共有时刻与 144 fps 相差 %.2e°，俯仰峰值 %.3f°"), MaxDifference, Peak));
	TestTrue(FString::Printf(TEXT("共有时刻与 144 fps 相差 %.2e°（峰值 %.2f°）"), MaxDifference, Peak), MaxDifference < 1e-4 && Peak > 0.5);
	TestTrue(TEXT("每个滑铲开始一次冲量：落地、强度为 0 的不算"), Impulses[30] == 2 && Impulses[60] == 2 && Impulses[144] == 2);
	TestTrue(TEXT("2 s 时晃停了"), FMath::Abs(Samples[144][12].Rotation.Pitch) < 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOverlayCarriedTest, "CaseGinkgo.Camera.Overlay.Carried",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraOverlayCarriedTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 事件带来的冲量：每个冲量一个弹簧，ω、ζ 是它自己的。一个事件在共有的帧边界（0.5 s），一个在帧的中间（0.73 s），两个事件的弹簧不一样（换了枪）
	UGinkgoCameraConfig* Config = MakeEmptyConfig();
	UGinkgoCameraCarriedShake* Shake = NewObject<UGinkgoCameraCarriedShake>(Config);
	Shake->Name = TEXT("Carried");
	Shake->Event = FWeaponTags::Weapon_Event_Fire;
	Config->OverlayEffects = {Shake};
	FGinkgoCameraImpulse Kick;
	Kick.Rotation = FRotator(70.0, -10.0, 0.0);
	Kick.Omega = 14.f;
	Kick.Zeta = 0.75f;
	Kick.ReturnOmega = 8.f;
	Kick.ReturnZeta = 1.f;
	FGinkgoCameraImpulse Tremor;
	Tremor.Rotation = FRotator(37.0, 0.0, 60.0);
	Tremor.FieldOfView = 90.f;
	Tremor.Omega = 50.f;
	Tremor.Zeta = 0.3f;
	Tremor.ReturnOmega = Tremor.Omega;
	Tremor.ReturnZeta = Tremor.Zeta;
	const auto MakeEvent = [](const FGameplayTag& Tag, const double Time, TArray<FGinkgoCameraImpulse> Impulses)
	{
		FGinkgoCameraEvent Event;
		Event.Tag = Tag;
		Event.Time = Time;
		Event.Impulses = MoveTemp(Impulses);
		return Event;
	};
	const TArray<FGinkgoCameraEvent> Timeline = {
		MakeEvent(FWeaponTags::Weapon_Event_Fire, 0.5, {Kick}),
		MakeEvent(FMovementTags::Movement_Event_Landed, 0.6, {Kick}),
		MakeEvent(FWeaponTags::Weapon_Event_Fire, 0.73, {Tremor, Kick}),
	};
	struct FRun
	{
		TMap<int32, FGinkgoCameraOverlayOutput> Samples;
		int32 Impulses = 0;
		int32 MaxSprings = 0;
		int32 LeftSprings = 0;
	};
	const auto Run = [Config](const TConstArrayView<FGinkgoCameraEvent> Events, const int32 Fps)
	{
		FRun Out;
		FGinkgoCameraOverlayLayer Layer;
		const double Step = 1.0 / Fps;
		for (int32 Frame = 1; Frame <= Fps * 3; ++Frame)
		{
			const double End = Frame * Step;
			const double Start = End - Step;
			TArray<FGinkgoCameraEvent> Current;
			for (const FGinkgoCameraEvent& Event : Events)
			{
				if (Event.Time >= Start - 1e-9 && Event.Time < End - 1e-9)
				{
					Current.Add(Event);
				}
			}
			FGinkgoCameraOverlayStep OverlayStep;
			OverlayStep.Events = Current;
			OverlayStep.DeltaTime = Step;
			OverlayStep.FrameStartTime = Start;
			OverlayStep.Clock = End;
			const FGinkgoCameraOverlayOutput Output = Layer.Step(*Config, OverlayStep);
			const int32 Index = CommonSampleIndex(End);
			if (Index != INDEX_NONE)
			{
				Out.Samples.Add(Index, Output);
			}
			Out.MaxSprings = FMath::Max(Out.MaxSprings, Layer.GetStates()[0].Carried.Num());
		}
		Out.Impulses = Layer.GetStates()[0].ImpulseCount;
		Out.LeftSprings = Layer.GetStates()[0].Carried.Num();
		return Out;
	};
	TMap<int32, FRun> Runs;
	for (const int32 Fps : FrameRates)
	{
		Runs.Add(Fps, Run(Timeline, Fps));
	}
	double MaxDifference = 0.0;
	for (const int32 Fps : {30, 60})
	{
		for (const TPair<int32, FGinkgoCameraOverlayOutput>& Sample : Runs[Fps].Samples)
		{
			const FGinkgoCameraOverlayOutput& Reference = Runs[144].Samples[Sample.Key];
			MaxDifference = FMath::Max(MaxDifference, static_cast<double>(FMath::Abs(Sample.Value.Rotation.Pitch - Reference.Rotation.Pitch)));
			MaxDifference = FMath::Max(MaxDifference, static_cast<double>(FMath::Abs(Sample.Value.Rotation.Roll - Reference.Rotation.Roll)));
			MaxDifference = FMath::Max(MaxDifference, static_cast<double>(FMath::Abs(Sample.Value.FieldOfView - Reference.FieldOfView)));
		}
	}
	AddInfo(FString::Printf(TEXT("事件带来的冲量：共有时刻与 144 fps 相差 %.2e°，同时最多 %d 个弹簧"), MaxDifference, Runs[144].MaxSprings));
	TestTrue(FString::Printf(TEXT("与帧率无关：共有时刻与 144 fps 相差 %.2e°"), MaxDifference), MaxDifference < 1e-3);
	TestTrue(TEXT("每个开枪的事件算一次，别的事件带着冲量也不收"), Runs[30].Impulses == 2 && Runs[60].Impulses == 2 && Runs[144].Impulses == 2);
	TestEqual(TEXT("三个冲量三个弹簧"), Runs[144].MaxSprings, 3);
	TestEqual(TEXT("晃停了就删"), Runs[144].LeftSprings, 0);

	// 线性：一起晃等于各晃各的加起来，互不影响（ω、ζ 不一样也是）
	const FRun First = Run(MakeArrayView(&Timeline[0], 1), 144);
	const FRun Second = Run(MakeArrayView(&Timeline[2], 1), 144);
	double MaxSum = 0.0;
	for (const TPair<int32, FGinkgoCameraOverlayOutput>& Sample : Runs[144].Samples)
	{
		const double Sum = First.Samples[Sample.Key].Rotation.Pitch + Second.Samples[Sample.Key].Rotation.Pitch;
		MaxSum = FMath::Max(MaxSum, FMath::Abs(Sample.Value.Rotation.Pitch - Sum));
	}
	TestTrue(FString::Printf(TEXT("两个事件一起晃等于各晃各的（差 %.2e°）"), MaxSum), MaxSum < 1e-3);

	// 整体倍数：只影响之后的冲量
	Shake->Scale = 2.f;
	const FRun Doubled = Run(Timeline, 144);
	Shake->Scale = 1.f;
	double MaxScaled = 0.0;
	for (const TPair<int32, FGinkgoCameraOverlayOutput>& Sample : Runs[144].Samples)
	{
		MaxScaled = FMath::Max(MaxScaled, FMath::Abs(Doubled.Samples[Sample.Key].Rotation.Pitch - Sample.Value.Rotation.Pitch * 2.0));
	}
	TestTrue(FString::Printf(TEXT("整体倍数 2：俯仰也是两倍（差 %.2e°）"), MaxScaled), MaxScaled < 1e-3);

	// 关掉：输出 0，状态清空
	FGinkgoCameraOverlayLayer Layer;
	FGinkgoCameraOverlayStep OverlayStep;
	OverlayStep.Events = Timeline;
	OverlayStep.DeltaTime = 1.0;
	OverlayStep.FrameStartTime = 0.0;
	Layer.Step(*Config, OverlayStep);
	TestTrue(TEXT("还在晃"), Layer.GetStates()[0].Carried.Num() > 0);
	OverlayStep.Events = TConstArrayView<FGinkgoCameraEvent>();
	const FGinkgoCameraOverlayOutput Disabled = Layer.Step(*Config, OverlayStep, /*bEnabled*/ false);
	TestTrue(TEXT("叠加层关了：输出 0，弹簧清空"), Disabled.Rotation.IsZero() && Layer.GetStates()[0].Carried.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOverlayCarriedReturnTest, "CaseGinkgo.Camera.Overlay.CarriedReturn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraOverlayCarriedReturnTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 踢出去与回弹两组弹簧，到峰值换：写的峰值不变，到峰值的时间只看踢出去的弹簧，之后走回弹弹簧的解，与帧率无关；两组一样时就是一个弹簧走到底
	UGinkgoCameraConfig* Config = MakeEmptyConfig();
	UGinkgoCameraCarriedShake* Shake = NewObject<UGinkgoCameraCarriedShake>(Config);
	Shake->Name = TEXT("Carried");
	Shake->Event = FWeaponTags::Weapon_Event_Fire;
	Config->OverlayEffects = {Shake};

	// 到峰值的时间与冲量对得上：走那么久正好到写的峰值，速度是 0
	for (const FVector2D& Spring : {FVector2D(14.0, 0.75), FVector2D(50.0, 0.3), FVector2D(30.0, 0.8), FVector2D(20.0, 1.0)})
	{
		double Value = 0.0;
		double Velocity = Ginkgo::Spring::DampedImpulseForPeak(1.0, Spring.X, Spring.Y);
		Ginkgo::Spring::StepDamped(Value, Velocity, Spring.X, Spring.Y, Ginkgo::Spring::DampedPeakTime(Spring.X, Spring.Y));
		TestEqual(FString::Printf(TEXT("ω %.0f ζ %.2f：到峰值的时刻值就是峰值"), Spring.X, Spring.Y), Value, 1.0, 1e-6);
		TestEqual(FString::Printf(TEXT("ω %.0f ζ %.2f：到峰值的时刻速度是 0"), Spring.X, Spring.Y), Velocity, 0.0, 1e-4);
	}
	TestEqual(TEXT("ω 不大于 0：到峰值的时间是 0"), Ginkgo::Spring::DampedPeakTime(0.0, 0.5), 0.0);

	constexpr double Peak = 3.0;
	constexpr double FireTime = 0.53;
	constexpr float Omega = 30.f;
	constexpr float Zeta = 0.8f;
	const double Impulse = Ginkgo::Spring::DampedImpulseForPeak(Peak, Omega, Zeta);
	const double PeakTime = Ginkgo::Spring::DampedPeakTime(Omega, Zeta);
	const auto MakeFire = [Impulse](const float ReturnOmega, const float ReturnZeta)
	{
		FGinkgoCameraImpulse Kick;
		Kick.Rotation = FRotator(Impulse, 0.0, 0.0);
		Kick.Omega = Omega;
		Kick.Zeta = Zeta;
		Kick.ReturnOmega = ReturnOmega;
		Kick.ReturnZeta = ReturnZeta;
		FGinkgoCameraEvent Event;
		Event.Tag = FWeaponTags::Weapon_Event_Fire;
		Event.Time = FireTime;
		Event.Impulses = {Kick};
		return Event;
	};
	struct FFrame
	{
		double Time = 0.0;
		double Pitch = 0.0;
	};
	const auto Run = [Config](const FGinkgoCameraEvent& Fire, const int32 Fps, int32& OutLeft)
	{
		TArray<FFrame> Frames;
		FGinkgoCameraOverlayLayer Layer;
		const double Step = 1.0 / Fps;
		bool bFired = false;
		for (int32 Frame = 1; Frame <= Fps * 3; ++Frame)
		{
			const double End = Frame * Step;
			TArray<FGinkgoCameraEvent> Current;
			if (!bFired && Fire.Time < End - 1e-9)
			{
				Current.Add(Fire);
				bFired = true;
			}
			FGinkgoCameraOverlayStep OverlayStep;
			OverlayStep.Events = Current;
			OverlayStep.DeltaTime = Step;
			OverlayStep.FrameStartTime = End - Step;
			OverlayStep.Clock = End;
			Frames.Add({End, Layer.Step(*Config, OverlayStep).Rotation.Pitch});
		}
		OutLeft = Layer.GetStates()[0].Carried.Num();
		return Frames;
	};

	// 回弹 ω 8、ζ 1：细的步长看曲线。到峰值之前是踢出去的弹簧，之后是 P(1 + ωτ)e^(−ωτ)
	constexpr double ReturnOmega = 8.0;
	const FGinkgoCameraEvent Fire = MakeFire(static_cast<float>(ReturnOmega), 1.f);
	int32 Left = 0;
	const TArray<FFrame> Fine = Run(Fire, 2000, Left);
	TestEqual(TEXT("晃停了就删"), Left, 0);
	FFrame Top;
	double MaxKickError = 0.0;
	double MaxReturnError = 0.0;
	for (const FFrame& Frame : Fine)
	{
		Top = Frame.Pitch > Top.Pitch ? Frame : Top;
		const double Age = Frame.Time - FireTime;
		if (Age <= 0.0)
		{
			continue;
		}
		if (Age <= PeakTime)
		{
			double Value = 0.0;
			double Velocity = Impulse;
			Ginkgo::Spring::StepDamped(Value, Velocity, Omega, Zeta, Age);
			MaxKickError = FMath::Max(MaxKickError, FMath::Abs(Frame.Pitch - Value));
		}
		else if (Frame.Pitch != 0.0) // 晃停了就删，之后是 0
		{
			const double Tau = (Age - PeakTime) * ReturnOmega;
			MaxReturnError = FMath::Max(MaxReturnError, FMath::Abs(Frame.Pitch - Peak * (1.0 + Tau) * FMath::Exp(-Tau)));
		}
	}
	AddInfo(FString::Printf(TEXT("峰值 %.5f° 在事件后 %.2f ms（算的 %.2f ms）；踢出去差 %.2e°，回弹差 %.2e°"), Top.Pitch, (Top.Time - FireTime) * 1000.0, PeakTime * 1000.0, MaxKickError, MaxReturnError));
	TestEqual(TEXT("峰值还是写的那么多"), Top.Pitch, Peak, 1e-3);
	TestEqual(TEXT("到峰值的时间只看踢出去的弹簧"), Top.Time - FireTime, PeakTime, 1e-3);
	TestTrue(FString::Printf(TEXT("到峰值之前是踢出去的弹簧（差 %.2e°）"), MaxKickError), MaxKickError < 1e-9);
	TestTrue(FString::Printf(TEXT("到峰值之后是回弹的弹簧（差 %.2e°）"), MaxReturnError), MaxReturnError < 1e-9);

	// 与帧率无关：换弹簧的时刻落在帧的中间也一样
	double MaxRateError = 0.0;
	TMap<int32, double> Reference;
	for (const int32 Fps : {144, 60, 30})
	{
		for (const FFrame& Frame : Run(Fire, Fps, Left))
		{
			const int32 Index = CommonSampleIndex(Frame.Time);
			if (Index == INDEX_NONE)
			{
				continue;
			}
			if (Fps == 144)
			{
				Reference.Add(Index, Frame.Pitch);
			}
			else
			{
				MaxRateError = FMath::Max(MaxRateError, FMath::Abs(Frame.Pitch - Reference[Index]));
			}
		}
	}
	TestTrue(FString::Printf(TEXT("与帧率无关：共有时刻与 144 fps 相差 %.2e°"), MaxRateError), MaxRateError < 1e-9);

	// 两组一样：就是一个弹簧走到底
	double MaxSameError = 0.0;
	for (const FFrame& Frame : Run(MakeFire(Omega, Zeta), 144, Left))
	{
		if (Frame.Time > FireTime && Frame.Pitch != 0.0)
		{
			double Value = 0.0;
			double Velocity = Impulse;
			Ginkgo::Spring::StepDamped(Value, Velocity, Omega, Zeta, Frame.Time - FireTime);
			MaxSameError = FMath::Max(MaxSameError, FMath::Abs(Frame.Pitch - Value));
		}
	}
	TestTrue(FString::Printf(TEXT("两组一样时与一个弹簧相同（差 %.2e°）"), MaxSameError), MaxSameError < 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOverlayNoiseTest, "CaseGinkgo.Camera.Overlay.Noise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraOverlayNoiseTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 噪声是相机时钟的函数：同一个时刻同一个值；最多偏幅度那么多。包络：连续量线性映射、夹到上限，激活条件不成立时是 0。
	// 用一个随速度晃的（同 2026-11-01 上午的跑动晃动，类默认值里已经换成冲刺落脚的冲量）
	UGinkgoCameraNoiseSway* Run = NewObject<UGinkgoCameraNoiseSway>();
	FGameplayTagQueryExpression Grounded = FGameplayTagQueryExpression().AllTagsMatch().AddTag(FMovementTags::Movement_Grounded);
	FGameplayTagQueryExpression NotSliding = FGameplayTagQueryExpression().NoTagsMatch().AddTag(FMovementTags::Movement_Slide);
	FGameplayTagQueryExpression Running = FGameplayTagQueryExpression().AllExprMatch();
	Running.AddExpr(Grounded).AddExpr(NotSliding);
	Run->Activation.Query = FGameplayTagQuery::BuildQuery(Running);
	Run->EnvelopeScalar = FMovementTags::Movement_Value_HorizontalSpeed;
	Run->ScalarForZero = 150.f;
	Run->ScalarForOne = 476.25f;
	Run->MaxEnvelope = 1.5f;
	Run->Pitch.Amplitude = 0.35f;
	Run->Pitch.Frequency = 0.9f;
	Run->Yaw.Amplitude = 0.3f;
	Run->Yaw.Frequency = 0.6f;
	Run->Roll.Amplitude = 0.45f;
	Run->Roll.Frequency = 0.5f;
	Run->Seed = 1;
	double Largest = 0.0;
	for (int32 Sample = 0; Sample < 20000; ++Sample)
	{
		const double Time = Sample * 0.001;
		TestEqual(TEXT("同一个时刻同一个值"), Run->EvaluateNoise(0, Time), Run->EvaluateNoise(0, Time));
		Largest = FMath::Max(Largest, FMath::Abs(Run->EvaluateNoise(2, Time)));
	}
	TestTrue(FString::Printf(TEXT("侧倾最多 %.3f°，不超过幅度 %.2f°"), Largest, Run->Roll.Amplitude), Largest <= Run->Roll.Amplitude + 1e-6 && Largest > 0.5 * Run->Roll.Amplitude);
	TestTrue(TEXT("几个轴的相位不同"), FMath::Abs(Run->EvaluateNoise(0, 1.234) / Run->Pitch.Amplitude - Run->EvaluateNoise(1, 1.234) / Run->Yaw.Amplitude) > 1e-3);

	UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Grounded}));
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 476.25f);
	TestEqual(TEXT("跑速时包络是 1"), Run->EvaluateEnvelopeTarget(Context), 1.0, 1e-6);
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 762.f);
	TestEqual(TEXT("冲刺时封顶 1.5"), Run->EvaluateEnvelopeTarget(Context), 1.5, 1e-6);
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 100.f);
	TestEqual(TEXT("慢于 150 cm/s 时是 0"), Run->EvaluateEnvelopeTarget(Context), 0.0);
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 762.f);
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Grounded, FMovementTags::Movement_Slide}));
	TestEqual(TEXT("滑铲时是 0"), Run->EvaluateEnvelopeTarget(Context), 0.0);
	Context->SetTags(EGinkgoCameraContextSource::Movement, FGameplayTagContainer());
	TestEqual(TEXT("在空中是 0"), Run->EvaluateEnvelopeTarget(Context), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOverlayFootstepTest, "CaseGinkgo.Camera.Overlay.Footstep",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraOverlayFootstepTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraLayerTests;
	// 冲刺落脚的震动（类默认值）：激活条件成立时每个落脚一次冲量，往下沉；侧倾按左右脚反过来（左脚 +1 往左、右脚 −1 往右）。
	// 跑速（476 cm/s）、滑铲、在空中都不收
	const UGinkgoCameraConfig* Defaults = GetDefault<UGinkgoCameraConfig>();
	const UGinkgoCameraImpulseShake* Footstep = nullptr;
	for (const UGinkgoCameraOverlayEffect* Effect : Defaults->OverlayEffects)
	{
		Footstep = Effect && Effect->Name == TEXT("FootstepShake") ? Cast<UGinkgoCameraImpulseShake>(Effect) : Footstep;
	}
	if (!TestNotNull(TEXT("类默认值带冲刺落脚的震动"), Footstep))
	{
		return false;
	}
	UGinkgoCameraConfig* Config = MakeEmptyConfig();
	Config->OverlayEffects = {DuplicateObject(Footstep, Config)};
	UGinkgoCameraContextComponent* Context = NewObject<UGinkgoCameraContextComponent>();
	const auto Run = [&Config, Context](const float Magnitude)
	{
		// 一帧 1/144 s，事件在帧的中间；之后约 0.08 s 里取侧倾、俯仰最偏的（覆盖当前 ω=25 的首峰约 50 ms）
		FGinkgoCameraOverlayLayer Layer;
		FRotator Extreme = FRotator::ZeroRotator;
		const double Step = 1.0 / 144.0;
		// 步进的输入只看着事件（数组视图），事件放在这里
		const TArray<FGinkgoCameraEvent> Events = {{FMovementTags::Movement_Event_Footstep, 0.5 * Step, Magnitude}};
		for (int32 Frame = 1; Frame <= 12; ++Frame)
		{
			FGinkgoCameraOverlayStep OverlayStep;
			OverlayStep.Context = Context;
			OverlayStep.DeltaTime = Step;
			OverlayStep.FrameStartTime = (Frame - 1) * Step;
			OverlayStep.Clock = Frame * Step;
			if (Frame == 1)
			{
				OverlayStep.Events = Events;
			}
			const FRotator Rotation = Layer.Step(*Config, OverlayStep).Rotation;
			Extreme.Pitch = FMath::Abs(Rotation.Pitch) > FMath::Abs(Extreme.Pitch) ? Rotation.Pitch : Extreme.Pitch;
			Extreme.Roll = FMath::Abs(Rotation.Roll) > FMath::Abs(Extreme.Roll) ? Rotation.Roll : Extreme.Roll;
		}
		return TPair<FRotator, int32>(Extreme, Layer.GetStates()[0].ImpulseCount);
	};

	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Grounded, FMovementTags::Movement_Sprint}));
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 762.f);
	const TPair<FRotator, int32> Left = Run(1.f);
	const TPair<FRotator, int32> Right = Run(-1.f);
	AddInfo(FString::Printf(TEXT("冲刺落脚：左脚俯仰 %.3f°、侧倾 %.3f°；右脚俯仰 %.3f°、侧倾 %.3f°"), Left.Key.Pitch, Left.Key.Roll, Right.Key.Pitch, Right.Key.Roll));
	TestTrue(TEXT("冲刺时每个落脚一次冲量"), Left.Value == 1 && Right.Value == 1);
	TestTrue(TEXT("往下沉，两只脚一样"), Left.Key.Pitch < -0.05 && FMath::IsNearlyEqual(Left.Key.Pitch, Right.Key.Pitch, 1e-6));
	TestTrue(TEXT("侧倾按左右脚反过来"), Left.Key.Roll < -0.05 && FMath::IsNearlyEqual(Left.Key.Roll, -Right.Key.Roll, 1e-6));

	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 476.25f);
	TestEqual(TEXT("跑速时不收"), Run(1.f).Value, 0);
	Context->SetScalar(FMovementTags::Movement_Value_HorizontalSpeed, 762.f);
	Context->SetTags(EGinkgoCameraContextSource::Movement, Tags({FMovementTags::Movement_Grounded, FMovementTags::Movement_Slide}));
	TestEqual(TEXT("滑铲时不收"), Run(1.f).Value, 0);
	Context->SetTags(EGinkgoCameraContextSource::Movement, FGameplayTagContainer());
	TestEqual(TEXT("在空中不收"), Run(1.f).Value, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOverlayComposeTest, "CaseGinkgo.Camera.Overlay.Compose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraOverlayComposeTest::RunTest(const FString& Parameters)
{
	// 叠加的旋转在视角空间里转：没有叠加时视角原样不动；抬头 40° 时叠加的偏航绕相机自己的上方转，不是绕世界的上方
	const FRotator View(40.0, 25.0, 0.0);
	TestTrue(TEXT("没有叠加时原样"), FGinkgoCameraOverlayLayer::Compose(View, FRotator::ZeroRotator) == View);
	const FRotator Composed = FGinkgoCameraOverlayLayer::Compose(View, FRotator(0.0, 2.0, 0.0));
	const FVector Expected = FQuat(View.Quaternion().GetUpVector(), FMath::DegreesToRadians(2.0)).RotateVector(View.Vector());
	TestTrue(TEXT("偏航绕相机自己的上方"), Composed.Vector().Equals(Expected, 1e-5));
	TestTrue(TEXT("叠加的俯仰在视角空间里加上去"), FMath::IsNearlyEqual(FGinkgoCameraOverlayLayer::Compose(FRotator(10.0, 0.0, 0.0), FRotator(-1.5, 0.0, 0.0)).Pitch, 8.5, 1e-4));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

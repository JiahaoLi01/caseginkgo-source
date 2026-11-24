#include "GinkgoCameraConfig.h"

#include "Utilities/Enumeration/Tags/CameraTags.h"
#include "Utilities/Enumeration/Tags/MovementTags.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"

namespace
{
	FGinkgoCameraNoiseAxis MakeNoiseAxis(const float Amplitude, const float Frequency)
	{
		FGinkgoCameraNoiseAxis Axis;
		Axis.Amplitude = Amplitude;
		Axis.Frequency = Frequency;
		return Axis;
	}
}

UGinkgoCameraConfig::UGinkgoCameraConfig()
{
	// ===== 参数层 =====
	// 修改器的数值来源也是类默认值的子对象，配置资产各有一份（同移动配置的窗口）

	// 滑铲的效果都只给拿到起步加速的滑铲（2026-11-01 你说疲劳的滑铲不要）：在滑铲、这次的起步加速不小于 1 cm/s（疲劳时是 0）
	const FGameplayTagQuery Sliding = FGameplayTagQuery::MakeQuery_MatchAllTags(FGameplayTagContainer(FMovementTags::Movement_Slide));
	FGinkgoCameraScalarCondition Boosted;
	Boosted.Scalar = FMovementTags::Movement_Value_SlideBoost;
	Boosted.Threshold = 1.f;
	FGinkgoCameraScalarCondition Fast;
	Fast.Scalar = FMovementTags::Movement_Value_HorizontalSpeed;
	Fast.ThresholdScalar = FMovementTags::Movement_Value_RunSpeed;

	// 滑铲 FOV（你要的）：拿到起步加速的那一刻起 FOV +10，水平速度低于跑速以后失效（淡出，不跳）。
	// 激活：拿到加速的滑铲、水平速度不小于跑速（上下文里移动配置的跑速）；条件不成立一次就锁住，同一次滑铲里再快起来（下坡）也不再加。
	// 速度类增量：以后开镜覆盖时屏蔽它
	FGinkgoCameraParamModifier SlideFov;
	SlideFov.Name = TEXT("SlideFov");
	SlideFov.Activation.Query = Sliding;
	SlideFov.Activation.Conditions = {Boosted, Fast};
	SlideFov.bLatchOff = true;
	SlideFov.Param = FGinkgoCameraParamField::FromMember(GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, FieldOfView));
	SlideFov.Category = FCameraTags::Camera_Category_Speed;
	UGinkgoCameraValueConstant* SlideFovValue = CreateDefaultSubobject<UGinkgoCameraValueConstant>(TEXT("SlideFovValue"));
	SlideFovValue->Value = 10.f;
	SlideFov.Value = SlideFovValue;
	// 加速是一下子的：权重当帧到 1，FOV 由参数弹簧拉上去（ω 15：0.11 s 到一半、0.26 s 到 90%）；失效时 0.3 s 淡出
	SlideFov.BlendInTime = 0.f;
	SlideFov.BlendOutTime = 0.3f;
	Modifiers.Add(SlideFov);

	// 滑铲臂长（提高 FOV 的同时降低 spring arm length）：与 FOV 同起同落（同样的激活、锁住、淡出与弹簧），臂长 −50.2 cm。
	FGinkgoCameraParamModifier SlideArm = SlideFov;
	SlideArm.Name = TEXT("SlideArm");
	SlideArm.Param = FGinkgoCameraParamField::FromMember(GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, ArmLength));
	UGinkgoCameraValueConstant* SlideArmValue = CreateDefaultSubobject<UGinkgoCameraValueConstant>(TEXT("SlideArmValue"));
	SlideArmValue->Value = -50.2f;
	SlideArm.Value = SlideArmValue;
	Modifiers.Add(SlideArm);

	// 滑铲的侧倾（你要的：滑动期间持续的 roll）：拿到加速的整个滑铲里相机向左倾 4°（画面里地平线左高右低），只转画面、不改位置与瞄准。
	// 淡入 0.2 s、淡出 0.3 s，再过侧倾的参数弹簧。不算速度类（2026-11-03 你手测说开镜把它吞掉了）：开镜要退掉的是和拉近冲突的视场、臂长，侧倾不影响拉近，开镜时还在
	FGinkgoCameraParamModifier SlideRoll;
	SlideRoll.Name = TEXT("SlideRoll");
	SlideRoll.Activation.Query = Sliding;
	SlideRoll.Activation.Conditions = {Boosted};
	SlideRoll.Param = FGinkgoCameraParamField::FromMember(GET_MEMBER_NAME_CHECKED(FGinkgoCameraParams, Roll));
	UGinkgoCameraValueConstant* SlideRollValue = CreateDefaultSubobject<UGinkgoCameraValueConstant>(TEXT("SlideRollValue"));
	SlideRollValue->Value = -4.f;
	SlideRoll.Value = SlideRollValue;
	SlideRoll.BlendInTime = 0.2f;
	SlideRoll.BlendOutTime = 0.3f;
	Modifiers.Add(SlideRoll);

	// 开镜的视场（Docs/Design/Weapons.md「相机与控制」，2026-11-02 你说接上）：覆盖成基础视场按枪的倍率放大（98k 的 1.73 倍：90° → 60°），
	// 权重就是开镜进度，盖在弹簧之后，相机拉近与上半身抬枪同时到位。开镜时屏蔽速度类的增量（滑铲的视场、臂长）
	FGinkgoCameraParamModifier AdsFov;
	AdsFov.Name = TEXT("AdsFov");
	AdsFov.Param = SlideFov.Param;
	AdsFov.Blend = EGinkgoCameraBlend::Override;
	AdsFov.BlockedCategories.AddTag(FCameraTags::Camera_Category_Speed);
	AdsFov.Activation.Query = FGameplayTagQuery::MakeQuery_MatchAllTags(FGameplayTagContainer(FWeaponTags::Weapon_ADS));
	// 混入混出是开发者的量，在调参面板上（你说的）。盖在弹簧之后：时间到了就到位，默认的 0.3 s 同 98k 的开镜时间；先快后慢：按下马上有反应，到位时收住
	AdsFov.bAfterSprings = true;
	AdsFov.BlendInTime = 0.3f;
	AdsFov.BlendOutTime = 0.3f;
	AdsFov.BlendInCurve = EGinkgoCameraBlendCurve::EaseOut;
	AdsFov.BlendOutCurve = EGinkgoCameraBlendCurve::EaseOut;
	AdsFov.BlendCurveStrength = 2.5f;
	UGinkgoCameraValueZoomedFov* AdsFovValue = CreateDefaultSubobject<UGinkgoCameraValueZoomedFov>(TEXT("AdsFovValue"));
	AdsFovValue->ZoomScalar = FWeaponTags::Weapon_Value_AdsZoom;
	AdsFov.Value = AdsFovValue;
	Modifiers.Add(AdsFov);

	// 参数弹簧：臂长与 FOV 同一个 ω，推拉变焦的两半一起走，角色在画面上的大小一路不变
	FGinkgoCameraParamSpring FieldOfViewSpring;
	FieldOfViewSpring.Param = SlideFov.Param;
	FieldOfViewSpring.Omega = 15.f;
	FGinkgoCameraParamSpring ArmSpring;
	ArmSpring.Param = SlideArm.Param;
	ArmSpring.Omega = 15.f;
	FGinkgoCameraParamSpring RollSpring;
	RollSpring.Param = SlideRoll.Param;
	RollSpring.Omega = 10.f;
	ParamSprings = {FieldOfViewSpring, ArmSpring, RollSpring};

	// ===== 叠加层 =====

	// 滑铲开始的震动（你要的：瞬时、固定、相对明显；2026-11-01 频率略降）：拿到加速的滑铲开始时给同样的冲量，不随机（事件的强度是起步加速，
	// 疲劳时是 0，MinMagnitude 1 不收）。两个弹簧错开频率：往下一沉（俯仰，约 4.8 Hz，ζ 0.3，峰值约 2°，0.32 s 收住）带一点偏航，
	// 再加一下侧倾的抖（约 7.2 Hz，ζ 0.25，峰值约 1°），合起来不像单轴的摆。降频时冲量跟着 ω 缩，峰值不变
	UGinkgoCameraImpulseShake* SlideShake = CreateDefaultSubobject<UGinkgoCameraImpulseShake>(TEXT("SlideShake"));
	SlideShake->Name = TEXT("SlideShake");
	SlideShake->Event = FMovementTags::Movement_Event_SlideStarted;
	SlideShake->MinMagnitude = 1.f;
	SlideShake->Impulse = FRotator(-75.0, 14.4, 8.5);
	SlideShake->Omega = 31.6f;
	SlideShake->Zeta = 0.3f;
	OverlayEffects.Add(SlideShake);

	UGinkgoCameraImpulseShake* SlideShakeRoll = CreateDefaultSubobject<UGinkgoCameraImpulseShake>(TEXT("SlideShakeRoll"));
	SlideShakeRoll->Name = TEXT("SlideShakeRoll");
	SlideShakeRoll->Event = FMovementTags::Movement_Event_SlideStarted;
	SlideShakeRoll->MinMagnitude = 1.f;
	SlideShakeRoll->Impulse = FRotator(0.0, 0.0, 65.6);
	SlideShakeRoll->Omega = 46.7f;
	SlideShakeRoll->Zeta = 0.25f;
	OverlayEffects.Add(SlideShakeRoll);
	// 第一人称开镜时滑铲不震（2026-11-12 你说的：滑铲过程中开镜不需要镜子抖动）：越肩照旧
	SlideShake->FirstPersonScale = 0.f;
	SlideShakeRoll->FirstPersonScale = 0.f;

	// 开枪（2026-11-03 你说的：参数放在武器配置里，每把枪不一样，可以有随机范围，见 FGinkgoCameraRecoil）：这里只收开火事件带来的冲量，只晃画面
	UGinkgoCameraCarriedShake* WeaponRecoil = CreateDefaultSubobject<UGinkgoCameraCarriedShake>(TEXT("WeaponRecoil"));
	WeaponRecoil->Name = TEXT("WeaponRecoil");
	WeaponRecoil->Event = FWeaponTags::Weapon_Event_Fire;
	// 第一人称开镜照旧（2026-11-12 你手测以后说的：枪踢不代替画面的踢动，view kick 与 camera shake 保留），枪也跟着这一层转：
	// 瞄具里的画面同画面一起跳，跳的角度同腰射一样（你说的）。枪自己另外再踢一点（FGinkgoScopeHandling::KickScale）
	WeaponRecoil->FirstPersonScale = 1.f;
	WeaponRecoil->bSightFollows = true;
	OverlayEffects.Add(WeaponRecoil);

	// 冲刺落脚的震动：普通移动不晃，冲刺时每一步落地的瞬间给一组柔和低幅的三轴微震（俯仰 -4、偏航 -2、侧倾 -4，ω 25、ζ 0.45，左脚/右脚镜像）。
	// 激活：在地面上、不在滑铲、水平速度不小于 600 cm/s（跑 476、冲刺 762）：冲刺时有，松开冲刺减速到 600 以下就停。
	// 两次至少隔 0.1 s：滑铲起身的片段里两只脚 20 ms 内先后着地，两下叠成双倍的一沉，只算一下（冲刺一步约 0.21 s）
	UGinkgoCameraImpulseShake* FootstepShake = CreateDefaultSubobject<UGinkgoCameraImpulseShake>(TEXT("FootstepShake"));
	FootstepShake->Name = TEXT("FootstepShake");
	FootstepShake->Event = FMovementTags::Movement_Event_Footstep;
	FGameplayTagQueryExpression Grounded = FGameplayTagQueryExpression().AllTagsMatch().AddTag(FMovementTags::Movement_Grounded);
	FGameplayTagQueryExpression NotSliding = FGameplayTagQueryExpression().NoTagsMatch().AddTag(FMovementTags::Movement_Slide);
	FGameplayTagQueryExpression Running = FGameplayTagQueryExpression().AllExprMatch();
	Running.AddExpr(Grounded).AddExpr(NotSliding);
	FootstepShake->Activation.Query = FGameplayTagQuery::BuildQuery(Running);
	FGinkgoCameraScalarCondition Sprinting;
	Sprinting.Scalar = FMovementTags::Movement_Value_HorizontalSpeed;
	Sprinting.Threshold = 600.f;
	FootstepShake->Activation.Conditions = {Sprinting};
	FootstepShake->Impulse = FRotator(-4.0, -2.0, -4.0);
	FootstepShake->bMirrorOnNegative = true;
	FootstepShake->MinInterval = 0.1f;
	FootstepShake->Omega = 25.f;
	FootstepShake->Zeta = 0.45f;
	OverlayEffects.Add(FootstepShake);

	// 开镜的晃动（你要的：低频）：包络就是开镜进度（两头缓过的，不再平滑），比呼吸还慢的漂（0.15–0.25 Hz），不侧倾。
	// 只晃画面：枪口照旧对着视角的瞄准点，准星画在枪真正对着的地方，所以准星在画面上反着晃这么多、不影响命中
	UGinkgoCameraNoiseSway* AdsSway = CreateDefaultSubobject<UGinkgoCameraNoiseSway>(TEXT("AdsSway"));
	AdsSway->Name = TEXT("AdsSway");
	AdsSway->EnvelopeScalar = FWeaponTags::Weapon_Value_AdsAlpha;
	AdsSway->EnvelopeOmega = 0.f;
	AdsSway->Pitch = MakeNoiseAxis(0.3f, 0.22f);
	AdsSway->Yaw = MakeNoiseAxis(0.35f, 0.15f);
	AdsSway->Seed = 2;
	// 第一人称开镜时晃的是枪：瞄具连同分划一起晃，画面不动，子弹跟着分划走；画面上这一份收掉
	AdsSway->bMovesSight = true;
	AdsSway->FirstPersonScale = 0.f;
	OverlayEffects.Add(AdsSway);

	// 操作枪的小震动（2026-11-12 你说的：拉栓时也给一些相机震动，不用很大）：参数在枪上（UGinkgoWeaponDefinition::CueCameraShakes），
	// 拉栓的扳起、拉到底、推到底、压下那几帧带着冲量过来，只晃画面。越肩、第一人称都有（第一人称拉栓时眼睛让开了，枪不跟）
	UGinkgoCameraCarriedShake* WeaponHandling = CreateDefaultSubobject<UGinkgoCameraCarriedShake>(TEXT("WeaponHandling"));
	WeaponHandling->Name = TEXT("WeaponHandling");
	WeaponHandling->Event = FWeaponTags::Weapon_Event_Handling;
	OverlayEffects.Add(WeaponHandling);
}

TArray<FName> UGinkgoCameraConfig::GetParamFieldOptions()
{
	using namespace UE::Query;
	return Ginkgo::Camera::GetParamFieldInfoList(FGinkgoCameraParams::StaticStruct())
		| Select([](const FGinkgoCameraParamFieldInfo& Info) { return Info.Field.Name; })
		| ToArray();
}

#include "GinkgoWeaponFxCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Engine/StaticMesh.h"
#include "NiagaraEditorUtilities.h"
#include "PhysicsEngine/BodySetup.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "Stateless/NiagaraStatelessDistribution.h"
#include "UObject/Linker.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoWeaponFx, Log, All);

namespace GinkgoWeaponFx
{
	const TCHAR* const TemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Systems/DirectionalBurstLightweight.DirectionalBurstLightweight");
	const TCHAR* const SystemPackage = TEXT("/Game/Weapons/FX/NS_MuzzleBlast");
	const TCHAR* const WispPackage = TEXT("/Game/Weapons/FX/NS_MuzzleWisp");
	const TCHAR* const FlashPackage = TEXT("/Game/Weapons/FX/NS_MuzzleFlash");
	/** 普通发射器的模板：有出生率、初始化、重力、阻力、解算，小硝烟在它上面改 */
	const TCHAR* const WispTemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain");
	const TCHAR* const CurlNoisePath = TEXT("/Niagara/Modules/Update/Forces/CurlNoiseForce.CurlNoiseForce");
	/** 武器组件每帧写它：透明度是这一刻冒出来的烟有多浓（UGinkgoWeaponComponent::UpdateMuzzleWisps） */
	const TCHAR* const WispColorParameter = TEXT("User.WispColor");
	/** 枪口的热浪（Docs/Design/Weapons.md「枪管的热」）：武器组件按枪管多热每帧写两个用户参数，透明度是折射有多强、出生率是冒得多密 */
	const TCHAR* const HeatPackage = TEXT("/Game/Weapons/FX/NS_MuzzleHeat");
	const TCHAR* const HeatColorParameter = TEXT("User.HeatColor");
	const TCHAR* const HeatRateParameter = TEXT("User.HeatRate");
	/** 弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：武器组件每颗弹丸放一个，用户参数给终点、点数、寿命、宽度、长到头的快慢、颜色（UGinkgoWeaponComponent::SpawnBulletTrails） */
	const TCHAR* const TrailPackage = TEXT("/Game/Weapons/FX/NS_BulletTrail");
	/** 光束的发射器模板：发射器更新里 BeamEmitterSetup 定起止点、一次性爆发，粒子出生时 SpawnBeam 沿线摆开，飘带画 */
	const TCHAR* const TrailTemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Emitters/StaticBeam.StaticBeam");
	const TCHAR* const DragPath = TEXT("/Niagara/Modules/Update/Forces/Drag.Drag");

	/** 打出一个对象的属性：调的时候看字段 */
	void DumpObject(const UObject& Object, const int32 Depth)
	{
		for (TFieldIterator<FProperty> It(Object.GetClass()); It; ++It)
		{
			FString Value;
			It->ExportText_InContainer(0, Value, &Object, nullptr, nullptr, PPF_None);
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%*s%s (%s) = %s"), Depth * 2, TEXT(""), *It->GetName(), *It->GetCPPType(), *Value.Left(400));
		}
	}

	/** 轻量发射器的类在 Internal 里：只当 UObject 用。它的基类链上第一个就是 UObject，转换不用挪指针 */
	UObject* EmitterOf(const FNiagaraEmitterHandle& Handle)
	{
		return reinterpret_cast<UObject*>(Handle.GetEmitterBase());
	}

	/** 一个属性的地址，类型由调用的一方保证（公开头文件里的分布） */
	template <typename T>
	T& Field(UObject& Object, const TCHAR* Name)
	{
		const FProperty* Property = Object.GetClass()->FindPropertyByName(Name);
		checkf(Property, TEXT("%s 没有属性 %s"), *Object.GetClass()->GetName(), Name);
		return *Property->ContainerPtrToValuePtr<T>(&Object);
	}

	/** 按文本设一个属性：枚举、布尔（包括位域）、浮点都走这里 */
	void SetText(UObject& Object, const TCHAR* Name, const TCHAR* Text)
	{
		const FProperty* Property = Object.GetClass()->FindPropertyByName(Name);
		checkf(Property, TEXT("%s 没有属性 %s"), *Object.GetClass()->GetName(), Name);
		const TCHAR* End = Property->ImportText_InContainer(Text, &Object, &Object, PPF_None);
		checkf(End, TEXT("%s.%s 读不了 %s"), *Object.GetClass()->GetName(), Name, Text);
	}

	/** 发射器的模块，按短类名 */
	UObject& Module(UObject& Emitter, const TCHAR* Name)
	{
		const FArrayProperty* Array = FindFProperty<FArrayProperty>(Emitter.GetClass(), TEXT("Modules"));
		const FObjectPropertyBase* Inner = CastFieldChecked<FObjectPropertyBase>(Array->Inner);
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(&Emitter));
		const FString ClassName = FString(TEXT("NiagaraStatelessModule_")) + Name;
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			UObject* Element = Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index));
			if (Element && Element->GetClass()->GetName() == ClassName)
			{
				return *Element;
			}
		}
		checkf(false, TEXT("%s 没有模块 %s"), *Emitter.GetName(), Name);
		return Emitter;
	}

	/** 模块，并设开关 */
	UObject& Toggle(UObject& Emitter, const TCHAR* Name, const bool bEnabled)
	{
		UObject& Found = Module(Emitter, Name);
		SetText(Found, TEXT("bModuleEnabled"), bEnabled ? TEXT("True") : TEXT("False"));
		return Found;
	}

	void SetRange(UObject& Object, const TCHAR* Name, const float Min, const float Max)
	{
		FNiagaraDistributionRangeFloat& Distribution = Field<FNiagaraDistributionRangeFloat>(Object, Name);
		if (Min == Max)
		{
			Distribution.InitConstant(Min);
		}
		else
		{
			Distribution.InitRange(Min, Max);
		}
		Distribution.UpdateValuesFromDistribution();
	}

	void SetRange2(UObject& Object, const TCHAR* Name, const FVector2f& Min, const FVector2f& Max)
	{
		// 引擎没有导出向量的 InitRange：照浮点的那个写，通道的值依次是最小、最大
		FNiagaraDistributionRangeVector2& Distribution = Field<FNiagaraDistributionRangeVector2>(Object, Name);
		Distribution.Mode = ENiagaraDistributionMode::NonUniformRange;
		Distribution.ChannelConstantsAndRanges = {Min.X, Min.Y, Max.X, Max.Y};
		Distribution.UpdateValuesFromDistribution();
	}

	void SetRange3(UObject& Object, const TCHAR* Name, const FVector3f& Min, const FVector3f& Max)
	{
		FNiagaraDistributionRangeVector3& Distribution = Field<FNiagaraDistributionRangeVector3>(Object, Name);
		Distribution.Mode = ENiagaraDistributionMode::NonUniformRange;
		Distribution.ChannelConstantsAndRanges = {Min.X, Min.Y, Min.Z, Max.X, Max.Y, Max.Z};
		Distribution.UpdateValuesFromDistribution();
	}

	/** 一条曲线：时间、值的几个键，线性 */
	FRichCurve MakeCurve(const std::initializer_list<TPair<float, float>> Keys)
	{
		FRichCurve Curve;
		for (const TPair<float, float>& Key : Keys)
		{
			const FKeyHandle Handle = Curve.AddKey(Key.Key, Key.Value);
			Curve.SetKeyInterpMode(Handle, RCIM_Linear);
		}
		return Curve;
	}

	/** 颜色随年龄：RGB 不变，透明度按曲线 */
	void SetAlphaOverLife(UObject& Module, const std::initializer_list<TPair<float, float>> Keys)
	{
		FNiagaraDistributionColor& Distribution = Field<FNiagaraDistributionColor>(Module, TEXT("ScaleDistribution"));
		Distribution.Mode = ENiagaraDistributionMode::NonUniformCurve;
		const FRichCurve One = MakeCurve({{0.f, 1.f}, {1.f, 1.f}});
		Distribution.ChannelCurves = {One, One, One, MakeCurve(Keys)};
		Distribution.UpdateValuesFromDistribution();
	}

	/** 大小随年龄：两个方向一样 */
	void SetSizeOverLife(UObject& Module, const std::initializer_list<TPair<float, float>> Keys)
	{
		FNiagaraDistributionVector2& Distribution = Field<FNiagaraDistributionVector2>(Module, TEXT("ScaleDistribution"));
		Distribution.Mode = ENiagaraDistributionMode::NonUniformCurve;
		const FRichCurve Curve = MakeCurve(Keys);
		Distribution.ChannelCurves = {Curve, Curve};
		Distribution.UpdateValuesFromDistribution();
	}

	/** 这个发射器一次爆发几个（第一条爆发）、一次性、包围盒 */
	void SetBurst(UObject& Emitter, const int32 Min, const int32 Max, const float BoundsRadius)
	{
		const FArrayProperty* Array = FindFProperty<FArrayProperty>(Emitter.GetClass(), TEXT("SpawnInfos"));
		const FStructProperty* Inner = CastFieldChecked<FStructProperty>(Array->Inner);
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(&Emitter));
		check(Helper.Num() > 0);
		FNiagaraDistributionRangeInt& Amount = *Inner->Struct->FindPropertyByName(TEXT("Amount"))->ContainerPtrToValuePtr<FNiagaraDistributionRangeInt>(Helper.GetRawPtr(0));
		Amount.InitConstant(Min);
		if (Min != Max)
		{
			Amount.Mode = ENiagaraDistributionMode::UniformRange;
			Amount.Max = Max;
		}
		Field<FBox>(Emitter, TEXT("FixedBounds")) = FBox(FVector(-BoundsRadius), FVector(BoundsRadius));
	}

	/** 这些模块每个发射器都不要 */
	void DisableCommon(UObject& Emitter)
	{
		Toggle(Emitter, TEXT("GravityForce"), false);
		Toggle(Emitter, TEXT("ScaleSpriteSizeBySpeed"), false);
		Toggle(Emitter, TEXT("ShapeLocation"), false);
	}

	/** 火光画在硝烟后面（越大越晚画）：半透明的硝烟后画的话，火舌的根部一开始就被盖住，看着像离开了枪口 */
	constexpr int32 OverSmokeHint = 1;

	/** 第一个渲染器（模板里是精灵）：材质、翻页、排序、朝向 */
	UNiagaraSpriteRendererProperties& SetupSprite(UObject& Emitter, const TCHAR* MaterialPath, const ENiagaraSpriteAlignment Alignment, const ENiagaraSpriteFacingMode Facing,
		const FVector2D& Pivot, const ENiagaraSortMode Sort, const int32 SubImages = 2)
	{
		const FArrayProperty* Array = FindFProperty<FArrayProperty>(Emitter.GetClass(), TEXT("RendererProperties"));
		const FObjectPropertyBase* Inner = CastFieldChecked<FObjectPropertyBase>(Array->Inner);
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(&Emitter));
		UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(0)));
		check(Sprite);
		Sprite->Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
		checkf(Sprite->Material, TEXT("读不到材质 %s：先跑 muzzle_fx_ue.py"), MaterialPath);
		Sprite->Alignment = Alignment;
		Sprite->FacingMode = Facing;
		Sprite->PivotInUVSpace = Pivot;
		Sprite->SubImageSize = FVector2D(SubImages, SubImages);
		// 翻页的帧之间混：寿命长的粒子放得比模拟慢，不混看得出一帧一帧
		Sprite->bSubImageBlend = SubImages > 2;
		Sprite->SortMode = Sort;
		Sprite->bCastShadows = false;
		return *Sprite;
	}

	/** 每个粒子从 2 × 2 里随机挑一张，不换 */
	void RandomFrame(UObject& Emitter)
	{
		UObject& SubUV = Toggle(Emitter, TEXT("SubUVAnimation"), true);
		SetText(SubUV, TEXT("NumFrames"), TEXT("4"));
		SetText(SubUV, TEXT("AnimationMode"), TEXT("Random"));
		SetText(SubUV, TEXT("RandomChangeInterval"), TEXT("100.0"));
	}

	/** 火光：对着相机的星形，一两帧。开枪的这一下最亮的部分 */
	void BuildFlash(UObject& Emitter)
	{
		DisableCommon(Emitter);
		Toggle(Emitter, TEXT("AddVelocity"), false);
		Toggle(Emitter, TEXT("Drag"), false);
		SetBurst(Emitter, 2, 2, 150.f);
		UObject& Init = Toggle(Emitter, TEXT("InitializeParticle"), true);
		SetRange(Init, TEXT("LifetimeDistribution"), 0.035f, 0.055f);
		SetRange2(Init, TEXT("SpriteSizeDistribution"), FVector2f(24.f), FVector2f(36.f));
		SetRange(Init, TEXT("SpriteRotationDistribution"), 0.f, 360.f);
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).InitConstant(FLinearColor(9.f, 5.2f, 2.f, 1.f));
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).UpdateValuesFromDistribution();
		SetAlphaOverLife(Toggle(Emitter, TEXT("ScaleColor"), true), {{0.f, 1.f}, {0.4f, 0.9f}, {1.f, 0.f}});
		Toggle(Emitter, TEXT("ScaleSpriteSize"), false);
		RandomFrame(Emitter);
		SetupSprite(Emitter, TEXT("/Game/Weapons/FX/M_MuzzleFlash.M_MuzzleFlash"), ENiagaraSpriteAlignment::Unaligned, ENiagaraSpriteFacingMode::FaceCamera, FVector2D(0.5, 0.5), ENiagaraSortMode::None).SortOrderHint = OverSmokeHint;
	}

	/** 火舌：沿枪管喷出去，从侧面看得见长度 */
	void BuildFlame(UObject& Emitter)
	{
		DisableCommon(Emitter);
		Toggle(Emitter, TEXT("Drag"), false);
		SetBurst(Emitter, 3, 3, 200.f);
		UObject& Velocity = Toggle(Emitter, TEXT("AddVelocity"), true);
		SetText(Velocity, TEXT("VelocityType"), TEXT("Linear"));
		SetText(Velocity, TEXT("CoordinateSpace"), TEXT("Local"));
		// 速度只用来定火舌的方向（按速度对齐的精灵）：很小，不然几帧里火舌就离开枪口了
		SetRange3(Velocity, TEXT("LinearVelocityDistribution"), FVector3f(40.f, -7.f, -7.f), FVector3f(80.f, 7.f, 7.f));
		UObject& Init = Toggle(Emitter, TEXT("InitializeParticle"), true);
		SetRange(Init, TEXT("LifetimeDistribution"), 0.04f, 0.07f);
		SetRange2(Init, TEXT("SpriteSizeDistribution"), FVector2f(9.f, 24.f), FVector2f(13.f, 42.f));
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).InitConstant(FLinearColor(6.f, 3.f, 0.9f, 1.f));
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).UpdateValuesFromDistribution();
		SetAlphaOverLife(Toggle(Emitter, TEXT("ScaleColor"), true), {{0.f, 1.f}, {0.5f, 0.8f}, {1.f, 0.f}});
		Toggle(Emitter, TEXT("ScaleSpriteSize"), false);
		RandomFrame(Emitter);
		SetupSprite(Emitter, TEXT("/Game/Weapons/FX/M_MuzzleFlame.M_MuzzleFlame"), ENiagaraSpriteAlignment::VelocityAligned, ENiagaraSpriteFacingMode::FaceCamera, FVector2D(0.5, 1.0), ENiagaraSortMode::None).SortOrderHint = OverSmokeHint;
	}

	/**
	 * 烟的大小跟着组件的缩放（枪上的 MuzzleSmokeScale）：轻量发射器是局部空间，位置本来就跟着缩，精灵的大小要这个模块才跟。
	 * 没有它时手枪的 0.6 只把烟排得近了，每一团还是 98k 那么大
	 */
	void ApplyOwnerScaleToSmoke(UObject& Emitter)
	{
		SetText(Toggle(Emitter, TEXT("ApplyOwnerScaleToAttributes"), true), TEXT("SystemScaleData"), TEXT("(bScaleInitialSpriteSize=True)"));
	}

	/** 翻页的烟：贴图是模拟出来的一团（muzzle_smoke_sim.py），寿命里放完 8 × 8 帧。顺着枪管喷出去、很快停下，所以几团沿枪管排开，往上飘 */
	void SetupSmokeCommon(UObject& Emitter, const int32 FirstFrame)
	{
		UObject& SubUV = Toggle(Emitter, TEXT("SubUVAnimation"), true);
		SetText(SubUV, TEXT("NumFrames"), TEXT("64"));
		SetText(SubUV, TEXT("AnimationMode"), TEXT("Linear"));
		SetText(SubUV, TEXT("bStartFrameRangeOverride_Enabled"), FirstFrame > 0 ? TEXT("True") : TEXT("False"));
		SetText(SubUV, TEXT("StartFrameRangeOverride"), *FString::FromInt(FirstFrame));
		SetupSprite(Emitter, TEXT("/Game/Weapons/FX/M_MuzzleSmoke.M_MuzzleSmoke"), ENiagaraSpriteAlignment::Unaligned, ENiagaraSpriteFacingMode::FaceCamera, FVector2D(0.5, 0.5), ENiagaraSortMode::ViewDepth, 8);
	}

	/** 沿枪管喷的速度：圆锥默认朝 Z（枪的上方），转到 X */
	void SetupBarrelVelocity(UObject& Emitter, const float ConeAngle, const float MinSpeed, const float MaxSpeed)
	{
		UObject& Velocity = Toggle(Emitter, TEXT("AddVelocity"), true);
		SetText(Velocity, TEXT("VelocityType"), TEXT("InCone"));
		SetText(Velocity, TEXT("CoordinateSpace"), TEXT("Local"));
		SetText(Velocity, TEXT("ConeAngle"), *FString::SanitizeFloat(ConeAngle));
		SetRange(Velocity, TEXT("ConeVelocityDistribution"), MinSpeed, MaxSpeed);
		SetText(Velocity, TEXT("ConeRotationType"), TEXT("Rotation"));
		FNiagaraDistributionRangeRotator& ConeRotation = Field<FNiagaraDistributionRangeRotator>(Velocity, TEXT("ConeRotation"));
		ConeRotation.InitConstant(FRotator3f(-90.f, 0.f, 0.f));
		ConeRotation.UpdateValuesFromDistribution();
	}

	/** @struct FSmokePuff : 一组烟。喷得越远的团越大、散得越慢，几组排起来是个从枪口张开的锥 */
	struct FSmokePuff
	{
		int32 Count;
		float MinSpeed, MaxSpeed;
		float MinSize, MaxSize;
		float MinLife, MaxLife;
		float Alpha;
	};

	/** 烟：顺着枪管喷出去，停在枪口前（速度 ÷ 阻力），胀开、变薄、往上飘 */
	void BuildSmoke(UObject& Emitter, const FSmokePuff& Puff)
	{
		DisableCommon(Emitter);
		SetBurst(Emitter, Puff.Count, Puff.Count, 500.f);
		SetupBarrelVelocity(Emitter, 7.f, Puff.MinSpeed, Puff.MaxSpeed);
		SetRange(Toggle(Emitter, TEXT("Drag"), true), TEXT("DragDistribution"), 7.f, 9.f);
		UObject& Rise = Toggle(Emitter, TEXT("AccelerationForce"), true);
		SetText(Rise, TEXT("CoordinateSpace"), TEXT("World"));
		SetRange3(Rise, TEXT("AccelerationDistribution"), FVector3f(-4.f, -4.f, 14.f), FVector3f(4.f, 4.f, 32.f));
		Toggle(Emitter, TEXT("CurlNoiseForce"), false);
		UObject& Init = Toggle(Emitter, TEXT("InitializeParticle"), true);
		SetRange(Init, TEXT("LifetimeDistribution"), Puff.MinLife, Puff.MaxLife);
		// 整张贴图的边长：模拟的那团放完时约占七成
		SetRange2(Init, TEXT("SpriteSizeDistribution"), FVector2f(Puff.MinSize), FVector2f(Puff.MaxSize));
		// 贴图里的光从上面来：只转一点
		SetRange(Init, TEXT("SpriteRotationDistribution"), -22.f, 22.f);
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).InitConstant(FLinearColor(0.85f, 0.85f, 0.85f, Puff.Alpha));
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).UpdateValuesFromDistribution();
		SetAlphaOverLife(Toggle(Emitter, TEXT("ScaleColor"), true), {{0.f, 0.f}, {0.012f, 1.f}, {0.25f, 0.7f}, {0.55f, 0.32f}, {1.f, 0.f}});
		SetSizeOverLife(Toggle(Emitter, TEXT("ScaleSpriteSize"), true), {{0.f, 1.f}, {1.f, 1.22f}});
		SetRange(Toggle(Emitter, TEXT("SpriteRotationRate"), true), TEXT("RotationRateDistribution"), -5.f, 5.f);
		ApplyOwnerScaleToSmoke(Emitter);
		SetupSmokeCommon(Emitter, 0);
	}

	/** 留下的薄雾：大、很淡、飘得久，从模拟后面的帧开始（那时已经散了） */
	void BuildHaze(UObject& Emitter)
	{
		DisableCommon(Emitter);
		SetBurst(Emitter, 2, 2, 600.f);
		SetupBarrelVelocity(Emitter, 12.f, 120.f, 480.f);
		SetRange(Toggle(Emitter, TEXT("Drag"), true), TEXT("DragDistribution"), 4.f, 6.f);
		UObject& Rise = Toggle(Emitter, TEXT("AccelerationForce"), true);
		SetText(Rise, TEXT("CoordinateSpace"), TEXT("World"));
		SetRange3(Rise, TEXT("AccelerationDistribution"), FVector3f(-5.f, -5.f, 8.f), FVector3f(5.f, 5.f, 18.f));
		Toggle(Emitter, TEXT("CurlNoiseForce"), false);
		UObject& Init = Toggle(Emitter, TEXT("InitializeParticle"), true);
		SetRange(Init, TEXT("LifetimeDistribution"), 1.8f, 2.6f);
		SetRange2(Init, TEXT("SpriteSizeDistribution"), FVector2f(150.f), FVector2f(210.f));
		SetRange(Init, TEXT("SpriteRotationDistribution"), -30.f, 30.f);
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).InitConstant(FLinearColor(0.85f, 0.85f, 0.86f, 0.22f));
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).UpdateValuesFromDistribution();
		SetAlphaOverLife(Toggle(Emitter, TEXT("ScaleColor"), true), {{0.f, 0.f}, {0.08f, 1.f}, {0.5f, 0.5f}, {1.f, 0.f}});
		SetSizeOverLife(Toggle(Emitter, TEXT("ScaleSpriteSize"), true), {{0.f, 0.8f}, {1.f, 1.3f}});
		SetRange(Toggle(Emitter, TEXT("SpriteRotationRate"), true), TEXT("RotationRateDistribution"), -3.f, 3.f);
		ApplyOwnerScaleToSmoke(Emitter);
		SetupSmokeCommon(Emitter, 20);
	}

	/** 闪光：一盏灯，一两帧，照亮枪和近处的墙。灯只能 CPU 模拟，所以自成一个发射器，别的发射器还能走 GPU */
	void BuildLight(UObject& Emitter)
	{
		DisableCommon(Emitter);
		Toggle(Emitter, TEXT("AddVelocity"), false);
		Toggle(Emitter, TEXT("Drag"), false);
		Toggle(Emitter, TEXT("ScaleSpriteSize"), false);
		SetBurst(Emitter, 1, 1, 600.f);
		UObject& Init = Toggle(Emitter, TEXT("InitializeParticle"), true);
		SetRange(Init, TEXT("LifetimeDistribution"), 0.07f, 0.07f);
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).InitConstant(FLinearColor(30.f, 15.f, 5.f, 1.f));
		Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution")).UpdateValuesFromDistribution();
		// 灯的颜色乘透明度：用 ScaleColor 的 RGB 衰减
		FNiagaraDistributionColor& Fade = Field<FNiagaraDistributionColor>(Toggle(Emitter, TEXT("ScaleColor"), true), TEXT("ScaleDistribution"));
		Fade.Mode = ENiagaraDistributionMode::NonUniformCurve;
		const FRichCurve Down = MakeCurve({{0.f, 1.f}, {0.35f, 0.75f}, {1.f, 0.f}});
		Fade.ChannelCurves = {Down, Down, Down, MakeCurve({{0.f, 1.f}, {1.f, 1.f}})};
		Fade.UpdateValuesFromDistribution();
		UObject& Attributes = Toggle(Emitter, TEXT("LightAttributes"), true);
		SetText(Attributes, TEXT("bApplyRadius"), TEXT("True"));
		Field<FNiagaraDistributionFloat>(Attributes, TEXT("Radius")).InitConstant(520.f);
		Field<FNiagaraDistributionFloat>(Attributes, TEXT("Radius")).UpdateValuesFromDistribution();

		// 精灵渲染器换成灯
		const FArrayProperty* Array = FindFProperty<FArrayProperty>(Emitter.GetClass(), TEXT("RendererProperties"));
		const FObjectPropertyBase* Inner = CastFieldChecked<FObjectPropertyBase>(Array->Inner);
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(&Emitter));
		UNiagaraLightRendererProperties* Light = NewObject<UNiagaraLightRendererProperties>(&Emitter, NAME_None, RF_Transactional);
		Light->RadiusScale = 1.f;
		Inner->SetObjectPropertyValue(Helper.GetRawPtr(0), Light);
	}

	/** 按名字找模块：模板里同名的只有一个 */
	UNiagaraNodeFunctionCall& FindModuleNode(const UNiagaraGraph& Graph, const TCHAR* Name)
	{
		TArray<UNiagaraNodeFunctionCall*> Calls;
		Graph.GetNodesOfClass<UNiagaraNodeFunctionCall>(Calls);
		UNiagaraNodeFunctionCall** Found = Calls.FindByPredicate([Name](const UNiagaraNodeFunctionCall* Call) { return Call->GetFunctionName() == Name; });
		checkf(Found, TEXT("没有模块 %s"), Name);
		return **Found;
	}

	/** 设快速迭代参数：模块的输入没有接别的时候值在这里。粒子的两段脚本都有（插值出生时更新也在出生脚本里跑） */
	template <typename T>
	void SetRapid(const FVersionedNiagaraEmitterData& Data, const FString& EmitterName, const TCHAR* Input, const FNiagaraTypeDefinition& Type, const T& Value)
	{
		TArray<UNiagaraScript*> Scripts;
		Data.GetScripts(Scripts, /*bCompilableOnly*/ false);
		const FNiagaraVariable Variable(Type, *FString::Printf(TEXT("Constants.%s.%s"), *EmitterName, Input));
		// 自己加的模块（卷曲噪声；弹道的烟的阻力）还没有这个参数：粒子的两段脚本都加上
		const bool bAddedModule = FString(Input).StartsWith(TEXT("CurlNoiseForce")) || FString(Input).StartsWith(TEXT("Drag."));
		int32 Applied = 0;
		for (UNiagaraScript* Script : Scripts)
		{
			const bool bParticleScript = Script->IsParticleSpawnScript() || Script->IsParticleUpdateScript();
			if (Script->RapidIterationParameters.IndexOf(Variable) != INDEX_NONE || (bAddedModule && bParticleScript))
			{
				Script->RapidIterationParameters.SetParameterValue(Value, Variable, /*bAdd*/ true);
				++Applied;
			}
		}
		checkf(Applied > 0, TEXT("没有参数 %s"), *Variable.GetName().ToString());
	}

	/** 模块的一个输入接到系统的用户参数上（武器组件每帧写） */
	void LinkModuleInput(UNiagaraNodeFunctionCall& Module, const TCHAR* Input, const FNiagaraVariable& UserParameter)
	{
		const FNiagaraParameterHandle Handle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle::CreateModuleParameterHandle(Input), &Module);
		UEdGraphPin& Pin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(Module, Handle, UserParameter.GetType(), FGuid(), FGuid());
		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(Pin, UserParameter, {UserParameter});
	}

	/** 静态开关的枚举：内部名=显示的名字，一个一个列出来（调的时候看） */
	FString DescribeSwitchValues(const UEdGraphPin& Pin)
	{
		const UEnum* Enum = Cast<UEnum>(Pin.PinType.PinSubCategoryObject.Get());
		if (!Enum)
		{
			return FString();
		}
		FString Values;
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			Values += FString::Printf(TEXT(" %s=%s"), *Enum->GetNameStringByIndex(Index), *Enum->GetDisplayNameTextByIndex(Index).ToString());
		}
		return Values;
	}

	/** 模块的静态开关按显示的名字设：开关在模块节点自己的针脚上（同编辑器的堆栈），枚举的内部名是 NewEnumeratorN，同显示的对不上 */
	void SetStaticSwitch(UNiagaraNodeFunctionCall& Module, const TCHAR* Name, const TCHAR* DisplayValue)
	{
		UEdGraphPin* Pin = Module.FindPin(FName(Name), EGPD_Input);
		checkf(Pin, TEXT("模块 %s 没有开关 %s"), *Module.GetFunctionName(), Name);
		FString Value = DisplayValue;
		if (const UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
		{
			int32 Found = INDEX_NONE;
			for (int32 Index = 0; Index < Enum->NumEnums() - 1 && Found == INDEX_NONE; ++Index)
			{
				Found = Enum->GetDisplayNameTextByIndex(Index).ToString() == DisplayValue ? Index : INDEX_NONE;
			}
			checkf(Found != INDEX_NONE, TEXT("开关 %s.%s 没有 %s：%s"), *Module.GetFunctionName(), Name, DisplayValue, *DescribeSwitchValues(*Pin));
			Value = Enum->GetNameStringByIndex(Found);
		}
		Pin->Modify();
		Pin->DefaultValue = Value;
		Module.MarkNodeRequiresSynchronization(TEXT("GinkgoWeaponFx 设静态开关"), /*bRaiseGraphNeedsRecompile*/ true);
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("  开关 %s.%s = %s（%s）"), *Module.GetFunctionName(), Name, DisplayValue, *Value);
	}

	/**
	 * 枪口的小硝烟：一条细的飘带，从枪口冒出来往上飘。普通发射器（轻量的只有局部空间，飘带会和枪绑死）：世界空间，枪动的时候留在原地的部分落在后面。
	 * 宽度、透明度随年龄都在材质里（M_MuzzleWisp），这里只管位置；颜色接用户参数，武器组件每帧写，越冒越淡
	 */
	bool BuildWisp()
	{
		UPackage* Package = CreatePackage(WispPackage);
		Package->FullyLoad();
		const FName AssetName(*FPackageName::GetShortName(WispPackage));
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName.ToString()))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
		}
		UNiagaraEmitter* Source = LoadObject<UNiagaraEmitter>(nullptr, WispTemplatePath);
		UNiagaraScript* CurlNoise = LoadObject<UNiagaraScript>(nullptr, CurlNoisePath);
		if (!Source || !CurlNoise)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到发射器模板或卷曲噪声的模块"));
			return false;
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/FX/M_MuzzleWisp.M_MuzzleWisp"));
		checkf(Material, TEXT("读不到 M_MuzzleWisp：先跑 muzzle_fx_ue.py"));

		UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
		UNiagaraSystemFactoryNew::InitializeSystem(System, /*bCreateDefaultNodes*/ true);
		const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Source, Source->GetExposedVersion().VersionGuid, /*bCreateCopy*/ true);
		FNiagaraEmitterHandle* Handle = System->GetEmitterHandles().FindByPredicate([&HandleId](const FNiagaraEmitterHandle& Item) { return Item.GetId() == HandleId; });
		check(Handle);
		Handle->SetName(TEXT("Wisp"), *System);
		UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
		const FGuid Version = Handle->GetInstance().Version;
		FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
		const FString EmitterName = Emitter->GetUniqueEmitterName();
		Data->bLocalSpace = false;
		Data->SimTarget = ENiagaraSimTarget::CPUSim;
		Data->bRequiresPersistentIDs = true;

		UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(Data->GraphSource)->NodeGraph;
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(FindModuleNode(*Graph, TEXT("ShapeLocation")), false);
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(FindModuleNode(*Graph, TEXT("AddVelocity")), false);
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(FindModuleNode(*Graph, TEXT("ScaleColor")), false);
		// 卷曲噪声是力，要排在解算之前（解算之后加的力没人用）：模板的第一个是粒子状态，紧跟它，后面是重力、阻力，它们之间的先后不影响结果
		UNiagaraNodeOutput* UpdateOutput = Graph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleUpdateScript);
		check(UpdateOutput);
		FNiagaraStackGraphUtilities::AddScriptModuleToStack(CurlNoise, *UpdateOutput, 1);

		const FNiagaraTypeDefinition Float = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition Vector = FNiagaraTypeDefinition::GetVec3Def();
		SetRapid<float>(*Data, EmitterName, TEXT("SpawnRate.SpawnRate"), Float, 20.f);
		SetRapid<float>(*Data, EmitterName, TEXT("InitializeParticle.Lifetime Min"), Float, 1.6f);
		SetRapid<float>(*Data, EmitterName, TEXT("InitializeParticle.Lifetime Max"), Float, 1.6f);
		// 热的烟往上：加速度除以阻力是最后的速度，60 cm/s
		SetRapid<FVector3f>(*Data, EmitterName, TEXT("GravityForce.Gravity"), Vector, FVector3f(0.f, 0.f, 150.f));
		SetRapid<float>(*Data, EmitterName, TEXT("Drag.Drag"), Float, 2.5f);
		SetRapid<float>(*Data, EmitterName, TEXT("CurlNoiseForce.Noise Strength"), Float, 220.f);
		SetRapid<float>(*Data, EmitterName, TEXT("CurlNoiseForce.Noise Frequency"), Float, 35.f);
		// 噪声场自己也往下走：不然每个粒子走同一条线，飘带的形状不变
		SetRapid<FVector3f>(*Data, EmitterName, TEXT("CurlNoiseForce.Pan Noise Field"), Vector, FVector3f(0.f, 0.f, -30.f));
		// 飘带的宽度：初始化模块的那个输入要开开关才写，单独放一个赋值。没有这个属性飘带只有 1 cm 宽
		// 初速度就是最后的速度：从 0 起步的话枪口附近点挤在一起，噪声把每一段的朝向搅乱，飘带叠成一团条纹
		UNiagaraNodeOutput* SpawnOutput = Graph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleSpawnScript);
		check(SpawnOutput);
		FNiagaraStackGraphUtilities::AddParameterModuleToStack({FNiagaraVariable(Float, TEXT("Particles.RibbonWidth")), FNiagaraVariable(Vector, TEXT("Particles.Velocity"))}, *SpawnOutput, INDEX_NONE, {TEXT("14.0"), TEXT("0.0,0.0,60.0")});

		// 颜色接用户参数
		const FNiagaraTypeDefinition ColorDef = FNiagaraTypeDefinition::GetColorDef();
		const FNiagaraVariable WispColor(ColorDef, WispColorParameter);
		System->GetExposedParameters().AddParameter(WispColor, /*bInitialize*/ true, /*bTriggerRebind*/ true);
		const FLinearColor DefaultColor(0.72f, 0.72f, 0.72f, 0.f);
		System->GetExposedParameters().SetParameterValue(DefaultColor, WispColor, /*bAdd*/ false);
		UNiagaraNodeFunctionCall& Initialize = FindModuleNode(*Graph, TEXT("InitializeParticle"));
		const FNiagaraParameterHandle ColorHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(FNiagaraParameterHandle::CreateModuleParameterHandle(TEXT("Color")), &Initialize);
		UEdGraphPin& ColorPin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(Initialize, ColorHandle, ColorDef, FGuid(), FGuid());
		FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(ColorPin, WispColor, {WispColor});

		// 飘带代替精灵
		const TArray<UNiagaraRendererProperties*> OldRenderers = Data->GetRenderers();
		for (UNiagaraRendererProperties* Old : OldRenderers)
		{
			Emitter->RemoveRenderer(Old, Version);
		}
		UNiagaraRibbonRendererProperties* Ribbon = NewObject<UNiagaraRibbonRendererProperties>(Emitter, NAME_None, RF_Transactional);
		Ribbon->Material = Material;
		Ribbon->FacingMode = ENiagaraRibbonFacingMode::Screen;
		Ribbon->UV0Settings.DistributionMode = ENiagaraRibbonUVDistributionMode::TiledOverRibbonLength;
		Ribbon->UV0Settings.TilingLength = 70.f;
		Ribbon->bCastShadows = false;
		Emitter->AddRenderer(Ribbon, Version);
		Graph->NotifyGraphChanged();

		System->RequestCompile(true);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ true);
		FAssetRegistryModule::AssetCreated(System);
		const FString Filename = FPackageName::LongPackageNameToFilename(WispPackage, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, System, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%s：飘带，发射器 %s"), WispPackage, *EmitterName);
		return true;
	}

	/**
	 * 子弹、弹壳的碰撞：一个沿 X 的胶囊。放在这里不放在 round_ue_import.py：Python 里加碰撞要编辑器的子系统，命令行里没有
	 * @param Length - 整个的长度（cm）
	 */
	bool SetupRoundCollision(const TCHAR* Path, const float Radius, const float Length)
	{
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path);
		if (!Mesh)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到 %s：先跑 round_ue_import.py"), Path);
			return false;
		}
		Mesh->CreateBodySetup();
		UBodySetup* Body = Mesh->GetBodySetup();
		Body->RemoveSimpleCollision();
		FKSphylElem Capsule(Radius, Length - 2.f * Radius);
		// 胶囊默认沿 Z
		Capsule.Rotation = FRotator(90.f, 0.f, 0.f);
		Body->AggGeom.SphylElems.Add(Capsule);
		Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
		Mesh->MarkPackageDirty();
		UPackage* Package = Mesh->GetOutermost();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Package, Mesh, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%s：胶囊半径 %.2f、长 %.2f cm"), Path, Radius, Length);
		return true;
	}

	/** 打出一个普通发射器：每段脚本的模块、快速迭代参数、渲染器。写飘带的时候看 */
	void DumpEmitter(const UNiagaraEmitter& Emitter)
	{
		const FVersionedNiagaraEmitterData* Data = Emitter.GetLatestEmitterData();
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("发射器 %s：local %d，sim %d，bounds %d"), *Emitter.GetName(), Data->bLocalSpace ? 1 : 0, static_cast<int32>(Data->SimTarget), static_cast<int32>(Data->CalculateBoundsMode));
		const UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(Data->GraphSource)->NodeGraph;
		TArray<UNiagaraNodeOutput*> Outputs;
		Graph->GetNodesOfClass<UNiagaraNodeOutput>(Outputs);
		for (const UNiagaraNodeOutput* Output : Outputs)
		{
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("输出节点 usage %d"), static_cast<int32>(Output->GetUsage()));
		}
		TArray<UNiagaraNodeFunctionCall*> Calls;
		Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(Calls);
		const FVersionedNiagaraEmitter Versioned(const_cast<UNiagaraEmitter*>(&Emitter), Emitter.GetExposedVersion().VersionGuid);
		for (const UNiagaraNodeFunctionCall* Call : Calls)
		{
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("模块 %s（%s）"), *Call->GetFunctionName(), *GetPathNameSafe(Call->FunctionScript));
			// 模块的输入（接用户参数、写快速迭代参数时用的名字）与静态开关（在节点的针脚上）
			const FCompileConstantResolver Resolver(Versioned, ENiagaraScriptUsage::Module);
			TArray<FNiagaraVariable> Inputs;
			FNiagaraStackGraphUtilities::GetStackFunctionInputs(*Call, Inputs, Resolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly);
			for (const FNiagaraVariable& Input : Inputs)
			{
				UE_LOG(LogGinkgoWeaponFx, Display, TEXT("  输入 %s : %s"), *Input.GetName().ToString(), *Input.GetType().GetName());
			}
			TArray<UEdGraphPin*> Switches;
			TSet<UEdGraphPin*> Hidden;
			FCompileConstantResolver SwitchResolver(Versioned, ENiagaraScriptUsage::Module);
			FNiagaraStackGraphUtilities::GetStackFunctionStaticSwitchPins(*Call, Switches, Hidden, SwitchResolver);
			for (const UEdGraphPin* Pin : Switches)
			{
				UE_LOG(LogGinkgoWeaponFx, Display, TEXT("  开关 %s = %s%s%s"), *Pin->PinName.ToString(), *Pin->DefaultValue, Hidden.Contains(Pin) ? TEXT("（藏着）") : TEXT(""),
					*DescribeSwitchValues(*Pin));
			}
		}
		TArray<UNiagaraScript*> Scripts;
		Data->GetScripts(Scripts, /*bCompilableOnly*/ false);
		for (const UNiagaraScript* Script : Scripts)
		{
			TArray<FNiagaraVariable> Parameters;
			Script->RapidIterationParameters.GetParameters(Parameters);
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("脚本 usage %d：%d 个快速迭代参数"), static_cast<int32>(Script->GetUsage()), Parameters.Num());
			for (const FNiagaraVariable& Parameter : Parameters)
			{
				FString Value;
				if (Parameter.GetType() == FNiagaraTypeDefinition::GetFloatDef())
				{
					Value = FString::SanitizeFloat(Script->RapidIterationParameters.GetParameterValue<float>(Parameter));
				}
				else if (Parameter.GetType() == FNiagaraTypeDefinition::GetVec3Def())
				{
					Value = Script->RapidIterationParameters.GetParameterValue<FVector3f>(Parameter).ToString();
				}
				UE_LOG(LogGinkgoWeaponFx, Display, TEXT("  %s : %s = %s"), *Parameter.GetName().ToString(), *Parameter.GetType().GetName(), *Value);
			}
		}
		for (const UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("渲染器 %s"), *GetNameSafe(Renderer ? Renderer->GetClass() : nullptr));
			if (Renderer)
			{
				DumpObject(*Renderer, 1);
			}
		}
	}

	/**
	 * 枪口的热浪：热的枪管上方的空气往上走，折射着后面的东西晃（M_HeatHaze 只有折射）。普通发射器、世界空间：热空气留在原地往上走，枪挪开了它不跟着。
	 * 出生的地方是枪口往后一段沿枪管拉长的椭球（组件挂在枪上，X 是枪管），往上 40 cm/s 冒出来，浮力与阻力让它停在约 45 cm/s，卷曲噪声搅着它。
	 * 出生率、透明度都接用户参数：武器组件按枪管多热每帧写，冷的时候一个也不冒（UGinkgoWeaponComponent::UpdateBarrelHeat）
	 */
	bool BuildHeat()
	{
		UPackage* Package = CreatePackage(HeatPackage);
		Package->FullyLoad();
		const FName AssetName(*FPackageName::GetShortName(HeatPackage));
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName.ToString()))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
		}
		UNiagaraEmitter* Source = LoadObject<UNiagaraEmitter>(nullptr, WispTemplatePath);
		UNiagaraScript* CurlNoise = LoadObject<UNiagaraScript>(nullptr, CurlNoisePath);
		if (!Source || !CurlNoise)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到发射器模板或卷曲噪声的模块"));
			return false;
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/FX/M_HeatHaze.M_HeatHaze"));
		checkf(Material, TEXT("读不到 M_HeatHaze：先跑 heat_haze_ue.py"));

		UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
		UNiagaraSystemFactoryNew::InitializeSystem(System, /*bCreateDefaultNodes*/ true);
		const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Source, Source->GetExposedVersion().VersionGuid, /*bCreateCopy*/ true);
		FNiagaraEmitterHandle* Handle = System->GetEmitterHandles().FindByPredicate([&HandleId](const FNiagaraEmitterHandle& Item) { return Item.GetId() == HandleId; });
		check(Handle);
		Handle->SetName(TEXT("Heat"), *System);
		UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
		FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
		const FString EmitterName = Emitter->GetUniqueEmitterName();
		Data->bLocalSpace = false;
		Data->SimTarget = ENiagaraSimTarget::CPUSim;

		UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(Data->GraphSource)->NodeGraph;
		// 速度直接给（世界的上），透明度随年龄在材质里
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(FindModuleNode(*Graph, TEXT("AddVelocity")), false);
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(FindModuleNode(*Graph, TEXT("ScaleColor")), false);
		UNiagaraNodeOutput* UpdateOutput = Graph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleUpdateScript);
		check(UpdateOutput);
		FNiagaraStackGraphUtilities::AddScriptModuleToStack(CurlNoise, *UpdateOutput, 1);

		const FNiagaraTypeDefinition Float = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition Vector = FNiagaraTypeDefinition::GetVec3Def();
		SetRapid<float>(*Data, EmitterName, TEXT("InitializeParticle.Lifetime Min"), Float, 0.7f);
		SetRapid<float>(*Data, EmitterName, TEXT("InitializeParticle.Lifetime Max"), Float, 1.1f);
		SetRapid<float>(*Data, EmitterName, TEXT("InitializeParticle.Uniform Sprite Size Min"), Float, 9.f);
		SetRapid<float>(*Data, EmitterName, TEXT("InitializeParticle.Uniform Sprite Size Max"), Float, 16.f);
		// 枪口往后的一段枪管：半径 4 cm 的球沿枪管（组件的 X）拉长到 ±10 cm，横着只有 ±2 cm
		SetRapid<float>(*Data, EmitterName, TEXT("ShapeLocation.Sphere Radius"), Float, 4.f);
		SetRapid<FVector3f>(*Data, EmitterName, TEXT("ShapeLocation.Non Uniform Scale"), Vector, FVector3f(2.5f, 0.5f, 0.5f));
		// 热空气往上：浮力除以阻力是最后的速度，44 cm/s
		SetRapid<FVector3f>(*Data, EmitterName, TEXT("GravityForce.Gravity"), Vector, FVector3f(0.f, 0.f, 110.f));
		SetRapid<float>(*Data, EmitterName, TEXT("Drag.Drag"), Float, 2.5f);
		SetRapid<float>(*Data, EmitterName, TEXT("CurlNoiseForce.Noise Strength"), Float, 80.f);
		SetRapid<float>(*Data, EmitterName, TEXT("CurlNoiseForce.Noise Frequency"), Float, 30.f);
		SetRapid<FVector3f>(*Data, EmitterName, TEXT("CurlNoiseForce.Pan Noise Field"), Vector, FVector3f(0.f, 0.f, -20.f));
		UNiagaraNodeOutput* SpawnOutput = Graph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleSpawnScript);
		check(SpawnOutput);
		// 起步就往上走得快一点：刚冒出来的还贴着枪管，折射在材质里先弱着（M_HeatHaze 的前 40%）
		FNiagaraStackGraphUtilities::AddParameterModuleToStack({FNiagaraVariable(Vector, TEXT("Particles.Velocity"))}, *SpawnOutput, INDEX_NONE, {TEXT("0.0,0.0,40.0")});

		// 用户参数：透明度是折射有多强，出生率是冒得多密
		const FNiagaraVariable HeatColor(FNiagaraTypeDefinition::GetColorDef(), HeatColorParameter);
		const FNiagaraVariable HeatRate(Float, HeatRateParameter);
		System->GetExposedParameters().AddParameter(HeatColor, /*bInitialize*/ true, /*bTriggerRebind*/ true);
		System->GetExposedParameters().AddParameter(HeatRate, /*bInitialize*/ true, /*bTriggerRebind*/ true);
		System->GetExposedParameters().SetParameterValue(FLinearColor(1.f, 1.f, 1.f, 0.f), HeatColor, /*bAdd*/ false);
		System->GetExposedParameters().SetParameterValue(0.f, HeatRate, /*bAdd*/ false);
		LinkModuleInput(FindModuleNode(*Graph, TEXT("InitializeParticle")), TEXT("Color"), HeatColor);
		LinkModuleInput(FindModuleNode(*Graph, TEXT("SpawnRate")), TEXT("SpawnRate"), HeatRate);

		int32 Sprites = 0;
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{
				Sprite->Material = Material;
				++Sprites;
			}
		}
		checkf(Sprites == 1, TEXT("模板应该正好一个精灵渲染器，有 %d 个"), Sprites);
		Graph->NotifyGraphChanged();

		System->RequestCompile(true);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ true);
		FAssetRegistryModule::AssetCreated(System);
		const FString Filename = FPackageName::LongPackageNameToFilename(HeatPackage, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, System, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%s：热浪，发射器 %s"), HeatPackage, *EmitterName);
		return true;
	}

	/**
	 * 弹道的烟（Docs/Design/Weapons.md「弹道的烟」）：子弹飞过的一道淡淡的烟。光束的模板（普通发射器、世界空间）：一次性爆发 User.TrailPoints 个粒子，
	 * 出生时沿 枪口（系统的位置）→ 枪口 + User.TrailEnd 摆开，飘带连起来；之后卷曲噪声推着、阻力拖着慢慢散开。
	 * 宽度（User.TrailWidth）是飘带的宽度：子弹上的宽度乘 Ginkgo::Combat::TrailGeometryScale（远处至少 1.5 px 要有地方画）；变宽、淡掉、按弹速从枪口长出来都在材质里（M_BulletTrail，bullet_trail_ue.py）：
	 * 长到头的快慢（User.TrailReveal）借 Material Random 交给材质（每道烟一个数，不加动态参数的模块）
	 */
	bool BuildTrail()
	{
		UPackage* Package = CreatePackage(TrailPackage);
		Package->FullyLoad();
		const FName AssetName(*FPackageName::GetShortName(TrailPackage));
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName.ToString()))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
		}
		UNiagaraEmitter* Source = LoadObject<UNiagaraEmitter>(nullptr, TrailTemplatePath);
		UNiagaraScript* CurlNoise = LoadObject<UNiagaraScript>(nullptr, CurlNoisePath);
		UNiagaraScript* Drag = LoadObject<UNiagaraScript>(nullptr, DragPath);
		if (!Source || !CurlNoise || !Drag)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到光束的模板、卷曲噪声或阻力的模块"));
			return false;
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Weapons/FX/M_BulletTrail.M_BulletTrail"));
		checkf(Material, TEXT("读不到 M_BulletTrail：先跑 bullet_trail_ue.py"));

		UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);
		UNiagaraSystemFactoryNew::InitializeSystem(System, /*bCreateDefaultNodes*/ true);
		const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Source, Source->GetExposedVersion().VersionGuid, /*bCreateCopy*/ true);
		FNiagaraEmitterHandle* Handle = System->GetEmitterHandles().FindByPredicate([&HandleId](const FNiagaraEmitterHandle& Item) { return Item.GetId() == HandleId; });
		check(Handle);
		Handle->SetName(TEXT("Trail"), *System);
		UNiagaraEmitter* Emitter = Handle->GetInstance().Emitter;
		FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
		const FString EmitterName = Emitter->GetUniqueEmitterName();
		Data->bLocalSpace = false;
		Data->SimTarget = ENiagaraSimTarget::CPUSim;

		UNiagaraGraph* Graph = CastChecked<UNiagaraScriptSource>(Data->GraphSource)->NodeGraph;
		const FNiagaraTypeDefinition Float = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition Vector = FNiagaraTypeDefinition::GetVec3Def();
		// 只放一次、马上不再放：模板一直循环（4 s 一圈又爆发一次），组件不会完成、不回池。粒子活完发射器就完成
		SetStaticSwitch(FindModuleNode(*Graph, TEXT("EmitterState")), TEXT("Loop Behavior"), TEXT("Once"));
		SetRapid<float>(*Data, EmitterName, TEXT("EmitterState.Loop Duration"), Float, 0.05f);
		// 宽度不按沿线的曲线：初始化直接给（用户参数），光束的宽度模块关掉；颜色随年龄在材质里，更新里的颜色模块关掉
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(FindModuleNode(*Graph, TEXT("BeamWidth")), false);
		FNiagaraStackGraphUtilities::SetModuleIsEnabled(FindModuleNode(*Graph, TEXT("Color")), false);
		UNiagaraNodeFunctionCall& Initialize = FindModuleNode(*Graph, TEXT("InitializeParticle"));
		SetStaticSwitch(Initialize, TEXT("Ribbon Width Mode"), TEXT("Direct Set"));
		// 颜色也要开：模板的初始化不写颜色（Unset，颜色在更新里的颜色模块写，上面关了），不开的话粒子是默认的白、不透明，用户参数的颜色、浓淡都不起作用
		SetStaticSwitch(Initialize, TEXT("Color Mode"), TEXT("Direct Set"));
		// 散开：卷曲噪声推着、阻力拖着（都是力，排在解算之前；模板的更新是粒子状态、颜色、解算）。噪声场自己慢慢往上走，烟一段一段各自弯
		UNiagaraNodeOutput* UpdateOutput = Graph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleUpdateScript);
		check(UpdateOutput);
		FNiagaraStackGraphUtilities::AddScriptModuleToStack(CurlNoise, *UpdateOutput, 1);
		FNiagaraStackGraphUtilities::AddScriptModuleToStack(Drag, *UpdateOutput, 2);
		SetRapid<float>(*Data, EmitterName, TEXT("CurlNoiseForce.Noise Strength"), Float, 30.f);
		SetRapid<float>(*Data, EmitterName, TEXT("CurlNoiseForce.Noise Frequency"), Float, 300.f);
		SetRapid<FVector3f>(*Data, EmitterName, TEXT("CurlNoiseForce.Pan Noise Field"), Vector, FVector3f(0.f, 0.f, 8.f));
		SetRapid<float>(*Data, EmitterName, TEXT("Drag.Drag"), Float, 2.f);

		// 用户参数（武器组件每道烟写一次）
		const FNiagaraVariable End(Vector, TEXT("User.TrailEnd"));
		const FNiagaraVariable Points(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.TrailPoints"));
		const FNiagaraVariable Lifetime(Float, TEXT("User.TrailLifetime"));
		const FNiagaraVariable Width(Float, TEXT("User.TrailWidth"));
		const FNiagaraVariable Reveal(Float, TEXT("User.TrailReveal"));
		const FNiagaraVariable Color(FNiagaraTypeDefinition::GetColorDef(), TEXT("User.TrailColor"));
		FNiagaraUserRedirectionParameterStore& Exposed = System->GetExposedParameters();
		for (const FNiagaraVariable* Parameter : {&End, &Points, &Lifetime, &Width, &Reveal, &Color})
		{
			Exposed.AddParameter(*Parameter, /*bInitialize*/ true, /*bTriggerRebind*/ true);
		}
		Exposed.SetParameterValue(FVector3f(1000.f, 0.f, 0.f), End, /*bAdd*/ false);
		Exposed.SetParameterValue(0, Points, /*bAdd*/ false);
		Exposed.SetParameterValue(1.2f, Lifetime, /*bAdd*/ false);
		Exposed.SetParameterValue(42.f, Width, /*bAdd*/ false);
		Exposed.SetParameterValue(30.f, Reveal, /*bAdd*/ false);
		Exposed.SetParameterValue(FLinearColor(1.5f, 1.5f, 1.5f, 0.8f), Color, /*bAdd*/ false);
		LinkModuleInput(FindModuleNode(*Graph, TEXT("BeamEmitterSetup")), TEXT("Beam End"), End);
		LinkModuleInput(FindModuleNode(*Graph, TEXT("SpawnBurst_Instantaneous")), TEXT("Spawn Count"), Points);
		LinkModuleInput(Initialize, TEXT("Lifetime"), Lifetime);
		LinkModuleInput(Initialize, TEXT("Color"), Color);
		LinkModuleInput(Initialize, TEXT("Ribbon Width"), Width);
		LinkModuleInput(Initialize, TEXT("Material Random"), Reveal);

		// 飘带：对着屏幕；UV0 沿线每 150 cm 一段（丝缕），UV1 整条从枪口 0 到头 1（按弹速长出来）
		int32 Ribbons = 0;
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (UNiagaraRibbonRendererProperties* Ribbon = Cast<UNiagaraRibbonRendererProperties>(Renderer))
			{
				Ribbon->Material = Material;
				Ribbon->FacingMode = ENiagaraRibbonFacingMode::Screen;
				Ribbon->UV0Settings.DistributionMode = ENiagaraRibbonUVDistributionMode::TiledOverRibbonLength;
				Ribbon->UV0Settings.TilingLength = 150.f;
				Ribbon->UV1Settings.DistributionMode = ENiagaraRibbonUVDistributionMode::ScaledUsingRibbonSegmentLength;
				Ribbon->bCastShadows = false;
				++Ribbons;
			}
		}
		checkf(Ribbons == 1, TEXT("模板应该正好一个飘带渲染器，有 %d 个"), Ribbons);
		Graph->NotifyGraphChanged();

		System->RequestCompile(true);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ true);
		FAssetRegistryModule::AssetCreated(System);
		const FString Filename = FPackageName::LongPackageNameToFilename(TrailPackage, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, System, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%s：弹道的烟，发射器 %s"), TrailPackage, *EmitterName);
		DumpEmitter(*Emitter);
		return true;
	}

	/** -SmokeOwnerScale：已有的系统只开烟的这个模块，别的不动。缩放是 1 时同没开一样 */
	bool EnableSmokeOwnerScale()
	{
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *(FString(SystemPackage) + TEXT(".") + FPackageName::GetShortName(SystemPackage)));
		if (!System)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到 %s"), SystemPackage);
			return false;
		}
		for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			const FName Name = Handle.GetName();
			if (Name == TEXT("SmokeNear") || Name == TEXT("SmokeFar") || Name == TEXT("Haze"))
			{
				UObject& Emitter = *EmitterOf(Handle);
				ApplyOwnerScaleToSmoke(Emitter);
				Emitter.PostEditChange();
				UE_LOG(LogGinkgoWeaponFx, Display, TEXT("发射器 %s：精灵的大小跟着组件的缩放"), *Name.ToString());
			}
		}
		System->RequestCompile(true);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ true);
		const FString Filename = FPackageName::LongPackageNameToFilename(SystemPackage, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(System->GetPackage(), System, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		return true;
	}

	/** 只有火光、没有硝烟的枪口效果：连发的枪隔一阵才冒一次烟（MuzzleSmokeInterval），中间的枪放它。从 NS_MuzzleBlast 复制，去掉烟的发射器 */
	bool BuildFlashOnly()
	{
		const UNiagaraSystem* Blast = LoadObject<UNiagaraSystem>(nullptr, *(FString(SystemPackage) + TEXT(".") + FPackageName::GetShortName(SystemPackage)));
		if (!Blast)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到 %s"), SystemPackage);
			return false;
		}
		UPackage* Package = CreatePackage(FlashPackage);
		Package->FullyLoad();
		const FName AssetName(*FPackageName::GetShortName(FlashPackage));
		UNiagaraSystem* System = Cast<UNiagaraSystem>(StaticDuplicateObject(Blast, Package, AssetName, RF_Public | RF_Standalone | RF_Transactional));
		if (!System)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("复制不了 %s"), SystemPackage);
			return false;
		}
		TArray<FNiagaraEmitterHandle> Handles = System->GetEmitterHandles();
		for (const FNiagaraEmitterHandle& Handle : Handles)
		{
			const FName Name = Handle.GetName();
			if (Name == TEXT("SmokeNear") || Name == TEXT("SmokeFar") || Name == TEXT("Haze"))
			{
				System->RemoveEmitterHandle(Handle);
				UE_LOG(LogGinkgoWeaponFx, Display, TEXT("去掉发射器 %s"), *Name.ToString());
			}
		}
		System->RequestCompile(true);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ true);
		const FString Filename = FPackageName::LongPackageNameToFilename(FlashPackage, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, System, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%s：%d 个发射器"), FlashPackage, System->GetEmitterHandles().Num());
		return true;
	}

	bool BuildBurstSystem(const UNiagaraSystem& Template, const TCHAR* PackageName, TFunctionRef<void(UNiagaraSystem&)> Configure)
	{
		UPackage* Package = CreatePackage(PackageName);
		Package->FullyLoad();
		const FName AssetName(*FPackageName::GetShortName(PackageName));
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName.ToString()))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
		}
		UNiagaraSystem* System = Cast<UNiagaraSystem>(StaticDuplicateObject(&Template, Package, AssetName, RF_Public | RF_Standalone | RF_Transactional));
		if (!System || System->GetEmitterHandles().IsEmpty())
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("复制不了模板"));
			return false;
		}
		// 同编辑器里从模板新建：复制出来的不再是模板
		System->TemplateAssetDescription = FText();
		System->Category = FText();

		Configure(*System);
		System->RequestCompile(true);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ true);
		FAssetRegistryModule::AssetCreated(System);
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, System, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		// 不用 IsReadyToRun 判断：命令行里不渲染（FApp::CanEverRender），它总是假
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%s：%d 个发射器"), PackageName, System->GetEmitterHandles().Num());
		return true;
	}

	/** 与尘团同时迸出的少量硬边碎屑。沿接触面法线散开，随后受世界空间的重力向下落。 */
	void BuildImpactFragments(UObject& Emitter)
	{
		DisableCommon(Emitter);
		SetBurst(Emitter, 3, 6, 200.f);
		SetupBarrelVelocity(Emitter, 58.f, 90.f, 230.f);
		SetRange(Toggle(Emitter, TEXT("Drag"), true), TEXT("DragDistribution"), 0.5f, 1.5f);
		Toggle(Emitter, TEXT("CurlNoiseForce"), false);
		UObject& Gravity = Toggle(Emitter, TEXT("AccelerationForce"), true);
		SetText(Gravity, TEXT("CoordinateSpace"), TEXT("World"));
		SetRange3(Gravity, TEXT("AccelerationDistribution"), FVector3f(0, 0, -800), FVector3f(0, 0, -800));
		UObject& Init = Toggle(Emitter, TEXT("InitializeParticle"), true);
		SetRange(Init, TEXT("LifetimeDistribution"), .18f, .40f);
		SetRange2(Init, TEXT("SpriteSizeDistribution"), FVector2f(2.f, 3.5f), FVector2f(5.f, 8.f));
		SetRange(Init, TEXT("SpriteRotationDistribution"), 0.f, 360.f);
		auto& Color = Field<FNiagaraDistributionColor>(Init, TEXT("ColorDistribution"));
		Color.InitConstant(FLinearColor(.35f, .30f, .24f, 1.f));
		Color.UpdateValuesFromDistribution();
		SetRange(Toggle(Emitter, TEXT("SpriteRotationRate"), true), TEXT("RotationRateDistribution"), -300.f, 300.f);
		SetSizeOverLife(Toggle(Emitter, TEXT("ScaleSpriteSize"), true), {{0.f, 1.f}, {.75f, 1.f}, {1.f, 0.f}});
		Toggle(Emitter, TEXT("SubUVAnimation"), false);
		Toggle(Emitter, TEXT("ScaleColor"), false);
		SetupSprite(Emitter, TEXT("/Game/Weapons/FX/M_BulletImpactFragment.M_BulletImpactFragment"),
			ENiagaraSpriteAlignment::Unaligned, ENiagaraSpriteFacingMode::FaceCamera, FVector2D(.5, .5), ENiagaraSortMode::None, 1);
	}

	/** 两类弹着共用烟团图集。系统局部 X 朝表面外侧，粒子喷出后独立于接收面的后续移动。 */
	void ConfigureImpact(UNiagaraSystem& System, const bool bBlood)
	{
		FNiagaraEmitterHandle& Handle = System.GetEmitterHandle(0);
		Handle.SetName(bBlood ? TEXT("BloodMist") : TEXT("Dust"), System);
		UObject& Emitter = *EmitterOf(Handle);
		const FSmokePuff Puff = bBlood ? FSmokePuff{3, 80.f, 180.f, 15.f, 30.f, .12f, .23f, .95f}
			: FSmokePuff{4, 65.f, 190.f, 19.f, 37.f, .20f, .38f, .85f};
		BuildSmoke(Emitter, Puff);
		SetupBarrelVelocity(Emitter, 38.f, Puff.MinSpeed, Puff.MaxSpeed);
		FNiagaraDistributionColor& Color = Field<FNiagaraDistributionColor>(Module(Emitter, TEXT("InitializeParticle")), TEXT("ColorDistribution"));
		Color.InitConstant(bBlood ? FLinearColor(.30f, .009f, .011f, Puff.Alpha) : FLinearColor(.52f, .42f, .33f, Puff.Alpha));
		Color.UpdateValuesFromDistribution();
		Emitter.PostEditChange();
		if (!bBlood)
		{
			const FNiagaraEmitterHandle Source = Handle;
			System.DuplicateEmitterHandle(Source, TEXT("Fragments"));
			UObject& Fragments = *EmitterOf(System.GetEmitterHandles().Last());
			BuildImpactFragments(Fragments);
			Fragments.PostEditChange();
		}
	}

	void ConfigureMuzzle(UNiagaraSystem& System)
	{
		// 一个发射器复制成六个，各配各的
		System.GetEmitterHandle(0).SetName(TEXT("Flash"), System);
		const FNiagaraEmitterHandle Base = System.GetEmitterHandle(0);
		for (const TCHAR* Name : {TEXT("Flame"), TEXT("SmokeNear"), TEXT("SmokeFar"), TEXT("Haze"), TEXT("Light")})
		{
			System.DuplicateEmitterHandle(Base, Name);
		}
		TArray<FNiagaraEmitterHandle>& Handles = System.GetEmitterHandles();
		for (FNiagaraEmitterHandle& Handle : Handles)
		{
			UObject& Emitter = *EmitterOf(Handle);
			const FName Name = Handle.GetName();
			if (Name == TEXT("Flash"))
			{
				BuildFlash(Emitter);
			}
			else if (Name == TEXT("Flame"))
			{
				BuildFlame(Emitter);
			}
			else if (Name == TEXT("SmokeNear"))
			{
				BuildSmoke(Emitter, {4, 40.f, 300.f, 80.f, 125.f, 0.8f, 1.1f, 0.78f});
			}
			else if (Name == TEXT("SmokeFar"))
			{
				BuildSmoke(Emitter, {4, 420.f, 950.f, 115.f, 170.f, 1.0f, 1.4f, 0.7f});
			}
			else if (Name == TEXT("Haze"))
			{
				BuildHaze(Emitter);
			}
			else
			{
				BuildLight(Emitter);
			}
			Emitter.PostEditChange();
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("发射器 %s"), *Name.ToString());
		}
	}

}

UGinkgoWeaponFxCommandlet::UGinkgoWeaponFxCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGinkgoWeaponFxCommandlet::Main(const FString& Params)
{
	using namespace GinkgoWeaponFx;
	// -AK47Rounds：只给这把枪的子弹加碰撞，别的枪的文件不动
	if (FParse::Param(*Params, TEXT("AK47Rounds")))
	{
		return SetupRoundCollision(TEXT("/Game/Weapons/AK47/Rounds/SM_AK47_Round.SM_AK47_Round"), 0.57f, 5.6f)
			&& SetupRoundCollision(TEXT("/Game/Weapons/AK47/Rounds/SM_AK47_Case.SM_AK47_Case"), 0.57f, 3.87f) ? 0 : 1;
	}
	// -ShotgunRounds：只给霰弹枪的壳加碰撞。12 号 2 3/4 英寸：壳身直径 20.3 mm，压口后 61 mm，打过的壳口张开 70 mm（Private/anim/PumpShotgun/README.txt）
	if (FParse::Param(*Params, TEXT("ShotgunRounds")))
	{
		return SetupRoundCollision(TEXT("/Game/Weapons/Shotgun/Rounds/SM_Shotgun_Shell.SM_Shotgun_Shell"), 1.02f, 6.1f)
			&& SetupRoundCollision(TEXT("/Game/Weapons/Shotgun/Rounds/SM_Shotgun_Hull.SM_Shotgun_Hull"), 1.02f, 7.f) ? 0 : 1;
	}
	// -MG3Rounds：只给 MG3 的壳加碰撞。7.62 × 51 NATO（C.I.P.，Private/anim/MG3/README.txt）：壳底直径 11.96 mm，壳长 51.18 mm
	if (FParse::Param(*Params, TEXT("MG3Rounds")))
	{
		return SetupRoundCollision(TEXT("/Game/Weapons/MG3/Rounds/SM_MG3_Case.SM_MG3_Case"), 0.6f, 5.12f) ? 0 : 1;
	}
	// -M79Rounds：只给 M79 的 40 × 46mm 整弹与空铝壳加碰撞（壳身直径 39.8 mm、导带 40.4 mm，整弹长 99 mm、空壳长 46 mm）
	if (FParse::Param(*Params, TEXT("M79Rounds")))
	{
		return SetupRoundCollision(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round.SM_M79_Round"), 2.02f, 9.9f)
			&& SetupRoundCollision(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round_Flash.SM_M79_Round_Flash"), 2.02f, 9.7f)
			&& SetupRoundCollision(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round_Flechette.SM_M79_Round_Flechette"), 2.02f, 6.8f)
			&& SetupRoundCollision(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Case.SM_M79_Case"), 2.02f, 4.6f) ? 0 : 1;
	}
	// -M79Explosion：将导入自 Fire_EXP_Vol01_Free 的 NS_Sub_EXP_Mid_002_02 改为单次爆发（原展厅资产为 Infinite 循环），重编 UE 5.8 脚本并保存闭包资产
	if (FParse::Param(*Params, TEXT("M79Explosion")))
	{
		const TCHAR* const ExplosionPkg = TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Mid_002_02");
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Fire_EXP_Vol01_Free/Niagara/EXP/NS_Sub_EXP_Mid_002_02.NS_Sub_EXP_Mid_002_02"));
		if (!System)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到 %s"), ExplosionPkg);
			return 1;
		}
		if (UNiagaraScript* SysUpdate = System->GetSystemUpdateScript())
		{
			if (UNiagaraScriptSource* SysSrc = Cast<UNiagaraScriptSource>(SysUpdate->GetLatestSource()))
			{
				SetStaticSwitch(FindModuleNode(*SysSrc->NodeGraph, TEXT("SystemState")), TEXT("Loop Behavior"), TEXT("Once"));
				SysSrc->NodeGraph->NotifyGraphChanged();
			}
		}
		for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
			UNiagaraGraph* Graph = Data && Data->GraphSource ? CastChecked<UNiagaraScriptSource>(Data->GraphSource)->NodeGraph : nullptr;
			if (!Graph)
			{
				continue;
			}
			UNiagaraNodeFunctionCall& EmitterState = FindModuleNode(*Graph, TEXT("EmitterState"));
			SetStaticSwitch(EmitterState, TEXT("Life Cycle Mode"), TEXT("Self"));
			SetStaticSwitch(EmitterState, TEXT("Loop Behavior"), TEXT("Once"));
			Graph->NotifyGraphChanged();
		}
		System->RequestCompile(true);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ true);

		const TCHAR* const ClosurePackages[] = {
			TEXT("/Game/Fire_EXP_Vol01_Free/Texture/T_DefaultWhite"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Texture/Debris/T_Sub_Debris_Glass_001"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Texture/EXP/T_Sub_EXP_Large_001_01"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Texture/EXP/T_Sub_EXP_Mid_002_02"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Texture/Spark/T_Glow_001"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Texture/Spark/T_Spark_Round_001"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Material/MF/MF_UV"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Material/M_Additive_Master"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Material/M_Masked_Master"),
			TEXT("/Game/Fire_EXP_Vol01_Free/Material/M_Sub_EXPMaster"),
			TEXT("/Game/Fire_EXP_Vol01_Free/MI/Debris/MI_Sub_Debris_Glass_001"),
			TEXT("/Game/Fire_EXP_Vol01_Free/MI/EXP/MI_Sub_EXP_Large_001_01"),
			TEXT("/Game/Fire_EXP_Vol01_Free/MI/EXP/MI_Sub_EXP_Mid_002_02"),
			TEXT("/Game/Fire_EXP_Vol01_Free/MI/Spark/MI_Glow"),
			TEXT("/Game/Fire_EXP_Vol01_Free/MI/Spark/MI_Spark_Round"),
			ExplosionPkg,
		};
		for (const TCHAR* PkgName : ClosurePackages)
		{
			const FString ObjPath = FString::Printf(TEXT("%s.%s"), PkgName, *FPackageName::GetShortName(PkgName));
			UObject* Asset = LoadObject<UObject>(nullptr, *ObjPath);
			UPackage* Pkg = Asset ? Asset->GetOutermost() : nullptr;
			if (!Asset || !Pkg)
			{
				UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到闭包资产 %s"), *ObjPath);
				return 1;
			}
			Pkg->FullyLoad();
			ResetLoaders(Pkg);
			const FString Filename = FPackageName::LongPackageNameToFilename(PkgName, FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(Pkg, Asset, *Filename, SaveArgs))
			{
				UE_LOG(LogGinkgoWeaponFx, Error, TEXT("存不了 %s"), *Filename);
				return 1;
			}
		}
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%s：已设为单次爆发并完成 UE 5.8 编译落盘（%d 个发射器，looping=%d）"),
			ExplosionPkg, System->GetEmitterHandles().Num(), System->IsLooping() ? 1 : 0);
		return 0;
	}
	if (FParse::Param(*Params, TEXT("Rounds")))
	{
		const bool bRound = SetupRoundCollision(TEXT("/Game/Weapons/Mauser98k/Rounds/SM_Mauser98k_Round.SM_Mauser98k_Round"), 0.57f, 8.2f);
		const bool bCase = SetupRoundCollision(TEXT("/Game/Weapons/Mauser98k/Rounds/SM_Mauser98k_Case.SM_Mauser98k_Case"), 0.57f, 5.7f);
		// .45 ACP（Private/anim/M1911/tools/m1911_rounds.py）：有了才加，98k 那边不受影响
		bool bPistol = true;
		if (FPackageName::DoesPackageExist(TEXT("/Game/Weapons/M1911/Rounds/SM_M1911_Round")))
		{
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/M1911/Rounds/SM_M1911_Round.SM_M1911_Round"), 0.6f, 3.24f);
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/M1911/Rounds/SM_M1911_Case.SM_M1911_Case"), 0.6f, 2.28f);
		}
		// 7.62 × 39（Private/anim/AK47/tools/ak_rounds.py）：C.I.P. 全长 56.0 mm、壳长 38.7 mm，底径 11.35 mm
		if (FPackageName::DoesPackageExist(TEXT("/Game/Weapons/AK47/Rounds/SM_AK47_Round")))
		{
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/AK47/Rounds/SM_AK47_Round.SM_AK47_Round"), 0.57f, 5.6f);
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/AK47/Rounds/SM_AK47_Case.SM_AK47_Case"), 0.57f, 3.87f);
		}
		if (FPackageName::DoesPackageExist(TEXT("/Game/Weapons/Shotgun/Rounds/SM_Shotgun_Shell")))
		{
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/Shotgun/Rounds/SM_Shotgun_Shell.SM_Shotgun_Shell"), 1.02f, 6.1f);
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/Shotgun/Rounds/SM_Shotgun_Hull.SM_Shotgun_Hull"), 1.02f, 7.f);
		}
		if (FPackageName::DoesPackageExist(TEXT("/Game/Weapons/MG3/Rounds/SM_MG3_Case")))
		{
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/MG3/Rounds/SM_MG3_Case.SM_MG3_Case"), 0.6f, 5.12f);
		}
		if (FPackageName::DoesPackageExist(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round")))
		{
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round.SM_M79_Round"), 2.02f, 9.9f);
			bPistol &= SetupRoundCollision(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Case.SM_M79_Case"), 2.02f, 4.6f);
		}
		return bRound && bCase && bPistol ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("FlashOnly")))
	{
		return BuildFlashOnly() ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("SmokeOwnerScale")))
	{
		return EnableSmokeOwnerScale() ? 0 : 1;
	}
	// -Heat：只重建热浪（已有的也重建）
	if (FParse::Param(*Params, TEXT("Heat")))
	{
		return BuildHeat() ? 0 : 1;
	}
	// -Trail：只重建弹道的烟（已有的也重建；材质 M_BulletTrail 由 bullet_trail_ue.py 先做好）
	if (FParse::Param(*Params, TEXT("Trail")))
	{
		return BuildTrail() ? 0 : 1;
	}
	FString EmitterPath;
	if (FParse::Value(*Params, TEXT("DumpEmitter="), EmitterPath))
	{
		const UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterPath);
		if (!Emitter)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到发射器 %s"), *EmitterPath);
			return 1;
		}
		DumpEmitter(*Emitter);
		return 0;
	}
	FString DumpSystemPath;
	if (FParse::Value(*Params, TEXT("DumpSystem="), DumpSystemPath))
	{
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *DumpSystemPath);
		if (!System)
		{
			UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到系统 %s"), *DumpSystemPath);
			return 1;
		}
		UE_LOG(LogGinkgoWeaponFx, Display, TEXT("系统 %s：looping=%d，发射器=%d"),
			*System->GetPathName(), System->IsLooping() ? 1 : 0, System->GetEmitterHandles().Num());
		if (UNiagaraScript* SysUpdate = System->GetSystemUpdateScript())
		{
			if (const UNiagaraScriptSource* SysSrc = Cast<UNiagaraScriptSource>(SysUpdate->GetLatestSource()))
			{
				TArray<UNiagaraNodeFunctionCall*> SysCalls;
				SysSrc->NodeGraph->GetNodesOfClass<UNiagaraNodeFunctionCall>(SysCalls);
				for (const UNiagaraNodeFunctionCall* Call : SysCalls)
				{
					UE_LOG(LogGinkgoWeaponFx, Display, TEXT("系统模块 %s（%s）"), *Call->GetFunctionName(), *GetPathNameSafe(Call->FunctionScript));
					for (const UEdGraphPin* Pin : Call->Pins)
					{
						if (Pin->Direction == EGPD_Input)
						{
							UE_LOG(LogGinkgoWeaponFx, Display, TEXT("  系统针脚 %s = %s%s"), *Pin->PinName.ToString(), *Pin->DefaultValue, *DescribeSwitchValues(*Pin));
						}
					}
				}
			}
		}
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("=== Handle %s (enabled=%d) ==="), *Handle.GetName().ToString(), Handle.GetIsEnabled() ? 1 : 0);
			if (const UNiagaraEmitter* InstEmitter = Handle.GetInstance().Emitter)
			{
				DumpEmitter(*InstEmitter);
			}
		}
		return 0;
	}
	const UNiagaraSystem* Template = LoadObject<UNiagaraSystem>(nullptr, TemplatePath);
	if (!Template)
	{
		UE_LOG(LogGinkgoWeaponFx, Error, TEXT("读不到模板 %s"), TemplatePath);
		return 1;
	}
	if (FParse::Param(*Params, TEXT("Impacts")))
	{
		return BuildBurstSystem(*Template, TEXT("/Game/Weapons/FX/NS_BulletImpactDust"), [](UNiagaraSystem& S) { ConfigureImpact(S, false); })
			&& BuildBurstSystem(*Template, TEXT("/Game/Weapons/FX/NS_BulletImpactBlood"), [](UNiagaraSystem& S) { ConfigureImpact(S, true); }) ? 0 : 1;
	}
	if (FParse::Param(*Params, TEXT("Dump")))
	{
		const UNiagaraSystem* Existing = LoadObject<UNiagaraSystem>(nullptr, *(FString(SystemPackage) + TEXT(".") + FPackageName::GetShortName(SystemPackage)));
		const UNiagaraSystem& Target = Existing ? *Existing : *Template;
		for (const FNiagaraEmitterHandle& Handle : Target.GetEmitterHandles())
		{
			UObject* Emitter = EmitterOf(Handle);
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("发射器 %s：%s"), *Handle.GetName().ToString(), *GetNameSafe(Emitter ? Emitter->GetClass() : nullptr));
			if (!Emitter)
			{
				continue;
			}
			DumpObject(*Emitter, 1);
			for (const FName Name : {FName(TEXT("Modules")), FName(TEXT("RendererProperties"))})
			{
				const FArrayProperty* Array = FindFProperty<FArrayProperty>(Emitter->GetClass(), Name);
				const FObjectPropertyBase* Inner = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
				if (!Inner)
				{
					continue;
				}
				FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Emitter));
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					const UObject* Element = Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index));
					UE_LOG(LogGinkgoWeaponFx, Display, TEXT("  %s[%d] %s"), *Name.ToString(), Index, *GetNameSafe(Element ? Element->GetClass() : nullptr));
					if (Element)
					{
						DumpObject(*Element, 2);
					}
				}
			}
		}
		return 0;
	}
	const bool bForce = FParse::Param(*Params, TEXT("Force"));
	bool bOk = true;
	for (const TCHAR* Package : {SystemPackage, WispPackage, HeatPackage, TrailPackage})
	{
		if (!bForce && FPackageName::DoesPackageExist(Package))
		{
			UE_LOG(LogGinkgoWeaponFx, Display, TEXT("%s 已有，不动（-Force 才重建）"), Package);
			continue;
		}
		bOk &= Package == SystemPackage ? BuildBurstSystem(*Template, SystemPackage, ConfigureMuzzle) : Package == WispPackage ? BuildWisp() : Package == HeatPackage ? BuildHeat() : BuildTrail();
	}
	return bOk ? 0 : 1;
}

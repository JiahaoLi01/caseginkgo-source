#include "GinkgoScopeView.h"

#include "Camera/CameraTypes.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LegacyScreenPercentageDriver.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SceneViewExtension.h"
#include "TemporalUpscaler.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Debug/DebugStatGroup.h"

DECLARE_CYCLE_STAT(TEXT("Scope view"), STAT_GinkgoScopeView, STATGROUP_Ginkgo3C);

namespace
{
	/** 合成材质的参数名：同 Scripts/Assets/scope_material.py 建的一致 */
	namespace Param
	{
		const FName Capture(TEXT("ScopeCapture"));
		/** 目镜中心（相对相机，cm，世界的轴）与瞄具的三个轴 */
		const FName Eyepiece(TEXT("ScopeEyepiece"));
		const FName AxisX(TEXT("ScopeAxisX"));
		const FName AxisY(TEXT("ScopeAxisY"));
		const FName AxisZ(TEXT("ScopeAxisZ"));
		/** 主画面的三个轴、竖直半视场的正切、宽高比：由屏幕的位置还原视线 */
		const FName CameraX(TEXT("CameraAxisX"));
		const FName CameraY(TEXT("CameraAxisY"));
		const FName CameraZ(TEXT("CameraAxisZ"));
		const FName ViewTanHalfY(TEXT("ViewTanHalfY"));
		const FName ViewAspect(TEXT("ViewAspect"));
		/** 光学 */
		const FName ApparentTan(TEXT("ScopeApparentTan"));
		const FName Magnification(TEXT("ScopeMagnification"));
		const FName CaptureTan(TEXT("ScopeCaptureTan"));
		const FName EyepieceRadius(TEXT("ScopeEyepieceRadius"));
		const FName EyeRelief(TEXT("ScopeEyeRelief"));
		const FName ExitPupilRadius(TEXT("ScopeExitPupilRadius"));
		const FName EyeBoxSoftness(TEXT("ScopeEyeBoxSoftness"));
		const FName DistortionStart(TEXT("ScopeDistortionStart"));
		const FName DistortionStrength(TEXT("ScopeDistortionStrength"));
		const FName ChromaticAberration(TEXT("ScopeChromaticAberration"));
		const FName LateralColor(TEXT("ScopeLateralColor"));
		const FName EyeColor(TEXT("ScopeEyeColor"));
		const FName PupilColor(TEXT("ScopePupilColor"));
		const FName EdgeDarkening(TEXT("ScopeEdgeDarkening"));
		const FName FieldStopSoftness(TEXT("ScopeFieldStopSoftness"));
		const FName Transmission(TEXT("ScopeTransmission"));
		const FName DustOpacity(TEXT("ScopeDustOpacity"));
		const FName ObjectiveScatter(TEXT("ScopeObjectiveScatter"));
		/** 分划（mrad）与颜色 */
		const FName ReticleGap(TEXT("ReticleGap"));
		const FName ReticleWidth(TEXT("ReticleWidth"));
		const FName ReticleTip(TEXT("ReticleTip"));
		const FName ReticleColor(TEXT("ReticleColor"));
		/** 镜外 */
		const FName FirstPerson(TEXT("FirstPerson"));
		const FName SurroundBlur(TEXT("SurroundBlur"));
		const FName SurroundVignette(TEXT("SurroundVignette"));
		const FName SurroundNearDistance(TEXT("SurroundNearDistance"));
		const FName SurroundNearBlur(TEXT("SurroundNearBlur"));
	}

	FLinearColor ToParam(const FVector& Value)
	{
		return FLinearColor(static_cast<float>(Value.X), static_cast<float>(Value.Y), static_cast<float>(Value.Z), 0.f);
	}

	/** 主视图继续用玩家的画质比例选择超分档位；创建本帧渲染器时才缩小输入，保留原分配上限以免进出镜时反复分配。 */
	class FScopeScreenPercentage final : public FLegacyScreenPercentageDriver
	{
	public:
		FScopeScreenPercentage(const FSceneViewFamily& Family, float InBaseFraction, float InScale)
			: FLegacyScreenPercentageDriver(Family, InBaseFraction), BaseFraction(InBaseFraction), Scale(InScale)
		{
		}

		virtual ISceneViewFamilyScreenPercentage* Fork_GameThread(const FSceneViewFamily& Family) const override
		{
			float Fraction = BaseFraction;
			if (Family.bIsMainViewFamily && Family.Views.Num() == 1 && Family.EngineShowFlags.ScreenPercentage)
			{
				const UE::Renderer::Private::ITemporalUpscaler* Upscaler = Family.GetTemporalUpscalerInterface();
				const float Minimum = Upscaler ? Upscaler->GetMinUpsampleResolutionFraction()
					: (Family.Views[0]->AntiAliasingMethod == AAM_TSR ? kMinTSRResolutionFraction : kMinTAAUpsampleResolutionFraction);
				Fraction = FMath::Min(BaseFraction, FMath::Max(Minimum, BaseFraction * Scale));
			}
			return new FLegacyScreenPercentageDriver(Family, Fraction, BaseFraction);
		}

	private:
		/** 当前视口的静态渲染比例，1 表示输出分辨率。 */
		float BaseFraction;

		/** 本帧镜外模糊允许的相对线性比例，1 表示恢复玩家的画质设置。 */
		float Scale;
	};
}

/** 在本地玩家创建主视图时安装静态比例驱动。场景捕获、编辑器视口以及已有的动态分辨率驱动各自处理分辨率。 */
class FGinkgoScopeResolutionExtension final : public FWorldSceneViewExtension
{
public:
	FGinkgoScopeResolutionExtension(const FAutoRegister& AutoRegister, AActor& Owner)
		: FWorldSceneViewExtension(AutoRegister, Owner.GetWorld()), OwnerId(Owner.GetUniqueID())
	{
	}

	virtual void SetupView(FSceneViewFamily& Family, FSceneView& View) override
	{
		// 合成在时序放大之后、空间放大之前。只有前者能在降低主场景分辨率时保住分划和镜内纹理的细节。
		if (Scale >= 1.f || View.ViewActor.ActorUniqueId != OwnerId || View.bIsSceneCapture || View.StereoPass != EStereoscopicPass::eSSP_FULL
			|| View.PrimaryScreenPercentageMethod != EPrimaryScreenPercentageMethod::TemporalUpscale
			|| !Family.EngineShowFlags.ScreenPercentage || !Family.EngineShowFlags.PostProcessing || !Family.EngineShowFlags.PostProcessMaterial
			|| !Family.EngineShowFlags.TemporalAA || Family.ViewMode != VMI_Lit || Family.GetScreenPercentageInterface())
		{
			return;
		}
		UWorld* ScopeWorld = GetWorld();
		UGameViewportClient* Viewport = ScopeWorld ? ScopeWorld->GetGameViewport() : nullptr;
		if (!Viewport || Family.RenderTarget != Viewport->Viewport || !ScopeWorld->GetGameInstance() || ScopeWorld->GetGameInstance()->GetNumLocalPlayers() != 1)
		{
			return;
		}
		// 同 UGameViewportClient::Draw 使用当前 DPI、视口面积和用户设置，兼容 PIE 的自动屏幕比例。
		FStaticResolutionFractionHeuristic Heuristic;
		Heuristic.Settings.PullRunTimeRenderingSettings(Viewport->GetViewStatusForScreenPercentage());
		// SetupView 返回后视口才把 UnscaledViewRect 写入 CameraConstrainedViewRect；此时直接取同一个输出矩形。
		Heuristic.TotalDisplayedPixelCount = View.UnscaledViewRect.Area();
		Heuristic.SecondaryViewFraction = Family.SecondaryViewFraction;
		Heuristic.DPIScale = Viewport->GetDPIScale();
		Family.SetScreenPercentageInterface(new FScopeScreenPercentage(Family, Heuristic.ResolveResolutionFraction(), Scale));
	}

	/** 由所属瞄具在游戏线程更新，SetupView 将本帧的值复制进视图族。 */
	float Scale = 1.f;

private:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override
	{
		return Scale < 1.f && FWorldSceneViewExtension::IsActiveThisFrame_Internal(Context);
	}

	/** 只接受这台相机所属角色的视图。世界与角色均由引擎持有。 */
	uint32 OwnerId;
};

int32 UGinkgoScopeView::ComputeCaptureSize(const FGinkgoScope& Scope, const double ScreenHeight, const double TanHalfFovY)
{
	const double Diameter = ScreenHeight * Ginkgo::Scope::GetApparentTanHalfField(Scope.Optics) / FMath::Max(TanHalfFovY, 1e-3);
	const double Wanted = Diameter * Scope.CaptureMargin * Scope.CaptureSupersample;
	const int32 Rounded = FMath::DivideAndRoundUp(FMath::Max(FMath::CeilToInt32(Wanted), 1), 32) * 32;
	return FMath::Clamp(Rounded, Scope.CaptureMinSize, FMath::Max(Scope.CaptureMinSize, Scope.CaptureMaxSize));
}

void UGinkgoScopeView::Update(AActor& Owner, const AActor* Pawn, const Ginkgo::Scope::FSightFrame& Sight, const FGinkgoScope& Scope, const float FirstPerson,
	const float EyeAway, const FVector2D& ViewportSize, FMinimalViewInfo& InOutView)
{
	SCOPE_CYCLE_COUNTER(STAT_GinkgoScopeView);
	if (!bLoggedOptics)
	{
		// 每次进第一人称写一行：测量（Scripts/Measurements/scope.py）按这一行的数透过瞄具投影，不在脚本里另写一份。没有画面的进程里也写。
		// 色差接在最后（脚本的式子从头匹配，后面多了不碍事）
		const FGinkgoScopeOptics& Optics = Scope.Optics;
		const FGinkgoScopeHandling& Handling = Scope.Handling;
		UE_LOG(LogGinkgoCamera, Log, TEXT("[Camera] 第一人称开镜：倍率 %.4f，真实视场 %.4f°，眼距 %.4f cm，物镜到目镜 %.4f cm，畸变 %.4f/%.4f/%.4f，出瞳 %.4f/%.4f cm，")
			TEXT("眼睛跟枪 %.4f/%.4f（上下左右/前后），眼睛在出瞳后面 %.4f cm，转镜头的落后 %.4f，第一人称的枪踢 %.4f，色差 %.4f/%.4f/%.4f（倍率/眼位/出瞳 cm），镜片污点 %.4f，物镜散射 %.4f"),
			Optics.Magnification, Optics.TrueFieldOfView, Optics.EyeRelief, Scope.Mount.ObjectiveDistance, Optics.DistortionStart, Optics.DistortionStrength,
			Optics.ChromaticAberration, Optics.ExitPupil, Optics.EyeBoxSoftness, Handling.EyeFollow, Handling.EyeFollowAxial, Handling.EyeBehind, Handling.AimLag,
			Handling.KickScale, Optics.LateralColor, Optics.EyeColor, Optics.PupilColor, Optics.DustOpacity, Optics.ObjectiveScatter);
		bLoggedOptics = true;
	}
	UMaterialInstanceDynamic* Material = EnsureComposite(Scope);
	if (!Material || ViewportSize.X < 2.0 || ViewportSize.Y < 2.0)
	{
		// 不拍（没有画面、没有材质）：下次拍时当作镜头切换。光学的数这一次已经写过，不重写
		bActive = false;
		if (ResolutionExtension)
		{
			ResolutionExtension->Scale = 1.f;
		}
		return;
	}
	// 主画面的竖直半视场：相机输出的是 4:3 画面的水平视场（FMinimalViewInfo 默认的宽高比），引擎保持竖直视场换到实际的画面
	const double TanHalfFovY = FMath::Tan(FMath::DegreesToRadians(0.5 * static_cast<double>(InOutView.FOV))) / FMath::Max(static_cast<double>(InOutView.AspectRatio), 1e-3);
	EnsureCapture(Owner, ComputeCaptureSize(Scope, ViewportSize.Y, TanHalfFovY));

	// 1. 拍：物镜上、朝光轴（Y、Z 同枪：镜内的上方就是瞄具的上方），视场是真实视场加余量。排到这一帧末尾，同主画面一样
	const double CaptureTan = FMath::Tan(FMath::DegreesToRadians(0.5 * static_cast<double>(Scope.Optics.TrueFieldOfView))) * Scope.CaptureMargin;
	Capture->SetWorldLocationAndRotation(Sight.Objective, Sight.Rotation);
	Capture->FOVAngle = static_cast<float>(FMath::RadiansToDegrees(2.0 * FMath::Atan(CaptureTan)));
	if (HiddenPawn.Get() != Pawn)
	{
		Capture->HiddenActors.Reset();
		if (Pawn)
		{
			Capture->HiddenActors.Add(const_cast<AActor*>(Pawn));
		}
		HiddenPawn = Pawn;
	}
	if (!bActive)
	{
		Capture->bCameraCutThisFrame = true;
	}
	Capture->CaptureSceneDeferred();
	bActive = true;

	// 2. 合成：位置都相对主画面的相机（单精度够用），方向是世界的轴
	const FRotationMatrix CameraAxes(InOutView.Rotation);
	const FMatrix ScopeAxes = FQuatRotationMatrix(Sight.Rotation);
	Material->SetTextureParameterValue(Param::Capture, Target);
	Material->SetVectorParameterValue(Param::Eyepiece, ToParam(Sight.Eyepiece - InOutView.Location));
	Material->SetVectorParameterValue(Param::AxisX, ToParam(ScopeAxes.GetUnitAxis(EAxis::X)));
	Material->SetVectorParameterValue(Param::AxisY, ToParam(ScopeAxes.GetUnitAxis(EAxis::Y)));
	Material->SetVectorParameterValue(Param::AxisZ, ToParam(ScopeAxes.GetUnitAxis(EAxis::Z)));
	Material->SetVectorParameterValue(Param::CameraX, ToParam(CameraAxes.GetUnitAxis(EAxis::X)));
	Material->SetVectorParameterValue(Param::CameraY, ToParam(CameraAxes.GetUnitAxis(EAxis::Y)));
	Material->SetVectorParameterValue(Param::CameraZ, ToParam(CameraAxes.GetUnitAxis(EAxis::Z)));
	Material->SetScalarParameterValue(Param::ViewTanHalfY, static_cast<float>(TanHalfFovY));
	Material->SetScalarParameterValue(Param::ViewAspect, static_cast<float>(ViewportSize.X / ViewportSize.Y));

	const FGinkgoScopeOptics& Optics = Scope.Optics;
	Material->SetScalarParameterValue(Param::ApparentTan, static_cast<float>(Ginkgo::Scope::GetApparentTanHalfField(Optics)));
	Material->SetScalarParameterValue(Param::Magnification, FMath::Max(Optics.Magnification, 1.f));
	Material->SetScalarParameterValue(Param::CaptureTan, static_cast<float>(CaptureTan));
	Material->SetScalarParameterValue(Param::EyepieceRadius, Scope.Mount.EyepieceRadius);
	Material->SetScalarParameterValue(Param::EyeRelief, Optics.EyeRelief);
	Material->SetScalarParameterValue(Param::ExitPupilRadius, 0.5f * Optics.ExitPupil);
	Material->SetScalarParameterValue(Param::EyeBoxSoftness, Optics.EyeBoxSoftness);
	Material->SetScalarParameterValue(Param::DistortionStart, FMath::Clamp(Optics.DistortionStart, 0.f, 0.99f));
	Material->SetScalarParameterValue(Param::DistortionStrength, Optics.DistortionStrength);
	Material->SetScalarParameterValue(Param::ChromaticAberration, Optics.ChromaticAberration);
	Material->SetScalarParameterValue(Param::LateralColor, Optics.LateralColor);
	Material->SetScalarParameterValue(Param::EyeColor, Optics.EyeColor);
	Material->SetScalarParameterValue(Param::PupilColor, Optics.PupilColor);
	Material->SetScalarParameterValue(Param::EdgeDarkening, Optics.EdgeDarkening);
	Material->SetScalarParameterValue(Param::FieldStopSoftness, Optics.FieldStopSoftness);
	Material->SetScalarParameterValue(Param::Transmission, Optics.Transmission);
	Material->SetScalarParameterValue(Param::DustOpacity, Optics.DustOpacity);
	Material->SetScalarParameterValue(Param::ObjectiveScatter, Optics.ObjectiveScatter);
	Material->SetScalarParameterValue(Param::ReticleGap, Scope.Reticle.Gap);
	Material->SetScalarParameterValue(Param::ReticleWidth, Scope.Reticle.PostWidth);
	Material->SetScalarParameterValue(Param::ReticleTip, Scope.Reticle.TipLength);
	Material->SetVectorParameterValue(Param::ReticleColor, Scope.Reticle.Color);
	Material->SetScalarParameterValue(Param::FirstPerson, FMath::Clamp(FirstPerson, 0.f, 1.f));
	// 拉栓时眼睛让开了：不再透过瞄具对焦在远处，镜外远处、近处的模糊都跟着收掉（暗角照旧）
	const float Focus = 1.f - FMath::Clamp(EyeAway, 0.f, 1.f);
	const float Scale = Scope.Surround.GetRenderScale(FirstPerson, Focus);
	if (!ResolutionExtension && Scale < 1.f)
	{
		ResolutionExtension = FSceneViewExtensions::NewExtension<FGinkgoScopeResolutionExtension>(Owner);
	}
	if (ResolutionExtension)
	{
		ResolutionExtension->Scale = Scale;
	}
	Material->SetScalarParameterValue(Param::SurroundBlur, Scope.Surround.BlurRadius * Focus);
	Material->SetScalarParameterValue(Param::SurroundVignette, Scope.Surround.Vignette);
	Material->SetScalarParameterValue(Param::SurroundNearDistance, Scope.Surround.NearDistance);
	Material->SetScalarParameterValue(Param::SurroundNearBlur, Scope.Surround.NearBlurRadius * Focus);

	InOutView.PostProcessSettings.AddBlendable(Material, 1.f);
	InOutView.PostProcessBlendWeight = 1.f;
}

void UGinkgoScopeView::Deactivate()
{
	if (ResolutionExtension)
	{
		ResolutionExtension->Scale = 1.f;
	}
	bActive = false;
	bLoggedOptics = false;
}

void UGinkgoScopeView::PassThrough(const FGinkgoScope& Scope, FMinimalViewInfo& InOutView)
{
	UMaterialInstanceDynamic* Material = EnsureComposite(Scope);
	if (!Material)
	{
		return;
	}
	// 第一人称的进度 0：着色器读了主画面就返回，别的参数不用设
	Material->SetScalarParameterValue(Param::FirstPerson, 0.f);
	InOutView.PostProcessSettings.AddBlendable(Material, 1.f);
	InOutView.PostProcessBlendWeight = 1.f;
}

void UGinkgoScopeView::EnsureCapture(AActor& Owner, const int32 Size)
{
	if (!Capture)
	{
		Capture = NewObject<USceneCaptureComponent2D>(&Owner, TEXT("GinkgoScopeCapture"), RF_Transient);
		// 不自己拍：每帧相机算完以后由 Update 排到这一帧的末尾（CaptureSceneDeferred），位置同主画面同一帧
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		// 留着渲染的状态：Lumen、虚拟阴影的时间性的历史要它，不然每帧都像第一帧（镜内的光照与镜外不一样）
		Capture->bAlwaysPersistRenderingState = true;
		// 等主视图完成后处理体积的混合，再沿用它本帧的光照与反射设置。
		// 单独的场景捕获默认关闭 Lumen，在室外镜内只能看到缺少遮蔽的天光。
		// 只绑定主视图族；物镜仍用自己的位置、窄视场和方形分辨率。
		Capture->bMainViewFamily = true;
		Capture->bInheritMainViewCameraPostProcessSettings = true;
		Capture->bUseRayTracingIfEnabled = true;
		// 保留引擎给场景捕获的半分辨率 Lumen 表面缓存，开镜时只需覆盖镜片里的画面。
		Capture->PostProcessSettings.bOverride_LumenSurfaceCacheResolution = true;
		// HDR 的场景色、不做后处理：不带预曝光（引擎在不做后处理的视角上用 1），就是亮度，合成时乘主画面的预曝光
		Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDRNoAlpha;
		Capture->ProjectionType = ECameraProjectionMode::Perspective;
		// 拍的是窄视场，物件在画面上大：按屏幕大小选的细节层次自然是近处的，不用再调
		Capture->ShowFlags.SetMotionBlur(false);
		Capture->RegisterComponent();
	}
	if (!Target || TargetSize != Size)
	{
		Target = NewObject<UTextureRenderTarget2D>(this, TEXT("GinkgoScopeTarget"), RF_Transient);
		// 超采样拍的：合成时按导数取 mip，缩小的那一点不闪
		Target->bAutoGenerateMips = true;
		Target->MipsSamplerFilter = TF_Trilinear;
		Target->ClearColor = FLinearColor::Black;
		Target->InitCustomFormat(static_cast<uint32>(Size), static_cast<uint32>(Size), PF_FloatRGBA, /*bInForceLinearGamma*/ true);
		Target->UpdateResourceImmediate(true);
		TargetSize = Size;
		Capture->TextureTarget = Target;
		bActive = false;
		UE_LOG(LogGinkgoCamera, Log, TEXT("[Camera] 瞄具的画面：%d × %d"), Size, Size);
	}
}

UMaterialInstanceDynamic* UGinkgoScopeView::EnsureComposite(const FGinkgoScope& Scope)
{
	UMaterialInterface* Parent = Scope.CompositeMaterial.LoadSynchronous();
	if (!Parent)
	{
		UE_CLOG(!bReportedMissingMaterial, LogGinkgoCamera, Warning, TEXT("[Camera] 读不到瞄具的合成材质 %s：第一人称照常，镜内没有画面（先跑 Scripts/Assets/scope_material.py）"),
			*Scope.CompositeMaterial.ToString());
		bReportedMissingMaterial = true;
		Composite = nullptr;
		CompositeParent = nullptr;
		return nullptr;
	}
	if (!Composite || CompositeParent != Parent)
	{
		Composite = UMaterialInstanceDynamic::Create(Parent, this);
		CompositeParent = Parent;
	}
	return Composite;
}

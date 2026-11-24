#include "Animation/GinkgoWeaponAnimLayers.h"

#include "AnimExecutionContextLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Camera/GinkgoSpring.h"
#include "Components/SkeletalMeshComponent.h"
#include "SequenceEvaluatorLibrary.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponTypes.h"

namespace
{
	/**
	 * 片段里脚的同步标记：导入时按接触曲线的上升沿生成的 L、R（GinkgoAnimationImportCommandlet），持枪循环导入时照 GASP 的同名片段加了同样的。
	 * 跳跃片段里另有 Takeoff、Apex、Impact，跟步伐时不算
	 */
	TArray<FAnimSyncMarker> GetLoopFootMarkers(const UAnimSequence* Sequence)
	{
		using namespace UE::Query;
		if (!Sequence)
		{
			return {};
		}
		static const FName Left(TEXT("L"));
		static const FName Right(TEXT("R"));
		return Sequence->AuthoredSyncMarkers
			| Where([](const FAnimSyncMarker& Marker) { return Marker.MarkerName == Left || Marker.MarkerName == Right; })
			| ToArray();
	}
}

void UGinkgoWeaponAnimLayers::NativeThreadSafeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	// 编辑器里单独预览层的蓝图时，网格上的就是它自己，没有主实例可读
	const UGinkgoAnimInstance* Main = GetMainInstance();
	if (!Main)
	{
		return;
	}
	const FGinkgoWeaponAnimData& Data = Main->GetWeaponAnimData();
	// 换层不靠惯性化：链上时从 0 淡入，手上没枪了淡到 0（组件等淡完才撤掉层），淡出时留着上一把的素材与俯仰的折扣
	// 拔枪、收枪在右臂的槽上时层跟着片段的曲线让开（主实例给的）。层还没淡入时直接取：刚链上就是拔枪的开始，不从 1 走过来。
	// 枪一落到背上手上就没枪了，层自己淡出，这里不再动。别的时候限速：半路换蒙太奇时曲线一下子变，片段自己走得比这慢
	if (LayerProgress <= 0.f)
	{
		HoldPoseAlpha = Data.HoldPoseAlpha;
	}
	else if (Data.Weapon)
	{
		HoldPoseAlpha = FMath::FInterpConstantTo(HoldPoseAlpha, Data.HoldPoseAlpha, DeltaSeconds, HoldPoseMaxRate);
	}
	LayerProgress = Ginkgo::Weapon::StepProgress(LayerProgress, Data.Weapon != nullptr, LayerBlendTime, LayerBlendTime, DeltaSeconds);
	if (Data.Weapon)
	{
		Weapon = Data.Weapon;
	}
	LoweredPose = Weapon ? Weapon->LoweredPose.Get() : nullptr;
	HipPose = Weapon ? Weapon->HipPose.Get() : nullptr;
	AimPose = Weapon ? Weapon->AimPose.Get() : nullptr;
	RunLoop = Weapon ? Weapon->RunLoop.Get() : nullptr;
	SprintLoop = Weapon ? Weapon->SprintLoop.Get() : nullptr;
	AimLoop = Weapon ? Weapon->AimLoop.Get() : nullptr;
	LoweredLoop = Weapon ? Weapon->LoweredLoop.Get() : nullptr;
	HipLoop = Weapon ? Weapon->HipLoop.Get() : nullptr;
	AimOffsetBlendSpace = Weapon ? Weapon->AimOffset.Get() : nullptr;
	RaiseAlpha = Data.RaiseAlpha;
	AdsAlpha = Data.AdsAlpha;
	const float LayerAlpha = Ginkgo::Weapon::EaseProgress(LayerProgress);
	UpperBodyAlpha = Weapon ? LayerAlpha * HoldPoseAlpha : 0.f;

	// 跑的循环按地速混进来，过弹簧：离地、着地、开始滑铲时目标一下子变（地速只算地面上的），换片段的惯性化不一定在同一帧
	// 单手跑的枪（M1911）换弹时不放：循环里枪在腿边、左手在摆，片段是从低持叠的，叠上去枪就到头顶了。回到低持，腿照样跑
	const bool bRunLoop = RunLoop && Data.bHoldLoops && (!Data.bBothHandsOnGun || !Weapon || Weapon->bRunTwoHanded);
	const double RunTarget = bRunLoop ? FMath::GetMappedRangeValueClamped(RunSpeedRange, FVector2D(0.0, 1.0), static_cast<double>(Data.GroundSpeed)) : 0.0;
	double RunOffset = RunWeight - RunTarget;
	Ginkgo::Spring::StepCritical(RunOffset, RunWeightVelocity, RunAlphaOmega, DeltaSeconds);
	RunWeight = FMath::Clamp(RunTarget + RunOffset, 0.0, 1.0);
	RunAlpha = static_cast<float>(RunWeight);
	// 单手跑的枪左手放开多少：主实例下一帧用（UGinkgoAnimInstance::ReportRunAlpha）
	if (UGinkgoAnimInstance* MainForRun = GetMainInstance())
	{
		MainForRun->ReportRunAlpha(RunAlpha);
	}
	// 冲刺的循环按冲刺进度：主实例给的已经两头缓过（枪没有冲刺的循环、循环关着时为 0），Control Rig 里左手放开枪也按它
	SprintAlpha = SprintLoop ? Data.SprintAlpha : 0.f;
	// 开镜的循环代替开镜的姿势：第一帧就是它，枪管方向不变、手相对枪不动，开镜的握法与枪口对准照旧
	AimLoopAlpha = AimLoop && Data.bHoldLoops ? 1.f : 0.f;
	LoweredLoopAlpha = LoweredLoop && Data.bHoldLoops ? 1.f : 0.f;
	HipLoopAlpha = HipLoop && Data.bHoldLoops ? 1.f : 0.f;
	LoopClock = Data.LoopClock;

	// 低持时俯仰打折，抬起来以后全用；夹在瞄准偏移的范围里
	const float PitchScale = Weapon ? FMath::Lerp(Weapon->LoweredAimPitchScale, 1.f, FMath::Max(RaiseAlpha, AdsAlpha)) : 1.f;
	AimYaw = Data.AimYaw;
	AimPitch = Data.AimPitch * PitchScale;
	if (AimOffsetBlendSpace)
	{
		const FBlendParameter& Yaw = AimOffsetBlendSpace->GetBlendParameter(0);
		const FBlendParameter& Pitch = AimOffsetBlendSpace->GetBlendParameter(1);
		AimYaw = FMath::Clamp(AimYaw, Yaw.Min, Yaw.Max);
		AimPitch = FMath::Clamp(AimPitch, Pitch.Min, Pitch.Max);
	}

	// 冲刺时枪放低、不瞄准：权重过弹簧收掉，进出冲刺不跳；再乘换层的淡入淡出
	const bool bAiming = Data.Stance != EGinkgoHoldStance::Sprint && Data.Stance != EGinkgoHoldStance::Unarmed;
	const double Target = bAiming ? 1.0 : 0.0;
	double Offset = AimOffsetWeight - Target;
	Ginkgo::Spring::StepCritical(Offset, AimOffsetWeightVelocity, AimOffsetAlphaOmega, DeltaSeconds);
	AimOffsetWeight = FMath::Clamp(Target + Offset, 0.0, 1.0);
	AimOffsetAlpha = Weapon ? static_cast<float>(AimOffsetWeight) * LayerAlpha * HoldPoseAlpha : 0.f;

#if !UE_BUILD_SHIPPING
	// 三个循环这一帧放到的时刻由它们的节点函数记下（之后才调，没放的留着负的）
	DebugRunTime = -1.f;
	DebugSprintTime = -1.f;
	DebugAimTime = -1.f;
	ReportLoops();
#endif
}

UGinkgoAnimInstance* UGinkgoWeaponAnimLayers::GetMainInstance() const
{
	const USkeletalMeshComponent* Mesh = GetOwningComponent();
	return Mesh ? Cast<UGinkgoAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
}

// ===== 节点函数 =====

void UGinkgoWeaponAnimLayers::SetupRunLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	RunFollow = FLoopFollow();
}

void UGinkgoWeaponAnimLayers::UpdateRunLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	FollowLocomotion(Context, Node, RunLoop, RunFollow);
#if !UE_BUILD_SHIPPING
	DebugRunTime = RunFollow.Marker.Time;
	ReportLoops();
#endif
}

void UGinkgoWeaponAnimLayers::SetupSprintLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	SprintFollow = FLoopFollow();
}

void UGinkgoWeaponAnimLayers::UpdateSprintLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	FollowLocomotion(Context, Node, SprintLoop, SprintFollow);
#if !UE_BUILD_SHIPPING
	DebugSprintTime = SprintFollow.Marker.Time;
	ReportLoops();
#endif
}

void UGinkgoWeaponAnimLayers::UpdateAimLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	const float Time = UpdateClockLoop(Node, AimLoop);
#if !UE_BUILD_SHIPPING
	if (Time >= 0.f)
	{
		DebugAimTime = Time;
		ReportLoops();
	}
#endif
}

void UGinkgoWeaponAnimLayers::UpdateLoweredLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	UpdateClockLoop(Node, LoweredLoop);
}

void UGinkgoWeaponAnimLayers::UpdateHipLoop(const FAnimUpdateContext& Context, const FAnimNodeReference& Node)
{
	UpdateClockLoop(Node, HipLoop);
}

float UGinkgoWeaponAnimLayers::UpdateClockLoop(const FAnimNodeReference& Node, const UAnimSequence* Loop) const
{
	EAnimNodeReferenceConversionResult Result;
	const FSequenceEvaluatorReference Evaluator = USequenceEvaluatorLibrary::ConvertToSequenceEvaluator(Node, Result);
	if (Result != EAnimNodeReferenceConversionResult::Succeeded || !Loop)
	{
		return -1.f;
	}
	// 每次开镜都从 0 起的话，每次都从同一口呼吸开始，看得出重复；按时钟取，各个帧率下同一个时刻也是同一帧
	const float Time = Ginkgo::Animation::LoopTimeAt(LoopClock, Loop->GetPlayLength());
	USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Time);
	return Time;
}

void UGinkgoWeaponAnimLayers::FollowLocomotion(const FAnimUpdateContext& Context, const FAnimNodeReference& Node, const UAnimSequence* Loop, FLoopFollow& Follow)
{
	EAnimNodeReferenceConversionResult Result;
	const FSequenceEvaluatorReference Evaluator = USequenceEvaluatorLibrary::ConvertToSequenceEvaluator(Node, Result);
	const UGinkgoAnimInstance* Main = GetMainInstance();
	if (Result != EAnimNodeReferenceConversionResult::Succeeded || !Loop || !Main)
	{
		return;
	}
	// 领头的是移动状态机这一帧求姿势时放的片段与时刻：向前的循环与持枪循环是同一条时间线（在它上面逐帧做的），时刻相同、两边逐帧对应；
	// 侧向、后退、起步、急停按脚的标记对步伐；没有标记的（站着的循环、下落）照帧长往前走
	const UAnimSequence* Leader = Main->GetLocomotionSequence();
	const bool bLeaderChanged = Leader != Follow.Leader;
	Follow.Leader = Leader;
	Ginkgo::Animation::FollowMarkers(Follow.Marker, GetLoopFootMarkers(Leader), Main->GetLocomotionTime(), Leader ? Leader->GetPlayLength() : 0.f, bLeaderChanged,
		GetLoopFootMarkers(Loop), Loop->GetPlayLength(), UAnimExecutionContextLibrary::GetDeltaTime(Context));
	USequenceEvaluatorLibrary::SetExplicitTime(Evaluator, Follow.Marker.Time);
}

#if !UE_BUILD_SHIPPING
void UGinkgoWeaponAnimLayers::ReportLoops() const
{
	if (UGinkgoAnimInstance* Main = GetMainInstance())
	{
		UGinkgoAnimInstance::FDebugFrame::FWeaponLayer Report;
		Report.RunAlpha = RunAlpha;
		Report.SprintAlpha = SprintAlpha;
		Report.AimAlpha = AimLoopAlpha;
		Report.UpperBodyAlpha = UpperBodyAlpha;
		Report.RunTime = DebugRunTime;
		Report.SprintTime = DebugSprintTime;
		Report.AimTime = DebugAimTime;
		Main->ReportWeaponLayer(Report);
	}
}
#endif

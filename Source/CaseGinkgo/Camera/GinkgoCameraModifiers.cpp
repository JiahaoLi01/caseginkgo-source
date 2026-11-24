#include "GinkgoCameraModifiers.h"

#include "Algo/AllOf.h"
#include "Algo/Sort.h"
#include "GinkgoCameraContextComponent.h"
#include "GinkgoCameraMath.h"

namespace
{
	float ReadScalar(const UGinkgoCameraContextComponent* Context, const FGameplayTag& Key)
	{
		return Context ? Context->FindScalar(Key).Get(0.f) : 0.f;
	}

	/** 噪声三个分量的频率比（无理数比，叠起来不重复）与权重（加起来是 1：最多偏幅度那么多） */
	constexpr double NoiseRatios[] = {1.0, 1.6180339887498949, 2.6180339887498949};
	constexpr double NoiseWeights[] = {0.6, 0.3, 0.1};

	/** 相位：R2 序列（广义黄金比）按种子、轴、分量取，几个轴与几个效果互相错开 */
	double NoisePhase(const int32 Seed, const int32 Axis, const int32 Component)
	{
		constexpr double A1 = 0.7548776662466927;
		constexpr double A2 = 0.5698402909980532;
		const double Index = 1.0 + Seed * 9.0 + Axis * 3.0 + Component;
		return UE_DOUBLE_TWO_PI * FMath::Frac(0.5 + A1 * Index + A2 * Index * Index);
	}
}

// ===== 激活条件 =====

bool FGinkgoCameraScalarCondition::Holds(const UGinkgoCameraContextComponent* Context) const
{
	const float Value = ReadScalar(Context, Scalar);
	const float Limit = ThresholdScalar.IsValid() ? ReadScalar(Context, ThresholdScalar) : Threshold;
	return Compare == EGinkgoCameraCompare::AtLeast ? Value >= Limit : Value < Limit;
}

double FGinkgoCameraScalarCondition::Margin(const UGinkgoCameraContextComponent* Context) const
{
	const double Value = ReadScalar(Context, Scalar);
	const double Limit = ThresholdScalar.IsValid() ? ReadScalar(Context, ThresholdScalar) : Threshold;
	return Compare == EGinkgoCameraCompare::AtLeast ? Value - Limit : Limit - Value;
}

bool FGinkgoCameraActivation::MatchesQuery(const UGinkgoCameraContextComponent* Context) const
{
	if (Query.IsEmpty())
	{
		return true;
	}
	return Context && Query.Matches(Context->GetTags());
}

// ===== 数值来源 =====

float UGinkgoCameraValueScalarCurve::Evaluate(const UGinkgoCameraContextComponent* Context, const float BaseValue) const
{
	return Curve.GetRichCurveConst()->Eval(ReadScalar(Context, Scalar));
}

float UGinkgoCameraValueZoomedFov::Evaluate(const UGinkgoCameraContextComponent* Context, const float BaseValue) const
{
	return Ginkgo::Camera::ZoomedFieldOfView(BaseValue, ReadScalar(Context, ZoomScalar));
}

// ===== 叠加效果 =====

bool UGinkgoCameraOverlayEffect::IsActive(const UGinkgoCameraContextComponent* Context) const
{
	return Activation.MatchesQuery(Context)
		&& Algo::AllOf(Activation.Conditions, [Context](const FGinkgoCameraScalarCondition& Condition) { return Condition.Holds(Context); });
}

// ===== 冲量弹簧 =====

FGinkgoCameraOverlayOutput UGinkgoCameraImpulseShake::Step(FGinkgoCameraOverlayState& State, const FGinkgoCameraOverlayStep& Input) const
{
	if (!State.bActive)
	{
		State.bActive = true;
		State.Rotation.Reset();
		State.FieldOfView.Reset();
	}
	// 这一帧里这个事件的冲量，按时刻排好（早于这一帧开头的按开头算）。激活条件不成立时不收；离上一次冲量近于 MinInterval 的不收
	TArray<TGinkgoImpulseSpring<FVector>::FImpulse, TInlineAllocator<2>> Kicks;
	TArray<TGinkgoImpulseSpring<double>::FImpulse, TInlineAllocator<2>> FieldOfViewKicks;
	const bool bActive = IsActive(Input.Context);
	TArray<const FGinkgoCameraEvent*, TInlineAllocator<2>> Matching;
	for (const FGinkgoCameraEvent& Item : Input.Events)
	{
		if (bActive && Item.Tag == Event && FMath::Abs(Item.Magnitude) >= MinMagnitude)
		{
			Matching.Add(&Item);
		}
	}
	Algo::SortBy(Matching, [](const FGinkgoCameraEvent* Item) { return Item->Time; });
	for (const FGinkgoCameraEvent* Item : Matching)
	{
		if (Item->Time - State.LastImpulseTime < MinInterval)
		{
			continue;
		}
		State.LastImpulseTime = Item->Time;
		const double At = FMath::Clamp(Item->Time - Input.FrameStartTime, 0.0, Input.DeltaTime);
		const double Side = bMirrorOnNegative && Item->Magnitude < 0.f ? -1.0 : 1.0;
		Kicks.Add({At, FVector(Impulse.Pitch, Impulse.Yaw * Side, Impulse.Roll * Side)});
		FieldOfViewKicks.Add({At, FieldOfViewImpulse});
		++State.ImpulseCount;
	}
	Kicks.Sort([](const auto& A, const auto& B) { return A.Time < B.Time; });
	FieldOfViewKicks.Sort([](const auto& A, const auto& B) { return A.Time < B.Time; });
	State.Rotation.Step(Input.DeltaTime, Omega, Zeta, Kicks);
	State.FieldOfView.Step(Input.DeltaTime, Omega, Zeta, FieldOfViewKicks);

	FGinkgoCameraOverlayOutput Output;
	Output.Rotation = FRotator(State.Rotation.Value.X, State.Rotation.Value.Y, State.Rotation.Value.Z);
	Output.FieldOfView = static_cast<float>(State.FieldOfView.Value);
	return Output;
}

// ===== 事件带来的冲量 =====

FGinkgoCameraOverlayOutput UGinkgoCameraCarriedShake::Step(FGinkgoCameraOverlayState& State, const FGinkgoCameraOverlayStep& Input) const
{
	if (!State.bActive)
	{
		State.bActive = true;
		State.Carried.Reset();
	}
	for (FGinkgoCameraCarriedSpring& Spring : State.Carried)
	{
		Spring.Step(Input.DeltaTime);
	}
	// 这一帧的事件：每个冲量开一个弹簧，从事件的时刻晃起（早于这一帧开头的按开头算）。激活条件不成立时不收
	const bool bActive = IsActive(Input.Context);
	for (const FGinkgoCameraEvent& Item : Input.Events)
	{
		if (!bActive || Item.Tag != Event)
		{
			continue;
		}
		++State.ImpulseCount;
		const double At = FMath::Clamp(Item.Time - Input.FrameStartTime, 0.0, Input.DeltaTime);
		for (const FGinkgoCameraImpulse& Impulse : Item.Impulses)
		{
			FGinkgoCameraCarriedSpring& Spring = State.Carried.AddDefaulted_GetRef();
			Spring.Omega = Impulse.Omega;
			Spring.Zeta = Impulse.Zeta;
			Spring.ReturnOmega = Impulse.ReturnOmega;
			Spring.ReturnZeta = Impulse.ReturnZeta;
			Spring.TimeToPeak = Ginkgo::Spring::DampedPeakTime(Impulse.Omega, Impulse.Zeta);
			Spring.Rotation.Velocity = FVector(Impulse.Rotation.Pitch, Impulse.Rotation.Yaw, Impulse.Rotation.Roll) * Scale;
			Spring.FieldOfView.Velocity = static_cast<double>(Impulse.FieldOfView) * Scale;
			Spring.Step(Input.DeltaTime - At);
		}
	}
	// 晃停了就删：差这么一点看不出来，也不影响帧率无关
	State.Carried.RemoveAll([](const FGinkgoCameraCarriedSpring& Spring)
	{
		return Spring.Rotation.Value.IsNearlyZero(1e-4) && Spring.Rotation.Velocity.IsNearlyZero(1e-3)
			&& FMath::Abs(Spring.FieldOfView.Value) <= 1e-4 && FMath::Abs(Spring.FieldOfView.Velocity) <= 1e-3;
	});

	FVector Rotation = FVector::ZeroVector;
	double FieldOfView = 0.0;
	for (const FGinkgoCameraCarriedSpring& Spring : State.Carried)
	{
		Rotation += Spring.Rotation.Value;
		FieldOfView += Spring.FieldOfView.Value;
	}
	FGinkgoCameraOverlayOutput Output;
	Output.Rotation = FRotator(Rotation.X, Rotation.Y, Rotation.Z);
	Output.FieldOfView = static_cast<float>(FieldOfView);
	return Output;
}

// ===== 噪声 × 包络 =====

double UGinkgoCameraNoiseSway::MapEnvelopeScalar(const UGinkgoCameraContextComponent* Context) const
{
	if (!EnvelopeScalar.IsValid())
	{
		return FMath::Min(1.0, static_cast<double>(MaxEnvelope));
	}
	const double Span = static_cast<double>(ScalarForOne) - ScalarForZero;
	const double Mapped = FMath::Abs(Span) > UE_KINDA_SMALL_NUMBER ? (ReadScalar(Context, EnvelopeScalar) - ScalarForZero) / Span : 1.0;
	return FMath::Clamp(Mapped, 0.0, static_cast<double>(MaxEnvelope));
}

double UGinkgoCameraNoiseSway::EvaluateEnvelopeTarget(const UGinkgoCameraContextComponent* Context) const
{
	return IsActive(Context) ? MapEnvelopeScalar(Context) : 0.0;
}

double UGinkgoCameraNoiseSway::EvaluateNoise(const int32 Axis, const double Time) const
{
	const FGinkgoCameraNoiseAxis& Settings = Axis == 0 ? Pitch : Axis == 1 ? Yaw : Roll;
	if (Settings.Amplitude <= 0.f)
	{
		return 0.0;
	}
	double Sum = 0.0;
	for (int32 Component = 0; Component < UE_ARRAY_COUNT(NoiseRatios); ++Component)
	{
		Sum += NoiseWeights[Component] * FMath::Sin(UE_DOUBLE_TWO_PI * Settings.Frequency * NoiseRatios[Component] * Time + NoisePhase(Seed, Axis, Component));
	}
	return Settings.Amplitude * Sum;
}

FGinkgoCameraOverlayOutput UGinkgoCameraNoiseSway::Step(FGinkgoCameraOverlayState& State, const FGinkgoCameraOverlayStep& Input) const
{
	const bool bActive = IsActive(Input.Context);
	const double Mapped = MapEnvelopeScalar(Input.Context);
	if (!State.bActive)
	{
		State.bActive = true;
		State.Envelope.Reset(bActive ? Mapped : 0.0);
	}
	else
	{
		// 激活条件变了算在帧的开头（移动组件在每次移动的开头改状态），连续量（速度、开镜进度）当作在帧里从上一帧的匀速变过来（跟随点的做法）：
		// 这一帧里目标从"现在的激活 × 上一帧的连续量"匀速变到"现在的激活 × 现在的连续量"，两种变化都与帧率无关
		State.Envelope.StepMoving(bActive ? State.LastEnvelopeTarget : 0.0, bActive ? Mapped : 0.0, EnvelopeOmega, Input.DeltaTime);
	}
	State.LastEnvelopeTarget = Mapped;

	const double Envelope = FMath::Max(State.Envelope.Value, 0.0);
	FGinkgoCameraOverlayOutput Output;
	Output.Rotation = FRotator(Envelope * EvaluateNoise(0, Input.Clock), Envelope * EvaluateNoise(1, Input.Clock), Envelope * EvaluateNoise(2, Input.Clock));
	return Output;
}

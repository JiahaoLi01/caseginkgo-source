#include "GinkgoCameraParamLayer.h"

#include "GinkgoCameraConfig.h"
#include "GinkgoCameraContextComponent.h"
#include "GinkgoCameraMath.h"

namespace
{
	/**
	 * 一个修改器在这一帧里的权重：分段线性。开头的值，目标在帧里哪一刻变（没变为负）、变之前与之后的目标；
	 * 淡入淡出时间为 0 时在那一刻一下子跳过去（取右极限）
	 */
	struct FWeightPath
	{
		float Start = 0.f;
		double Flip = -1.0;
		bool bBefore = false;
		bool bAfter = false;
		float BlendIn = 0.f;
		float BlendOut = 0.f;
		/** 盖在弹簧之后的覆盖（权重沿曲线走）：这一帧里当作从 Start 线性地走到 End，整帧长 Duration，没有折点。只用来算它屏蔽的增量 */
		bool bDriven = false;
		float End = 0.f;
		double Duration = 0.0;

		float Ramp(const float Weight, const bool bTarget, const double Time) const
		{
			if ((bTarget ? BlendIn : BlendOut) <= 0.f)
			{
				return bTarget ? 1.f : 0.f;
			}
			return FGinkgoCameraParamLayer::StepWeight(Weight, bTarget, Time, BlendIn, BlendOut);
		}

		/** @brief 帧里某一刻的权重；bRight 取右极限（目标恰好在这一刻变时取变了以后的） */
		float At(const double Time, const bool bRight) const
		{
			if (bDriven)
			{
				return Duration > 0.0 ? FMath::Lerp(Start, End, static_cast<float>(FMath::Clamp(Time / Duration, 0.0, 1.0))) : End;
			}
			if (Flip < 0.0 || Time < Flip || (Time == Flip && !bRight))
			{
				return Ramp(Start, bBefore, Time);
			}
			return Ramp(Ramp(Start, bBefore, Flip), bAfter, Time - Flip);
		}

		/** @brief 折点：目标变的那一刻、淡到 0 或 1 的那一刻（落在帧里的） */
		void AddBreaks(TArray<double, TInlineAllocator<8>>& Breaks, const double DeltaTime) const
		{
			if (bDriven)
			{
				return;
			}
			const auto Saturation = [this](const float Weight, const bool bTarget)
			{
				const float Blend = bTarget ? BlendIn : BlendOut;
				return Blend <= 0.f ? 0.0 : static_cast<double>(bTarget ? 1.f - Weight : Weight) * Blend;
			};
			const auto Add = [&Breaks, DeltaTime](const double Time)
			{
				if (Time > 0.0 && Time < DeltaTime)
				{
					Breaks.Add(Time);
				}
			};
			if (Flip < 0.0)
			{
				Add(Saturation(Start, bBefore));
				return;
			}
			Add(FMath::Min(Saturation(Start, bBefore), Flip));
			Add(Flip);
			Add(Flip + Saturation(Ramp(Start, bBefore, Flip), bAfter));
		}
	};
}

float FGinkgoCameraParamLayer::StepWeight(const float Weight, const bool bTarget, const double Time, const float BlendInTime, const float BlendOutTime)
{
	if (Time <= 0.0)
	{
		return Weight;
	}
	if (bTarget)
	{
		return BlendInTime <= 0.f ? 1.f : static_cast<float>(FMath::Min(1.0, Weight + Time / BlendInTime));
	}
	return BlendOutTime <= 0.f ? 0.f : static_cast<float>(FMath::Max(0.0, Weight - Time / BlendOutTime));
}

FGinkgoCameraParams FGinkgoCameraParamLayer::Compose(const UGinkgoCameraConfig& Config, const TConstArrayView<float> Weights)
{
	const TArray<FGinkgoCameraParamModifier>& Definitions = Config.Modifiers;
	FGinkgoCameraParams Params = Config.BaseParams;

	// 覆盖：按优先级从低到高（同优先级按列表的顺序）逐层插值
	TArray<int32, TInlineAllocator<8>> Overrides;
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		if (Definitions[Index].Blend == EGinkgoCameraBlend::Override && Weights[Index] > 0.f)
		{
			Overrides.Add(Index);
		}
	}
	Overrides.StableSort([&Definitions](const int32 A, const int32 B) { return Definitions[A].Priority < Definitions[B].Priority; });
	for (const int32 Index : Overrides)
	{
		// 盖在弹簧之后的覆盖在 Evaluate 的最后合成，这里只留着算它屏蔽的增量
		if (Definitions[Index].IsAfterSprings())
		{
			continue;
		}
		if (const FFloatProperty* Property = Ginkgo::Camera::FindParamProperty(Definitions[Index].Param))
		{
			const float Current = Ginkgo::Camera::GetParamValue(Params, *Property);
			Ginkgo::Camera::SetParamValue(Params, *Property, Current + Weights[Index] * (Modifiers[Index].Value - Current));
		}
	}

	// 增量：屏蔽的程度是屏蔽这个类别的覆盖里权重最大的那个
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FGinkgoCameraParamModifier& Modifier = Definitions[Index];
		Modifiers[Index].Block = 0.f;
		if (Modifier.Blend != EGinkgoCameraBlend::Delta || Weights[Index] <= 0.f)
		{
			continue;
		}
		for (const int32 Override : Overrides)
		{
			if (Modifier.Category.IsValid() && Modifier.Category.MatchesAny(Definitions[Override].BlockedCategories))
			{
				Modifiers[Index].Block = FMath::Max(Modifiers[Index].Block, Weights[Override]);
			}
		}
		if (const FFloatProperty* Property = Ginkgo::Camera::FindParamProperty(Modifier.Param))
		{
			const float Current = Ginkgo::Camera::GetParamValue(Params, *Property);
			Ginkgo::Camera::SetParamValue(Params, *Property, Current + (1.f - Modifiers[Index].Block) * Weights[Index] * Modifiers[Index].Value);
		}
	}
	return Params;
}

FGinkgoCameraParams FGinkgoCameraParamLayer::Evaluate(const UGinkgoCameraConfig& Config, const UGinkgoCameraContextComponent* Context, const double DeltaTime,
	const bool bEnabled)
{
	const TArray<FGinkgoCameraParamModifier>& Definitions = Config.Modifiers;
	// 关掉（对照用）：直接是基础值，状态清空，再开时直接放到位
	if (!bEnabled)
	{
		bActive = false;
		Modifiers.Reset();
		Springs.Reset();
		Target = Config.BaseParams;
		return Target;
	}
	// 运行中增删了修改器、参数弹簧：状态对不上了，从头开始
	const bool bFirst = !bActive || Modifiers.Num() != Definitions.Num() || Springs.Num() != Config.ParamSprings.Num();
	if (bFirst)
	{
		Modifiers.Reset();
		Modifiers.SetNum(Definitions.Num());
		Springs.Reset();
		Springs.SetNum(Config.ParamSprings.Num());
	}
	bActive = true;
	const double Duration = bFirst ? 0.0 : FMath::Max(DeltaTime, 0.0);

	// 修改器的值：基础值是配置里这个参数的 P₀
	const auto EvaluateValue = [&Config, Context](const FGinkgoCameraParamModifier& Modifier)
	{
		const FFloatProperty* Property = Ginkgo::Camera::FindParamProperty(Modifier.Param);
		return Modifier.Value ? Modifier.Value->Evaluate(Context, Property ? Ginkgo::Camera::GetParamValue(Config.BaseParams, *Property) : 0.f) : 0.f;
	};

	// ===== 权重：目标与它在帧里变的那一刻 =====
	TArray<FWeightPath, TInlineAllocator<8>> Paths;
	Paths.SetNum(Definitions.Num());
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		const FGinkgoCameraParamModifier& Modifier = Definitions[Index];
		const TArray<FGinkgoCameraScalarCondition>& Conditions = Modifier.Activation.Conditions;
		FModifierState& State = Modifiers[Index];
		const bool bQuery = Modifier.bEnabled && Modifier.Activation.MatchesQuery(Context);
		TArray<double, TInlineAllocator<2>> Margins;
		bool bConditions = true;
		for (const FGinkgoCameraScalarCondition& Condition : Conditions)
		{
			Margins.Add(Condition.Margin(Context));
			bConditions &= Condition.Holds(Context);
		}
		// 锁住：标签查询还成立时，激活着的修改器因为连续量的条件失效了，之后条件再成立也不再激活，直到标签查询不成立
		const bool bLatched = Modifier.bLatchOff && bQuery && (State.bLatched || (State.bTarget && !bConditions));
		const bool bTarget = bQuery && bConditions && !bLatched;

		FWeightPath& Path = Paths[Index];
		Path.BlendIn = Modifier.BlendInTime;
		Path.BlendOut = Modifier.BlendOutTime;
		Path.Start = bFirst ? (bTarget ? 1.f : 0.f) : State.Weight;
		Path.bBefore = bFirst ? bTarget : State.bTarget;
		Path.bAfter = bTarget;
		State.FlipTime = -1.0;
		if (!bFirst && bTarget != State.bTarget)
		{
			// 目标在帧里哪一刻变的：标签查询变了算在帧的开头（移动组件在每次移动的开头改状态）；只是连续量的条件跨过了 0 时，
			// 余量当作在帧里线性地变，插出跨过的时刻。变成激活取最后一个变成立的条件，变成不激活取第一个变不成立的
			double Flip = 0.0;
			if (bQuery == State.bQuery && State.Margins.Num() == Margins.Num())
			{
				bool bFound = false;
				for (int32 Condition = 0; Condition < Margins.Num(); ++Condition)
				{
					const double Before = State.Margins[Condition];
					const double After = Margins[Condition];
					const bool bAtLeast = Conditions[Condition].Compare == EGinkgoCameraCompare::AtLeast;
					const bool bHeldBefore = bAtLeast ? Before >= 0.0 : Before > 0.0;
					const bool bHoldsNow = bAtLeast ? After >= 0.0 : After > 0.0;
					if (bHeldBefore == bHoldsNow || bHoldsNow != bTarget)
					{
						continue;
					}
					const double Crossing = FMath::Abs(Before - After) > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(Before / (Before - After), 0.0, 1.0) * DeltaTime : 0.0;
					Flip = !bFound ? Crossing : bTarget ? FMath::Max(Flip, Crossing) : FMath::Min(Flip, Crossing);
					bFound = true;
				}
			}
			Path.Flip = Flip;
			State.FlipTime = Flip;
		}
		if (Modifier.IsAfterSprings())
		{
			// 权重沿曲线走：先走到目标变的那一刻，再换方向走完这一帧
			const auto Step = [&Modifier](const float Weight, const bool bTowards, const double Time)
			{
				return Ginkgo::Camera::StepCurvedWeight(Weight, bTowards, Time, Modifier.BlendInTime, Modifier.BlendOutTime, Modifier.BlendInCurve, Modifier.BlendOutCurve,
					Modifier.BlendCurveStrength);
			};
			Path.End = Path.Flip < 0.0
				? Step(Path.Start, bTarget, Duration)
				: Step(Step(Path.Start, Path.bBefore, Path.Flip), bTarget, Duration - Path.Flip);
			Path.bDriven = true;
			Path.Duration = Duration;
		}
		State.bTarget = bTarget;
		State.bQuery = bQuery;
		State.bLatched = bLatched;
		State.Margins = Margins;
		State.Value = EvaluateValue(Modifier);
	}

	// ===== 折点：权重在帧里分段线性，合成的参数在折点之间也是线性的 =====
	TArray<double, TInlineAllocator<8>> Breaks = {0.0};
	for (const FWeightPath& Path : Paths)
	{
		Path.AddBreaks(Breaks, Duration);
	}
	Breaks.Add(Duration);
	Breaks.Sort();
	TArray<float, TInlineAllocator<8>> Weights;
	Weights.SetNum(Paths.Num());
	const auto ComposeAt = [&](const double Time, const bool bRight)
	{
		for (int32 Index = 0; Index < Paths.Num(); ++Index)
		{
			Weights[Index] = Paths[Index].At(Time, bRight);
		}
		return Compose(Config, Weights);
	};
	TArray<FGinkgoCameraParams, TInlineAllocator<8>> Left;
	TArray<FGinkgoCameraParams, TInlineAllocator<8>> Right;
	for (const double Break : Breaks)
	{
		Left.Add(ComposeAt(Break, false));
		Right.Add(ComposeAt(Break, true));
	}
	for (int32 Index = 0; Index < Paths.Num(); ++Index)
	{
		Modifiers[Index].Weight = Paths[Index].At(Duration, true);
	}
	// 帧末的合成（最后一次 Compose 用的就是帧末的右极限，屏蔽的程度也是帧末的）
	Target = Right.Last();

	// ===== 参数弹簧：在折点上拆开推进，每一段里目标匀速地变；目标跳的地方（淡入淡出时间为 0）先到左极限、再从右极限起 =====
	FGinkgoCameraParams Result = Target;
	for (int32 Index = 0; Index < Config.ParamSprings.Num(); ++Index)
	{
		const FFloatProperty* Property = Ginkgo::Camera::FindParamProperty(Config.ParamSprings[Index].Param);
		if (!Property)
		{
			continue;
		}
		TGinkgoCriticalSpring<double>& Spring = Springs[Index];
		const float Omega = Config.ParamSprings[Index].Omega;
		if (bFirst || Omega <= 0.f)
		{
			Spring.Reset(Ginkgo::Camera::GetParamValue(Target, *Property));
		}
		else
		{
			for (int32 Break = 0; Break + 1 < Breaks.Num(); ++Break)
			{
				const double Span = Breaks[Break + 1] - Breaks[Break];
				if (Span > 0.0)
				{
					Spring.StepMoving(Ginkgo::Camera::GetParamValue(Right[Break], *Property), Ginkgo::Camera::GetParamValue(Left[Break + 1], *Property), Omega, Span);
				}
			}
		}
		Ginkgo::Camera::SetParamValue(Result, *Property, static_cast<float>(Spring.Value));
	}

	// ===== 盖在弹簧之后的覆盖：按优先级从低到高 =====
	TArray<int32, TInlineAllocator<4>> Driven;
	for (int32 Index = 0; Index < Definitions.Num(); ++Index)
	{
		if (Definitions[Index].IsAfterSprings() && Modifiers[Index].Weight > 0.f)
		{
			Driven.Add(Index);
		}
	}
	Driven.StableSort([&Definitions](const int32 A, const int32 B) { return Definitions[A].Priority < Definitions[B].Priority; });
	for (const int32 Index : Driven)
	{
		if (const FFloatProperty* Property = Ginkgo::Camera::FindParamProperty(Definitions[Index].Param))
		{
			const auto Cover = [&](FGinkgoCameraParams& Params)
			{
				const float Current = Ginkgo::Camera::GetParamValue(Params, *Property);
				Ginkgo::Camera::SetParamValue(Params, *Property, Current + Modifiers[Index].Weight * (Modifiers[Index].Value - Current));
			};
			Cover(Result);
			Cover(Target);
		}
	}
	return Result;
}

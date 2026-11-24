#pragma once

#include "CoreMinimal.h"

/**
 * 相机的弹簧原语（Docs/Design/CameraPipeline.md「弹簧原语」）：相机的平滑与抖动都由两种弹簧实现，全部按解析解推进。
 * - 临界阻尼弹簧：跟随点与各个参数，追一个目标，不过冲。
 * - 欠阻尼冲量弹簧：叠加层，静止点为 0，事件给速度一个冲量。
 *
 * 为什么用解析解：先推进 t₁ 再推进 t₂，与一次推进 t₁ + t₂ 的结果相同（只差浮点舍入），所以同一组参数在任何帧率下手感一致；
 * 刚度再高也不会数值发散（显式积分在 ω·Δt 大于 2 左右就炸）。
 *
 * 弹簧不是管线的阶段：跟随点、各个参数、各个叠加效果各自持有实例。参数（ω、ζ）放在相机配置里，
 * 状态（值与速度）由玩家相机组件持有，同一份配置可以给几个相机用。
 *
 * 值的类型 T 要支持 T ± T 与 T × double，并能从 0.0 构造：double、FVector、FVector2D、FRotator 都行。
 * 用 T(0.0) 而不用 T{}：FVector 的默认构造不初始化分量。
 */
namespace Ginkgo::Spring
{
	/**
	 * @brief 临界阻尼：把偏差与它的速度按解析解推进一段时间
	 *        x(t) = (x₀ + (v₀ + ωx₀)t)·e^(−ωt)，v(t) = (v₀ − ωt(v₀ + ωx₀))·e^(−ωt)
	 * @param X - 当前值相对目标的偏差（目标在这一段里不动），推进后改写
	 * @param V - 偏差的变化速度，推进后改写
	 * @param Omega - 角频率（rad/s）：越大跟得越紧。跟踪匀速 v 运动的目标时稳态滞后为 2v/ω；≤ 0 表示不平滑，直接到目标、速度清零
	 * @param DeltaTime - 推进的时长（s），≤ 0 时不变
	 */
	template <typename T>
	void StepCritical(T& X, T& V, const double Omega, const double DeltaTime)
	{
		if (Omega <= 0.0)
		{
			X = T(0.0);
			V = T(0.0);
			return;
		}
		if (DeltaTime <= 0.0)
		{
			return;
		}
		const double Decay = FMath::Exp(-Omega * DeltaTime);
		// v₀ + ωx₀：两个式子共用
		const T Slope = V + X * Omega;
		X = (X + Slope * DeltaTime) * Decay;
		V = (V - Slope * (Omega * DeltaTime)) * Decay;
	}

	/**
	 * @brief 欠阻尼：从静止开始，要冲到 Peak，速度的冲量要多大
	 *        x(t) = v₀/ω_d · e^(−ζωt) · sin ω_d t 的最大值 = v₀/ω · e^(−ζ/√(1−ζ²) · atan2(√(1−ζ²), ζ))，出现在 atan2(√(1−ζ²), ζ) / ω_d
	 * @param Omega - 角频率（rad/s）；≤ 0 时为 0
	 */
	inline double DampedImpulseForPeak(const double Peak, const double Omega, const double Zeta)
	{
		if (Omega <= 0.0)
		{
			return 0.0;
		}
		const double ClampedZeta = FMath::Clamp(Zeta, 0.0, 1.0);
		const double Root = FMath::Sqrt(FMath::Max(1.0 - ClampedZeta * ClampedZeta, 1e-9));
		return Peak * Omega * FMath::Exp(ClampedZeta / Root * FMath::Atan2(Root, ClampedZeta));
	}

	/**
	 * @brief 欠阻尼：从静止开始给一个冲量，多久到峰值（速度第一次回到 0）：atan2(√(1−ζ²), ζ) / ω_d，ζ = 1 时趋于 1/ω
	 * @param Omega - 角频率（rad/s）；≤ 0 时为 0
	 */
	inline double DampedPeakTime(const double Omega, const double Zeta)
	{
		if (Omega <= 0.0)
		{
			return 0.0;
		}
		const double ClampedZeta = FMath::Clamp(Zeta, 0.0, 1.0);
		const double Root = FMath::Sqrt(FMath::Max(1.0 - ClampedZeta * ClampedZeta, 1e-9));
		return FMath::Atan2(Root, ClampedZeta) / (Omega * Root);
	}

	/**
	 * @brief 欠阻尼：把偏差与速度按解析解推进一段时间，静止点为 0
	 *        ω_d = ω√(1 − ζ²)，x(t) = e^(−ζωt)·(x₀cos ω_d t + (v₀ + ζωx₀)/ω_d · sin ω_d t)，
	 *        v(t) = e^(−ζωt)·(v₀cos ω_d t − (ω²x₀ + ζωv₀)/ω_d · sin ω_d t)
	 * @param X - 偏差，推进后改写
	 * @param V - 速度，推进后改写
	 * @param Omega - 无阻尼时的角频率（rad/s）；≤ 0 表示不起作用，值与速度清零
	 * @param Zeta - 阻尼比，限在 0–1：0 一直振荡，越接近 1 越快停下；振荡的频率是 ω_d / 2π
	 * @param DeltaTime - 推进的时长（s），≤ 0 时不变
	 * @note ζ 接近 1 时 ω_d 趋于 0，式子里要除以它，所以改用临界阻尼的解：两者在 ζ = 1 处连续（sin(ω_d t) / ω_d → t）
	 */
	template <typename T>
	void StepDamped(T& X, T& V, const double Omega, const double Zeta, const double DeltaTime)
	{
		if (Omega <= 0.0)
		{
			X = T(0.0);
			V = T(0.0);
			return;
		}
		if (DeltaTime <= 0.0)
		{
			return;
		}
		const double ClampedZeta = FMath::Clamp(Zeta, 0.0, 1.0);
		const double DampedSquared = 1.0 - ClampedZeta * ClampedZeta;
		if (DampedSquared < 1e-9)
		{
			StepCritical(X, V, Omega, DeltaTime);
			return;
		}
		const double OmegaD = Omega * FMath::Sqrt(DampedSquared);
		const double Decay = FMath::Exp(-ClampedZeta * Omega * DeltaTime);
		const double Cos = FMath::Cos(OmegaD * DeltaTime);
		const double SinOverOmegaD = FMath::Sin(OmegaD * DeltaTime) / OmegaD;
		const T X0 = X;
		const T V0 = V;
		X = (X0 * Cos + (V0 + X0 * (ClampedZeta * Omega)) * SinOverOmegaD) * Decay;
		V = (V0 * Cos - (X0 * (Omega * Omega) + V0 * (ClampedZeta * Omega)) * SinOverOmegaD) * Decay;
	}
}

/**
 * @struct TGinkgoCriticalSpring : 追目标的临界阻尼弹簧
 * 跟随点与各个相机参数各持一个。Value 是平滑后的值，Velocity 是它的速度。
 * 两种推进：Step 的目标在这一段里不动；StepMoving 的目标在这一段里匀速移动，追每帧都在动的目标（跟随点）用它。
 */
template <typename T>
struct TGinkgoCriticalSpring
{
	T Value = T(0.0);
	T Velocity = T(0.0);

	/**
	 * @brief 向目标推进一段时间
	 * @param Target - 这一段里的目标
	 * @param Omega - 角频率（rad/s），≤ 0 时直接到目标
	 * @param DeltaTime - 时长（s）
	 */
	void Step(const T& Target, const double Omega, const double DeltaTime)
	{
		T Offset = Value - Target;
		Ginkgo::Spring::StepCritical(Offset, Velocity, Omega, DeltaTime);
		Value = Target + Offset;
	}

	/**
	 * @brief 向一个匀速移动的目标推进一段时间：目标在这一段里从 TargetFrom 匀速移到 TargetTo
	 * @param TargetFrom - 这一段开始时的目标（上一帧的目标）
	 * @param TargetTo - 这一段结束时的目标（这一帧的目标）
	 * @param Omega - 角频率（rad/s），≤ 0 时直接到 TargetTo、速度清零
	 * @param DeltaTime - 时长（s），≤ 0 时不变
	 * @note 为什么不用 Step(TargetTo)：那等于目标在这一段一开始就跳到了帧末的位置。追匀速 v 的目标时，稳态滞后因此比 2v/ω 少约 v·Δt/2，
	 *       帧率越低少得越多（20 m/s 时 30 fps 少 33 cm、144 fps 少 7 cm）。
	 *       这里按目标在这一段里匀速移动求解：相对目标的偏差 e 满足 ë + 2ωė + ω²e = −2ωw（w 是目标的速度），特解是常数 −2w/ω，
	 *       也就是稳态滞后；e 减去特解满足齐次方程，照常按临界阻尼的解推进。所以目标匀速时结果就是连续时间的解，与帧率无关；
	 *       目标平滑地变速时误差是 Δt² 量级。目标在一帧里跳一下（蹲下时胶囊中心下移）会被当成在这一帧里匀速移过去
	 */
	void StepMoving(const T& TargetFrom, const T& TargetTo, const double Omega, const double DeltaTime)
	{
		if (Omega <= 0.0)
		{
			Reset(TargetTo);
			return;
		}
		if (DeltaTime <= 0.0)
		{
			return;
		}
		const T TargetVelocity = (TargetTo - TargetFrom) * (1.0 / DeltaTime);
		const T SteadyLag = TargetVelocity * (2.0 / Omega);
		// 相对目标的偏差减去特解（−2w/ω）、速度减去目标的速度：齐次方程的状态
		T Offset = Value - TargetFrom + SteadyLag;
		T OffsetVelocity = Velocity - TargetVelocity;
		Ginkgo::Spring::StepCritical(Offset, OffsetVelocity, Omega, DeltaTime);
		Value = TargetTo + Offset - SteadyLag;
		Velocity = OffsetVelocity + TargetVelocity;
	}

	/** @brief 直接放到某个值，速度清零：第一帧、切换视角目标时 */
	void Reset(const T& InValue)
	{
		Value = InValue;
		Velocity = T(0.0);
	}

	/** @brief 直接放到某个值，带上速度：从一个正在运动的状态接着跟 */
	void Reset(const T& InValue, const T& InVelocity)
	{
		Value = InValue;
		Velocity = InVelocity;
	}
};

/**
 * @struct TGinkgoImpulseSpring : 叠加层的冲量弹簧
 * 静止点为 0。开火、落地等事件给速度一个冲量：连续的冲量自然累积、自然饱和，"硬"的手感来自冲量瞬间改变速度，不来自高频振荡。
 * 振荡频率 ω_d / 2π 宜在 10–20 Hz：60 fps 下超过 30 Hz 会混叠。
 *
 * 一帧里的事件按时刻拆开推进：先推进到事件的时刻，加冲量，再推进剩下的时间。所以结果只取决于事件的时刻，
 * 与它落在哪一帧、帧率多少无关。
 */
template <typename T>
struct TGinkgoImpulseSpring
{
	/** 一次冲量 */
	struct FImpulse
	{
		/** 从这一步开始算起的时刻（s） */
		double Time = 0.0;
		/** 加到速度上的量 */
		T Impulse = T(0.0);
	};

	T Value = T(0.0);
	T Velocity = T(0.0);

	/**
	 * @brief 推进一段时间，期间按时刻施加冲量
	 * @param DeltaTime - 时长（s）
	 * @param Omega - 无阻尼时的角频率（rad/s）
	 * @param Zeta - 阻尼比，0–1
	 * @param Impulses - 这一段里的冲量，按时刻从早到晚排好；时刻限在这一段里，早于前一个的按前一个的时刻算（不往回推进）
	 */
	void Step(const double DeltaTime, const double Omega, const double Zeta, TConstArrayView<FImpulse> Impulses = {})
	{
		double Elapsed = 0.0;
		for (const FImpulse& Item : Impulses)
		{
			const double At = FMath::Clamp(Item.Time, Elapsed, FMath::Max(DeltaTime, Elapsed));
			Ginkgo::Spring::StepDamped(Value, Velocity, Omega, Zeta, At - Elapsed);
			Velocity = Velocity + Item.Impulse;
			Elapsed = At;
		}
		Ginkgo::Spring::StepDamped(Value, Velocity, Omega, Zeta, DeltaTime - Elapsed);
	}

	/** @brief 回到静止 */
	void Reset()
	{
		Value = T(0.0);
		Velocity = T(0.0);
	}
};

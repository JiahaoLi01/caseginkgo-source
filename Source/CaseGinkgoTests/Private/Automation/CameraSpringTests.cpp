#include "Camera/GinkgoSpring.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoCameraSpringTests
{
	/**
	 * 参照：四阶龙格-库塔按 1e-5 s 的小步积分 x'' = −2ζωx' − ω²x（ζ = 1 是临界阻尼）。
	 * ω·h 不到 1e-3，每步的截断误差在 1e-16 量级，拿来对照解析解
	 */
	void IntegrateReference(double& X, double& V, const double Omega, const double Zeta, const double Duration)
	{
		constexpr double Step = 1e-5;
		const auto Accel = [Omega, Zeta](const double Position, const double Velocity) { return -2.0 * Zeta * Omega * Velocity - Omega * Omega * Position; };
		const int32 Count = FMath::CeilToInt32(Duration / Step);
		const double H = Duration / Count;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double K1X = V;
			const double K1V = Accel(X, V);
			const double K2X = V + 0.5 * H * K1V;
			const double K2V = Accel(X + 0.5 * H * K1X, V + 0.5 * H * K1V);
			const double K3X = V + 0.5 * H * K2V;
			const double K3V = Accel(X + 0.5 * H * K2X, V + 0.5 * H * K2V);
			const double K4X = V + H * K3V;
			const double K4V = Accel(X + H * K3X, V + H * K3V);
			X += H / 6.0 * (K1X + 2.0 * K2X + 2.0 * K3X + K4X);
			V += H / 6.0 * (K1V + 2.0 * K2V + 2.0 * K3V + K4V);
		}
	}

	/**
	 * 参照：四阶龙格-库塔按 1e-5 s 的小步积分追随一个随时间变化的目标 x'' = −ω²(x − T(t)) − 2ωx'（临界阻尼），
	 * 目标 T 是连续时间的函数，拿来对照逐帧推进的结果
	 */
	template <typename FTargetFunction>
	void IntegrateTrackingReference(double& X, double& V, const double Omega, FTargetFunction&& Target, const double StartTime, const double Duration)
	{
		constexpr double Step = 1e-5;
		const auto Accel = [Omega, &Target](const double Time, const double Position, const double Velocity)
		{
			return -Omega * Omega * (Position - Target(Time)) - 2.0 * Omega * Velocity;
		};
		const int32 Count = FMath::Max(FMath::CeilToInt32(Duration / Step), 1);
		const double H = Duration / Count;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double T = StartTime + Index * H;
			const double K1X = V;
			const double K1V = Accel(T, X, V);
			const double K2X = V + 0.5 * H * K1V;
			const double K2V = Accel(T + 0.5 * H, X + 0.5 * H * K1X, V + 0.5 * H * K1V);
			const double K3X = V + 0.5 * H * K2V;
			const double K3V = Accel(T + 0.5 * H, X + 0.5 * H * K2X, V + 0.5 * H * K2V);
			const double K4X = V + H * K3V;
			const double K4V = Accel(T + H, X + H * K3X, V + H * K3V);
			X += H / 6.0 * (K1X + 2.0 * K2X + 2.0 * K3X + K4X);
			V += H / 6.0 * (K1V + 2.0 * K2V + 2.0 * K3V + K4V);
		}
	}

	/** 按固定帧率把一段时间切成等长的帧，逐帧推进 */
	template <typename FStepFunction>
	void StepFrames(const double Fps, const double Duration, FStepFunction&& StepFunction)
	{
		const int32 Frames = FMath::RoundToInt32(Duration * Fps);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			StepFunction(Frame / Fps, 1.0 / Fps);
		}
	}

	/** 帧末恰好是 1/6 s 的整数倍（30、60、144 fps 共有的时刻）时返回它是第几个，否则 INDEX_NONE */
	int32 CommonSampleIndex(const double FrameEnd)
	{
		const double Sixths = FrameEnd * 6.0;
		return FMath::IsNearlyEqual(Sixths, FMath::RoundToDouble(Sixths), 1e-9) ? FMath::RoundToInt32(Sixths) : INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCriticalSpringTest, "CaseGinkgo.Camera.Spring.Critical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCriticalSpringTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraSpringTests;

	// 解析解对得上细步长的数值积分：几组角频率、初始偏差与速度，几个时刻
	struct FCase
	{
		double Omega;
		double X0;
		double V0;
	};
	for (const FCase& Case : {FCase{10.0, 1.0, 0.0}, FCase{10.0, 0.0, 5.0}, FCase{25.0, -3.0, 40.0}, FCase{4.0, 2.0, -1.0}})
	{
		for (const double Time : {0.01, 0.1, 0.3, 1.0})
		{
			double X = Case.X0;
			double V = Case.V0;
			Ginkgo::Spring::StepCritical(X, V, Case.Omega, Time);
			double ReferenceX = Case.X0;
			double ReferenceV = Case.V0;
			IntegrateReference(ReferenceX, ReferenceV, Case.Omega, 1.0, Time);
			const FString Label = FString::Printf(TEXT("ω %.0f、x₀ %.0f、v₀ %.0f，%.2f s 后"), Case.Omega, Case.X0, Case.V0, Time);
			TestEqual(Label + TEXT("的偏差与数值积分一致"), X, ReferenceX, 1e-9);
			TestEqual(Label + TEXT("的速度与数值积分一致"), V, ReferenceV, 1e-8);
		}
	}

	// 与帧率无关：30 / 60 / 144 fps 逐帧推进 1 s，与一次推进 1 s 相同；不等长的帧也一样
	{
		double OnceX = 1.0;
		double OnceV = -2.0;
		Ginkgo::Spring::StepCritical(OnceX, OnceV, 12.0, 1.0);
		for (const double Fps : {30.0, 60.0, 144.0})
		{
			double X = 1.0;
			double V = -2.0;
			StepFrames(Fps, 1.0, [&](double, const double DeltaTime) { Ginkgo::Spring::StepCritical(X, V, 12.0, DeltaTime); });
			TestEqual(FString::Printf(TEXT("%.0f fps 逐帧推进 1 s 的偏差与一次推进相同"), Fps), X, OnceX, 1e-12);
			TestEqual(FString::Printf(TEXT("%.0f fps 逐帧推进 1 s 的速度与一次推进相同"), Fps), V, OnceV, 1e-11);
		}
		double X = 1.0;
		double V = -2.0;
		double Total = 0.0;
		for (const double DeltaTime : {0.013, 0.021, 0.0071, 0.05, 0.0004, 0.0333})
		{
			Ginkgo::Spring::StepCritical(X, V, 12.0, DeltaTime);
			Total += DeltaTime;
		}
		double ExpectedX = 1.0;
		double ExpectedV = -2.0;
		Ginkgo::Spring::StepCritical(ExpectedX, ExpectedV, 12.0, Total);
		TestEqual(TEXT("不等长的几帧与一次推进总时长相同"), X, ExpectedX, 1e-12);
	}

	// 边界：ω ≤ 0 直接到目标；Δt ≤ 0 不变；刚度再高也不发散（e^(−ωt) 下溢成 0，不出 NaN）
	{
		double X = 3.0;
		double V = 7.0;
		Ginkgo::Spring::StepCritical(X, V, 0.0, 0.1);
		TestTrue(TEXT("ω 为 0 时直接到目标、速度清零"), X == 0.0 && V == 0.0);
		X = 3.0;
		V = 7.0;
		Ginkgo::Spring::StepCritical(X, V, 10.0, 0.0);
		TestTrue(TEXT("Δt 为 0 时不变"), X == 3.0 && V == 7.0);
		X = 3.0;
		V = 7.0;
		Ginkgo::Spring::StepCritical(X, V, 1e4, 1.0);
		TestTrue(TEXT("ω·Δt 为 1e4 时到达目标，没有发散"), X == 0.0 && V == 0.0);
		X = 3.0;
		V = 7.0;
		Ginkgo::Spring::StepCritical(X, V, 1000.0, 1.0 / 60.0);
		TestTrue(TEXT("ω 为 1000、60 fps 一帧（显式积分早已发散）时有限且很小"), FMath::IsFinite(X) && FMath::Abs(X) < 1e-5 && FMath::IsFinite(V));
	}

	// 向量的各分量各自按标量推进
	{
		FVector X(1.0, -2.0, 3.0);
		FVector V(0.0, 10.0, -5.0);
		Ginkgo::Spring::StepCritical(X, V, 8.0, 0.2);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			double ScalarX = FVector(1.0, -2.0, 3.0)[Axis];
			double ScalarV = FVector(0.0, 10.0, -5.0)[Axis];
			Ginkgo::Spring::StepCritical(ScalarX, ScalarV, 8.0, 0.2);
			TestEqual(FString::Printf(TEXT("分量 %d 与标量相同"), Axis), X[Axis], ScalarX, 1e-15);
		}
	}

	// 追一个匀速运动的目标：稳态滞后为 2v/ω（设计文档的例子：20 m/s、ω = 10 时落后 4 m），速度跟上目标
	{
		TGinkgoCriticalSpring<double> Spring;
		constexpr double Speed = 2000.0;
		constexpr double Omega = 10.0;
		double Target = 0.0;
		StepFrames(1000.0, 3.0, [&](const double Time, const double DeltaTime)
		{
			Target = Speed * (Time + DeltaTime);
			Spring.Step(Target, Omega, DeltaTime);
		});
		TestEqual(TEXT("稳态滞后约为 2v/ω = 400 cm"), Target - Spring.Value, 2.0 * Speed / Omega, 2.0 * Speed / Omega * 0.01);
		TestEqual(TEXT("稳态速度等于目标的速度"), Spring.Velocity, Speed, Speed * 0.01);
		Spring.Reset(5.0);
		TestTrue(TEXT("Reset 放到那个值、速度清零"), Spring.Value == 5.0 && Spring.Velocity == 0.0);
		Spring.Reset(5.0, -3.0);
		TestTrue(TEXT("Reset 也可以带上速度"), Spring.Value == 5.0 && Spring.Velocity == -3.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoMovingTargetSpringTest, "CaseGinkgo.Camera.Spring.Moving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoMovingTargetSpringTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraSpringTests;
	constexpr double Omega = 10.0;

	// 目标在这一段里匀速移动：与细步长的数值积分一致（几组起点、目标的速度与时长）
	struct FCase
	{
		double X0;
		double V0;
		double TargetStart;
		double TargetVelocity;
		double Duration;
	};
	for (const FCase& Case : {FCase{0.0, 0.0, 0.0, 2000.0, 0.3}, FCase{50.0, -100.0, 20.0, -700.0, 0.05}, FCase{0.0, 0.0, 0.0, 762.0, 1.0}})
	{
		TGinkgoCriticalSpring<double> Spring;
		Spring.Reset(Case.X0, Case.V0);
		Spring.StepMoving(Case.TargetStart, Case.TargetStart + Case.TargetVelocity * Case.Duration, Omega, Case.Duration);
		double ReferenceX = Case.X0;
		double ReferenceV = Case.V0;
		IntegrateTrackingReference(ReferenceX, ReferenceV, Omega, [&Case](const double Time) { return Case.TargetStart + Case.TargetVelocity * Time; }, 0.0, Case.Duration);
		const FString Label = FString::Printf(TEXT("目标从 %.0f 以 %.0f cm/s 移动 %.2f s"), Case.TargetStart, Case.TargetVelocity, Case.Duration);
		TestEqual(Label + TEXT("：值与数值积分一致"), Spring.Value, ReferenceX, 1e-8);
		TestEqual(Label + TEXT("：速度与数值积分一致"), Spring.Velocity, ReferenceV, 1e-7);
	}

	// 匀速目标的稳态滞后：StepMoving 在任何帧率下都恰好是 2v/ω；只给帧末目标的 Step 少约 v·Δt/2（目标在帧里提前到了帧末）
	{
		constexpr double Speed = 2000.0;
		for (const double Fps : {30.0, 60.0, 144.0})
		{
			TGinkgoCriticalSpring<double> Moving;
			TGinkgoCriticalSpring<double> Held;
			double Target = 0.0;
			StepFrames(Fps, 3.0, [&](const double Time, const double DeltaTime)
			{
				const double NextTarget = Speed * (Time + DeltaTime);
				Moving.StepMoving(Target, NextTarget, Omega, DeltaTime);
				Held.Step(NextTarget, Omega, DeltaTime);
				Target = NextTarget;
			});
			TestEqual(FString::Printf(TEXT("%.0f fps 时 StepMoving 的稳态滞后恰好是 2v/ω = 400 cm"), Fps), Target - Moving.Value, 2.0 * Speed / Omega, 1e-6);
			const double HalfFrame = Speed / Fps / 2.0;
			TestEqual(FString::Printf(TEXT("%.0f fps 时 Step 的稳态滞后少约 v·Δt/2 = %.1f cm"), Fps, HalfFrame), 2.0 * Speed / Omega - (Target - Held.Value), HalfFrame,
				HalfFrame * 0.01);
		}
	}

	// 目标平滑地变速（正弦，1 Hz、振幅 300 cm）：与连续时间的解比，StepMoving 的误差随帧长的平方变小，Step 的大一个量级以上、随帧长线性变小
	{
		constexpr double Amplitude = 300.0;
		const double Angular = 2.0 * UE_DOUBLE_PI;
		const auto Target = [Angular](const double Time) { return Amplitude * FMath::Sin(Angular * Time); };
		// 从目标上、带着目标的速度出发，每 1/6 s 记一次连续时间的解
		TArray<double> Reference;
		{
			double X = 0.0;
			double V = Amplitude * Angular;
			for (int32 Index = 1; Index <= 12; ++Index)
			{
				IntegrateTrackingReference(X, V, Omega, Target, (Index - 1) / 6.0, 1.0 / 6.0);
				Reference.Add(X);
			}
		}
		TMap<double, double> MovingErrors;
		for (const double Fps : {30.0, 60.0, 144.0})
		{
			TGinkgoCriticalSpring<double> Moving;
			TGinkgoCriticalSpring<double> Held;
			Moving.Reset(0.0, Amplitude * Angular);
			Held.Reset(0.0, Amplitude * Angular);
			double MovingError = 0.0;
			double HeldError = 0.0;
			StepFrames(Fps, 2.0, [&](const double Time, const double DeltaTime)
			{
				Moving.StepMoving(Target(Time), Target(Time + DeltaTime), Omega, DeltaTime);
				Held.Step(Target(Time + DeltaTime), Omega, DeltaTime);
				const int32 Sample = CommonSampleIndex(Time + DeltaTime);
				if (Sample != INDEX_NONE)
				{
					MovingError = FMath::Max(MovingError, FMath::Abs(Moving.Value - Reference[Sample - 1]));
					HeldError = FMath::Max(HeldError, FMath::Abs(Held.Value - Reference[Sample - 1]));
				}
			});
			MovingErrors.Add(Fps, MovingError);
			AddInfo(FString::Printf(TEXT("正弦目标 %.0f fps：StepMoving 最大误差 %.4f cm，Step %.3f cm"), Fps, MovingError, HeldError));
			TestTrue(FString::Printf(TEXT("%.0f fps 时 StepMoving 的误差不到 1 cm"), Fps), MovingError < 1.0);
			TestTrue(FString::Printf(TEXT("%.0f fps 时 Step 的误差是 StepMoving 的 10 倍以上"), Fps), HeldError > 10.0 * MovingError);
		}
		const double Ratio = MovingErrors[30.0] / MovingErrors[60.0];
		TestTrue(FString::Printf(TEXT("帧长减半误差约为四分之一（30 / 60 fps 的误差之比 %.2f）"), Ratio), Ratio > 3.5 && Ratio < 4.5);
	}

	// 同一段匀速的路线切成不等长的几步，与一次推进相同
	{
		TGinkgoCriticalSpring<double> Once;
		Once.Reset(10.0, 50.0);
		Once.StepMoving(0.0, 0.2 * 900.0, Omega, 0.2);
		TGinkgoCriticalSpring<double> Pieces;
		Pieces.Reset(10.0, 50.0);
		double Elapsed = 0.0;
		for (const double DeltaTime : {0.013, 0.05, 0.0071, 0.0999, 0.0300})
		{
			Pieces.StepMoving(900.0 * Elapsed, 900.0 * (Elapsed + DeltaTime), Omega, DeltaTime);
			Elapsed += DeltaTime;
		}
		TestEqual(TEXT("切成不等长的几步与一次推进的值相同"), Pieces.Value, Once.Value, 1e-9);
		TestEqual(TEXT("切成不等长的几步与一次推进的速度相同"), Pieces.Velocity, Once.Velocity, 1e-8);
	}

	// 边界：ω ≤ 0 直接到帧末的目标、速度清零；Δt ≤ 0 不变
	{
		TGinkgoCriticalSpring<FVector> Spring;
		Spring.Reset(FVector(1.0, 2.0, 3.0), FVector(4.0, 5.0, 6.0));
		Spring.StepMoving(FVector::ZeroVector, FVector(10.0, 0.0, 0.0), Omega, 0.0);
		TestTrue(TEXT("Δt 为 0 时不变"), Spring.Value == FVector(1.0, 2.0, 3.0) && Spring.Velocity == FVector(4.0, 5.0, 6.0));
		Spring.StepMoving(FVector::ZeroVector, FVector(10.0, 0.0, 0.0), 0.0, 0.1);
		TestTrue(TEXT("ω 为 0 时直接到帧末的目标、速度清零"), Spring.Value == FVector(10.0, 0.0, 0.0) && Spring.Velocity == FVector::ZeroVector);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoDampedSpringTest, "CaseGinkgo.Camera.Spring.Damped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoDampedSpringTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraSpringTests;
	const double Omega12Hz = 2.0 * UE_DOUBLE_PI * 12.0;

	// 解析解对得上细步长的数值积分：几个阻尼比，冲量式的初速度与初始偏移两种起点
	for (const double Zeta : {0.0, 0.2, 0.5, 0.9})
	{
		for (const TPair<double, double>& Start : {TPair<double, double>(0.0, 100.0), TPair<double, double>(1.0, 0.0)})
		{
			for (const double Time : {0.005, 0.02, 0.1, 0.5})
			{
				double X = Start.Key;
				double V = Start.Value;
				Ginkgo::Spring::StepDamped(X, V, Omega12Hz, Zeta, Time);
				double ReferenceX = Start.Key;
				double ReferenceV = Start.Value;
				IntegrateReference(ReferenceX, ReferenceV, Omega12Hz, Zeta, Time);
				const FString Label = FString::Printf(TEXT("ζ %.1f、x₀ %.0f、v₀ %.0f，%.3f s 后"), Zeta, Start.Key, Start.Value, Time);
				TestEqual(Label + TEXT("的偏差与数值积分一致"), X, ReferenceX, 1e-9);
				TestEqual(Label + TEXT("的速度与数值积分一致"), V, ReferenceV, 1e-7);
			}
		}
	}

	// 与帧率无关：逐帧推进与一次推进相同
	{
		double OnceX = 0.0;
		double OnceV = 100.0;
		Ginkgo::Spring::StepDamped(OnceX, OnceV, Omega12Hz, 0.3, 1.0);
		for (const double Fps : {30.0, 60.0, 144.0})
		{
			double X = 0.0;
			double V = 100.0;
			StepFrames(Fps, 1.0, [&](double, const double DeltaTime) { Ginkgo::Spring::StepDamped(X, V, Omega12Hz, 0.3, DeltaTime); });
			TestEqual(FString::Printf(TEXT("%.0f fps 逐帧推进 1 s 与一次推进相同"), Fps), X, OnceX, 1e-12);
		}
	}

	// 振荡的周期：从静止点出发，每过 π/ω_d 回到静止点一次，正负交替
	{
		constexpr double Zeta = 0.3;
		const double OmegaD = Omega12Hz * FMath::Sqrt(1.0 - Zeta * Zeta);
		double X = 0.0;
		double V = 100.0;
		Ginkgo::Spring::StepDamped(X, V, Omega12Hz, Zeta, UE_DOUBLE_PI / OmegaD);
		TestEqual(TEXT("π/ω_d 后回到静止点"), X, 0.0, 1e-12);
		TestTrue(TEXT("这时速度反向"), V < 0.0);
	}

	// ζ = 0 不衰减：x² + (v/ω)² 守恒
	{
		double X = 1.0;
		double V = 0.0;
		StepFrames(60.0, 2.0, [&](double, const double DeltaTime) { Ginkgo::Spring::StepDamped(X, V, Omega12Hz, 0.0, DeltaTime); });
		TestEqual(TEXT("ζ 为 0 时振幅不变"), X * X + FMath::Square(V / Omega12Hz), 1.0, 1e-12);
	}

	// ζ → 1 时与临界阻尼连续；ζ = 1 时就是临界阻尼；超出 0–1 的按边界算
	{
		double CriticalX = 0.5;
		double CriticalV = 30.0;
		Ginkgo::Spring::StepCritical(CriticalX, CriticalV, Omega12Hz, 0.05);
		double NearX = 0.5;
		double NearV = 30.0;
		Ginkgo::Spring::StepDamped(NearX, NearV, Omega12Hz, 1.0 - 1e-7, 0.05);
		// ζ 本身差 1e-7，衰减 e^(−ζωt) 就差 1e-7 · ωt 的相对量（这里约 4e-7）
		TestEqual(TEXT("ζ 为 1 − 1e-7 时与临界阻尼几乎相同"), NearX, CriticalX, 1e-6);
		double OneX = 0.5;
		double OneV = 30.0;
		Ginkgo::Spring::StepDamped(OneX, OneV, Omega12Hz, 1.0, 0.05);
		TestTrue(TEXT("ζ 为 1 时就是临界阻尼"), OneX == CriticalX && OneV == CriticalV);
		double OverX = 0.5;
		double OverV = 30.0;
		Ginkgo::Spring::StepDamped(OverX, OverV, Omega12Hz, 3.0, 0.05);
		TestTrue(TEXT("ζ 大于 1 按 1 算"), OverX == CriticalX && OverV == CriticalV);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoImpulseSpringTest, "CaseGinkgo.Camera.Spring.Impulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoImpulseSpringTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraSpringTests;
	using FSpring = TGinkgoImpulseSpring<double>;
	const double Omega = 2.0 * UE_DOUBLE_PI * 15.0;
	constexpr double Zeta = 0.4;

	// 按事件的时刻拆开推进，与帧率无关：同一个冲量（0.1234 s，不落在任何帧率的帧边界上），30 / 60 / 144 fps 下
	// 在三者共有的时刻（1/6 s 的整数倍）状态相同，也与"推进到事件的时刻、加冲量、再推进"一样
	constexpr double EventTime = 0.1234;
	constexpr double Impulse = 300.0;
	TArray<double> Expected;
	{
		FSpring Spring;
		Spring.Step(EventTime, Omega, Zeta);
		Spring.Velocity += Impulse;
		double Time = EventTime;
		for (int32 K = 1; K <= 6; ++K)
		{
			Spring.Step(K / 6.0 - Time, Omega, Zeta);
			Time = K / 6.0;
			Expected.Add(Spring.Value);
		}
	}
	for (const double Fps : {30.0, 60.0, 144.0})
	{
		FSpring Spring;
		TArray<double> Samples;
		StepFrames(Fps, 1.0, [&](const double FrameStart, const double DeltaTime)
		{
			TArray<FSpring::FImpulse> Impulses;
			if (EventTime >= FrameStart && EventTime < FrameStart + DeltaTime)
			{
				Impulses.Add({EventTime - FrameStart, Impulse});
			}
			Spring.Step(DeltaTime, Omega, Zeta, Impulses);
			// 帧末恰好是 1/6 s 的整数倍时记下
			const double FrameEnd = FrameStart + DeltaTime;
			if (FMath::IsNearlyEqual(FrameEnd * 6.0, FMath::RoundToDouble(FrameEnd * 6.0), 1e-9))
			{
				Samples.Add(Spring.Value);
			}
		});
		if (TestEqual(FString::Printf(TEXT("%.0f fps 有 6 个共有时刻"), Fps), Samples.Num(), 6))
		{
			for (int32 Index = 0; Index < 6; ++Index)
			{
				TestEqual(FString::Printf(TEXT("%.0f fps 在 %d/6 s 时与按事件时刻推进的相同"), Fps, Index + 1), Samples[Index], Expected[Index], 1e-9);
			}
		}
	}

	// 线性：两个冲量的响应等于各自响应之和；同一时刻的两个冲量等于一个相加的冲量
	{
		const auto Run = [&](TArray<FSpring::FImpulse> Impulses)
		{
			FSpring Spring;
			Spring.Step(0.05, Omega, Zeta, Impulses);
			Spring.Step(0.1, Omega, Zeta);
			return Spring.Value;
		};
		TestEqual(TEXT("两次冲量的响应是各自响应之和"), Run({{0.01, 100.0}, {0.03, -40.0}}), Run({{0.01, 100.0}}) + Run({{0.03, -40.0}}), 1e-12);
		TestEqual(TEXT("同一时刻的两个冲量等于相加的一个"), Run({{0.02, 100.0}, {0.02, 200.0}}), Run({{0.02, 300.0}}), 1e-12);
		TestEqual(TEXT("没有冲量时一直静止"), Run({}), 0.0, 0.0);
		// 没排好序时不往回推进：晚的在前，早的那个按晚的时刻算
		TestEqual(TEXT("早于前一个的冲量按前一个的时刻算"), Run({{0.03, 100.0}, {0.01, 50.0}}), Run({{0.03, 150.0}}), 1e-12);
	}

	// 帧末的冲量与下一帧开头的冲量相同
	{
		const FSpring::FImpulse EndOfFrame{1.0 / 60.0, 100.0};
		const FSpring::FImpulse StartOfFrame{0.0, 100.0};
		FSpring AtEnd;
		AtEnd.Step(1.0 / 60.0, Omega, Zeta, MakeArrayView(&EndOfFrame, 1));
		AtEnd.Step(1.0 / 60.0, Omega, Zeta);
		FSpring AtStart;
		AtStart.Step(1.0 / 60.0, Omega, Zeta);
		AtStart.Step(1.0 / 60.0, Omega, Zeta, MakeArrayView(&StartOfFrame, 1));
		TestEqual(TEXT("帧末的冲量与下一帧开头的相同"), AtEnd.Value, AtStart.Value, 1e-12);
	}

	// 旋转偏移用向量（Pitch、Yaw、Roll），各分量各自响应
	{
		const TGinkgoImpulseSpring<FVector>::FImpulse KickImpulse{0.01, FVector(-120.0, 30.0, 0.0)};
		const FSpring::FImpulse PitchImpulse{0.01, -120.0};
		const FSpring::FImpulse YawImpulse{0.01, 30.0};
		TGinkgoImpulseSpring<FVector> Kick;
		Kick.Step(0.05, Omega, Zeta, MakeArrayView(&KickImpulse, 1));
		FSpring Pitch;
		Pitch.Step(0.05, Omega, Zeta, MakeArrayView(&PitchImpulse, 1));
		FSpring Yaw;
		Yaw.Step(0.05, Omega, Zeta, MakeArrayView(&YawImpulse, 1));
		TestTrue(TEXT("向量的各分量与标量相同"), FMath::IsNearlyEqual(Kick.Value.X, Pitch.Value, 1e-12) && FMath::IsNearlyEqual(Kick.Value.Y, Yaw.Value, 1e-12)
			&& Kick.Value.Z == 0.0);
		Kick.Reset();
		TestTrue(TEXT("Reset 回到静止"), Kick.Value == FVector::ZeroVector && Kick.Velocity == FVector::ZeroVector);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

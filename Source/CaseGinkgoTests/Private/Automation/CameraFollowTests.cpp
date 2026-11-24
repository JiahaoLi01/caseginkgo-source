#include "Camera/GinkgoCameraCollision.h"
#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraFollow.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GinkgoCameraFollowTests
{
	/** 冲刺速度（移动配置的类默认值） */
	constexpr double SprintSpeed = 762.0;

	/** 帧末恰好是 1/6 s 的整数倍（30、60、144 fps 共有的时刻）时返回它是第几个，否则 INDEX_NONE */
	int32 CommonSampleIndex(const double FrameEnd)
	{
		const double Sixths = FrameEnd * 6.0;
		return FMath::IsNearlyEqual(Sixths, FMath::RoundToDouble(Sixths), 1e-9) ? FMath::RoundToInt32(Sixths) : INDEX_NONE;
	}

	/**
	 * 地面上的一段冲刺：静止到 0.5 s，匀加速到 2/3 s 时达到冲刺速度，匀速到 2 s，匀减速到 2.5 s 停下。
	 * 变速的时刻都是 30、60、144 fps 共有的帧边界；加速度 4572 cm/s²，比移动组件的起步（约 7620）缓一些
	 */
	struct FSprintRoute
	{
		FVector Direction = FVector(1.0, 0.3, 0.0).GetSafeNormal();

		double Speed(const double Time) const
		{
			constexpr double Start = 0.5;
			constexpr double Full = 2.0 / 3.0;
			constexpr double Brake = 2.0;
			constexpr double Stop = 2.5;
			if (Time <= Start || Time >= Stop)
			{
				return 0.0;
			}
			if (Time < Full)
			{
				return SprintSpeed * (Time - Start) / (Full - Start);
			}
			return Time < Brake ? SprintSpeed : SprintSpeed * (Stop - Time) / (Stop - Brake);
		}

		/** 速度对时间的积分（分段是一次、二次函数，解析地求） */
		double Distance(const double Time) const
		{
			constexpr double Start = 0.5;
			constexpr double Full = 2.0 / 3.0;
			constexpr double Brake = 2.0;
			constexpr double Stop = 2.5;
			const double Accelerating = FMath::Clamp(Time, Start, Full) - Start;
			const double Cruising = FMath::Clamp(Time, Full, Brake) - Full;
			const double Braking = FMath::Clamp(Time, Brake, Stop) - Brake;
			const double Accel = SprintSpeed / (Full - Start);
			const double Decel = SprintSpeed / (Stop - Brake);
			return 0.5 * Accel * Accelerating * Accelerating + SprintSpeed * Cruising + SprintSpeed * Braking - 0.5 * Decel * Braking * Braking;
		}

		FVector Location(const double Time) const { return FVector(100.0, -200.0, 90.0) + Direction * Distance(Time); }
		FVector Velocity(const double Time) const { return Direction * Speed(Time); }
	};

	/**
	 * 像移动组件那样瞬时变速：速度在 0.5 s 一下子到冲刺速度，1 s 时转 76°（lurch），2 s 时停下（撞墙），2.5 s 起以 300 cm/s 后退。
	 * 每帧的速度是这一帧移动用的速度（帧末时刻往前那一段的），位置按它积分；变速的时刻都是共有的帧边界
	 */
	struct FStepRoute
	{
		struct FSegment
		{
			double Start;
			double End;
			FVector Velocity;
		};
		TArray<FSegment> Segments;

		FStepRoute()
		{
			const FVector Forward = FVector(1.0, 0.3, 0.0).GetSafeNormal() * SprintSpeed;
			const FVector Turned = FRotator(0.0, 76.0, 0.0).RotateVector(Forward);
			Segments = {{0.0, 0.5, FVector::ZeroVector}, {0.5, 1.0, Forward}, {1.0, 2.0, Turned}, {2.0, 2.5, FVector::ZeroVector}, {2.5, 1e3, FVector(-300.0, 0.0, 0.0)}};
		}

		FVector Location(const double Time) const
		{
			FVector Result(100.0, -200.0, 90.0);
			for (const FSegment& Segment : Segments)
			{
				Result += Segment.Velocity * (FMath::Clamp(Time, Segment.Start, Segment.End) - Segment.Start);
			}
			return Result;
		}

		FVector Velocity(const double Time) const
		{
			const FSegment* Found = Segments.FindByPredicate([Time](const FSegment& Segment) { return Time > Segment.Start + 1e-9 && Time <= Segment.End + 1e-9; });
			return Found ? Found->Velocity : FVector::ZeroVector;
		}
	};

	/** 按固定帧率推进跟随 Duration 秒，记下每个共有时刻的滞后（平滑后的减去真实的胶囊中心），以及整段里最大的滞后 */
	struct FFollowRun
	{
		TMap<int32, FVector> Lags;
		double MaxLag = 0.0;
	};

	template <typename FRoute>
	FFollowRun RunFollow(const FRoute& Route, const double Fps, const double Duration, const FGinkgoFollowSettings& Settings)
	{
		FFollowRun Run;
		FGinkgoCameraFollow Follow;
		Follow.Step(Route.Location(0.0), Route.Velocity(0.0), 0.0, Settings);
		const int32 Frames = FMath::RoundToInt32(Duration * Fps);
		for (int32 Frame = 1; Frame <= Frames; ++Frame)
		{
			const double Time = Frame / Fps;
			const FVector Lag = Follow.Step(Route.Location(Time), Route.Velocity(Time), 1.0 / Fps, Settings) - Route.Location(Time);
			Run.MaxLag = FMath::Max(Run.MaxLag, Lag.Size());
			const int32 Sample = CommonSampleIndex(Time);
			if (Sample != INDEX_NONE)
			{
				Run.Lags.Add(Sample, Lag);
			}
		}
		return Run;
	}

	/** 两次推进在共有时刻的滞后最多差多少 */
	double MaxDifference(const FFollowRun& Run, const FFollowRun& Reference)
	{
		double Difference = 0.0;
		for (const TPair<int32, FVector>& Pair : Run.Lags)
		{
			Difference = FMath::Max(Difference, FVector::Dist(Pair.Value, Reference.Lags.FindRef(Pair.Key)));
		}
		return Difference;
	}

	/** 匀速直线运动的目标 */
	struct FConstantRoute
	{
		FVector Start = FVector::ZeroVector;
		FVector Speed = FVector::ZeroVector;

		FVector Location(const double Time) const { return Start + Speed * Time; }
		FVector Velocity(double) const { return Speed; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraFollowFeedforwardTest, "CaseGinkgo.Camera.Follow.Feedforward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraFollowFeedforwardTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraFollowTests;
	const FConstantRoute Sprint{FVector(500.0, 300.0, 90.0), FVector(SprintSpeed * 0.8, SprintSpeed * 0.6, 0.0)};

	// 前馈系数为 1：匀速移动时滞后恰好是 0，每一帧、每个帧率都是
	FGinkgoFollowSettings FullFeedforward;
	FullFeedforward.Feedforward = 1.f;
	for (const double Fps : {30.0, 60.0, 144.0})
	{
		const FFollowRun Run = RunFollow(Sprint, Fps, 3.0, FullFeedforward);
		TestTrue(FString::Printf(TEXT("%.0f fps 匀速冲刺时每一帧的滞后都是 0（最大 %.2g cm）"), Fps, Run.MaxLag), Run.MaxLag < 1e-6);
	}

	// 没有前馈时稳态滞后是 2v/ω（冲刺 762 cm/s、ω 为 10 时 152.4 cm），前馈系数为 0.5 / 默认 0.8 时按 (1 - k) 缩减；不限最大滞后
	{
		FGinkgoFollowSettings Settings;
		Settings.MaxLag = 0.f;
		for (const double Feedforward : {0.0, 0.5, static_cast<double>(FGinkgoFollowSettings().Feedforward)})
		{
			Settings.Feedforward = Feedforward;
			FGinkgoCameraFollow Follow;
			const FConstantRoute Route{FVector::ZeroVector, FVector(SprintSpeed, 0.0, 0.0)};
			// 从静止开始：先停 0.5 s，再按冲刺速度匀速走 3 s
			FVector Lag = FVector::ZeroVector;
			Follow.Step(FVector::ZeroVector, FVector::ZeroVector, 0.0, Settings);
			for (int32 Frame = 1; Frame <= 210; ++Frame)
			{
				const double Time = FMath::Max(Frame / 60.0 - 0.5, 0.0);
				const FVector Target = Route.Location(Time);
				Lag = Follow.Step(Target, Time > 0.0 ? Route.Velocity(Time) : FVector::ZeroVector, 1.0 / 60.0, Settings) - Target;
			}
			const double Expected = (1.0 - Feedforward) * 2.0 * SprintSpeed / Settings.Omega;
			TestEqual(FString::Printf(TEXT("前馈系数 %.1f 时稳态滞后是 %.1f cm"), Feedforward, Expected), -Lag.X, Expected, 1e-6);
		}
	}

	// 竖直方向各用各的：前馈系数为 0（类默认值）时匀速上升滞后 2v/ω，为 1 时没有滞后；水平前馈为 1 时照常没有滞后
	{
		const FConstantRoute Climb{FVector::ZeroVector, FVector(SprintSpeed, 0.0, 300.0)};
		FGinkgoFollowSettings Settings;
		Settings.Feedforward = 1.f;
		Settings.VerticalMaxLag = 0.f;
		const FFollowRun Soft = RunFollow(Climb, 60.0, 3.0, Settings);
		const FVector SoftLag = Soft.Lags.FindRef(18);
		TestEqual(TEXT("竖直不前馈时匀速上升 300 cm/s 滞后 2v/ω = 60 cm"), -SoftLag.Z, 2.0 * 300.0 / Settings.VerticalOmega, 1e-6);
		TestEqual(TEXT("这时水平照常没有滞后"), FVector2D(SoftLag).Size(), 0.0, 1e-6);
		Settings.VerticalFeedforward = 1.f;
		const FFollowRun Stiff = RunFollow(Climb, 60.0, 3.0, Settings);
		TestTrue(TEXT("竖直前馈系数为 1 时没有滞后"), Stiff.MaxLag < 1e-6);
	}

	// 速度先过低通时，前馈为 1 匀速移动的稳态滞后照样是 0（低通的输出收敛到速度本身）
	{
		FGinkgoFollowSettings Settings;
		Settings.Feedforward = 1.f;
		Settings.FeedforwardFilterOmega = 20.f;
		Settings.MaxLag = 0.f;
		FGinkgoCameraFollow Follow;
		Follow.Step(FVector::ZeroVector, FVector::ZeroVector, 0.0, Settings);
		FVector Lag = FVector::ZeroVector;
		for (int32 Frame = 1; Frame <= 240; ++Frame)
		{
			const FVector Target(SprintSpeed * Frame / 60.0, 0.0, 0.0);
			Lag = Follow.Step(Target, FVector(SprintSpeed, 0.0, 0.0), 1.0 / 60.0, Settings) - Target;
		}
		TestEqual(TEXT("速度过低通时匀速移动 4 s 后滞后为 0"), Lag.Size(), 0.0, 1e-6);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraFollowFrameRateTest, "CaseGinkgo.Camera.Follow.FrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraFollowFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraFollowTests;

	// 像移动组件那样在帧的开头瞬时变速（起步、lurch、撞墙、后退），变速的时刻是共有的帧边界：30 / 60 / 144 fps 与 6000 fps 在共有时刻相同。
	// 胶囊中心在帧里匀速移动、前馈的偏移整帧取这一帧的速度，正是这种运动的连续时间的解
	{
		const FStepRoute Route;
		const FFollowRun Reference = RunFollow(Route, 6000.0, 3.5, FGinkgoFollowSettings());
		for (const double Fps : {30.0, 60.0, 144.0})
		{
			const FFollowRun Run = RunFollow(Route, Fps, 3.5, FGinkgoFollowSettings());
			const double Difference = MaxDifference(Run, Reference);
			AddInfo(FString::Printf(TEXT("瞬时变速 %.0f fps：共有时刻的滞后与 6000 fps 最多差 %.2g cm（最大滞后 %.1f cm）"), Fps, Difference, Run.MaxLag));
			TestTrue(FString::Printf(TEXT("瞬时变速：%.0f fps 与 6000 fps 相同"), Fps), Difference < 1e-6);
		}
	}

	// 平滑地变速（匀加速起步、匀减速刹车），没有前馈：胶囊中心是二次曲线，帧里按匀速移动只差 Δt² 量级，误差随帧长的平方变小
	const FSprintRoute Smooth;
	FGinkgoFollowSettings NoFeedforward;
	NoFeedforward.Feedforward = 0.f;
	NoFeedforward.MaxLag = 0.f;
	{
		const FFollowRun Reference = RunFollow(Smooth, 6000.0, 3.0, NoFeedforward);
		TMap<double, double> Errors;
		for (const double Fps : {30.0, 60.0, 144.0})
		{
			Errors.Add(Fps, MaxDifference(RunFollow(Smooth, Fps, 3.0, NoFeedforward), Reference));
			AddInfo(FString::Printf(TEXT("平滑变速、没有前馈 %.0f fps：与 6000 fps 最多差 %.4f cm"), Fps, Errors[Fps]));
		}
		// 预先算过的量级：0.21、0.053、0.0091 cm
		TestTrue(TEXT("平滑变速、没有前馈：30 fps 的误差不到 0.3 cm"), Errors[30.0] < 0.3);
		TestTrue(TEXT("平滑变速、没有前馈：144 fps 的误差不到 0.015 cm"), Errors[144.0] < 0.015);
		const double Ratio = Errors[30.0] / Errors[60.0];
		TestTrue(FString::Printf(TEXT("平滑变速、没有前馈：帧长减半误差约为四分之一（之比 %.2f）"), Ratio), Ratio > 3.5 && Ratio < 4.5);
	}

	// 平滑地变速、有前馈：前馈的偏移整帧取帧末的速度，比帧里的平均速度多 a·Δt/2，前馈的目标偏 系数 × a·Δt/ω，误差随帧长线性变小。
	// 这是为瞬时变速精确付的代价；移动组件里连续变速的只有下落中的重力（竖直默认不前馈）
	{
		const FFollowRun Reference = RunFollow(Smooth, 6000.0, 3.0, FGinkgoFollowSettings());
		const double Accel = SprintSpeed / (2.0 / 3.0 - 0.5);
		for (const double Fps : {30.0, 60.0, 144.0})
		{
			const double Error = MaxDifference(RunFollow(Smooth, Fps, 3.0, FGinkgoFollowSettings()), Reference);
			const double Bias = Accel / Fps / FGinkgoFollowSettings().Omega;
			AddInfo(FString::Printf(TEXT("平滑变速、有前馈 %.0f fps：与 6000 fps 最多差 %.3f cm（前馈目标的偏差 a·Δt/ω = %.2f cm）"), Fps, Error, Bias));
			TestTrue(FString::Printf(TEXT("平滑变速、有前馈：%.0f fps 的误差不超过前馈目标的偏差"), Fps), Error < Bias);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraFollowLimitsTest, "CaseGinkgo.Camera.Follow.Limits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraFollowLimitsTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraFollowTests;
	const FGinkgoFollowSettings Settings;

	// 第一帧直接放到目标上
	FGinkgoCameraFollow Follow;
	const FVector Start(100.0, 200.0, 90.0);
	TestTrue(TEXT("第一帧直接放到目标上"), Follow.Step(Start, FVector::ZeroVector, 1.0 / 60.0, Settings) == Start);

	// 最大滞后：目标一帧跳开 500 cm（没有 Reset 的传送、大校正），水平滞后恰好是 150 cm；竖直跳开 300 cm，滞后恰好是 100 cm
	{
		const FVector Jumped = Start + FVector(500.0, 0.0, 300.0);
		const FVector Lag = Follow.Step(Jumped, FVector::ZeroVector, 1.0 / 60.0, Settings) - Jumped;
		TestEqual(TEXT("水平滞后被投影回最大滞后"), FVector2D(Lag).Size(), static_cast<double>(Settings.MaxLag), 1e-9);
		TestEqual(TEXT("竖直滞后被限在最大滞后"), Lag.Z, -static_cast<double>(Settings.VerticalMaxLag), 1e-9);
		TestTrue(TEXT("这一帧标记为被投影回来"), Follow.WasClamped());
		// 之后追上来，不再超过
		double MaxHorizontal = 0.0;
		double MaxVertical = 0.0;
		FVector Settled = FVector::ZeroVector;
		for (int32 Frame = 0; Frame < 120; ++Frame)
		{
			Settled = Follow.Step(Jumped, FVector::ZeroVector, 1.0 / 60.0, Settings) - Jumped;
			MaxHorizontal = FMath::Max(MaxHorizontal, FVector2D(Settled).Size());
			MaxVertical = FMath::Max(MaxVertical, FMath::Abs(Settled.Z));
		}
		TestTrue(TEXT("之后不再超过最大滞后"), MaxHorizontal <= Settings.MaxLag + 1e-9 && MaxVertical <= Settings.VerticalMaxLag + 1e-9);
		TestTrue(TEXT("2 s 后追上了（不到 0.01 cm）"), Settled.Size() < 0.01);
	}

	// Reset 之后下一帧直接放到目标上，不追过去
	{
		const FVector Teleported = Start + FVector(-3000.0, 4000.0, 0.0);
		Follow.Reset();
		TestTrue(TEXT("Reset 之后直接放到新目标上"), Follow.Step(Teleported, FVector::ZeroVector, 1.0 / 60.0, Settings) == Teleported);
		TestTrue(TEXT("没有被投影"), !Follow.WasClamped());
	}

	// ω ≤ 0：不平滑，也不前馈，一直在目标上
	{
		FGinkgoFollowSettings Rigid;
		Rigid.Omega = 0.f;
		Rigid.VerticalOmega = 0.f;
		FGinkgoCameraFollow RigidFollow;
		const FSprintRoute Route;
		double MaxLag = 0.0;
		for (int32 Frame = 0; Frame <= 180; ++Frame)
		{
			const double Time = Frame / 60.0;
			MaxLag = FMath::Max(MaxLag, FVector::Dist(RigidFollow.Step(Route.Location(Time), Route.Velocity(Time), 1.0 / 60.0, Rigid), Route.Location(Time)));
		}
		TestEqual(TEXT("ω 为 0 时一直在目标上"), MaxLag, 0.0, 1e-9);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraCollisionPushOutTest, "CaseGinkgo.Camera.Collision.PushOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraCollisionPushOutTest::RunTest(const FString& Parameters)
{
	using namespace GinkgoCameraFollowTests;
	constexpr double PushOutOmega = 6.0;

	// 第一帧直接用要求的压缩；有东西挡进来时当帧拉到位
	{
		FGinkgoCameraCollision Collision;
		TestEqual(TEXT("第一帧直接用要求的压缩"), Collision.Step(80.0, 1.0 / 60.0, PushOutOmega), 80.0);
		Collision.Step(0.0, 1.0 / 60.0, PushOutOmega);
		TestEqual(TEXT("挡进来时当帧拉到要求的压缩"), Collision.Step(200.0, 1.0 / 60.0, PushOutOmega), 200.0);
		TestEqual(TEXT("拉近时速度清零"), Collision.GetCompressionVelocity(), 0.0);
		// 正在推远时又挡进来：少于现在的压缩时继续推远，多于时当帧拉到位
		Collision.Step(0.0, 0.1, PushOutOmega);
		const double Pushing = Collision.GetCompression();
		TestTrue(TEXT("遮挡消失后开始推远"), Pushing < 200.0 && Pushing > 0.0);
		TestTrue(TEXT("要求的少于现在的压缩时继续推远，不往回拉"), Collision.Step(Pushing * 0.5, 1.0 / 60.0, PushOutOmega) < Pushing);
		TestEqual(TEXT("要求的多于现在的压缩时当帧拉到位"), Collision.Step(Pushing + 50.0, 1.0 / 60.0, PushOutOmega), Pushing + 50.0);
	}

	// 一段推远的过程：0.5 s 时挡进来 200 cm，1 s 起 0.5 s 里匀速退开；2.5 s 时又挡进来 150 cm，3 s 时一下子消失
	const auto Required = [](const double Time)
	{
		if (Time < 0.5 - 1e-9 || (Time > 1.5 && Time < 2.5 - 1e-9) || Time > 3.0 + 1e-9)
		{
			return 0.0;
		}
		if (Time <= 1.0)
		{
			return 200.0;
		}
		return Time <= 1.5 ? 200.0 * (1.5 - Time) / 0.5 : 150.0;
	};
	const auto RunPushOut = [&Required](const double Fps, double& OutViolation)
	{
		TMap<int32, double> Samples;
		FGinkgoCameraCollision Collision;
		OutViolation = 0.0;
		const int32 Frames = FMath::RoundToInt32(4.0 * Fps);
		for (int32 Frame = 0; Frame <= Frames; ++Frame)
		{
			const double Time = Frame / Fps;
			const double Compression = Collision.Step(Required(Time), 1.0 / Fps, PushOutOmega);
			OutViolation = FMath::Max(OutViolation, Required(Time) - Compression);
			const int32 Sample = CommonSampleIndex(Time);
			if (Sample != INDEX_NONE)
			{
				Samples.Add(Sample, Compression);
			}
		}
		return Samples;
	};
	double ReferenceViolation = 0.0;
	const TMap<int32, double> Reference = RunPushOut(6000.0, ReferenceViolation);
	for (const double Fps : {30.0, 60.0, 144.0})
	{
		double Violation = 0.0;
		const TMap<int32, double> Samples = RunPushOut(Fps, Violation);
		double RampDifference = 0.0;
		double StepDifference = 0.0;
		for (const TPair<int32, double>& Pair : Samples)
		{
			// 2.5 s（第 15 个共有时刻）之前是匀速退开的那一段，之后是一下子消失的那一段
			double& Difference = Pair.Key <= 15 ? RampDifference : StepDifference;
			Difference = FMath::Max(Difference, FMath::Abs(Pair.Value - Reference.FindRef(Pair.Key)));
		}
		// 匀速退开时按匀速变化推进是精确的：与帧率无关
		TestEqual(FString::Printf(TEXT("%.0f fps：匀速退开的推远与 6000 fps 相同"), Fps), RampDifference, 0.0, 1e-9);
		// 一下子消失时不知道是在帧里哪一刻，按帧中间算：与 6000 fps 最多差推远的最大速度乘半帧（C·ω/e·Δt/2）
		const double Bound = 150.0 * PushOutOmega / UE_DOUBLE_EULERS_NUMBER / Fps / 2.0;
		AddInfo(FString::Printf(TEXT("%.0f fps：一下子消失后与 6000 fps 最多差 %.3f cm（上限 %.3f）"), Fps, StepDifference, Bound));
		TestTrue(FString::Printf(TEXT("%.0f fps：一下子消失后的差不超过半帧的推远"), Fps), StepDifference <= Bound * 1.02);
		TestEqual(FString::Printf(TEXT("%.0f fps：压缩从不少于要求的"), Fps), Violation, 0.0);
	}

	// 推远的角频率 ≤ 0：推远也是瞬时的
	{
		FGinkgoCameraCollision Collision;
		Collision.Step(120.0, 1.0 / 60.0, 0.0);
		TestEqual(TEXT("推远的角频率为 0 时遮挡消失当帧回到原位"), Collision.Step(0.0, 1.0 / 60.0, 0.0), 0.0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

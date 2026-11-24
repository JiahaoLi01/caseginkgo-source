#include "Character/GinkgoCrouchMeshBlend.h"
#include "Misc/AutomationTest.h"
#include "Movement/GinkgoMovementMath.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCrouchMeshBlendTest, "CaseGinkgo.Movement.CrouchMeshBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCrouchMeshBlendTest::RunTest(const FString& Parameters)
{
	constexpr double Omega = 25.0;

	// 空中蹲下：基准在世界里抬高 48 cm，这一刻偏移反着记下，模型不动
	FGinkgoCrouchMeshBlend Blend;
	TestFalse(TEXT("开始时不用更新"), Blend.IsActive());
	Blend.Absorb(48.0);
	TestEqual(TEXT("跳的那一段反着记下"), Blend.GetOffset(), -48.0);
	TestTrue(TEXT("要更新"), Blend.IsActive());

	// 往 0 收：一路单调、不过冲，收完不用更新
	bool bMonotonic = true;
	double Last = Blend.GetOffset();
	for (int32 Step = 0; Step < 240; ++Step)
	{
		Blend.Step(Omega, 1.0 / 240.0);
		bMonotonic &= Blend.GetOffset() >= Last && Blend.GetOffset() <= 0.0;
		Last = Blend.GetOffset();
	}
	TestTrue(TEXT("一路单调、不过冲"), bMonotonic);
	TestEqual(TEXT("1 s 以后收完"), Blend.GetOffset(), 0.0);
	TestFalse(TEXT("收完不用更新"), Blend.IsActive());

	// 与帧率无关：一步 0.1 s 同十步 0.01 s
	FGinkgoCrouchMeshBlend Coarse;
	FGinkgoCrouchMeshBlend Fine;
	Coarse.Absorb(48.0);
	Fine.Absorb(48.0);
	Coarse.Step(Omega, 0.1);
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Fine.Step(Omega, 0.01);
	}
	TestEqual(TEXT("与帧率无关"), Coarse.GetOffset(), Fine.GetOffset(), 1e-6);

	// 还没收完又站起来：偏移接着上，模型还是不跳
	FGinkgoCrouchMeshBlend Toggle;
	Toggle.Absorb(48.0);
	Toggle.Step(Omega, 0.05);
	const double BeforeToggle = Toggle.GetOffset();
	Toggle.Absorb(-48.0);
	TestEqual(TEXT("站起：基准放低 48 cm，偏移多 48 cm"), Toggle.GetOffset(), BeforeToggle + 48.0);

	// 地面上胶囊同时挪了，基准在世界里没动：不用平滑
	FGinkgoCrouchMeshBlend Ground;
	Ground.Absorb(0.0);
	TestFalse(TEXT("地面上：基准没跳，不用更新"), Ground.IsActive());

	// ω 是 0：不平滑，一步收回
	FGinkgoCrouchMeshBlend Off;
	Off.Absorb(48.0);
	Off.Step(0.0, 1.0 / 60.0);
	TestEqual(TEXT("ω 是 0：直接收回"), Off.GetOffset(), 0.0);
	TestFalse(TEXT("ω 是 0：不用更新"), Off.IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoAccelerateTest, "CaseGinkgo.Movement.Math.Accelerate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoAccelerateTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	const FVector Forward(1.0, 0.0, 0.0);
	const FVector Right(0.0, 1.0, 0.0);

	// 限制投影，不限制大小：顺着输入方向已经比上限快时，不减速
	FVector V(800.0, 0.0, 0.0);
	Accelerate(V, Forward, 500.f, 100.f);
	TestEqual(TEXT("顺向超速时不减速"), V.X, 800.0);

	V = FVector::ZeroVector;
	Accelerate(V, Forward, 500.f, 100.f);
	TestEqual(TEXT("一步最多加 AccelStep"), V.X, 100.0);

	V = FVector(450.0, 0.0, 0.0);
	Accelerate(V, Forward, 500.f, 100.f);
	TestEqual(TEXT("投影加到上限为止"), V.X, 500.0);

	V = FVector(500.0, 0.0, 0.0);
	Accelerate(V, -Forward, 500.f, 100.f);
	TestEqual(TEXT("反向输入会减速"), V.X, 400.0);

	// 空中扫射：输入与速度垂直时投影为 0，总能加满空中上限，速度大小因此增加
	const float AirSpeedCap = 57.15f;
	V = FVector(500.0, 0.0, 0.0);
	Accelerate(V, Right, AirSpeedCap, 10.f * 500.f / 60.f);
	TestEqual(TEXT("垂直输入加满空中上限"), V.Y, double(AirSpeedCap));
	TestTrue(TEXT("垂直输入让速度大小增加"), V.Size() > 500.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFrictionTest, "CaseGinkgo.Movement.Math.Friction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFrictionTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	const float Friction = 4.f;
	const float StopSpeed = 190.5f;

	FVector V(600.0, 0.0, 0.0);
	ApplyFriction(V, Friction, StopSpeed, 0.1f);
	TestEqual(TEXT("高于停止速度时按速度算"), V.X, 600.0 - 600.0 * 4.0 * 0.1, 1e-3);

	V = FVector(100.0, 0.0, 0.0);
	ApplyFriction(V, Friction, StopSpeed, 0.1f);
	TestEqual(TEXT("低于停止速度时按停止速度算"), V.X, 100.0 - 190.5 * 4.0 * 0.1, 1e-3);

	V = FVector(10.0, 0.0, 0.0);
	ApplyFriction(V, Friction, StopSpeed, 0.1f);
	TestEqual(TEXT("减到 0 为止，不反向"), V.X, 0.0);

	V = FVector(300.0, 400.0, 0.0);
	ApplyFriction(V, Friction, StopSpeed, 0.05f);
	TestTrue(TEXT("方向不变"), V.GetSafeNormal().Equals(FVector(0.6, 0.8, 0.0), 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoJumpVelocityTest, "CaseGinkgo.Movement.Math.JumpVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoJumpVelocityTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	const float Gravity = 1143.f;
	const float Height = 108.6f;
	const float Velocity = JumpVelocityForHeight(Height, Gravity);
	// 上抛的最高点 v² / 2g 应等于给定高度
	TestEqual(TEXT("最高点等于跳跃高度"), Velocity * Velocity / (2.f * Gravity), Height, 1e-3f);
	TestEqual(TEXT("高度为 0 时不起跳"), JumpVelocityForHeight(0.f, Gravity), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoGroundSteeringTest, "CaseGinkgo.Movement.Math.GroundSteering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoGroundSteeringTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	// 默认配置：跑速 250 u/s，60 fps
	const float WishSpeed = 476.25f;
	const float GroundAccel = 10.f;
	const float Friction = 4.f;
	const float StopSpeed = 190.5f;
	const float DeltaTime = 1.f / 60.f;
	const float AccelStep = GroundAccel * WishSpeed * DeltaTime;
	const FVector ForwardRight = FVector(1.0, 1.0, 0.0).GetSafeNormal();
	const FVector ForwardLeft = FVector(1.0, -1.0, 0.0).GetSafeNormal();

	// 已经以跑速直跑，之后按住 W、每 0.1 秒在 D 与 A 之间切换，持续 3 秒；与 CalcGroundVelocity 相同的顺序：先摩擦再加速
	const auto Wiggle = [&](const bool bGroundLimit)
	{
		FVector V(WishSpeed, 0.0, 0.0);
		double MaxSpeed = 0.0;
		for (int32 Frame = 0; Frame < 180; ++Frame)
		{
			const FVector& WishDir = (Frame / 6) % 2 == 0 ? ForwardRight : ForwardLeft;
			ApplyFriction(V, Friction, StopSpeed, DeltaTime);
			if (bGroundLimit)
			{
				AccelerateOnGround(V, WishDir, WishSpeed, AccelStep);
			}
			else
			{
				Accelerate(V, WishDir, WishSpeed, AccelStep);
			}
			MaxSpeed = FMath::Max(MaxSpeed, V.Size());
		}
		return MaxSpeed;
	};
	// 只用投影规则时，旧速度垂直于新 wishdir 的分量不受限制，转向时速度会超过跑速——这是要修的问题
	TestTrue(TEXT("只用投影规则时交替按 A / D 会超过跑速"), Wiggle(false) > WishSpeed * 1.1);
	TestTrue(TEXT("地面加速下交替按 A / D 不超过跑速"), Wiggle(true) <= WishSpeed + 1e-3);

	// 更快的来速不被截断：落地免摩擦窗口里不摩擦，垂直输入只转向，速度大小不变
	FVector V(WishSpeed * 2.0, 0.0, 0.0);
	AccelerateOnGround(V, FVector(0.0, 1.0, 0.0), WishSpeed, AccelStep);
	TestEqual(TEXT("更快的来速保持大小"), V.Size(), WishSpeed * 2.0, 1e-3);
	TestTrue(TEXT("垂直输入让方向转向输入"), V.Y > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoLurchTest, "CaseGinkgo.Movement.Math.Lurch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoLurchTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	const FVector Right(0.0, 1.0, 0.0);
	const double Speed = 762.0;
	const double Up = 300.0;
	const auto Turned = [](const FVector& V) { return FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)); };

	// k = 1：大小不变，方向完全换过去；竖直速度不动
	FVector V(Speed, 0.0, Up);
	Lurch(V, Right, 1.f);
	TestEqual(TEXT("k = 1 时水平速度完全转到输入方向"), FVector(V.X, V.Y, 0.0), Right * Speed, 1e-6f);
	TestEqual(TEXT("竖直速度不变"), V.Z, Up);

	// 中间值落在弦上：向侧面按（90°）时转过 atan(k / (1 − k))，剩 √(k² + (1 − k)²)
	for (const float K : {0.25f, 0.5f, 0.8f})
	{
		V = FVector(Speed, 0.0, Up);
		Lurch(V, Right, K);
		// 速度是 double，k 在里面按 double 乘
		const double Kd = K;
		TestEqual(*FString::Printf(TEXT("k = %.2f 时 90° 转过的角度"), K), Turned(V), FMath::RadiansToDegrees(FMath::Atan2(Kd, 1.0 - Kd)), 1e-6);
		TestEqual(*FString::Printf(TEXT("k = %.2f 时 90° 剩下的速度"), K), V.Size2D(), Speed * FMath::Sqrt(Kd * Kd + (1.0 - Kd) * (1.0 - Kd)), 1e-6);
	}
	// 默认的满强度 0.8：90° 转过约 76°，剩约 82%（UGinkgoLurchWindow::Strength 的注释）
	V = FVector(Speed, 0.0, 0.0);
	Lurch(V, Right, 0.8f);
	TestEqual(TEXT("0.8 时 90° 转过约 76°"), Turned(V), 76.0, 0.1);
	TestEqual(TEXT("0.8 时 90° 剩约 82%"), V.Size2D() / Speed, 0.8246, 1e-4);

	// 往回按（180°）时反向，剩 |2k − 1|
	V = FVector(Speed, 0.0, 0.0);
	Lurch(V, FVector(-1.0, 0.0, 0.0), 0.8f);
	TestEqual(TEXT("180° 时反向，剩 |2k − 1|"), V.X, -Speed * (2.0 * double(0.8f) - 1.0), 1e-6);

	// 顺着速度按、或者水平速度为 0 时什么也不改
	V = FVector(Speed, 0.0, Up);
	Lurch(V, FVector(1.0, 0.0, 0.0), 0.8f);
	TestEqual(TEXT("顺着速度按不变"), V, FVector(Speed, 0.0, Up), 1e-9f);
	V = FVector(0.0, 0.0, Up);
	Lurch(V, Right, 0.8f);
	TestEqual(TEXT("没有水平速度时不变"), V, FVector(0.0, 0.0, Up), 1e-9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSpeedScaleMathTest, "CaseGinkgo.Movement.Math.SpeedScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSpeedScaleMathTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;

	// 量化：1 正好是 255、解回来正好是 1（徒手的手感一点不变）；超出 0–1 的夹回来，改过的客户端只能让自己更慢；一格不到 0.4%
	TestEqual(TEXT("1 是 255"), static_cast<int32>(QuantizeSpeedScale(1.f)), 255);
	TestEqual(TEXT("255 解回来正好是 1"), UnquantizeSpeedScale(255), 1.f);
	TestEqual(TEXT("0 是 0"), static_cast<int32>(QuantizeSpeedScale(0.f)), 0);
	TestEqual(TEXT("大于 1 夹回来"), static_cast<int32>(QuantizeSpeedScale(1.5f)), 255);
	TestEqual(TEXT("负的夹回来"), static_cast<int32>(QuantizeSpeedScale(-0.5f)), 0);
	TestEqual(TEXT("98k 的持枪 0.82"), UnquantizeSpeedScale(QuantizeSpeedScale(0.82f)), 0.82f, 1.f / 510.f);
	TestEqual(TEXT("98k 的开镜 0.4"), UnquantizeSpeedScale(QuantizeSpeedScale(0.4f)), 0.4f, 1.f / 510.f);

	// wishspeed（Docs/Design/Weapons.md 的 98k 那张表）：冲刺不乘开镜，蹲与跑都乘
	constexpr float Run = 476.25f, Sprint = 762.f, Crouch = 158.75f, Held = 0.82f, Ads = 0.4f;
	TestEqual(TEXT("徒手跑"), GroundWishSpeed(Run, Sprint, Crouch, false, false, 1.f, 1.f), Run);
	TestEqual(TEXT("徒手冲刺"), GroundWishSpeed(Run, Sprint, Crouch, false, true, 1.f, 1.f), Sprint);
	TestEqual(TEXT("徒手蹲"), GroundWishSpeed(Run, Sprint, Crouch, true, false, 1.f, 1.f), Crouch);
	TestEqual(TEXT("拿着跑"), GroundWishSpeed(Run, Sprint, Crouch, false, false, Held, 1.f), Run * Held, 1e-3f);
	TestEqual(TEXT("拿着冲刺"), GroundWishSpeed(Run, Sprint, Crouch, false, true, Held, 1.f), Sprint * Held, 1e-3f);
	TestEqual(TEXT("拿着蹲"), GroundWishSpeed(Run, Sprint, Crouch, true, false, Held, 1.f), Crouch * Held, 1e-3f);
	TestEqual(TEXT("开镜跑"), GroundWishSpeed(Run, Sprint, Crouch, false, false, Held, Ads), Run * Held * Ads, 1e-3f);
	TestEqual(TEXT("开镜蹲"), GroundWishSpeed(Run, Sprint, Crouch, true, false, Held, Ads), Crouch * Held * Ads, 1e-3f);
	TestEqual(TEXT("冲刺不乘开镜"), GroundWishSpeed(Run, Sprint, Crouch, false, true, Held, Ads), Sprint * Held, 1e-3f);
	TestEqual(TEXT("蹲着按冲刺还是蹲的"), GroundWishSpeed(Run, Sprint, Crouch, true, true, Held, Ads), Crouch * Held * Ads, 1e-3f);

	// 低速：同 CalcGroundVelocity 的一步。加速按 GroundAccelMinWishSpeed 起算时拿着枪蹲着开镜走得起来，按 wishspeed 算时只在原地蹭一帧的加速
	constexpr float Accel = 10.f, Friction = 4.f, Stop = 190.5f, MinWish = 100.f;
	const float WishLow = GroundWishSpeed(Run, Sprint, Crouch, true, false, 209.f / 255.f, 102.f / 255.f);
	for (const float Fps : {30.f, 60.f, 144.f})
	{
		const float Dt = 1.f / Fps;
		FVector Floored = FVector::ZeroVector;
		FVector Source = FVector::ZeroVector;
		for (int32 Frame = 0; Frame < FMath::RoundToInt(Fps); ++Frame)
		{
			ApplyFriction(Floored, Friction, Stop, Dt);
			AccelerateOnGround(Floored, FVector::ForwardVector, WishLow, Accel * FMath::Max(WishLow, MinWish) * Dt);
			ApplyFriction(Source, Friction, Stop, Dt);
			AccelerateOnGround(Source, FVector::ForwardVector, WishLow, Accel * WishLow * Dt);
		}
		TestEqual(*FString::Printf(TEXT("%.0f fps：1 s 以后到 wishspeed"), Fps), Floored.X, static_cast<double>(WishLow), 1e-3);
		TestEqual(*FString::Printf(TEXT("%.0f fps：不起算时只有一帧的加速"), Fps), Source.X, static_cast<double>(Accel * WishLow * Dt), 1e-3);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoDirectionKeysMathTest, "CaseGinkgo.Movement.Math.DirectionKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoDirectionKeysMathTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;

	// 打包成 4 位，九种朝向都原样解出来；超出 −1 到 1 的按符号取，不认识的值当作没有
	for (int32 X = -1; X <= 1; ++X)
	{
		for (int32 Y = -1; Y <= 1; ++Y)
		{
			const uint8 Packed = PackDirectionKeys(FIntPoint(X, Y));
			TestTrue(*FString::Printf(TEXT("(%d, %d) 打包后在 4 位以内"), X, Y), Packed < 16);
			TestEqual(*FString::Printf(TEXT("(%d, %d) 原样解出"), X, Y), UnpackDirectionKeys(Packed), FIntPoint(X, Y));
		}
	}
	TestEqual(TEXT("超出范围按符号取"), UnpackDirectionKeys(PackDirectionKeys(FIntPoint(5, -3))), FIntPoint(1, -1));
	TestEqual(TEXT("不认识的值当作没有"), UnpackDirectionKeys(3 | (3 << 2)), FIntPoint::ZeroValue);

	// 转到世界：同 AGinkgoCharacter::Move，Y 是视角前方、X 是右方；斜着按是单位向量；没按为零
	TestEqual(TEXT("视角朝 +X 时向右是 +Y"), DirectionKeysToWorld(FIntPoint(1, 0), 0.0), FVector(0.0, 1.0, 0.0), 1e-12f);
	TestEqual(TEXT("视角朝 +X 时向前是 +X"), DirectionKeysToWorld(FIntPoint(0, 1), 0.0), FVector(1.0, 0.0, 0.0), 1e-12f);
	TestEqual(TEXT("斜着按是单位向量"), DirectionKeysToWorld(FIntPoint(-1, 1), 0.0), FVector(1.0, -1.0, 0.0) / FMath::Sqrt(2.0), 1e-12f);
	TestEqual(TEXT("没按是零"), DirectionKeysToWorld(FIntPoint::ZeroValue, 37.0), FVector::ZeroVector, 0.f);
	TestEqual(TEXT("视角朝 +Y 时向前是 +Y"), DirectionKeysToWorld(FIntPoint(0, 1), 90.0), FVector(0.0, 1.0, 0.0), 1e-6f);

	// 视角先按网络上的 16 位压一遍：服务器收到的是压过的，与客户端用的原值算出来逐位相同；绕一整圈也相同
	const double RawYaw = 37.12345;
	const double NetYaw = FRotator::DecompressAxisFromShort(FRotator::CompressAxisToShort(RawYaw));
	TestTrue(TEXT("原值与压过的视角算出逐位相同的方向"), DirectionKeysToWorld(FIntPoint(1, 1), RawYaw) == DirectionKeysToWorld(FIntPoint(1, 1), NetYaw));
	TestTrue(TEXT("差一整圈的视角算出逐位相同的方向"), DirectionKeysToWorld(FIntPoint(1, 1), RawYaw - 360.0) == DirectionKeysToWorld(FIntPoint(1, 1), NetYaw));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoFacingYawTest, "CaseGinkgo.Movement.Math.FacingYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoFacingYawTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Movement;
	// 平时面向视角，不看速度；在地面上冲刺时面向水平速度，不限方向
	const FVector Backward(-500.0, 0.0, 0.0);
	TestEqual(TEXT("不冲刺时面向视角"), ComputeFacingYaw(false, Backward, 30.0, 50.0), 30.0);
	TestEqual(TEXT("向后冲刺就转过身去"), FMath::Abs(ComputeFacingYaw(true, Backward, 30.0, 50.0)), 180.0, 1e-9);
	TestEqual(TEXT("向右冲刺面向 +Y"), ComputeFacingYaw(true, FVector(0.0, 300.0, 0.0), 0.0, 50.0), 90.0, 1e-9);
	TestEqual(TEXT("只看水平速度"), ComputeFacingYaw(true, FVector(0.0, 300.0, -2000.0), 0.0, 50.0), 90.0, 1e-9);
	// 还没快起来（从静止起步的第一帧之前、急停的最后一段）方向不可信，面向视角；门槛只看水平速度
	TestEqual(TEXT("比门槛慢时面向视角"), ComputeFacingYaw(true, FVector(40.0, 0.0, 0.0), 30.0, 50.0), 30.0);
	TestEqual(TEXT("竖直速度不算进门槛"), ComputeFacingYaw(true, FVector(0.0, 40.0, -2000.0), 30.0, 50.0), 30.0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

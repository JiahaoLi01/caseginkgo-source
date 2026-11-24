#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "TraversalTestScene.h"
#include "Movement/GinkgoMovementConfig.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalSpeedReachTest, "CaseGinkgo.Movement.Traversal.SpeedReachCurve", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalSpeedReachTest::RunTest(const FString& Parameters)
{
	auto* Config = NewObject<UGinkgoMovementConfig>();
	TestEqual(TEXT("静止前缘接受 50 cm"), Config->GetTraversalReach(0.f), 50.f);
	TestEqual(TEXT("跑速前缘接受 100 cm"), Config->GetTraversalReach(Config->RunSpeed), 100.f);
	TestEqual(TEXT("冲刺前缘接受 125 cm"), Config->GetTraversalReach(Config->SprintSpeed), 125.f);
	TestEqual(TEXT("静止到跑速线性插值"), Config->GetTraversalReach(Config->RunSpeed * .5f), 75.f);
	TestEqual(TEXT("跑速到冲刺线性插值"), Config->GetTraversalReach((Config->RunSpeed + Config->SprintSpeed) * .5f), 112.5f);
	TestEqual(TEXT("超速仍受 125 cm 上限约束"), Config->GetTraversalReach(Config->SprintSpeed * 2.f), 125.f);
	TestEqual(TEXT("负输入按静止处理"), Config->GetTraversalReach(-100.f), 50.f);
	Config->TraversalReachBySpeed.GetRichCurve()->Reset();
	Config->Traversal.Reach = 60.f;
	TestEqual(TEXT("清空曲线回退基础距离"), Config->GetTraversalReach(Config->SprintSpeed), 60.f);
	Config->Traversal.Reach = 200.f;
	TestEqual(TEXT("基础距离也不能绕过上限"), Config->GetTraversalReach(0.f), 125.f);
	Config->TraversalReachBySpeed.ExternalCurve = NewObject<UCurveFloat>(Config);
	Config->TraversalReachBySpeed.ExternalCurve->FloatCurve.AddKey(0.f, 80.f);
	TestEqual(TEXT("支持外部曲线资产"), Config->GetTraversalReach(0.f), 80.f);
	Config->TraversalReachBySpeed.ExternalCurve->FloatCurve.AddKey(100.f, 200.f);
	TestEqual(TEXT("编辑曲线后的超限输出被截断"), Config->GetTraversalReach(100.f), 125.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalDirectionTest, "CaseGinkgo.Movement.Traversal.CameraProjection", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalDirectionTest::RunTest(const FString& Parameters)
{
	for (const double Pitch : {-90., -89., -30., 0., 35., 89., 90.})
	{
		TestTrue(TEXT("任意俯仰都沿相机 XY 投影，垂直时使用相机偏航"),
			Ginkgo::Traversal::CameraDirection(FRotator(Pitch, 90., 0.)).Equals(FVector::RightVector, .00001));
	}
	GinkgoTraversalTests::FScene Scene;
	Scene.Floor();
	Scene.Wall(100., 200.);
	Scene.Query.Forward = Ginkgo::Traversal::CameraDirection(FRotator(65., 0., 20.));
	TestTrue(TEXT("相机朝向墙时能找到平台，俯仰滚转不改变查询起点"), Scene.Detect().IsValid());
	Scene.Query.Forward = Ginkgo::Traversal::CameraDirection(FRotator(0., 90., 0.));
	TestFalse(TEXT("相机转开后不沿原角色前方找墙"), Scene.Detect().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalClassificationTest, "CaseGinkgo.Movement.Traversal.Classification", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalClassificationTest::RunTest(const FString& Parameters)
{
	for (const auto& Case : TArray<FVector>{FVector(100., 40., 1.), FVector(100., 200., 2.), FVector(220., 200., 3.)})
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Wall(Case.X, Case.Y);
		const auto Result = Scene.Detect();
		TestTrue(FString::Printf(TEXT("高度 %.0f 厚度 %.0f 可通过：%s"), Case.X, Case.Y, Ginkgo::Traversal::Describe(Result.Failure)), Result.IsValid());
		TestEqual(TEXT("薄矮墙翻越、厚矮台攀上、高台高攀"), static_cast<int32>(Result.Kind), static_cast<int32>(Case.Z));
		TestTrue(TEXT("双手保存有效且独立的支撑锚点"), Result.LeftHand.Component.IsValid() && Result.RightHand.Component.IsValid());
		TestTrue(TEXT("左手位于相机左侧"), Result.LeftHand.Position.Y < Result.RightHand.Position.Y);
		TestTrue(TEXT("局部锚点可还原世界接触点"), Result.Ledge.Component->GetComponentTransform().TransformPosition(Result.Ledge.LocalPosition).Equals(Result.Ledge.Position, .001));
		TestTrue(TEXT("查询量在硬预算内"), Result.QueryCount > 0 && Result.QueryCount <= 1024);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalLimitsTest, "CaseGinkgo.Movement.Traversal.HeightAndDepthLimits", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalLimitsTest::RunTest(const FString& Parameters)
{
	for (const auto& Case : TArray<FVector2D>{FVector2D(40., 200.), FVector2D(260., 200.), FVector2D(140., 40.), FVector2D(100., 74.)})
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Wall(Case.X, Case.Y);
		TestFalse(TEXT("可跨台阶、过高墙、高薄墙和超出翻越厚度但不足站立宽度均不启动"), Scene.Detect().IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalHandsTest, "CaseGinkgo.Movement.Traversal.HandSupport", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalHandsTest::RunTest(const FString& Parameters)
{
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Wall(100., 40., 12.);
		TestEqual(TEXT("窄柱不能只凭中央命中通过"), Scene.Detect().Failure, EGinkgoTraversalFailure::Hands);
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Box(FVector(170., -50., 50.), FVector(100., 50., 50.));
		Scene.Box(FVector(170., 50., 50.), FVector(100., 50., 50.));
		const auto Result = Scene.Detect();
		TestTrue(TEXT("拼接平台的不同组件可以共同支撑"), Result.IsValid());
		TestNotEqual(TEXT("双手没有强制绑定同一组件"), Result.LeftHand.Component.Get(), Result.RightHand.Component.Get());
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Box(FVector(170., -50., 50.), FVector(100., 50., 50.));
		Scene.Box(FVector(170., 50., 70.), FVector(100., 50., 70.));
		TestFalse(TEXT("左右手支撑高差过大时拒绝"), Scene.Detect().IsValid());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalClearanceTest, "CaseGinkgo.Movement.Traversal.ClearanceAndLanding", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalClearanceTest::RunTest(const FString& Parameters)
{
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Box(FVector(-100., 0., -10.), FVector(150., 500., 10.));
		Scene.Wall(100., 40.);
		TestEqual(TEXT("墙后悬崖不算有效翻越"), Scene.Detect().Failure, EGinkgoTraversalFailure::NoLanding);
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Wall(100., 40.);
		Scene.Box(FVector(170., 0., 100.), FVector(20., 100., 100.));
		TestFalse(TEXT("背后第二堵墙阻止翻越，不寻找更远落点"), Scene.Detect().IsValid());
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Wall(100., 200.);
		Scene.Box(FVector(0., 0., 235.), FVector(35., 100., 5.));
		TestEqual(TEXT("起终点可站，但抬升中撞梁仍然拒绝"), Scene.Detect().Failure, EGinkgoTraversalFailure::PathBlocked);
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Wall(100., 200.);
		Scene.Query.Feet.X = 60.;
		TestEqual(TEXT("起始胶囊已穿墙不接受碰撞退穿法线"), Scene.Detect().Failure, EGinkgoTraversalFailure::InitialOverlap);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalSupportPolicyTest, "CaseGinkgo.Movement.Traversal.SupportPolicy", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalSupportPolicyTest::RunTest(const FString& Parameters)
{
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		auto* Wall = Scene.Box(FVector(170., 0., 50.), FVector(100., 100., 50.), EComponentMobility::Movable);
		TestEqual(TEXT("第一阶段不使用移动支撑"), Scene.Detect().Failure, EGinkgoTraversalFailure::UnsupportedSurface);
		TestNotNull(TEXT("拒绝不会销毁支撑"), Wall);
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		auto* Wall = Scene.Wall(100., 200.);
		Wall->ComponentTags.Add(TEXT("Traversal.Blocked"));
		TestEqual(TEXT("可单独禁止表面攀爬"), Scene.Detect().Failure, EGinkgoTraversalFailure::UnsupportedSurface);
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Wall(100., 200.);
		Scene.Query.CurrentHalfHeight = 44.;
		TestTrue(TEXT("蹲伏发起仍按站立目标验证"), Scene.Detect().IsValid());
		Scene.Settings.TraceChannel = ECC_Visibility;
		auto* Obstruction = Scene.Box(FVector(0., 0., 235.), FVector(35., 100., 5.));
		Obstruction->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		TestEqual(TEXT("候选通道忽略的梁仍按角色碰撞阻挡路径"), Scene.Detect().Failure, EGinkgoTraversalFailure::PathBlocked);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalInvalidInputTest, "CaseGinkgo.Movement.Traversal.InvalidInput", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalInvalidInputTest::RunTest(const FString& Parameters)
{
	GinkgoTraversalTests::FScene Scene;
	Scene.Settings.ScanStep = 0.f;
	TestEqual(TEXT("零采样间隔不能死循环"), Scene.Detect().Failure, EGinkgoTraversalFailure::InvalidInput);
	Scene.Settings = {};
	Scene.Query.Forward = FVector::ZeroVector;
	TestEqual(TEXT("零方向不回退到角色朝向"), Scene.Detect().Failure, EGinkgoTraversalFailure::InvalidInput);
	Scene.Query.Forward = FVector::ForwardVector;
	Scene.Query.StandingHalfHeight = 1.f;
	TestEqual(TEXT("无效胶囊不发物理查询"), Scene.Detect().QueryCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalCharacterTest, "CaseGinkgo.Movement.Traversal.CharacterSnapshot", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalCharacterTest::RunTest(const FString& Parameters)
{
	GinkgoTraversalTests::FScene Scene;
	Scene.Floor();
	Scene.Wall(100., 200.);
	auto* Character = Scene.World->SpawnActor<AGinkgoCharacter>(FVector(0., 0., 90.), FRotator(0., 180., 0.));
	if (!TestNotNull(TEXT("测试角色"), Character)) { return false; }
	auto* Movement = Character->GetGinkgoMovement();
	Movement->SetMovementMode(MOVE_Walking);
	const FTransform Before = Character->GetActorTransform();
	const auto* Traversal = Character->GetTraversal();
	TestTrue(TEXT("真实角色背对墙时，仍能按相机方向找平台"), Traversal->Evaluate(FRotator(35., 0., 0.)).IsValid());
	TestFalse(TEXT("相机转开不沿角色朝向补查"), Traversal->Evaluate(FRotator(0., 90., 0.)).IsValid());
	TestTrue(TEXT("检测不修改位置或朝向"), Character->GetActorTransform().Equals(Before));
	TestEqual(TEXT("检测不修改移动模式"), Movement->MovementMode.GetValue(), MOVE_Walking);
	Movement->SetMovementMode(MOVE_Falling);
	TestEqual(TEXT("当前阶段不把空中抓边与地面高度规则混用"), Traversal->Evaluate(FRotator::ZeroRotator).Failure, EGinkgoTraversalFailure::InvalidState);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalLouversTest, "CaseGinkgo.Movement.Traversal.HorizontalLouvers", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalLouversTest::RunTest(const FString& Parameters)
{
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Louvers();
		const auto Result = Scene.Detect();
		TestEqual(TEXT("下部镂空但顶梁连续的矮栏杆允许翻越"), Result.Kind, EGinkgoTraversalKind::Vault);
		TestTrue(TEXT("抓最高横条，不把中间空隙当作墙顶"), FMath::IsNearlyEqual(Result.Height, 100.f, .01f));
		TestTrue(TEXT("双手均在最高横条"), FMath::IsNearlyEqual(Result.LeftHand.Position.Z, 100., .01)
			&& FMath::IsNearlyEqual(Result.RightHand.Position.Z, 100., .01));
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Louvers();
		Scene.Box(FVector(170., 0., 95.), FVector(100., 150., 5.));
		TestEqual(TEXT("镂空墙增加足够宽的连续顶板后可攀上"), Scene.Detect().Kind, EGinkgoTraversalKind::Mantle);
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Louvers();
		Scene.Box(FVector(90., 0., 215.), FVector(20., 150., 5.));
		const auto Result = Scene.Detect();
		TestEqual(TEXT("高薄栏杆不能借用下层矮横条触发翻越"), Result.Failure, EGinkgoTraversalFailure::NoSupport);
		TestTrue(TEXT("分层墙仍按最上方边沿高度分类"), FMath::IsNearlyEqual(Result.Height, 220.f, .01f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalLayeredLouversTest, "CaseGinkgo.Movement.Traversal.LayeredLouvers", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalLayeredLouversTest::RunTest(const FString& Parameters)
{
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Louvers();
		for (const double Height : {75., 115., 155.})
		{
			Scene.Box(FVector(130., 0., Height), FVector(10., 150., 5.));
		}
		const auto Result = Scene.Detect();
		TestEqual(TEXT("前后错层的百叶墙不能穿过后排横条落地"), Result.Failure, EGinkgoTraversalFailure::NoLanding);
		TestTrue(TEXT("失败不会无限向更远地面搜索"), Result.QueryCount <= 1024);
	}
	for (const double Ceiling : {280., 350.})
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Louvers();
		Scene.Box(FVector(90., 0., Ceiling), FVector(20., 150., 5.));
		const auto Result = Scene.Detect();
		if (Ceiling < 300.)
		{
			TestEqual(TEXT("下探起点以上的横条仍会阻挡身体经过"), Result.Failure, EGinkgoTraversalFailure::PathBlocked);
		}
		else
		{
			TestEqual(TEXT("横条高于整个身体路径时仍可翻越"), Result.Kind, EGinkgoTraversalKind::Vault);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalLouverHandsTest, "CaseGinkgo.Movement.Traversal.LouverHandGaps", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalLouverHandsTest::RunTest(const FString& Parameters)
{
	for (const double Height : {100., 220.})
	{
		for (const double Side : {-1., 1.})
		{
			GinkgoTraversalTests::FScene Scene;
			Scene.Floor();
			Scene.Box(FVector(90., 0., 55.), FVector(20., 150., 5.));
			Scene.Box(FVector(90., Side * 70., Height - 5.), FVector(20., 80., 5.));
			Scene.Box(FVector(90., Side * -90., Height - 5.), FVector(20., 60., 5.));
			if (Side > 0. || Height > 125.)
			{
				TestEqual(TEXT("左手缺口一律拒绝，高障碍还要求右手支撑"), Scene.Detect().Failure, EGinkgoTraversalFailure::Hands);
			}
			else { TestEqual(TEXT("右手缺口不妨碍左手短翻越"), Scene.Detect().Kind, EGinkgoTraversalKind::Vault); }
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalTopGapsTest, "CaseGinkgo.Movement.Traversal.SlottedTopSupport", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalTopGapsTest::RunTest(const FString& Parameters)
{
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.SlottedSurface(170., 100., 100.);
		TestEqual(TEXT("中心与外圈有支撑，但脚底内部两条 12 cm 空缝不能被跳过"), Scene.Detect().Failure, EGinkgoTraversalFailure::NoSupport);
	}
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.SlottedSurface(170., 100., 100.);
		for (const double Side : {-1., 1.})
		{
			Scene.Box(FVector(170., Side * 12., 95.), FVector(100., 6., 5.));
		}
		TestEqual(TEXT("用独立组件填平空缝后允许站立，不要求整个平台属于同一组件"), Scene.Detect().Kind, EGinkgoTraversalKind::Mantle);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalLandingGapsTest, "CaseGinkgo.Movement.Traversal.SlottedLandingSupport", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalLandingGapsTest::RunTest(const FString& Parameters)
{
	for (const bool bFillGaps : {false, true})
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Box(FVector(-220., 0., -10.), FVector(280., 500., 10.));
		Scene.Wall(100., 40.);
		Scene.SlottedSurface(270., 200., 0.);
		if (bFillGaps)
		{
			for (const double Side : {-1., 1.})
			{
				Scene.Box(FVector(270., Side * 12., -5.), FVector(200., 6., 5.));
			}
		}
		const auto Result = Scene.Detect();
		if (bFillGaps) { TestEqual(TEXT("连续拼接落地平台允许翻越"), Result.Kind, EGinkgoTraversalKind::Vault); }
		else { TestEqual(TEXT("墙后落点也必须检查脚底内部空缝"), Result.Failure, EGinkgoTraversalFailure::NoLanding); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoTraversalTiltedLouversTest, "CaseGinkgo.Movement.Traversal.TiltedLouvers", GinkgoTraversalTests::Flags)
bool FGinkgoTraversalTiltedLouversTest::RunTest(const FString& Parameters)
{
	for (const double Pitch : {-20., 20., 65.})
	{
		GinkgoTraversalTests::FScene Scene;
		Scene.Floor();
		Scene.Louvers(Pitch);
		const auto Result = Scene.Detect();
		const FString Context = FString::Printf(TEXT("叶片俯仰 %.0f 度，结果 %s（高 %.1f / 深 %.1f / 后缘 %d）"),
			Pitch, Ginkgo::Traversal::Describe(Result.Failure), Result.Height, Result.Depth, Result.bFoundBackEdge);
		if (FMath::Abs(Pitch) < 45.)
		{
			TestEqual(Context + TEXT("：平缓顶面仍可翻越"), Result.Kind, EGinkgoTraversalKind::Vault);
			if (Result.IsValid() && TestEqual(TEXT("成功方案保存完整三段路径"), Result.Path.Num(), 4))
			{
				const double Radians = FMath::DegreesToRadians(FMath::Abs(Pitch));
				const double HighestCorner = 95. + 20. * FMath::Sin(Radians) + 5. * FMath::Cos(Radians);
				TestTrue(TEXT("抬升后胶囊底部高于实际倾斜叶片最高角，而非只高于抓点"),
					Result.Path[1].Z - Scene.Query.StandingHalfHeight > HighestCorner);
			}
		}
		else { TestFalse(Context + TEXT("：过陡叶片不能当作支撑顶面"), Result.IsValid()); }
	}
	return true;
}

#endif

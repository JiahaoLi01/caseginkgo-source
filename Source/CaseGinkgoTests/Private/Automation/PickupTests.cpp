#include "Animation/GinkgoAnimInstance.h"  // Ginkgo::Animation::ConstrainReachBearing、SoftLimitReach
#include "Interaction/GinkgoInteractionMath.h"
#include "Interaction/GinkgoPickup.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPickupRangeTest, "CaseGinkgo.Interaction.Pickup.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPickupRangeTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Interaction;
	// 引擎默认的胶囊（半径 34、半高 88）站在地上：中心离地 88，中轴线段从离地 34 到 142
	FVector Bottom, Top;
	GetCapsuleSegment(FVector(0.0, 0.0, 88.0), FVector::UpVector, 88.0, 34.0, Bottom, Top);

	// 和中心一样高时两种量法一样
	TestEqual(TEXT("齐腰：量到中心与量到线段一样"), InteractDistance(FVector(120.0, 0.0, 88.0), Bottom, Top, true), 120.0, 1e-4);
	TestEqual(TEXT("齐腰：量到线段"), InteractDistance(FVector(120.0, 0.0, 88.0), Bottom, Top, false), 120.0, 1e-4);
	// 拾取的球（半径 150）：地上、头顶的东西比按线段量远，水平距离一样时按线段够得着、按球够不着
	const FVector Floor(130.0, 0.0, 5.0);
	TestEqual(TEXT("地上：量到中心"), InteractDistance(Floor, Bottom, Top, true), FMath::Sqrt(130.0 * 130.0 + 83.0 * 83.0), 1e-4);
	TestTrue(TEXT("地上 130 cm：按线段在 150 以内"), IsWithinRange(InteractDistance(Floor, Bottom, Top, false), 150.0, false, 0.0));
	TestFalse(TEXT("地上 130 cm：按球出了 150"), IsWithinRange(InteractDistance(Floor, Bottom, Top, true), 150.0, false, 0.0));
	// 高处的架子（离地 2.2 m）：量到线段的上端（离地 142）127 cm，量到中心（离地 88）166 cm
	const FVector Shelf(0.0, 100.0, 220.0);
	TestEqual(TEXT("高处的架子：量到线段"), InteractDistance(Shelf, Bottom, Top, false), FMath::Sqrt(100.0 * 100.0 + 78.0 * 78.0), 1e-4);
	TestEqual(TEXT("高处的架子：量到中心"), InteractDistance(Shelf, Bottom, Top, true), FMath::Sqrt(100.0 * 100.0 + 132.0 * 132.0), 1e-4);
	TestFalse(TEXT("高处的架子：按球出了 150"), IsWithinRange(InteractDistance(Shelf, Bottom, Top, true), 150.0, false, 0.0));

	// 蹲下：中心降到离地 40，地上的东西近了
	GetCapsuleSegment(FVector(0.0, 0.0, 40.0), FVector::UpVector, 40.0, 34.0, Bottom, Top);
	TestTrue(TEXT("蹲下：地上 130 cm 回到 150 以内"), IsWithinRange(InteractDistance(Floor, Bottom, Top, true), 150.0, false, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPickupHandTest, "CaseGinkgo.Interaction.Pickup.ChooseHand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPickupHandTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Pickup;
	// 角色面朝 +X：左肩在 -Y，右肩在 +Y（UE 左手系：+Y 在右边）
	const FVector Left(0.0, -16.0, 131.0);
	const FVector Right(0.0, 16.0, 131.0);
	TestEqual(TEXT("都能用，东西在右前方：右手"), ChooseHand(true, true, Left, Right, FVector(40.0, 30.0, 100.0)).GetValue(), EGinkgoHand::Right);
	TestEqual(TEXT("都能用，东西在左前方：左手"), ChooseHand(true, true, Left, Right, FVector(40.0, -30.0, 100.0)).GetValue(), EGinkgoHand::Left);
	TestEqual(TEXT("都能用，正前方一样近：右手"), ChooseHand(true, true, Left, Right, FVector(50.0, 0.0, 100.0)).GetValue(), EGinkgoHand::Right);
	TestEqual(TEXT("右臂占着（拿着枪），东西在右前方：还是左手"), ChooseHand(true, false, Left, Right, FVector(40.0, 30.0, 100.0)).GetValue(), EGinkgoHand::Left);
	TestEqual(TEXT("左臂占着，东西在左前方：右手"), ChooseHand(false, true, Left, Right, FVector(40.0, -30.0, 100.0)).GetValue(), EGinkgoHand::Right);
	TestFalse(TEXT("两条胳膊都占着：没有"), ChooseHand(false, false, Left, Right, FVector(40.0, 30.0, 100.0)).IsSet());
	// GASP 的待机是侧身的：左肩比右肩靠前 12 cm，正前方的东西离左肩近
	TestEqual(TEXT("侧身站着，正前方：左肩近，左手"), ChooseHand(true, true, FVector(12.0, -16.0, 131.0), Right, FVector(50.0, 0.0, 100.0)).GetValue(), EGinkgoHand::Left);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPickupArmReachTest, "CaseGinkgo.Interaction.Pickup.ArmReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPickupArmReachTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Pickup;
	const FVector Forward(1.0, 0.0, 0.0);
	const FVector Centre(0.0, 16.0, 131.0);
	// 椭球的三个半轴：往前后 74、往两边 76、往上下 66；左手的身体中间是 +Y
	const FVector Radii(74.0, 76.0, 66.0);
	TestTrue(TEXT("往前正好 74"), IsWithinArmReach(Centre, Centre + FVector(74.0, 0.0, 0.0), Forward, EGinkgoHand::Left, Radii, 0.0));
	TestFalse(TEXT("往前差 1 cm"), IsWithinArmReach(Centre, Centre + FVector(75.0, 0.0, 0.0), Forward, EGinkgoHand::Left, Radii, 0.0));
	TestTrue(TEXT("往下 66 够得着，往下 67 够不着"), IsWithinArmReach(Centre, Centre + FVector(0.0, 0.0, -66.0), Forward, EGinkgoHand::Left, Radii, 0.0)
		&& !IsWithinArmReach(Centre, Centre + FVector(0.0, 0.0, -67.0), Forward, EGinkgoHand::Left, Radii, 0.0));
	TestTrue(TEXT("往下 67 加 5 cm 余量就够得着"), IsWithinArmReach(Centre, Centre + FVector(0.0, 0.0, -67.0), Forward, EGinkgoHand::Left, Radii, 5.0));
	TestTrue(TEXT("往外 76 正好"), FMath::IsNearlyEqual(ArmReachRatio(Centre, Centre + FVector(0.0, -76.0, 0.0), Forward, EGinkgoHand::Left, Radii, 0.0), 1.0, 1e-9));
	TestTrue(TEXT("两个半轴各取一半：(37, 0, -33) 在 0.71 倍处"), FMath::IsNearlyEqual(ArmReachRatio(Centre, Centre + FVector(37.0, 0.0, -33.0), Forward, EGinkgoHand::Left, Radii, 0.0),
		FMath::Sqrt(0.5), 1e-9));
	// 右手的身体中间是 -Y：同一个往右 76 的点，对右手是往身体中间
	TestTrue(TEXT("右手镜像"), FMath::IsNearlyEqual(ArmReachRatio(Centre, Centre + FVector(0.0, 76.0, 0.0), Forward, EGinkgoHand::Right, Radii, 0.0), 1.0, 1e-9));
	// 角色转了：面朝 +Y 时往前是 +Y
	TestTrue(TEXT("面朝 +Y"), FMath::IsNearlyEqual(ArmReachRatio(Centre, Centre + FVector(0.0, 74.0, 0.0), FVector(0.0, 1.0, 0.0), EGinkgoHand::Left, Radii, 0.0), 1.0, 1e-9));
	// 站着够不到地面：肩关节 1.31 m，地上的盒子差得多
	TestFalse(TEXT("地上的盒子"), IsWithinArmReach(Centre, FVector(30.0, 16.0, 3.2), Forward, EGinkgoHand::Left, Radii, 5.0));

	// 球心挪 Offset（角色的坐标：往前、往身体中间、往上）：左手的身体中间是角色的右方，右手的是左方；角色转了也跟着转，朝向带竖直分量时只取水平的
	const FVector LeftShoulder(4.8, -19.2, 131.3);
	const FVector Offset(-8.0, 14.0, 2.0);
	TestTrue(TEXT("左手：往后 8、往右 14、往上 2"), ReachCentre(LeftShoulder, FVector(1.0, 0.0, 0.0), EGinkgoHand::Left, Offset).Equals(FVector(-3.2, -5.2, 133.3), 1e-6));
	TestTrue(TEXT("右手的身体中间往左"), ReachCentre(FVector(-9.5, 12.9, 131.6), FVector(1.0, 0.0, 0.0), EGinkgoHand::Right, Offset).Equals(FVector(-17.5, -1.1, 133.6), 1e-6));
	TestTrue(TEXT("面朝 +Y 时右方是 -X"), ReachCentre(LeftShoulder, FVector(0.0, 1.0, 0.0), EGinkgoHand::Left, Offset).Equals(FVector(-9.2, -27.2, 133.3), 1e-6));
	TestTrue(TEXT("朝向带竖直分量"), ReachCentre(LeftShoulder, FVector(2.0, 0.0, 1.0), EGinkgoHand::Left, Offset).Equals(FVector(-3.2, -5.2, 133.3), 1e-6));
	TestTrue(TEXT("0 就是肩关节"), ReachCentre(LeftShoulder, FVector(1.0, 0.0, 0.0), EGinkgoHand::Left, FVector::ZeroVector).Equals(LeftShoulder, 1e-6));
	// 实测（pickup.py --reach-sweep，手转向东西、上半身参与并前倾，挂点离抓的点不超过 0.5 cm）：正前方 66、正左 63、正下 68、
	// 前上 61、身前往右 78 cm；拟合的椭球（心 (-8, 12, -6)，半轴 74、76、66）比各方向都靠里或差不到 0.9 cm，这里取各方向往里一点的距离
	const FVector ReachOffset(-8.0, 12.0, -6.0);
	const FVector LeftCentre = ReachCentre(LeftShoulder, Forward, EGinkgoHand::Left, ReachOffset);
	TestTrue(TEXT("正前方 64 cm"), IsWithinArmReach(LeftCentre, LeftShoulder + FVector(64.0, 0.0, 0.0), Forward, EGinkgoHand::Left, Radii, 0.0));
	TestTrue(TEXT("正左 62 cm"), IsWithinArmReach(LeftCentre, LeftShoulder + FVector(0.0, -62.0, 0.0), Forward, EGinkgoHand::Left, Radii, 0.0));
	TestFalse(TEXT("正左 70 cm"), IsWithinArmReach(LeftCentre, LeftShoulder + FVector(0.0, -70.0, 0.0), Forward, EGinkgoHand::Left, Radii, 0.0));
	TestTrue(TEXT("身前往右 74 cm"), IsWithinArmReach(LeftCentre, LeftShoulder + FVector(74.0, 74.0, 0.0) * UE_INV_SQRT_2, Forward, EGinkgoHand::Left, Radii, 0.0));
	// 你第一次截图那种：拿着枪，左边腰高（离左肩 49 cm）
	TestTrue(TEXT("左边腰高的盒子"), IsWithinArmReach(LeftCentre, FVector(10.0, -55.0, 98.2), Forward, EGinkgoHand::Left, Radii, 0.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPickupBeyondReachTest, "CaseGinkgo.Interaction.Pickup.BeyondReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPickupBeyondReachTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Pickup;
	const FVector Forward(1.0, 0.0, 0.0);
	const FVector Centre(0.0, 16.0, 131.0);
	const FVector Radii(74.0, 76.0, 66.0);
	const double Margin = 5.0;

	// 走开：开始时够得着的照旧（出了椭球再加余量才中止）
	TestTrue(TEXT("开始时够得着：半轴不变"), WalkAwayRadii(Radii, 0.8).Equals(Radii, 1e-9));
	TestTrue(TEXT("正好在椭球上：半轴不变"), WalkAwayRadii(Radii, 1.0).Equals(Radii, 1e-9));
	TestFalse(TEXT("开始时够得着：往前出了 74 + 5 就中止"),
		IsWithinArmReach(Centre, Centre + FVector(80.0, 0.0, 0.0), Forward, EGinkgoHand::Left, WalkAwayRadii(Radii, 0.8), Margin));
	// 开始时就够不着（地上的盒子在正下方 99 cm，椭球的 1.5 倍处）：放大到正好过开始的那个点，站着不动不中止，再远出余量才中止
	const FVector Floor = Centre + FVector(0.0, 0.0, -99.0);
	const double StartRatio = ArmReachRatio(Centre, Floor, Forward, EGinkgoHand::Left, Radii, 0.0);
	TestTrue(TEXT("地上的盒子在 1.5 倍处"), FMath::IsNearlyEqual(StartRatio, 1.5, 1e-9));
	const FVector Walk = WalkAwayRadii(Radii, StartRatio);
	TestTrue(TEXT("开始时就够不着：放大 1.5 倍"), Walk.Equals(Radii * 1.5, 1e-9));
	TestTrue(TEXT("站着不动：不中止"), IsWithinArmReach(Centre, Floor, Forward, EGinkgoHand::Left, Walk, Margin));
	TestTrue(TEXT("远出 4 cm：不中止"), IsWithinArmReach(Centre, Floor + FVector(0.0, 0.0, -4.0), Forward, EGinkgoHand::Left, Walk, Margin));
	TestFalse(TEXT("远出 6 cm：中止"), IsWithinArmReach(Centre, Floor + FVector(0.0, 0.0, -6.0), Forward, EGinkgoHand::Left, Walk, Margin));
	TestTrue(TEXT("走近了：不中止"), IsWithinArmReach(Centre, Floor + FVector(0.0, 0.0, 30.0), Forward, EGinkgoHand::Left, Walk, Margin));

	// 够不着时手的目标拉回：限制以内不变，过限制时不折，再远也不超过限制加余量
	using Ginkgo::Animation::SoftLimitReach;
	TestEqual(TEXT("限制以内不变"), SoftLimitReach(50.0, 56.0, 5.0), 50.0, 1e-12);
	TestEqual(TEXT("正好在限制上"), SoftLimitReach(56.0, 56.0, 5.0), 56.0, 1e-12);
	TestEqual(TEXT("过限制时斜率还是 1"), SoftLimitReach(56.001, 56.0, 5.0), 56.001, 1e-6);
	TestTrue(TEXT("越远越大"), SoftLimitReach(70.0, 56.0, 5.0) > SoftLimitReach(60.0, 56.0, 5.0));
	TestTrue(TEXT("地上的（170 cm）不超过限制加余量"), SoftLimitReach(170.0, 56.0, 5.0) <= 61.0);
	TestEqual(TEXT("没有余量时就是限制"), SoftLimitReach(170.0, 56.0, 0.0), 56.0, 1e-12);

	// 挂到手上：朝向照此刻的，东西上抓的点落在手里的挂点上
	const FVector GraspPoint(0.0, 0.0, 6.4);   // 东西的坐标：盒子顶上
	const FVector ItemInHand(7.82, -5.95, 0.87);  // 手骨的坐标：片段的挂点
	const FTransform Far(FQuat(FVector(0.3, -0.5, 0.8).GetSafeNormal(), 0.7), FVector(42.0, -18.0, 7.0), FVector(1.0));
	const FTransform Snapped = SnapToHand(Far, GraspPoint, ItemInHand);
	TestTrue(TEXT("够不着（差 45 cm）：抓的点落在挂点上"), Snapped.TransformPosition(GraspPoint).Equals(ItemInHand, 1e-6));
	TestTrue(TEXT("朝向不变"), Snapped.GetRotation().Equals(Far.GetRotation(), 1e-9));
	// 缩放的东西也一样：抓的点按缩放后的位置对上
	const FTransform Scaled(FQuat::Identity, FVector(30.0, 0.0, 0.0), FVector(2.0));
	TestTrue(TEXT("缩放过的东西"), SnapToHand(Scaled, GraspPoint, ItemInHand).TransformPosition(GraspPoint).Equals(ItemInHand, 1e-6));
	// 够得着时两点已经对上：不动
	FTransform Reached = Far;
	Reached.SetTranslation(Far.GetTranslation() + ItemInHand - Far.TransformPosition(GraspPoint));
	TestTrue(TEXT("够得着：不动"), SnapToHand(Reached, GraspPoint, ItemInHand).Equals(Reached, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPickupReachBearingTest, "CaseGinkgo.Interaction.Pickup.ReachBearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPickupReachBearingTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Animation::ConstrainReachBearing;
	// 角色朝 +X，右方是 +Y；肩关节在 (0, -20, 130)，东西离肩关节水平 50 cm、比肩低 30 cm
	const FVector Forward(1.0, 0.0, 0.0);
	const FVector Shoulder(0.0, -20.0, 130.0);
	const auto At = [&Shoulder](const double Bearing) { return Shoulder + FRotator(0.0, Bearing, 0.0).Vector() * 50.0 + FVector(0.0, 0.0, -30.0); };
	FVector Out;

	// 左手：往外（左）100°、往身体中间（右）45° 以内不动
	TestEqual(TEXT("正前方：系数 1"), ConstrainReachBearing(At(0.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out), 1.f);
	TestTrue(TEXT("正前方：不动"), Out.Equals(At(0.0), 1e-9));
	TestEqual(TEXT("右前 30°：系数 1"), ConstrainReachBearing(At(30.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out), 1.f);
	TestEqual(TEXT("左后 100°（正好在边上）：系数 1"), ConstrainReachBearing(At(-100.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out), 1.f);

	// 另一只肩膀那边：右前 60° 转回 45°，距离、高度不变；超出 15°，系数 1 − smoothstep(15/45)
	const float Across = ConstrainReachBearing(At(60.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out);
	TestTrue(TEXT("右前 60°：转回 45°"), Out.Equals(At(45.0), 1e-6));
	TestEqual(TEXT("右前 60°：系数"), Across, 1.f - FMath::SmoothStep(0.f, 45.f, 15.f), 1e-5f);
	TestEqual(TEXT("右前 60°：高度不变"), Out.Z, At(60.0).Z, 1e-9);

	// 身后：左后 150° 转回 100°，超出 50°（多过 45°），不再伸过去；正后方离往外那条边近（80°，往里那条 135°）
	TestEqual(TEXT("左后 150°：系数 0"), ConstrainReachBearing(At(-150.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out), 0.f);
	TestTrue(TEXT("左后 150°：转回 100°"), Out.Equals(At(-100.0), 1e-6));
	ConstrainReachBearing(At(180.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out);
	TestTrue(TEXT("正后方：转回往外那条边"), Out.Equals(At(-100.0), 1e-6));
	// 两条边在身后的中间（+152.5°）两边都已经是 0：东西扫过身后时手不会从一条边跳到另一条
	TestEqual(TEXT("身后两条边中间的左边：系数 0"), ConstrainReachBearing(At(150.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out), 0.f);
	TestEqual(TEXT("身后两条边中间的右边：系数 0"), ConstrainReachBearing(At(155.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out), 0.f);

	// 右手镜像：往身体中间是左边
	TestEqual(TEXT("右手，左前 30°：系数 1"), ConstrainReachBearing(At(-30.0), Shoulder, Forward, EGinkgoHand::Right, 100.f, 45.f, 45.f, Out), 1.f);
	ConstrainReachBearing(At(-60.0), Shoulder, Forward, EGinkgoHand::Right, 100.f, 45.f, 45.f, Out);
	TestTrue(TEXT("右手，左前 60°：转回左前 45°"), Out.Equals(At(-45.0), 1e-6));
	TestEqual(TEXT("右手，右后 100°：系数 1"), ConstrainReachBearing(At(100.0), Shoulder, Forward, EGinkgoHand::Right, 100.f, 45.f, 45.f, Out), 1.f);

	// 角色转了：朝 +Y 时，世界里的 +X 方向是它的左边（往外 90°）
	TestEqual(TEXT("朝 +Y：左边 90° 在扇形里"), ConstrainReachBearing(Shoulder + FVector(50.0, 0.0, 0.0), Shoulder, FVector(0.0, 1.0, 0.0), EGinkgoHand::Left, 100.f, 45.f, 45.f,
		Out), 1.f);
	// 在肩关节正上方、正下方：没有方位，不动
	TestEqual(TEXT("正下方：系数 1"), ConstrainReachBearing(Shoulder + FVector(0.0, 0.0, -60.0), Shoulder, Forward, EGinkgoHand::Left, 100.f, 45.f, 45.f, Out), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoPickupGrantTest, "CaseGinkgo.Interaction.Pickup.Grant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoPickupGrantTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Pickup::FGrant;
	{
		// 单机：本地先到时刻（不放动画时就是开始），再当场接受
		FGrant Grant;
		TestFalse(TEXT("先到时刻：还没接受，不给"), Grant.MarkDue());
		TestTrue(TEXT("接受的那一下给"), Grant.MarkAccepted());
		TestFalse(TEXT("再接受一次不再给"), Grant.MarkAccepted());
		TestFalse(TEXT("再到一次时刻也不再给"), Grant.MarkDue());
		TestTrue(TEXT("记成给过"), Grant.IsGranted());
	}
	{
		// 远端客户端：回复比 Stow 早到，到时刻的那一下给
		FGrant Grant;
		TestFalse(TEXT("先接受：还没到时刻，不给"), Grant.MarkAccepted());
		TestTrue(TEXT("到时刻（或中止）的那一下给"), Grant.MarkDue());
		TestFalse(TEXT("中止跟在 Stow 后面：不再给"), Grant.MarkDue());
	}
	{
		// 被拒绝：不管先后都不给
		FGrant Grant;
		Grant.MarkDue();
		Grant.MarkRejected();
		TestFalse(TEXT("被拒绝以后再接受也不给"), Grant.MarkAccepted());
		TestFalse(TEXT("没给"), Grant.IsGranted());
		FGrant Late;
		Late.MarkRejected();
		TestFalse(TEXT("被拒绝以后才到时刻：不给"), Late.MarkDue());
		TestFalse(TEXT("没给"), Late.IsGranted());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS

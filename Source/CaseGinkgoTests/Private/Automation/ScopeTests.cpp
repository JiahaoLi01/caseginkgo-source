#include "Control/GinkgoInputMath.h"
#include "Control/GinkgoInputSettings.h"
#include "Misc/AutomationTest.h"
#include "Weapon/GinkgoScope.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * 第一人称开镜的瞄具（Docs/Design/Weapons.md「第一人称开镜：ZF39 瞄具」）：光学的式子（Ginkgo::Scope）。
 * 后处理材质（Scripts/Assets/scope_composite.hlsl）照同样的式子算，这里比的是 C++ 的那一份
 */
namespace GinkgoScopeTests
{
	const EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** 枪摆在一个随便的地方、朝一个随便的方向：瞄具跟着它 */
	FTransform MakeGun()
	{
		return FTransform(FRotator(-7.5, 123.0, 4.0), FVector(120.0, -340.0, 95.0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeOpticsTest, "CaseGinkgo.Weapon.Scope.Optics", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeOpticsTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	const FGinkgoScope Scope;
	const FGinkgoScopeOptics& Optics = Scope.Optics;
	// 真的 Ziel 4/S（对照）：100 m 上 11.25 m 是 6.44°，4 倍时目镜里 25.4°
	FGinkgoScopeOptics Real = Optics;
	Real.TrueFieldOfView = 2.f * static_cast<float>(FMath::RadiansToDegrees(FMath::Atan(5.625 / 100.0)));
	TestEqual(TEXT("真的：100 m 上 11.25 m 是 6.44°"), static_cast<double>(Real.TrueFieldOfView), 6.44, 0.005);
	TestEqual(TEXT("真的：目镜里 25.4°"), 2.0 * FMath::RadiansToDegrees(FMath::Atan(GetApparentTanHalfField(Real))), 25.4, 0.1);
	// 用的（你选的镜内做大）：倍率照真的 4 倍，真实视场 9°，目镜里 35°；FOV 90（4:3 的水平视场，竖直半视场的正切 0.75）时镜内的像占屏幕高的 42%
	TestEqual(TEXT("倍率 4"), Optics.Magnification, 4.f);
	TestEqual(TEXT("真实视场 9°"), Optics.TrueFieldOfView, 9.f);
	TestEqual(TEXT("目镜里半视场的正切"), GetApparentTanHalfField(Optics), 4.0 * FMath::Tan(FMath::DegreesToRadians(4.5)), 1e-9);
	TestEqual(TEXT("目镜里的视场 35°"), 2.0 * FMath::RadiansToDegrees(FMath::Atan(GetApparentTanHalfField(Optics))), 35.0, 0.1);
	TestEqual(TEXT("FOV 90 时占屏幕高的 42%"), GetApparentTanHalfField(Optics) / 0.75, 0.42, 0.005);
	// 眼睛（出瞳后面一点）看目镜里的像正好填满模型的目镜（眼睛到目镜 × 正切 = 通光半径，差不到 1 mm）
	TestEqual(TEXT("像正好填满目镜"), (Optics.EyeRelief + Scope.Handling.EyeBehind) * GetApparentTanHalfField(Optics), static_cast<double>(Scope.Mount.EyepieceRadius), 0.1);
	// 眼睛在出瞳后面：站稳了视场的边进出瞳的地方还在出瞳的实心那一圈里（整个视场都亮）
	TestTrue(TEXT("站稳了整个视场都亮"), Scope.Handling.EyeBehind * GetApparentTanHalfField(Optics) < 0.5 * Optics.ExitPupil - Optics.EyeBoxSoftness);
	// 出瞳 8 mm = 物镜 32 mm ÷ 4
	TestEqual(TEXT("出瞳 8 mm"), Optics.ExitPupil, 0.8f);
	// 一号分划：100 m 上空 70 cm、遮住 20 cm
	TestEqual(TEXT("横柱之间 7 mrad"), Scope.Reticle.Gap, 7.f);
	TestEqual(TEXT("柱宽 2 mrad"), Scope.Reticle.PostWidth, 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeSurroundResolutionTest, "CaseGinkgo.Weapon.Scope.SurroundResolution", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeSurroundResolutionTest::RunTest(const FString& Parameters)
{
	FGinkgoScopeSurround Surround;
	TestEqual(TEXT("完全开镜时按镜外配置降低分辨率"), Surround.GetRenderScale(1.f, 1.f), Surround.RenderScale);
	TestEqual(TEXT("进镜途中随模糊混合"), Surround.GetRenderScale(0.5f, 1.f), (1.f + Surround.RenderScale) * 0.5f);
	TestEqual(TEXT("退出和眼睛完全让开都恢复原比例"), Surround.GetRenderScale(0.f, 1.f), 1.f);
	TestEqual(TEXT("拉栓让开目镜"), Surround.GetRenderScale(1.f, 0.f), 1.f);
	Surround.BlurRadius = 0.f;
	TestEqual(TEXT("只模糊近处枪身时保留镜外街景细节"), Surround.GetRenderScale(1.f, 1.f), 1.f);
	Surround.BlurRadius = 5.f;
	Surround.RenderScale = 1.f;
	TestEqual(TEXT("配置为 1 时始终使用原画质比例"), Surround.GetRenderScale(1.f, 1.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeDistortionTest, "CaseGinkgo.Weapon.Scope.Distortion", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeDistortionTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	const FGinkgoScopeOptics Optics;
	const double Start = Optics.DistortionStart;
	TestEqual(TEXT("开始之内不变"), DistortRadius(0.5, Optics), 0.5, 1e-12);
	TestEqual(TEXT("开始处不变"), DistortRadius(Start, Optics), Start, 1e-12);
	TestEqual(TEXT("边上多看到 k(1 − ρ0)"), DistortRadius(1.0, Optics), 1.0 + Optics.DistortionStrength * (1.0 - Start), 1e-9);
	// 开始处一阶导数是 1（接得上，不折）；往外单调增、越来越陡（越靠边压得越紧）
	const double Step = 1e-6;
	TestEqual(TEXT("开始处导数是 1"), (DistortRadius(Start + Step, Optics) - DistortRadius(Start, Optics)) / Step, 1.0, 1e-4);
	double Last = 0.0;
	double LastSlope = 0.0;
	bool bMonotonic = true;
	bool bSteepening = true;
	for (int32 Index = 1; Index <= 200; ++Index)
	{
		const double Rho = Index / 200.0;
		const double Value = DistortRadius(Rho, Optics);
		const double Slope = (DistortRadius(Rho + Step, Optics) - Value) / Step;
		bMonotonic &= Value > Last;
		bSteepening &= Slope >= LastSlope - 1e-6;
		Last = Value;
		LastSlope = Slope;
	}
	TestTrue(TEXT("单调增"), bMonotonic);
	TestTrue(TEXT("越往外越陡"), bSteepening);
	// 反过来：显示在哪
	double Worst = 0.0;
	for (int32 Index = 0; Index <= 300; ++Index)
	{
		const double Rho = Index / 250.0;
		for (const int32 Channel : {-1, 0, 1})
		{
			Worst = FMath::Max(Worst, FMath::Abs(UndistortRadius(DistortRadius(Rho, Optics, Channel), Optics, Channel) - Rho));
		}
	}
	TestTrue(FString::Printf(TEXT("反函数来回差不到 1e-9（%.3g）"), Worst), Worst < 1e-9);
	// 色差：红的压得最多、蓝的最少；开始之内三色一样
	TestTrue(TEXT("边上红 > 绿 > 蓝"), DistortRadius(1.0, Optics, -1) > DistortRadius(1.0, Optics, 0) && DistortRadius(1.0, Optics, 0) > DistortRadius(1.0, Optics, 1));
	TestEqual(TEXT("开始之内三色一样"), DistortRadius(0.6, Optics, -1), DistortRadius(0.6, Optics, 1), 1e-12);
	// 拍的视场的余量盖得住最外沿（红的最多）
	const FGinkgoScope Scope;
	TestTrue(TEXT("拍的视场盖得住畸变的最外沿"), DistortRadius(1.0, Optics, -1) <= Scope.CaptureMargin);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeColorTest, "CaseGinkgo.Weapon.Scope.Color", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeColorTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	const FGinkgoScope Scope;
	const FGinkgoScopeOptics& Optics = Scope.Optics;
	const double Edge = GetApparentTanHalfField(Optics);
	// 1080p、FOV 90（4:3 的水平视场，竖直半视场的正切 0.75）：屏幕中心附近一个正切是 720 px
	const double PixelsPerTan = 540.0 / 0.75;
	// 绿是基准：方向不变（分划的尖、所见即所打都照绿的）；光轴上、光线从出瞳的中心过时三色一样（尖上没有彩边）
	const FVector2D Apparent(0.21, -0.13);
	const FVector2D Pupil(0.3, 0.2);
	TestTrue(TEXT("绿的不变"), ChannelApparent(Apparent, Pupil, Optics, 0).Equals(Apparent, 1e-12));
	TestTrue(TEXT("光轴上、出瞳中心：红的不偏"), ChannelApparent(FVector2D::ZeroVector, FVector2D::ZeroVector, Optics, -1).IsNearlyZero(1e-12));
	TestTrue(TEXT("光轴上、出瞳中心：蓝的不偏"), ChannelApparent(FVector2D::ZeroVector, FVector2D::ZeroVector, Optics, 1).IsNearlyZero(1e-12));

	// 倍率色差：蓝的像大（显示在同一处的取得靠里），红的取得靠外；视场的边上红、蓝取的差 倍率色差 × 半径
	const FVector2D AtEdge(Edge, 0.0);
	const FVector2D Red = ChannelApparent(AtEdge, FVector2D::ZeroVector, Optics, -1);
	const FVector2D Blue = ChannelApparent(AtEdge, FVector2D::ZeroVector, Optics, 1);
	TestTrue(TEXT("红的取得靠外、蓝的靠里"), Red.X > Edge && Blue.X < Edge);
	TestEqual(TEXT("边上红、蓝差 倍率色差 × 半径"), Red.X - Blue.X, Optics.LateralColor * Edge, 1e-12);
	const double EdgePixels = (Red.X - Blue.X) * PixelsPerTan;
	TestTrue(FString::Printf(TEXT("视场的边上红、蓝差 1–4 px：看得出、不过分（%.2f px）"), EdgePixels), EdgePixels > 1.0 && EdgePixels < 4.0);

	// 眼位色差：光线从出瞳偏 0.4 cm 的地方过，红、蓝的方向差 眼位色差 × 0.4；蓝的往那一点那边偏，所以取的地方往反方向挪
	const FVector2D Off(0.4, 0.0);
	const FVector2D RedOff = ChannelApparent(FVector2D::ZeroVector, Off, Optics, -1);
	const FVector2D BlueOff = ChannelApparent(FVector2D::ZeroVector, Off, Optics, 1);
	TestEqual(TEXT("偏 0.4 cm 时红、蓝差 眼位色差 × 0.4"), RedOff.X - BlueOff.X, Optics.EyeColor * 0.4, 1e-12);
	TestTrue(TEXT("蓝的取的地方往反方向挪"), BlueOff.X < 0.0 && RedOff.X > 0.0);
	const double OffPixels = (RedOff.X - BlueOff.X) * PixelsPerTan;
	TestTrue(FString::Printf(TEXT("偏 0.4 cm 时红、蓝差 0.5–3 px（%.2f px）"), OffPixels), OffPixels > 0.5 && OffPixels < 3.0);
	// 站稳了眼睛在出瞳后面：边上的光从出瞳靠外的地方过，眼位色差同倍率色差一个方向（蓝的大）
	const FVector2D Through = AtEdge * static_cast<double>(Scope.Handling.EyeBehind);
	TestTrue(TEXT("站稳了两种色差同向"), ChannelApparent(AtEdge, Through, Optics, -1).X > Red.X && ChannelApparent(AtEdge, Through, Optics, 1).X < Blue.X);

	// 出瞳的色差：眼睛在出瞳后面、往右偏 0.25 cm，看视场右边（月牙那一边）：红的出瞳离眼睛近，蓝的先暗，边上偏橙
	const FSightFrame Sight = MakeSightFrame(GinkgoScopeTests::MakeGun(), Scope);
	const FVector Eye = Sight.Eye - Sight.GetAxis() * Scope.Handling.EyeBehind + Sight.Rotation.RotateVector(FVector(0.0, 0.25, 0.0));
	const FVector Toward = Sight.Rotation.RotateVector(FVector(1.0, 0.9 * Edge, 0.0)).GetSafeNormal();
	const float VisibleRed = ExitPupilVisibility(Sight, Optics, Eye, Toward, -1);
	const float VisibleGreen = ExitPupilVisibility(Sight, Optics, Eye, Toward, 0);
	const float VisibleBlue = ExitPupilVisibility(Sight, Optics, Eye, Toward, 1);
	TestTrue(FString::Printf(TEXT("月牙的边上蓝的先暗（红 %.3f、绿 %.3f、蓝 %.3f）"), VisibleRed, VisibleGreen, VisibleBlue),
		VisibleRed > VisibleGreen && VisibleGreen > VisibleBlue && VisibleGreen > 0.f && VisibleGreen < 1.f);
	// 只在月牙的边上有：眼睛在出瞳的中心，哪个方向三色都亮
	TestEqual(TEXT("出瞳的中心：视场的边上蓝的也亮"), ExitPupilVisibility(Sight, Optics, Sight.Eye, Sight.Rotation.RotateVector(FVector(1.0, Edge, 0.0)).GetSafeNormal(), 1), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeReticleTest, "CaseGinkgo.Weapon.Scope.Reticle", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeReticleTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	const FGinkgoScopeReticle Reticle;
	// 一个像素 0.01 mrad：边上一个像素宽的过渡，里外取远一点的点
	const double Pixel = 0.01;
	const auto Cover = [&](const double X, const double Y) { return ReticleCoverage(FVector2D(X, Y), Reticle, Pixel); };
	// 竖柱的尖就是瞄准点：在边上（一半），往上一点就没有，往下一点在柱子里
	TestEqual(TEXT("尖在边上"), Cover(0.0, 0.0), 0.5f, 1e-3f);
	TestEqual(TEXT("尖上面是空的"), Cover(0.0, 0.1), 0.f);
	TestEqual(TEXT("尖下面是柱子"), Cover(0.0, -0.1), 1.f);
	// 收尖：尖长 3 mrad，往下 1.5 mrad 时柱子宽一半（半宽 0.5 mrad）
	TestEqual(TEXT("收尖的一半处，柱子里"), Cover(0.4, -1.5), 1.f);
	TestEqual(TEXT("收尖的一半处，柱子外"), Cover(0.6, -1.5), 0.f);
	// 全宽的竖柱：半宽 1 mrad
	TestEqual(TEXT("竖柱里"), Cover(0.9, -10.0), 1.f);
	TestEqual(TEXT("竖柱外"), Cover(1.1, -10.0), 0.f);
	// 两根横柱：空隙的一半 3.5 mrad，半宽 1 mrad，中线与尖同高
	TestEqual(TEXT("空隙里（尖的两边）"), Cover(2.0, 0.0), 0.f);
	TestEqual(TEXT("右边的横柱"), Cover(5.0, 0.0), 1.f);
	TestEqual(TEXT("左边的横柱"), Cover(-5.0, 0.0), 1.f);
	TestEqual(TEXT("横柱的上沿外"), Cover(5.0, 1.1), 0.f);
	TestEqual(TEXT("横柱的下沿里"), Cover(5.0, -0.9), 1.f);
	TestEqual(TEXT("横柱的内端在边上"), Cover(3.5, 0.0), 0.5f, 1e-3f);
	// 一路往外都是横柱（直到视场外）
	TestEqual(TEXT("视场的边上还是横柱"), Cover(60.0, 0.0), 1.f);
	// 横柱上方没有东西（一号分划上面是空的）
	TestEqual(TEXT("上面是空的"), Cover(0.0, 30.0), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeSightFrameTest, "CaseGinkgo.Weapon.Scope.SightFrame", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeSightFrameTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	const FGinkgoScope Scope;
	const FTransform Gun = GinkgoScopeTests::MakeGun();
	const FSightFrame Sight = MakeSightFrame(Gun, Scope);
	TestTrue(TEXT("光轴就是枪的 X 轴"), Sight.GetAxis().Equals(Gun.GetUnitAxis(EAxis::X), 1e-9));
	TestTrue(TEXT("目镜在安装的位置"), Sight.Eyepiece.Equals(Gun.TransformPosition(Scope.Mount.EyepieceCenter), 1e-6));
	TestEqual(TEXT("眼睛在目镜后面眼距那么远"), FVector::DotProduct(Sight.Eyepiece - Sight.Eye, Sight.GetAxis()), static_cast<double>(Scope.Optics.EyeRelief), 1e-6);
	TestEqual(TEXT("眼睛在光轴上"), FVector::CrossProduct(Sight.Eyepiece - Sight.Eye, Sight.GetAxis()).Size(), 0.0, 1e-6);
	TestEqual(TEXT("物镜在目镜前面"), FVector::DotProduct(Sight.Objective - Sight.Eyepiece, Sight.GetAxis()), static_cast<double>(Scope.Mount.ObjectiveDistance), 1e-6);
	// 98k 模型上的 ZF39：光轴比枪管高 5.6 cm（枪管在网格坐标的 Z 6.34，SK_Mauser98k.markers.json）
	TestEqual(TEXT("光轴比枪管高 5.6 cm"), Scope.Mount.EyepieceCenter.Z - 6.335, 5.6, 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeExitPupilTest, "CaseGinkgo.Weapon.Scope.ExitPupil", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeExitPupilTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	const FGinkgoScope Scope;
	const FGinkgoScopeOptics& Optics = Scope.Optics;
	const FSightFrame Sight = MakeSightFrame(GinkgoScopeTests::MakeGun(), Scope);
	const auto Direction = [&](const double TanRight, const double TanUp) { return Sight.Rotation.RotateVector(FVector(1.0, TanRight, TanUp)).GetSafeNormal(); };
	const double Edge = GetApparentTanHalfField(Optics);
	// 眼睛在出瞳的中心：哪个方向都看得到
	TestEqual(TEXT("出瞳的中心：光轴"), ExitPupilVisibility(Sight, Optics, Sight.Eye, Direction(0.0, 0.0)), 1.f);
	TestEqual(TEXT("出瞳的中心：视场的边"), ExitPupilVisibility(Sight, Optics, Sight.Eye, Direction(Edge, 0.0)), 1.f);
	TestEqual(TEXT("出瞳的中心：斜着到边"), ExitPupilVisibility(Sight, Optics, Sight.Eye, Direction(Edge * 0.7, -Edge * 0.7)), 1.f);
	// 往后退 5 cm：只剩中间一小块（tan θ' < 出瞳半径 ÷ 5 cm，软边的外沿是 0）
	const FVector Behind = Sight.Eye - Sight.GetAxis() * 5.0;
	const double Inner = (0.5 * Optics.ExitPupil - Optics.EyeBoxSoftness) / 5.0;
	const double Outer = (0.5 * Optics.ExitPupil + Optics.EyeBoxSoftness) / 5.0;
	TestEqual(TEXT("退后 5 cm：中间看得到"), ExitPupilVisibility(Sight, Optics, Behind, Direction(Inner * 0.9, 0.0)), 1.f);
	TestEqual(TEXT("退后 5 cm：外面黑了"), ExitPupilVisibility(Sight, Optics, Behind, Direction(Outer * 1.1, 0.0)), 0.f);
	TestTrue(TEXT("退后 5 cm：视场的边早就黑了"), Outer < Edge);
	// 往旁边偏 1 cm（比出瞳的半径加软边还多）：光轴方向是黑的
	const FVector Aside = Sight.Eye + Sight.Rotation.RotateVector(FVector(0.0, 1.0, 0.0));
	TestEqual(TEXT("偏 1 cm：黑的"), ExitPupilVisibility(Sight, Optics, Aside, Direction(0.0, 0.0)), 0.f);
	// 偏一点（在软边以内）：亮度在 0 和 1 之间，越偏越暗
	const FVector Slight = Sight.Eye + Sight.Rotation.RotateVector(FVector(0.0, 0.0, 0.4));
	const float Half = ExitPupilVisibility(Sight, Optics, Slight, Direction(0.0, 0.0));
	TestTrue(FString::Printf(TEXT("偏到出瞳的边：一半左右（%.3f）"), Half), Half > 0.3f && Half < 0.7f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeProjectionTest, "CaseGinkgo.Weapon.Scope.Projection", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeProjectionTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	const FGinkgoScope Scope;
	const FGinkgoScopeOptics& Optics = Scope.Optics;
	const FSightFrame Sight = MakeSightFrame(GinkgoScopeTests::MakeGun(), Scope);
	FVector Apparent;
	double Rho = 0.0;
	// 光轴上的点（从眼睛沿光轴打出去的那条线）就在分划的尖上：同一条线
	const FVector OnAxis = Sight.Eye + Sight.GetAxis() * 2500.0;
	TestTrue(TEXT("光轴上的点在视场里"), ProjectThroughScope(Sight, Optics, OnAxis, Apparent, Rho));
	TestEqual(TEXT("光轴上的点在镜内的中心"), Rho, 0.0, 1e-9);
	TestTrue(TEXT("光轴上的点看过去就是光轴的方向"), Apparent.Equals(Sight.GetAxis(), 1e-9));
	// 偏一点的点（畸变以内）：目镜出来的正切是真实的正切乘倍率
	const double TrueTan = 0.02;
	const FVector Off = Sight.Objective + Sight.Rotation.RotateVector(FVector(1.0, TrueTan, 0.0)) * 3000.0;
	TestTrue(TEXT("偏 0.02 的点在视场里"), ProjectThroughScope(Sight, Optics, Off, Apparent, Rho));
	const FVector Local = Sight.Rotation.UnrotateVector(Apparent);
	TestEqual(TEXT("放大倍率那么多"), Local.Y / Local.X, TrueTan * Optics.Magnification, 1e-9);
	TestEqual(TEXT("镜内的半径"), Rho, TrueTan * Optics.Magnification / GetApparentTanHalfField(Optics), 1e-9);
	// 畸变那一圈里：显示的地方取的正是这个点（材质按显示的地方取画面，两边对得上）
	const double EdgeTrue = GetApparentTanHalfField(Optics) / Optics.Magnification * DistortRadius(0.95, Optics);
	const FVector NearEdge = Sight.Objective + Sight.Rotation.RotateVector(FVector(1.0, 0.0, EdgeTrue)) * 3000.0;
	TestTrue(TEXT("边上的点在视场里"), ProjectThroughScope(Sight, Optics, NearEdge, Apparent, Rho));
	TestEqual(TEXT("边上的点显示在 0.95"), Rho, 0.95, 1e-9);
	// 视场外、身后：看不到
	const FVector Outside = Sight.Objective + Sight.Rotation.RotateVector(FVector(1.0, 0.2, 0.0)) * 3000.0;
	TestFalse(TEXT("视场外看不到"), ProjectThroughScope(Sight, Optics, Outside, Apparent, Rho));
	TestFalse(TEXT("身后看不到"), ProjectThroughScope(Sight, Optics, Sight.Objective - Sight.GetAxis() * 100.0, Apparent, Rho));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeHandlingTest, "CaseGinkgo.Weapon.Scope.Handling", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeHandlingTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	FGinkgoScope Scope;
	const FTransform Gun = GinkgoScopeTests::MakeGun();
	const FVector Pivot(-22.0, 0.0, 0.0);

	// 绕一点转：那一点不动，别的点离它一样远
	const FRotator Angles(5.0, -1.2, 3.0);
	const FTransform Turn = MakePivotRotation(Angles, Pivot);
	TestTrue(TEXT("枪托不动"), Turn.TransformPosition(Pivot).Equals(Pivot, 1e-9));
	TestEqual(TEXT("转的角度"), Turn.GetRotation().AngularDistance(Angles.Quaternion()), 0.0, 1e-9);
	const FVector Front(30.0, 0.0, 12.0);
	TestEqual(TEXT("别的点离枪托一样远"), (Turn.TransformPosition(Front) - Pivot).Size(), (Front - Pivot).Size(), 1e-9);

	// 眼睛：没有偏移时在出瞳后面 EyeBehind；有偏移时在去掉偏移的出瞳与真的出瞳之间（前后跟 EyeFollowAxial、上下左右跟 EyeFollow），再往后 EyeBehind
	const FSightFrame RestSight = MakeSightFrame(Gun, Scope);
	const FVector RestEye = RestSight.Eye;
	const FVector Behind = RestSight.GetAxis() * static_cast<double>(Scope.Handling.EyeBehind);
	FSightFrame Rest;
	TestTrue(TEXT("没有偏移：眼睛在出瞳后面"), ComputeFirstPersonEye(Gun, FTransform::Identity, Scope, &Rest).Equals(RestEye - Behind, 1e-9));
	// 像越肩开枪的那一下：绕枪托上扬 5°、往后顶 2.5 cm
	const FTransform Kick = MakePivotRotation(FRotator(5.0, 0.0, 0.0), Pivot) * FTransform(FVector(-2.5, 0.0, 0.0));
	const FTransform Kicked = Kick * Gun;
	const FVector KickedEye = MakeSightFrame(Kicked, Scope).Eye;
	for (const float Follow : {0.f, 0.5f, 1.f})
	{
		Scope.Handling.EyeFollow = Follow;
		Scope.Handling.EyeFollowAxial = Follow;
		const FVector Eye = ComputeFirstPersonEye(Kicked, Kick, Scope, &Rest);
		TestTrue(FString::Printf(TEXT("跟 %.1f：去掉偏移就是枪口对准以后的出瞳"), Follow), Rest.Eye.Equals(RestEye, 1e-6));
		TestTrue(FString::Printf(TEXT("前后、上下左右都跟 %.1f：眼睛在两个出瞳之间、往后 EyeBehind"), Follow), Eye.Equals(FMath::Lerp(RestEye, KickedEye, static_cast<double>(Follow)) - Behind, 1e-6));
	}
	// 前后、上下左右分开跟：沿光轴的那份跟 EyeFollowAxial，横着的跟 EyeFollow
	Scope.Handling.EyeFollow = 0.25f;
	Scope.Handling.EyeFollowAxial = 0.75f;
	const FVector Moved = KickedEye - RestEye;
	const FVector Along = RestSight.GetAxis() * FVector::DotProduct(Moved, RestSight.GetAxis());
	TestTrue(TEXT("前后、上下左右分开跟"), ComputeFirstPersonEye(Kicked, Kick, Scope).Equals(RestEye + Along * 0.75 + (Moved - Along) * 0.25 - Behind, 1e-6));
	// 越肩那么大的踢、眼睛照默认的跟：出瞳离开眼睛 0.8 cm 以上，镜内照着分划看是黑的；眼睛完全跟着枪时照样看得到
	Scope.Handling = FGinkgoScopeHandling();
	const FSightFrame KickedSight = MakeSightFrame(Kicked, Scope);
	const FVector FullEye = ComputeFirstPersonEye(Kicked, Kick, Scope);
	TestEqual(TEXT("越肩那么大的踢：照着分划看是黑的"), ExitPupilVisibility(KickedSight, Scope.Optics, FullEye, KickedSight.GetAxis()), 0.f);
	TestEqual(TEXT("眼睛完全跟着枪：看得到"), ExitPupilVisibility(KickedSight, Scope.Optics, KickedEye - Behind, KickedSight.GetAxis()), 1.f);
	// 第一人称的枪踢（KickScale）：照着分划还看得到，偏的那一边的边上暗（月牙），另一边亮
	{
		const double Scale = Scope.Handling.KickScale;
		const FTransform Small = MakePivotRotation(FRotator(5.0 * Scale, 0.0, 0.0), Pivot) * FTransform(FVector(-2.5 * Scale, 0.0, 0.0));
		const FSightFrame SmallSight = MakeSightFrame(Small * Gun, Scope);
		const FVector Eye = ComputeFirstPersonEye(Small * Gun, Small, Scope);
		const double EdgeTan = 0.95 * GetApparentTanHalfField(Scope.Optics);
		const auto Look = [&](const double TanUp) { return SmallSight.Rotation.RotateVector(FVector(1.0, 0.0, TanUp)).GetSafeNormal(); };
		const float Centre = ExitPupilVisibility(SmallSight, Scope.Optics, Eye, SmallSight.GetAxis());
		const float Top = ExitPupilVisibility(SmallSight, Scope.Optics, Eye, Look(EdgeTan));
		const float Bottom = ExitPupilVisibility(SmallSight, Scope.Optics, Eye, Look(-EdgeTan));
		TestTrue(FString::Printf(TEXT("第一人称的枪踢：中间看得到（%.3f），一边暗（%.3f、%.3f）、另一边亮"), Centre, Top, Bottom),
			Centre > 0.9f && FMath::Min(Top, Bottom) < 0.9f && FMath::Max(Top, Bottom) > 0.99f);
	}
	// 眼睛在出瞳后面，往右偏一点（0.3 cm）：往右看的视线越走越偏，右边的视场的边暗了；往左看的视线走向中心，左边还亮（月牙在偏的那一边）
	const double Edge = 0.95 * GetApparentTanHalfField(Scope.Optics);
	const FVector Aside = RestEye - Behind + RestSight.Rotation.RotateVector(FVector(0.0, 0.3, 0.0));
	const auto Toward = [&](const double TanRight) { return RestSight.Rotation.RotateVector(FVector(1.0, TanRight, 0.0)).GetSafeNormal(); };
	const float RightEdge = ExitPupilVisibility(RestSight, Scope.Optics, Aside, Toward(Edge));
	const float LeftEdge = ExitPupilVisibility(RestSight, Scope.Optics, Aside, Toward(-Edge));
	TestTrue(FString::Printf(TEXT("往右偏 0.3 cm：右边的边暗（%.3f）、左边亮（%.3f）"), RightEdge, LeftEdge), RightEdge < 0.6f && LeftEdge > 0.99f);

	// 走路的偏：站着不动没有；以参考速度横着走够久，偏航、侧倾就是"走路的偏"（晃关掉看）
	FGinkgoScopeHandling NoBob = Scope.Handling;
	NoBob.MoveBob = FVector2D::ZeroVector;
	NoBob.MoveBobShift = FVector2D::ZeroVector;
	{
		FMoveSwayState State;
		FRotator Still = FRotator(1.0, 1.0, 1.0);
		for (int32 Index = 0; Index < 120; ++Index)
		{
			Still = StepMoveSway(State, NoBob, FVector2D::ZeroVector, true, 1.0 / 60.0);
		}
		TestTrue(TEXT("站着不动不偏"), Still.IsNearlyZero(1e-12));
		FRotator Strafe;
		FVector Shift;
		for (int32 Index = 0; Index < 600; ++Index)
		{
			Strafe = StepMoveSway(State, NoBob, FVector2D(0.0, NoBob.MoveReferenceSpeed), true, 1.0 / 60.0, &Shift);
		}
		TestEqual(TEXT("往右走：枪口往左落"), Strafe.Yaw, static_cast<double>(NoBob.MoveLean.Yaw), 1e-6);
		TestEqual(TEXT("往右走：往右侧倾"), Strafe.Roll, static_cast<double>(NoBob.MoveLean.Roll), 1e-6);
		TestEqual(TEXT("往右走：俯仰不动"), Strafe.Pitch, 0.0, 1e-9);
		TestTrue(TEXT("往右走：枪往左挪"), Shift.Equals(FVector(0.0, NoBob.MoveLeanShift.Y, 0.0), 1e-6));
		// 起步时枪落在后面：刚走 0.1 s 还没偏到一半
		FMoveSwayState Start;
		StepMoveSway(Start, NoBob, FVector2D::ZeroVector, true, 1.0 / 60.0);
		FRotator Early;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			Early = StepMoveSway(Start, NoBob, FVector2D(0.0, NoBob.MoveReferenceSpeed), true, 1.0 / 60.0);
		}
		TestTrue(FString::Printf(TEXT("起步 0.1 s：偏得还少（%.3f°）"), Early.Yaw), FMath::Abs(Early.Yaw) < 0.5 * FMath::Abs(NoBob.MoveLean.Yaw));
	}

	// 走路的晃：参考速度走时偏航晃到 ±幅度、两步一个来回；离地慢慢收掉（偏关掉看）
	FGinkgoScopeHandling NoLean = Scope.Handling;
	NoLean.MoveLean = FRotator::ZeroRotator;
	{
		FMoveSwayState State;
		const FVector2D Walk(NoLean.MoveReferenceSpeed, 0.0);
		double MaxYaw = -1.0;
		double MinYaw = 1.0;
		for (int32 Index = 0; Index < 1200; ++Index)
		{
			const FRotator Sway = StepMoveSway(State, NoLean, Walk, true, 1.0 / 120.0);
			if (Index >= 240)
			{
				MaxYaw = FMath::Max(MaxYaw, Sway.Yaw);
				MinYaw = FMath::Min(MinYaw, Sway.Yaw);
			}
		}
		TestEqual(TEXT("走路：偏航晃到 +幅度"), MaxYaw, NoLean.MoveBob.X, 0.01);
		TestEqual(TEXT("走路：偏航晃到 −幅度"), MinYaw, -NoLean.MoveBob.X, 0.01);
		// 相位：走过一步是 π。存的状态到上一帧末：一帧走一步的长度，下一次推进时把这一步记上
		FMoveSwayState Phase;
		const double StepTime = static_cast<double>(NoLean.StepLength) / static_cast<double>(NoLean.MoveReferenceSpeed);
		StepMoveSway(Phase, NoLean, Walk, true, StepTime);
		const double Before = Phase.Phase;
		StepMoveSway(Phase, NoLean, Walk, true, 1.0 / 60.0);
		TestEqual(TEXT("走一步相位走 π"), FMath::Fmod(Phase.Phase - Before + 2.0 * UE_DOUBLE_PI, 2.0 * UE_DOUBLE_PI), UE_DOUBLE_PI, 1e-6);
		FRotator Air;
		for (int32 Index = 0; Index < 240; ++Index)
		{
			Air = StepMoveSway(State, NoLean, Walk, false, 1.0 / 60.0);
		}
		TestTrue(FString::Printf(TEXT("离地 2 s：不晃了（%.4f°）"), Air.Yaw), Air.IsNearlyZero(1e-3));
	}

	// 与帧率无关：斜着起步（0.3 s 加到参考速度）、走、停下（0.3 s），30、60、144 fps 在共有的时刻（1/6 s 的整数倍）差不到 0.01°。
	// 同游戏里一样，每一帧读到的是上一帧末的速度（武器组件在移动之前推进），返回的是这一帧末的
	const auto Speed = [&](const double Time)
	{
		const double Ramp = FMath::Clamp(Time / 0.3, 0.0, 1.0) * (1.0 - FMath::Clamp((Time - 2.0) / 0.3, 0.0, 1.0));
		return FVector2D(0.6, 0.8) * (Scope.Handling.MoveReferenceSpeed * Ramp);
	};
	TMap<int32, FRotator> ByFps[3];
	const int32 Rates[] = {30, 60, 144};
	for (int32 Rate = 0; Rate < 3; ++Rate)
	{
		FMoveSwayState State;
		const int32 Frames = Rates[Rate] * 3;
		for (int32 Frame = 1; Frame <= Frames; ++Frame)
		{
			const FRotator Sway = StepMoveSway(State, Scope.Handling, Speed(static_cast<double>(Frame - 1) / Rates[Rate]), true, 1.0 / Rates[Rate]);
			if (Frame * 6 % Rates[Rate] == 0)
			{
				ByFps[Rate].Add(Frame * 6 / Rates[Rate], Sway);
			}
		}
	}
	double Worst = 0.0;
	for (const TPair<int32, FRotator>& Pair : ByFps[0])
	{
		for (int32 Rate = 1; Rate < 3; ++Rate)
		{
			const FRotator Other = ByFps[Rate].FindRef(Pair.Key);
			Worst = FMath::Max3(Worst, FMath::Abs(Other.Pitch - Pair.Value.Pitch), FMath::Max(FMath::Abs(Other.Yaw - Pair.Value.Yaw), FMath::Abs(Other.Roll - Pair.Value.Roll)));
		}
	}
	TestEqual(TEXT("共有的时刻"), ByFps[0].Num(), 18);
	TestTrue(FString::Printf(TEXT("三种帧率差不到 0.01°（%.6f°）"), Worst), Worst < 1e-2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeShotEyeShiftTest, "CaseGinkgo.Weapon.Scope.ShotEyeShift", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeShotEyeShiftTest::RunTest(const FString& Parameters)
{
	using Ginkgo::Scope::SampleShotEyeShift;
	FGinkgoScopeHandling Handling;
	TestTrue(TEXT("开火前与开火瞬间没有跳变"), SampleShotEyeShift(Handling, -0.1).IsZero() && SampleShotEyeShift(Handling, 0.0).IsZero());
	TestTrue(TEXT("第一拍顶近，峰值由设置给定"), SampleShotEyeShift(Handling, 0.033).Equals(Handling.ShotEyeShift, 1e-6));
	const FVector FirstFrame = SampleShotEyeShift(Handling, 1.0 / 60.0);
	TestTrue(TEXT("60 fps 第一帧已开始顶近"), FirstFrame.X > 0 && FirstFrame.X < Handling.ShotEyeShift.X);
	const FVector Rebound = SampleShotEyeShift(Handling, 0.08);
	TestTrue(TEXT("前后还在回，横向已经回摆"), Rebound.X > 0 && Rebound.X < Handling.ShotEyeShift.X
		&& Rebound.Y * Handling.ShotEyeShift.Y < 0 && Rebound.Z * Handling.ShotEyeShift.Z < 0);
	TestTrue(TEXT("收住后没有残余"), SampleShotEyeShift(Handling, 0.22).IsZero() && SampleShotEyeShift(Handling, 1.0).IsZero());
	Handling.ShotEyeShift = FVector::ZeroVector;
	TestTrue(TEXT("可关掉新增的位移"), SampleShotEyeShift(Handling, 0.05).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoScopeLookScaleTest, "CaseGinkgo.Weapon.Scope.LookScale", GinkgoScopeTests::TestFlags)
bool FGinkgoScopeLookScaleTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Scope;
	TestEqual(TEXT("不开镜是 1"), ScopeLookScale(0.f, 1.f, 4.f), 1.f);
	TestEqual(TEXT("开镜到位：倍镜灵敏度 ÷ 倍率"), ScopeLookScale(1.f, 1.f, 4.f), 0.25f);
	TestEqual(TEXT("倍镜灵敏度 2 倍"), ScopeLookScale(1.f, 2.f, 4.f), 0.5f);
	TestEqual(TEXT("一半进度在中间"), ScopeLookScale(0.5f, 1.f, 4.f), 0.625f);
	// 鼠标：有倍镜时开镜灵敏度、按视场换算都不管，按倍镜的算；没有倍镜时同以前
	FGinkgoInputSettings Settings;
	Settings.MouseAdsScale = 1.5f;
	Settings.bMouseAdsMatchFov = true;
	const float Dt = 1.f / 60.f;
	const double Hip = Ginkgo::Input::MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 0.f, 1.732f, Dt, 4.f).X;
	const double Scoped = Ginkgo::Input::MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 1.f, 1.732f, Dt, 4.f).X;
	TestEqual(TEXT("倍镜里转的是不开镜的四分之一"), Scoped, Hip * 0.25, 1e-6);
	const double ShoulderAds = Ginkgo::Input::MouseLookDegrees(FVector2D(1000.0, 0.0), Settings, 1.f, 1.732f, Dt).X;
	TestEqual(TEXT("没有倍镜：照开镜灵敏度与视场换算"), ShoulderAds, Hip * 1.5 / 1.732, 1e-4);
	// 手柄同样
	Ginkgo::Input::FGinkgoTurnRamp Ramp;
	const double PadHip = Ginkgo::Input::StickLookDegrees(FVector2D(0.6, 0.0), Settings, 0.f, 1.732f, Ramp, Dt, 4.f).X;
	Ginkgo::Input::FGinkgoTurnRamp RampScoped;
	const double PadScoped = Ginkgo::Input::StickLookDegrees(FVector2D(0.6, 0.0), Settings, 1.f, 1.732f, RampScoped, Dt, 4.f).X;
	TestEqual(TEXT("手柄：倍镜里转的是不开镜的四分之一"), PadScoped, PadHip * 0.25, 1e-6);
	// 第一人称的进度：没有瞄具时总是 0，有瞄具时就是开镜进度
	TestEqual(TEXT("没有瞄具不进第一人称"), FirstPersonAlpha(1.f, false), 0.f);
	TestEqual(TEXT("有瞄具：开镜进度"), FirstPersonAlpha(0.4f, true), 0.4f);
	return true;
}

#endif

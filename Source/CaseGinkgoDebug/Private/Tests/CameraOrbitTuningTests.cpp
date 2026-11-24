#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraConfigPath.h"
#include "Camera/GinkgoCameraMath.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraOrbitTuningTest, "CaseGinkgo.Camera.Orbit.TuningPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoCameraOrbitTuningTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Debug::Camera;
	auto* Config = NewObject<UGinkgoCameraConfig>();
	auto* Reloaded = NewObject<UGinkgoCameraConfig>();
	const TMap<FString, FString> Changes = {
		{TEXT("Orbit.bEnabled"), TEXT("True")},
		{TEXT("Orbit.UpperRadiusScale"), TEXT("0.27")},
		{TEXT("Orbit.UpperPitch"), TEXT("52")},
		{TEXT("Orbit.LowerRadiusScale"), TEXT("0.61")},
		{TEXT("Orbit.LowerPitch"), TEXT("38")},
	};
	const TArray<FTunable> Rows = CollectTunables(*Config);
	for (const auto& Change : Changes)
	{
		TestTrue(TEXT("F7 反射发现三环项并归入独立配置组"), Rows.ContainsByPredicate([&](const FTunable& Row)
		{
			return Row.Path == Change.Key && Row.Group == TEXT("Orbit");
		}));
		FConfigItem Item;
		FConfigItem ReloadedItem;
		FString Error;
		if (!TestTrue(TEXT("调参路径可解析"), ResolveConfigPath(*Config, Change.Key, Item, Error))
			|| !TestTrue(TEXT("重读目标路径可解析"), ResolveConfigPath(*Reloaded, Change.Key, ReloadedItem, Error)))
		{
			return false;
		}
		const FString Default = ExportItem(Item);
		TestTrue(TEXT("调参写入成功"), ImportItem(Item, Change.Value));
		const FString Saved = ExportItem(Item);
		TestTrue(TEXT("按本地文件的属性文本格式重新载入"), ImportItem(ReloadedItem, Saved));
		TestEqual(TEXT("小数与开关不丢失"), ExportItem(ReloadedItem), Saved);
		TestTrue(TEXT("恢复默认成功"), ImportItem(Item, Default));
		TestEqual(TEXT("恢复默认后比较稳定"), ExportItem(Item), Default);
	}
	TestEqual(TEXT("重新载入的上环真正影响位姿"), Ginkgo::Camera::ComputeOrbitArmLength(150, -52, Reloaded->Orbit)
		* FMath::Cos(FMath::DegreesToRadians(52.0)), 150.0 * 0.27, 1e-4);
	TestEqual(TEXT("旧调参文件未包含 Orbit 时默认启用"), Config->Orbit.bEnabled, true);
	return true;
}

#endif

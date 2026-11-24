#include "Camera/GinkgoCameraConfig.h"
#include "Camera/GinkgoCameraConfigPath.h"
#include "Camera/GinkgoCameraRayTuning.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCameraRayTuningTest, "CaseGinkgo.Camera.Shoulder.RayTuningPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGinkgoCameraRayTuningTest::RunTest(const FString& Parameters)
{
	using namespace Ginkgo::Debug::Camera;
	UGinkgoCameraConfig* Config = NewObject<UGinkgoCameraConfig>();
	FConfigItem Item;
	FString Error;
	TestTrue(TEXT("列表路径可解析"), ResolveConfigPath(*Config, TEXT("Shoulder.Rays"), Item, Error));
	if (!Item.Property) { return false; }
	TestTrue(TEXT("列表按反射类型识别，不依赖编辑器元数据"), IsRayList(*Item.Property));
	const FString Default = ExportItem(Item);
	const FString Custom = TEXT("[{\"angle\":-22.5,\"length\":240,\"score\":0.75},{\"angle\":0,\"length\":500,\"score\":3.25}]");
	TestTrue(TEXT("支持增删及小数值"), ImportItem(Item, Custom));
	TestEqual(TEXT("完整替换列表长度"), Config->Shoulder.Rays.Num(), 2);
	TestEqual(TEXT("角度保持小数"), Config->Shoulder.Rays[0].Angle, -22.5f);
	TestEqual(TEXT("独立射程以 cm 保存"), Config->Shoulder.Rays[1].Length, 500.f);
	TestEqual(TEXT("命中分保持小数"), Config->Shoulder.Rays[1].Score, 3.25f);
	const FString Saved = ExportItem(Item);
	for (const FString& Invalid : {
		FString(TEXT("{}")), FString(TEXT("null")), FString(TEXT("[{\"angle\":0}]")),
		FString(TEXT("[{\"angle\":1,\"length\":350,\"score\":1},{\"angle\":181,\"length\":350,\"score\":1}]")),
		FString(TEXT("[{\"angle\":0,\"length\":0,\"score\":1}]")),
		FString(TEXT("[{\"angle\":0,\"length\":350,\"score\":-1}]"))})
	{
		TestFalse(TEXT("非法列表拒绝导入"), ImportItem(Item, Invalid));
		TestEqual(TEXT("即使前半段合法也不部分覆盖原列表"), ExportItem(Item), Saved);
	}
	TArray<FGinkgoCameraShoulderRay> TooMany;
	TooMany.Init(FGinkgoCameraShoulderRay(), FGinkgoCameraShoulderSettings::MaxRays + 1);
	TestFalse(TEXT("拒绝超过查询预算的列表"), ImportItem(Item, ExportRays(TooMany)));
	TestEqual(TEXT("超预算不修改原值"), ExportItem(Item), Saved);
	TestTrue(TEXT("空列表可保存"), ImportItem(Item, TEXT("[]")));
	TestTrue(TEXT("删除最后一条后运行时列表为空"), Config->Shoulder.Rays.IsEmpty());
	int32 RayRows = 0;
	for (const FTunable& Row : CollectTunables(*Config))
	{
		if (Row.Path.StartsWith(TEXT("Shoulder.Rays")))
		{
			++RayRows;
			TestEqual(TEXT("列表不拆成下标路径"), Row.Path, FString(TEXT("Shoulder.Rays")));
		}
	}
	TestEqual(TEXT("空列表仍有可编辑/恢复的调参行"), RayRows, 1);
	TestTrue(TEXT("重读保存列表"), ImportItem(Item, Saved));
	TestEqual(TEXT("重读后比较结果稳定"), ExportItem(Item), Saved);
	TestTrue(TEXT("整组恢复默认"), ImportItem(Item, Default));
	TestEqual(TEXT("恢复默认九条"), Config->Shoulder.Rays.Num(), 9);
	TestEqual(TEXT("默认比较稳定"), ExportItem(Item), Default);

	TMap<FString, FString> Legacy = {{TEXT("Shoulder.TraceDistance"), TEXT("450")}, {TEXT("Shoulder.Cooldown"), TEXT("8")}};
	MigrateRayTuning(Legacy, Config->Shoulder.Rays);
	TestFalse(TEXT("旧路径已迁移"), Legacy.Contains(TEXT("Shoulder.TraceDistance")));
	TestTrue(TEXT("旧统一射程转换为列表"), ImportItem(Item, Legacy.FindRef(TEXT("Shoulder.Rays"))));
	TestEqual(TEXT("旧射程保留到各条"), Config->Shoulder.Rays[8].Length, 450.f);
	TestEqual(TEXT("其他配置保留"), Legacy.FindRef(TEXT("Shoulder.Cooldown")), FString(TEXT("8")));
	Legacy.Add(TEXT("Shoulder.Rays"), Saved);
	Legacy.Add(TEXT("Shoulder.TraceDistance"), TEXT("200"));
	MigrateRayTuning(Legacy, Config->Shoulder.Rays);
	TestEqual(TEXT("新列表优先于旧全局射程"), Legacy.FindRef(TEXT("Shoulder.Rays")), Saved);
	return true;
}

#endif

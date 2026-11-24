#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/GinkgoEnemyArchetype.h"
#include "Checkpoint/GinkgoCheckpoint.h"
#include "Checkpoint/GinkgoCheckpointEnemySpawn.h"
#include "Checkpoint/GinkgoEncounterEdMode.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "Selection.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "Tests/AutomationEditorCommon.h"
#include "Weapon/GinkgoAmmoPickup.h"
#include "Weapon/GinkgoWeaponDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoEncounterAuthoringTest, "CaseGinkgo.Checkpoint.Encounter.EditorPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoEncounterAuthoringTest::RunTest(const FString& Parameters)
{
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	auto& Modes = GLevelEditorModeTools();
	const FName ModeId(TEXT("EM_GinkgoEncounter"));
	Modes.ActivateMode(ModeId);
	ON_SCOPE_EXIT { Modes.DeactivateMode(ModeId); };
	auto* Mode = Cast<UGinkgoEncounterEdMode>(Modes.GetActiveScriptableMode(ModeId));
	if (!TestNotNull(TEXT("关卡模式可通过注册 ID 进入"), Mode)) { return false; }
	auto* Brush = Mode->GetBrushSettings();
	if (!TestNotNull(TEXT("模式创建了面板参数"), Brush)) { return false; }
	const FVector Ground(350., 500., 80.);
	FText Error;
	TestFalse(TEXT("没有 Checkpoint 时不创建孤立标记"), Mode->PlaceAtGround(*World, Ground, Error));
	Brush->Brush = EGinkgoEncounterBrush::AmmoBox;
	Brush->AmmoBoxClass = LoadClass<AGinkgoAmmoPickup>(nullptr, TEXT("/Game/BPPrefab/AmmoBoxes/BP_AmmoBox_NATO556x45.BP_AmmoBox_NATO556x45_C"));
	if (!TestNotNull(TEXT("可选择现有 5.56 子弹盒预制体"), Brush->AmmoBoxClass.Get())) { return false; }
	Brush->AmmoRounds = 32;
	if (!TestTrue(TEXT("没有检查点也可放置子弹盒"), Mode->PlaceAtGround(*World, Ground, Error))) { return false; }
	auto* Box = GEditor->GetSelectedActors()->GetTop<AGinkgoAmmoPickup>();
	if (!TestNotNull(TEXT("放置生成实际拾取物"), Box)) { return false; }
	TestEqual(TEXT("使用选中的弹种预制体"), Box->GetClass(), Brush->AmmoBoxClass.Get());
	TestTrue(TEXT("盒底在当前编辑层的点击位置"), Box->GetLevel() == World->GetCurrentLevel() && Box->GetActorLocation().Equals(Ground));
	TestEqual(TEXT("保留指定发数"), Box->GetRounds(), 32);
	GEditor->UndoTransaction();
	TestFalse(TEXT("撤销移除子弹盒"), static_cast<bool>(TActorIterator<AGinkgoAmmoPickup>(World)));
	GEditor->RedoTransaction();
	TActorIterator<AGinkgoAmmoPickup> Restored(World);
	TestTrue(TEXT("重做恢复子弹盒与发数"), Restored && Restored->GetRounds() == 32);
	Brush->AmmoRounds = 0;
	if (!TestTrue(TEXT("连续放置不需重选预制体"), Mode->PlaceAtGround(*World, Ground + FVector(40., 0., 0.), Error))) { return false; }
	Box = GEditor->GetSelectedActors()->GetTop<AGinkgoAmmoPickup>();
	TestTrue(TEXT("零发数覆盖保留预制体默认值"), Box && Box->GetRounds() == Brush->AmmoBoxClass.GetDefaultObject()->GetRounds());
	GEditor->SelectNone(false, true);
	Brush->Brush = EGinkgoEncounterBrush::Spawn;
	auto* Point = World->SpawnActor<AGinkgoCheckpoint>(FVector::ZeroVector, FRotator(0., 42., 0.));
	GEditor->SelectActor(Point, true, true);
	TestEqual(TEXT("选择检查点同步到工具"), Brush->Checkpoint.Get(), Point);
	Brush->EnemyType = LoadObject<UGinkgoEnemyArchetype>(nullptr, TEXT("/Game/AI/Enemies/DA_Enemy_Heavy"));
	if (!TestNotNull(TEXT("可选择重甲类型"), Brush->EnemyType.Get())) { return false; }
	Brush->Weapon = TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911")));
	auto& Reference = Brush->BehaviorOverrides.Reference;
	Reference.SetStateTree(LoadObject<UStateTree>(nullptr, TEXT("/Game/AI/ST_Enemy_SprintToRange")));
	const FName Range(TEXT("SprintFireRangeMeters"));
	const auto* Parameter = Reference.GetParameters().FindPropertyDescByName(Range);
	if (!TestNotNull(TEXT("选择冲刺树后出现标准根参数"), Parameter)) { return false; }
	Reference.SetPropertyOverridden(Parameter->ID, true);
	Reference.GetMutableParameters().SetValueFloat(Range, 11.f);
	if (!TestTrue(TEXT("地面点击生成出生位"), Mode->PlaceAtGround(*World, Ground, Error))) { return false; }
	auto* Marker = GEditor->GetSelectedActors()->GetTop<AGinkgoCheckpointEnemySpawn>();
	if (!TestNotNull(TEXT("自动选中新出生位"), Marker)) { return false; }
	TestEqual(TEXT("归属所选检查点"), Marker->Checkpoint.Get(), Point);
	TestEqual(TEXT("出生位与检查点同层"), Marker->GetLevel(), Point->GetLevel());
	TestTrue(TEXT("使用指定武器"), Marker->WeaponOverride == Brush->Weapon);
	TestTrue(TEXT("出生位独立保存面板所选的敌人类型"), Marker->EnemyType == Brush->EnemyType);
	const auto& Placed = Marker->BehaviorOverrides.Reference;
	TestTrue(TEXT("出生位保存树引用、覆盖勾选及数值"), Placed.GetStateTree() == Reference.GetStateTree()
		&& Placed.IsPropertyOverridden(Parameter->ID) && Placed.GetParameters().GetValueFloat(Range).GetValue() == 11.f);
	TestTrue(TEXT("保留脚底位置"), Marker->GetActorLocation().Equals(Ground, .01));
	TestEqual(TEXT("守卫默认原地待命"), Marker->Points.Num(), 0);
	Brush->Brush = EGinkgoEncounterBrush::Patrol;
	Brush->WaitSeconds = 1.5f;
	const FVector Destination = Ground + FVector(100., -320., 12.);
	if (!TestTrue(TEXT("点击地面追加巡逻点"), Mode->PlaceAtGround(*World, Destination, Error))
		|| !TestEqual(TEXT("路线有一个点"), Marker->Points.Num(), 1)) { return false; }
	TestTrue(TEXT("旋转后的标记仍能还原世界点击位置"), Marker->GetPointLocation(0).Equals(Destination, .01));
	TestEqual(TEXT("每点保存停留时间"), Marker->Points[0].WaitSeconds, 1.5f);
	GEditor->UndoTransaction();
	TestEqual(TEXT("撤销单个巡逻点"), Marker->Points.Num(), 0);
	GEditor->RedoTransaction();
	TestEqual(TEXT("重做恢复巡逻点"), Marker->Points.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoSprintParameterInheritanceTest, "CaseGinkgo.Checkpoint.Encounter.StateTreeParameterInheritance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGinkgoSprintParameterInheritanceTest::RunTest(const FString& Parameters)
{
	auto* Source = LoadObject<UStateTree>(nullptr, TEXT("/Game/AI/ST_Enemy_SprintToRange"));
	if (!TestNotNull(TEXT("有可复制的冲刺树"), Source)) { return false; }
	auto* Copy = DuplicateObject<UStateTree>(Source, GetTransientPackage());
	auto* Data = CastChecked<UStateTreeEditorData>(Copy->EditorData);
	const FName Range(TEXT("SprintFireRangeMeters"));
	const auto* Desc = Copy->GetDefaultParameters().FindPropertyDescByName(Range);
	if (!TestNotNull(TEXT("根参数可用于编辑器逐项覆盖"), Desc)) { return false; }
	const FGuid Id = Desc->ID;
	FStateTreeReference Inherited;
	Inherited.SetStateTree(Copy);
	FStateTreeReference Overridden = Inherited;
	Overridden.SetPropertyOverridden(Id, true);
	Overridden.GetMutableParameters().SetValueFloat(Range, 0.f);
	TestFalse(TEXT("选择树不会隐式勾选距离"), Inherited.IsPropertyOverridden(Id));
	TestEqual(TEXT("初始继承 15 米"), Inherited.GetParameters().GetValueFloat(Range).GetValue(), 15.f);

	// 在临时资产的编辑数据上改值并真正编译；现有两个引用仍是修改前创建的。
	auto& EditedBag = const_cast<FInstancedPropertyBag&>(Data->GetRootParametersPropertyBag());
	EditedBag.SetValueFloat(Range, 21.f);
	FStateTreeCompilerLog Log;
	if (!TestTrue(TEXT("重新编译临时树"), UStateTreeEditingSubsystem::CompileStateTree(Copy, Log))) { return false; }
	TestEqual(TEXT("未勾选的引用跟随新默认值"), Inherited.GetParameters().GetValueFloat(Range).GetValue(), 21.f);
	TestEqual(TEXT("显式覆盖的零值保留"), Overridden.GetParameters().GetValueFloat(Range).GetValue(), 0.f);
	Overridden.SetPropertyOverridden(Id, false);
	TestEqual(TEXT("取消勾选恢复当前树默认值"), Overridden.GetParameters().GetValueFloat(Range).GetValue(), 21.f);
	return true;
}

#endif

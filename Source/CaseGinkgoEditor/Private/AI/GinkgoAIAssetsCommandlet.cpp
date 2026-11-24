#include "GinkgoAIAssetsCommandlet.h"

#include "AI/GinkgoAIConfig.h"
#include "AI/GinkgoAIStateTree.h"
#include "AI/EQS/GinkgoEnvQueryTest_Cover.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_SimpleGrid.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Pathfinding.h"
#include "Misc/PackageName.h"
#include "PropertyBindingTypes.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeFactory.h"
#include "StateTreeState.h"
#include "UObject/SavePackage.h"
#include "Weapon/GinkgoWeaponDefinition.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoAIAssets, Log, All);

/** 灰盒验收关卡与默认行为资产分别生成，避免重建参数时覆盖手调关卡。 */
bool BuildGinkgoAIDemoMap(bool bForce, bool bWeaponVariants = false);

namespace
{
	bool Save(UObject* Asset)
	{
		FAssetRegistryModule::AssetCreated(Asset);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
		return UPackage::SavePackage(Asset->GetPackage(), Asset, *Filename, Args);
	}

	/** 只替换显式要求重建的包内对象，旧引用随本次 commandlet 退出释放。 */
	UPackage* Prepare(const TCHAR* Path)
	{
		UPackage* Package = CreatePackage(Path);
		Package->FullyLoad();
		if (UObject* Existing = FindObject<UObject>(Package, *FPackageName::GetLongPackageAssetName(Path)))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
		}
		return Package;
	}

	/** 原地升级冲刺树的两个距离消费者，保留节点 ID、已有连线和作者在树上修改的参数默认值。 */
	bool UpgradeSprintParameters(UStateTreeEditorData& Data)
	{
		const FName InputName = GET_MEMBER_NAME_CHECKED(FGinkgoAISprintTaskData, SprintFireRangeMeters);
		const FGuid ParameterID = FGinkgoAISprintTaskData::RangeParameterID;
		const auto* Parameter = Data.GetRootParametersPropertyBag().FindPropertyDescByID(ParameterID);
		FName SourceName = Parameter ? Parameter->Name : InputName;
		bool bChanged = false;
		if (!Parameter)
		{
			FGinkgoAISprintTaskData Defaults;
			Defaults.SprintFireRangeMeters = 15.f;
			const FProperty* Input = FindFieldChecked<FFloatProperty>(FGinkgoAISprintTaskData::StaticStruct(), InputName);
			UE::PropertyBinding::FPropertyCreationDescriptor Creation;
			Creation.PropertyDesc = FPropertyBagPropertyDesc(InputName, EPropertyBagPropertyType::Float);
			Creation.PropertyDesc.ID = ParameterID;
			Creation.PropertyDesc.MetaData.Emplace(TEXT("DisplayName"), TEXT("冲刺射击距离"));
			Creation.PropertyDesc.MetaData.Emplace(TEXT("ForceUnits"), TEXT("m"));
			Creation.PropertyDesc.MetaData.Emplace(TEXT("ClampMin"), TEXT("0"));
			Creation.SourceProperty = Input;
			Creation.SourceContainerAddress = &Defaults;
			Data.CreateParametersForStruct(Data.GetRootParametersGuid(), MakeArrayView(&Creation, 1));
			SourceName = Creation.PropertyDesc.Name;
			bChanged = true;
		}
		Data.VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
		{
			for (FStateTreeEditorNode& Task : State.Tasks)
			{
				if (Task.Node.GetScriptStruct() != FGinkgoAIBehaviorTask::StaticStruct()) { continue; }
				const auto Old = Task.Node.Get<FGinkgoAIBehaviorTask>();
				if (Old.Tactics.Positioning != EGinkgoAIPositioning::SprintToRange
					|| (Old.Behavior != EGinkgoAIBehavior::Engage && Old.Behavior != EGinkgoAIBehavior::Pursue)) { continue; }
				Task.Node.InitializeAs<FGinkgoAISprintTask>();
				static_cast<FGinkgoAIBehaviorTask&>(Task.Node.GetMutable<FGinkgoAISprintTask>()) = Old;
				Task.Instance.InitializeAs<FGinkgoAISprintTaskData>();
				Data.AddPropertyBinding(FPropertyBindingPath(Data.GetRootParametersGuid(), SourceName), FPropertyBindingPath(Task.ID, InputName));
				bChanged = true;
			}
			return EStateTreeVisitor::Continue;
		});
		return bChanged;
	}

	/** 生成树时决定是否添加优先级最高的警戒分支，以及该分支的退出条件。 */
	enum class EGuardMode { None, UntilFirstSight, Always };

	/** 共用感知和巡逻结构，永久驻守树只生成原地警戒任务。 */
	UStateTree* BuildTree(const TCHAR* TreePath, const FGinkgoAITactics& Tactics, const bool bRebuild, const EGuardMode Guard = EGuardMode::None)
	{
		UStateTree* Tree = LoadObject<UStateTree>(nullptr, TreePath, nullptr, LOAD_NoWarn);
		bool bChanged = !Tree || bRebuild;
		if (bChanged)
		{
			UPackage* Package = Prepare(TreePath);
			UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
			Factory->SetSchemaClass(UGinkgoAIStateTreeSchema::StaticClass());
			Tree = Cast<UStateTree>(Factory->FactoryCreateNew(UStateTree::StaticClass(), Package, *FPackageName::GetLongPackageAssetName(TreePath), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
			UStateTreeEditorData* Data = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
			if (!Data || Data->SubTrees.IsEmpty()) return nullptr;
			UStateTreeState& Root = *Data->SubTrees[0];
			Root.Name = TEXT("Enemy");
			const bool bWaitForSight = Guard == EGuardMode::UntilFirstSight;
			CastChecked<UGinkgoAIStateTreeSchema>(Data->Schema)->bUseDefaultBehaviorAfterSight = bWaitForSight;
			TArray<EGinkgoAIBehavior, TInlineAllocator<6>> Behaviors;
			if (Guard != EGuardMode::None) Behaviors.Add(EGinkgoAIBehavior::StandGuard);
			if (Guard != EGuardMode::Always)
			{
				Behaviors.Append({ EGinkgoAIBehavior::Engage, EGinkgoAIBehavior::Pursue, EGinkgoAIBehavior::Investigate, EGinkgoAIBehavior::Search, EGinkgoAIBehavior::Patrol });
			}
			for (const EGinkgoAIBehavior Behavior : Behaviors)
			{
				UStateTreeState& State = Root.AddChildState(*UEnum::GetValueAsString(Behavior));
				FGinkgoAIBehaviorTask& Task = State.AddTask<FGinkgoAIBehaviorTask>().GetNode();
				Task.Behavior = Behavior;
				Task.Tactics = Tactics;
				if (Guard == EGuardMode::Always) continue;
				State.AddEnterCondition<FGinkgoAICondition>().GetNode().Behavior = Behavior;
				FStateTreeTransition& Changed = State.AddTransition(EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Root);
				FGinkgoAICondition& Condition = Changed.AddConditionWithOuter<FGinkgoAICondition>(&State).GetNode();
				Condition.Behavior = Behavior;
				Condition.bInvert = true;
				State.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
			}
		}
		if (Tactics.Positioning == EGinkgoAIPositioning::SprintToRange)
		{
			auto* Data = Cast<UStateTreeEditorData>(Tree->EditorData);
			if (!Data) return nullptr;
			bChanged |= UpgradeSprintParameters(*Data);
		}
		if (bChanged)
		{
			FStateTreeCompilerLog Log;
			if (!UStateTreeEditingSubsystem::CompileStateTree(Tree, Log))
			{
				Log.DumpToLog(LogGinkgoAIAssets);
				return nullptr;
			}
			if (!Save(Tree)) return nullptr;
		}
		return Tree;
	}
}

UGinkgoAIAssetsCommandlet::UGinkgoAIAssetsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UGinkgoAIAssetsCommandlet::Main(const FString& Params)
{
	const bool bForce = FParse::Param(*Params, TEXT("Force"));
	const TCHAR* TreePath = TEXT("/Game/AI/ST_Enemy");
	const TCHAR* QueryPath = TEXT("/Game/AI/EQS_CombatCover");
	const TCHAR* ConfigPath = TEXT("/Game/AI/DA_Enemy");
	const bool bRebuildTree = bForce || FParse::Param(*Params, TEXT("ForceTree"));
	FGinkgoAITactics Cover;
	UStateTree* Tree = BuildTree(TreePath, Cover, bRebuildTree);
	FGinkgoAITactics Approach;
	Approach.Positioning = EGinkgoAIPositioning::Approach;
	Approach.ApproachDistance = 700.f;
	UStateTree* PistolTree = BuildTree(TEXT("/Game/AI/ST_Enemy_Pistol"), Approach, bRebuildTree);
	Approach.ApproachDistance = 450.f;
	UStateTree* ShotgunTree = BuildTree(TEXT("/Game/AI/ST_Enemy_Shotgun"), Approach, bRebuildTree);
	FGinkgoAITactics Sprint;
	Sprint.Positioning = EGinkgoAIPositioning::SprintToRange;
	UStateTree* SprintTree = BuildTree(TEXT("/Game/AI/ST_Enemy_SprintToRange"), Sprint, bRebuildTree);
	UStateTree* WaitTree = BuildTree(TEXT("/Game/AI/ST_Enemy_WaitForSight"), Cover, bRebuildTree, EGuardMode::UntilFirstSight);
	UStateTree* GuardTree = BuildTree(TEXT("/Game/AI/ST_Enemy_HoldPosition"), Cover, bRebuildTree, EGuardMode::Always);
	if (!Tree || !PistolTree || !ShotgunTree || !SprintTree || !WaitTree || !GuardTree) return 1;
	UEnvQuery* Query = LoadObject<UEnvQuery>(nullptr, QueryPath, nullptr, LOAD_NoWarn);
	if (!Query || bForce)
	{
		Query = NewObject<UEnvQuery>(Prepare(QueryPath), TEXT("EQS_CombatCover"), RF_Public | RF_Standalone | RF_Transactional);
		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query);
		UEnvQueryGenerator_SimpleGrid* Grid = NewObject<UEnvQueryGenerator_SimpleGrid>(Option);
		Grid->GridSize.DefaultValue = 700.f;
		Grid->SpaceBetween.DefaultValue = 100.f;
		Grid->ProjectionData.TraceMode = EEnvQueryTrace::Navigation;
		Option->Generator = Grid;
		Option->Tests.Add(NewObject<UGinkgoEnvQueryTest_Cover>(Option));
		UEnvQueryTest_Pathfinding* Path = NewObject<UEnvQueryTest_Pathfinding>(Option);
		Path->TestPurpose = EEnvTestPurpose::Filter;
		Path->TestMode = EEnvTestPathfinding::PathExist;
		Path->FilterType = EEnvTestFilterType::Match;
		Path->BoolValue.DefaultValue = true;
		Path->PathFromContext.DefaultValue = true;
		Option->Tests.Add(Path);
		UEnvQueryTest_Distance* Distance = NewObject<UEnvQueryTest_Distance>(Option);
		Distance->TestPurpose = EEnvTestPurpose::Score;
		Distance->ScoringEquation = EEnvTestScoreEquation::InverseLinear;
		Option->Tests.Add(Distance);
		Query->GetOptionsMutable().Add(Option);
		if (!Save(Query)) return 1;
	}
	UGinkgoAIConfig* Config = LoadObject<UGinkgoAIConfig>(nullptr, ConfigPath, nullptr, LOAD_NoWarn);
	if (!Config || bForce)
	{
		Config = NewObject<UGinkgoAIConfig>(Prepare(ConfigPath), TEXT("DA_Enemy"), RF_Public | RF_Standalone | RF_Transactional);
		Config->StateTree = Tree;
		Config->CoverQuery = Query;
		Config->Weapon = TSoftObjectPtr<UGinkgoWeaponDefinition>(FSoftObjectPath(TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47")));
		if (!Save(Config)) return 1;
	}
	// 只在未配置时补默认值；显式 -WeaponBehaviors 可把三把样例枪重新绑定到本次生成的树。
	const TCHAR* Weapons[] = { TEXT("/Game/Weapons/AK47/DA_AK47"), TEXT("/Game/Weapons/M1911/DA_M1911"), TEXT("/Game/Weapons/Shotgun/DA_Shotgun") };
	UStateTree* Trees[] = { Tree, PistolTree, ShotgunTree };
	const TCHAR* Definitions[] = { TEXT("/Game/AI/DA_Enemy"), TEXT("/Game/AI/DA_Enemy_Pistol"), TEXT("/Game/AI/DA_Enemy_Shotgun") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, Weapons[Index]);
		if (!Weapon) return 1;
		if (!Weapon->DefaultAIStateTree || FParse::Param(*Params, TEXT("WeaponBehaviors")))
		{
			Weapon->DefaultAIStateTree = Trees[Index];
			if (!Save(Weapon)) return 1;
		}
		if (Index > 0 && !LoadObject<UGinkgoAIConfig>(nullptr, Definitions[Index], nullptr, LOAD_NoWarn))
		{
			UGinkgoAIConfig* Variant = NewObject<UGinkgoAIConfig>(Prepare(Definitions[Index]),
				*FPackageName::GetLongPackageAssetName(Definitions[Index]), RF_Public | RF_Standalone | RF_Transactional);
			Variant->StateTree = Tree;
			Variant->CoverQuery = Query;
			Variant->Weapon = Weapon;
			Variant->ReserveRounds = Index == 1 ? 56 : 32;
			Variant->BurstDuration = 0.12f;
			Variant->BurstPause = Index == 1 ? 0.4f : 0.9f;
			if (!Save(Variant)) return 1;
		}
	}
	UE_LOG(LogGinkgoAIAssets, Display, TEXT("AI 资产就绪：%s / %s / %s"), TreePath, QueryPath, ConfigPath);
	if (FParse::Param(*Params, TEXT("Demo")) && !BuildGinkgoAIDemoMap(FParse::Param(*Params, TEXT("ForceDemo")))) return 1;
	if (FParse::Param(*Params, TEXT("WeaponDemo")) && !BuildGinkgoAIDemoMap(FParse::Param(*Params, TEXT("ForceDemo")), true)) return 1;
	return 0;
}

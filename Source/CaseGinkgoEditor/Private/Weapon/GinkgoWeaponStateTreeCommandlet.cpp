#include "GinkgoWeaponStateTreeCommandlet.h"

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeFactory.h"
#include "StateTreeState.h"
#include "Character/GinkgoCharacter.h"     // 蒙太奇分段的名字（Ginkgo::WeaponSections）
#include "UObject/SavePackage.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"    // 霰弹枪的机构打开直接压膛
#include "Weapon/GinkgoWeaponStateTree.h"

DEFINE_LOG_CATEGORY_STATIC(LogGinkgoWeaponStateTree, Log, All);

namespace GinkgoWeaponStateTree
{
	const TCHAR* const RootTreePath = TEXT("/Game/WeaponStates/ST_WeaponRoot");
	const TCHAR* const ReadyTreePath = TEXT("/Game/WeaponStates/ST_ActionReady");
	const TCHAR* const BoltTreePath = TEXT("/Game/WeaponStates/ST_ActionBolt");
	const TCHAR* const SemiAutoTreePath = TEXT("/Game/WeaponStates/ST_ActionSemiAuto");
	const TCHAR* const PumpTreePath = TEXT("/Game/WeaponStates/ST_ActionPump");
	const TCHAR* const OpenBoltTreePath = TEXT("/Game/WeaponStates/ST_ActionOpenBolt");
	const TCHAR* const BreakTreePath = TEXT("/Game/WeaponStates/ST_ActionBreak");

	/** 进入条件：根树的状态是它 */
	void EnterWhen(UStateTreeState& State, const EGinkgoWeaponRootState RootState)
	{
		State.AddEnterCondition<FGinkgoWeaponRootStateCondition>().GetNode().State = RootState;
	}

	/** 每帧看：根树的状态不再是它就回到根重选 */
	void ReselectUnless(UStateTreeState& State, UStateTreeState& Root, const EGinkgoWeaponRootState RootState, const EStateTreeTransitionPriority Priority)
	{
		FStateTreeTransition& Transition = State.AddTransition(EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Root);
		Transition.Priority = Priority;
		FGinkgoWeaponRootStateCondition& Condition = Transition.AddConditionWithOuter<FGinkgoWeaponRootStateCondition>(&State).GetNode();
		Condition.State = RootState;
		Condition.bInvert = true;
	}

	/** 跑一步，做完回到根重选 */
	void AddStep(UStateTreeState& State, UStateTreeState& Root, const FGameplayTag& Step)
	{
		FGinkgoWeaponStepTask& Task = State.AddTask<FGinkgoWeaponStepTask>().GetNode();
		Task.Step = Step;
		Task.bAllowAim = false;
		State.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
	}

	/** 子树：只有就绪。没有自己的子树的枪用它 */
	void BuildReadyTree(UStateTreeState& Root)
	{
		UStateTreeState& Ready = Root.AddChildState(TEXT("Ready"));
		Ready.Description = TEXT("就绪：等按键");
		Ready.AddTask<FGinkgoWeaponIdleTask>();
	}

	/** 进入条件：枪的子树现在该做这个 */
	void EnterWhen(UStateTreeState& State, const EGinkgoWeaponAction Action)
	{
		State.AddEnterCondition<FGinkgoWeaponActionCondition>().GetNode().Action = Action;
	}

	/** 每帧看：现在不该做这个就回到子树的根重选 */
	void ReselectUnless(UStateTreeState& State, UStateTreeState& Root, const EGinkgoWeaponAction Action)
	{
		FStateTreeTransition& Transition = State.AddTransition(EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Root);
		FGinkgoWeaponActionCondition& Condition = Transition.AddConditionWithOuter<FGinkgoWeaponActionCondition>(&State).GetNode();
		Condition.Action = Action;
		Condition.bInvert = true;
	}

	/** 枪上的一步：没做成不动目标槽，做完回到子树的根重选。只有开枪时能开镜（拉栓、装填都不能，你说的）；抬枪、两只手见 FGinkgoWeaponStepTask */
	void AddGunStep(UStateTreeState& State, UStateTreeState& Root, const FGameplayTag& Step, const bool bAllowAim, const bool bAllowRaise, const bool bBothHands)
	{
		FGinkgoWeaponStepTask& Task = State.AddTask<FGinkgoWeaponStepTask>().GetNode();
		Task.Step = Step;
		Task.bAllowAim = bAllowAim;
		Task.bAllowRaise = bAllowRaise;
		Task.bBothHandsOnGun = bBothHands;
		Task.bAbandonTargetOnFail = false;
		State.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
	}

	/** 子树后面都一样的几个：开火（要先抬枪）、空仓、就绪 */
	void AddFireAndReady(UStateTreeState& Root)
	{
		UStateTreeState& Fire = Root.AddChildState(TEXT("Fire"), EStateTreeStateType::Group);
		Fire.Description = TEXT("开火：想开火，能响");
		EnterWhen(Fire, EGinkgoWeaponAction::Fire);

		UStateTreeState& Raise = Fire.AddChildState(TEXT("Raise"));
		Raise.Description = TEXT("要先抵肩，抬枪走满才开（你定的）");
		Raise.AddEnterCondition<FGinkgoWeaponRaisedCondition>().GetNode().bInvert = true;
		Raise.AddTask<FGinkgoWeaponIdleTask>();
		FStateTreeTransition& Raised = Raise.AddTransition(EStateTreeTransitionTrigger::OnTick, EStateTreeTransitionType::GotoState, &Root);
		Raised.AddConditionWithOuter<FGinkgoWeaponRaisedCondition>(&Raise);
		ReselectUnless(Raise, Root, EGinkgoWeaponAction::Fire);

		UStateTreeState& Shoot = Fire.AddChildState(TEXT("Shoot"));
		Shoot.Description = TEXT("开一枪：进状态的那一帧改机构、算命中，片段只是表现");
		AddGunStep(Shoot, Root, FWeaponTags::Weapon_Step_Fire, /*bAllowAim*/ true, /*bAllowRaise*/ true, /*bBothHands*/ false);

		UStateTreeState& DryFire = Root.AddChildState(TEXT("DryFire"));
		DryFire.Description = TEXT("空仓：想开火，不能响，咔一声");
		EnterWhen(DryFire, EGinkgoWeaponAction::DryFire);
		AddGunStep(DryFire, Root, FWeaponTags::Weapon_Step_DryFire, /*bAllowAim*/ true, /*bAllowRaise*/ true, /*bBothHands*/ false);

		UStateTreeState& Ready = Root.AddChildState(TEXT("Ready"));
		Ready.Description = TEXT("就绪：等按键");
		Ready.AddTask<FGinkgoWeaponIdleTask>();
		ReselectUnless(Ready, Root, EGinkgoWeaponAction::Ready);
	}

	/** 子树：自动装填、可拆弹匣（M1911，Docs/Design/ActionStates.md「M1911」）。射速在开火那一步里：到时间才算做完 */
	void BuildSemiAutoTree(UStateTreeState& Root)
	{
		UStateTreeState& ReloadEmpty = Root.AddChildState(TEXT("ReloadEmpty"));
		ReloadEmpty.Description = TEXT("空仓换弹：想装填，枪机挂在后面。换弹匣，放枪机回去");
		EnterWhen(ReloadEmpty, EGinkgoWeaponAction::ReloadEmpty);
		AddGunStep(ReloadEmpty, Root, FWeaponTags::Weapon_Step_ReloadEmpty, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& ReloadTactical = Root.AddChildState(TEXT("ReloadTactical"));
		ReloadTactical.Description = TEXT("战术换弹：想装填，枪机关着。膛里那一发留着");
		EnterWhen(ReloadTactical, EGinkgoWeaponAction::ReloadTactical);
		AddGunStep(ReloadTactical, Root, FWeaponTags::Weapon_Step_ReloadTactical, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& CloseBolt = Root.AddChildState(TEXT("CloseBolt"));
		CloseBolt.Description = TEXT("放枪机：枪机挂在后面，弹匣有弹（换到一半收枪，拔出来接着）");
		EnterWhen(CloseBolt, EGinkgoWeaponAction::CloseBolt);
		AddGunStep(CloseBolt, Root, FWeaponTags::Weapon_Step_CloseBolt, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		// 放松排在开火之后：按开火、装填都先于它。一动就退出（每帧看，退出时取消片段）
		UStateTreeState& IdleRelax = Root.AddChildState(TEXT("IdleRelax"));
		IdleRelax.Description = TEXT("放松：站了一阵放一次，一动就打断");
		EnterWhen(IdleRelax, EGinkgoWeaponAction::IdleRelax);
		AddGunStep(IdleRelax, Root, FWeaponTags::Weapon_Step_IdleRelax, /*bAllowAim*/ true, /*bAllowRaise*/ true, /*bBothHands*/ false);
		ReselectUnless(IdleRelax, Root, EGinkgoWeaponAction::IdleRelax);

		AddFireAndReady(Root);
	}

	/** 子树：单发中折（M79）：区分空仓换弹（捏取退壳）与有弹重装填（折开高举重力甩弹），装入新弹后若被切枪打断则拔出时直接合膛 */
	void BuildBreakTree(UStateTreeState& Root)
	{
		UStateTreeState& ReloadEmpty = Root.AddChildState(TEXT("ReloadEmpty"));
		ReloadEmpty.Description = TEXT("空仓换弹：膛内空壳或空膛。折开捏出空壳、装入新榴弹并合膛；已折开时从对应分段接续");
		EnterWhen(ReloadEmpty, EGinkgoWeaponAction::ReloadEmpty);
		AddGunStep(ReloadEmpty, Root, FWeaponTags::Weapon_Step_ReloadEmpty, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& ReloadTactical = Root.AddChildState(TEXT("ReloadTactical"));
		ReloadTactical.Description = TEXT("有弹重装填：膛内已有活弹。折开炮管后高举利用重力甩出旧榴弹，再装入新榴弹合膛");
		EnterWhen(ReloadTactical, EGinkgoWeaponAction::ReloadTactical);
		AddGunStep(ReloadTactical, Root, FWeaponTags::Weapon_Step_ReloadTactical, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& CloseBolt = Root.AddChildState(TEXT("CloseBolt"));
		CloseBolt.Description = TEXT("合膛闭锁：新榴弹已入膛但未合上炮管（换弹后段被切枪打断，拔出接着合膛）");
		EnterWhen(CloseBolt, EGinkgoWeaponAction::CloseBolt);
		AddGunStep(CloseBolt, Root, FWeaponTags::Weapon_Step_CloseBolt, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& IdleRelax = Root.AddChildState(TEXT("IdleRelax"));
		IdleRelax.Description = TEXT("放松：久站不动单手垂枪活动肩颈，一动即打断");
		EnterWhen(IdleRelax, EGinkgoWeaponAction::IdleRelax);
		AddGunStep(IdleRelax, Root, FWeaponTags::Weapon_Step_IdleRelax, /*bAllowAim*/ true, /*bAllowRaise*/ true, /*bBothHands*/ false);
		ReselectUnless(IdleRelax, Root, EGinkgoWeaponAction::IdleRelax);

		AddFireAndReady(Root);
	}

	/** 子树：开膛待击、不装填（MG3，Docs/Design/ActionStates.md「MG3」）：只有放松、开火（空扣也在里面）、就绪 */
	void BuildOpenBoltTree(UStateTreeState& Root)
	{
		UStateTreeState& IdleRelax = Root.AddChildState(TEXT("IdleRelax"));
		IdleRelax.Description = TEXT("放松：站了一阵放一次，一动就打断");
		EnterWhen(IdleRelax, EGinkgoWeaponAction::IdleRelax);
		AddGunStep(IdleRelax, Root, FWeaponTags::Weapon_Step_IdleRelax, /*bAllowAim*/ true, /*bAllowRaise*/ true, /*bBothHands*/ false);
		ReselectUnless(IdleRelax, Root, EGinkgoWeaponAction::IdleRelax);

		AddFireAndReady(Root);
	}

	/** 子树：栓动（Docs/Design/ActionStates.md「每种枪机一棵子树」）。按顺序选第一个条件成立的，每做完一步回到根重选 */
	void BuildBoltTree(UStateTreeState& Root)
	{
		UStateTreeState& CloseBolt = Root.AddChildState(TEXT("CloseBolt"));
		CloseBolt.Description = TEXT("闭栓：栓开着，弹仓有弹");
		EnterWhen(CloseBolt, EGinkgoWeaponAction::CloseBolt);
		AddGunStep(CloseBolt, Root, FWeaponTags::Weapon_Step_CloseBolt, /*bAllowAim*/ false, /*bAllowRaise*/ true, /*bBothHands*/ true);

		UStateTreeState& CycleBolt = Root.AddChildState(TEXT("CycleBolt"));
		CycleBolt.Description = TEXT("拉栓：膛里空壳，栓关着。一个片段：抛壳 + 上膛");
		EnterWhen(CycleBolt, EGinkgoWeaponAction::CycleBolt);
		AddGunStep(CycleBolt, Root, FWeaponTags::Weapon_Step_CycleBolt, /*bAllowAim*/ false, /*bAllowRaise*/ true, /*bBothHands*/ true);

		UStateTreeState& Reload = Root.AddChildState(TEXT("Reload"), EStateTreeStateType::Group);
		Reload.Description = TEXT("装填：想装填，还能压。按开火、开镜时这一发压完就停（你定的）");

		UStateTreeState& OpenBolt = Reload.AddChildState(TEXT("OpenBolt"));
		OpenBolt.Description = TEXT("开栓：栓关着。停在手还在拉柄上");
		EnterWhen(OpenBolt, EGinkgoWeaponAction::OpenBolt);
		AddGunStep(OpenBolt, Root, FWeaponTags::Weapon_Step_OpenBolt, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& ReloadStart = Reload.AddChildState(TEXT("ReloadStart"));
		ReloadStart.Description = TEXT("起手：栓开着，手不在拉柄上（拔枪后接着压）");
		EnterWhen(ReloadStart, EGinkgoWeaponAction::ReloadStart);
		AddGunStep(ReloadStart, Root, FWeaponTags::Weapon_Step_ReloadStart, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& LoadRound = Reload.AddChildState(TEXT("LoadRound"));
		LoadRound.Description = TEXT("压一发：做完回根，两发之间能打断");
		EnterWhen(LoadRound, EGinkgoWeaponAction::LoadRound);
		AddGunStep(LoadRound, Root, FWeaponTags::Weapon_Step_LoadRound, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		AddFireAndReady(Root);
	}

	/**
	 * 子树：泵动、管状弹仓（霰弹枪，Docs/Design/ActionStates.md「霰弹枪」）。同栓动按顺序选、每做完一步回到根重选，差别在 Ginkgo::Weapon::SelectWeaponAction 里：
	 * 不开栓，抬到装填位压，压完收回来；打完一枪先泵好再去压，压完膛还空着就泵一下
	 */
	void BuildPumpTree(UStateTreeState& Root)
	{
		UStateTreeState& CloseBolt = Root.AddChildState(TEXT("CloseBolt"));
		CloseBolt.Description = TEXT("推回去：泵到一半被打断，护木还在后面");
		EnterWhen(CloseBolt, EGinkgoWeaponAction::CloseBolt);
		AddGunStep(CloseBolt, Root, FWeaponTags::Weapon_Step_CloseBolt, /*bAllowAim*/ false, /*bAllowRaise*/ true, /*bBothHands*/ true);

		UStateTreeState& CycleBolt = Root.AddChildState(TEXT("CycleBolt"));
		CycleBolt.Description = TEXT("泵动：膛里空壳（开完一枪），或者压完了膛还空着。一个片段：抛壳 + 上膛；枪在低持时放低持那条");
		EnterWhen(CycleBolt, EGinkgoWeaponAction::CycleBolt);
		AddGunStep(CycleBolt, Root, FWeaponTags::Weapon_Step_CycleBolt, /*bAllowAim*/ false, /*bAllowRaise*/ true, /*bBothHands*/ true);

		// 2026-11-07 加的（Docs/Design/Weapons.md「切换弹种」）
		UStateTreeState& LoadChamber = Root.AddChildState(TEXT("LoadChamber"));
		LoadChamber.Description = TEXT("抛壳口压膛：换了弹种、下一枪还不是它，或者枪里一发也没有时装填（紧急装填）。拉开、放一发进去、推回去；护木已经在后面时从放一发开始");
		EnterWhen(LoadChamber, EGinkgoWeaponAction::LoadChamber);
		AddGunStep(LoadChamber, Root, FWeaponTags::Weapon_Step_LoadChamber, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& Reload = Root.AddChildState(TEXT("Reload"), EStateTreeStateType::Group);
		Reload.Description = TEXT("装填：想装填，还能压。按开火、开镜时这一下压完就收回来（同 98k）");

		UStateTreeState& ReloadStart = Reload.AddChildState(TEXT("ReloadStart"));
		ReloadStart.Description = TEXT("起手：枪抬到装填位，左手到装填口下面");
		EnterWhen(ReloadStart, EGinkgoWeaponAction::ReloadStart);
		AddGunStep(ReloadStart, Root, FWeaponTags::Weapon_Step_ReloadStart, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& LoadRound = Reload.AddChildState(TEXT("LoadRound"));
		LoadRound.Description = TEXT("压：一次两发，只剩一发或者只缺一发时一发（片段在这一步里换）。做完回根，两下之间能打断");
		EnterWhen(LoadRound, EGinkgoWeaponAction::LoadRound);
		AddGunStep(LoadRound, Root, FWeaponTags::Weapon_Step_LoadRound, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& ReloadEnd = Reload.AddChildState(TEXT("ReloadEnd"));
		ReloadEnd.Description = TEXT("收回来：压满了、没子弹、按了开火或开镜，手回到护木上，枪放回低持");
		EnterWhen(ReloadEnd, EGinkgoWeaponAction::ReloadEnd);
		AddGunStep(ReloadEnd, Root, FWeaponTags::Weapon_Step_ReloadEnd, /*bAllowAim*/ false, /*bAllowRaise*/ false, /*bBothHands*/ true);

		UStateTreeState& IdleRelax = Root.AddChildState(TEXT("IdleRelax"));
		IdleRelax.Description = TEXT("放松：站了一阵放一次，一动就打断");
		EnterWhen(IdleRelax, EGinkgoWeaponAction::IdleRelax);
		AddGunStep(IdleRelax, Root, FWeaponTags::Weapon_Step_IdleRelax, /*bAllowAim*/ true, /*bAllowRaise*/ true, /*bBothHands*/ false);
		ReselectUnless(IdleRelax, Root, EGinkgoWeaponAction::IdleRelax);

		AddFireAndReady(Root);
	}

	/** 根树：所有枪共用（Docs/Design/ActionStates.md「树」） */
	void BuildRootTree(UStateTreeState& Root, UStateTree& ActionTree)
	{
		UStateTreeState& Unarmed = Root.AddChildState(TEXT("Unarmed"));
		Unarmed.Description = TEXT("徒手：手上没枪，等拔枪");
		EnterWhen(Unarmed, EGinkgoWeaponRootState::Unarmed);
		Unarmed.AddTask<FGinkgoWeaponIdleTask>();
		ReselectUnless(Unarmed, Root, EGinkgoWeaponRootState::Unarmed, EStateTreeTransitionPriority::Normal);

		UStateTreeState& Draw = Root.AddChildState(TEXT("Draw"));
		Draw.Description = TEXT("拔枪：放的时候不打断，按键只改放完以后去哪");
		EnterWhen(Draw, EGinkgoWeaponRootState::Draw);
		AddStep(Draw, Root, FWeaponTags::Weapon_Step_Draw);

		UStateTreeState& Armed = Root.AddChildState(TEXT("Armed"), EStateTreeStateType::Group);
		Armed.Description = TEXT("持枪：这一层接收枪、换枪，优先级高于子树");
		EnterWhen(Armed, EGinkgoWeaponRootState::Armed);
		ReselectUnless(Armed, Root, EGinkgoWeaponRootState::Armed, EStateTreeTransitionPriority::High);

		UStateTreeState& Action = Armed.AddChildState(TEXT("Action"), EStateTreeStateType::LinkedAsset);
		Action.Description = TEXT("枪的子树：按手上的枪换（定义的 ActionTree），没有时用这里链着的");
		Action.Tag = FWeaponTags::Weapon_State_Action;
		Action.SetLinkedStateAsset(&ActionTree);

		UStateTreeState& Stow = Root.AddChildState(TEXT("Stow"));
		Stow.Description = TEXT("收枪：放的时候不打断，放完看目标槽");
		EnterWhen(Stow, EGinkgoWeaponRootState::Stow);
		AddStep(Stow, Root, FWeaponTags::Weapon_Step_Stow);
	}

	/** 打出状态的层级 */
	void LogStates(const UStateTreeState& State, const int32 Depth)
	{
		UE_LOG(LogGinkgoWeaponStateTree, Display, TEXT("  %*s%s：%s"), Depth * 2, TEXT(""), *State.Name.ToString(), *State.Description);
		for (const UStateTreeState* Child : State.Children)
		{
			LogStates(*Child, Depth + 1);
		}
	}

	/**
	 * 建一棵树：用编辑器自己的工厂建（编辑器数据、schema、根状态都是它设好的），搭好、编译、存
	 * @return 树；编译不过时为空
	 */
	UStateTree* Create(const FString& PackageName, const TFunctionRef<void(UStateTreeState&)> Build)
	{
		const FName AssetName(*FPackageName::GetShortName(PackageName));
		UPackage* Package = CreatePackage(*PackageName);
		Package->FullyLoad();
		// 已有的挪开，在同一个包里重新建
		if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *AssetName.ToString()))
		{
			Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			Existing->ClearFlags(RF_Public | RF_Standalone);
		}
		UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();
		Factory->SetSchemaClass(UGinkgoWeaponStateTreeSchema::StaticClass());
		UStateTree* Tree = Cast<UStateTree>(Factory->FactoryCreateNew(UStateTree::StaticClass(), Package, AssetName, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		UStateTreeEditorData* EditorData = Tree ? Cast<UStateTreeEditorData>(Tree->EditorData) : nullptr;
		if (!EditorData || EditorData->SubTrees.IsEmpty())
		{
			UE_LOG(LogGinkgoWeaponStateTree, Error, TEXT("建不了树 %s"), *PackageName);
			return nullptr;
		}
		UStateTreeState& Root = *EditorData->SubTrees[0];
		Build(Root);
		FStateTreeCompilerLog Log;
		if (!UStateTreeEditingSubsystem::CompileStateTree(Tree, Log))
		{
			UE_LOG(LogGinkgoWeaponStateTree, Error, TEXT("树 %s 编译不过："), *PackageName);
			Log.DumpToLog(LogGinkgoWeaponStateTree);
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Tree);
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Tree, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponStateTree, Error, TEXT("存不了 %s"), *Filename);
			return nullptr;
		}
		UE_LOG(LogGinkgoWeaponStateTree, Display, TEXT("%s："), *PackageName);
		LogStates(Root, 0);
		return Tree;
	}

	template <typename T>
	T* LoadAssetByPackage(const TCHAR* PackageName)
	{
		return LoadObject<T>(nullptr, *(FString(PackageName) + TEXT(".") + FPackageName::GetShortName(PackageName)));
	}

	/**
	 * 一步：蒙太奇、右手的轨迹读哪份片段（不叠加的那份；开火的手一直在握把上，不要）、枪自己的片段、从哪一段开始，
	 * 片段自己带着进膛的那一发（抛壳口装填，FGinkgoWeaponStep::bNextRoundInClip）
	 */
	struct FStepClip
	{
		FGameplayTag Step;
		const TCHAR* Montage;
		const TCHAR* Track;
		const TCHAR* GunTrack = nullptr;
		FName StartSection = NAME_None;
		bool bNextRoundInClip = false;
	};

	/**
	 * 片段里左手整段不在枪上：蒙太奇的每段片段上都有 HandIK_L，每个键都是 0。这样的曲线压缩叠加片段时被丢掉
	 * （引擎烘叠加数据时出现过非 0 的值才建这条曲线），运行时读不到，记在这一步上（FGinkgoWeaponStep::bLeftHandOff）
	 */
	bool IsLeftHandOffClip(const UAnimMontage& Montage)
	{
		static const FName LeftHandIkCurveName(TEXT("HandIK_L"));
		bool bAnySegment = false;
		for (const FSlotAnimationTrack& Track : Montage.SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
			{
				const UAnimSequenceBase* Sequence = Segment.GetAnimReference().Get();
				const IAnimationDataModel* Model = Sequence ? Sequence->GetDataModel() : nullptr;
				const FFloatCurve* Curve = Model ? Model->FindFloatCurve(FAnimationCurveIdentifier(LeftHandIkCurveName, ERawCurveTrackTypes::RCT_Float)) : nullptr;
				if (!Curve || Curve->FloatCurve.GetNumKeys() == 0)
				{
					return false;
				}
				for (const FRichCurveKey& Key : Curve->FloatCurve.GetConstRefOfKeys())
				{
					if (!FMath::IsNearlyZero(Key.Value, 1.e-3f))
					{
						return false;
					}
				}
				bAnySegment = true;
			}
		}
		return bAnySegment;
	}

	/**
	 * 枪机的子树与片段填进枪的数据资产；SaveAs 不空时不动原来的，存一份副本。
	 * bCanLoadChamber 为真时机构打开"能直接压膛"（霰弹枪的抛壳口装填，2026-11-06 你定的）；别的项不动（手调过的数留着）
	 */
	bool ApplyToWeapon(const FString& WeaponPackage, const FString& SaveAs, UStateTree& Tree, const TConstArrayView<FStepClip> Clips, const bool bCanLoadChamber = false)
	{
		UGinkgoWeaponDefinition* Weapon = LoadAssetByPackage<UGinkgoWeaponDefinition>(*WeaponPackage);
		if (!Weapon)
		{
			UE_LOG(LogGinkgoWeaponStateTree, Error, TEXT("读不到枪 %s"), *WeaponPackage);
			return false;
		}
		const FString& TargetPackage = SaveAs.IsEmpty() ? WeaponPackage : SaveAs;
		if (!SaveAs.IsEmpty())
		{
			UPackage* Package = CreatePackage(*SaveAs);
			Package->FullyLoad();
			if (UObject* Existing = StaticFindObject(UObject::StaticClass(), Package, *FPackageName::GetShortName(SaveAs)))
			{
				Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				Existing->ClearFlags(RF_Public | RF_Standalone);
			}
			Weapon = Cast<UGinkgoWeaponDefinition>(StaticDuplicateObject(Weapon, Package, FName(FPackageName::GetShortName(SaveAs))));
			Weapon->SetFlags(RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Weapon);
		}
		Weapon->ActionTree = &Tree;
		if (bCanLoadChamber)
		{
			UGinkgoMagazineMechanism* Mechanism = Cast<UGinkgoMagazineMechanism>(Weapon->Mechanism);
			if (!Mechanism)
			{
				UE_LOG(LogGinkgoWeaponStateTree, Error, TEXT("%s 的机构不是 UGinkgoMagazineMechanism，开不了直接压膛"), *WeaponPackage);
				return false;
			}
			Mechanism->Rules.bCanLoadChamber = true;
			UE_LOG(LogGinkgoWeaponStateTree, Display, TEXT("%s：机构能直接压膛（抛壳口装填）"), *WeaponPackage);
		}
		for (const FStepClip& Clip : Clips)
		{
			UAnimMontage* Montage = LoadAssetByPackage<UAnimMontage>(Clip.Montage);
			UAnimSequence* Track = Clip.Track ? LoadAssetByPackage<UAnimSequence>(Clip.Track) : nullptr;
			UAnimSequence* GunTrack = Clip.GunTrack ? LoadAssetByPackage<UAnimSequence>(Clip.GunTrack) : nullptr;
			if (!Montage || (Clip.Track && !Track) || (Clip.GunTrack && !GunTrack))
			{
				UE_LOG(LogGinkgoWeaponStateTree, Error, TEXT("%s 的片段读不全：先跑 GinkgoAnimMontage"), *Clip.Step.ToString());
				return false;
			}
			FGinkgoWeaponStep& Step = Weapon->Steps.FindOrAdd(Clip.Step);
			Step.Montage = Montage;
			Step.RightHandTrack = Track;
			Step.GunTrack = GunTrack;
			Step.StartSection = Clip.StartSection;
			Step.bNextRoundInClip = Clip.bNextRoundInClip;
			Step.bLeftHandOff = IsLeftHandOffClip(*Montage);
			if (Step.bLeftHandOff)
			{
				UE_LOG(LogGinkgoWeaponStateTree, Display, TEXT("%s：片段里左手整段不在枪上（HandIK_L 全是 0），记在步上"), *Clip.Step.ToString());
			}
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(TargetPackage, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Weapon->GetPackage(), Weapon, *Filename, SaveArgs))
		{
			UE_LOG(LogGinkgoWeaponStateTree, Error, TEXT("存不了 %s"), *Filename);
			return false;
		}
		UE_LOG(LogGinkgoWeaponStateTree, Display, TEXT("%s：子树 %s，%d 步的片段"), *TargetPackage, *Tree.GetName(), Weapon->Steps.Num());
		return true;
	}

	/** 已有的不动（可能手改过），除非 -Force，或者 -Rebuild= 点了它的名字（ST_ActionPump，逗号分开：只重建这几棵） */
	UStateTree* FindOrCreate(const FString& PackageName, const bool bForce, const TFunctionRef<void(UStateTreeState&)> Build, const TConstArrayView<FString> Rebuild = {})
	{
		const bool bRebuild = Rebuild.Contains(FPackageName::GetShortName(PackageName));
		if (!bForce && !bRebuild && FPackageName::DoesPackageExist(PackageName))
		{
			const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetShortName(PackageName);
			UStateTree* Existing = LoadObject<UStateTree>(nullptr, *ObjectPath);
			UE_LOG(LogGinkgoWeaponStateTree, Display, TEXT("%s 已有，不动（-Force 才重建）"), *PackageName);
			return Existing;
		}
		return Create(PackageName, Build);
	}
}

UGinkgoWeaponStateTreeCommandlet::UGinkgoWeaponStateTreeCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGinkgoWeaponStateTreeCommandlet::Main(const FString& Params)
{
	using namespace GinkgoWeaponStateTree;
	const bool bForce = FParse::Param(*Params, TEXT("Force"));
	// -Rebuild=ST_ActionPump：只重建点了名的几棵（2026-11-07 霰弹枪加了抛壳口压膛那一步，别的树不动）
	FString RebuildList;
	TArray<FString> Rebuild;
	if (FParse::Value(*Params, TEXT("Rebuild="), RebuildList))
	{
		RebuildList.ParseIntoArray(Rebuild, TEXT(","));
	}
	UStateTree* Ready = FindOrCreate(ReadyTreePath, bForce, [](UStateTreeState& Root) { BuildReadyTree(Root); }, Rebuild);
	if (!Ready)
	{
		return 1;
	}
	const UStateTree* Root = FindOrCreate(RootTreePath, bForce, [Ready](UStateTreeState& RootState) { BuildRootTree(RootState, *Ready); }, Rebuild);
	UStateTree* Bolt = FindOrCreate(BoltTreePath, bForce, [](UStateTreeState& RootState) { BuildBoltTree(RootState); }, Rebuild);
	UStateTree* SemiAuto = FindOrCreate(SemiAutoTreePath, bForce, [](UStateTreeState& RootState) { BuildSemiAutoTree(RootState); }, Rebuild);
	UStateTree* Pump = FindOrCreate(PumpTreePath, bForce, [](UStateTreeState& RootState) { BuildPumpTree(RootState); }, Rebuild);
	UStateTree* OpenBolt = FindOrCreate(OpenBoltTreePath, bForce, [](UStateTreeState& RootState) { BuildOpenBoltTree(RootState); }, Rebuild);
	UStateTree* Break = FindOrCreate(BreakTreePath, bForce, [](UStateTreeState& RootState) { BuildBreakTree(RootState); }, Rebuild);
	if (!Root || !Root->IsReadyToRun() || !Bolt || !Bolt->IsReadyToRun() || !SemiAuto || !SemiAuto->IsReadyToRun() || !Pump || !Pump->IsReadyToRun()
		|| !OpenBolt || !OpenBolt->IsReadyToRun() || !Break || !Break->IsReadyToRun())
	{
		return 1;
	}
	FString WeaponPackage;
	if (FParse::Value(*Params, TEXT("Weapon="), WeaponPackage))
	{
		FString SaveAs;
		FParse::Value(*Params, TEXT("SaveAs="), SaveAs);
		const FStepClip Mauser98kClips[] = {
			{FWeaponTags::Weapon_Step_Fire, TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_Fire"), nullptr},
			{FWeaponTags::Weapon_Step_CycleBolt, TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_BoltCycle"), TEXT("/Game/Developers/JiahaoLi/Mauser98kFire/M_Mauser98k_BoltCycle_ADS")},
			{FWeaponTags::Weapon_Step_OpenBolt, TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_ReloadOpen"), TEXT("/Game/Developers/JiahaoLi/Mauser98kReload/M_Mauser98k_ReloadOpen")},
			{FWeaponTags::Weapon_Step_ReloadStart, TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_ReloadReach"), TEXT("/Game/Developers/JiahaoLi/Mauser98kReload/M_Mauser98k_ReloadReach")},
			{FWeaponTags::Weapon_Step_LoadRound, TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_ReloadLoad"), TEXT("/Game/Developers/JiahaoLi/Mauser98kReload/M_Mauser98k_ReloadLoad")},
			{FWeaponTags::Weapon_Step_CloseBolt, TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_ReloadClose"), TEXT("/Game/Developers/JiahaoLi/Mauser98kReload/M_Mauser98k_ReloadReachClose")},
		};
		// M1911：枪自己的片段（套筒、击锤、弹匣）跟着放；放枪机回去就是空仓换弹的后一段
		const FStepClip M1911Clips[] = {
			{FWeaponTags::Weapon_Step_Fire, TEXT("/Game/Weapons/M1911/AM_M1911_Fire"), nullptr, TEXT("/Game/Weapons/M1911/Anims/A_M1911_Fire")},
			{FWeaponTags::Weapon_Step_FireLast, TEXT("/Game/Weapons/M1911/AM_M1911_FireLast"), nullptr, TEXT("/Game/Weapons/M1911/Anims/A_M1911_FireLast")},
			{FWeaponTags::Weapon_Step_ReloadTactical, TEXT("/Game/Weapons/M1911/AM_M1911_ReloadTactical"), TEXT("/Game/Developers/JiahaoLi/M1911Reload/M_M1911_Reload_Tactical"), TEXT("/Game/Weapons/M1911/Anims/A_M1911_Reload_Tactical")},
			{FWeaponTags::Weapon_Step_ReloadEmpty, TEXT("/Game/Weapons/M1911/AM_M1911_ReloadEmpty"), TEXT("/Game/Developers/JiahaoLi/M1911Reload/M_M1911_Reload_Empty"), TEXT("/Game/Weapons/M1911/Anims/A_M1911_Reload_Empty")},
			{FWeaponTags::Weapon_Step_CloseBolt, TEXT("/Game/Weapons/M1911/AM_M1911_ReloadEmpty"), TEXT("/Game/Developers/JiahaoLi/M1911Reload/M_M1911_Reload_Empty"), TEXT("/Game/Weapons/M1911/Anims/A_M1911_Reload_Empty"), Ginkgo::WeaponSections::SlideRelease},
			{FWeaponTags::Weapon_Step_Draw, TEXT("/Game/Weapons/M1911/AM_M1911_Draw"), nullptr, TEXT("/Game/Weapons/M1911/Anims/A_M1911_Draw")},
			{FWeaponTags::Weapon_Step_Stow, TEXT("/Game/Weapons/M1911/AM_M1911_Stow"), nullptr, TEXT("/Game/Weapons/M1911/Anims/A_M1911_Stow")},
			{FWeaponTags::Weapon_Step_IdleRelax, TEXT("/Game/Weapons/M1911/AM_M1911_IdleRelax"), nullptr},
		};
		// AK-47：同 M1911 是自动装填、可拆弹匣，共用一棵子树，连发是定义上的射击模式。没有打空的片段（枪机不挂住）；拉机柄就是空仓换弹的后一段
		const FStepClip AK47Clips[] = {
			{FWeaponTags::Weapon_Step_Fire, TEXT("/Game/Weapons/AK47/AM_AK47_Fire"), nullptr, TEXT("/Game/Weapons/AK47/Anims/A_AK47_Fire")},
			{FWeaponTags::Weapon_Step_ReloadTactical, TEXT("/Game/Weapons/AK47/AM_AK47_ReloadTactical"), TEXT("/Game/Developers/JiahaoLi/AK47Reload/M_AK47_Reload_Tactical"), TEXT("/Game/Weapons/AK47/Anims/A_AK47_Reload_Tactical")},
			{FWeaponTags::Weapon_Step_ReloadEmpty, TEXT("/Game/Weapons/AK47/AM_AK47_ReloadEmpty"), TEXT("/Game/Developers/JiahaoLi/AK47Reload/M_AK47_Reload_Empty"), TEXT("/Game/Weapons/AK47/Anims/A_AK47_Reload_Empty")},
			{FWeaponTags::Weapon_Step_CloseBolt, TEXT("/Game/Weapons/AK47/AM_AK47_ReloadEmpty"), TEXT("/Game/Developers/JiahaoLi/AK47Reload/M_AK47_Reload_Empty"), TEXT("/Game/Weapons/AK47/Anims/A_AK47_Reload_Empty"), Ginkgo::WeaponSections::SlideRelease},
			{FWeaponTags::Weapon_Step_Draw, TEXT("/Game/Weapons/AK47/AM_AK47_Draw"), nullptr, TEXT("/Game/Weapons/AK47/Anims/A_AK47_Draw")},
			{FWeaponTags::Weapon_Step_Stow, TEXT("/Game/Weapons/AK47/AM_AK47_Stow"), nullptr, TEXT("/Game/Weapons/AK47/Anims/A_AK47_Stow")},
			{FWeaponTags::Weapon_Step_IdleRelax, TEXT("/Game/Weapons/AK47/AM_AK47_IdleRelax"), nullptr},
		};
		// 泵动霰弹枪：右手一直在握把上（左手泵、压），不要右手的轨迹；枪自己的片段跟着放。泵到一半被打断的推回去是泵动的后一段
		const FStepClip ShotgunClips[] = {
			{FWeaponTags::Weapon_Step_Fire, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_Fire"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Fire")},
			{FWeaponTags::Weapon_Step_CycleBolt, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_PumpHip"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Pump_Hip")},
			{FWeaponTags::Weapon_Step_CycleBoltLowered, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_PumpIdle"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Pump_Idle")},
			{FWeaponTags::Weapon_Step_CloseBolt, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_PumpHip"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Pump_Hip"), Ginkgo::WeaponSections::BoltClose},
			{FWeaponTags::Weapon_Step_ReloadStart, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_ReloadStart"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Reload_Start")},
			{FWeaponTags::Weapon_Step_LoadRound, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_LoadOne"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Reload_One")},
			{FWeaponTags::Weapon_Step_LoadRoundTwin, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_LoadTwo"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Reload_Twin")},
			{FWeaponTags::Weapon_Step_ReloadEnd, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_ReloadEnd"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Reload_End")},
			// 抛壳口装填（2026-11-07）：手里那一发就是枪上的 shell_feed，放这一步时不按供弹缩它
			{FWeaponTags::Weapon_Step_LoadChamber, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_LoadPort"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Reload_Port"), NAME_None,
				/*bNextRoundInClip*/ true},
			{FWeaponTags::Weapon_Step_Draw, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_Draw"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Draw")},
			{FWeaponTags::Weapon_Step_Stow, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_Stow"), nullptr, TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Stow")},
			{FWeaponTags::Weapon_Step_IdleRelax, TEXT("/Game/Weapons/Shotgun/AM_Shotgun_IdleRelax"), nullptr},
		};
		// MG3：开膛待击、不装填、收不起来，只有开火、放松，拔枪是捡起来以后接的后一段
		const FStepClip MG3Clips[] = {
			{FWeaponTags::Weapon_Step_Fire, TEXT("/Game/Weapons/MG3/AM_MG3_Fire"), nullptr, TEXT("/Game/Weapons/MG3/Anims/A_MG3_Fire")},
			{FWeaponTags::Weapon_Step_Draw, TEXT("/Game/Weapons/MG3/AM_MG3_Draw"), nullptr, TEXT("/Game/Weapons/MG3/Anims/A_MG3_Draw")},
			{FWeaponTags::Weapon_Step_IdleRelax, TEXT("/Game/Weapons/MG3/AM_MG3_IdleRelax"), nullptr},
		};
		// M79：单发中折，区分有弹重装填（折开高举重力甩弹）与空仓换弹（捏取退壳）；合膛闭锁复用空仓换弹的合膛尾段
		const FStepClip M79Clips[] = {
			{FWeaponTags::Weapon_Step_Fire, TEXT("/Game/Weapons/M79/AM_M79_Fire"), nullptr, TEXT("/Game/Weapons/M79/Anims/A_M79_Fire")},
			{FWeaponTags::Weapon_Step_ReloadTactical, TEXT("/Game/Weapons/M79/AM_M79_ReloadTactical"), TEXT("/Game/Developers/JiahaoLi/M79Reload/M_M79_Reload_Tactical"), TEXT("/Game/Weapons/M79/Anims/A_M79_Reload_Tactical")},
			{FWeaponTags::Weapon_Step_ReloadEmpty, TEXT("/Game/Weapons/M79/AM_M79_ReloadEmpty"), TEXT("/Game/Developers/JiahaoLi/M79Reload/M_M79_Reload_Empty"), TEXT("/Game/Weapons/M79/Anims/A_M79_Reload_Empty")},
			{FWeaponTags::Weapon_Step_CloseBolt, TEXT("/Game/Weapons/M79/AM_M79_ReloadEmpty"), TEXT("/Game/Developers/JiahaoLi/M79Reload/M_M79_Reload_Empty"), TEXT("/Game/Weapons/M79/Anims/A_M79_Reload_Empty"), Ginkgo::WeaponSections::BoltClose},
			{FWeaponTags::Weapon_Step_Draw, TEXT("/Game/Weapons/M79/AM_M79_Draw"), nullptr, TEXT("/Game/Weapons/M79/Anims/A_M79_Draw")},
			{FWeaponTags::Weapon_Step_Stow, TEXT("/Game/Weapons/M79/AM_M79_Stow"), nullptr, TEXT("/Game/Weapons/M79/Anims/A_M79_Stow")},
			{FWeaponTags::Weapon_Step_IdleRelax, TEXT("/Game/Weapons/M79/AM_M79_IdleRelax"), nullptr},
		};
		const bool bM79 = WeaponPackage.Contains(TEXT("M79"));
		const bool bMG3 = WeaponPackage.Contains(TEXT("MG3"));
		const bool bM1911 = WeaponPackage.Contains(TEXT("M1911"));
		const bool bAK47 = WeaponPackage.Contains(TEXT("AK47"));
		const bool bShotgun = WeaponPackage.Contains(TEXT("Shotgun"));
		const TConstArrayView<FStepClip> Clips = bM79 ? TConstArrayView<FStepClip>(M79Clips) : bMG3 ? TConstArrayView<FStepClip>(MG3Clips) : bShotgun ? TConstArrayView<FStepClip>(ShotgunClips)
			: bAK47 ? TConstArrayView<FStepClip>(AK47Clips) : bM1911 ? TConstArrayView<FStepClip>(M1911Clips) : TConstArrayView<FStepClip>(Mauser98kClips);
		UStateTree& Tree = bM79 ? *Break : bMG3 ? *OpenBolt : bShotgun ? *Pump : bM1911 || bAK47 ? *SemiAuto : *Bolt;
		return ApplyToWeapon(WeaponPackage, SaveAs, Tree, Clips, /*bCanLoadChamber*/ bShotgun || bM79) ? 0 : 1;
	}
	return 0;
}

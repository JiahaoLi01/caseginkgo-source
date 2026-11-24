#include "Animation/GinkgoAnimBlueprintCommandlet.h"
#include "AnimGraphNode_GinkgoPeekReference.h"

#include "AnimGraph/AnimGraphNode_OrientationWarping.h"
#include "AnimGraph/AnimGraphNode_StrideWarping.h"
#include "AnimGraphNode_ApplyMeshSpaceAdditive.h"
#include "AnimPose.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_ControlRig.h"
#include "AnimGraphNode_DeadBlending.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_RotateRootBone.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/GinkgoAnimInstance.h"
#include "Animation/GinkgoWeaponAnimLayers.h"
#include "Animation/RigUnit_GinkgoHandReach.h"
#include "Animation/RigUnit_GinkgoLegIK.h"
#include "Animation/RigUnit_GinkgoWeaponAim.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "BoneControllers/AnimNode_StrideWarping.h"
#include "ControlRigAssetFactory.h"
#include "ControlRigEditorAsset.h"
#include "ControlRigRuntimeAsset.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Editor.h"
#include "Engine/MemberReference.h"
#include "Engine/SkeletalMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "HAL/FileManager.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Rigs/RigHierarchyController.h"
#include "RigVMFunctions/RigVMFunction_ControlFlow.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "RigVMModel/RigVMController.h"
#include "StaticMeshResources.h"
#include "UObject/SavePackage.h"
#include "UObject/Linker.h"
#include "Units/Execution/RigUnit_BeginExecution.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Utilities/Enumeration/Tags/WeaponTags.h"
#include "Weapon/GinkgoRecoilPattern.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoWeaponMechanism.h"

namespace
{
	/** 生成的动画蓝图：本工程自己的资产放在 Characters/Ginkgo，不放进与 GASP 同路径的 UEFN_Mannequin */
	const TCHAR* const BlueprintPackageName = TEXT("/Game/Characters/Ginkgo/ABP_GinkgoCharacter");

	const TCHAR* const SkeletonPath = TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SK_UEFN_Mannequin.SK_UEFN_Mannequin");

	/** 角色的 Control Rig：腿部 IK（以后手部的全身 IK、瞄准也放这里），与动画蓝图放在一起 */
	const TCHAR* const ControlRigPackageName = TEXT("/Game/Characters/Ginkgo/CR_GinkgoCharacter");

	/** Control Rig 的骨骼、曲线从这个网格导入，也是它的预览网格 */
	const TCHAR* const MeshPath = TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin");

	/** Control Rig 里腿部 IK 权重的公开变量：动画蓝图的 Control Rig 节点上露成引脚，接动画实例的同名成员 */
	const TCHAR* const LegIKAlphaVariable = TEXT("LegIKAlpha");

	/** 导入命令搬来的动画，按 GASP 的子目录 */
	const TCHAR* const AnimationRoot = TEXT("/Game/Characters/UEFN_Mannequin/Animations");

	/**
	 * 状态机里各状态的转换都发惯性化的请求（图里由 Dead Blending 节点接），时长（s）。
	 * 状态切得频繁（0.1 s 量级），交叉淡化要同时求两个姿势、混合期间脚还会滑
	 */
	constexpr float DefaultBlendTime = 0.2f;

	// ===== 图的小工具 =====

	/** 在图里放一个节点：先配置（变量引用、节点的值这些决定引脚的东西），再生成引脚 */
	template <typename TNode, typename TConfigure>
	TNode* SpawnNode(UEdGraph& Graph, const int32 X, const int32 Y, TConfigure&& Configure)
	{
		FGraphNodeCreator<TNode> Creator(Graph);
		TNode* Node = Creator.CreateNode();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Configure(*Node);
		Creator.Finalize();
		return Node;
	}

	template <typename TNode>
	TNode* SpawnNode(UEdGraph& Graph, const int32 X, const int32 Y)
	{
		return SpawnNode<TNode>(Graph, X, Y, [](TNode&) {});
	}

	/** 节点上第一个这个方向的姿势引脚（局部空间或组件空间） */
	UEdGraphPin* FindPosePin(const UEdGraphNode& Node, const EEdGraphPinDirection Direction)
	{
		for (UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin->Direction == Direction && UAnimationGraphSchema::IsPosePin(Pin->PinType))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** 连两个引脚，连不上时记一条问题 */
	void Connect(UEdGraphPin* From, UEdGraphPin* To, TArray<FString>& Problems)
	{
		if (!From || !To || !From->GetSchema()->TryCreateConnection(From, To))
		{
			Problems.Add(FString::Printf(TEXT("连不上 %s.%s → %s.%s"), From ? *From->GetOwningNode()->GetName() : TEXT("?"), From ? *From->PinName.ToString() : TEXT("?"),
				To ? *To->GetOwningNode()->GetName() : TEXT("?"), To ? *To->PinName.ToString() : TEXT("?")));
		}
	}

	/** 前一个节点的姿势输出接到后一个节点的姿势输入 */
	void ConnectPose(const UEdGraphNode& From, const UEdGraphNode& To, TArray<FString>& Problems)
	{
		Connect(FindPosePin(From, EGPD_Output), FindPosePin(To, EGPD_Input), Problems);
	}

	/** 动画节点结构里的一个值（节点的成员多半是私有的，按反射取） */
	template <typename T>
	T& NodeValue(UAnimGraphNode_Base& GraphNode, const FName PropertyName)
	{
		const FStructProperty* NodeProperty = GraphNode.GetFNodeProperty();
		const FProperty* Property = NodeProperty->Struct->FindPropertyByName(PropertyName);
		checkf(Property, TEXT("%s 没有 %s"), *NodeProperty->Struct->GetName(), *PropertyName.ToString());
		return *Property->ContainerPtrToValuePtr<T>(NodeProperty->ContainerPtrToValuePtr<void>(&GraphNode));
	}

	/** 读动画实例成员的变量节点，接到某个节点的引脚上 */
	void BindMember(UEdGraph& Graph, UEdGraphNode& Target, const FName PinName, const FName Member, const int32 X, const int32 Y, TArray<FString>& Problems)
	{
		UK2Node_VariableGet* Get = SpawnNode<UK2Node_VariableGet>(Graph, X, Y, [Member](UK2Node_VariableGet& Node) { Node.VariableReference.SetSelfMember(Member); });
		Connect(Get->GetValuePin(), Target.FindPin(PinName), Problems);
	}

	/** 一段说明，框住一片节点 */
	void AddComment(UEdGraph& Graph, const int32 X, const int32 Y, const int32 Width, const int32 Height, const TCHAR* Text)
	{
		SpawnNode<UEdGraphNode_Comment>(Graph, X, Y, [&](UEdGraphNode_Comment& Comment)
		{
			Comment.NodeWidth = Width;
			Comment.NodeHeight = Height;
			Comment.NodeComment = Text;
		});
	}

	// ===== 状态机 =====

	/** 一个状态与它的说明（挂在节点上的气泡） */
	UAnimStateNode* AddState(UAnimationStateMachineGraph& Machine, const TCHAR* Name, const int32 X, const int32 Y, const TCHAR* Comment)
	{
		UAnimStateNode* State = SpawnNode<UAnimStateNode>(Machine, X, Y);
		FBlueprintEditorUtils::RenameGraph(State->BoundGraph, Name);
		State->NodeComment = Comment;
		State->bCommentBubbleVisible = true;
		State->bCommentBubblePinned = true;
		return State;
	}

	/**
	 * @brief 状态里放一个序列播放器（循环）或求值器（由节点函数给时刻），接到状态的结果上，绑上节点函数
	 * @param BecomeRelevant - 进入状态时调的节点函数，没有时为 NAME_None
	 */
	UAnimGraphNode_Base* AddSequenceNode(UAnimStateNode& State, const bool bEvaluator, UAnimSequence* Preview, const FName BecomeRelevant, const FName Update,
		TArray<FString>& Problems)
	{
		UAnimationStateGraph& Graph = *CastChecked<UAnimationStateGraph>(State.BoundGraph);
		UAnimGraphNode_Base* Node = nullptr;
		if (bEvaluator)
		{
			// 片段与时刻都由节点函数设：两个值要动态（Always Dynamic），否则编译时折叠成常量、设不进去；不循环
			Node = SpawnNode<UAnimGraphNode_SequenceEvaluator>(Graph, -400, 0);
			NodeValue<TObjectPtr<UAnimSequenceBase>>(*Node, TEXT("Sequence")) = Preview;
			NodeValue<bool>(*Node, TEXT("bShouldLoop")) = false;
			Node->AlwaysDynamicProperties.Add(TEXT("Sequence"));
			Node->AlwaysDynamicProperties.Add(TEXT("ExplicitTime"));
		}
		else
		{
			// 片段由节点函数惯性化地换（Sequence 本来就不折叠），播放速率要动态
			Node = SpawnNode<UAnimGraphNode_SequencePlayer>(Graph, -400, 0);
			NodeValue<TObjectPtr<UAnimSequenceBase>>(*Node, TEXT("Sequence")) = Preview;
			Node->AlwaysDynamicProperties.Add(TEXT("PlayRate"));
		}
		if (BecomeRelevant != NAME_None)
		{
			Node->BecomeRelevantFunction.SetSelfMember(BecomeRelevant);
		}
		Node->UpdateFunction.SetSelfMember(Update);
		ConnectPose(*Node, *Graph.MyResultNode, Problems);
		return Node;
	}

	/** 两个状态之间的转换：规则读动画实例的一个条件，惯性化，优先级小的先判 */
	void AddTransition(UAnimationStateMachineGraph& Machine, UAnimStateNode* From, UAnimStateNode* To, const FName Condition, const int32 Priority,
		TArray<FString>& Problems, const float BlendTime = DefaultBlendTime)
	{
		UAnimStateTransitionNode* Transition = SpawnNode<UAnimStateTransitionNode>(Machine, (From->NodePosX + To->NodePosX) / 2, (From->NodePosY + To->NodePosY) / 2);
		Transition->CreateConnections(From, To);
		Transition->PriorityOrder = Priority;
		Transition->LogicType = ETransitionLogicType::TLT_Inertialization;
		Transition->CrossfadeDuration = BlendTime;
		UAnimationTransitionGraph& Rule = *CastChecked<UAnimationTransitionGraph>(Transition->BoundGraph);
		BindMember(Rule, *Rule.MyResultNode, TEXT("bCanEnterTransition"), Condition, -300, 0, Problems);
	}

	// ===== 片段 =====

	/** 按 GASP 的名字找导入的片段；找不到时记一条问题 */
	UAnimSequence* FindAnimation(const TCHAR* Folder, const FString& Name, TArray<FString>& Problems)
	{
		const FString Path = FString::Printf(TEXT("%s/%s/%s.%s"), AnimationRoot, Folder, *Name, *Name);
		UAnimSequence* Sequence = LoadObject<UAnimSequence>(nullptr, *Path);
		if (!Sequence)
		{
			Problems.Add(FString::Printf(TEXT("找不到片段 %s（先跑导入命令 GinkgoAnimationImport）"), *Path));
		}
		return Sequence;
	}

	/** 六个方向的名字（GASP 的 F、B、LL、LR、RL、RR）与反向片段的名字（按原来的方向） */
	const TCHAR* const DirectionNames[] = { TEXT("F"), TEXT("B"), TEXT("LL"), TEXT("LR"), TEXT("RL"), TEXT("RR") };
	const TCHAR* const PivotNames[] = { TEXT("F_B"), TEXT("B_F"), TEXT("LL_RL"), TEXT("LR_RR"), TEXT("RL_LL"), TEXT("RR_LR") };

	/** 一组六个方向的片段，名字由方向的名字拼出来；bAllDirections 为假时只有 F（冲刺） */
	void FillDirectional(FGinkgoDirectionalAnims& Anims, const TCHAR* Folder, const bool bAllDirections, TFunctionRef<FString(int32)> MakeName, TArray<FString>& Problems)
	{
		TObjectPtr<UAnimSequence>* Slots[] = { &Anims.F, &Anims.B, &Anims.LL, &Anims.LR, &Anims.RL, &Anims.RR };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Slots); ++Index)
		{
			*Slots[Index] = Index == 0 || bAllDirections ? FindAnimation(Folder, MakeName(Index), Problems) : nullptr;
		}
	}

	/** 一种步态：循环、起步（左脚那套）、急停与反向的左右脚两套。冲刺只有向前，也没有反向 */
	void FillGait(FGinkgoGaitAnims& Anims, const TCHAR* Gait, TArray<FString>& Problems)
	{
		const bool bAllDirections = FCString::Strcmp(Gait, TEXT("Sprint")) != 0;
		FillDirectional(Anims.Cycle, Gait, bAllDirections, [Gait](const int32 Index) { return FString::Printf(TEXT("M_Neutral_%s_Loop_%s"), Gait, DirectionNames[Index]); }, Problems);
		FillDirectional(Anims.Start, Gait, bAllDirections, [Gait](const int32 Index) { return FString::Printf(TEXT("M_Neutral_%s_Start_%s_Lfoot"), Gait, DirectionNames[Index]); },
			Problems);
		FillDirectional(Anims.StopLeftFoot, Gait, bAllDirections,
			[Gait](const int32 Index) { return FString::Printf(TEXT("M_Neutral_%s_Stop_%s_Lfoot"), Gait, DirectionNames[Index]); }, Problems);
		FillDirectional(Anims.StopRightFoot, Gait, bAllDirections,
			[Gait](const int32 Index) { return FString::Printf(TEXT("M_Neutral_%s_Stop_%s_Rfoot"), Gait, DirectionNames[Index]); }, Problems);
		if (bAllDirections)
		{
			FillDirectional(Anims.PivotLeftFoot, Gait, true, [Gait](const int32 Index) { return FString::Printf(TEXT("M_Neutral_%s_Pivot_%s_Lfoot"), Gait, PivotNames[Index]); },
				Problems);
			FillDirectional(Anims.PivotRightFoot, Gait, true, [Gait](const int32 Index) { return FString::Printf(TEXT("M_Neutral_%s_Pivot_%s_Rfoot"), Gait, PivotNames[Index]); },
				Problems);
		}
	}

	/** 站着或蹲着：Idle 循环与 45°、90°、135°、180° 的原地转身 */
	void FillStance(FGinkgoStanceAnims& Anims, const TCHAR* IdleFolder, const TCHAR* IdleName, const TCHAR* TurnFolder, const TCHAR* TurnPrefix, TArray<FString>& Problems)
	{
		Anims.Idle = FindAnimation(IdleFolder, IdleName, Problems);
		Anims.TurnLeft.Reset();
		Anims.TurnRight.Reset();
		for (const TCHAR* Angle : { TEXT("045"), TEXT("090"), TEXT("135"), TEXT("180") })
		{
			Anims.TurnLeft.Add(FindAnimation(TurnFolder, FString::Printf(TEXT("%s_%s_L"), TurnPrefix, Angle), Problems));
			Anims.TurnRight.Add(FindAnimation(TurnFolder, FString::Printf(TEXT("%s_%s_R"), TurnPrefix, Angle), Problems));
		}
	}

	/** 滑铲：GASP 的 FootOut 一套（另有 KneesOut，在类默认值里换） */
	void FillSlide(FGinkgoSlideAnims& Anims, TArray<FString>& Problems)
	{
		const auto Find = [&Problems](const TCHAR* Suffix) { return FindAnimation(TEXT("Slide"), FString::Printf(TEXT("M_Neutral_Slide_FootOut_%s"), Suffix), Problems); };
		Anims.IntoLeftFoot = Find(TEXT("Into_Lfoot"));
		Anims.IntoRightFoot = Find(TEXT("Into_Rfoot"));
		Anims.Loop = Find(TEXT("Loop"));
		Anims.ExitIdleStand = Find(TEXT("Out_Idle_Stand"));
		Anims.ExitIdleCrouch = Find(TEXT("Out_Idle_Crouch"));
		Anims.ExitMovingWalk = Find(TEXT("Out_Moving_Walk"));
		Anims.ExitMovingRun = Find(TEXT("Out_Moving_Run"));
		Anims.ExitMovingCrouch = Find(TEXT("Out_Moving_Crouch"));
	}

	template <typename T>
	T& DefaultValue(UGinkgoAnimInstance& Defaults, const TCHAR* PropertyName)
	{
		const FProperty* Property = UGinkgoAnimInstance::StaticClass()->FindPropertyByName(PropertyName);
		checkf(Property, TEXT("UGinkgoAnimInstance 没有 %s"), PropertyName);
		return *Property->ContainerPtrToValuePtr<T>(&Defaults);
	}

	/** 类默认值里的片段，按 GASP 的名字（Neutral 的那套） */
	void FillAnimations(UGinkgoAnimInstance& Defaults, TArray<FString>& Problems)
	{
		FillGait(DefaultValue<FGinkgoGaitAnims>(Defaults, TEXT("WalkAnims")), TEXT("Walk"), Problems);
		FillGait(DefaultValue<FGinkgoGaitAnims>(Defaults, TEXT("RunAnims")), TEXT("Run"), Problems);
		FillGait(DefaultValue<FGinkgoGaitAnims>(Defaults, TEXT("SprintAnims")), TEXT("Sprint"), Problems);
		FillGait(DefaultValue<FGinkgoGaitAnims>(Defaults, TEXT("CrouchAnims")), TEXT("Crouch"), Problems);
		FillStance(DefaultValue<FGinkgoStanceAnims>(Defaults, TEXT("StandAnims")), TEXT("Idle"), TEXT("M_Neutral_Stand_Idle_Loop"), TEXT("Idle"), TEXT("M_Neutral_Stand_Turn"),
			Problems);
		FillStance(DefaultValue<FGinkgoStanceAnims>(Defaults, TEXT("CrouchedAnims")), TEXT("Idle"), TEXT("M_Neutral_Crouch_Idle_Loop"), TEXT("Crouch"),
			TEXT("M_Neutral_Crouch_Idle_Turn"), Problems);
		DefaultValue<TObjectPtr<UAnimSequence>>(Defaults, TEXT("FallLoop")) = FindAnimation(TEXT("Jump"), TEXT("M_Neutral_Jump_Loop_Fall"), Problems);
		FillSlide(DefaultValue<FGinkgoSlideAnims>(Defaults, TEXT("SlideAnims")), Problems);
	}

	// ===== 搭图 =====

	/** 移动状态机：地面的七个状态、滑铲的三个，与它们之间的转换 */
	void BuildLocomotion(UAnimationStateMachineGraph& Machine, const UGinkgoAnimInstance& Defaults, TArray<FString>& Problems)
	{
		const FGinkgoGaitAnims& Run = DefaultValue<FGinkgoGaitAnims>(const_cast<UGinkgoAnimInstance&>(Defaults), TEXT("RunAnims"));
		const FGinkgoStanceAnims& Stand = DefaultValue<FGinkgoStanceAnims>(const_cast<UGinkgoAnimInstance&>(Defaults), TEXT("StandAnims"));
		UAnimSequence* Fall = DefaultValue<TObjectPtr<UAnimSequence>>(const_cast<UGinkgoAnimInstance&>(Defaults), TEXT("FallLoop"));
		const FGinkgoSlideAnims& Slide = DefaultValue<FGinkgoSlideAnims>(const_cast<UGinkgoAnimInstance&>(Defaults), TEXT("SlideAnims"));

		UAnimStateNode* Idle = AddState(Machine, TEXT("Idle"), 0, 0, TEXT("站着或蹲着的循环。视角转了，网格留在原地（根骨骼的偏航补偿）"));
		UAnimStateNode* Turn = AddState(Machine, TEXT("TurnInPlace"), 0, 350, TEXT("偏航补偿超过阈值：按补偿的方向与大小选 45°–180° 的转身，片段的剩余转角把补偿还回去"));
		UAnimStateNode* Start = AddState(Machine, TEXT("Start"), 450, -250, TEXT("起步：按意图的方向选片段，按走过的距离推进，走过 40 cm 接循环"));
		UAnimStateNode* Cycle = AddState(Machine, TEXT("Cycle"), 900, 0, TEXT("循环：按方向与步态换片段（步伐对上），播放速率与步幅分担速度比"));
		UAnimStateNode* Stop = AddState(Machine, TEXT("Stop"), 450, 250, TEXT("急停：对到预测的停止点（距离匹配），停下以后放完收势"));
		UAnimStateNode* Pivot = AddState(Machine, TEXT("Pivot"), 900, 400, TEXT("反向：过零之前对到预测的反向点，之后按走过的距离推进"));
		UAnimStateNode* Air = AddState(Machine, TEXT("Air"), 450, -600, TEXT("空中：下落循环。起跳、下落、落地在第三步细分"));
		UAnimStateNode* SlideStart = AddState(Machine, TEXT("SlideStart"), 1400, -250,
			TEXT("滑铲的进入：按倒下去的那只脚选 Into，从那只脚最后一次落地放起。网格的根转向速度（胶囊照常面向视角）"));
		UAnimStateNode* SlideLoop = AddState(Machine, TEXT("SlideLoop"), 1850, 0, TEXT("滑铲的滑行：循环，不按地速"));
		UAnimStateNode* SlideExit = AddState(Machine, TEXT("SlideExit"), 1400, 250, TEXT("滑铲结束：按还蹲不蹲、有没有意图选起身的片段，起来以后交给平常的状态"));

		AddSequenceNode(*Idle, false, Stand.Idle, NAME_None, TEXT("UpdateIdleAnim"), Problems);
		AddSequenceNode(*Turn, true, Stand.TurnRight.IsValidIndex(1) ? Stand.TurnRight[1].Get() : nullptr, TEXT("SetupTurnInPlaceAnim"), TEXT("UpdateTurnInPlaceAnim"), Problems);
		AddSequenceNode(*Start, true, Run.Start.F, TEXT("SetupStartAnim"), TEXT("UpdateStartAnim"), Problems);
		AddSequenceNode(*Cycle, false, Run.Cycle.F, TEXT("SetupCycleAnim"), TEXT("UpdateCycleAnim"), Problems);
		AddSequenceNode(*Stop, true, Run.StopLeftFoot.F, TEXT("SetupStopAnim"), TEXT("UpdateStopAnim"), Problems);
		AddSequenceNode(*Pivot, true, Run.PivotLeftFoot.F, TEXT("SetupPivotAnim"), TEXT("UpdatePivotAnim"), Problems);
		AddSequenceNode(*Air, false, Fall, NAME_None, TEXT("UpdateAirAnim"), Problems);
		AddSequenceNode(*SlideStart, true, Slide.IntoLeftFoot, TEXT("SetupSlideStartAnim"), TEXT("UpdateSlideStartAnim"), Problems);
		AddSequenceNode(*SlideLoop, false, Slide.Loop, NAME_None, TEXT("UpdateSlideLoopAnim"), Problems);
		AddSequenceNode(*SlideExit, true, Slide.ExitIdleStand, TEXT("SetupSlideExitAnim"), TEXT("UpdateSlideExitAnim"), Problems);

		Connect(Machine.EntryNode->GetOutputPin(), Idle->GetInputPin(), Problems);

		// 每个状态离开的转换按优先级判：离地最先，其次滑铲，再是松手、反向，最后是"放完了"
		AddTransition(Machine, Idle, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, Idle, SlideStart, TEXT("bShouldSlide"), 1, Problems);
		AddTransition(Machine, Idle, Start, TEXT("bShouldStart"), 2, Problems);
		AddTransition(Machine, Idle, Turn, TEXT("bShouldTurnInPlace"), 3, Problems);

		AddTransition(Machine, Turn, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, Turn, SlideStart, TEXT("bShouldSlide"), 1, Problems);
		AddTransition(Machine, Turn, Start, TEXT("bShouldStart"), 2, Problems);
		AddTransition(Machine, Turn, Idle, TEXT("bTurnInPlaceFinished"), 3, Problems);

		AddTransition(Machine, Start, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, Start, SlideStart, TEXT("bShouldSlide"), 1, Problems);
		AddTransition(Machine, Start, Pivot, TEXT("bShouldPivot"), 2, Problems);
		AddTransition(Machine, Start, Stop, TEXT("bShouldStop"), 3, Problems);
		AddTransition(Machine, Start, Idle, TEXT("bShouldIdle"), 4, Problems);
		AddTransition(Machine, Start, Cycle, TEXT("bStartFinished"), 5, Problems);

		AddTransition(Machine, Cycle, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, Cycle, SlideStart, TEXT("bShouldSlide"), 1, Problems);
		AddTransition(Machine, Cycle, Pivot, TEXT("bShouldPivot"), 2, Problems);
		AddTransition(Machine, Cycle, Stop, TEXT("bShouldStop"), 3, Problems);
		AddTransition(Machine, Cycle, Idle, TEXT("bShouldIdle"), 4, Problems);

		AddTransition(Machine, Pivot, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, Pivot, SlideStart, TEXT("bShouldSlide"), 1, Problems);
		AddTransition(Machine, Pivot, Stop, TEXT("bShouldStop"), 2, Problems);
		AddTransition(Machine, Pivot, Idle, TEXT("bShouldIdle"), 3, Problems);
		AddTransition(Machine, Pivot, Cycle, TEXT("bPivotFinished"), 4, Problems);

		AddTransition(Machine, Stop, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, Stop, SlideStart, TEXT("bShouldSlide"), 1, Problems);
		AddTransition(Machine, Stop, Cycle, TEXT("bShouldCycleFromStop"), 2, Problems);
		AddTransition(Machine, Stop, Start, TEXT("bShouldStart"), 3, Problems);
		AddTransition(Machine, Stop, Turn, TEXT("bShouldTurnInPlace"), 4, Problems);
		AddTransition(Machine, Stop, Idle, TEXT("bStopFinished"), 5, Problems, 0.3f);

		// 落地按着蹲直接接滑铲，先于平常的落地
		AddTransition(Machine, Air, SlideStart, TEXT("bShouldSlide"), 0, Problems);
		AddTransition(Machine, Air, Cycle, TEXT("bShouldStart"), 1, Problems);
		AddTransition(Machine, Air, Stop, TEXT("bShouldStop"), 2, Problems);
		AddTransition(Machine, Air, Idle, TEXT("bShouldIdle"), 3, Problems);

		AddTransition(Machine, SlideStart, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, SlideStart, SlideExit, TEXT("bSlideEnded"), 1, Problems);
		AddTransition(Machine, SlideStart, SlideLoop, TEXT("bSlideStartFinished"), 2, Problems);

		AddTransition(Machine, SlideLoop, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, SlideLoop, SlideExit, TEXT("bSlideEnded"), 1, Problems);

		// 起身时又铲（冲刺中又按了蹲）直接进入；起来以后按意图、速度交给平常的状态
		AddTransition(Machine, SlideExit, Air, TEXT("bInAir"), 0, Problems);
		AddTransition(Machine, SlideExit, SlideStart, TEXT("bShouldSlide"), 1, Problems);
		AddTransition(Machine, SlideExit, Cycle, TEXT("bSlideExitToCycle"), 2, Problems);
		AddTransition(Machine, SlideExit, Start, TEXT("bSlideExitToStart"), 3, Problems);
		AddTransition(Machine, SlideExit, Stop, TEXT("bSlideExitToStop"), 4, Problems);
		AddTransition(Machine, SlideExit, Idle, TEXT("bSlideExitToIdle"), 5, Problems);
	}

	/** 动画图与它的输出节点 */
	UEdGraph* FindAnimGraph(UAnimBlueprint& Blueprint)
	{
		UEdGraph* AnimGraph = nullptr;
		for (UEdGraph* Graph : Blueprint.FunctionGraphs)
		{
			AnimGraph = Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph ? Graph : AnimGraph;
		}
		return AnimGraph;
	}

	/**
	 * 上半身槽那个分层混合的曲线怎么混：移动那份（基础姿势）有的曲线一律用它的，蒙太奇里才有的（HandIK_R 这类）才取槽的。
	 * 槽把蒙太奇与它的输入按权重混，蒙太奇里没有的曲线（接触、落脚的预测）跟着乘 1 − 蒙太奇的权重；
	 * 默认的 Override 再拿槽的值盖掉基础姿势的，跑着拔枪时腿部 IK 读到的落脚曲线只剩一点（见 Docs/Design/Weapons.md「第 1 批」）
	 */
	constexpr ECurveBlendOption::Type UpperBodyCurveBlend = ECurveBlendOption::DoNotOverride;

	/**
	 * 蒙太奇槽，插在 Source（状态机）与它原来的下游之间。见 Docs/Design/AnimActions.md「动画蓝图与素材」：
	 * Source → 缓存姿势；一份作基础姿势，一份过上半身槽（UpperBody），按 spine_01 往上分层混合（网格空间的旋转，上半身保持朝向）；
	 * 再过全身槽（DefaultSlot），接回原来的下游。姿势引脚只能接一处，所以用缓存姿势分成两份。
	 * 两个槽都在朝向与步幅修正、混合（Dead Blending）之前：蒙太奇的惯性化混入混出由下游的 Dead Blending 接住
	 * @return 插上了没有
	 */
	bool InsertMontageSlots(UEdGraph& Graph, UEdGraphNode& Source, TArray<FString>& Problems)
	{
		UEdGraphPin* SourceOut = FindPosePin(Source, EGPD_Output);
		if (!SourceOut || SourceOut->LinkedTo.Num() != 1)
		{
			Problems.Add(FString::Printf(TEXT("%s 的姿势输出要正好接一处才能插槽，现在接了 %d 处"), *Source.GetName(), SourceOut ? SourceOut->LinkedTo.Num() : 0));
			return false;
		}
		UEdGraphPin* Downstream = SourceOut->LinkedTo[0];
		SourceOut->BreakAllPinLinks();

		const int32 X = Source.NodePosX;
		const int32 Y = Source.NodePosY + 400;
		UAnimGraphNode_SaveCachedPose* Save = SpawnNode<UAnimGraphNode_SaveCachedPose>(Graph, X, Y, [](UAnimGraphNode_SaveCachedPose& Node)
		{
			Node.CacheName = TEXT("LocomotionPose");
		});
		const auto MakeUse = [&Graph, Save](const int32 UseX, const int32 UseY)
		{
			return SpawnNode<UAnimGraphNode_UseCachedPose>(Graph, UseX, UseY, [Save](UAnimGraphNode_UseCachedPose& Node)
			{
				Node.SaveCachedPoseNode = Save;
				if (FStrProperty* NameProperty = FindFProperty<FStrProperty>(UAnimGraphNode_UseCachedPose::StaticClass(), TEXT("NameOfCache")))
				{
					NameProperty->SetPropertyValue_InContainer(&Node, Save->CacheName);
				}
			});
		};
		UAnimGraphNode_UseCachedPose* UseBase = MakeUse(X + 300, Y);
		UAnimGraphNode_UseCachedPose* UseUpper = MakeUse(X + 300, Y + 150);
		UAnimGraphNode_Slot* UpperBody = SpawnNode<UAnimGraphNode_Slot>(Graph, X + 550, Y + 150, [](UAnimGraphNode_Slot& Node)
		{
			Node.Node.SlotName = TEXT("UpperBody");
		});
		UAnimGraphNode_LayeredBoneBlend* Layered = SpawnNode<UAnimGraphNode_LayeredBoneBlend>(Graph, X + 850, Y, [](UAnimGraphNode_LayeredBoneBlend& Node)
		{
			Node.Node.BlendMode = ELayeredBoneBlendMode::BranchFilter;
			Node.Node.bMeshSpaceRotationBlend = true;
			// 上半身是 spine_01 往上，再加 IK 手的骨头：它们挂在 root 下、不在 spine_01 下，武器的片段里 ik_hand_gun 跟着枪走，
			// 不带上的话枪还跟着下半身（移动）那份姿势里的 ik_hand_gun，换挂点时跳 46 cm（weapon 路线实测）
			Node.Node.LayerSetup.SetNum(1);
			FBranchFilter Spine;
			Spine.BoneName = TEXT("spine_01");
			Spine.BlendDepth = 0;
			FBranchFilter IKHands;
			IKHands.BoneName = TEXT("ik_hand_root");
			IKHands.BlendDepth = 0;
			Node.Node.LayerSetup[0].BranchFilters = {Spine, IKHands};
			Node.Node.BlendWeights.SetNum(1);
			Node.Node.BlendWeights[0] = 1.f;
			Node.Node.BlendPoses.SetNum(1);
			Node.Node.CurveBlendOption = UpperBodyCurveBlend;
		});
		UAnimGraphNode_Slot* FullBody = SpawnNode<UAnimGraphNode_Slot>(Graph, X + 1150, Y, [](UAnimGraphNode_Slot& Node)
		{
			Node.Node.SlotName = TEXT("DefaultSlot");
			// 全身动作覆盖时仍推进底层状态，落稳后可立即承接方向与步态。
			Node.Node.bAlwaysUpdateSourcePose = true;
		});

		Connect(SourceOut, FindPosePin(*Save, EGPD_Input), Problems);
		Connect(FindPosePin(*UseBase, EGPD_Output), Layered->FindPin(TEXT("BasePose")), Problems);
		ConnectPose(*UseUpper, *UpperBody, Problems);
		Connect(FindPosePin(*UpperBody, EGPD_Output), Layered->FindPin(TEXT("BlendPoses_0")), Problems);
		ConnectPose(*Layered, *FullBody, Problems);
		Connect(FindPosePin(*FullBody, EGPD_Output), Downstream, Problems);
		AddComment(Graph, X - 50, Y - 80, 1450, 380,
			TEXT("蒙太奇槽：上半身 UpperBody（从 spine_01 往上，网格空间的旋转）、全身 DefaultSlot。动画动作的蒙太奇放在这里，见 Docs/Design/AnimActions.md"));
		return true;
	}

	/**
	 * 左右倾斜，插在根骨骼的偏航补偿（Rotate Root Bone）与它原来的上游之间：向左、向右各一个网格空间叠加（Apply Mesh Space Additive），
	 * 叠加的是跑与冲刺两套倾斜姿势（静止的，求值器放首帧）按冲刺的比重混出来的（Two Way Blend）。
	 * 权重读动画实例的 LeanLeftAlpha、LeanRightAlpha，比重读 LeanSprintWeight；同一时刻只有一边的权重不为 0，为 0 的那边不求值。
	 * 在转根骨骼之前：倾斜随身体（偏航补偿）一起转；在混合（Dead Blending）之后：倾斜量本来就连续，不用混
	 * @return 插上了没有
	 */
	bool InsertLean(UEdGraph& Graph, UEdGraphNode& RotateRoot, TArray<FString>& Problems)
	{
		UEdGraphPin* RootIn = FindPosePin(RotateRoot, EGPD_Input);
		if (!RootIn || RootIn->LinkedTo.Num() != 1)
		{
			Problems.Add(FString::Printf(TEXT("%s 的姿势输入要正好接一处才能插倾斜，现在接了 %d 处"), *RotateRoot.GetName(), RootIn ? RootIn->LinkedTo.Num() : 0));
			return false;
		}
		UEdGraphPin* Upstream = RootIn->LinkedTo[0];
		RootIn->BreakAllPinLinks();

		const auto AddSide = [&Graph, &Problems](const TCHAR* RunPose, const TCHAR* SprintPose, const FName AlphaMember, const int32 X, const int32 Y)
		{
			const auto AddPose = [&Graph, &Problems](const TCHAR* Name, const int32 PoseX, const int32 PoseY)
			{
				UAnimSequence* Pose = FindAnimation(TEXT("Poses"), Name, Problems);
				return SpawnNode<UAnimGraphNode_SequenceEvaluator>(Graph, PoseX, PoseY, [Pose](UAnimGraphNode_SequenceEvaluator& Node)
				{
					NodeValue<TObjectPtr<UAnimSequenceBase>>(Node, TEXT("Sequence")) = Pose;
					NodeValue<bool>(Node, TEXT("bShouldLoop")) = false;
				});
			};
			UAnimGraphNode_SequenceEvaluator* Run = AddPose(RunPose, X - 700, Y);
			UAnimGraphNode_SequenceEvaluator* Sprint = AddPose(SprintPose, X - 700, Y + 150);
			UAnimGraphNode_TwoWayBlend* Blend = SpawnNode<UAnimGraphNode_TwoWayBlend>(Graph, X - 350, Y);
			Connect(FindPosePin(*Run, EGPD_Output), Blend->FindPin(TEXT("A")), Problems);
			Connect(FindPosePin(*Sprint, EGPD_Output), Blend->FindPin(TEXT("B")), Problems);
			BindMember(Graph, *Blend, TEXT("Alpha"), TEXT("LeanSprintWeight"), X - 650, Y + 300, Problems);
			UAnimGraphNode_ApplyMeshSpaceAdditive* Apply = SpawnNode<UAnimGraphNode_ApplyMeshSpaceAdditive>(Graph, X, Y - 100);
			Connect(FindPosePin(*Blend, EGPD_Output), Apply->FindPin(TEXT("Additive")), Problems);
			BindMember(Graph, *Apply, TEXT("Alpha"), AlphaMember, X - 300, Y - 150, Problems);
			return Apply;
		};
		const int32 X = RotateRoot.NodePosX - 300;
		const int32 Y = RotateRoot.NodePosY + 450;
		UAnimGraphNode_ApplyMeshSpaceAdditive* Left = AddSide(TEXT("M_Neutral_Run_Lean_Pose_Left"), TEXT("M_Neutral_Sprint_Pose_Lean_L_hold"),
			TEXT("LeanLeftAlpha"), X - 1100, Y);
		UAnimGraphNode_ApplyMeshSpaceAdditive* Right = AddSide(TEXT("M_Neutral_Run_Lean_Pose_Right"), TEXT("M_Neutral_Sprint_Pose_Lean_R_hold"),
			TEXT("LeanRightAlpha"), X, Y);
		Connect(Upstream, Left->FindPin(TEXT("Base")), Problems);
		Connect(FindPosePin(*Left, EGPD_Output), Right->FindPin(TEXT("Base")), Problems);
		Connect(FindPosePin(*Right, EGPD_Output), RootIn, Problems);
		AddComment(Graph, X - 1900, Y - 300, 2150, 700,
			TEXT("左右倾斜：GASP 跑与冲刺的倾斜姿势按网格空间叠加，权重读 LeanLeftAlpha、LeanRightAlpha，两套按 LeanSprintWeight 混。")
			TEXT("在转根骨骼之前（随身体转）、混合之后（倾斜量本来就连续）。见 Docs/Design/AnimationBlueprint.md 的\"倾斜\""));
		return true;
	}

	/** 在现有图的转根之前插入参考捕获和两个探身叠加；资产与权重均显式绑定，不重建其他节点。 */
	bool InsertPeek(UEdGraph& Graph, UEdGraphNode& RotateRoot, TArray<FString>& Problems)
	{
		UEdGraphPin* Input = FindPosePin(RotateRoot, EGPD_Input);
		if (!Input || Input->LinkedTo.Num() != 1)
		{
			Problems.Add(TEXT("探身需要 Rotate Root Bone 的唯一姿势输入"));
			return false;
		}
		UAnimSequence* Poses[2] = {};
		const TCHAR* Sides[] = { TEXT("L"), TEXT("R") };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FString Path = FString::Printf(TEXT("%s/Additives/Peek/M_UpperBody_Peek_%s.M_UpperBody_Peek_%s"), AnimationRoot, Sides[Index], Sides[Index]);
			Poses[Index] = LoadObject<UAnimSequence>(nullptr, *Path);
			if (!Poses[Index] || Poses[Index]->AdditiveAnimType != AAT_RotationOffsetMeshSpace || !Poses[Index]->RefPoseSeq)
			{
				Problems.Add(FString::Printf(TEXT("探身姿势缺少网格空间叠加或基准：%s"), *Path));
				return false;
			}
		}
		UEdGraphPin* Upstream = Input->LinkedTo[0];
		Input->BreakAllPinLinks();
		const int32 X = RotateRoot.NodePosX - 1000;
		const int32 Y = RotateRoot.NodePosY + 1150;
		auto* Capture = SpawnNode<UAnimGraphNode_GinkgoPeekReference>(Graph, X, Y);
		Connect(Upstream, FindPosePin(*Capture, EGPD_Input), Problems);
		UEdGraphPin* Previous = FindPosePin(*Capture, EGPD_Output);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			auto* Pose = SpawnNode<UAnimGraphNode_SequenceEvaluator>(Graph, X + Index * 450, Y + 220, [&](UAnimGraphNode_SequenceEvaluator& Node)
			{
				NodeValue<TObjectPtr<UAnimSequenceBase>>(Node, TEXT("Sequence")) = Poses[Index];
				NodeValue<bool>(Node, TEXT("bShouldLoop")) = false;
			});
			auto* Apply = SpawnNode<UAnimGraphNode_ApplyMeshSpaceAdditive>(Graph, X + 300 + Index * 450, Y);
			Connect(Previous, Apply->FindPin(TEXT("Base")), Problems);
			Connect(FindPosePin(*Pose, EGPD_Output), Apply->FindPin(TEXT("Additive")), Problems);
			BindMember(Graph, *Apply, TEXT("Alpha"), Index == 0 ? TEXT("PeekLeftAlpha") : TEXT("PeekRightAlpha"), X + Index * 450, Y + 380, Problems);
			Previous = FindPosePin(*Apply, EGPD_Output);
		}
		Connect(Previous, Input, Problems);
		AddComment(Graph, X - 80, Y - 100, 1400, 650,
			TEXT("自动探身：参考捕获 → 左/右网格空间叠加 → 转根 → Control Rig。参考枪口经同一瞄准求解后驱动下一帧判定，避免探身成功后反复回正。"));
		return Problems.IsEmpty();
	}

	// ===== Control Rig =====

	/** 节点上一个引脚的路径，没有这个引脚时为空（连线时连不上） */
	FString PinPath(const URigVMNode* Node, const TCHAR* PinName)
	{
		const URigVMPin* Pin = Node ? Node->FindPin(PinName) : nullptr;
		return Pin ? Pin->GetPinPath() : FString();
	}

	/**
	 * 角色的 Control Rig，没有时生成：骨骼、曲线从小白人的网格导入，补上腿部 IK 读的曲线（导入命令烘焙到片段里的不在网格的曲线表里，
	 * 层级里没有的曲线动画蓝图的 Control Rig 节点不传进来）；图是 Forwards Solve → 腿部 IK（FRigUnit_GinkgoLegIK，脚与曲线用节点的默认值），
	 * 权重读公开变量 LegIKAlpha。已经有了就用它：之后在编辑器里改（手部的全身 IK、瞄准也加在这里），不覆盖
	 */
	UControlRigRuntimeAsset* FindOrCreateControlRig(const FString& PackageName, TArray<FString>& Problems)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *FPackageName::GetShortName(PackageName));
		if (FPackageName::DoesPackageExist(PackageName))
		{
			UControlRigRuntimeAsset* Existing = LoadObject<UControlRigRuntimeAsset>(nullptr, *ObjectPath);
			if (!Existing)
			{
				Problems.Add(FString::Printf(TEXT("%s 已经有了，但读不出 Control Rig"), *PackageName));
			}
			return Existing;
		}
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
		if (!Mesh)
		{
			Problems.Add(FString::Printf(TEXT("找不到网格 %s（先跑导入命令 GinkgoAnimationImport）"), MeshPath));
			return nullptr;
		}
		UControlRigRuntimeAsset* Rig = UControlRigAssetFactory::CreateNewControlRigAsset(PackageName);
		UControlRigEditorAsset* Editor = Rig ? Cast<UControlRigEditorAsset>(Rig->GetEditorOnlyData()) : nullptr;
		URigHierarchyController* Hierarchy = Editor ? Cast<IControlRigEditorAssetInterface>(Editor)->GetHierarchyController() : nullptr;
		if (!Hierarchy || Rig->GetPathName() != ObjectPath)
		{
			Problems.Add(FString::Printf(TEXT("建不了 Control Rig %s"), *PackageName));
			return nullptr;
		}

		Hierarchy->ImportBones(Mesh->GetSkeleton(), NAME_None, /*bReplaceExistingBones*/ false, /*bRemoveObsoleteBones*/ false);
		Hierarchy->ImportCurvesFromSkeletalMesh(Mesh);
		const FRigUnit_GinkgoLegIK LegIKDefaults;
		for (const FRigUnit_GinkgoLegIK_Foot& Foot : LegIKDefaults.Feet)
		{
			for (const FName Curve : { Foot.ContactCurve, Foot.LandInCurve, Foot.LandXCurve, Foot.LandYCurve, Foot.DisableCurve })
			{
				if (!Curve.IsNone() && !Hierarchy->GetHierarchy()->Contains(FRigElementKey(Curve, ERigElementType::Curve)))
				{
					Hierarchy->AddCurve(Curve, 0.f, /*bSetupUndo*/ false);
				}
			}
		}
		Rig->GetSourceHierarchyImport() = Mesh->GetSkeleton();
		Rig->GetSourceCurveImport() = Mesh->GetSkeleton();
		Editor->PropagateHierarchyFromBPToInstances();
		Editor->SetPreviewMesh(Mesh);

		// 图：编辑器打开时才补 Forwards Solve 的事件节点，这里自己加
		Editor->AddMemberVariable(LegIKAlphaVariable, TEXT("float"), /*bIsPublic*/ true, /*bIsReadOnly*/ false, TEXT("1.0"));
		URigVMController* Controller = Editor->GetOrCreateController(Editor->GetDefaultModel());
		URigVMNode* Begin = nullptr;
		for (URigVMNode* Node : Controller->GetGraph()->GetNodes())
		{
			const URigVMUnitNode* Unit = Cast<URigVMUnitNode>(Node);
			Begin = Unit && Unit->GetScriptStruct() == FRigUnit_BeginExecution::StaticStruct() ? Node : Begin;
		}
		if (!Begin)
		{
			Begin = Controller->AddUnitNode(FRigUnit_BeginExecution::StaticStruct(), FRigUnit::GetMethodName(), FVector2D(0.0, 0.0), FString(), false);
		}
		URigVMUnitNode* LegIK = Controller->AddUnitNode(FRigUnit_GinkgoLegIK::StaticStruct(), FRigUnit::GetMethodName(), FVector2D(350.0, 0.0), FString(), false);
		URigVMVariableNode* Alpha = Controller->AddVariableNode(LegIKAlphaVariable, TEXT("float"), nullptr, /*bIsGetter*/ true, FString(), FVector2D(0.0, 250.0), FString(),
			false);
		const URigVMPin* AlphaValue = Alpha ? Alpha->GetValuePin() : nullptr;
		if (!Controller->AddLink(PinPath(Begin, TEXT("ExecutePin")), PinPath(LegIK, TEXT("ExecutePin")), false)
			|| !Controller->AddLink(AlphaValue ? AlphaValue->GetPinPath() : FString(), PinPath(LegIK, TEXT("Alpha")), false))
		{
			Problems.Add(FString::Printf(TEXT("%s 里连不上 Forwards Solve → 腿部 IK 或权重"), *PackageName));
		}
		Controller->AddCommentNode(
			TEXT("腿部 IK（FRigUnit_GinkgoLegIK）：着地的脚锁在世界里（锁住、滑、放开），摆动的脚预测落脚点。权重读 LegIKAlpha，")
			TEXT("动画蓝图的 Control Rig 节点上露成引脚，接动画实例的 LegIKAlpha。由命令 GinkgoAnimBlueprint 生成，之后在编辑器里改；")
			TEXT("见 Docs/Design/AnimationBlueprint.md 的\"脚：预测式的腿部 IK\""),
			FVector2D(-50.0, -150.0), FVector2D(1000.0, 550.0), FLinearColor::White, FString(), false);
		Editor->RecompileVM();

		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		Rig->MarkPackageDirty();
		if (!UPackage::SavePackage(Rig->GetPackage(), Rig, *Filename, SaveArgs))
		{
			Problems.Add(FString::Printf(TEXT("存不了 %s"), *Filename));
			return nullptr;
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] 生成了 Control Rig %s"), *Filename);
		return Rig;
	}

	/**
	 * 角色的 Control Rig（腿部 IK 在里面），插在输出与它原来的上游（转根骨骼）之间；权重的公开变量 LegIKAlpha 露成引脚，接动画实例的 LegIKAlpha
	 * （地面上为 1，空中、滑铲为 0）。在转根骨骼之后：锁脚要在世界里锁，求解的是网格最终的朝向
	 * @return 插上了没有
	 */
	bool InsertControlRig(UEdGraph& Graph, UEdGraphNode& Output, UControlRigRuntimeAsset& Rig, TArray<FString>& Problems)
	{
		UEdGraphPin* OutputIn = FindPosePin(Output, EGPD_Input);
		if (!OutputIn || OutputIn->LinkedTo.Num() != 1)
		{
			Problems.Add(FString::Printf(TEXT("%s 的姿势输入要正好接一处才能插 Control Rig，现在接了 %d 处"), *Output.GetName(), OutputIn ? OutputIn->LinkedTo.Num() : 0));
			return false;
		}
		UEdGraphPin* Upstream = OutputIn->LinkedTo[0];
		OutputIn->BreakAllPinLinks();

		const int32 X = Output.NodePosX - 600;
		const int32 Y = Output.NodePosY + 1300;
		UAnimGraphNode_ControlRig* ControlRig = SpawnNode<UAnimGraphNode_ControlRig>(Graph, X, Y, [&Rig](UAnimGraphNode_ControlRig& Node)
		{
			NodeValue<FControlRigAssetStrongReference>(Node, TEXT("ControlRigAssetReference")).Set(TObjectPtr<UControlRigRuntimeAsset>(&Rig));
		});
		// 露出权重的引脚：与细节面板里勾上变量的"Use Pin"一样，改节点的 CustomPinProperties 再重建
		const FArrayProperty* PinsProperty = FindFProperty<FArrayProperty>(UAnimGraphNode_CustomProperty::StaticClass(), TEXT("CustomPinProperties"));
		TArray<FOptionalPinFromProperty>& Pins = *PinsProperty->ContainerPtrToValuePtr<TArray<FOptionalPinFromProperty>>(ControlRig);
		FOptionalPinFromProperty* AlphaPin = Pins.FindByPredicate([](const FOptionalPinFromProperty& Pin) { return Pin.PropertyName == FName(LegIKAlphaVariable); });
		if (!AlphaPin)
		{
			Problems.Add(FString::Printf(TEXT("%s 没有公开变量 %s"), *Rig.GetPathName(), LegIKAlphaVariable));
		}
		else
		{
			AlphaPin->bShowPin = true;
			ControlRig->ReconstructNode();
		}
		BindMember(Graph, *ControlRig, LegIKAlphaVariable, TEXT("LegIKAlpha"), X - 300, Y + 200, Problems);

		Connect(Upstream, FindPosePin(*ControlRig, EGPD_Input), Problems);
		Connect(FindPosePin(*ControlRig, EGPD_Output), OutputIn, Problems);
		AddComment(Graph, X - 350, Y - 80, 750, 380,
			TEXT("Control Rig（CR_GinkgoCharacter）：腿部 IK——着地的脚锁在世界里（锁住、滑、放开），摆动的脚预测落脚点；权重读 LegIKAlpha。")
			TEXT("以后手部的全身 IK、瞄准也放进这个 Control Rig。见 Docs/Design/AnimationBlueprint.md 的\"脚：预测式的腿部 IK\""));
		return true;
	}

	/**
	 * 动画图：状态机 → 蒙太奇槽 → 转到组件空间 → 朝向修正 → 步幅修正 → 转回局部空间 → 混合（Dead Blending）→ 左右倾斜 → 根骨骼的偏航补偿 →
	 * Control Rig（腿部 IK）→ 输出。混合接住状态机里所有转换与换片段的请求，混的是修正过的姿势；两个修正节点用 AnimationWarping 插件，
	 * 值都读动画实例的成员（手动模式）
	 */
	void BuildAnimGraph(UAnimBlueprint& Blueprint, const UGinkgoAnimInstance& Defaults, UControlRigRuntimeAsset* Rig, TArray<FString>& Problems)
	{
		UEdGraph* AnimGraph = nullptr;
		for (UEdGraph* Graph : Blueprint.FunctionGraphs)
		{
			AnimGraph = Graph->GetFName() == UEdGraphSchema_K2::GN_AnimGraph ? Graph : AnimGraph;
		}
		UAnimGraphNode_Root* Root = nullptr;
		for (UEdGraphNode* Node : AnimGraph ? AnimGraph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
		{
			Root = Root ? Root : Cast<UAnimGraphNode_Root>(Node);
		}
		if (!AnimGraph || !Root)
		{
			Problems.Add(TEXT("新建的动画蓝图里没有动画图或它的输出节点"));
			return;
		}
		Root->NodePosX = 0;
		Root->NodePosY = 0;

		UAnimGraphNode_StateMachine* Machine = SpawnNode<UAnimGraphNode_StateMachine>(*AnimGraph, -2100, 0);
		FBlueprintEditorUtils::RenameGraph(Machine->EditorStateMachineGraph, TEXT("Locomotion"));
		BuildLocomotion(*Machine->EditorStateMachineGraph, Defaults, Problems);

		UAnimGraphNode_LocalToComponentSpace* ToComponent = SpawnNode<UAnimGraphNode_LocalToComponentSpace>(*AnimGraph, -1800, 0);

		// 朝向修正：腿转向移动的方向，脊柱分摊着转回去，上半身仍朝前（Lyra 的骨骼设置）。
		// 不用节点自己的插值：角度为 0 时节点整个不求值，插值也跳过，腿一下子转回去；再有角度时又从停下前那个旧角度插起。
		// 角度由动画实例保证连续，换片段、换状态时的跳变交给后面的混合节点
		UAnimGraphNode_OrientationWarping* Orientation = SpawnNode<UAnimGraphNode_OrientationWarping>(*AnimGraph, -1550, 0, [](UAnimGraphNode_OrientationWarping& Node)
		{
			Node.Node.Mode = EWarpingEvaluationMode::Manual;
			Node.Node.RotationInterpSpeed = 0.f;
			Node.Node.SpineBones = { FBoneReference(TEXT("spine_01")), FBoneReference(TEXT("spine_02")), FBoneReference(TEXT("spine_03")), FBoneReference(TEXT("spine_04")),
				FBoneReference(TEXT("spine_05")) };
			Node.Node.IKFootRootBone = FBoneReference(TEXT("ik_foot_root"));
			Node.Node.IKFootBones = { FBoneReference(TEXT("ik_foot_l")), FBoneReference(TEXT("ik_foot_r")) };
		});
		BindMember(*AnimGraph, *Orientation, TEXT("OrientationAngle"), TEXT("OrientationWarpingAngle"), -1800, 250, Problems);

		// 步幅修正：速度比里播放速率夹不下的部分，沿速度的方向把步子拉长或缩短
		UAnimGraphNode_StrideWarping* Stride = SpawnNode<UAnimGraphNode_StrideWarping>(*AnimGraph, -1200, 0, [](UAnimGraphNode_StrideWarping& Node)
		{
			Node.Node.Mode = EWarpingEvaluationMode::Manual;
			Node.Node.PelvisBone = FBoneReference(TEXT("pelvis"));
			Node.Node.IKFootRootBone = FBoneReference(TEXT("ik_foot_root"));
			FStrideWarpingFootDefinition Left;
			Left.IKFootBone = FBoneReference(TEXT("ik_foot_l"));
			Left.FKFootBone = FBoneReference(TEXT("foot_l"));
			Left.ThighBone = FBoneReference(TEXT("thigh_l"));
			FStrideWarpingFootDefinition Right;
			Right.IKFootBone = FBoneReference(TEXT("ik_foot_r"));
			Right.FKFootBone = FBoneReference(TEXT("foot_r"));
			Right.ThighBone = FBoneReference(TEXT("thigh_r"));
			Node.Node.FootDefinitions = { Left, Right };
		});
		BindMember(*AnimGraph, *Stride, TEXT("StrideScale"), TEXT("StrideScale"), -1450, 300, Problems);
		BindMember(*AnimGraph, *Stride, TEXT("StrideDirection"), TEXT("StrideDirection"), -1450, 400, Problems);

		UAnimGraphNode_ComponentToLocalSpace* ToLocal = SpawnNode<UAnimGraphNode_ComponentToLocalSpace>(*AnimGraph, -850, 0);

		// 混合放在修正之后（Lyra 把修正放在各状态里、惯性化在最后，是同一个意思）：换片段、换状态时混的是修正过的两个姿势，
		// 修正的角度随片段一起跳也不会让腿跳。放在修正之前的话，混合期间腿按新片段的方向修正、姿势却还是旧片段的，腿一下子转 90°
		UAnimGraphNode_DeadBlending* Blending = SpawnNode<UAnimGraphNode_DeadBlending>(*AnimGraph, -600, 0);

		// 根骨骼的偏航补偿：站着时网格不跟着胶囊转，脚留在原地
		UAnimGraphNode_RotateRootBone* RotateRoot = SpawnNode<UAnimGraphNode_RotateRootBone>(*AnimGraph, -350, 0);
		BindMember(*AnimGraph, *RotateRoot, TEXT("Yaw"), TEXT("RootYawOffset"), -600, 250, Problems);

		ConnectPose(*Machine, *ToComponent, Problems);
		InsertMontageSlots(*AnimGraph, *Machine, Problems);
		ConnectPose(*ToComponent, *Orientation, Problems);
		ConnectPose(*Orientation, *Stride, Problems);
		ConnectPose(*Stride, *ToLocal, Problems);
		ConnectPose(*ToLocal, *Blending, Problems);
		ConnectPose(*Blending, *RotateRoot, Problems);
		ConnectPose(*RotateRoot, *Root, Problems);
		InsertLean(*AnimGraph, *RotateRoot, Problems);
		if (Rig)
		{
			InsertControlRig(*AnimGraph, *Root, *Rig, Problems);
		}

		AddComment(*AnimGraph, -2150, -200, 2300, 700,
			TEXT("由命令 GinkgoAnimBlueprint 生成（Source/CaseGinkgoEditor/Private/Animation），之后在编辑器里改。")
			TEXT("各节点读的值、状态绑定的节点函数都在 UGinkgoAnimInstance 里，设计见 Docs/Design/AnimationBlueprint.md"));
	}

	bool SaveBlueprint(UPackage& Package, UAnimBlueprint& Blueprint)
	{
		// 增量改图时仍可能保留旧包的读取器；先完整载入并释放文件句柄，再原地保存，避免 Windows 共享冲突。
		Package.FullyLoad();
		ResetLoaders(&Package);
		const FString Filename = FPackageName::LongPackageNameToFilename(Package.GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(&Package, &Blueprint, *Filename, SaveArgs);
	}

	/** 编译，打出编译器的每一条消息；有错误或警告时返回 false */
	bool CompileBlueprint(UAnimBlueprint& Blueprint);

	/** 改已有的动画蓝图之前，原文件备份到 Saved/Ginkgo/Backup；备份不了时返回空 */
	FString BackupBlueprint(const FString& AssetName, const FString& Filename)
	{
		const FString Backup = FPaths::ProjectSavedDir() / TEXT("Ginkgo/Backup") / FString::Printf(TEXT("%s_%s.uasset"), *AssetName, *FDateTime::Now().ToString(TEXT("%H%M%S")));
		return IFileManager::Get().Copy(*Backup, *Filename) == COPY_OK ? Backup : FString();
	}

	/** 改完已有的动画蓝图：编译、存；插的时候有问题就不存（原文件在备份里） */
	int32 CompileAndSaveEdited(UAnimBlueprint& Blueprint, const FString& Filename, const FString& Backup, const TArray<FString>& Problems, const TCHAR* What)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		const bool bCompiled = CompileBlueprint(Blueprint);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		Blueprint.MarkPackageDirty();
		if (!Problems.IsEmpty() || !SaveBlueprint(*Blueprint.GetPackage(), Blueprint))
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 没存（原文件备份在 %s）"), *Filename, *Backup);
			return 1;
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s %s%s，原文件备份在 %s"), *Blueprint.GetPathName(), What, bCompiled ? TEXT("") : TEXT("（编译有问题，见上面）"),
			*Backup);
		return bCompiled ? 0 : 1;
	}

	UAnimBlueprint* LoadBlueprint(const FString& PackageName)
	{
		return LoadObject<UAnimBlueprint>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackageName, *FPackageName::GetShortName(PackageName)));
	}

	/**
	 * -From=：先把那份动画蓝图复制到 -Package= 再改（试改法时不碰手改的那份），与编辑器里的复制相同（ObjectTools::DuplicateSingleObject）。
	 * 目标已经有了时不复制
	 * @return 复制出来的那份；不复制或复制不了时为空
	 */
	UAnimBlueprint* DuplicateBlueprint(const FString& SourcePackage, const FString& TargetPackage)
	{
		if (FPackageName::DoesPackageExist(TargetPackage))
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 已经有了，不从 %s 复制"), *TargetPackage, *SourcePackage);
			return nullptr;
		}
		UAnimBlueprint* Source = LoadBlueprint(SourcePackage);
		if (!Source)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s"), *SourcePackage);
			return nullptr;
		}
		ObjectTools::FPackageGroupName Target;
		Target.PackageName = TargetPackage;
		Target.ObjectName = FPackageName::GetShortName(TargetPackage);
		TSet<UPackage*> RefusedPackages;
		UAnimBlueprint* Copy = Cast<UAnimBlueprint>(ObjectTools::DuplicateSingleObject(Source, Target, RefusedPackages, /*bPromptToOverwrite*/ false));
		UE_CLOG(!Copy, LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 复制不了 %s 到 %s"), *SourcePackage, *TargetPackage);
		return Copy;
	}

	/**
	 * -AddSlots：在已有的动画蓝图（默认是手改的那份）里插上蒙太奇槽，不重新生成，别的节点都不动。
	 * 原文件先备份到 Saved/Ginkgo/Backup；已经有槽节点的不插
	 */
	int32 AddSlots(const FString& PackageName, const FString& Filename)
	{
		const FString AssetName = FPackageName::GetShortName(PackageName);
		UAnimBlueprint* Blueprint = LoadBlueprint(PackageName);
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!AnimGraph)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或它的动画图"), *PackageName);
			return 1;
		}
		// 状态机的姿势接到哪里，槽就插在哪里；有好几个状态机时取 Locomotion
		UAnimGraphNode_StateMachine* Machine = nullptr;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (Node->IsA<UAnimGraphNode_Slot>())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已经有槽节点了，不插"), *PackageName);
				return 0;
			}
			UAnimGraphNode_StateMachine* Candidate = Cast<UAnimGraphNode_StateMachine>(Node);
			if (Candidate && (!Machine || Candidate->EditorStateMachineGraph->GetName() == TEXT("Locomotion")))
			{
				Machine = Candidate;
			}
		}
		if (!Machine)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 的动画图里没有状态机"), *PackageName);
			return 1;
		}

		const FString Backup = BackupBlueprint(AssetName, Filename);
		if (Backup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *Filename);
			return 1;
		}
		TArray<FString> Problems;
		InsertMontageSlots(*AnimGraph, *Machine, Problems);
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, TEXT("插上了蒙太奇槽"));
	}

	/**
	 * -FixUpperBodyCurves：已有的动画蓝图里，上半身槽那个分层混合的曲线改成 UpperBodyCurveBlend（-AddSlots 早先插的是默认的 Override），
	 * 别的都不动。加 -From= 时先从那份复制到 -Package= 再改。原文件先备份到 Saved/Ginkgo/Backup；已经是了的不改
	 */
	int32 FixUpperBodyCurves(const FString& PackageName, const FString& Filename, const FString& FromPackage)
	{
		UAnimBlueprint* Blueprint = FromPackage.IsEmpty() ? LoadBlueprint(PackageName) : DuplicateBlueprint(FromPackage, PackageName);
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!AnimGraph)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或它的动画图"), *PackageName);
			return 1;
		}
		// 上半身槽接进它的 BlendPoses_0 的那个分层混合
		UAnimGraphNode_LayeredBoneBlend* Layered = nullptr;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			UAnimGraphNode_LayeredBoneBlend* Candidate = Cast<UAnimGraphNode_LayeredBoneBlend>(Node);
			const UEdGraphPin* Blend = Candidate ? Candidate->FindPin(TEXT("BlendPoses_0")) : nullptr;
			for (const UEdGraphPin* Linked : Blend ? Blend->LinkedTo : TArray<UEdGraphPin*>())
			{
				const UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Linked->GetOwningNode());
				Layered = Slot && Slot->Node.SlotName == TEXT("UpperBody") ? Candidate : Layered;
			}
		}
		if (!Layered)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 里没有接着上半身槽的分层混合（先 -AddSlots）"), *PackageName);
			return 1;
		}
		if (Layered->Node.CurveBlendOption == UpperBodyCurveBlend)
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 上半身槽的曲线已经是基础姿势优先了，不改"), *PackageName);
			return 0;
		}
		// 刚复制出来的还没存过，没有原文件可备份
		const FString Backup = FromPackage.IsEmpty() ? BackupBlueprint(FPackageName::GetShortName(PackageName), Filename) : FString(TEXT("（新复制的，没有）"));
		if (Backup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *Filename);
			return 1;
		}
		Layered->Modify();
		Layered->Node.CurveBlendOption = UpperBodyCurveBlend;
		const TArray<FString> Problems;
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, TEXT("上半身槽的曲线改成基础姿势优先"));
	}

	/**
	 * -AddLean：在已有的动画蓝图（默认是手改的那份）里插上左右倾斜（InsertLean），不重新生成，别的节点都不动。
	 * 加 -From= 时先从那份复制到 -Package= 再插。原文件先备份到 Saved/Ginkgo/Backup；已经有网格空间叠加节点的不插
	 */
	int32 AddLean(const FString& PackageName, const FString& Filename, const FString& FromPackage)
	{
		UAnimBlueprint* Blueprint = FromPackage.IsEmpty() ? LoadBlueprint(PackageName) : DuplicateBlueprint(FromPackage, PackageName);
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!AnimGraph)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或它的动画图"), *PackageName);
			return 1;
		}
		// 倾斜插在转根骨骼之前；有好几个时取第一个
		UAnimGraphNode_RotateRootBone* RotateRoot = nullptr;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (Node->IsA<UAnimGraphNode_ApplyMeshSpaceAdditive>())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已经有网格空间叠加的节点了，不插"), *PackageName);
				return 0;
			}
			RotateRoot = RotateRoot ? RotateRoot : Cast<UAnimGraphNode_RotateRootBone>(Node);
		}
		if (!RotateRoot)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 的动画图里没有 Rotate Root Bone"), *PackageName);
			return 1;
		}

		// 刚复制出来的还没存过，没有原文件可备份
		const FString Backup = FromPackage.IsEmpty() ? BackupBlueprint(FPackageName::GetShortName(PackageName), Filename) : FString(TEXT("（新复制的，没有）"));
		if (Backup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *Filename);
			return 1;
		}
		TArray<FString> Problems;
		InsertLean(*AnimGraph, *RotateRoot, Problems);
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, TEXT("插上了左右倾斜"));
	}

	/** -AddPeek：在当前角色动画图中增量接入探身；先备份，重复执行不重复插入。 */
	int32 AddPeek(const FString& PackageName, const FString& Filename)
	{
		UAnimBlueprint* Blueprint = LoadBlueprint(PackageName);
		UEdGraph* Graph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!Graph) { return 1; }
		UAnimGraphNode_RotateRootBone* RotateRoot = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->IsA<UAnimGraphNode_GinkgoPeekReference>())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已有探身节点，不重复插入"), *PackageName);
				return 0;
			}
			if (auto* Rotate = Cast<UAnimGraphNode_RotateRootBone>(Node))
			{
				if (RotateRoot) { return 1; }
				RotateRoot = Rotate;
			}
		}
		if (!RotateRoot) { return 1; }
		const FString Backup = BackupBlueprint(FPackageName::GetShortName(PackageName), Filename);
		if (Backup.IsEmpty()) { return 1; }
		TArray<FString> Problems;
		InsertPeek(*Graph, *RotateRoot, Problems);
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, TEXT("插上了参考捕获与左右探身"));
	}

	/**
	 * -AddControlRig：在已有的动画蓝图（默认是手改的那份）里插上角色的 Control Rig（InsertControlRig），插在输出之前，别的节点都不动。
	 * Control Rig 没有时先生成（-ControlRig= 换路径）。加 -From= 时先从那份复制到 -Package= 再插。
	 * 原文件先备份到 Saved/Ginkgo/Backup；已经有 Control Rig 节点的不插
	 */
	int32 AddControlRig(const FString& PackageName, const FString& Filename, const FString& FromPackage, const FString& ControlRigPackage)
	{
		UAnimBlueprint* Blueprint = FromPackage.IsEmpty() ? LoadBlueprint(PackageName) : DuplicateBlueprint(FromPackage, PackageName);
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!AnimGraph)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或它的动画图"), *PackageName);
			return 1;
		}
		UAnimGraphNode_Root* Root = nullptr;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (Node->IsA<UAnimGraphNode_ControlRig>())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已经有 Control Rig 节点了，不插"), *PackageName);
				return 0;
			}
			Root = Root ? Root : Cast<UAnimGraphNode_Root>(Node);
		}
		if (!Root)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 的动画图里没有输出节点"), *PackageName);
			return 1;
		}
		TArray<FString> Problems;
		UControlRigRuntimeAsset* Rig = FindOrCreateControlRig(ControlRigPackage, Problems);
		if (!Rig)
		{
			for (const FString& Problem : Problems)
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
			}
			return 1;
		}

		// 刚复制出来的还没存过，没有原文件可备份
		const FString Backup = FromPackage.IsEmpty() ? BackupBlueprint(FPackageName::GetShortName(PackageName), Filename) : FString(TEXT("（新复制的，没有）"));
		if (Backup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *Filename);
			return 1;
		}
		InsertControlRig(*AnimGraph, *Root, *Rig, Problems);
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, TEXT("插上了 Control Rig"));
	}

	// ===== 武器层（Docs/Design/Weapons.md「动画」） =====

	/** 层接口与步枪的层：与角色的动画蓝图放在一起 */
	const TCHAR* const WeaponLayerInterfacePackage = TEXT("/Game/Characters/Ginkgo/ALI_GinkgoWeaponLayers");
	const TCHAR* const RifleLayersPackage = TEXT("/Game/Characters/Ginkgo/ABP_GinkgoWeaponLayers_Rifle");

	/** 98k 的数据资产：一把具体设定的枪 */
	const TCHAR* const Mauser98kPackage = TEXT("/Game/Weapons/Mauser98k/DA_Mauser98k");

	/** 98k 的两个持枪循环：Blender 在 GASP 的向前跑、冲刺的循环上逐帧做的（Private/anim/Mauser98k/tools/rifle_loops.py），在你的 Developers 目录里 */
	const TCHAR* const Mauser98kRunLoopPath = TEXT("/Game/Developers/JiahaoLi/Mauser98kLoops/M_Mauser98k_Run_Loop_F.M_Mauser98k_Run_Loop_F");
	const TCHAR* const Mauser98kSprintLoopPath = TEXT("/Game/Developers/JiahaoLi/Mauser98kLoops/M_Mauser98k_Sprint_Loop_F.M_Mauser98k_Sprint_Loop_F");

	/** 98k 的开镜循环：Blender 在开镜的姿势上做的（Private/anim/Mauser98k/tools/aim_loop.py），16 s，第一帧就是开镜的姿势 */
	const TCHAR* const Mauser98kAimLoopPath = TEXT("/Game/Developers/JiahaoLi/Mauser98kLoops/M_Mauser98k_Aim_Loop.M_Mauser98k_Aim_Loop");

	/** 两个层：持枪姿势（槽之前）与瞄准偏移（槽之后），同一个组，共用一个实例 */
	const FName UpperBodyLayer(TEXT("UpperBodyPose"));
	const FName AimOffsetLayer(TEXT("AimOffset"));
	const FName WeaponLayers[] = { UpperBodyLayer, AimOffsetLayer };
	const FName WeaponLayerGroup(TEXT("Weapon"));

	/** 每个层的输入姿势：进来的移动姿势（UpperBodyPose）或过了槽的姿势（AimOffset） */
	const FName LayerInputPose(TEXT("InPose"));

	UAnimGraphNode_Root* FindLayerRoot(const UEdGraph& Graph)
	{
		TArray<UAnimGraphNode_Root*> Roots;
		Graph.GetNodesOfClass(Roots);
		return Roots.IsEmpty() ? nullptr : Roots[0];
	}

	UAnimGraphNode_LinkedInputPose* FindLayerInput(const UEdGraph& Graph)
	{
		TArray<UAnimGraphNode_LinkedInputPose*> Inputs;
		Graph.GetNodesOfClass(Inputs);
		return Inputs.IsEmpty() ? nullptr : Inputs[0];
	}

	/** 实现了接口的动画蓝图里某个层的图 */
	UEdGraph* FindImplementedLayer(const UAnimBlueprint& Blueprint, const FName Layer)
	{
		for (const FBPInterfaceDescription& Interface : Blueprint.ImplementedInterfaces)
		{
			for (UEdGraph* Graph : Interface.Graphs)
			{
				if (Graph && Graph->GetFName() == Layer)
				{
					return Graph;
				}
			}
		}
		return nullptr;
	}

	/** 节点上默认藏着的属性露成引脚（如求值器的 Sequence、瞄准偏移的 BlendSpace），露了以后才能接成员 */
	void ExposePin(UAnimGraphNode_Base& Node, const FName Property, TArray<FString>& Problems)
	{
		for (FOptionalPinFromProperty& Pin : Node.ShowPinForProperties)
		{
			if (Pin.PropertyName == Property)
			{
				Pin.bShowPin = true;
				Node.ReconstructNode();
				return;
			}
		}
		Problems.Add(FString::Printf(TEXT("%s 没有能露成引脚的 %s"), *Node.GetName(), *Property.ToString()));
	}

	UAnimBlueprint* NewAnimBlueprint(const FString& PackageName, UClass* Parent, const EBlueprintType Type, TArray<FString>& Problems)
	{
		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, SkeletonPath);
		if (!Skeleton)
		{
			Problems.Add(FString::Printf(TEXT("找不到骨架 %s（先跑导入命令 GinkgoAnimationImport）"), SkeletonPath));
			return nullptr;
		}
		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->TargetSkeleton = Skeleton;
		Factory->ParentClass = Parent;
		Factory->BlueprintType = Type;
		UPackage* Package = CreatePackage(*PackageName);
		UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, FName(FPackageName::GetShortName(PackageName)),
			RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
		if (!Blueprint)
		{
			Problems.Add(FString::Printf(TEXT("建不了 %s"), *PackageName));
		}
		return Blueprint;
	}

	/** 编译、存一个新生成的动画蓝图；编译有错误或警告、存不了时记一条问题 */
	void CompileAndSaveNew(UAnimBlueprint& Blueprint, TArray<FString>& Problems)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		if (!CompileBlueprint(Blueprint))
		{
			Problems.Add(FString::Printf(TEXT("%s 编译有问题（见上面）"), *Blueprint.GetPathName()));
		}
		Blueprint.MarkPackageDirty();
		if (!SaveBlueprint(*Blueprint.GetPackage(), Blueprint))
		{
			Problems.Add(FString::Printf(TEXT("存不了 %s"), *Blueprint.GetPathName()));
		}
	}

	/**
	 * @brief 层接口 ALI_GinkgoWeaponLayers：两个层，各一个输入姿势 InPose，同在 Weapon 组
	 * @note 组记在层的输出节点上，编译时成为层函数的分类；实现接口的蓝图按分类把输出节点放进同一个组（ConformAnimGraphToInterface）。
	 *       5.8 新建的层本来就在一个共享组（DefaultSharedGroup）里，这里写明是武器的。已经有了就不动
	 */
	UAnimBlueprint* FindOrCreateLayerInterface(TArray<FString>& Problems)
	{
		if (FPackageName::DoesPackageExist(WeaponLayerInterfacePackage))
		{
			return LoadBlueprint(WeaponLayerInterfacePackage);
		}
		UAnimBlueprint* Interface = NewAnimBlueprint(WeaponLayerInterfacePackage, nullptr, BPTYPE_Interface, Problems);
		if (!Interface)
		{
			return nullptr;
		}
		// 新建的接口自带的层（如果有）去掉，只留这两个：实现接口的蓝图要把每个层都实现
		const TArray<UEdGraph*> Existing = Interface->FunctionGraphs;
		for (UEdGraph* Graph : Existing)
		{
			FBlueprintEditorUtils::RemoveGraph(Interface, Graph);
		}
		for (const FName Layer : WeaponLayers)
		{
			UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Interface, Layer, UAnimationGraph::StaticClass(), UAnimationGraphSchema::StaticClass());
			// 实现接口的蓝图按这个 GUID 认层（节点的 InterfaceGuid）：写死成第一次生成时的值，重新生成接口以后手改过的蓝图还认得（2026-11-05 接口丢过一次，蓝图有备份）
			FGuid::Parse(Layer == UpperBodyLayer ? TEXT("EBE08752491F5D24EB46EEA735782FD3") : TEXT("F603A47F4B2DEDF9C26C6EAA075C4A0A"), Graph->GraphGuid);
			FBlueprintEditorUtils::AddDomainSpecificGraph(Interface, Graph);
			UAnimGraphNode_Root* Root = FindLayerRoot(*Graph);
			if (!Root)
			{
				Problems.Add(FString::Printf(TEXT("层接口的 %s 没有输出节点"), *Layer.ToString()));
				continue;
			}
			Root->Node.SetGroup(WeaponLayerGroup);
			SpawnNode<UAnimGraphNode_LinkedInputPose>(*Graph, Root->NodePosX - 400, Root->NodePosY, [](UAnimGraphNode_LinkedInputPose& Node)
			{
				Node.Node.Name = LayerInputPose;
			});
		}
		CompileAndSaveNew(*Interface, Problems);
		return Interface;
	}

	/**
	 * @brief 动画蓝图实现层接口：接口的每个层成为蓝图里的一张图（输出节点与输入姿势由引擎按接口生成）
	 * @return 实现了没有（已经实现了也算）
	 */
	bool ImplementWeaponLayers(UAnimBlueprint& Blueprint, const UAnimBlueprint& Interface, TArray<FString>& Problems)
	{
		const bool bImplemented = Blueprint.ImplementedInterfaces.ContainsByPredicate([&Interface](const FBPInterfaceDescription& Description)
		{
			return Description.Interface == Interface.GeneratedClass;
		});
		if (!bImplemented && !FBlueprintEditorUtils::ImplementNewInterface(&Blueprint, Interface.GeneratedClass->GetClassPathName()))
		{
			Problems.Add(FString::Printf(TEXT("%s 实现不了 %s（有同名的图？）"), *Blueprint.GetName(), *Interface.GetName()));
			return false;
		}
		// 实现的图按接口长出输出节点与输入姿势；要的是蓝图的骨架类里有层的函数，后面的层节点才找得到
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(&Blueprint);
		for (const FName Layer : WeaponLayers)
		{
			const UEdGraph* Graph = FindImplementedLayer(Blueprint, Layer);
			if (!Graph || !FindLayerRoot(*Graph) || !FindLayerInput(*Graph))
			{
				Problems.Add(FString::Printf(TEXT("%s 实现的 %s 缺输出节点或输入姿势"), *Blueprint.GetName(), *Layer.ToString()));
			}
		}
		return true;
	}

	/** 层原样输出进来的姿势：主蓝图自己的实现，没链接武器的层（徒手）时用它 */
	void BuildPassThroughLayers(UAnimBlueprint& Blueprint, TArray<FString>& Problems)
	{
		for (const FName Layer : WeaponLayers)
		{
			UEdGraph* Graph = FindImplementedLayer(Blueprint, Layer);
			UAnimGraphNode_Root* Root = Graph ? FindLayerRoot(*Graph) : nullptr;
			UAnimGraphNode_LinkedInputPose* Input = Graph ? FindLayerInput(*Graph) : nullptr;
			if (Root && Input && FindPosePin(*Root, EGPD_Input)->LinkedTo.IsEmpty())
			{
				ConnectPose(*Input, *Root, Problems);
			}
		}
	}

	/**
	 * @brief 持枪循环的求值器：同移动状态机的循环那样循环着取，片段接父类的成员；时刻由节点函数设，要动态（Always Dynamic），否则编译时折叠成常量、设不进去
	 * @param Setup - 绑 On Become Relevant 的节点函数；没有时为 NAME_None
	 * @param Update - 绑 On Update 的节点函数
	 */
	UAnimGraphNode_SequenceEvaluator* SpawnLoopEvaluator(UEdGraph& Upper, const FName Member, const FName Setup, const FName Update, const int32 X, const int32 Y,
		TArray<FString>& Problems)
	{
		UAnimGraphNode_SequenceEvaluator* Loop = SpawnNode<UAnimGraphNode_SequenceEvaluator>(Upper, X, Y, [](UAnimGraphNode_SequenceEvaluator& Node)
		{
			NodeValue<bool>(Node, TEXT("bShouldLoop")) = true;
			Node.AlwaysDynamicProperties.Add(TEXT("ExplicitTime"));
		});
		ExposePin(*Loop, TEXT("Sequence"), Problems);
		BindMember(Upper, *Loop, TEXT("Sequence"), Member, X - 350, Y + 40, Problems);
		if (!Setup.IsNone())
		{
			Loop->BecomeRelevantFunction.SetSelfMember(Setup);
		}
		Loop->UpdateFunction.SetSelfMember(Update);
		return Loop;
	}

	/**
	 * @brief 步枪的层的 UpperBodyPose 里加上两个持枪循环（Docs/Design/Weapons.md「持枪循环」）：
	 *        低持的姿势 → 跑的循环（RunAlpha）→ 冲刺的循环（SprintAlpha）→ 原来的抬枪混合的 A。
	 *        两个循环的求值器循环着取，片段接父类的成员（每帧从手上那把枪的定义里取），时刻由父类的节点函数给（SetupRunLoop、UpdateRunLoop 这几个）；
	 *        分层混合改成先更新输入的姿势（bUpdateBasePoseFirst）：输入的姿势就是主蓝图的状态机，节点函数调到时它这一帧的片段与时刻已经记下了
	 * @param LoweredOut - 低持的姿势的输出（原来接抬枪混合的 A）
	 * @param RaiseA - 抬枪混合的 A
	 */
	void AddHoldLoopNodes(UEdGraph& Upper, UEdGraphPin& LoweredOut, UEdGraphPin& RaiseA, UAnimGraphNode_LayeredBoneBlend& Layered, const int32 X, const int32 Y,
		TArray<FString>& Problems)
	{
		UAnimGraphNode_SequenceEvaluator* Run = SpawnLoopEvaluator(Upper, TEXT("RunLoop"), TEXT("SetupRunLoop"), TEXT("UpdateRunLoop"), X, Y, Problems);
		UAnimGraphNode_SequenceEvaluator* Sprint = SpawnLoopEvaluator(Upper, TEXT("SprintLoop"), TEXT("SetupSprintLoop"), TEXT("UpdateSprintLoop"), X, Y + 200, Problems);
		UAnimGraphNode_TwoWayBlend* RunBlend = SpawnNode<UAnimGraphNode_TwoWayBlend>(Upper, X + 400, Y - 50);
		UAnimGraphNode_TwoWayBlend* SprintBlend = SpawnNode<UAnimGraphNode_TwoWayBlend>(Upper, X + 750, Y + 50);
		LoweredOut.BreakLinkTo(&RaiseA);
		Connect(&LoweredOut, RunBlend->FindPin(TEXT("A")), Problems);
		Connect(FindPosePin(*Run, EGPD_Output), RunBlend->FindPin(TEXT("B")), Problems);
		BindMember(Upper, *RunBlend, TEXT("Alpha"), TEXT("RunAlpha"), X + 150, Y + 150, Problems);
		Connect(FindPosePin(*RunBlend, EGPD_Output), SprintBlend->FindPin(TEXT("A")), Problems);
		Connect(FindPosePin(*Sprint, EGPD_Output), SprintBlend->FindPin(TEXT("B")), Problems);
		BindMember(Upper, *SprintBlend, TEXT("Alpha"), TEXT("SprintAlpha"), X + 500, Y + 350, Problems);
		Connect(FindPosePin(*SprintBlend, EGPD_Output), &RaiseA, Problems);
		Layered.Node.bUpdateBasePoseFirst = true;
		AddComment(Upper, X - 400, Y - 100, 1400, 500,
			TEXT("持枪循环：低持的姿势按地速混到跑的循环，再按冲刺进度换到冲刺的循环，之后才是抬枪、开镜。两个循环的时刻由节点函数按移动状态机这一帧的片段给")
			TEXT("（同一条时间线时同一个时刻，否则按脚的标记对步伐）；所以右边的分层混合先更新输入的姿势（bUpdateBasePoseFirst）。见 Docs/Design/Weapons.md「持枪循环」"));
	}

	/**
	 * @brief 步枪的层的 UpperBodyPose 里加上开镜的循环（Docs/Design/Weapons.md「持枪循环」）：开镜的姿势 →（AimLoopAlpha）开镜的循环 → 原来的开镜混合的 B。
	 *        枪有开镜的循环时它代替开镜的姿势（第一帧就是开镜的姿势）；片段接父类的成员 AimLoop，时刻由父类的节点函数 UpdateAimLoop 按主实例的时钟给
	 * @param AimOut - 开镜的姿势的输出（原来接开镜混合的 B）
	 * @param AdsB - 开镜混合的 B
	 */
	void AddAimLoopNodes(UEdGraph& Upper, UEdGraphPin& AimOut, UEdGraphPin& AdsB, const int32 X, const int32 Y, TArray<FString>& Problems)
	{
		UAnimGraphNode_SequenceEvaluator* Loop = SpawnLoopEvaluator(Upper, TEXT("AimLoop"), NAME_None, TEXT("UpdateAimLoop"), X, Y, Problems);
		UAnimGraphNode_TwoWayBlend* Blend = SpawnNode<UAnimGraphNode_TwoWayBlend>(Upper, X + 400, Y - 50);
		AimOut.BreakLinkTo(&AdsB);
		Connect(&AimOut, Blend->FindPin(TEXT("A")), Problems);
		Connect(FindPosePin(*Loop, EGPD_Output), Blend->FindPin(TEXT("B")), Problems);
		BindMember(Upper, *Blend, TEXT("Alpha"), TEXT("AimLoopAlpha"), X + 150, Y + 150, Problems);
		Connect(FindPosePin(*Blend, EGPD_Output), &AdsB, Problems);
		AddComment(Upper, X - 400, Y - 100, 1100, 350,
			TEXT("开镜的循环：枪有开镜的循环时代替开镜的姿势（AimLoopAlpha 为 1），第一帧就是开镜的姿势。时刻由节点函数按主实例的时钟给，一直走，")
			TEXT("不随开镜从头放。见 Docs/Design/Weapons.md「持枪循环」"));
	}

	/**
	 * @brief 步枪的层：UpperBodyPose 把低持、腰射、开镜三个姿势按两个进度混（两个 Two Way Blend），低持之后先混两个持枪循环（AddHoldLoopNodes），
	 *        开镜的姿势之后混开镜的循环（AddAimLoopNodes），
	 *        从 spine_01 往上（带 ik_hand_root）按网格空间的旋转分层混到进来的移动姿势上；AimOffset 把瞄准偏移（网格空间叠加）叠到进来的姿势上。
	 *        姿势与瞄准偏移的引脚接父类 UGinkgoWeaponAnimLayers 的成员：每帧从手上那把枪的定义里取
	 */
	void BuildRifleLayers(UAnimBlueprint& Blueprint, TArray<FString>& Problems)
	{
		UEdGraph* Upper = FindImplementedLayer(Blueprint, UpperBodyLayer);
		UAnimGraphNode_Root* UpperRoot = Upper ? FindLayerRoot(*Upper) : nullptr;
		UAnimGraphNode_LinkedInputPose* UpperInput = Upper ? FindLayerInput(*Upper) : nullptr;
		if (UpperRoot && UpperInput)
		{
			const int32 X = UpperRoot->NodePosX - 1700;
			const int32 Y = UpperRoot->NodePosY;
			const auto AddPose = [&](const FName Member, const int32 PoseY)
			{
				UAnimGraphNode_SequenceEvaluator* Pose = SpawnNode<UAnimGraphNode_SequenceEvaluator>(*Upper, X, PoseY, [](UAnimGraphNode_SequenceEvaluator& Node)
				{
					NodeValue<bool>(Node, TEXT("bShouldLoop")) = false;
				});
				ExposePin(*Pose, TEXT("Sequence"), Problems);
				BindMember(*Upper, *Pose, TEXT("Sequence"), Member, X - 350, PoseY + 40, Problems);
				return Pose;
			};
			UAnimGraphNode_SequenceEvaluator* Lowered = AddPose(TEXT("LoweredPose"), Y + 200);
			UAnimGraphNode_SequenceEvaluator* Hip = AddPose(TEXT("HipPose"), Y + 400);
			UAnimGraphNode_SequenceEvaluator* Aim = AddPose(TEXT("AimPose"), Y + 600);
			UAnimGraphNode_TwoWayBlend* Raise = SpawnNode<UAnimGraphNode_TwoWayBlend>(*Upper, X + 400, Y + 250);
			Connect(FindPosePin(*Lowered, EGPD_Output), Raise->FindPin(TEXT("A")), Problems);
			Connect(FindPosePin(*Hip, EGPD_Output), Raise->FindPin(TEXT("B")), Problems);
			BindMember(*Upper, *Raise, TEXT("Alpha"), TEXT("RaiseAlpha"), X + 150, Y + 450, Problems);
			UAnimGraphNode_TwoWayBlend* Ads = SpawnNode<UAnimGraphNode_TwoWayBlend>(*Upper, X + 750, Y + 300);
			Connect(FindPosePin(*Raise, EGPD_Output), Ads->FindPin(TEXT("A")), Problems);
			Connect(FindPosePin(*Aim, EGPD_Output), Ads->FindPin(TEXT("B")), Problems);
			BindMember(*Upper, *Ads, TEXT("Alpha"), TEXT("AdsAlpha"), X + 500, Y + 650, Problems);
			// 同蒙太奇的上半身槽：spine_01 往上，再加 IK 手的骨头（它们挂在 root 下）；网格空间的旋转让上半身保持朝向
			UAnimGraphNode_LayeredBoneBlend* Layered = SpawnNode<UAnimGraphNode_LayeredBoneBlend>(*Upper, X + 1150, Y, [](UAnimGraphNode_LayeredBoneBlend& Node)
			{
				Node.Node.BlendMode = ELayeredBoneBlendMode::BranchFilter;
				Node.Node.bMeshSpaceRotationBlend = true;
				Node.Node.LayerSetup.SetNum(1);
				FBranchFilter Spine;
				Spine.BoneName = TEXT("spine_01");
				Spine.BlendDepth = 0;
				FBranchFilter IKHands;
				IKHands.BoneName = TEXT("ik_hand_root");
				IKHands.BlendDepth = 0;
				Node.Node.LayerSetup[0].BranchFilters = { Spine, IKHands };
				Node.Node.BlendWeights.SetNum(1);
				Node.Node.BlendWeights[0] = 1.f;
				Node.Node.BlendPoses.SetNum(1);
			});
			Connect(FindPosePin(*UpperInput, EGPD_Output), Layered->FindPin(TEXT("BasePose")), Problems);
			Connect(FindPosePin(*Ads, EGPD_Output), Layered->FindPin(TEXT("BlendPoses_0")), Problems);
			BindMember(*Upper, *Layered, TEXT("BlendWeights_0"), TEXT("UpperBodyAlpha"), X + 850, Y + 150, Problems);
			ConnectPose(*Layered, *UpperRoot, Problems);
			AddHoldLoopNodes(*Upper, *FindPosePin(*Lowered, EGPD_Output), *Raise->FindPin(TEXT("A")), *Layered, X, Y + 900, Problems);
			AddAimLoopNodes(*Upper, *FindPosePin(*Aim, EGPD_Output), *Ads->FindPin(TEXT("B")), X, Y + 1600, Problems);
			AddComment(*Upper, X - 400, Y - 100, 1900, 900,
				TEXT("持枪姿势：低持、腰射、开镜三个姿势按抬枪、开镜两个进度混，从 spine_01 往上（带 ik_hand_root）按网格空间的旋转分层混到移动姿势上。")
				TEXT("姿势每帧从手上那把枪的定义里取（UGinkgoWeaponAnimLayers），见 Docs/Design/Weapons.md"));
		}
		else
		{
			Problems.Add(TEXT("步枪的层的 UpperBodyPose 缺输出节点或输入姿势"));
		}

		UEdGraph* AimGraph = FindImplementedLayer(Blueprint, AimOffsetLayer);
		UAnimGraphNode_Root* AimRoot = AimGraph ? FindLayerRoot(*AimGraph) : nullptr;
		UAnimGraphNode_LinkedInputPose* AimInput = AimGraph ? FindLayerInput(*AimGraph) : nullptr;
		if (AimRoot && AimInput)
		{
			const int32 X = AimRoot->NodePosX - 600;
			const int32 Y = AimRoot->NodePosY;
			UAnimGraphNode_RotationOffsetBlendSpace* Offset = SpawnNode<UAnimGraphNode_RotationOffsetBlendSpace>(*AimGraph, X, Y);
			ExposePin(*Offset, TEXT("BlendSpace"), Problems);
			BindMember(*AimGraph, *Offset, TEXT("BlendSpace"), TEXT("AimOffsetBlendSpace"), X - 350, Y + 100, Problems);
			BindMember(*AimGraph, *Offset, TEXT("X"), TEXT("AimYaw"), X - 350, Y + 200, Problems);
			BindMember(*AimGraph, *Offset, TEXT("Y"), TEXT("AimPitch"), X - 350, Y + 300, Problems);
			BindMember(*AimGraph, *Offset, TEXT("Alpha"), TEXT("AimOffsetAlpha"), X - 350, Y + 400, Problems);
			Connect(FindPosePin(*AimInput, EGPD_Output), Offset->FindPin(TEXT("BasePose")), Problems);
			ConnectPose(*Offset, *AimRoot, Problems);
			AddComment(*AimGraph, X - 450, Y - 100, 900, 600,
				TEXT("瞄准偏移：网格空间叠加，偏航、俯仰是视角相对网格根的。主蓝图里在蒙太奇槽之后，拉栓、装填也跟着视角转；冲刺时权重收到 0"));
		}
		else
		{
			Problems.Add(TEXT("步枪的层的 AimOffset 缺输出节点或输入姿势"));
		}
	}

	/**
	 * @brief 步枪的层 ABP_GinkgoWeaponLayers_Rifle（父类 UGinkgoWeaponAnimLayers，实现层接口）；已经有了就不动。
	 *        层的图不设淡入、淡出（默认 -1，换层时不请求惯性化）：父类自己按权重淡入淡出，见 UGinkgoWeaponAnimLayers
	 */
	UAnimBlueprint* FindOrCreateRifleLayers(const UAnimBlueprint& Interface, TArray<FString>& Problems)
	{
		if (FPackageName::DoesPackageExist(RifleLayersPackage))
		{
			return LoadBlueprint(RifleLayersPackage);
		}
		UAnimBlueprint* Rifle = NewAnimBlueprint(RifleLayersPackage, UGinkgoWeaponAnimLayers::StaticClass(), BPTYPE_Normal, Problems);
		if (!Rifle || !ImplementWeaponLayers(*Rifle, Interface, Problems))
		{
			return Rifle;
		}
		BuildRifleLayers(*Rifle, Problems);
		CompileAndSaveNew(*Rifle, Problems);
		return Rifle;
	}

	template <typename T>
	T* LoadAsset(const TCHAR* Path, TArray<FString>& Problems)
	{
		T* Asset = LoadObject<T>(nullptr, Path);
		if (!Asset)
		{
			Problems.Add(FString::Printf(TEXT("找不到 %s"), Path));
		}
		return Asset;
	}

	/**
	 * @brief 98k 的界面与机构（Docs/Design/Weapons.md「界面：武器栏与准星」「机构」）：游戏里的名字（你起的）、口径、武器栏的图标与
	 *        开镜准星的名字（页面 Content/UI/WeaponHud 认的），机构（内置弹仓 5 发、逐发压、压弹要开栓、手动拉栓）、弹种、开局的后备子弹
	 * @return 改了没有
	 */
	bool ApplyMauser98kHud(UGinkgoWeaponDefinition& Weapon)
	{
		bool bChanged = false;
		const auto SetText = [&bChanged](FText& Field, const FText& Value)
		{
			if (!Field.EqualTo(Value))
			{
				Field = Value;
				bChanged = true;
			}
		};
		const auto SetValue = [&bChanged](auto& Field, const auto& Value)
		{
			if (Field != Value)
			{
				Field = Value;
				bChanged = true;
			}
		};
		SetText(Weapon.DisplayName, NSLOCTEXT("Ginkgo", "Kar98kName", "Karabiner 98 kurz 7.92mm"));
		SetText(Weapon.CaliberName, NSLOCTEXT("Ginkgo", "Kar98kCaliber", "7.92 × 57mm Mauser"));
		SetValue(Weapon.HudIcon, FName(TEXT("Mauser98k")));
		SetValue(Weapon.AdsReticle, FName(TEXT("ZF39")));
		SetValue(Weapon.AmmoType, FGameplayTag(FWeaponTags::Ammo_Mauser792x57_Standard));
		// 二战德军步兵的两组三格弹盒，每格两个桥夹
		SetValue(Weapon.StartingReserve, 60);
		UGinkgoMagazineMechanism* Mechanism = Cast<UGinkgoMagazineMechanism>(Weapon.Mechanism);
		if (!Mechanism)
		{
			Mechanism = NewObject<UGinkgoMagazineMechanism>(&Weapon, NAME_None, RF_Transactional);
			Weapon.Mechanism = Mechanism;
			bChanged = true;
		}
		// 带 ZF39 的这把用不了桥夹，只能一发一发压，最多 5 发；控制供弹，不允许直接压膛
		SetValue(Mechanism->Rules.Feed, EGinkgoFeed::InternalMagazine);
		SetValue(Mechanism->Rules.Capacity, 5);
		SetValue(Mechanism->Rules.bLoadNeedsOpenAction, true);
		SetValue(Mechanism->Rules.bSelfLoading, false);
		return bChanged;
	}

	/**
	 * @brief 步枪的层的 UpperBodyPose 里，一个姿势的后面加上它的循环（Docs/Design/Weapons.md「持枪循环」）：姿势 →（Alpha）循环 → 原来接的地方。
	 *        枪有这个循环时它代替姿势（第一帧就是姿势）；片段接父类的成员，时刻由父类的节点函数按时钟给。枪没有这个循环时 Alpha 是 0，原样
	 * @param PoseOut - 姿势的输出
	 * @param Target - 它原来接的输入
	 */
	void AddClockLoopNodes(UEdGraph& Upper, UEdGraphPin& PoseOut, UEdGraphPin& Target, const FName Member, const FName Update, const FName Alpha, const int32 X, const int32 Y, TArray<FString>& Problems)
	{
		UAnimGraphNode_SequenceEvaluator* Loop = SpawnLoopEvaluator(Upper, Member, NAME_None, Update, X, Y, Problems);
		UAnimGraphNode_TwoWayBlend* Blend = SpawnNode<UAnimGraphNode_TwoWayBlend>(Upper, X + 400, Y - 50);
		PoseOut.BreakLinkTo(&Target);
		Connect(&PoseOut, Blend->FindPin(TEXT("A")), Problems);
		Connect(FindPosePin(*Loop, EGPD_Output), Blend->FindPin(TEXT("B")), Problems);
		BindMember(Upper, *Blend, TEXT("Alpha"), Alpha, X + 150, Y + 150, Problems);
		Connect(FindPosePin(*Blend, EGPD_Output), &Target, Problems);
		AddComment(Upper, X - 400, Y - 100, 1100, 350,
			*FString::Printf(TEXT("%s：枪有这个循环时代替姿势，第一帧就是姿势。时刻由节点函数按主实例的时钟给。由命令 GinkgoAnimBlueprint -AddM1911 加"), *Member.ToString()));
	}

	/**
	 * @brief 98k 的数据资产 DA_Mauser98k：网格、步枪的层、三个姿势与瞄准偏移（Blender 做的，在你的 Developers 目录里）、拔枪与收枪，
	 *        持枪 0.82、开镜 0.4 的地速倍率（你定的），界面与机构（ApplyMauser98kHud），其余用定义里的默认值。已经有了就不动：之后在编辑器里调
	 */
	UGinkgoWeaponDefinition* FindOrCreateMauser98k(UClass* LayerClass, TArray<FString>& Problems)
	{
		if (FPackageName::DoesPackageExist(Mauser98kPackage))
		{
			return LoadObject<UGinkgoWeaponDefinition>(nullptr, *FString::Printf(TEXT("%s.%s"), Mauser98kPackage, *FPackageName::GetShortName(Mauser98kPackage)));
		}
		UPackage* Package = CreatePackage(Mauser98kPackage);
		UGinkgoWeaponDefinition* Weapon = NewObject<UGinkgoWeaponDefinition>(Package, FName(FPackageName::GetShortName(Mauser98kPackage)),
			RF_Public | RF_Standalone | RF_Transactional);
		Weapon->DisplayName = NSLOCTEXT("Ginkgo", "Kar98kName", "98k");
		Weapon->Mesh = LoadAsset<UStaticMesh>(TEXT("/Game/Weapons/Mauser98k/SM_Mauser98k.SM_Mauser98k"), Problems);
		Weapon->AnimLayerClass = LayerClass;
		Weapon->LoweredPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/Mauser98kPoses/M_Mauser98k_Idle_Pose.M_Mauser98k_Idle_Pose"), Problems);
		Weapon->HipPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/Mauser98kPoses/M_Mauser98k_HipAim_Pose.M_Mauser98k_HipAim_Pose"), Problems);
		Weapon->AimPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/Mauser98kPoses/M_Mauser98k_Aim_Pose.M_Mauser98k_Aim_Pose"), Problems);
		Weapon->AimOffset = LoadAsset<UBlendSpace>(TEXT("/Game/Developers/JiahaoLi/Mauser98kAimOffset/AO_Mauser98k_ADS.AO_Mauser98k_ADS"), Problems);
		Weapon->RunLoop = LoadAsset<UAnimSequence>(Mauser98kRunLoopPath, Problems);
		Weapon->SprintLoop = LoadAsset<UAnimSequence>(Mauser98kSprintLoopPath, Problems);
		Weapon->AimLoop = LoadAsset<UAnimSequence>(Mauser98kAimLoopPath, Problems);
		Weapon->DrawMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_Draw.AM_Mauser98k_Draw"), Problems);
		Weapon->StowMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/Mauser98k/AM_Mauser98k_Stow.AM_Mauser98k_Stow"), Problems);
		// 全威力弹的长管步枪，还带一个瞄具：比一般的枪（Apex 的 0.87）慢
		Weapon->HeldSpeedScale = 0.82f;
		Weapon->AdsSpeedScale = 0.4f;
		ApplyMauser98kHud(*Weapon);
		Weapon->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Mauser98kPackage, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Weapon, *Filename, SaveArgs))
		{
			Problems.Add(FString::Printf(TEXT("存不了 %s"), Mauser98kPackage));
		}
		return Weapon;
	}

	/** 在动画图里放一个调层接口某个层的节点：同编辑器里从层的列表拖出来（UAnimGraphNode_LinkedAnimLayer::SetupFromLayerId 不导出，照它设） */
	UAnimGraphNode_LinkedAnimLayer* SpawnLayerNode(UEdGraph& Graph, const UAnimBlueprint& Blueprint, const FName Layer, const int32 X, const int32 Y, TArray<FString>& Problems)
	{
		UClass* Interface = nullptr;
		FGuid InterfaceGuid;
		for (const FBPInterfaceDescription& Description : Blueprint.ImplementedInterfaces)
		{
			for (const UEdGraph* InterfaceGraph : Description.Graphs)
			{
				if (InterfaceGraph && InterfaceGraph->GetFName() == Layer)
				{
					Interface = Description.Interface;
					InterfaceGuid = InterfaceGraph->InterfaceGuid;
				}
			}
		}
		if (!Interface)
		{
			Problems.Add(FString::Printf(TEXT("%s 没有实现层 %s"), *Blueprint.GetName(), *Layer.ToString()));
			return nullptr;
		}
		return SpawnNode<UAnimGraphNode_LinkedAnimLayer>(Graph, X, Y, [&](UAnimGraphNode_LinkedAnimLayer& Node)
		{
			Node.Node.Layer = Layer;
			Node.Node.Interface = Interface;
			Node.InterfaceGuid = InterfaceGuid;
			// 没指定实例的类：没链接武器的层时用蓝图自己实现的那份（徒手，原样输出）
			FStructProperty* Reference = FindFProperty<FStructProperty>(UAnimGraphNode_LinkedAnimGraphBase::StaticClass(), TEXT("FunctionReference"));
			check(Reference);
			Reference->ContainerPtrToValuePtr<FMemberReference>(&Node)->SetSelfMember(Layer);
		});
	}

	/** 层节点插在 From 的姿势输出与它原来的下游之间 */
	bool InsertLayerAfter(UEdGraph& Graph, const UAnimBlueprint& Blueprint, UEdGraphNode& From, const FName Layer, TArray<FString>& Problems)
	{
		UEdGraphPin* FromOut = FindPosePin(From, EGPD_Output);
		if (!FromOut || FromOut->LinkedTo.Num() != 1)
		{
			Problems.Add(FString::Printf(TEXT("%s 的姿势输出要正好接一处才能插层 %s，现在接了 %d 处"), *From.GetName(), *Layer.ToString(), FromOut ? FromOut->LinkedTo.Num() : 0));
			return false;
		}
		UEdGraphPin* Downstream = FromOut->LinkedTo[0];
		UAnimGraphNode_LinkedAnimLayer* Node = SpawnLayerNode(Graph, Blueprint, Layer, From.NodePosX + 250, From.NodePosY - 250, Problems);
		if (!Node)
		{
			return false;
		}
		FromOut->BreakAllPinLinks();
		Connect(FromOut, FindPosePin(*Node, EGPD_Input), Problems);
		Connect(FindPosePin(*Node, EGPD_Output), Downstream, Problems);
		return true;
	}

	/**
	 * @brief 主蓝图里插上武器的两个层：实现层接口（自己的实现原样输出，徒手时用），UpperBodyPose 插在状态机之后、蒙太奇槽之前，
	 *        AimOffset 插在混合（Dead Blending）之后：在槽之后（拉栓、装填也跟着视角转），又不让状态机的惯性化把它也混进去
	 *        （进入原地转身时枪口超前视线 3.2°，见 Docs/Design/Weapons.md「图里的顺序」）。换层由层自己淡入淡出，不用惯性化
	 */
	void InsertWeaponLayers(UAnimBlueprint& Blueprint, UEdGraph& AnimGraph, const UAnimBlueprint& Interface, TArray<FString>& Problems)
	{
		if (!ImplementWeaponLayers(Blueprint, Interface, Problems))
		{
			return;
		}
		BuildPassThroughLayers(Blueprint, Problems);
		UAnimGraphNode_StateMachine* Machine = nullptr;
		UAnimGraphNode_Slot* FullBody = nullptr;
		UAnimGraphNode_DeadBlending* Blending = nullptr;
		for (UEdGraphNode* Node : AnimGraph.Nodes)
		{
			UAnimGraphNode_StateMachine* Candidate = Cast<UAnimGraphNode_StateMachine>(Node);
			if (Candidate && (!Machine || Candidate->EditorStateMachineGraph->GetName() == TEXT("Locomotion")))
			{
				Machine = Candidate;
			}
			UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Node);
			FullBody = Slot && Slot->Node.SlotName == TEXT("DefaultSlot") ? Slot : FullBody;
			Blending = Blending ? Blending : Cast<UAnimGraphNode_DeadBlending>(Node);
		}
		if (!Machine || !FullBody || !Blending)
		{
			Problems.Add(TEXT("动画图里要有状态机、全身槽（DefaultSlot，先 -AddSlots）与混合（Dead Blending）才能插武器的层"));
			return;
		}
		InsertLayerAfter(AnimGraph, Blueprint, *Machine, UpperBodyLayer, Problems);
		InsertLayerAfter(AnimGraph, Blueprint, *Blending, AimOffsetLayer, Problems);
	}

	// ===== 胳膊的槽（拾取，Docs/Design/Interaction.md「拾取子弹盒」） =====

	/** 左右两条胳膊的槽与它们的根骨骼（分层从这里往下） */
	const FName ArmSlots[] = { FName(TEXT("ArmL")), FName(TEXT("ArmR")) };
	const FName ArmRoots[] = { FName(TEXT("clavicle_l")), FName(TEXT("clavicle_r")) };

	/**
	 * 两个胳膊的槽各一个组（引擎一个组只留一个蒙太奇）：左手拾取时拔枪照放，两只手能同时拾取。互斥交给占用（Docs/Design/AnimActions.md「占用」）。
	 * 2026-11-03 之前都在 Partials（同上半身槽），拾取与收枪、拔枪互相顶掉
	 */
	const FName ArmSlotGroups[] = { FName(TEXT("ArmLeft")), FName(TEXT("ArmRight")) };

	/** 右臂的槽还放拔枪、收枪：枪挂的虚拟骨骼也跟着片段（ik_hand_gun 相对 spine_05），手和枪的关系同片段 */
	const FName ArmSlotGunBone(TEXT("VB spine_05_ik_hand_gun"));

	bool SavePackageOf(UObject& Asset, TArray<FString>& Problems);

	/** @brief 骨架里登记两个胳膊的槽（放进 Partials 组），存回去；已经登记过的不改 */
	void RegisterArmSlots(TArray<FString>& Problems)
	{
		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, SkeletonPath);
		if (!Skeleton)
		{
			Problems.Add(FString::Printf(TEXT("读不到骨架 %s"), SkeletonPath));
			return;
		}
		bool bChanged = false;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FName Slot = ArmSlots[Index];
			if (Skeleton->ContainsSlotName(Slot) && Skeleton->GetSlotGroupName(Slot) == ArmSlotGroups[Index])
			{
				continue;
			}
			Skeleton->Modify();
			Skeleton->AddSlotGroupName(ArmSlotGroups[Index]);
			Skeleton->RegisterSlotNode(Slot);
			Skeleton->SetSlotGroupName(Slot, ArmSlotGroups[Index]);
			bChanged = true;
		}
		if (bChanged)
		{
			SavePackageOf(*Skeleton, Problems);
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] 骨架 %s 登记了槽：%s 在 %s 组，%s 在 %s 组"), SkeletonPath, *ArmSlots[0].ToString(), *ArmSlotGroups[0].ToString(), *ArmSlots[1].ToString(), *ArmSlotGroups[1].ToString());
		}
	}

	/**
	 * @brief 两个胳膊的槽插在 Upstream 与它原来的下游之间：进来的姿势存一份，左右两个槽各放在一份上，按局部空间的旋转分层混回去
	 *        （clavicle_l、clavicle_r 往下，各一层）
	 * @note 局部空间：胳膊照片段相对锁骨的父骨骼（spine_05）动，身体照原来的，不跟着片段前倾、转身；Control Rig 的全身 IK 再把手拉到东西上。
	 *       放在瞄准偏移的层之后：胳膊是最后盖上去的 FK。曲线不盖（DoNotOverride，同上半身槽：不然槽的权重会把别的曲线也缩了）
	 * @return 插上了没有
	 */
	bool InsertArmSlots(UEdGraph& Graph, UEdGraphNode& Upstream, TArray<FString>& Problems)
	{
		UEdGraphPin* UpstreamOut = FindPosePin(Upstream, EGPD_Output);
		if (!UpstreamOut || UpstreamOut->LinkedTo.Num() != 1)
		{
			Problems.Add(FString::Printf(TEXT("%s 的姿势输出要正好接一处才能插胳膊的槽，现在接了 %d 处"), *Upstream.GetName(), UpstreamOut ? UpstreamOut->LinkedTo.Num() : 0));
			return false;
		}
		UEdGraphPin* Downstream = UpstreamOut->LinkedTo[0];
		UpstreamOut->BreakAllPinLinks();

		const int32 X = Upstream.NodePosX;
		const int32 Y = Upstream.NodePosY + 500;
		UAnimGraphNode_SaveCachedPose* Save = SpawnNode<UAnimGraphNode_SaveCachedPose>(Graph, X, Y, [](UAnimGraphNode_SaveCachedPose& Node)
		{
			Node.CacheName = TEXT("ArmBase");
		});
		const auto MakeUse = [&Graph, Save](const int32 UseX, const int32 UseY)
		{
			return SpawnNode<UAnimGraphNode_UseCachedPose>(Graph, UseX, UseY, [Save](UAnimGraphNode_UseCachedPose& Node)
			{
				Node.SaveCachedPoseNode = Save;
				if (FStrProperty* NameProperty = FindFProperty<FStrProperty>(UAnimGraphNode_UseCachedPose::StaticClass(), TEXT("NameOfCache")))
				{
					NameProperty->SetPropertyValue_InContainer(&Node, Save->CacheName);
				}
			});
		};
		UAnimGraphNode_LayeredBoneBlend* Layered = SpawnNode<UAnimGraphNode_LayeredBoneBlend>(Graph, X + 850, Y, [](UAnimGraphNode_LayeredBoneBlend& Node)
		{
			Node.Node.BlendMode = ELayeredBoneBlendMode::BranchFilter;
			Node.Node.bMeshSpaceRotationBlend = false;
			Node.Node.LayerSetup.SetNum(2);
			Node.Node.BlendWeights.SetNum(2);
			Node.Node.BlendPoses.SetNum(2);
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FBranchFilter Arm;
				Arm.BoneName = ArmRoots[Index];
				Arm.BlendDepth = 0;
				Node.Node.LayerSetup[Index].BranchFilters = { Arm };
				Node.Node.BlendWeights[Index] = 1.f;
				if (Index == 1)
				{
					FBranchFilter Gun;
					Gun.BoneName = ArmSlotGunBone;
					Gun.BlendDepth = 0;
					Node.Node.LayerSetup[Index].BranchFilters.Add(Gun);
				}
			}
			Node.Node.CurveBlendOption = UpperBodyCurveBlend;
		});
		Connect(UpstreamOut, FindPosePin(*Save, EGPD_Input), Problems);
		Connect(FindPosePin(*MakeUse(X + 300, Y), EGPD_Output), Layered->FindPin(TEXT("BasePose")), Problems);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UAnimGraphNode_UseCachedPose* Use = MakeUse(X + 300, Y + 150 * (Index + 1));
			UAnimGraphNode_Slot* Slot = SpawnNode<UAnimGraphNode_Slot>(Graph, X + 550, Y + 150 * (Index + 1), [Index](UAnimGraphNode_Slot& Node)
			{
				Node.Node.SlotName = ArmSlots[Index];
			});
			ConnectPose(*Use, *Slot, Problems);
			Connect(FindPosePin(*Slot, EGPD_Output), Layered->FindPin(*FString::Printf(TEXT("BlendPoses_%d"), Index)), Problems);
		}
		Connect(FindPosePin(*Layered, EGPD_Output), Downstream, Problems);
		AddComment(Graph, X - 50, Y - 80, 1200, 520,
			TEXT("胳膊的槽：ArmL、ArmR（从 clavicle_l、clavicle_r 往下，局部空间的旋转）。拾取的蒙太奇放在这里，全身 IK 再把手拉到东西上，见 Docs/Design/Interaction.md「拾取子弹盒」"));
		return true;
	}

	/**
	 * -AddArmSlots：在已有的动画蓝图（默认是手改的那份）里插上两个胳膊的槽，插在瞄准偏移的层与它原来的下游（倾斜）之间，别的节点不动；
	 * 骨架里登记这两个槽。加 -From= 时先从那份复制到 -Package= 再改（量的时候用复制的）。原文件先备份到 Saved/Ginkgo/Backup；已经有了的不插
	 */
	int32 AddArmSlots(const FString& PackageName, const FString& Filename, const FString& FromPackage)
	{
		UAnimBlueprint* Blueprint = FromPackage.IsEmpty() ? LoadBlueprint(PackageName) : DuplicateBlueprint(FromPackage, PackageName);
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!AnimGraph)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或它的动画图"), *PackageName);
			return 1;
		}
		UAnimGraphNode_LinkedAnimLayer* AimOffset = nullptr;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			const UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(Node);
			if (Slot && (Slot->Node.SlotName == ArmSlots[0] || Slot->Node.SlotName == ArmSlots[1]))
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已经有胳膊的槽了，不插"), *PackageName);
				return 0;
			}
			UAnimGraphNode_LinkedAnimLayer* Layer = Cast<UAnimGraphNode_LinkedAnimLayer>(Node);
			AimOffset = Layer && Layer->Node.Layer == AimOffsetLayer ? Layer : AimOffset;
		}
		if (!AimOffset)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 的动画图里没有瞄准偏移的层（先 -AddWeaponLayers）"), *PackageName);
			return 1;
		}
		const FString Backup = FromPackage.IsEmpty() ? BackupBlueprint(FPackageName::GetShortName(PackageName), Filename) : FString(TEXT("（新复制的，没有）"));
		if (Backup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *Filename);
			return 1;
		}
		TArray<FString> Problems;
		RegisterArmSlots(Problems);
		InsertArmSlots(*AnimGraph, *AimOffset, Problems);
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, TEXT("插上了两个胳膊的槽"));
	}

	/**
	 * -ArmSlotsForSwap：拔枪、收枪搬到右臂的槽上要改的两处：骨架上两个胳膊的槽各一个组；动画蓝图里右臂那层多一个分支（枪挂的虚拟骨骼）。别的节点不动。
	 * 加 -From= 时先从那份复制到 -Package= 再改（量的时候用复制的）。原文件先备份到 Saved/Ginkgo/Backup；已经改过的不改
	 */
	int32 ArmSlotsForSwap(const FString& PackageName, const FString& Filename, const FString& FromPackage)
	{
		TArray<FString> Problems;
		RegisterArmSlots(Problems);
		UAnimBlueprint* Blueprint = FromPackage.IsEmpty() ? LoadBlueprint(PackageName) : DuplicateBlueprint(FromPackage, PackageName);
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!AnimGraph)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或它的动画图"), *PackageName);
			return 1;
		}
		UAnimGraphNode_LayeredBoneBlend* Layered = nullptr;
		int32 RightLayer = INDEX_NONE;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			UAnimGraphNode_LayeredBoneBlend* Candidate = Cast<UAnimGraphNode_LayeredBoneBlend>(Node);
			for (int32 Layer = 0; Candidate && Layer < Candidate->Node.LayerSetup.Num(); ++Layer)
			{
				if (Candidate->Node.LayerSetup[Layer].BranchFilters.ContainsByPredicate([](const FBranchFilter& Filter) { return Filter.BoneName == ArmRoots[1]; }))
				{
					Layered = Candidate;
					RightLayer = Layer;
				}
			}
		}
		if (!Layered)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 的动画图里没有胳膊的槽那个分层混合（先 -AddArmSlots）"), *PackageName);
			return 1;
		}
		TArray<FBranchFilter>& Filters = Layered->Node.LayerSetup[RightLayer].BranchFilters;
		if (Filters.ContainsByPredicate([](const FBranchFilter& Filter) { return Filter.BoneName == ArmSlotGunBone; }))
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 的右臂那层已经有 %s 了，不改"), *PackageName, *ArmSlotGunBone.ToString());
			return Problems.IsEmpty() ? 0 : 1;
		}
		const FString Backup = FromPackage.IsEmpty() ? BackupBlueprint(FPackageName::GetShortName(PackageName), Filename) : FString(TEXT("（新复制的，没有）"));
		if (Backup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *Filename);
			return 1;
		}
		Layered->Modify();
		FBranchFilter Gun;
		Gun.BoneName = ArmSlotGunBone;
		Gun.BlendDepth = 0;
		Filters.Add(Gun);
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, TEXT("右臂那层加了枪挂的虚拟骨骼"));
	}

	/**
	 * @brief 混合空间运行时取样用的三角剖分（或网格）只在编辑器的混合空间面板里改样本时才建（SAnimationBlendSpace 调 ResampleData），
	 *        载入时不建。Python 造的（Blender 管线的 ue_make_aimoffset.py 只设样本，Python 调不到 ResampleData）存下来是空的：
	 *        哪儿都取不到样本，节点原样输出、看不出错。在第一个样本的位置上取不到时重建并存回去
	 */
	void EnsureBlendSpaceData(UBlendSpace& BlendSpace, TArray<FString>& Problems)
	{
		const FBlendSample* First = BlendSpace.GetBlendSamples().FindByPredicate([](const FBlendSample& Sample) { return Sample.bIsValid; });
		if (!First)
		{
			Problems.Add(FString::Printf(TEXT("%s 没有有效的样本"), *BlendSpace.GetPathName()));
			return;
		}
		const auto CountSamples = [&BlendSpace, First]
		{
			TArray<FBlendSampleData> Samples;
			int32 TriangulationIndex = INDEX_NONE;
			BlendSpace.GetSamplesFromBlendInput(First->SampleValue, Samples, TriangulationIndex, false);
			return Samples.Num();
		};
		if (CountSamples() > 0)
		{
			return;
		}
		BlendSpace.ResampleData();
		const int32 Found = CountSamples();
		if (Found == 0)
		{
			Problems.Add(FString::Printf(TEXT("%s 重建了运行时的数据还是取不到样本"), *BlendSpace.GetPathName()));
			return;
		}
		UPackage* Package = BlendSpace.GetPackage();
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, &BlendSpace, *Filename, SaveArgs))
		{
			Problems.Add(FString::Printf(TEXT("存不了 %s"), *Package->GetName()));
			return;
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 没有运行时的三角剖分，重建了（%d 个三角形，在第一个样本上取到 %d 个），存回去了"),
			*BlendSpace.GetPathName(), BlendSpace.GetBlendSpaceData().Triangles.Num(), Found);
	}

	/**
	 * -AddWeaponLayers：先生成层接口、步枪的层与 98k 的数据资产（已经有的不动；数据资产的瞄准偏移缺运行时的数据时补上），
	 * 再在已有的动画蓝图（默认是手改的那份）里插上武器的两个层，别的节点都不动。加 -From= 时先从那份复制到 -Package= 再插。
	 * 原文件先备份到 Saved/Ginkgo/Backup；已经有层节点的不插
	 */
	int32 AddWeaponLayers(const FString& PackageName, const FString& Filename, const FString& FromPackage)
	{
		TArray<FString> Problems;
		UAnimBlueprint* Interface = FindOrCreateLayerInterface(Problems);
		UAnimBlueprint* Rifle = Interface ? FindOrCreateRifleLayers(*Interface, Problems) : nullptr;
		UGinkgoWeaponDefinition* Weapon = Rifle ? FindOrCreateMauser98k(Rifle->GeneratedClass, Problems) : nullptr;
		if (Weapon && Weapon->AimOffset)
		{
			EnsureBlendSpaceData(*Weapon->AimOffset, Problems);
		}
		if (!Problems.IsEmpty())
		{
			for (const FString& Problem : Problems)
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
			}
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 层接口、步枪的层或数据资产有问题，不改 %s"), *PackageName);
			return 1;
		}

		UAnimBlueprint* Blueprint = FromPackage.IsEmpty() ? LoadBlueprint(PackageName) : DuplicateBlueprint(FromPackage, PackageName);
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!AnimGraph)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或它的动画图"), *PackageName);
			return 1;
		}
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (Node->IsA<UAnimGraphNode_LinkedAnimLayer>())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已经有层节点了，不插"), *PackageName);
				return 0;
			}
		}
		// 刚复制出来的还没存过，没有原文件可备份
		const FString Backup = FromPackage.IsEmpty() ? BackupBlueprint(FPackageName::GetShortName(PackageName), Filename) : FString(TEXT("（新复制的，没有）"));
		if (Backup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *Filename);
			return 1;
		}
		InsertWeaponLayers(*Blueprint, *AnimGraph, *Interface, Problems);
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, TEXT("插上了武器的两个层"));
	}

	/**
	 * -RestoreWeaponFromText=<文本>：按 da_dump_t3d.py 导出的文本重建 98k 的数据资产（2026-11-05 资产丢了，文本还在）。
	 * 文本只写与类默认值不同的属性，所以从新的资产开始；已经有这个包时不动。之后再导出一次同文本比
	 */
	int32 RestoreWeaponFromText(const FString& TextFile)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *TextFile))
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s"), *TextFile);
			return 1;
		}
		if (FPackageName::DoesPackageExist(Mauser98kPackage))
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 已经有了，不动"), Mauser98kPackage);
			return 1;
		}
		// 第一行是资产自己的 Begin Object，最后一个 End Object 是它的结尾
		const int32 BodyStart = Text.Find(TEXT("\n"));
		const int32 BodyEnd = Text.Find(TEXT("End Object"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (BodyStart == INDEX_NONE || BodyEnd <= BodyStart)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 不是一个 Begin Object 块"), *TextFile);
			return 1;
		}
		const FString Body = Text.Mid(BodyStart + 1, BodyEnd - BodyStart - 1);
		UPackage* Package = CreatePackage(Mauser98kPackage);
		Package->FullyLoad();
		UGinkgoWeaponDefinition* Weapon = NewObject<UGinkgoWeaponDefinition>(Package, *FPackageName::GetShortName(Mauser98kPackage), RF_Public | RF_Standalone | RF_Transactional);
		FImportObjectParams ImportParams;
		ImportParams.DestData = reinterpret_cast<uint8*>(Weapon);
		ImportParams.SourceText = *Body;
		ImportParams.ObjectStruct = Weapon->GetClass();
		ImportParams.SubobjectRoot = Weapon;
		ImportParams.SubobjectOuter = Weapon;
		ImportParams.Warn = GWarn;
		ImportObjectProperties(ImportParams);
		FAssetRegistryModule::AssetCreated(Weapon);
		TArray<FString> Problems;
		SavePackageOf(*Weapon, Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		UE_CLOG(Problems.IsEmpty(), LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 按 %s 重建了"), Mauser98kPackage, *TextFile);
		return Problems.IsEmpty() ? 0 : 1;
	}

	// ===== 持枪第 2 批：瞄准与双手（Docs/Design/Weapons.md「手与瞄准」） =====

	/** 存一个包（不是动画蓝图的：数据资产、Control Rig） */
	bool SavePackageOf(UObject& Asset, TArray<FString>& Problems)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(Asset.GetPackage()->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		Asset.MarkPackageDirty();
		if (!UPackage::SavePackage(Asset.GetPackage(), &Asset, *Filename, SaveArgs))
		{
			Problems.Add(FString::Printf(TEXT("存不了 %s"), *Filename));
			return false;
		}
		return true;
	}

	/**
	 * @brief 数据资产里枪上的点：三个姿势里两只手相对枪挂的骨骼的变换（虚拟骨骼在组件空间里就是 ik_hand_gun），
	 *        网格最前端那圈顶点的中心（枪口）。量出来与存着的不同时写进去、存回去
	 */
	void EnsureWeaponAnchors(UGinkgoWeaponDefinition& Weapon, TArray<FString>& Problems)
	{
		const auto MeasureHands = [&Problems](const UAnimSequenceBase* Pose, const double Time, FGinkgoWeaponHandTargets& Out)
		{
			if (!Pose)
			{
				Problems.Add(TEXT("数据资产缺持枪的姿势"));
				return;
			}
			FAnimPose AnimPose;
			UAnimPoseExtensions::GetAnimPoseAtTime(Pose, Time, FAnimPoseEvaluationOptions(), AnimPose);
			const FTransform Gun = UAnimPoseExtensions::GetBonePose(AnimPose, TEXT("ik_hand_gun"), EAnimPoseSpaces::World);
			Out.Left = UAnimPoseExtensions::GetBonePose(AnimPose, TEXT("hand_l"), EAnimPoseSpaces::World).GetRelativeTransform(Gun);
			Out.Right = UAnimPoseExtensions::GetBonePose(AnimPose, TEXT("hand_r"), EAnimPoseSpaces::World).GetRelativeTransform(Gun);
		};
		FGinkgoWeaponHandTargets Lowered, Hip, Aim;
		MeasureHands(Weapon.LoweredPose, 0.0, Lowered);
		MeasureHands(Weapon.HipPose, 0.0, Hip);
		MeasureHands(Weapon.AimPose, 0.0, Aim);

		// 冲刺的循环：右手的握法取第一帧的。单手冲刺时左手不在枪上，目标留在低持的护木上：权重随冲刺进度放到 0，不往别处拉
		FGinkgoWeaponHandTargets Sprint = Lowered;
		if (Weapon.SprintLoop)
		{
			MeasureHands(Weapon.SprintLoop, 0.0, Sprint);
			Sprint.Left = Weapon.bSprintTwoHanded ? Sprint.Left : Lowered.Left;
		}
		// 两个循环里手在枪上的握法应当逐帧不变（Blender 里手按枪上的握持点放）：跑的循环对低持的，冲刺的对它第一帧的，逐帧量了写进日志
		const auto CheckLoopGrip = [&](const UAnimSequence* Loop, const FGinkgoWeaponHandTargets& Reference, const bool bLeftOnGun, const TCHAR* What)
		{
			if (!Loop)
			{
				return;
			}
			double Left = 0.0;
			double Right = 0.0;
			double RightTurn = 0.0;
			int32 Frames = 0;
			for (double Time = 0.0; Time <= Loop->GetPlayLength() + UE_KINDA_SMALL_NUMBER; Time += 1.0 / 30.0, ++Frames)
			{
				FGinkgoWeaponHandTargets Hands;
				MeasureHands(Loop, FMath::Min(Time, static_cast<double>(Loop->GetPlayLength())), Hands);
				Left = FMath::Max(Left, FVector::Dist(Hands.Left.GetLocation(), Reference.Left.GetLocation()));
				Right = FMath::Max(Right, FVector::Dist(Hands.Right.GetLocation(), Reference.Right.GetLocation()));
				RightTurn = FMath::Max(RightTurn, FMath::RadiansToDegrees(Hands.Right.GetRotation().AngularDistance(Reference.Right.GetRotation())));
			}
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s %s：%d 帧里手在枪上离%s最多 左手 %.3f cm%s、右手 %.3f cm（转 %.2f°）"), *Weapon.GetName(), *Loop->GetName(),
				Frames, What, Left, bLeftOnGun ? TEXT("") : TEXT("（不在枪上，不用）"), Right, RightTurn);
		};
		CheckLoopGrip(Weapon.RunLoop, Lowered, true, TEXT("低持的握法"));
		CheckLoopGrip(Weapon.SprintLoop, Sprint, Weapon.bSprintTwoHanded, TEXT("它第一帧的握法"));
		CheckLoopGrip(Weapon.AimLoop, Aim, true, TEXT("开镜的握法"));
		// 开镜的循环代替开镜的姿势：枪管方向应当逐帧与开镜的姿势相同（只平移），枪口对准才用不着改；逐帧量了写进日志
		if (Weapon.AimLoop && Weapon.AimPose)
		{
			const auto GunAt = [](const UAnimSequenceBase* Pose, const double Time)
			{
				FAnimPose AnimPose;
				UAnimPoseExtensions::GetAnimPoseAtTime(Pose, Time, FAnimPoseEvaluationOptions(), AnimPose);
				return UAnimPoseExtensions::GetBonePose(AnimPose, TEXT("ik_hand_gun"), EAnimPoseSpaces::World);
			};
			const FTransform AimGun = GunAt(Weapon.AimPose, 0.0);
			const double Length = Weapon.AimLoop->GetPlayLength();
			double Turn = 0.0;
			FBox Shift(ForceInit);
			int32 Frames = 0;
			for (double Time = 0.0; Time <= Length + UE_KINDA_SMALL_NUMBER; Time += 1.0 / 30.0, ++Frames)
			{
				const FTransform Gun = GunAt(Weapon.AimLoop, FMath::Min(Time, Length));
				Turn = FMath::Max(Turn, FMath::RadiansToDegrees(Gun.GetRotation().AngularDistance(AimGun.GetRotation())));
				Shift += Gun.GetLocation() - AimGun.GetLocation();
			}
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s %s：%d 帧里枪管方向离开镜的姿势最多 %.4f°，枪离开镜的位置 X %.2f..%.2f、Y %.2f..%.2f、Z %.2f..%.2f cm"),
				*Weapon.GetName(), *Weapon.AimLoop->GetName(), Frames, Turn, Shift.Min.X, Shift.Max.X, Shift.Min.Y, Shift.Max.Y, Shift.Min.Z, Shift.Max.Z);
		}

		// 枪口：网格最前端（X 最大）0.5 cm 以内那圈顶点的中心
		FVector Muzzle = FVector::ZeroVector;
		const FStaticMeshRenderData* RenderData = Weapon.Mesh ? Weapon.Mesh->GetRenderData() : nullptr;
		if (!RenderData || RenderData->LODResources.IsEmpty())
		{
			Problems.Add(TEXT("数据资产的网格没有渲染数据，量不了枪口"));
			return;
		}
		const FPositionVertexBuffer& Positions = RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
		double MaxX = -UE_BIG_NUMBER;
		for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
		{
			MaxX = FMath::Max(MaxX, static_cast<double>(Positions.VertexPosition(Index).X));
		}
		int32 Count = 0;
		FBox2D Ring(ForceInit);
		for (uint32 Index = 0; Index < Positions.GetNumVertices(); ++Index)
		{
			const FVector Position(Positions.VertexPosition(Index));
			if (Position.X >= MaxX - 0.5)
			{
				Muzzle += Position;
				Ring += FVector2D(Position.Y, Position.Z);
				++Count;
			}
		}
		Muzzle /= FMath::Max(Count, 1);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 的枪口：%d 个顶点，中心 %s，Y %.2f..%.2f，Z %.2f..%.2f"), *Weapon.GetName(), Count, *Muzzle.ToString(),
			Ring.Min.X, Ring.Max.X, Ring.Min.Y, Ring.Max.Y);

		const auto Same = [](const FGinkgoWeaponHandTargets& A, const FGinkgoWeaponHandTargets& B)
		{
			return A.Left.Equals(B.Left, 1e-3) && A.Right.Equals(B.Right, 1e-3);
		};
		if (Same(Weapon.LoweredHands, Lowered) && Same(Weapon.HipHands, Hip) && Same(Weapon.AimHands, Aim) && Same(Weapon.SprintHands, Sprint)
			&& Weapon.MuzzleOffset.Equals(Muzzle, 1e-3))
		{
			return;
		}
		Weapon.LoweredHands = Lowered;
		Weapon.HipHands = Hip;
		Weapon.AimHands = Aim;
		Weapon.SprintHands = Sprint;
		Weapon.MuzzleOffset = Muzzle;
		const TPair<const TCHAR*, const FGinkgoWeaponHandTargets*> Logged[] = { { TEXT("低持"), &Lowered }, { TEXT("腰射"), &Hip }, { TEXT("开镜"), &Aim },
			{ TEXT("冲刺"), &Sprint } };
		for (const TPair<const TCHAR*, const FGinkgoWeaponHandTargets*>& Entry : Logged)
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s %s：左手 %s，右手 %s"), *Weapon.GetName(), Entry.Key, *Entry.Value->Left.GetLocation().ToString(),
				*Entry.Value->Right.GetLocation().ToString());
		}
		if (SavePackageOf(Weapon, Problems))
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 量好了枪上的点，存回去了"), *Weapon.GetName());
		}
	}

	/** 节点的某个引脚接着的动画实例成员（BindMember 接的变量节点）；没接时为 NAME_None */
	FName BoundMember(const UEdGraphNode& Node, const FName PinName)
	{
		const UEdGraphPin* Pin = Node.FindPin(PinName);
		const UK2Node_VariableGet* Get = Pin && Pin->LinkedTo.Num() == 1 ? Cast<UK2Node_VariableGet>(Pin->LinkedTo[0]->GetOwningNode()) : nullptr;
		return Get ? Get->VariableReference.GetMemberName() : NAME_None;
	}

	/**
	 * 手枪的声音（你给的 audio_selected/M1911，Private/audio/tools/split_m1911_audio.py 切的，weapon_audio_ue.py 生成 MetaSound）：提前量是切好以后的撞击点，
	 * 音量是我按波形同 98k 的枪栓、枪声比出来的。给的素材里没有的（空仓、拔枪收枪）不配；落地的声音也没有，类默认值是 98k 的，清掉
	 */
	/** AK-47 的声音（你给的 audio_selected/AK47-fire，split_ak47_audio.py 切的，/Game/Weapons/AK47/Audio）：音量按到输出的响度同前两把枪比，提前量在 parts.json */
	void FillAK47Cues(UGinkgoWeaponDefinition& Weapon)
	{
		const auto AddCue = [&Weapon](const FGameplayTag& Tag, const TCHAR* Group, const float Volume, const float Lead, const float FadePrevious = 0.f)
		{
			FGinkgoWeaponCue Cue;
			Cue.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(FString::Printf(TEXT("/Game/Weapons/AK47/Audio/MS_AK47_%s.MS_AK47_%s"), Group, Group)));
			Cue.Volume = Volume;
			Cue.Lead = Lead;
			Cue.FadePrevious = FadePrevious;
			Weapon.Cues.Add(Tag, Cue);
		};
		Weapon.Cues.Reset();
		AddCue(FWeaponTags::Weapon_Cue_Fire, TEXT("Fire"), 0.6f, 0.f, 0.25f);
		AddCue(FWeaponTags::Weapon_Cue_MagOut, TEXT("MagOut"), 1.f, 0.0618f);
		AddCue(FWeaponTags::Weapon_Cue_MagIn, TEXT("MagIn"), 0.57f, 0.4351f);
		AddCue(FWeaponTags::Weapon_Cue_BoltPull, TEXT("BoltPull"), 0.56f, 0.1951f);
		AddCue(FWeaponTags::Weapon_Cue_SlideHome, TEXT("SlideHome"), 1.3f, 0.07f);
	}

	void FillM1911Cues(UGinkgoWeaponDefinition& Weapon)
	{
		const auto AddCue = [&Weapon](const FGameplayTag& Tag, const TCHAR* Name, const float Volume, const float Lead)
		{
			FGinkgoWeaponCue Cue;
			Cue.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(FString::Printf(TEXT("/Game/Weapons/M1911/Audio/MS_M1911_%s.MS_M1911_%s"), Name, Name)));
			Cue.Volume = Volume;
			Cue.Lead = Lead;
			Weapon.Cues.Add(Tag, Cue);
		};
		Weapon.Cues.Reset();
		AddCue(FWeaponTags::Weapon_Cue_Fire, TEXT("Fire"), 0.7f, 0.f);
		AddCue(FWeaponTags::Weapon_Cue_MagOut, TEXT("MagOut"), 0.35f, 0.0034f);
		AddCue(FWeaponTags::Weapon_Cue_MagIn, TEXT("MagIn"), 1.f, 0.1251f);
		AddCue(FWeaponTags::Weapon_Cue_SlideHome, TEXT("SlideHome"), 0.8f, 0.0222f);
		Weapon.Rounds.HitSoundHard.Reset();
		Weapon.Rounds.HitSoundSoft.Reset();
	}

	/**
	 * 手枪的散布（Docs/Design/Weapons.md「散布」），数都是我定的：腰射比步枪准，开镜比步枪散；一枪散得少，按射速连着开累积上去，第 7 发到上限；
	 * 每枪隔 0.4 s 以上就都收回来，慢慢开一直准
	 */
	void FillM1911Spread(UGinkgoWeaponDefinition& Weapon)
	{
		Weapon.CrosshairGap = 5.f;
		FGinkgoWeaponSpread& Spread = Weapon.Spread;
		Spread.HipDefault = 0.8f;
		Spread.AdsDefault = 0.2f;
		Spread.GrowthPerShot = 0.55f;
		Spread.RecoveryRate = 1.5f;
		Spread.HipMax = 3.f;
		Spread.AdsMax = 1.5f;
		Spread.AirFactor = 2.f;
		FRichCurve* SpeedFactor = Spread.SpeedFactor.GetRichCurve();
		SpeedFactor->Reset();
		SpeedFactor->AddKey(50.f, 1.f);
		SpeedFactor->AddKey(200.f, 1.2f);
		SpeedFactor->AddKey(450.f, 1.6f);
		SpeedFactor->AddKey(720.f, 2.2f);
	}

	/**
	 * -AddM1911：步枪的层里加上低持、腰射两个循环（先备份，已经有的不加；枪没有这两个循环时权重是 0，98k 的姿势不变），再生成手枪的数据资产 DA_M1911。
	 * 数据资产已经有了只量握法（可能手改过），加 -RebuildM1911 才重建。后面的步骤：GinkgoWeaponStateTree -Weapon=/Game/Weapons/M1911/DA_M1911 填子树与片段
	 */
	int32 AddM1911(const bool bRebuild)
	{
		TArray<FString> Problems;
		UAnimBlueprint* Rifle = LoadBlueprint(RifleLayersPackage);
		UEdGraph* Upper = Rifle ? FindImplementedLayer(*Rifle, UpperBodyLayer) : nullptr;
		if (!Upper)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 %s 或它的 UpperBodyPose（先 -AddWeaponLayers）"), RifleLayersPackage);
			return 1;
		}
		UAnimGraphNode_SequenceEvaluator* Lowered = nullptr;
		UAnimGraphNode_SequenceEvaluator* Hip = nullptr;
		bool bHasLoweredLoop = false;
		bool bHasHipLoop = false;
		for (UEdGraphNode* Node : Upper->Nodes)
		{
			if (UAnimGraphNode_SequenceEvaluator* Evaluator = Cast<UAnimGraphNode_SequenceEvaluator>(Node))
			{
				const FName Member = BoundMember(*Evaluator, TEXT("Sequence"));
				Lowered = Member == TEXT("LoweredPose") ? Evaluator : Lowered;
				Hip = Member == TEXT("HipPose") ? Evaluator : Hip;
				bHasLoweredLoop = bHasLoweredLoop || Member == TEXT("LoweredLoop");
				bHasHipLoop = bHasHipLoop || Member == TEXT("HipLoop");
			}
		}
		if (!bHasLoweredLoop || !bHasHipLoop)
		{
			UEdGraphPin* LoweredOut = Lowered ? FindPosePin(*Lowered, EGPD_Output) : nullptr;
			UEdGraphPin* HipOut = Hip ? FindPosePin(*Hip, EGPD_Output) : nullptr;
			UEdGraphPin* LoweredTarget = LoweredOut && LoweredOut->LinkedTo.Num() == 1 ? LoweredOut->LinkedTo[0] : nullptr;
			UEdGraphPin* HipTarget = HipOut && HipOut->LinkedTo.Num() == 1 ? HipOut->LinkedTo[0] : nullptr;
			if (!LoweredTarget || !HipTarget)
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 的 UpperBodyPose 里找不到低持、腰射的姿势或它们接的地方，不改"), RifleLayersPackage);
				return 1;
			}
			const FString RifleFile = FPackageName::LongPackageNameToFilename(RifleLayersPackage, FPackageName::GetAssetPackageExtension());
			const FString RifleBackup = BackupBlueprint(FPackageName::GetShortName(RifleLayersPackage), RifleFile);
			if (RifleBackup.IsEmpty())
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *RifleFile);
				return 1;
			}
			if (!bHasLoweredLoop)
			{
				AddClockLoopNodes(*Upper, *LoweredOut, *LoweredTarget, TEXT("LoweredLoop"), TEXT("UpdateLoweredLoop"), TEXT("LoweredLoopAlpha"), Lowered->NodePosX, Lowered->NodePosY + 2200, Problems);
			}
			if (!bHasHipLoop)
			{
				AddClockLoopNodes(*Upper, *HipOut, *HipTarget, TEXT("HipLoop"), TEXT("UpdateHipLoop"), TEXT("HipLoopAlpha"), Hip->NodePosX, Hip->NodePosY + 2600, Problems);
			}
			const int32 Result = CompileAndSaveEdited(*Rifle, RifleFile, RifleBackup, Problems, TEXT("加上了低持、腰射两个循环"));
			if (Result != 0)
			{
				return Result;
			}
		}

		const FString Package = TEXT("/Game/Weapons/M1911/DA_M1911");
		const FString ObjectPath = Package + TEXT(".DA_M1911");
		UGinkgoWeaponDefinition* Weapon = FPackageName::DoesPackageExist(Package) ? LoadObject<UGinkgoWeaponDefinition>(nullptr, *ObjectPath) : nullptr;
		if (!Weapon || bRebuild)
		{
			UPackage* WeaponPackage = CreatePackage(*Package);
			WeaponPackage->FullyLoad();
			if (UObject* Existing = StaticFindObject(UObject::StaticClass(), WeaponPackage, TEXT("DA_M1911")))
			{
				Existing->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
				Existing->ClearFlags(RF_Public | RF_Standalone);
			}
			Weapon = NewObject<UGinkgoWeaponDefinition>(WeaponPackage, TEXT("DA_M1911"), RF_Public | RF_Standalone | RF_Transactional);
			Weapon->DisplayName = NSLOCTEXT("Ginkgo", "M1911Name", "M1911A1 .45 ACP");
			Weapon->CaliberName = NSLOCTEXT("Ginkgo", "M1911Caliber", ".45 ACP");
			Weapon->HudIcon = TEXT("M1911");
			Weapon->AdsReticle = NAME_None;
			FillM1911Spread(*Weapon);
			Weapon->SlotKind = EGinkgoWeaponSlotKind::Sidearm;
			Weapon->Mesh = LoadAsset<UStaticMesh>(TEXT("/Game/Weapons/M1911/SM_M1911.SM_M1911"), Problems);
			Weapon->SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Weapons/M1911/SK_M1911.SK_M1911")));
			Weapon->Bolt.CarrierBone = TEXT("slide");
			Weapon->Bolt.HandleBone = NAME_None;
			Weapon->Bolt.Stroke = 6.35f;
			Weapon->Bolt.LiftAngle = 0.f;
			Weapon->GunRestClosed = LoadAsset<UAnimSequence>(TEXT("/Game/Weapons/M1911/Anims/A_M1911_Fire.A_M1911_Fire"), Problems);
			Weapon->GunRestOpen = LoadAsset<UAnimSequence>(TEXT("/Game/Weapons/M1911/Anims/A_M1911_FireLast.A_M1911_FireLast"), Problems);
			Weapon->MagazineBone = TEXT("mag");
			Weapon->AnimLayerClass = Rifle->GeneratedClass;
			Weapon->LoweredPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/M1911Poses/M_M1911_Idle_Pose.M_M1911_Idle_Pose"), Problems);
			Weapon->HipPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/M1911Poses/M_M1911_HipAim_Pose.M_M1911_HipAim_Pose"), Problems);
			Weapon->AimPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/M1911Poses/M_M1911_Aim_Pose.M_M1911_Aim_Pose"), Problems);
			Weapon->AimOffset = LoadAsset<UBlendSpace>(TEXT("/Game/Developers/JiahaoLi/Mauser98kAimOffset/AO_Mauser98k_ADS.AO_Mauser98k_ADS"), Problems);
			Weapon->RunLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M1911Loops/M_M1911_Run_Loop_F.M_M1911_Run_Loop_F"), Problems);
			Weapon->SprintLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M1911Loops/M_M1911_Sprint_Loop_F.M_M1911_Sprint_Loop_F"), Problems);
			Weapon->AimLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M1911Loops/M_M1911_Aim_Loop.M_M1911_Aim_Loop"), Problems);
			Weapon->LoweredLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M1911Loops/M_M1911_Idle_Loop.M_M1911_Idle_Loop"), Problems);
			Weapon->HipLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M1911Loops/M_M1911_HipAim_Loop.M_M1911_HipAim_Loop"), Problems);
			Weapon->bSprintTwoHanded = false;
			Weapon->bRunTwoHanded = false;
			Weapon->DrawMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/M1911/AM_M1911_Draw.AM_M1911_Draw"), Problems);
			Weapon->StowMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/M1911/AM_M1911_Stow.AM_M1911_Stow"), Problems);
			// 手枪轻：抬得快，开镜快，跑得比步枪快。这些数是我先定的，手测以后调
			Weapon->RaiseTime = 0.12f;
			Weapon->LowerTime = 0.15f;
			Weapon->SprintTime = 0.15f;
			Weapon->AdsTime = 0.2f;
			Weapon->AdsZoom = 1.35f;
			Weapon->HipHandlingOmega = 30.f;
			Weapon->HipHandlingZeta = 0.75f;
			Weapon->HeldSpeedScale = 0.95f;
			Weapon->AdsSpeedScale = 0.65f;
			// 片段里套筒一个来回 5 帧（0.167 s）
			Weapon->FireRate = 360.f;
			// 枪踢：短枪管大口径，枪口翻得比步枪多，绕手腕转
			Weapon->GunKick.Back = 1.6f;
			Weapon->GunKick.Climb = 9.f;
			Weapon->GunKick.Yaw = 1.5f;
			Weapon->GunKick.Roll = 3.5f;
			Weapon->GunKick.Pivot = FVector(-2.0, 0.0, -6.0);
			Weapon->GunKick.RotationOmega = 34.f;
			Weapon->GunKick.BackOmega = 60.f;
			// 相机：约步枪的四成
			Weapon->CameraRecoil.Reset();
			FGinkgoCameraRecoil Kick;
			Kick.Name = TEXT("Kick");
			Kick.Pitch = FFloatInterval(0.7f, 1.f);
			Kick.Omega = FFloatInterval(20.f, 20.f);
			Kick.Zeta = FFloatInterval(0.75f, 0.75f);
			Kick.bSeparateReturn = true;
			Kick.ReturnOmega = FFloatInterval(14.f, 14.f);
			Weapon->CameraRecoil.Add(Kick);
			FGinkgoCameraRecoil Shake;
			Shake.Name = TEXT("Shake");
			Shake.Pitch = FFloatInterval(0.15f, 0.25f);
			Shake.Yaw = FFloatInterval(0.1f, 0.2f);
			Shake.Roll = FFloatInterval(0.25f, 0.45f);
			Shake.bRandomSide = true;
			Shake.FieldOfView = FFloatInterval(0.3f, 0.5f);
			Shake.Omega = FFloatInterval(60.f, 60.f);
			Shake.Zeta = FFloatInterval(0.35f, 0.35f);
			Weapon->CameraRecoil.Add(Shake);
			FillM1911Cues(*Weapon);
			Weapon->Rounds.LiveRound = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/M1911/Rounds/SM_M1911_Round.SM_M1911_Round")));
			Weapon->Rounds.SpentCase = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/M1911/Rounds/SM_M1911_Case.SM_M1911_Case")));
			Weapon->Rounds.InHand = FTransform::Identity;
			Weapon->Rounds.FeedTop = FVector::ZeroVector;
			Weapon->Rounds.ChamberRise = 0.f;
			// 套筒退到后面时壳从抛壳窗出来，往右上略向后。片段里第 3 帧退到底（两帧 66 ms）
			Weapon->Rounds.EjectOffset = FVector(4.0, 0.8, 6.9);
			Weapon->Rounds.EjectDirection = FVector(-0.25, 0.7, 0.66);
			Weapon->Rounds.EjectDelay = 0.05f;
			Weapon->Rounds.EjectSpeed = FFloatInterval(220.f, 320.f);
			Weapon->Rounds.EjectSpread = 12.f;
			Weapon->Rounds.EjectSpin = FFloatInterval(1800.f, 3000.f);
			// 硝烟同 98k 长得一样，体积小一些（你说的）。火药约十分之一，气体的体积也约十分之一，边长约一半
			Weapon->MuzzleSmokeScale = 0.5f;
			UGinkgoMagazineMechanism* Mechanism = NewObject<UGinkgoMagazineMechanism>(Weapon, NAME_None, RF_Transactional);
			Mechanism->Rules.Feed = EGinkgoFeed::DetachableMagazine;
			Mechanism->Rules.Capacity = 7;
			Mechanism->Rules.bLoadNeedsOpenAction = false;
			Mechanism->Rules.bSelfLoading = true;
			Mechanism->Rules.bLockOpenOnEmpty = true;
			Weapon->Mechanism = Mechanism;
			Weapon->AmmoType = FWeaponTags::Ammo_ACP45_Standard;
			Weapon->StartingReserve = 28;
			FAssetRegistryModule::AssetCreated(Weapon);
			SavePackageOf(*Weapon, Problems);
		}
		if (Problems.IsEmpty())
		{
			EnsureWeaponAnchors(*Weapon, Problems);
			// 枪口：EnsureWeaponAnchors 取最前端一圈顶点，这把枪的套筒前端也在那一圈里，中心偏了。用导出时量的（SK_M1911.markers.json）
			const FVector Muzzle(19.09, 0.0, 6.80);
			if (!Weapon->MuzzleOffset.Equals(Muzzle, 1e-3))
			{
				Weapon->MuzzleOffset = Muzzle;
				SavePackageOf(*Weapon, Problems);
			}
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}

	/** 首次生成配件定义并挂到 AK 上。已有资产的文案、声音、挂点以编辑器里保存的为准 */
	void EnsureAK47Attachment(UGinkgoWeaponDefinition& Weapon, TArray<FString>& Problems)
	{
		const FString PackageName = TEXT("/Game/Weapons/AK47/DA_AK47Suppressor");
		UGinkgoWeaponAttachment* Part = nullptr;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			Part = LoadAsset<UGinkgoWeaponAttachment>(TEXT("/Game/Weapons/AK47/DA_AK47Suppressor.DA_AK47Suppressor"), Problems);
		}
		else
		{
			UPackage* Package = CreatePackage(*PackageName);
			Part = NewObject<UGinkgoWeaponAttachment>(Package, TEXT("DA_AK47Suppressor"), RF_Public | RF_Standalone | RF_Transactional);
			Part->DisplayName = NSLOCTEXT("Ginkgo", "AK47SuppressorName", "消音器");
			Part->Description = NSLOCTEXT("Ginkgo", "AK47SuppressorDescription", "安装在枪口的消音装置，改变射击时的声音。");
			FGinkgoAttachmentEffectDescription& RecoilEffect = Part->EffectDescriptions.AddDefaulted_GetRef();
			RecoilEffect.Text = NSLOCTEXT("Ginkgo", "AK47SuppressorRecoilEffect", "+后坐力控制");
			RecoilEffect.Tone = EGinkgoAttachmentEffectTone::Positive;
			Part->HudIcon = TEXT("AK47Suppressor");
			Part->InstallWristTurn = 48.f;
			Part->Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/AK47/SM_AK47_Suppressor.SM_AK47_Suppressor")));
			Part->FireSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Weapons/AK47/Audio/MS_AK47_FireSuppressed.MS_AK47_FireSuppressed")));
			Part->PatternScaleOffset = -0.2f;
			FAssetRegistryModule::AssetCreated(Part);
			if (!SavePackageOf(*Part, Problems))
			{
				return;
			}
		}
		if (Part && !Weapon.Attachments.ContainsByPredicate([Part](const FGinkgoWeaponAttachmentMount& Entry) { return Entry.Part == Part; }))
		{
			FGinkgoWeaponAttachmentMount& Entry = Weapon.Attachments.AddDefaulted_GetRef();
			Entry.Part = Part;
			Entry.Mount = TEXT("muzzle");
			Entry.RelativeTransform.SetLocation(Weapon.MuzzleOffset);
			SavePackageOf(Weapon, Problems);
		}
	}

	/**
	 * -AddAK47：生成 AK-47 的数据资产（Private/anim/AK47/README.txt）。层蓝图不用改：M1911 加的低持、腰射循环它也用。
	 * 已经有就只补量的东西，你手调的数不动。声音、后坐模板在别处：声音借别的枪的，等你找到 AK 的录音再换；模板是 Tools/RecoilEditor 导出的 RP_AK47，有了才填
	 */
	int32 AddAK47()
	{
		TArray<FString> Problems;
		const UAnimBlueprint* Rifle = LoadBlueprint(RifleLayersPackage);
		if (!Rifle)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 %s（先 -AddWeaponLayers）"), RifleLayersPackage);
			return 1;
		}
		const FString Package = TEXT("/Game/Weapons/AK47/DA_AK47");
		const FString ObjectPath = Package + TEXT(".DA_AK47");
		UGinkgoWeaponDefinition* Weapon = FPackageName::DoesPackageExist(Package) ? LoadObject<UGinkgoWeaponDefinition>(nullptr, *ObjectPath) : nullptr;
		if (!Weapon)
		{
			UPackage* WeaponPackage = CreatePackage(*Package);
			WeaponPackage->FullyLoad();
			Weapon = NewObject<UGinkgoWeaponDefinition>(WeaponPackage, TEXT("DA_AK47"), RF_Public | RF_Standalone | RF_Transactional);
			Weapon->DisplayName = NSLOCTEXT("Ginkgo", "AK47Name", "AK-47 7.62mm");
			Weapon->CaliberName = NSLOCTEXT("Ginkgo", "AK47Caliber", "7.62 × 39mm");
			Weapon->HudIcon = TEXT("AK47");
			Weapon->AdsReticle = NAME_None;
			Weapon->SlotKind = EGinkgoWeaponSlotKind::Primary;
			Weapon->Mesh = LoadAsset<UStaticMesh>(TEXT("/Game/Weapons/AK47/SM_AK47.SM_AK47"), Problems);
			Weapon->SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Weapons/AK47/SK_AK47.SK_AK47")));
			Weapon->Bolt.CarrierBone = TEXT("bolt_carrier");
			Weapon->Bolt.HandleBone = NAME_None;
			Weapon->Bolt.Stroke = 9.4f;
			Weapon->Bolt.LiftAngle = 0.f;
			Weapon->GunRestClosed = LoadAsset<UAnimSequence>(TEXT("/Game/Weapons/AK47/Anims/A_AK47_Fire.A_AK47_Fire"), Problems);
			Weapon->MagazineBone = TEXT("mag");
			Weapon->AnimLayerClass = Rifle->GeneratedClass;
			Weapon->LoweredPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/AK47Poses/M_AK47_Idle_Pose.M_AK47_Idle_Pose"), Problems);
			Weapon->HipPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/AK47Poses/M_AK47_HipAim_Pose.M_AK47_HipAim_Pose"), Problems);
			Weapon->AimPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/AK47Poses/M_AK47_Aim_Pose.M_AK47_Aim_Pose"), Problems);
			Weapon->AimOffset = LoadAsset<UBlendSpace>(TEXT("/Game/Developers/JiahaoLi/Mauser98kAimOffset/AO_Mauser98k_ADS.AO_Mauser98k_ADS"), Problems);
			Weapon->RunLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/AK47Loops/M_AK47_Run_Loop_F.M_AK47_Run_Loop_F"), Problems);
			Weapon->SprintLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/AK47Loops/M_AK47_Sprint_Loop_F.M_AK47_Sprint_Loop_F"), Problems);
			Weapon->AimLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/AK47Loops/M_AK47_Aim_Loop.M_AK47_Aim_Loop"), Problems);
			Weapon->LoweredLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/AK47Loops/M_AK47_Idle_Loop.M_AK47_Idle_Loop"), Problems);
			Weapon->HipLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/AK47Loops/M_AK47_HipAim_Loop.M_AK47_HipAim_Loop"), Problems);
			Weapon->bSprintTwoHanded = false;
			Weapon->bRunTwoHanded = true;
			Weapon->DrawMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/AK47/AM_AK47_Draw.AM_AK47_Draw"), Problems);
			Weapon->StowMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/AK47/AM_AK47_Stow.AM_AK47_Stow"), Problems);
			// 这些数是我先定的，手测以后调。比 98k 轻一点，比手枪重
			Weapon->RaiseTime = 0.16f;
			Weapon->LowerTime = 0.18f;
			Weapon->SprintTime = 0.18f;
			Weapon->AdsTime = 0.25f;
			Weapon->AdsZoom = 1.5f;
			Weapon->HipHandlingOmega = 36.f;
			Weapon->HipHandlingZeta = 0.8f;
			Weapon->HeldSpeedScale = 0.87f;
			Weapon->AdsSpeedScale = 0.5f;
			// 600 发/分：片段里枪机 3 帧一个来回，CS2 的 AK、真枪都是这个数
			Weapon->FireRate = 600.f;
			Weapon->FireMode = EGinkgoFireMode::Automatic;
			Weapon->PatternRecoil.Pattern = LoadObject<UGinkgoRecoilPattern>(nullptr, TEXT("/Game/Weapons/AK47/RP_AK47.RP_AK47"));
			// 你手测调的（2026-11-05）
			Weapon->PatternRecoil.YawScale = 1.5f;
			Weapon->PatternRecoil.PitchScale = 1.5f;
			Weapon->PatternRecoil.CrouchScale = 0.7f;
			Weapon->PatternRecoil.RecoveryDecay = 2.f;
			Weapon->PatternRecoil.RecoveryLinear = 4.f;
			// 枪踢：每枪的手感，弹道在模板里。一秒十发，每枪小一点，弹簧叠起来
			Weapon->GunKick.Back = 1.2f;
			Weapon->GunKick.Climb = 2.5f;
			Weapon->GunKick.Yaw = 0.8f;
			Weapon->GunKick.Roll = 1.2f;
			Weapon->GunKick.RotationOmega = 34.f;
			Weapon->GunKick.BackOmega = 60.f;
			// 相机：持续的上抬归模板，踢动层只留一点；抖动同手枪
			Weapon->CameraRecoil.Reset();
			FGinkgoCameraRecoil Kick;
			Kick.Name = TEXT("Kick");
			Kick.Pitch = FFloatInterval(0.1f, 0.2f);
			Kick.Yaw = FFloatInterval(0.05f, 0.12f);
			Kick.Omega = FFloatInterval(30.f, 30.f);
			Kick.Zeta = FFloatInterval(0.75f, 0.75f);
			Kick.bSeparateReturn = true;
			Kick.ReturnOmega = FFloatInterval(16.f, 16.f);
			Weapon->CameraRecoil.Add(Kick);
			FGinkgoCameraRecoil Shake;
			Shake.Name = TEXT("Shake");
			Shake.Pitch = FFloatInterval(0.1f, 0.18f);
			Shake.Yaw = FFloatInterval(0.08f, 0.15f);
			Shake.Roll = FFloatInterval(0.2f, 0.35f);
			Shake.bRandomSide = true;
			Shake.FieldOfView = FFloatInterval(0.15f, 0.3f);
			Shake.Omega = FFloatInterval(60.f, 60.f);
			Shake.Zeta = FFloatInterval(0.35f, 0.35f);
			Weapon->CameraRecoil.Add(Shake);
			FillAK47Cues(*Weapon);
			Weapon->Rounds.LiveRound = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/AK47/Rounds/SM_AK47_Round.SM_AK47_Round")));
			Weapon->Rounds.SpentCase = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/AK47/Rounds/SM_AK47_Case.SM_AK47_Case")));
			Weapon->Rounds.InHand = FTransform::Identity;
			Weapon->Rounds.FeedTop = FVector::ZeroVector;
			Weapon->Rounds.ChamberRise = 0.f;
			// 抛壳窗在枪的右边，枪机后面；往右上略向后。片段里枪机第 2、3 帧在后面
			Weapon->Rounds.EjectOffset = FVector(17.0, 2.0, 11.0);
			Weapon->Rounds.EjectDirection = FVector(-0.2, 0.75, 0.63);
			Weapon->Rounds.EjectDelay = 0.03f;
			Weapon->Rounds.EjectSpeed = FFloatInterval(240.f, 340.f);
			Weapon->Rounds.EjectSpread = 10.f;
			Weapon->Rounds.EjectSpin = FFloatInterval(1800.f, 3000.f);
			// 散布：腰射 3°，开镜 0.15°。一秒十发，一枪 0.3° 减掉回复，打一串到上限
			Weapon->Spread.HipDefault = 3.f;
			Weapon->Spread.AdsDefault = 0.15f;
			Weapon->Spread.GrowthPerShot = 0.3f;
			Weapon->Spread.RecoveryRate = 2.5f;
			Weapon->Spread.HipMax = 7.f;
			Weapon->Spread.AdsMax = 1.2f;
			Weapon->Spread.AirFactor = 2.f;
			Weapon->MuzzleSmokeScale = 0.7f;
			Weapon->MuzzleSmokeInterval = 0.35f;
			UGinkgoMagazineMechanism* Mechanism = NewObject<UGinkgoMagazineMechanism>(Weapon, NAME_None, RF_Transactional);
			Mechanism->Rules.Feed = EGinkgoFeed::DetachableMagazine;
			Mechanism->Rules.Capacity = 30;
			Mechanism->Rules.bLoadNeedsOpenAction = false;
			Mechanism->Rules.bSelfLoading = true;
			Mechanism->Rules.bLockOpenOnEmpty = false;
			Weapon->Mechanism = Mechanism;
			Weapon->AmmoType = FWeaponTags::Ammo_M43762x39_Standard;
			Weapon->StartingReserve = 90;
			FAssetRegistryModule::AssetCreated(Weapon);
			SavePackageOf(*Weapon, Problems);
		}
		if (Problems.IsEmpty())
		{
			EnsureWeaponAnchors(*Weapon, Problems);
			// 枪口用导出时量的（SK_AK47.markers.json）：最前端那圈顶点里有准星座
			const FVector Muzzle(63.21, 0.0, 9.25);
			if (!Weapon->MuzzleOffset.Equals(Muzzle, 1e-3))
			{
				Weapon->MuzzleOffset = Muzzle;
				SavePackageOf(*Weapon, Problems);
			}
			EnsureAK47Attachment(*Weapon, Problems);
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}

	/**
	 * 霰弹枪的声音（你给的 audio_selected/shotgun，split_shotgun_audio.py 切的，/Game/Weapons/Shotgun/Audio）：护木往后到底（Bolt_Travel 到 1）、往前到底（落回 0）、
	 * 一发过卡榫（Weapon_RoundIn，一次两发时两下）。提前量是切好以后的撞击点（parts.json）；音量按到输出的响度同别的枪比：枪声比 M1911 的大一点，泵动比枪声低约 10 dB
	 */
	void FillShotgunCues(UGinkgoWeaponDefinition& Weapon)
	{
		const auto AddCue = [&Weapon](const FGameplayTag& Tag, const TCHAR* Group, const float Volume, const float Lead)
		{
			FGinkgoWeaponCue Cue;
			Cue.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(FString::Printf(TEXT("/Game/Weapons/Shotgun/Audio/MS_Shotgun_%s.MS_Shotgun_%s"), Group, Group)));
			Cue.Volume = Volume;
			Cue.Lead = Lead;
			Weapon.Cues.Add(Tag, Cue);
		};
		Weapon.Cues.Reset();
		AddCue(FWeaponTags::Weapon_Cue_Fire, TEXT("Fire"), 0.75f, 0.f);
		AddCue(FWeaponTags::Weapon_Cue_BoltPull, TEXT("PumpBack"), 1.f, 0.0238f);
		AddCue(FWeaponTags::Weapon_Cue_BoltPush, TEXT("PumpForward"), 1.f, 0.0167f);
		AddCue(FWeaponTags::Weapon_Cue_RoundIn, TEXT("ShellIn"), 0.7f, 0.022f);
	}

	/**
	 * -AddShotgun：生成泵动霰弹枪的数据资产（Private/anim/PumpShotgun/README.txt，Docs/Design/ActionStates.md「霰弹枪」）。层蓝图不用改，同 AK。
	 * 已经有就只补量的东西，你手调的数不动。后面的步骤：GinkgoWeaponStateTree -Weapon=/Game/Weapons/Shotgun/DA_Shotgun 填子树与片段
	 */
	int32 AddShotgun()
	{
		TArray<FString> Problems;
		const UAnimBlueprint* Rifle = LoadBlueprint(RifleLayersPackage);
		if (!Rifle)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 %s（先 -AddWeaponLayers）"), RifleLayersPackage);
			return 1;
		}
		const FString Package = TEXT("/Game/Weapons/Shotgun/DA_Shotgun");
		const FString ObjectPath = Package + TEXT(".DA_Shotgun");
		UGinkgoWeaponDefinition* Weapon = FPackageName::DoesPackageExist(Package) ? LoadObject<UGinkgoWeaponDefinition>(nullptr, *ObjectPath) : nullptr;
		if (!Weapon)
		{
			UPackage* WeaponPackage = CreatePackage(*Package);
			WeaponPackage->FullyLoad();
			Weapon = NewObject<UGinkgoWeaponDefinition>(WeaponPackage, TEXT("DA_Shotgun"), RF_Public | RF_Standalone | RF_Transactional);
			Weapon->DisplayName = NSLOCTEXT("Ginkgo", "ShotgunName", "M870 12 Gauge");
			Weapon->CaliberName = NSLOCTEXT("Ginkgo", "ShotgunCaliber", "12 Gauge 00 Buck");
			Weapon->HudIcon = TEXT("Shotgun");
			// 开镜是 "( · )"（你说的）：两段弧的中点在圆锥上，页面里弧的半径 12
			Weapon->AdsReticle = TEXT("Shotgun");
			Weapon->AdsReticleGap = 12.f;
			Weapon->SlotKind = EGinkgoWeaponSlotKind::Primary;
			Weapon->Mesh = LoadAsset<UStaticMesh>(TEXT("/Game/Weapons/Shotgun/SM_Shotgun.SM_Shotgun"), Problems);
			Weapon->SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Weapons/Shotgun/SK_Shotgun.SK_Shotgun")));
			// 护木与枪机一起走 7.4 cm（枪自己的片段摆它们，这里只给回放记、给左手跟着护木）
			Weapon->Bolt.CarrierBone = TEXT("bolt");
			Weapon->Bolt.HandleBone = TEXT("pump");
			Weapon->Bolt.Stroke = 7.4f;
			Weapon->Bolt.LiftAngle = 0.f;
			// A_Shotgun_Fire 的第一帧扳机已经扣了：静止的样子用收枪的第一帧
			Weapon->GunRestClosed = LoadAsset<UAnimSequence>(TEXT("/Game/Weapons/Shotgun/Anims/A_Shotgun_Stow.A_Shotgun_Stow"), Problems);
			Weapon->NextRoundBone = TEXT("shell_feed");
			Weapon->SpentCaseBone = TEXT("shell_hull");
			Weapon->bLeftHandOnForend = true;
			Weapon->AnimLayerClass = Rifle->GeneratedClass;
			Weapon->LoweredPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/ShotgunPoses/M_Shotgun_Idle_Pose.M_Shotgun_Idle_Pose"), Problems);
			Weapon->HipPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/ShotgunPoses/M_Shotgun_HipAim_Pose.M_Shotgun_HipAim_Pose"), Problems);
			Weapon->AimPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/ShotgunPoses/M_Shotgun_Aim_Pose.M_Shotgun_Aim_Pose"), Problems);
			Weapon->AimOffset = LoadAsset<UBlendSpace>(TEXT("/Game/Developers/JiahaoLi/Mauser98kAimOffset/AO_Mauser98k_ADS.AO_Mauser98k_ADS"), Problems);
			Weapon->RunLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/ShotgunLoops/M_Shotgun_Run_Loop_F.M_Shotgun_Run_Loop_F"), Problems);
			Weapon->SprintLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/ShotgunLoops/M_Shotgun_Sprint_Loop_F.M_Shotgun_Sprint_Loop_F"), Problems);
			Weapon->AimLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/ShotgunLoops/M_Shotgun_Aim_Loop.M_Shotgun_Aim_Loop"), Problems);
			Weapon->LoweredLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/ShotgunLoops/M_Shotgun_Idle_Loop.M_Shotgun_Idle_Loop"), Problems);
			Weapon->HipLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/ShotgunLoops/M_Shotgun_HipAim_Loop.M_Shotgun_HipAim_Loop"), Problems);
			Weapon->bSprintTwoHanded = false;
			Weapon->bRunTwoHanded = true;
			Weapon->DrawMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/Shotgun/AM_Shotgun_Draw.AM_Shotgun_Draw"), Problems);
			Weapon->StowMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/Shotgun/AM_Shotgun_Stow.AM_Shotgun_Stow"), Problems);
			// 这些数是我先定的，手测以后调。同 98k 差不多重，开镜放大少一点（霰弹枪不靠瞄得细）
			Weapon->RaiseTime = 0.2f;
			Weapon->LowerTime = 0.2f;
			Weapon->SprintTime = 0.2f;
			Weapon->AdsTime = 0.28f;
			Weapon->AdsZoom = 1.3f;
			Weapon->HipHandlingOmega = 30.f;
			Weapon->HipHandlingZeta = 0.8f;
			Weapon->HeldSpeedScale = 0.87f;
			Weapon->AdsSpeedScale = 0.5f;
			// 开一枪 0.25 s 后接泵动（后坐的片段放到一半，98k 是 0.2）
			Weapon->ShotRecoveryTime = 0.25f;
			// 一发 00 鹿弹九颗弹丸（12 号 2 3/4 英寸的标准装药）
			Weapon->PelletCount = 9;
			// 枪踢：12 号的后坐比步枪大一截，枪口翻得多、回得慢
			Weapon->GunKick.Back = 3.f;
			Weapon->GunKick.Climb = 8.f;
			Weapon->GunKick.Yaw = 1.5f;
			Weapon->GunKick.Roll = 2.5f;
			Weapon->GunKick.RotationOmega = 26.f;
			Weapon->GunKick.BackOmega = 45.f;
			Weapon->CameraRecoil.Reset();
			FGinkgoCameraRecoil Kick;
			Kick.Name = TEXT("Kick");
			Kick.Pitch = FFloatInterval(1.6f, 2.2f);
			Kick.Yaw = FFloatInterval(0.1f, 0.3f);
			Kick.Omega = FFloatInterval(16.f, 16.f);
			Kick.Zeta = FFloatInterval(0.75f, 0.75f);
			Kick.bSeparateReturn = true;
			Kick.ReturnOmega = FFloatInterval(11.f, 11.f);
			Weapon->CameraRecoil.Add(Kick);
			FGinkgoCameraRecoil Shake;
			Shake.Name = TEXT("Shake");
			Shake.Pitch = FFloatInterval(0.3f, 0.5f);
			Shake.Yaw = FFloatInterval(0.2f, 0.35f);
			Shake.Roll = FFloatInterval(0.5f, 0.8f);
			Shake.bRandomSide = true;
			Shake.FieldOfView = FFloatInterval(0.6f, 1.f);
			Shake.Omega = FFloatInterval(50.f, 50.f);
			Shake.Zeta = FFloatInterval(0.35f, 0.35f);
			Weapon->CameraRecoil.Add(Shake);
			FillShotgunCues(*Weapon);
			Weapon->Rounds.LiveRound = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/Shotgun/Rounds/SM_Shotgun_Shell.SM_Shotgun_Shell")));
			Weapon->Rounds.SpentCase = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/Shotgun/Rounds/SM_Shotgun_Hull.SM_Shotgun_Hull")));
			Weapon->Rounds.InHand = FTransform::Identity;
			Weapon->Rounds.FeedTop = FVector::ZeroVector;
			Weapon->Rounds.ChamberRise = 0.f;
			// 抛壳口在枪的右边（README：(23, 1.6, 6.1) cm），壳往右上略向后；泵动往后拉时（ActionOpen）抛，没有延迟
			Weapon->Rounds.EjectOffset = FVector(23.0, 1.6, 6.1);
			Weapon->Rounds.EjectDirection = FVector(-0.3, 0.8, 0.5);
			Weapon->Rounds.EjectDelay = 0.f;
			Weapon->Rounds.EjectSpeed = FFloatInterval(160.f, 240.f);
			Weapon->Rounds.EjectSpread = 10.f;
			Weapon->Rounds.EjectSpin = FFloatInterval(600.f, 1200.f);
			// 散布就是弹丸散开的圆锥：腰射 4.5°、开镜 2.5°，连着打长一点（打得慢，长得不多）
			Weapon->Spread.HipDefault = 4.5f;
			Weapon->Spread.AdsDefault = 2.5f;
			Weapon->Spread.GrowthPerShot = 1.f;
			Weapon->Spread.RecoveryRate = 3.f;
			Weapon->Spread.HipMax = 6.5f;
			Weapon->Spread.AdsMax = 3.5f;
			Weapon->Spread.AirFactor = 1.5f;
			// 火大、烟多（README：霰弹枪的火大一些）
			Weapon->MuzzleSmokeScale = 1.3f;
			UGinkgoMagazineMechanism* Mechanism = NewObject<UGinkgoMagazineMechanism>(Weapon, NAME_None, RF_Transactional);
			Mechanism->Rules.Feed = EGinkgoFeed::TubeMagazine;
			Mechanism->Rules.Capacity = 4;
			Mechanism->Rules.bLoadNeedsOpenAction = false;
			Mechanism->Rules.bSelfLoading = false;
			Mechanism->Rules.bLockOpenOnEmpty = false;
			Weapon->Mechanism = Mechanism;
			Weapon->AmmoType = FWeaponTags::Ammo_Gauge12_Buckshot;
			Weapon->StartingReserve = 24;
			FAssetRegistryModule::AssetCreated(Weapon);
			SavePackageOf(*Weapon, Problems);
		}
		if (Problems.IsEmpty())
		{
			EnsureWeaponAnchors(*Weapon, Problems);
			// 枪口用导出时量的（SK_Shotgun.markers.json）
			const FVector Muzzle(76.4, 0.0, 6.2);
			if (!Weapon->MuzzleOffset.Equals(Muzzle, 1e-3))
			{
				Weapon->MuzzleOffset = Muzzle;
				SavePackageOf(*Weapon, Problems);
			}
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}

	/**
	 * MG3 的枪管的热（Docs/Design/Weapons.md「枪管的热」，我定的）：粗枪管，一发热得少（装药 / 枪管质量约 AK 的 0.6 倍，2 °C），凉得慢（25 s）。
	 * 一秒二十发打一秒看得见，打三秒满；第一次生成以后加的，-MG3Heat 只改这两项
	 */
	void FillMG3Heat(UGinkgoWeaponDefinition& Weapon)
	{
		Weapon.HeatPerShot = 2.f;
		Weapon.HeatCoolTime = 25.f;
	}

	/** MG3 的声音：只有枪声（你给的 audio_selected/mg3，split_mg3_audio.py 切的，/Game/Weapons/MG3/Audio）。一秒二十发，下一发来时上一发 0.15 s 淡掉 */
	void FillMG3Cues(UGinkgoWeaponDefinition& Weapon)
	{
		FGinkgoWeaponCue Cue;
		Cue.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Weapons/MG3/Audio/MS_MG3_Fire.MS_MG3_Fire")));
		Cue.Volume = 0.55f;
		Cue.Lead = 0.f;
		Cue.FadePrevious = 0.15f;
		Weapon.Cues.Reset();
		Weapon.Cues.Add(FWeaponTags::Weapon_Cue_Fire, Cue);
	}

	/**
	 * -AddMG3：生成 MG3 的数据资产（Private/anim/MG3/README.txt，Docs/Design/ActionStates.md「MG3」）。层蓝图不用改，同 AK。
	 * 你定的：不能装填，只有一个弹箱（250 发），收不起来（重武器的槽：换枪、收枪就扔在地上，捡别的枪直接替换它），所以没有收枪的片段、只有枪声。
	 * 已经有了不动（你可能手改过），要重来先删
	 */
	int32 AddMG3()
	{
		TArray<FString> Problems;
		const UAnimBlueprint* Rifle = LoadBlueprint(RifleLayersPackage);
		if (!Rifle)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 %s（先 -AddWeaponLayers）"), RifleLayersPackage);
			return 1;
		}
		const FString Package = TEXT("/Game/Weapons/MG3/DA_MG3");
		const FString ObjectPath = Package + TEXT(".DA_MG3");
		UGinkgoWeaponDefinition* Weapon = FPackageName::DoesPackageExist(Package) ? LoadObject<UGinkgoWeaponDefinition>(nullptr, *ObjectPath) : nullptr;
		if (!Weapon)
		{
			UPackage* WeaponPackage = CreatePackage(*Package);
			WeaponPackage->FullyLoad();
			Weapon = NewObject<UGinkgoWeaponDefinition>(WeaponPackage, TEXT("DA_MG3"), RF_Public | RF_Standalone | RF_Transactional);
			Weapon->DisplayName = NSLOCTEXT("Ginkgo", "MG3Name", "MG3 7.62mm");
			Weapon->CaliberName = NSLOCTEXT("Ginkgo", "MG3Caliber", "7.62 × 51mm NATO");
			Weapon->HudIcon = TEXT("MG3");
			// 机械瞄具（README：游戏里的 MG3 不装配件），开镜同 AK 是一个点
			Weapon->AdsReticle = NAME_None;
			// 收不起来：只能拿在手上（你说的）
			Weapon->SlotKind = EGinkgoWeaponSlotKind::Heavy;
			Weapon->Mesh = LoadAsset<UStaticMesh>(TEXT("/Game/Weapons/MG3/SM_MG3.SM_MG3"), Problems);
			Weapon->SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Weapons/MG3/SK_MG3.SK_MG3")));
			// 不拉机柄：枪机不动，样子跟着枪自己的片段
			Weapon->Bolt.CarrierBone = TEXT("bolt");
			Weapon->Bolt.HandleBone = NAME_None;
			Weapon->Bolt.Stroke = 0.f;
			Weapon->Bolt.LiftAngle = 0.f;
			// A_MG3_Fire 的第一帧扳机已经扣了：静止的样子用收枪的第一帧（README）
			Weapon->GunRestClosed = LoadAsset<UAnimSequence>(TEXT("/Game/Weapons/MG3/Anims/A_MG3_Stow.A_MG3_Stow"), Problems);
			// 露在进弹口和弹箱之间的七发弹链：剩下不够七发时从弹箱那头往前缩
			Weapon->VisibleFeedBones = {TEXT("belt_1"), TEXT("belt_2"), TEXT("belt_3"), TEXT("belt_4"), TEXT("belt_5"), TEXT("belt_6"), TEXT("belt_7")};
			Weapon->AnimLayerClass = Rifle->GeneratedClass;
			Weapon->LoweredPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/MG3Poses/M_MG3_Idle_Pose.M_MG3_Idle_Pose"), Problems);
			Weapon->HipPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/MG3Poses/M_MG3_HipAim_Pose.M_MG3_HipAim_Pose"), Problems);
			Weapon->AimPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/MG3Poses/M_MG3_Aim_Pose.M_MG3_Aim_Pose"), Problems);
			Weapon->AimOffset = LoadAsset<UBlendSpace>(TEXT("/Game/Developers/JiahaoLi/Mauser98kAimOffset/AO_Mauser98k_ADS.AO_Mauser98k_ADS"), Problems);
			Weapon->RunLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/MG3Loops/M_MG3_Run_Loop_F.M_MG3_Run_Loop_F"), Problems);
			Weapon->SprintLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/MG3Loops/M_MG3_Sprint_Loop_F.M_MG3_Sprint_Loop_F"), Problems);
			Weapon->AimLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/MG3Loops/M_MG3_Aim_Loop.M_MG3_Aim_Loop"), Problems);
			Weapon->LoweredLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/MG3Loops/M_MG3_Idle_Loop.M_MG3_Idle_Loop"), Problems);
			Weapon->HipLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/MG3Loops/M_MG3_HipAim_Loop.M_MG3_HipAim_Loop"), Problems);
			// 11.5 kg 不单手拿：冲刺两手端着斜挎在胸前，跑也两只手（README）
			Weapon->bSprintTwoHanded = true;
			Weapon->bRunTwoHanded = true;
			// 捡起来以后接拔枪的后一段（枪已经在手上）；收不起来，没有收枪
			Weapon->DrawMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/MG3/AM_MG3_Draw.AM_MG3_Draw"), Problems);
			Weapon->StowMontage = nullptr;
			// 这些数是我先定的，手测以后调。11.5 kg，抬枪、开镜、转身都慢一截，走得慢
			Weapon->RaiseTime = 0.3f;
			Weapon->LowerTime = 0.25f;
			Weapon->SprintTime = 0.3f;
			Weapon->AdsTime = 0.4f;
			Weapon->AdsZoom = 1.3f;
			Weapon->HipHandlingOmega = 22.f;
			Weapon->HipHandlingZeta = 0.85f;
			Weapon->HeldSpeedScale = 0.75f;
			Weapon->AdsSpeedScale = 0.4f;
			// 标准枪机每分钟约 1200 发（README；重枪机约 1000），片段按这个做的
			Weapon->FireRate = 1200.f;
			Weapon->FireMode = EGinkgoFireMode::Automatic;
			// 后坐模板：CS2 的 M249 的参数按 0.05 s 一发重算（Private/recoil/cs2_pattern.py，我定的，你没选），先不放大
			Weapon->PatternRecoil.Pattern = LoadObject<UGinkgoRecoilPattern>(nullptr, TEXT("/Game/Weapons/MG3/RP_MG3.RP_MG3"));
			if (!Weapon->PatternRecoil.Pattern)
			{
				Problems.Add(TEXT("读不到 /Game/Weapons/MG3/RP_MG3（先用 Scripts/Assets/import_recoil.py 导入 Tools/RecoilEditor/patterns/MG3.recoil）"));
			}
			Weapon->PatternRecoil.YawScale = 1.f;
			Weapon->PatternRecoil.PitchScale = 1.f;
			Weapon->PatternRecoil.CrouchScale = 0.7f;
			Weapon->PatternRecoil.RecoveryDecay = 2.f;
			Weapon->PatternRecoil.RecoveryLinear = 4.f;
			// 一发的增量要在下一发之前走完：0.05 s 一发，30 fps 时两发之间有时只隔一帧（33 ms）。默认的 0.06 s 时开枪那一刻视线差模板 0.05°，0.04 s 时 30 fps 还差 0.056°
			Weapon->PatternRecoil.ApplyTime = 0.03f;
			// 枪踢：一秒二十发，每枪比 AK 小，弹簧叠起来
			Weapon->GunKick.Back = 0.8f;
			Weapon->GunKick.Climb = 1.6f;
			Weapon->GunKick.Yaw = 0.6f;
			Weapon->GunKick.Roll = 0.8f;
			Weapon->GunKick.RotationOmega = 30.f;
			Weapon->GunKick.BackOmega = 55.f;
			Weapon->CameraRecoil.Reset();
			FGinkgoCameraRecoil Kick;
			Kick.Name = TEXT("Kick");
			Kick.Pitch = FFloatInterval(0.08f, 0.15f);
			Kick.Yaw = FFloatInterval(0.04f, 0.1f);
			Kick.Omega = FFloatInterval(30.f, 30.f);
			Kick.Zeta = FFloatInterval(0.75f, 0.75f);
			Kick.bSeparateReturn = true;
			Kick.ReturnOmega = FFloatInterval(16.f, 16.f);
			Weapon->CameraRecoil.Add(Kick);
			FGinkgoCameraRecoil Shake;
			Shake.Name = TEXT("Shake");
			Shake.Pitch = FFloatInterval(0.08f, 0.14f);
			Shake.Yaw = FFloatInterval(0.06f, 0.12f);
			Shake.Roll = FFloatInterval(0.15f, 0.3f);
			Shake.bRandomSide = true;
			Shake.FieldOfView = FFloatInterval(0.12f, 0.25f);
			Shake.Omega = FFloatInterval(60.f, 60.f);
			Shake.Zeta = FFloatInterval(0.35f, 0.35f);
			Weapon->CameraRecoil.Add(Shake);
			FillMG3Cues(*Weapon);
			// 没有手里、弹仓里的那一发（不装填）：只有打过的壳
			Weapon->Rounds.LiveRound = nullptr;
			Weapon->Rounds.SpentCase = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/MG3/Rounds/SM_MG3_Case.SM_MG3_Case")));
			Weapon->Rounds.InHand = FTransform::Identity;
			Weapon->Rounds.FeedTop = FVector::ZeroVector;
			Weapon->Rounds.ChamberRise = 0.f;
			// 抛壳口在机匣下面（README：(10.7, 0, 4.3) cm），壳往下掉，略向右后
			Weapon->Rounds.EjectOffset = FVector(10.7, 0.0, 4.3);
			Weapon->Rounds.EjectDirection = FVector(-0.15, 0.2, -1.0);
			Weapon->Rounds.EjectDelay = 0.02f;
			Weapon->Rounds.EjectSpeed = FFloatInterval(60.f, 120.f);
			Weapon->Rounds.EjectSpread = 12.f;
			Weapon->Rounds.EjectSpin = FFloatInterval(600.f, 1200.f);
			// 站着端机枪：腰射散得开，开镜也稳不到步枪那样，连射长得快。一秒二十发，两发之间回 4 × 0.05 = 0.2°：每发长的要比它大才长得起来（净长 0.1°/发）
			Weapon->Spread.HipDefault = 4.f;
			Weapon->Spread.AdsDefault = 0.6f;
			Weapon->Spread.GrowthPerShot = 0.3f;
			Weapon->Spread.RecoveryRate = 4.f;
			Weapon->Spread.HipMax = 8.f;
			Weapon->Spread.AdsMax = 3.f;
			Weapon->Spread.AirFactor = 2.f;
			Weapon->MuzzleSmokeScale = 1.f;
			Weapon->MuzzleSmokeInterval = 0.35f;
			FillMG3Heat(*Weapon);
			UGinkgoMagazineMechanism* Mechanism = NewObject<UGinkgoMagazineMechanism>(Weapon, NAME_None, RF_Transactional);
			Mechanism->Rules.Feed = EGinkgoFeed::Belt;
			Mechanism->Rules.Capacity = 250;
			Mechanism->Rules.bLoadNeedsOpenAction = false;
			Mechanism->Rules.bSelfLoading = true;
			Mechanism->Rules.bLockOpenOnEmpty = false;
			Mechanism->Rules.bOpenBolt = true;
			Weapon->Mechanism = Mechanism;
			Weapon->AmmoType = FWeaponTags::Ammo_NATO762x51_Ball;
			Weapon->StartingReserve = 0;
			if (Problems.IsEmpty())
			{
				FAssetRegistryModule::AssetCreated(Weapon);
				SavePackageOf(*Weapon, Problems);
			}
		}
		if (Problems.IsEmpty())
		{
			EnsureWeaponAnchors(*Weapon, Problems);
			// 枪口用导出时量的（SK_MG3.markers.json）
			const FVector Muzzle(87.06, 0.0, 9.6);
			if (!Weapon->MuzzleOffset.Equals(Muzzle, 1e-3))
			{
				Weapon->MuzzleOffset = Muzzle;
				SavePackageOf(*Weapon, Problems);
			}
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}

	/**
	 * M79 榴弹发射器的操作与发射音效：默认开火（高爆弹、碰炸闪光弹）使用专用轻膛压发射声 MS_M79_Fire，
	 * 16 钢镖霰弹通过自身 DA_Ammo_Grenade40x46Flechette::FireSound 覆盖为 MS_Shotgun_Fire；折开/合膛/退弹/入膛映射对应机械提示音
	 */
	void FillM79Cues(UGinkgoWeaponDefinition& Weapon)
	{
		const auto AddCue = [&Weapon](const FGameplayTag& Tag, const TCHAR* SoundPath, const float Volume, const float Lead)
		{
			FGinkgoWeaponCue Cue;
			Cue.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(SoundPath));
			Cue.Volume = Volume;
			Cue.Lead = Lead;
			Weapon.Cues.Add(Tag, Cue);
		};
		Weapon.Cues.Reset();
		AddCue(FWeaponTags::Weapon_Cue_Fire, TEXT("/Game/Weapons/M79/Audio/MS_M79_Fire.MS_M79_Fire"), 1.f, 0.f);
		AddCue(FWeaponTags::Weapon_Cue_BoltPull, TEXT("/Game/Weapons/Shotgun/Audio/MS_Shotgun_PumpBack.MS_Shotgun_PumpBack"), 0.9f, 0.02f);
		AddCue(FWeaponTags::Weapon_Cue_BoltPush, TEXT("/Game/Weapons/Shotgun/Audio/MS_Shotgun_PumpForward.MS_Shotgun_PumpForward"), 1.f, 0.017f);
		AddCue(FWeaponTags::Weapon_Cue_MagOut, TEXT("/Game/Weapons/AK47/Audio/MS_AK47_MagOut.MS_AK47_MagOut"), 0.65f, 0.015f);
		AddCue(FWeaponTags::Weapon_Cue_RoundIn, TEXT("/Game/Weapons/Shotgun/Audio/MS_Shotgun_ShellIn.MS_Shotgun_ShellIn"), 0.85f, 0.02f);
	}

	/**
	 * -AddM79：生成 M79 40mm 中折单发榴弹发射器的数据资产（Private/anim/M79/README.txt）。
	 * 复用 ABP_GinkgoWeaponLayers_Rifle，配置开闭膛两份静止序列（A_M79_Rest_Closed / A_M79_Rest_Open）以及膛内整弹/空壳骨骼，
	 * 后续由 GinkgoWeaponStateTree -Weapon=/Game/Weapons/M79/DA_M79 挂载 ST_ActionBreak 子树与动作片段
	 */
	int32 AddM79()
	{
		TArray<FString> Problems;
		const UAnimBlueprint* Rifle = LoadBlueprint(RifleLayersPackage);
		if (!Rifle)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 %s（先 -AddWeaponLayers）"), RifleLayersPackage);
			return 1;
		}
		const FString Package = TEXT("/Game/Weapons/M79/DA_M79");
		const FString ObjectPath = Package + TEXT(".DA_M79");
		UGinkgoWeaponDefinition* Weapon = FPackageName::DoesPackageExist(Package) ? LoadObject<UGinkgoWeaponDefinition>(nullptr, *ObjectPath) : nullptr;
		if (!Weapon)
		{
			UPackage* WeaponPackage = CreatePackage(*Package);
			WeaponPackage->FullyLoad();
			Weapon = NewObject<UGinkgoWeaponDefinition>(WeaponPackage, TEXT("DA_M79"), RF_Public | RF_Standalone | RF_Transactional);
			Weapon->DisplayName = NSLOCTEXT("Ginkgo", "M79Name", "M79 40mm");
			Weapon->CaliberName = NSLOCTEXT("Ginkgo", "M79Caliber", "40 × 46mm M406 HE");
			Weapon->HudIcon = TEXT("M79");
			Weapon->AdsReticle = TEXT("M79");
			Weapon->AdsReticleGap = 10.f;
			Weapon->SlotKind = EGinkgoWeaponSlotKind::Primary;
			Weapon->Mesh = LoadAsset<UStaticMesh>(TEXT("/Game/Weapons/M79/SM_M79.SM_M79"), Problems);
			Weapon->SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Weapons/M79/SK_M79.SK_M79")));
			Weapon->Bolt.CarrierBone = TEXT("barrel");
			Weapon->Bolt.HandleBone = TEXT("latch");
			Weapon->Bolt.Stroke = 0.f;
			Weapon->Bolt.LiftAngle = 0.f;
			Weapon->GunRestClosed = LoadAsset<UAnimSequence>(TEXT("/Game/Weapons/M79/Anims/A_M79_Rest_Closed.A_M79_Rest_Closed"), Problems);
			Weapon->GunRestOpen = LoadAsset<UAnimSequence>(TEXT("/Game/Weapons/M79/Anims/A_M79_Rest_Open.A_M79_Rest_Open"), Problems);
			Weapon->NextRoundBone = TEXT("round_chamber");
			Weapon->SpentCaseBone = TEXT("case_chamber");
			Weapon->RoundMaterialSlot = 1;
			Weapon->AnimLayerClass = Rifle->GeneratedClass;
			Weapon->LoweredPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/M79Poses/M_M79_Idle_Pose.M_M79_Idle_Pose"), Problems);
			Weapon->HipPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/M79Poses/M_M79_HipAim_Pose.M_M79_HipAim_Pose"), Problems);
			Weapon->AimPose = LoadAsset<UAnimSequenceBase>(TEXT("/Game/Developers/JiahaoLi/M79Poses/M_M79_Aim_Pose.M_M79_Aim_Pose"), Problems);
			Weapon->AimOffset = LoadAsset<UBlendSpace>(TEXT("/Game/Developers/JiahaoLi/Mauser98kAimOffset/AO_Mauser98k_ADS.AO_Mauser98k_ADS"), Problems);
			Weapon->RunLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M79Loops/M_M79_Run_Loop_F.M_M79_Run_Loop_F"), Problems);
			Weapon->SprintLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M79Loops/M_M79_Sprint_Loop_F.M_M79_Sprint_Loop_F"), Problems);
			Weapon->AimLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M79Loops/M_M79_Aim_Loop.M_M79_Aim_Loop"), Problems);
			Weapon->LoweredLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M79Loops/M_M79_Idle_Loop.M_M79_Idle_Loop"), Problems);
			Weapon->HipLoop = LoadAsset<UAnimSequence>(TEXT("/Game/Developers/JiahaoLi/M79Loops/M_M79_HipAim_Loop.M_M79_HipAim_Loop"), Problems);
			Weapon->bSprintTwoHanded = false;
			Weapon->bRunTwoHanded = true;
			Weapon->DrawMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/M79/AM_M79_Draw.AM_M79_Draw"), Problems);
			Weapon->StowMontage = LoadAsset<UAnimMontage>(TEXT("/Game/Weapons/M79/AM_M79_Stow.AM_M79_Stow"), Problems);
			// M79 空重 2.7 kg，短粗轻便，举枪与开镜略快于步枪，单发低速榴弹后坐厚重柔和
			Weapon->RaiseTime = 0.18f;
			Weapon->LowerTime = 0.18f;
			Weapon->SprintTime = 0.18f;
			Weapon->AdsTime = 0.26f;
			Weapon->AdsZoom = 1.35f;
			Weapon->HipHandlingOmega = 32.f;
			Weapon->HipHandlingZeta = 0.82f;
			Weapon->HeldSpeedScale = 0.89f;
			Weapon->AdsSpeedScale = 0.52f;
			Weapon->ShotRecoveryTime = 0.35f;
			Weapon->PelletCount = 1;
			Weapon->GunKick.Back = 2.6f;
			Weapon->GunKick.Climb = 6.5f;
			Weapon->GunKick.Yaw = 1.1f;
			Weapon->GunKick.Roll = 1.8f;
			Weapon->GunKick.RotationOmega = 24.f;
			Weapon->GunKick.BackOmega = 42.f;
			Weapon->CameraRecoil.Reset();
			FGinkgoCameraRecoil Kick;
			Kick.Name = TEXT("Kick");
			Kick.Pitch = FFloatInterval(1.4f, 1.9f);
			Kick.Yaw = FFloatInterval(0.1f, 0.25f);
			Kick.Omega = FFloatInterval(15.f, 15.f);
			Kick.Zeta = FFloatInterval(0.78f, 0.78f);
			Kick.bSeparateReturn = true;
			Kick.ReturnOmega = FFloatInterval(10.f, 10.f);
			Weapon->CameraRecoil.Add(Kick);
			FGinkgoCameraRecoil Shake;
			Shake.Name = TEXT("Shake");
			Shake.Pitch = FFloatInterval(0.25f, 0.42f);
			Shake.Yaw = FFloatInterval(0.15f, 0.3f);
			Shake.Roll = FFloatInterval(0.4f, 0.65f);
			Shake.bRandomSide = true;
			Shake.FieldOfView = FFloatInterval(0.5f, 0.85f);
			Shake.Omega = FFloatInterval(46.f, 46.f);
			Shake.Zeta = FFloatInterval(0.38f, 0.38f);
			Weapon->CameraRecoil.Add(Shake);
			FillM79Cues(*Weapon);
			Weapon->Rounds.LiveRound = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Round.SM_M79_Round")));
			Weapon->Rounds.SpentCase = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Weapons/M79/Rounds/SM_M79_Case.SM_M79_Case")));
			Weapon->Rounds.InHand = FTransform::Identity;
			Weapon->Rounds.FeedTop = FVector::ZeroVector;
			Weapon->Rounds.ChamberRise = 0.f;
			// 折开 38° 后的敞开膛口位于机匣后上方，退出的空铝壳或重力滑出的整弹向后下方坠落
			Weapon->Rounds.EjectOffset = FVector(8.5, 2.5, 9.5);
			Weapon->Rounds.EjectDirection = FVector(-0.75, 0.15, -0.64);
			Weapon->Rounds.EjectDelay = 0.f;
			Weapon->Rounds.EjectSpeed = FFloatInterval(85.f, 135.f);
			Weapon->Rounds.EjectSpread = 8.f;
			Weapon->Rounds.EjectSpin = FFloatInterval(240.f, 520.f);
			Weapon->Spread.HipDefault = 2.2f;
			Weapon->Spread.AdsDefault = 0.25f;
			Weapon->Spread.GrowthPerShot = 1.2f;
			Weapon->Spread.RecoveryRate = 2.5f;
			Weapon->Spread.HipMax = 4.5f;
			Weapon->Spread.AdsMax = 1.f;
			Weapon->Spread.AirFactor = 1.8f;
			Weapon->MuzzleSmokeScale = 1.45f;
			UGinkgoMagazineMechanism* Mechanism = NewObject<UGinkgoMagazineMechanism>(Weapon, NAME_None, RF_Transactional);
			Mechanism->Rules.Feed = EGinkgoFeed::SingleShot;
			Mechanism->Rules.Capacity = 0;
			Mechanism->Rules.bLoadNeedsOpenAction = true;
			Mechanism->Rules.bSelfLoading = false;
			Mechanism->Rules.bLockOpenOnEmpty = false;
			Mechanism->Rules.bCanLoadChamber = true;
			Weapon->Mechanism = Mechanism;
			Weapon->AmmoType = FWeaponTags::Ammo_Grenade40x46_HE;
			Weapon->StartingReserve = 12;
			if (Problems.IsEmpty())
			{
				FAssetRegistryModule::AssetCreated(Weapon);
				SavePackageOf(*Weapon, Problems);
			}
		}
		if (Problems.IsEmpty())
		{
			EnsureWeaponAnchors(*Weapon, Problems);
			// 炮口坐标取自导出实测（SK_M79.markers.json），弹药材质槽 1 随所装弹头动态切换涂装
			const FVector Muzzle(45.91, 0.0, 2.78);
			if (!Weapon->MuzzleOffset.Equals(Muzzle, 1e-3) || Weapon->RoundMaterialSlot != 1)
			{
				Weapon->MuzzleOffset = Muzzle;
				Weapon->RoundMaterialSlot = 1;
				SavePackageOf(*Weapon, Problems);
			}
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}

	/**
	 * -AddHoldLoops：持枪循环（Docs/Design/Weapons.md「持枪循环」）。步枪的层 ABP_GinkgoWeaponLayers_Rifle 的 UpperBodyPose 里加上跑、冲刺两个循环
	 * （AddHoldLoopNodes）与开镜的循环（AddAimLoopNodes），98k 的数据资产填上三个循环（空的才填）、量好冲刺的握法（EnsureWeaponAnchors）。
	 * 两个资产都先备份到 Saved/Ginkgo/Backup；层里已经有的循环不加。主蓝图（你那份）与 Control Rig 都不动：左手放开枪在 Control Rig 的瞄准节点里，读动画实例给的数据。
	 * 加 -LoopLayersTo=、-LoopWeaponTo= 时先把两个资产复制到那两处、只改复制的（数据资产的层换成复制的那个）：量的时候用，
	 * 回放时用 -ini 把项目设置的 StartingWeapon 指过去，原来的两份不动（参数名不含 "Package="：FParse::Value 按子串找）
	 */
	int32 AddHoldLoops(const FString& LayersCopy, const FString& WeaponCopy)
	{
		TArray<FString> Problems;
		const bool bCopy = !LayersCopy.IsEmpty() || !WeaponCopy.IsEmpty();
		if (bCopy && (LayersCopy.IsEmpty() || WeaponCopy.IsEmpty() || FPackageName::DoesPackageExist(WeaponCopy)))
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] -LoopLayersTo= 与 -LoopWeaponTo= 要一起给，复制的目标还不能有"));
			return 1;
		}
		const FString LayersPackage = bCopy ? LayersCopy : FString(RifleLayersPackage);
		const FString WeaponPackage = bCopy ? WeaponCopy : FString(Mauser98kPackage);
		UGinkgoWeaponDefinition* Source = LoadObject<UGinkgoWeaponDefinition>(nullptr,
			*FString::Printf(TEXT("%s.%s"), Mauser98kPackage, *FPackageName::GetShortName(Mauser98kPackage)));
		UAnimBlueprint* Rifle = bCopy ? DuplicateBlueprint(RifleLayersPackage, LayersCopy) : LoadBlueprint(RifleLayersPackage);
		if (!Source || !Rifle)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 %s 或 %s（先 -AddWeaponLayers），或复制不了"), Mauser98kPackage, RifleLayersPackage);
			return 1;
		}
		// 两个循环要有脚的同步标记（L、R）才跟得上侧向、后退的步伐：没有时它们只按帧长自己走。导入时照它们做在上面的 GASP 循环加
		//（同名同时刻）；缺的先补，什么也不改
		UAnimSequence* RunLoop = Source->RunLoop ? Source->RunLoop.Get() : LoadAsset<UAnimSequence>(Mauser98kRunLoopPath, Problems);
		UAnimSequence* SprintLoop = Source->SprintLoop ? Source->SprintLoop.Get() : LoadAsset<UAnimSequence>(Mauser98kSprintLoopPath, Problems);
		UAnimSequence* AimLoop = Source->AimLoop ? Source->AimLoop.Get() : LoadAsset<UAnimSequence>(Mauser98kAimLoopPath, Problems);
		for (const UAnimSequence* Loop : { RunLoop, SprintLoop })
		{
			const bool bHasFeet = Loop && Loop->AuthoredSyncMarkers.ContainsByPredicate([](const FAnimSyncMarker& Marker)
			{
				return Marker.MarkerName == TEXT("L") || Marker.MarkerName == TEXT("R");
			});
			if (Loop && !bHasFeet)
			{
				Problems.Add(FString::Printf(TEXT("%s 没有脚的同步标记（L、R）：跟不上侧向、后退的步伐，先照它做在上面的 GASP 循环补上"), *Loop->GetPathName()));
			}
		}
		if (!Problems.IsEmpty())
		{
			for (const FString& Problem : Problems)
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
			}
			return 1;
		}

		// 层：低持、开镜的求值器（片段接 LoweredPose、AimPose），它们接着的抬枪混合的 A、开镜混合的 B，分层混合
		UEdGraph* Upper = FindImplementedLayer(*Rifle, UpperBodyLayer);
		UAnimGraphNode_SequenceEvaluator* Lowered = nullptr;
		UAnimGraphNode_SequenceEvaluator* Aim = nullptr;
		UAnimGraphNode_LayeredBoneBlend* Layered = nullptr;
		bool bHasLoops = false;
		bool bHasAimLoop = false;
		for (UEdGraphNode* Node : Upper ? Upper->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
		{
			if (UAnimGraphNode_SequenceEvaluator* Evaluator = Cast<UAnimGraphNode_SequenceEvaluator>(Node))
			{
				const FName Member = BoundMember(*Evaluator, TEXT("Sequence"));
				Lowered = Member == TEXT("LoweredPose") ? Evaluator : Lowered;
				Aim = Member == TEXT("AimPose") ? Evaluator : Aim;
				bHasLoops = bHasLoops || Member == TEXT("RunLoop") || Member == TEXT("SprintLoop");
				bHasAimLoop = bHasAimLoop || Member == TEXT("AimLoop");
			}
			Layered = Layered ? Layered : Cast<UAnimGraphNode_LayeredBoneBlend>(Node);
		}
		int32 Result = 0;
		if (bHasLoops && bHasAimLoop)
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已经有三个持枪循环了，不加"), *LayersPackage);
		}
		else
		{
			const auto BlendInput = [](const UAnimGraphNode_SequenceEvaluator* Pose) -> UEdGraphPin*
			{
				UEdGraphPin* Out = Pose ? FindPosePin(*Pose, EGPD_Output) : nullptr;
				UEdGraphPin* Input = Out && Out->LinkedTo.Num() == 1 ? Out->LinkedTo[0] : nullptr;
				return Input && Input->GetOwningNode()->IsA<UAnimGraphNode_TwoWayBlend>() ? Input : nullptr;
			};
			UEdGraphPin* RaiseA = BlendInput(Lowered);
			UEdGraphPin* AdsB = BlendInput(Aim);
			if ((!bHasLoops && (!RaiseA || !Layered)) || (!bHasAimLoop && !AdsB))
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 的 UpperBodyPose 里找不到低持、开镜的姿势，它们接着的抬枪、开镜混合，或分层混合，不改"), *LayersPackage);
				return 1;
			}
			const FString RifleFile = FPackageName::LongPackageNameToFilename(LayersPackage, FPackageName::GetAssetPackageExtension());
			const FString RifleBackup = bCopy ? FString(TEXT("（新复制的，没有）")) : BackupBlueprint(FPackageName::GetShortName(LayersPackage), RifleFile);
			if (RifleBackup.IsEmpty())
			{
				UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *RifleFile);
				return 1;
			}
			if (!bHasLoops)
			{
				AddHoldLoopNodes(*Upper, *FindPosePin(*Lowered, EGPD_Output), *RaiseA, *Layered, Lowered->NodePosX, Lowered->NodePosY + 700, Problems);
			}
			if (!bHasAimLoop)
			{
				AddAimLoopNodes(*Upper, *FindPosePin(*Aim, EGPD_Output), *AdsB, Aim->NodePosX, Aim->NodePosY + 1000, Problems);
			}
			Result = CompileAndSaveEdited(*Rifle, RifleFile, RifleBackup, Problems,
				bHasLoops ? TEXT("加上了开镜的循环") : bHasAimLoop ? TEXT("加上了跑、冲刺两个持枪循环") : TEXT("加上了三个持枪循环"));
			if (Result != 0)
			{
				return Result;
			}
		}

		// 数据资产：复制的换成复制的层；原来的先备份。空的循环填上，再量握法（量的有变化时 EnsureWeaponAnchors 存回去，复制的另存一次）
		UGinkgoWeaponDefinition* Weapon = Source;
		FString WeaponBackup = TEXT("（新复制的，没有）");
		if (bCopy)
		{
			ObjectTools::FPackageGroupName Target;
			Target.PackageName = WeaponCopy;
			Target.ObjectName = FPackageName::GetShortName(WeaponCopy);
			TSet<UPackage*> RefusedPackages;
			Weapon = Cast<UGinkgoWeaponDefinition>(ObjectTools::DuplicateSingleObject(Source, Target, RefusedPackages, /*bPromptToOverwrite*/ false));
			if (Weapon)
			{
				Weapon->AnimLayerClass = Rifle->GeneratedClass;
			}
		}
		else
		{
			WeaponBackup = BackupBlueprint(FPackageName::GetShortName(WeaponPackage), FPackageName::LongPackageNameToFilename(WeaponPackage, FPackageName::GetAssetPackageExtension()));
		}
		if (!Weapon || WeaponBackup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 复制不了或备份不了 %s，不改"), *WeaponPackage);
			return 1;
		}
		if (!Weapon->RunLoop || !Weapon->SprintLoop || !Weapon->AimLoop)
		{
			Weapon->RunLoop = RunLoop;
			Weapon->SprintLoop = SprintLoop;
			Weapon->AimLoop = AimLoop;
			if (SavePackageOf(*Weapon, Problems))
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 填上了持枪循环（原文件备份在 %s）"), *WeaponPackage, *WeaponBackup);
			}
		}
		EnsureWeaponAnchors(*Weapon, Problems);
		if (bCopy && Problems.IsEmpty())
		{
			SavePackageOf(*Weapon, Problems);
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? Result : 1;
	}

	/**
	 * @brief Control Rig 里接上瞄准与双手：层级从网格重新导入（补上虚拟骨骼）、加 HandIK_L/R 两条曲线；
	 *        腿部 IK → 瞄准（FRigUnit_GinkgoWeaponAim）→ 全身 IK（Full Body IK 插件的 FRigUnit_PBIK：根 spine_05 按输入钉住，
	 *        两个效应器 hand_l、hand_r 接瞄准节点给的目标与权重）
	 * @return 改了没有（已经有瞄准节点的不改）
	 */
	bool BuildWeaponAim(UControlRigRuntimeAsset& Rig, TArray<FString>& Problems)
	{
		UControlRigEditorAsset* Editor = Cast<UControlRigEditorAsset>(Rig.GetEditorOnlyData());
		URigHierarchyController* Hierarchy = Editor ? Cast<IControlRigEditorAssetInterface>(Editor)->GetHierarchyController() : nullptr;
		USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
		UScriptStruct* PBIKStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/PBIK.RigUnit_PBIK"));
		if (!Editor || !Hierarchy || !Mesh || !PBIKStruct)
		{
			Problems.Add(FString::Printf(TEXT("%s：读不出 Control Rig、网格或全身 IK 的节点（要开 Full Body IK 插件）"), *Rig.GetPathName()));
			return false;
		}
		URigVMController* Controller = Editor->GetOrCreateController(Editor->GetDefaultModel());
		URigVMNode* LegIK = nullptr;
		for (URigVMNode* Node : Controller->GetGraph()->GetNodes())
		{
			const URigVMUnitNode* Unit = Cast<URigVMUnitNode>(Node);
			if (Unit && Unit->GetScriptStruct() == FRigUnit_GinkgoWeaponAim::StaticStruct())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已经有瞄准节点了，不改"), *Rig.GetPathName());
				return false;
			}
			LegIK = Unit && Unit->GetScriptStruct() == FRigUnit_GinkgoLegIK::StaticStruct() ? Node : LegIK;
		}
		if (!LegIK)
		{
			Problems.Add(FString::Printf(TEXT("%s 里没有腿部 IK 的节点（先 -AddControlRig）"), *Rig.GetPathName()));
			return false;
		}

		// 层级：Control Rig 是加虚拟骨骼之前生成的，重新导入补上（已有的不动）；瞄准节点读两条曲线
		Hierarchy->ImportBones(Mesh->GetSkeleton(), NAME_None, /*bReplaceExistingBones*/ false, /*bRemoveObsoleteBones*/ false);
		const FRigUnit_GinkgoWeaponAim AimDefaults;
		const FRigElementKey GunKey(AimDefaults.GunBone, ERigElementType::Bone);
		if (!Hierarchy->GetHierarchy()->Contains(GunKey))
		{
			Problems.Add(FString::Printf(TEXT("%s 导入以后还是没有 %s（导入命令 -VirtualBones 加了没有）"), *Rig.GetPathName(), *AimDefaults.GunBone.ToString()));
			return false;
		}
		// 虚拟骨骼从骨架导入时不带父骨骼，挂到网格里它的父骨骼（源 spine_05）下：不然动画蓝图编译时报层级不一致，枪也不跟脊柱转
		const FReferenceSkeleton& MeshSkeleton = Mesh->GetRefSkeleton();
		const int32 GunIndex = MeshSkeleton.FindBoneIndex(AimDefaults.GunBone);
		const int32 GunParent = GunIndex == INDEX_NONE ? INDEX_NONE : MeshSkeleton.GetParentIndex(GunIndex);
		if (GunParent != INDEX_NONE)
		{
			const FRigElementKey ParentKey(MeshSkeleton.GetBoneName(GunParent), ERigElementType::Bone);
			if (Hierarchy->GetHierarchy()->GetFirstParent(GunKey) != ParentKey)
			{
				Hierarchy->SetParent(GunKey, ParentKey, /*bMaintainGlobalTransform*/ true, /*bSetupUndo*/ false);
			}
		}
		for (const FName Curve : { AimDefaults.LeftHandCurve, AimDefaults.RightHandCurve })
		{
			if (!Hierarchy->GetHierarchy()->Contains(FRigElementKey(Curve, ERigElementType::Curve)))
			{
				Hierarchy->AddCurve(Curve, 0.f, /*bSetupUndo*/ false);
			}
		}
		Editor->PropagateHierarchyFromBPToInstances();

		const FVector2D At = LegIK->GetPosition();
		URigVMUnitNode* Aim = Controller->AddUnitNode(FRigUnit_GinkgoWeaponAim::StaticStruct(), FRigUnit::GetMethodName(), At + FVector2D(450.0, 0.0), FString(), false);
		URigVMUnitNode* PBIK = Controller->AddUnitNode(PBIKStruct, FRigUnit::GetMethodName(), At + FVector2D(900.0, 0.0), FString(), false);
		bool bLinked = Aim && PBIK
			&& Controller->AddLink(PinPath(LegIK, TEXT("ExecutePin")), PinPath(Aim, TEXT("ExecutePin")), false)
			&& Controller->AddLink(PinPath(Aim, TEXT("ExecutePin")), PinPath(PBIK, TEXT("ExecutePin")), false);
		if (bLinked)
		{
			const FString Effectors = PinPath(PBIK, TEXT("Effectors"));
			Controller->SetPinDefaultValue(PinPath(PBIK, TEXT("Root")), TEXT("spine_05"), true, false);
			Controller->SetPinDefaultValue(PinPath(PBIK, TEXT("Settings.RootBehavior")), TEXT("PinToInput"), true, false);
			bLinked = Controller->SetArrayPinSize(Effectors, 2, FString(), false);
			for (int32 Index = 0; bLinked && Index < 2; ++Index)
			{
				const FString Effector = FString::Printf(TEXT("%s.%d"), *Effectors, Index);
				const TCHAR* Side = Index == 0 ? TEXT("Left") : TEXT("Right");
				Controller->SetPinDefaultValue(Effector + TEXT(".Bone"), Index == 0 ? TEXT("hand_l") : TEXT("hand_r"), true, false);
				bLinked = Controller->AddLink(PinPath(Aim, *FString::Printf(TEXT("%sHandTarget"), Side)), Effector + TEXT(".Transform"), false)
					&& Controller->AddLink(PinPath(Aim, *FString::Printf(TEXT("%sHandAlpha"), Side)), Effector + TEXT(".PositionAlpha"), false)
					&& Controller->AddLink(PinPath(Aim, *FString::Printf(TEXT("%sHandAlpha"), Side)), Effector + TEXT(".RotationAlpha"), false);
			}
		}
		if (!bLinked)
		{
			Problems.Add(FString::Printf(TEXT("%s 里连不上 腿部 IK → 瞄准 → 全身 IK"), *Rig.GetPathName()));
			return false;
		}
		Controller->AddCommentNode(
			TEXT("瞄准（FRigUnit_GinkgoWeaponAim）：枪口对准瞄准点，余差分给 spine_03–05 与枪；按抬枪进度混。再给双手的目标与权重。")
			TEXT("全身 IK：根 spine_05 按输入钉住，只解锁骨与胳膊，枪不被带走。数据从动画实例读。由命令 GinkgoAnimBlueprint -AddWeaponAim 加，")
			TEXT("见 Docs/Design/Weapons.md 的\"手与瞄准\""),
			At + FVector2D(400.0, -150.0), FVector2D(1000.0, 550.0), FLinearColor::White, FString(), false);
		Editor->RecompileVM();
		return true;
	}

	/**
	 * -AddWeaponAim：角色的 Control Rig（-ControlRig=，默认那份）里接上第 2 批的瞄准与双手，先量好 98k 的数据资产里枪上的点。
	 * 加 -FromControlRig= 时先从那份复制到 -ControlRig= 再改（量的时候用副本）。原文件先备份到 Saved/Ginkgo/Backup；已经有瞄准节点的不改
	 */
	int32 AddWeaponAim(const FString& ControlRigPackage, const FString& FromControlRig)
	{
		TArray<FString> Problems;
		if (UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr,
			*FString::Printf(TEXT("%s.%s"), Mauser98kPackage, *FPackageName::GetShortName(Mauser98kPackage))))
		{
			EnsureWeaponAnchors(*Weapon, Problems);
		}
		else
		{
			Problems.Add(FString::Printf(TEXT("找不到 %s（先 -AddWeaponLayers）"), Mauser98kPackage));
		}

		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *ControlRigPackage, *FPackageName::GetShortName(ControlRigPackage));
		const FString Filename = FPackageName::LongPackageNameToFilename(ControlRigPackage, FPackageName::GetAssetPackageExtension());
		FString Backup = TEXT("（新复制的，没有）");
		UControlRigRuntimeAsset* Rig = nullptr;
		if (!FromControlRig.IsEmpty())
		{
			UObject* Source = LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *FromControlRig, *FPackageName::GetShortName(FromControlRig)));
			if (Source && !FPackageName::DoesPackageExist(ControlRigPackage))
			{
				ObjectTools::FPackageGroupName Target;
				Target.PackageName = ControlRigPackage;
				Target.ObjectName = FPackageName::GetShortName(ControlRigPackage);
				TSet<UPackage*> RefusedPackages;
				Rig = Cast<UControlRigRuntimeAsset>(ObjectTools::DuplicateSingleObject(Source, Target, RefusedPackages, /*bPromptToOverwrite*/ false));
			}
		}
		else
		{
			Rig = LoadObject<UControlRigRuntimeAsset>(nullptr, *ObjectPath);
			Backup = Rig ? BackupBlueprint(FPackageName::GetShortName(ControlRigPackage), Filename) : FString();
		}
		if (!Rig || Backup.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("读不到、复制不了或备份不了 %s"), *ControlRigPackage));
		}
		const bool bChanged = Rig && !Backup.IsEmpty() && Problems.IsEmpty() && BuildWeaponAim(*Rig, Problems);
		if (bChanged && Problems.IsEmpty())
		{
			SavePackageOf(*Rig, Problems);
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		if (!Problems.IsEmpty())
		{
			return 1;
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s %s，原文件备份在 %s"), *ControlRigPackage, bChanged ? TEXT("接上了瞄准与双手") : TEXT("没改"), *Backup);
		return 0;
	}

	/** 节点上所有引脚的名字（连不上时写进问题里，看引脚叫什么） */
	FString DescribePins(const URigVMNode* Node)
	{
		return Node ? FString::JoinBy(Node->GetPins(), TEXT(", "), [](const URigVMPin* Pin) { return Pin->GetName(); }) : FString(TEXT("（没有节点）"));
	}

	/**
	 * @brief Control Rig 里接上伸手时上半身参与（拾取第 2 批，Docs/Design/Interaction.md「拾取子弹盒」）：
	 *        腿部 IK → 伸手（FRigUnit_GinkgoHandReach）→ 分支（有手在伸）→ 第一遍全身 IK → 瞄准 → 原来的全身 IK。
	 *        第一遍全身 IK：根钉在骨盆，效应器只有伸手的那只手（骨骼、目标、权重接伸手节点）；脊柱、锁骨的转动刚度，胳膊伸长时的平移刚度与允不允许伸长
	 *        也接伸手节点（它的输入存在 Control Rig 里，调试时控制台变量覆盖）
	 * @return 改了没有（已经有伸手节点的不改）
	 */
	bool BuildReachLean(UControlRigRuntimeAsset& Rig, TArray<FString>& Problems)
	{
		UControlRigEditorAsset* Editor = Cast<UControlRigEditorAsset>(Rig.GetEditorOnlyData());
		UScriptStruct* PBIKStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/PBIK.RigUnit_PBIK"));
		if (!Editor || !PBIKStruct)
		{
			Problems.Add(FString::Printf(TEXT("%s：读不出 Control Rig 或全身 IK 的节点（要开 Full Body IK 插件）"), *Rig.GetPathName()));
			return false;
		}
		URigVMController* Controller = Editor->GetOrCreateController(Editor->GetDefaultModel());
		URigVMNode* LegIK = nullptr;
		URigVMNode* Aim = nullptr;
		for (URigVMNode* Node : Controller->GetGraph()->GetNodes())
		{
			const URigVMUnitNode* Unit = Cast<URigVMUnitNode>(Node);
			const UScriptStruct* Struct = Unit ? Unit->GetScriptStruct() : nullptr;
			if (Struct == FRigUnit_GinkgoHandReach::StaticStruct())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s 已经有伸手节点了，不改"), *Rig.GetPathName());
				return false;
			}
			LegIK = Struct == FRigUnit_GinkgoLegIK::StaticStruct() ? Node : LegIK;
			Aim = Struct == FRigUnit_GinkgoWeaponAim::StaticStruct() ? Node : Aim;
		}
		if (!LegIK || !Aim)
		{
			Problems.Add(FString::Printf(TEXT("%s 里没有腿部 IK 或瞄准的节点（先 -AddControlRig、-AddWeaponAim）"), *Rig.GetPathName()));
			return false;
		}

		const FVector2D At = LegIK->GetPosition() + FVector2D(0.0, 420.0);
		URigVMUnitNode* Reach = Controller->AddUnitNode(FRigUnit_GinkgoHandReach::StaticStruct(), FRigUnit::GetMethodName(), At, FString(), false);
		URigVMUnitNode* Branch = Controller->AddUnitNode(FRigVMFunction_ControlFlowBranch::StaticStruct(), FRigUnit::GetMethodName(), At + FVector2D(380.0, 0.0), FString(), false);
		URigVMUnitNode* Lean = Controller->AddUnitNode(PBIKStruct, FRigUnit::GetMethodName(), At + FVector2D(680.0, 0.0), FString(), false);
		if (!Reach || !Branch || !Lean)
		{
			Problems.Add(FString::Printf(TEXT("%s 里加不上伸手、分支或全身 IK 的节点"), *Rig.GetPathName()));
			return false;
		}
		const auto Link = [&](const URigVMNode* From, const TCHAR* FromPin, const URigVMNode* To, const FString& ToPin)
		{
			const FString Source = PinPath(From, FromPin);
			const FString Target = To->GetName() + TEXT(".") + ToPin;
			if (Source.IsEmpty() || !Controller->AddLink(Source, Target, false))
			{
				Problems.Add(FString::Printf(TEXT("连不上 %s.%s → %s（%s 的引脚：%s；%s 的引脚：%s）"), *From->GetName(), FromPin, *Target, *From->GetName(), *DescribePins(From),
					*To->GetName(), *DescribePins(To)));
			}
		};
		const auto SetDefault = [&](const URigVMNode* Node, const FString& Pin, const FString& Value)
		{
			if (!Controller->SetPinDefaultValue(Node->GetName() + TEXT(".") + Pin, Value, true, false))
			{
				Problems.Add(FString::Printf(TEXT("设不上 %s.%s = %s"), *Node->GetName(), *Pin, *Value));
			}
		};

		// 执行顺序：腿部 IK → 伸手 → 分支（真：第一遍全身 IK；之后：瞄准）
		Controller->BreakLink(PinPath(LegIK, TEXT("ExecutePin")), PinPath(Aim, TEXT("ExecutePin")), false);
		Link(LegIK, TEXT("ExecutePin"), Reach, TEXT("ExecutePin"));
		Link(Reach, TEXT("ExecutePin"), Branch, TEXT("ExecutePin"));
		Link(Reach, TEXT("bActive"), Branch, TEXT("Condition"));
		Link(Branch, TEXT("True"), Lean, TEXT("ExecutePin"));
		Link(Branch, TEXT("Completed"), Aim, TEXT("ExecutePin"));

		// 第一遍全身 IK：根钉在骨盆；一个效应器，伸手的那只手
		SetDefault(Lean, TEXT("Root"), TEXT("pelvis"));
		SetDefault(Lean, TEXT("Settings.RootBehavior"), TEXT("PinToInput"));
		Link(Reach, TEXT("bAllowStretchUsed"), Lean, TEXT("Settings.bAllowStretch"));
		Link(Reach, TEXT("IterationsUsed"), Lean, TEXT("Settings.Iterations"));
		Link(Reach, TEXT("MassMultiplierUsed"), Lean, TEXT("Settings.MassMultiplier"));
		if (!Controller->SetArrayPinSize(Lean->GetName() + TEXT(".Effectors"), 1, FString(), false))
		{
			Problems.Add(TEXT("第一遍全身 IK 的效应器设不成一个"));
		}
		Link(Reach, TEXT("Bone"), Lean, TEXT("Effectors.0.Bone"));
		Link(Reach, TEXT("Target"), Lean, TEXT("Effectors.0.Transform"));
		Link(Reach, TEXT("Alpha"), Lean, TEXT("Effectors.0.PositionAlpha"));
		Link(Reach, TEXT("Alpha"), Lean, TEXT("Effectors.0.RotationAlpha"));
		Link(Reach, TEXT("PullChainAlphaUsed"), Lean, TEXT("Effectors.0.PullChainAlpha"));

		// 骨骼设置：脊柱、锁骨的转动刚度；胳膊伸长时的平移刚度。脊柱、锁骨不设平移刚度：接近 1 时全身 IK 把那根骨骼的体整个锁住（IsPositionLocked），
		// 转不动；不允许伸长时关节照样保持骨长
		struct FBoneSettingLink
		{
			const TCHAR* Bone;
			const TCHAR* RotationStiffness;  // 伸手节点的输出，空的不接
			const TCHAR* PositionStiffness;  // 同上
		};
		const FBoneSettingLink BoneLinks[] = {
			{ TEXT("spine_01"), TEXT("SpineStiffnessUsed"), nullptr },
			{ TEXT("spine_02"), TEXT("SpineStiffnessUsed"), nullptr },
			{ TEXT("spine_03"), TEXT("SpineStiffnessUsed"), nullptr },
			{ TEXT("spine_04"), TEXT("SpineStiffnessUsed"), nullptr },
			{ TEXT("spine_05"), TEXT("SpineStiffnessUsed"), nullptr },
			{ TEXT("clavicle_l"), TEXT("ClavicleStiffnessUsed"), nullptr },
			{ TEXT("clavicle_r"), TEXT("ClavicleStiffnessUsed"), nullptr },
			{ TEXT("upperarm_l"), nullptr, TEXT("ArmStretchStiffnessUsed") },
			{ TEXT("lowerarm_l"), nullptr, TEXT("ArmStretchStiffnessUsed") },
			{ TEXT("hand_l"), nullptr, TEXT("ArmStretchStiffnessUsed") },
			{ TEXT("upperarm_r"), nullptr, TEXT("ArmStretchStiffnessUsed") },
			{ TEXT("lowerarm_r"), nullptr, TEXT("ArmStretchStiffnessUsed") },
			{ TEXT("hand_r"), nullptr, TEXT("ArmStretchStiffnessUsed") },
		};
		if (!Controller->SetArrayPinSize(Lean->GetName() + TEXT(".BoneSettings"), UE_ARRAY_COUNT(BoneLinks), FString(), false))
		{
			Problems.Add(TEXT("第一遍全身 IK 的骨骼设置设不成要的个数"));
		}
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(BoneLinks); ++Index)
		{
			const FBoneSettingLink& Setting = BoneLinks[Index];
			const FString Element = FString::Printf(TEXT("BoneSettings.%d"), Index);
			SetDefault(Lean, Element + TEXT(".Bone"), Setting.Bone);
			if (Setting.RotationStiffness)
			{
				Link(Reach, Setting.RotationStiffness, Lean, Element + TEXT(".RotationStiffness"));
			}
			if (Setting.PositionStiffness)
			{
				Link(Reach, Setting.PositionStiffness, Lean, Element + TEXT(".PositionStiffness"));
			}
		}
		if (!Problems.IsEmpty())
		{
			return false;
		}
		Controller->AddCommentNode(
			TEXT("伸手时上半身参与（FRigUnit_GinkgoHandReach）：只在有手在伸时走这一遍全身 IK——根钉在骨盆，效应器只有伸手的那只手，脊柱、锁骨按刚度转。")
			TEXT("拿枪的手不当效应器，连同枪跟着胸口走；后面的瞄准在弯过的胸口上重新算握法与伸手的目标，原来的全身 IK 补齐。")
			TEXT("由命令 GinkgoAnimBlueprint -AddReachLean 加，见 Docs/Design/Interaction.md 的\"拾取子弹盒\""),
			At + FVector2D(-40.0, -150.0), FVector2D(1150.0, 520.0), FLinearColor::White, FString(), false);
		Editor->RecompileVM();
		return true;
	}

	/**
	 * -AddReachLean：角色的 Control Rig（-ControlRig=，默认那份）里接上伸手时上半身参与。
	 * 加 -ReachRigFrom= 时先从那份复制到 -ControlRig= 再改（量的时候用副本）。原文件先备份到 Saved/Ginkgo/Backup；已经有伸手节点的不改
	 */
	int32 AddReachLean(const FString& ControlRigPackage, const FString& FromControlRig)
	{
		TArray<FString> Problems;
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *ControlRigPackage, *FPackageName::GetShortName(ControlRigPackage));
		const FString Filename = FPackageName::LongPackageNameToFilename(ControlRigPackage, FPackageName::GetAssetPackageExtension());
		FString Backup = TEXT("（新复制的，没有）");
		UControlRigRuntimeAsset* Rig = nullptr;
		if (!FromControlRig.IsEmpty())
		{
			UObject* Source = LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *FromControlRig, *FPackageName::GetShortName(FromControlRig)));
			if (Source && !FPackageName::DoesPackageExist(ControlRigPackage))
			{
				ObjectTools::FPackageGroupName Target;
				Target.PackageName = ControlRigPackage;
				Target.ObjectName = FPackageName::GetShortName(ControlRigPackage);
				TSet<UPackage*> RefusedPackages;
				Rig = Cast<UControlRigRuntimeAsset>(ObjectTools::DuplicateSingleObject(Source, Target, RefusedPackages, /*bPromptToOverwrite*/ false));
			}
		}
		else
		{
			Rig = LoadObject<UControlRigRuntimeAsset>(nullptr, *ObjectPath);
			Backup = Rig ? BackupBlueprint(FPackageName::GetShortName(ControlRigPackage), Filename) : FString();
		}
		if (!Rig || Backup.IsEmpty())
		{
			Problems.Add(FString::Printf(TEXT("读不到、复制不了或备份不了 %s（复制时目标不能已经存在）"), *ControlRigPackage));
		}
		const bool bChanged = Rig && !Backup.IsEmpty() && Problems.IsEmpty() && BuildReachLean(*Rig, Problems);
		if (bChanged && Problems.IsEmpty())
		{
			SavePackageOf(*Rig, Problems);
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		if (!Problems.IsEmpty())
		{
			return 1;
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s %s，原文件备份在 %s"), *ControlRigPackage, bChanged ? TEXT("接上了伸手时上半身参与") : TEXT("没改"), *Backup);
		return 0;
	}

	/**
	 * -SetControlRig=<Control Rig 的包>：动画蓝图里的 Control Rig 节点换成那一份（量第 2 批时副本用副本的 Control Rig），别的不动。
	 * 加 -From= 时先复制到 -Package= 再换。原文件先备份
	 */
	int32 SetControlRig(const FString& PackageName, const FString& Filename, const FString& FromPackage, const FString& ControlRigPackage)
	{
		UControlRigRuntimeAsset* Rig = LoadObject<UControlRigRuntimeAsset>(nullptr,
			*FString::Printf(TEXT("%s.%s"), *ControlRigPackage, *FPackageName::GetShortName(ControlRigPackage)));
		UAnimBlueprint* Blueprint = Rig ? (FromPackage.IsEmpty() ? LoadBlueprint(PackageName) : DuplicateBlueprint(FromPackage, PackageName)) : nullptr;
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		UAnimGraphNode_ControlRig* Node = nullptr;
		for (UEdGraphNode* Candidate : AnimGraph ? AnimGraph->Nodes : TArray<TObjectPtr<UEdGraphNode>>())
		{
			Node = Node ? Node : Cast<UAnimGraphNode_ControlRig>(Candidate);
		}
		if (!Node)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s、%s，或动画图里没有 Control Rig 节点"), *ControlRigPackage, *PackageName);
			return 1;
		}
		const FString Backup = FromPackage.IsEmpty() ? BackupBlueprint(FPackageName::GetShortName(PackageName), Filename) : FString(TEXT("（新复制的，没有）"));
		if (Backup.IsEmpty())
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 备份不了 %s，不改"), *Filename);
			return 1;
		}
		NodeValue<FControlRigAssetStrongReference>(*Node, TEXT("ControlRigAssetReference")).Set(TObjectPtr<UControlRigRuntimeAsset>(Rig));
		Node->ReconstructNode();
		const TArray<FString> Problems;
		return CompileAndSaveEdited(*Blueprint, Filename, Backup, Problems, *FString::Printf(TEXT("Control Rig 节点换成了 %s"), *ControlRigPackage));
	}

	/** -DumpAnimGraph：从输出节点往上游顺着姿势引脚打出动画图（查插的节点在不在通往输出的那条路上） */
	int32 DumpAnimGraph(const FString& PackageName)
	{
		UAnimBlueprint* Blueprint = LoadBlueprint(PackageName);
		UEdGraph* AnimGraph = Blueprint ? FindAnimGraph(*Blueprint) : nullptr;
		if (!AnimGraph)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或它的动画图"), *PackageName);
			return 1;
		}
		TSet<const UEdGraphNode*> Visited;
		TFunction<void(const UEdGraphNode&, int32)> Walk = [&](const UEdGraphNode& Node, const int32 Depth)
		{
			const bool bSeen = Visited.Contains(&Node);
			Visited.Add(&Node);
			const UAnimGraphNode_Slot* Slot = Cast<UAnimGraphNode_Slot>(&Node);
			const UAnimGraphNode_LinkedAnimLayer* Layer = Cast<UAnimGraphNode_LinkedAnimLayer>(&Node);
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[Dump] %s%s %s%s%s"), *FString::ChrN(Depth * 2, TEXT(' ')), *Node.GetClass()->GetName(), *Node.GetName(),
				Slot ? *FString::Printf(TEXT(" slot=%s"), *Slot->Node.SlotName.ToString()) : (Layer ? *FString::Printf(TEXT(" layer=%s"), *Layer->Node.Layer.ToString()) : TEXT("")),
				bSeen ? TEXT(" (again)") : TEXT(""));
			if (bSeen)
			{
				return;
			}
			for (const UEdGraphPin* Pin : Node.Pins)
			{
				if (Pin->Direction == EGPD_Input && UAnimationGraphSchema::IsPosePin(Pin->PinType))
				{
					for (const UEdGraphPin* Linked : Pin->LinkedTo)
					{
						UE_LOG(LogGinkgoAnimation, Display, TEXT("[Dump] %s  <- %s"), *FString::ChrN(Depth * 2, TEXT(' ')), *Pin->PinName.ToString());
						Walk(*Linked->GetOwningNode(), Depth + 1);
					}
				}
			}
		};
		for (const UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (const UAnimGraphNode_Root* Root = Cast<UAnimGraphNode_Root>(Node))
			{
				Walk(*Root, 0);
			}
		}
		for (const UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (!Visited.Contains(Node) && Node->IsA<UAnimGraphNode_Base>())
			{
				UE_LOG(LogGinkgoAnimation, Display, TEXT("[Dump] not on the path to the output: %s %s"), *Node->GetClass()->GetName(), *Node->GetName());
			}
		}
		return 0;
	}

	bool CompileBlueprint(UAnimBlueprint& Blueprint)
	{
		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(&Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		TArray<UEdGraph*> Graphs;
		Blueprint.GetAllGraphs(Graphs);
		for (const UEdGraph* Graph : Graphs)
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node->bHasCompilerMessage && !Node->ErrorMsg.IsEmpty())
				{
					UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] 节点 %s：%s"), *Node->GetName(), *Node->ErrorMsg);
				}
			}
		}
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			const bool bProblem = Message->GetSeverity() == EMessageSeverity::Error || Message->GetSeverity() == EMessageSeverity::Warning;
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] 编译%s：%s"), bProblem ? TEXT("问题") : TEXT("消息"), *Message->ToText().ToString());
		}
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] 编译：%d 个错误，%d 个警告"), Results.NumErrors, Results.NumWarnings);
		return Results.NumErrors == 0 && Results.NumWarnings == 0;
	}
}

UGinkgoAnimBlueprintCommandlet::UGinkgoAnimBlueprintCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UGinkgoAnimBlueprintCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);
	// -Package= 生成到别处（试图的改法时不碰手改过的那份；回放时用 -ini 把项目设置的 CharacterAnimClass 指过去）
	FString PackageName = BlueprintPackageName;
	FParse::Value(*Params, TEXT("Package="), PackageName);
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] -Package=%s 不是包路径（例如 /Game/Developers/ABP_Test）"), *PackageName);
		return 1;
	}
	const FString AssetName = FPackageName::GetShortName(PackageName);
	const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	if (Switches.Contains(TEXT("CompileOnly")))
	{
		UAnimBlueprint* Blueprint = LoadBlueprint(PackageName);
		return Blueprint && CompileBlueprint(*Blueprint) ? 0 : 1;
	}
	if (Switches.Contains(TEXT("KeepLocomotionActive")))
	{
		// 只修改现有全身槽，不重建用户编辑过的图、状态或节点绑定。
		UAnimBlueprint* Blueprint = LoadBlueprint(PackageName);
		if (!Blueprint) { return 1; }
		int32 Found = 0;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (auto* Slot = Cast<UAnimGraphNode_Slot>(Node); Slot && Slot->Node.SlotName == TEXT("DefaultSlot"))
				{
					Slot->Modify();
					Slot->Node.bAlwaysUpdateSourcePose = true;
					++Found;
				}
			}
		}
		if (Found != 1 || !CompileBlueprint(*Blueprint)) { return 1; }
		return SaveBlueprint(*Blueprint->GetOutermost(), *Blueprint) ? 0 : 1;
	}

	// -ControlRig= 角色的 Control Rig 放在别处（没有时生成）
	FString ControlRigPackage = ControlRigPackageName;
	FParse::Value(*Params, TEXT("ControlRig="), ControlRigPackage);
	if (!FPackageName::IsValidLongPackageName(ControlRigPackage))
	{
		UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] -ControlRig=%s 不是包路径"), *ControlRigPackage);
		return 1;
	}

	// 重建用（2026-11-05）：-RestoreLayerInterface 只生成层接口；-RegisterArmSlots 只在骨架里登记胳膊的槽；
	// -EnsureAimOffset 只给 98k 的瞄准偏移补运行时的数据；-RestoreWeaponFromText= 按文本重建 98k 的数据资产。都不碰动画蓝图
	if (Switches.Contains(TEXT("RestoreLayerInterface")))
	{
		TArray<FString> Problems;
		const UAnimBlueprint* Interface = FindOrCreateLayerInterface(Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Interface && Problems.IsEmpty() ? 0 : 1;
	}
	if (Switches.Contains(TEXT("RegisterArmSlots")))
	{
		TArray<FString> Problems;
		RegisterArmSlots(Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}
	if (Switches.Contains(TEXT("EnsureAimOffset")))
	{
		TArray<FString> Problems;
		UBlendSpace* AimOffset = LoadObject<UBlendSpace>(nullptr, TEXT("/Game/Developers/JiahaoLi/Mauser98kAimOffset/AO_Mauser98k_ADS.AO_Mauser98k_ADS"));
		if (!AimOffset)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 AO_Mauser98k_ADS（先 ue_make_aimoffset.py）"));
			return 1;
		}
		EnsureBlendSpaceData(*AimOffset, Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}
	FString WeaponText;
	if (FParse::Value(*Params, TEXT("RestoreWeaponFromText="), WeaponText))
	{
		return RestoreWeaponFromText(WeaponText);
	}
	// -Mauser98kAnchorsFrom=<旧资产的对象路径>：文本只存到小数点后 6 位，枪上的点从备份的旧资产里取完整的值（比到第 6 位相同才取）
	FString AnchorsFrom;
	if (FParse::Value(*Params, TEXT("Mauser98kAnchorsFrom="), AnchorsFrom))
	{
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, *FString::Printf(TEXT("%s.%s"), Mauser98kPackage, *FPackageName::GetShortName(Mauser98kPackage)));
		const UGinkgoWeaponDefinition* Source = LoadObject<UGinkgoWeaponDefinition>(nullptr, *AnchorsFrom);
		if (!Weapon || !Source)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s 或 %s"), Mauser98kPackage, *AnchorsFrom);
			return 1;
		}
		const auto Close = [](const FGinkgoWeaponHandTargets& A, const FGinkgoWeaponHandTargets& B) { return A.Left.Equals(B.Left, 1e-4) && A.Right.Equals(B.Right, 1e-4); };
		const bool bClose = Close(Weapon->LoweredHands, Source->LoweredHands) && Close(Weapon->HipHands, Source->HipHands) && Close(Weapon->AimHands, Source->AimHands)
			&& Close(Weapon->SprintHands, Source->SprintHands) && Weapon->MuzzleOffset.Equals(Source->MuzzleOffset, 1e-4);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] 枪口：现在 (%.9f, %.9f, %.9f)，备份 (%.9f, %.9f, %.9f)；手的目标同备份 %s"), Weapon->MuzzleOffset.X, Weapon->MuzzleOffset.Y, Weapon->MuzzleOffset.Z,
			Source->MuzzleOffset.X, Source->MuzzleOffset.Y, Source->MuzzleOffset.Z, bClose ? TEXT("差不到 0.0001") : TEXT("差得多，不取"));
		if (!bClose)
		{
			return 1;
		}
		Weapon->LoweredHands = Source->LoweredHands;
		Weapon->HipHands = Source->HipHands;
		Weapon->AimHands = Source->AimHands;
		Weapon->SprintHands = Source->SprintHands;
		Weapon->MuzzleOffset = Source->MuzzleOffset;
		TArray<FString> Problems;
		SavePackageOf(*Weapon, Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -RemeasureMauser98k：文本只存到小数点后 6 位，按片段与网格重新量枪上的点（EnsureWeaponAnchors 只在差过 0.001 时才写，所以先让枪口不同）
	if (Switches.Contains(TEXT("RemeasureMauser98k")))
	{
		TArray<FString> Problems;
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, *FString::Printf(TEXT("%s.%s"), Mauser98kPackage, *FPackageName::GetShortName(Mauser98kPackage)));
		if (!Weapon)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s"), Mauser98kPackage);
			return 1;
		}
		Weapon->MuzzleOffset = FVector::ZeroVector;
		EnsureWeaponAnchors(*Weapon, Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}

	// -AddSlots、-AddLean、-AddControlRig：只在已有的那份里插上蒙太奇槽、左右倾斜、Control Rig（先备份），别的不动；
	// -AddLean、-AddControlRig 加 -From= 时先从那份复制过来
	if (Switches.Contains(TEXT("AddSlots")))
	{
		return AddSlots(PackageName, Filename);
	}
	// -AddArmSlots：两个胳膊的槽（拾取），加 -From= 时先复制
	if (Switches.Contains(TEXT("AddArmSlots")))
	{
		FString FromPackage;
		FParse::Value(*Params, TEXT("From="), FromPackage);
		return AddArmSlots(PackageName, Filename, FromPackage);
	}
	// -ArmSlotsForSwap：拔枪、收枪搬到右臂的槽上，加 -From= 时先复制
	if (Switches.Contains(TEXT("ArmSlotsForSwap")))
	{
		FString FromPackage;
		FParse::Value(*Params, TEXT("From="), FromPackage);
		return ArmSlotsForSwap(PackageName, Filename, FromPackage);
	}
	if (Switches.Contains(TEXT("DumpAnimGraph")))
	{
		return DumpAnimGraph(PackageName);
	}
	// -AddHoldLoops：步枪的层里加上三个持枪循环（跑、冲刺、开镜），98k 的数据资产填上循环、量好冲刺的握法（主蓝图不动）；-LoopLayersTo=、-LoopWeaponTo= 时只改复制的
	if (Switches.Contains(TEXT("AddHoldLoops")))
	{
		FString LayersCopy;
		FString WeaponCopy;
		FParse::Value(*Params, TEXT("LoopLayersTo="), LayersCopy);
		FParse::Value(*Params, TEXT("LoopWeaponTo="), WeaponCopy);
		return AddHoldLoops(LayersCopy, WeaponCopy);
	}
	// -M1911Smoke：手枪的数据资产只填硝烟的大小，别的不动（你手测改过上面的数）
	if (Switches.Contains(TEXT("M1911Smoke")))
	{
		TArray<FString> Problems;
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911"));
		if (!Weapon)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 DA_M1911（先 -AddM1911）"));
			return 1;
		}
		Weapon->MuzzleSmokeScale = 0.5f;
		SavePackageOf(*Weapon, Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -M1911Spread：手枪的数据资产只填散布与准星的横线，别的不动。会盖掉你手测改的数，只在重建时用
	if (Switches.Contains(TEXT("M1911Spread")))
	{
		TArray<FString> Problems;
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911"));
		if (!Weapon)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 DA_M1911（先 -AddM1911）"));
			return 1;
		}
		FillM1911Spread(*Weapon);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] 散布：腰射 %.2f，开镜 %.2f，一枪 %.2f，恢复 %.2f/s，上限 %.2f、%.2f，空中 ×%.2f，速度的键 %d 个"), Weapon->Spread.HipDefault, Weapon->Spread.AdsDefault,
			Weapon->Spread.GrowthPerShot, Weapon->Spread.RecoveryRate, Weapon->Spread.HipMax, Weapon->Spread.AdsMax, Weapon->Spread.AirFactor, Weapon->Spread.SpeedFactor.GetRichCurveConst()->GetNumKeys());
		SavePackageOf(*Weapon, Problems);
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -M1911Cues：手枪的数据资产只填声音，别的不动
	if (Switches.Contains(TEXT("M1911Cues")))
	{
		TArray<FString> Problems;
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M1911/DA_M1911.DA_M1911"));
		if (!Weapon)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 DA_M1911（先 -AddM1911）"));
			return 1;
		}
		FillM1911Cues(*Weapon);
		for (const TPair<FGameplayTag, FGinkgoWeaponCue>& Cue : Weapon->Cues)
		{
			if (!Cue.Value.Sound.LoadSynchronous())
			{
				Problems.Add(FString::Printf(TEXT("%s 的声音读不出来：%s"), *Cue.Key.ToString(), *Cue.Value.Sound.ToString()));
			}
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s：%s，音量 %.2f，提前 %.1f ms"), *Cue.Key.ToString(), *Cue.Value.Sound.ToString(), Cue.Value.Volume, Cue.Value.Lead * 1000.f);
		}
		if (Problems.IsEmpty())
		{
			SavePackageOf(*Weapon, Problems);
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}
	if (Switches.Contains(TEXT("AddAK47Attachment")))
	{
		TArray<FString> Problems;
		if (auto* Weapon = LoadAsset<UGinkgoWeaponDefinition>(TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"), Problems))
		{
			EnsureAK47Attachment(*Weapon, Problems);
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -AddAK47：生成 AK-47 的数据资产
	if (Switches.Contains(TEXT("AddAK47")))
	{
		return AddAK47();
	}
	// -AddShotgun：生成泵动霰弹枪的数据资产
	if (Switches.Contains(TEXT("AddShotgun")))
	{
		return AddShotgun();
	}
	// -AddMG3：生成 MG3 的数据资产
	if (Switches.Contains(TEXT("AddMG3")))
	{
		return AddMG3();
	}
	// -AddM79：生成 M79 榴弹发射器的数据资产
	if (Switches.Contains(TEXT("AddM79")))
	{
		return AddM79();
	}
	// -MG3Heat：MG3 的数据资产只改枪管的热，别的不动
	if (Switches.Contains(TEXT("MG3Heat")))
	{
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/MG3/DA_MG3.DA_MG3"));
		if (!Weapon)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 DA_MG3（先 -AddMG3）"));
			return 1;
		}
		TArray<FString> Problems;
		FillMG3Heat(*Weapon);
		SavePackageOf(*Weapon, Problems);
		UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] DA_MG3：一发 %.1f °C，凉的时间常数 %.0f s"), Weapon->HeatPerShot, Weapon->HeatCoolTime);
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -ShotgunCues：霰弹枪的数据资产只填声音，别的不动
	if (Switches.Contains(TEXT("ShotgunCues")))
	{
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/Shotgun/DA_Shotgun.DA_Shotgun"));
		if (!Weapon)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 DA_Shotgun（先 -AddShotgun）"));
			return 1;
		}
		TArray<FString> Problems;
		FillShotgunCues(*Weapon);
		SavePackageOf(*Weapon, Problems);
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -M79Cues：M79 的数据资产只填声音，保留已挂载的 ST_ActionBreak 子树与动作片段不动
	if (Switches.Contains(TEXT("M79Cues")))
	{
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/M79/DA_M79.DA_M79"));
		if (!Weapon)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 DA_M79（先 -AddM79）"));
			return 1;
		}
		TArray<FString> Problems;
		FillM79Cues(*Weapon);
		for (const TPair<FGameplayTag, FGinkgoWeaponCue>& Cue : Weapon->Cues)
		{
			if (!Cue.Value.Sound.LoadSynchronous())
			{
				Problems.Add(FString::Printf(TEXT("%s 的声音读不出来：%s"), *Cue.Key.ToString(), *Cue.Value.Sound.ToString()));
			}
			UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] M79 %s：%s，音量 %.2f，提前 %.1f ms"),
				*Cue.Key.ToString(), *Cue.Value.Sound.ToString(), Cue.Value.Volume, Cue.Value.Lead * 1000.f);
		}
		if (Problems.IsEmpty())
		{
			SavePackageOf(*Weapon, Problems);
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -AK47Cues：AK-47 的数据资产只填声音，别的不动（声音资产导入以后跑）
	if (Switches.Contains(TEXT("AK47Cues")))
	{
		TArray<FString> Problems;
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
		if (!Weapon)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 DA_AK47"));
			return 1;
		}
		FillAK47Cues(*Weapon);
		for (const TPair<FGameplayTag, FGinkgoWeaponCue>& Cue : Weapon->Cues)
		{
			if (!Cue.Value.Sound.LoadSynchronous())
			{
				Problems.Add(Cue.Value.Sound.ToString());
			}
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 读不到 %s"), *Problem);
		}
		SavePackageOf(*Weapon, Problems);
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -AK47Pattern：AK-47 的数据资产填上后坐模板（模板导入以后跑），别的不动
	if (Switches.Contains(TEXT("AK47Pattern")))
	{
		TArray<FString> Problems;
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr, TEXT("/Game/Weapons/AK47/DA_AK47.DA_AK47"));
		UGinkgoRecoilPattern* Pattern = LoadObject<UGinkgoRecoilPattern>(nullptr, TEXT("/Game/Weapons/AK47/RP_AK47.RP_AK47"));
		if (!Weapon || !Pattern)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到 DA_AK47 或 RP_AK47"));
			return 1;
		}
		Weapon->PatternRecoil.Pattern = Pattern;
		SavePackageOf(*Weapon, Problems);
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -AddM1911：层里加上低持、腰射两个循环，生成手枪的数据资产
	if (Switches.Contains(TEXT("AddM1911")))
	{
		return AddM1911(Switches.Contains(TEXT("RebuildM1911")));
	}
	// -WeaponHud：98k 的数据资产填上界面与机构（名字、口径、图标、开镜准星、机构、弹种、后备），别的不动
	if (Switches.Contains(TEXT("WeaponHud")))
	{
		TArray<FString> Problems;
		UGinkgoWeaponDefinition* Weapon = LoadObject<UGinkgoWeaponDefinition>(nullptr,
			*FString::Printf(TEXT("%s.%s"), Mauser98kPackage, *FPackageName::GetShortName(Mauser98kPackage)));
		if (!Weapon)
		{
			Problems.Add(FString::Printf(TEXT("找不到 %s（先 -AddWeaponLayers）"), Mauser98kPackage));
		}
		const bool bChanged = Weapon && ApplyMauser98kHud(*Weapon);
		if (bChanged)
		{
			SavePackageOf(*Weapon, Problems);
		}
		for (const FString& Problem : Problems)
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
		}
		UE_CLOG(Problems.IsEmpty(), LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s %s"), Mauser98kPackage, bChanged ? TEXT("填上了界面与机构，存回去了") : TEXT("界面与机构已经是要的，没改"));
		return Problems.IsEmpty() ? 0 : 1;
	}
	// -AddWeaponAim：-ControlRig= 那份 Control Rig 里接上瞄准与双手，-AimRigFrom= 先从那份复制（参数名不含 "ControlRig="：FParse::Value 按子串找）
	if (Switches.Contains(TEXT("AddPeek")))
	{
		return AddPeek(PackageName, Filename);
	}

	if (Switches.Contains(TEXT("AddWeaponAim")))
	{
		FString FromControlRig;
		FParse::Value(*Params, TEXT("AimRigFrom="), FromControlRig);
		return AddWeaponAim(ControlRigPackage, FromControlRig);
	}
	// -AddReachLean：-ControlRig= 那份 Control Rig 里接上伸手时上半身参与，-ReachRigFrom= 先从那份复制
	if (Switches.Contains(TEXT("AddReachLean")))
	{
		FString FromControlRig;
		FParse::Value(*Params, TEXT("ReachRigFrom="), FromControlRig);
		return AddReachLean(ControlRigPackage, FromControlRig);
	}
	// -UseRig=：动画蓝图的 Control Rig 节点换成那一份，-From= 先复制
	FString UseRigPackage;
	if (FParse::Value(*Params, TEXT("UseRig="), UseRigPackage))
	{
		FString FromPackage;
		FParse::Value(*Params, TEXT("From="), FromPackage);
		return SetControlRig(PackageName, Filename, FromPackage, UseRigPackage);
	}
	if (Switches.Contains(TEXT("AddLean")) || Switches.Contains(TEXT("AddControlRig")) || Switches.Contains(TEXT("AddWeaponLayers"))
		|| Switches.Contains(TEXT("FixUpperBodyCurves")))
	{
		FString FromPackage;
		FParse::Value(*Params, TEXT("From="), FromPackage);
		if (Switches.Contains(TEXT("AddWeaponLayers")))
		{
			return AddWeaponLayers(PackageName, Filename, FromPackage);
		}
		if (Switches.Contains(TEXT("FixUpperBodyCurves")))
		{
			return FixUpperBodyCurves(PackageName, Filename, FromPackage);
		}
		return Switches.Contains(TEXT("AddLean")) ? AddLean(PackageName, Filename, FromPackage) : AddControlRig(PackageName, Filename, FromPackage, ControlRigPackage);
	}

	// 生成以后在编辑器里改：已经有了就不动，加 -Force 才重新生成
	if (IFileManager::Get().FileExists(*Filename))
	{
		if (!Switches.Contains(TEXT("Force")))
		{
			UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s 已经有了，不覆盖（编辑器里手改的会丢）。要重新生成加 -Force"), *Filename);
			return 1;
		}
		IFileManager::Get().Delete(*Filename);
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, SkeletonPath);
	if (!Skeleton)
	{
		UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 找不到骨架 %s（先跑导入命令 GinkgoAnimationImport）"), SkeletonPath);
		return 1;
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = UGinkgoAnimInstance::StaticClass();
	Factory->BlueprintType = BPTYPE_Normal;
	UPackage* Package = CreatePackage(*PackageName);
	UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(), Package, FName(AssetName),
		RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn));
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 建不了 %s"), *PackageName);
		return 1;
	}

	// 先填片段（状态里的节点要用它们做预览），再搭图、编译；编译会换掉生成的类，最后在新的类默认值上再填一遍
	TArray<FString> Problems;
	UControlRigRuntimeAsset* Rig = FindOrCreateControlRig(ControlRigPackage, Problems);
	FillAnimations(*Blueprint->GeneratedClass->GetDefaultObject<UGinkgoAnimInstance>(), Problems);
	BuildAnimGraph(*Blueprint, *Blueprint->GeneratedClass->GetDefaultObject<UGinkgoAnimInstance>(), Rig, Problems);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	const bool bCompiled = CompileBlueprint(*Blueprint);
	TArray<FString> RefillProblems;
	FillAnimations(*Blueprint->GeneratedClass->GetDefaultObject<UGinkgoAnimInstance>(), RefillProblems);
	Blueprint->MarkPackageDirty();

	for (const FString& Problem : Problems)
	{
		UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] %s"), *Problem);
	}
	if (!SaveBlueprint(*Package, *Blueprint))
	{
		UE_LOG(LogGinkgoAnimation, Error, TEXT("[AnimBlueprint] 存不了 %s"), *Filename);
		return 1;
	}
	const bool bPassed = bCompiled && Problems.IsEmpty();
	UE_LOG(LogGinkgoAnimation, Display, TEXT("[AnimBlueprint] %s：存到 %s，%d 个问题"), bPassed ? TEXT("生成了") : TEXT("生成了但有问题"), *Filename, Problems.Num());
	return bPassed ? 0 : 1;
}

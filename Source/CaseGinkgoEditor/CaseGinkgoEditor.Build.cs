using UnrealBuildTool;

// 编辑器扩展：移动追踪的分析器与 Rewind Debugger 的轨道，动画素材的导入命令，生成动画蓝图的命令。只在编辑器里编译与加载
public class CaseGinkgoEditor : ModuleRules
{
	public CaseGinkgoEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AnimationBlueprintLibrary",
			"MotionWarping",
			"AnimationWarpingEditor",
			"AnimationWarpingRuntime",
			"AnimGraph",
			"IKRig",
			"IKRigDeveloper",
			"IKRigEditor",
			"AnimGraphRuntime",
			"AssetRegistry",
			"AIModule",
			"NavigationSystem",
			"Niagara",
			"NiagaraEditor",
			"BlueprintGraph",
			"ControlRig",
			"ControlRigDeveloper",
			"ControlRigEditor",
			"Core",
			"CoreUObject",
			"CustomizableObject",
			"CustomizableObjectEditor",
			"Json",
			"DirectoryWatcher",
			"Engine",
			"EditorFramework",
			"InputCore",
			"GameplayTags",
			"ImageCore",
			"PropertyBindingUtils",
			"RenderCore",
			"RigVM",
			"RigVMDeveloper",
			"RigVMEditor",
			"Slate",
			"SlateCore",
			"StateTreeEditorModule",
			"StateTreeModule",
			"UMG",
			"TraceAnalysis",
			"TraceLog",
			"TraceServices",
			"UnrealEd",
			"RewindDebuggerInterface",
			"RewindDebuggerRuntimeInterface",
			"CaseGinkgo",
			"CaseGinkgoAnimGraph",
		});
	}
}

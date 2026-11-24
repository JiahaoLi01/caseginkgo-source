using UnrealBuildTool;

// 动画图节点的编辑与编译支持：UncookedOnly 允许节点出现在运行时蓝图中，烘焙后只保留 CaseGinkgo 的运行时结构。
public class CaseGinkgoAnimGraph : ModuleRules
{
	public CaseGinkgoAnimGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AnimGraph", "BlueprintGraph", "CaseGinkgo" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AnimGraphRuntime" });
	}
}

using UnrealBuildTool;

// 自动化测试（CaseGinkgo.*）。DeveloperTool 模块：编辑器与开发版带着它，发行版没有
public class CaseGinkgoTests : ModuleRules
{
	public CaseGinkgoTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "EnhancedInput", "InputCore", "GameplayTags", "CaseGinkgo", "Weft" });
		PrivateDependencyModuleNames.AddRange(new string[] { "AIModule", "NavigationSystem", "StateTreeModule" });
		PrivateDependencyModuleNames.Add("CustomizableObject");
		PrivateDependencyModuleNames.Add("RenderCore");
		PrivateDependencyModuleNames.Add("Niagara");
		PrivateDependencyModuleNames.Add("MotionWarping");
		// 检查地点音效的真实 MetaSound 图及随机/共享状态，避免仅凭资产名通过验证。
		PrivateDependencyModuleNames.AddRange(new string[] { "MetasoundEngine", "MetasoundFrontend" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}

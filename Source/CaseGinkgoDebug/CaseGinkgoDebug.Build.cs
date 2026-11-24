using UnrealBuildTool;

// 调试工具：相机的开关、画线、运行时改配置的命令与调参面板（weft 页面），输入的测量与设置界面的测试命令（Control/）。DeveloperTool 模块：编辑器与开发版带着它，发行版没有。
// 主模块不知道它：它挂在主模块的钩子上（AGinkgoPlayerCameraManager::OnDebugView）
public class CaseGinkgoDebug : ModuleRules
{
	public CaseGinkgoDebug(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Slate", "SlateCore", "Json", "GameplayTags", "Weft", "CaseGinkgo", "AudioMixer", "AudioMixerCore" });

		// 调参面板的页面跟着这个模块打包：Content/UI 在发行版里也打包（DefaultGame.ini），调试页面不放那里
		RuntimeDependencies.Add("$(ModuleDir)/UI/...", StagedFileType.NonUFS);
	}
}

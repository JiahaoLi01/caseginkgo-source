// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CaseGinkgo : ModuleRules
{
	public CaseGinkgo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// 模块按功能平铺目录、不分 Public/Private，而 BuildSettingsVersion.V7 不再自动把模块根目录加进 include 路径；
		// 这里显式加上，模块内外统一写相对模块根的路径（如 "Utilities/FunctionLibrary/Range/UEQuery.h"）
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayTags", "DeveloperSettings" });

		// 武器的动作状态（Weapon/GinkgoWeaponStateTree）：武器组件的头文件里有树的实例数据，所以是公开依赖
		PublicDependencyModuleNames.Add("StateTreeModule");

		// AI 的控制器、感知与 EQS，以及沿导航网格的移动；公开头文件继承这些模块的类型
		PublicDependencyModuleNames.AddRange(new string[] { "AIModule", "NavigationSystem" });

		// 动画实例的节点函数用序列播放器与求值器的库（Animation/GinkgoAnimInstance_Locomotion.cpp）
		PrivateDependencyModuleNames.Add("AnimGraphRuntime");
		// 攀爬修正器的公开头文件继承 Motion Warping 类型；位移仍由 CharacterMovement 执行。
		PublicDependencyModuleNames.Add("MotionWarping");
		// 枪的声音：MetaSound 先建好，第一枪不等现建（UGinkgoWeaponComponent::PreloadCues）
		PrivateDependencyModuleNames.Add("MetasoundEngine");
		// 腿部 IK 的两骨骼 IK（AnimationCore::SolveTwoBoneIK）
		PrivateDependencyModuleNames.Add("AnimationCore");
		// 腿部 IK 是 Control Rig 里的节点（Animation/RigUnit_GinkgoLegIK）
		PrivateDependencyModuleNames.AddRange(new string[] { "ControlRig", "RigVM" });

		// 移动的追踪（Movement/GinkgoMovementTrace），给编辑器里的 Rewind Debugger 用
		PrivateDependencyModuleNames.Add("TraceLog");

		// 攀爬校正快照使用量化世界坐标，保持在 CMC 移动回包的位预算内。
		PrivateDependencyModuleNames.Add("NetCore");

		// 枪口的火光与硝烟（Weapon/GinkgoWeaponComponent.cpp）
		PrivateDependencyModuleNames.Add("Niagara");
		// M4 的组合网格异步生成；配件状态和玩法规则仍属于武器组件。
		PrivateDependencyModuleNames.Add("CustomizableObject");
		// 抛壳的物理材质
		PrivateDependencyModuleNames.Add("PhysicsCore");

		// 镜外降分辨率需要查询当前帧超分器允许的输入范围。
		PrivateDependencyModuleNames.Add("Renderer");

		// 界面（UI/）：weft 的页面放在视口上的 Slate 控件里，与页面交换的数据是 JSON
		PrivateDependencyModuleNames.AddRange(new string[] { "Weft", "Slate", "SlateCore", "Json" });

		// 可选 SDK 来自项目的固定版本安装脚本。没有安装的机器仍可使用全部原生画质与显示设置。
		var NvidiaDir = System.IO.Path.Combine(ModuleDirectory, "../../Plugins/NVIDIA");
		bool bWithDLSS = Target.Platform == UnrealTargetPlatform.Win64 && Target.Type != TargetType.Server &&
			System.IO.File.Exists(System.IO.Path.Combine(NvidiaDir, "DLSS/DLSS.uplugin")) &&
			System.IO.File.Exists(System.IO.Path.Combine(NvidiaDir, "StreamlineDLSSG/StreamlineDLSSG.uplugin"));
		PrivateDefinitions.Add("GINKGO_WITH_DLSS=" + (bWithDLSS ? "1" : "0"));
		if (bWithDLSS)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "DLSSBlueprint", "StreamlineDLSSGBlueprint", "StreamlineBlueprint", "StreamlineRHI", "RHI" });
		}

		if (Target.bBuildEditor)
		{
			// 存档（SaveGame/）：读档前清掉编辑器的选中；蓝图连错时跳到节点
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}

// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FPSDemo : ModuleRules
{
	// Target 是 UBT 的只读构建上下文；GAS 三个模块为原生技能、属性与任务提供依赖。
	public FPSDemo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// 模块级非 Unity 保证分类后的每个翻译单元独立包含所需头文件，不触发整套源码引擎重编。
		bUseUnity = false;
		// GAS 子目录使用模块根相对路径，显式包含根目录以支持 Unity/非 Unity 构建。
		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayAbilities", "GameplayTags", "GameplayTasks" });
		// Canvas 使用 Slate 运行时复合字体提供中文回退，RenderCore 提供圆角面板的白色纹理。
		// JSON是跨端协议；独立Online模块负责HTTP/凭据，不把数据库驱动链接进UE。
		// 已有DemoEnemyNavigation.cpp调用导航投影/同步寻路；显式链接引擎模块以免整合构建出现未解析符号。
		// PhysicsCore支持骨骼受击体/调试查询；运行时不依赖Chaos编辑器制作模块。
		PrivateDependencyModuleNames.AddRange(new string[] { "SlateCore", "RenderCore", "Json", "JsonUtilities", "FPSDemoOnline", "NavigationSystem", "PhysicsCore" });
	}
}

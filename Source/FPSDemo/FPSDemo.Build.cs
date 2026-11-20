// 职责：声明FPSDemo运行模块依赖；UBT调用。GAS负责战斗，Voice提供本机采集/Opus，AudioMixer/SignalProcessing满足引擎语音公共头依赖。

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

		// 公网驱动继承EOS基类；默认OSS仍为NULL，EOS仅在显式登录后创建。
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayAbilities", "GameplayTags", "GameplayTasks", "OnlineSubsystem", "SocketSubsystemEOS", "AnimGraphRuntime" }); // 原生最终足底控制节点需要公开SkeletalControl基类，不引入Editor依赖。
		// Slate提供UMG底层键盘焦点；Canvas 使用 Slate 运行时复合字体提供中文回退，RenderCore 提供圆角面板的白色纹理。
		// JSON是跨端协议；独立Online模块负责HTTP/凭据，不把数据库驱动链接进UE。
		// 已有DemoEnemyNavigation.cpp调用导航投影/同步寻路；显式链接引擎模块以免整合构建出现未解析符号。
		// Niagara提供可配置子弹拖尾/撞墙表现，物理伤害不依赖特效资源。
		// PhysicsCore支持骨骼受击体/调试查询；运行时不依赖Chaos编辑器制作模块。
		// DeveloperSettings为核心流派集中配置提供运行时类型，不引入UnrealEd或第二套配置读取器。
		// Renderer/RHI仅用于原生轮廓的公开View/后处理阶段接口；不包含Renderer私有头或新增外部插件。
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "RenderCore", "Renderer", "RHI", "Json", "JsonUtilities", "FPSDemoOnline", "NavigationSystem", "PhysicsCore", "Niagara", "DeveloperSettings", "OnlineSubsystemUtils", "OnlineSubsystemEOS", "AnimationCore" }); // AnimationCore仅用于保持原骨长的足底TwoBoneIK；EOS使用公开ID接口。
		AddEngineThirdPartyPrivateStaticDependencies(Target, "EOSSDK"); // 只用SDK的PUID格式转换，运行服务仍经OSS；版本随UE5.4引擎。
        PublicDependencyModuleNames.AddRange(new string[] { "UMG", "CommonUI", "CommonInput" }); // 页面继承CommonActivatableWidget；Slate仍作为底层和旧页面字体依赖。
		PrivateDependencyModuleNames.AddRange(new string[] { "Voice", "AudioMixer", "SignalProcessing" }); // 队伍语音走现有游戏连接，不启用旧OSS自动采集或另建EOS RTC。
	}
}

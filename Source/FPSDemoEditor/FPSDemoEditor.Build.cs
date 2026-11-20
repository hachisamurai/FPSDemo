// 职责：声明离线资产制作依赖；仅Editor Target加载，Niagara默认参数及Python隐藏WPO材质引脚桥接供作者工具使用。
using UnrealBuildTool;

/** 编辑器资产编排与验证模块；音频、导航、骨骼制作工具不进入游戏运行时依赖。 */
public class FPSDemoEditor : ModuleRules
{
    // Target 为 UBT 只读构建上下文，只在 Editor Target 编译此模块。
    public FPSDemoEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        System.Console.WriteLine("[FPSDemoEditor.Build] Configure editor audio authoring dependencies");
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Save/试玩清理与Tests/均使用编辑器模块根相对路径，保持功能目录分类。
        PrivateIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayAbilities" }); // 临时Lyra图导入父类保留GameplayTag属性映射结构；不引入LyraGame。
        PrivateDependencyModuleNames.AddRange(new string[] { "GameplayTags", "GameplayTasks" }); // 仅Editor交叉火力候选GA的标签/生命周期；不增加Runtime到Editor的反向依赖。
        PrivateDependencyModuleNames.Add("Niagara"); // 只给编辑器桥接写入User参数及完成系统编译；建图插件不成为运行时依赖。
        PrivateDependencyModuleNames.Add("NiagaraEditor"); // Lyra流线候选通过公开StackGraph API连接可配置参数，仅编辑器制作。
        PrivateDependencyModuleNames.Add("MovieScene"); // R05仅Editor迁移实际Sequencer动画键时钟，保留未编辑Euler通道，避免全轨Quat往返；不进入游戏依赖。
        PrivateDependencyModuleNames.Add("MaterialEditor"); // 枪墙描边作者复用原生WPO连线/查询；仅桥接Python隐藏枚举，不增加Game依赖。
        PrivateDependencyModuleNames.AddRange(new string[] { "UMG", "UMGEditor", "CommonUI", "CommonInput", "SlateCore" }); // 仅Editor命令行生成/编译真正的Widget Blueprint外壳，禁止伪造uasset。
        // Slate/InputCore仅供编辑器快捷键回归检查，均不进入Runtime模块依赖。
        // NavigationSystem仅供编辑器桥接创建导航体积、构建并验证真实Recast数据。
        // Json用于检查V4云导出字段的精确小写ID，不能只用UStruct往返掩盖打包FName大小写差异。
        // RenderCore/RHI 只用于检查 SkeletalMesh 导入后的渲染顶点权重，验证真实蒙皮而非仅验证骨动画。
        // AnimGraph/BlueprintGraph/KismetCompiler只供离线创建薄AnimBP，运行时不依赖编辑器建图模块。
        // PhysicsCore构建骨骼受击几何，Chaos显式链接离线凸体射线验证；Editor桥接不进入打包游戏。
        // AnimationBlueprintLibrary采样模板与枪械姿势，AnimationCore只在离线烘焙时求解双臂IK。
        PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AudioEditor", "FPSDemo", "FPSDemoAnimGraph", "Slate", "InputCore", "NavigationSystem", "Json", "RenderCore", "RHI", "AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "KismetCompiler", "AssetRegistry", "PhysicsCore", "Chaos", "AnimationBlueprintLibrary", "AnimationCore" }); // 自定义图类属于UncookedOnly模块，-game也必须能解析已存图。
        // 仅Editor/Windows的被动诊断读取接口、默认路由和邻居缓存；不链接到游戏包体，也不调用网络配置API。
        if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("iphlpapi.lib");
    }
}

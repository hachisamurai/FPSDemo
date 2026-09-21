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
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        // Slate/InputCore仅供编辑器快捷键回归检查，均不进入Runtime模块依赖。
        // NavigationSystem仅供编辑器桥接创建导航体积、构建并验证真实Recast数据。
        // Json用于检查V4云导出字段的精确小写ID，不能只用UStruct往返掩盖打包FName大小写差异。
        // RenderCore/RHI 只用于检查 SkeletalMesh 导入后的渲染顶点权重，验证真实蒙皮而非仅验证骨动画。
        // AnimGraph/BlueprintGraph/KismetCompiler只供离线创建薄AnimBP，运行时不依赖编辑器建图模块。
        // PhysicsCore构建骨骼受击几何，Chaos显式链接离线凸体射线验证；Editor桥接不进入打包游戏。
        // AnimationBlueprintLibrary采样模板与枪械姿势，AnimationCore只在离线烘焙时求解双臂IK。
        PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AudioEditor", "FPSDemo", "Slate", "InputCore", "NavigationSystem", "Json", "RenderCore", "RHI", "AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "KismetCompiler", "AssetRegistry", "PhysicsCore", "Chaos", "AnimationBlueprintLibrary", "AnimationCore" });
    }
}

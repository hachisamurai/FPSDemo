using UnrealBuildTool;

/** 编辑器音频资产编排模块；SoundCue 图构建不进入游戏运行时依赖。 */
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
        PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "AudioEditor", "FPSDemo", "Slate", "InputCore", "NavigationSystem", "Json" });
    }
}

using UnrealBuildTool;

/** 自定义动画图节点在编辑器和未Cook Standalone都要可反序列化；不将作者工具/存档清理带到-game。 */
public class FPSDemoAnimGraph : ModuleRules
{
    // Target为UBT只读上下文；UncookedOnly由uproject限制，Cook包只依赖FPSDemo运行节点。
    public FPSDemoAnimGraph(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity=false;
        PublicIncludePaths.Add(ModuleDirectory);
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "FPSDemo", "AnimGraph", "AnimGraphRuntime" }); // 图节点公开继承AnimGraph骨控制基类。
        PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "BlueprintGraph" }); // 只在未Cook工具目标使用的编译/Pin接口。
    }
}

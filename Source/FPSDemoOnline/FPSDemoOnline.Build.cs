using UnrealBuildTool;

// 独立Runtime模块只提供HTTP和本机凭据，不依赖具体武器/GAS或MongoDB。
public class FPSDemoOnline : ModuleRules
{
    // Target是UBT只读平台信息；DPAPI当前仅支持Windows客户端。
    public FPSDemoOnline(ReadOnlyTargetRules Target) : base(Target)
    {
        System.Console.WriteLine("[CALL] FPSDemoOnline Build rules");
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "HTTP" });
        if (Target.Platform == UnrealTargetPlatform.Win64) PublicSystemLibraries.Add("Crypt32.lib");
    }
}

// 职责：验证调试命令及日志的Editor/包体编译边界。使用：Automation RunTests FPSDemo.Debug.BuildBoundary；依赖控制台注册表，不修改存档。
#include "Misc/AutomationTest.h"
#include "HAL/IConsoleManager.h"
#include "Debug/DemoLog.h"
#include "DemoApiClient.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoDebugBoundaryTest, "FPSDemo.Debug.BuildBoundary",
    EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)

/** Parameters为未使用的测试参数；只读注册表和编译上限，不创建存档/联网或改日志级别。 */
bool FDemoDebugBoundaryTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo, Display, TEXT("DEBUG_BOUNDARY_TEST begin editor=%d"), WITH_EDITOR); // 包体关键自检结果，保留日志。
#if WITH_EDITOR
    TestNotNull(TEXT("Editor registers unlock command"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.UnlockAll")));
    TestNotNull(TEXT("Editor registers mode unlock command"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.UnlockAllModes"))); // 两类解锁独立注册。
    // 实体弹轨迹调试与全解锁遵循相同Editor边界，发布包不能注册调试绘制入口。
    TestNotNull(TEXT("Editor registers projectile trajectory drawing"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.ProjectileTrajectories")));
    // 2026-11-19新增：加币命令与FP构图调试同样只在Editor存在。
    TestNotNull(TEXT("Editor registers add gold command"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.AddGold")));
    TestNotNull(TEXT("Editor registers add silver command"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.AddSilver")));
    TestNotNull(TEXT("Editor registers viewmodel offset tuning"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Feel.ViewmodelOffset")));
    TestTrue(TEXT("Editor retains full gameplay log instrumentation"), FLogCategoryLogFPSDemo::CompileTimeVerbosity==ELogVerbosity::All);
    TestTrue(TEXT("Editor retains full online log instrumentation"), FLogCategoryLogDemoOnline::CompileTimeVerbosity==ELogVerbosity::All);
#else
    TestNull(TEXT("Packaged build has no unlock command even in Development"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.UnlockAll")));
    TestNull(TEXT("Packaged build has no mode unlock command even in Development"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.UnlockAllModes"))); // 非Editor不能靠运行时开关重新开放。
    TestNull(TEXT("Packaged build has no projectile trajectory drawing"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.ProjectileTrajectories"))); // 非Editor必须编译移除，不能仅默认关闭。
    TestNull(TEXT("Packaged build has no add gold command"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.AddGold"))); // 包体不能凭空加币。
    TestNull(TEXT("Packaged build has no add silver command"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.AddSilver")));
    TestNull(TEXT("Packaged build has no viewmodel offset tuning"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Feel.ViewmodelOffset")));
    TestTrue(TEXT("Packaged gameplay compile ceiling is Display"), FLogCategoryLogFPSDemo::CompileTimeVerbosity==ELogVerbosity::Display);
    TestTrue(TEXT("Packaged online compile ceiling is Display"), FLogCategoryLogDemoOnline::CompileTimeVerbosity==ELogVerbosity::Display);
#endif
    UE_LOG(LogFPSDemo, Log, TEXT("DEBUG_BOUNDARY_DETAIL_SENTINEL")); // Editor可输出；包体即使命令行设VeryVerbose也不能产生此标记。
    UE_LOG(LogDemoOnline, VeryVerbose, TEXT("DEBUG_BOUNDARY_ONLINE_SENTINEL")); // 同样验证网络模块编译裁剪，无HTTP请求。
    if (!HasAnyErrors()) UE_LOG(LogFPSDemo, Display, TEXT("DEBUG_BOUNDARY_TEST_SUCCESS"));
    return !HasAnyErrors();
}
#endif

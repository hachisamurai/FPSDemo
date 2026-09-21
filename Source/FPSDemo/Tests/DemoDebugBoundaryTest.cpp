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
    TestTrue(TEXT("Editor retains full gameplay log instrumentation"), FLogCategoryLogFPSDemo::CompileTimeVerbosity==ELogVerbosity::All);
    TestTrue(TEXT("Editor retains full online log instrumentation"), FLogCategoryLogDemoOnline::CompileTimeVerbosity==ELogVerbosity::All);
#else
    TestNull(TEXT("Packaged build has no unlock command even in Development"), IConsoleManager::Get().FindConsoleObject(TEXT("Demo.Debug.UnlockAll")));
    TestTrue(TEXT("Packaged gameplay compile ceiling is Display"), FLogCategoryLogFPSDemo::CompileTimeVerbosity==ELogVerbosity::Display);
    TestTrue(TEXT("Packaged online compile ceiling is Display"), FLogCategoryLogDemoOnline::CompileTimeVerbosity==ELogVerbosity::Display);
#endif
    UE_LOG(LogFPSDemo, Log, TEXT("DEBUG_BOUNDARY_DETAIL_SENTINEL")); // Editor可输出；包体即使命令行设VeryVerbose也不能产生此标记。
    UE_LOG(LogDemoOnline, VeryVerbose, TEXT("DEBUG_BOUNDARY_ONLINE_SENTINEL")); // 同样验证网络模块编译裁剪，无HTTP请求。
    if (!HasAnyErrors()) UE_LOG(LogFPSDemo, Display, TEXT("DEBUG_BOUNDARY_TEST_SUCCESS"));
    return !HasAnyErrors();
}
#endif

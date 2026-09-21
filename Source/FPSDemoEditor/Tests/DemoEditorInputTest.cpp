#include "Misc/AutomationTest.h"
#include "Kismet2/DebuggerCommands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Debug/DemoLog.h"

#if WITH_DEV_AUTOMATION_TESTS
// 仅编辑器自动化队列显式调用，不启动PIE、不改用户快捷键；宏声明不包含功能实现。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoEditorPauseBindingTest, "FPSDemo.Editor.PauseBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters为自动化命令的可选参数，此用例不使用；检查引擎实际解析后的停止快捷键。 */
bool FDemoEditorPauseBindingTest::RunTest(const FString& Parameters)
{
    // AutomationTest不是UObject，不能使用基于GetNameSafe(this)的DEMO_LOG_CALL；等价记录入口。
    UE_LOG(LogFPSDemo, Log, TEXT("FDemoEditorPauseBindingTest::RunTest"));
    if (!FPlayWorldCommands::IsRegistered())
    {
        AddError(TEXT("PlayWorld commands not registered"));
        UE_LOG(LogFPSDemo, Error, TEXT("Editor PlayWorld commands unavailable"));
        return false;
    }
    const FInputChord Primary = *FPlayWorldCommands::Get().StopPlaySession->GetActiveChord(EMultipleKeyBindingIndex::Primary); // 复制实际主快捷键值，不持有Slate对象。
    const FInputChord Secondary = *FPlayWorldCommands::Get().StopPlaySession->GetActiveChord(EMultipleKeyBindingIndex::Secondary); // 副键也不能抢占单独Esc。
    TestTrue(TEXT("Project PIE stop uses Shift+Esc"), Primary == FInputChord(EKeys::Escape, true, false, false, false));
    TestTrue(TEXT("Secondary stop does not consume plain Esc"), Secondary != FInputChord(EKeys::Escape));
    UE_LOG(LogFPSDemo, Display, TEXT("EDITOR_PAUSE_BINDING primary=%s secondary=%s"), *Primary.GetInputText().ToString(), *Secondary.GetInputText().ToString());
    return !HasAnyErrors();
}
#endif

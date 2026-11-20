// 职责：正式模型空闭包拒绝及原15语义的纯合同；Editor Automation入口，不生成假Mesh/Profile资产，不代表实际导入/射线/多人通过。
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceProfile.h"
#include "Debug/DemoLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoCasinoModelProfileContract,"FPSDemo.Casino.ModelProfile.SemanticsAndEmptyGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDemoCasinoModelProfileContract::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] Casino.ModelProfile.SemanticsAndEmptyGuard"));
    const UDemoCasinoDeviceProfile* Empty=NewObject<UDemoCasinoDeviceProfile>(); FString Error; // 仅瞬态默认定义，不伪造最终网格/源SHA或保存资产。
    TestFalse(TEXT("An unmeasured empty model cannot become a usable formal profile"),Empty->Validate(Error));
    TestFalse(TEXT("Missing source/mesh closure returns an actionable error"),Error.IsEmpty());
    for (int32 Digit=0;Digit<10;++Digit)
    {
        EDemoCasinoControlAction Action; int32 Index; const FName Id(*FString::Printf(TEXT("amount.digit.%d"),Digit)); // 正式/原型共用固定十位语义。
        TestTrue(TEXT("Every existing digit identity resolves"),DemoCasinoControls::ResolveSemantic(Id,Action,Index));
        TestTrue(TEXT("A digit cannot become an economic action"),Action==EDemoCasinoControlAction::AmountDigit && Index==Digit);
    }
    EDemoCasinoControlAction Action; int32 Index;
    TestTrue(TEXT("Actual lever keeps the original single PullSlots action"),DemoCasinoControls::ResolveSemantic(TEXT("slots.pull"),Action,Index) && Action==EDemoCasinoControlAction::PullSlots && Index==INDEX_NONE);
    for (const TCHAR* Invalid:{TEXT("amount.digit.10"),TEXT("amount.digit.-1"),TEXT("amount.digit.."),TEXT("slots.cash"),TEXT("device.delete")})
        TestFalse(TEXT("Unknown or ambiguous controls cannot invent permissions"),DemoCasinoControls::ResolveSemantic(FName(Invalid),Action,Index));
    return !HasAnyErrors();
}
#endif

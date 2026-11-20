// 职责：房间/Hub Profile字段认领、拒绝越权与冲突安全恢复的纯内存合同测试。
// 使用：未来独立Editor测试窗口运行FPSDemo.Rendering.AtmosphereContract；不建World、不读真实配置/存档，不代表RHI/游戏验收。
#include "Misc/AutomationTest.h"
#include "World/Rooms/DemoRoomAtmosphereProfile.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "Debug/DemoStaticLog.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoRoomAtmosphereContractTest,"FPSDemo.Rendering.AtmosphereContract",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Automation传入Parameters仅作测试上下文，不作为路径；只变独立瞬态Profile/Settings和值，不改配置CDO或资产。 */
bool FDemoRoomAtmosphereContractTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    AddExpectedError(TEXT("HUB_ATMO_RESTORE reason=ExternalFieldConflict"),EAutomationExpectedErrorFlags::Contains,1); // 下方主动制造一次外部冲突，不能忽略真实运行警告。
    AddExpectedError(TEXT("ROOM_ATMO_REJECT"),EAutomationExpectedErrorFlags::Contains,3); // 三种非法Profile必须触发生产拒绝。
    UDemoRoomAtmosphereProfile* Profile=NewObject<UDemoRoomAtmosphereProfile>(); // 本测试栈内同步使用，不跨帧/GC/异步。
    FPostProcessSettings Applied; FString Error; // 生产白名单输出和可核拒绝原因。
    TestTrue(TEXT("Default candidate builds"),Profile->BuildSettings(Applied,Error));
    FPostProcessSettings Original; Original.AutoExposureSpeedDown=2.f; // 原有无override值也要在恢复时完整还原。
    Original.DepthOfFieldFocalDistance=123.f; Original.bOverride_DepthOfFieldFocalDistance=true; // 非认领字段代表其他PP所有者。
    FPostProcessSettings Target=Original;
    TestTrue(TEXT("Apply validated owned fields"),UDemoRoomAtmosphereProfile::ApplyOwnedOverrides(Applied,Target));
    TestEqual(TEXT("Unowned value preserved"),Target.DepthOfFieldFocalDistance,123.f);
    TestTrue(TEXT("Unowned override preserved"),!!Target.bOverride_DepthOfFieldFocalDistance);
    TestTrue(TEXT("Restore clean fields"),UDemoRoomAtmosphereProfile::RestoreOwnedOverrides(Applied,Original,Target));
    TestEqual(TEXT("Underlying previous value restored"),Target.AutoExposureSpeedDown,2.f);
    TestFalse(TEXT("Previous unselected bit restored"),!!Target.bOverride_AutoExposureSpeedDown);

    UDemoRoomAtmosphereProfile::ApplyOwnedOverrides(Applied,Target);
    Target.BloomIntensity=0.27f; // 模拟另一个所有者在运行中更新同一认领字段，不允许整体快照覆盖。
    TestFalse(TEXT("Conflict reported"),UDemoRoomAtmosphereProfile::RestoreOwnedOverrides(Applied,Original,Target));
    TestEqual(TEXT("External bloom survives restore"),Target.BloomIntensity,0.27f);
    TestEqual(TEXT("Other owned fields still restored"),Target.AutoExposureSpeedDown,2.f);

    Profile->Overrides.bOverride_AutoExposureMinBrightness=false;
    TestFalse(TEXT("EV pair required"),Profile->BuildSettings(Applied,Error));
    TestFalse(TEXT("EV rejection has reason"),Error.IsEmpty());
    Profile->Overrides.bOverride_AutoExposureMinBrightness=true;
    Profile->Overrides.bOverride_DepthOfFieldFocalDistance=true;
    TestFalse(TEXT("Unknown override rejected"),Profile->BuildSettings(Applied,Error));
    Profile->Overrides.bOverride_DepthOfFieldFocalDistance=false;
    Profile->Overrides.BloomIntensity=0.6f;
    TestFalse(TEXT("Bloom budget enforced"),Profile->BuildSettings(Applied,Error));

    UDemoRoomStreamingSettings* Settings=NewObject<UDemoRoomStreamingSettings>(); // 独立配置对象，绝不GetMutableDefault或SaveConfig。
    Settings->Rooms.Reset(); Settings->RoomAtmospheres.Reset();
    Settings->Rooms.Add(TEXT("D03_L01"),FDemoRoomStreamDefinition());
    Settings->Rooms.Add(TEXT("R01"),FDemoRoomStreamDefinition());
    TestFalse(TEXT("No explicit L01 binding preserves legacy"),Settings->UsesPhaseAAtmosphere(TEXT("D03_L01")));
    Settings->RoomAtmospheres.Add(TEXT("D03_L01"),TSoftObjectPtr<UDemoRoomAtmosphereProfile>()); // 坏显式路径仍进入接管/REJECT，不静默当未启用。
    Settings->RoomAtmospheres.Add(TEXT("R01"),TSoftObjectPtr<UDemoRoomAtmosphereProfile>());
    TestTrue(TEXT("Explicit L01 selected for validation"),Settings->UsesPhaseAAtmosphere(TEXT("D03_L01")));
    TestFalse(TEXT("Old R01 never implicitly opted in"),Settings->UsesPhaseAAtmosphere(TEXT("R01")));
    return true;
}
#endif

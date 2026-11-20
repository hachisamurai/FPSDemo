// 职责：原生房间目录/槽选择/大图锚点边界合同；不加载地图、不BeginPlay、不建立物理/Nav/RHI或修改安装配置。
// 使用：Editor Automation RunTests FPSDemo.Rooms.PortContracts；负例有明确ROOM_REQUEST/ROOM_MARKERS Warning，不能称整个进程零告警。
// 依赖：生产Settings/MakeSnapshot与原有无Scene瞬态Editor World夹具；只证明输入校验/值输出，不证明10图玩法或多人已验。
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomAnchor.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Debug/DemoStaticLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoPortRoomSettingsContract,"FPSDemo.Rooms.PortContracts.Settings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters由Automation提供；独立Settings值只调用实际生产API，不写CDO/Config/资产。 */
bool FDemoPortRoomSettingsContract::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    TStrongObjectPtr<UDemoRoomStreamingSettings> Settings(NewObject<UDemoRoomStreamingSettings>(GetTransientPackage())); // 同步独占配置实例。
    Settings->bEnabled=true; Settings->Rooms.Reset(); Settings->SlotLocations={FVector(50000,0,10000),FVector(65000,0,10000)};
    FDemoRoomStreamDefinition Definition; // 固定既有地图身份，仅用于白名单值校验，本合同不加载它。
    Definition.Map=TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Demo/Rooms/L_R01.L_R01")));
    Definition.RequiredActorTags={TEXT("DemoRoomFloor")}; Settings->Rooms.Add(TEXT("R01"),Definition);
    FDemoRoomStreamRequest Request; FString Error; // 生产输出；失败不能被测试自行修正后冒充成功。
    TestTrue(TEXT("Legacy room still builds an installed request"),Settings->BuildRequest(TEXT("R01"),1,0,Request,Error));
    TestTrue(TEXT("Legacy first generation keeps original first slot"),Request.Transform.GetLocation().Equals(FVector(50000,0,10000)));
    TestTrue(TEXT("Legacy directory still has empty explicit marker contract"),Settings->Rooms.FindChecked(TEXT("R01")).SpawnPointTags.IsEmpty());

    Settings->SlotLocations[1]=FVector(100000,0,10000); // 显式作者安装空间，原请求形状未加任意位置字段。
    Definition.bUseAuthoredBounds=true; Definition.LocalBoundsMin=FVector(-12000,-8000,-500); Definition.LocalBoundsMax=FVector(12000,8000,2200);
    for(int32 Index=1;Index<=8;++Index) Definition.SpawnPointTags.Add(FName(*FString::Printf(TEXT("PortLockdown.Id.L01_Spawn_Spawn%02d"),Index)));
    Definition.BossSpawnTag=TEXT("PortLockdown.Id.L01_Boss_Boss"); Settings->Rooms.Add(TEXT("D03_L01"),Definition);
    TestTrue(TEXT("Authored dotted marker tags and finite large bounds are accepted"),Settings->BuildRequest(TEXT("D03_L01"),2,1,Request,Error));
    TestTrue(TEXT("Second generation uses the installed distant slot"),Request.Transform.GetLocation().Equals(FVector(100000,0,10000)));
    TestTrue(TEXT("Retry chooses opposite installed slot from actual active frame"),Settings->UseOtherSlot(FTransform(FVector(100000,0,10000)),Request,Error));
    TestTrue(TEXT("Opposite slot is original first slot"),Request.Transform.GetLocation().Equals(FVector(50000,0,10000)));

    FDemoRoomStreamRequest Spoof=Request; Spoof.Transform.SetLocation(FVector(75000,0,10000)); // 请求端未安装的位置不得被宽泛距离判定接受。
    TestFalse(TEXT("Arbitrary uninstalled client slot is rejected"),Settings->ValidateRequest(Spoof,Error));
    Definition.SpawnPointTags[7]=Definition.SpawnPointTags[0]; Settings->Rooms.Add(TEXT("D03_L01"),Definition);
    TestFalse(TEXT("Repeated precise marker cannot stand in for eight unique points"),Settings->BuildRequest(TEXT("D03_L01"),3,1,Request,Error));
    Definition.SpawnPointTags[7]=TEXT("/Game/Injected"); Settings->Rooms.Add(TEXT("D03_L01"),Definition);
    TestFalse(TEXT("Marker token cannot become a resource path"),Settings->BuildRequest(TEXT("D03_L01"),4,1,Request,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoPortRoomAnchorContract,"FPSDemo.Rooms.PortContracts.AnchorBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters不作文件路径；临时原生Anchor读取生产校验，World无Scene/物理/BeginPlay，最后由DestroyWorld统一清理。 */
bool FDemoPortRoomAnchorContract::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    const UWorld::InitializationValues Init=UWorld::InitializationValues().InitializeScenes(false).AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).EnableTraceCollision(false).CreateFXSystem(false).SetTransactional(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Editor,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("PortAnchorContract")),GetTransientPackage(),true,ERHIFeatureLevel::Num,&Init); // 同步独占Root World，不登记为用户当前地图。
    if(!TestNotNull(TEXT("Isolated native contract World exists"),World)) return false;
    FActorSpawnParameters Spawn; Spawn.OverrideLevel=World->PersistentLevel; Spawn.ObjectFlags=RF_Transient; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADemoRoomAnchor* Anchor=World->SpawnActor<ADemoRoomAnchor>(ADemoRoomAnchor::StaticClass(),FTransform::Identity,Spawn);
    if(!Anchor) { AddError(TEXT("Isolated native RoomAnchor creation failed")); World->DestroyWorld(false); return false; }
    FDemoAreaSnapshot Snapshot; FString Error;
    Anchor->CombatPlayerOffset=FVector(9000,0,100); // 大图真实远端入场；不能钳回40m通过旧门限。
    TestFalse(TEXT("Legacy API still rejects a distant point without installed bounds"),Anchor->MakeSnapshot(Snapshot,Error));
    const FBox Installed(FVector(-12000,-8000,-500),FVector(12000,8000,2200)); // 输入是明确可信安装预算，不是测试重算生产结果。
    TestTrue(TEXT("Same unchanged distant point passes explicit authored bounds"),Anchor->MakeSnapshot(Snapshot,Error,&Installed));
    TestTrue(TEXT("Output keeps all 90m rather than silently clamping geometry"),Snapshot.CombatStart.Equals(FVector(9000,0,100)));
    Anchor->CombatPlayerOffset=FVector(13000,0,100);
    TestFalse(TEXT("Authored room still rejects a point outside its installed envelope"),Anchor->MakeSnapshot(Snapshot,Error,&Installed));
    TestFalse(TEXT("Native fixture never entered gameplay"),World->HasBegunPlay());
    World->DestroyWorld(false);
    return true;
}
#endif

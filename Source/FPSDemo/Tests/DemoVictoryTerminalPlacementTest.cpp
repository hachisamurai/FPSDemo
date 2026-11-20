// 职责：独立Editor -game真实Hub/流送地面上的终端摆放技术回归；不启动刷怪、不保存槽、不把Combat夹具称为正常Flow清关。
// 使用：-game -DemoVictoryTerminalPlacementTest -GameUserSettingsINI="私有绝对路径.ini"
//       -ExecCmds="Automation RunTests FPSDemo.Interaction.VictoryTerminalPlacement" -TestExit="Automation Test Queue Empty"。
// 私有INI须含[FPSDemo.PortRoomVerification] PrivateIni=<同一绝对路径>；总60秒/单图30秒，末尾恢复Pawn/原Phase并正常回收自身实例。
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Characters/DemoCharacter.h"
#include "Components/BoxComponent.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "HAL/PlatformTime.h"
#include "Interaction/DemoInteractable.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Weapons/DemoWeaponComponent.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"

namespace
{
    /** 本显式进程唯一实际Standalone World；不打开地图或领取PIE。 */
    UWorld* FindTerminalTestWorld()
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        UWorld* Result=nullptr;
        if (GEngine) for (const FWorldContext& Context:GEngine->GetWorldContexts())
            if (UWorld* World=Context.World();World && World->WorldType==EWorldType::Game && !World->bIsTearingDown && World->GetNetMode()==NM_Standalone)
            { if (Result) return nullptr; Result=World; }
        return Result;
    }

    /** Automation拥有的真实物理夹具；游戏对象均弱引用，异常结束也只清自己已领取的服务。 */
    class FVictoryTerminalPlacementLatent final:public IAutomationLatentCommand
    {
    public:
        /** InTest为Automation队列借用；Started采用真实单调秒，总预算含恢复和回收。 */
        explicit FVictoryTerminalPlacementLatent(FAutomationTestBase& InTest):Test(InTest),Started(FPlatformTime::Seconds()) { DEMO_LOG_STATIC_CALL(nullptr); }
        /** latent被外部提前结束时亦撤销夹具，不退出或管理其他UE进程。 */
        virtual ~FVictoryTerminalPlacementLatent() override { DEMO_LOG_STATIC_CALL(nullptr); Restore(); }
        /** GT轮询原生流送/地面/Nav；Combat仅为明确位置快照fixture，不创建战斗Session或敌人。 */
        virtual bool Update() override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            const double Now=FPlatformTime::Seconds();
            if (Now-Started>=60.) { Fail(TEXT("60-second terminal fixture budget exhausted")); Restore(); return true; }
            if (Stage==3)
            {
                UWorld* World=TestWorld.Get(); UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get();
                ULevelStreaming* Streaming=TestInstance.Get();
                if (World && Rooms && !Rooms->HasActiveRoom() && !Rooms->GetPreparedLevel()
                    && (!Streaming || (!Streaming->GetLoadedLevel() && !World->GetStreamingLevels().Contains(Streaming))))
                {
                    if (!bFailed) UE_LOG(LogFPSDemo,Display,TEXT("VICTORY_TERMINAL_PLACEMENT_SUCCESS initial_hub_pair=passed actual_D03_ground_nav=passed nearby_pair=passed same_source_floor=passed cleanup=passed; EXPLICIT_COMBAT_FIXTURE_ONLY_FULL_FLOW_MULTIPLAYER_NOT_RUN"));
                    return true;
                }
                return false;
            }
            UWorld* World=FindTerminalTestWorld();
            if (Stage==0)
            {
                if (Now-Started>30.) { Fail(TEXT("actual initialized Lobby exceeded 30 seconds")); return false; }
                if (!World || !World->HasBegunPlay() || World->GetTimeSeconds()<1.f) return false;
                UGameInstance* GI=World->GetGameInstance();
                ADemoPlayerController* PC=GI?Cast<ADemoPlayerController>(GI->GetFirstLocalPlayerController()):nullptr;
                ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
                AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>();
                ADemoGameState* State=World->GetGameState<ADemoGameState>();
                UDemoRunSaves* Saves=GI?GI->GetSubsystem<UDemoRunSaves>():nullptr;
                UDemoRoomStreamingSubsystem* Rooms=World->GetSubsystem<UDemoRoomStreamingSubsystem>();
                if (!PC || !Player || !Mode || !State || !Saves || !Rooms || !Player->GetDemoASC() || !Player->GetDemoAttributes()
                    || !Player->GetWeaponComponent() || !Player->GetWeaponComponent()->GetActiveWeapon()) return false;
                if (!World->PersistentLevel || World->PersistentLevel->GetOutermost()->GetName()!=TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")
                    || !PC->HasAuthority() || !PC->IsLocalController() || State->Phase!=EDemoPhase::Lobby || Saves->GetActiveSlot()!=INDEX_NONE
                    || Saves->SlotExists(0) || Saves->SlotExists(1) || Saves->SlotExists(2) || Rooms->HasActiveRoom() || Rooms->GetPreparedLevel())
                { Fail(TEXT("requires actual parent map and fresh standalone Lobby without existing slots/rooms")); return false; }
                TestWorld=World; TestPlayer=Player; TestRooms=Rooms; TestTerminals=Mode->GetTerminalSystem(); TestState=State;
                OriginalPawn=Player->GetActorTransform(); OriginalPhase=State->Phase; OriginalLevel=State->LevelNumber; OriginalRevision=State->PhaseRevision;
                FDemoAreaSnapshot Hub; FString Error;
                if (!World->GetSubsystem<UDemoEncounterAreaSubsystem>()->ResolveArea(-1,Hub,Error) || !TestTerminals.IsValid()
                    || !TestTerminals->CreateTerminals(Hub) || !TestTerminals->GetShopTerminal() || !TestTerminals->GetNextLevelTerminal())
                { Fail(TEXT("initial authored Hub pair could not be safely created: ")+Error); return false; }
                TestTerminals->ClearTerminals();
                UE_LOG(LogFPSDemo,Display,TEXT("VICTORY_TERMINAL_TEST_HUB authored_initial_pair=passed no_slot_created=1"));
                FDemoRoomStreamRequest Request;
                const FName RoomId=bLevelNine?FName(TEXT("D03_L09")):FName(TEXT("D03_L01")); // 用正式配置流送对应房间。
                if (!GetDefault<UDemoRoomStreamingSettings>()->BuildRequest(RoomId,1,0,Request,Error)) { Fail(Error); return false; }
                TSet<const ULevelStreaming*> Before; for (const ULevelStreaming* Item:World->GetStreamingLevels()) Before.Add(Item); // 同GT冻结真实加入前列表。
                bOwnsRooms=true;
                if (!Rooms->BeginPrepare(Request,Error)) { Fail(Error); return false; }
                for (ULevelStreaming* Item:World->GetStreamingLevels()) if (Item && !Before.Contains(Item)) TestInstance=Item;
                RoomStarted=Now; Stage=1; return false;
            }
            if (!World || World!=TestWorld.Get() || !TestRooms.IsValid() || !TestPlayer.IsValid() || !TestState.IsValid() || !TestTerminals.IsValid())
            { Fail(TEXT("fixture World or actual Pawn changed")); return false; }
            if (Now-RoomStarted>30.) { Fail(TEXT("actual D03 room preparation exceeded 30 seconds: ")+WaitReason); return false; }
            if (Now<NextPoll) return false;
            NextPoll=Now+.1;
            FDemoAreaSnapshot Area; FString Error;
            const EDemoRoomLoadStatus Status=TestRooms->PollPrepared(Area,Error);
            if (Status==EDemoRoomLoadStatus::Pending) { WaitReason=Error; return false; }
            if (Status!=EDemoRoomLoadStatus::Ready || !Area.bRoomAnchors || Area.Geometry.ExplicitSourceLevelPackage.IsNone() || !TestRooms->CommitPrepared())
            { Fail(TEXT("actual D03 floor/Nav/commit failed: ")+Error); return false; }
            ADemoCharacter* Player=TestPlayer.Get(); ADemoGameState* State=TestState.Get();
            Player->GetCharacterMovement()->StopMovementImmediately();
            Player->SetActorLocationAndRotation(Area.CombatStart,FRotator::ZeroRotator,false,nullptr,ETeleportType::TeleportPhysics); // 明确测试摆放位置，不声称正常Flow传送/清关。
            State->Phase=EDemoPhase::Combat; State->LevelNumber=bLevelNine?9:1; ++State->PhaseRevision; bChangedState=true; // 有界技术夹具，Restore还原，不调用奖励/存档/生成系统。
            const FVector VictoryLocation=Player->GetActorLocation();
            TestTerminals->CaptureVictoryPlacementOrigin();
            if (!TestTerminals->CreateTerminals(Area)) { Fail(TEXT("near-victory pair production CreateTerminals failed")); return false; }
            const ADemoInteractable* Shop=TestTerminals->GetShopTerminal();
            const ADemoInteractable* Next=TestTerminals->GetNextLevelTerminal();
            if (!Shop || !Next || FVector::Dist2D(Shop->GetActorLocation(),VictoryLocation)>700.f || FVector::Dist2D(Next->GetActorLocation(),VictoryLocation)>700.f
                || FVector::Dist2D(Shop->GetActorLocation(),Next->GetActorLocation())<300.f || FVector::Dist2D(Shop->GetActorLocation(),Next->GetActorLocation())>500.f
                || !Player->GetActorLocation().Equals(VictoryLocation,.1f))
            { Fail(TEXT("actual pair is not close/separated or production placement moved the Pawn")); return false; }
            const UBoxComponent* Box=Cast<UBoxComponent>(GetDefault<ADemoInteractable>()->GetRootComponent());
            for (const ADemoInteractable* Terminal:{Shop,Next})
            {
                FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoVictoryTerminalProof),false); Query.AddIgnoredActor(Shop); Query.AddIgnoredActor(Next); Query.AddIgnoredActor(Player);
                FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
                FHitResult Hit;
                const FVector Bottom=Terminal->GetActorLocation()-FVector(0,0,Box?Box->GetScaledBoxExtent().Z:80.f);
                if (!Box || !World->LineTraceSingleByObjectType(Hit,Bottom+FVector(0,0,20),Bottom-FVector(0,0,40),Objects,Query) || Hit.bStartPenetrating
                    || !Hit.GetActor() || !Hit.GetActor()->GetLevel() || Hit.GetActor()->GetLevel()->GetOutermost()->GetFName()!=Area.Geometry.ExplicitSourceLevelPackage
                    || FMath::Abs(Hit.ImpactPoint.Z-(Bottom.Z-2.f))>2.f
                    || World->OverlapBlockingTestByProfile(Terminal->GetActorLocation(),FQuat::Identity,TEXT("BlockAllDynamic"),FCollisionShape::MakeBox(Box->GetScaledBoxExtent()),Query))
                { Fail(TEXT("actual spawned terminal lacks the real same-source floor or intersects environment")); return false; }
            }
            UE_LOG(LogFPSDemo,Display,TEXT("VICTORY_TERMINAL_TEST_NEAR victory=%s shop=%s next=%s source=%s distance_shop=%.2f distance_next=%.2f separation=%.2f"),*VictoryLocation.ToString(),*Shop->GetActorLocation().ToString(),*Next->GetActorLocation().ToString(),*Area.Geometry.ExplicitSourceLevelPackage.ToString(),FVector::Dist2D(VictoryLocation,Shop->GetActorLocation()),FVector::Dist2D(VictoryLocation,Next->GetActorLocation()),FVector::Dist2D(Shop->GetActorLocation(),Next->GetActorLocation()));
            if (bLevelNine)
            {
                TestTerminals->ClearTerminals();
                const FVector UnusableOrigin=VictoryLocation+FVector(0,0,10000); // 高于房间边界和脚下探测范围，强制没有可用的附近布局。
                Player->SetActorLocation(UnusableOrigin,false,nullptr,ETeleportType::TeleportPhysics);
                TestTerminals->CaptureVictoryPlacementOrigin();
                if (!TestTerminals->CreateTerminals(Area)) { Fail(TEXT("L09 authored fallback failed after forced nearby rejection")); return false; }
                Shop=TestTerminals->GetShopTerminal(); Next=TestTerminals->GetNextLevelTerminal();
                if (!Shop || !Next || FVector::Dist2D(Shop->GetActorLocation(),Area.ShopAnchor)>181.f
                    || FVector::Dist2D(Next->GetActorLocation(),Area.NextAnchor)>181.f || !Player->GetActorLocation().Equals(UnusableOrigin,.1f))
                { Fail(TEXT("L09 recovery moved the player or failed to deploy both terminals at this room's authored anchors")); return false; }
                UE_LOG(LogFPSDemo,Display,TEXT("L09_TERMINAL_FALLBACK_SUCCESS room=%s authored_pair=1 player_unmoved=1"),*Area.Geometry.ExplicitSourceLevelPackage.ToString());
            }
            Restore(); Stage=3; return false;
        }
    private:
        /** 首错留Automation和业务日志；同步还原后继续观察正常房间回收，不强制卸载。 */
        void Fail(const FString& Reason)
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            if (!bFailed) { bFailed=true; Test.AddError(Reason); UE_LOG(LogFPSDemo,Error,TEXT("VICTORY_TERMINAL_PLACEMENT_FAIL %s"),*Reason); }
            Restore(); Stage=3;
        }
        /** 只还原本次夹具领取的Pawn/Phase及服务；先回原Hub再Release，防原地面保护正确阻止卸载。 */
        void Restore()
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            if (bRestored) return;
            bRestored=true;
            if (TestTerminals.IsValid()) TestTerminals->ClearTerminals();
            if (bChangedState && TestState.IsValid()) { TestState->Phase=OriginalPhase; TestState->LevelNumber=OriginalLevel; TestState->PhaseRevision=OriginalRevision; }
            if (bChangedState && TestPlayer.IsValid()) { TestPlayer->GetCharacterMovement()->StopMovementImmediately(); TestPlayer->SetActorTransform(OriginalPawn,false,nullptr,ETeleportType::TeleportPhysics); }
            if (bOwnsRooms && TestRooms.IsValid()) TestRooms->ReleaseRooms();
        }
        FAutomationTestBase& Test; // Automation队列拥有，只借用错误接收器。
        const bool bLevelNine=FParse::Param(FCommandLine::Get(),TEXT("DemoLevelNineTerminals")); // 独立进程专测第九关的附近布点和回退。
        const double Started; // 总60真实秒，包含清理。
        TWeakObjectPtr<UWorld> TestWorld; // 原实际Game World；不接管替代World。
        TWeakObjectPtr<ADemoCharacter> TestPlayer; // 只移动/还原本fixture的实际Pawn。
        TWeakObjectPtr<ADemoGameState> TestState; // 显式临时Combat事实的恢复对象。
        TWeakObjectPtr<UDemoTerminalComponent> TestTerminals; // 本GameMode公开终端服务。
        TWeakObjectPtr<UDemoRoomStreamingSubsystem> TestRooms; // 只有fresh空服务允许领取。
        TWeakObjectPtr<ULevelStreaming> TestInstance; // 本次Begin加入的真实实例，最终等World移除。
        FTransform OriginalPawn; // 夹具前的实际Hub位姿。
        EDemoPhase OriginalPhase=EDemoPhase::Lobby; // 不持久化的原阶段。
        int32 OriginalLevel=0; // 不持久化的原关号。
        uint32 OriginalRevision=0; // 不持久化的原阶段版本。
        double RoomStarted=0.; // 单图30真实秒起点。
        double NextPoll=0.; // 10Hz原生准备观察。
        FString WaitReason; // 超时保留最后实际等待原因。
        int32 Stage=0; // 0真实初始化/Hub检查，1原生准备及一次摆放，3还原回收。
        bool bOwnsRooms=false; // 不对领取前的外来Rooms调用Release。
        bool bChangedState=false; // 只有确实设置过fixture才还原Pawn/阶段。
        bool bRestored=false; // 退出/析构共用一次还原。
        bool bFailed=false; // 失败不能被清理成功覆盖。
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoVictoryTerminalPlacementTest,"FPSDemo.Interaction.VictoryTerminalPlacement",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
/** Parameters由Automation提供；需要显式新进程和已实际加载的私有INI标记，普通PIE绝不触发fixture。 */
bool FDemoVictoryTerminalPlacementTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    FString SettingsINI,Marker; // 读取已生效的私有设置缓存，不把期望文件存在当加载证据。
    static bool bInvoked=false; // 每fresh进程只运行一次，不复用先前实例/phase。
    const FString Effective=FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"),nullptr,*FPaths::GeneratedConfigDir());
    if (bInvoked || !FParse::Param(FCommandLine::Get(),TEXT("game")) || !FParse::Param(FCommandLine::Get(),TEXT("DemoVictoryTerminalPlacementTest"))
        || !FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),SettingsINI) || FPaths::IsRelative(SettingsINI) || SettingsINI.IsEmpty()
        || !GConfig || !GConfig->GetString(TEXT("FPSDemo.PortRoomVerification"),TEXT("PrivateIni"),Marker,GGameUserSettingsIni)
        || !FPaths::IsSamePath(Marker,SettingsINI) || !FPaths::IsSamePath(Effective,SettingsINI)
        || FPaths::IsSamePath(SettingsINI,FPaths::GeneratedConfigDir()/TEXT("GameUserSettings.ini")))
    { AddError(TEXT("Requires a fresh Editor -game -DemoVictoryTerminalPlacementTest and loaded private GameUserSettingsINI with matching FPSDemo.PortRoomVerification.PrivateIni marker.")); return false; }
    bInvoked=true;
    ADD_LATENT_AUTOMATION_COMMAND(FVictoryTerminalPlacementLatent(*this));
    return true;
}
#endif

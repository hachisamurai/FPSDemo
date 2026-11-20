// 职责：在全新Editor -game进程中使用真实L06港区验证目标探地/两设施准备回归；不StartRun、不接GM目标广播、不移动Pawn或写存档。
// 使用：-game -DemoObjectiveRoomGroundTest -GameUserSettingsINI="私有绝对路径.ini"
//       -ExecCmds="Automation RunTests FPSDemo.Objectives.PortL06Ground" -TestExit="Automation Test Queue Empty"。
// 私有INI必须含[FPSDemo.PortRoomVerification] PrivateIni=其绝对路径；沿用港区流送测试隔离标记，不修改共享设置。
// 依赖：安装的revision3房间目录/真实L06地图、正式Rooms地面及完整Nav门禁、真实CombatSession和正式ObjectiveCoordinator。
// 范围：证明旧+300cm探地在L06地下设施处错层，验证PrepareObjective实际创建两设施、来源拒绝及正常回收；非完整关卡/AI/多人验收。
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Characters/DemoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "CoreGlobals.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Game/Objectives/DemoObjectiveCoordinatorComponent.h"
#include "Game/Objectives/DemoObjectiveDevice.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"

namespace
{
    constexpr double ObjectiveGroundBudgetSeconds=90.; // 含初始化/真实Nav构建及正常释放的总真实秒预算。
    constexpr double ObjectiveGroundStepSeconds=30.; // 初始化、准备、释放各自最多30真实秒，不改变生产门限。
    const TCHAR* const ObjectiveParentMap=TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox"); // 保留Hub的正式常驻地图。
    const TCHAR* const ObjectiveL06Map=TEXT("/Game/Maps/Candidates/PortLockdown20261117/D03_R01/L_D03_06"); // 只验证当前交付，不接收任意资源路径。

    /** 同步借用唯一Standalone Game World；不创建World或接管PIE/资产预览。 */
    UWorld* FindObjectiveGroundWorld()
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        UWorld* Result=nullptr; // 引擎拥有；多个实际游戏World时拒绝任选其一。
        if (GEngine)
            for (const FWorldContext& Context:GEngine->GetWorldContexts())
            {
                UWorld* Candidate=Context.World(); // 当前GT调用借用，不跨帧保存裸指针。
                if (!Candidate || Candidate->WorldType!=EWorldType::Game || Candidate->bIsTearingDown || Candidate->GetNetMode()!=NM_Standalone) continue;
                if (Result) return nullptr;
                Result=Candidate;
            }
        return Result;
    }

    /** Automation持有的有界GT状态机；房间由正式服务释放，独立Owner避免触发GM/Flow委托。 */
    class FObjectiveRoomGroundLatent final : public IAutomationLatentCommand
    {
    public:
        /** InTest由Automation队列持有至本命令结束；Started约束所有等待与清理。 */
        explicit FObjectiveRoomGroundLatent(FAutomationTestBase& InTest):Test(InTest),Started(FPlatformTime::Seconds())
        { DEMO_LOG_STATIC_CALL(nullptr); }

        /** 队列异常销毁也撤销本夹具对象/会话并请求正常释放；不强拆关卡或结束其它进程。 */
        virtual ~FObjectiveRoomGroundLatent() override
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            CleanupObjects();
            if (bOwnsRooms && !bReleaseRequested)
                if (UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get()) Rooms->ReleaseRooms();
        }

        /** 真实时间10Hz观察正式准备链；Ready后同步执行真实查询和PrepareObjective，任何失败均进入正常释放。 */
        virtual bool Update() override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            const double Now=FPlatformTime::Seconds(); // 单调真实秒，不受Lobby菜单暂停/TimeDilation影响。
            if (Now-Started>=ObjectiveGroundBudgetSeconds && Stage!=EStage::Cleanup) Fail(TEXT("total 90-second budget exhausted"));
            if (Stage==EStage::Cleanup) return PollCleanup(Now);
            if (Stage==EStage::Initialize)
            {
                if (Now-Started>=ObjectiveGroundStepSeconds) Fail(TEXT("fresh Lobby/Pawn initialization exceeded 30 seconds"));
                else Initialize(Now);
                return false;
            }
            UWorld* World=TestWorld.Get(); // 同World重验，地图旅行或退出不得接管新World。
            UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get();
            if (!World || World!=FindObjectiveGroundWorld() || !Rooms || !CheckIsolation(*World))
            { Fail(TEXT("fresh Lobby/Pawn/save/World isolation changed")); return false; }
            if (Now-PrepareStarted>=ObjectiveGroundStepSeconds)
            { Fail(TEXT("actual L06 preparation exceeded 30 seconds: ")+LastWait); return false; }
            if (Now<NextPoll) return false;
            NextPoll=Now+.1;
            FDemoAreaSnapshot Area; FString Error; // 正式Poll保持所属Level地板/完整非partial导航门禁；测试不改参数。
            const EDemoRoomLoadStatus Status=Rooms->PollPrepared(Area,Error);
            if (Status==EDemoRoomLoadStatus::Failed) { Fail(TEXT("actual L06 PollPrepared failed: ")+Error); return false; }
            if (Status==EDemoRoomLoadStatus::Pending) { LastWait=Error; return false; }
            if (!VerifyGroundAndPrepare(*World,*Rooms,Area,Error)) { Fail(Error); return false; }
            bVerified=true; BeginCleanup();
            return false;
        }

    private:
        enum class EStage:uint8 { Initialize,Preparing,Cleanup }; // 测试状态，不写游戏Phase。

        /** 冻结原Lobby/Pawn/保存水位后只领取空Rooms；Begin请求采用正式L06白名单/第二空间槽，不创建存档。 */
        void Initialize(double Now)
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            UWorld* World=FindObjectiveGroundWorld(); // 只读当前进程唯一真实游戏World。
            if (!World || !World->HasBegunPlay() || World->GetTimeSeconds()<1.f) return;
            UGameInstance* Instance=World->GetGameInstance();
            ADemoPlayerController* PC=Instance?Cast<ADemoPlayerController>(Instance->GetFirstLocalPlayerController()):nullptr;
            ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
            ADemoGameState* State=World->GetGameState<ADemoGameState>();
            UDemoRunSaves* Saves=Instance?Instance->GetSubsystem<UDemoRunSaves>():nullptr;
            UDemoRoomStreamingSubsystem* Rooms=World->GetSubsystem<UDemoRoomStreamingSubsystem>();
            if (!PC || !Player || !Player->HasActorBegunPlay() || !State || !Saves || !Rooms) return;
            if (!PC->HasAuthority() || !PC->IsLocalController() || !World->PersistentLevel || World->PersistentLevel->GetOutermost()->GetFName()!=FName(ObjectiveParentMap)
                || State->Phase!=EDemoPhase::Lobby || State->LevelNumber!=0 || Saves->GetActiveSlot()!=INDEX_NONE
                || Saves->SlotExists(0) || Saves->SlotExists(1) || Saves->SlotExists(2) || Rooms->HasActiveRoom() || Rooms->GetPreparedLevel())
            { Fail(TEXT("requires fresh authoritative Lobby in actual parent map without slots or rooms")); return; }
            TestWorld=World; TestPlayer=Player; TestRooms=Rooms;
            InitialLocation=Player->GetActorLocation(); InitialPhaseRevision=State->PhaseRevision; InitialSaveSerial=Saves->GetChangeSerial();
            for (TActorIterator<ADemoObjectiveDevice> It(World);It;++It)
                if (IsValid(*It)) { Fail(TEXT("fresh Lobby already contains an objective device")); return; }
            FDemoRoomStreamRequest Request; FString Error; // Generation6选择已安装第二槽，与原实玩失败的实际坐标一致；Arena仅旧兼容键。
            if (!GetDefault<UDemoRoomStreamingSettings>()->BuildRequest(TEXT("D03_L06"),6,0,Request,Error)
                || !Request.bRequireNavigation || FName(*Request.Map.GetLongPackageName())!=FName(ObjectiveL06Map))
            { Fail(TEXT("installed L06 request/source/full-navigation contract mismatch: ")+Error); return; }
            TSet<const ULevelStreaming*> Before; // 同一GT栈前后差集识别唯一自有实例，失败亦能观察正常释放。
            for (const ULevelStreaming* Streaming:World->GetStreamingLevels()) if (Streaming) Before.Add(Streaming);
            bOwnsRooms=true;
            if (!Rooms->BeginPrepare(Request,Error)) { Fail(TEXT("actual L06 BeginPrepare failed: ")+Error); return; }
            for (ULevelStreaming* Streaming:World->GetStreamingLevels())
                if (Streaming && !Before.Contains(Streaming)) OwnedInstances.Add(Streaming);
            if (OwnedInstances.Num()!=1) { Fail(TEXT("L06 BeginPrepare did not add exactly one actual streaming instance")); return; }
            TestTransform=Request.Transform; PrepareStarted=Now; NextPoll=Now; Stage=EStage::Preparing;
            UE_LOG(LogFPSDemo,Display,TEXT("OBJECTIVE_L06_GROUND_BEGIN source=%s slot=%s no_start_run=1 no_gm_subscriptions=1"),*Request.Map.ToString(),*Request.Transform.GetLocation().ToString());
        }

        /** 读取真实Level标记/物理结果并调用正式准备接口；不改点位、强制生成、代写Health或借旧World碰撞。 */
        bool VerifyGroundAndPrepare(UWorld& World,UDemoRoomStreamingSubsystem& Rooms,const FDemoAreaSnapshot& Area,FString& Error)
        {
            DEMO_LOG_STATIC_CALL(&World);
            ULevel* Level=Rooms.GetPreparedLevel(); // 服务持有；仅本同步验证栈借用。
            ULevelStreaming* Streaming=OwnedInstances.Num()==1?OwnedInstances[0].Get():nullptr;
            if (!Level || !Streaming || Streaming->GetLoadedLevel()!=Level || Streaming->PackageNameToLoad!=FName(ObjectiveL06Map)
                || !Streaming->IsLevelVisible() || !Area.bRoomAnchors || Area.DeviceAnchors.Num()!=2
                || Area.Geometry.ExplicitSourceLevelPackage!=Level->GetOutermost()->GetFName())
            { Error=TEXT("actual L06 prepared source/anchors/provenance mismatch"); return false; }
            const FVector ExpectedLocal[]={FVector(-800,-2100,-360),FVector(-600,2100,-360)}; // 已保存TA R03实图的两个地下设施地面；不是运行修正值。
            FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoObjectiveRoomGroundTest),false); // 原生真实物理；不忽略本图天花板。
            for (int32 Index=0;Index<2;++Index)
            {
                const FVector& Anchor=Area.DeviceAnchors[Index]; // 正式Rooms实际冻结的世界cm。
                const FName MarkerTag(*FString::Printf(TEXT("PortLockdown.Id.L06_Device_Device%d"),Index+1)); // 稳定作者标签，精确查本Level。
                const AActor* Marker=nullptr; // 不保存Actor或假造TargetPoint。
                for (const AActor* Actor:Level->Actors)
                    if (IsValid(Actor) && Actor->ActorHasTag(MarkerTag))
                    { if (Marker) { Error=TEXT("duplicate actual L06 device marker"); return false; } Marker=Actor; }
                if (!Marker || !Anchor.Equals(Marker->GetActorLocation(),.1f) || !TestTransform.InverseTransformPosition(Anchor).Equals(ExpectedLocal[Index],.1f))
                { Error=TEXT("L06 snapshot does not match actual saved underground device marker"); return false; }
                FHitResult LegacyHit; // 原生产+300/-500射线重现错误前提，但不执行旧版错误Spawn。
                const bool bLegacyHit=World.LineTraceSingleByObjectType(LegacyHit,Anchor+FVector(0,0,300),Anchor-FVector(0,0,500),FCollisionObjectQueryParams(ECC_WorldStatic),Query);
                const bool bLegacyWrongLayer=bLegacyHit && (LegacyHit.bStartPenetrating || FMath::Abs(LegacyHit.ImpactPoint.Z-Anchor.Z)>32.f);
                UE_LOG(LogFPSDemo,Display,TEXT("OBJECTIVE_L06_LEGACY_RAY index=%d anchor=%s start=%s hit=%d penetrating=%d actor=%s impact=%s normal=%s wrong_layer=%d"),Index,*Anchor.ToString(),*(Anchor+FVector(0,0,300)).ToString(),bLegacyHit,LegacyHit.bStartPenetrating,*GetNameSafe(LegacyHit.GetActor()),*LegacyHit.ImpactPoint.ToString(),*LegacyHit.ImpactNormal.ToString(),bLegacyWrongLayer);
                if (!bLegacyWrongLayer || !LegacyHit.GetActor() || LegacyHit.GetActor()->GetLevel()!=Level)
                { Error=TEXT("actual L06 physics did not reproduce the legacy initial-penetration/wrong-layer ray"); return false; }
            }
            FActorSpawnParameters Spawn; // 独立无碰撞Owner，未订阅GM/Flow；所有目标对象仍按生产DontSpawnIfColliding生成。
            Spawn.ObjectFlags|=RF_Transient;
            AActor* Owner=World.SpawnActor<AActor>(AActor::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
            if (!Owner) { Error=TEXT("isolated objective owner spawn failed"); return false; }
            TestOwner=Owner;
            UDemoObjectiveCoordinatorComponent* Objectives=NewObject<UDemoObjectiveCoordinatorComponent>(Owner);
            Owner->AddInstanceComponent(Objectives); Objectives->RegisterComponent(); TestObjectives=Objectives;
            Session.Reset(NewObject<UDemoCombatSession>(Owner)); // 强引用只覆盖本次latent；Initialize是真实公开入口，不伪造Active字段。
            if (!Session->Initialize(&World,TEXT("ObjectiveL06GroundFixture"),6)) { Error=TEXT("real combat session initialization failed"); return false; }
            FDemoObjectiveConfig Config; // 与Flow相同的准备输入，只设本测试的Sabotage；不激活、不发奖励/保存。
            Config.Kind=EDemoObjectiveKind::Sabotage; Config.DeviceAnchors=Area.DeviceAnchors; Config.SourceLevelPackage=Area.Geometry.ExplicitSourceLevelPackage;
            if (!Objectives->PrepareObjective(Config,Session.Get(),Error)) { Error=TEXT("actual L06 PrepareObjective failed: ")+Error; return false; }
            const FDemoObjectiveSnapshot& Snapshot=Objectives->GetSnapshot(); // 同步纯值观察，不给GameState挂订阅。
            if (Snapshot.State!=EDemoObjectiveState::Preparing || Snapshot.Total!=2 || Snapshot.Completed!=0)
            { Error=TEXT("actual sabotage did not remain in Preparing with exactly two devices"); return false; }
            TSet<int32> SeenAnchors; // 验证每个真实生成物对应不同作者点，不能以数量掩盖重复生成。
            for (TActorIterator<ADemoObjectiveDevice> It(&World);It;++It)
                if (IsValid(*It) && It->GetOwner()==Owner)
                {
                    OwnedDevices.Add(*It);
                    const FVector Ground=It->GetActorLocation()-FVector(0,0,91); // 生产180cm高设施中心抬高91cm，保留1cm净空。
                    const int32 Index=Ground.Equals(Area.DeviceAnchors[0],.1f)?0:Ground.Equals(Area.DeviceAnchors[1],.1f)?1:INDEX_NONE;
                    FHitResult Floor; // 从真实设施底部下探2cm，独立证实实际站立表面和Level，避免以配置自证。
                    UStaticMeshComponent* Body=It->FindComponentByClass<UStaticMeshComponent>();
                    const bool bFloor=World.LineTraceSingleByObjectType(Floor,Ground+FVector(0,0,1),Ground-FVector(0,0,2),FCollisionObjectQueryParams(ECC_WorldStatic),Query);
                    const FDemoObjectiveDeviceView View=It->GetStatusView(); // 实际GAS准备后的公开生命值。
                    if (Index==INDEX_NONE || SeenAnchors.Contains(Index) || !Body || !bFloor || Floor.bStartPenetrating || !Floor.GetActor()
                        || Floor.GetActor()->GetLevel()!=Level || FMath::Abs(Floor.ImpactPoint.Z-Ground.Z)>.1f
                        || FMath::Abs(Body->Bounds.Origin.Z-Body->Bounds.BoxExtent.Z-Ground.Z-1.f)>.2f
                        || !FMath::IsNearlyEqual(View.Health,Config.DeviceHealth) || !FMath::IsNearlyEqual(View.MaxHealth,Config.DeviceHealth))
                    { Error=TEXT("actual device duplicate/height/floor-source/clearance/GAS health mismatch"); return false; }
                    SeenAnchors.Add(Index);
                    UE_LOG(LogFPSDemo,Display,TEXT("OBJECTIVE_L06_DEVICE_VERIFIED index=%d actor=%s ground=%s floor=%s level=%s"),Index,*It->GetName(),*Ground.ToString(),*GetNameSafe(Floor.GetActor()),*Level->GetOutermost()->GetName());
                }
            if (OwnedDevices.Num()!=2 || SeenAnchors.Num()!=2) { Error=TEXT("actual sabotage did not create exactly two distinct live devices"); return false; }
            Objectives->CancelObjective(); // 回收正例后再检真实来源拒绝；不引入额外占地或影响下一个检查。
            Config.SourceLevelPackage=World.PersistentLevel->GetOutermost()->GetFName(); // 故意错用真实常驻包；不是伪造本关源包。
            FString Rejection; // 预期拒绝保留生产具体原因，不能把碰撞误拒绝当来源校验通过。
            if (Objectives->PrepareObjective(Config,Session.Get(),Rejection) || !Rejection.Contains(TEXT("归属")) || Objectives->GetSnapshot().State!=EDemoObjectiveState::SystemError)
            { Error=TEXT("actual PrepareObjective accepted the wrong source Level or omitted explicit failure"); return false; }
            for (TActorIterator<ADemoObjectiveDevice> It(&World);It;++It)
                if (IsValid(*It) && It->GetOwner()==Owner) { Error=TEXT("wrong-source rejection left an owned live device"); return false; }
            return true;
        }

        /** 保留原Pawn/位置/Lobby阶段及空存档；不会通过StartRun或传送人为满足准备门。 */
        bool CheckIsolation(const UWorld& World) const
        {
            DEMO_LOG_STATIC_TICK(&World);
            const UGameInstance* Instance=World.GetGameInstance();
            const ADemoPlayerController* PC=Instance?Cast<ADemoPlayerController>(Instance->GetFirstLocalPlayerController()):nullptr;
            const ADemoGameState* State=World.GetGameState<ADemoGameState>();
            const UDemoRunSaves* Saves=Instance?Instance->GetSubsystem<UDemoRunSaves>():nullptr;
            const ADemoCharacter* Player=TestPlayer.Get();
            return PC && Player && PC->GetPawn()==Player && Player->GetWorld()==&World && FVector::DistSquared(Player->GetActorLocation(),InitialLocation)<=FMath::Square(50.f)
                && State && State->Phase==EDemoPhase::Lobby && State->PhaseRevision==InitialPhaseRevision && State->LevelNumber==0 && State->EnemiesRemaining==0
                && Saves && Saves->GetActiveSlot()==INDEX_NONE && !Saves->SlotExists(0) && !Saves->SlotExists(1) && !Saves->SlotExists(2) && Saves->GetChangeSerial()==InitialSaveSerial;
        }

        /** 所有退出路径先取消独立目标，再撤销会话/销毁Owner；无需Flow参与，不销毁任何非本夹具Actor。 */
        void CleanupObjects()
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            if (UDemoObjectiveCoordinatorComponent* Objectives=TestObjectives.Get()) Objectives->CancelObjective();
            TestObjectives.Reset();
            if (Session.IsValid()) Session->Invalidate();
            Session.Reset();
            if (AActor* Owner=TestOwner.Get()) Owner->Destroy();
            TestOwner.Reset();
        }

        /** 首次失败保留原因并立即进入清理；不重试/放宽生产查询，不强制卸载房间。 */
        void Fail(const FString& Reason)
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            if (!bFailed) { bFailed=true; Test.AddError(Reason); UE_LOG(LogFPSDemo,Error,TEXT("OBJECTIVE_L06_GROUND_FAIL reason=%s"),*Reason); }
            BeginCleanup();
        }

        /** 正常释放只针对初始化确认原为空的Rooms，最多请求一次；清理仍受剩余总预算限制。 */
        void BeginCleanup()
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            Stage=EStage::Cleanup;
            if (bReleaseRequested) return;
            CleanupObjects(); bReleaseRequested=true; CleanupDeadline=FMath::Min(Started+ObjectiveGroundBudgetSeconds,FPlatformTime::Seconds()+ObjectiveGroundStepSeconds);
            if (bOwnsRooms) if (UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get()) Rooms->ReleaseRooms();
        }

        /** 等候原生服务真实移除候选实例并验证目标已销毁；失败同样清理，绝不Flush/GC或强拆脚下地面。 */
        bool PollCleanup(double Now)
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            UWorld* World=TestWorld.Get();
            UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get();
            bool bRemoved=!bOwnsRooms; // 领取之前失败不触及其它房间或World。
            if (bOwnsRooms && World && !World->bIsTearingDown && Rooms)
            {
                bRemoved=!Rooms->HasActiveRoom() && !Rooms->GetPreparedLevel();
                for (const TWeakObjectPtr<ULevelStreaming>& Weak:OwnedInstances)
                    if (const ULevelStreaming* Streaming=Weak.Get()) bRemoved&=!Streaming->GetLoadedLevel() && !World->GetStreamingLevels().Contains(Streaming);
            }
            for (const TWeakObjectPtr<ADemoObjectiveDevice>& Device:OwnedDevices) if (Device.IsValid()) bRemoved=false;
            if (!bRemoved && Now<CleanupDeadline) return false;
            if (!bRemoved || (!bFailed && (!bVerified || !World || !CheckIsolation(*World))))
            { if (!bFailed) Test.AddError(TEXT("actual room/device cleanup or final Lobby isolation not observed")); bFailed=true; }
            if (!bFailed) UE_LOG(LogFPSDemo,Display,TEXT("OBJECTIVE_L06_GROUND_SUCCESS actual_legacy_wrong_layer=2 actual_sabotage_devices=2 same_floor_and_source=passed wrong_source=rejected normal_release=passed elapsed=%.3f; FULL_FLOW_AI_MULTIPLAYER_NOT_RUN"),Now-Started);
            bOwnsRooms=false;
            return true;
        }

        FAutomationTestBase& Test; // Automation拥有，生命周期覆盖latent。
        const double Started; // 单调真实秒起点，预算含清理。
        TWeakObjectPtr<UWorld> TestWorld; // 当前真实Standalone World，不延长寿命或接管旅行后新World。
        TWeakObjectPtr<ADemoCharacter> TestPlayer; // 原Hub Pawn，仅观察，不移动/销毁。
        TWeakObjectPtr<UDemoRoomStreamingSubsystem> TestRooms; // 确认初始为空才领取。
        TArray<TWeakObjectPtr<ULevelStreaming>> OwnedInstances; // 本次Begin实际加入World的实例，正常释放后须真实移除。
        TArray<TWeakObjectPtr<ADemoObjectiveDevice>> OwnedDevices; // 正式组件创建的真实设备身份，只观察取消后销毁。
        TWeakObjectPtr<AActor> TestOwner; // 单独无碰撞Owner，World拥有；无GM/Flow订阅。
        TWeakObjectPtr<UDemoObjectiveCoordinatorComponent> TestObjectives; // 自有Owner持有，不与GM正式组件共享状态。
        TStrongObjectPtr<UDemoCombatSession> Session; // 本测试唯一可撤销凭据，覆盖GC；cleanup先Invalidate再释放。
        FTransform TestTransform; // 已安装正式请求值，仅转换实际标记以核作者坐标。
        FVector InitialLocation=FVector::ZeroVector; // 原Pawn世界cm，允许50cm物理沉降。
        uint32 InitialPhaseRevision=0; // 原Lobby阶段水位，不得改变。
        uint64 InitialSaveSerial=0; // 原GI保存水位，不得改变。
        double PrepareStarted=0.; // 本次真实L06准备起点秒。
        double NextPoll=0.; // 10Hz下一次导航/地板查询时刻秒。
        double CleanupDeadline=0.; // 清理最多30秒且不超过总90秒。
        FString LastWait; // 超时保留正式Poll最后等待原因。
        EStage Stage=EStage::Initialize; // 独立测试状态，不修改游戏Phase。
        bool bOwnsRooms=false; // 只释放本测试领取的初始空服务。
        bool bReleaseRequested=false; // 保证失败/析构不重复请求。
        bool bFailed=false; // 首错不可被清理成功覆盖。
        bool bVerified=false; // 真实查询及生产Prepare两设施均通过才设置。
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoObjectiveRoomGroundTest,"FPSDemo.Objectives.PortL06Ground",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)

/** Parameters不接受资产路径；只允许显式全新Editor -game和实际生效的私有设备INI，拒绝PIE/普通编辑器/共享设置。 */
bool FDemoObjectiveRoomGroundTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    FString SettingsINI; // 当前进程私有绝对文件路径，不写共享GameUserSettings。
    FString LoadedPrivateINI; // 从实际已加载Config缓存读回隔离标记，不能由期待文件自证。
    static bool bInvoked=false; // 一个新GI只运行一次，不复用流送代次或覆盖先前失败。
    const bool bHasOverride=FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),SettingsINI);
    const FString EffectiveINI=FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"),nullptr,*FPaths::GeneratedConfigDir()); // 引擎公开路径解析包含命令行覆盖。
    const bool bPrivateLoaded=GConfig && GConfig->FindConfigFile(GGameUserSettingsIni)
        && GConfig->GetString(TEXT("FPSDemo.PortRoomVerification"),TEXT("PrivateIni"),LoadedPrivateINI,GGameUserSettingsIni)
        && FPaths::IsSamePath(SettingsINI,LoadedPrivateINI) && FPaths::IsSamePath(SettingsINI,EffectiveINI);
    if (bInvoked || !FParse::Param(FCommandLine::Get(),TEXT("game")) || !FParse::Param(FCommandLine::Get(),TEXT("DemoObjectiveRoomGroundTest"))
        || !bHasOverride || SettingsINI.IsEmpty() || FPaths::IsRelative(SettingsINI) || !bPrivateLoaded
        || FPaths::IsSamePath(SettingsINI,FPaths::GeneratedConfigDir()/TEXT("GameUserSettings.ini"))
        || FPaths::IsSamePath(SettingsINI,FPaths::ProjectConfigDir()/TEXT("DefaultGameUserSettings.ini")))
    { AddError(TEXT("Requires fresh Editor -game -DemoObjectiveRoomGroundTest and active private absolute GameUserSettingsINI with matching [FPSDemo.PortRoomVerification] PrivateIni marker.")); return false; }
    bInvoked=true;
    ADD_LATENT_AUTOMATION_COMMAND(FObjectiveRoomGroundLatent(*this));
    return true;
}
#endif

// 职责：独立Editor游戏进程中的D03真实流送/物理地面/导航集成回归；只借用正式Rooms准备链，不创建槽、移动Pawn或推进战局。
// 使用：UnrealEditor FPSDemo.uproject /Game/Whitebox/Maps/L_ThreeSector_Whitebox -game -DemoPortRoomStreamingTest
//       -GameUserSettingsINI="本次私有绝对路径.ini" -ExecCmds="Automation RunTests FPSDemo.Rooms.PortLockdownStreaming" -TestExit="Automation Test Queue Empty"。
//       私有INI须含[FPSDemo.PortRoomVerification] PrivateIni=该绝对路径；任务run_verification.ps1负责创建并核实际配置缓存标记。
// 依赖：当前revision4 Campaign/19项房间目录、真实Game World/Pawn、BeginPrepare/PollPrepared/CommitPrepared及原生回收Ticker；revision3原通过源码保留在旧证据归档。
// 范围：11个请求覆盖10张地图，PollPrepared实际执行所属Level地面与完整导航路径检查。首关验证已提交的四人入场禁刷区与八个真实刷怪点。
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyNavigation.h"
#include "Components/SphereComponent.h"
#include "CoreGlobals.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "Game/Content/DemoCampaignDefinition.h"
#include "Game/Content/DemoCampaignSettings.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h" // UE5.4已知配置以逻辑名缓存；用实际缓存标记核私有INI加载，不把GGameUserSettingsIni误当物理文件名。
#include "Misc/Parse.h"
#include "UObject/Package.h" // 实读准备Level外包身份，验证floor来源桥；不持有或构造另一个包冒充来源。
#include "Misc/Paths.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Save/DemoRunSave.h"
#include "Weapons/DemoWeaponComponent.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"

namespace
{
    constexpr double PortTotalBudgetSeconds=180.; // 初始化、11次准备/回收及末尾释放的总真实秒上限。
    constexpr double PortRoomBudgetSeconds=30.; // 每个请求含准备和本次提交产生的旧实例回收，最多30真实秒。
    constexpr double PortPollSeconds=.1; // 与正式准备/回收频率相同；不逐帧反复查询Nav路径。
    const TCHAR* const PortParentMap=TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox"); // 只接受保留Hub的正式常驻地图。

    /** 根据固定验收序号生成稳定业务ID；最后两项是同图不同Boss身份，不修改安装目录。 */
    FName PortRoomId(int32 Index)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        return Index<9?FName(*FString::Printf(TEXT("D03_L%02d"),Index+1)):FName(Index==9?TEXT("D03_L10_Warden"):TEXT("D03_L10_Weaver"));
    }

    /** 只定义本次交付的源包期望；实际存在/加载身份另从真实Streaming对象及PollPrepared验证。 */
    FName PortMapPackage(int32 Index)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        return FName(*FString::Printf(TEXT("/Game/Maps/Candidates/PortLockdown20261117/D03_R01/L_D03_%02d"),FMath::Min(Index+1,10)));
    }

    /** 只寻找当前进程唯一Standalone Game World；不创建World、不接管PIE或Editor资产预览。 */
    UWorld* FindPortGameWorld()
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        UWorld* Result=nullptr; // 本次GT同步借用；多个Game World时拒绝任意挑选。
        if (!GEngine) return nullptr;
        for (const FWorldContext& Context:GEngine->GetWorldContexts())
        {
            UWorld* Candidate=Context.World(); // 引擎拥有，弱引用只在初始化成功后冻结。
            if (!Candidate || Candidate->WorldType!=EWorldType::Game || Candidate->bIsTearingDown || Candidate->GetNetMode()!=NM_Standalone) continue;
            if (Result) return nullptr;
            Result=Candidate;
        }
        return Result;
    }

    /** 只读公开World列表/LoadedLevel，等待正式Ticker真正移除本测试已记录的实例；不改其加载状态。 */
    bool PortInstanceRemoved(const UWorld& World,const TWeakObjectPtr<ULevelStreaming>& Instance)
    {
        DEMO_LOG_STATIC_TICK(&World);
        const ULevelStreaming* Streaming=Instance.Get(); // World持有时不会由此弱引用延长寿命。
        return !Streaming || (!Streaming->GetLoadedLevel() && !World.GetStreamingLevels().Contains(Streaming));
    }

    /** Automation拥有的有界GT状态机；所有跨帧游戏对象均为弱引用，失败只释放本测试领取的Rooms实例。 */
    class FPortRoomStreamingLatent final : public IAutomationLatentCommand
    {
    public:
        /** InTest由Automation队列持有至本命令结束；单调Started同时约束初始化、准备和清理。 */
        explicit FPortRoomStreamingLatent(FAutomationTestBase& InTest):Test(InTest),Started(FPlatformTime::Seconds())
        { DEMO_LOG_STATIC_CALL(nullptr); }

        /** 队列异常提前销毁时也请求自有服务正常释放；不强制卸载、不退出进程、不接触其他World。 */
        virtual ~FPortRoomStreamingLatent() override
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            if (bOwnsRooms && !bReleaseRequested)
                if (UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get()) Rooms->ReleaseRooms();
        }

        /** GT按真实时钟轮询；使用正式准备/提交/回收顺序，任何失败都不修改私有状态或跳过地面/Nav。 */
        virtual bool Update() override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            const double Now=FPlatformTime::Seconds(); // 真实秒，不受菜单/World暂停或固定时间步影响。
            if (Now-Started>=PortTotalBudgetSeconds && Stage!=EStage::Cleanup)
                Fail(TEXT("total 180-second budget exhausted"));
            if (Stage==EStage::Cleanup) return UpdateCleanup(Now);
            if (Stage==EStage::Initialize)
            {
                if (Now-Started>=PortRoomBudgetSeconds) { Fail(TEXT("actual fresh Lobby/Pawn initialization exceeded 30 seconds")); return false; }
                return Initialize(Now);
            }

            UWorld* World=TestWorld.Get(); // 每帧重新验证同World，旅行/退出不得接管替代World。
            UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get();
            if (!World || World->bIsTearingDown || World!=FindPortGameWorld() || !Rooms || Rooms->GetWorld()!=World)
            { Fail(TEXT("owned Game World or Rooms changed during streaming integration")); return false; }
            if (!CheckIsolation(*World)) { Fail(TEXT("fresh Lobby/Pawn/save isolation changed during streaming integration")); return false; }
            if (Now-RoomStarted>=PortRoomBudgetSeconds)
            { Fail(FString::Printf(TEXT("room %s exceeded 30 seconds: %s"),*CurrentRequest.RoomId.ToString(),*LastWaitReason)); return false; }
            if (Now<NextPoll) return false;
            NextPoll=Now+PortPollSeconds;

            if (Stage==EStage::Begin)
            {
                const UDemoRoomStreamingSettings* Settings=GetDefault<UDemoRoomStreamingSettings>(); // 安装配置只读；不复制一份有利于测试的目录。
                FString Error; // 生产拒绝原因原样保留，不把Retiring失败当作可跳过步骤。
                if (!Settings->BuildRequest(PortRoomId(RoomIndex),RoomIndex+1,ArenaIndices[RoomIndex],CurrentRequest,Error)
                    || (Rooms->HasActiveRoom() && !Settings->UseOtherSlot(LastArea.Transform,CurrentRequest,Error)))
                { Fail(TEXT("installed BuildRequest/UseOtherSlot failed: ")+Error); return false; }
                if (Rooms->HasActiveRoom() && CurrentRequest.Transform.GetLocation().Equals(LastArea.Transform.GetLocation()))
                { Fail(TEXT("next request did not use the other installed slot")); return false; }
                TSet<const ULevelStreaming*> Before; // 同一GT调用前冻结实际列表，识别本次生产Begin加入的唯一实例，连加载失败退出也可观察其正常回收。
                for (const ULevelStreaming* Streaming:World->GetStreamingLevels()) if (Streaming) Before.Add(Streaming);
                if (!Rooms->BeginPrepare(CurrentRequest,Error)) { Fail(TEXT("actual BeginPrepare failed: ")+Error); return false; }
                CurrentInstance.Reset();
                for (ULevelStreaming* Streaming:World->GetStreamingLevels())
                    if (Streaming && !Before.Contains(Streaming))
                    {
                        OwnedInstances.Add(Streaming);
                        if (CurrentInstance.IsValid()) { Fail(TEXT("one BeginPrepare added more than one streaming instance")); return false; }
                        CurrentInstance=Streaming;
                    }
                if (!CurrentInstance.IsValid()) { Fail(TEXT("successful BeginPrepare did not add an actual World streaming instance")); return false; }
                Stage=EStage::Preparing; LastWaitReason.Reset();
                UE_LOG(LogFPSDemo,Display,TEXT("PORT_ROOM_STREAMING_BEGIN index=%d room=%s source=%s slot=%s"),RoomIndex+1,*CurrentRequest.RoomId.ToString(),*CurrentRequest.Map.ToString(),*CurrentRequest.Transform.GetLocation().ToString());
                return false;
            }
            if (Stage==EStage::Preparing)
            {
                FDemoAreaSnapshot Area; FString Error; // PollPrepared真正检查本实例Actor、地面、四人落点及非partial导航路径。
                const EDemoRoomLoadStatus Status=Rooms->PollPrepared(Area,Error);
                if (Status==EDemoRoomLoadStatus::Failed) { Fail(TEXT("actual PollPrepared failed: ")+Error); return false; }
                if (Status==EDemoRoomLoadStatus::Pending)
                {
                    if (Error!=LastWaitReason) { LastWaitReason=Error; UE_LOG(LogFPSDemo,Log,TEXT("PORT_ROOM_STREAMING_WAIT room=%s reason=%s"),*CurrentRequest.RoomId.ToString(),*Error); }
                    return false;
                }
                ULevelStreaming* Actual=nullptr; // 只从World现存列表定位已准备Level，不创建或直接操纵Streaming对象。
                ULevel* PreparedLevel=Rooms->GetPreparedLevel();
                for (ULevelStreaming* Streaming:World->GetStreamingLevels())
                    if (Streaming && PreparedLevel && Streaming->GetLoadedLevel()==PreparedLevel)
                    {
                        if (Actual) { Fail(TEXT("prepared Level belongs to multiple streaming objects")); return false; }
                        Actual=Streaming;
                    }
                if (!Actual || Actual!=CurrentInstance.Get() || !PreparedLevel || PreparedLevel->GetWorld()!=World || !Actual->IsLevelVisible()
                    || Actual->PackageNameToLoad!=PortMapPackage(RoomIndex) || !Actual->LevelTransform.Equals(CurrentRequest.Transform,.1f)
                    || CurrentRequest.RoomId!=PortRoomId(RoomIndex) || FName(*CurrentRequest.Map.GetLongPackageName())!=PortMapPackage(RoomIndex)
                    || !CurrentRequest.bRequireNavigation || !Area.bRoomAnchors || Area.Geometry.ExplicitSpawnOffsets.Num()!=8
                    || Area.Geometry.ExplicitSourceLevelPackage!=PreparedLevel->GetOutermost()->GetFName()
                    || Area.DeviceAnchors.Num()!=2 || Area.ControlAnchors.Num()!=3 || !Area.Transform.Equals(CurrentRequest.Transform,.1f))
                { Fail(TEXT("actual prepared source/slot/identity/room anchors/eight explicit floor contacts mismatch")); return false; }
                const UDemoRoomStreamingSettings* Settings=GetDefault<UDemoRoomStreamingSettings>();
                if (!Area.Transform.GetLocation().Equals(Settings->SlotLocations[0]) && !Area.Transform.GetLocation().Equals(Settings->SlotLocations[1]))
                { Fail(TEXT("actual area does not belong to either installed slot")); return false; }
                for (const FVector& Point:Area.Geometry.ExplicitSpawnOffsets)
                    if (Point.ContainsNaN()) { Fail(TEXT("actual explicit spawn floor contact contains a non-finite value")); return false; }
                PreviousInstance=LastInstance; // Commit之后只等待原Active对应的真实实例回收，不以新Active状态冒充旧实例已移除。
                if (!Rooms->CommitPrepared()) { Fail(TEXT("actual CommitPrepared rejected the ready room")); return false; }
                bOwnsRooms=true; LastInstance=Actual;
                FDemoAreaSnapshot Active; // 活动只读输出必须等于刚通过物理/Nav的真实准备值。
                if (!Rooms->HasActiveRoom() || !Rooms->GetActiveArea(CurrentRequest.ArenaIndex,Active,Error)
                    || !Active.Transform.Equals(Area.Transform,.1f) || Active.Geometry.ExplicitSpawnOffsets!=Area.Geometry.ExplicitSpawnOffsets
                    || Active.Geometry.BossOffset!=Area.Geometry.BossOffset || Active.Geometry.ExplicitSourceLevelPackage!=Area.Geometry.ExplicitSourceLevelPackage
                    || Active.Geometry.ProtectedPlayerEntryPoints!=Area.Geometry.ProtectedPlayerEntryPoints
                    || Active.Geometry.PlayerEntryExclusionRadiusCm!=Area.Geometry.PlayerEntryExclusionRadiusCm
                    || !Active.bRoomAnchors || Active.DeviceAnchors!=Area.DeviceAnchors || Active.ControlAnchors!=Area.ControlAnchors)
                { Fail(TEXT("committed active area differs from the actual prepared snapshot: ")+Error); return false; }
                if (RoomIndex==0 && !CheckFirstRoomEntryGuard(Active,Error))
                { Fail(Error); return false; }
                if (CurrentRequest.RoomId==TEXT("d03_l05")
                    && (!CheckFifthRoomStairs(*World,Active,-5460.) || !CheckFifthRoomStairs(*World,Active,4440.)))
                { Fail(TEXT("actual L05 enemy sphere failed staircase ascent/descent")); return false; }
                LastArea=Active; ++PassedRooms; Stage=EStage::Retiring;
                UE_LOG(LogFPSDemo,Display,TEXT("PORT_ROOM_STREAMING_COMMITTED room=%s source=%s explicit_points=%d elapsed=%.3f native_floor_nav=passed"),*CurrentRequest.RoomId.ToString(),*Actual->PackageNameToLoad.ToString(),Active.Geometry.ExplicitSpawnOffsets.Num(),Now-RoomStarted);
                return false;
            }
            if (!PortInstanceRemoved(*World,PreviousInstance)) return false; // 由原生Ticker卸载并移出World；不因Retiring等待而篡改服务状态。
            UE_LOG(LogFPSDemo,Log,TEXT("PORT_ROOM_STREAMING_RETIRED after=%s previous_instance_removed=1"),*CurrentRequest.RoomId.ToString());
            ++RoomIndex;
            if (RoomIndex==11) { BeginCleanup(); return false; }
            RoomStarted=Now; Stage=EStage::Begin; LastWaitReason=TEXT("waiting to begin next installed request");
            return false;
        }

    private:
        /** 在实图提交后检验四名玩家落点、禁刷边界和八个作者点。原玩家仍在大厅，结果必须与当前位置无关。 */
        bool CheckFirstRoomEntryGuard(const FDemoAreaSnapshot& Area,FString& Error) const
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            const FDemoSpawnGeometry& Geometry=Area.Geometry; // 提交后的只读快照，调用和实际刷怪相同的生产筛选。
            if (Geometry.PlayerEntryExclusionRadiusCm!=1200.f || Geometry.ProtectedPlayerEntryPoints.Num()!=4)
            { Error=TEXT("L01 did not retain its 1200cm exclusion around four entry slots"); return false; }
            for (int32 Index=0;Index<4;++Index)
            {
                const FVector Expected=Area.CombatStart+FVector(0,Index*120.,0); // 与实图出生锚及约定的四人世界Y队形独立对照。
                if (!Geometry.ProtectedPlayerEntryPoints[Index].Equals(Expected,.1)
                    || !Area.GetCombatLandingPoint(Index).Equals(Expected,.1)
                    || Geometry.IsOutsidePlayerEntryZone(Expected))
                { Error=FString::Printf(TEXT("L01 entry slot %d is missing or unprotected"),Index); return false; }
            }
            const FVector Last=Geometry.ProtectedPlayerEntryPoints.Last(); // 第四槽外侧单独验证第三、四槽之间的保护差异及XY判定。
            if (Geometry.IsOutsidePlayerEntryZone(Last+FVector(0,1199.,1000.))
                || !Geometry.IsOutsidePlayerEntryZone(Last+FVector(0,1200.,0)))
            { Error=TEXT("L01 entry radius boundary or planar distance differs at fourth slot"); return false; }
            for (int32 Index=0;Index<Geometry.ExplicitSpawnOffsets.Num();++Index)
            {
                const FVector Floor=Area.Transform.TransformPosition(Geometry.ExplicitSpawnOffsets[Index]); // 实际流送世界地面点，不从原始JSON读取。
                const bool bExpectedOutside=Index!=3 && Index!=7; // 当前地图的Spawn04、08位于入口带，其他六点保存原作者位置。
                if (Geometry.IsOutsidePlayerEntryZone(Floor)!=bExpectedOutside)
                { Error=FString::Printf(TEXT("L01 Spawn%02d entry exclusion differs at %s"),Index+1,*Floor.ToCompactString()); return false; }
            }
            UE_LOG(LogFPSDemo,Display,TEXT("L01_ENTRY_ZONE_VERIFIED slots=4 radius_cm=1200 markers_blocked=04,08 other_markers_outside=6 pawn_at_lobby=1"));
            return true;
        }
        /** StairX 为五关东西梯中心线的局部 X（cm），用原生48cm根球从320cm平台下行并返回。 */
        bool CheckFifthRoomStairs(UWorld& World,const FDemoAreaSnapshot& Area,double StairX)
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            const FVector Start=Area.Transform.TransformPosition(FVector(StairX,4340,370)); // 地图上平台，球底净空2cm。
            ADemoEnemy* Probe=World.SpawnActor<ADemoEnemy>(Start,FRotator::ZeroRotator);
            if (!Probe) return false;
            Probe->SetActorTickEnabled(false); Probe->SetActorEnableCollision(true);
            auto* Nav=Probe->FindComponentByClass<UDemoEnemyNavigation>();
            Nav->BindSpawnFloor(Area.Geometry.ExplicitSourceLevelPackage);
            const FVector Forward=Area.Transform.TransformVectorNoScale(FVector(0,-12,0)); // cm，每次小于一阶深度。
            bool bOK=true;
            for (int32 Pass=0;Pass<2 && bOK;++Pass)
            {
                FHitResult Block;
                for (int32 Step=0;Step<92 && bOK;++Step) bOK=Nav->MoveGrounded(Pass==0?Forward:-Forward,Block);
                const FVector Local=Area.Transform.InverseTransformPosition(Probe->GetActorLocation());
                UE_LOG(LogFPSDemo,Display,TEXT("L05_ENEMY_STAIRS pass=%d end=%s blocker=%s"),Pass,*Local.ToCompactString(),*GetNameSafe(Block.GetActor()));
                bOK &= FMath::Abs(Local.Z-(Pass==0?50.:370.))<3. && FMath::Abs(Local.Y-(Pass==0?3236.:4340.))<2.;
            }
            Probe->Destroy(); return bOK;
        }
        enum class EStage:uint8 { Initialize,Begin,Preparing,Retiring,Cleanup }; // 只控制夹具，不修改游戏Phase或生产回收状态。

        /** 初始化完成前仅观察；冻结实际Pawn/保存水位后验证安装目录和默认/GM优先的真实Campaign。 */
        bool Initialize(double Now)
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            UWorld* World=FindPortGameWorld(); // 当前唯一实际Game World，不运行AutomationOpenMap或新建World。
            if (!World || !World->HasBegunPlay() || World->GetTimeSeconds()<1.f) return false;
            UGameInstance* Instance=World->GetGameInstance();
            ADemoPlayerController* PC=Instance?Cast<ADemoPlayerController>(Instance->GetFirstLocalPlayerController()):nullptr;
            ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
            ADemoPlayerState* PS=PC?PC->GetPlayerState<ADemoPlayerState>():nullptr;
            AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>();
            ADemoGameState* State=World->GetGameState<ADemoGameState>();
            UDemoRunSaves* Saves=Instance?Instance->GetSubsystem<UDemoRunSaves>():nullptr;
            UDemoRoomStreamingSubsystem* Rooms=World->GetSubsystem<UDemoRoomStreamingSubsystem>();
            if (!PC || !Player || !PS || !Mode || !State || !Saves || !Rooms || !Player->GetDemoASC() || !Player->GetDemoAttributes()
                || !Player->GetWeaponComponent() || !Player->GetWeaponComponent()->GetActiveWeapon() || !Player->HasActorBegunPlay()) return false;
            if (!World->PersistentLevel || World->PersistentLevel->GetOutermost()->GetFName()!=FName(PortParentMap)
                || !PC->IsLocalController() || !PC->HasAuthority() || !Player->IsLocallyControlled()
                || PS->GetDemoASC()!=Player->GetDemoASC() || Player->GetDemoASC()->GetOwnerActor()!=PS || Player->GetDemoASC()->GetAvatarActor()!=Player
                || State->Phase!=EDemoPhase::Lobby || State->LevelNumber!=0 || Saves->GetActiveSlot()!=INDEX_NONE
                || Saves->SlotExists(0) || Saves->SlotExists(1) || Saves->SlotExists(2) || Rooms->HasActiveRoom() || Rooms->GetPreparedLevel())
            { Fail(TEXT("requires initialized authoritative Pawn in the actual parent map and fresh Lobby with no slots/rooms")); return false; }
            TestWorld=World; TestPlayer=Player; TestRooms=Rooms; InitialLocation=Player->GetActorLocation();
            InitialPhaseRevision=State->PhaseRevision; InitialSaveSerial=Saves->GetChangeSerial();
            FString Error; // 生产作者校验原因原样写入失败证据，测试不创建替代Campaign。
            const UDemoRoomStreamingSettings* Settings=GetDefault<UDemoRoomStreamingSettings>();
            UDemoCampaignDefinition* Configured=GetDefault<UDemoCampaignSettings>()->LoadCampaign(Error);
            if (!Configured || !ValidateCampaign(*Configured,Error)) { Fail(TEXT("project default Campaign is not the installed revision4: ")+Error); return false; }
            if (Mode->DemoCampaign && !ValidateCampaign(*Mode->DemoCampaign,Error))
            { Fail(TEXT("GM explicit campaign overrides project default: ")+Error); return false; }
            const UDemoCampaignDefinition* Effective=Mode->DemoCampaign?Mode->DemoCampaign.Get():Configured; // 与Flow相同的优先级，仅借用读取区域兼容键。
            if (!Settings->bEnabled || Settings->MinimumContentRevision!=2 || Settings->Rooms.Num()!=19 || Settings->SlotLocations.Num()!=2
                || !Settings->SlotLocations[0].Equals(FVector(50000,0,10000)) || !Settings->SlotLocations[1].Equals(FVector(100000,0,10000)))
            { Fail(FString::Printf(TEXT("installed room directory/revision gate/two explicit slots mismatch enabled=%d minimum=%d rooms=%d slots=%d first=%s second=%s"),Settings->bEnabled,Settings->MinimumContentRevision,Settings->Rooms.Num(),Settings->SlotLocations.Num(),Settings->SlotLocations.Num()>0?*Settings->SlotLocations[0].ToString():TEXT("missing"),Settings->SlotLocations.Num()>1?*Settings->SlotLocations[1].ToString():TEXT("missing"))); return false; }
            const TCHAR* const OldIds[]={TEXT("R01"),TEXT("R02"),TEXT("R03"),TEXT("R04"),TEXT("R05"),TEXT("R06"),TEXT("B01"),TEXT("B02")}; // 旧冻档八身份必须继续使用原包，不重映射至新十图。
            for (const TCHAR* Id:OldIds)
            {
                const FDemoRoomStreamDefinition* Old=Settings->Rooms.Find(FName(Id));
                if (!Old || FName(*Old->Map.ToSoftObjectPath().GetLongPackageName())!=FName(*FString::Printf(TEXT("/Game/Demo/Rooms/L_%s"),Id))
                    || Old->bUseAuthoredBounds || !Old->SpawnPointTags.IsEmpty() || !Old->BossSpawnTag.IsNone())
                { Fail(FString::Printf(TEXT("legacy frozen room %s no longer preserves its original package/geometry contract"),Id)); return false; }
            }
            for (int32 Index=0;Index<11;++Index)
            {
                const FName Id=PortRoomId(Index); // 只查已安装的实际新目录，不临时Add新定义。
                const FDemoRoomStreamDefinition* Definition=Settings->Rooms.Find(Id);
                const int32* Arena=Effective->RoomAreas.Find(Id);
                if (!Definition || !Arena || *Arena<0 || *Arena>2 || !Definition->bUseAuthoredBounds || !Definition->bRequireNavigation
                    || Definition->SpawnPointTags.Num()!=8 || Definition->BossSpawnTag.IsNone()
                    || FName(*Definition->Map.ToSoftObjectPath().GetLongPackageName())!=PortMapPackage(Index))
                { Fail(TEXT("installed D03 room has incorrect source/arena/native navigation/marker contract: ")+Id.ToString()); return false; }
                ArenaIndices.Add(*Arena);
            }
            bOwnsRooms=true; RoomStarted=Now; NextPoll=Now; Stage=EStage::Begin;
            UE_LOG(LogFPSDemo,Display,TEXT("PORT_ROOM_STREAMING_INITIALIZED world=%s pawn=%s position=%s default_revision=%d gm_override=%d legacy_rooms=8 slots=2 no_slot_or_phase_mutation=1"),*World->GetName(),*Player->GetName(),*InitialLocation.ToString(),Effective->ContentRevision,Mode->DemoCampaign!=nullptr);
            return false;
        }

        /** 同步检验真实资产的稳定身份/十关池与Boss行；不由构造默认值或期望列表自己证明地图Actor存在。 */
        bool ValidateCampaign(const UDemoCampaignDefinition& Definition,FString& Error) const
        {
            DEMO_LOG_STATIC_CALL(&Definition);
            if (!Definition.Validate(Error)) return false;
            if (Definition.CampaignId!=TEXT("steam_demo") || Definition.ContentRevision!=4 || Definition.Stages.Num()!=10)
            { Error=TEXT("expected steam_demo revision4 and ten stages"); return false; }
            for (int32 Index=0;Index<9;++Index)
                if (Definition.Stages[Index].RoomPool.Num()!=1 || Definition.Stages[Index].RoomPool[0]!=PortRoomId(Index))
                { Error=FString::Printf(TEXT("unexpected stage %d pool"),Index+1); return false; }
            const TArray<FName>& BossPool=Definition.Stages[9].RoomPool; // 作者顺序冻结，避免改序影响同Seed计划。
            const FName* Warden=Definition.RoomBossRows.Find(PortRoomId(9));
            const FName* Weaver=Definition.RoomBossRows.Find(PortRoomId(10));
            if (BossPool.Num()!=2 || BossPool[0]!=PortRoomId(9) || BossPool[1]!=PortRoomId(10)
                || !Warden || *Warden!=TEXT("Warden") || !Weaver || *Weaver!=TEXT("Weaver"))
            { Error=TEXT("stage10 aliases/order/Boss rows differ from the installed D03 contract"); return false; }
            return true;
        }

        /** 只读确认本测试始终保留原Pawn/常驻位置、Lobby阶段及空存档；不以StartRun或传送制造准备条件。 */
        bool CheckIsolation(const UWorld& World) const
        {
            DEMO_LOG_STATIC_TICK(&World);
            const ADemoGameState* State=World.GetGameState<ADemoGameState>();
            const UGameInstance* Instance=World.GetGameInstance();
            const UDemoRunSaves* Saves=Instance?Instance->GetSubsystem<UDemoRunSaves>():nullptr;
            const ADemoCharacter* Player=TestPlayer.Get();
            const ADemoPlayerController* PC=Instance?Cast<ADemoPlayerController>(Instance->GetFirstLocalPlayerController()):nullptr;
            return State && Saves && Player && PC && PC->GetPawn()==Player && Player->GetWorld()==&World
                && FVector::DistSquared(Player->GetActorLocation(),InitialLocation)<=FMath::Square(50.f)
                && State->Phase==EDemoPhase::Lobby && State->PhaseRevision==InitialPhaseRevision && State->LevelNumber==0 && State->EnemiesRemaining==0
                && Saves->GetActiveSlot()==INDEX_NONE && !Saves->SlotExists(0) && !Saves->SlotExists(1) && !Saves->SlotExists(2) && Saves->GetChangeSerial()==InitialSaveSerial;
        }

        /** 记录首个确定失败，立即进入正常释放；不重试失败请求、不停止任何Editor进程。 */
        void Fail(const FString& Reason)
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            if (!bFailed)
            {
                bFailed=true; Test.AddError(Reason);
                UE_LOG(LogFPSDemo,Error,TEXT("PORT_ROOM_STREAMING_FAIL index=%d room=%s reason=%s"),RoomIndex+1,*CurrentRequest.RoomId.ToString(),*Reason);
            }
            BeginCleanup();
        }

        /** 只向初始化时冻结的同World Rooms发释放请求，最多一次；正式Ticker负责卸载与安全地面保护。 */
        void BeginCleanup()
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            Stage=EStage::Cleanup;
            if (bReleaseRequested) return;
            bReleaseRequested=true; CleanupDeadline=FMath::Min(Started+PortTotalBudgetSeconds,FPlatformTime::Seconds()+PortRoomBudgetSeconds);
            if (bOwnsRooms)
                if (UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get()) Rooms->ReleaseRooms();
            UE_LOG(LogFPSDemo,Log,TEXT("PORT_ROOM_STREAMING_RELEASE requested=%d passed_rooms=%d"),bOwnsRooms,PassedRooms);
        }

        /** 末尾观察Active为空且全部已记录实例真实移除；不足预算保留明确失败，不Flush/GC或强拆关卡。 */
        bool UpdateCleanup(double Now)
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            UWorld* World=TestWorld.Get();
            UDemoRoomStreamingSubsystem* Rooms=TestRooms.Get();
            bool bRemoved=!bOwnsRooms; // 初始化前失败没有领取任何房间，不触及世界里别人的实例。
            if (bOwnsRooms && World && !World->bIsTearingDown && Rooms)
            {
                bRemoved=!Rooms->HasActiveRoom() && !Rooms->GetPreparedLevel();
                for (const TWeakObjectPtr<ULevelStreaming>& Instance:OwnedInstances) bRemoved&=PortInstanceRemoved(*World,Instance);
                if (bRemoved)
                    for (int32 Arena=0;Arena<3;++Arena)
                    {
                        FDemoAreaSnapshot Area; FString Error; // 公开活动查询必须不再返回任何本服务实例。
                        if (Rooms->GetActiveArea(Arena,Area,Error)) bRemoved=false;
                    }
            }
            if (!bRemoved && Now<CleanupDeadline) return false;
            if (!bRemoved)
            {
                if (!bFailed) Test.AddError(TEXT("normal room release did not complete within the remaining bounded cleanup budget"));
                bFailed=true;
                UE_LOG(LogFPSDemo,Error,TEXT("PORT_ROOM_STREAMING_CLEANUP_NOT_OBSERVED release_requested=1 no_forced_unload=1 world_available=%d"),World!=nullptr);
            }
            if (!bFailed && (PassedRooms!=11 || !World || !CheckIsolation(*World)))
            { bFailed=true; Test.AddError(TEXT("final room count or fresh Lobby isolation mismatch")); }
            if (!bFailed)
                UE_LOG(LogFPSDemo,Display,TEXT("PORT_ROOM_STREAMING_SUCCESS requests=11 maps=10 native_ground_navigation=passed legacy_directory=8 actual_old_instance_removal=passed final_release=passed elapsed=%.3f; FULL_FLOW_COMBAT_MULTIPLAYER_COOK_NOT_RUN"),Now-Started);
            bOwnsRooms=false;
            return true;
        }

        FAutomationTestBase& Test; // Automation拥有，生命周期覆盖本latent；只追加此测试的失败证据。
        const double Started; // 单调真实秒起点，全流程180秒上限。
        TWeakObjectPtr<UWorld> TestWorld; // 本次初始化成功的实际Standalone World，旅行后不复用。
        TWeakObjectPtr<ADemoCharacter> TestPlayer; // 原Hub Pawn；测试不创建、销毁、换Possess或传送。
        TWeakObjectPtr<UDemoRoomStreamingSubsystem> TestRooms; // 同World服务；仅领取确认初始为空的Rooms。
        TWeakObjectPtr<ULevelStreaming> CurrentInstance; // 当前Begin实际加入World的唯一实例，加载未完成也必须正常回收。
        TWeakObjectPtr<ULevelStreaming> LastInstance; // 刚提交Active的实际Streaming对象，弱引用不阻止回收。
        TWeakObjectPtr<ULevelStreaming> PreviousInstance; // 本次Commit替换的旧实例；移出World后才继续。
        TArray<TWeakObjectPtr<ULevelStreaming>> OwnedInstances; // 最多11项实际实例身份，仅用于最终只读回收确认。
        TArray<int32> ArenaIndices; // 实际有效Campaign的11个旧适配键，范围0..2，不能当Room身份。
        FDemoRoomStreamRequest CurrentRequest; // 正式Settings构建的当前不可变请求，不改地图/导航/实例代数条件。
        FDemoAreaSnapshot LastArea; // 实际提交值，下一轮UseOtherSlot输入，不持有关卡Actor。
        FVector InitialLocation=FVector::ZeroVector; // 初始化后的Pawn实际位置cm，允许50cm物理沉降但绝不移至空间槽。
        uint32 InitialPhaseRevision=0; // Lobby原始阶段版本，测试不得推进。
        uint64 InitialSaveSerial=0; // GI实际保存水位，测试不创建槽或存盘。
        double RoomStarted=0.; // 当前请求含其旧实例回收的真实起点秒。
        double NextPoll=0.; // 下一次有限准备/回收观察时刻秒。
        double CleanupDeadline=0.; // 最后释放最多30秒且不超过总180秒。
        FString LastWaitReason; // 仅变化时打印生产Pending原因，超时保留最后确定等待点。
        int32 RoomIndex=0; // 0..10，后两项都加载L10但Boss身份不同。
        int32 PassedRooms=0; // 仅真实Ready和Commit成功后累计。
        EStage Stage=EStage::Initialize; // 夹具时序，与游戏Phase完全独立。
        bool bOwnsRooms=false; // 确认初始服务为空且安装配置通过后才允许释放此服务。
        bool bReleaseRequested=false; // 所有退出路径共用一次正常ReleaseRooms。
        bool bFailed=false; // 保留首错，清理不能把失败改写成成功。
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoPortRoomStreamingTest,"FPSDemo.Rooms.PortLockdownStreaming",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

/** Parameters为Automation参数，不接受资源路径；仅显式独立Editor -game及已生效的私有设备INI允许运行一次。 */
bool FDemoPortRoomStreamingTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    FString SettingsINI; // 本进程专用绝对路径；引擎已读取，测试不改设备设置或生成共享INI。
    static bool bInvoked=false; // 显式fresh进程契约：不在同GI内反复领取曾经回收的服务或覆盖失败证据。
    const bool bHasSettingsOverride=FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),SettingsINI); // 先取实值以诊断保护门，不能因后续短路只留下泛化失败。
    const FString EffectiveINI=FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"),nullptr,*FPaths::GeneratedConfigDir()); // 与本机ConfigContext加载相同的公开路径解析，保留命令行覆盖。
    FString LoadedPrivateINI; // 仅当前INI带有本次绝对路径标记；从实际已加载缓存读取，不从期待的文件反向自证。
    const bool bPrivateLoaded=GConfig && GConfig->FindConfigFile(GGameUserSettingsIni) && GConfig->GetString(TEXT("FPSDemo.PortRoomVerification"),TEXT("PrivateIni"),LoadedPrivateINI,GGameUserSettingsIni)
        && FPaths::IsSamePath(SettingsINI,LoadedPrivateINI) && FPaths::IsSamePath(SettingsINI,EffectiveINI);
    UE_LOG(LogFPSDemo,Display,TEXT("PORT_ROOM_TEST_CONTEXT game=%d flag=%d invoked=%d override=%d private_loaded=%d requested=%s cache_key=%s effective=%s marker=%s"),FParse::Param(FCommandLine::Get(),TEXT("game")),FParse::Param(FCommandLine::Get(),TEXT("DemoPortRoomStreamingTest")),bInvoked,bHasSettingsOverride,bPrivateLoaded,*SettingsINI,*GGameUserSettingsIni,*EffectiveINI,*LoadedPrivateINI); // Editor专项单次来源证据，无业务权限/保存副作用。
    if (bInvoked || !FParse::Param(FCommandLine::Get(),TEXT("game")) || !FParse::Param(FCommandLine::Get(),TEXT("DemoPortRoomStreamingTest"))
        || !bHasSettingsOverride || SettingsINI.IsEmpty() || FPaths::IsRelative(SettingsINI)
        || FPaths::IsSamePath(SettingsINI,FPaths::GeneratedConfigDir()/TEXT("GameUserSettings.ini"))
        || FPaths::IsSamePath(SettingsINI,FPaths::ProjectConfigDir()/TEXT("DefaultGameUserSettings.ini"))
        || !bPrivateLoaded)
    {
        AddError(TEXT("Requires a fresh Editor -game -DemoPortRoomStreamingTest with an active private absolute -GameUserSettingsINI and matching [FPSDemo.PortRoomVerification] PrivateIni marker; ordinary Editor/PIE/shared settings are rejected."));
        return false;
    }
    bInvoked=true;
    ADD_LATENT_AUTOMATION_COMMAND(FPortRoomStreamingLatent(*this));
    return true;
}
#endif

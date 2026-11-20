// 职责：在隔离Editor -game观测默认首关真实出生、区域预算与击杀结算。
// 使用：-DemoL01RegionalSpawnTest -GameUserSettingsINI=<私有绝对路径>，Automation RunTests FPSDemo.Rooms.L01RegionalSpawn。
// 私有INI：[FPSDemo.L01Verification] PrivateIni=<同一绝对路径>。Listen房主加-DemoL01RegionalListen，远端以-DemoL01RegionalJoin=127.0.0.1:<port>运行同名专项。
// 依赖：真实D03首关流送、GameMode、区域配置、Spawn公开事件与原Health GE。出发后测试只安排服务端Pawn站位和GE击杀。
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Components/CapsuleComponent.h"
#include "CoreGlobals.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "Game/Content/DemoCampaignDefinition.h"
#include "Game/Content/DemoCampaignSettings.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Objectives/DemoObjectiveCoordinatorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Interaction/DemoInteractable.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Player/Coop/Ready/DemoTeamReadyComponent.h"
#include "Player/DemoPlayerController.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Save/DemoRunSave.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UObject/Package.h"
#include "Weapons/DemoWeaponComponent.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"

namespace
{
    constexpr double EntryRadius = 1200.; // 本工单永久入场禁刷距离，XY cm。
    constexpr double PawnRadius = 450.; // 本工单默认实时活Pawn距离，XY cm。
    constexpr double WaveGap = 5.; // 全场相邻非空批的最小游戏秒。
    constexpr int32 ZoneBudget = 13; // 单人普通每区份额，联机独立按正式倍率冻结。
    constexpr int32 AliveCap = 6; // 两环境的全关硬上限。
    constexpr int32 WaveCap = 4; // 每个实际非空批上限。

    /** 当前进程唯一正式常驻图的Game World。允许旅行期间暂缺，拒绝PIE及别图。 */
    UWorld* FindL01World()
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (!GEngine) return nullptr;
        UWorld* Result = nullptr; // 同步借用，不跨帧保活。
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            UWorld* World = Context.World(); // 引擎管理，普通PIE/Viz World不属于本夹具。
            if (!World || World->WorldType != EWorldType::Game || World->bIsTearingDown || !World->PersistentLevel
                || World->PersistentLevel->GetOutermost()->GetFName() != TEXT("/Game/Whitebox/Maps/L_ThreeSector_Whitebox")) continue;
            if (Result) return nullptr;
            Result = World;
        }
        return Result;
    }

    /** GT latent拥有观察订阅，析构移除；检测对象借用不跨World。 */
    class FL01RegionalRun final : public IAutomationLatentCommand
    {
    public:
        /** InTest由Automation管理至本命令结束。 */
        explicit FL01RegionalRun(FAutomationTestBase& InTest) : Test(InTest), Started(FPlatformTime::Seconds()),
            bExpectedListen(FParse::Param(FCommandLine::Get(),TEXT("DemoL01RegionalListen"))) { DEMO_LOG_STATIC_CALL(nullptr); }

        /** 清除同步原生委托，避免测试结束后使用悬空this。 */
        ~FL01RegionalRun() override
        {
            DEMO_LOG_STATIC_CALL(ObservedWorld.Get());
            if (UDemoEnemySpawnComponent* S = Spawner.Get())
            {
                S->OnRemainingChanged.RemoveAll(this);
                S->OnEnemyDefeated.RemoveAll(this);
                S->OnCleared.RemoveAll(this);
                S->OnFailed.RemoveAll(this);
            }
            if (UDemoObjectiveCoordinatorComponent* O = Objective.Get()) O->OnSnapshotChanged.RemoveAll(this);
        }

        /** 只在独立权威World推进脚本。真实时间约束挂起，业务断言使用World游戏秒。 */
        bool Update() override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            if (bFailed) return true;
            if (FPlatformTime::Seconds() - Started > 240.) return Fail(FString::Printf(TEXT("timeout at stage %d"), Stage));
            UWorld* World = FindL01World(); // 本帧同步借用。
            if (!World || !World->HasBegunPlay() || World->GetTimeSeconds() < 1.f) return false;
            if (World->GetNetMode() == NM_Client) return Fail(TEXT("authority fixture started as a client"));
            if (ObservedWorld.IsValid() && ObservedWorld.Get() != World) return Fail(TEXT("authority World changed"));
            AFPSDemoGameMode* GM = World->GetAuthGameMode<AFPSDemoGameMode>(); // 只使用正式权威GM。
            ADemoGameState* GS = World->GetGameState<ADemoGameState>();
            if (!GM || !GS) return false;
            ADemoPlayerController* PC = Cast<ADemoPlayerController>(World->GetFirstPlayerController()); // 代表当前房主Pawn。
            ADemoCharacter* Host = PC ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr;
            if (!Host || !Host->GetDemoASC() || !Host->GetDemoAttributes() || !Host->GetWeaponComponent() || !Host->GetWeaponComponent()->GetActiveWeapon()) return false;
            ADemoCharacter* Remote = nullptr; // Listen的真实远端连接Pawn，只在本帧使用。
            int32 Registered = 0;
            for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
            {
                const ADemoPlayerController* Member = Cast<ADemoPlayerController>(It->Get());
                if (!Member || !Member->IsCoopRegistered()) continue;
                ++Registered;
                if (!Member->IsLocalController()) Remote = Cast<ADemoCharacter>(Member->GetPawn());
            }
            const double Now = World->GetTimeSeconds(); // 每阶段的World游戏秒。
            const bool bListen = World->GetNetMode() == NM_ListenServer;
            if (Stage == 0)
            {
                if (bExpectedListen && !bListen)
                {
                    if (bHostRequested || GS->Phase != EDemoPhase::Lobby) return false;
                    UDemoRoomSubsystem* Room = World->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // GI拥有，使用正式LAN槽开房。
                    if (!Room || Room->IsPublic() || !Room->HostSlot(0,true)) return Fail(TEXT("formal LAN HostSlot failed"));
                    bHostRequested = true;
                    return false;
                }
                if (!bExpectedListen && bListen) return Fail(TEXT("Listen host requires explicit Listen test flag"));
                if (bListen && (Registered != 2 || !Remote || !Remote->GetDemoAttributes())) return false;
                if (GS->LevelNumber != 0 || GS->Phase == EDemoPhase::Combat) return Fail(TEXT("test must attach before first departure"));
                if (!Bind(*World, *GM)) return Fail(TEXT("production spawn observer unavailable"));
                if (!bListen)
                {
                    UDemoRunSaves* Saves = World->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 当前Editor GI独立临时槽。
                    UDemoMenuFlowComponent* Menu = PC->GetMenuFlow(); // 由正式UI动作关闭隔离档的新手指引。
                    if (!Menu) return Fail(TEXT("no real menu flow for first-run guide"));
                    Menu->UpdateRealtime(0.f);
                    if (Menu->GetMenuPage() == EDemoMenuPage::FirstRunGuide) Menu->DispatchUIAction(EDemoUIAction::OverlayBack);
                    if (!Saves || GS->Phase != EDemoPhase::Lobby || Saves->GetActiveSlot() != INDEX_NONE || Saves->SlotExists(0)
                        || !Saves->CreateSlot(0) || !GM->StartRun()) return Fail(TEXT("fresh editor temporary slot / StartRun rejected"));
                    Saves->ConsumePending(); // 本夹具直接走生产StartRun，消费UI本会处理的创建意图。
                    PC->OnRunReady();
                    if (!GM->SelectDifficulty(EDemoDifficulty::Normal)) return Fail(TEXT("normal difficulty selection rejected"));
                    UDemoRunBuildComponent* Build = UDemoRunBuildComponent::FindForPawn(Host); // 正式核心选择门。
                    if (!Build || (!Build->HasSelectedCore() && !Build->TrySelectCore(TEXT("B01")))) return Fail(TEXT("real core selection rejected"));
                    GM->StartNextLevel();
                }
                else
                {
                    if (GS->Phase != EDemoPhase::Hub) return Fail(TEXT("Listen test requires fresh formal Hub"));
                    if (!PrepareListen(*GM, *GS, *Host, *Remote)) return Fail(TEXT("formal readiness/terminal/core validation failed"));
                }
                Stage = 1;
                return false;
            }
            if (bListen && (Registered != 2 || !Remote || !Remote->GetDemoAttributes() || Remote->GetDemoAttributes()->GetHealth() <= 0.f))
                return Fail(TEXT("two live registered Listen players required throughout"));
            if (Stage == 1)
            {
                if (GS->Phase != EDemoPhase::Combat) return false;
                if (GS->LevelNumber != 1 || GS->Difficulty != EDemoDifficulty::Normal || !InitializeRoom(*World, *GM, *Host, bListen))
                    return Fail(TEXT("production default L01 normal room, budget or entry geometry differs"));
                CombatRevision = GS->PhaseRevision; // 末尾只允许一次Combat→Reward阶段提交。
                ExitAt = Now + WaveGap + 1.; Stage = 2;
                return false;
            }
            if (Now < ExitAt) return false;
            if (Stage < 9 && GS->Phase != EDemoPhase::Combat) return Fail(TEXT("left Combat before exhausting regional budget"));
            switch (Stage)
            {
            case 2:
                if (Births != 0 || Spawner->GetRemaining() != Total) return Fail(TEXT("A produced enemies or consumed budget"));
                if (!Place(*Host, 2, true)) return Fail(TEXT("no valid D point blocks both authored points with margin"));
                ExitAt = Now + WaveGap + 1.; Stage = 3;
                break;
            case 3:
                if (Births != 0 || Spawner->GetRemaining() != Total) return Fail(TEXT("blocked D consumed or spawned a unit"));
                if (!Place(*Host, 2, false)) return Fail(TEXT("no valid unblocked D station"));
                Stage = 4;
                break;
            case 4:
                if (!ZoneBirths[2]) return false;
                if (ZoneBirths[0] || ZoneBirths[1]) return Fail(TEXT("D-first caused B or C births"));
                if (!ReturnToEntry(*Host)) return Fail(TEXT("cannot leave D for A"));
                // 观察器暂停AI，首批仍占两处窄点。真实击杀先腾出位置，避免把合法碰撞等待误判为离区丢预算。
                if (!Drain(*Host)) return Fail(TEXT("failed to release both D spawn points via real deaths"));
                BeforeReentry = ZoneBirths[2]; ExitAt = Now + WaveGap + 1.; Stage = 5;
                break;
            case 5:
                if (ZoneBirths[2] <= BeforeReentry) return Fail(TEXT("latched D did not resume its budget while the original entrant was back in A"));
                if (!Place(*Host, 2, false)) return Fail(TEXT("cannot reenter D"));
                UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_D_REENTER before_leave=%d before_reenter=%d"), BeforeReentry, ZoneBirths[2]);
                if (Remote && !Place(*Remote, 0, false)) return Fail(TEXT("cannot split host D / client B"));
                Stage = 6;
                break;
            case 6:
                if (!Drain(*Host)) return Fail(TEXT("real Health GE did not cause a registered defeat"));
                if (ZoneBirths[2] != PerZone || (Remote && ZoneBirths[0] != PerZone) || Spawner->GetAliveCount()) return false;
                if (ClearEvents || Spawner->GetRemaining() != PerZone * (Remote ? 1 : 2)) return Fail(TEXT("empty explored regions completed with unentered budget"));
                if (!ReturnToEntry(*Host) || (Remote && !ReturnToEntry(*Remote, 1))) return Fail(TEXT("cannot return both Pawns outside combat zones"));
                ExitAt = Now + .6; Stage = 7;
                break;
            case 7:
                if (Remote)
                {
                    // 两个独立已注册Pawn同一GT帧进同区；仅使用服务端真实变换，客户端复制仍走生产。
                    if (!Place(*Host, 1, false) || !Place(*Remote, 1, false, Host)) return Fail(TEXT("cannot place both Listen Pawns in C on the same frame"));
                }
                else if (!Place(*Host, 0, false)) return Fail(TEXT("cannot enter B after D"));
                Stage = 8;
                break;
            case 8:
                if (!Drain(*Host)) return Fail(TEXT("real Health GE did not cause a registered defeat"));
                if (!Remote && ZoneBirths[0] == PerZone && !Spawner->GetAliveCount() && !bSoloEnteredC)
                {
                    if (ClearEvents || Spawner->GetRemaining() != PerZone || !Place(*Host, 1, false)) return Fail(TEXT("B/D cleared early or cannot enter C"));
                    bSoloEnteredC = true;
                }
                if (Births != Total || Deaths != Total) return false;
                ExitAt = Now + WaveGap + 1.; Stage = 9;
                break;
            case 9:
                if (Births != Total || Deaths != Total || ClearEvents != 1 || GS->Phase != EDemoPhase::Reward
                    || GS->LevelNumber != 1 || GS->EnemiesRemaining != 0 || GS->PhaseRevision != CombatRevision + 1
                    || ZoneBirths[0] != PerZone || ZoneBirths[1] != PerZone || ZoneBirths[2] != PerZone)
                    return Fail(TEXT("final production reward/remaining or unique clear mismatch"));
                UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_SUCCESS parties=%d A=0 B=%d C=%d D=%d spawned=%d killed=%d clear=%d peak=%d nonempty_batches=%d"),
                    bListen ? 2 : 1, ZoneBirths[0], ZoneBirths[1], ZoneBirths[2], Births, Deaths, ClearEvents, Peak, BatchCount);
                return true;
            default: return Fail(TEXT("invalid test stage"));
            }
            return bFailed;
        }

    private:
        /** 隔离LAN使用真实终端会话、核心选择和准备服务，客户端仍经原连接及房间ACK。测试不覆盖准备按钮UI。 */
        bool PrepareListen(AFPSDemoGameMode& GM, const ADemoGameState& GS, ADemoCharacter& Host, ADemoCharacter& Remote)
        {
            DEMO_LOG_STATIC_CALL(GM.GetWorld());
            ADemoInteractable* Terminal = GM.GetNextLevelTerminal(); // 本阶段真实终端；出发后不得继续借用。
            UDemoTeamReadyComponent* Ready = GM.GetTeamReadySystem(); // 正式只接受已登记成员和现终端会话。
            UDemoTerminalComponent* Terminals = GM.GetTerminalSystem(); // GM持有，实际终端验证距离/阶段/代数。
            if (!Terminal || !Ready || !Terminals || !GM.SelectDifficulty(EDemoDifficulty::Normal)) return false;
            ADemoCharacter* Players[] = {&Host, &Remote}; // 当前同步栈两名真实Pawn。
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(Players); ++Index)
            {
                ADemoCharacter& Pawn = *Players[Index];
                UDemoRunBuildComponent* Build = UDemoRunBuildComponent::FindForPawn(&Pawn); // PS权威核心，选完再统一Ready。
                if (!Build || (!Build->HasSelectedCore() && !Build->TrySelectCore(TEXT("B01")))) return false;
                const FVector Anchor = Terminal->GetActorLocation(); // 世界cm，允许碰撞的测试交互站位。
                const double Z = Pawn.GetActorLocation().Z;
                bool bFound = false;
                for (int32 Step = 0; Step < 8 && !bFound; ++Step)
                {
                    const double Angle = (Index * 4 + Step) * PI / 4.; // 两人从相反方位开始，避免胶囊重叠。
                    const FVector Point(Anchor.X + 160.*FMath::Cos(Angle), Anchor.Y + 160.*FMath::Sin(Angle), Z);
                    bFound = Pawn.TeleportTo(Point, Pawn.GetActorRotation(), false, false) && Terminals->BeginInteraction(&Pawn,Terminal);
                }
                if (!bFound) return false;
                Pawn.GetCharacterMovement()->StopMovementImmediately();
            }
            Ready->Refresh();
            FString Error; // 保留生产拒绝原因，不直接写公开Ready值。
            for (ADemoCharacter* Pawn : Players)
                if (!Ready->SetReady(Cast<ADemoPlayerController>(Pawn->GetController()),true,GS.TeamReady.Revision,GS.LevelNumber,Error))
                {
                    UE_LOG(LogFPSDemo, Warning, TEXT("L01_QA_LISTEN_READY_REJECT %s"), *Error);
                    return false;
                }
            UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_LISTEN_READY production setup with two registered Pawns"));
            GM.StartNextLevel();
            return true;
        }

        /** 公开事件同步取样。绑定早于出发，能发现A中非法首发，不记录未激活的Deferred Actor。 */
        bool Bind(UWorld& World, AFPSDemoGameMode& GM)
        {
            DEMO_LOG_STATIC_CALL(&World);
            UDemoEnemySpawnComponent* S = GM.FindComponentByClass<UDemoEnemySpawnComponent>(); // GM持有，析构时按弱引用解绑。
            UDemoObjectiveCoordinatorComponent* O = GM.FindComponentByClass<UDemoObjectiveCoordinatorComponent>(); // 同一GM的正式目标发布者。
            if (!S || !O) return false;
            ObservedWorld = &World; Spawner = S; Objective = O;
            S->OnRemainingChanged.AddRaw(this, &FL01RegionalRun::ObserveBatch);
            S->OnEnemyDefeated.AddRaw(this, &FL01RegionalRun::ObserveKill);
            S->OnCleared.AddRaw(this, &FL01RegionalRun::ObserveClear);
            S->OnFailed.AddRaw(this, &FL01RegionalRun::ObserveFailure);
            O->OnSnapshotChanged.AddRaw(this, &FL01RegionalRun::ObserveObjective);
            return true;
        }

        /** 读回真实活动区域并基于安装的默认战役计算单人及两人预期。公开计数只作实际值。 */
        bool InitializeRoom(UWorld& World, AFPSDemoGameMode& GM, ADemoCharacter& Host, bool bListen)
        {
            DEMO_LOG_STATIC_CALL(&World);
            FDemoLevelRow Level; FDemoDifficultyRow Difficulty; FString Error; // 仅取正式关卡适配键。
            UDemoRoomStreamingSubsystem* Rooms = World.GetSubsystem<UDemoRoomStreamingSubsystem>(); // World服务，真实提交后只读。
            const UDemoCampaignDefinition* Campaign = GetDefault<UDemoCampaignSettings>()->LoadCampaign(Error); // 安装项目默认，不读Spawn执行器私有计划。
            if (!Campaign || (GM.DemoCampaign && GM.DemoCampaign.Get() != Campaign) || Campaign->CampaignId != TEXT("steam_demo")
                || Campaign->ContentRevision != 4 || Campaign->Party.Num() < 2 || Campaign->Party[0].MinionCount <= 0.f
                || !Rooms || !GM.GetLevelConfig(1, Level, Difficulty, Error) || !Rooms->GetActiveArea(Level.ArenaIndex, Area, Error)
                || !Area.Geometry.ExplicitSourceLevelPackage.ToString().Contains(TEXT("d03_l01"), ESearchCase::IgnoreCase)
                || Area.Geometry.PlayerEntryExclusionRadiusCm != EntryRadius || Area.Geometry.ProtectedPlayerEntryPoints.Num() != 4
                || Area.Geometry.ExplicitSpawnOffsets.Num() != 8 || FVector::Dist2D(Area.GetCombatLandingPoint(0), Host.GetActorLocation()) > 150.
                || Births || Spawner->Settings.MinPlayerDistance != PawnRadius) return false;
            const UDemoRegionalSpawnSettings* Settings = GetDefault<UDemoRegionalSpawnSettings>(); // 只读安装地理；硬门使用工单预期。
            if (Settings->Zones.Num() != 3 || Settings->MaxAlive != AliveCap || Settings->WaveSize != WaveCap || Settings->WaveInterval != WaveGap) return false;
            const FName Ids[] = {TEXT("B"), TEXT("C"), TEXT("D")}; // 固定位置仅用于独立统计顺序。
            for (int32 Index = 0; Index < 3; ++Index)
            {
                // 同步借用候选Id，查找不会跨帧保留引用。
                const FDemoRegionalSpawnZone* Found = Settings->Zones.FindByPredicate([&Ids, Index](const FDemoRegionalSpawnZone& Z)
                { DEMO_LOG_STATIC_TICK(nullptr); return Z.Id == Ids[Index]; });
                if (!Found || Found->EnemyCount != ZoneBudget || Found->SpawnPointIndices.Num() != 2) return false;
                Zones[Index] = *Found;
                for (int32 Point : Found->SpawnPointIndices)
                    if (!Area.Geometry.ExplicitSpawnOffsets.IsValidIndex(Point)) return false;
            }
            Total = Spawner->GetRemaining();
            PerZone = bListen ? FMath::RoundToInt(double(ZoneBudget) * Campaign->Party[1].MinionCount / Campaign->Party[0].MinionCount) : ZoneBudget;
            if (Total <= 0 || PerZone * 3 != Total) return false;
            UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_ENTER room=%s normal=1 total=%d per_zone=%d pawn=%s entry=%s"),
                *Area.Geometry.ExplicitSourceLevelPackage.ToString(), Total, PerZone, *Host.GetActorLocation().ToCompactString(), *Area.GetCombatLandingPoint(0).ToCompactString());
            return true;
        }

        /** 只按实际World点逆变换后与安装边界比较，不调用生成器内部分类。 */
        bool Contains(int32 Index, const FVector& Point) const
        {
            DEMO_LOG_STATIC_TICK(ObservedWorld.Get());
            const FVector Local = Area.Transform.InverseTransformPosition(Point); // 房间局部cm。
            const FDemoRegionalSpawnZone& Zone = Zones[Index]; // 已冻结且经过索引校验。
            return Local.X >= Zone.LocalMin.X && Local.X <= Zone.LocalMax.X && Local.Y >= Zone.LocalMin.Y && Local.Y <= Zone.LocalMax.Y;
        }

        /** 用真导航和真实胶囊找测试站位；block需覆盖两个作者点及64cm散布，unblock与两点都保持余量。 */
        bool Place(ADemoCharacter& Pawn, int32 Index, bool bBlock, const ADemoCharacter* Other = nullptr)
        {
            DEMO_LOG_STATIC_CALL(Pawn.GetWorld());
            UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn.GetWorld()); // 本帧导航借用。
            if (!Nav || !Pawn.GetCapsuleComponent()) return false;
            const FDemoRegionalSpawnZone& Zone = Zones[Index]; // 本次站位的两个独占作者点。
            const FVector A = Area.Transform.TransformPosition(Area.Geometry.ExplicitSpawnOffsets[Zone.SpawnPointIndices[0]]);
            const FVector B = Area.Transform.TransformPosition(Area.Geometry.ExplicitSpawnOffsets[Zone.SpawnPointIndices[1]]);
            const FVector Center = (A + B) * .5;
            const double Half = Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight(); // cm，Nav点是脚下地面。
            for (int32 Ring = 0; Ring <= (bBlock ? 2 : 22); ++Ring)
                for (int32 Direction = 0; Direction < (Ring ? 16 : 1); ++Direction)
                {
                    const double Angle = 2. * PI * Direction / 16.; // 定向探测有限站位，无随机/无几何写入。
                    FNavLocation Projected;
                    if (!Nav->ProjectPointToNavigation(Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Ring * 100.), Projected, FVector(45,45,220))) continue;
                    const FVector Site = Projected.Location + FVector(0,0,Half+3.);
                    const double DA = FVector::Dist2D(Site,A), DB = FVector::Dist2D(Site,B); // 预留20cm位置校正及真实再测。
                    if (!Contains(Index,Site) || (Other && FVector::Dist2D(Site,Other->GetActorLocation()) < 160.)
                        || (bBlock ? FMath::Max(DA,DB) > PawnRadius-84. : FMath::Min(DA,DB) < PawnRadius+84.)) continue;
                    if (!Pawn.TeleportTo(Site, Pawn.GetActorRotation(), false, false)) continue;
                    if (!Pawn.GetActorLocation().Equals(Site,20.)) return false;
                    Pawn.GetCharacterMovement()->StopMovementImmediately();
                    UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_PLACE world_s=%.6f pawn=%s zone=%s block=%d pos=%s d1=%.1f d2=%.1f"),
                        Pawn.GetWorld()->GetTimeSeconds(), *Pawn.GetName(), *Zone.Id.ToString(), bBlock, *Pawn.GetActorLocation().ToCompactString(),
                        FVector::Dist2D(Pawn.GetActorLocation(),A), FVector::Dist2D(Pawn.GetActorLocation(),B));
                    return Contains(Index,Pawn.GetActorLocation());
                }
            return false;
        }

        /** 回到已提交入场锚点，仅为可控重入刺激而定位，不测试玩家跑图。 */
        bool ReturnToEntry(ADemoCharacter& Pawn, int32 Slot = 0) const
        {
            DEMO_LOG_STATIC_CALL(Pawn.GetWorld());
            const FVector Target = Area.GetCombatLandingPoint(Slot); // 正式落点世界cm。
            if (!Pawn.TeleportTo(Target, Pawn.GetActorRotation(), false, false) || !Pawn.GetActorLocation().Equals(Target,30.)) return false;
            Pawn.GetCharacterMovement()->StopMovementImmediately();
            return true;
        }

        /** 在Remaining同步通知中观察成功激活的真实Actor。用于保留同调度栈中的坐标和所有活人距离。 */
        void ObserveBatch(int32 Remaining)
        {
            DEMO_LOG_STATIC_TICK(ObservedWorld.Get());
            UWorld* World = ObservedWorld.Get(); // 委托生命周期内弱引用，不延长World生命。
            if (!World || bFailed) return;
            int32 Added = 0; // 本通知首次见到的实际成功激活数。
            const double Now = World->GetTimeSeconds(); // 出生同一时间栈，不用日志墙钟换算。
            for (TActorIterator<ADemoEnemy> It(World); It; ++It)
            {
                ADemoEnemy* Enemy = *It; // 本GT同步借用。
                if (!Enemy->HasAuthority() || !Enemy->IsCombatReady() || Seen.Contains(Enemy)) continue;
                Seen.Add(Enemy); ++Births; ++Added;
                if (Stage < 2) { Fail(TEXT("nonempty spawn batch before protected entry observation")); return; }
                const FVector P = Enemy->GetActorLocation(); // 实际激活Actor世界中心cm。
                int32 Region = INDEX_NONE; // 只能属于一个已安装区域。
                for (int32 Index = 0; Index < 3; ++Index)
                    if (Contains(Index,P))
                    {
                        if (Region != INDEX_NONE) { Fail(TEXT("spawn classified into multiple regions")); return; }
                        Region = Index;
                    }
                double EntryDist = TNumericLimits<double>::Max(), PlayerDist = TNumericLimits<double>::Max(); // 最小XY cm。
                for (const FVector& Entry : Area.Geometry.ProtectedPlayerEntryPoints) EntryDist = FMath::Min(EntryDist,FVector::Dist2D(Entry,P));
                for (TActorIterator<ADemoCharacter> Member(World); Member; ++Member)
                    if (Member->GetController() && Member->GetDemoAttributes() && Member->GetDemoAttributes()->GetHealth() > 0.f)
                        PlayerDist = FMath::Min(PlayerDist,FVector::Dist2D(Member->GetActorLocation(),P));
                UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_BIRTH seq=%d world_s=%.6f region=%d actor=%s pos=%s entry_min_cm=%.3f player_min_cm=%.3f remaining=%d"),
                    Births, Now, Region, *Enemy->GetName(), *P.ToCompactString(), EntryDist, PlayerDist, Remaining);
                if (Region == INDEX_NONE || EntryDist < EntryRadius || PlayerDist < PawnRadius || ++ZoneBirths[Region] > PerZone)
                { Fail(TEXT("successful spawn violated regional budget or one of the live/fixed distance guards")); return; }
                Enemy->SetActorTickEnabled(false); // 固定出生观察和并发驻留；GE及注册死亡不替换。此夹具不验AI或玩家实战难度。
            }
            if (!Added) return;
            if (Now != LastBatchAt)
            {
                if (BatchCount && Now - LastBatchAt < WaveGap) { Fail(TEXT("nonempty batches closer than five game seconds")); return; }
                ++BatchCount; BatchSize = 0; LastBatchAt = Now;
            }
            BatchSize += Added; Peak = FMath::Max(Peak,Spawner->GetAliveCount());
            UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_BATCH index=%d world_s=%.6f count=%d alive=%d peak=%d"), BatchCount, Now, BatchSize, Spawner->GetAliveCount(), Peak);
            if (BatchSize > WaveCap || Peak > AliveCap) Fail(TEXT("global batch or alive cap exceeded"));
        }

        /** 用真实Health GE杀已经观测的生产敌人，保留死亡、奖励和结算委托，不直接减队列或调用Notify。 */
        bool Drain(ADemoCharacter& Host)
        {
            DEMO_LOG_STATIC_CALL(Host.GetWorld());
            TArray<TWeakObjectPtr<ADemoEnemy>> Targets; // 先冻结本次同步列表，避免死亡回调使遍历失效。
            for (TActorIterator<ADemoEnemy> It(Host.GetWorld()); It; ++It)
                if (It->IsCombatReady() && Seen.Contains(*It)) Targets.Add(*It);
            for (const TWeakObjectPtr<ADemoEnemy>& Candidate : Targets)
                if (ADemoEnemy* Enemy = Candidate.Get())
                {
                    const int32 Before = Deaths; // 每次真实GE必须产生恰一登记死亡。
                    DemoEffects::Apply(Host.GetDemoASC(), Enemy->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -2.f * Enemy->GetMaxHealth());
                    if (bFailed || Enemy->IsAlive() || Deaths != Before + 1) return false;
                }
            return true;
        }

        /** 权威出生组件已注册且唯一的真实死亡。Enemy仅在同步广播栈借用。 */
        void ObserveKill(ADemoEnemy* Enemy)
        {
            DEMO_LOG_STATIC_CALL(ObservedWorld.Get());
            if (!Enemy || !Seen.Contains(Enemy) || Defeated.Contains(Enemy)) { Fail(TEXT("unobserved or repeated registered defeat")); return; }
            Defeated.Add(Enemy); ++Deaths;
            UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_KILL seq=%d actor=%s remaining=%d"), Deaths, *Enemy->GetName(), Spawner->GetRemaining());
        }

        /** 通用生成器也可能发清场信号。新版目标先提交并取消生成器的完成Timer，因此此辅助信号可以为零。 */
        void ObserveClear()
        {
            DEMO_LOG_STATIC_CALL(ObservedWorld.Get());
            if (++SpawnerClearEvents > 1 || Births != Total || Deaths != Total) Fail(TEXT("early or duplicate spawner clear notification"));
        }

        /** 以目标快照进入Completed及最终Reward代数为生产结算事实，忽略同一状态的普通快照重发。 */
        void ObserveObjective(const FDemoObjectiveSnapshot& Snapshot)
        {
            DEMO_LOG_STATIC_CALL(ObservedWorld.Get());
            const bool bCompleted = Snapshot.State == EDemoObjectiveState::Completed; // 只计真实状态边沿。
            if (bCompleted && !bLastCompleted)
            {
                if (++ClearEvents != 1 || Snapshot.Kind != EDemoObjectiveKind::KillAll || Births != Total || Deaths != Total || Snapshot.Remaining != 0)
                    Fail(TEXT("early or duplicate production objective settlement"));
                UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_SETTLED completed=%d spawned=%d killed=%d remaining=%d"), ClearEvents, Births, Deaths, Snapshot.Remaining);
            }
            bLastCompleted = bCompleted;
        }

        /** 原失败原因只在广播栈借用，立即持久记录到Automation/日志。 */
        void ObserveFailure(const FString& Reason) { DEMO_LOG_STATIC_CALL(ObservedWorld.Get()); Fail(TEXT("production spawn failure: ") + Reason); }

        /** 保留首次失败及场景进度，返回true结束当前latent。 */
        bool Fail(const FString& Reason)
        {
            DEMO_LOG_STATIC_CALL(ObservedWorld.Get());
            if (!bFailed)
            {
                bFailed = true;
                Test.AddError(Reason);
                UE_LOG(LogFPSDemo, Error, TEXT("L01_QA_FAILED stage=%d spawned=%d killed=%d clear=%d reason=%s"), Stage, Births, Deaths, ClearEvents, *Reason);
            }
            return true;
        }

        FAutomationTestBase& Test; // Automation拥有。
        double Started; // 单调真实秒，有限等待。
        const bool bExpectedListen; // 启动时冻结执行环境，建房前不自动创建Standalone进度。
        TWeakObjectPtr<UWorld> ObservedWorld; // GT运行World，不跨旅行复用。
        TWeakObjectPtr<UDemoEnemySpawnComponent> Spawner; // GM拥有，析构解绑。
        TWeakObjectPtr<UDemoObjectiveCoordinatorComponent> Objective; // GM完成事实，析构解绑。
        FDemoAreaSnapshot Area; // 已提交区域值快照。
        FDemoRegionalSpawnZone Zones[3]; // B/C/D位置及标记索引，独立分类。
        TSet<TWeakObjectPtr<ADemoEnemy>> Seen, Defeated; // Actor唯一身份，分别记录生成和注册死亡。
        int32 ZoneBirths[3] = {}; // B/C/D成功累计，不包含A。
        int32 Total = 0, PerZone = 0; // 本轮初始预算与单区份额。
        int32 Stage = 0; // 0绑定，1出发，2～8区域动作，9结算观察。
        int32 Births = 0, Deaths = 0, ClearEvents = 0; // 实际事件计数。
        int32 SpawnerClearEvents = 0; // 通用组件的辅助清场事件，观察去重。
        uint32 CombatRevision = 0; // Combat实际阶段代数，仅允许向Reward推进一次。
        int32 Peak = 0, BatchCount = 0, BatchSize = 0; // 全场并发及非空批观察。
        int32 BeforeReentry = 0; // D离开前计数，供重入时证据比较。
        double LastBatchAt = 0., ExitAt = 0.; // World游戏秒，不用墙钟近似波次间隔。
        bool bSoloEnteredC = false; // 单人D→B→C，C只安排一次。
        bool bLastCompleted = false; // 用于只记录目标完成边沿。
        bool bHostRequested = false; // Listen旅行只发一次，旅行完成后再订阅新World。
        bool bFailed = false; // 锁存首个实际失败。
    };

    /** 同一Automation入口的远端分支，经实际本地LAN连接，等正式Combat及Reward复制收敛。 */
    class FL01RegionalClient final : public IAutomationLatentCommand
    {
    public:
        /** Test由队列拥有，InAddress限调用者已校验的环回地址。 */
        FL01RegionalClient(FAutomationTestBase& InTest, FString InAddress)
            : Test(InTest), Address(MoveTemp(InAddress)), Started(FPlatformTime::Seconds()) { DEMO_LOG_STATIC_CALL(nullptr); }

        /** 客户端正式加入一次。Reward后留观十真实秒，供房主完成相同轮次观测。 */
        bool Update() override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            const double Now = FPlatformTime::Seconds(); // 客户端超时/留观的真实秒，不参与服务端节奏。
            if (RewardAt) return Now >= RewardAt + 10.;
            if (Now >= Started + 240.) { Test.AddError(TEXT("L01 LAN client timed out")); return true; }
            UWorld* World = FindL01World(); // 允许JoinCoop旅行交换当前Game World。
            if (!World || !World->HasBegunPlay() || World->GetTimeSeconds() < 1.f) return false;
            UGameInstance* GI = World->GetGameInstance(); // 只在本帧借用。
            ADemoPlayerController* PC = GI ? Cast<ADemoPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
            if (!PC) return false;
            if (!bJoinRequested && World->GetNetMode() == NM_Standalone)
            {
                UDemoCoopSessionSubsystem* Session = GI->GetSubsystem<UDemoCoopSessionSubsystem>(); // 正式注入连接身份并Travel。
                if (!Session || !Session->JoinCoop(Address)) { Test.AddError(TEXT("L01 official LAN JoinCoop failed")); return true; }
                bJoinRequested = true;
                return false;
            }
            const ADemoGameState* GS = World->GetGameState<ADemoGameState>(); // 远端只读实际复制态。
            if (World->GetNetMode() != NM_Client || !PC->IsCoopRegistered() || !GS || GS->LevelNumber != 1) return false;
            if (GS->Phase == EDemoPhase::Combat) bSawCombat = true;
            if (bSawCombat && GS->Phase == EDemoPhase::Reward && GS->EnemiesRemaining == 0 && GS->Objective.State == EDemoObjectiveState::Completed)
            {
                UE_LOG(LogFPSDemo, Display, TEXT("L01_QA_CLIENT_REWARD level=1 remaining=0 objective_completed=1"));
                RewardAt = Now;
            }
            return false;
        }

    private:
        FAutomationTestBase& Test; // 队列拥有，失败时记录结果。
        FString Address; // 本次QA房主的环回地址。
        double Started; // 真实秒，计入正常连接/准备时限。
        double RewardAt = 0.; // 已收敛最终态后延后退出的真实秒。
        bool bJoinRequested = false; // 禁止对同一次测试重复旅行。
        bool bSawCombat = false; // 要先观测正式Combat再接受Reward。
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoL01RegionalSpawnTest,"FPSDemo.Rooms.L01RegionalSpawn",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

/** Parameters由Automation给出，本夹具不接受替换生产地图或参数；只允许每进程一次。 */
bool FDemoL01RegionalSpawnTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    static bool bInvoked = false; // 独立Editor进程一次，避免给已经退出的临时局重发任务。
    FString Requested, Marker; // 当前进程私有配置的实际路径与加载标记。
    const FString Effective = FConfigCacheIni::GetDestIniFilename(TEXT("GameUserSettings"),nullptr,*FPaths::GeneratedConfigDir());
    if (bInvoked || !FParse::Param(FCommandLine::Get(),TEXT("game")) || !FParse::Param(FCommandLine::Get(),TEXT("DemoL01RegionalSpawnTest"))
        || !FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),Requested) || Requested.IsEmpty() || FPaths::IsRelative(Requested)
        || !FPaths::IsSamePath(Requested,Effective) || FPaths::IsSamePath(Requested,FPaths::GeneratedConfigDir()/TEXT("GameUserSettings.ini"))
        || FPaths::IsSamePath(Requested,FPaths::ProjectConfigDir()/TEXT("DefaultGameUserSettings.ini")) || !GConfig
        || !GConfig->GetString(TEXT("FPSDemo.L01Verification"),TEXT("PrivateIni"),Marker,GGameUserSettingsIni) || !FPaths::IsSamePath(Requested,Marker))
    {
        AddError(TEXT("L01 requires an explicitly isolated fresh Editor -game and matching loaded private settings marker"));
        return false;
    }
    FString JoinAddress; // 仅自动化环回连接，不接触真实玩家或公网房间。
    const bool bJoining = FParse::Value(FCommandLine::Get(),TEXT("DemoL01RegionalJoin="),JoinAddress);
    if (bJoining && (!JoinAddress.StartsWith(TEXT("127.0.0.1:")) || !JoinAddress.RightChop(10).IsNumeric()))
    {
        AddError(TEXT("L01 remote endpoint must be an explicit IPv4 loopback port"));
        return false;
    }
    bInvoked = true;
    if (bJoining)
    {
        ADD_LATENT_AUTOMATION_COMMAND(FL01RegionalClient(*this,MoveTemp(JoinAddress)));
    }
    else
    {
        ADD_LATENT_AUTOMATION_COMMAND(FL01RegionalRun(*this));
    }
    return true;
}
#endif

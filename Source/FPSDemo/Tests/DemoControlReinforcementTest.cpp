// 职责：真实Standalone World中验证单点/旧三点配置、间隔增援、有界alive及完成/取消停调度。
// 使用：独立Editor -game -DemoControlReinforcementTest -GameUserSettingsINI="独占INI"
// -ExecCmds="Automation RunTests FPSDemo.Objectives.ControlReinforcements" -TestExit="Automation Test Queue Empty"。
// 依赖：原生Objective/Spawn组件、真实CombatSession/敌人模板/Health GE和World Timer；不创建槽/StartRun/存档。
// 边界：默认30秒检查正式Prepare快照，实际完成用合法1秒配置缩短合同；不代替整轮Flow/多人/奖励钱包验收。
#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Game/Objectives/DemoObjectiveCoordinatorComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
/** Automation拥有至结束的隔离GT夹具；Raw委托仅绑定本对象，Cleanup在latent析构前全部解绑。 */
class FControlReinforcementLatent final : public IAutomationLatentCommand
{
public:
    /** Test为Automation当前断言接收器；Started限制初始化/真实Timer总墙钟，不手动Tick世界或推进计时。 */
    explicit FControlReinforcementLatent(FAutomationTestBase& InTest):Test(InTest),Started(FPlatformTime::Seconds()) { DEMO_LOG_STATIC_CALL(nullptr); }
    /** 析构只回收本Owner/地板与本测试临时移动，不清理正式关卡对象。 */
    virtual ~FControlReinforcementLatent() { DEMO_LOG_STATIC_CALL(nullptr); Cleanup(); }
    /** GT逐帧观察实际World Timer；每步有限断言，失败即清理，不重放初始化或生产死亡事件。 */
    virtual bool Update() override
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (bFailed) return true;
        if (FPlatformTime::Seconds()-Started>30.) { Check(false,TEXT("bounded real-world timer contract")); return true; }
        if (!World.IsValid())
        {
            if (!GEngine) return false;
            for (const FWorldContext& Context:GEngine->GetWorldContexts())
                if (UWorld* Candidate=Context.World(); Candidate && Candidate->WorldType==EWorldType::Game && Candidate->GetNetMode()==NM_Standalone && !Candidate->bIsTearingDown)
                    World=Candidate;
            if (!World.IsValid()) return false;
        }
        UWorld* Current=World.Get(); // 引擎拥有，本次Update同步借用。
        ADemoCharacter* Player=nullptr; // 只借真实本机Pawn，测试结束恢复原位置。
        for (TActorIterator<ADemoCharacter> It(Current);It;++It) if (It->GetController() && It->GetDemoAttributes() && It->GetDemoAttributes()->GetHealth()>0.f) { Player=*It; break; }
        if (!Player) return false;
        if (!Owner.IsValid()) return !Initialize(Current,Player);
        for (TActorIterator<ADemoEnemy> It(Current);It;++It) if (It->GetOwner()==Owner.Get()) It->SetActorTickEnabled(false); // 隔离夹具AI移动，生产Spawn/ASC/死亡/Timer均保持真实。
        if (Current->GetTimeSeconds()<Due) return false;
        if (Step==0)
        {
            if (!Check(Spawner->GetAliveCount()==3 && Defeated==1,TEXT("no replacement before the configured interval"))) return true;
            Step=1; Due=Current->GetTimeSeconds()+.5; return false;
        }
        if (Step==1)
        {
            if (!Check(Spawner->GetAliveCount()==4 && Spawner->GetRemaining()==4 && Bosses()==0,TEXT("real interval refills a free slot while three initial enemies remain; alive cap4 and no repeated Boss"))) return true;
            Kill(false);
            if (!Check(Defeated==5 && Clears==0 && !Spawner->IsEncounterComplete(),TEXT("real GE deaths and duplicate notifications give one event each; empty control pressure does not clear"))) return true;
            Step=2; Due=Current->GetTimeSeconds()+.7; return false;
        }
        if (Step==2)
        {
            if (!Check(Spawner->GetAliveCount()>=2 && Spawner->GetAliveCount()<=4 && Spawner->GetRemaining()<=4 && Bosses()==0,TEXT("a later interval creates a bounded ordinary batch without accumulating old waves"))) return true;
            FDemoObjectiveConfig Short=ControlConfig; Short.ControlSeconds=1.f; FString Error; // 合法可配置1秒只用于实际完成边沿；默认30秒已经通过Prepare公开快照验证。
            if (!Check(Objective->PrepareObjective(Short,Session.Get(),Error) && Objective->ActivateObjective(),TEXT("production objective accepts a configurable single-point completion fixture"))) return true;
            SavedPawn=Player; SavedLocation=Player->GetActorLocation(); bMovedPawn=true;
            Player->SetActorLocation(Origin+FVector(0,0,96),false,nullptr,ETeleportType::TeleportPhysics);
            Step=3; Due=Current->GetTimeSeconds()+1.4; return false;
        }
        if (Step==3)
        {
            if (!Check(Candidates==1 && Objective->GetSnapshot().State==EDemoObjectiveState::Resolving && Objective->GetSnapshot().Completed==1,TEXT("actual player occupancy and World sampling complete once and submit only a candidate"))) return true;
            FrozenAlive=Spawner->GetAliveCount(); FrozenDefeated=Defeated;
            if (!Check(Spawner->GetRemaining()==FrozenAlive,TEXT("completion bridge discards pending reinforcements without killing or rewarding live enemies"))) return true;
            Step=4; Due=Current->GetTimeSeconds()+.7; return false;
        }
        if (Step==4)
        {
            if (!Check(Spawner->GetAliveCount()==FrozenAlive && Spawner->GetRemaining()==FrozenAlive && Defeated==FrozenDefeated && Candidates==1,TEXT("completed objective stays stopped past another reinforcement interval"))) return true;
            Spawner->CancelEncounter(true); Objective->CancelObjective(); Session->Invalidate();
            Step=5; Due=Current->GetTimeSeconds()+.7; return false;
        }
        if (!Check(Spawner->GetRemaining()==0 && Spawner->GetAliveCount()==0 && Defeated==FrozenDefeated && Clears==0 && Candidates==1,TEXT("Cancel/session invalidation leaves no queued work, extra rewards or false clear past another interval"))) return true;
        UE_LOG(LogFPSDemo,Display,TEXT("CONTROL_REINFORCEMENT_TEST_SUCCESS default30/legacy3; real interval/cap/Boss isolation; GE event dedup; configurable occupancy completion/stop/cancel. Full Flow/wallet/multiplayer NOT_RUN"));
        return true;
    }
private:
    /** 初始化只运行一次；真实数据表建计划，并在空中独立地板上建立本Owner组件/Session。 */
    bool Initialize(UWorld* Current,ADemoCharacter* Player)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        AFPSDemoGameMode* Mode=Current->GetAuthGameMode<AFPSDemoGameMode>(); FDemoLevelRow Level,BossLevel; FDemoDifficultyRow Difficulty,BossDifficulty; FString Error; // 原生表解析，不伪造敌人GAS/体型定义。
        if (!Mode || !Mode->GetLevelConfig(1,Level,Difficulty,Error) || !Mode->GetLevelConfig(10,BossLevel,BossDifficulty,Error)) return true;
        Owner=Current->SpawnActor<AActor>(); Floor=Current->SpawnActor<AStaticMeshActor>(Origin-FVector(0,0,10),FRotator::ZeroRotator);
        if (!Check(Owner.IsValid() && Floor.IsValid(),TEXT("create isolated owner and real blocking floor"))) return false;
        Floor->SetMobility(EComponentMobility::Movable); Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Floor->SetActorScale3D(FVector(50,50,.2f)); Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Floor->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);
        Objective=NewObject<UDemoObjectiveCoordinatorComponent>(Owner.Get()); Owner->AddInstanceComponent(Objective.Get()); Objective->RegisterComponent();
        Spawner=NewObject<UDemoEnemySpawnComponent>(Owner.Get()); Owner->AddInstanceComponent(Spawner.Get()); Spawner->RegisterComponent();
        Session.Reset(NewObject<UDemoCombatSession>(Owner.Get()));
        if (!Check(Session->Initialize(Current,TEXT("ControlIntervalContract"),1),TEXT("initialize actual isolated combat session"))) return false;
        ControlConfig.Kind=EDemoObjectiveKind::RotatingControl; ControlConfig.ControlAnchors={Origin};
        if (!Check(Objective->PrepareObjective(ControlConfig,Session.Get(),Error) && Objective->GetSnapshot().Total==1 && Objective->GetSnapshot().RequiredSeconds==30.f,TEXT("native default prepares one real control marker requiring30 seconds"))) return false;
        FDemoObjectiveConfig Legacy=ControlConfig; Legacy.ControlSeconds=20.f; Legacy.ControlAnchors={Origin-FVector(0,1000,0),Origin,Origin+FVector(0,1000,0)};
        if (!Check(Objective->PrepareObjective(Legacy,Session.Get(),Error) && Objective->GetSnapshot().Total==3 && Objective->GetSnapshot().RequiredSeconds==20.f,TEXT("legacy three-point20-second runtime configuration remains accepted"))) return false;
        if (!Check(Objective->PrepareObjective(ControlConfig,Session.Get(),Error) && Objective->ActivateObjective(),TEXT("activate authoritative single control objective"))) return false;
        Spawner->Settings.Interval=.05f; Spawner->Settings.MinPlayerDistance=0.f; Spawner->Settings.ControlReinforcementInterval=.5f; Spawner->Settings.ControlReinforcementBatch=2;
        Level.EnemyCount=3; Level.SpecialRows.Reset(); BossLevel.EnemyCount=32;
        const UDataTable* Enemies=Mode->EnemyTableAsset.LoadSynchronous(); FDemoSpawnPlan Plan,BossPlan; // 有限真实模板，Boss仅追加为首发，不进入增援循环。
        if (!Check(DemoSpawnPlan::Build(Level,Difficulty,Enemies,Spawner->Settings,TEXT("ControlIntervalContract"),false,4,Plan,Error)
            && DemoSpawnPlan::Build(BossLevel,BossDifficulty,Enemies,Spawner->Settings,TEXT("ControlIntervalContract"),false,4,BossPlan,Error) && BossPlan.OpeningGroups.Num()==1,TEXT("build production ordinary and Boss template plans"))) return false;
        Plan.OpeningGroups.Add(Plan.Groups.Add(BossPlan.Groups[BossPlan.OpeningGroups[0]])); Plan.bBoss=true; Plan.bControlReinforcements=true;
        Spawner->OnEnemyDefeated.AddRaw(this,&FControlReinforcementLatent::OnDefeated); Spawner->OnCleared.AddRaw(this,&FControlReinforcementLatent::OnCleared); Spawner->OnFailed.AddRaw(this,&FControlReinforcementLatent::OnFailed);
        Objective->OnCompletionCandidate.AddRaw(this,&FControlReinforcementLatent::OnCompleted); // Raw仅当前GT latent续存，Cleanup先Clear再释放本对象。
        FDemoSpawnGeometry Geometry; Geometry.CampaignOffset=FVector(1000,0,110); Geometry.BossOffset=FVector(-1000,0,130);
        if (!Check(Spawner->StartEncounter(Plan,FTransform(Origin),Geometry,Error) && Spawner->GetAliveCount()==4 && Bosses()==1,TEXT("real initial plan creates one Boss and three ordinary Actors within cap"))) return false;
        Kill(true);
        if (!Check(Defeated==1 && Spawner->GetAliveCount()==3,TEXT("real Boss GE death opens one slot without clearing the ordinary population"))) return false;
        Due=Current->GetTimeSeconds()+.2; return true;
    }
    /** 真实死亡事件计数，再重入同一生产通知检验去重；不模拟钱包或广播伪击杀。 */
    void OnDefeated(ADemoEnemy* Enemy) { DEMO_LOG_STATIC_CALL(nullptr); ++Defeated; Spawner->NotifyEnemyDefeated(Enemy); }
    /** 意外清场会在后续断言失败，控制完成只能由Objective提交。 */
    void OnCleared() { DEMO_LOG_STATIC_CALL(nullptr); ++Clears; }
    /** 原生生成失败直接保留原因并结束，不继续生成更多夹具。 */
    void OnFailed(const FString& Reason) { DEMO_LOG_STATIC_CALL(nullptr); Test.AddError(Reason); bFailed=true; }
    /** 复用Flow约定的完成候选桥：先停增援，保留Resolving给正式Flow裁决；不提交奖励。 */
    void OnCompleted(UDemoCombatSession* Expected) { DEMO_LOG_STATIC_CALL(nullptr); Check(Expected==Session.Get(),TEXT("candidate retains the same session")); ++Candidates; Spawner->StopControlReinforcements(); }
    /** 仅本Owner敌人使用实际Health GE；bBossOnly不触碰普通单位/正式场景Actor。 */
    void Kill(bool bBossOnly) { DEMO_LOG_STATIC_CALL(nullptr); for (TActorIterator<ADemoEnemy> It(World.Get());It;++It) if (It->GetOwner()==Owner.Get() && It->IsAlive() && (!bBossOnly || It->IsBoss())) DemoEffects::Apply(It->GetAbilitySystemComponent(),It->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-1000000.f); }
    /** 只数本Owner当前活Boss，证明控制增援不重放首发组。 */
    int32 Bosses() const { DEMO_LOG_STATIC_TICK(nullptr); int32 Count=0; for (TActorIterator<ADemoEnemy> It(World.Get());It;++It) if (It->GetOwner()==Owner.Get() && It->IsAlive() && It->IsBoss()) ++Count; return Count; }
    /** 所有断言保留业务PASS，失败即终止后续步骤。 */
    bool Check(bool bCondition,const TCHAR* Message) { DEMO_LOG_STATIC_CALL(nullptr); Test.TestTrue(Message,bCondition); if (!bCondition) bFailed=true; else UE_LOG(LogFPSDemo,Log,TEXT("CONTROL_REINFORCEMENT_TEST PASS %s"),Message); return bCondition; }
    /** 先解绑所有Raw再撤销生产组件，恢复Pawn并仅销毁本夹具；析构可重复调用。 */
    void Cleanup() { DEMO_LOG_STATIC_CALL(nullptr); if (Spawner.IsValid()) { Spawner->OnEnemyDefeated.Clear(); Spawner->OnCleared.Clear(); Spawner->OnFailed.Clear(); Spawner->CancelEncounter(true); } if (Objective.IsValid()) { Objective->OnCompletionCandidate.Clear(); Objective->CancelObjective(); } if (Session.IsValid()) Session->Invalidate(); if (bMovedPawn && SavedPawn.IsValid()) SavedPawn->SetActorLocation(SavedLocation,false,nullptr,ETeleportType::TeleportPhysics); if (Owner.IsValid()) Owner->Destroy(); if (Floor.IsValid()) Floor->Destroy(); }
    FAutomationTestBase& Test; // Automation拥有，本latent仅在有效测试期间借用。
    double Started; double Due=0.; // 单调墙钟超时与World游戏秒步骤截止，职责分开。
    TWeakObjectPtr<UWorld> World; TWeakObjectPtr<AActor> Owner; TWeakObjectPtr<AStaticMeshActor> Floor; // World拥有的本测试对象，不延长World生命周期。
    TWeakObjectPtr<UDemoObjectiveCoordinatorComponent> Objective; TWeakObjectPtr<UDemoEnemySpawnComponent> Spawner; // Owner组件生命周期覆盖所有GT步骤。
    TStrongObjectPtr<UDemoCombatSession> Session; FDemoObjectiveConfig ControlConfig; // Session由latent显式保活，纯值配置不写资产/存档。
    const FVector Origin=FVector(200000,200000,10000); // 独立50m测试地板，避开Hub及两流送槽。
    TWeakObjectPtr<ADemoCharacter> SavedPawn; FVector SavedLocation=FVector::ZeroVector; // 本人真实Pawn的恢复位置，不改永久属性或来源。
    int32 Step=0,Defeated=0,Clears=0,Candidates=0,FrozenAlive=0,FrozenDefeated=0; // 有限步骤/真实事件计数，停止后快照用于跨间隔比较。
    bool bFailed=false,bMovedPawn=false; // 失败不可重放；仅真实移动后恢复位置。
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoControlReinforcementTest,"FPSDemo.Objectives.ControlReinforcements",EAutomationTestFlags::ClientContext|EAutomationTestFlags::EngineFilter)
/** 仅显式独立进程启动，防普通PIE/用户场景被移动或附加夹具。 */
bool FDemoControlReinforcementTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    if (!FParse::Param(FCommandLine::Get(),TEXT("DemoControlReinforcementTest"))) { AddError(TEXT("Requires isolated -DemoControlReinforcementTest game process")); return false; }
    ADD_LATENT_AUTOMATION_COMMAND(FControlReinforcementLatent(*this)); return true;
}
#endif

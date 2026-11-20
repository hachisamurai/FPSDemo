// 职责：新战役内容计划、核心选择及目标裁决。用法：RunFlow主流程显式调用；依赖既有Spawn/Reward/PS，不另建战局或钱包。
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Content/DemoCampaignDefinition.h"
#include "Game/Content/DemoCampaignSettings.h"
#include "Game/Objectives/DemoObjectiveCoordinatorComponent.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "Engine/DataTable.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"

bool UDemoRunFlowComponent::UsesDemoCampaign() const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // World生命周期内借用。
    return ContentId==TEXT("steam_demo") && ContentRevision>0 && CampaignDefinition && State && !State->bEndless;
}
bool UDemoRunFlowComponent::HasResolvedEncounter(const FString& RunId,int32 Level) const
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 不能将上关或跨World的结果用于当前奖励。
    return GetOwner()->HasAuthority() && !bStopping && bEndingEncounter && CampaignRunId==RunId && State
        && State->Phase==EDemoPhase::Combat && State->LevelNumber==Level && CombatSession && !CombatSession->IsActive()
        && ObjectiveSystem && ObjectiveSystem->GetSnapshot().State==EDemoObjectiveState::Completed;
}
UDemoCampaignDefinition* UDemoRunFlowComponent::FreezeConfiguredCampaign(FString& Json,FString& Fingerprint,FString& Error)
{
    DEMO_LOG_CALL();
    const AFPSDemoGameMode* Mode=CastChecked<AFPSDemoGameMode>(GetOwner()); // 显式GM覆盖优先；默认从项目设置资源路径加载。
    UDemoCampaignDefinition* Authored=Mode->DemoCampaign?Mode->DemoCampaign.Get():GetDefault<UDemoCampaignSettings>()->LoadCampaign(Error); // 只借用本次同步冻结，不用于本轮后续查询。
    if(!Authored || !Authored->ExportSnapshot(Json,Fingerprint,Error))
    { UE_LOG(LogFPSDemo,Warning,TEXT("CAMPAIGN_FREEZE rejected %s"),*Error); return nullptr; }
    UDemoCampaignDefinition* Frozen=UDemoCampaignDefinition::FromSnapshot(this,Json,Fingerprint,Error); // 同一校验契约处理新配置与存档，运行值不随作者资产变化。
    if(Frozen) UE_LOG(LogFPSDemo,Log,TEXT("CAMPAIGN_FREEZE source=%s revision=%d fingerprint=%s"),*Authored->GetPathName(),Frozen->ContentRevision,*Fingerprint);
    return Frozen;
}
bool UDemoRunFlowComponent::BeginCampaignContent()
{
    DEMO_LOG_CALL();
    FString Json,Fingerprint,Error; // 全部准备成功才替换现有轮次，失败不能留下半份内容身份。
    UDemoCampaignDefinition* Frozen=FreezeConfiguredCampaign(Json,Fingerprint,Error);
    if(!Frozen)
    {
        if(ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>()) State->FailureReason=Error;
        return false;
    }
    CommitCampaignContent(Frozen,Json,Fingerprint,static_cast<int32>(GetTypeHash(CampaignRunId)&MAX_int32));
    return true;
}
void UDemoRunFlowComponent::CommitCampaignContent(UDemoCampaignDefinition* Definition,const FString& Json,const FString& Fingerprint,int32 Seed)
{
    DEMO_LOG_CALL();
    CampaignDefinition=Definition; ContentId=Definition?Definition->CampaignId:NAME_None;
    ContentRevision=Definition?Definition->ContentRevision:0; ContentSeed=Definition?Seed:0;
    ContentSnapshot=Definition?Json:FString(); ContentFingerprint=Definition?Fingerprint:FString();
    PreparedCampaign=FDemoCampaignPlan();
    if(ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>())
    {
        State->CampaignPlan=FDemoCampaignPlan(); State->CampaignPlan.CampaignId=ContentId;
        State->CampaignPlan.ContentRevision=ContentRevision; State->CampaignPlan.RunSeed=ContentSeed;
        State->Objective=FDemoObjectiveSnapshot();
        State->ForceNetUpdate();
    }
}
bool UDemoRunFlowComponent::SelectRunCore(ADemoCharacter* Player,int32 Choice)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前服务器阶段，不信任客户端菜单。
    ADemoPlayerState* PS=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr; // 只处理调用者的组件。
    if(bStopping||!State||State->Phase!=EDemoPhase::Hub||State->LevelNumber!=0||!PS||Choice<0||Choice>=DemoRunBuild::CoreCount()
        || !TerminalSystem || (!TerminalSystem->ValidateSession(Player,true).IsEmpty()&&!TerminalSystem->ValidateSession(Player,false).IsEmpty())) // 2026-11-20：升级终端或出发终端的有效本人会话均可选核心；Hub/第0关/未锁定门禁不变。
    { UE_LOG(LogFPSDemo,Log,TEXT("CORE_SELECT rejected phase/player/terminal/index")); return false; }
    const FName Core=DemoRunBuild::CoreAt(Choice); // 中央可信目录，不接受客户端资源路径或自行拼接未知ID。
    UDemoRunBuildComponent* Build=PS->GetRunBuildComponent(); // PS生命周期独立于Pawn。
    if(!Build||!Build->TrySelectCore(Core)) return false;
    // 命令入口统一保存并返回Saved/Pending；此处不重复写盘或吞掉保存状态。
    return true;
}
bool UDemoRunFlowComponent::PrepareCampaignPlan(int32 Stage,FDemoLevelRow& Level,FDemoDifficultyRow& Difficulty,FDemoSpawnPlan& Plan,FString& Error)
{
    DEMO_LOG_CALL();
    if(!CampaignDefinition||!ObjectiveSystem) { Error=TEXT("新战役内容/目标服务未安装"); return false; }
    const TArray<ADemoCharacter*> Members=CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers(); // 本场起始名单，战斗中不会重算倍率。
    for(ADemoCharacter* Member:Members)
        if(!Member->GetPlayerState<ADemoPlayerState>()->GetRunBuildComponent()->HasSelectedCore())
        { Error=TEXT("所有玩家需要先到安全区升级终端选择本轮核心"); UE_LOG(LogFPSDemo,Log,TEXT("DEPARTURE_BUILD %s"),*Error); return false; }
    if(!CampaignDefinition->BuildPlan(ContentSeed,Stage,Members.Num(),PreparedCampaign,Error)) return false;
    const FDemoPartyScaling& Scale=CampaignDefinition->Party[Members.Num()-1]; // 完整配置经BuildPlan校验。
    const UDemoRoomStreamingSettings* Rooms=GetDefault<UDemoRoomStreamingSettings>(); // 区域矩形只在D03作者地图上有意义，复用出发事务对物理房间的选择条件。
    const bool bRegional=Stage==1 && ContentRevision==4 && PreparedCampaign.RoomId==TEXT("D03_L01")
        && PreparedCampaign.Objective==EDemoObjectiveKind::KillAll && Rooms->bEnabled && ContentRevision>=Rooms->MinimumContentRevision;
    TArray<FDemoRegionalSpawnZone> Zones; // 冻结各区待出预算，通用Build仍负责一次性生成兵种目录。
    int32 MaxAlive=Scale.MaxAlive; // 普通计划沿用队伍上限，区域计划还受房间全域上限约束。
    const UDemoRegionalSpawnSettings* Regional=GetDefault<UDemoRegionalSpawnSettings>();
    if (bRegional)
    {
        const FDemoDifficultyRow* Normal=DifficultyTable ? DifficultyTable->FindRow<FDemoDifficultyRow>(DemoCombatConfig::DifficultyName(EDemoDifficulty::Normal),TEXT("FirstRoomBaseline"),false) : nullptr;
        if (!Normal || Normal->EnemyCountMultiplier<=0.f || CampaignDefinition->Party[0].MinionCount<=0.f)
        { Error=TEXT("无法取得普通单人的分区人数基准"); UE_LOG(LogFPSDemo,Warning,TEXT("REGIONAL_CONFIG %s"),*Error); return false; }
        const double RelativeScale=(double(Difficulty.EnemyCountMultiplier)/Normal->EnemyCountMultiplier)
            * (double(Scale.MinionCount)/CampaignDefinition->Party[0].MinionCount); // 配置13表示普通单人实际值，每区独立舍入。
        if (!Regional->ResolveZones(RelativeScale, Zones, Level.EnemyCount, Error)) return false;
        MaxAlive=FMath::Min(MaxAlive,Regional->MaxAlive);
    }
    else
    {
        const float ObjectiveCount=PreparedCampaign.Objective==EDemoObjectiveKind::KillAll?1.f:CampaignDefinition->ObjectiveCountMultiplier;
        Level.EnemyCount=FMath::Clamp(FMath::RoundToInt(Level.EnemyCount*ObjectiveCount*Scale.MinionCount*Difficulty.EnemyCountMultiplier),1,1000000);
    }
    Level.ArenaIndex=PreparedCampaign.ArenaIndex;
    if(!DemoSpawnPlan::Build(Level,Difficulty,EnemyTable,SpawnSystem->Settings,CampaignRunId,false,MaxAlive,Plan,Error)) return false;
    if (bRegional)
    {
        Plan.RegionalZones=MoveTemp(Zones);
        Plan.RegionalWaveSize=Regional->WaveSize;
        Plan.RegionalWaveInterval=Regional->WaveInterval;
        UE_LOG(LogFPSDemo,Log,TEXT("REGIONAL_PLAN room=%s party=%d queued=%d maxAlive=%d batch=%d interval=%.2f"),
            *PreparedCampaign.RoomId.ToString(),Members.Num(),Plan.GetTotalCount(),Plan.MaxAlive,Plan.RegionalWaveSize,Plan.RegionalWaveInterval);
    }
    Plan.bControlReinforcements=CampaignDefinition->ContentRevision>=4 && PreparedCampaign.Objective==EDemoObjectiveKind::RotatingControl; // 旧冻结战役保留有限队列，修订4才启用持续增援。
    if (Plan.bControlReinforcements)
        Plan.ControlReinforcementBatch=FMath::Clamp(FMath::RoundToInt(Plan.ControlReinforcementBatch*Difficulty.EnemyCountMultiplier),1,64); // 增援同样按难度调整，实际生成仍受本场MaxAlive约束，不积压错过批次。
    for(FDemoSpawnGroup& Group:Plan.Groups) // 含Boss支持组，所有生成路径共用同一人数HP快照。
        Group.Stats.Health=FMath::Min(1.e12f,Group.Stats.Health*(Group.Stats.bBoss?Scale.BossHealth:Scale.MinionHealth));
    for(auto& Entry:Plan.Minions) Entry.Value.Health=FMath::Min(1.e12f,Entry.Value.Health*Scale.MinionHealth); // 保持诊断摘要一致。
    Plan.Boss.Health=FMath::Min(1.e12f,Plan.Boss.Health*Scale.BossHealth);
    return Plan.Validate(Error);
}
void UDemoRunFlowComponent::HandleSpawnerCleared()
{
    DEMO_LOG_CALL();
    if(bStopping||bEndingEncounter||!CombatSession||!CombatSession->IsActive()) return;
    if(ObjectiveSystem) ObjectiveSystem->UpdatePopulation(0,0); // 原歼灭和新任务共用裁决；不直接领奖。
}
void UDemoRunFlowComponent::HandleObjectiveSnapshot(const FDemoObjectiveSnapshot& Snapshot)
{
    DEMO_LOG_TICK();
    if(bStopping) return;
    if(ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>())
    { State->Objective=Snapshot; State->ForceNetUpdate(); }
}
void UDemoRunFlowComponent::HandleObjectiveCompleted(UDemoCombatSession* Session)
{
    DEMO_LOG_CALL();
    if(bStopping||bEndingEncounter||bObjectiveCandidate||Session!=CombatSession||!Session||!Session->IsActive())
    { UE_LOG(LogFPSDemo,Log,TEXT("OBJECTIVE_OUTCOME rejected stale/duplicate")); return; }
    SpawnSystem->StopControlReinforcements(); // 占点累计完成当帧即撤销后续批次；同帧团灭仍由原延迟裁决处理。
    bObjectiveCandidate=true;
    OutcomeTimer=GetWorld()->GetTimerManager().SetTimerForNextTick(this,&UDemoRunFlowComponent::ResolveObjectiveOutcome); // UObject弱绑定，退出战斗显式取消。
}
void UDemoRunFlowComponent::HandleObjectiveError(UDemoCombatSession* Session,const FString& Error)
{
    DEMO_LOG_CALL();
    if(bStopping||bEndingEncounter||Session!=CombatSession) return;
    FailRun(Error); // 系统失败不清永久账本，不覆盖出发前检查点。
}
void UDemoRunFlowComponent::ResolveObjectiveOutcome()
{
    DEMO_LOG_CALL();
    OutcomeTimer.Invalidate();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前阶段重新核验，延迟回调不能沿用旧事实。
    if(bStopping||bEndingEncounter||!bObjectiveCandidate||!State||State->Phase!=EDemoPhase::Combat||!CombatSession||!CombatSession->IsActive()) return;
    bool bAnyAlive=false; // 查真实HP而非通知到达顺序，复活只能在裁决之后。
    for(ADemoCharacter* Member:CastChecked<AFPSDemoGameMode>(GetOwner())->GetCoopRoster()->GetPlayers())
        bAnyAlive|=!Member->IsCoopEliminated() && Member->GetDemoAttributes() && Member->GetDemoAttributes()->GetHealth()>0.f;
    if(!bAnyAlive) { bObjectiveCandidate=false; NotifyPlayerDied(); return; }
    bEndingEncounter=true;
    ObjectiveSystem->CompleteObjective(); // 停目标并保留Completed，再撤销会话，清理不作为击杀。
    CombatSession->Invalidate();
    SpawnSystem->CancelEncounter(true);
    FinishLevel();
}

#include "Spawning/DemoSpawnPlan.h"
#include "Game/DemoEncounterRules.h"
#include "AI/DemoEnemy.h"
#include "Debug/DemoLog.h"

bool DemoSpawnPlan::ValidateSettings(const FDemoSpawnSettings& Settings, FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    if (!FMath::IsFinite(Settings.Interval) || Settings.Interval < .05f || Settings.Interval > 10.f
        || Settings.BatchLimit < 4 || Settings.BatchLimit > 64 || Settings.CandidateAttempts < 1 || Settings.CandidateAttempts > 64
        || Settings.BlockedPassLimit < 1 || Settings.BlockedPassLimit > 600
        || !FMath::IsFinite(Settings.MinPlayerDistance) || Settings.MinPlayerDistance < 0.f || Settings.MinPlayerDistance > 5000.f)
    { Error = TEXT("刷怪间隔、批量、候选点、重试次数或安全距离非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_CONFIG %s"), *Error); return false; }
    for (const auto& Entry : Settings.MinionRows) // 只读行映射；旧五类战术不自动混入当前生产兵种池。
        if ((Entry.Key != EDemoEnemyRole::Melee && Entry.Key != EDemoEnemyRole::Charger && Entry.Key != EDemoEnemyRole::Ranged) || Entry.Value.IsNone())
        { Error = TEXT("刷怪兵种模板映射非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_CONFIG %s"), *Error); return false; }
    for (const auto& Entry : Settings.MinionClasses) // 蓝图类由Settings的UPROPERTY持有；拒绝抽象类而不是生成时静默回退。
        if ((Entry.Key != EDemoEnemyRole::Melee && Entry.Key != EDemoEnemyRole::Charger && Entry.Key != EDemoEnemyRole::Ranged)
            || !Entry.Value || Entry.Value->HasAnyClassFlags(CLASS_Abstract))
        { Error = TEXT("刷怪兵种蓝图类为空、抽象或兵种不支持"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_CONFIG %s"), *Error); return false; }
    if (Settings.BossClass && Settings.BossClass->HasAnyClassFlags(CLASS_Abstract))
    { Error = TEXT("Boss蓝图类不能是抽象类"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_CONFIG %s"), *Error); return false; }
    return true;
}

bool DemoSpawnPlan::Build(const FDemoLevelRow& Level, const FDemoDifficultyRow& Difficulty, const UDataTable* Enemies,
    const FDemoSpawnSettings& Settings, const FString& RunId, bool bEndless, int32 MaxAlive, FDemoSpawnPlan& OutPlan, FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs level=%d"), __FUNCTION__, Level.MonsterLevel);
    if (!ValidateSettings(Settings, Error)) return false;
    if (!Enemies || Enemies->GetRowStruct() != FDemoEnemyRow::StaticStruct() || RunId.IsEmpty() || Level.MonsterLevel < 1
        || Level.ArenaIndex < 0 || Level.ArenaIndex > 2 || Level.EnemyCount < 1 || Level.EnemyCount > 1000000 || MaxAlive < 1 || MaxAlive > 64
        || (Level.MonsterLevel % DemoEncounterRules::BossInterval == 0) == Level.BossRow.IsNone()
        || (Level.MonsterLevel % DemoEncounterRules::RangedInterval == 0 && (Level.EnemyCount < 3 || MaxAlive < (Level.BossRow.IsNone() ? 3 : 4))))
    { Error = TEXT("刷怪计划的关号、区域、数量或Boss/三兵种并存预算非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_PLAN %s"), *Error); return false; }
    FDemoSpawnPlan Result; // 全部成功后才提交输出，禁止部分计划开始运行。
    Result.RunId = RunId; Result.Level = Level.MonsterLevel; Result.ArenaIndex = Level.ArenaIndex;
    Result.MinionCount = Level.EnemyCount; Result.MaxAlive = MaxAlive; Result.bBoss = !Level.BossRow.IsNone(); Result.bReinforcementLayout = bEndless;
    for (const EDemoEnemyRole MinionRole : {EDemoEnemyRole::Melee, EDemoEnemyRole::Charger, EDemoEnemyRole::Ranged}) // 预解析三兵种，包括下一关才用的远程映射，尽早暴露配置错误。
    {
        const FName* OverrideRow = Settings.MinionRows.Find(MinionRole); // 本次借用；未配置时沿用原关卡数值。
        const FName RowName = OverrideRow ? *OverrideRow : Level.EnemyRow; // 稳定资产行键，不使用UI名称。
        const FDemoEnemyRow* Row = Enemies->FindRow<FDemoEnemyRow>(RowName, TEXT("SpawnPlan"), false); // 外层完整表校验后只检查角色引用。
        if (!Row || Row->bBoss) { Error = FString::Printf(TEXT("小怪模板无效: %s"), *RowName.ToString()); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_PLAN %s"), *Error); return false; }
        FDemoEnemySpawnStats Stats = DemoCombatConfig::Resolve(*Row, Difficulty, Level); // 深拷贝数值，不持有表行地址。
        Stats.bUseTactics = true; Stats.Tactics.bEnabled = true; Stats.Tactics.Role = MinionRole;
        Result.Minions.Add(MinionRole, Stats);
    }
    if (Result.bBoss)
    {
        const FDemoEnemyRow* Row = Enemies->FindRow<FDemoEnemyRow>(Level.BossRow, TEXT("SpawnBossPlan"), false); // 当前关Boss模板借用到Resolve结束。
        if (!Row || !Row->bBoss) { Error = TEXT("Boss模板缺失或不是Boss"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_PLAN %s"), *Error); return false; }
        Result.Boss = DemoCombatConfig::Resolve(*Row, Difficulty, Level);
    }
    OutPlan = MoveTemp(Result);
    return true;
}

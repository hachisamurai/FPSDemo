#include "Game/DemoEncounterRules.h"
#include "Debug/DemoLog.h"

bool DemoEncounterRules::AssignMinionRole(int32 Level, int32 Index, uint32 Seed, FDemoEnemySpawnStats& Stats)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs level=%d minion=%d"), __FUNCTION__, Level, Index);
    if (Level < 1 || Index < 0 || Stats.bBoss)
    { UE_LOG(LogFPSDemo, Warning, TEXT("ENCOUNTER_ROLE rejected: level/index/boss")); return false; }
    TArray<EDemoEnemyRole> Pool = { EDemoEnemyRole::Melee, EDemoEnemyRole::Charger }; // 局部随机袋，每轮覆盖允许兵种，杜绝低关混入旧远程追击兵。
    if (Level % RangedInterval == 0) Pool.Add(EDemoEnemyRole::Ranged);
    // 同一RunId、关卡、小怪序号得到相同结果；无尽分批重试不会重新抽类型，普通检查点恢复可复现。
    const uint32 BagSeed = HashCombine(HashCombine(Seed, GetTypeHash(Level)), GetTypeHash(Index / Pool.Num())); // 每2/3只更换随机袋，Boss不消耗小怪序号。
    FRandomStream Random(static_cast<int32>(BagSeed)); // 只在本次调用使用，不依赖全局随机调用顺序。
    for (int32 Position = Pool.Num() - 1; Position > 0; --Position) // Fisher-Yates打乱袋内次序，避免固定循环造成同一出生点总是同一兵种。
        Pool.Swap(Position, Random.RandRange(0, Position));
    Stats.bUseTactics = true;
    Stats.Tactics.bEnabled = true; // 战役规则明确启用对应攻击组件；保留模板的前摇、伤害、拉距等调参。
    Stats.Tactics.Role = Pool[Index % Pool.Num()];
    UE_LOG(LogFPSDemo, Log, TEXT("ENCOUNTER_ROLE level=%d minion=%d role=%d seed=%u"), Level, Index, static_cast<int32>(Stats.Tactics.Role), BagSeed);
    return true;
}

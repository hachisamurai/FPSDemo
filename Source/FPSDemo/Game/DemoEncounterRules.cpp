// 职责：解析基础角色随机袋；特殊替换槽在SpawnPlan处理，角色枚举追加保持旧序列化稳定。
#include "Game/DemoEncounterRules.h"
#include "Debug/DemoLog.h"

UDemoEncounterPolicy::UDemoEncounterPolicy()
{
    DEMO_LOG_CALL(); BaseRoles={EDemoEnemyRole::Melee,EDemoEnemyRole::Charger}; PeriodicRoles={EDemoEnemyRole::Ranged};
}
bool UDemoEncounterPolicy::Resolve(int32 Level,int32 Count,int32 MaxAlive,bool bHasBossRow,FDemoEncounterRecipe& Out,FString& Error) const
{
    DEMO_LOG_CALL();
    if (Level<1||Count<1||Count>1000000||MaxAlive<1||MaxAlive>64||MixedInterval<1||ExtraBossInterval<1||BaseRoles.IsEmpty())
    { Error=TEXT("遭遇策略的关号、数量、周期或基本角色池非法"); UE_LOG(LogFPSDemo,Warning,TEXT("ENCOUNTER_POLICY %s"),*Error); return false; }
    FDemoEncounterRecipe Recipe; // 完整验证后才提交，失败不会把部分配方传给执行器。
    Recipe.Roles=BaseRoles; Recipe.bExtraBoss=Level%ExtraBossInterval==0;
    if (Level%MixedInterval==0) Recipe.Roles.Append(PeriodicRoles);
    TSet<EDemoEnemyRole> Unique; // 拒绝重复权重，保留每袋覆盖全部角色的规则。
    for (const EDemoEnemyRole Role:Recipe.Roles) // 每项都必须对应已有单角色执行策略。
    {
        if (Role==EDemoEnemyRole::Mixed||static_cast<uint8>(Role)>static_cast<uint8>(EDemoEnemyRole::Exploder)||Unique.Contains(Role))
        { Error=TEXT("遭遇角色池重复或包含不支持的角色"); UE_LOG(LogFPSDemo,Warning,TEXT("ENCOUNTER_POLICY %s"),*Error); return false; }
        Unique.Add(Role);
    }
    Recipe.RequiredConcurrent=Level%MixedInterval==0?Recipe.Roles.Num()+(Recipe.bExtraBoss?1:0):1;
    if (Recipe.bExtraBoss!=bHasBossRow||Count<Recipe.Roles.Num()&&Level%MixedInterval==0||MaxAlive<Recipe.RequiredConcurrent)
    { Error=TEXT("遭遇Boss引用或混编并存预算不满足策略"); UE_LOG(LogFPSDemo,Warning,TEXT("ENCOUNTER_POLICY %s"),*Error); return false; }
    Out=MoveTemp(Recipe); return true;
}

bool DemoEncounterRules::AssignMinionRole(int32 Level, int32 Index, uint32 Seed, FDemoEnemySpawnStats& Stats)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs level=%d minion=%d"), __FUNCTION__, Level, Index);
    if (Level < 1 || Index < 0 || Stats.bBoss)
    { UE_LOG(LogFPSDemo, Warning, TEXT("ENCOUNTER_ROLE rejected: level/index/boss")); return false; }
    FDemoEncounterRecipe Recipe; FString Error; // 旧接口兼容层也使用原生策略，不再复制一份角色池规则。
    if (!GetDefault<UDemoEncounterPolicy>()->Resolve(Level,1000000,64,Level%BossInterval==0,Recipe,Error)) return false;
    TArray<EDemoEnemyRole> Pool=Recipe.Roles; // 局部袋仅按组数分配，不按无尽怪物总量分配。
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

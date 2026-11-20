// 职责：冻结生成模板和稳定选择；特殊兵种有限替换、Boss按RunId选定，执行器不再次查表。
#include "Spawning/DemoSpawnPlan.h"
#include "Game/DemoEncounterRules.h"
#include "AI/DemoEnemy.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"

namespace
{
    // 项目设置和冻结计划共用的结构校验。矩形含边界，区域共享边或重叠会导致一次位置命中两区。
    bool ValidateRegionalLayout(const TArray<FDemoRegionalSpawnZone>& Zones, int32 WaveSize, float Interval, int32& OutTotal, FString& Error)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        if (Zones.IsEmpty() || Zones.Num() > 8 || WaveSize < 1 || WaveSize > 64 ||
            !FMath::IsFinite(Interval) || Interval < 5.f || Interval > 600.f)
        {
            Error = TEXT("刷怪分区数、批量或间隔越界");
            UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_PLAN %s"), *Error);
            return false;
        }
        TSet<FName> Names; // 本次验证持有，拒绝同一ID对应多个预算。
        TSet<int32> AssignedPoints; // 每个实际刷怪标记最多分配给一个战斗区。
        int32 Total = 0; // 各项<=1,000,000且最多8区，无int32累加溢出。
        for (int32 Index = 0; Index < Zones.Num(); ++Index)
        {
            const FDemoRegionalSpawnZone& Zone = Zones[Index]; // 只在同步校验中借用。
            if (Zone.Id.IsNone() || Names.Contains(Zone.Id) || Zone.EnemyCount < 1 || Zone.EnemyCount > 1000000 ||
                Zone.SpawnPointIndices.IsEmpty() || Zone.SpawnPointIndices.Num() > 8 ||
                Zone.LocalMin.ContainsNaN() || Zone.LocalMax.ContainsNaN() ||
                Zone.LocalMin.GetAbsMax() > 1000000. || Zone.LocalMax.GetAbsMax() > 1000000. ||
                Zone.LocalMin.X >= Zone.LocalMax.X || Zone.LocalMin.Y >= Zone.LocalMax.Y)
            {
                Error = FString::Printf(TEXT("刷怪分区名称、预算、矩形或标记数量非法：%s"), *Zone.Id.ToString());
                UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_PLAN %s"), *Error);
                return false;
            }
            for (int32 Prior = 0; Prior < Index; ++Prior)
            {
                const FDemoRegionalSpawnZone& Other = Zones[Prior]; // 只比较已通过基本校验的前序区。
                if (Zone.LocalMin.X <= Other.LocalMax.X && Zone.LocalMax.X >= Other.LocalMin.X &&
                    Zone.LocalMin.Y <= Other.LocalMax.Y && Zone.LocalMax.Y >= Other.LocalMin.Y)
                {
                    Error = FString::Printf(TEXT("刷怪分区矩形重叠：%s / %s"), *Zone.Id.ToString(), *Other.Id.ToString());
                    UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_PLAN %s"), *Error);
                    return false;
                }
            }
            for (const int32 Point : Zone.SpawnPointIndices)
            {
                if (Point < 0 || Point >= 8 || AssignedPoints.Contains(Point))
                {
                    Error = FString::Printf(TEXT("刷怪分区标记重复或越界：%s[%d]"), *Zone.Id.ToString(), Point);
                    UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_PLAN %s"), *Error);
                    return false;
                }
                AssignedPoints.Add(Point);
            }
            Names.Add(Zone.Id);
            Total += Zone.EnemyCount;
        }
        if (Total > 1000000)
        {
            Error = TEXT("分区合计超过本关总预算上限");
            UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_PLAN %s"), *Error);
            return false;
        }
        OutTotal = Total;
        return true;
    }
}

bool UDemoRegionalSpawnSettings::ResolveZones(double PopulationScale, TArray<FDemoRegionalSpawnZone>& OutZones, int32& OutTotal, FString& Error) const
{
    DEMO_LOG_CALL();
    // 只在真实D03首关装配三个作战区。缺少配置时拒绝开战，不能把空数组传成旧的全图开场调度。
    if (!FMath::IsFinite(PopulationScale) || PopulationScale <= 0. || MaxAlive < 1 || MaxAlive > 64 || Zones.Num() != 3)
    {
        Error = TEXT("首关须配置三个作战区、有效人数倍率与存活上限");
        UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_CONFIG %s"), *Error);
        return false;
    }
    int32 Total = 0; // 先验作者总数，再核缩放后的总数。
    if (!ValidateRegionalLayout(Zones, WaveSize, WaveInterval, Total, Error)) return false;
    Total = 0;
    TArray<FDemoRegionalSpawnZone> Frozen = Zones; // 服务端独立副本，项目设置与运行预算互不修改。
    for (FDemoRegionalSpawnZone& Zone : Frozen)
    {
        const double Scaled = Zone.EnemyCount * PopulationScale; // 普通单人基准×队伍系数×所选难度/普通难度。
        if ((Zone.Id != TEXT("B") && Zone.Id != TEXT("C") && Zone.Id != TEXT("D")) || !FMath::IsFinite(Scaled) || Scaled > 1000000.)
        {
            Error = FString::Printf(TEXT("首关区域ID或缩放人数非法：%s"), *Zone.Id.ToString());
            UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_CONFIG %s"), *Error);
            return false;
        }
        Zone.EnemyCount = FMath::Max(1, FMath::RoundToInt32(Scaled));
        Total += Zone.EnemyCount;
    }
    if (Total > 1000000)
    {
        Error = TEXT("首关分区缩放后超过单关敌人总数上限");
        UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_CONFIG %s"), *Error);
        return false;
    }
    OutZones = MoveTemp(Frozen);
    OutTotal = Total;
    return true;
}

bool DemoSpawnPlan::ValidateSettings(const FDemoSpawnSettings& Settings, FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    if (!FMath::IsFinite(Settings.Interval) || Settings.Interval < .05f || Settings.Interval > 10.f
        || Settings.BatchLimit < 4 || Settings.BatchLimit > 64 || Settings.CandidateAttempts < 1 || Settings.CandidateAttempts > 64
        || !FMath::IsFinite(Settings.ControlReinforcementInterval) || Settings.ControlReinforcementInterval < .5f || Settings.ControlReinforcementInterval > 60.f
        || Settings.ControlReinforcementBatch < 1 || Settings.ControlReinforcementBatch > 64
        || Settings.BlockedPassLimit < 1 || Settings.BlockedPassLimit > 600
        || !FMath::IsFinite(Settings.MinPlayerDistance) || Settings.MinPlayerDistance < 0.f || Settings.MinPlayerDistance > 5000.f)
    { Error = TEXT("刷怪间隔、批量、候选点、重试次数或安全距离非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_CONFIG %s"), *Error); return false; }
    for (const auto& Entry : Settings.MinionRows) // 只读行映射；旧五类战术不自动混入当前生产兵种池。
        if ((Entry.Key==EDemoEnemyRole::Mixed||static_cast<uint8>(Entry.Key)>static_cast<uint8>(EDemoEnemyRole::Exploder)) || Entry.Value.IsNone()) // EDemoEnemyRole是战术角色（封锁眼复用Ranged），与RosterKind无关。
        { Error = TEXT("刷怪兵种模板映射非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_CONFIG %s"), *Error); return false; }
    for (const auto& Entry : Settings.MinionClasses) // 蓝图类由Settings的UPROPERTY持有；拒绝抽象类而不是生成时静默回退。
        if ((Entry.Key==EDemoEnemyRole::Mixed||static_cast<uint8>(Entry.Key)>static_cast<uint8>(EDemoEnemyRole::Exploder))
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
        )
    { Error = TEXT("刷怪计划的关号、区域、数量或Boss/三兵种并存预算非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_PLAN %s"), *Error); return false; }
    const UDemoEncounterPolicy* Policy=Settings.EncounterPolicy?Settings.EncounterPolicy.Get():GetDefault<UDemoEncounterPolicy>(); // 同步解析策略，运行期只持有值配方。
    FDemoEncounterRecipe Recipe; // 决定组池与首发组，实际生成器不再解释关号取模。
    if (!Policy->Resolve(Level.MonsterLevel,Level.EnemyCount,MaxAlive,!Level.BossRow.IsNone(),Recipe,Error)) return false;
    FDemoSpawnPlan Result; // 全部成功后才提交输出，禁止部分计划开始运行。
    Result.RunId = RunId; Result.Level = Level.MonsterLevel; Result.ArenaIndex = Level.ArenaIndex;
    Result.MinionCount = Level.EnemyCount; Result.MaxAlive = MaxAlive; Result.bBoss = !Level.BossRow.IsNone(); Result.bReinforcementLayout = bEndless;
    Result.ControlReinforcementInterval = Settings.ControlReinforcementInterval; Result.ControlReinforcementBatch = Settings.ControlReinforcementBatch; // 只冻结配置，不在此处开启任务或重复应用数量倍率。
    Result.SelectionSeed=GetTypeHash(RunId); // 保留旧随机袋种子算法。
    for (const EDemoEnemyRole MinionRole : Recipe.Roles) // 只按策略输出解析组，不在执行器枚举三兵种。
    {
        const FName* OverrideRow = Settings.MinionRows.Find(MinionRole); // 本次借用；未配置时沿用原关卡数值。
        FName RowName = OverrideRow ? *OverrideRow : Level.EnemyRow; // 显式映射优先；新角色必须使用对应模板，不能退化成Drone接触攻击。
        if (!OverrideRow)
        {
            switch (MinionRole)
            {
            case EDemoEnemyRole::Guard: RowName=TEXT("Guard"); break;
            case EDemoEnemyRole::Bombardier: RowName=TEXT("Bombardier"); break;
            case EDemoEnemyRole::Mender: RowName=TEXT("Mender"); break;
            case EDemoEnemyRole::Exploder: RowName=TEXT("Exploder"); break;
            default: break;
            }
        }
        const FDemoEnemyRow* Row = Enemies->FindRow<FDemoEnemyRow>(RowName, TEXT("SpawnPlan"), false); // 外层完整表校验后只检查角色引用。
        if (!Row || Row->bBoss) { Error = FString::Printf(TEXT("小怪模板无效: %s"), *RowName.ToString()); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_PLAN %s"), *Error); return false; }
        FDemoEnemySpawnStats Stats = DemoCombatConfig::Resolve(*Row, Difficulty, Level); // 深拷贝数值，不持有表行地址。
        Stats.bUseTactics = true; Stats.Tactics.bEnabled = true; Stats.Tactics.Role = MinionRole;
        Result.Minions.Add(MinionRole, Stats); // 只读兼容摘要，执行器使用下方通用组。
        FDemoSpawnGroup Group; // 每种模板只复制一次，实际数量用计数器表达。
        Group.Id=FName(*FString::Printf(TEXT("Role%d"),static_cast<int32>(MinionRole))); Group.Stats=Stats;
        const TSubclassOf<ADemoEnemy>* OverrideClass=Settings.MinionClasses.Find(MinionRole); // 配置拥有的类，仅此调用借用。
        Group.EnemyClass=OverrideClass?*OverrideClass:TSubclassOf<ADemoEnemy>(ADemoEnemy::StaticClass());
        Group.Body=Group.EnemyClass->GetDefaultObject<ADemoEnemy>()->ResolveBodyDefinition(Stats.Definition,Stats.bBoss);
        if (!Group.Body.Validate(Error)) return false;
        Result.CyclingGroups.Add(Result.Groups.Add(MoveTemp(Group)));
    }
    if (Result.bBoss)
    {
        const FName BossRow=Level.BossPool.IsEmpty()?Level.BossRow:Level.BossPool[HashCombine(GetTypeHash(RunId),GetTypeHash(Level.MonsterLevel))%Level.BossPool.Num()]; // 同存档同关稳定，重试不重新抽签。
        const FDemoEnemyRow* Row = Enemies->FindRow<FDemoEnemyRow>(BossRow, TEXT("SpawnBossPlan"), false); // 当前关Boss模板借用到Resolve结束。
        if (!Row || !Row->bBoss) { Error = TEXT("Boss模板缺失或不是Boss"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_PLAN %s"), *Error); return false; }
        Result.Boss = DemoCombatConfig::Resolve(*Row, Difficulty, Level);
        FDemoSpawnGroup Group; // 专用锚点只是布局选择，不把Boss分支传进调度器。
        Group.Id=TEXT("Boss"); Group.Stats=Result.Boss; Group.bUseAnchor=true;
        Group.EnemyClass=Settings.BossClass?Settings.BossClass:TSubclassOf<ADemoEnemy>(ADemoEnemy::StaticClass());
        Group.Body=Group.EnemyClass->GetDefaultObject<ADemoEnemy>()->ResolveBodyDefinition(Group.Stats.Definition,true);
        if (!Group.Body.Validate(Error)) return false;
        Result.OpeningGroups.Add(Result.Groups.Add(MoveTemp(Group)));
    }
    if (Level.SpecialRows.Num()>7||Level.EnemyCount<Result.CyclingGroups.Num()+Level.SpecialRows.Num())
    { Error=TEXT("特殊兵种替换槽过多，必须保留完整基础兵种首袋"); return false; }
    TMap<EDemoRosterKind,int32> SpecialCounts; // 每关有限替换上限，不重复循环特殊怪；与召唤并发限制一致。
    TArray<FName> ExtraRows=Level.SpecialRows; // 拷贝有限7项；Boss支持模板额外冻结但不自动出场。
    const int32 SpecialCount=ExtraRows.Num(); // 固定分界，后续追加的只属于SupportGroups。
    if (Result.bBoss&&Result.Boss.Roster.Kind==EDemoRosterKind::Warden) { ExtraRows.Add(TEXT("Guard")); ExtraRows.Add(TEXT("Bombardier")); }
    if (Result.bBoss&&Result.Boss.Roster.Kind==EDemoRosterKind::Weaver) ExtraRows.Add(TEXT("StormAnchor"));
    for (int32 Index=0;Index<ExtraRows.Num();++Index) // O(特殊类型数)，与百万无尽数量无关。
    {
        const FDemoEnemyRow* Row=Enemies->FindRow<FDemoEnemyRow>(ExtraRows[Index],TEXT("RosterPlan"),false); // 表仅此同步解析借用。
        if (!Row||Row->bBoss||!Row->Roster.IsValid()||Row->Roster.Kind==EDemoRosterKind::None)
        { Error=TEXT("特殊兵种/召唤模板缺失或类型非法"); return false; }
        const EDemoRosterKind Kind=Row->Roster.Kind; // 只允许DemoRosterRules列出的特殊小怪，Anchor仅可作为支持组。
        if (Index<SpecialCount)
        {
            if (!DemoRosterRules::IsSpecialMinion(Kind))
            { Error=TEXT("替换槽只能引用特殊小怪（护盾/迫击/修复/爆裂/封锁眼）"); return false; }
            if (++SpecialCounts.FindOrAdd(Kind)>DemoRosterRules::SpecialCap(Kind)) { Error=TEXT("特殊小怪超过单关上限（修复者1，其余2）"); return false; }
        }
        FDemoSpawnGroup Group; // 使用同一实际体型验证路径，不能靠名字伪造碰撞尺寸。
        Group.Id=ExtraRows[Index]; Group.Stats=DemoCombatConfig::Resolve(*Row,Difficulty,Level);
        const TSubclassOf<ADemoEnemy>* OverrideClass=Settings.MinionClasses.Find(Row->Tactics.Role); // 特殊替换同样支持蓝图类覆盖，不把新模型接入锁死在原生类。
        Group.EnemyClass=OverrideClass?*OverrideClass:TSubclassOf<ADemoEnemy>(ADemoEnemy::StaticClass());
        Group.Body=Group.EnemyClass->GetDefaultObject<ADemoEnemy>()->ResolveBodyDefinition(Group.Stats.Definition,false);
        if (!Group.Body.Validate(Error)) return false;
        const int32 Added=Result.Groups.Add(MoveTemp(Group)); // 有限组索引，计划本身拥有副本。
        if (Index<SpecialCount) Result.SpecialGroups.Add(Added); else Result.SupportGroups.Add(ExtraRows[Index],Added);
    }
    if (!Result.Validate(Error)) return false;
    OutPlan = MoveTemp(Result);
    return true;
}

int32 FDemoSpawnPlan::GetTotalCount() const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__); return MinionCount+OpeningGroups.Num();
}
bool FDemoSpawnPlan::Validate(FString& Error) const
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (RunId.IsEmpty()||Level<1||MinionCount<1||MinionCount>1000000||MaxAlive<1||MaxAlive>64||Groups.IsEmpty()||CyclingGroups.IsEmpty())
    { Error=TEXT("生成组计划为空或预算非法"); return false; }
    if (!RegionalZones.IsEmpty())
    {
        int32 RegionalTotal = 0; // 包含未进过的区域，避免批间清空时提前完成歼灭。
        if (!ValidateRegionalLayout(RegionalZones, RegionalWaveSize, RegionalWaveInterval, RegionalTotal, Error)) return false;
        if (RegionalTotal != MinionCount || bBoss || bControlReinforcements || !OpeningGroups.IsEmpty() || !SupportGroups.IsEmpty())
        {
            Error = TEXT("有限分区预算与关卡总数、首发或增援策略不兼容");
            UE_LOG(LogFPSDemo, Warning, TEXT("REGIONAL_PLAN %s"), *Error);
            return false;
        }
    }
    if (!FMath::IsFinite(ControlReinforcementInterval)||ControlReinforcementInterval<.5f||ControlReinforcementInterval>60.f
        ||ControlReinforcementBatch<1||ControlReinforcementBatch>64)
    { Error=TEXT("控制增援的冻结间隔或单批预算非法"); return false; }
    for (const int32 Index:OpeningGroups) if (!Groups.IsValidIndex(Index)) { Error=TEXT("首发组索引非法"); return false; } // 只校验引用，不推断兵种。
    for (const int32 Index:CyclingGroups) if (!Groups.IsValidIndex(Index)) { Error=TEXT("循环组索引非法"); return false; } // 任意数量组都可复用执行器。
    if (bControlReinforcements)
        for (const int32 Index:CyclingGroups) if (Groups[Index].Stats.bBoss || Groups[Index].Stats.Roster.Kind != EDemoRosterKind::None)
        { Error=TEXT("控制增援只能复用普通循环组，不能重复Boss或特殊替换单位"); return false; }
    for (const int32 Index:SpecialGroups) if (!Groups.IsValidIndex(Index)) { Error=TEXT("特殊替换组索引非法"); return false; } // 不允许越界替换返回未定义模板。
    for (const auto& Entry:SupportGroups) if (Entry.Key.IsNone()||!Groups.IsValidIndex(Entry.Value)) { Error=TEXT("Boss支持组引用非法"); return false; } // 召唤延迟执行前先验证所有冻结引用。
    for (const FDemoSpawnGroup& Group:Groups) // 类和最终体型必须一起可用，否则禁止半份计划开始运行。
        if (Group.Id.IsNone()||!Group.EnemyClass||Group.EnemyClass->HasAnyClassFlags(CLASS_Abstract)||!Group.Body.Validate(Error)) return false;
    return true;
}
int32 FDemoSpawnPlan::SelectGroup(int32 Ordinal,int32& FormationSlot) const
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs ordinal=%d"),__FUNCTION__,Ordinal);
    if (Ordinal<0||Ordinal>=GetTotalCount()||CyclingGroups.IsEmpty()) return INDEX_NONE;
    if (Ordinal<OpeningGroups.Num()) { FormationSlot=MinionCount+Ordinal; return OpeningGroups[Ordinal]; }
    const int32 Index=Ordinal-OpeningGroups.Num(); // 首发组不消耗循环袋序号，保持旧Boss+小怪随机结果。
    if (Index>=CyclingGroups.Num()&&Index<CyclingGroups.Num()+SpecialGroups.Num())
    { FormationSlot=Index; return SpecialGroups[Index-CyclingGroups.Num()]; } // 特殊小怪替换原槽，首袋保留原三类。
    TArray<int32> Bag=CyclingGroups; // 分配量只与组数相关，百万波次仍有界。
    const uint32 BagSeed=HashCombine(HashCombine(SelectionSeed,GetTypeHash(Level)),GetTypeHash(Index/Bag.Num())); // 旧随机规则原样保留。
    FRandomStream Random(static_cast<int32>(BagSeed)); // 隔离全局随机调用次序。
    for (int32 Position=Bag.Num()-1;Position>0;--Position) Bag.Swap(Position,Random.RandRange(0,Position)); // Fisher-Yates每袋覆盖一次。
    FormationSlot=Index; return Bag[Index%Bag.Num()];
}

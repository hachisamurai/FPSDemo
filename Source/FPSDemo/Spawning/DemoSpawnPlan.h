#pragma once
#include "CoreMinimal.h"
#include "Game/DemoCombatConfig.h"
#include "DemoSpawnPlan.generated.h"

class ADemoEnemy;

/** GameMode默认组件上的策划配置；开关卡时复制，运行中修改不影响已建立计划。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoSpawnSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Scheduling", meta=(ClampMin="0.05", ClampMax="10")) float Interval = .5f; // World秒，暂停时停止；普通/无尽共用补怪及阻挡重试时钟。
    UPROPERTY(EditAnywhere, Category="Scheduling", meta=(ClampMin="4", ClampMax="64")) int32 BatchLimit = 32; // 单次最多生成数量，至少容纳Boss和三兵种，避免大数量同帧卡顿。
    UPROPERTY(EditAnywhere, Category="Placement", meta=(ClampMin="1", ClampMax="64")) int32 CandidateAttempts = 24; // 每只每次调度最多尝试候选点数，有界碰撞查询。
    UPROPERTY(EditAnywhere, Category="Placement", meta=(ClampMin="1", ClampMax="600")) int32 BlockedPassLimit = 60; // 连续整次找不到位置的次数，达到后发失败事件，不永久卡关。
    UPROPERTY(EditAnywhere, Category="Placement", meta=(ClampMin="0", ClampMax="5000")) float MinPlayerDistance = 450.f; // XY厘米，候选点与实际生成位置都不能贴近玩家。
    UPROPERTY(EditAnywhere, Category="Templates") TMap<EDemoEnemyRole, FName> MinionRows; // 可选兵种专属DT_Enemies行；不填沿用关卡EnemyRow，只允许近战/冲刺/远程。
    UPROPERTY(EditAnywhere, Category="Templates") TMap<EDemoEnemyRole, TSubclassOf<ADemoEnemy>> MinionClasses; // 可选兵种蓝图类；必须继承ADemoEnemy，空映射使用原生类。
    UPROPERTY(EditAnywhere, Category="Templates") TSubclassOf<ADemoEnemy> BossClass; // 可选Boss蓝图类；未指定使用原生ADemoEnemy，不改变Boss数值行。
};

/** 无UObject所有权的出生计划；数量使用计数器，不为百万只无尽怪提前分配数组。 */
struct FPSDEMO_API FDemoSpawnPlan
{
    FString RunId; // 本轮身份，随检查点恢复；用于稳定兵种随机序列和日志。
    int32 Level = 0; // 正逻辑关号，独立于三块物理区域。
    int32 ArenaIndex = 0; // 0..2，用于匹配场景刷怪区域。
    int32 MinionCount = 0; // 本关小怪总数1..1000000，不含额外Boss。
    int32 MaxAlive = 0; // 普通关等于总数量，无尽来自表；Boss也占名额。
    bool bBoss = false; // 本计划额外一只Boss；每十关启用。
    bool bReinforcementLayout = false; // 无尽使用外圈补充布局，普通使用原椭圆分布。
    TMap<EDemoEnemyRole, FDemoEnemySpawnStats> Minions; // 每种允许兵种的冻结GAS/行为/掉落快照。
    FDemoEnemySpawnStats Boss; // 有Boss时使用的冻结快照。
};

namespace DemoSpawnPlan
{
    /** Level/Difficulty/Enemies为已校验的本关数据，Settings为组件配置，RunId用于随机；MaxAlive>0，输出OutPlan或Error，不修改资产。 */
    FPSDEMO_API bool Build(const FDemoLevelRow& Level, const FDemoDifficultyRow& Difficulty, const UDataTable* Enemies,
        const FDemoSpawnSettings& Settings, const FString& RunId, bool bEndless, int32 MaxAlive, FDemoSpawnPlan& OutPlan, FString& Error);
    /** Settings借用到返回；统一验证编辑器元数据不能限制的C++/蓝图输入，失败写Error。 */
    FPSDEMO_API bool ValidateSettings(const FDemoSpawnSettings& Settings, FString& Error);
}

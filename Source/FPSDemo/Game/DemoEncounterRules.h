#pragma once
#include "Game/DemoCombatConfig.h"
#include "Engine/DataAsset.h"
#include "DemoEncounterRules.generated.h"

/** 策略输出的值配方；仅声明本关可选角色和额外首发组，不参与Actor生成。 */
struct FPSDEMO_API FDemoEncounterRecipe
{
    TArray<EDemoEnemyRole> Roles; // 每袋恰好覆盖一次，保持当前2/3兵种稳定混编。
    bool bExtraBoss=false; // 额外Boss独占首发组，不消耗小怪随机袋序号。
    int32 RequiredConcurrent=1; // 周期混编关需要同时容纳全部角色及Boss。
};

/** 玩法周期策略；Spawner只执行已解析组计划，新增角色/周期不修改执行器。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoEncounterPolicy : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 原生默认保持1~4近战/冲刺，5混编，10混编加Boss，无需创建资产。 */
    UDemoEncounterPolicy();
    /** Level/Count/MaxAlive为本关预算，bHasBossRow为数据引用；成功填充Out，非法配置写Error。 */
    bool Resolve(int32 Level,int32 Count,int32 MaxAlive,bool bHasBossRow,FDemoEncounterRecipe& Out,FString& Error) const;
    UPROPERTY(EditAnywhere, Category="Encounter") TArray<EDemoEnemyRole> BaseRoles; // 常规关随机袋，可扩展已有执行器支持的单一角色，不允许Mixed。
    UPROPERTY(EditAnywhere, Category="Encounter") TArray<EDemoEnemyRole> PeriodicRoles; // 周期关追加角色；不可与Base重复。
    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="1")) int32 MixedInterval=5; // 正整数；仅策略解释关号取模。
    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="1")) int32 ExtraBossInterval=10; // 正整数；关卡Boss行必须与策略匹配。
};

/** 固定战役与无尽共用的兵种节奏；只改出生快照，不修改怪物模板或具体攻击实现。 */
namespace DemoEncounterRules
{
    constexpr int32 RangedInterval = 5; // 每五关的混编池加入远程，其他关只有近战/冲刺。
    constexpr int32 BossInterval = 10; // 每十关额外生成一只Boss，模板引用来自Level10。
    /** Level为正关号、Index为从0开始的小怪序号（不含Boss）、Seed为当前RunId哈希；Stats为该只出生快照。返回false表示输入错误。 */
    FPSDEMO_API bool AssignMinionRole(int32 Level, int32 Index, uint32 Seed, FDemoEnemySpawnStats& Stats);
}

// 职责：把战役表解析为有界生成计划；使用稳定RunId选择双Boss，特殊小怪替换普通槽，依赖敌人定义与遭遇策略。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Game/DemoCombatConfig.h"
#include "AI/DemoEnemyDefinition.h"
#include "DemoSpawnPlan.generated.h"

class ADemoEnemy;

/** 分区入口的房间局部XY矩形与该区敌人锚点。服务器开战前拷贝并冻结，不复制到客户端。 */
USTRUCT()
struct FPSDEMO_API FDemoRegionalSpawnZone
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Zone") FName Id; // 区域稳定ID，用于校验、去重与日志。
    UPROPERTY(EditAnywhere, Category="Zone") FVector2D LocalMin = FVector2D::ZeroVector; // 区域矩形较小端点，房间局部厘米。
    UPROPERTY(EditAnywhere, Category="Zone") FVector2D LocalMax = FVector2D::ZeroVector; // 区域矩形较大端点，包含边界。
    UPROPERTY(EditAnywhere, Category="Zone") TArray<int32> SpawnPointIndices; // 作者房间八个敌人标记的零基下标，各区独占。
    UPROPERTY(EditAnywhere, Category="Zone", meta=(ClampMin="1")) int32 EnemyCount = 13; // 配置为单人普通难度总数；冻结计划中为本区实际总数，含尚未生成的波次。
};

/** 默认战役修订4的D03首关歼灭区域配置。服务器准备该房间时才冻结区域预算。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Demo First Room Regional Spawning"))
class FPSDEMO_API UDemoRegionalSpawnSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="First Room") TArray<FDemoRegionalSpawnZone> Zones; // B、C、D三个战斗区；公路入口A保留给队伍落点。
    UPROPERTY(Config, EditAnywhere, Category="First Room", meta=(ClampMin="1",ClampMax="64")) int32 WaveSize = 4; // 单批敌人数上限，实际仍受全场存活与调度预算限制。
    UPROPERTY(Config, EditAnywhere, Category="First Room", meta=(ClampMin="5",ClampMax="600")) float WaveInterval = 5.f; // 两批最小间隔，暂停时随World停表，单位秒。
    UPROPERTY(Config, EditAnywhere, Category="First Room", meta=(ClampMin="1",ClampMax="64")) int32 MaxAlive = 6; // 区域间共享的敌人存活上限；队伍策略有更低上限时取较低值。
    /** 准备首关时验证完整分区、波次设置。缩放每区人数并只舍入一次；成功才提交输出。 */
    bool ResolveZones(double PopulationScale, TArray<FDemoRegionalSpawnZone>& OutZones, int32& OutTotal, FString& Error) const;
};

/** GameMode默认组件上的策划配置；开关卡时复制，运行中修改不影响已建立计划。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoSpawnSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Templates") TObjectPtr<class UDemoEncounterPolicy> EncounterPolicy; // 可空；默认原生策略保留5/10周期，执行器不读取周期。
    UPROPERTY(EditAnywhere, Category="Scheduling", meta=(ClampMin="0.05", ClampMax="10")) float Interval = .5f; // World秒，暂停时停止；普通/无尽共用补怪及阻挡重试时钟。
    UPROPERTY(EditAnywhere, Category="Scheduling", meta=(ClampMin="4", ClampMax="64")) int32 BatchLimit = 32; // 单次最多生成数量，至少容纳Boss和三兵种，避免大数量同帧卡顿。
    UPROPERTY(EditAnywhere, Category="Scheduling", meta=(ClampMin="0.5", ClampMax="60")) float ControlReinforcementInterval = 8.f; // 新控制任务间隔增援的World游戏秒；仅Plan显式启用，旧冻结任务不自动开启。
    UPROPERTY(EditAnywhere, Category="Scheduling", meta=(ClampMin="1", ClampMax="64")) int32 ControlReinforcementBatch = 4; // 每次最多新增的普通循环组数；执行期仍受总alive cap/BatchLimit限制，不叠积错过波次。
    UPROPERTY(EditAnywhere, Category="Placement", meta=(ClampMin="1", ClampMax="64")) int32 CandidateAttempts = 24; // 每只每次调度最多尝试候选点数，有界碰撞查询。
    UPROPERTY(EditAnywhere, Category="Placement", meta=(ClampMin="1", ClampMax="600")) int32 BlockedPassLimit = 60; // 连续整次找不到位置的次数，达到后发失败事件，不永久卡关。
    UPROPERTY(EditAnywhere, Category="Placement", meta=(ClampMin="0", ClampMax="5000")) float MinPlayerDistance = 450.f; // XY厘米，候选点与实际生成位置都不能贴近玩家。
    UPROPERTY(EditAnywhere, Category="Templates") TMap<EDemoEnemyRole, FName> MinionRows; // 可选兵种专属DT_Enemies行；不填沿用关卡EnemyRow，只允许近战/冲刺/远程。
    UPROPERTY(EditAnywhere, Category="Templates") TMap<EDemoEnemyRole, TSubclassOf<ADemoEnemy>> MinionClasses; // 可选兵种蓝图类；必须继承ADemoEnemy，空映射使用原生类。
    UPROPERTY(EditAnywhere, Category="Templates") TSubclassOf<ADemoEnemy> BossClass; // 可选Boss蓝图类；未指定使用原生ADemoEnemy，不改变Boss数值行。
};

/** 一类可复用生成组；角色、数值、类和最终体型在开关时共同冻结。 */
struct FPSDEMO_API FDemoSpawnGroup
{
    FName Id; // 稳定诊断键，不作为玩法判断条件。
    FDemoEnemySpawnStats Stats; // 已完成关卡/难度倍率的快照，生成时只补阵型序号。
    TSubclassOf<ADemoEnemy> EnemyClass; // 对应Settings或原生类；运行执行器另以UPROPERTY保活。
    FDemoEnemyBodyDefinition Body; // 与Enemy Prepare相同来源，候选重叠不能另写48/115常量。
    bool bUseAnchor=false; // 首选区域专用锚点；通用布局标记，不要求执行器识别Boss身份。
};

/** 无UObject所有权的出生计划；数量使用计数器，不为百万只无尽怪提前分配数组。 */
struct FPSDEMO_API FDemoSpawnPlan
{
    TArray<FDemoSpawnGroup> Groups; // 小数量模板组，内存与兵种数相关，与百万无尽怪数量无关。
    TArray<int32> OpeningGroups; // 优先生成的有限组索引；当前策略为额外Boss。
    TArray<int32> CyclingGroups; // 每袋洗牌的组索引，由策略决定可用兵种。
    TArray<FDemoRegionalSpawnZone> RegionalZones; // 空为原调度；非空时冻结各区待激活及已激活预算，合计必须等于MinionCount。
    int32 RegionalWaveSize = 4; // 每批预算，区域计划生效时使用。
    float RegionalWaveInterval = 5.f; // World游戏秒，区域计划生效时两批的最小间隔。
    TArray<int32> SpecialGroups; // 最多7个替换槽，首袋基础兵种之后各生成一次，不增加总数量。
    TMap<FName,int32> SupportGroups; // Boss可请求的冻结组；不自动生成，不占基础小怪预算。
    uint32 SelectionSeed=0; // 冻结RunId哈希，重试和补怪不改变抽样结果。
    /** Ordinal为0起成功生成序号；返回组索引及FormationSlot，失败不修改任何队列。 */
    int32 SelectGroup(int32 Ordinal,int32& FormationSlot) const;
    /** 全部首发组加小怪预算；不会分配等长数组。 */
    int32 GetTotalCount() const;
    /** 通用完整性校验；不解释关号、兵种或Boss周期。Error只在失败时输出。 */
    bool Validate(FString& Error) const;
    FString RunId; // 本轮身份，随检查点恢复；用于稳定兵种随机序列和日志。
    int32 Level = 0; // 正逻辑关号，独立于三块物理区域。
    int32 ArenaIndex = 0; // 0..2，用于匹配场景刷怪区域。
    int32 MinionCount = 0; // 本关小怪总数1..1000000，不含额外Boss。
    int32 MaxAlive = 0; // 本场各区、各组共享的最终存活上限；Boss也占名额。
    bool bBoss = false; // 本计划额外一只Boss；每十关启用。
    bool bReinforcementLayout = false; // 无尽使用外圈补充布局，普通使用原椭圆分布。
    bool bControlReinforcements = false; // Flow仅对新修订控制目标显式开启；旧战役/无尽保持原有限计划与清场语义，不进入存档或网络。
    float ControlReinforcementInterval = 8.f; // 从SpawnSettings复制的本场间隔秒；开始后不再读取可变组件设置。
    int32 ControlReinforcementBatch = 4; // 本场单批待生成上限1..64；只重用CyclingGroups，不重复Boss/特殊替换/召唤。
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

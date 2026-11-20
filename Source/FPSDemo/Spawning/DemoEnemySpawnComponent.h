// 职责：主遭遇生成、并发预算和Boss子单位清理；依赖冻结SpawnPlan及同GM目标位置门禁，临时出生保护只延期、不消费数量或发奖励。分区计划使用同一服务器时钟及死亡登记。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Spawning/DemoSpawnPlan.h"
#include "Spawning/DemoEnemySpawnArea.h"
#include "DemoEnemySpawnComponent.generated.h"

class UDemoObjectiveCoordinatorComponent;

// 同步游戏线程事件；Enemy只在广播栈内借用，奖励方不可保存裸指针。
DECLARE_MULTICAST_DELEGATE_OneParam(FDemoSpawnEnemyDefeated, ADemoEnemy*);
// Remaining包含待生成和存活，失败/取消不广播清场。
DECLARE_MULTICAST_DELEGATE_OneParam(FDemoSpawnRemainingChanged, int32);
DECLARE_MULTICAST_DELEGATE(FDemoEncounterCleared);
// Reason为当前调用栈内的值引用，接收者要持久显示时需复制。
DECLARE_MULTICAST_DELEGATE_OneParam(FDemoEncounterFailed, const FString&);

/** 权威World内的通用刷怪执行器；不管理金币、存档、阶段或敌人技能。 */
UCLASS(ClassGroup=(Demo), meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoEnemySpawnComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 禁用Tick，使用World定时器，随暂停冻结。 */
    UDemoEnemySpawnComponent();
    /** EndPlayReason由World提供；先撤销回调，再交还Actor生命周期，不奖励清理销毁。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** Plan为冻结计划、AreaTransform/Geometry为区域值；绑定同Owner目标位置门禁，拒绝未结束的旧计划；失败写Error，运行异常通过OnFailed。 */
    bool StartEncounter(const FDemoSpawnPlan& Plan, const FTransform& AreaTransform, const FDemoSpawnGeometry& Geometry, FString& Error);
    /** bDestroyEnemies=true用于显式取消清场；false用于死亡/旅行以避免GAS死亡回调栈内销毁ASC，World随后回收。 */
    void CancelEncounter(bool bDestroyEnemies);
    /** Flow收到控制完成候选后同栈调用；幂等停止间隔补怪并丢弃未生成增援，现有Actor由原EndEncounter/Cancel清理，不发奖励。 */
    void StopControlReinforcements();
    /** Enemy借用本次死亡通知；必须登记、已死亡且计划运行，只接受一次；兼容旧测试入口。 */
    void NotifyEnemyDefeated(ADemoEnemy* Enemy);
    /** Enemy为异常销毁通知；已取消/未登记对象忽略，真实丢失导致失败。 */
    void NotifyEnemyLost(ADemoEnemy* Enemy);
    /** 只读统计；Actor注册表不对外暴露，清场必须是自然完成而非取消。 */
    int32 GetRemaining() const;
    int32 GetAliveCount() const;
    bool IsEncounterComplete() const;
    /** Boss必须在当前Alive登记；Row为冻结支持组，最多两次请求，排队等待总名额和兵种上限。 */
    bool RequestSupport(ADemoEnemy* Boss,FName Row);
    /** Boss死亡/会话撤销时同步取消请求与已生成子单位；不发击杀、不清理原有小怪。 */
    void CancelSupport(ADemoEnemy* Boss);
    UPROPERTY(EditAnywhere, Category="Spawn") FDemoSpawnSettings Settings; // 蓝图CDO可配置；StartEncounter复制为本关只读设置。
    FDemoSpawnEnemyDefeated OnEnemyDefeated; // 仅登记移除成功后广播，奖励组件绑定此事件。
    FDemoSpawnRemainingChanged OnRemainingChanged; // GameMode将快照转发GameState/HUD。
    FDemoEncounterCleared OnCleared; // 最后一只死亡后的下一帧，只发一次。
    FDemoEncounterFailed OnFailed; // 不发布伪清场；GameMode决定失败UI与恢复策略。
private:
    /** 定时补充到并发/批量上限；出生成功才消费队列，阻挡保留原角色序号。 */
    void Fill();
    /** 用全局Ordinal选择兵种。可选RegionalIndex把候选严格限制在本区作者点。召唤、控制增援分别传父单位、组和增援标志。 */
    ADemoEnemy* TrySpawn(int32 Ordinal,int32 SupportGroup=INDEX_NONE,ADemoEnemy* SupportBoss=nullptr,bool bControlReinforcement=false,int32 RegionalIndex=INDEX_NONE);
    /** 在权威Fill时钟采样各存活队员的房间局部XY。一旦入区便持续开放，按全关时钟轮换已开放区域，每次最多发一批。 */
    void FillRegionalWave();
    /** 同步检查当前有效控制会话；缺失、完成候选或取消均立即禁止新增援，不按UI人口推断生命周期。 */
    bool IsControlReinforcementOpen() const;
    /** 复用Fill时钟检查到期；最多一个批次，首发队列优先，满alive cap或旧批未耗尽时跳过本次，不补算历史波次。 */
    void QueueControlReinforcements();
    /** Kind为冻结身份；计算全遭遇存活数量，召唤和原有单位共用2/2/1/2限制。 */
    bool HasRosterCapacity(EDemoRosterKind Kind) const;
    /** Location为候选/构造完成时的实际世界位置；同时核冻结的出生区及所有活队员的实时安全距离。 */
    bool IsPlayerDistanceSafe(const FVector& Location) const;
    /** 单次失败Reason复制给调用方，先停止调度再广播，避免接收者递归清理。 */
    void Fail(const FString& Reason);
    /** 下一帧再次校验队列和存活集合，消费完成意图后广播。 */
    void Complete();
    /** Actor/Reason由引擎EndPlay同步提供；销毁和地图卸载区分，避免正常旅行被当异常死亡。 */
    UFUNCTION() void HandleEnemyEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    /** Enemy只同步借用；死亡/清理都解绑本组件两类回调。 */
    void UnbindEnemy(ADemoEnemy* Enemy);
    FDemoSpawnPlan ActivePlan; // 当前World的值计划，不跨存档保存运行队列。
    struct FRegionalProgress
    {
        int32 Spawned = 0; // 此区已完成生成握手的总数，杀敌不退回。
        int32 BlockedPasses = 0; // 全区持续没有可站位置的尝试次数。玩家占位或已有在场敌人时等待。
        bool bEntered = false; // 本局首次进区后锁存，队员离开再进入不会重发预算。
    };
    TArray<FRegionalProgress> RegionalProgress; // 与本次冻结的RegionalZones逐项对应，仅服务器持有。
    int32 NextRegionalZone = 0; // 多个已激活区域轮流获得下一批，队员分头探索时共享并发上限。
    double NextRegionalWaveTime = 0.; // World游戏秒，实际生成一批后才推进；暂停不补发历史批次。
    UPROPERTY() FDemoSpawnSettings FrozenSettings; // 强引用本计划可能使用的蓝图类，运行时编辑Settings不影响它。
    UPROPERTY() TArray<TSubclassOf<ADemoEnemy>> FrozenClasses; // 值计划不是USTRUCT，显式保活其中类，避免延迟批次GC后失效。
    UPROPERTY() TArray<FDemoEnemyBodyDefinition> FrozenBodies; // 强持有计划外观和受击配置，直到计划取消/结束。
    UPROPERTY() TArray<TObjectPtr<UDemoEnemyDefinition>> FrozenDefinitions; // DT可选定义的强引用，保持Prepare接线稳定。
    FTransform SpawnTransform; // 区域开关时的位置/旋转快照，不引用场景Actor。
    FDemoSpawnGeometry SpawnGeometry; // 本关几何快照，区域后续移动不改变已提交计划。
    TSet<TWeakObjectPtr<ADemoEnemy>> Alive; // 不拥有敌人；移除成功是击杀事件唯一来源。
    struct FSupportRequest
    {
        TWeakObjectPtr<ADemoEnemy> Boss; // 仅限本会话活Boss，失效请求立即丢弃。
        int32 Group=INDEX_NONE; // ActivePlan内冻结支持组，不读可变数据表。
        int32 Attempts=0; // 空间阻挡重试次数，上限沿用FrozenSettings；预算不足不算失败。
    };
    TArray<FSupportRequest> SupportRequests; // 最多两项，不形成无限召唤链。
    TMap<TWeakObjectPtr<ADemoEnemy>,TWeakObjectPtr<ADemoEnemy>> SupportOwners; // 子单位->Boss，主Alive同时计数。
    TMap<TWeakObjectPtr<ADemoEnemy>,int32> SupportRequestCounts; // 每Boss生命周期最多两个，取消不重置限额。
    TWeakObjectPtr<UDemoObjectiveCoordinatorComponent> ObjectiveGate; // StartEncounter只查同Owner一次；可选组件，旧独立刷怪测试无需GM反查。
    bool bCapacityDeferred=false; // 本次TrySpawn缺角色预算、暂处目标出生保护或分区有玩家占位，不计入空间阻挡失败。
    int32 PendingCount = 0; // 全部组剩余预算，不预分配无限波次实体数组。
    int32 NextSpawn = 0; // 完整握手成功后才递增，失败/阻挡重试同一组。
    bool bControlReinforcementsEnabled = false; // 本次控制间隔窗口；完成/取消先置false，不能重启同计划。
    int32 ControlPendingCount = 0; // 唯一增援批次剩余0..64，共用Alive总上限；永不累加历史波次。
    int32 ControlSelectionCursor = 0; // 普通循环组的环形索引，成功后取模，不累积无限总生成计数。
    int32 ControlFormationSlot = 0; // 0..63环形空间游标，避免同一批始终使用第一个标记；不改变首发NextSpawn。
    double NextControlReinforcementTime = 0.; // 本World游戏秒，暂停冻结；每次到期从Now设下一次，不补偿卡顿漏掉的波次。
    uint64 Generation=0; // 每次开始/取消递增；BP BeginPlay或委托重入不得提交旧计划生成物。
    int32 BlockedPasses = 0; // 连续无可用候选的补充次数，成功一只后清零。
    bool bRunning = false; // 取消/失败/清场时先置false，防止回调重入。
    bool bComplete = false; // 自然清场标记，取消不会冒充完成。
    FTimerHandle FillTimer; // 当前World拥有的UObject回调，不捕获裸对象。
    FTimerHandle CompleteTimer; // 取消/EndPlay同时撤销下一帧完成通知。
};

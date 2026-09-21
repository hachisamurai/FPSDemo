#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Spawning/DemoSpawnPlan.h"
#include "Spawning/DemoEnemySpawnArea.h"
#include "DemoEnemySpawnComponent.generated.h"

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
    /** Plan为已冻结计划，AreaTransform/Geometry为区域值快照；拒绝未结束的旧计划，失败写Error；运行中失败通过OnFailed通知。 */
    bool StartEncounter(const FDemoSpawnPlan& Plan, const FTransform& AreaTransform, const FDemoSpawnGeometry& Geometry, FString& Error);
    /** bDestroyEnemies=true用于显式取消清场；false用于死亡/旅行以避免GAS死亡回调栈内销毁ASC，World随后回收。 */
    void CancelEncounter(bool bDestroyEnemies);
    /** Enemy借用本次死亡通知；必须登记、已死亡且计划运行，只接受一次；兼容旧测试入口。 */
    void NotifyEnemyDefeated(ADemoEnemy* Enemy);
    /** Enemy为异常销毁通知；已取消/未登记对象忽略，真实丢失导致失败。 */
    void NotifyEnemyLost(ADemoEnemy* Enemy);
    /** 只读统计；Actor注册表不对外暴露，清场必须是自然完成而非取消。 */
    int32 GetRemaining() const;
    int32 GetAliveCount() const;
    bool IsEncounterComplete() const;
    UPROPERTY(EditAnywhere, Category="Spawn") FDemoSpawnSettings Settings; // 蓝图CDO可配置；StartEncounter复制为本关只读设置。
    FDemoSpawnEnemyDefeated OnEnemyDefeated; // 仅登记移除成功后广播，奖励组件绑定此事件。
    FDemoSpawnRemainingChanged OnRemainingChanged; // GameMode将快照转发GameState/HUD。
    FDemoEncounterCleared OnCleared; // 最后一只死亡后的下一帧，只发一次。
    FDemoEncounterFailed OnFailed; // 不发布伪清场；GameMode决定失败UI与恢复策略。
private:
    /** 定时补充到并发/批量上限；出生成功才消费队列，阻挡保留原角色序号。 */
    void Fill();
    /** 当前候选bBoss区分额外Boss，MinionIndex为0起兵种序号；成功返回World拥有的Actor，失败返回null待重试。 */
    ADemoEnemy* TrySpawn(bool bBoss, int32 MinionIndex);
    /** 单次失败Reason复制给调用方，先停止调度再广播，避免接收者递归清理。 */
    void Fail(const FString& Reason);
    /** 下一帧再次校验队列和存活集合，消费完成意图后广播。 */
    void Complete();
    /** Actor/Reason由引擎EndPlay同步提供；销毁和地图卸载区分，避免正常旅行被当异常死亡。 */
    UFUNCTION() void HandleEnemyEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    /** Enemy只同步借用；死亡/清理都解绑本组件两类回调。 */
    void UnbindEnemy(ADemoEnemy* Enemy);
    FDemoSpawnPlan ActivePlan; // 当前World的值计划，不跨存档保存运行队列。
    UPROPERTY() FDemoSpawnSettings FrozenSettings; // 强引用本计划可能使用的蓝图类，运行时编辑Settings不影响它。
    FTransform SpawnTransform; // 区域开关时的位置/旋转快照，不引用场景Actor。
    FDemoSpawnGeometry SpawnGeometry; // 本关几何快照，区域后续移动不改变已提交计划。
    TSet<TWeakObjectPtr<ADemoEnemy>> Alive; // 不拥有敌人；移除成功是击杀事件唯一来源。
    int32 PendingMinions = 0; // 未生成数量，不预分配无限波次实体数组。
    int32 NextMinion = 0; // 只在成功生成后递增，Boss不消耗随机袋序号。
    int32 BlockedPasses = 0; // 连续无可用候选的补充次数，成功一只后清零。
    bool bPendingBoss = false; // Boss优先生成，第一批可同时容纳三兵种。
    bool bRunning = false; // 取消/失败/清场时先置false，防止回调重入。
    bool bComplete = false; // 自然清场标记，取消不会冒充完成。
    FTimerHandle FillTimer; // 当前World拥有的UObject回调，不捕获裸对象。
    FTimerHandle CompleteTimer; // 取消/EndPlay同时撤销下一帧完成通知。
};

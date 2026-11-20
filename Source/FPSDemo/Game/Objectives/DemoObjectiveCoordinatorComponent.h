// 职责：GM服务器目标协调服务。Flow准备/激活，刷怪器提交人口事实，目标仅发完成候选；不发奖励、不写总阶段、不保存半场。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Game/Objectives/DemoObjectiveTypes.h"
#include "DemoObjectiveCoordinatorComponent.generated.h"

class UDemoCombatSession;
class ADemoObjectiveDevice;
class ADemoControlPoint;

DECLARE_MULTICAST_DELEGATE_OneParam(FDemoObjectiveSnapshotChanged, const FDemoObjectiveSnapshot&); // 公开值快照，由GameState持有并复制。
DECLARE_MULTICAST_DELEGATE_OneParam(FDemoObjectiveCompletionCandidate, UDemoCombatSession*); // 仅提交候选，Flow仍需帧末检查团灭与幂等。
DECLARE_MULTICAST_DELEGATE_TwoParams(FDemoObjectiveSystemError, UDemoCombatSession*, const FString&); // 必需目标异常缺失，禁止当作清关。

UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoObjectiveCoordinatorComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无普通Tick、无复制，GM拥有；计时只在Active阶段使用World Timer。 */
    UDemoObjectiveCoordinatorComponent();
    /** Config按值复制，Session借用当前战斗；创建必需目标但保持免伤，失败Error明确说明且回收已建对象。 */
    bool PrepareObjective(const FDemoObjectiveConfig& Config, UDemoCombatSession* Session, FString& Error);
    /** Flow完成玩家/刷怪准备并提交Combat后调用；重复激活拒绝，歼灭人口未知不提前完成。 */
    bool ActivateObjective();
    /** Pending/Alive为同场刷怪器的非负待生成/活体数；歼灭仅双零完成，其他目标只更新UI压力。 */
    void UpdatePopulation(int32 Pending, int32 Alive);
    /** 主动离场/会话结束幂等撤销；先锁状态、停止Timer/伤害，再解绑回收对象。 */
    void CancelObjective();
    /** 仅Flow最终批准候选时调用；Completed不触发第二次候选，回收对象不算异常缺失。 */
    void CompleteObjective();
    /** 返回本World只读公开值，调用方可复制进GameState；不能缓存返回引用跨异步。 */
    const FDemoObjectiveSnapshot& GetSnapshot() const;
    /** RunId/Level为刷怪计划冻结身份；仅本人当前有效会话的Active控制目标允许间隔增援，完成候选同栈即关闭。 */
    bool IsControlReinforcementActive(const FString& RunId, int32 Level) const;
    /** WorldLocation为服务器候选/最终出生cm；仅新控制点激活1秒内禁止圈内出生，不消费生成预算或视为失败。 */
    bool IsSpawnLocationAllowed(const FVector& WorldLocation) const;
    /** World/GM卸载独立清理，不依赖Flow先后顺序；Reason为引擎结束原因。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    FDemoObjectiveSnapshotChanged OnSnapshotChanged; // 服务器订阅，纯值不包含秘密或对象引用。
    FDemoObjectiveCompletionCandidate OnCompletionCandidate; // 服务器Flow唯一裁决订阅。
    FDemoObjectiveSystemError OnSystemError; // Flow负责保留最近检查点及可恢复错误UI。
private:
    /** 同步检查范围/锚点数/有限值，Error返回具体原因；不修正非法配置为另一任务。 */
    bool ValidateConfig(const FDemoObjectiveConfig& Config, FString& Error) const;
    /** Anchor为已作者/准备验证的地面cm；窄测±40cm并拒绝初始穿透、>32cm高度漂移及FrozenConfig来源不符，Ground返回合法静态地面。 */
    bool ResolveGround(const FVector& Anchor, FVector& Ground, FString& Error) const;
    /** 0.1s服务器占区采样，用实际游戏时间且补时上限；暂停不推进，客户端不执行。 */
    void SampleControl();
    /** 最多4Hz发布变化并刷新控制标记；阶段边沿可立即发布，普通采样不发RPC。 */
    void PublishSnapshot();
    /** 首次满足规则锁Resolving、停计时/设施伤害，再广播一次候选；不提前给奖励。 */
    void SubmitCompletion();
    /** Device为自己登记的正常击碎设施；按身份去重，只累计本场事实。 */
    void HandleDeviceBroken(ADemoObjectiveDevice* Device);
    /** Actor为必需目标，Reason区别异常销毁/流送移除与正常退出；正常击碎不冒充目标丢失。 */
    UFUNCTION() void HandleRequiredEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    /** Reason仅纯文本，先锁错误和停止行为再通知Flow，避免异常清场回调重入成功。 */
    void ReportSystemError(const FString& Reason);
    /** Expected是注册时冻结的弱会话；旧会话迟到撤销不能取消已经准备的新任务。 */
    void HandleSessionEnded(TWeakObjectPtr<UDemoCombatSession> Expected);
    /** 清除两种Timer并禁用设施损血，顺序在所有结果/取消分支保持一致。 */
    void StopActivity();
    /** 只移除本组件生成对象，回收前解绑EndPlay/击碎；bDestroyActors=false用于World退出，避免引擎卸载栈内递归Destroy。 */
    void ReleaseActors(bool bDestroyActors=true);
    FDemoObjectiveConfig FrozenConfig; // 本场冻结参数，编辑共享资产不改变当前规则。
    FDemoObjectiveSnapshot Snapshot; // 本组件权威真值；GameState只持其公开复制副本。
    TWeakObjectPtr<UDemoCombatSession> ActiveSession; // Flow拥有，弱引用杜绝跨关延长凭据。
    UPROPERTY(Transient) TArray<TObjectPtr<ADemoObjectiveDevice>> Devices; // 仅本场两个设施，主动回收前保活。
    UPROPERTY(Transient) TArray<TObjectPtr<ADemoControlPoint>> ControlPoints; // 一个或三个公开标记，协调器负责生命周期；不在运行期复制/保存候选点目录。
    TSet<TWeakObjectPtr<ADemoObjectiveDevice>> BrokenDevices; // 本场正常击碎事实去重，不以Actor数量推算。
    FTimerHandle SampleTimer; // 控制任务独有10Hz Timer；其它任务无常规Tick。
    FTimerHandle PublishTimer; // 活跃任务4Hz公开快照，只在脏时广播。
    double LastSampleTime = 0.; // 本World游戏秒，卡顿补计受配置上限约束。
    double TransitionUntil = 0.; // 下一个点允许占领的绝对游戏秒，0表示没有迁移。
    double SpawnProtectionUntil = 0.; // 当前点激活后1秒禁止圈内出生；不是无敌，不驱逐已经存在的怪。
    bool bSnapshotDirty = false; // 只在规则字段改变时发布，ServerTime变化不独立触发。
    bool bReleasing = false; // 回收事务闩锁，防外部Destroy/委托同步重入。
};

// 职责：每PlayerState独立的天赋运行句柄管理。用法：权威账本提交后Sync，新增/升阶/移除逐项差异同步；依赖ASC和定义目录。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffectTypes.h"
#include "Economy/Gambling/DemoGambleTypes.h" // 仅保存同步回调的值快照，不持有账本数组的跨回调引用。
#include "DemoTalentComponent.generated.h"
class UDemoTalentDefinition;
class UAbilitySystemComponent;
struct FDemoGambleProgress;
/** 仅服务器运行记录，不序列化到存档；客户端通过原有账本、GE和AbilitySpec复制。 */
USTRUCT()
struct FDemoTalentGrant
{
    GENERATED_BODY()
    /** 只读定义强引用，所有句柄均属于这一项。 */
    UPROPERTY() TObjectPtr<UDemoTalentDefinition> Definition;
    /** 上次已成功应用的等级，至少1；更新失败时保持旧值供下次修复。 */
    UPROPERTY() int32 Rank=0;
    /** 此定义的权限GE句柄，移除后立即撤销新激活权限。 */
    FActiveGameplayEffectHandle Permission;
    /** 仅本定义授予的GA句柄；升阶修改Spec.Level并MarkDirty，保留实例/任务/冷却。 */
    TArray<FGameplayAbilitySpecHandle> Abilities;
    /** 与Definition.PassiveEffects同序的持有GE；升级原位改等级/SetByCaller，不刷新其他项。 */
    TArray<FActiveGameplayEffectHandle> Effects;
};
UCLASS(ClassGroup=(Demo),meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoTalentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick、无需额外复制；生命周期跟随PlayerState。 */
    UDemoTalentComponent();
    /** ASC是同一PS拥有的权威GAS，Data为已验证账本；回调重入合并为最新值快照，幂等调用不重新授予。 */
    void Sync(UAbilitySystemComponent* ASC,const FDemoGambleProgress& Data);
    /** 当前Avatar死亡/结束战斗时取消本组件授予的长时GA任务，保留等级、授予句柄、被动GE与冷却。 */
    void CancelActiveBehaviors();
    /** 核心行为仍由原天赋句柄管理器授予；ASC必须同PS，Source为本人RunBuild组件，幂等返回唯一GA句柄，不进原随机账本。 */
    FGameplayAbilitySpecHandle EnsureRunBuildAbility(UAbilitySystemComponent* ASC,UObject* Source);
    /** 清理本组件授予的句柄；Reason为引擎结束原因，不移除其他系统的GE/GA。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** ASC为本人GAS，Data为已复制的稳定快照；一轮差异同步，不保留PS可变账本引用。 */
    void SyncSnapshot(UAbilitySystemComponent* ASC,const FDemoGambleProgress& Data);
    /** UObject计时器在游戏线程冲刷有界同步剩余项；EndPlay清除定时器，销毁后不访问PS。 */
    void FlushPending();
    /** 按Definition预制本等级被动Spec；ASC/Definition只借用，Rank正整数，Out由调用栈持有，失败不改变旧运行记录。 */
    bool PrepareEffects(UAbilitySystemComponent* ASC,const UDemoTalentDefinition* Definition,int32 Rank,TArray<FGameplayEffectSpecHandle>& Out) const;
    /** 仅处理这一项；ASC为本人GAS，Grant为已有/新记录，Rank正等级；无关天赋绝不受影响。 */
    bool ApplyRank(UAbilitySystemComponent* ASC,FDemoTalentGrant& Grant,int32 Rank);
    /** 精确撤销Grant的权限/GA/被动和自身临时GE；ASC为原授予者，不触碰别的玩家。 */
    void Revoke(UAbilitySystemComponent* ASC,FDemoTalentGrant& Grant);
    /** 服务器内存账本，Key为稳定天赋ID；升级不能清空整张表。 */
    UPROPERTY(Transient) TMap<FName,FDemoTalentGrant> Grants;
    /** 服务器PS拥有的GAS弱引用，EndPlay不能延长其生命周期。 */
    TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
    /** GAS委托可能同步重入账本，避免在遍历期间再次改变Grants。 */
    bool bSyncing=false;
    /** 同步GAS回调产生的新账本只保留最新副本，避免失效引用和嵌套修改Grants。 */
    FDemoGambleProgress Pending;
    /** Pending是否尚未应用；单轮最多8次同步，防止错误蓝图无限重入卡死游戏线程。 */
    bool bHasPending=false;
    /** EndPlay开始后拒绝新的授予，取消回调不能复活运行句柄。 */
    bool bEnding=false;
    /** 连续重入超过预算时下一帧继续，World拥有定时器；EndPlay显式清理。 */
    FTimerHandle PendingTimer;
    FGameplayAbilitySpecHandle RunBuildAbility; // 仅核心事件GA；与原15项等级/权限分开，EndPlay统一撤销。
};

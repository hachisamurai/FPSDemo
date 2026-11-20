// 职责：所有近战武器共用的ServerOnly GAS生命周期和可选连招分段等待。使用：启动授予，组件持有唯一攻击时钟/队列；GA逐刀提交，不识别具体武器名。
#pragma once
#include "GAS/DemoGameplayAbility.h"
#include "DemoMeleeAbility.generated.h"
UCLASS()
class FPSDEMO_API UDemoMeleeAbility : public UDemoGameplayAbility
{
    GENERATED_BODY()
public:
    /** 允许存活玩家在Hub/Intermission挥击；伤害仍由组件的Combat会话凭据单独授权。 */
    UDemoMeleeAbility();
    /** GAS上下文Handle/ActorInfo/ActivationInfo与可空事件仅本次借用；成功后建立受GA取消管理的收招任务。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
    /** 原生GAS结束上下文；bReplicateEndAbility沿框架，bWasCancelled区分正常收招与菜单/死亡中断。 */
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled) override;
private:
    /** 按组件唯一截止时刻建立下一段WaitDelay；成功连砍复用当前GA，取消随GAS销毁任务，不再激活同一仍Active技能。 */
    void WaitForSegment();
    /** WaitDelay的UObject委托回调；分支处继续等待选定出口，出口处检查/提交下一刀，否则正常结束。仅服务器游戏线程调用。 */
    UFUNCTION() void OnFinished();
    TWeakObjectPtr<class UDemoMeleeComponent> Melee; // 固定本次Avatar组件，PS重生后旧任务不能结束新Pawn攻击。
    UPROPERTY(Transient) TObjectPtr<class UAbilityTask_WaitDelay> PendingWait; // 本GA当前唯一分段任务；回调先清引用，结束时解除委托，绝不跨Avatar保留。
};

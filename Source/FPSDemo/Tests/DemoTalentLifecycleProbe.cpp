// 职责：通用GA生命周期实际任务探针。用法：Editor回归调用，验证基类不会在激活返回时结束延迟任务；不用裸Lambda/线程。
#include "Tests/DemoTalentLifecycleProbe.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Debug/DemoLog.h"
void UDemoTalentLifecycleProbe::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    Super::ActivateAbility(Handle,ActorInfo,ActivationInfo,TriggerEventData); // 使用真正通用基类，不能绕过本次被验证的入口。
    if(!CommitAbility(Handle,ActorInfo,ActivationInfo)) { EndAbility(Handle,ActorInfo,ActivationInfo,true,true); return; }
    UAbilityTask_WaitDelay* Task=UAbilityTask_WaitDelay::WaitDelay(this,60.f); // 任务由Ability拥有，正常取消必须同步清理。
    Task->OnFinish.AddDynamic(this,&UDemoTalentLifecycleProbe::OnDelayFinished); // UObject弱委托，不跨线程，不捕获临时对象。
    Task->ReadyForActivation();
#else
    EndAbility(Handle,ActorInfo,ActivationInfo,true,true); // 包体不提供测试能力行为。
#endif
}
void UDemoTalentLifecycleProbe::OnDelayFinished()
{
    DEMO_LOG_CALL();
    EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);
}

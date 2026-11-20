// 职责：即时天赋事件分发及GC表现。用法：GAS门禁已由通用基类检查，此处只保证同步完成契约，不限制普通GA的异步生命周期。
#include "GAS/Talents/DemoTalentEventAbility.h"
#include "AbilitySystemComponent.h"
#include "Debug/DemoLog.h"
bool UDemoTalentEventAbility::OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank) { DEMO_LOG_CALL(); return false; }
void UDemoTalentEventAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    UAbilitySystemComponent* ASC=ActorInfo?ActorInfo->AbilitySystemComponent.Get():nullptr; // 当前同步借用，GAS拥有生命周期。
    const bool bExecuted=ASC&&TriggerEventData&&OnTalentEvent(*TriggerEventData,GetAbilityLevel()); // 未接受事件不得播放完成表现。
    if(bExecuted&&EventCue.IsValid())
    {
        FGameplayCueParameters Cue; // 仅表现参数，不产生第二份伤害或奖励。
        Cue.Location=TriggerEventData->Target?TriggerEventData->Target->GetActorLocation():ActorInfo->AvatarActor->GetActorLocation();
        Cue.EffectContext=TriggerEventData->ContextHandle; ASC->ExecuteGameplayCue(EventCue,Cue);
    }
    if(!bExecuted) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("TALENT_EVENT ignored missing event/unmet condition/config class=%s"),*GetClass()->GetName());
    EndAbility(Handle,ActorInfo,ActivationInfo,true,false);
}

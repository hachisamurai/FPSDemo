// 职责：连接ASC的激活/取消与兵种读条；依赖统一Boss Casting标签避免技能重叠。
#include "GAS/Abilities/DemoRosterAbility.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "AI/DemoEnemyRoster.h"
#include "Debug/DemoLog.h"
UDemoRosterAbility::UDemoRosterAbility()
{
    DEMO_LOG_CALL(); InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ActivationOwnedTags.AddTag(DemoBossTags::Casting); ActivationBlockedTags.AddTag(DemoBossTags::Casting);
}
void UDemoRosterAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL(); Super::ActivateAbility(Handle,ActorInfo,ActivationInfo,TriggerEventData);
    UDemoEnemyRoster* Roster=GetAvatarActorFromActorInfo()?GetAvatarActorFromActorInfo()->FindComponentByClass<UDemoEnemyRoster>():nullptr; // 当前Avatar组件，生命周期由Actor/GAS约束。
    if (!Roster||!Roster->BeginSkill()) Complete(true);
}
void UDemoRosterAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled)
{
    DEMO_LOG_CALL();
    if (UDemoEnemyRoster* Roster=GetAvatarActorFromActorInfo()?GetAvatarActorFromActorInfo()->FindComponentByClass<UDemoEnemyRoster>():nullptr) // 同步借用，不持有组件。
        Roster->FinishSkill(bWasCancelled);
    Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}
void UDemoRosterAbility::Complete(bool bCancelled)
{ DEMO_LOG_CALL(); if (IsActive()) EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,bCancelled); }

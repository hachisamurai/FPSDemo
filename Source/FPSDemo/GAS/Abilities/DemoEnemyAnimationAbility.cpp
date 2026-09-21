#include "GAS/Abilities/DemoEnemyAnimationAbility.h"
#include "Animation/DemoEnemyPresentation.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Debug/DemoLog.h"

UDemoEnemyAnimationAbility::UDemoEnemyAnimationAbility()
{
    DEMO_LOG_CALL(); InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
}
void UDemoEnemyAnimationAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    UDemoEnemyPresentation* Presentation=GetAvatarActorFromActorInfo()?GetAvatarActorFromActorInfo()->FindComponentByClass<UDemoEnemyPresentation>():nullptr; // 同步借用 Avatar 组件。
    if (!Presentation||!Presentation->GetRequestedMontage())
    { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_ANIM rejected missing presentation/montage")); EndAbility(Handle,ActorInfo,ActivationInfo,true,true); return; }
    UAbilityTask_PlayMontageAndWait* Task=UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,
        Presentation->GetRequestedMontage(),Presentation->GetRequestedRate(),NAME_None,true); // GA拥有Task，结束自动停止；无裸Lambda。
    Task->OnCompleted.AddDynamic(this,&ThisClass::FinishAnimation);
    Task->OnInterrupted.AddDynamic(this,&ThisClass::FinishAnimation);
    Task->OnCancelled.AddDynamic(this,&ThisClass::FinishAnimation);
    Task->ReadyForActivation();
}
void UDemoEnemyAnimationAbility::FinishAnimation()
{
    DEMO_LOG_CALL();
    if (IsActive()) EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);
}

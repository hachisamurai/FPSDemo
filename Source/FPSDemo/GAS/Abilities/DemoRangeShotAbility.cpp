// 职责：训练开火的GAS执行边界。使用：GiveAbilityAndActivateOnce执行后自动移除，玩家天赋和元素GE不能用于靶场刷分。
#include "GAS/Abilities/DemoRangeShotAbility.h"
#include "Economy/Gambling/DemoCasinoRange.h"
#include "Characters/DemoCharacter.h"
#include "Debug/DemoLog.h"
UDemoRangeShotAbility::UDemoRangeShotAbility() { DEMO_LOG_CALL(); InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor; NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly; }
void UDemoRangeShotAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,const FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event)
{
    DEMO_LOG_CALL();
    if(Info&&Info->IsNetAuthority()) if(auto* Range=ADemoCasinoRange::Find(Cast<ADemoCharacter>(Info->AvatarActor.Get()))) Range->Fire();
    EndAbility(Handle,Info,Activation,true,false);
}

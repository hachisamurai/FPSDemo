// 职责：Dash技能独立实现。使用：由自身GA生命周期触发；武器/角色组件承担各自行为，新增技能不追加到此文件。
#include "GAS/Abilities/DemoDashAbility.h"
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "GAS/Abilities/DemoPlayerAbilityTypes.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"

UDemoDashAbility::UDemoDashAbility()
{
    DEMO_LOG_CALL();
    bAllowSafeAreaSkills = true;
    CooldownGameplayEffectClass = UDemoDashCooldownEffect::StaticClass();
}
void UDemoDashAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,const FGameplayAbilityActivationInfo ActivationInfo) const
{
    DEMO_LOG_CALL();
    UAbilitySystemComponent* ASC=ActorInfo?ActorInfo->AbilitySystemComponent.Get():nullptr; // 当前权威激活ASC。
    if(!DemoGambleEffects::Has(ASC,DemoGamble::Relic(3))) { Super::ApplyCooldown(Handle,ActorInfo,ActivationInfo); return; }
    FGameplayEffectSpecHandle Spec=MakeOutgoingGameplayEffectSpec(Handle,ActorInfo,ActivationInfo,CooldownGameplayEffectClass,GetAbilityLevel(Handle,ActorInfo)); // 继承原冷却Tag。
    if(Spec.IsValid()) { Spec.Data->SetDuration(Spec.Data->GetDuration()*(1.f-GetDefault<UDemoGambleSettings>()->DashCooldownReduction),true); ApplyGameplayEffectSpecToOwner(Handle,ActorInfo,ActivationInfo,Spec); }
}
void UDemoDashAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = ActorInfo ? Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()) : nullptr; // 权威推进所属Pawn；位移复制由CharacterMovement处理。
    const bool bCommitted = Character && Character->HasAuthority() && CommitAbility(Handle, ActorInfo, ActivationInfo); // 失败不执行位移。
    if (bCommitted) Character->PerformDash();
    else UE_LOG(LogFPSDemo, Log, TEXT("Dash ability rejected: commit/avatar"));
    EndAbility(Handle, ActorInfo, ActivationInfo, true, !bCommitted);
}

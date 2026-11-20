// 职责：Heal技能独立实现。使用：由自身GA生命周期触发；武器/角色组件承担各自行为，新增技能不追加到此文件。
#include "GAS/Abilities/DemoHealAbility.h"
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

UDemoHealAbility::UDemoHealAbility()
{
    DEMO_LOG_CALL();
    bAllowSafeAreaSkills = true;
    CooldownGameplayEffectClass = UDemoHealCooldownEffect::StaticClass();
}
bool UDemoHealAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    DEMO_LOG_CALL();
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
    const ADemoCharacter* Character = Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()); // 公共门禁确认本玩家ASC与生命。
    const UDemoAttributeSet* Attributes = Character ? Character->GetDemoAttributes() : nullptr; // 只读本次治疗资格。
    if (!Attributes || Attributes->GetHealth() >= Attributes->GetMaxHealth()) { UE_LOG(LogFPSDemo, Log, TEXT("Heal ability rejected: full health")); return false; }
    return true;
}
void UDemoHealAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = ActorInfo ? Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()) : nullptr; // 治疗效果只授予本技能ASC。
    const bool bCommitted = Character && Character->HasAuthority() && CommitAbility(Handle, ActorInfo, ActivationInfo); // 满血/冷却拒绝时不产生GE。
    if (bCommitted)
    {
        UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo(); // 当前权威PS ASC，仅触发治疗GE与表现Cue。
        DemoEffects::Apply(ASC, ASC, UDemoHealthEffect::StaticClass(), Character->GetHealAmount());
        if (ASC)
        {
            FGameplayCueParameters Cue; // 仅表现参数，不产生第二份治疗。
            Cue.RawMagnitude = Character->GetHealAmount();
            Cue.Location = Character->GetActorLocation();
            ASC->ExecuteGameplayCue(DemoTags::HealCue, Cue);
        }
    }
    else UE_LOG(LogFPSDemo, Log, TEXT("Heal ability rejected: commit/avatar"));
    EndAbility(Handle, ActorInfo, ActivationInfo, true, !bCommitted);
}

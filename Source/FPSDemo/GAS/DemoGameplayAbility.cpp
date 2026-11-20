// 职责：玩家GA公共门禁实现。使用：各派生类调用公共CanActivate/End逻辑，动作实现分文件维护。
#include "GAS/DemoGameplayAbility.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"

UDemoGameplayAbility::UDemoGameplayAbility()
{
    DEMO_LOG_CALL();
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ActivationBlockedTags.AddTag(DemoTags::Dead);
}
bool UDemoGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    DEMO_LOG_CALL();
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags) || !ActorInfo) return false;
    const ADemoCharacter* Character = Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()); // 仅本次借用所属Avatar，无GetPlayer0/共享GI玩家状态。
    const UDemoAttributeSet* Attributes = Character ? Character->GetDemoAttributes() : nullptr; // 当前玩家PS ASC的属性，不缓存旧Avatar。
    if (!Attributes || Attributes->GetHealth() <= 0.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("Ability rejected: missing Avatar/attributes or dead")); return false; }
    if (!(bAllowSafeAreaSkills ? Character->CanUsePlayerSkills()
        : bAllowIntermissionFirearmActions ? Character->CanUseFirearmActions() : Character->CanUseCombatAbilities()))
    { UE_LOG(LogFPSDemo, Log, TEXT("Ability rejected: class=%s phase/action gate"), *GetClass()->GetName()); return false; }
    return true;
}
void UDemoGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    // 抽象公共类不实现具体动作；遗漏派生实现时拒绝，而非误触发其他技能。
    UE_LOG(LogFPSDemo, Warning, TEXT("Ability rejected: concrete activation required class=%s"), *GetClass()->GetName());
    EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
}
void UDemoGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Log, TEXT("Ability ended class=%s cancelled=%d"), *GetClass()->GetName(), bWasCancelled);
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

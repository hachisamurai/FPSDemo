// 职责：Aim技能独立实现。使用：由自身GA生命周期触发；武器/角色组件承担各自行为，新增技能不追加到此文件。
#include "GAS/Abilities/DemoAimAbility.h"
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

UDemoAimAbility::UDemoAimAbility()
{
    DEMO_LOG_CALL();
    ActivationOwnedTags.AddTag(DemoTags::Aiming);
    ActivationBlockedTags.AddTag(DemoTags::Reloading);
}
bool UDemoAimAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    DEMO_LOG_CALL();
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
    const ADemoCharacter* Character = Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()); // 本次只读所属Avatar。
    const ADemoWeaponBase* Weapon = Character ? Character->GetWeaponComponent()->GetActiveWeapon() : nullptr; // 不跨异步保存当前装备。
    if (!Weapon || !Weapon->Config.bSupportsScope || Weapon->IsReloading()) { UE_LOG(LogFPSDemo, Log, TEXT("Aim ability rejected: weapon/scope/reload")); return false; }
    return true;
}
void UDemoAimAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = ActorInfo ? Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()) : nullptr; // 服务器为各玩家分别持有GA实例。
    if (Character && Character->HasAuthority() && CommitAbility(Handle, ActorInfo, ActivationInfo) && Character->GetWeaponComponent()->BeginAim()) return;
    UE_LOG(LogFPSDemo, Log, TEXT("Aim ability rejected: commit/avatar"));
    EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
}
void UDemoAimAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = ActorInfo ? Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()) : nullptr; // 仅恢复本玩家，组件复制回调驱动拥有者镜头。
    if (Character) Character->GetWeaponComponent()->EndAim();
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

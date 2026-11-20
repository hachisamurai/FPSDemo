// 职责：Reload技能独立实现。使用：由自身GA生命周期触发；武器/角色组件承担各自行为，新增技能不追加到此文件。
#include "GAS/Abilities/DemoReloadAbility.h"
#include "Engine/World.h" // 按World秒计算同一周期的剩余回握时间。
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
#include "Progression/Builds/DemoRunBuildComponent.h" // 核心只观察完整GA会话，逐颗装填不逐颗授予凭据。

UDemoReloadAbility::UDemoReloadAbility()
{
    DEMO_LOG_CALL();
    bAllowIntermissionFirearmActions = true; // 备战练枪耗尽弹匣后沿用同一装填生命周期。
    ActivationOwnedTags.AddTag(DemoTags::Reloading);
}
bool UDemoReloadAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
    DEMO_LOG_CALL();
    if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) return false;
    const ADemoCharacter* Character = Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()); // 公共门禁已验证Avatar，仍只同步借用。
    const ADemoWeaponBase* Weapon = Character ? Character->GetWeaponComponent()->GetActiveWeapon() : nullptr; // 玩家独立库存。
    if (!Weapon || !Weapon->CanReload()) { UE_LOG(LogFPSDemo, Log, TEXT("Reload ability rejected: weapon/full/reserve")); return false; }
    return true;
}
void UDemoReloadAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = ActorInfo ? Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()) : nullptr; // 本次GAS授予对应的服务器Avatar。
    ReloadPawn = Character;
    ReloadSessionSequence = 0;
    bReloadSessionNotified = false;
    bFinalCycleCompleted = false;
    if (!Character || !Character->HasAuthority() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
    { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
    Character->GetWeaponComponent()->StopFire();
    Character->GetWeaponComponent()->StopAim();
    ReloadWeapon = Character->GetWeaponComponent()->GetActiveWeapon();
    if (!StartReloadCycle())
    { UE_LOG(LogFPSDemo, Log, TEXT("Reload ability rejected: stale weapon")); EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
    ReloadSessionSequence = ReloadSequence;
    bReloadSessionNotified = true;
    if (UDemoRunBuildComponent* Build = UDemoRunBuildComponent::FindForPawn(Character)) Build->NotifyReloadStarted(ReloadWeapon.Get(),ReloadSessionSequence);
}
bool UDemoReloadAbility::StartReloadCycle()
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = Cast<ADemoCharacter>(GetAvatarActorFromActorInfo()); // 当前GAS所属Pawn；每周期重新借用。
    if (!Character || !Character->HasAuthority() || !ReloadWeapon.IsValid() || !Character->GetWeaponComponent()->IsEquipped(ReloadWeapon.Get()) || !ReloadWeapon->BeginReload())
    { UE_LOG(LogFPSDemo,Log,TEXT("Reload cycle rejected: authority/equipment/begin")); return false; }
    ReloadSequence = ReloadWeapon->GetReloadSequence();
    // 动画只展示冻结的动作，不拥有弹药转移；Task和武器序号才决定完成。
    if (Character->IsLocallyControlled() && Character->GetWeaponAnimationComponent() && ReloadWeapon->Config.WeaponAnimLayerClass)
        Character->GetWeaponAnimationComponent()->BeginReloadPresentation(ReloadWeapon.Get(), this, ReloadSequence, ReloadWeapon->GetReloadDuration(), ReloadWeapon->IsEmptyReload());
    UAbilityTask_WaitDelay* DelayTask = UAbilityTask_WaitDelay::WaitDelay(this, ReloadWeapon->GetReloadInsertionSeconds()); // 首次权威回调在弹药就位帧，回握阶段可继续播放。
    if (!DelayTask) { UE_LOG(LogFPSDemo,Warning,TEXT("Reload cycle failed: delay task unavailable")); return false; }
    DelayTask->OnFinish.AddDynamic(this, &UDemoReloadAbility::OnAmmoInserted);
    DelayTask->ReadyForActivation();
    return true;
}
bool UDemoReloadAbility::IsReloadContextValid() const
{
    DEMO_LOG_CALL();
    ADemoCharacter* Character = Cast<ADemoCharacter>(GetAvatarActorFromActorInfo()); // 回调重取Avatar，不使旧Pawn继续补弹。
    const UDemoAttributeSet* Attributes = Character ? Character->GetDemoAttributes() : nullptr; // 生命与阶段都重新检查。
    return Character && Character->HasAuthority() && Attributes && Attributes->GetHealth() > 0.f && Character->CanUseFirearmActions()
        && ReloadWeapon.IsValid() && ReloadWeapon->IsReloading() && ReloadWeapon->GetReloadSequence() == ReloadSequence && Character->GetWeaponComponent()->IsEquipped(ReloadWeapon.Get());
}
void UDemoReloadAbility::OnAmmoInserted()
{
    DEMO_LOG_CALL();
    if (!IsReloadContextValid() || !ReloadWeapon->CommitReloadInsertion(ReloadSequence))
    { UE_LOG(LogFPSDemo, Log, TEXT("Reload insertion rejected: stale or ineligible")); EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
    if (!ReloadWeapon->Config.bReloadOneRoundAtATime && bReloadSessionNotified)
    {
        bReloadSessionNotified = false; // 单匣入位即完成，后续回握、切枪或开火不得重复发放节律凭据。
        if (UDemoRunBuildComponent* Build = UDemoRunBuildComponent::FindForPawn(ReloadPawn.Get()))
            Build->NotifyReloadFinished(ReloadWeapon.Get(), ReloadSessionSequence, true);
        if (!IsActive() || !ReloadWeapon.IsValid()) return; // 通知允许同步撤销原激活。
    }
    // 使用原截止时间而不是再次累加完整尾段，低帧率回调迟到不延长装填周期。
    const float Remaining = FMath::Max(.001f,ReloadWeapon->GetReloadServerStartTime()+ReloadWeapon->GetReloadDuration()-GetWorld()->GetTimeSeconds()); // 秒，至少一次有效任务调度。
    UAbilityTask_WaitDelay* DelayTask = UAbilityTask_WaitDelay::WaitDelay(this,Remaining); // 同技能拥有，开火/切枪取消会销毁此任务。
    if (!DelayTask) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
    DelayTask->OnFinish.AddDynamic(this,&UDemoReloadAbility::OnReloadFinished);
    DelayTask->ReadyForActivation();
}
void UDemoReloadAbility::OnReloadFinished()
{
    DEMO_LOG_CALL();
    if (IsReloadContextValid() && ReloadWeapon->CompleteReload())
    {
        if (ReloadWeapon->Config.bReloadOneRoundAtATime && ReloadWeapon->CanReload())
        {
            if (StartReloadCycle()) return; // 中间周期不发送Finished，不重复判节拍或授予供弹。
            UE_LOG(LogFPSDemo,Warning,TEXT("Reload finalization rejected: next shell cycle could not start"));
            EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
            return;
        }
        bFinalCycleCompleted = true;
    }
    else
    {
        UE_LOG(LogFPSDemo, Log, TEXT("Reload callback ignored: stale weapon/avatar/phase"));
        EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
        return;
    }
    EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
void UDemoReloadAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    DEMO_LOG_CALL();
    const TWeakObjectPtr<ADemoWeaponBase> EndingWeapon = ReloadWeapon; // 先清成员凭据，取消动画可能同步回调GAS。
    const int32 EndingSequence = ReloadSequence; // 只取消本次动作，不误取消同枪后续激活。
    ADemoCharacter* EndingPawn = ReloadPawn.Get(); // 原Avatar；ActorInfo可能已随ASC复活变更。
    const int32 EndingSession = ReloadSessionSequence; // 整次装填首周期ID，不使用最后一颗序号冒充新会话。
    const bool bPublishEnd = bReloadSessionNotified; // 先冻结并清标志，核心/动画同步取消不能重发。
    const bool bSuccess = !bWasCancelled && bFinalCycleCompleted && EndingPawn && EndingWeapon.IsValid()
        && EndingPawn->GetWeaponComponent()->IsEquipped(EndingWeapon.Get()) && EndingPawn->CanUseFirearmActions(); // 不把无效回调或阶段切换视为完成。
    ReloadWeapon.Reset();
    ReloadSequence = 0;
    ReloadPawn.Reset();
    ReloadSessionSequence = 0;
    bReloadSessionNotified = false;
    bFinalCycleCompleted = false;
    if (bPublishEnd)
        if (UDemoRunBuildComponent* Build = UDemoRunBuildComponent::FindForPawn(EndingPawn)) Build->NotifyReloadFinished(EndingWeapon.Get(),EndingSession,bSuccess);
    if (EndingWeapon.IsValid() && EndingWeapon->GetReloadSequence() == EndingSequence) EndingWeapon->CancelReload();
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

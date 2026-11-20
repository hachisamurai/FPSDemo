// 职责：共用近战GA的逐刀提交、单时钟分段等待及收招/取消。使用：ASC激活；MeleeComponent决定分支/出口/Entry，GA持有WaitDelay，动画只消费快照。
#include "Weapons/Melee/DemoMeleeAbility.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Characters/DemoCharacter.h"

UDemoMeleeAbility::UDemoMeleeAbility()
{
    DEMO_LOG_CALL();
    bAllowSafeAreaSkills=true; // 只放开挥击阶段；死亡/菜单/观战/暂停仍走公共门禁，枪械GA不改变。
}

void UDemoMeleeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=ActorInfo?Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()):nullptr; // 当前服务器Avatar，只在本次初始化借用。
    Melee=Pawn?Pawn->FindComponentByClass<UDemoMeleeComponent>():nullptr;
    if (!Pawn || !Pawn->HasAuthority() || !Melee.IsValid() || !CommitAbility(Handle,ActorInfo,ActivationInfo) || !Melee->BeginAttack())
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_GA_REJECT commit/context")); EndAbility(Handle,ActorInfo,ActivationInfo,true,true); return; }
    WaitForSegment();
}

void UDemoMeleeAbility::WaitForSegment()
{
    DEMO_LOG_CALL();
    if (!IsActive()) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_GA_WAIT_REJECT inactive")); return; }
    if (!Melee.IsValid())
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_GA_WAIT_REJECT avatar")); EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
    const float Remaining=Melee->GetAbilityWaitSeconds(); // 从当前作者时轴原点反解；不另累加Duration，也不因长帧推迟计划出口。
    if (!FMath::IsFinite(Remaining) || Remaining<0.f)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_GA_WAIT_REJECT invalid deadline")); EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
    if (PendingWait)
    {
        PendingWait->OnFinish.RemoveDynamic(this,&UDemoMeleeAbility::OnFinished);
        PendingWait->EndTask(); PendingWait=nullptr;
    }
    PendingWait=UAbilityTask_WaitDelay::WaitDelay(this,Remaining); // 0秒由原生任务安排下一Tick，不在当前回调递归推进。
    if (!PendingWait)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_GA_WAIT_REJECT task allocation")); EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
    PendingWait->OnFinish.AddDynamic(this,&UDemoMeleeAbility::OnFinished);
    PendingWait->ReadyForActivation();
}

void UDemoMeleeAbility::OnFinished()
{
    DEMO_LOG_CALL();
    PendingWait=nullptr; // 发回调的原生WaitDelay在Broadcast返回后自行EndTask；新任务不能被旧回调清掉。
    if (!IsActive()) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_GA_SEGMENT_REJECT inactive")); return; }
    if (!Melee.IsValid())
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_GA_SEGMENT_REJECT avatar")); EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
    if (!Melee->IsAttackSegmentComplete()) { WaitForSegment(); return; } // Branch仅选出口；伤害/表现从未在此重置。
    if (!Melee->CanAdvanceCombo()) { EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false); return; }
    const TWeakObjectPtr<UDemoMeleeComponent> Expected=Melee; // Commit可触发同步GE/死亡；不能在其后使用已失效或已替换Avatar。
    if (!CommitAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo) || !IsActive() || !Expected.IsValid() || Melee!=Expected || !Melee->AdvanceCombo())
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_GA_COMBO_REJECT cost/context/advance")); if (IsActive()) EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true); return; }
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_GA_COMBO_ADVANCED"));
    WaitForSegment(); // 同一GA的新刀，不在旧GA的EndAbility/Super结束前重入TryActivate。
}

void UDemoMeleeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled)
{
    DEMO_LOG_CALL();
    if (PendingWait)
    {
        PendingWait->OnFinish.RemoveDynamic(this,&UDemoMeleeAbility::OnFinished);
        PendingWait->EndTask(); PendingWait=nullptr; // 先解除回调，再清组件攻击；迟到任务不能续刀。
    }
    const TWeakObjectPtr<UDemoMeleeComponent> Ending=Melee; // 先清引用，避免GE/取消同步重入复用同一凭据。
    Melee.Reset(); if (Ending.IsValid()) Ending->FinishAttack(bWasCancelled);
    Super::EndAbility(Handle,ActorInfo,ActivationInfo,bReplicateEndAbility,bWasCancelled);
}

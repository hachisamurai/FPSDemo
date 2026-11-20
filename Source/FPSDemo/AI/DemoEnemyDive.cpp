// 职责：旧俯冲入口与新Boss阶段衔接；依赖ASC和Roster，普通旧测试None仍保持原规则。
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyRoster.h"
#include "AI/DemoEnemyTargeting.h"
#include "AI/DemoEnemyTactics.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "Game/DemoGameState.h"

const FDemoBossDiveSettings& ADemoEnemy::GetDiveSettings() const { DEMO_LOG_TICK(); return DiveSettings; }
UDemoBossDiveAbility* ADemoEnemy::GetDiveAbility() const
{
    DEMO_LOG_TICK();
    const FGameplayAbilitySpec* Spec=AbilitySystem->FindAbilitySpecFromHandle(DiveHandle); // ASC拥有，仅本调用借用。
    return Spec?Cast<UDemoBossDiveAbility>(Spec->GetPrimaryInstance()):nullptr;
}
bool ADemoEnemy::CanStartDiveAttack() const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前关卡真值。
    const ADemoCharacter* Player=GetCombatTarget(); // 当前活玩家，不持有跨World引用。
    return (Roster->GetKind()!=EDemoRosterKind::Warden||Roster->GetPhase()>=2)&&Roster->GetKind()!=EDemoRosterKind::Weaver
        &&IsCombatReady()&&IsCombatSessionValid()&&bBossEnemy&&DiveSettings.bEnabled&&GetWorld()->GetTimeSeconds()>=NextDiveTime
        &&!bAttackPending&&!bGlobalAttackPending&&!AbilitySystem->HasMatchingGameplayTag(DemoBossTags::Casting)
        &&!AbilitySystem->HasMatchingGameplayTag(DemoAmmoTags::Frozen)&&State&&State->Phase==EDemoPhase::Combat
        &&DemoEnemyTargeting::IsAliveParticipant(Player); // GA 激活前目标必须仍是已登记存活参战者。
}
bool ADemoEnemy::TryStartDiveAttack()
{
    DEMO_LOG_TICK();
    if (!CanStartDiveAttack()) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("DIVE_DEFER phase/cast/frozen/cooldown")); return false; }
    const bool bActivated=AbilitySystem->TryActivateAbility(DiveHandle); // GA还会独立验证，不能通过外部入口绕过约束。
    if (!bActivated) { NextDiveTime=GetWorld()->GetTimeSeconds()+1.f; UE_LOG(LogFPSDemo,Log,TEXT("DIVE_RETRY GAS activation failed")); }
    return bActivated;
}
void ADemoEnemy::OnDiveFinished()
{
    DEMO_LOG_CALL();
    const float Now=GetWorld()->GetTimeSeconds(); // 结束后计时，不从起飞时开始扣冷却。
    Roster->LegacyFinished(); // 不改变3秒悬停无敌/50固定伤害，只增加大招后输出窗口。
    NextDiveTime=Now+Tactics->AttackInterval(DiveSettings.Cooldown,6.f);
    NextGlobalTime=FMath::Max(NextGlobalTime,Now+1.f); NextAttackTime=FMath::Max(NextAttackTime,Now+1.f); NextProjectileTime=FMath::Max(NextProjectileTime,Now+1.f);
    Tactics->ResetMovement();
    UE_LOG(LogFPSDemo,Log,TEXT("DIVE_END next=%.2f otherAttackGrace=1"),NextDiveTime);
}

// 职责：冰封碎裂的数值/事件实现。使用：仅由天赋目录和GAS调用；新增技能使用新的派生文件，不向本文件追加分支。
#include "GAS/Talents/Abilities/DemoTalentShatter.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/DemoTalentMoveEffect.h"
#include "GAS/DemoEffects.h"
#include "Debug/DemoLog.h"
UDemoTalentShatter::UDemoTalentShatter()
{
    DEMO_LOG_CALL(); 
    DamageEffect=UDemoHealthEffect::StaticClass(); SlowEffect=UDemoTalentMoveEffect::StaticClass(); // 可由蓝图替换相同数值契约的GE。
    ListenFor(DemoTalentEvents::Kill); // 原生GAS事件触发，不在奖励系统枚举天赋。
}
FText UDemoTalentShatter::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    const float Scale=DemoGamble::Strength(Rank); // 服务投影冻结描述，UI不重算。
    return FText::FromString(FString::Printf(TEXT("冻结敌死亡冰爆%g伤害，减速%.0f%%，不连锁"),Damage*Scale,Slow*100));
}

#include "AI/DemoEnemy.h"
#include "AbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
bool UDemoTalentShatter::OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank)
{
    DEMO_LOG_CALL();
    const auto* Death=Cast<UDemoTalentDeathContext>(Event.OptionalObject); // 原死亡状态，不读取已清除的冻结GE。
    ADemoEnemy* Origin=const_cast<ADemoEnemy*>(Cast<ADemoEnemy>(Event.Target)); // 只读取尸体位置，等待正常销毁。
    UAbilitySystemComponent* Source=GetAbilitySystemComponentFromActorInfo(); // 真实击杀者。
    if(!Death||!Death->bFrozen||Death->bSecondary||!Origin||!Source||!DamageEffect||!SlowEffect
        ||!FMath::IsFinite(Damage)||Damage<0||!FMath::IsFinite(Slow)||Slow<0||!FMath::IsFinite(Seconds)||Seconds<=0) return false;
    FGameplayCueParameters Cue; Cue.Location=Origin->GetActorLocation(); Cue.RawMagnitude=Radius; // 本次AOE范围反馈，不在GC执行伤害。
    Origin->GetAbilitySystemComponent()->ExecuteGameplayCue(DemoTalentEvents::FrostBurst,Cue);
    for(ADemoEnemy* Enemy:DemoTalentEvents::Nearby(Origin,Radius,MaxTargets)) // 世界仍可能在前一目标死亡时切换阶段。
    {
        if(!IsValid(Enemy)||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()) continue;
        UAbilitySystemComponent* Target=Enemy->GetAbilitySystemComponent(); // 本次伤害唯一目标，使用其原生免伤门禁。
        if(Target->HasMatchingGameplayTag(DemoBossTags::Invulnerable)) continue; // 减速不能绕过Boss免伤窗口。
        FGameplayEffectContextHandle Context=Source->MakeEffectContext(); Context.AddSourceObject(GetClass()->GetDefaultObject()); // 派生伤害不能再次产生冰爆/传播。
        FGameplayEffectSpecHandle Hit=Source->MakeOutgoingSpec(DamageEffect,Rank,Context); // 伤害GE与原直击分开，不触发虹吸/回收。
        if(!Hit.IsValid()||DamageEffect->GetDefaultObject<UGameplayEffect>()->DurationPolicy!=EGameplayEffectDurationType::Instant) continue;
        Hit.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-Damage*DemoGamble::Strength(Rank)); Source->ApplyGameplayEffectSpecToTarget(*Hit.Data.Get(),Target);
        if(!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||SlowEffect->GetDefaultObject<UGameplayEffect>()->DurationPolicy!=EGameplayEffectDurationType::HasDuration) continue;
        FGameplayEffectSpecHandle Chill=Source->MakeOutgoingSpec(SlowEffect,Rank,Context); // 只加移速GE，绝不添加Chill/Frozen层。
        if(!Chill.IsValid()) continue;
        Chill.Data->DynamicGrantedTags.AddTag(DemoTalentEvents::CombatTransient); // 死亡/清场立即撤销。
        Chill.Data->SetDuration(FMath::Min(Seconds,30.f),true); Chill.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-FMath::Clamp(Slow,0.f,.9f));
        Target->ApplyGameplayEffectSpecToSelf(*Chill.Data.Get());
    }
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_SHATTER rank=%d damage=%.2f"),Rank,Damage*DemoGamble::Strength(Rank));
    return true; // 仅接受的事件才允许基类播放配置GC。
}

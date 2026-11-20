// 职责：GAS护盾耐久复制与瞬时消耗模板；依赖DemoTags的统一Magnitude键。
#include "GAS/DemoEnemyDefenseSet.h"
#include "GAS/DemoTags.h"
#include "Debug/DemoLog.h"
#include "Net/UnrealNetwork.h"
UDemoEnemyDefenseSet::UDemoEnemyDefenseSet() : Shield(0.f) { DEMO_LOG_CALL(); }
void UDemoEnemyDefenseSet::PreAttributeChange(const FGameplayAttribute& Attribute,float& NewValue)
{
    DEMO_LOG_CALL(); Super::PreAttributeChange(Attribute,NewValue);
    if (Attribute==GetShieldAttribute()) NewValue=FMath::Clamp(NewValue,0.f,1000000.f);
}
void UDemoEnemyDefenseSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(UDemoEnemyDefenseSet,Shield,COND_None,REPNOTIFY_Always);
}
void UDemoEnemyDefenseSet::OnRep_Shield(const FGameplayAttributeData& OldValue)
{ DEMO_LOG_CALL(); GAMEPLAYATTRIBUTE_REPNOTIFY(UDemoEnemyDefenseSet,Shield,OldValue); }
UDemoShieldEffect::UDemoShieldEffect()
{
    DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::Instant;
    FGameplayModifierInfo Modifier; // CDO拥有的不可变耐久加法模板，数值每次通过Spec提供。
    Modifier.Attribute=UDemoEnemyDefenseSet::GetShieldAttribute(); Modifier.ModifierOp=EGameplayModOp::Additive;
    FSetByCallerFloat Magnitude; // 与DemoEffects::Apply的SetByCaller键一致，不修改共享CDO。
    Magnitude.DataTag=DemoTags::Magnitude;
    Modifier.ModifierMagnitude=FGameplayEffectModifierMagnitude(Magnitude); Modifiers.Add(Modifier);
}

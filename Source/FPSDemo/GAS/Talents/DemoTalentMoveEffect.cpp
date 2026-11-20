// 职责：构造默认移动GE。使用：猎杀节奏/冰爆分别生成Spec，所有权与清理由调用GA管理，不写基础移动速度。
#include "GAS/Talents/DemoTalentMoveEffect.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Debug/DemoLog.h"
UDemoTalentMoveEffect::UDemoTalentMoveEffect()
{
    DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::HasDuration; DurationMagnitude=FScalableFloat(5.f);
    FGameplayModifierInfo Modifier; // MoveSpeedMultiplier基线1，加速与减速统一经GAS聚合。
    Modifier.Attribute=UDemoAttributeSet::GetMoveSpeedMultiplierAttribute(); Modifier.ModifierOp=EGameplayModOp::Additive;
    FSetByCallerFloat Amount; Amount.DataTag=DemoTags::Magnitude; // 每次Spec独立数值，不修改CDO。
    Modifier.ModifierMagnitude=FGameplayEffectModifierMagnitude(Amount); Modifiers.Add(Modifier);
}

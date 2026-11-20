// 职责：GAS持有型效果的默认构造。用法：只通过管理组件创建Spec，升级不重建其他天赋/临时战斗层。
#include "GAS/Talents/DemoTalentPassiveEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "Debug/DemoLog.h"
UDemoTalentPermissionEffect::UDemoTalentPermissionEffect() { DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::Infinite; }
UDemoTalentFireRateEffect::UDemoTalentFireRateEffect()
{
    DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::Infinite;
    FGameplayModifierInfo Rate; // Infinite仅影响CurrentValue，移除恢复基础射速。
    Rate.Attribute=UDemoAttributeSet::GetFireRateMultiplierAttribute(); Rate.ModifierOp=EGameplayModOp::Multiplicitive;
    FSetByCallerFloat Magnitude; Magnitude.DataTag=DemoTags::Magnitude; // 定义ConfigureEffect必须提供至少1的倍率。
    Rate.ModifierMagnitude=FGameplayEffectModifierMagnitude(Magnitude); Modifiers.Add(Rate);
}

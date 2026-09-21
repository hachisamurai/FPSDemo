#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "AI/DemoEnemy.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/CustomCanApplyGameplayEffectComponent.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"

namespace DemoAmmoTags
{
    // Burn/Chill/Frozen在DemoTags中统一注册，此处只补充弹药专属状态与表现。
    UE_DEFINE_GAMEPLAY_TAG(Immune,"Immunity.Frost");
    UE_DEFINE_GAMEPLAY_TAG(BurnCue,"GameplayCue.Ammo.Burn");
    UE_DEFINE_GAMEPLAY_TAG(ChillCue,"GameplayCue.Ammo.Chill");
    UE_DEFINE_GAMEPLAY_TAG(FrozenCue,"GameplayCue.Ammo.Frozen");
    UE_DEFINE_GAMEPLAY_TAG(ImmuneCue,"GameplayCue.Ammo.FrostImmune");
    UE_DEFINE_GAMEPLAY_TAG(ExplosionCue,"GameplayCue.Ammo.Explosion");
    UE_DEFINE_GAMEPLAY_TAG(PiercingCue,"GameplayCue.Ammo.Piercing");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(NormalType,"Ammo.Type.Normal"); // 装配标签仅由GE授予。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(FireType,"Ammo.Type.Fire");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(FrostType,"Ammo.Type.Frost");
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PiercingType,"Ammo.Type.Piercing");
    FGameplayTag Type(int32 Index) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] Ammo Type")); return Index==1?FireType:Index==2?FrostType:Index==3?PiercingType:NormalType; }
}
namespace
{
    /** GE/Tags为正在构造的模板/子对象；Tag为目标状态，Cue为只负责表现的GC。 */
    void Configure(UGameplayEffect& GE, UTargetTagsGameplayEffectComponent& Tags, FGameplayTag Tag, FGameplayTag Cue)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] Ammo Configure"));
        GE.DurationPolicy=EGameplayEffectDurationType::HasDuration; GE.DurationMagnitude=FScalableFloat(4.f);
        FInheritedTagContainer Changes; // UE5.4标签组件的不可变默认值。
        Changes.AddTag(Tag); Tags.SetAndApplyTargetTagChanges(Changes);
        GE.GameplayCues.Add(FGameplayEffectCue(Cue,1,32));
    }
    /** GE为栈模板；上限32允许配置1..32阈值，第N层由组件触发而非Overflow。 */
    void Stack(UGameplayEffect& GE)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] Ammo Stack"));
        GE.StackingType=EGameplayEffectStackingType::AggregateByTarget; GE.StackLimitCount=32;
        GE.StackDurationRefreshPolicy=EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
        GE.StackPeriodResetPolicy=EGameplayEffectStackingPeriodPolicy::NeverReset;
        GE.StackExpirationPolicy=EGameplayEffectStackingExpirationPolicy::ClearEntireStack;
        GE.bSuppressStackingCues=true;
    }
    /** GE/Attribute/Op指定模板的唯一修改器，Magnitude每次从Spec注入。 */
    void Modifier(UGameplayEffect& GE,FGameplayAttribute Attribute,EGameplayModOp::Type Op)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] Ammo Modifier"));
        FSetByCallerFloat Caller; Caller.DataTag=DemoTags::Magnitude; // 和既有生命GE兼容的有符号数值。
        FGameplayModifierInfo Mod; Mod.Attribute=Attribute; Mod.ModifierOp=Op; Mod.ModifierMagnitude=FGameplayEffectModifierMagnitude(Caller); // 值对象归CDO持有。
        GE.Modifiers.Add(Mod);
    }
}
bool UDemoAttackDebuffRequirement::CanApplyGameplayEffect_Implementation(const UGameplayEffect* GameplayEffect,const FGameplayEffectSpec& Spec,UAbilitySystemComponent* ASC) const
{
    DEMO_LOG_CALL();
    const ADemoEnemy* Enemy=ASC?Cast<ADemoEnemy>(ASC->GetAvatarActor()):nullptr; // 只对存活敌人添加攻击型状态。
    const bool Allowed=Enemy&&Enemy->IsAlive()&&!ASC->HasMatchingGameplayTag(DemoBossTags::Invulnerable); // 不清除已有栈，不重置DOT时钟。
    if (!Allowed) UE_LOG(LogFPSDemo,Log,TEXT("DEBUFF_REJECT invulnerable/dead/target"));
    return Allowed;
}
bool UDemoFrostRequirement::CanApplyGameplayEffect_Implementation(const UGameplayEffect* GameplayEffect,const FGameplayEffectSpec& Spec,UAbilitySystemComponent* ASC) const
{
    DEMO_LOG_CALL();
    const ADemoEnemy* Enemy=ASC?Cast<ADemoEnemy>(ASC->GetAvatarActor()):nullptr; // 只对存活敌人施加控制。
    const bool Allowed=Enemy && Enemy->IsAlive() && !ASC->HasMatchingGameplayTag(DemoAmmoTags::Immune) && !ASC->HasMatchingGameplayTag(DemoBossTags::Invulnerable); // 冰霜免疫仅阻止冰霜，通用无敌另在属性入口阻止伤害。
    if(!Allowed)UE_LOG(LogFPSDemo,Log,TEXT("AMMO_FROST_REJECT immune/dead/target"));
    return Allowed;
}
UDemoAmmoLoadoutEffect::UDemoAmmoLoadoutEffect() { DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::Infinite; }
UDemoBurnEffect::UDemoBurnEffect()
{
    DEMO_LOG_CALL();
    auto* Tags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("Tags")); // GE拥有默认标签组件。
    GEComponents.Add(Tags); Configure(*this,*Tags,DemoAmmoTags::Burn,DemoAmmoTags::BurnCue); Stack(*this);
    UCustomCanApplyGameplayEffectComponent* Rule=CreateDefaultSubobject<UCustomCanApplyGameplayEffectComponent>(TEXT("Invulnerability")); // GE本身校验，不能通过绕过弹药组件叠层。
    Rule->ApplicationRequirements.Add(UDemoAttackDebuffRequirement::StaticClass()); GEComponents.Add(Rule);
    Period=FScalableFloat(1.f); bExecutePeriodicEffectOnApplication=false;
    Modifier(*this,UDemoAttributeSet::GetHealthAttribute(),EGameplayModOp::Additive);
}
UDemoChillEffect::UDemoChillEffect()
{
    DEMO_LOG_CALL();
    auto* Tags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("Tags")); // 目标状态子对象。
    GEComponents.Add(Tags); Configure(*this,*Tags,DemoAmmoTags::Chill,DemoAmmoTags::ChillCue); Stack(*this);
    auto* Rule=CreateDefaultSubobject<UCustomCanApplyGameplayEffectComponent>(TEXT("Immunity")); // GE自身强制验证冰霜免疫。
    Rule->ApplicationRequirements.Add(UDemoFrostRequirement::StaticClass()); GEComponents.Add(Rule);
}
UDemoFrozenEffect::UDemoFrozenEffect()
{
    DEMO_LOG_CALL();
    auto* Tags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("Tags")); // 定身时限归GE。
    GEComponents.Add(Tags); Configure(*this,*Tags,DemoAmmoTags::Frozen,DemoAmmoTags::FrozenCue);
    auto* Rule=CreateDefaultSubobject<UCustomCanApplyGameplayEffectComponent>(TEXT("Immunity")); // 防外部绕过免疫重复定身。
    Rule->ApplicationRequirements.Add(UDemoFrostRequirement::StaticClass()); GEComponents.Add(Rule);
}
UDemoFrostImmunityEffect::UDemoFrostImmunityEffect()
{
    DEMO_LOG_CALL();
    auto* Tags=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("Tags")); // 免疫独立到期，不跟随冻结移除。
    GEComponents.Add(Tags); Configure(*this,*Tags,DemoAmmoTags::Immune,DemoAmmoTags::ImmuneCue);
}
UDemoChillMoveEffect::UDemoChillMoveEffect() { DEMO_LOG_CALL(); DurationPolicy=EGameplayEffectDurationType::Infinite; Modifier(*this,UDemoAttributeSet::GetMoveSpeedMultiplierAttribute(),EGameplayModOp::Override); }

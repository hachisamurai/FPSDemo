#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayEffectCustomApplicationRequirement.h"
#include "NativeGameplayTags.h"
#include "GAS/DemoTags.h" // 复用敌人元素HUD已经注册的状态Tag，避免重复Native注册。
#include "DemoAmmoEffects.generated.h"

namespace DemoAmmoTags
{
    using DemoTags::Burn; // 敌人灼烧状态，层数在GE中。
    using DemoTags::Chill; // 敌人冰霜状态，独立于冻结。
    using DemoTags::Frozen; // 定身Tag，不阻止攻击。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Immune); // 冻结中与解冻后免疫。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(BurnCue); // 常驻灼烧表现。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(ChillCue); // 常驻冰霜表现。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(FrozenCue); // 定身表现。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(ImmuneCue); // 免疫状态表现。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(ExplosionCue); // 单次爆炸表现。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(PiercingCue); // 单次穿透表现。
    /** Index=0..3装配类型；未知回退普通弹Tag。 */
    FPSDEMO_API FGameplayTag Type(int32 Index);
}
/** GE本身检查通用无敌和死亡，不能通过绕开弹药组件增加灼烧层数。 */
UCLASS() class FPSDEMO_API UDemoAttackDebuffRequirement : public UGameplayEffectCustomApplicationRequirement
{
    GENERATED_BODY()
public:
    /** GameplayEffect/Spec为待施加Debuff，ASC为目标；无敌阻止新增层数，已有周期仍单独在属性入口拦伤害。 */
    virtual bool CanApplyGameplayEffect_Implementation(const UGameplayEffect* GameplayEffect,const FGameplayEffectSpec& Spec,UAbilitySystemComponent* ASC) const override;
};
/** 冰霜同时检查原冻结免疫及通用无敌，避免新增定身绕过Boss蓄力状态。 */
UCLASS() class FPSDEMO_API UDemoFrostRequirement : public UGameplayEffectCustomApplicationRequirement
{
    GENERATED_BODY()
public:
    /** GameplayEffect/Spec来自本次施加，ASC为目标；只读有效性与免疫，不修改任何状态。 */
    virtual bool CanApplyGameplayEffect_Implementation(const UGameplayEffect* GameplayEffect, const FGameplayEffectSpec& Spec, UAbilitySystemComponent* ASC) const override;
};
/** 原生不可变GE模板，参数由每次Spec提供；构造入口均记录日志。 */
UCLASS() class FPSDEMO_API UDemoAmmoLoadoutEffect : public UGameplayEffect { GENERATED_BODY() public: /** 玩家装配持续至换类型或Avatar销毁。 */ UDemoAmmoLoadoutEffect(); };
UCLASS() class FPSDEMO_API UDemoBurnEffect : public UGameplayEffect { GENERATED_BODY() public: /** 有时限周期HP伤害，按目标叠层。 */ UDemoBurnEffect(); };
UCLASS() class FPSDEMO_API UDemoChillEffect : public UGameplayEffect { GENERATED_BODY() public: /** 冰霜栈与寿命，移速由独立GE更新。 */ UDemoChillEffect(); };
UCLASS() class FPSDEMO_API UDemoFrozenEffect : public UGameplayEffect { GENERATED_BODY() public: /** 定身Tag，由GAS计时移除。 */ UDemoFrozenEffect(); };
UCLASS() class FPSDEMO_API UDemoFrostImmunityEffect : public UGameplayEffect { GENERATED_BODY() public: /** 独立免疫Tag，命中不刷新。 */ UDemoFrostImmunityEffect(); };
UCLASS() class FPSDEMO_API UDemoChillMoveEffect : public UGameplayEffect { GENERATED_BODY() public: /** 由冰霜栈生命周期管理的移速倍率。 */ UDemoChillMoveEffect(); };

#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

namespace
{
	/** Effect 是构造中的 CDO，Attribute 指定加法属性；只在类构造期间配置。 */
	void ConfigureDelta(UGameplayEffect& Effect, const FGameplayAttribute& Attribute)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("%hs attribute=%s"), __FUNCTION__, *Attribute.GetName());
		Effect.DurationPolicy = EGameplayEffectDurationType::Instant;
		// SetByCaller 字段与所有调用者共用同一原生标签。
		FSetByCallerFloat Caller;
		Caller.DataTag = DemoTags::Magnitude;
		// 每个效果仅修改一项属性，避免升级和补给互相依赖。
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Caller);
		Effect.Modifiers.Add(Modifier);
	}
	/** Effect 为构造中模板，TagComponent 为其默认子对象，Tag 为冷却标记，Seconds 为正秒数。 */
	void ConfigureCooldown(UGameplayEffect& Effect, UTargetTagsGameplayEffectComponent& TagComponent, FGameplayTag Tag, float Seconds)
	{
		UE_LOG(LogFPSDemo, Log, TEXT("%hs tag=%s duration=%.2f"), __FUNCTION__, *Tag.ToString(), Seconds);
		Effect.DurationPolicy = EGameplayEffectDurationType::HasDuration;
		Effect.DurationMagnitude = FScalableFloat(Seconds);
		// 采用 UE5.4 GE Component API，避免使用已弃用的 GE 授予标签字段。
		FInheritedTagContainer Tags;
		Tags.AddTag(Tag);
		TagComponent.SetAndApplyTargetTagChanges(Tags);
	}
}

UDemoHealthEffect::UDemoHealthEffect() { DEMO_LOG_CALL(); ConfigureDelta(*this, UDemoAttributeSet::GetHealthAttribute()); }
UDemoPowerEffect::UDemoPowerEffect() { DEMO_LOG_CALL(); ConfigureDelta(*this, UDemoAttributeSet::GetWeaponDamageBonusAttribute()); }
UDemoMaxHealthEffect::UDemoMaxHealthEffect() { DEMO_LOG_CALL(); ConfigureDelta(*this, UDemoAttributeSet::GetMaxHealthAttribute()); }
UDemoMagazineEffect::UDemoMagazineEffect() { DEMO_LOG_CALL(); ConfigureDelta(*this, UDemoAttributeSet::GetMagazineBonusAttribute()); }
UDemoDashCooldownEffect::UDemoDashCooldownEffect()
{
	DEMO_LOG_CALL();
	// GE 持有的默认标签组件，生命周期与效果定义一致。
	UTargetTagsGameplayEffectComponent* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTags"));
	GEComponents.Add(Tags);
	ConfigureCooldown(*this, *Tags, DemoTags::DashCooldown, 4.f);
}
UDemoHealCooldownEffect::UDemoHealCooldownEffect()
{
	DEMO_LOG_CALL();
	// GE 持有的默认标签组件；激活 Spec 不会修改它。
	UTargetTagsGameplayEffectComponent* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("CooldownTags"));
	GEComponents.Add(Tags);
	ConfigureCooldown(*this, *Tags, DemoTags::HealCooldown, 12.f);
}

UDemoDashEvadeEffect::UDemoDashEvadeEffect()
{
	DEMO_LOG_CALL();
	// GE持有的默认标签组件，窗口在技能立即结束之后仍存活0.45秒。
	UTargetTagsGameplayEffectComponent* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("EvadeTags"));
	GEComponents.Add(Tags);
	ConfigureCooldown(*this, *Tags, DemoTags::DashEvading, .45f);
}

UDemoSlowEffect::UDemoSlowEffect()
{
	DEMO_LOG_CALL();
	// 默认3秒作为配置缺失时的模板值；实际每次在独立Spec指定，不更改CDO。
	UTargetTagsGameplayEffectComponent* Tags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("SlowTags"));
	GEComponents.Add(Tags);
	ConfigureCooldown(*this, *Tags, DemoTags::Slowed, 3.f);
	// 倍率为有时限Override，效果移除后恢复基础值；调用入口先移除旧减速防止叠乘。
	FSetByCallerFloat Caller;
	Caller.DataTag = DemoTags::Magnitude;
	FGameplayModifierInfo Modifier; // 本模板唯一的临时移动属性修改器。
	Modifier.Attribute = UDemoAttributeSet::GetMoveSpeedMultiplierAttribute();
	Modifier.ModifierOp = EGameplayModOp::Override;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Caller);
	Modifiers.Add(Modifier);
}

bool DemoEffects::ApplySlow(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Multiplier, float Seconds)
{
	UE_LOG(LogFPSDemo, Log, TEXT("%hs multiplier=%.2f seconds=%.2f"), __FUNCTION__, Multiplier, Seconds);
	if (!Source || !Target || !Source->IsOwnerActorAuthoritative() || !Target->IsOwnerActorAuthoritative()
		|| !FMath::IsFinite(Multiplier) || Multiplier < .1f || Multiplier > 1.f
		|| !FMath::IsFinite(Seconds) || Seconds < .1f || Seconds > 15.f)
	{
		UE_LOG(LogFPSDemo, Warning, TEXT("Slow rejected: authority/ASC/range"));
		return false;
	}
	// Spec仅在当前游戏线程调用持有；先校验后替换现有效果，避免多个Boss叠乘到无法移动。
	FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(UDemoSlowEffect::StaticClass(), 1.f, Source->MakeEffectContext());
	if (!Spec.IsValid()) { UE_LOG(LogFPSDemo, Warning, TEXT("Slow rejected: missing spec")); return false; }
	Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude, Multiplier);
	Spec.Data->SetDuration(Seconds, true);
	FGameplayTagContainer Tags; // 只移除Slow，不清掉升级或其他技能冷却。
	Tags.AddTag(DemoTags::Slowed);
	Target->RemoveActiveEffectsWithGrantedTags(Tags);
	return Source->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), Target).IsValid();
}

bool DemoEffects::Apply(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, TSubclassOf<UGameplayEffect> EffectClass, float Magnitude)
{
	UE_LOG(LogFPSDemo, Log, TEXT("%hs source=%s target=%s amount=%.2f"), __FUNCTION__, *GetNameSafe(Source), *GetNameSafe(Target), Magnitude);
	if (!Source || !Target || !EffectClass || !Source->IsOwnerActorAuthoritative() || !FMath::IsFinite(Magnitude))
	{
		UE_LOG(LogFPSDemo, Warning, TEXT("GE rejected: invalid source/target/class/magnitude or no authority"));
		return false;
	}
	// Spec 归此调用持有，Context 自动记录 ASC Owner/Avatar，方便追踪伤害来源。
	FGameplayEffectSpecHandle Spec = Source->MakeOutgoingSpec(EffectClass, 1.f, Source->MakeEffectContext());
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude, Magnitude);
	Source->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), Target);
	return true;
}

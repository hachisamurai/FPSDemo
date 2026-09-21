#include "GAS/DemoAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "Net/UnrealNetwork.h"

// 移动倍率基础值1确保无状态时仍使用原650cm/s步速。
UDemoAttributeSet::UDemoAttributeSet() : MoveSpeedMultiplier(1.f), Health(100.f), MaxHealth(100.f), AttackPower(25.f), WeaponDamageBonus(0.f), MagazineBonus(0.f)
{
	DEMO_LOG_CALL();
}

void UDemoAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	DEMO_LOG_CALL();
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, WeaponDamageBonus, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDemoAttributeSet, MagazineBonus, COND_None, REPNOTIFY_Always);
}

void UDemoAttributeSet::ClampValue(const FGameplayAttribute& Attribute, float& NewValue) const
{
	DEMO_LOG_TICK();
	if (Attribute == GetHealthAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	else if (Attribute == GetMaxHealthAttribute()) NewValue = FMath::Max(1.f, NewValue);
	// 局内武器加成独立于敌人攻击力，设置合理上限避免错误配置溢出整数弹药。
	else if (Attribute == GetWeaponDamageBonusAttribute() || Attribute == GetMagazineBonusAttribute()) NewValue = FMath::Clamp(NewValue, 0.f, 10000.f);
	else if (Attribute == GetAttackPowerAttribute()) NewValue = FMath::Max(0.f, NewValue);
	// 只允许减速；到期重新聚合为基础值1，不手动覆盖其他属性。
	else if (Attribute == GetMoveSpeedMultiplierAttribute()) NewValue = FMath::Clamp(NewValue, .1f, 1.f);
}

void UDemoAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	DEMO_LOG_CALL();
	Super::PreAttributeChange(Attribute, NewValue);
	ClampValue(Attribute, NewValue);
}

void UDemoAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	DEMO_LOG_CALL();
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampValue(Attribute, NewValue);
}

bool UDemoAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	DEMO_LOG_CALL();
	// 本项目枪伤/爆炸/持续伤害均通过负向加法Health GE；同时防御更低的Override值。
	if (Data.EvaluatedData.Attribute==GetHealthAttribute() && GetOwningAbilitySystemComponent()->HasMatchingGameplayTag(DemoBossTags::Invulnerable)
		&& ((Data.EvaluatedData.ModifierOp==EGameplayModOp::Additive && Data.EvaluatedData.Magnitude<0)
			|| (Data.EvaluatedData.ModifierOp==EGameplayModOp::Override && Data.EvaluatedData.Magnitude<GetHealth())))
	{ UE_LOG(LogFPSDemo,Log,TEXT("DAMAGE_BLOCKED invulnerable target=%s magnitude=%.1f"),*GetNameSafe(GetOwningActor()),Data.EvaluatedData.Magnitude); return false; }
	return Super::PreGameplayEffectExecute(Data);
}
void UDemoAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	DEMO_LOG_CALL();
	Super::PostGameplayEffectExecute(Data);
	if (GetHealth() > GetMaxHealth()) SetHealth(GetMaxHealth());
	UE_LOG(LogFPSDemo, Log, TEXT("GE target=%s attribute=%s magnitude=%.2f health=%.1f weaponBonus=%.1f capacityBonus=%.0f"),
		*GetNameSafe(GetOwningActor()), *Data.EvaluatedData.Attribute.GetName(), Data.EvaluatedData.Magnitude, GetHealth(), GetWeaponDamageBonus(), GetMagazineBonus());
}

// Property 指定复制属性；OldValue 是引擎传入的旧值，不被回调保存。
#define DEMO_REP(Property) void UDemoAttributeSet::OnRep_##Property(const FGameplayAttributeData& OldValue) \
{ DEMO_LOG_CALL(); GAMEPLAYATTRIBUTE_REPNOTIFY(UDemoAttributeSet, Property, OldValue); }
DEMO_REP(Health)
DEMO_REP(MoveSpeedMultiplier)
DEMO_REP(MaxHealth)
DEMO_REP(AttackPower)
DEMO_REP(WeaponDamageBonus)
DEMO_REP(MagazineBonus)
#undef DEMO_REP

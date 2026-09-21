#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "DemoEffects.generated.h"

/** 有符号生命变化：伤害为负、治疗为正，Magnitude 通过 SetByCaller 注入。 */
UCLASS() class FPSDEMO_API UDemoHealthEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 构造不可变的瞬时生命 GE 模板。 */
	UDemoHealthEffect();
};
/** 本局伤害升级。 */
UCLASS() class FPSDEMO_API UDemoPowerEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 构造玩家WeaponDamageBonus加法GE，不改变敌人AttackPower。 */
	UDemoPowerEffect();
};
/** 本局生命上限升级，生命补给使用独立 Health GE，顺序可审计。 */
UCLASS() class FPSDEMO_API UDemoMaxHealthEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 构造生命上限加法 GE。 */
	UDemoMaxHealthEffect();
};
/** 本局弹匣容量升级。 */
UCLASS() class FPSDEMO_API UDemoMagazineEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 构造玩家MagazineBonus加法GE，作用于所有持有武器。 */
	UDemoMagazineEffect();
};
/** Dash/Heal使用独立有时限冷却GE；开火间隔由武器实例与Fire GA冷却接口维护。 */
UCLASS() class FPSDEMO_API UDemoDashCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 默认 4 秒冲刺间隔。 */
	UDemoDashCooldownEffect();
};
UCLASS() class FPSDEMO_API UDemoHealCooldownEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 默认 12 秒治疗间隔。 */
	UDemoHealCooldownEffect();
};

/** 成功冲刺后短暂授予躲避窗口；不是通用伤害免疫。 */
UCLASS() class FPSDEMO_API UDemoDashEvadeEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 设置0.45秒窗口标签；冷却与窗口各自独立到期。 */
	UDemoDashEvadeEffect();
};
/** Boss全图技能的临时步速倍率；同类效果刷新，绝不叠乘。 */
UCLASS() class FPSDEMO_API UDemoSlowEffect : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 设置有时限标签及 SetByCaller 倍率，具体时长通过 Spec 覆盖。 */
	UDemoSlowEffect();
};

/** GE 工具只创建 Spec，不修改共享 CDO；调用方必须处于服务器。 */
namespace DemoEffects
{
	/** Source/Target 是有效权威ASC；Multiplier=[0.1,1]，Seconds=[0.1,15]；刷新现有减速，成功返回true。 */
	FPSDEMO_API bool ApplySlow(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target, float Multiplier, float Seconds);
	/** Source 为来源 ASC，Target 为目标 ASC，EffectClass 为模板，Magnitude 为有符号点数。
	 * 支持瞬时属性变化或固定时长状态（Dash窗口传0）；返回Spec创建/提交成功，不保证数值改变。 */
	FPSDEMO_API bool Apply(UAbilitySystemComponent* Source, UAbilitySystemComponent* Target,
		TSubclassOf<UGameplayEffect> EffectClass, float Magnitude);
}

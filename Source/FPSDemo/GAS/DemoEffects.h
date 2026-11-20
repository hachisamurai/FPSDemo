// 职责：原生属性/冷却GE与服务器Spec提交工具；Apply保持旧合同，ApplyDamage附加玩家受击表现元数据，不裁决命中或播放表现。
#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "NativeGameplayTags.h"
#include "Combat/Feedback/Player/DemoPlayerDamageTypes.h"
#include "DemoEffects.generated.h"

/** 受击元数据只放在本次Spec动态AssetTags；不是授予玩家的状态Tag，不可作为伤害授权。 */
namespace DemoPlayerDamageTags
{
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Root); // 只有受击辅助入口添加，区分普通初始化/治疗GE。
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Unknown);
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Projectile);
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Melee);
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Charge);
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Area);
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Global);
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Periodic);
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Environment);
    FPSDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Omni); // 全图/周期/环境明确无方位，不能回退指向当前施法者。
}

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
    /** 权威攻击已通过命中/Session/闪避门禁后调用；Damage为非负请求HP，Kind只分类表现。
     * Origin是当次接触源/锁定爆心的世界厘米；IncomingDirection为指向来袭方向的冻结单位向量（可零）。
     * 有向量时在目标当前点前方100cm保存等价Origin，防飞弹停止/来源移动丢失入射方向；Hit只在本同步栈借用并复制。
     * 返回Spec创建/提交成功，不保证HP变化；不授予免伤、不播放声音，非法位置仅降级元数据而不取消合法伤害。 */
    FPSDEMO_API bool ApplyDamage(UAbilitySystemComponent* Source,UAbilitySystemComponent* Target,float Damage,
        EDemoPlayerDamageKind Kind,const FVector& Origin,const FVector& IncomingDirection=FVector::ZeroVector,const FHitResult* Hit=nullptr);
}

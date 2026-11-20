// 职责：Dash独立玩家GA。使用：由ASC授予，行为在对应CPP；沿用原反射类名，资源无需迁移。
#pragma once
#include "GAS/DemoGameplayAbility.h"
#include "DemoDashAbility.generated.h"
UCLASS(Blueprintable) class FPSDEMO_API UDemoDashAbility : public UDemoGameplayAbility
{
    GENERATED_BODY()
public:
    /** 水平方向冲刺，4秒冷却；允许安全区及备战区使用。 */
    UDemoDashAbility();
    /** Handle/ActorInfo/ActivationInfo为当前GAS激活上下文；只修改本次冷却Spec，不改CDO。 */
    virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
    /** 本次GAS上下文及事件只借用；服务器提交冷却后只推动所属Avatar。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

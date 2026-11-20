// 职责：Heal独立玩家GA。使用：由ASC授予，行为在对应CPP；沿用原反射类名，资源无需迁移。
#pragma once
#include "GAS/DemoGameplayAbility.h"
#include "DemoHealAbility.generated.h"
UCLASS(Blueprintable) class FPSDEMO_API UDemoHealAbility : public UDemoGameplayAbility
{
    GENERATED_BODY()
public:
    /** 恢复角色配置生命值，12秒冷却；满血检查由自身技能维护。 */
    UDemoHealAbility();
    /** GAS上下文及Tags只借用；满生命拒绝，且不消耗冷却。 */
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    /** 本次GAS上下文及事件只借用；治疗只施加到所属玩家ASC。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

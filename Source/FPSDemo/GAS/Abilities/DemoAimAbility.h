// 职责：Aim独立玩家GA。使用：由ASC授予，行为在对应CPP；沿用原反射类名，资源无需迁移。
#pragma once
#include "GAS/DemoGameplayAbility.h"
#include "DemoAimAbility.generated.h"
UCLASS(Blueprintable) class FPSDEMO_API UDemoAimAbility : public UDemoGameplayAbility
{
    GENERATED_BODY()
public:
    /** 持有Aiming标签；Reloading时禁止进入。 */
    UDemoAimAbility();
    /** GAS上下文及Tags只借用；额外检查当前枪支持瞄准且未装填。 */
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    /** 本次GAS上下文只借用；Commit后进入一级镜并保持激活至取消。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    /** GAS结束标记沿用父类；恢复当前Avatar镜头，不能修改别的玩家摄像机。 */
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};

// 职责：真正行为天赋的GA基类。用法：Definition按需引用，管理组件以Definition作为SourceObject授予；支持蓝图/AbilityTask自主结束。
#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DemoTalentAbility.generated.h"
class UDemoTalentDefinition;
/** 只约束权威与授权，绝不在激活后强制EndAbility；即时事件请继承DemoTalentEventAbility。 */
UCLASS(Abstract,Blueprintable)
class FPSDEMO_API UDemoTalentAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** 每ASC独立实例、服务器执行；CDO仅保存配置，异步任务由Ability生命周期持有。 */
    UDemoTalentAbility();
    /** Rank为账本/预览等级；返回此行为的等级说明，Definition组合展示。 */
    UFUNCTION(BlueprintNativeEvent,Category="Talent") FText Describe(int32 Rank) const;
    /** 返回当前Spec来源定义；仅已授予实例有效，CDO/撤销期间可能为空，调用方不得修改。 */
    UFUNCTION(BlueprintPure,Category="Talent") const UDemoTalentDefinition* GetTalentDefinition() const;
    /** 激活前校验本人ASC/存活/Combat/定义权限；Handle/ActorInfo为借用上下文，Tags为引擎可选检查输入输出。 */
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags=nullptr,const FGameplayTagContainer* TargetTags=nullptr,FGameplayTagContainer* OptionalRelevantTags=nullptr) const override;
    /** GAS激活入口；上下文均为本次借用，转给Super的蓝图事件，不代替派生类完成或取消AbilityTask。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
protected:
    /** Tag为已注册事件；构造时登记触发，蓝图也可在Ability Triggers配置。 */
    void ListenFor(FGameplayTag Tag);
};

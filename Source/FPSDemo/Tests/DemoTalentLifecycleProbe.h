// 职责：验证通用天赋GA允许AbilityTask跨帧运行。用法：仅Editor战斗测试显式授予，生产目录不引用；依赖真实ASC和WaitDelay任务。
#pragma once
#include "GAS/Talents/DemoTalentAbility.h"
#include "DemoTalentLifecycleProbe.generated.h"
/** 反射类型可编译于包体，但测试激活实现/授予入口仅WITH_EDITOR，普通游戏不可通过Debug调用。 */
UCLASS(NotBlueprintable,Transient)
class UDemoTalentLifecycleProbe : public UDemoTalentAbility
{
    GENERATED_BODY()
public:
    /** Handle/ActorInfo/ActivationInfo/TriggerEventData由GAS借用；Editor创建60秒任务，测试会先显式取消验证清理。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
private:
    /** WaitDelay的UObject委托，仅有效Ability实例在游戏线程触发；取消/销毁任务后不回调。 */
    UFUNCTION() void OnDelayFinished();
};

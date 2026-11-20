// 职责：赏金靶场独立训练射击GA。使用：武器输入检测个人训练实例后临时授予并激活；不修改战斗开火GA或消耗正式弹药。
#pragma once
#include "Abilities/GameplayAbility.h"
#include "DemoRangeShotAbility.generated.h"
UCLASS()
class FPSDEMO_API UDemoRangeShotAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** 设置服务器独占执行，实例按拥有者创建。 */
    UDemoRangeShotAbility();
    /** 只调用当前Avatar所属训练实例的权威射线结算；无训练实例直接结束。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* Info,const FGameplayAbilityActivationInfo Activation,const FGameplayEventData* Event) override;
};

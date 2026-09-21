#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DemoEnemyAnimationAbility.generated.h"

/** 表现专用 GAS Ability：唯一职责是 ASC Montage 播放/取消，绝不结算伤害或修改冷却。 */
UCLASS()
class FPSDEMO_API UDemoEnemyAnimationAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** 按 Actor 实例化，由服务器请求；不声明会阻断现有攻击的技能标签。 */
    UDemoEnemyAnimationAbility();
    /** Handle/ActorInfo/ActivationInfo为 GAS 当次上下文；TriggerEventData只借用，本实现不依赖事件负载。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
private:
    /** Task 完成/中断/取消的 UObject 委托；结束只清理本表现技能，不回调伤害。 */
    UFUNCTION() void FinishAnimation();
};

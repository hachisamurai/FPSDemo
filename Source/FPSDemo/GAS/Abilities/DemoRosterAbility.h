// 职责：扩展敌人施法的GAS生命周期门；仅服务器实例，执行细节委托EnemyRoster，无异步裸捕获。
#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DemoRosterAbility.generated.h"
UCLASS()
class FPSDEMO_API UDemoRosterAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** 设置ServerOnly、InstancedPerActor，施法期间授予统一Casting标签。 */
    UDemoRosterAbility();
    /** GAS提供Handle/ActorInfo/ActivationInfo/Trigger；没有合法Roster动作则立即取消。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
    /** 所有结束路径清除组件读条；上下文由GAS拥有，bWasCancelled区分被打断。 */
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled) override;
    /** 服务器仲裁完成实际动作后结束，bCancelled用于打断/冻结。 */
    void Complete(bool bCancelled);
};

// 职责：同步事件天赋专用GA。用法：补给/击杀效果继承此类，延迟/蓄力技能继承DemoTalentAbility；依赖GAS事件与GC。
#pragma once
#include "GAS/Talents/DemoTalentAbility.h"
#include "DemoTalentEventAbility.generated.h"
UCLASS(Abstract,Blueprintable)
class FPSDEMO_API UDemoTalentEventAbility : public UDemoTalentAbility
{
    GENERATED_BODY()
public:
    /** 接受事件后播放的可选表现Tag，GC不能伤害/发奖；多阶段技能由自身管理Cue。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|GAS") FGameplayTag EventCue;
    /** Event只借用至本次同步调用，Rank为Spec等级；返回是否执行，不能从此入口启动延迟任务。 */
    UFUNCTION(BlueprintNativeEvent,Category="Talent") bool OnTalentEvent(const FGameplayEventData& Event,int32 Rank);
    /** GAS事件入口：借用Handle/ActorInfo/ActivationInfo/TriggerEventData，立即执行和结束；拒绝空事件，不保留目标裸引用。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
};

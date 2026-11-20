// 职责：护盾卫士的可复制GAS耐久；Enemy创建，Roster初始化，ShieldEffect消耗；不写存档。
#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "DemoEnemyDefenseSet.generated.h"
UCLASS()
class FPSDEMO_API UDemoEnemyDefenseSet : public UAttributeSet
{
    GENERATED_BODY()
public:
    /** 原生默认0表示无护盾，只有Guard初始化为正值。 */
    UDemoEnemyDefenseSet();
    // 当前剩余点数；服务端GE写入，复制供客户端盾壳/调试读取。
    UPROPERTY(ReplicatedUsing=OnRep_Shield) FGameplayAttributeData Shield;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UDemoEnemyDefenseSet,Shield)
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Shield)
    /** NewValue为聚合点数；钳制负值，防止耗尽后产生负盾。 */
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute,float& NewValue) override;
    /** OutLifetimeProps由引擎提供，注册Shield复制。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    /** OldValue为复制前属性，通知GAS聚合器，不在客户端结算。 */
    UFUNCTION() void OnRep_Shield(const FGameplayAttributeData& OldValue);
};
UCLASS()
class FPSDEMO_API UDemoShieldEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 瞬时SetByCaller耐久变化，使用DemoEffects统一提交Spec。 */
    UDemoShieldEffect();
};

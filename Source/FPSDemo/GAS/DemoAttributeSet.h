#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Debug/DemoLog.h"
#include "UObject/UnrealType.h"
#include "DemoAttributeSet.generated.h"

// Class/Property 为生成属性反射访问器的类型和成员；读取同样保留高频日志。
// NewValue 是服务器写入的属性基础值；运行时修改统一从 GE 进入，Setter 只供钳制使用。
#define DEMO_ATTRIBUTE_ACCESSORS(Class, Property) \
	static FGameplayAttribute Get##Property##Attribute() { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("%hs"), __FUNCTION__); return FGameplayAttribute(FindFieldChecked<FProperty>(Class::StaticClass(), GET_MEMBER_NAME_CHECKED(Class, Property))); } \
	float Get##Property() const { DEMO_LOG_TICK(); return Property.GetCurrentValue(); } \
	void Set##Property(float NewValue) { DEMO_LOG_TICK(); GetOwningAbilitySystemComponentChecked()->SetNumericAttributeBase(Get##Property##Attribute(), NewValue); }

/** 玩家和敌人共享的属性边界；参考 Lyra，在结算处钳制并通过 ASC 属性委托响应死亡。 */
UCLASS()
class FPSDEMO_API UDemoAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	/** 默认健康与零武器加成；敌人生成后覆盖AttackPower，弹药由各武器实例持有。 */
	UDemoAttributeSet();
	// 临时移动倍率 [0.1,1]，基础值1；减速GE到期由GAS自动恢复，复制通知驱动角色步速。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MoveSpeedMultiplier, Category="Demo|Attributes")
	FGameplayAttributeData MoveSpeedMultiplier;
	DEMO_ATTRIBUTE_ACCESSORS(UDemoAttributeSet, MoveSpeedMultiplier)
	// 当前生命 [0,MaxHealth]，由服务器结算，复制到客户端。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="Demo|Attributes")
	FGameplayAttributeData Health;
	DEMO_ATTRIBUTE_ACCESSORS(UDemoAttributeSet, Health)
	// 生命上限至少为 1，升级永久增加本局基础值。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth, Category="Demo|Attributes")
	FGameplayAttributeData MaxHealth;
	DEMO_ATTRIBUTE_ACCESSORS(UDemoAttributeSet, MaxHealth)
	// 敌人攻击点数，非负；玩家武器基础伤害来自BP配置，升级使用独立WeaponDamageBonus。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_AttackPower, Category="Demo|Attributes")
	FGameplayAttributeData AttackPower;
	DEMO_ATTRIBUTE_ACCESSORS(UDemoAttributeSet, AttackPower)
	// 玩家本局每次触发的额外伤害，非负默认0；霰弹按弹丸数分摊。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_WeaponDamageBonus, Category="Demo|Attributes")
	FGameplayAttributeData WeaponDamageBonus;
	DEMO_ATTRIBUTE_ACCESSORS(UDemoAttributeSet, WeaponDamageBonus)
	// 玩家所有武器的额外弹匣容量，非负默认0；武器使用向下取整后的发数。
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MagazineBonus, Category="Demo|Attributes")
	FGameplayAttributeData MagazineBonus;
	DEMO_ATTRIBUTE_ACCESSORS(UDemoAttributeSet, MagazineBonus)
	/** OutLifetimeProps 是引擎填写的复制列表，所有属性使用 Always 通知以兼容 GAS。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** Attribute 标识变动字段，NewValue 为可被钳制的当前值。 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	/** 基础值入口也需要钳制；Attribute/NewValue 语义同上，避免直接初始化绕过边界。 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	/** Data为即将执行的瞬时/周期GE修改器；无敌时在扣血前拒绝负向Health变化，避免触发死亡后补血。 */
	virtual bool PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data) override;
	/** Data 为本次 GE 结算上下文；确保生命不超过上限并记录属性变化。 */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
private:
	/** OldValue 为复制前的移动倍率；交给 GAS 广播属性委托，驱动移动组件。 */
	UFUNCTION() void OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue);
	/** Attribute 指定字段；NewValue 按合法范围就地修正。 */
	void ClampValue(const FGameplayAttribute& Attribute, float& NewValue) const;
	/** OldValue 是复制前属性；各回调转交 GAS 聚合器并打印调用日志。 */
	UFUNCTION() void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_AttackPower(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_WeaponDamageBonus(const FGameplayAttributeData& OldValue);
	UFUNCTION() void OnRep_MagazineBonus(const FGameplayAttributeData& OldValue);
};

// 职责：GAS可复制属性与健康结算门禁。使用：伤害/治疗/移动通过GE聚合，Pre/PostExecute拒绝非法状态，不直接发奖励。
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Debug/DemoLog.h"
#include "UObject/UnrealType.h"
#include "Combat/Feedback/Player/DemoPlayerDamageTypes.h"
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
    // 射速乘数基础1、有限且>=1，按天赋等级由GE聚合复制；不写成长基础值，读档从等级重建。
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_FireRateMultiplier, Category="Demo|Attributes")
    FGameplayAttributeData FireRateMultiplier;
    DEMO_ATTRIBUTE_ACCESSORS(UDemoAttributeSet, FireRateMultiplier)
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
	/** 当次真实写入的Old/New，早于死亡委托；只冻结执行帧，不在这里发事件或派生GE。 */
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	/** Data 为本次 GE 结算上下文；确保生命不超过上限并记录属性变化。 */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FDemoPlayerDamageContractTest; // Editor纯合同专项可检查帧快照；不新增游戏/蓝图写入接口。
#endif
    struct FHealthExecuteFrame // 同步栈内快照；弱引用不能延长已阵亡Avatar/PC/敌人生命周期，不复制/保存。
    {
        FDemoPlayerDamageCue Cue; // 本次写入冻结的值，Post之后不再持有GE/Spec指针。
        TWeakObjectPtr<class ADemoCharacter> Player; // Pre时的原Avatar，不在Post重新绑定当前Avatar。
        TWeakObjectPtr<class ADemoEnemy> Enemy; // B07原来源过滤，不参与HUD猜测方向。
        TWeakObjectPtr<class UDemoPlayerDamageFeedbackComponent> Feedback; // 原PC的受击发送组件。
        float Before=0.f; // Pre生命，限制实际损失不含过量伤害。
        bool bDamage=false; // 只给会降低Health的权威玩家GE采集，不采集读档/治疗。
        bool bCaptured=false; // 第一次匹配Health写入即消费，之后同步治疗不可覆盖事实。
    };
    TArray<FHealthExecuteFrame,TInlineAllocator<4>> HealthExecuteStack; // 每个获准Health修改器一帧，Post先弹再发布，兼容嵌套GE。
    /** OldValue为复制前射速；通知GAS聚合监听，不改武器CDO或已经开始的冷却。 */
    UFUNCTION() void OnRep_FireRateMultiplier(const FGameplayAttributeData& OldValue);
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

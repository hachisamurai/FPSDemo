// 职责：Fire独立玩家GA。使用：由ASC授予，行为在对应CPP；沿用原反射类名，资源无需迁移。
#pragma once
#include "GAS/DemoGameplayAbility.h"
#include "DemoFireAbility.generated.h"
UCLASS(Blueprintable) class FPSDEMO_API UDemoFireAbility : public UDemoGameplayAbility
{
	GENERATED_BODY()
public:
	/** Combat/Intermission允许射击，装填与菜单仍阻塞；成本/间隔从当前武器读取，不绑定固定弹药GE。 */
    UDemoFireAbility();
    /** Handle/ActorInfo/ActivationInfo为GAS本次上下文，TriggerEventData只借用；先准备全组再Commit，固定同一武器。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    /** GAS结束/取消时撤销尚未发射的准备凭据；参数遵循父类，不影响已飞行子弹。 */
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	/** Handle/ActorInfo为当前激活上下文；OptionalRelevantTags可写入失败原因，检查当前武器弹药。 */
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/** 同一上下文的成本提交；ActivationInfo为GAS提交信息，只在权威同步调用中扣弹。 */
	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
	/** 同一GAS上下文检查每武器冷却，OptionalRelevantTags为可选失败标签输出。 */
	virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/** 同一GAS上下文提交RPM计算的下次时间；不会在切枪时移除冷却。 */
    virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
private:
    // 仅同步准备/提交窗口持有的武器弱引用；成本/冷却不能在回调换枪后重新选择目标。
    TWeakObjectPtr<class ADemoWeaponBase> CommitWeapon;
    // 区分未激活的GAS预检与激活内凭据校验，失效弱引用不能回退到新装备。
    bool bPreparingFire = false;
    // 单实例递增代数；同步蓝图/GAS委托取消并重新激活后，旧调用栈不能结束或提交新一枪。
    uint64 FireGeneration = 0;
};

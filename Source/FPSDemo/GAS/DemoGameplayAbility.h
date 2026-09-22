#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DemoGameplayAbility.generated.h"

/** 原生技能类型；派生类只设置类型、成本和冷却，激活生命周期统一实现。 */
UENUM()
enum class EDemoAbilityAction : uint8 { Fire, Reload, Dash, Heal, Aim };

/** 单人服务器技能基类，使用 CommitAbility 和 AbilityTask 维护成本/冷却/取消。 */
UCLASS(Abstract)
class FPSDEMO_API UDemoGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	/** 配置按 Actor 实例化，死亡标签阻止激活。 */
	UDemoGameplayAbility();
	/** Handle 是已授予技能句柄，ActorInfo 是当次 Owner/Avatar；SourceTags/TargetTags 为可选约束，
	 * OptionalRelevantTags返回失败相关标签；Dash/Heal允许战斗/备战/安全区，武器限战斗，统一校验生命/菜单。 */
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/** Handle/ActorInfo/ActivationInfo 为 GAS 本次激活上下文；TriggerEventData 可空且不持有。
	 * 装填等待任务、瞄准保持激活，其余执行后立即结束；武器实例保留射击间隔。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** GAS 结束入口；上下文与激活相同，bReplicateEndAbility 指定同步结束，bWasCancelled 表示取消。 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
protected:
	// 子类构造时设置的动作类型，每个技能实例在整个授予周期保持不变。
	EDemoAbilityAction Action = EDemoAbilityAction::Fire;
private:
	// 装填任务启动时锁定的武器弱引用，切枪/销毁后绝不给新枪补弹。
	TWeakObjectPtr<class ADemoWeaponBase> ReloadWeapon;
	// 激活时复制武器动作序号；旧WaitDelay即使迟到也不能完成该武器的新一轮装填。
	int32 ReloadSequence = 0;
	/** WaitDelay 在游戏线程回调；技能取消后任务销毁，不会再补充弹药。 */
	UFUNCTION() void OnReloadFinished();
};

UCLASS() class FPSDEMO_API UDemoFireAbility : public UDemoGameplayAbility
{
	GENERATED_BODY()
public:
	/** 装填阻止射击；成本/间隔从当前武器读取，不绑定固定弹药GE。 */
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
UCLASS() class FPSDEMO_API UDemoReloadAbility : public UDemoGameplayAbility
{
	GENERATED_BODY()
public:
	/** 装填等待武器配置秒数；活动期间持有Reloading标签。 */
	UDemoReloadAbility();
};

/** 持续激活的瞄准状态，由装备组件右键切档，取消时恢复镜头。 */
UCLASS() class FPSDEMO_API UDemoAimAbility : public UDemoGameplayAbility
{
	GENERATED_BODY()
public:
	/** 配置Aim动作和Aiming标签；Reloading时禁止进入。 */
	UDemoAimAbility();
};
UCLASS() class FPSDEMO_API UDemoDashAbility : public UDemoGameplayAbility
{
	GENERATED_BODY()
public:
	/** 水平方向冲刺，4 秒冷却。 */
	UDemoDashAbility();
};
UCLASS() class FPSDEMO_API UDemoHealAbility : public UDemoGameplayAbility
{
	GENERATED_BODY()
public:
	/** 恢复 35 生命，12 秒冷却，满生命时拒绝且不消耗冷却。 */
	UDemoHealAbility();
};

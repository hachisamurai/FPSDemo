// 职责：玩家主动技能的公共GAS授权与结束门禁。使用：独立GA继承；兼容调用方统一包含DemoPlayerAbilityTypes.h。
#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "DemoGameplayAbility.generated.h"

/** 保留旧原生枚举名供反射兼容；运行时不再按Action集中分派，生命周期由每个派生技能维护。 */
UENUM()
enum class EDemoAbilityAction : uint8 { Fire, Reload, Dash, Heal, Aim };

/** 服务器权威技能公共门禁；派生类拥有成本提交、动作与任务，不保存其他技能的可变状态。 */
UCLASS(Abstract,Blueprintable)
class FPSDEMO_API UDemoGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
public:
	/** 配置按 Actor 实例化，死亡标签阻止激活。 */
	UDemoGameplayAbility();
	/** Handle 是已授予技能句柄，ActorInfo 是当次 Owner/Avatar；SourceTags/TargetTags 为可选约束，
	 * OptionalRelevantTags返回失败相关标签；Dash/Heal/近战挥击允许战斗/备战/安全区，Fire/Reload允许战斗/备战，其他动作限战斗；统一校验生命/菜单。 */
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	/** Handle/ActorInfo/ActivationInfo 为 GAS 本次激活上下文；TriggerEventData 可空且不持有。
	 * 基类不执行具体动作；派生类须实现生命周期，遗漏时明确拒绝，避免误提交成本。 */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** GAS 结束入口；上下文与激活相同，bReplicateEndAbility 指定同步结束，bWasCancelled 表示取消。 */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
protected:
    // Dash/Heal/近战子类开启：允许Hub/Intermission使用；不授予跨阶段伤害权限，不决定执行哪个动作。
    bool bAllowSafeAreaSkills = false;
    // 仅Fire/Reload启用：关间可以练枪/换弹，仍不允许Hub枪械动作，也不授予CombatSession伤害权限。
    bool bAllowIntermissionFirearmActions = false;
};

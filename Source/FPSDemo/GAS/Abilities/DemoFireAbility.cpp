// 职责：射击GA准备/提交事务。使用：ASC授予此类；全组实体弹成功准备后才扣弹，取消回滚。
#include "GAS/Abilities/DemoFireAbility.h"
#include "GAS/Abilities/DemoPlayerAbilityTypes.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"

UDemoFireAbility::UDemoFireAbility()
{
    DEMO_LOG_CALL();
    bAllowIntermissionFirearmActions = true; // 备战真实扣弹/飞行；伤害资格不由GA公共阶段开关决定。
    // Fire独自维护整组实体弹准备/提交/退款，不经由通用动作分支。
    ActivationBlockedTags.AddTag(DemoTags::Reloading);
}
namespace
{
	/** ActorInfo只在本次GAS调用期间借用，返回当前装备；不跨异步任务持有裸指针。 */
	ADemoWeaponBase* GetAbilityWeapon(const FGameplayAbilityActorInfo* ActorInfo)
	{
		UE_LOG(LogFPSDemo, VeryVerbose, TEXT("%hs"), __FUNCTION__);
		const ADemoCharacter* Character = ActorInfo ? Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
		return Character ? Character->GetWeaponComponent()->GetActiveWeapon() : nullptr;
	}
}
bool UDemoFireAbility::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	DEMO_LOG_CALL();
    const ADemoWeaponBase* Weapon = bPreparingFire ? CommitWeapon.Get() : GetAbilityWeapon(ActorInfo); // 激活内固定凭据，外部预检才查询当前装备。
	return Super::CheckCost(Handle,ActorInfo,OptionalRelevantTags) && Weapon && Weapon->CanPayShotCost();
}
void UDemoFireAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	DEMO_LOG_CALL();
	Super::ApplyCost(Handle,ActorInfo,ActivationInfo);
    if (ADemoWeaponBase* Weapon = CommitWeapon.Get()) Weapon->PayShotCost(); // 只扣准备武器，绝不转移到新装备。
}
bool UDemoFireAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	DEMO_LOG_CALL();
    const ADemoWeaponBase* Weapon = bPreparingFire ? CommitWeapon.Get() : GetAbilityWeapon(ActorInfo); // 每武器World时间戳，提交内不重新选择。
	return Super::CheckCooldown(Handle,ActorInfo,OptionalRelevantTags) && Weapon && Weapon->GetFireCooldownRemaining() <= KINDA_SMALL_NUMBER;
}
void UDemoFireAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	DEMO_LOG_CALL();
	Super::ApplyCooldown(Handle,ActorInfo,ActivationInfo);
    if (ADemoWeaponBase* Weapon = CommitWeapon.Get()) Weapon->CommitFireCooldown(); // 与扣弹共享同一准备实例。
}

void UDemoFireAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    // 不调用会立即Commit的通用Activate；完整准备成功后才允许GAS进入成本/冷却提交。
    const uint64 ActivationGeneration = ++FireGeneration; // 本次同步激活的身份，不用于网络预测或存档。
    bPreparingFire = true;
    CommitWeapon = GetAbilityWeapon(ActorInfo);
    const TWeakObjectPtr<ADemoWeaponBase> PreparingWeapon = CommitWeapon; // 跨构造/提交回调仍固定本实例，弱引用不延长武器生命。
    const bool bPrepared = PreparingWeapon.IsValid() && PreparingWeapon->PrepareShot(); // Actor构造可同步取消甚至再激活技能。
    if (FireGeneration != ActivationGeneration) return; // 旧End已经撤销旧凭据，不能干扰后继激活。
    if (!bPrepared || !IsActive() || !PreparingWeapon.IsValid() || !PreparingWeapon->IsPreparedShotValid())
    {
        UE_LOG(LogFPSDemo, Log, TEXT("FIRE_ABILITY_REJECT preparation failed"));
        EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
        return;
    }
    const bool bCommitted = CommitAbility(Handle, ActorInfo, ActivationInfo); // GAS提交也会同步广播外部委托，返回后重查代数。
    if (FireGeneration != ActivationGeneration) return;
    if (!bCommitted || !IsActive())
    { UE_LOG(LogFPSDemo, Log, TEXT("FIRE_ABILITY_REJECT GAS commit failed")); EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
    if (PreparingWeapon.IsValid()) PreparingWeapon->ExecuteCommittedShot();
    if (FireGeneration == ActivationGeneration) EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
void UDemoFireAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
    DEMO_LOG_CALL();
    ++FireGeneration; // 先作废旧栈，再销毁待发Actor/广播GAS结束。
    const TWeakObjectPtr<ADemoWeaponBase> EndingWeapon = CommitWeapon; // 先清引用，再撤销；销毁子弹可同步回调技能结束。
    CommitWeapon.Reset();
    bPreparingFire = false;
    if (EndingWeapon.IsValid()) EndingWeapon->CancelPreparedShot();
    Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

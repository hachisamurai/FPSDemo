#include "GAS/DemoGameplayAbility.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"

UDemoGameplayAbility::UDemoGameplayAbility()
{
	DEMO_LOG_CALL();
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActivationBlockedTags.AddTag(DemoTags::Dead);
}
UDemoFireAbility::UDemoFireAbility()
{
	DEMO_LOG_CALL();
	Action = EDemoAbilityAction::Fire;
	// 通过自定义GAS成本/冷却接口访问武器实例，避免四把枪共享一个弹匣。
	ActivationBlockedTags.AddTag(DemoTags::Reloading);
}
UDemoReloadAbility::UDemoReloadAbility()
{
	DEMO_LOG_CALL();
	Action = EDemoAbilityAction::Reload;
	ActivationOwnedTags.AddTag(DemoTags::Reloading);
}
UDemoDashAbility::UDemoDashAbility()
{
	DEMO_LOG_CALL();
	Action = EDemoAbilityAction::Dash;
	CooldownGameplayEffectClass = UDemoDashCooldownEffect::StaticClass();
}
UDemoHealAbility::UDemoHealAbility()
{
	DEMO_LOG_CALL();
	Action = EDemoAbilityAction::Heal;
	CooldownGameplayEffectClass = UDemoHealCooldownEffect::StaticClass();
}

UDemoAimAbility::UDemoAimAbility()
{
	DEMO_LOG_CALL();
	Action = EDemoAbilityAction::Aim;
	ActivationOwnedTags.AddTag(DemoTags::Aiming);
	ActivationBlockedTags.AddTag(DemoTags::Reloading);
}

bool UDemoGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	DEMO_LOG_CALL();
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags) || !ActorInfo) return false;
	// Pawn 与属性只在当前同步检查期间借用，不跨任务保存裸指针。
	const ADemoCharacter* Character = Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get());
	const UDemoAttributeSet* Attributes = Character ? Character->GetDemoAttributes() : nullptr;
	if (!Attributes || Attributes->GetHealth() <= 0.f) { UE_LOG(LogFPSDemo, Log, TEXT("Ability rejected: missing Avatar/attributes or dead")); return false; }
	// 放行战斗外移动/恢复技能，保持武器的战斗阶段边界；不能简单扩大所有GA的权限。
	const bool bPlayerSkill = Action == EDemoAbilityAction::Dash || Action == EDemoAbilityAction::Heal; // 本次动作分类，无跨帧状态。
	if (!(bPlayerSkill ? Character->CanUsePlayerSkills() : Character->CanUseCombatAbilities()))
	{ UE_LOG(LogFPSDemo, Log, TEXT("Ability rejected: action=%d phase/menu/paused"), static_cast<int32>(Action)); return false; }
	// 武器仅当前调用借用；装填/开镜使用同一装备实例，不从玩家AttributeSet读取弹药。
	const ADemoWeaponBase* Weapon = Character->GetWeaponComponent()->GetActiveWeapon();
	if (Action == EDemoAbilityAction::Reload) return Weapon && Weapon->CanReload();
	if (Action == EDemoAbilityAction::Aim) return Weapon && Weapon->Config.bSupportsScope && !Weapon->IsReloading();
	if (Action == EDemoAbilityAction::Heal) return Attributes->GetHealth() < Attributes->GetMaxHealth();
	return true;
}

void UDemoGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	DEMO_LOG_CALL();
	// ActorInfo 来自 GAS；角色失效或提交失败必须结束，避免技能永久处于激活状态。
	ADemoCharacter* Character = ActorInfo ? Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	switch (Action)
	{
	case EDemoAbilityAction::Fire: Character->PerformShot(); break;
	case EDemoAbilityAction::Dash: Character->PerformDash(); break;
	case EDemoAbilityAction::Heal:
		DemoEffects::Apply(GetAbilitySystemComponentFromActorInfo(), GetAbilitySystemComponentFromActorInfo(), UDemoHealthEffect::StaticClass(), Character->GetHealAmount());
		break;
	case EDemoAbilityAction::Reload:
		{
			// 从R或空弹射击进入相同GA；即使直接激活GA也停止连射/退出镜头。
			Character->GetWeaponComponent()->StopFire();
			Character->GetWeaponComponent()->StopAim();
			ReloadWeapon = Character->GetWeaponComponent()->GetActiveWeapon();
			if (!ReloadWeapon.IsValid() || !ReloadWeapon->BeginReload()) { EndAbility(Handle,ActorInfo,ActivationInfo,true,true); return; }
			ReloadSequence = ReloadWeapon->GetReloadSequence();
			// 动画可降级但不拥有补弹：本次类型/时长/序号均从BeginReload的冻结快照读取。
			if (Character->GetWeaponAnimationComponent() && ReloadWeapon->Config.WeaponAnimLayerClass)
				Character->GetWeaponAnimationComponent()->BeginReloadPresentation(ReloadWeapon.Get(), this, ReloadSequence, ReloadWeapon->GetReloadDuration(), ReloadWeapon->IsEmptyReload());
			// UObject 任务归技能所有，AddDynamic 弱绑定 this，取消/卸载时由 GAS 自动结束任务。
			UAbilityTask_WaitDelay* DelayTask = UAbilityTask_WaitDelay::WaitDelay(this, ReloadWeapon->GetReloadDuration());
			if (!DelayTask) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
			DelayTask->OnFinish.AddDynamic(this, &UDemoGameplayAbility::OnReloadFinished);
			DelayTask->ReadyForActivation();
			return;
		}
	case EDemoAbilityAction::Aim:
		if (Character->GetWeaponComponent()->BeginAim()) return; // 保持GA和OwnedTag直到主动退镜/取消。
		EndAbility(Handle,ActorInfo,ActivationInfo,true,true);
		return;
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UDemoGameplayAbility::OnReloadFinished()
{
	DEMO_LOG_CALL();
	// 回调时重新取得 Avatar，避免装填期间角色切换导致访问旧 Pawn。
	ADemoCharacter* Character = Cast<ADemoCharacter>(GetAvatarActorFromActorInfo());
	const UDemoAttributeSet* Attributes = Character ? Character->GetDemoAttributes() : nullptr;
	if (Attributes && Attributes->GetHealth() > 0.f && Character->CanUseCombatAbilities()
		&& ReloadWeapon.IsValid() && ReloadWeapon->GetReloadSequence() == ReloadSequence && Character->GetWeaponComponent()->IsEquipped(ReloadWeapon.Get()))
	{
		ReloadWeapon->CompleteReload();
	}
	else UE_LOG(LogFPSDemo, Log, TEXT("Reload callback ignored: stale weapon/avatar/phase"));
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UDemoGameplayAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	DEMO_LOG_CALL();
	UE_LOG(LogFPSDemo, Log, TEXT("Ability ended action=%d cancelled=%d"), static_cast<int32>(Action), bWasCancelled);
	if (Action == EDemoAbilityAction::Reload)
	{
		if (ReloadWeapon.IsValid() && ReloadWeapon->GetReloadSequence() == ReloadSequence) ReloadWeapon->CancelReload();
		ReloadWeapon.Reset();
		ReloadSequence = 0;
	}
	if (Action == EDemoAbilityAction::Aim)
		if (ADemoCharacter* Character = ActorInfo ? Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()) : nullptr) Character->GetWeaponComponent()->EndAim();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
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
	const ADemoWeaponBase* Weapon = GetAbilityWeapon(ActorInfo); // 同步成本预检，不消费。
	return Super::CheckCost(Handle,ActorInfo,OptionalRelevantTags) && Weapon && Weapon->CanPayShotCost();
}
void UDemoFireAbility::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	DEMO_LOG_CALL();
	Super::ApplyCost(Handle,ActorInfo,ActivationInfo);
	if (ADemoWeaponBase* Weapon = GetAbilityWeapon(ActorInfo)) Weapon->PayShotCost(); // GAS Commit同步扣一次。
}
bool UDemoFireAbility::CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	DEMO_LOG_CALL();
	const ADemoWeaponBase* Weapon = GetAbilityWeapon(ActorInfo); // 每武器World时间戳，换枪仍保留。
	return Super::CheckCooldown(Handle,ActorInfo,OptionalRelevantTags) && Weapon && Weapon->GetFireCooldownRemaining() <= KINDA_SMALL_NUMBER;
}
void UDemoFireAbility::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	DEMO_LOG_CALL();
	Super::ApplyCooldown(Handle,ActorInfo,ActivationInfo);
	if (ADemoWeaponBase* Weapon = GetAbilityWeapon(ActorInfo)) Weapon->CommitFireCooldown();
}

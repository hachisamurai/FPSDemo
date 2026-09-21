#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoGameplayAbility.h"
#include "Debug/DemoLog.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/DemoFPAnimInstance.h"

UDemoAbilitySystemComponent::UDemoAbilitySystemComponent()
{
	DEMO_LOG_CALL();
	SetIsReplicatedByDefault(true);
	SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

bool UDemoAbilitySystemComponent::SetFirstPersonAnimationMesh(USkeletalMeshComponent* Arms)
{
	DEMO_LOG_CALL();
	if (!AbilityActorInfo.IsValid() || !Arms || Arms->GetOwner() != GetAvatarActor()
		|| !Cast<UDemoFPAnimInstance>(Arms->GetAnimInstance()))
	{
		UE_LOG(LogFPSDemo, Warning, TEXT("FP_ASC_CONTEXT_REJECT mesh=%s avatar=%s"), *GetNameSafe(Arms), *GetNameSafe(GetAvatarActor()));
		return false;
	}
	// UE默认查找第一个SkeletalMesh；显式绑定主手臂，Linked Layer仍不拥有独立GAS Montage通道。
	AbilityActorInfo->SkeletalMeshComponent = Arms;
	AbilityActorInfo->AnimInstance = Arms->GetAnimInstance();
	AbilityActorInfo->AffectedAnimInstanceTag = NAME_None;
	UE_LOG(LogFPSDemo, Log, TEXT("FP_ASC_CONTEXT mesh=%s anim=%s"), *Arms->GetName(), *GetNameSafe(Arms->GetAnimInstance()));
	return true;
}

void UDemoAbilitySystemComponent::GrantStartupAbilities()
{
	DEMO_LOG_CALL();
	if (!IsOwnerActorAuthoritative() || bStartupGranted) return;
	bStartupGranted = true;
	GiveAbility(FGameplayAbilitySpec(UDemoFireAbility::StaticClass(), 1));
	GiveAbility(FGameplayAbilitySpec(UDemoReloadAbility::StaticClass(), 1));
	GiveAbility(FGameplayAbilitySpec(UDemoDashAbility::StaticClass(), 1));
	GiveAbility(FGameplayAbilitySpec(UDemoHealAbility::StaticClass(), 1));
	GiveAbility(FGameplayAbilitySpec(UDemoAimAbility::StaticClass(), 1));
}

bool UDemoAbilitySystemComponent::ActivateDemoAbility(TSubclassOf<UGameplayAbility> AbilityClass)
{
	DEMO_LOG_CALL();
	// TryActivate 返回值涵盖 GAS 成本、冷却、标签和本 Demo 的阶段约束。
	const bool bActivated = AbilityClass && TryActivateAbilityByClass(AbilityClass);
	if (!bActivated) UE_LOG(LogFPSDemo, Log, TEXT("Ability rejected: %s (phase, cost, cooldown or state)"), *GetNameSafe(AbilityClass));
	return bActivated;
}

void UDemoAbilitySystemComponent::CancelDemoAbility(TSubclassOf<UGameplayAbility> AbilityClass)
{
	DEMO_LOG_CALL();
	// Spec由ASC持有，只借用句柄；CancelAbilityHandle同步结束AbilityTask及武器状态。
	if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromClass(AbilityClass)) CancelAbilityHandle(Spec->Handle);
}

float UDemoAbilitySystemComponent::GetCooldownRemaining(FGameplayTag Tag) const
{
	DEMO_LOG_TICK();
	// Query 只匹配拥有此冷却标签的活动 GE，不检查已结束的技能实例。
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag));
	// 多个同类冷却存在时取最大剩余时间，以免 HUD 提前显示可用。
	float Remaining = 0.f;
	// Time 是活动 GE 的剩余秒数；查询数组仅在本函数存活。
	for (float Time : GetActiveEffectsTimeRemaining(Query)) Remaining = FMath::Max(Remaining, Time);
	return Remaining;
}

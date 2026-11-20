// 职责：天赋行为的通用GAS门禁。用法：派生GA遵守Super::CanActivateAbility，持续行为自行管理Commit/End/取消；不包含任何天赋ID分支。
#include "GAS/Talents/DemoTalentAbility.h"
#include "GAS/Talents/DemoTalentDefinition.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoTags.h"
#include "Game/DemoGameState.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"
UDemoTalentAbility::UDemoTalentAbility()
{
    DEMO_LOG_CALL();
    InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy=EGameplayAbilityNetExecutionPolicy::ServerOnly;
    ActivationBlockedTags.AddTag(DemoTags::Dead); // 个体阵亡即使尚未销毁Avatar也不能重新激活天赋。
}
void UDemoTalentAbility::ListenFor(FGameplayTag Tag)
{
    DEMO_LOG_CALL();
    FAbilityTriggerData Trigger; // CDO配置，GAS在授予时注册、移除时注销。
    Trigger.TriggerTag=Tag; Trigger.TriggerSource=EGameplayAbilityTriggerSource::GameplayEvent; AbilityTriggers.Add(Trigger);
}
FText UDemoTalentAbility::Describe_Implementation(int32 Rank) const { DEMO_LOG_TICK(); return FText::GetEmpty(); }
const UDemoTalentDefinition* UDemoTalentAbility::GetTalentDefinition() const
{
    DEMO_LOG_TICK();
    return Cast<UDemoTalentDefinition>(GetSourceObject(CurrentSpecHandle,CurrentActorInfo));
}
bool UDemoTalentAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayTagContainer* SourceTags,const FGameplayTagContainer* TargetTags,FGameplayTagContainer* OptionalRelevantTags) const
{
    DEMO_LOG_CALL();
    const UAbilitySystemComponent* ASC=ActorInfo?ActorInfo->AbilitySystemComponent.Get():nullptr; // 引擎本次激活的拥有者，不能读取Player0。
    const ADemoCharacter* Pawn=ActorInfo?Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()):nullptr; // 当前Avatar，防止跨重生借用。
    const ADemoGameState* State=Pawn&&Pawn->GetWorld()?Pawn->GetWorld()->GetGameState<ADemoGameState>():nullptr; // 本World战斗门禁。
    const UDemoTalentDefinition* Definition=ActorInfo?Cast<UDemoTalentDefinition>(GetSourceObject(Handle,ActorInfo)):nullptr; // CDO检查时使用传入句柄。
    if(!ASC||!ASC->IsOwnerActorAuthoritative()||!Pawn||Pawn->IsCoopEliminated()||!Pawn->GetDemoAttributes()||Pawn->GetDemoAttributes()->GetHealth()<=0
        ||!State||State->Phase!=EDemoPhase::Combat||!Definition||!ASC->HasMatchingGameplayTag(Definition->TalentTag))
    { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_ACTIVATE rejected authority/avatar/phase/definition/tag")); return false; }
    return Super::CanActivateAbility(Handle,ActorInfo,SourceTags,TargetTags,OptionalRelevantTags);
}
void UDemoTalentAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
    const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_CALL();
    Super::ActivateAbility(Handle,ActorInfo,ActivationInfo,TriggerEventData); // UE执行K2_ActivateAbility/FromEvent；结束权留给持续技能。
}

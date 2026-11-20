#include "AI/DemoEnemyTargeting.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Debug/DemoLog.h"

bool DemoEnemyTargeting::IsAliveParticipant(const ADemoCharacter* Player)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[TICK] %hs"), __FUNCTION__);
    // 登记失败/尚未完成或离场中的 Pawn 可能仍存在于 World；Actor 存活不能代替参战资格。
    const ADemoPlayerController* Controller = IsValid(Player) ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 当前拥有者只在本次判断借用。
    const UDemoAbilitySystemComponent* AbilitySystem = IsValid(Player) ? Player->GetDemoASC() : nullptr; // 校验当前 Avatar，拒绝重生期间仍绑定旧角色的 ASC。
    const UDemoAttributeSet* Attributes = IsValid(Player) ? Player->GetDemoAttributes() : nullptr; // 生命值是服务器当前数值，不使用缓存快照。
    const bool bEligible = IsValid(Player) && Player->HasAuthority() && Controller && Controller->GetPawn() == Player
        && Controller->IsCoopRegistered() && AbilitySystem && AbilitySystem->GetAvatarActor() == Player
        && Attributes && !Player->IsCoopEliminated() && Attributes->GetHealth() > 0.f; // 离场先标记阵亡，即使尚未销毁或扣至零也立即失去资格。
    if (!bEligible) UE_LOG(LogFPSDemo, VeryVerbose, TEXT("ENEMY_TARGET_REJECT player=%s authority/registration/avatar/life invalid"), *GetNameSafe(Player));
    return bEligible;
}

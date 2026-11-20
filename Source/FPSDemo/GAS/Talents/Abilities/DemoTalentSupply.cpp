// 职责：协同补给的数值/事件实现。使用：仅由天赋目录和GAS调用；新增技能使用新的派生文件，不向本文件追加分支。
#include "GAS/Talents/Abilities/DemoTalentSupply.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Debug/DemoLog.h"
UDemoTalentSupply::UDemoTalentSupply()
{
    DEMO_LOG_CALL(); 
    ListenFor(DemoTalentEvents::TeamKill); // 原生GAS事件触发，不在奖励系统枚举天赋。
}
FText UDemoTalentSupply::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 与执行使用相同配置。
    return FText::FromString(FString::Printf(TEXT("每%d击杀补%d发"),Settings->SupplyKills,Rank));
}

#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
bool UDemoTalentSupply::OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetAvatarActorFromActorInfo()); // GAS当前Avatar，死亡门禁已由基类处理。
    ADemoPlayerState* PS=Pawn?Pawn->GetPlayerState<ADemoPlayerState>():nullptr; // 队伍击杀账本在发事件前提交。
    const int32 Interval=GetDefault<UDemoGambleSettings>()->SupplyKills; // 旧INI仍是每若干击杀补弹，非法零不取模。
    if(!PS||Interval<1||PS->GetProgressComponent()->GetSnapshot().Kills%Interval!=0) return false;
    if(ADemoWeaponBase* Weapon=Pawn->GetWeaponComponent()->GetActiveWeapon()) Weapon->ModifyAmmo(FMath::Min(Rank,Weapon->GetCapacity())); // 极高等级先按容量截断。
    return true; // 仅接受的事件才允许基类播放配置GC。
}

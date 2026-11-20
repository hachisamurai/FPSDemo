// 职责：双重弹头的独立数值策略。使用：由Definition按等级聚合快照，武器消费快照；无GA执行或World副作用。
#include "GAS/Talents/Modifiers/DemoTalentTwin.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Debug/DemoLog.h"
UDemoTalentTwin::UDemoTalentTwin()
{
    DEMO_LOG_CALL(); 
}
void UDemoTalentTwin::BuildModifiers_Implementation(int32 Rank,FDemoTalentModifiers& Modifiers) const
{
    DEMO_LOG_TICK();
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 旧平衡INI保持兼容；新参数归策略蓝图配置。
    const float Scale=DemoGamble::Strength(Rank); // 等级只增强收益，不增加实体/事件递归次数。
    Modifiers.Projectiles=2; Modifiers.PelletDamage*=Settings->TwinDamageRatio*Scale; Modifiers.Spread=Settings->TwinSpreadDegrees;
}
FText UDemoTalentTwin::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 与执行使用相同配置。
    const float Scale=DemoGamble::Strength(Rank); // 服务投影冻结描述，UI不重算。
    return FText::FromString(FString::Printf(TEXT("弹丸×2，每颗伤害%.2f%%，成本不变"),Settings->TwinDamageRatio*Scale*100));
}

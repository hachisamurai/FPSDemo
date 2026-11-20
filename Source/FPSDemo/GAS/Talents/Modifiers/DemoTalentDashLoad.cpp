// 职责：冲刺装填的独立数值策略。使用：由Definition按等级聚合快照，武器消费快照；无GA执行或World副作用。
#include "GAS/Talents/Modifiers/DemoTalentDashLoad.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Debug/DemoLog.h"
UDemoTalentDashLoad::UDemoTalentDashLoad()
{
    DEMO_LOG_CALL(); 
}
void UDemoTalentDashLoad::BuildModifiers_Implementation(int32 Rank,FDemoTalentModifiers& Modifiers) const
{
    DEMO_LOG_TICK();
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 旧平衡INI保持兼容；新参数归策略蓝图配置。
    Modifiers.DashFraction=DemoGamble::Fraction(Settings->DashLoadFraction,Rank);
}
FText UDemoTalentDashLoad::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 与执行使用相同配置。
    return FText::FromString(FString::Printf(TEXT("冲刺结束装填%.2f%%，消耗备用"),DemoGamble::Fraction(Settings->DashLoadFraction,Rank)*100));
}

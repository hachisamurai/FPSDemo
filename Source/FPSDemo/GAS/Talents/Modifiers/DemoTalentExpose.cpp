// 职责：破绽追击的独立数值策略。使用：由Definition按等级聚合快照，武器消费快照；无GA执行或World副作用。
#include "GAS/Talents/Modifiers/DemoTalentExpose.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Debug/DemoLog.h"
UDemoTalentExpose::UDemoTalentExpose()
{
    DEMO_LOG_CALL(); 
}
void UDemoTalentExpose::BuildModifiers_Implementation(int32 Rank,FDemoTalentModifiers& Modifiers) const
{
    DEMO_LOG_TICK();
    const float Scale=DemoGamble::Strength(Rank); // 等级只增强收益，不增加实体/事件递归次数。
    Modifiers.ExecuteThreshold=HealthThreshold; Modifiers.ExecuteMultiplier=1.f+DamageBonus*Scale;
}
FText UDemoTalentExpose::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    const float Scale=DemoGamble::Strength(Rank); // 服务投影冻结描述，UI不重算。
    return FText::FromString(FString::Printf(TEXT("目标生命低于%.0f%%时直伤+%.1f%%"),HealthThreshold*100,DamageBonus*Scale*100));
}

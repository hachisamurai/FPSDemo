// 职责：精准回收的独立数值策略。使用：由Definition按等级聚合快照，武器消费快照；无GA执行或World副作用。
#include "GAS/Talents/Modifiers/DemoTalentPrecision.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Debug/DemoLog.h"
UDemoTalentPrecision::UDemoTalentPrecision()
{
    DEMO_LOG_CALL(); 
}
void UDemoTalentPrecision::BuildModifiers_Implementation(int32 Rank,FDemoTalentModifiers& Modifiers) const
{
    DEMO_LOG_TICK();
    Modifiers.PrecisionRounds=Rank;
}
FText UDemoTalentPrecision::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    // 弱点迁到 head 后同步玩家说明；沿用已有 bCore 弱点协议及每枪闩锁，不为胸部保留返弹入口。
    return FText::FromString(FString::Printf(TEXT("命中头部返%d发，每枪最多1次"),Rank));
}

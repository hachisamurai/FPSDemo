// 职责：超频扳机的独立数值策略。使用：由Definition按等级聚合快照，武器消费快照；无GA执行或World副作用。
#include "GAS/Talents/Modifiers/DemoTalentOverdrive.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "Debug/DemoLog.h"
#include "GAS/DemoTags.h"
#include "GameplayEffect.h"
UDemoTalentOverdrive::UDemoTalentOverdrive()
{
    DEMO_LOG_CALL(); 
}
void UDemoTalentOverdrive::ConfigureEffect_Implementation(int32 Rank,const FGameplayEffectSpecHandle& Spec) const
{
    DEMO_LOG_CALL();
    FDemoTalentModifiers Values; // 复用同一纯函数，保证展示/GE实际倍率一致。
    BuildModifiers(Rank,Values);
    if(Spec.IsValid()) Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,Values.FireRate);
}
void UDemoTalentOverdrive::BuildModifiers_Implementation(int32 Rank,FDemoTalentModifiers& Modifiers) const
{
    DEMO_LOG_TICK();
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 旧平衡INI保持兼容；新参数归策略蓝图配置。
    const float Scale=DemoGamble::Strength(Rank); // 等级只增强收益，不增加实体/事件递归次数。
    Modifiers.FireRate*=1.f+(Settings->OverdriveRate-1.f)*Scale;
}
FText UDemoTalentOverdrive::Describe_Implementation(int32 Rank) const
{
    DEMO_LOG_TICK();
    const auto* Settings=GetDefault<UDemoGambleSettings>(); // 与执行使用相同配置。
    const float Scale=DemoGamble::Strength(Rank); // 服务投影冻结描述，UI不重算。
    return FText::FromString(FString::Printf(TEXT("射速×%.2f，不加快换弹"),1.f+(Settings->OverdriveRate-1.f)*Scale));
}

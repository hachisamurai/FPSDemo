// 职责：数值策略的无副作用默认实现。用法：派生类只覆盖需要的通道，日志遵守Editor/包体类别上限。
#include "GAS/Talents/DemoTalentModifier.h"
#include "Debug/DemoLog.h"
void UDemoTalentModifier::BuildModifiers_Implementation(int32 Rank,FDemoTalentModifiers& Modifiers) const { DEMO_LOG_TICK(); }
FText UDemoTalentModifier::Describe_Implementation(int32 Rank) const { DEMO_LOG_TICK(); return FText::GetEmpty(); }
void UDemoTalentModifier::ConfigureEffect_Implementation(int32 Rank,const FGameplayEffectSpecHandle& Spec) const { DEMO_LOG_CALL(); }

// 职责：协同补给独立天赋GA。使用：由Definition引用、PS组件增量授予，可创建蓝图派生配置GE/GC或覆盖事件，禁止CDO保存运行状态。
#pragma once
#include "GAS/Talents/DemoTalentEventAbility.h"
#include "DemoTalentSupply.generated.h"
/** 协同补给；详细触发与边界见Documentation/天赋系统与扩展.md。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoTalentSupply : public UDemoTalentEventAbility
{
    GENERATED_BODY()
public:
    /** 初始化本实现的默认参数；内容身份由独立Definition资产配置。 */
    UDemoTalentSupply();
    /** Rank为权威报价/拥有等级；生成与本类实际公式一致的说明。 */
    virtual FText Describe_Implementation(int32 Rank) const override;
    /** Event为本次同步权威击杀快照，Rank为Spec等级；不持有裸事件目标。 */
    virtual bool OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank) override;
};

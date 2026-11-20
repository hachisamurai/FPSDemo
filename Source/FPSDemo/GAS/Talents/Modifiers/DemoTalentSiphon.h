// 职责：战斗虹吸独立无状态修正策略。使用：Definition引用此类或蓝图派生；不授予GA，CDO禁止保存玩家运行状态。
#pragma once
#include "GAS/Talents/DemoTalentModifier.h"
#include "DemoTalentSiphon.generated.h"
/** 战斗虹吸；详细触发与边界见Documentation/天赋系统与扩展.md。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoTalentSiphon : public UDemoTalentModifier
{
    GENERATED_BODY()
public:
    /** 初始化本实现的默认参数；内容身份由独立Definition资产配置。 */
    UDemoTalentSiphon();
    /** Rank为正等级，Modifiers为同步聚合输出，不得修改共享CDO。 */
    virtual void BuildModifiers_Implementation(int32 Rank,FDemoTalentModifiers& Modifiers) const override;
    /** Rank为权威报价/拥有等级；生成与本类实际公式一致的说明。 */
    virtual FText Describe_Implementation(int32 Rank) const override;
};

// 职责：破绽追击独立无状态修正策略。使用：Definition引用此类或蓝图派生；不授予GA，CDO禁止保存玩家运行状态。
#pragma once
#include "GAS/Talents/DemoTalentModifier.h"
#include "DemoTalentExpose.generated.h"
/** 破绽追击；详细触发与边界见Documentation/天赋系统与扩展.md。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoTalentExpose : public UDemoTalentModifier
{
    GENERATED_BODY()
public:
    /** 初始化本实现的默认参数；内容身份由独立Definition资产配置。 */
    UDemoTalentExpose();
    /** Rank为正等级，Modifiers为同步聚合输出，不得修改共享CDO。 */
    virtual void BuildModifiers_Implementation(int32 Rank,FDemoTalentModifiers& Modifiers) const override;
    /** Rank为权威报价/拥有等级；生成与本类实际公式一致的说明。 */
    virtual FText Describe_Implementation(int32 Rank) const override;
    /** 严格低于的敌人HP比例，0..1。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") float HealthThreshold=.3f;
    /** 1级直击额外比例，等级系数放大。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") float DamageBonus=.25f;
};

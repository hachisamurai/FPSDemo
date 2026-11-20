// 职责：冰封碎裂独立天赋GA。使用：由Definition引用、PS组件增量授予，可创建蓝图派生配置GE/GC或覆盖事件，禁止CDO保存运行状态。
#pragma once
#include "GAS/Talents/DemoTalentEventAbility.h"
#include "DemoTalentShatter.generated.h"
/** 冰封碎裂；详细触发与边界见Documentation/天赋系统与扩展.md。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoTalentShatter : public UDemoTalentEventAbility
{
    GENERATED_BODY()
public:
    /** 初始化本实现的默认参数；内容身份由独立Definition资产配置。 */
    UDemoTalentShatter();
    /** Rank为权威报价/拥有等级；生成与本类实际公式一致的说明。 */
    virtual FText Describe_Implementation(int32 Rank) const override;
    /** Event为本次同步权威击杀快照，Rank为Spec等级；不持有裸事件目标。 */
    virtual bool OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank) override;
    /** 冰爆半径cm，墙壁阻挡。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") float Radius=500.f;
    /** 每次最多伤害目标，1..8。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") int32 MaxTargets=8;
    /** 1级范围伤害HP。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") float Damage=40.f;
    /** 减速比例，0..0.9，不附加冰霜层。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") float Slow=.3f;
    /** 减速持续游戏秒。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") float Seconds=3.f;
    /** 可配置Instant Health GE，使用Demo.Magnitude。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") TSubclassOf<UGameplayEffect> DamageEffect;
    /** 可配置HasDuration移动GE，使用Demo.Magnitude。 */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Talent|Balance") TSubclassOf<UGameplayEffect> SlowEffect;
};

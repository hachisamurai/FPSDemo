// 职责：银币抽取与天赋/遗物效果配置。使用：DefaultGame.ini提供权威数值；金币回收及遗物价格独立配置于DemoCasinoSettings。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoGambleSettings.generated.h"

/** DefaultGame.ini集中配置；服务器只读CDO，UI使用权威投影，不信客户端概率。 */
UCLASS(Config=Game,DefaultConfig)
class FPSDEMO_API UDemoGambleSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) int32 AttributeCost=30; // 单次属性下注银币，1..1000000。
    UPROPERTY(Config) int32 TalentCost=60; // 单次天赋下注银币。
    UPROPERTY(Config) float AttributeChance=.7f; // 0..1，成功才增加指定属性。
    UPROPERTY(Config) float TalentChance=.35f; // 0..1，不含额外保底。
    // 银币抽取不设节点/单局次数预算；只按余额、有效奖池与等级存储范围验证。
    UPROPERTY(Config) float RankGrowth=.25f; // 每次等级翻倍增加的效果系数，(0,1]；无槽位/抽取/玩法等级上限。
    UPROPERTY(Config) float OverdriveRate=2.f; // 1级超频射速乘数1..4；不影响半自动触发规则或换弹。
    UPROPERTY(Config) float TwinDamageRatio=.7f; // 双倍弹丸每颗含升级伤害乘数(0,1]，整次成本不变。
    UPROPERTY(Config) float TwinSpreadDegrees=.35f; // 双弹最小锥半角0..5度，与原枪/开镜散布取较大值。
    UPROPERTY(Config) float RicochetRatio=.5f; // 1级弹射后段相对首段基伤倍率(0,1]，不继承首目标部位倍率。
    UPROPERTY(Config) float RicochetRadius=800.f; // 可见候选目标搜索半径cm，1..3000，最多一次转向。
    UPROPERTY(Config) float SiphonRatio=.03f; // 1级真实直接扣血转生命的比例(0,0.2]，DOT/爆炸不触发。
    UPROPERTY(Config) float SiphonPerSecond=8.f; // 每个1秒World时间窗口的最大治疗HP，1..100。
    UPROPERTY(Config) float DashLoadFraction=.25f; // 冲刺窗口结束装填容量比例(0,1]，受空位/备用约束。
    UPROPERTY(Config) float DamageGain=5.f; // 属性中奖直接伤害加值HP。
    UPROPERTY(Config) float HealthGain=20.f; // 属性中奖生命上限及等量恢复HP。
    UPROPERTY(Config) float MagazineGain=2.f; // 属性中奖容量加值发数，必须正整数。
    UPROPERTY(Config) int32 SupplyKills=5; // 队伍累计击杀每N次补1发，N>0。
    UPROPERTY(Config) float EvadeHeal=3.f; // 战术医疗额外HP。
    UPROPERTY(Config) float ExplosionBonus=.2f; // 灼烧余烬额外爆炸比例。
    UPROPERTY(Config) float FrozenBonus=.15f; // 冰霜猎手额外直伤比例。
    UPROPERTY(Config) float PierceBonus=.1f; // 后段比例额外百分点，最终钳至1。
    UPROPERTY(Config) float CapacityBonus=.2f; // 超载弹匣额外容量比例。
    UPROPERTY(Config) float ReloadPenalty=.15f; // 超载弹匣额外换弹时长比例。
    UPROPERTY(Config) float BurnExtraSeconds=1.f; // 催化器额外持续秒。
    UPROPERTY(Config) float ExplosionPenalty=.1f; // 催化器爆炸损失比例，0..0.9。
    UPROPERTY(Config) float DashCooldownReduction=.15f; // 推进器冷却减少比例，0..0.9。
    UPROPERTY(Config) float DashDistanceReduction=.1f; // 推进器距离减少比例，0..0.9。
    UPROPERTY(Config) float RescueSeconds=1.f; // 濒死后保护时长秒，0.1..10。
    /** 所有配置在服务执行前验证；失败禁止交易，不静默免费或修改概率。 */
    bool Validate() const;
};

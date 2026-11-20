// 职责：B04～B09可调数值；Project Settings/Game统一配置，服务器预检，客户端仅显示，不包含玩家存档。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DemoExpandedBuildSettings.generated.h"
UCLASS(Config=Game,DefaultConfig,meta=(DisplayName="Demo Expanded Builds"))
class FPSDEMO_API UDemoExpandedBuildSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** 出发前严格检查有限值、窗口顺序与预算，失败保持备战。 */
    bool Validate() const;
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") float RhythmWindowStart=0.35f; // 装填进度窗口起点0..1。
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") float RhythmWindowEnd=0.65f; // 装填进度窗口终点0..1。
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") float RhythmToleranceSeconds=0.08f; // 权威判窗有界延迟容差秒。
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") int32 RhythmCredits=3; // 成功装填供弹枪数。
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") float RhythmCreditSeconds=10.f; // 供弹凭据有效秒（兼容慢射速狙击打满3枪）。
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") int32 RhythmStableHits=3; // D分支本弹匣有效命中次数上限（运行时按武器实际容量钳制）。
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") int32 RhythmStableCredits=2; // D分支自然装填供弹枪数。
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") float RhythmCarrySeconds=2.f; // C成功冲刺允许携带窗口秒。
    UPROPERTY(Config,EditAnywhere,Category="Rhythm") float RhythmBoltActionBonus=0.2f; // B04-B拉栓武器凭据改为本次直击增伤比例，避免退镜强制盲射。
    UPROPERTY(Config,EditAnywhere,Category="Crossfire") float CrossfireWindow=3.f; // 破势窗口秒。
    UPROPERTY(Config,EditAnywhere,Category="Crossfire") float CrossfireRatio=0.6f; // 派生近战/枪击基数倍率。
    UPROPERTY(Config,EditAnywhere,Category="Crossfire") float CrossfireDamageCap=110.f; // 单次冲击伤害HP上限（避免散弹/狙击高单发被低上限截断）。
    UPROPERTY(Config,EditAnywhere,Category="Crossfire") float CrossfireRadius=260.f; // A分支溅射半径cm。
    UPROPERTY(Config,EditAnywhere,Category="Crossfire") int32 CrossfireTargets=4; // 溅射目标上限。
    UPROPERTY(Config,EditAnywhere,Category="Crossfire") int32 CrossfireRounds=1; // C返入原枪的备弹发数。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorRadius=350.f; // 锚点作用半径cm。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorDeployRange=1200.f; // 部署最大距离cm。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") int32 AnchorChargeHits=4; // 完整充能直击枪数。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorChargeSeconds=8.f; // 固定充能窗口秒。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorCooldown=8.f; // 部署冷却秒。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorLifetime=15.f; // 单锚点最长存活秒。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorDamage=55.f; // 脉冲单目标HP。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") int32 AnchorTargets=6; // 脉冲目标上限。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorConeCos=0.5f; // 兼容保留的基础定向余弦。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorDirectedRange=1000.f; // B06-B固守信标中远程定向冲击射程cm。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorDirectedConeCos=0.85f; // B06-B中远程窄锥定向冲击余弦。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") int32 AnchorPelletDoubleThreshold=5; // 多弹丸单枪命中达到此值直接充2格。
    UPROPERTY(Config,EditAnywhere,Category="Anchor") float AnchorChainSeconds=3.f; // C单次迁移token秒。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterWindow=3.f; // 反击窗口秒（兼容拉栓/双管换弹）。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterCooldown=5.f; // 受伤开窗冷却秒。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterLossRatio=0.5f; // 实际损血预算倍率，上限0.5。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterBudgetCap=25.f; // 单次治疗预算HP上限。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterMeleeRatio=0.75f; // 实际近战HP可兑现倍率。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterHeavyRatio=1.f; // A近战HP可兑现倍率。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterWeakRatio=0.35f; // B弱点直击HP可兑现倍率。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterDashGrace=1.f; // C成功位移后窗口秒。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterShareRadius=700.f; // D队友搜索半径cm。
    UPROPERTY(Config,EditAnywhere,Category="Counter") float CounterShareRatio=0.4f; // D固定预算分给队友的比例。
    // B08 抵近爆膛（近距多弹丸高爆发核心，全武器可触发，散弹枪一枪全中瞬间满压）。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseRangeCm=800.f; // 近距判定上限cm，与散弹无衰减区对齐。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") int32 ClosePressureMax=8; // 满膛压层数，触发爆膛冲击波。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float ClosePressureDecaySeconds=4.f; // 膛压保持秒。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") int32 CloseSingleShotStacks=2; // 单发武器每次近距直击积攒层数。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseBurstRatio=0.55f; // 冲击伤害占触发枪整枪实伤比例。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseBurstMinDamage=25.f; // 残血击杀保底冲击伤害HP。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseBurstCap=90.f; // 基础爆膛单目标伤害上限HP。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseBurstCapB=120.f; // B08-B阔口清阵单目标伤害上限HP。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseBurstRadius=300.f; // 基础爆膛范围半径cm。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseBurstRadiusB=450.f; // B08-B宽幅扇面范围半径cm。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseBurstConeCosB=0.35f; // B08-B宽幅扇面余弦。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") int32 CloseBurstTargets=5; // 基础范围目标上限。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") int32 CloseBurstTargetsB=7; // B08-B范围目标上限。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseBossMultiplier=1.25f; // Boss免击退，承受额外集中冲击倍率。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseKnockbackDistance=200.f; // 小怪短距震退距离cm。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseKnockbackSeconds=0.2f; // 震退时长秒。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseReloadCooldown=3.f; // B08-A双膛续压回弹冷却秒。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseTightSpreadScale=0.65f; // B08-A下一枪散布倍率。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseTightSpreadSeconds=2.f; // B08-A散布收束有效秒。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseKillMoveBonus=0.15f; // B08-B冲击击杀移速加成。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseKillMoveSeconds=1.5f; // B08-B移速持续秒。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseGuardHeal=8.f; // B08-C贴身护幕回复HP。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseGuardCooldown=5.f; // B08-C回复冷却秒。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseTremorBonus=0.15f; // B08-D震颤公开易伤增伤比例。
    UPROPERTY(Config,EditAnywhere,Category="CloseQuarters") float CloseTremorSeconds=3.f; // B08-D震颤易伤持续秒。
    // B09 蓄势贯杀（停火/开镜蓄势+过量伤害转化核心，全武器可触发，狙击枪拉栓天然满蓄）。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanChargeSeconds=1.2f; // 常规停火蓄满秒数。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanScopedRate=2.f; // 开镜期间蓄势速度倍率。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanBaseBonus=0.3f; // 满蓄基础直击增伤比例。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanBonusExtra=0.25f; // 远距或弱点额外直击增伤比例。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanLongRangeCm=1200.f; // 远距判定距离cm。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanOverkillRatio=0.75f; // 满蓄击杀过量伤害转化率。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanOverkillMin=25.f; // 溢伤追击保底伤害HP。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanOverkillCap=140.f; // 溢伤追击上限HP。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanOverkillRadius=600.f; // 基础单目标追击半径cm。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanOverkillRadiusA=450.f; // B09-A溢伤连爆范围半径cm。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") int32 MarksmanOverkillTargetsA=4; // B09-A连爆目标上限。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanKillRefundFocusA=0.5f; // B09-A满蓄击杀返还蓄势比例0..1。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") int32 MarksmanRiftMaxStacksB=3; // B09-B贯心裂痕上限层数。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanRiftDurationB=6.f; // B09-B裂痕持续秒。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanRiftRatioPerStackB=0.15f; // B09-B每层附加非元素穿透伤害比例。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanRiftDamageCapB=130.f; // B09-B单次裂痕引爆上限HP。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanRiftSpeedBonusB=0.25f; // B09-B满蓄弱点命中后下次蓄势提速比例。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanScopeMoveMitigationC=0.5f; // B09-C蓄势>=50%抵消开镜移速惩罚比例0..1。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanWeakMoveBonusC=0.15f; // B09-C满蓄弱点移速加成。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanWeakMoveSecondsC=2.f; // B09-C移速持续秒。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanMarkBonusD=0.15f; // B09-D公开猎杀标定增伤比例。
    UPROPERTY(Config,EditAnywhere,Category="Marksman") float MarksmanMarkSecondsD=4.f; // B09-D猎杀标定持续秒。
};

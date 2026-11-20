// 职责：天赋纯数值聚合快照。用法：定义组合无状态Modifier策略，武器冻结结果；不要求GA存在，不保存UObject或经济权限。
#pragma once
#include "CoreMinimal.h"
#include "DemoTalentModifiers.generated.h"
/** 可在蓝图策略中扩展已有战斗通道；追加通道需同步消费端验证。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoTalentModifiers
{
    GENERATED_BODY()
    /** 射速GE乘数，默认不改变射速。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float FireRate=1.f;
    /** 弹体数量倍率，只允许1..2以限制分配。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") int32 Projectiles=1;
    /** 每弹伤害倍率，发射冻结。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float PelletDamage=1.f;
    /** 附加散布角度，单位度。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float Spread=0.f;
    /** 后段弹射伤害比例，0禁用。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float RicochetRatio=0.f;
    /** 弹射索敌半径cm。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float RicochetRadius=0.f;
    /** 实际直伤回血比例。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float SiphonRatio=0.f;
    /** 每Pawn每秒共享回血HP预算。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float SiphonBudget=0.f;
    /** 每枪头部弱点命中返弹数，按容量截断；兼容原 Core 协议，旧胸部 core 不再触发。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") int32 PrecisionRounds=0;
    /** 火焰爆炸伤害倍率。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float ExplosionMultiplier=1.f;
    /** 命中冻结敌人直伤倍率。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float FrozenMultiplier=1.f;
    /** 穿透基础后段比例的加值。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float PierceBonus=0.f;
    /** 穿透剩余衰减除数，至少1。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float PierceScale=1.f;
    /** 成功冲刺结束从备用转移的弹匣比例。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float DashFraction=0.f;
    /** Boss成功躲避额外回血HP。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float EvadeHeal=0.f;
    /** 破绽直伤触发的命中前HP比例。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float ExecuteThreshold=0.f;
    /** 低血量目标直接伤害倍率。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Talent") float ExecuteMultiplier=1.f;
};

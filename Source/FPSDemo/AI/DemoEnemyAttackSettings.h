#pragma once
#include "CoreMinimal.h"
#include "DemoEnemyAttackSettings.generated.h"

/** 怪物模板内的攻击配置；生成时复制到敌人，不在 Tick 中反复查询数据表。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoEnemyAttackSettings
{
    GENERATED_BODY()
    // 普通怪原接触攻击间隔，秒，[0.2,60]；不替换飞行物的独立时钟。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0.2", ClampMax="60")) float MeleeInterval = 1.2f;
    // Boss 原锁定地面范围攻击间隔，秒，[1,60]，保留0.9秒预警。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="1", ClampMax="60")) float AreaInterval = 2.6f;
    // 普通怪/Boss 均使用的飞行物发射间隔，秒，[0.2,60]。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0.2", ClampMax="60")) float ProjectileInterval = 2.4f;
    // Boss 全图技能两次开始之间的最小间隔，秒，[3,120]，必须长于固定2秒前摇。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="3", ClampMax="120")) float GlobalInterval = 10.f;
    // 非追踪直线弹速度 cm/s，[200,5000]；高速碰撞使用扫掠，寿命6秒。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="200", ClampMax="5000")) float ProjectileSpeed = 1000.f;
    // 发射最大三维距离 cm，[200,4500]；视线阻挡时不发射，飞行中仍与墙碰撞。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="200", ClampMax="4500")) float ProjectileRange = 3500.f;
    // 全图命中后行走速度倍率，[0.1,1]；0.5表示减速50%，不降低冲刺发动速度。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0.1", ClampMax="1")) float SlowMultiplier = 0.5f;
    // 全图命中后减速持续秒数，[0.1,15]，重复命中刷新而不叠乘。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0.1", ClampMax="15")) float SlowDuration = 3.f;
    /** 同步检查全部数值有限且在合法区间；供数据表与直接生成入口共同使用。 */
    bool IsValid() const;
};

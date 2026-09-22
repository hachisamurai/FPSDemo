#pragma once

#include "CoreMinimal.h"
#include "DemoAmmoEffectSnapshot.generated.h"

class UDemoAmmoCatalog;

/** 一次开火复制的纯数值弹药配置；不引用可变Catalog，持续状态再按首层GE句柄保留副本。 */
USTRUCT()
struct FPSDEMO_API FDemoAmmoEffectSnapshot
{
    GENERATED_BODY()

    /** Config为开火时借用的目录；目录非法/为空返回Validate失败的快照，不保留资产引用。 */
    static FDemoAmmoEffectSnapshot FromCatalog(const UDemoAmmoCatalog* Config);
    /** 在开火/状态施加入口检查有限数值和GAS层数上限；默认未初始化快照不能参与结算。 */
    bool Validate() const;

    UPROPERTY() float BurnDuration = 4.f; // 首层固定的整组灼烧寿命，秒；后续层按此值刷新。
    UPROPERTY() float BurnPeriod = 1.f; // 首层固定的DOT周期，秒；叠层不重置当前周期。
    UPROPERTY() float BurnDamagePerStack = 3.f; // 每层每周期HP，GAS按实际层数累加。
    UPROPERTY() int32 BurnThreshold = 5; // 1..32；达到即消费整组并执行一次爆炸。
    UPROPERTY() float ExplosionDamage = 40.f; // 满层单体爆炸HP，不作为子弹直击重复加元素。
    UPROPERTY() float ChillDuration = 4.f; // 首层固定的整组冰霜寿命，秒。
    UPROPERTY() float SlowPerStack = .1f; // 每层减速比例，0..0.9；独立移动GE使用。
    UPROPERTY() float MaxSlow = .4f; // 整组最大减速比例，0..0.9。
    UPROPERTY() int32 FreezeThreshold = 5; // 1..32；达到后消费冰霜并施加冻结。
    UPROPERTY() float FreezeDuration = 2.f; // 满层冻结秒数，必须大于0。
    UPROPERTY() float PostThawImmunityDuration = 6.f; // 解冻后免疫秒数，允许0。
    UPROPERTY() float PiercingDamageMultiplier = 1.25f; // 穿透弹首段基础伤害倍率，不含部位倍率。
    UPROPERTY() float SecondaryDamageRatio = .5f; // 第二目标相对首段衰减后基础伤害的比例，0..1。

private:
    UPROPERTY() bool bInitialized = false; // 仅由成功的FromCatalog设置，拒绝把默认值误当合法资产快照。
};

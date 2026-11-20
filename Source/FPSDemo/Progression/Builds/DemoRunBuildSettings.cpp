// 职责：核心数值集中校验；使用：服务器开始新内容战斗时检查，失败不以零/负值开战。
#include "Progression/Builds/DemoRunBuildSettings.h"
#include "Debug/DemoLog.h"
bool UDemoRunBuildSettings::Validate() const
{
    DEMO_LOG_CALL();
    const float Positive[]={IceChainSeconds,FreezeSeconds,FreezeCooldown,BossMarkSeconds,ShatterCooldown,ShatterCooldownA,PushDistance,PushDistanceA,PushSeconds,BurstRadius,BurstRadiusB,BurstSlowSeconds,BurstHealCooldown,DashMinimumDistance,AssaultSeconds,AssaultSecondsA,AssaultMoveSeconds,PrecisionSeconds,PrecisionSecondsA,PrecisionSecondsB,PrecisionChargeSeconds,ExposeSeconds,SlowWeaponIntervalSeconds}; // 单位见声明；拒绝配置产生无期限状态。
    for(float Value:Positive) if(!FMath::IsFinite(Value)||Value<=0.f||Value>2000.f) return false;
    const float Ratios[]={BossBonus,BurstRatio,BurstRatioB,BurstSlow,BurstHeal,AssaultBonus,AssaultBonusA,AssaultBonusB,AssaultMoveBonus,AssaultSpreadScale,AssaultRecoilScale,PrecisionBonus,PrecisionBonusA,PrecisionBonusB,ExposeBonus}; // 非负有限数值。
    for(float Value:Ratios) if(!FMath::IsFinite(Value)||Value<0.f||Value>100.f) return false;
    return IceHits>=1&&IceHits<=10&&PrecisionHits>=1&&PrecisionHits<=10&&BurstTargets>=1&&BurstTargets<=6&&PushDistance<=600.f&&PushDistanceA<=600.f&&PushSeconds>=.05f&&PushSeconds<=2.f&&BurstRadius<=1500.f&&BurstRadiusB<=1500.f&&BurstSlow<=.9f&&AssaultSpreadScale>=.1f&&AssaultSpreadScale<=1.f&&AssaultRecoilScale>=.1f&&AssaultRecoilScale<=1.f&&SlowWeaponIntervalSeconds>=.2f&&SlowWeaponIntervalSeconds<=5.f&&IcePelletDoubleThreshold>=2&&IcePelletDoubleThreshold<=IcePelletFullThreshold&&IcePelletFullThreshold<=32;
}

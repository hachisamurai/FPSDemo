#include "AI/DemoEnemyAttackSettings.h"
#include "Debug/DemoLog.h"

bool FDemoEnemyAttackSettings::IsValid() const
{
    UE_LOG(LogFPSDemo, Log, TEXT("%hs projectile=%.2fs global=%.2fs"), __FUNCTION__, ProjectileInterval, GlobalInterval);
    // 每一项显式验证有限值；编辑器 Clamp 元数据不能拦截 JSON 或 C++ 传入的 NaN。
    return FMath::IsFinite(MeleeInterval) && MeleeInterval >= .2f && MeleeInterval <= 60.f
        && FMath::IsFinite(AreaInterval) && AreaInterval >= 1.f && AreaInterval <= 60.f
        && FMath::IsFinite(ProjectileInterval) && ProjectileInterval >= .2f && ProjectileInterval <= 60.f
        && FMath::IsFinite(GlobalInterval) && GlobalInterval >= 3.f && GlobalInterval <= 120.f
        && FMath::IsFinite(ProjectileSpeed) && ProjectileSpeed >= 200.f && ProjectileSpeed <= 5000.f
        && FMath::IsFinite(ProjectileRange) && ProjectileRange >= 200.f && ProjectileRange <= 4500.f
        && FMath::IsFinite(SlowMultiplier) && SlowMultiplier >= .1f && SlowMultiplier <= 1.f
        && FMath::IsFinite(SlowDuration) && SlowDuration >= .1f && SlowDuration <= 15.f;
}

#include "Weapons/Projectiles/DemoProjectileConfig.h"
#include "Debug/DemoLog.h"

bool FDemoProjectileConfig::Validate(FString& Error) const
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    Error.Reset();
    if (!FMath::IsFinite(InitialSpeed) || InitialSpeed < 100.f || InitialSpeed > 200000.f) Error = TEXT("Projectile.InitialSpeed must be 100..200000 cm/s");
    else if (!FMath::IsFinite(CollisionRadius) || CollisionRadius < .1f || CollisionRadius > 20.f) Error = TEXT("Projectile.CollisionRadius must be 0.1..20 cm");
    else if (!FMath::IsFinite(MaxLifeSeconds) || MaxLifeSeconds < .05f || MaxLifeSeconds > 60.f) Error = TEXT("Projectile.MaxLifeSeconds must be 0.05..60 seconds");
    else if (VisualTransform.ContainsNaN()) Error = TEXT("Projectile.VisualTransform must be finite");
    // 显式关闭亮条时，未启用的历史/自定义视觉数值不应阻止合法实体弹发射。
    else if (bEnableTracer && (!FMath::IsFinite(TracerWidth) || TracerWidth < .1f || TracerWidth > 50.f)) Error = TEXT("Projectile.TracerWidth must be 0.1..50 cm");
    else if (bEnableTracer && (!FMath::IsFinite(TracerLength) || TracerLength < 1.f || TracerLength > 1000.f)) Error = TEXT("Projectile.TracerLength must be 1..1000 cm");
    else if (bEnableTracer && (!FMath::IsFinite(TracerFadeSeconds) || TracerFadeSeconds < 0.f || TracerFadeSeconds > 1.f)) Error = TEXT("Projectile.TracerFadeSeconds must be 0..1 seconds");
    if (!Error.IsEmpty()) UE_LOG(LogFPSDemo, Error, TEXT("PROJECTILE_CONFIG_REJECT %s"), *Error);
    return Error.IsEmpty();
}

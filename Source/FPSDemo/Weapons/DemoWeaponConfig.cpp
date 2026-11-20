// 职责：武器CDO数值与资源的运行时预检；失败返回明确原因，拒绝异常时长、缩放和不兼容配置。
#include "Weapons/DemoWeaponConfig.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h" // 独立附件必须绑定当前机械骨架上真实存在的骨/插槽。

bool FDemoWeaponConfig::Validate(FString& Error) const
{
    UE_LOG(LogFPSDemo, Log, TEXT("%hs weapon=%s"), __FUNCTION__, *DisplayName.ToString());
    Error.Empty();
    // 两类网格至少提供一种；静态优先。零/负缩放仍拒绝，避免不可见或反转的武器。
    if ((!Mesh && !StaticMesh) || AttachSocket.IsNone() || AttachOffset.ContainsNaN() || AttachOffset.GetScale3D().GetMin() <= 0.f) Error = TEXT("Mesh/attach socket/transform invalid");
    else if (OpticMesh && (!Mesh || StaticMesh || OpticSocket.IsNone() || OpticOffset.ContainsNaN()
        || OpticOffset.GetScale3D().GetMin()<=0.f
        || (!Mesh->FindSocket(OpticSocket) && Mesh->GetRefSkeleton().FindBoneIndex(OpticSocket)==INDEX_NONE))) Error=TEXT("Optic requires a valid mechanical socket and transform");
    else if (!ProjectileClass || ProjectileClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) Error = TEXT("Projectile class missing/abstract/stale");
    else if (FireMode != EDemoFireMode::Automatic && FireMode != EDemoFireMode::SemiAutomatic) Error = TEXT("Fire mode invalid");
    else if (!FMath::IsFinite(RoundsPerMinute) || RoundsPerMinute < 1.f || RoundsPerMinute > 1200.f
        || !FMath::IsFinite(BaseDamage) || BaseDamage < 0.f || BaseDamage > 10000.f
        || !FMath::IsFinite(Range) || Range < 100.f || Range > 100000.f) Error = TEXT("RPM/damage/range invalid");
    else if (MagazineCapacity < 1 || MagazineCapacity > 1000 || InitialAmmo < 0 || InitialAmmo > MagazineCapacity
        || AmmoPerShot < 1 || AmmoPerShot > MagazineCapacity || InitialReserve < 0 || InitialReserve > 100000
        || !FMath::IsFinite(ReloadSeconds) || ReloadSeconds < .1f || ReloadSeconds > 15.f
        || !FMath::IsFinite(EmptyReloadSeconds) || (EmptyReloadSeconds!=0.f && (EmptyReloadSeconds<.1f || EmptyReloadSeconds>15.f))) Error = TEXT("Ammo/reload configuration invalid");
    // 即使编辑器限制被绕过，单颗任务时长和提交阶段也不能为NaN/端点或负数。
    else if (!FMath::IsFinite(EmptyShellReloadSeconds) || EmptyShellReloadSeconds < .1f || EmptyShellReloadSeconds > 15.f
        || !FMath::IsFinite(ShellInsertPhase) || ShellInsertPhase <= 0.f || ShellInsertPhase >= 1.f) Error = TEXT("Tube reload timing invalid");
    else if (PelletCount < 1 || PelletCount > 32 || !FMath::IsFinite(SpreadHalfAngle) || SpreadHalfAngle < 0.f || SpreadHalfAngle > 45.f
        || !FMath::IsFinite(ScopedSpreadHalfAngle) || ScopedSpreadHalfAngle < 0.f || ScopedSpreadHalfAngle > 45.f
        || !FMath::IsFinite(FalloffStart) || FalloffStart < 0.f || FalloffStart > Range
        || !FMath::IsFinite(MinimumDamageMultiplier) || MinimumDamageMultiplier < 0.f || MinimumDamageMultiplier > 1.f) Error = TEXT("Pellets/spread/falloff invalid");
    else if (!FMath::IsFinite(ScopeFOV) || !FMath::IsFinite(DeepScopeFOV) || DeepScopeFOV <= 0.f || DeepScopeFOV >= ScopeFOV || ScopeFOV >= 179.f
        || !FMath::IsFinite(AimMoveMultiplier) || AimMoveMultiplier < .1f || AimMoveMultiplier > 1.f
        || !FMath::IsFinite(RecoilPitch) || RecoilPitch < 0.f || RecoilPitch > 10.f) Error = TEXT("Scope/recoil invalid");
    if (!Error.IsEmpty()) UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_CONFIG_REJECTED %s"), *Error);
    return Error.IsEmpty();
}

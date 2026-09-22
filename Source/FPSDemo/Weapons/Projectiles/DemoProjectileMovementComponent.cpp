#include "Weapons/Projectiles/DemoProjectileMovementComponent.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Debug/DemoLog.h"

void UDemoProjectileMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    DEMO_LOG_TICK();
    ADemoProjectileBase* Projectile = Cast<ADemoProjectileBase>(GetOwner()); // 本组件Owner，不能在GE之后假定仍有效。
    if (!IsValid(Projectile) || !Projectile->CanSimulate())
    {
        if (IsValid(Projectile) && !Projectile->IsPrepared()) Projectile->CancelProjectile();
        return;
    }
    const float Speed = Velocity.Size(); // 恒速cm/s；外部错误清零速度则结束，避免悬空弹永久存在。
    const float Remaining = Projectile->GetRemainingDistance(); // 本帧开始剩余厘米。
    if (!FMath::IsFinite(Speed) || Speed <= UE_SMALL_NUMBER || Remaining <= UE_KINDA_SMALL_NUMBER)
    {
        Projectile->CancelProjectile();
        return;
    }
    const float FlightSeconds = FMath::Min(DeltaTime, Remaining / Speed); // 在执行碰撞前限制路程，不允许超射程一帧伤害。
    Super::TickComponent(FlightSeconds, TickType, ThisTickFunction);
    if (IsValid(Projectile))
    {
        Projectile->RecordTravel(Projectile->GetActorLocation());
        if (!Projectile->CanSimulate() || Projectile->GetRemainingDistance() <= UE_KINDA_SMALL_NUMBER) Projectile->CancelProjectile();
    }
}

UProjectileMovementComponent::EHandleBlockingHitResult UDemoProjectileMovementComponent::HandleBlockingHit(
    const FHitResult& Hit, float TimeTick, const FVector& MoveDelta, float& SubTickTimeRemaining)
{
    DEMO_LOG_CALL();
    ADemoProjectileBase* Projectile = Cast<ADemoProjectileBase>(GetOwner()); // 只从真实运动Owner解析，不绑定OnHit第二次伤害入口。
    if (!IsValid(Projectile) || !Projectile->CanSimulate()) return EHandleBlockingHitResult::Abort;
    Projectile->RecordTravel(Projectile->GetActorLocation());
    if (Projectile->ResolveImpact(Hit) && IsValid(Projectile) && Projectile->CanSimulate() && UpdatedComponent)
    {
        SubTickTimeRemaining = TimeTick * (1.f - Hit.Time); // 继续同一帧剩余时间，不穿越敌后墙体。
        return EHandleBlockingHitResult::AdvanceNextSubstep;
    }
    SubTickTimeRemaining = 0.f;
    return EHandleBlockingHitResult::Abort;
}

// 职责：沿原首敌索敌规则弹射，额外冻结表面接触/出射方向并拆分纯表现亮条；不更改弹速、倍率、半径、次数或碰撞。
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/Projectiles/DemoProjectileTracer.h"
#include "Weapons/Projectiles/DemoProjectileMovementComponent.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"

bool ADemoProjectileBase::TryRicochet(const FHitResult& Hit)
{
    DEMO_LOG_CALL();
    if(bRicochetUsed||!ShotContext||!ShotContext->IsAttackValid()||ShotContext->GetRicochetRatio()<=0.f)
    { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_RICOCHET rejected used/context/phase/ratio")); return false; }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoRicochet),false,this); // 使用实体弹同一碰撞通道，搜索绝不施伤。
    Query.AddIgnoredActor(ShotContext->GetSourcePawn());
    for(const TWeakObjectPtr<AActor>& HitActor:HitActors) // 单弹已打过的整只怪物全部排除，避免回弹自己。
        if(HitActor.IsValid()) Query.AddIgnoredActor(HitActor.Get());
    float BestDistance=FMath::Square(ShotContext->GetRicochetRadius()); // 以当前真实接触点为圆心，单位cm²。
    FVector BestDirection=FVector::ZeroVector; // 只有可见候选才写入，不缓存目标或后续追踪。
    for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It) // 权威World局部遍历，最多在每颗首敌实际受伤后一次。
    {
        ADemoEnemy* Enemy=*It; // World拥有；本同步查询结束不持有。
        if(!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||HitActors.Contains(Enemy)) continue;
        const USkeletalMeshComponent* Mesh=Enemy->FindComponentByClass<USkeletalMeshComponent>(); // 使用实际身体中心而非脚底Actor原点。
        if(!Mesh) continue;
        const FVector Aim=Mesh->Bounds.Origin; // 当前姿势快照，飞行中敌人仍可躲开。
        const FVector Offset=Aim-GetActorLocation(); // 从真实接触点继续飞行，无跳移。
        const float Distance=Offset.SizeSquared(); // 最近可见敌人优先。
        if(Distance<=1.f||Distance>BestDistance) continue;
        FHitResult Sight; // 阻挡必须就是候选敌人的可命中网格，墙和其他怪均可遮挡。
        if(!GetWorld()->LineTraceSingleByChannel(Sight,GetActorLocation(),Aim,ECC_GameTraceChannel3,Query)||Sight.GetActor()!=Enemy) continue;
        BestDistance=Distance; BestDirection=Offset.GetSafeNormal();
    }
    if(BestDirection.IsNearlyZero()) { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_RICOCHET no visible candidate")); return false; }
    bRicochetUsed=true;
    for(const TWeakObjectPtr<AActor>& HitActor:HitActors) // 忽略首敌但不忽略墙；后续实际扫掠决定是否命中。
        if(HitActor.IsValid()) Collision->IgnoreActorWhenMoving(HitActor.Get(),true);
    Movement->Velocity=BestDirection*Movement->Velocity.Size(); // 保持弹速，射程仍累计折线总长。
    RicochetPoint=Hit.ImpactPoint; RicochetDirection=BestDirection; // 真实表面与成功转向事实，后续所有表现包重复携带。
    if (IsValid(Tracer))
    {
        Tracer->UpdateSegment(Launch.Origin,RicochetPoint); // 接触已关闭FP桥，冻结真实直线入射至表面；近枪桥重建的空条也补齐，不继承偏移或画跨弯线。
        Tracer->Finish(Config.IncomingTracerFadeOnRicochet); Tracer=nullptr;
    }
    CosmeticTravelDistance=0.f; LastCosmeticPosition=RicochetPoint;
    CreateLocalTracer(RicochetPoint); // 新Origin严格为接触点，后续长度不可能跨回入射线。
    ADemoProjectileTracer::SpawnContact(GetWorld(),Config,VisualStyle,RicochetPoint,BestDirection,true);
    PublishNetworkTravel(true); // 零长度也发送成功事实；后续飞行/终包携带相同接触点以补丢首包。
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_RICOCHET redirected shot=%s pellet=%d"),*ShotContext->GetShotId().ToString(),Launch.PelletIndex);
    return true;
}

void ADemoProjectileBase::UpdatePiercingExit(const FVector& Position)
{
    DEMO_LOG_TICK();
    if (!HasAuthority() || bPiercingExit || bRicochetUsed || !PiercingComponent.IsValid() || Position.ContainsNaN()) return;
    if (FVector::DotProduct(Position-PiercingEntry,PiercingDirection)<=Config.CollisionRadius*2.f) return;
    FHitResult Exit; // 仅对已命中首敌组件从已到达球心向入射点反查实际出口，不寻找下一目标或改变伤害判定。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoPiercingExitVisual),false); // 同步GameThread借用组件，客户端不执行新碰撞。
    if (!PiercingComponent->LineTraceComponent(Exit,Position,PiercingEntry,Query) || Exit.bStartPenetrating
        || !Exit.bBlockingHit || FVector::DotProduct(Exit.ImpactPoint-PiercingEntry,PiercingDirection)<=Config.CollisionRadius)
        return; // 尚未穿出/骨骼物理面未可用时不伪造出射点，下一实际运动段重试。
    bPiercingExit=true; PiercingExitPoint=Exit.ImpactPoint;
    ADemoProjectileTracer::SpawnContact(GetWorld(),Config,VisualStyle,PiercingExitPoint,PiercingDirection,false);
    PiercingComponent.Reset();
    UE_LOG(LogFPSDemo,Log,TEXT("PROJECTILE_PIERCING_EXIT point=%s"),*PiercingExitPoint.ToString());
}

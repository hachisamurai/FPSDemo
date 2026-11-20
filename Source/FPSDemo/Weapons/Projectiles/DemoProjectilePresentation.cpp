// 职责：冻结弹种/接触网络校验、单弹接触去重、FP有限映射与分段尾长数学。使用：仅渲染消费，物理Sphere/GAS从不调用其输出。
#include "Weapons/Projectiles/DemoProjectilePresentation.h"
#include "Debug/DemoLog.h"

bool FDemoProjectilePresentationKey::operator==(const FDemoProjectilePresentationKey& Other) const
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] ProjectilePresentationKey.Equal"));
    return Shot == Other.Shot && Pellet == Other.Pellet;
}
uint32 GetTypeHash(const FDemoProjectilePresentationKey& Key)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] ProjectilePresentationKey.Hash"));
    return HashCombine(GetTypeHash(Key.Shot), GetTypeHash(Key.Pellet));
}
bool FDemoProjectileCosmeticBudget::AllowNew(int32 Count)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectilePresentation.Budget"));
    if (Count<0 || Count>=Capacity) bRejectUnknown=true;
    return !bRejectUnknown;
}
bool FDemoProjectileMuzzleReceipt::IsExpired(float Now) const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectilePresentation.MuzzleExpiry"));
    return !FMath::IsFinite(Now) || Now<Created || Now-Created>1.f;
}
FVector DemoProjectilePresentation::Map(const FVector& Physical, const FVector& Offset, float Distance, float BlendDistance)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] ProjectilePresentation.Map"));
    if (Physical.ContainsNaN() || Offset.ContainsNaN() || !FMath::IsFinite(Distance) || !FMath::IsFinite(BlendDistance)
        || Distance < 0.f || BlendDistance <= UE_SMALL_NUMBER) return Physical;
    const float Fraction = FMath::Clamp(Distance / BlendDistance, 0.f, 1.f); // 真实路径进度0..1，与帧率/弹速无关。
    const float Weight = 1.f - Fraction * Fraction * (3.f - 2.f * Fraction); // 端点导数为0，接入世界轨迹时不突然换向。
    return Physical + Offset * Weight;
}
FVector DemoProjectilePresentation::Converge(const FVector& Physical, const FVector& Offset, float Distance, float ConvergeDistance)
{
    // 纯静态高频数学，与DEMO_LOG_TICK同级别；默认不输出逐弹调用。
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] ProjectilePresentation.Converge"));
    if (Physical.ContainsNaN() || Offset.ContainsNaN() || !FMath::IsFinite(Distance) || !FMath::IsFinite(ConvergeDistance)
        || Distance < 0.f || ConvergeDistance <= UE_SMALL_NUMBER || Distance >= ConvergeDistance) return Physical;
    // 线性权重：物理弹道是直线时，视觉点也在F0→P(D)直线上匀速前进，玩家看到弹道从枪口飞向准星目标，而不是先并入视线中心。
    return Physical + Offset * (1.f - Distance / ConvergeDistance);
}
float DemoProjectilePresentation::RenderScale(float InitialScale,float Distance,float BlendDistance)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectilePresentation.RenderScale"));
    if (!FMath::IsFinite(InitialScale) || InitialScale<=0.f || InitialScale>1.f || !FMath::IsFinite(Distance)
        || Distance<0.f || !FMath::IsFinite(BlendDistance) || BlendDistance<=UE_SMALL_NUMBER) return 1.f;
    const float Fraction=FMath::Clamp(Distance/BlendDistance,0.f,1.f); // 与位置桥相同的实际路径进度，无dt/当前装备依赖。
    const float Merge=Fraction*Fraction*(3.f-2.f*Fraction);
    return FMath::Lerp(InitialScale,1.f,Merge);
}
FVector DemoProjectilePresentation::TracerMeshScale(float LengthCm,float WidthCm,float SizeScale)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectilePresentation.TracerMeshScale"));
    if (!FMath::IsFinite(LengthCm) || LengthCm<0.f || !FMath::IsFinite(WidthCm) || WidthCm<0.f) return FVector::ZeroVector;
    const float SafeScale=FMath::IsFinite(SizeScale) && SizeScale>0.f && SizeScale<=1.f?SizeScale:1.f; // 仅横截面容错，不能影响mapped段两端。
    return FVector(LengthCm/100.f,WidthCm*SafeScale/100.f,WidthCm*SafeScale/100.f);
}
float DemoProjectilePresentation::SegmentLength(const FVector& Origin,const FVector& Position,float TravelledDistance,float MaximumLength)
{
    // 纯静态高频数学无this；与DEMO_LOG_TICK同类别/级别，不默认输出逐弹调用。
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectilePresentation.SegmentLength"));
    if (Origin.ContainsNaN() || Position.ContainsNaN() || !FMath::IsFinite(TravelledDistance) || TravelledDistance<0.f
        || !FMath::IsFinite(MaximumLength) || MaximumLength<0.f)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_SEGMENT_REJECT reason=nonfinite/negative path input"));
        return 0.f;
    }
    return FMath::Min3(FMath::Min(MaximumLength,1000.f),TravelledDistance,static_cast<float>(FVector::Distance(Origin,Position)));
}
bool DemoProjectilePresentation::IsValid(const FDemoProjectilePresentationLaunch& Launch)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectilePresentation.IsValidLaunch"));
    // Shooter允许引用晚到；弹种来自发射冻结事实，不能以当前装配补签。
    const bool bValid=Launch.ShotId.IsValid() && Launch.Pellet>=0 && Launch.Pellet<64 && Launch.AmmoType<=3 // 只读结构结果，不授予World权限。
        && Launch.Phase!=0 && Launch.Level>0 && FMath::IsFinite(Launch.ServerTime)
        && !Launch.Origin.ContainsNaN() && !Launch.Direction.ContainsNaN()
        && FMath::IsNearlyEqual(Launch.Direction.SizeSquared(),1.,.01);
    if (!bValid) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_LAUNCH_REJECT reason=identity/ammo/time/coordinate structure shot=%s pellet=%d ammo=%u phase=%u level=%d"),
        *Launch.ShotId.ToString(),Launch.Pellet,Launch.AmmoType,Launch.Phase,Launch.Level);
    return bValid;
}
bool DemoProjectilePresentation::IsValid(const FDemoProjectilePresentationSample& Sample)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectilePresentation.IsValidSample"));
    const bool bValid=IsValid(Sample.Launch) && Sample.Sequence!=0 // 原身份/距离与新增可选接触事实结构的联合结果。
        && !Sample.Start.ContainsNaN() && !Sample.End.ContainsNaN()
        && FMath::IsFinite(Sample.ServerTime)
        && FMath::IsFinite(Sample.StartDistance) && FMath::IsFinite(Sample.EndDistance)
        && Sample.StartDistance>=0.f && Sample.EndDistance>=Sample.StartDistance
        && (!Sample.bFirst || Sample.Sequence==1)
        && (!Sample.bRicochetSegment || (!Sample.RicochetPoint.ContainsNaN() && !Sample.RicochetDirection.ContainsNaN()
            && FMath::IsNearlyEqual(Sample.RicochetDirection.SizeSquared(),1.,.01)))
        && (!Sample.bEnvironmentImpact || (!Sample.EnvironmentPoint.ContainsNaN() && !Sample.EnvironmentNormal.ContainsNaN()
            && FMath::IsNearlyEqual(Sample.EnvironmentNormal.SizeSquared(),1.,.01)))
        && (!Sample.bPiercingExit || (!Sample.PiercingExitPoint.ContainsNaN() && !Sample.PiercingExitDirection.ContainsNaN()
            && FMath::IsNearlyEqual(Sample.PiercingExitDirection.SizeSquared(),1.,.01)));
    if (!bValid) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_SAMPLE_REJECT reason=launch/sequence/distance/contact structure sequence=%u ricochet=%d environment=%d exit=%d"),
        Sample.Sequence,Sample.bRicochetSegment,Sample.bEnvironmentImpact,Sample.bPiercingExit);
    return bValid;
}
const FDemoAmmoVisualStyle& FDemoProjectileCosmeticState::ResolveVisualStyle(const FDemoProjectilePresentationLaunch& Launch)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectileCosmeticState.ResolveVisualStyle"));
    if (!bVisualStyleResolved)
    {
        VisualStyle=DemoAmmoVisuals::Resolve(Launch.Shooter,Launch.AmmoType);
        bVisualStyleResolved=true;
        UE_LOG(LogFPSDemo,Log,TEXT("PROJECTILE_STYLE_FROZEN shot=%s pellet=%d ammo=%u"),*Launch.ShotId.ToString(),Launch.Pellet,Launch.AmmoType);
    }
    return VisualStyle;
}
bool FDemoProjectileCosmeticState::ConsumeRicochet(const FDemoProjectilePresentationSample& Sample)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectileCosmeticState.ConsumeRicochet"));
    // 接收方先通过生命周期/网络顺序门；本方法只保留可补显的一次性消费，不改变游戏状态。
    if (!Sample.bRicochetSegment || bRicochetShown)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_RICOCHET_COSMETIC_SKIP absent=%d consumed=%d"),!Sample.bRicochetSegment,bRicochetShown);
        return false;
    }
    bRicochetShown=true;
    UE_LOG(LogFPSDemo,Log,TEXT("PROJECTILE_RICOCHET_COSMETIC consumed shot=%s pellet=%d sequence=%u"),*Sample.Launch.ShotId.ToString(),Sample.Launch.Pellet,Sample.Sequence);
    return true;
}
bool FDemoProjectileCosmeticState::ConsumeEnvironmentImpact(const FDemoProjectilePresentationSample& Sample)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectileCosmeticState.ConsumeEnvironmentImpact"));
    if (!Sample.bEnvironmentImpact || bEnvironmentImpactShown)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_ENVIRONMENT_COSMETIC_SKIP absent=%d consumed=%d"),!Sample.bEnvironmentImpact,bEnvironmentImpactShown);
        return false;
    }
    bEnvironmentImpactShown=true;
    UE_LOG(LogFPSDemo,Log,TEXT("PROJECTILE_ENVIRONMENT_COSMETIC consumed shot=%s pellet=%d sequence=%u"),*Sample.Launch.ShotId.ToString(),Sample.Launch.Pellet,Sample.Sequence);
    return true;
}
bool FDemoProjectileCosmeticState::ConsumePiercingExit(const FDemoProjectilePresentationSample& Sample)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectileCosmeticState.ConsumePiercingExit"));
    if (!Sample.bPiercingExit || bPiercingExitShown)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_PIERCING_EXIT_COSMETIC_SKIP absent=%d consumed=%d"),!Sample.bPiercingExit,bPiercingExitShown);
        return false;
    }
    bPiercingExitShown=true;
    UE_LOG(LogFPSDemo,Log,TEXT("PROJECTILE_PIERCING_EXIT_COSMETIC consumed shot=%s pellet=%d sequence=%u"),*Sample.Launch.ShotId.ToString(),Sample.Launch.Pellet,Sample.Sequence);
    return true;
}
bool DemoProjectilePresentation::IsWorldBoundary(const FDemoProjectilePresentationSample& Sample)
{
    // 无this的高频纯值边界查询；与DEMO_LOG_TICK相同级别，不新增默认逐帧输出。
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ProjectilePresentation.IsWorldBoundary"));
    return Sample.bBoundary || Sample.bRicochetSegment;
}

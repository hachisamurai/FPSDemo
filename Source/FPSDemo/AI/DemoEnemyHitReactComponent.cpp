// 职责：驱动存活敌人的解析受击弹簧及Facing前后变换租约。使用：Configure/Kick由Enemy调用，Remove/Apply由Presentation配对；依赖本地实际网格，不创建物理模拟或GA。
#include "AI/DemoEnemyHitReactComponent.h"
#include "AI/DemoEnemy.h"
#include "Animation/DemoEnemyPresentation.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

UDemoEnemyHitReactComponent::UDemoEnemyHitReactComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDemoEnemyHitReactComponent::Configure(USkeletalMeshComponent* Mesh, UDemoEnemyPresentation* Presentation)
{
    DEMO_LOG_CALL(); Reset(); Body = Mesh;
    if (!GetOwner() || !Body || !Presentation)
    { UE_LOG(LogFPSDemo, Warning, TEXT("HIT_REACT_CONFIG_REJECT missing dependency")); return; }
    AddTickPrerequisiteActor(GetOwner()); // Enemy先处理死亡/原近战位移，再推进本地弹簧。
    Presentation->AddTickPrerequisiteComponent(this); // 先推进数值，再Facing前后卸载/叠加；无循环依赖。
}

void UDemoEnemyHitReactComponent::Kick(const FVector& ShotDirection, bool bCore, bool bHeavyWeapon)
{
    DEMO_LOG_TICK();
    const ADemoEnemy* Enemy = Cast<ADemoEnemy>(GetOwner()); // 同步借用宿主身份，不保存玩家/武器。
    const UDemoHitFxSettings* Settings = GetDefault<UDemoHitFxSettings>(); // 只读本地配置，开关不修改伤害。
    if (GetNetMode() == NM_DedicatedServer)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_REACT_SKIPPED enemy=%s reason=dedicated"), *GetNameSafe(Enemy)); return; }
    if (!Enemy || !Body || !Settings->bHitReact)
    { Reset(); UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_REACT_SKIPPED enemy=%s reason=disabled/dependency"), *GetNameSafe(Enemy)); return; }
    if (Enemy->IsBoss() || !Enemy->IsAlive())
    { Reset(); UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_REACT_SKIPPED enemy=%s reason=%s"), *GetNameSafe(Enemy), Enemy->IsBoss() ? TEXT("boss") : TEXT("dead")); return; }
    if (ShotDirection.ContainsNaN() || ShotDirection.IsNearlyZero())
    { Reset(); UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_REACT_SKIPPED enemy=%s reason=nodir"), *GetNameSafe(Enemy)); return; }
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.; // World秒，单人暂停不会继续推进。
    const float Strength = Now - LastKickAt < FMath::Clamp(Settings->ReactMinInterval, 0.f, .25f)
        ? FMath::Clamp(Settings->ReactRapidScale, 0.f, 1.f) : 1.f; // 同敌人极短连发缩减新冲量，旧回弹保留。
    if (!Spring.Kick(ShotDirection, bCore, bHeavyWeapon, *Settings, Strength, Body->GetForwardVector())) return;
    LastKickAt = Now; SetComponentTickEnabled(true);
    UE_LOG(LogFPSDemo, Log, TEXT("HIT_REACT_START enemy=%s tier=%s heavy=%d deg=%.1f"), *Enemy->GetName(), bCore ? TEXT("head") : TEXT("body"), bHeavyWeapon,
        FMath::Min(Settings->ReactMaxDeg, (bHeavyWeapon ? Settings->ReactHeavyDeg : bCore ? Settings->ReactHeadDeg : Settings->ReactBodyDeg) + (bCore ? Settings->ReactHeadBackDeg : 0.f)));
}

void UDemoEnemyHitReactComponent::RemoveAppliedOffset()
{
    DEMO_LOG_TICK();
    if (!bApplied) return;
    if (Body)
    {
        // 仅剥离上次本组件写入量，不能把转向/其他表现写回第一次命中时的陈旧变换。
        Body->SetRelativeLocationAndRotation(Body->GetRelativeLocation() - AppliedLocalOffset,
            (AppliedLocalRotation.Inverse() * Body->GetRelativeTransform().GetRotation()).GetNormalized());
    }
    AppliedLocalOffset = FVector::ZeroVector; AppliedLocalRotation = FQuat::Identity; bApplied = false;
}

void UDemoEnemyHitReactComponent::ApplyOffset()
{
    DEMO_LOG_TICK();
    if (!Body || !IsComponentTickEnabled() || GetNetMode() == NM_DedicatedServer) return;
    if (bApplied) RemoveAppliedOffset(); // 防御非标准调用顺序，任何一帧最多一份偏移。
    const FTransform ParentWorld = Body->GetAttachParent() ? Body->GetAttachParent()->GetComponentTransform() : FTransform::Identity; // 当前父变换，不以Actor旋转猜网格局部轴。
    AppliedLocalOffset = ParentWorld.InverseTransformVector(Spring.OffsetCm); // 世界cm转父空间，继承比例也按真实矩阵消除。
    const FVector ParentAngle = ParentWorld.InverseTransformVectorNoScale(Spring.AngleDegrees); // 轴角不能把父缩放乘入角度。
    const double Degrees = ParentAngle.Size(); // 总倾斜角，已由纯弹簧限制在12°以内。
    AppliedLocalRotation = Degrees > UE_DOUBLE_SMALL_NUMBER ? FQuat(ParentAngle / Degrees, FMath::DegreesToRadians(Degrees)) : FQuat::Identity;
    Body->SetRelativeLocationAndRotation(Body->GetRelativeLocation() + AppliedLocalOffset,
        (AppliedLocalRotation * Body->GetRelativeTransform().GetRotation()).GetNormalized());
    bApplied = true;
}

void UDemoEnemyHitReactComponent::Reset()
{
    DEMO_LOG_TICK(); RemoveAppliedOffset(); Spring.Reset(); LastKickAt = -100.; SetComponentTickEnabled(false);
}

void UDemoEnemyHitReactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    const ADemoEnemy* Enemy = Cast<ADemoEnemy>(GetOwner()); // 本帧复核死亡/Boss/开关，不能让旧冲量跨生命周期。
    const UDemoHitFxSettings* Settings = GetDefault<UDemoHitFxSettings>(); // 每帧只读，不创建资源。
    if (GetNetMode() == NM_DedicatedServer || !Enemy || !Body || !Enemy->IsAlive() || Enemy->IsBoss() || !Settings->bHitReact)
    { Reset(); return; }
    Spring.Step(DeltaTime, *Settings);
    if (Spring.IsSettled())
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("HIT_REACT_SETTLED enemy=%s"), *Enemy->GetName()); Reset(); }
}

void UDemoEnemyHitReactComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); Reset(); Body = nullptr; Super::EndPlay(EndPlayReason);
}

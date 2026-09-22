#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/Projectiles/DemoProjectileMovementComponent.h"
#include "Weapons/Projectiles/DemoProjectileTracer.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Combat/DemoProjectileDamage.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "Debug/DemoLog.h"

#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
namespace
{
    // 只存在于Editor目标；按需显示已实际扫掠的路径/接触点，默认关闭，所有非Editor包不注册此调试命令。
    TAutoConsoleVariable<int32> CVarDemoProjectileTrajectories(TEXT("Demo.Debug.ProjectileTrajectories"), 0,
        TEXT("Editor only: draw actual player projectile sweep paths (0 off, 1 on)."), ECVF_Cheat);
}
#endif

ADemoProjectileBase::ADemoProjectileBase()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false; // 每帧校验与移动统一由Movement执行，避免两份Tick顺序依赖。
    bReplicates = false; // 本轮明确单人权威；不宣称拥有客户端预测/插值实现。
    InitialLifeSpan = 0.f; // 准备阶段不启动时限，扣费成功激活后才开始计时。
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("ProjectileCollision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(1.f);
    Collision->SetCollisionObjectType(ECC_GameTraceChannel3);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Collision->SetGenerateOverlapEvents(false);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileVisual"));
    Visual->SetupAttachment(Collision);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetGenerateOverlapEvents(false);
    Visual->SetCastShadow(false);
    // 基础球体是引擎已有Cook资源；子弹BP可以覆盖，不写入任何自造uasset。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere")); // CDO硬引用默认外观，不在逐枪路径加载。
    Config.Mesh = Sphere.Object;
    Visual->SetStaticMesh(Config.Mesh);
    Visual->SetRelativeTransform(Config.VisualTransform);
    Movement = CreateDefaultSubobject<UDemoProjectileMovementComponent>(TEXT("ProjectileMovement"));
    Movement->SetUpdatedComponent(Collision);
    Movement->SetAutoActivate(false);
    Movement->bAutoUpdateTickRegistration = true;
    Movement->ProjectileGravityScale = 0.f;
    Movement->bInitialVelocityInLocalSpace = false; // Prepare给世界方向，FinishSpawning不能再按Actor旋转一次。
    Movement->bRotationFollowsVelocity = true;
    Movement->bShouldBounce = false;
    Movement->bSweepCollision = true;
    Movement->bForceSubStepping = true;
    Movement->MaxSimulationTimeStep = 1.f / 120.f;
    Movement->MaxSimulationIterations = 8;
    Movement->BounceAdditionalIterations = 2; // 两个合法穿透接触可消化本帧剩余时间。
    Movement->Velocity = FVector::ZeroVector;
    Movement->InitialSpeed = 0.f;
    SetActorHiddenInGame(true);
}

void ADemoProjectileBase::OnConstruction(const FTransform& Transform)
{
    DEMO_LOG_CALL();
    Super::OnConstruction(Transform);
    ApplyConfiguration();
}

void ADemoProjectileBase::ApplyConfiguration()
{
    DEMO_LOG_CALL();
    SetActorScale3D(FVector::OneVector); // BP外观缩放仅来自VisualTransform，根球保持精确世界厘米半径。
    Collision->SetSphereRadius(FMath::IsFinite(Config.CollisionRadius) ? FMath::Clamp(Config.CollisionRadius, .1f, 20.f) : 1.f);
    // 构造脚本完成后恢复系统碰撞协议；派生BP只配置外观/尺寸，不得悄悄改为根球或Overlap伤害。
    Collision->SetSimulatePhysics(false);
    Collision->SetCollisionObjectType(ECC_GameTraceChannel3);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
    Collision->SetGenerateOverlapEvents(false);
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetStaticMesh(Config.Mesh);
    Visual->SetSimulatePhysics(false);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetRelativeTransform(Config.VisualTransform.ContainsNaN() ? FTransform::Identity : Config.VisualTransform);
    for (int32 Index = 0; Index < Config.MaterialOverrides.Num(); ++Index) // 索引对应外观材质槽，不改变碰撞形状。
    {
        if (Config.MaterialOverrides[Index]) Visual->SetMaterial(Index, Config.MaterialOverrides[Index]);
    }
    Movement->Velocity = FVector::ZeroVector;
    Movement->Deactivate();
    Movement->SetComponentTickEnabled(false);
}

bool ADemoProjectileBase::Prepare(UDemoShotContext* Context, int32 PelletIndex, const FVector& Direction)
{
    DEMO_LOG_CALL();
    FString Error; // 子弹CDO配置校验原因；拒绝时调用方销毁整组准备弹。
    if (!HasAuthority() || bConsumed || bPrepared || bFlying || !IsValid(Context) || !Context->IsAttackValid()
        || !Config.Validate(Error) || PelletIndex < 0 || PelletIndex > 31 || Direction.ContainsNaN() || Direction.IsNearlyZero()
        || !Context->GetSourcePawn() || Context->GetSourcePawn()->GetWorld() != GetWorld())
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_PREPARE_REJECT actor=%s pellet=%d reason=%s"), *GetName(), PelletIndex, *Error);
        return false;
    }
    // FinishSpawning期间蓝图可能覆盖实例速度/寿命；必须再次验证，不能仅信任开火前的CDO检查。
    if (Config.MaxLifeSeconds * Config.InitialSpeed < Context->GetRange())
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_PREPARE_REJECT actor=%s configured lifetime cannot cover weapon range"), *GetName());
        return false;
    }
    ApplyConfiguration(); // 蓝图构造已完成，强制准备态关闭运动，不能在Commit前撞敌人。
    ShotContext = Context;
    Launch.PelletIndex = PelletIndex;
    Launch.Origin = GetActorLocation();
    Launch.Direction = Direction.GetSafeNormal();
    LastTravelLocation = Launch.Origin;
    TravelledDistance = 0.f;
    Collision->IgnoreActorWhenMoving(Context->GetSourcePawn(), true);
    if (GetOwner()) Collision->IgnoreActorWhenMoving(GetOwner(), true); // Owner为发射武器，枪Mesh即使配置错误也不自伤。
    SetActorRotation(Launch.Direction.Rotation());
    SetActorHiddenInGame(true);
    SetLifeSpan(0.f);
    bPrepared = true;
    return true;
}

bool ADemoProjectileBase::ActivateProjectile()
{
    DEMO_LOG_CALL();
    if (!IsPrepared() || !ShotContext->IsAttackValid())
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_ACTIVATE_REJECT actor=%s stale prepared context"), *GetName());
        CancelProjectile();
        return false;
    }
    bFlying = true; // 必须先转状态，再开放可能触发同步回调的碰撞/表现。
    bPrepared = false;
    SetActorHiddenInGame(false);
    // Actor显隐不会清除组件自身的Hidden/Owner限制；激活时显式恢复世界子弹外观，不改用户Mesh或缩放。
    Visual->SetHiddenInGame(false);
    Visual->SetVisibility(true);
    Visual->SetOwnerNoSee(false);
    Visual->SetOnlyOwnerSee(false);
    SetActorEnableCollision(true); // 不继承蓝图构造期间的临时Actor禁碰撞状态。
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Movement->SetUpdatedComponent(Collision);
    Movement->InitialSpeed = Config.InitialSpeed;
    Movement->MaxSpeed = Config.InitialSpeed;
    Movement->Velocity = Launch.Direction * Config.InitialSpeed;
    Movement->Activate(true);
    Movement->SetComponentTickEnabled(true);
    SetLifeSpan(Config.MaxLifeSeconds);
    if (Config.TrailEffect)
        Trail = UNiagaraFunctionLibrary::SpawnSystemAttached(Config.TrailEffect, Collision, NAME_None, FVector::ZeroVector,
            FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, true);
    if (Config.bEnableTracer)
    {
        FActorSpawnParameters TracerSpawn; // 独立World表现无Owner/Instigator依赖，原弹或武器销毁不会带走已完成亮条。
        TracerSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Tracer = GetWorld()->SpawnActor<ADemoProjectileTracer>(Launch.Origin, Launch.Direction.Rotation(), TracerSpawn);
        if (IsValid(Tracer) && !Tracer->Initialize(Config, Launch.Origin)) { Tracer->Destroy(); Tracer = nullptr; }
        if (!IsValid(Tracer)) UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_TRACER_CREATE_FAILED actor=%s gameplay remains active"), *GetName());
    }
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_LAUNCHED shot=%s pellet=%d level=%d speed=%.1f"),
        *ShotContext->GetShotId().ToString(), Launch.PelletIndex, ShotContext->GetLevelNumber(), Config.InitialSpeed);
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_VISUAL mesh=%s scale=%s actorHidden=%d visible=%d hiddenInGame=%d onlyOwner=%d ownerNoSee=%d tracer=%s"),
        *GetNameSafe(Visual->GetStaticMesh()), *Visual->GetRelativeScale3D().ToString(), IsHidden(), Visual->GetVisibleFlag(),
        Visual->bHiddenInGame, Visual->bOnlyOwnerSee, Visual->bOwnerNoSee, *GetNameSafe(Tracer));
    OnProjectileLaunched(); // 蓝图仅表现；即使主动销毁，发射已发生，不能按失败退款。
    return true;
}

void ADemoProjectileBase::CancelProjectile()
{
    DEMO_LOG_CALL();
    if (bConsumed) return;
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_CANCEL shot=%s pellet=%d distance=%.2f flying=%d"),
        ShotContext ? *ShotContext->GetShotId().ToString() : TEXT("unprepared"), Launch.PelletIndex, TravelledDistance, bFlying);
    bConsumed = true; // 先闩锁，Destroy/EndPlay和Flow同步取消都不能二次进入。
    bFlying = false;
    bPrepared = false;
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Movement->StopMovementImmediately();
    Movement->Deactivate();
    Movement->SetComponentTickEnabled(false);
    SetLifeSpan(0.f);
    if (Trail) { Trail->DeactivateImmediate(); Trail->DestroyComponent(); Trail = nullptr; }
    if (IsValid(Tracer))
    {
        // 正常命中/距离结束只保留纯表现；世界或Avatar失效立即删除，不能把战斗效果带进安全区。
        if (IsValid(ShotContext) && ShotContext->IsAttackValid()) Tracer->Finish();
        else Tracer->Destroy();
        Tracer = nullptr;
    }
    Destroy();
}

void ADemoProjectileBase::CancelAll(UWorld* World)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs world=%s"), __FUNCTION__, *GetNameSafe(World));
    if (!World) return;
    for (TActorIterator<ADemoProjectileBase> It(World); It; ++It) // World拥有Actor；取消幂等，允许清关在伤害调用栈中执行。
    {
        It->CancelProjectile();
    }
    ADemoProjectileTracer::CancelAll(World); // 同时收回已经脱离原弹的淡出Actor，不等0.06秒尾迹。
}

void ADemoProjectileBase::LifeSpanExpired()
{
    DEMO_LOG_CALL();
    CancelProjectile();
}

void ADemoProjectileBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    bConsumed = true;
    bFlying = false;
    bPrepared = false;
    Movement->Deactivate();
    if (Trail) { Trail->DeactivateImmediate(); Trail->DestroyComponent(); Trail = nullptr; }
    // 正常Cancel已移交淡出并清空引用；未经过Cancel的外部Destroy/旅行必须立即撤销仍在更新的亮条。
    if (IsValid(Tracer) && !Tracer->IsFinishing()) Tracer->Destroy();
    Tracer = nullptr;
    // 不在这里清空ShotContext：当前同步GE调用栈可能仍在使用它，Actor回收时自然释放强引用。
    Super::EndPlay(EndPlayReason);
}

bool ADemoProjectileBase::IsPrepared() const { DEMO_LOG_TICK(); return bPrepared && !bFlying && !bConsumed && IsValid(ShotContext); }
bool ADemoProjectileBase::CanSimulate() const
{
    DEMO_LOG_TICK();
    return HasAuthority() && bFlying && !bConsumed && !bResolvingImpact && IsValid(ShotContext) && ShotContext->IsAttackValid();
}
float ADemoProjectileBase::GetRemainingDistance() const
{
    DEMO_LOG_TICK();
    return ShotContext ? FMath::Max(0.f, ShotContext->GetRange() - TravelledDistance) : 0.f;
}
void ADemoProjectileBase::RecordTravel(const FVector& Position)
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (CVarDemoProjectileTrajectories.GetValueOnGameThread() > 0)
        DrawDebugLine(GetWorld(), LastTravelLocation, Position, FColor::Cyan, false, 1.f, 0, .75f);
#endif
    TravelledDistance += FVector::Distance(LastTravelLocation, Position);
    LastTravelLocation = Position;
    if (!bConsumed && IsValid(Tracer)) Tracer->UpdateTravel(Position, TravelledDistance); // 只使用本次真实扫掠终点，碰撞前不预测可见路径。
}
UDemoShotContext* ADemoProjectileBase::GetShotContext() const { DEMO_LOG_TICK(); return ShotContext; }
int32 ADemoProjectileBase::GetPelletIndex() const { DEMO_LOG_TICK(); return Launch.PelletIndex; }

bool ADemoProjectileBase::ResolveImpact(const FHitResult& Hit)
{
    DEMO_LOG_CALL();
    if (!CanSimulate() || !Hit.bBlockingHit || TravelledDistance > ShotContext->GetRange() + .01f)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_IMPACT_REJECT actor=%s invalid phase/range/hit"), *GetName());
        CancelProjectile();
        return false;
    }
    ADemoEnemy* Enemy = Cast<ADemoEnemy>(Hit.GetActor()); // 真实当前阻挡者；墙体及其他Actor不能产生敌人伤害。
#if WITH_EDITOR
    if (CVarDemoProjectileTrajectories.GetValueOnGameThread() > 0)
        DrawDebugSphere(GetWorld(), Hit.ImpactPoint, Config.CollisionRadius, 8, Enemy ? FColor::Green : FColor::Red, false, 1.f);
#endif
    if (!IsValid(Enemy))
    {
        bFlying = false; // 表现事件之前关闭伤害资格，禁止表现回调重入。
        PlayWallImpact(Hit);
        OnProjectileImpact(Hit, false);
        CancelProjectile();
        return false;
    }
    if (HitActors.Contains(Enemy))
    {
        Collision->IgnoreActorWhenMoving(Enemy, true); // 理论上已忽略，仍防同敌多个骨骼刚体在同一子步重复返回。
        return true;
    }
    HitActors.Add(Enemy); // 必须先登记再提交GE，死亡/清关可能同步重入取消流程。
    bResolvingImpact = true;
    const bool bPiercing = ShotContext->GetAmmoType() == 3; // 本枪冻结的穿透类型。
    if (!bPiercing) bFlying = false; // 普通弹在GE前消费飞行资格；伤害Helper只使用ShotContext验证战斗。
    if (AcceptedEnemyCount == 0) FirstHitBaseDamage = ShotContext->GetFirstHitBaseDamage(Hit.ImpactPoint);
    const float BaseDamage = AcceptedEnemyCount == 0 ? FirstHitBaseDamage
        : FirstHitBaseDamage * ShotContext->GetAmmoSnapshot().SecondaryDamageRatio; // 首段衰减一次，后段独立骨骼倍率。
    TStrongObjectPtr<UDemoShotContext> ContextGuard(ShotContext); // 同步GE可能Destroy本弹；跨同步回调保活上下文，离开此栈即释放。
    const bool bAccepted = DemoProjectileDamage::Apply(ContextGuard.Get(), Hit, BaseDamage, Launch.PelletIndex); // 每个真实Hit只调用一次。
    bResolvingImpact = false;
    if (!IsValid(this) || bConsumed) return false; // 最后一敌死亡可同步清关/清弹。
    if (!ContextGuard->IsAttackValid()) { CancelProjectile(); return false; } // Avatar/阶段直接变化也立即结束，不等待下一帧。
    if (bAccepted) ++AcceptedEnemyCount;
    OnProjectileImpact(Hit, true);
    if (!IsValid(this) || bConsumed) return false; // 表现事件也可能主动销毁，不继续访问组件。
    if (bAccepted && bPiercing && AcceptedEnemyCount < 2 && ContextGuard->IsAttackValid())
    {
        Collision->IgnoreActorWhenMoving(Enemy, true); // 忽略整只敌人，不跳移球心，不射线寻找后敌。
        return true;
    }
    CancelProjectile();
    return false;
}

void ADemoProjectileBase::PlayWallImpact(const FHitResult& Hit)
{
    DEMO_LOG_CALL();
    if (Config.WallImpactEffect)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Config.WallImpactEffect, Hit.ImpactPoint, Hit.ImpactNormal.Rotation());
    if (Config.WallImpactSound) UGameplayStatics::PlaySoundAtLocation(this, Config.WallImpactSound, Hit.ImpactPoint);
}

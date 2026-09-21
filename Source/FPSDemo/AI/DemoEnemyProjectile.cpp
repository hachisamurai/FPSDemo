#include "AI/DemoEnemyProjectile.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ADemoEnemyProjectile::ADemoEnemyProjectile()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    InitialLifeSpan = 6.f;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    Collision->InitSphereRadius(12.f);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionObjectType(ECC_WorldDynamic);
    Collision->SetCollisionResponseToAllChannels(ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
    SetRootComponent(Collision);
    Collision->OnComponentHit.AddDynamic(this, &ADemoEnemyProjectile::OnImpact);
    Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileVisual"));
    Visual->SetupAttachment(Collision);
    Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Visual->SetRelativeScale3D(FVector(.24f));
    // CDO硬引用保证球体与Editor创建的发光材质被Cooker收集。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/EnemyAttacks/M_EnemyGlow"));
    Visual->SetStaticMesh(Sphere.Object);
    if (Glow.Object) Visual->SetMaterial(0, Glow.Object);
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->SetUpdatedComponent(Collision);
    Movement->ProjectileGravityScale = 0.f;
    // Initialize传入世界方向；禁止FinishSpawning时再次按Actor朝向旋转速度导致反向发射。
    Movement->bInitialVelocityInLocalSpace = false;
    Movement->bRotationFollowsVelocity = true;
    Movement->bShouldBounce = false;
    Movement->bForceSubStepping = true;
}
void ADemoEnemyProjectile::Initialize(ADemoEnemy* Source, const FVector& Direction, float Speed)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !Source || !Source->IsAlive() || Direction.IsNearlyZero() || !FMath::IsFinite(Speed) || Speed <= 0.f)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_REJECTED invalid source/direction/speed"));
        Destroy();
        return;
    }
    Shooter = Source;
    SetOwner(Source);
    Collision->IgnoreActorWhenMoving(Source, true);
    Damage = FMath::Max(0.f, Source->GetAttackPower());
    // State仅借用本World，出生关卡冻结到标量，不把对象保存在飞行物中。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
    FiredLevel = State ? State->LevelNumber : 0;
    Movement->Velocity = Direction.GetSafeNormal() * Speed;
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_FIRED source=%s damage=%.2f speed=%.0f level=%d"), *Source->GetName(), Damage, Speed, FiredLevel);
}
bool ADemoEnemyProjectile::IsAttackValid() const
{
    DEMO_LOG_TICK();
    // State仅当前帧借用；死亡、阶段或关卡变化都使弹失效。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
    return HasAuthority() && !bConsumed && Shooter.IsValid() && Shooter->IsAlive()
        && State && State->Phase == EDemoPhase::Combat && State->LevelNumber == FiredLevel;
}
void ADemoEnemyProjectile::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    if (HasAuthority() && !IsAttackValid())
    {
        UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_CANCELLED source/phase/level invalid"));
        Destroy();
    }
}
void ADemoEnemyProjectile::OnImpact(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent,
    FVector NormalImpulse, const FHitResult& Hit)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || bConsumed || OtherActor == Shooter.Get()) return;
    // 先快照有效性再消耗，不允许GE导致的终局回调重入后再次造成伤害。
    const bool bValidAttack = IsAttackValid();
    bConsumed = true;
    SetActorEnableCollision(false);
    Movement->StopMovementImmediately();
    // Player只在本次同步回调借用；普通投射物可走位躲避，不受Boss专用冲刺窗口免疫。
    ADemoCharacter* Player = Cast<ADemoCharacter>(OtherActor);
    if (bValidAttack && Player && Player->GetDemoAttributes() && Player->GetDemoAttributes()->GetHealth() > 0.f)
        DemoEffects::Apply(Shooter->GetAbilitySystemComponent(), Player->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -Damage);
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_IMPACT target=%s player=%d valid=%d"), *GetNameSafe(OtherActor), Player != nullptr, bValidAttack);
    Destroy();
}

#include "AI/DemoAttackPulse.h"
#include "Debug/DemoLog.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ADemoAttackPulse::ADemoAttackPulse()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    InitialLifeSpan = .4f;
    Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PulseShell"));
    SetRootComponent(Shell);
    Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shell->CastShadow = false;
    // 使用真实Editor材质资产；基础球半径50cm，Tick按世界半径转换比例。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/EnemyAttacks/M_EnemyGlow"));
    Shell->SetStaticMesh(Sphere.Object);
    if (Glow.Object) Shell->SetMaterial(0, Glow.Object);
    Shell->SetRelativeScale3D(FVector(2.6f));
}
void ADemoAttackPulse::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    // 扩散只做释放反馈，不能用球体碰撞把全图即时攻击变成二次伤害。
    Shell->SetWorldScale3D(FVector(FMath::Lerp(FMath::Min(130.f,EndRadius), EndRadius, FMath::Clamp(Age / .4f, 0.f, 1.f)) / 50.f)); // 弹药用局部半径，不能复用Boss全图扩散尺寸。
}

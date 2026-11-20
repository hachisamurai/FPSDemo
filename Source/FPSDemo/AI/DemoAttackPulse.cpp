// 职责：GC/Boss攻击的无伤害可复制球壳表现。使用：设置EndRadius与Tint，World在短寿命内扩展并回收；不得用于命中判定。
#include "AI/DemoAttackPulse.h"
#include "Debug/DemoLog.h"
#include "Net/UnrealNetwork.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

ADemoAttackPulse::ADemoAttackPulse()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true; bReplicates=true; // 服务器生成的爆发同时对相关客户端可见，球壳始终无碰撞。
    InitialLifeSpan = .4f;
    Shell = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PulseShell"));
    SetRootComponent(Shell);
    Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shell->CastShadow = false;
    // 使用真实Editor材质资产；基础球半径50cm，Tick按世界半径转换比例。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/EnemyAttacks/M_EnemyGlow"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> TalentGlow(TEXT("/Game/Data/Talents/VFX/M_TalentPulse")); // 独立参数材质，避免给常量红光设置不存在的Color参数。
    TintMaterial=TalentGlow.Object;
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

void ADemoAttackPulse::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ADemoAttackPulse,EndRadius); DOREPLIFETIME(ADemoAttackPulse,Tint);
}

void ADemoAttackPulse::SetTint(FLinearColor Color)
{ DEMO_LOG_CALL(); Tint=Color; OnRep_Tint(); if(HasAuthority()) ForceNetUpdate(); }
void ADemoAttackPulse::OnRep_Tint()
{
    DEMO_LOG_CALL();
    if(TintMaterial) Shell->SetMaterial(0,TintMaterial); // 每个实例拥有独立MID，不修改原Boss材质。
    if(UMaterialInstanceDynamic* Material=Shell->CreateAndSetMaterialInstanceDynamic(0)) Material->SetVectorParameterValue(TEXT("Color"),Tint*6.f); // 材质归Shell持有，不共享修改原资产。
}

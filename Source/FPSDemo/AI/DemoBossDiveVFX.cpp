#include "AI/DemoBossDiveVFX.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Debug/DemoLog.h"
ADemoBossDiveVFX::ADemoBossDiveVFX()
{
    DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere")); // Engine球网格，默认半径50cm。
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Blue(TEXT("/Game/VFX/EnemyAttacks/Dive/M_BossDiveBlue")); // 独立蓝材质，不改变原全图红光。
    for (int32 Index=0;Index<3;++Index) // 三层相位错开形成持续发散，而非单次缩放球。
    {
        UStaticMeshComponent* Shell=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Shell%d"),Index)); // Actor持有的组件。
        Shell->SetupAttachment(GetRootComponent()); Shell->SetStaticMesh(Sphere.Object); Shell->SetMaterial(0,Blue.Object);
        Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision); Shell->SetCanEverAffectNavigation(false); Shell->CastShadow=false; Shell->SetHiddenInGame(true);
        Shells.Add(Shell);
    }
    Marker=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LandingMarker"));
    Marker->SetupAttachment(GetRootComponent()); Marker->SetStaticMesh(Sphere.Object); Marker->SetMaterial(0,Blue.Object);
    Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision); Marker->SetCanEverAffectNavigation(false); Marker->CastShadow=false; Marker->SetHiddenInGame(true);
    Light=CreateDefaultSubobject<UPointLightComponent>(TEXT("ChargeLight")); Light->SetupAttachment(GetRootComponent());
    Light->SetLightColor(FLinearColor(.02f,.4f,1.f)); Light->SetAttenuationRadius(1100); Light->SetIntensity(22000); Light->SetCastShadows(false); Light->SetVisibility(false);
}
void ADemoBossDiveVFX::ShowCharge(const FVector& Center)
{
    DEMO_LOG_CALL(); SetActorLocation(Center); Mode=1; Age=0;
    for (UStaticMeshComponent* Shell:Shells) Shell->SetHiddenInGame(false); // 同步激活全部壳，Tick计算错峰半径。
    Light->SetVisibility(true);
}
void ADemoBossDiveVFX::ShowTarget(const FVector& Center,float Radius)
{
    DEMO_LOG_CALL(); Mode=2; Age=0; TargetRadius=Radius;
    for (UStaticMeshComponent* Shell:Shells) Shell->SetHiddenInGame(true); // 无敌结束时蓝色蓄力壳同步消失。
    Light->SetVisibility(false); SetActorLocation(Center-FVector(0,0,124)); // Boss中心高130cm，标记高地面6cm避免Z冲突。
    Marker->SetRelativeScale3D(FVector(Radius/50.f,Radius/50.f,.06f)); Marker->SetHiddenInGame(false);
}
void ADemoBossDiveVFX::ShowImpact() { DEMO_LOG_CALL(); Mode=3; Age=0; }
void ADemoBossDiveVFX::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds); Age+=DeltaSeconds;
    if (Mode==1)
        for (int32 Index=0;Index<Shells.Num();++Index) // 固定1秒扩散周期，三层均匀错相。
            Shells[Index]->SetRelativeScale3D(FVector(FMath::Lerp(2.6f,9.f,FMath::Fmod(Age+Index/3.f,1.f))));
    if (Mode==3)
    {
        const float Scale=TargetRadius/50.f*(1.f+FMath::Clamp(Age/.4f,0.f,1.f)); // 落地半秒内扩大，只表现无二次伤害。
        Marker->SetRelativeScale3D(FVector(Scale,Scale,.12f));
        if (Age>.4f) Marker->SetHiddenInGame(true);
    }
}

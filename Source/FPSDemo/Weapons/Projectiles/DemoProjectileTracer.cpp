#include "Weapons/Projectiles/DemoProjectileTracer.h"
#include "Weapons/Projectiles/DemoProjectileConfig.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"
#include "CoreGlobals.h"

ADemoProjectileTracer::ADemoProjectileTracer()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false; // 飞行更新由真实弹推动；只有独立淡出需要Tick。
    bReplicates = false; // 本轮单人纯表现，不增加网络Actor协议。
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TracerMesh"));
    SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCastShadow(false);
    Mesh->SetOwnerNoSee(false);
    Mesh->SetOnlyOwnerSee(false);
    Mesh->SetReceivesDecals(false);
    Mesh->bUseAsOccluder = false; // 亮条不参与其他几何的遮挡优化。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube")); // 引擎100cm立方体硬引用，Cook自动收集。
    Mesh->SetStaticMesh(Cube.Object);
    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);
}

bool ADemoProjectileTracer::Initialize(const FDemoProjectileConfig& Config, const FVector& InOrigin)
{
    DEMO_LOG_CALL();
    FString Error; // 借用完整子弹配置校验，非法视觉数值不能产生NaN组件变换。
    if (bInitialized || !Config.bEnableTracer || !Config.Validate(Error) || InOrigin.ContainsNaN() || !Mesh->GetStaticMesh())
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_TRACER_REJECT config/mesh/origin %s"), *Error);
        return false;
    }
    Origin = InOrigin;
    LastPosition = InOrigin;
    Width = Config.TracerWidth;
    MaximumLength = Config.TracerLength;
    FadeSeconds = Config.TracerFadeSeconds;
    SetActorLocation(InOrigin);
    Mesh->SetHiddenInGame(false);
    Mesh->SetVisibility(true);
    UMaterialInterface* BaseMaterial = Config.TracerMaterial ? Config.TracerMaterial.Get() : Mesh->GetMaterial(0); // 空配置仍显示默认Mesh材质，但没有可调发光效果。
    if (BaseMaterial)
    {
        Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        if (Material)
        {
            Material->SetScalarParameterValue(TEXT("TracerOpacity"), 1.f);
            Mesh->SetMaterial(0, Material);
        }
    }
    bInitialized = true;
    SetLifeSpan(Config.MaxLifeSeconds + FadeSeconds + 1.f); // 兜底覆盖弹本体寿命；Finish会缩短到独立淡出秒数。
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_TRACER_READY material=%s length=%.1f width=%.1f fade=%.3f"),
        *GetNameSafe(BaseMaterial), MaximumLength, Width, FadeSeconds);
    return true;
}

void ADemoProjectileTracer::UpdateTravel(const FVector& Position, float TravelledDistance)
{
    DEMO_LOG_TICK();
    if (!bInitialized || bFinishing || Position.ContainsNaN() || !FMath::IsFinite(TravelledDistance) || TravelledDistance <= 0.f) return;
    const FVector Delta = Position - LastPosition; // 世界cm，仅计算实际已经到达的方向，绝不读取瞄准目标。
    if (Delta.IsNearlyZero()) return;
    const FVector Direction = Delta.GetSafeNormal(); // 当前方案直线无重力；未来曲线弹道应替换为分段/ribbon，不画跨弯弦线。
    const float Length = FMath::Min3(MaximumLength, TravelledDistance, static_cast<float>(FVector::Distance(Origin, Position))); // 首帧短路径不伸到枪口后。
    if (Length <= UE_KINDA_SMALL_NUMBER) return;
    // 引擎Cube以中心为原点，100cm边长；前端严格落在本次实际球心，后端只覆盖已经飞过的路径。
    SetActorLocationAndRotation(Position - Direction * (Length * .5f), Direction.Rotation());
    SetActorScale3D(FVector(Length / 100.f, Width / 100.f, Width / 100.f));
    LastPosition = Position;
    bHasTravel = true;
    SetActorHiddenInGame(false);
}

void ADemoProjectileTracer::Finish()
{
    DEMO_LOG_CALL();
    if (bFinishing) return;
    bFinishing = true;
    if (!bInitialized || !bHasTravel || FadeSeconds <= UE_KINDA_SMALL_NUMBER)
    {
        Destroy();
        return;
    }
    FadeElapsed = 0.f;
    FinishFrame = GFrameCounter;
    SetActorTickEnabled(true);
    // 改由Tick淡出后释放：寿命Timer若在下一帧直接消费0.06秒，15fps时仍可能没有任何可见帧。
    // 初始完成帧及下一游戏帧都保留，再累计World秒；实际是否被相机看到仍取决于遮挡/视锥。
    SetLifeSpan(0.f);
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_TRACER_FINISH fade=%.3f"), FadeSeconds);
}

void ADemoProjectileTracer::CancelAll(UWorld* World)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs world=%s"), __FUNCTION__, *GetNameSafe(World));
    if (!World) return;
    for (TActorIterator<ADemoProjectileTracer> It(World); It; ++It) // 包含原弹已结束的淡出Actor，清场不得留下战斗表现。
        It->Destroy();
}

bool ADemoProjectileTracer::IsFinishing() const { DEMO_LOG_TICK(); return bFinishing; }

void ADemoProjectileTracer::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    if (!bFinishing || !bHasTravel) return;
    if (GFrameCounter <= FinishFrame + 1) return; // 不能在创建/完成同帧或第一个后续游戏帧提前删掉纯表现。
    FadeElapsed += DeltaSeconds;
    const float Opacity = FMath::Clamp(1.f - FadeElapsed / FMath::Max(FadeSeconds, UE_KINDA_SMALL_NUMBER), 0.f, 1.f); // 材质渐隐值0..1，纯表现不修改任何Gameplay状态。
    if (Material) Material->SetScalarParameterValue(TEXT("TracerOpacity"), Opacity);
    if (Opacity <= 0.f) Destroy();
}

void ADemoProjectileTracer::LifeSpanExpired()
{
    DEMO_LOG_CALL();
    Destroy();
}

void ADemoProjectileTracer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    SetActorTickEnabled(false);
    Material = nullptr;
    Super::EndPlay(EndPlayReason);
}

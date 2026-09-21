#include "Spawning/DemoEnemySpawnArea.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"

ADemoEnemySpawnArea::ADemoEnemySpawnArea()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false;
    Preview = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnAreaPreview"));
    SetRootComponent(Preview); Preview->InitBoxExtent(FVector(1400, 1400, 150));
    Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision); Preview->SetCanEverAffectNavigation(false); Preview->SetHiddenInGame(true);
}
bool ADemoEnemySpawnArea::Resolve(UWorld* World, int32 Index, const FVector& FallbackCenter, FTransform& OutTransform, FDemoSpawnGeometry& OutGeometry, FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs arena=%d"), __FUNCTION__, Index);
    if (!World || Index < 0 || Index > 2 || FallbackCenter.ContainsNaN())
    { Error = TEXT("刷怪区域World/编号/回退中心非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_AREA %s"), *Error); return false; }
    const ADemoEnemySpawnArea* Found = nullptr; // 仅同步解析借用，不把场景Actor跨关卡保存在计划中。
    for (TActorIterator<ADemoEnemySpawnArea> It(World); It; ++It) // 当前World中唯一匹配，重复配置不能由不稳定迭代顺序决定。
        if (It->ArenaIndex == Index)
        {
            if (Found) { Error = TEXT("同一区号存在多个刷怪区域"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_AREA %s"), *Error); return false; }
            Found = *It;
        }
    OutTransform = Found ? FTransform(Found->GetActorRotation(), Found->GetActorLocation()) : FTransform(FallbackCenter);
    OutGeometry = Found ? Found->Geometry : FDemoSpawnGeometry();
    if (OutTransform.ContainsNaN() || OutGeometry.CampaignRadius.ContainsNaN() || OutGeometry.ReinforcementRadius.ContainsNaN()
        || OutGeometry.CampaignRadius.X <= 0 || OutGeometry.CampaignRadius.Y <= 0 || OutGeometry.ReinforcementRadius.X <= 0 || OutGeometry.ReinforcementRadius.Y <= 0
        || OutGeometry.CampaignOffset.ContainsNaN() || OutGeometry.BossOffset.ContainsNaN() || !FMath::IsFinite(OutGeometry.ReinforcementHeight))
    { Error = TEXT("刷怪区域半径/高度/变换非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_AREA %s"), *Error); return false; }
    UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_AREA arena=%d source=%s"), Index, Found ? *Found->GetName() : TEXT("default whitebox"));
    return true;
}

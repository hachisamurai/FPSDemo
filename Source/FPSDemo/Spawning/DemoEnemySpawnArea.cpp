#include "Spawning/DemoEnemySpawnArea.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Components/BoxComponent.h"
#include "Debug/DemoLog.h"
ADemoEnemySpawnArea::ADemoEnemySpawnArea()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false;
    Preview = CreateDefaultSubobject<UBoxComponent>(TEXT("SpawnAreaPreview"));
    SetRootComponent(Preview); Preview->InitBoxExtent(FVector(1400, 1400, 150));
    Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision); Preview->SetCanEverAffectNavigation(false); Preview->SetHiddenInGame(true);
}
void ADemoEnemySpawnArea::BeginPlay()
{
    DEMO_LOG_CALL(); Super::BeginPlay();
    if (UDemoEncounterAreaSubsystem* Areas = GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>()) Areas->RegisterArea(this); // 当前World借用服务，Editor预览不存在。
}
void ADemoEnemySpawnArea::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    if (UDemoEncounterAreaSubsystem* Areas = GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>()) Areas->UnregisterArea(this); // 独立于子系统释放顺序。
    Super::EndPlay(EndPlayReason);
}
bool ADemoEnemySpawnArea::Resolve(UWorld* World, int32 Index, const FVector& FallbackCenter, FTransform& OutTransform, FDemoSpawnGeometry& OutGeometry, FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    UDemoEncounterAreaSubsystem* Areas = World ? World->GetSubsystem<UDemoEncounterAreaSubsystem>() : nullptr; // 兼容入口借用当前World注册表。
    FDemoAreaSnapshot Snapshot; // 只返回出生几何，正式玩法使用完整区域快照。
    if (!Areas || !Areas->ResolveArea(Index, Snapshot, Error, &FallbackCenter)) return false;
    OutTransform = Snapshot.Transform; OutGeometry = Snapshot.Geometry;
    return true;
}

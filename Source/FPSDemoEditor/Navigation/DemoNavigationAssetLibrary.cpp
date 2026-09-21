#include "Navigation/DemoNavigationAssetLibrary.h"
#include "Debug/DemoLog.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "EngineUtils.h"
#include "AssetCompilingManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

bool UDemoNavigationAssetLibrary::ConfigureNavigation(UWorld* World)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] ConfigureNavigation"));
    if (!World || World->IsGameWorld()) { UE_LOG(LogFPSDemo,Error,TEXT("NAV_AUTHOR requires editor world")); return false; }
    FAssetCompilingManager::Get().FinishAllCompilation(); // Commandlet同步脚本早于Editor首帧；未完成编译的StaticMesh不参与导航几何导出。
    TArray<FVector> Centers; // 实际白模Actor的地面原点，用于验证；区域移动后不依赖硬编码坐标。
    for (int32 Arena = 0; Arena < 3; ++Arena) // 仅三个战斗区域，安全区无需可走敌人网格。
    {
        AActor* Area = nullptr; // World借用的白模区域Actor，不改变用户模型/碰撞。
        ANavMeshBoundsVolume* Bounds = nullptr; // 精确标签匹配生成体积，重复执行不增生，不删除用户体积。
        const FName AreaTag(*FString::Printf(TEXT("DemoArea%d"),Arena)); // 现有GameMode白模识别标签。
        const FName BoundsTag(*FString::Printf(TEXT("DemoEnemyNav%d"),Arena)); // 本模块专用生成标签。
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->ActorHasTag(TEXT("DemoAuthoredBlockout")) && It->ActorHasTag(AreaTag)) Area = *It;
            if (It->ActorHasTag(BoundsTag)) Bounds = Cast<ANavMeshBoundsVolume>(*It);
        }
        if (!Area) { UE_LOG(LogFPSDemo,Error,TEXT("NAV_AUTHOR missing arena=%d"),Arena); return false; }
        Centers.Add(Area->GetActorLocation());
        if (UStaticMeshComponent* Mesh = Area->FindComponentByClass<UStaticMeshComponent>()) // 只读检查白模组件，定位有碰撞却无导航几何的问题。
            UE_LOG(LogFPSDemo,Log,TEXT("NAV_AUTHOR geometry=%s relevant=%d affects=%d compiling=%d"),*Area->GetName(),Mesh->IsNavigationRelevant(),Mesh->CanEverAffectNavigation(),Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->IsCompiling());
        if (!Bounds) Bounds = World->SpawnActor<ANavMeshBoundsVolume>();
        if (!Bounds) { UE_LOG(LogFPSDemo,Error,TEXT("NAV_AUTHOR spawn volume failed")); return false; }
        Bounds->Modify();
        Bounds->Tags.AddUnique(BoundsTag);
        Bounds->SetActorLabel(FString::Printf(TEXT("DemoEnemyNav_Arena%d"),Arena));
        Bounds->SetActorLocation(Area->GetActorLocation()+FVector(0,0,150));
        UCubeBuilder* Builder = NewObject<UCubeBuilder>(); // 临时编辑器BrushBuilder，CreateBrush会拷贝给Volume。
        Builder->X = 4000.f; Builder->Y = 4000.f; Builder->Z = 600.f; // cm，覆盖34米区域及地面下150cm，上界不包含高处顶棚。
        UActorFactory::CreateBrushForVolumeActor(Bounds,Builder);
        Bounds->PostEditChange(); // 新Brush创建后重新通知导航，不能沿用Spawn时空Brush登记的零尺寸范围。
        Bounds->MarkPackageDirty();
        UE_LOG(LogFPSDemo,Log,TEXT("NAV_AUTHOR volume=%s center=%s bounds=%s"),*Bounds->GetName(),*Bounds->GetActorLocation().ToString(),*Bounds->GetComponentsBoundingBox(true).ToString());
    }
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World); // World拥有构建任务，只在Editor同步等待完成。
    if (!Nav) { UE_LOG(LogFPSDemo,Error,TEXT("NAV_AUTHOR missing navigation system")); return false; }
    for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It) Nav->OnNavigationBoundsUpdated(*It); // Commandlet没有正常Editor帧，显式提交新范围。
    Nav->Tick(0.f); // 只在制作脚本处理待提交的Bounds/OcTree更新，Build之前完成注册。
    UE_LOG(LogFPSDemo,Log,TEXT("NAV_AUTHOR registered bounds=%d"),Nav->GetNavigationBounds().Num());
    Nav->OnWorldInitDone(FNavigationSystemRunMode::EditorMode); // 原地图初载无Bounds；编辑器重新初始化导航，收集完整静态碰撞再构建。
    Nav->Build();
    for (ANavigationData* Data : Nav->NavDataSet) if (Data) Data->EnsureBuildCompletion(); // 编辑器制作阶段允许等待，游戏Tick绝不等待构建线程。
    const ANavigationData* Data = Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate); // 只验证已经构建的数据，不用空实例冒充成功。
    if (!Data) { UE_LOG(LogFPSDemo,Error,TEXT("NAV_AUTHOR no navigation data generated")); return false; }
    for (int32 Arena = 0; Arena < 3; ++Arena)
    {
        FNavLocation Probe; // 三个平坦中心必须能投影，失败不保存缺失导航的地图。
        if (!Nav->ProjectPointToNavigation(Centers[Arena]+FVector(0,0,50),Probe,FVector(250,250,150),Data))
        { UE_LOG(LogFPSDemo,Error,TEXT("NAV_AUTHOR arena=%d projection failed"),Arena); return false; }
        UE_LOG(LogFPSDemo,Log,TEXT("NAV_AUTHOR arena=%d ground=%s radius=%.0f"),Arena,*Probe.Location.ToString(),Data->GetConfig().AgentRadius);
    }
    UE_LOG(LogFPSDemo,Display,TEXT("NAV_AUTHOR_SUCCESS"));
    return true;
}

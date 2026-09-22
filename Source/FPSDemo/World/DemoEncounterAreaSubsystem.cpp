#include "World/DemoEncounterAreaSubsystem.h"
#include "Debug/DemoLog.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"

bool UDemoEncounterAreaSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{ DEMO_LOG_CALL(); return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }
void UDemoEncounterAreaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{ DEMO_LOG_CALL(); Super::Initialize(Collection); }
void UDemoEncounterAreaSubsystem::Deinitialize()
{ DEMO_LOG_CALL(); Areas.Empty(); AuthoredIndices.Empty(); GeneratedGeometry.Empty(); bAreasReady = false; Super::Deinitialize(); }
void UDemoEncounterAreaSubsystem::RegisterArea(ADemoEnemySpawnArea* Area)
{
    DEMO_LOG_CALL();
    if (IsValid(Area) && Area->GetWorld() == GetWorld()) { Areas.Add(Area); AuthoredIndices.Add(Area->ArenaIndex); }
    else UE_LOG(LogFPSDemo, Warning, TEXT("AREA_REGISTER rejected invalid/foreign World"));
}
void UDemoEncounterAreaSubsystem::UnregisterArea(ADemoEnemySpawnArea* Area)
{ DEMO_LOG_CALL(); if (Area) AuthoredIndices.Add(Area->ArenaIndex); Areas.Remove(Area); } // 保留已配置区号，流送卸载不能变成白模回退。
void UDemoEncounterAreaSubsystem::ClearGeneratedGeometry()
{
    DEMO_LOG_CALL();
    for (const TWeakObjectPtr<AActor>& Geometry : GeneratedGeometry) // 仅遍历本服务所有的回退几何。
        if (Geometry.IsValid()) Geometry->Destroy();
    GeneratedGeometry.Empty(); bAreasReady = false;
}
bool UDemoEncounterAreaSubsystem::IsReady() const { DEMO_LOG_TICK(); return bAreasReady; }
FVector UDemoEncounterAreaSubsystem::GetAreaCenter(int32 Index) const
{
    DEMO_LOG_TICK();
    FDemoAreaSnapshot Snapshot; FString Error; // 兼容接口只返回中心，正式入场必须检查ResolveArea结果。
    return ResolveArea(Index, Snapshot, Error) ? Snapshot.Transform.GetLocation() : FVector(Index * 6000.f, 0, 10000.f);
}
bool UDemoEncounterAreaSubsystem::PrepareAreas()
{
	DEMO_LOG_CALL();
    if (bAreasReady) return true;
    if (GetWorld()->GetNetMode() == NM_Client) { UE_LOG(LogFPSDemo, Warning, TEXT("AREA_PREPARE rejected client")); return false; }
    // 请求时重新解析注册表，先验证全部区号，防止生成一半才发现冲突。
    for (int32 Index = -1; Index < 3; ++Index) // 四个物理区域，不按逻辑关数扩容。
    {
        FDemoAreaSnapshot Snapshot; FString Error; // 当前区域值和明确失败原因。
        if (!ResolveArea(Index, Snapshot, Error)) return false;
    }
	// 白模地图在编辑器持有四个带区号标签的网格；完整匹配才跳过旧运行时地板/墙体。
	int32 AuthoredAreaCount = 0;
	// AreaIndex 与 GetAreaCenter 共用 -1..2 编号，防止只加载部分区域仍被当作完整地图。
	for (int32 AreaIndex = -1; AreaIndex < 3; ++AreaIndex)
	{
		// AreaTag 仅在本次查找中使用；每个区域最多计数一次。
		const FName AreaTag(*FString::Printf(TEXT("DemoArea%d"), AreaIndex));
		// It 只遍历当前 World 网格，引用由 World 持有，不缓存跨关卡指针。
		for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("DemoAuthoredBlockout")) && It->ActorHasTag(AreaTag) && It->GetStaticMeshComponent()->GetStaticMesh())
			{
				++AuthoredAreaCount;
				break;
			}
		}
	}
	if (AuthoredAreaCount != 0 && AuthoredAreaCount != 4)
	{
		UE_LOG(LogFPSDemo, Error, TEXT("Incomplete authored whitebox: expected 4 areas, found %d"), AuthoredAreaCount);
		bAreasReady = false;
		return false;
	}
	UE_LOG(LogFPSDemo, Log, TEXT("BuildAreas uses %s geometry"), AuthoredAreaCount == 4 ? TEXT("Blender authored") : TEXT("procedural fallback"));
	if (AuthoredAreaCount == 4) { bAreasReady = true; return true; }
	// 共享 Cube 资产由组件持有；构建时加载，打包时 Engine 基础形状同样可用。
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* FloorMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	// Area=-1 安全区、0..2 战斗区；每区一块地板和四面 5m 高边界。
	for (int32 Area = -1; AuthoredAreaCount == 0 && Area < 3; ++Area)
	{
		const FVector Center = GetAreaCenter(Area);
		// Piece=0 地板，1..4 四面墙；变换用厘米/基础 Cube 100cm 换算。
		for (int32 Piece = 0; Piece < 5; ++Piece)
		{
			FVector Offset(0,0,-50);
			FVector Scale(34,34,1);
			if (Piece == 1) { Offset = FVector(1700,0,250); Scale = FVector(1,35,5); }
			if (Piece == 2) { Offset = FVector(-1700,0,250); Scale = FVector(1,35,5); }
			if (Piece == 3) { Offset = FVector(0,1700,250); Scale = FVector(35,1,5); }
			if (Piece == 4) { Offset = FVector(0,-1700,250); Scale = FVector(35,1,5); }
			// 动态组件先设为 Movable 再赋网格，避免 BeginPlay 后设置 Static 网格被引擎拒绝。
			AStaticMeshActor* Geometry = GetWorld()->SpawnActor<AStaticMeshActor>(Center + Offset, FRotator::ZeroRotator);
			if (Geometry) GeneratedGeometry.Add(Geometry);
            if (!Geometry || !Cube) { UE_LOG(LogFPSDemo, Error, TEXT("Area geometry spawn failed")); ClearGeneratedGeometry(); return false; }
			Geometry->SetMobility(EComponentMobility::Movable);
			Geometry->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Geometry->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Geometry->SetActorScale3D(Scale);
			if (FloorMaterial) Geometry->GetStaticMeshComponent()->SetMaterial(0, FloorMaterial);
		}
	}
	// 地形与终端分开管理：地形整局复用，两个交互物只属于当前备战区域。
	bAreasReady = GeneratedGeometry.Num() == 20;
    return bAreasReady;
}
bool UDemoEncounterAreaSubsystem::ResolveArea(int32 Index, FDemoAreaSnapshot& OutArea, FString& Error, const FVector* FallbackCenter) const
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs arena=%d"), __FUNCTION__, Index);
    if (Index < -1 || Index > 2 || (FallbackCenter && FallbackCenter->ContainsNaN()))
    { Error = TEXT("刷怪区域World/编号/回退中心非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_AREA %s"), *Error); return false; }
    const ADemoEnemySpawnArea* Found = nullptr; // 仅同步解析借用，不把场景Actor跨关卡保存在计划中。
    for (const TWeakObjectPtr<ADemoEnemySpawnArea>& Entry : Areas) // 只遍历已注册锚点，销毁后弱引用无效。
        if (const ADemoEnemySpawnArea* Candidate = Entry.Get())
            if (Candidate->ArenaIndex == Index)
            {
                if (Found) { Error = TEXT("同一区号存在多个区域锚点"); UE_LOG(LogFPSDemo, Warning, TEXT("AREA_CONFLICT index=%d"), Index); return false; }
                Found = Candidate;
            }
    if (!Found && AuthoredIndices.Contains(Index))
    { Error = TEXT("区域锚点已卸载，请等待区域重新加载"); UE_LOG(LogFPSDemo, Log, TEXT("AREA_UNAVAILABLE index=%d"), Index); return false; }
    FTransform& OutTransform = OutArea.Transform; // 本次调用输出别名，不保存外部引用。
    FDemoSpawnGeometry& OutGeometry = OutArea.Geometry; // 所有生成服务读取同一份验证后的值。
    const FVector Center = FallbackCenter ? *FallbackCenter : FVector(Index * 6000.f, 0, 10000.f); // 未配置锚点时兼容原三战区布局。
    OutTransform = Found ? FTransform(Found->GetActorRotation(), Found->GetActorLocation()) : FTransform(Center);
    OutGeometry = Found ? Found->Geometry : FDemoSpawnGeometry();
    if (OutTransform.ContainsNaN() || OutGeometry.CampaignRadius.ContainsNaN() || OutGeometry.ReinforcementRadius.ContainsNaN()
        || OutGeometry.CampaignRadius.X <= 0 || OutGeometry.CampaignRadius.Y <= 0 || OutGeometry.ReinforcementRadius.X <= 0 || OutGeometry.ReinforcementRadius.Y <= 0
        || OutGeometry.CampaignOffset.ContainsNaN() || OutGeometry.BossOffset.ContainsNaN() || !FMath::IsFinite(OutGeometry.ReinforcementHeight))
    { Error = TEXT("刷怪区域半径/高度/变换非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_AREA %s"), *Error); return false; }
    UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_AREA arena=%d source=%s"), Index, Found ? *Found->GetName() : TEXT("default whitebox"));
    OutArea.CombatStart = OutTransform.TransformPosition(Found ? Found->CombatPlayerOffset : FVector(-1100, 0, 100));
    OutArea.PreparedStart = OutTransform.TransformPosition(Found ? Found->PreparedPlayerOffset : FVector(-650, 0, 100));
    OutArea.ShopAnchor = OutTransform.TransformPosition(Found ? Found->ShopOffset : FVector(0, -300, 0));
    OutArea.NextAnchor = OutTransform.TransformPosition(Found ? Found->NextOffset : FVector(0, 300, 0));
    if (OutArea.CombatStart.ContainsNaN() || OutArea.PreparedStart.ContainsNaN() || OutArea.ShopAnchor.ContainsNaN() || OutArea.NextAnchor.ContainsNaN())
    { Error = TEXT("区域玩家/终端位置非法"); UE_LOG(LogFPSDemo, Warning, TEXT("AREA_INVALID offsets index=%d"), Index); return false; }
    return true;
}

// 职责：UE原生动态关卡的有界生命周期与就绪检查。参考引擎同名实例/可见性协议，沿用既有Preparation做战斗事务，不引入第三方插件。
#include "World/Rooms/DemoRoomStreamingSubsystem.h"
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomAnchor.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h" // 失败诊断只读实际Recast分辨率参数/末端tile；不修改构建配置或寻路策略。
#include "Components/StaticMeshComponent.h" // 只读取真实阻挡网格的变换后Bounds，剔除编辑器图标/灯球显示数据。
#include "UObject/Package.h" // 显式出生几何冻结本ULevel的真实外包FName；不跨生命周期保留Level指针。
#include "Debug/DemoLog.h"

bool UDemoRoomStreamingSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{ DEMO_LOG_CALL(); return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

bool UDemoRoomStreamingSubsystem::BeginPrepare(const FDemoRoomStreamRequest& Request, FString& Error)
{
    DEMO_LOG_CALL(); Error.Reset();
    if (bStopping || !GetWorld() || GetWorld()->bIsTearingDown || !GetDefault<UDemoRoomStreamingSettings>()->ValidateRequest(Request, Error)) return false;
    if (Retiring && IsRemoved(Retiring)) { Retiring = nullptr; RetiringRenderRequest=FDemoRoomStreamRequest(); bRetiringWasCommitted=false; bRetireRequested = false; }
    if (Pending && PendingRequest.Generation == Request.Generation && PendingRequest.InstanceName == Request.InstanceName && PendingRequest.Map == Request.Map && PendingRequest.Transform.Equals(Request.Transform)) return true;
    if (Pending || Retiring || bReleasing || Request.Generation <= LastGeneration)
    {
        Error = TEXT("旧房间仍在回收或准备代数已过期，请稍后重试");
        if (Retiring || bReleasing) StartCleanupTicker();
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_BEGIN rejected pending=%d retiring=%d generation=%d"), !!Pending, !!Retiring, Request.Generation); return false;
    }
    FBox LocalBounds; // 可信安装包络，加载前先保守排除占地重叠；加载后再核真实组件，不以固定中心距离冒充安全。
    if (!GetDefault<UDemoRoomStreamingSettings>()->GetLocalBounds(Request.RoomId,LocalBounds,Error)) return false;
    const FBox ProposedBounds=LocalBounds.TransformBy(Request.Transform); // 统一世界厘米，单位缩放/旋转已由ValidateRequest限制。
    if (Active && (!ActiveCollisionBounds.IsValid || ProposedBounds.ExpandBy(250.f).IntersectXY(ActiveCollisionBounds)))
    { Error = TEXT("候选房间包络与当前真实占地重叠"); UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_BEGIN overlapping collision footprints")); return false; }
    LastGeneration = Request.Generation; // 包查找失败也消费名字，防迟到异步完成复用同一身份。
    bool bAccepted = false; // UE返回值只表示已加入加载列表，不代表地图可见或可开战。
    Pending = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(GetWorld(), TSoftObjectPtr<UWorld>(Request.Map), Request.Transform, bAccepted, Request.InstanceName.ToString());
    if (!bAccepted || !Pending) { Pending = nullptr; Error = TEXT("房间地图无法加载，请检查地图资产与Cook清单"); UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_BEGIN missing package room=%s"), *Request.RoomId.ToString()); return false; }
    PendingRequest = Request; bPreparedReady = false; PendingArea = FDemoAreaSnapshot(); PendingCollisionBounds=ProposedBounds;
    UE_LOG(LogFPSDemo, Log, TEXT("ROOM_BEGIN generation=%d room=%s instance=%s"), Request.Generation, *Request.RoomId.ToString(), *Request.InstanceName.ToString());
    return true;
}

EDemoRoomLoadStatus UDemoRoomStreamingSubsystem::InspectPrepared(ADemoRoomAnchor*& OutAnchor, FDemoAreaSnapshot& OutArea, FBox& OutCollisionBounds, FString& Error) const
{
    DEMO_LOG_TICK(); OutAnchor = nullptr; OutCollisionBounds=FBox(ForceInit); Error.Reset();
    if (!Pending || Pending->GetLevelStreamingState() == ELevelStreamingState::FailedToLoad) { Error = TEXT("房间加载失败或候选实例已取消"); return EDemoRoomLoadStatus::Failed; }
    if (!Pending->IsLevelVisible() || !Pending->GetLoadedLevel()) return EDemoRoomLoadStatus::Pending;
    const ULevel* Level = Pending->GetLoadedLevel(); // 仅扫描本实例，最多1024作者Actor，禁止每次遍历全World。
    const UDemoRoomStreamingSettings* Settings=GetDefault<UDemoRoomStreamingSettings>(); // 安装目录，只读而不信任请求端新增几何字段。
    const FDemoRoomStreamDefinition* Definition=Settings->Rooms.Find(PendingRequest.RoomId);
    FBox LocalBounds;
    if (!Definition || !Settings->GetLocalBounds(PendingRequest.RoomId,LocalBounds,Error)) return EDemoRoomLoadStatus::Failed;
    if (Level->Actors.Num() > 1024) { Error = TEXT("房间Actor超过1024项准备预算"); return EDemoRoomLoadStatus::Failed; }
    TMap<FName, int32> Matches; // 关键标签最多16个，唯一性不以第一次找到为准。
    for (FName Tag : PendingRequest.RequiredActorTags) Matches.Add(Tag, 0);
    TMap<FName,const AActor*> SpawnMarkers; // 只在当前同步检查借用本Level已BeginPlay作者点；有界8+1，顺序仍取目录数组。
    if (Definition->bUseAuthoredBounds)
    {
        for (FName Tag:Definition->SpawnPointTags) SpawnMarkers.Add(Tag,nullptr);
        SpawnMarkers.Add(Definition->BossSpawnTag,nullptr);
    }
    for (AActor* Actor : Level->Actors)
    {
        if (!IsValid(Actor)) continue;
        if (ADemoRoomAnchor* Anchor = Cast<ADemoRoomAnchor>(Actor))
        {
            if (OutAnchor) { Error = TEXT("一个房间只能有一个DemoRoomAnchor"); return EDemoRoomLoadStatus::Failed; }
            OutAnchor = Anchor;
        }
        for (auto& Entry : Matches) if (Actor->ActorHasTag(Entry.Key) && Actor->HasActorBegunPlay()) ++Entry.Value;
        for (auto& Entry:SpawnMarkers)
            if (Actor->ActorHasTag(Entry.Key) && Actor->HasActorBegunPlay())
            {
                if (Entry.Value) { Error=TEXT("本关精确出生标记重复"); return EDemoRoomLoadStatus::Failed; }
                Entry.Value=Actor;
            }
    }
    if (!OutAnchor || !OutAnchor->HasActorBegunPlay()) { Error = TEXT("房间锚点尚未就绪"); return EDemoRoomLoadStatus::Pending; }
    for (const auto& Entry : Matches)
    {
        if (Entry.Value > 1) { Error = TEXT("关键场景对象标签重复"); return EDemoRoomLoadStatus::Failed; }
        if (Entry.Value == 0) { Error = TEXT("关键场景对象尚未就绪"); return EDemoRoomLoadStatus::Pending; }
    }
    if (Definition->bUseAuthoredBounds && !OutAnchor->GetActorTransform().Equals(PendingRequest.Transform,.1f))
    { Error=TEXT("作者房间根变换必须与流送实例一致"); return EDemoRoomLoadStatus::Failed; }
    if (!OutAnchor->MakeSnapshot(OutArea,Error,Definition->bUseAuthoredBounds?&LocalBounds:nullptr)) return EDemoRoomLoadStatus::Failed;
    if (FVector::Dist(OutArea.Transform.GetLocation(), PendingRequest.Transform.GetLocation()) > 500.f)
    { Error = TEXT("房间根锚点偏离子关卡原点超过500cm"); return EDemoRoomLoadStatus::Failed; }
    if (GetWorld()->GetNetMode() == NM_Client && OutAnchor->PreparedGeneration != PendingRequest.Generation) return EDemoRoomLoadStatus::Pending;
    if (!ReadCollisionBounds(Level,OutCollisionBounds,Error)) return EDemoRoomLoadStatus::Failed;
    const FBox ActualLocal=OutCollisionBounds.TransformBy(PendingRequest.Transform.Inverse()); // 空间槽为单位轴向变换，AABB逆变换仍保守。
    if (!LocalBounds.ExpandBy(2.f).IsInsideOrOn(ActualLocal.Min) || !LocalBounds.ExpandBy(2.f).IsInsideOrOn(ActualLocal.Max))
    { Error=TEXT("本关实际碰撞几何超出已安装作者占地"); return EDemoRoomLoadStatus::Failed; }
    TArray<FVector> GroundPoints = { OutArea.ShopAnchor, OutArea.NextAnchor }; // 设施/控制点及四个玩家落点均需真实地板，不能被旧区碰撞冒充。
    GroundPoints.Append(OutArea.DeviceAnchors); GroundPoints.Append(OutArea.ControlAnchors);
    GroundPoints.Add(OutArea.PreparedStart); // 读档备战入口也必须有本图地面。
    OutArea.Geometry.PlayerEntryExclusionRadiusCm = Definition->PlayerEntryExclusionRadiusCm;
    OutArea.Geometry.ProtectedPlayerEntryPoints.Reset(); // 准备快照逐次重建；旧房和未启用的目录项不携带禁刷点。
    for (int32 Index = 0; Index < FDemoSpawnGeometry::CombatLandingSlotCount; ++Index)
    {
        const FVector LandingPoint = OutArea.GetCombatLandingPoint(Index); // 与队员实际传送共用的世界位置，全部通过下面的本关地板检查才会提交。
        GroundPoints.Add(LandingPoint);
        if (Definition->PlayerEntryExclusionRadiusCm > 0.f) OutArea.Geometry.ProtectedPlayerEntryPoints.Add(LandingPoint);
    }
    FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic); // 不以Pawn或弹丸当作地面。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoRoomGround), false); // 本实例静态地面验证，无客户端上传碰撞结果。
    for (const FVector& Point : GroundPoints)
    {
        FHitResult Floor; // 命中必须属于加载Level，空间重叠或旧平台不能使准备假通过。
        if (!GetWorld()->LineTraceSingleByObjectType(Floor, Point + FVector(0,0,150), Point - FVector(0,0,350), Objects, Query)
            || Floor.ImpactNormal.Z < .7f || !Floor.GetActor() || Floor.GetActor()->GetLevel() != Level)
        { Error = TEXT("房间入场或任务锚点缺少本关可站立地面"); return EDemoRoomLoadStatus::Pending; }
    }
    if (Definition->bUseAuthoredBounds)
    {
        OutArea.Geometry.ExplicitSpawnOffsets.Reset(); // 标记是作者Actor原点，实际生成保存本关地面接触；不套旧默认椭圆。
        OutArea.Geometry.ExplicitSourceLevelPackage=Level->GetOutermost()->GetFName(); // 后续局部散布只能命中同一已验证实例；客户端和服务器各冻结本端名字，不在RPC传UObject。
        TArray<FName> Ordered=Definition->SpawnPointTags; Ordered.Add(Definition->BossSpawnTag); // 最后一项单独作为Boss；最多9次射线。
        for (int32 Index=0;Index<Ordered.Num();++Index)
        {
            const AActor* Marker=SpawnMarkers.FindChecked(Ordered[Index]); // 仅该Level精确匹配，缺BeginPlay仍等待原准备deadline。
            if (!Marker) { Error=TEXT("等待本关精确出生标记就绪"); return EDemoRoomLoadStatus::Pending; }
            const FVector Point=Marker->GetActorLocation();
            if (!LocalBounds.ExpandBy(2.f).IsInsideOrOn(PendingRequest.Transform.InverseTransformPosition(Point)))
            { Error=TEXT("本关出生标记超出作者占地"); return EDemoRoomLoadStatus::Failed; }
            FHitResult Floor; // 从作者100cm抬高原点向下近测，避免上方另一层被150cm额外上探错误选中。
            if (!GetWorld()->LineTraceSingleByObjectType(Floor,Point+FVector(0,0,20),Point-FVector(0,0,350),Objects,Query)
                || Floor.ImpactNormal.Z<.7f || !Floor.GetActor() || Floor.GetActor()->GetLevel()!=Level)
            { Error=TEXT("本关出生标记缺少所属关卡可站地面"); return EDemoRoomLoadStatus::Pending; }
            const FVector LocalFloor=OutArea.Transform.InverseTransformPosition(Floor.ImpactPoint); // 当前静态地面的真实高度；实际球心按冻结敌人体型抬高。
            if (Index<Definition->SpawnPointTags.Num()) OutArea.Geometry.ExplicitSpawnOffsets.Add(LocalFloor);
            else OutArea.Geometry.BossOffset=LocalFloor;
        }
    }
    return EDemoRoomLoadStatus::Ready;
}

bool UDemoRoomStreamingSubsystem::ReadCollisionBounds(const ULevel* Level,FBox& OutBounds,FString& Error) const
{
    DEMO_LOG_TICK(); OutBounds=FBox(ForceInit);
    if (!Level || Level->Actors.Num()>1024) { Error=TEXT("碰撞占地Level缺失或超过作者预算"); return false; }
    for (const AActor* Actor:Level->Actors)
    {
        if (!IsValid(Actor)) continue;
        TInlineComponentArray<UStaticMeshComponent*> Meshes; Actor->GetComponents(Meshes); // 同步只读原生组件，不扫描World或创建显示代理。
        for (const UStaticMeshComponent* Mesh:Meshes)
            if (IsValid(Mesh) && Mesh->IsRegistered() && Mesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision && Mesh->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block)
            {
                const FBox Box=Mesh->Bounds.GetBox(); // 已完成组件变换的真实渲染几何包络；保守用于隔离/回收，不宣称凸包/门洞通行验收。
                if (!Box.IsValid || Box.Min.ContainsNaN() || Box.Max.ContainsNaN()) { Error=TEXT("本关阻挡网格bounds非法"); return false; }
                OutBounds+=Box;
            }
    }
    if (!OutBounds.IsValid) { Error=TEXT("本关缺少已注册的阻挡地面几何"); return false; }
    return true;
}

bool UDemoRoomStreamingSubsystem::IsNavigationReady(const FDemoAreaSnapshot& Area, FString& Error) const
{
    DEMO_LOG_TICK();
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); // 服务器自己的AI导航，不信任客户端导航。
    const ANavigationData* Data = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
    if (!Data) { Error=FString::Printf(TEXT("等待服务器导航数据 pointIndex=-1 source=%s projectedOrigin=Unavailable destination=Unavailable pathResult=NotQueried partial=Unknown"),*Area.CombatStart.ToString()); return false; }
    FNavLocation Origin; // 玩家入场地面为连通路径起点。
    const FVector Extent(200, 200, 250); // 与项目敌人导航近似的局部投影范围，禁止吸附到远处旧房间。
    if (!Nav->ProjectPointToNavigation(Area.CombatStart, Origin, Extent, Data))
    { Error=FString::Printf(TEXT("等待入场区域导航构建 pointIndex=-1 source=%s projectedOrigin=Failed destination=Unavailable pathResult=NotQueried partial=Unknown"),*Area.CombatStart.ToString()); return false; }
    TArray<FVector> Points = Area.ControlAnchors; // 控制、设施与敌人/Boss出生必须通达；不强制全局导航系统完全空闲。
    Points.Append(Area.DeviceAnchors); Points.Add(Area.Transform.TransformPosition(Area.Geometry.BossOffset));
    if (!Area.Geometry.ExplicitSpawnOffsets.IsEmpty())
    {
        for (const FVector& Point:Area.Geometry.ExplicitSpawnOffsets) Points.Add(Area.Transform.TransformPosition(Point)); // 有高差的显式地面点，不制造建筑外椭圆。
    }
    else for (int32 Index = 0; Index < 8; ++Index)
    {
        const float Angle = Index * PI / 4.f; // 有界八方向代表普通刷怪椭圆，具体出生仍由原SpawnSystem逐个验证。
        Points.Add(Area.Transform.TransformPosition(Area.Geometry.CampaignOffset + FVector(FMath::Cos(Angle) * Area.Geometry.CampaignRadius.X, FMath::Sin(Angle) * Area.Geometry.CampaignRadius.Y, 0)));
    }
    for (int32 PointIndex=0;PointIndex<Points.Num();++PointIndex)
    {
        const FVector& Point=Points[PointIndex]; // 保留原顺序：全部Control、全部Device、Boss、八个Spawn/旧椭圆点，不重排或移动实际输入。
        const int32 DeviceStart=Area.ControlAnchors.Num(),BossIndex=DeviceStart+Area.DeviceAnchors.Num(); // 仅诊断下标域；不改变目标数组或导航策略。
        const FString PointKind=PointIndex<DeviceStart?FString::Printf(TEXT("Control[%d]"),PointIndex):PointIndex<BossIndex?FString::Printf(TEXT("Device[%d]"),PointIndex-DeviceStart):PointIndex==BossIndex?TEXT("Boss[0]"):FString::Printf(TEXT("Spawn[%d]"),PointIndex-BossIndex-1);
        FNavLocation Destination; // 局部投影，若在不可达岛上不能以partial path通过。
        if (!Nav->ProjectPointToNavigation(Point, Destination, Extent, Data))
        { Error=FString::Printf(TEXT("等待敌人或任务区域导航构建 pointIndex=%d kind=%s source=%s projectedOrigin=%s destination=Failed pathResult=NotQueried partial=Unknown"),PointIndex,*PointKind,*Point.ToString(),*Origin.Location.ToString()); return false; }
        FPathFindingQuery Query(this, *Data, Origin.Location, Destination.Location); Query.SetAllowPartialPaths(false); // 仅准备期间有限查询，不成为常驻寻路器。
        const FPathFindingResult Path = Nav->FindPathSync(Query);
        if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial())
        {
            const int32 PathPoints=Path.Path.IsValid()?Path.Path->GetPathPoints().Num():0; // 实际返回路径，不拿目标点代替最后可达点。
            const FVector LastPoint=PathPoints>0?Path.Path->GetPathPoints().Last().Location:FVector::ZeroVector;
            const double PathLength=Path.Path.IsValid()?Path.Path->GetLength():-1.; // 原生已返回路径的累计长度cm，未知为-1，不额外发起查询。
            FString NavInfo=FString::Printf(TEXT(" navData=%s agentRadius=%.2f agentHeight=%.2f agentStep=%.2f"),*Data->GetName(),Data->GetConfig().AgentRadius,Data->GetConfig().AgentHeight,Data->GetConfig().AgentStepHeight);
            if (const ARecastNavMesh* Recast=Cast<ARecastNavMesh>(Data))
            {
                for (uint8 ResolutionIndex=0;ResolutionIndex<uint8(ENavigationDataResolution::MAX);++ResolutionIndex)
                {
                    const ENavigationDataResolution Resolution=ENavigationDataResolution(ResolutionIndex); // 真实三档0=Low/1=Default/2=High；不读取已弃用旧CellSize字段猜实际值。
                    NavInfo+=FString::Printf(TEXT(" res[%d]={cell=%.2f,height=%.2f,step=%.2f}"),int32(ResolutionIndex),Recast->GetCellSize(Resolution),Recast->GetCellHeight(Resolution),Recast->GetAgentMaxStepHeight(Resolution));
                }
                uint32 PolyIndex=0,TileIndex=0; // 末端真实NodeRef的公开解码输出，仅用于定位断点所在tile。
                ENavigationDataResolution TileResolution=ENavigationDataResolution::Invalid;
                if (PathPoints>0 && Recast->GetPolyTileIndex(Path.Path->GetPathPoints().Last().NodeRef,PolyIndex,TileIndex)
                    && TileIndex<=uint32(MAX_int32) && Recast->GetNavmeshTileResolution(int32(TileIndex),TileResolution))
                    NavInfo+=FString::Printf(TEXT(" lastTile=%u lastTileResolution=%d"),TileIndex,int32(TileResolution));
                else NavInfo+=TEXT(" lastTileResolution=Unavailable");
            }
            else NavInfo+=TEXT(" recast=Unavailable");
            Error=FString::Printf(TEXT("房间入场与任务/出生点导航未连通 pointIndex=%d kind=%s source=%s projectedOrigin=%s destination=%s pathResult=%d pathValid=%d partial=%d pathPoints=%d pathLast=%s pathLengthCm=%.2f%s"),
                PointIndex,*PointKind,*Point.ToString(),*Origin.Location.ToString(),*Destination.Location.ToString(),int32(Path.Result),int32(Path.Path.IsValid()),Path.Path.IsValid()?int32(Path.Path->IsPartial()):-1,PathPoints,*LastPoint.ToString(),PathLength,*NavInfo); // 仅扩展失败理由；原门限、完整路径判定、准备deadline和输入点保持。
            return false;
        }
    }
    return true;
}

EDemoRoomLoadStatus UDemoRoomStreamingSubsystem::PollPrepared(FDemoAreaSnapshot& OutArea, FString& Error)
{
    DEMO_LOG_TICK();
    if (bStopping || !GetWorld() || GetWorld()->bIsTearingDown) { Error = TEXT("World正在退出"); return EDemoRoomLoadStatus::Failed; }
    if (bPreparedReady && Pending && Pending->IsLevelVisible()) { OutArea = PendingArea; return EDemoRoomLoadStatus::Ready; }
    ADemoRoomAnchor* Anchor = nullptr; FDemoAreaSnapshot Candidate; FBox CollisionBounds; // 当前同步检查，仅通过后缓存值，不保留裸Actor引用。
    const EDemoRoomLoadStatus Status = InspectPrepared(Anchor,Candidate,CollisionBounds,Error);
    if (Status != EDemoRoomLoadStatus::Ready)
    {
        if (!Error.IsEmpty()) Error=FString::Printf(TEXT("[%s] %s"),*PendingRequest.RoomId.ToString(),*Error); // 真实失败/等待状态都带内容身份，便于定位十图中的具体一关。
        if (Status==EDemoRoomLoadStatus::Failed) UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_PREPARE_REJECT room=%s reason=%s"),*PendingRequest.RoomId.ToString(),*Error); // 失败交上层取消；等待原因仅由原准备状态变化发布，避免逐帧刷屏。
        return Status;
    }
    if (GetWorld()->GetNetMode() != NM_Client)
    {
        if (PendingRequest.bRequireNavigation && !IsNavigationReady(Candidate, Error))
        { Error=FString::Printf(TEXT("[%s] %s"),*PendingRequest.RoomId.ToString(),*Error); return EDemoRoomLoadStatus::Pending; } // 等待不跳过导航，不把缺路误报为地图加载成功。
        Anchor->SetPreparedGeneration(PendingRequest.Generation);
    }
    PendingArea = Candidate; PendingCollisionBounds=CollisionBounds; bPreparedReady = true; OutArea = Candidate;
    UE_LOG(LogFPSDemo, Log, TEXT("ROOM_READY generation=%d client=%d"), PendingRequest.Generation, GetWorld()->GetNetMode() == NM_Client);
    return EDemoRoomLoadStatus::Ready;
}

ULevel* UDemoRoomStreamingSubsystem::GetPreparedLevel() const { DEMO_LOG_TICK(); return Pending ? Pending->GetLoadedLevel() : nullptr; }
bool UDemoRoomStreamingSubsystem::IsConnectionReady(APlayerController* Controller) const
{
    DEMO_LOG_TICK();
    if (!IsValid(Controller) || !bPreparedReady || !Pending || !Pending->IsLevelVisible()) return false;
    if (Controller->IsLocalController()) return true;
    const UNetConnection* Connection = Controller->GetNetConnection(); // 引擎visibility ACK，独立于可伪报的业务RPC。
    return Connection && Connection->ClientHasInitializedLevel(Pending->GetLoadedLevel());
}

bool UDemoRoomStreamingSubsystem::CommitPrepared()
{
    DEMO_LOG_CALL();
    if (!bPreparedReady || !Pending || !Pending->IsLevelVisible() || Retiring || bReleasing) { UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_COMMIT rejected state")); return false; }
    if (Active) { Retiring = Active; RetiringRenderRequest=ActiveRequest; bRetiringWasCommitted=true; RetiringBounds=ActiveCollisionBounds; bRetireRequested = false; }
    Active = Pending; ActiveRequest = PendingRequest; ActiveArea = PendingArea; ActiveCollisionBounds=PendingCollisionBounds; Pending = nullptr; PendingRequest = FDemoRoomStreamRequest(); PendingCollisionBounds=FBox(ForceInit); bPreparedReady = false;
    if (Retiring) StartCleanupTicker();
    UE_LOG(LogFPSDemo, Log, TEXT("ROOM_COMMIT generation=%d room=%s"), ActiveRequest.Generation, *ActiveRequest.RoomId.ToString()); return true;
}

void UDemoRoomStreamingSubsystem::CancelPrepared()
{
    DEMO_LOG_CALL();
    if (!Pending) return;
    Retiring = Pending; RetiringRenderRequest=PendingRequest; bRetiringWasCommitted=false; RetiringBounds=PendingCollisionBounds;
    if (const ULevel* Level=Pending->GetLoadedLevel()) { FBox Actual; FString BoundsError; if (ReadCollisionBounds(Level,Actual,BoundsError)) RetiringBounds=Actual; } // 失败地图亦保护实际已加载地面，不按原预计包络卸除。
    Pending = nullptr; PendingRequest = FDemoRoomStreamRequest(); PendingCollisionBounds=FBox(ForceInit); bPreparedReady = false; bRetireRequested = false;
    StartCleanupTicker(); // 当前Active不受影响，候选真正从World移除后才允许下一次加载。
}

bool UDemoRoomStreamingSubsystem::HasActiveRoom() const { DEMO_LOG_TICK(); return Active != nullptr; }
bool UDemoRoomStreamingSubsystem::GetRoomRenderContext(const ULevel* Level, FName& OutRoomId, FTransform& OutTransform, bool& bOutCommitted) const
{
    DEMO_LOG_TICK(); OutRoomId=NAME_None; OutTransform=FTransform::Identity; bOutCommitted=false;
    // BeginPlay时visibility可能尚未提交，但真实LoadedLevel已存在；只匹配服务拥有的指针，不猜PIE包名。
    if (!Level || bStopping) return false;
    if (Active && Active->GetLoadedLevel()==Level)
    { OutRoomId=ActiveRequest.RoomId; OutTransform=ActiveRequest.Transform; bOutCommitted=true; return true; }
    if (Pending && Pending->GetLoadedLevel()==Level)
    { OutRoomId=PendingRequest.RoomId; OutTransform=PendingRequest.Transform; return true; }
    if (Retiring && Retiring->GetLoadedLevel()==Level)
    { OutRoomId=RetiringRenderRequest.RoomId; OutTransform=RetiringRenderRequest.Transform; bOutCommitted=bRetiringWasCommitted; return true; }
    return false;
}
bool UDemoRoomStreamingSubsystem::GetActiveArea(int32 ArenaIndex, FDemoAreaSnapshot& OutArea, FString& Error) const
{
    DEMO_LOG_TICK(); Error.Reset();
    if (!Active || ActiveRequest.ArenaIndex != ArenaIndex) return false;
    if (!Active->IsLevelVisible() || !Active->GetLoadedLevel()) { Error = TEXT("当前战斗房间意外卸载，不能回退旧区"); return false; }
    OutArea = ActiveArea; return true;
}

bool UDemoRoomStreamingSubsystem::CanRetirePrevious() const
{
    DEMO_LOG_TICK();
    if (!Retiring) return true;
    if (!RetiringBounds.IsValid) return false; // 未读到占地时保守保留，不能猜测Pawn已经传送。
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Player = It->Get(); // 客户端只关心自己的视口，服务器关注所有仍连接玩家。
        if (!Player || (GetWorld()->GetNetMode() == NM_Client && !Player->IsLocalController())) continue;
        const APawn* Pawn = Player->GetPawn(); // RPC先到/换Avatar时保留地面，绝不猜测已传送。
        if (!Pawn || RetiringBounds.ExpandBy(250.f).IsInsideXY(Pawn->GetActorLocation())) return false; // 实际XY盒+胶囊/网络迟到余量，长走廊远端同样受保护。
    }
    // 仅已提交PhaseA图额外等本机相机退出实际PP混合范围，避免Pawn已移走但观战/视图晚到先撤旧Look。
    // Anchor对legacy恒false，原Pawn/地面规则及30秒有界超时保持；不把视觉资格当战斗提交。
    if (bRetiringWasCommitted && Retiring->GetLoadedLevel())
        for (const AActor* Actor:Retiring->GetLoadedLevel()->Actors)
            if (const ADemoRoomAnchor* Anchor=Cast<ADemoRoomAnchor>(Actor))
                if (Anchor->HasLocalAtmosphereView()) return false;
    return true;
}

void UDemoRoomStreamingSubsystem::RequestUnload(ULevelStreamingDynamic* Instance)
{
    DEMO_LOG_CALL();
    if (!Instance) return;
    Instance->SetShouldBeVisible(false); Instance->SetShouldBeLoaded(false); Instance->SetIsRequestingUnloadAndRemoval(true); // 非阻塞，不Flush、不强制全局GC。
}
bool UDemoRoomStreamingSubsystem::IsRemoved(const ULevelStreamingDynamic* Instance) const
{ DEMO_LOG_TICK(); return !Instance || (!Instance->GetLoadedLevel() && !GetWorld()->GetStreamingLevels().Contains(Instance)); }

void UDemoRoomStreamingSubsystem::StartCleanupTicker()
{
    DEMO_LOG_CALL();
    if (bStopping || !GetWorld() || GetWorld()->bIsTearingDown || CleanupTicker.IsValid()) return;
    CleanupDeadline = FPlatformTime::Seconds() + 30.;
    CleanupTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDemoRoomStreamingSubsystem::PollCleanup), .1f); // 游戏线程弱UObject回调，Deinitialize精确移除。
}

bool UDemoRoomStreamingSubsystem::PollCleanup(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (bStopping || !GetWorld() || GetWorld()->bIsTearingDown) { CleanupTicker.Reset(); return false; }
    if (Retiring && !bRetireRequested && CanRetirePrevious()) { RequestUnload(Retiring); bRetireRequested = true; }
    if (Retiring && IsRemoved(Retiring)) { Retiring = nullptr; RetiringRenderRequest=FDemoRoomStreamRequest(); bRetiringWasCommitted=false; bRetireRequested = false; }
    if (bReleasing && !Retiring && Active)
    { Retiring = Active; RetiringRenderRequest=ActiveRequest; bRetiringWasCommitted=true; RetiringBounds=ActiveCollisionBounds; Active = nullptr; ActiveRequest = FDemoRoomStreamRequest(); ActiveCollisionBounds=FBox(ForceInit); bRetireRequested = false; }
    if (!Retiring && (!bReleasing || !Active)) { bReleasing = false; CleanupTicker.Reset(); return false; }
    if (FPlatformTime::Seconds() >= CleanupDeadline)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_RETIRE timeout; retaining bounded room until safe/retry, no forced ground removal"));
        CleanupTicker.Reset(); return false; // 保留至多两实例；下一次出发会明确拒绝并重新观察，不无限扩容或永久10Hz等待。
    }
    return true;
}

void UDemoRoomStreamingSubsystem::ReleaseRooms()
{
    DEMO_LOG_CALL();
    if (Pending) CancelPrepared();
    bReleasing = true; StartCleanupTicker(); // 先等候选/旧图，再轮到Active；客户端位置复制迟到时不卸脚下地面。
}

void UDemoRoomStreamingSubsystem::Deinitialize()
{
    DEMO_LOG_CALL(); bStopping = true;
    if (CleanupTicker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(CleanupTicker); CleanupTicker.Reset(); }
    Pending = nullptr; Active = nullptr; Retiring = nullptr; PendingRequest = FDemoRoomStreamRequest(); ActiveRequest = FDemoRoomStreamRequest(); // World负责拆关卡，不在teardown网络栈发RPC或同步流送。
    PendingCollisionBounds=FBox(ForceInit); ActiveCollisionBounds=FBox(ForceInit); RetiringBounds=FBox(ForceInit);
    RetiringRenderRequest=FDemoRoomStreamRequest(); bRetiringWasCommitted=false; // 仅清本地视觉事实，不发RPC。
    Super::Deinitialize();
}

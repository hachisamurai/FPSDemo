// 职责：权威World执行冻结的分区、普通和Boss子单位出生/清理；冻结预算、真实碰撞与目标短时位置门禁，保护仅延后不消费计划；死亡事件只由已登记敌人触发。
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Game/Objectives/DemoObjectiveCoordinatorComponent.h" // 同Owner只读出生门禁，不让目标持有生成队列或改变预算。
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "EngineUtils.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyTargeting.h" // 复用服务器参战资格、Avatar和存活判定。
#include "AI/DemoEnemyNavigation.h"
#include "AI/DemoEnemyRoster.h"

#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Level.h" // 出生重射线核命中所属ULevel，不能由别的层或持久场景碰撞冒充准备地面。
#include "UObject/Package.h" // 纯值比较准备时来源包，不保留地图Actor/Level的强引用。
#include "Debug/DemoLog.h"

UDemoEnemySpawnComponent::UDemoEnemySpawnComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
int32 UDemoEnemySpawnComponent::GetRemaining() const { DEMO_LOG_TICK(); return Alive.Num() + PendingCount + ControlPendingCount + SupportRequests.Num(); }
int32 UDemoEnemySpawnComponent::GetAliveCount() const { DEMO_LOG_TICK(); return Alive.Num(); }
bool UDemoEnemySpawnComponent::IsEncounterComplete() const { DEMO_LOG_TICK(); return bComplete && GetRemaining() == 0; }
bool UDemoEnemySpawnComponent::StartEncounter(const FDemoSpawnPlan& Plan, const FTransform& AreaTransform, const FDemoSpawnGeometry& Geometry, FString& Error)
{
    DEMO_LOG_CALL();
    if (!DemoSpawnPlan::ValidateSettings(Settings, Error)) return false;
    if (!GetOwner()||!GetOwner()->HasAuthority()||bRunning||!Alive.IsEmpty()||AreaTransform.ContainsNaN()||!Plan.Validate(Error))
    { if (Error.IsEmpty()) Error=TEXT("刷怪启动被拒绝：权限、旧计划或几何非法"); UE_LOG(LogFPSDemo,Warning,TEXT("SPAWN_START %s"),*Error); return false; }
    if (Geometry.ExplicitSpawnOffsets.Num()>64 || Geometry.BossOffset.ContainsNaN())
    { Error=TEXT("显式刷怪几何超过预算或Boss点非法"); UE_LOG(LogFPSDemo,Warning,TEXT("SPAWN_START %s"),*Error); return false; }
    if (!Geometry.ExplicitSpawnOffsets.IsEmpty() && Geometry.ExplicitSourceLevelPackage.IsNone())
    { Error=TEXT("显式刷怪点缺少准备关卡来源"); UE_LOG(LogFPSDemo,Warning,TEXT("SPAWN_START %s"),*Error); return false; } // 旧空数组仍完整沿用原椭圆；新模式不能降级为任意World地面。
    for (const FVector& Point:Geometry.ExplicitSpawnOffsets) // 当前准备的有限floor-contact，不接受NaN或公里外任意生成点。
        if (Point.ContainsNaN() || Point.GetAbsMax()>100000.)
        { Error=TEXT("显式刷怪点非法或超过1000m预算"); UE_LOG(LogFPSDemo,Warning,TEXT("SPAWN_START %s"),*Error); return false; }
    const int32 ExpectedEntryPoints = Geometry.PlayerEntryExclusionRadiusCm > 0.f ? FDemoSpawnGeometry::CombatLandingSlotCount : 0; // 开启时必须同时保护所有可能的队员落点。
    if (!FMath::IsFinite(Geometry.PlayerEntryExclusionRadiusCm) || Geometry.PlayerEntryExclusionRadiusCm < 0.f || Geometry.PlayerEntryExclusionRadiusCm > 5000.f
        || Geometry.ProtectedPlayerEntryPoints.Num() != ExpectedEntryPoints)
    { Error=TEXT("玩家入场禁刷半径或队员落点数非法"); UE_LOG(LogFPSDemo,Warning,TEXT("SPAWN_START %s"),*Error); return false; }
    for (const FVector& Entry : Geometry.ProtectedPlayerEntryPoints)
        if (Entry.ContainsNaN())
        { Error=TEXT("玩家入场禁刷落点缺少有限世界坐标"); UE_LOG(LogFPSDemo,Warning,TEXT("SPAWN_START %s"),*Error); return false; }
    // 启动前验证分区实际点表及固定安全区，避免缺少标记时误走全房候选或旧椭圆。
    for (const FDemoRegionalSpawnZone& Zone : Plan.RegionalZones)
    {
        if (Geometry.PlayerEntryExclusionRadiusCm < 1200.f || Zone.SpawnPointIndices.Num() != 2)
        {
            Error = FString::Printf(TEXT("分区 %s 需要两个标记和四个至少12米的入场保护区"), *Zone.Id.ToString());
            UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_START %s"), *Error);
            return false;
        }
        for (const int32 Point : Zone.SpawnPointIndices)
        {
            if (!Geometry.ExplicitSpawnOffsets.IsValidIndex(Point) ||
                !FBox2D(Zone.LocalMin, Zone.LocalMax).IsInsideOrOn(FVector2D(Geometry.ExplicitSpawnOffsets[Point])) ||
                !Geometry.IsOutsidePlayerEntryZone(AreaTransform.TransformPosition(Geometry.ExplicitSpawnOffsets[Point])))
            {
                Error = FString::Printf(TEXT("分区 %s 标记 %d 缺失、越出本区或落在固定入场禁区"), *Zone.Id.ToString(), Point);
                UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_START %s"), *Error);
                return false;
            }
        }
    }
    ActivePlan=Plan; FrozenSettings=Settings; SpawnTransform=AreaTransform; SpawnGeometry=Geometry;
    ObjectiveGate=GetOwner()->FindComponentByClass<UDemoObjectiveCoordinatorComponent>(); // 准备期绑定一次，不在采样/TrySpawn反查GameMode。
    if (Plan.bControlReinforcements && (!ObjectiveGate.IsValid() || !ObjectiveGate->IsControlReinforcementActive(Plan.RunId, Plan.Level)))
    { Error=TEXT("控制增援缺少同场已激活控制目标"); UE_LOG(LogFPSDemo,Warning,TEXT("SPAWN_START %s"),*Error); return false; }
    FrozenClasses.Reset(); FrozenBodies.Reset(); FrozenDefinitions.Reset();
    for (const FDemoSpawnGroup& Group:ActivePlan.Groups) // 显式强引用小数量组资产，执行过程不再读取可变Settings模板。
    { FrozenClasses.Add(Group.EnemyClass); FrozenBodies.Add(Group.Body); FrozenDefinitions.Add(Group.Stats.Definition); }
    ++Generation; PendingCount=Plan.GetTotalCount(); NextSpawn=BlockedPasses=0; bComplete=false; bRunning=true;
    RegionalProgress.Reset();
    RegionalProgress.SetNum(Plan.RegionalZones.Num());
    NextRegionalZone = 0;
    NextRegionalWaveTime = 0.;
    bControlReinforcementsEnabled=Plan.bControlReinforcements; ControlPendingCount=ControlSelectionCursor=ControlFormationSlot=0;
    NextControlReinforcementTime=GetWorld()->GetTimeSeconds()+Plan.ControlReinforcementInterval;
    UE_LOG(LogFPSDemo,Log,TEXT("SPAWN_START run=%s level=%d count=%d groups=%d maxAlive=%d generation=%llu"),*Plan.RunId,Plan.Level,PendingCount,Plan.Groups.Num(),Plan.MaxAlive,Generation);
    // 先安装时钟再首次填充；填充耗尽队列时能够真正撤销它，避免遗留空转Timer。
    GetWorld()->GetTimerManager().SetTimer(FillTimer, this, &UDemoEnemySpawnComponent::Fill, FrozenSettings.Interval, true);
    const uint64 StartedGeneration = Generation; // HUD/Flow回调可同步撤销本场或启动下一场。
    OnRemainingChanged.Broadcast(GetRemaining());
    if (bRunning && Generation == StartedGeneration) Fill();
    return true;
}
bool UDemoEnemySpawnComponent::IsPlayerDistanceSafe(const FVector& Location) const
{
    DEMO_LOG_TICK();
    if (!SpawnGeometry.IsOutsidePlayerEntryZone(Location)) return false; // 玩家走远后固定入口仍受保护，首发、补员和构造后复核均走本门禁。
    for (TActorIterator<ADemoCharacter> It(GetWorld());It;++It) // 当前所有活参战Pawn；阵亡观战者不挡出生。
        if (It->GetController()&&It->GetDemoAttributes()&&It->GetDemoAttributes()->GetHealth()>0.f&&FVector::Dist2D(Location,It->GetActorLocation())<FrozenSettings.MinPlayerDistance) return false;
    return true;
}
bool UDemoEnemySpawnComponent::IsControlReinforcementOpen() const
{
    DEMO_LOG_TICK();
    return bRunning && bControlReinforcementsEnabled && ActivePlan.bControlReinforcements && ObjectiveGate.IsValid()
        && ObjectiveGate->IsControlReinforcementActive(ActivePlan.RunId,ActivePlan.Level);
}
void UDemoEnemySpawnComponent::QueueControlReinforcements()
{
    DEMO_LOG_TICK();
    if (!bControlReinforcementsEnabled) return;
    if (!IsControlReinforcementOpen()) { StopControlReinforcements(); return; }
    const double Now=GetWorld()->GetTimeSeconds(); // 与占点同用游戏秒，暂停和大帧不会补算历史波次。
    if (Now<NextControlReinforcementTime) return;
    NextControlReinforcementTime=Now+ActivePlan.ControlReinforcementInterval;
    if (PendingCount>0 || ControlPendingCount>0 || Alive.Num()>=ActivePlan.MaxAlive)
    { UE_LOG(LogFPSDemo,Log,TEXT("CONTROL_REINFORCEMENT_DEFER initial=%d batch=%d alive=%d cap=%d"),PendingCount,ControlPendingCount,Alive.Num(),ActivePlan.MaxAlive); return; }
    ControlPendingCount=FMath::Min(ActivePlan.ControlReinforcementBatch,ActivePlan.MaxAlive-Alive.Num()); // 只建立当前空位内单批；不预排无限总量，不抢首发预算。
    UE_LOG(LogFPSDemo,Log,TEXT("CONTROL_REINFORCEMENT_QUEUE batch=%d alive=%d cap=%d next=%.2f"),ControlPendingCount,Alive.Num(),ActivePlan.MaxAlive,NextControlReinforcementTime);
}
void UDemoEnemySpawnComponent::StopControlReinforcements()
{
    DEMO_LOG_CALL();
    if (!ActivePlan.bControlReinforcements) return; // 旧有限任务不能因共享Flow通知而改变原清场/计时语义。
    const int32 Discarded=ControlPendingCount; // 未生成项不产生Enemy、击杀或奖励；现有Alive仍由原会话取消链负责。
    bControlReinforcementsEnabled=false; ControlPendingCount=0; NextControlReinforcementTime=0.;
    if (GetWorld() && PendingCount==0 && SupportRequests.IsEmpty()) GetWorld()->GetTimerManager().ClearTimer(FillTimer);
    OnRemainingChanged.Broadcast(GetRemaining());
    UE_LOG(LogFPSDemo,Log,TEXT("CONTROL_REINFORCEMENT_STOP discarded=%d alive=%d"),Discarded,Alive.Num());
}
ADemoEnemy* UDemoEnemySpawnComponent::TrySpawn(int32 Ordinal,int32 SupportGroup,ADemoEnemy* SupportBoss,bool bControlReinforcement,int32 RegionalIndex)
{
    DEMO_LOG_CALL();
    const uint64 AttemptGeneration=Generation; // Deferred/Blueprint BeginPlay会同步触发外部逻辑，提交前必须验证计划未变。
    int32 FormationSlot=bControlReinforcement?Ordinal:0; // 首发由计划给出阵型，控制增援使用有限环形空间游标。
    bCapacityDeferred=false; // 每次出生尝试独立判别角色预算/目标保护和真正空间阻挡。
    bool bPlacementDeferred=false; // 若候选含临时目标保护或分区玩家占位，本轮等待。
    FBox2D RegionalBounds(ForceInit); // 分区限制冻结到本次尝试，防止散布、构造移动或回调改动跨区。
    if (RegionalIndex != INDEX_NONE)
    {
        const FDemoRegionalSpawnZone& Zone = ActivePlan.RegionalZones[RegionalIndex];
        RegionalBounds = FBox2D(Zone.LocalMin, Zone.LocalMax);
    }
    if (bControlReinforcement && !IsControlReinforcementOpen()) { bCapacityDeferred=true; return nullptr; }
    const int32 GroupIndex=SupportGroup!=INDEX_NONE?SupportGroup:ActivePlan.SelectGroup(Ordinal,FormationSlot); // 不消费随机袋，失败仍重试相同选择。
    if (!ActivePlan.Groups.IsValidIndex(GroupIndex)) { Fail(TEXT("出生计划的组索引失效")); return nullptr; }
    const FDemoSpawnGroup Group=ActivePlan.Groups[GroupIndex]; // 值快照防BeginPlay重入替换ActivePlan后引用失效。
    if (!HasRosterCapacity(Group.Stats.Roster.Kind)) { bCapacityDeferred=true; return nullptr; } // 预算不足正常等待，不触发阻挡失败。
    FDemoEnemySpawnStats Stats=Group.Stats; Stats.FormationSlot=FormationSlot;
    if (SupportBoss) { Stats.CoinReward=0; FormationSlot=ActivePlan.MinionCount+SupportOwners.Num()+1; } // 子单位零银币，独立布局序号。 // 只补实例阵型，不二次查表或修改倍率。
    for (int32 Attempt=0;Attempt<FrozenSettings.CandidateAttempts;++Attempt) // 候选工作量有界，阻挡不消费队列。
    {
        if (!bRunning || Generation != AttemptGeneration) return nullptr; // 上次候选的回滚/销毁允许回调取消或替换计划。
        const float Angle=2.f*PI*(float(FormationSlot)/(ActivePlan.bReinforcementLayout?24.f:FMath::Max(1,ActivePlan.MinionCount))+float(Attempt)/FrozenSettings.CandidateAttempts); // 保持原椭圆顺序。
        const FVector2D Radius=ActivePlan.bReinforcementLayout||Group.bUseAnchor?SpawnGeometry.ReinforcementRadius:SpawnGeometry.CampaignRadius; // 布局由组标记选择。
        const FVector Offset=ActivePlan.bReinforcementLayout||Group.bUseAnchor?FVector(0,0,Group.bUseAnchor?SpawnGeometry.BossOffset.Z:SpawnGeometry.ReinforcementHeight):SpawnGeometry.CampaignOffset; // 区域局部高度。
        const FVector Local=Group.bUseAnchor&&Attempt==0?SpawnGeometry.BossOffset:Offset+FVector(Radius.X*FMath::Cos(Angle),Radius.Y*FMath::Sin(Angle),0); // 专用锚点阻挡时走同一候选重试。
        FVector Location=SpawnTransform.TransformPosition(Local); // 旧分支保持世界厘米与原椭圆时序；新分支只在已准备的精确点附近选位。
        if (!SpawnGeometry.ExplicitSpawnOffsets.IsEmpty())
        {
            const TArray<int32>* AssignedPoints = RegionalIndex == INDEX_NONE ? nullptr : &ActivePlan.RegionalZones[RegionalIndex].SpawnPointIndices; // 仅取点时同步借用，不跨Actor构造。
            const int32 PlacementSlot = AssignedPoints ? RegionalProgress[RegionalIndex].Spawned : FormationSlot; // 本区游标只按本区成功数推进。
            const int32 Count=AssignedPoints ? AssignedPoints->Num() : SpawnGeometry.ExplicitSpawnOffsets.Num(); // 生产房间恰8；兼容值接口仍有界64，不按怪物总量分配候选。
            const bool bBossAnchor=!AssignedPoints && Group.bUseAnchor && Attempt==0; // 分区只认获配的两个点。
            const int32 PointSlot=(PlacementSlot+Attempt)%Count; // 分区仅循环自己的作者点。
            const int32 PointIndex=AssignedPoints ? (*AssignedPoints)[PointSlot] : PointSlot; // 重试不消费队列，同一FormationSlot始终同序；真实高差保留。
            FVector FloorPoint=bBossAnchor?SpawnGeometry.BossOffset:SpawnGeometry.ExplicitSpawnOffsets[PointIndex];
            if (!bBossAnchor && PlacementSlot/Count+Attempt/Count>0)
            {
                const float Scatter=FMath::Clamp(120.f-Group.Body.CollisionRadius-4.f,0.f,64.f); // 只用作者已审120cm支持半径内余量，不能把局部重试变成跨房大椭圆。
                const float ScatterAngle=2.f*PI*float((PlacementSlot/Count+Attempt/Count)%8)/8.f;
                FloorPoint+=FVector(FMath::Cos(ScatterAngle)*Scatter,FMath::Sin(ScatterAngle)*Scatter,0);
            }
            if (AssignedPoints && !RegionalBounds.IsInsideOrOn(FVector2D(FloorPoint))) continue;
            Location=SpawnTransform.TransformPosition(FloorPoint);
            FCollisionObjectQueryParams FloorObjects; FloorObjects.AddObjectTypesToQuery(ECC_WorldStatic); FloorObjects.AddObjectTypesToQuery(ECC_WorldDynamic); // 短地面复核不接受Pawn/敌人或弹体作为支撑。
            FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(DemoAuthoredSpawnFloor),false);
            FloorQuery.MobilityType=EQueryMobilityType::Static; // 与楼梯移动一致，临时敌人/弹体不能在高处成为支撑。
            FHitResult Floor; // 只在冻结floor-contact的同层近域复核，不吸附到上方150cm夹层；球心仍由实际Body决定。
            const bool bFloorFound=GetWorld()->LineTraceSingleByObjectType(Floor,Location+FVector(0,0,40),Location-FVector(0,0,40),FloorObjects,FloorQuery);
            const AActor* FloorActor=Floor.GetActor(); // 本同步物理查询有效命中的只读借用，不跨Tick缓存。
            const FName HitLevelPackage=FloorActor && FloorActor->GetLevel()?FloorActor->GetLevel()->GetOutermost()->GetFName():NAME_None;
            if (!bFloorFound || HitLevelPackage!=SpawnGeometry.ExplicitSourceLevelPackage || FMath::Abs(Floor.ImpactPoint.Z-Location.Z)>32.f || Floor.ImpactNormal.Z<.7f)
            {
                UE_LOG(LogFPSDemo,VeryVerbose,TEXT("SPAWN_SOURCE_FLOOR_REJECT level=%d source=%s hit=%s dz=%.2f"),ActivePlan.Level,*SpawnGeometry.ExplicitSourceLevelPackage.ToString(),*HitLevelPackage.ToString(),Floor.ImpactPoint.Z-Location.Z); // 逐候选原因仅按需跟踪；正常日志仍由原有界BlockedPass汇总。
                continue;
            }
            Location=Floor.ImpactPoint+FVector(0,0,Group.Body.CollisionRadius/Floor.ImpactNormal.Z+2.f); // 斜面球心沿竖直方向需要足够法线净空。
        }
        if (ObjectiveGate.IsValid() && !ObjectiveGate->IsSpawnLocationAllowed(Location)) { bPlacementDeferred=true; continue; } // 改试其它合法候选，不提前减少PendingCount。
        if (!IsPlayerDistanceSafe(Location)) { if (RegionalIndex != INDEX_NONE) bPlacementDeferred = true; continue; }
        if (GetWorld()->OverlapBlockingTestByChannel(Location,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(Group.Body.CollisionRadius))) continue;
        const FTransform Transform(SpawnTransform.GetRotation(),Location); // Deferred期间固定初始姿态，禁止自动挪到玩家身旁。
        ADemoEnemy* Enemy=GetWorld()->SpawnActorDeferred<ADemoEnemy>(Group.EnemyClass,Transform,GetOwner(),nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn); // World拥有；最终尺寸已预查询。
        if (!IsValid(Enemy)) continue;
        Enemy->FindComponentByClass<UDemoEnemyNavigation>()->BindSpawnFloor(SpawnGeometry.ExplicitSourceLevelPackage);
        if (!Enemy->PrepareSpawn(Stats,&Group.Body)) { Enemy->Destroy(); Fail(TEXT("敌人出生准备失败：请检查定义/数值")); return nullptr; }
        Enemy->FinishSpawning(Transform); // BeginPlay建立ASC和真实骨骼；此时仍不允许攻击或伤害查询。
        if (!IsValid(Enemy)||!bRunning||Generation!=AttemptGeneration)
        { if (IsValid(Enemy)) Enemy->Destroy(); UE_LOG(LogFPSDemo,Log,TEXT("SPAWN_ROLLBACK stale plan/BeginPlay destroyed")); return nullptr; }
        if (bControlReinforcement && !IsControlReinforcementOpen()) { Enemy->Destroy(); bCapacityDeferred=true; return nullptr; } // Blueprint重入完成占点后，尚未登记的增援不能留下或领奖。
        if (!Enemy->IsSpawnInitialized()) { Enemy->Destroy(); Fail(TEXT("敌人初始化失败：检查动画/PhysicsAsset/ASC")); return nullptr; }
        if (!SpawnGeometry.ExplicitSpawnOffsets.IsEmpty() && !Enemy->GetActorLocation().Equals(Location,1.))
        {
            UE_LOG(LogFPSDemo,Log,TEXT("SPAWN_FLOOR_MOVED_REJECT prepared=%s actual=%s"),*Location.ToCompactString(),*Enemy->GetActorLocation().ToCompactString());
            Enemy->Destroy(); continue; // 构造回调移动过的实例没有该处地面凭据，回到有界候选队列。
        }
        if (RegionalIndex != INDEX_NONE && !RegionalBounds.IsInsideOrOn(FVector2D(SpawnTransform.InverseTransformPosition(Enemy->GetActorLocation()))))
        { Enemy->Destroy(); continue; } // 实际位置仍须在被激活的分区内。
        if (ObjectiveGate.IsValid() && !ObjectiveGate->IsSpawnLocationAllowed(Enemy->GetActorLocation())) { bPlacementDeferred=true; Enemy->Destroy(); continue; } // Blueprint/构造移动后再验；尚未登记，不触发丢失/奖励。
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoPreparedSpawn),false,Enemy); // 忽略自身，只检验蓝图最终位置是否仍容纳冻结体型。
        if (!IsPlayerDistanceSafe(Enemy->GetActorLocation())) { if (RegionalIndex != INDEX_NONE) bPlacementDeferred = true; Enemy->Destroy(); continue; }
        if (GetWorld()->OverlapBlockingTestByChannel(Enemy->GetActorLocation(),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeSphere(Group.Body.CollisionRadius),Query))
        { Enemy->Destroy(); continue; }
        Alive.Add(Enemy); Enemy->OnDefeated.AddUObject(this,&UDemoEnemySpawnComponent::NotifyEnemyDefeated); Enemy->OnEndPlay.AddDynamic(this,&UDemoEnemySpawnComponent::HandleEnemyEndPlay);
        if (!Enemy->ActivateSpawn()) // 尚未消费队列，解绑后销毁不会发银币、清场或异常丢失事件。
        { UnbindEnemy(Enemy); Alive.Remove(Enemy); Enemy->Destroy(); Fail(TEXT("敌人出生激活失败：战局已失效")); return nullptr; }
        if (!bRunning || Generation != AttemptGeneration)
        {
            if (IsValid(Enemy)) { UnbindEnemy(Enemy); Enemy->DeactivateSpawn(); Enemy->Destroy(); }
            UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_ROLLBACK stale activation"));
            return nullptr;
        }
        if (bControlReinforcement && !IsControlReinforcementOpen())
        { UnbindEnemy(Enemy); Alive.Remove(Enemy); Enemy->DeactivateSpawn(); Enemy->Destroy(); bCapacityDeferred=true; return nullptr; } // 激活边沿若完成/取消，同栈回滚，不消费下一场/已丢弃队列。
        if (SupportBoss) { SupportOwners.Add(Enemy,SupportBoss); if (UDemoEnemyRoster* Roster=SupportBoss->FindComponentByClass<UDemoEnemyRoster>()) Roster->RegisterSupport(Enemy); }
        else if (bControlReinforcement) { --ControlPendingCount; ControlSelectionCursor=(ControlSelectionCursor+1)%ActivePlan.CyclingGroups.Num(); ControlFormationSlot=(ControlFormationSlot+1)%64; }
        else
        {
            --PendingCount; ++NextSpawn;
            if (RegionalIndex != INDEX_NONE) ++RegionalProgress[RegionalIndex].Spawned; // 同次成功登记消费区域及全场名额。
        } // 只有三阶段握手全部成功才消费一次，控制增援不累积无限总序号。
        UE_LOG(LogFPSDemo,Log,TEXT("SPAWN_CREATED level=%d ordinal=%d group=%s actor=%s center=%s radius=%.1f source=%s"),ActivePlan.Level,Ordinal,*Group.Id.ToString(),*Enemy->GetName(),*Enemy->GetActorLocation().ToCompactString(),Group.Body.CollisionRadius,*SpawnGeometry.ExplicitSourceLevelPackage.ToString());
        return Enemy;
    }
    bCapacityDeferred=bPlacementDeferred; // 目标或玩家占位保持预算，不占用BlockedPassLimit，原计划/Ordinal保持原样。
    return nullptr;
}
// 同一权威时钟先记录所有入区，即使满员或尚在批间冷却；此后按共享游标尝试，首个有成功生成的区独占本批。
void UDemoEnemySpawnComponent::FillRegionalWave()
{
    DEMO_LOG_TICK();
    for (TActorIterator<ADemoCharacter> It(GetWorld()); It; ++It)
    {
        if (!DemoEnemyTargeting::IsAliveParticipant(*It)) continue;
        const FVector Local = SpawnTransform.InverseTransformPosition(It->GetActorLocation()); // 与冻结作者标记同一局部坐标系，矩形含边界。
        for (int32 Index = 0; Index < RegionalProgress.Num(); ++Index)
        {
            const FDemoRegionalSpawnZone& Zone = ActivePlan.RegionalZones[Index]; // 只在查询时借用。
            if (!RegionalProgress[Index].bEntered && FBox2D(Zone.LocalMin, Zone.LocalMax).IsInsideOrOn(FVector2D(Local)))
            {
                RegionalProgress[Index].bEntered = true;
                UE_LOG(LogFPSDemo, Log, TEXT("REGIONAL_ENTER zone=%s pawn=%s quota=%d"), *Zone.Id.ToString(), *It->GetName(), Zone.EnemyCount);
            }
        }
    }

    const double Now = GetWorld()->GetTimeSeconds(); // 游戏暂停时停表，长帧不补发过去的批次。
    if (Now < NextRegionalWaveTime || Alive.Num() >= ActivePlan.MaxAlive) return;
    const uint64 WaveGeneration = Generation; // Actor构造/激活可同步取消并重启；下一场不可消费本场批次。
    const int32 ZoneCount = RegionalProgress.Num(); // 单次无成功时最多遍历所有区一轮。
    for (int32 Offset = 0; Offset < ZoneCount; ++Offset)
    {
        const int32 Index = (NextRegionalZone + Offset) % ZoneCount; // 从上个成功区之后继续，阻塞区让出本次机会。
        const int32 Remaining = ActivePlan.RegionalZones[Index].EnemyCount - RegionalProgress[Index].Spawned; // 含未进区储备，单区上限整场不返还。
        if (!RegionalProgress[Index].bEntered || Remaining <= 0) continue;
        const int32 Limit = FMath::Min3(Remaining, ActivePlan.RegionalWaveSize, FrozenSettings.BatchLimit); // Alive每次生成前重验。
        int32 Created = 0; // 本次此区成功登记数，不包含位置复核失败的Actor。
        while (Created < Limit && Alive.Num() < ActivePlan.MaxAlive)
        {
            ADemoEnemy* Enemy = TrySpawn(NextSpawn, INDEX_NONE, nullptr, false, Index);
            if (!bRunning || WaveGeneration != Generation) return;
            if (!Enemy) break;
            ++Created;
        }
        FRegionalProgress& Progress = RegionalProgress[Index]; // 已离开Actor构造调用栈且本场身份仍有效。
        if (Created > 0)
        {
            Progress.BlockedPasses = 0;
            NextRegionalZone = (Index + 1) % ZoneCount;
            NextRegionalWaveTime = Now + ActivePlan.RegionalWaveInterval;
            UE_LOG(LogFPSDemo, Log, TEXT("REGIONAL_WAVE zone=%s time=%.3f count=%d zoneSpawned=%d alive=%d pending=%d"),
                *ActivePlan.RegionalZones[Index].Id.ToString(), Now, Created, Progress.Spawned, Alive.Num(), PendingCount);
            if (PendingCount == 0)
            {
                GetWorld()->GetTimerManager().ClearTimer(FillTimer);
                // 最后一只在激活碰撞的同步回调中已死亡时，扣掉最后待出数后补交同一个下一帧清场检查。
                if (Alive.IsEmpty() && !GetWorld()->GetTimerManager().TimerExists(CompleteTimer))
                    CompleteTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UDemoEnemySpawnComponent::Complete);
            }
            OnRemainingChanged.Broadcast(GetRemaining());
            return;
        }
        if (bCapacityDeferred || !Alive.IsEmpty())
        {
            Progress.BlockedPasses = 0; // 活人占位、存活敌人及短时保护不耗用结构错误重试。
            continue;
        }
        if (++Progress.BlockedPasses >= FrozenSettings.BlockedPassLimit)
        {
            Fail(FString::Printf(TEXT("分区 %s 的出生点持续不可用，请检查地面及碰撞"), *ActivePlan.RegionalZones[Index].Id.ToString()));
            return;
        }
    }
}

void UDemoEnemySpawnComponent::Fill()
{
    DEMO_LOG_CALL();
    if (!bRunning) { UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_FILL ignored cancelled/completed")); return; }
    if (!RegionalProgress.IsEmpty()) { FillRegionalWave(); return; }
    QueueControlReinforcements();
    for (int32 Generated = 0; bRunning && Generated < FrozenSettings.BatchLimit && Alive.Num() < ActivePlan.MaxAlive && PendingCount > 0; ++Generated) // 每帧有界工作量，队列总数不影响帧开销。
    {
        if (!TrySpawn(NextSpawn))
        {
            if (!bRunning) return; // 初始化失败/BeginPlay取消已独立通知，不追加虚假的阻挡错误。
            if (bCapacityDeferred) return; // 角色池已满或目标1秒出生保护，只等下一批，不把暂时门禁当错误。
            ++BlockedPasses; // 重试计数必须独立于日志求值，包体裁掉详细日志也继续正确超限。
            UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_BLOCKED level=%d pass=%d pending=%d"), ActivePlan.Level, BlockedPasses, PendingCount);
            if (BlockedPasses == 1) UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_BLOCKED begin level=%d; retries summarized"), ActivePlan.Level);
            if (BlockedPasses >= FrozenSettings.BlockedPassLimit) Fail(TEXT("出生点持续被阻挡，请检查刷怪区域或重试"));
            return;
        }
        if (BlockedPasses > 0) UE_LOG(LogFPSDemo, Display, TEXT("SPAWN_RECOVERED level=%d retries=%d"), ActivePlan.Level, BlockedPasses);
        BlockedPasses = 0;
    }
    if (!bRunning) return;
    for (int32 Generated=0; bRunning && Generated<FrozenSettings.BatchLimit && Alive.Num()<ActivePlan.MaxAlive && PendingCount==0 && ControlPendingCount>0; ++Generated)
    {
        if (!IsControlReinforcementOpen()) { StopControlReinforcements(); break; }
        const int32 GroupIndex=ActivePlan.CyclingGroups[ControlSelectionCursor]; // 仅已冻结普通循环模板，不重放Boss/特殊替换/召唤队列。
        if (!TrySpawn(ControlFormationSlot,GroupIndex,nullptr,true))
        {
            if (!bRunning || bCapacityDeferred) return;
            ++BlockedPasses;
            UE_LOG(LogFPSDemo,Log,TEXT("CONTROL_REINFORCEMENT_BLOCKED pass=%d pending=%d"),BlockedPasses,ControlPendingCount);
            if (BlockedPasses>=FrozenSettings.BlockedPassLimit) Fail(TEXT("控制增援出生点持续被阻挡，请检查区域或重试"));
            return;
        }
        BlockedPasses=0;
    }
    OnRemainingChanged.Broadcast(GetRemaining());
    // 普通队列优先，召唤不会抢走未生成的基础关卡名额。
    if (PendingCount==0&&ControlPendingCount==0&&Alive.Num()<ActivePlan.MaxAlive&&!SupportRequests.IsEmpty())
    {
        const FSupportRequest Request=SupportRequests[0]; // Deferred期间外部回调可改变队列，使用值快照。
        ADemoEnemy* Boss=Request.Boss.Get(); // 弱引用，只本同步生成栈借用。
        if (!Boss||!Alive.Contains(Boss)||!Boss->IsCombatReady()) SupportRequests.RemoveAt(0);
        else if (TrySpawn(0,Request.Group,Boss)) { if (!SupportRequests.IsEmpty()&&SupportRequests[0].Boss==Request.Boss) SupportRequests.RemoveAt(0); }
        else if (!bCapacityDeferred&&bRunning&&!SupportRequests.IsEmpty()&&++SupportRequests[0].Attempts>=FrozenSettings.BlockedPassLimit)
        { UE_LOG(LogFPSDemo,Warning,TEXT("SUPPORT_SKIPPED placement exhausted; boss remains damageable")); SupportRequests.RemoveAt(0); }
    }
    OnRemainingChanged.Broadcast(GetRemaining());
    if (PendingCount==0&&ControlPendingCount==0&&SupportRequests.IsEmpty()&&!bControlReinforcementsEnabled) GetWorld()->GetTimerManager().ClearTimer(FillTimer);
}
void UDemoEnemySpawnComponent::UnbindEnemy(ADemoEnemy* Enemy)
{
    DEMO_LOG_CALL();
    if (Enemy) { Enemy->OnDefeated.RemoveAll(this); Enemy->OnEndPlay.RemoveDynamic(this, &UDemoEnemySpawnComponent::HandleEnemyEndPlay); }
}
void UDemoEnemySpawnComponent::NotifyEnemyDefeated(ADemoEnemy* Enemy)
{
    DEMO_LOG_CALL();
    if (!bRunning || !Enemy || Enemy->IsAlive() || Alive.Remove(Enemy) == 0)
    { UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_KILL rejected inactive/alive/unregistered/duplicate")); return; }
    SupportOwners.Remove(Enemy); // 真实死亡仍可发0银币事件；只清理不会进入此分支。
    UnbindEnemy(Enemy); // 先移除再发事件，重复死亡或尸体销毁不能重复奖励。
    OnRemainingChanged.Broadcast(GetRemaining());
    OnEnemyDefeated.Broadcast(Enemy);
    if (bRunning && !ActivePlan.bControlReinforcements && GetRemaining() == 0) CompleteTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UDemoEnemySpawnComponent::Complete); // 控制任务可在两波之间暂时无人，完成事实只能由目标提出。
}
void UDemoEnemySpawnComponent::NotifyEnemyLost(ADemoEnemy* Enemy)
{
    DEMO_LOG_CALL();
    if (bRunning && Alive.Contains(Enemy)) Fail(TEXT("An active enemy was unexpectedly removed"));
    else UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_LOST ignored unregistered/cancelled"));
}
void UDemoEnemySpawnComponent::HandleEnemyEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    if (Reason == EEndPlayReason::Destroyed) NotifyEnemyLost(Cast<ADemoEnemy>(Actor));
    else CancelEncounter(false); // World旅行或卸载先停止整项计划，后续敌人回调不再参与结算。
}
void UDemoEnemySpawnComponent::Complete()
{
    DEMO_LOG_CALL();
    if (!bRunning || ActivePlan.bControlReinforcements || GetRemaining() != 0) { UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_CLEAR ignored stale/nonempty/objective-owned")); return; }
    bRunning = false; bComplete = true;
    GetWorld()->GetTimerManager().ClearTimer(FillTimer);
    UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_CLEARED level=%d run=%s"), ActivePlan.Level, *ActivePlan.RunId);
    OnCleared.Broadcast();
}
void UDemoEnemySpawnComponent::Fail(const FString& Reason)
{
    DEMO_LOG_CALL();
    CancelEncounter(false);
    UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_FAILED %s"), *Reason);
    OnFailed.Broadcast(Reason);
}
void UDemoEnemySpawnComponent::CancelEncounter(bool bDestroyEnemies)
{
    DEMO_LOG_CALL();
    ++Generation; bRunning=bComplete=false; PendingCount=0;
    RegionalProgress.Reset(); NextRegionalZone=0; NextRegionalWaveTime=0.;
    bControlReinforcementsEnabled=false; ControlPendingCount=ControlSelectionCursor=ControlFormationSlot=0; NextControlReinforcementTime=0.;
    SupportRequests.Reset(); SupportOwners.Reset(); SupportRequestCounts.Reset(); // 先撤销父子登记，Enemy取消回调不会递归修改Alive快照。 // 先使所有Deferred提交凭据失效，再允许回调执行。
    GetWorld()->GetTimerManager().ClearTimer(FillTimer); GetWorld()->GetTimerManager().ClearTimer(CompleteTimer);
    const TArray<TWeakObjectPtr<ADemoEnemy>> Previous = Alive.Array(); // 弱引用值快照；先解绑所有对象再允许Destroy回调。
    for (const TWeakObjectPtr<ADemoEnemy>& Entry : Previous) if (ADemoEnemy* Enemy = Entry.Get()) { UnbindEnemy(Enemy); Enemy->DeactivateSpawn(); } // 仅当前同步循环借用。
    Alive.Empty(); FrozenClasses.Reset(); FrozenBodies.Reset(); FrozenDefinitions.Reset(); // 解除运行资产引用，取消后的值计划不再访问。
    ObjectiveGate.Reset(); // 不让结束计划持有下一场目标服务的旧门禁意图。
    if (bDestroyEnemies) for (const TWeakObjectPtr<ADemoEnemy>& Entry : Previous) if (ADemoEnemy* Enemy = Entry.Get()) Enemy->Destroy(); // 不发布击杀/清场事件。
    UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_CANCEL destroy=%d actors=%d"), bDestroyEnemies, Previous.Num());
}
void UDemoEnemySpawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); CancelEncounter(false); Super::EndPlay(EndPlayReason);
}
bool UDemoEnemySpawnComponent::HasRosterCapacity(EDemoRosterKind Kind) const
{
    DEMO_LOG_TICK();
    if (!DemoRosterRules::IsSpecialMinion(Kind)) return true; // 基础小怪/Boss/锚点不受特殊并发上限约束。
    int32 Count=0; // 当前活单位数，原生成和召唤共用同一Alive注册表。
    for (const TWeakObjectPtr<ADemoEnemy>& Entry:Alive) if (ADemoEnemy* Enemy=Entry.Get())
        if (const UDemoEnemyRoster* Roster=Enemy->FindComponentByClass<UDemoEnemyRoster>(); Roster&&Roster->GetKind()==Kind) ++Count;
    return Count<DemoRosterRules::SpecialCap(Kind); // 与表校验/生成计划同一上限来源。
}
bool UDemoEnemySpawnComponent::RequestSupport(ADemoEnemy* Boss,FName Row)
{
    DEMO_LOG_CALL();
    const int32* Group=ActivePlan.SupportGroups.Find(Row); // 当前计划冻结组，禁止运行期任意拼模板。
    if (!bRunning||!Boss||!Alive.Contains(Boss)||!Boss->IsBoss()||!Boss->IsCombatReady()||!Group||SupportRequestCounts.FindRef(Boss)>=2)
    { UE_LOG(LogFPSDemo,Warning,TEXT("SUPPORT_REJECT owner/group/quota")); return false; }
    FSupportRequest Request; // 值请求不拥有Boss，死亡可以同栈撤销。
    Request.Boss=Boss; Request.Group=*Group; SupportRequests.Add(Request); ++SupportRequestCounts.FindOrAdd(Boss);
    GetWorld()->GetTimerManager().SetTimer(FillTimer,this,&UDemoEnemySpawnComponent::Fill,FrozenSettings.Interval,true);
    OnRemainingChanged.Broadcast(GetRemaining());
    UE_LOG(LogFPSDemo,Log,TEXT("SUPPORT_QUEUED boss=%s row=%s"),*Boss->GetName(),*Row.ToString()); return true;
}
void UDemoEnemySpawnComponent::CancelSupport(ADemoEnemy* Boss)
{
    DEMO_LOG_CALL(); if (!bRunning||!Boss) return;
    // 无异步捕获；本同步游戏线程谓词只比较Boss身份，RemoveAll返回后捕获不再存在。
    SupportRequests.RemoveAll([Boss](const FSupportRequest& Request) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] Cancel support request")); return Request.Boss==Boss; });
    TArray<TWeakObjectPtr<ADemoEnemy>> Removed; // 先收集并解除注册，再允许Actor清理回调重入。
    for (const auto& Pair:SupportOwners) if (Pair.Value==Boss) Removed.Add(Pair.Key); // 只移除本Boss子单位，原有小怪保留。
    for (const TWeakObjectPtr<ADemoEnemy>& Entry:Removed)
    {
        SupportOwners.Remove(Entry); Alive.Remove(Entry);
        if (ADemoEnemy* Enemy=Entry.Get()) { UnbindEnemy(Enemy); Enemy->DeactivateSpawn(); Enemy->Destroy(); } // 不触发OnEnemyDefeated或奖励。
    }
    OnRemainingChanged.Broadcast(GetRemaining());
    UE_LOG(LogFPSDemo,Log,TEXT("SUPPORT_CANCEL boss=%s removed=%d"),*Boss->GetName(),Removed.Num());
}

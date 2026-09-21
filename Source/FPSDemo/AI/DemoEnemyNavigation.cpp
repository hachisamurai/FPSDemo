#include "AI/DemoEnemyNavigation.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Components/SphereComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Debug/DemoLog.h"

// 可在PIE控制台临时开启路径/停步原因；不写入存档、不改变游戏行为。
static TAutoConsoleVariable<int32> CVarDemoNavigation(TEXT("Demo.AI.DebugNavigation"),0,TEXT("Draw enemy navigation paths and stop reasons (0/1)."));

UDemoEnemyNavigation::UDemoEnemyNavigation() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
FName UDemoEnemyNavigation::GetStatus() const { DEMO_LOG_TICK(); return Status; }
void UDemoEnemyNavigation::SetStatus(FName NewStatus)
{
    DEMO_LOG_TICK();
    if (Status == NewStatus) return;
    Status = NewStatus;
    UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_NAV actor=%s state=%s points=%d"),*GetNameSafe(GetOwner()),*Status.ToString(),Points.Num());
}
void UDemoEnemyNavigation::Stop(FName Reason)
{
    DEMO_LOG_TICK();
    Points.Reset(); PointIndex = 0; BlockedSeconds = 0; NextRepathTime = 0;
    SetStatus(Reason);
    DrawNavigation();
}
bool UDemoEnemyNavigation::CanSee(const ADemoCharacter* Target) const
{
    DEMO_LOG_TICK();
    if (!Target || !GetOwner()) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("ENEMY_NAV sight rejected: missing owner/target")); return false; }
    FHitResult Hit; // 最近Visibility阻挡，包含墙、掩体及其他敌人。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoEnemyNavSight),false,GetOwner()); // 忽略本敌人，绝不忽略掩体。
    GetWorld()->LineTraceSingleByChannel(Hit,GetOwner()->GetActorLocation(),Target->GetActorLocation(),ECC_Visibility,Query);
    return Hit.GetActor() == Target;
}
bool UDemoEnemyNavigation::FindRoute(const FVector& Goal, TArray<FVector>& OutPoints) const
{
    DEMO_LOG_CALL();
    OutPoints.Reset();
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); // World持有导航系统，函数内借用。
    const ANavigationData* Data = Nav ? Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr; // 配置半径120cm，涵盖115cm Boss碰撞。
    if (!Data) { UE_LOG(LogFPSDemo,Warning,TEXT("ENEMY_NAV no generated navigation data")); return false; }
    FNavLocation Start; // 敌人中心向下投影到地面NavMesh，不使用悬浮Z作为路径高度。
    FNavLocation End; // 玩家脚下/射击候选点投影；禁止跨越较大墙体吸附到远处多边形。
    const FVector Extent(250.f,250.f,220.f); // cm，仅适用于当前平面白模；不支持多层飞行导航。
    if (!Nav->ProjectPointToNavigation(GetOwner()->GetActorLocation(),Start,Extent,Data)
        || !Nav->ProjectPointToNavigation(Goal,End,Extent,Data))
    { UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_NAV projection failed actor=%s goal=%s"),*GetNameSafe(GetOwner()),*Goal.ToString()); return false; }
    FPathFindingQuery Query(GetOwner(),*Data,Start.Location,End.Location); // 同步查询由Follow限频，最多30怪的小场景不每帧重算。
    Query.SetAllowPartialPaths(false); // 隔离岛/不可达目标不能用部分路径伪装追击成功。
    const FPathFindingResult Result = Nav->FindPathSync(Query); // 路径共享对象只在本调用借用，下面复制点值。
    if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
    { UE_LOG(LogFPSDemo,Log,TEXT("ENEMY_NAV no complete route actor=%s"),*GetNameSafe(GetOwner())); return false; }
    for (const FNavPathPoint& Point : Result.Path->GetPathPoints()) OutPoints.Add(Point.Location); // 复制点值，跨导航重建不保留悬空多边形引用。
    return OutPoints.Num() > 1;
}
bool UDemoEnemyNavigation::Repath(const ADemoCharacter* Target, float Range)
{
    DEMO_LOG_CALL();
    LastGoal = Target->GetNavAgentLocation();
    const float Now = GetWorld()->GetTimeSeconds(); // 受暂停控制的World时间，不在暂停菜单后台移动。
    NextRepathTime = Now + .75f; NextRefreshTime = Now + 3.f;
    Points.Reset(); PointIndex = 0; BlockedSeconds = 0;
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); // 缺导航时只记录状态变化，不对12个候选点重复发失败查询。
    if (!Nav || !Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)) { SetStatus(TEXT("NoNavigation")); return false; }
    // 优先直接追击可达的玩家脚下；末端隔着低掩体/站在掩体上时寻找附近可见位置。
    if (FindRoute(LastGoal,Points) && (FVector::Dist2D(GetOwner()->GetActorLocation(),Points.Last()) > 70.f || CanSee(Target)))
    { PointIndex = 1; SetStatus(TEXT("Following")); return true; }
    Points.Reset();
    for (int32 CandidateIndex = 0; CandidateIndex < 12; ++CandidateIndex) // 固定12个方向，只在直达失败/不可攻击末端限频搜索。
    {
        const float Angle = CandidateIndex * (2.f * PI / 12.f); // 弧度，确定性顺序便于复现寻路问题。
        const FVector Candidate = LastGoal + FVector(FMath::Cos(Angle),FMath::Sin(Angle),0) * FMath::Max(300.f,Range*.85f); // cm，Boss在攻击距离内选点。
        TArray<FVector> Route; // 当前候选临时路线，只在视线验证通过后转移给组件。
        if (!FindRoute(Candidate,Route)) continue;
        FVector SightStart = Route.Last(); // 把候选地面点提升至现有悬浮高度，按实际发射中心测试视线。
        SightStart.Z = GetOwner()->GetActorLocation().Z;
        FHitResult Hit; // 候选位置到玩家的首个阻挡，不允许隔墙选择攻击点。
        FCollisionQueryParams SightQuery(SCENE_QUERY_STAT(DemoEnemyNavCandidate),false,GetOwner());
        GetWorld()->LineTraceSingleByChannel(Hit,SightStart,Target->GetActorLocation(),ECC_Visibility,SightQuery);
        if (Hit.GetActor() != Target) continue;
        Points = MoveTemp(Route); PointIndex = 1; SetStatus(TEXT("Repositioning")); return true;
    }
    SetStatus(TEXT("NoPath"));
    return false;
}
void UDemoEnemyNavigation::Follow(ADemoCharacter* Target, float Range, float Speed, float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (bPointMode) { Stop(TEXT("ResumeChase")); bPointMode=false; } // 切换目标语义必须清除战术路径。
    if (!Target || !GetOwner() || !GetOwner()->HasAuthority() || Speed <= 0 || DeltaSeconds <= 0)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("ENEMY_NAV follow rejected: target/authority/speed/time")); return; }
    const float Distance = FVector::Dist2D(GetOwner()->GetActorLocation(),Target->GetActorLocation()); // cm，攻击距离使用现有平面定义。
    // 小怪近战还有三维190cm限制，玩家站高处时不能仅凭水平距离误判可攻击。
    if (Distance <= Range && (Range > 200.f || FVector::Dist(GetOwner()->GetActorLocation(),Target->GetActorLocation()) < 190.f) && CanSee(Target)) { Stop(TEXT("InRange")); return; }
    const float Now = GetWorld()->GetTimeSeconds(); // 路线更新/失败重试统一按World时钟。
    if (Now >= NextRepathTime && (Points.IsEmpty() || FVector::Dist2D(LastGoal,Target->GetNavAgentLocation()) > 100.f || Now >= NextRefreshTime || BlockedSeconds >= .6f))
        Repath(Target,Range);
    AdvancePath(Speed,DeltaSeconds);
}
bool UDemoEnemyNavigation::MoveTo(const FVector& Goal, ADemoCharacter* Target, float Speed, float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (!Target || !GetOwner() || !GetOwner()->HasAuthority() || Speed<=0 || DeltaSeconds<=0) return false;
    if (!bPointMode) { Stop(TEXT("TacticalMove")); bPointMode=true; }
    const float Now=GetWorld()->GetTimeSeconds(); // World秒，动态路径仍限频更新。
    if (FVector::Dist2D(GetOwner()->GetActorLocation(),Goal)<65.f) { Stop(TEXT("TacticalPosition")); return true; }
    if (Now>=NextRepathTime && (Points.IsEmpty() || FVector::Dist2D(LastGoal,Goal)>100.f || Now>=NextRefreshTime || BlockedSeconds>=.6f))
    {
        LastGoal=Goal; NextRepathTime=Now+.75f; NextRefreshTime=Now+3.f; BlockedSeconds=0; PointIndex=1;
        if (!FindRoute(Goal,Points)) { SetStatus(TEXT("TacticalNoPath")); return false; }
        FVector SightStart=Points.Last(); // 投影后的真实终点，不能验证未经投影的墙内候选。
        SightStart.Z=GetOwner()->GetActorLocation().Z;
        FHitResult Hit; // 首个Visibility阻挡，候选点必须能看见实际玩家。
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoTacticalSight),false,GetOwner()); // 同步射线只忽略来源敌人。
        GetWorld()->LineTraceSingleByChannel(Hit,SightStart,Target->GetActorLocation(),ECC_Visibility,Query);
        if (Hit.GetActor()!=Target) { Points.Reset(); SetStatus(TEXT("TacticalNoSight")); return false; }
        SetStatus(TEXT("TacticalMove"));
    }
    AdvancePath(Speed,DeltaSeconds);
    return true;
}
void UDemoEnemyNavigation::AdvancePath(float Speed,float DeltaSeconds)
{
    DEMO_LOG_TICK();
    while (Points.IsValidIndex(PointIndex) && FVector::Dist2D(GetOwner()->GetActorLocation(),Points[PointIndex]) < 35.f) ++PointIndex;
    if (!Points.IsValidIndex(PointIndex)) { Points.Reset(); DrawNavigation(); return; }
    const FVector Before = GetOwner()->GetActorLocation(); // 本帧起点，用实际位移检测卡住，而非只看碰撞返回。
    const FVector Direction = (Points[PointIndex]-Before).GetSafeNormal2D(); // 沿缓存拐点走，不能直接穿过中间障碍朝玩家移动。
    const float Travel = FMath::Min(Speed*DeltaSeconds,FVector::Dist2D(Before,Points[PointIndex])); // cm，防低帧率越过拐角。
    FHitResult Hit; // Sweep仍是最终防穿墙约束，NavMesh不替代实际碰撞。
    GetOwner()->AddActorWorldOffset(Direction*Travel,true,&Hit);
    if (Hit.bBlockingHit)
    {
        const FVector Slide = FVector::VectorPlaneProject(Direction,Hit.Normal).GetSafeNormal2D(); // 沿碰撞表面滑动，长度仍受本帧剩余预算约束。
        GetOwner()->AddActorWorldOffset(Slide*Travel*(1.f-Hit.Time),true);
        if (Cast<ADemoEnemy>(Hit.GetActor()))
        {
            // 动态同伴不烘焙进NavMesh；朝路径右侧让行，实际Sweep保证不能穿过墙体或其他敌人。
            const FVector Side = FVector::CrossProduct(Direction,FVector::UpVector);
            GetOwner()->AddActorWorldOffset(Side*Travel*.5f,true);
        }
    }
    if (FVector::DistSquared2D(Before,GetOwner()->GetActorLocation()) < FMath::Square(Travel*.15f)) BlockedSeconds += DeltaSeconds;
    else BlockedSeconds = 0;
    if (BlockedSeconds >= .6f) SetStatus(TEXT("Blocked"));
    DrawNavigation();
}
void UDemoEnemyNavigation::DrawNavigation() const
{
    DEMO_LOG_TICK();
    if (CVarDemoNavigation.GetValueOnGameThread() == 0 || !GetOwner()) return;
    FVector Previous = GetOwner()->GetActorLocation(); // 临时绘图起点，不修改缓存导航高度。
    for (int32 Index = PointIndex; Index < Points.Num(); ++Index) // 仅画尚未走过的路径。
    {
        FVector Point = Points[Index]; // 当前地面路径点提升到悬浮高度便于观察。
        Point.Z = Previous.Z;
        DrawDebugLine(GetWorld(),Previous,Point,FColor::Cyan,false,0.f,0,3.f);
        Previous = Point;
    }
    DrawDebugString(GetWorld(),GetOwner()->GetActorLocation()+FVector(0,0,190),Status.ToString(),nullptr,FColor::Cyan,0.f,true);
}

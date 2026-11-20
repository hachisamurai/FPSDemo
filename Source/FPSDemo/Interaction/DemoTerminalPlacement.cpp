// 职责：一次性、有界的终端同源地面/支撑/占位/接近通路预检；不生成Actor、修改Nav或把失败点投影到别处。
// 依赖：实际Room快照的ExplicitSourceLevelPackage、UE公开碰撞/Nav查询；Hub沿用常驻Level作者锚点。
#include "Interaction/DemoTerminalPlacement.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "World/Rooms/DemoRoomAnchor.h"
#include "Characters/DemoCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "UObject/Package.h" // 实际Level包身份需要完整UPackage，避免依赖Unity间接包含。
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavigationPath.h"
#include "Debug/DemoStaticLog.h"

namespace
{
    constexpr float GroundTolerance=32.f; // 已选楼层近测允许cm，不能吸附320cm上下层。
    constexpr float SupportTolerance=16.f; // 终端五个足印支撑点相对中心最大高差cm，拒绝边缘悬空/陡坡。
    // 清关两终端中心距离候选cm。2026-11-20用户要求两座终端间隔3～5米（原固定200cm过近）：优先4m，再试3.2m/4.8m；作者锚点路径不使用此值。
    constexpr float PairSeparations[]={400.f,320.f,480.f};
    constexpr float VictoryMinDistance=250.f; // 任一终端中心距胜利位置下限cm；纵向成对时近端也不能贴脸。
    constexpr float VictoryMaxDistance=700.f; // 清关终端中心距胜利位置上限cm；2026-11-20由450放宽，配合优先4m的半径避免贴脸生成。
    constexpr float VisualHalfClearance=90.f; // 清关时额外检查的方形显示占位半宽cm；Hub出发门显示宽166cm（半宽83）超出60cm根盒，避免门框插进墙。
    using DemoTerminalPlacement::ContactLift;

    /** 仅本次调用的实际场景上下文；所有UObject为同步借用，查询不忽略玩家占位。 */
    struct FPlacementContext
    {
        UWorld* World=nullptr; // 当前服务器World，不跨帧保存。
        const ULevel* Source=nullptr; // 只允许该Level支撑新终端。
        FBox Bounds=FBox(ForceInit); // 实际Pawn阻挡网格合并Box，不含图标/Nav显示范围。
        FVector HalfExtent=FVector::ZeroVector; // 真实终端Box半尺寸cm。
        FCollisionObjectQueryParams FloorObjects; // 只以真实静态/动态环境支撑，不把Pawn当地面。
        FCollisionQueryParams Query=FCollisionQueryParams(SCENE_QUERY_STAT(DemoTerminalPlacement),false); // 同步碰撞查询，无异步捕获。
    };

    /** 有界候选拒绝原因只在限时逐帧追踪时输出；正常运行仅保留最终摘要/失败，避免批量射线刷屏。 */
    bool RejectCandidate(const UObject* Context,const TCHAR* Reason)
    {
        DEMO_LOG_STATIC_TICK(Context);
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("TERMINAL_CANDIDATE_REJECT reason=%s"),Reason);
        return false;
    }

    /** 解析冻结来源，旧流送房用实际RoomAnchor归属，Hub/旧战区只认常驻Level；不存在或歧义即拒绝。 */
    bool BuildContext(UWorld& World,const FDemoAreaSnapshot& Area,const FVector& HalfExtent,FPlacementContext& Out,FString& Error)
    {
        DEMO_LOG_STATIC_CALL(&World);
        Out.World=&World; Out.HalfExtent=HalfExtent;
        Out.FloorObjects.AddObjectTypesToQuery(ECC_WorldStatic); Out.FloorObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
        if (!Area.Geometry.ExplicitSourceLevelPackage.IsNone())
        {
            for (const ULevel* Level:World.GetLevels())
                if (Level && Level->GetOutermost()->GetFName()==Area.Geometry.ExplicitSourceLevelPackage) Out.Source=Level;
        }
        else if (Area.bRoomAnchors)
        {
            for (const ULevel* Level:World.GetLevels())
                if (Level)
                    for (const AActor* Actor:Level->Actors)
                        if (const ADemoRoomAnchor* Anchor=Cast<ADemoRoomAnchor>(Actor); Anchor && Anchor->HasActorBegunPlay() && Anchor->GetActorTransform().Equals(Area.Transform,.1f))
                        {
                            if (Out.Source && Out.Source!=Level) { Error=TEXT("房间来源不唯一"); return false; }
                            Out.Source=Level;
                        }
        }
        else Out.Source=World.PersistentLevel;
        if (!Out.Source || HalfExtent.ContainsNaN() || HalfExtent.GetMin()<=0.f) { Error=TEXT("终端来源Level或真实碰撞尺寸无效"); return false; }
        for (const AActor* Actor:Out.Source->Actors)
            if (IsValid(Actor))
            {
                TInlineComponentArray<UStaticMeshComponent*> Meshes; Actor->GetComponents(Meshes); // 只读所属Level实际阻挡网格。
                for (const UStaticMeshComponent* Mesh:Meshes)
                    if (Mesh && Mesh->IsRegistered() && Mesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision && Mesh->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block)
                        Out.Bounds+=Mesh->Bounds.GetBox();
            }
        if (!Out.Bounds.IsValid) { Error=TEXT("终端来源Level没有真实阻挡地面"); return false; }
        return true;
    }

    /** 垂直近测实际指定楼层；对象必须来自冻结Level，不能落在旧房、角色、屋顶或另一层。 */
    bool ReadFloor(const FPlacementContext& Context,const FVector& Reference,float Up,float Down,float HeightTolerance,FVector& Out)
    {
        DEMO_LOG_STATIC_TICK(Context.World);
        FHitResult Hit; // 本次首个实际阻挡面，不能跳过墙/平台改捡更低的期望面。
        if (!Context.World->LineTraceSingleByObjectType(Hit,Reference+FVector(0,0,Up),Reference-FVector(0,0,Down),Context.FloorObjects,Context.Query)
            || Hit.bStartPenetrating || !Hit.GetActor() || Hit.GetActor()->GetLevel()!=Context.Source || Hit.ImpactNormal.Z<.85f
            || FMath::Abs(Hit.ImpactPoint.Z-Reference.Z)>HeightTolerance || !Context.Bounds.ExpandBy(2.f).IsInsideOrOn(Hit.ImpactPoint))
            return RejectCandidate(Context.World,TEXT("ground_missing_penetrating_foreign_wrong_layer_or_slope"));
        Out=Hit.ImpactPoint; return true;
    }

    /** 五点支撑及真实Box占位；bAuthored兼容旧作者锚点含80cm中心高度，清关点始终采用32cm同层近测。 */
    bool FitTerminal(const FPlacementContext& Context,const FVector& Reference,FVector& OutCenter,bool bAuthored=false)
    {
        DEMO_LOG_STATIC_TICK(Context.World);
        FVector Ground; // 当前候选中心的所属Level真实地面cm。
        if (!ReadFloor(Context,Reference,bAuthored?160.f:40.f,bAuthored?500.f:80.f,bAuthored?500.f:GroundTolerance,Ground)) return false;
        for (float X:{-1.f,1.f}) for (float Y:{-1.f,1.f})
        {
            FVector Corner; // Box四角都要有近似同高的真实地面，不以中心命中冒充整座有支撑。
            if (!ReadFloor(Context,Ground+FVector(X*Context.HalfExtent.X,Y*Context.HalfExtent.Y,0),24.f,40.f,SupportTolerance,Corner)) return false;
        }
        OutCenter=Ground+FVector(0,0,Context.HalfExtent.Z+ContactLift);
        if (Context.World->OverlapBlockingTestByProfile(OutCenter,FQuat::Identity,TEXT("BlockAllDynamic"),FCollisionShape::MakeBox(Context.HalfExtent+FVector(2,2,0)),Context.Query))
            return RejectCandidate(Context.World,TEXT("terminal_box_occupied"));
        return true;
    }

    /** 检查接近线段不会穿过即将生成的两座Box；使用玩家实际半径保守扩张，不用大Nav代理代替玩家。 */
    bool AvoidPlannedPair(const FVector& Start,const FVector& End,const FDemoTerminalPlacementPair& Pair,const FVector& HalfExtent,float Radius)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        const FVector A(Start.X,Start.Y,0),B(End.X,End.Y,0); // 同层通路已单独验证，二维Box相交这里只防新终端挡住所证明路径。
        for (const FVector& Center:{Pair.Shop,Pair.Next})
        {
            const FVector Half(HalfExtent.X+Radius+2,HalfExtent.Y+Radius+2,1);
            const FVector Flat(Center.X,Center.Y,0);
            if (FMath::LineBoxIntersection(FBox(Flat-Half,Flat+Half),A,B,B-A)) return RejectCandidate(nullptr,TEXT("approach_crosses_planned_terminal"));
        }
        return true;
    }

    /** 玩家实际胶囊沿有限、同源同层支撑点Sweep；只忽略会自行让路的角色，不忽略环境或新Box计划。 */
    bool WalkSegment(const FPlacementContext& Context,const FDemoTerminalPlacementOrigin& Origin,const FVector& Start,const FVector& End,const FDemoTerminalPlacementPair& Pair)
    {
        DEMO_LOG_STATIC_TICK(Context.World);
        if (!AvoidPlannedPair(Start,End,Pair,Context.HalfExtent,Origin.CapsuleRadius)) return false;
        FCollisionQueryParams Query=Context.Query; // 通路允许队友让路，终端自身占位查询则不忽略任何玩家。
        for (TActorIterator<ADemoCharacter> It(Context.World);It;++It) Query.AddIgnoredActor(*It);
        const int32 Count=FMath::Clamp(FMath::CeilToInt(FVector::Dist2D(Start,End)/40.f),1,24); // 每段最多24个真实支撑点，长度不超过附近搜索预算。
        const FCollisionShape Capsule=FCollisionShape::MakeCapsule(Origin.CapsuleRadius,Origin.CapsuleHalfHeight);
        FVector Previous=Start+FVector(0,0,Origin.CapsuleHalfHeight+ContactLift);
        for (int32 Index=1;Index<=Count;++Index)
        {
            const FVector Reference=FMath::Lerp(Start,End,static_cast<float>(Index)/Count);
            FVector Ground;
            if (!ReadFloor(Context,Reference,32.f,64.f,GroundTolerance,Ground)
                || FMath::Abs(Ground.Z-(Previous.Z-Origin.CapsuleHalfHeight-ContactLift))>Origin.MaxStepHeight) return RejectCandidate(Context.World,TEXT("walk_support_or_step"));
            const FVector Center=Ground+FVector(0,0,Origin.CapsuleHalfHeight+ContactLift);
            if (Context.World->OverlapBlockingTestByProfile(Center,FQuat::Identity,TEXT("Pawn"),Capsule,Query)
                || Context.World->SweepTestByProfile(Previous,Center,FQuat::Identity,TEXT("Pawn"),Capsule,Query)) return RejectCandidate(Context.World,TEXT("actual_player_capsule_blocked"));
            Previous=Center;
        }
        return true;
    }

    /** 优先直接实际胶囊路径，其次一个同层L形折点；Nav仅作额外完整路径证明，不挪终端候选坐标。 */
    bool CanApproach(const FPlacementContext& Context,const FDemoTerminalPlacementOrigin& Origin,const FVector& Start,const FVector& Approach,const FVector& Side,const FDemoTerminalPlacementPair& Pair)
    {
        DEMO_LOG_STATIC_TICK(Context.World);
        if (WalkSegment(Context,Origin,Start,Approach,Pair)) return true;
        FVector Turn; // 侧移后再前进，给狭走廊的纵向双终端保留真实玩家绕行通道。
        const FVector TurnReference=Start+Side*(Context.HalfExtent.X+Origin.CapsuleRadius+24.f);
        if (ReadFloor(Context,TurnReference,32.f,64.f,GroundTolerance,Turn)
            && WalkSegment(Context,Origin,Start,Turn,Pair) && WalkSegment(Context,Origin,Turn,Approach,Pair)) return true;
        UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(Context.World);
        const ANavigationData* Data=Nav?Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate):nullptr;
        FNavLocation From,To; // 投影只查通路，返回值绝不替换Pair或实际地面点。
        if (!Data || !Nav->ProjectPointToNavigation(Start,From,FVector(200,200,64),Data) || !Nav->ProjectPointToNavigation(Approach,To,FVector(200,200,64),Data)
            || FMath::Abs(From.Location.Z-Start.Z)>GroundTolerance || FMath::Abs(To.Location.Z-Approach.Z)>GroundTolerance) return RejectCandidate(Context.World,TEXT("no_same_layer_nav_projection_or_actual_walk"));
        FPathFindingQuery Query(Context.World,*Data,From.Location,To.Location); Query.SetAllowPartialPaths(false);
        const FPathFindingResult Path=Nav->FindPathSync(Query);
        if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial() || Path.Path->GetPathPoints().Num()>32 || Path.Path->GetLength()>1600.f) return RejectCandidate(Context.World,TEXT("no_bounded_complete_nav_or_actual_walk"));
        FVector Previous=Start;
        for (const FNavPathPoint& Point:Path.Path->GetPathPoints())
        {
            FVector Ground;
            if (!ReadFloor(Context,Point.Location,24.f,40.f,GroundTolerance,Ground) || !AvoidPlannedPair(Previous,Ground,Pair,Context.HalfExtent,Origin.CapsuleRadius)) return false;
            Previous=Ground;
        }
        return AvoidPlannedPair(Previous,Approach,Pair,Context.HalfExtent,Origin.CapsuleRadius);
    }
}

bool DemoTerminalPlacement::ResolvePair(UWorld& World,const FDemoAreaSnapshot& Area,const FVector& HalfExtent,
    const TArray<FDemoTerminalPlacementOrigin>& Origins,FDemoTerminalPlacementPair& Out,FString& Error)
{
    DEMO_LOG_STATIC_CALL(&World);
    Out=FDemoTerminalPlacementPair(); Error.Reset();
    FPlacementContext Context; // 只建立一次实际来源/碰撞上下文，所有候选共用，不扫描外来World。
    if (!BuildContext(World,Area,HalfExtent,Context,Error)) return false;
    if (Origins.IsEmpty())
    {
        const FVector Offsets[]={FVector::ZeroVector,FVector(120,0,0),FVector(-120,0,0),FVector(0,120,0),FVector(0,-120,0),FVector(180,0,0),FVector(-180,0,0)}; // Hub/检查点保作者位置，仅有界成对避让占位。
        for (const FVector& Offset:Offsets)
        {
            ++Out.Attempts;
            if (FitTerminal(Context,Area.ShopAnchor+Offset,Out.Shop,true) && FitTerminal(Context,Area.NextAnchor+Offset,Out.Next,true)
                && FVector::Dist2D(Out.Shop,Out.Next)>HalfExtent.X*2+4.f) return true;
        }
        Error=TEXT("作者终端锚点附近没有同源、完整支撑且无占位的一对位置"); return false;
    }
    // 配对中心距胜利位置cm。2026-11-20用户反馈150cm起步会"生成在脸上"：优先约4m，放不下再试更远/更近，250cm为小房间兜底；终端中心不超过VictoryMaxDistance。
    const float Radii[]={400.f,320.f,500.f,600.f,250.f};
    const float Angles[]={0.f,30.f,-30.f,60.f,-60.f,90.f,-90.f,120.f,-120.f,150.f,-150.f,180.f}; // 确定性优先前方，墙边允许两侧/背后，不使用随机扫参。
    for (const FDemoTerminalPlacementOrigin& Origin:Origins)
    {
        FVector Start; // 胜利时脚下实际层；向下有限找地，跳跃清关不会挑到上方屋顶。
        if (!ReadFloor(Context,Origin.Location-FVector(0,0,Origin.CapsuleHalfHeight),24.f,450.f,450.f,Start)) continue;
        for (float Separation:PairSeparations) for (float Radius:Radii) for (float Angle:Angles) for (int32 Layout=0;Layout<2;++Layout)
        {
            ++Out.Attempts;
            const FVector Forward=Origin.Forward.RotateAngleAxis(Angle,FVector::UpVector).GetSafeNormal2D();
            const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
            const FVector Center=Start+Forward*Radius;
            const FVector Axis=Layout==0?Right:Forward; // 横向或纵向成对，狭走廊也能在侧边留下实际玩家通路。
            if (!FitTerminal(Context,Center-Axis*(Separation*.5f),Out.Shop) || !FitTerminal(Context,Center+Axis*(Separation*.5f),Out.Next)
                || FMath::Abs(Out.Shop.Z-Out.Next.Z)>GroundTolerance
                || FVector::Dist2D(Start,Out.Shop)<VictoryMinDistance || FVector::Dist2D(Start,Out.Next)<VictoryMinDistance
                || FVector::Dist2D(Start,Out.Shop)>VictoryMaxDistance || FVector::Dist2D(Start,Out.Next)>VictoryMaxDistance) continue;
            {
                const FVector Visual(VisualHalfClearance,VisualHalfClearance,Context.HalfExtent.Z); // 方形显示占位与朝向无关；只查环境阻挡，玩家占位已由FitTerminal根盒检查。
                FCollisionQueryParams VisualQuery=Context.Query;
                for (TActorIterator<ADemoCharacter> It(&World);It;++It) VisualQuery.AddIgnoredActor(*It);
                if (World.OverlapBlockingTestByProfile(Out.Shop,FQuat::Identity,TEXT("BlockAllDynamic"),FCollisionShape::MakeBox(Visual),VisualQuery)
                    || World.OverlapBlockingTestByProfile(Out.Next,FQuat::Identity,TEXT("BlockAllDynamic"),FCollisionShape::MakeBox(Visual),VisualQuery))
                { RejectCandidate(&World,TEXT("terminal_visual_clearance")); continue; }
            }
            FCollisionQueryParams Sight=Context.Query; // 用服务器身体高度可见性，不从客户端Camera读取胜利位置。
            for (TActorIterator<ADemoCharacter> It(&World);It;++It) Sight.AddIgnoredActor(*It);
            FHitResult Wall;
            if (World.LineTraceSingleByObjectType(Wall,Start+FVector(0,0,90),Out.Shop,Context.FloorObjects,Sight)
                || World.LineTraceSingleByObjectType(Wall,Start+FVector(0,0,90),Out.Next,Context.FloorObjects,Sight)) continue;
            // Hub外观网格以相对Yaw+90挂载，本地+Y操作面落在Actor −X；令Actor +X指向远离胜利点即让操作面朝玩家。吸附90°保持60×60正方形根盒占位与已做检查一致。
            const FVector Away=((Out.Shop+Out.Next)*.5f-Start).GetSafeNormal2D();
            const float Yaw=FRotator::NormalizeAxis(FMath::GridSnap(FMath::RadiansToDegrees(FMath::Atan2(Away.Y,Away.X)),90.f));
            const FVector Front=-FRotator(0.f,Yaw,0.f).Vector(); // 吸附后两座终端共同的操作面方向（指向玩家一侧）。
            const FVector Turn=FVector::CrossProduct(FVector::UpVector,Front); // 直线受阻时L形绕行的侧向。
            // 3～5米间隔下不存在同时贴近两座终端的单一站位：分别验证每座终端操作面前方站位（距中心约118cm，在250cm交互距离内）可从胜利点走到。
            bool bReachable=true;
            for (const FVector& Terminal:{Out.Shop,Out.Next})
            {
                FVector Approach;
                if (!ReadFloor(Context,Terminal-FVector(0,0,HalfExtent.Z+ContactLift)+Front*(HalfExtent.X+Origin.CapsuleRadius+24.f),32.f,64.f,GroundTolerance,Approach)
                    || FVector::Dist(Approach+FVector(0,0,Origin.CapsuleHalfHeight),Terminal)>250.f
                    || !CanApproach(Context,Origin,Start,Approach,Turn,Out)) { bReachable=false; break; }
            }
            if (!bReachable) continue;
            Out.PlayerKey=Origin.PlayerKey; Out.Yaw=Yaw;
            UE_LOG(LogFPSDemo,Log,TEXT("TERMINAL_PAIR_PLACED key=%s attempts=%d source=%s origin=%s shop=%s next=%s layout=%d radius=%.0f separation=%.0f yaw=%.0f"),*Out.PlayerKey,Out.Attempts,*Context.Source->GetOutermost()->GetName(),*Origin.Location.ToString(),*Out.Shop.ToString(),*Out.Next.ToString(),Layout,Radius,Separation,Out.Yaw);
            return true;
        }
    }
    Error=FString::Printf(TEXT("%d个附近候选均未通过同源地面/支撑/墙体/占位/实际接近通路"),Out.Attempts);
    return false;
}

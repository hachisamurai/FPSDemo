// 职责：权威危险物轨迹/命中与复制可视化；依赖会话撤销、活参战者筛选、GAS健康和冲刺标签。
#include "AI/DemoRosterHazard.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyTargeting.h"
#include "Characters/DemoCharacter.h"
#include "Game/Flow/DemoCombatSession.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "AbilitySystemComponent.h" // 冲刺标签查询需要完整ASC声明，不能依赖Unity间接包含。
#include "Debug/DemoLog.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ADemoRosterHazard::ADemoRosterHazard()
{
    DEMO_LOG_CALL(); bReplicates=true; SetReplicateMovement(true); PrimaryActorTick.bCanEverTick=true;
    ShapeMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Warning")); SetRootComponent(ShapeMesh);
    ShapeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); ShapeMesh->SetCanEverAffectNavigation(false); ShapeMesh->CastShadow=false;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere")); // 引擎Cook资源，无手写uasset。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube")); // 环段/电弧共用。
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/EnemyAttacks/Roster/M_RosterGlow")); // 已有正式透明发光材质。
    ShapeMesh->SetStaticMesh(Sphere.Object); ShapeMesh->SetMaterial(0,Glow.Object);
    for (int32 Index=0;Index<32;++Index) // 固定有界表现组件，全部NoCollision，无逐段伤害。
    {
        UStaticMeshComponent* Segment=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Arc%d"),Index)); // Actor拥有，无异步裸引用。
        Segment->SetupAttachment(ShapeMesh); Segment->SetAbsolute(true,true,true); Segment->SetStaticMesh(Cube.Object); Segment->SetMaterial(0,Glow.Object);
        Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision); Segment->SetCanEverAffectNavigation(false); Segment->CastShadow=false; Segment->SetHiddenInGame(true);
        RingSegments.Add(Segment);
    }
}
void ADemoRosterHazard::Initialize(ADemoEnemy* Source,const FDemoHazardVisual& InVisual,UDemoRosterSalvo* Salvo,float Damage)
{
    DEMO_LOG_CALL();
    if (!HasAuthority()||!Source||!Source->IsCombatReady()||!Source->IsCombatSessionValid()||!Salvo||!FMath::IsFinite(Damage)||Damage<0)
    { UE_LOG(LogFPSDemo,Warning,TEXT("ROSTER_HAZARD_REJECT source/session/ledger/damage")); Cancel(); return; }
    Shooter=Source; SetOwner(Source); Visual=InVisual; Visual.Started=GetWorld()->GetTimeSeconds(); Ledger=Salvo; DamagePoints=Damage;
    if (UDemoCombatSession* Session=UDemoCombatSession::FindActive(GetWorld())) // 会话同步登记弱UObject委托，不跨线程。
        if (!Session->RegisterCleanup(this,FSimpleDelegate::CreateUObject(this,&ADemoRosterHazard::Cancel))) { Cancel(); return; }
    SetActorLocation(Visual.Origin); OnRep_Visual(); ForceNetUpdate();
}
void ADemoRosterHazard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(ADemoRosterHazard,Visual); }
void ADemoRosterHazard::Cancel()
{
    DEMO_LOG_CALL(); if (bConsumed) return; bConsumed=true; SetActorHiddenInGame(true); SetActorEnableCollision(false); Destroy();
}
void ADemoRosterHazard::OnRep_Visual()
{
    DEMO_LOG_TICK();
    const AGameStateBase* State=GetWorld()->GetGameState(); // 客户使用服务器World秒，时间未同步时暂用本地秒。
    const float Age=FMath::Max(0.f,float(State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds())-Visual.Started); // 不使用系统钟，暂停冻结。
    const bool bRing=Visual.Shape==EDemoHazardShape::Ring; // 环只显示环带，不显示填充盘。
    ShapeMesh->SetHiddenInGame(bRing);
    UMaterialInstanceDynamic* Material=Cast<UMaterialInstanceDynamic>(ShapeMesh->GetMaterial(0)); // 当前组件MID，首帧才创建。
    if (!Material) Material=ShapeMesh->CreateAndSetMaterialInstanceDynamic(0);
    if (Material) // 组件持有MID，仅本帧借用。
    { Material->SetVectorParameterValue(TEXT("Color"),Visual.Color); Material->SetVectorParameterValue(TEXT("GlowColor"),Visual.Color); }
    if (Visual.Shape==EDemoHazardShape::Shot) ShapeMesh->SetWorldScale3D(FVector(.22f));
    else if (Visual.Shape==EDemoHazardShape::Mortar&&Age>=Visual.Warning) ShapeMesh->SetWorldScale3D(FVector(.32f));
    else if (Visual.Shape==EDemoHazardShape::Lane)
    {
        SetActorLocation((Visual.Origin+Visual.Target)*.5f-FVector(0,0,85));
        SetActorRotation((Visual.Target-Visual.Origin).Rotation());
        ShapeMesh->SetWorldScale3D(FVector(FVector::Dist(Visual.Target,Visual.Origin)/100.f,Visual.Radius/50.f,.05f));
    }
    else
    {
        if (Visual.Shape!=EDemoHazardShape::Mortar||Age<Visual.Warning) SetActorLocation(Visual.Target-FVector(0,0,85));
        ShapeMesh->SetWorldScale3D(FVector(Visual.Radius/50.f,Visual.Radius/50.f,.035f+(Age>=Visual.Warning?.25f:0.f)));
    }
    for (int32 Index=0;Index<RingSegments.Num();++Index) // 本地32段，无碰撞；环半径与权威计算使用同一函数。
    {
        UStaticMeshComponent* Segment=RingSegments[Index]; // Actor拥有；客户端只借用表现对象。
        Segment->SetHiddenInGame(!bRing); if (!bRing) continue;
        const float Radius=Age<Visual.Warning?140.f:Visual.Radius*FMath::Clamp((Age-Visual.Warning)/3.f,0.f,1.f); // 前摇小圈，之后3秒传播。
        const float Angle=2.f*PI*Index/RingSegments.Num(); // 每段中心角弧度。
        Segment->SetWorldLocation(Visual.Origin+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,-65));
        Segment->SetWorldRotation(FRotator(0,FMath::RadiansToDegrees(Angle)+90,0));
        Segment->SetWorldScale3D(FVector(FMath::Max(.12f,2*PI*Radius/3200.f),.6f,.12f));
    }
}
void ADemoRosterHazard::HitPlayer(ADemoCharacter* Player,const FVector& From,bool bDash)
{
    DEMO_LOG_CALL();
    if (bConsumed||!Shooter.IsValid()||!Shooter->IsCombatReady()||!Shooter->IsCombatSessionValid()
        ||!DemoEnemyTargeting::IsAliveParticipant(Player)||!Ledger||Ledger->HitPlayers.Contains(Player))
    { UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_HIT_REJECT consumed/source/session/participant/ledger")); return; }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RosterLOS),false,Shooter.Get()); // 墙体遮挡AOE，其他敌人不成为隐形掩体。
    Query.AddIgnoredActor(this);
    for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) Query.AddIgnoredActor(*It); // 有界本关敌人，避免友方模型挡住落雷。
    FHitResult Hit; // 最近Visibility阻挡；无阻挡或目标本人均允许。
    if (GetWorld()->LineTraceSingleByChannel(Hit,From,Player->GetActorLocation(),ECC_Visibility,Query)&&Hit.GetActor()!=Player)
    { UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_HIT cover blocked")); return; }
    Ledger->HitPlayers.Add(Player); // 冲刺也消费一次机会，不能同一环下一帧补伤害。
    if (bDash&&Player->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoTags::DashEvading))
    { UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_RING_EVADED no heal reward")); return; }
    const bool bProjectile=Visual.Shape==EDemoHazardShape::Shot; // 飞弹走冻结发射轨迹，迫击/地面/扩散环走该次真实爆心。
    const FVector IncomingDirection=bProjectile?(Visual.Origin-Visual.Target).GetSafeNormal():FVector::ZeroVector; // Visual在生成时冻结，不反查之后的Shooter位置。
    DemoEffects::ApplyDamage(Shooter->GetAbilitySystemComponent(),Player->GetAbilitySystemComponent(),DamagePoints,
        bProjectile?EDemoPlayerDamageKind::Projectile:EDemoPlayerDamageKind::Area,From,IncomingDirection,&Hit);
    UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_HIT shape=%d player=%s damage=%.1f"),int32(Visual.Shape),*Player->GetName(),DamagePoints);
}
void ADemoRosterHazard::ResolveArea(const FVector& Position,float PreviousRadius,float CurrentRadius)
{
    DEMO_LOG_CALL();
    for (TActorIterator<ADemoCharacter> It(GetWorld());It;++It) // 遍历活参战者，每人独立账本/冲刺/视线。
    {
        ADemoCharacter* Player=*It; // 本次同步调用借用，不捕获跨帧指针。
        if (!DemoEnemyTargeting::IsAliveParticipant(Player)) continue;
        const FVector Location=Player->GetActorLocation(); // 世界厘米。
        bool bInside=false; // 距离通过后仍需LOS。
        if (Visual.Shape==EDemoHazardShape::Lane) bInside=FMath::PointDistToSegment(Location,Visual.Origin,Visual.Target)<=Visual.Radius;
        else if (Visual.Shape==EDemoHazardShape::Ring)
        {
            const float Distance=FVector::Dist2D(Location,Position); // 水平距离，环高度单独限制。
            const float Last=PreviousDistances.Contains(Player)?PreviousDistances[Player]:Distance; // 第一次观测只测试当前扫过区域。
            bInside=FMath::Abs(Location.Z-Position.Z)<220.f && FMath::Min(Last-PreviousRadius,Distance-CurrentRadius)<=60.f
                &&FMath::Max(Last-PreviousRadius,Distance-CurrentRadius)>=-60.f;
            PreviousDistances.Add(Player,Distance);
        }
        else bInside=FVector::Dist2D(Location,Position)<=Visual.Radius&&FMath::Abs(Location.Z-Position.Z)<250.f;
        if (bInside) HitPlayer(Player,Position,Visual.Shape==EDemoHazardShape::Ring);
        if (bConsumed) return; // 致死GE可能同栈撤销整个会话。
    }
}
bool ADemoRosterHazard::SweepProjectile(const FVector& From,const FVector& To)
{
    DEMO_LOG_TICK();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RosterProjectile),false,this); // 忽略友军，保留所有墙体/玩家实体碰撞。
    for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) Query.AddIgnoredActor(*It);
    FHitResult Hit; // 连续球扫掠的首个阻挡；不会越过薄墙或天花板。
    if (GetWorld()->SweepSingleByChannel(Hit,From,To,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12.f),Query))
    {
        SetActorLocation(Hit.Location);
        if (Visual.Shape==EDemoHazardShape::Shot) HitPlayer(Cast<ADemoCharacter>(Hit.GetActor()),From,false);
        else if (Cast<ADemoCharacter>(Hit.GetActor())) ResolveArea(Hit.Location); // 玩家主动撞到在飞迫击弹也算真实命中，不因胶囊侧面法线而吞伤。
        else if (FVector::Dist2D(Hit.Location,Visual.Target)<=Visual.Radius && Hit.ImpactNormal.Z>.4f) ResolveArea(Hit.Location+FVector(0,0,90));
        UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_PROJECTILE_BLOCK actor=%s"),*GetNameSafe(Hit.GetActor())); Cancel(); return true;
    }
    SetActorLocation(To); return false;
}
void ADemoRosterHazard::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (!HasAuthority()) { OnRep_Visual(); return; }
    if (bConsumed) return;
    if (!Shooter.IsValid()||!Shooter->IsCombatReady()||!Shooter->IsCombatSessionValid()) { Cancel(); return; }
    const float Age=FMath::Max(0.f,float(GetWorld()->GetTimeSeconds())-Visual.Started); // 统一float精度；同帧生成的零前摇爆炸不能因double->float舍入得到负Age而漏结算。
    OnRep_Visual();
    if (Age<Visual.Warning) return;
    const float Flight=Age-Visual.Warning; // 预警结束之后的飞行/传播秒。
    if (Visual.Shape==EDemoHazardShape::Shot||Visual.Shape==EDemoHazardShape::Mortar)
    {
        const float End=FMath::Min(Flight,Visual.Shape==EDemoHazardShape::Mortar?.9f:4.f); // 抛射0.9秒、普通弹最多4秒。
        while (PreviousFlight<End&&!bConsumed) // 20ms亚步，对高抛弧线细分碰撞而不是只扫整帧弦。
        {
            const float Next=FMath::Min(End,PreviousFlight+.02f); // 亚步秒。
            FVector Position; // 本亚步解析轨迹世界点。
            if (Visual.Shape==EDemoHazardShape::Shot) Position=Visual.Origin+(Visual.Target-Visual.Origin).GetSafeNormal()*1600.f*Next;
            else { const float T=Next/.9f; /* 固定落点插值比例；额外450cm抛物线高度，可被顶棚阻挡。 */ Position=FMath::Lerp(Visual.Origin,Visual.Target,T)+FVector(0,0,1800*T*(1-T)); }
            const FVector From=PreviousFlight==0?Visual.Origin:GetActorLocation(); // 初发从枪口，不能从地面预警盘扫起。
            PreviousFlight=Next; if (SweepProjectile(From,Position)) return;
        }
        if (Flight>=(Visual.Shape==EDemoHazardShape::Mortar?.9f:4.f))
        { if (Visual.Shape==EDemoHazardShape::Mortar) ResolveArea(Visual.Target); Cancel(); }
    }
    else if (Visual.Shape==EDemoHazardShape::Ring)
    {
        const float Radius=Visual.Radius*FMath::Clamp(Flight/3.f,0.f,1.f); // 与客户端环带相同速度。
        ResolveArea(Visual.Origin,PreviousRing,Radius); PreviousRing=Radius; if (Flight>=3.f) Cancel();
    }
    else if (Visual.Shape==EDemoHazardShape::Marker) { if (Flight>=.9f) Cancel(); } // 迫击落点覆盖整个飞行期；其他Marker由GA提前撤销。
    else { ResolveArea(Visual.Shape==EDemoHazardShape::Lane?Visual.Origin:Visual.Target); Cancel(); }
}

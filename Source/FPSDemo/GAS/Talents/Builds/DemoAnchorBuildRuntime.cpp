// 职责：B06服务器部署/充能/撤离与协同策略。复用UE导航和原G标记事实，公开Actor只表达结果，派生脉冲复用基类防递归GAS结算。
#include "GAS/Talents/Builds/DemoAnchorBuildRuntime.h"
#include "GAS/Talents/Builds/DemoTacticalAnchorActor.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Builds/DemoRunBuildSettings.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerState.h"
#include "AI/DemoEnemy.h"
#include "GAS/DemoAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"

void UDemoAnchorBuildRuntime::Handle(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK();
    if (!Active() || Selection().CoreId != TEXT("B06"))
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_EVENT_REJECT inactive/core")); return; }
    if (Event.Action == EDemoRunBuildAction::PingAccepted) Deploy(Event.Location);
    else if (Event.Action == EDemoRunBuildAction::AfterHit) ReceiveHit(Build(), Event);
}

bool UDemoAnchorBuildRuntime::TryGroundPoint(const FVector& Point, FVector& OutGround) const
{
    DEMO_LOG_CALL();
    const UDemoExpandedBuildSettings* Config = GetDefault<UDemoExpandedBuildSettings>(); // 本栈读取服务器已预检配置，不接受客户端数值。
    if (!Active() || Point.ContainsNaN() || FVector::DistSquared(Pawn()->GetActorLocation(), Point) > FMath::Square(Config->AnchorDeployRange + 600.f))
    { UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY_REJECT invalid/far ping")); return false; }
    UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); // World拥有，只有低频部署同步查询。
    FNavLocation NavGround; // 小范围XY投影，允许敌人躯干命中点向下落地，禁止吸附到墙后远处区域。
    if (!Navigation || !Navigation->ProjectPointToNavigation(Point, NavGround, FVector(120.f, 120.f, 600.f))
        || FVector::DistSquared2D(Point, NavGround.Location) > FMath::Square(120.f)
        || FMath::Abs(Point.Z - NavGround.Location.Z) > 600.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY_REJECT no nearby navigation ground")); return false; }
    FHitResult Floor; // NavMesh不能代替实际地面；仅静态地面射线避免标敌时把敌人身体当脚下地板。
    const FCollisionObjectQueryParams GroundObjects(ECC_WorldStatic); // UE静态地面/墙物体，不接受敌人或玩家表面。
    FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(DemoAnchorGround), false, Pawn()); // 只借用本次同步查询。
    if (!GetWorld()->LineTraceSingleByObjectType(Floor, NavGround.Location + FVector(0.f, 0.f, 80.f), NavGround.Location - FVector(0.f, 0.f, 100.f), GroundObjects, GroundQuery)
        || Floor.ImpactNormal.Z < .65f)
    { UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY_REJECT no supported walkable floor")); return false; }
    OutGround = Floor.ImpactPoint + FVector(0.f, 0.f, 2.f); // 圆盘在地面上方2cm避免Z争夺，规则中心保持同一固定位置。
    if (OutGround.ContainsNaN() || FVector::DistSquared(Pawn()->GetActorLocation(), OutGround) > FMath::Square(Config->AnchorDeployRange))
    { UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY_REJECT ground outside range")); return false; }
    FCollisionQueryParams SightQuery(SCENE_QUERY_STAT(DemoAnchorPlacementSight), false, Pawn()); // 真实墙/门/设施均能阻挡，身体只作为有限次可忽略遮挡。
    for (int32 Attempt = 0; Attempt < 5; ++Attempt) // 最多跳过四个角色身体；不扫描全敌、不穿过静态/动态障碍。
    {
        FHitResult Obstacle; // 每次只检查新的最近阻挡；目标敌人可被正常Ping但不能让其躯干阻止向脚下投影。
        if (!GetWorld()->LineTraceSingleByChannel(Obstacle, Pawn()->GetSharedViewLocation(), OutGround + FVector(0.f, 0.f, 35.f), ECC_Visibility, SightQuery)) return true;
        if (Attempt < 4 && (Cast<ADemoEnemy>(Obstacle.GetActor()) || Cast<ADemoCharacter>(Obstacle.GetActor())))
        { SightQuery.AddIgnoredActor(Obstacle.GetActor()); continue; }
        UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY_REJECT blocked sight actor=%s"), *GetNameSafe(Obstacle.GetActor()));
        return false;
    }
    UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY_REJECT sight body limit"));
    return false;
}

void UDemoAnchorBuildRuntime::Deploy(const FVector& Point)
{
    DEMO_LOG_CALL();
    if (!Active()) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_DEPLOY_REJECT inactive")); return; }
    const float Time = Now(); // 固定本次事务时刻，不在导航查询间用墙钟改变凭据。
    if (Anchor.IsValid())
    { UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY_REJECT existing anchor; normal ping retained")); return; }
    const bool bRelay = Selection().Branch7 == TEXT("B06-C") && RelayEndTime > Time; // 只有实际伤敌后留下的一次令牌可绕过CD。
    if (!bRelay && Time < NextDeployTime)
    { UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY_REJECT cooldown %.2f; normal ping retained"), NextDeployTime - Time); return; }
    FVector Ground; // 全部校验完成前不消耗迁移/冷却，也不生成Actor。
    if (!TryGroundPoint(Point, Ground)) return;
    int32 ActiveAnchors = 0; // 本World硬上限四个活跃场；退休脉冲已经断路由，不算游戏区域。
    for (TActorIterator<ADemoTacticalAnchorActor> It(GetWorld()); It; ++It) if (It->GetRuntime()) ++ActiveAnchors;
    if (ActiveAnchors >= 4)
    { UE_LOG(LogFPSDemo, Warning, TEXT("BUILD_ANCHOR_DEPLOY_REJECT four active areas")); return; }
    ADemoPlayerState* OwnerPS = Pawn()->GetPlayerState<ADemoPlayerState>(); // 仅公开所属玩家，不更改网络Actor拥有权。
    if (!OwnerPS) { UE_LOG(LogFPSDemo, Warning, TEXT("BUILD_ANCHOR_DEPLOY_REJECT missing player state")); return; }
    FActorSpawnParameters Spawn; // 纯表现不碰撞，落点规则已由服务器完成，不让Spawn碰撞调整固定中心。
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADemoTacticalAnchorActor* Created = GetWorld()->SpawnActor<ADemoTacticalAnchorActor>(ADemoTacticalAnchorActor::StaticClass(), Ground, FRotator::ZeroRotator, Spawn);
    if (!Created) { UE_LOG(LogFPSDemo, Warning, TEXT("BUILD_ANCHOR_DEPLOY_REJECT visual spawn failed")); return; }
    const UDemoExpandedBuildSettings* Config = GetDefault<UDemoExpandedBuildSettings>(); // 预检通过的集中参数，Actor不另读客户端设置。
    Anchor = Created; Center = Ground; Charge = 0; ChargeEndTime = 0.f; ShotCharges.Reset();
    ActiveRadius = Config->AnchorRadius; ChargeLimit = Config->AnchorChargeHits; ChargeWindowSeconds = Config->AnchorChargeSeconds;
    PulseDamage = Config->AnchorDamage; PulseTargetLimit = Config->AnchorTargets; PulseConeCos = Config->AnchorConeCos;
    DirectedRange = Config->AnchorDirectedRange; DirectedConeCos = Config->AnchorDirectedConeCos; PelletDoubleThreshold = Config->AnchorPelletDoubleThreshold;
    RelayWindowSeconds = Config->AnchorChainSeconds;
    AnchorEndTime = Time + Config->AnchorLifetime; NextDeployTime = Time + Config->AnchorCooldown;
    bEntered = IsInside(Pawn()); bRelayPlacement = bRelay; RelayEndTime = 0.f; // 新链重置一次迁移资格；接力链不能再次生成令牌。
    Created->Configure(this, OwnerPS, ActiveRadius, AnchorEndTime);
    Created->SetCharge(0, ChargeLimit, false);
    EnsureMaintenance(); RefreshPresentation();
    UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_DEPLOY owner=%s relay=%d point=%s radius=%.1f expires=%.2f"), *GetNameSafe(OwnerPS), bRelay, *Center.ToString(), Config->AnchorRadius, AnchorEndTime);
}

bool UDemoAnchorBuildRuntime::IsInside(const ADemoCharacter* Character) const
{
    DEMO_LOG_TICK();
    if (!IsValid(Character) || Character->GetWorld() != GetWorld())
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_INSIDE_REJECT character/world")); return false; }
    return FVector::DistSquared2D(Character->GetActorLocation(), Center) <= FMath::Square(ActiveRadius)
        && FMath::Abs(Character->GetActorLocation().Z - Center.Z) <= 300.f;
}

void UDemoAnchorBuildRuntime::ReceiveHit(UDemoRunBuildComponent* ActorBuild, const UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK();
    const bool bPelletBonusCandidate = Event.Shot.PlannedProjectiles > 1 && Event.ShotPelletsHit >= PelletDoubleThreshold;
    if (!Active() || !Anchor.IsValid() || !ActorBuild || Event.Action != EDemoRunBuildAction::AfterHit || Event.bSecondary
        || (!Event.bFirstEnemy && !bPelletBonusCandidate)
        || !FMath::IsFinite(Event.Damage) || Event.Damage <= 0.f || !Event.ShotId.IsValid())
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_HIT_REJECT inactive/anchor/direct receipt/duplicate")); return; }
    const int32 PrevContribution = ShotCharges.FindRef(Event.ShotId);
    if (PrevContribution >= 2 || (PrevContribution == 1 && !bPelletBonusCandidate)) return;
    const bool bOwnerHit = ActorBuild == Build(); // D仅共享充能，不能替本人触发固守分支兑现。
    if (!bOwnerHit && Selection().Branch7 != TEXT("B06-D"))
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_HIT_REJECT teammate without D")); return; }
    UAbilitySystemComponent* ShooterASC = ActorBuild->GetASC(); // 原PS ASC，不把传入Event.Pawn当可信当前Avatar。
    ADemoCharacter* Shooter = ShooterASC ? Cast<ADemoCharacter>(ShooterASC->GetAvatarActor()) : nullptr; // 只借用当前权威射手。
    if (!ActorBuild->GetOwner() || !ActorBuild->GetOwner()->HasAuthority() || ActorBuild->GetWorld() != GetWorld() || !ActorBuild->IsEncounterEnabled()
        || !Shooter || Shooter->IsCoopEliminated() || !Shooter->GetDemoAttributes() || Shooter->GetDemoAttributes()->GetHealth() <= 0.f
        || !IsInside(Shooter) || !IsInside(Pawn())) // 本人离场尚未到0.1s维护时，队友也不能抢先兑现一笔。
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_HIT_REJECT shooter/current phase/alive/inside")); return; }
    const float Time = Now(); // 同步弹丸回调使用一个时间点，固定窗口不被每颗弹丸延期。
    if (Time >= AnchorEndTime) { ClearAnchor(TEXT("Lifetime")); RefreshPresentation(); return; }
    if (ChargeEndTime > 0.f && Time >= ChargeEndTime)
    { Charge = 0; ChargeEndTime = 0.f; UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_CHARGE_EXPIRED")); }
    if (Charge >= ChargeLimit)
    {
        // 只有尚未参与本次锚点充能的新一枪才可触发B06-B，满档同枪后续弹丸不能抢触发。
        if (PrevContribution == 0 && bOwnerHit && Selection().Branch3 == TEXT("B06-B"))
        {
            FVector Direction = !Event.Direction.ContainsNaN() ? Event.Direction.GetSafeNormal2D() : FVector::ZeroVector; // 脉冲是地面扇形，优先原枪发射冻结的水平朝向。
            if (Direction.IsNearlyZero()) Direction = Shooter->GetBaseAimRotation().Vector().GetSafeNormal2D(); // 兼容缺方向的旧内部事件才回退当前服务器朝向。
            if (Direction.IsNearlyZero()) Direction = Shooter->GetActorForwardVector().GetSafeNormal2D(); // 极限垂直瞄准不能被基类解释成整圆脉冲。
            if (Direction.IsNearlyZero()) Direction = FVector::ForwardVector; // 防御性有限单位方向，不把B分支退化为无方向范围攻击。
            Release(Direction);
        }
        else UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_HIT_REJECT charged waiting for owner release"));
        return; // 基础/A等待撤离；D队友不能把满档重复兑现。
    }
    if (PrevContribution == 0 && ShotCharges.Num() >= 256)
    { UE_LOG(LogFPSDemo, Warning, TEXT("BUILD_ANCHOR_CHARGE_REJECT bounded shot ledger")); return; }
    const UDemoRunBuildSettings* RunSettings = GetDefault<UDemoRunBuildSettings>();
    int32 Add = 0;
    if (PrevContribution == 0)
    {
        Add = (Event.Shot.FireInterval >= RunSettings->SlowWeaponIntervalSeconds && Event.bWeakPoint) ? 2 : 1; // 慢射速重武器弱点+2格。
    }
    else if (PrevContribution == 1 && bPelletBonusCandidate)
    {
        Add = 1; // 散弹单枪命中>=5丸补第2格充能。
    }
    if (Add <= 0) return;
    ShotCharges.FindOrAdd(Event.ShotId) = PrevContribution + Add;
    Charge = FMath::Min(ChargeLimit, Charge + Add); bEntered = true;
    if (ChargeEndTime <= 0.f) ChargeEndTime = Time + ChargeWindowSeconds;
    RefreshPresentation();
    UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_CHARGE owner=%d add=%d count=%d/%d"), bOwnerHit, Add, Charge, ChargeLimit);
}

void UDemoAnchorBuildRuntime::NotifyTeamHit(UDemoRunBuildComponent* ActorBuild, const UDemoRunBuildEventContext& Event)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__); // static无this，保留高频埋点但默认不输出。
    const bool bPelletCandidate = Event.Shot.PlannedProjectiles > 1 && Event.ShotPelletsHit >= 2;
    if (!ActorBuild || !ActorBuild->GetWorld() || !ActorBuild->GetOwner() || !ActorBuild->GetOwner()->HasAuthority()
        || Event.Action != EDemoRunBuildAction::AfterHit || Event.bSecondary || (!Event.bFirstEnemy && !bPelletCandidate) || Event.Damage <= 0.f)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_TEAM_HIT_REJECT owner/authority/direct receipt")); return; }
    TArray<TWeakObjectPtr<UDemoAnchorBuildRuntime>, TInlineAllocator<4>> Recipients; // 先借用四个策略弱引用，避免GE清场销毁Actor时迭代器失效。
    for (TActorIterator<ADemoTacticalAnchorActor> It(ActorBuild->GetWorld()); It && Recipients.Num() < 4; ++It)
        if (UDemoAnchorBuildRuntime* Runtime = It->GetRuntime()) Recipients.AddUnique(TWeakObjectPtr<UDemoAnchorBuildRuntime>(Runtime));
    for (const TWeakObjectPtr<UDemoAnchorBuildRuntime>& Recipient : Recipients)
        if (Recipient.IsValid()) Recipient->ReceiveHit(ActorBuild, Event); // 游戏线程顺序消费，同ShotId本人事件重复到达由账本拒绝。
}

void UDemoAnchorBuildRuntime::EnsureMaintenance()
{
    DEMO_LOG_CALL();
    if (GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(MaintenanceTimer))
        GetWorld()->GetTimerManager().SetTimer(MaintenanceTimer, this, &UDemoAnchorBuildRuntime::Maintain, .1f, true);
}

void UDemoAnchorBuildRuntime::Maintain()
{
    DEMO_LOG_TICK();
    if (!Active())
    {
        ClearAnchor(TEXT("Inactive")); RelayEndTime = 0.f;
        if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer);
        Publish(NAME_None, 0, 0); return; // 最终撤销由原GA调用Reset，保持所有权边界。
    }
    const float Time = Now(); // 单一World时刻，暂停不会后台耗尽窗口。
    if (RelayEndTime > 0.f && Time >= RelayEndTime)
    { RelayEndTime = 0.f; UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_RELAY_EXPIRED")); }
    if (Anchor.IsValid())
    {
        if (Time >= AnchorEndTime) ClearAnchor(TEXT("Lifetime"));
        else
        {
            if (ChargeEndTime > 0.f && Time >= ChargeEndTime)
            { Charge = 0; ChargeEndTime = 0.f; UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_CHARGE_EXPIRED")); }
            const bool bInside = IsInside(Pawn()); // 每0.1秒仅本人距离，不遍历敌人或队伍。
            if (bInside)
            {
                if (!bEntered) UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_ENTERED"));
                bEntered = true;
            }
            else if (!bInside && bEntered)
            {
                if (Charge >= ChargeLimit && Selection().Branch3 != TEXT("B06-B")) Release(FVector::ZeroVector);
                else ClearAnchor(TEXT("LeftUnfulfilled"));
            }
        }
    }
    if (!Active()) return; // 脉冲的最后击杀可能同步结束会话，不能重新发布旧场状态。
    RefreshPresentation();
    if (!Anchor.IsValid() && RelayEndTime <= 0.f && Time >= NextDeployTime) GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer);
}

void UDemoAnchorBuildRuntime::Release(const FVector& Direction)
{
    DEMO_LOG_CALL();
    if (!Active() || !Anchor.IsValid()) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("BUILD_ANCHOR_RELEASE_REJECT inactive/missing")); return; }
    if (Charge < ChargeLimit || Now() >= AnchorEndTime || ChargeEndTime <= Now())
    { ClearAnchor(TEXT("InvalidReleaseWindow")); RefreshPresentation(); return; }
    const FVector PulseCenter = Center + FVector(0.f, 0.f, 45.f); // 离地查询避免地板遮挡；不把目标/射手位置作为可移动爆心。
    const bool bCanRelay = Selection().Branch7 == TEXT("B06-C") && !bRelayPlacement; // 只对第一锚点保留候选，实际伤敌后才授令牌。
    ADemoTacticalAnchorActor* Visual = Anchor.Get(); // 消费前同步借用，下面即清活跃引用，GE重入不能二次释放。
    Anchor.Reset(); Charge = 0; ChargeEndTime = 0.f; AnchorEndTime = 0.f; bEntered = false; ShotCharges.Reset();
    Visual->PulseAndRetire();
    const bool bDirected = !Direction.IsNearlyZero();
    const float EffectiveRange = bDirected ? DirectedRange : ActiveRadius; // B06-B固守信标释放中远程定向冲击波，A/基础保留圆盘范围。
    const float EffectiveCone = bDirected ? DirectedConeCos : PulseConeCos;
    const int32 Damaged = Pulse(PulseCenter, EffectiveRange, PulseDamage, PulseTargetLimit, nullptr, Direction, EffectiveCone); // 部署时冻结参数；基类仅合法敌人、LOS、有界目标，不附元素/击退。
    if (!Active()) return; // 最后击杀清场不允许留下迁移凭据或覆盖新阶段HUD。
    if (bCanRelay && Damaged > 0) RelayEndTime = Now() + RelayWindowSeconds;
    RefreshPresentation();
    UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_RELEASE directed=%d targets=%d relay=%d"), !Direction.IsNearlyZero(), Damaged, RelayEndTime > Now());
}

void UDemoAnchorBuildRuntime::RefreshPresentation()
{
    DEMO_LOG_TICK();
    const UDemoExpandedBuildSettings* Config = GetDefault<UDemoExpandedBuildSettings>(); // HUD只显示权威确认的计数/时间，不做客户端推测。
    if (Anchor.IsValid())
    {
        const bool bReady = Charge >= ChargeLimit; // 只有一档，不因队友数量扩容。
        Anchor->SetCharge(Charge, ChargeLimit, bReady);
        Publish(bReady ? (Selection().Branch3 == TEXT("B06-B") ? TEXT("AnchorFire") : TEXT("AnchorLeave")) : TEXT("AnchorCharge"), Charge, ChargeLimit,
            ChargeEndTime > 0.f ? FMath::Min(ChargeEndTime, AnchorEndTime) : AnchorEndTime, NextDeployTime);
    }
    else Publish(RelayEndTime > Now() ? TEXT("AnchorRelay") : Now() < NextDeployTime ? TEXT("AnchorCooldown") : TEXT("AnchorReady"), 0, Config->AnchorChargeHits, RelayEndTime, NextDeployTime);
}

void UDemoAnchorBuildRuntime::ClearAnchor(FName Reason)
{
    DEMO_LOG_CALL();
    if (Anchor.IsValid())
    {
        ADemoTacticalAnchorActor* Previous = Anchor.Get(); // 清路由目标前先清本策略引用，Destroy回调不可能再次消费。
        Anchor.Reset(); Previous->Destroy();
        UE_LOG(LogFPSDemo, Log, TEXT("BUILD_ANCHOR_CLEAR reason=%s"), *Reason.ToString());
    }
    Charge = 0; ChargeEndTime = 0.f; AnchorEndTime = 0.f; ShotCharges.Reset(); bEntered = false;
}

void UDemoAnchorBuildRuntime::ResetRuntime()
{
    DEMO_LOG_CALL();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer);
    ClearAnchor(TEXT("Reset")); RelayEndTime = 0.f; NextDeployTime = 0.f; bRelayPlacement = false; Center = FVector::ZeroVector;
    Publish(NAME_None, 0, 0);
    Super::ResetRuntime();
}

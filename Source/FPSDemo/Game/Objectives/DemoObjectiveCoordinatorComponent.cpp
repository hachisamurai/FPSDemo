// 职责：歼灭/破坏/转移控制的服务器目标运行。依赖CombatSession与外部刷怪事实，完成仅候选，不越权处理奖励/存档/总阶段。
#include "Game/Objectives/DemoObjectiveCoordinatorComponent.h"
#include "Game/Objectives/DemoObjectiveDevice.h"
#include "Game/Objectives/DemoControlPoint.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "GAS/DemoAttributeSet.h"
#include "Engine/World.h"
#include "Engine/Level.h" // 目标地板必须仍来自本次准备冻结的流送Level，不能借上层/旧房间碰撞通过。
#include "UObject/Package.h" // 来源比较使用实际流送Level包名，不依赖间接包含UPackage完整定义。
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"

UDemoObjectiveCoordinatorComponent::UDemoObjectiveCoordinatorComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick = false;
}

bool UDemoObjectiveCoordinatorComponent::ValidateConfig(const FDemoObjectiveConfig& Config, FString& Error) const
{
    DEMO_LOG_CALL();
    Error.Reset();
    if (Config.Kind != EDemoObjectiveKind::KillAll && Config.Kind != EDemoObjectiveKind::Sabotage && Config.Kind != EDemoObjectiveKind::RotatingControl)
        Error = TEXT("未知任务目标类型");
    else if (Config.Kind == EDemoObjectiveKind::Sabotage && (Config.DeviceAnchors.Num() != 2 || !FMath::IsFinite(Config.DeviceHealth) || Config.DeviceHealth <= 0.f || Config.DeviceHealth > 1.e9f))
        Error = TEXT("设施破坏需要两个有效锚点及合法生命值");
    else if (Config.Kind == EDemoObjectiveKind::RotatingControl && ((Config.ControlAnchors.Num() != 1 && Config.ControlAnchors.Num() != 3) || !FMath::IsFinite(Config.ControlSeconds) || Config.ControlSeconds < 1.f || Config.ControlSeconds > 600.f
        || !FMath::IsFinite(Config.TransitionSeconds) || Config.TransitionSeconds < 1.f || Config.TransitionSeconds > 30.f
        || !FMath::IsFinite(Config.ControlRadius) || Config.ControlRadius < 100.f || Config.ControlRadius > 2000.f
        || !FMath::IsFinite(Config.MaxSampleDelta) || Config.MaxSampleDelta < .1f || Config.MaxSampleDelta > 1.f))
        Error = TEXT("控制目标需要一个或三个锚点及合法计时/半径参数");
    const TArray<FVector>& Anchors = Config.Kind == EDemoObjectiveKind::Sabotage ? Config.DeviceAnchors : Config.ControlAnchors; // 同步借用待冻结配置，不保存外部引用。
    if (Config.Kind != EDemoObjectiveKind::KillAll)
        for (int32 Index = 0; Index < Anchors.Num() && Error.IsEmpty(); ++Index) // 每个必要坐标必须有限且与其它目标分开。
        {
            if (Anchors[Index].ContainsNaN()) { Error = TEXT("目标锚点包含非法坐标"); break; }
            for (int32 Other = 0; Other < Index; ++Other) // 当前设施2或控制1/3个目标，比较规模有界。
                if (FVector::DistSquared2D(Anchors[Index], Anchors[Other]) < FMath::Square(Config.Kind == EDemoObjectiveKind::Sabotage ? 200.f : Config.ControlRadius))
                { Error = TEXT("目标锚点重叠或距离过近"); break; }
        }
    if (!Error.IsEmpty()) UE_LOG(LogFPSDemo, Warning, TEXT("OBJECTIVE_CONFIG_REJECT %s"), *Error);
    return Error.IsEmpty();
}

bool UDemoObjectiveCoordinatorComponent::ResolveGround(const FVector& Anchor, FVector& Ground, FString& Error) const
{
    DEMO_LOG_CALL();
    FHitResult Hit; // 只投影静态地形，不把玩家/目标顶面当地图锚点。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoObjectiveGround), false, GetOwner()); // 同步物理查询，不产生额外Actor。
    const bool bHit = GetWorld()->LineTraceSingleByObjectType(Hit, Anchor + FVector(0, 0, 40), Anchor - FVector(0, 0, 40), FCollisionObjectQueryParams(ECC_WorldStatic), Query); // 作者锚点已经是地板；窄测防从低层上探进入顶板。
    const AActor* FloorActor = Hit.GetActor(); // 仅当前同步物理结果借用，不延长关卡Actor生命周期。
    const FName FloorLevel = FloorActor && FloorActor->GetLevel() ? FloorActor->GetLevel()->GetOutermost()->GetFName() : NAME_None; // PIE/动态实例使用实际包身份，不猜源地图短名。
    if (!bHit || Hit.bStartPenetrating || Hit.ImpactNormal.Z < .7f || FMath::Abs(Hit.ImpactPoint.Z - Anchor.Z) > 32.f
        || (!FrozenConfig.SourceLevelPackage.IsNone() && FloorLevel != FrozenConfig.SourceLevelPackage))
    { Error = TEXT("目标锚点附近缺少同层且归属正确的可用地面"); UE_LOG(LogFPSDemo, Warning, TEXT("OBJECTIVE_GROUND_REJECT anchor=%s hit=%d penetrating=%d floor=%s normalZ=%.2f expectedLevel=%s actualLevel=%s"), *Anchor.ToString(), bHit, Hit.bStartPenetrating, *Hit.ImpactPoint.ToString(), Hit.ImpactNormal.Z, *FrozenConfig.SourceLevelPackage.ToString(), *FloorLevel.ToString()); return false; }
    Ground = Hit.ImpactPoint;
    return true;
}

bool UDemoObjectiveCoordinatorComponent::PrepareObjective(const FDemoObjectiveConfig& Config, UDemoCombatSession* Session, FString& Error)
{
    DEMO_LOG_CALL();
    Error.Reset();
    if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld() || !IsValid(Session) || !Session->IsActive())
    { Error = TEXT("任务准备缺少权威World或有效战斗会话"); UE_LOG(LogFPSDemo, Warning, TEXT("OBJECTIVE_PREPARE_REJECT authority/session")); return false; }
    if (!ValidateConfig(Config, Error)) return false;
    CancelObjective(); // 只取消本组件旧目标，不撤销Flow当前会话。
    FrozenConfig = Config;
    ActiveSession = Session;
    Snapshot = FDemoObjectiveSnapshot();
    Snapshot.Kind = Config.Kind;
    Snapshot.State = EDemoObjectiveState::Preparing;
    Snapshot.Total = Config.Kind == EDemoObjectiveKind::Sabotage ? 2 : Config.Kind == EDemoObjectiveKind::RotatingControl ? Config.ControlAnchors.Num() : 0;
    Snapshot.Radius = Config.Kind == EDemoObjectiveKind::RotatingControl ? Config.ControlRadius : 0.f;
    Snapshot.RequiredSeconds = Config.Kind == EDemoObjectiveKind::RotatingControl ? Config.ControlSeconds : 0.f;
    FActorSpawnParameters Spawn; // 仅生成本目标拥有的Actor，不把它们加入Enemy人口或奖励订阅。
    Spawn.Owner = GetOwner();
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
    const TArray<FVector>& Anchors = Config.Kind == EDemoObjectiveKind::Sabotage ? Config.DeviceAnchors : Config.ControlAnchors; // 冻结之前仅本同步调用读取。
    if (Config.Kind != EDemoObjectiveKind::KillAll)
        for (const FVector& Anchor : Anchors) // 配置严格限制为设施2或控制1/3，失败回收前面已经创建的对象。
        {
            FVector Ground = FVector::ZeroVector; // 当前目标地面世界cm。
            if (!ResolveGround(Anchor, Ground, Error)) break;
            if (Config.Kind == EDemoObjectiveKind::Sabotage)
            {
                ADemoObjectiveDevice* Device = GetWorld()->SpawnActor<ADemoObjectiveDevice>(ADemoObjectiveDevice::StaticClass(), Ground + FVector(0, 0, 91.f), FRotator::ZeroRotator, Spawn); // 方体中心抬高91cm，底部离地1cm避免初始穿透。
                if (!Device) { Error = TEXT("设施目标生成失败或锚点被阻挡"); break; }
                Devices.Add(Device);
                if (!Device->PrepareDevice(Session, Config.DeviceHealth)) { Error = TEXT("设施GAS初始化失败"); break; }
                Device->OnBroken.AddUObject(this, &UDemoObjectiveCoordinatorComponent::HandleDeviceBroken);
                Device->OnEndPlay.AddDynamic(this, &UDemoObjectiveCoordinatorComponent::HandleRequiredEndPlay);
                if (Devices.Num() == 1) Snapshot.ActiveLocation = Ground;
            }
            else
            {
                ADemoControlPoint* Point = GetWorld()->SpawnActor<ADemoControlPoint>(ADemoControlPoint::StaticClass(), Ground + FVector(0, 0, 3.f), FRotator::ZeroRotator, Spawn); // 圆盘高于地面3cm避免z-fighting，无碰撞不阻挡路径。
                if (!Point) { Error = TEXT("控制点标记生成失败"); break; }
                ControlPoints.Add(Point);
                Point->Configure(ControlPoints.Num(), Config.ControlRadius);
                Point->OnEndPlay.AddDynamic(this, &UDemoObjectiveCoordinatorComponent::HandleRequiredEndPlay);
                if (ControlPoints.Num() == 1) Snapshot.ActiveLocation = Ground;
            }
        }
    if (!Error.IsEmpty())
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("OBJECTIVE_PREPARE_REJECT %s"), *Error);
        Snapshot.State = EDemoObjectiveState::SystemError;
        ReleaseActors(); ActiveSession.Reset(); bSnapshotDirty = true; PublishSnapshot();
        return false;
    }
    if (!Session->RegisterCleanup(this, FSimpleDelegate::CreateUObject(this, &UDemoObjectiveCoordinatorComponent::HandleSessionEnded, TWeakObjectPtr<UDemoCombatSession>(Session))))
    { Error = TEXT("目标会话在准备期间已撤销"); CancelObjective(); return false; }
    bSnapshotDirty = true; PublishSnapshot();
    UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_PREPARED kind=%d devices=%d controls=%d"), static_cast<int32>(Config.Kind), Devices.Num(), ControlPoints.Num());
    return true;
}

bool UDemoObjectiveCoordinatorComponent::ActivateObjective()
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || Snapshot.State != EDemoObjectiveState::Preparing || !ActiveSession.IsValid() || !ActiveSession->IsActive())
    { UE_LOG(LogFPSDemo, Warning, TEXT("OBJECTIVE_ACTIVATE_REJECT phase/session/authority")); return false; }
    Snapshot.State = EDemoObjectiveState::Active;
    for (ADemoObjectiveDevice* Device : Devices) if (IsValid(Device)) Device->SetObjectiveActive(true); // 准备之前的攻击不能在激活时自动补算。
    LastSampleTime = GetWorld()->GetTimeSeconds();
    TransitionUntil = 0.;
    if (Snapshot.Kind == EDemoObjectiveKind::RotatingControl)
    {
        Snapshot.ControlStatus = EDemoControlStatus::Empty;
        SpawnProtectionUntil = LastSampleTime + 1.; // 首点同样提供1秒出生保护，期间其它合法区域照常刷怪。
        GetWorld()->GetTimerManager().SetTimer(SampleTimer, this, &UDemoObjectiveCoordinatorComponent::SampleControl, .1f, true);
    }
    GetWorld()->GetTimerManager().SetTimer(PublishTimer, this, &UDemoObjectiveCoordinatorComponent::PublishSnapshot, .25f, true);
    bSnapshotDirty = true; PublishSnapshot();
    if (Snapshot.Kind == EDemoObjectiveKind::KillAll && Snapshot.Remaining == 0) SubmitCompletion(); // -1未知永不视为清场。
    return true;
}

void UDemoObjectiveCoordinatorComponent::UpdatePopulation(int32 Pending, int32 Alive)
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || !ActiveSession.IsValid() || !ActiveSession->IsActive()
        || (Snapshot.State != EDemoObjectiveState::Preparing && Snapshot.State != EDemoObjectiveState::Active) || Pending < 0 || Alive < 0 || Pending > MAX_int32 - Alive)
    { UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_POPULATION_REJECT authority/session/state/value")); return; }
    const int32 Remaining = Pending + Alive; // 仅服务器刷怪事实，不按场内Actor迭代数猜测未生成队列。
    if (Snapshot.Remaining != Remaining) { Snapshot.Remaining = Remaining; bSnapshotDirty = true; }
    if (Snapshot.State == EDemoObjectiveState::Active && Snapshot.Kind == EDemoObjectiveKind::KillAll && Pending == 0 && Alive == 0) SubmitCompletion();
}

void UDemoObjectiveCoordinatorComponent::SampleControl()
{
    DEMO_LOG_TICK();
    if (Snapshot.State != EDemoObjectiveState::Active || !ActiveSession.IsValid() || !ActiveSession->IsActive()) { CancelObjective(); return; }
    if (!ControlPoints.IsValidIndex(Snapshot.Completed) || !IsValid(ControlPoints[Snapshot.Completed])) { ReportSystemError(TEXT("当前控制点意外丢失")); return; }
    const double Now = GetWorld()->GetTimeSeconds(); // World游戏秒，暂停不推进，禁止使用墙钟补进度。
    const float Delta = FMath::Clamp(static_cast<float>(Now - LastSampleTime), 0.f, FrozenConfig.MaxSampleDelta); // 大帧只计有限实际参与时间。
    LastSampleTime = Now;
    if (TransitionUntil > Now)
    {
        Snapshot.TransitionRemaining = static_cast<float>(TransitionUntil - Now);
        Snapshot.ControlStatus = EDemoControlStatus::Relocating;
        bSnapshotDirty = true;
        return;
    }
    if (TransitionUntil > 0.)
    {
        TransitionUntil = 0.; Snapshot.TransitionRemaining = 0.f; Snapshot.ControlStatus = EDemoControlStatus::Empty;
        SpawnProtectionUntil = Now + 1.; // 迁移结束才开启保护，不能让5秒预告提前消耗保护窗口。
        bSnapshotDirty = true;
        return; // 激活边沿不补计迁移期间已流逝时间。
    }
    const FVector Center = ControlPoints[Snapshot.Completed]->GetActorLocation(); // 地面中心，不跟随客户端标记位置。
    bool bPlayerInside = false; // 至少一个存活参战者，不因人数加速。
    bool bEnemyInside = false; // 同会话存活敌人在圈内暂停，已死尸体不阻塞。
    for (TActorIterator<ADemoCharacter> It(GetWorld()); It; ++It) // 最多4人，读取当前Avatar生命与淘汰事实。
    {
        const UDemoAttributeSet* Health = It->GetDemoAttributes(); // PS拥有的当前属性借用，不缓存到下次采样。
        const FVector Offset = It->GetActorLocation() - Center; // 世界cm；同一平面上的玩家才参与。
        if (It->GetController() && !It->IsCoopEliminated() && Health && Health->GetHealth() > 0.f
            && FMath::Abs(Offset.Z) <= 250.f && Offset.SizeSquared2D() <= FMath::Square(FrozenConfig.ControlRadius)) { bPlayerInside = true; break; }
    }
    for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 已有敌人上限限制规模，无物理Overlap重复多骨体。
    {
        const FVector Offset = It->GetActorLocation() - Center; // 胶囊/根中心按统一半径判区，不按每骨刚体重复。
        if (It->IsAlive() && It->IsCombatSessionValid() && FMath::Abs(Offset.Z) <= 250.f && Offset.SizeSquared2D() <= FMath::Square(FrozenConfig.ControlRadius)) { bEnemyInside = true; break; }
    }
    const EDemoControlStatus NewStatus = bEnemyInside ? EDemoControlStatus::Contested : bPlayerInside ? EDemoControlStatus::Capturing : EDemoControlStatus::Empty; // 目标状态由权威人口与几何决定。
    if (Snapshot.ControlStatus != NewStatus) { Snapshot.ControlStatus = NewStatus; bSnapshotDirty = true; }
    if (!bPlayerInside || bEnemyInside) return; // 离开/被占只暂停，不倒退，最后一段也必须有人存活。
    Snapshot.ProgressSeconds = FMath::Min(FrozenConfig.ControlSeconds, Snapshot.ProgressSeconds + Delta);
    bSnapshotDirty = true;
    if (Snapshot.ProgressSeconds + KINDA_SMALL_NUMBER < FrozenConfig.ControlSeconds) return;
    ++Snapshot.Completed;
    if (Snapshot.Completed >= Snapshot.Total) { SubmitCompletion(); return; }
    Snapshot.ProgressSeconds = 0.f;
    Snapshot.ControlStatus = EDemoControlStatus::Relocating;
    Snapshot.ActiveLocation = ControlPoints[Snapshot.Completed]->GetActorLocation();
    TransitionUntil = Now + FrozenConfig.TransitionSeconds;
    Snapshot.TransitionRemaining = FrozenConfig.TransitionSeconds;
    UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_CONTROL_RELOCATE completed=%d next=%d seconds=%.1f"), Snapshot.Completed, Snapshot.Completed + 1, FrozenConfig.TransitionSeconds);
}

void UDemoObjectiveCoordinatorComponent::PublishSnapshot()
{
    DEMO_LOG_TICK();
    if (!bSnapshotDirty || !GetWorld()) return;
    bSnapshotDirty = false;
    Snapshot.ServerTime = GetWorld()->GetTimeSeconds();
    for (int32 Index = 0; Index < ControlPoints.Num(); ++Index) // 与公开快照同批刷新本场1/3个标记，避免标记/HUD显示不同阶段。
        if (IsValid(ControlPoints[Index]))
            ControlPoints[Index]->UpdatePresentation(Index == Snapshot.Completed ? Snapshot.ControlStatus : EDemoControlStatus::Inactive,
                Index == Snapshot.Completed ? (Snapshot.ControlStatus == EDemoControlStatus::Relocating ? Snapshot.TransitionRemaining : FMath::Max(0.f, Snapshot.RequiredSeconds - Snapshot.ProgressSeconds)) : 0.f,
                Index < Snapshot.Completed);
    OnSnapshotChanged.Broadcast(Snapshot);
}

void UDemoObjectiveCoordinatorComponent::HandleDeviceBroken(ADemoObjectiveDevice* Device)
{
    DEMO_LOG_CALL();
    if (Snapshot.State != EDemoObjectiveState::Active || !ActiveSession.IsValid() || !ActiveSession->IsActive() || !IsValid(Device)
        || !Devices.Contains(Device) || !Device->IsBroken() || BrokenDevices.Contains(Device))
    { UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_BROKEN_REJECT old/duplicate/foreign")); return; }
    BrokenDevices.Add(Device);
    Snapshot.Completed = BrokenDevices.Num(); bSnapshotDirty = true;
    for (ADemoObjectiveDevice* Remaining : Devices) if (IsValid(Remaining) && !Remaining->IsBroken()) { Snapshot.ActiveLocation = Remaining->GetActorLocation(); break; }
    if (Snapshot.Completed == Snapshot.Total) SubmitCompletion();
}

void UDemoObjectiveCoordinatorComponent::SubmitCompletion()
{
    DEMO_LOG_CALL();
    if (Snapshot.State != EDemoObjectiveState::Active || !ActiveSession.IsValid() || !ActiveSession->IsActive())
    { UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_COMPLETION_REJECT stale/duplicate")); return; }
    Snapshot.State = EDemoObjectiveState::Resolving; // 先锁定结果候选，后续击碎/人口/计时事件无法重复提交。
    StopActivity();
    bSnapshotDirty = true; PublishSnapshot();
    UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_COMPLETION_CANDIDATE kind=%d"), static_cast<int32>(Snapshot.Kind));
    OnCompletionCandidate.Broadcast(ActiveSession.Get()); // Flow帧末再查全员生命；此处不提前撤销整个战斗凭据。
}

void UDemoObjectiveCoordinatorComponent::HandleRequiredEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    if (bReleasing || (Snapshot.State != EDemoObjectiveState::Preparing && Snapshot.State != EDemoObjectiveState::Active && Snapshot.State != EDemoObjectiveState::Resolving)) return;
    if (Reason != EEndPlayReason::Destroyed && Reason != EEndPlayReason::RemovedFromWorld)
    {
        Snapshot.State = EDemoObjectiveState::Cancelled;
        StopActivity(); ReleaseActors(false); ActiveSession.Reset(); // 引擎正在退出/旅行，解绑即可，不在World卸载栈内递归销毁或发布新UI。
        return;
    }
    if (const ADemoObjectiveDevice* Device = Cast<ADemoObjectiveDevice>(Actor)) if (Device->IsBroken()) return; // 已确认击碎的残骸外部清除不撤销合法事实。
    if (ADemoObjectiveDevice* Device = Cast<ADemoObjectiveDevice>(Actor)) { Device->OnBroken.RemoveAll(this); Devices.Remove(Device); } // 当前Actor已在EndPlay栈内，先移出回收表，尤其避免RemovedFromWorld时递归Destroy本人。
    if (ADemoControlPoint* Point = Cast<ADemoControlPoint>(Actor)) ControlPoints.Remove(Point);
    Actor->OnEndPlay.RemoveDynamic(this, &UDemoObjectiveCoordinatorComponent::HandleRequiredEndPlay);
    ReportSystemError(FString::Printf(TEXT("必需目标意外移除：%s"), *GetNameSafe(Actor)));
}

void UDemoObjectiveCoordinatorComponent::ReportSystemError(const FString& Reason)
{
    DEMO_LOG_CALL();
    if (Snapshot.State == EDemoObjectiveState::SystemError || Snapshot.State == EDemoObjectiveState::Cancelled || Snapshot.State == EDemoObjectiveState::Completed) return;
    Snapshot.State = EDemoObjectiveState::SystemError;
    StopActivity(); ReleaseActors();
    bSnapshotDirty = true; PublishSnapshot();
    UE_LOG(LogFPSDemo, Error, TEXT("OBJECTIVE_SYSTEM_ERROR %s"), *Reason);
    OnSystemError.Broadcast(ActiveSession.Get(), Reason);
}

void UDemoObjectiveCoordinatorComponent::HandleSessionEnded(TWeakObjectPtr<UDemoCombatSession> Expected)
{
    DEMO_LOG_CALL();
    if (ActiveSession != Expected) { UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_SESSION_CANCEL_IGNORED obsolete registration")); return; }
    CancelObjective();
}

void UDemoObjectiveCoordinatorComponent::StopActivity()
{
    DEMO_LOG_CALL();
    if (GetWorld()) { GetWorld()->GetTimerManager().ClearTimer(SampleTimer); GetWorld()->GetTimerManager().ClearTimer(PublishTimer); }
    for (ADemoObjectiveDevice* Device : Devices) if (IsValid(Device)) Device->SetObjectiveActive(false);
}

void UDemoObjectiveCoordinatorComponent::ReleaseActors(bool bDestroyActors)
{
    DEMO_LOG_CALL();
    if (bReleasing) return;
    bReleasing = true;
    for (ADemoObjectiveDevice* Device : Devices)
        if (IsValid(Device)) { Device->OnBroken.RemoveAll(this); Device->OnEndPlay.RemoveDynamic(this, &UDemoObjectiveCoordinatorComponent::HandleRequiredEndPlay); if (bDestroyActors && !Device->IsActorBeingDestroyed()) Device->Destroy(); }
    for (ADemoControlPoint* Point : ControlPoints)
        if (IsValid(Point)) { Point->OnEndPlay.RemoveDynamic(this, &UDemoObjectiveCoordinatorComponent::HandleRequiredEndPlay); if (bDestroyActors && !Point->IsActorBeingDestroyed()) Point->Destroy(); }
    Devices.Reset(); ControlPoints.Reset(); BrokenDevices.Reset();
    bReleasing = false;
}

void UDemoObjectiveCoordinatorComponent::CancelObjective()
{
    DEMO_LOG_CALL();
    if (Snapshot.State != EDemoObjectiveState::Unprepared && Snapshot.State != EDemoObjectiveState::Completed && Snapshot.State != EDemoObjectiveState::SystemError) Snapshot.State = EDemoObjectiveState::Cancelled;
    StopActivity(); ReleaseActors(); ActiveSession.Reset();
    bSnapshotDirty = true; PublishSnapshot();
}

void UDemoObjectiveCoordinatorComponent::CompleteObjective()
{
    DEMO_LOG_CALL();
    if (Snapshot.State != EDemoObjectiveState::Resolving) { UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_COMMIT_REJECT missing candidate")); return; }
    Snapshot.State = EDemoObjectiveState::Completed;
    StopActivity(); ReleaseActors(); ActiveSession.Reset();
    bSnapshotDirty = true; PublishSnapshot();
}

const FDemoObjectiveSnapshot& UDemoObjectiveCoordinatorComponent::GetSnapshot() const { DEMO_LOG_TICK(); return Snapshot; }

bool UDemoObjectiveCoordinatorComponent::IsControlReinforcementActive(const FString& RunId, int32 Level) const
{
    DEMO_LOG_TICK();
    return GetOwner() && GetOwner()->HasAuthority() && Snapshot.Kind == EDemoObjectiveKind::RotatingControl
        && Snapshot.State == EDemoObjectiveState::Active && ActiveSession.IsValid() && ActiveSession->Matches(RunId, Level);
}

bool UDemoObjectiveCoordinatorComponent::IsSpawnLocationAllowed(const FVector& WorldLocation) const
{
    DEMO_LOG_TICK();
    if (Snapshot.State != EDemoObjectiveState::Active || Snapshot.Kind != EDemoObjectiveKind::RotatingControl || !GetWorld()
        || GetWorld()->GetTimeSeconds() >= SpawnProtectionUntil || !ControlPoints.IsValidIndex(Snapshot.Completed) || !IsValid(ControlPoints[Snapshot.Completed])) return true;
    const bool bAllowed = FVector::DistSquared2D(WorldLocation, ControlPoints[Snapshot.Completed]->GetActorLocation()) > FMath::Square(FrozenConfig.ControlRadius); // 与服务器控制区使用同一水平半径。
    if (!bAllowed) UE_LOG(LogFPSDemo, VeryVerbose, TEXT("OBJECTIVE_SPAWN_DEFER point=%d protection=%.2f"), Snapshot.Completed + 1, SpawnProtectionUntil - GetWorld()->GetTimeSeconds());
    return bAllowed;
}

void UDemoObjectiveCoordinatorComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    CancelObjective();
    OnSnapshotChanged.Clear(); OnCompletionCandidate.Clear(); OnSystemError.Clear();
    Super::EndPlay(Reason);
}

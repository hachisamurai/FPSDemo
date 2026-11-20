// 职责：G标记的权威射线、限频、生命周期及公开复制。使用：PC只能传本人意图；既有CombatSession负责跨关撤销。
#include "Player/Coop/Ping/DemoTeamPingComponent.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Debug/DemoLog.h"

UDemoTeamPingComponent::UDemoTeamPingComponent()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true);
}

bool UDemoTeamPingComponent::RequestPing(ADemoPlayerController* Sender)
{
    DEMO_LOG_TICK(); // 不可信RPC可高频到达，保留埋点而不让拒绝洪泛默认日志。
    ADemoGameState* State = Cast<ADemoGameState>(GetOwner()); // GS是唯一公开状态源，不从客户端菜单推断阶段。
    ADemoCharacter* Pawn = IsValid(Sender) ? Cast<ADemoCharacter>(Sender->GetPawn()) : nullptr; // 只解析此RPC拥有者当前Avatar。
    ADemoPlayerState* PS = IsValid(Sender) ? Sender->GetPlayerState<ADemoPlayerState>() : nullptr; // 一人一个公开标记键，换Pawn不产生第二项。
    if (bStopping || !State || !State->HasAuthority() || State->Phase != EDemoPhase::Combat || !IsValid(PS) || !IsValid(Pawn)
        || Sender->GetWorld() != GetWorld() || !Sender->IsCoopRegistered() || Pawn->IsCoopEliminated()
        || !Pawn->GetDemoAttributes() || Pawn->GetDemoAttributes()->GetHealth() <= 0.f || !Pawn->CanUseCombatAbilities())
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("TEAM_PING rejected authority/phase/owner/alive/menu")); return false; }
    const float Now = GetWorld()->GetTimeSeconds(); // 服务器游戏时钟，单人暂停时标记和限频一起暂停。
    bDirty |= Prune(Now);
    if (const float* Next = NextAllowedAt.Find(PS); Next && Now < *Next)
    { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("TEAM_PING rejected rate limit")); return false; }
    NextAllowedAt.Add(PS, Now + 1.f); // 在射线/配置校验前消费本次预算，坏请求也不能无限发射查询。
    EnsureMaintenance();
    UDemoCombatSession* CurrentSession = UDemoCombatSession::FindActive(GetWorld()); // 仅每次合法低频请求解析，不在定时维护查询GameMode。
    if (!CurrentSession || !FMath::IsFinite(TraceDistance) || TraceDistance < 100.f || TraceDistance > 100000.f)
    { UE_LOG(LogFPSDemo, Warning, TEXT("TEAM_PING rejected missing session/invalid trace distance")); return false; }
    if (Session.Get() != CurrentSession)
    {
        if (!CurrentSession->RegisterCleanup(this, FSimpleDelegate::CreateUObject(this, &UDemoTeamPingComponent::OnCombatEnded))) return false;
        Session = CurrentSession; // 新会话先注册再使用，旧会话回调已在进入新关前完成。
    }
    const FVector Start = Pawn->GetSharedViewLocation(); // 与枪械一致的真实稳定眼位，不读客户端相机坐标。
    const FVector Direction = Pawn->GetBaseAimRotation().Vector(); // 服务器既有控制朝向，RPC不能指定任意方向或目标。
    if (Start.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero())
    { UE_LOG(LogFPSDemo, Warning, TEXT("TEAM_PING rejected invalid authoritative view")); return false; }
    const FVector End = Start + Direction * TraceDistance; // 有界世界射线，未命中时标记此最大距离地点。
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DemoTeamPing), true, Pawn); // 武器瞄准通道遇墙即止，忽略本人但不穿透遮挡。
    FHitResult Hit; // 本次同步查询结果，不接受RPC传入的HitResult或Actor引用。
    const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_GameTraceChannel2, Params);
    ADemoEnemy* Enemy = bHit ? Cast<ADemoEnemy>(Hit.GetActor()) : nullptr; // 只有实际最先命中的活敌人可创建敌人标记。
    if (Enemy && (!Enemy->IsAlive() || !Enemy->IsCombatSessionValid())) Enemy = nullptr;
    int32 Index = INDEX_NONE; // 同一PS替换旧标记，不能通过连续按G积累多个地点。
    for (int32 I = 0; I < ActivePings.Num(); ++I) if (ActivePings[I].Sender == PS) { Index = I; break; }
    if (Index == INDEX_NONE && ActivePings.Num() >= 4)
    { UE_LOG(LogFPSDemo, Log, TEXT("TEAM_PING rejected four-player capacity")); return false; }
    FDemoTeamPing Ping; // 本次完整值，提交后最多保留5秒。
    Ping.Sender = PS; Ping.Target = Enemy; Ping.bEnemy = Enemy != nullptr;
    Ping.Location = bHit ? Hit.ImpactPoint : End; Ping.ExpiresAtServerTime = Now + 5.f; Ping.LevelNumber = State->LevelNumber;
    if (Enemy) Ping.TargetOffset = FVector(0, 0, Enemy->GetSimpleCollisionHalfHeight() + 35.f); // 敌人为AActor，使用通用简单碰撞高度；显示在目标上方，不改变命中点/伤害。
    LastSerial = LastSerial == MAX_int32 ? 1 : LastSerial + 1; Ping.Serial = LastSerial;
    if (Index == INDEX_NONE) ActivePings.Add(Ping); else ActivePings[Index] = Ping;
    bDirty = true; PublishIfDue(Now);
    UE_LOG(LogFPSDemo, Log, TEXT("TEAM_PING accepted serial=%d level=%d enemy=%d count=%d"), Ping.Serial, Ping.LevelNumber, Ping.bEnemy, ActivePings.Num());
    // 正常沟通标记已经提交后才发送附加玩法事实；锚点冷却/地面校验失败不撤销标记，也不改变本函数返回结果。
    if (UDemoRunBuildComponent* Build = PS->GetRunBuildComponent()) Build->NotifyPingAccepted(FVector(Ping.Location));
    return true;
}

bool UDemoTeamPingComponent::Prune(float Now)
{
    DEMO_LOG_TICK();
    bool bChanged = false; // 只在实际增删标记时触发公共快照变化。
    const ADemoGameState* State = Cast<ADemoGameState>(GetOwner()); // 同World阶段只读借用。
    for (int32 I = ActivePings.Num() - 1; I >= 0; --I)
    {
        const FDemoTeamPing& Ping = ActivePings[I]; // 删除前只在此迭代栈借用。
        const ADemoPlayerController* PC = IsValid(Ping.Sender) ? Cast<ADemoPlayerController>(Ping.Sender->GetOwner()) : nullptr; // Logout后控制器销毁即失效。
        const ADemoCharacter* Pawn = IsValid(PC) ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr; // 不把观战ViewTarget当原玩家。
        const ADemoEnemy* Enemy = Ping.bEnemy ? Cast<ADemoEnemy>(Ping.Target) : nullptr; // 原目标死亡/销毁不自动转成另一敌人。
        if (!State || State->Phase != EDemoPhase::Combat || Ping.LevelNumber != State->LevelNumber || Now >= Ping.ExpiresAtServerTime
            || !IsValid(PC) || !PC->IsCoopRegistered() || !IsValid(Pawn) || Pawn->IsCoopEliminated()
            || !Pawn->GetDemoAttributes() || Pawn->GetDemoAttributes()->GetHealth() <= 0.f
            || (Ping.bEnemy && (!IsValid(Enemy) || !Enemy->IsAlive() || !Enemy->IsCombatSessionValid())))
        { ActivePings.RemoveAt(I); bChanged = true; }
    }
    for (auto It = NextAllowedAt.CreateIterator(); It; ++It) // 限频键不延长PS寿命，过期/离线后回收，最多当前小队附近少量项。
        if (!It.Key().IsValid() || Now >= It.Value()) It.RemoveCurrent();
    return bChanged;
}

void UDemoTeamPingComponent::EnsureMaintenance()
{
    DEMO_LOG_CALL();
    if (!bStopping && GetWorld() && !GetWorld()->GetTimerManager().IsTimerActive(MaintenanceTimer))
        GetWorld()->GetTimerManager().SetTimer(MaintenanceTimer, this, &UDemoTeamPingComponent::Maintain, .25f, true);
}

void UDemoTeamPingComponent::Maintain()
{
    DEMO_LOG_TICK();
    if (bStopping || !GetOwner()->HasAuthority()) return;
    const float Now = GetWorld()->GetTimeSeconds(); // 与Expiry及GS客户端同步时钟同源。
    bDirty |= Prune(Now); PublishIfDue(Now);
    if (ActivePings.IsEmpty() && NextAllowedAt.IsEmpty() && !bDirty) GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer);
}

void UDemoTeamPingComponent::PublishIfDue(float Now)
{
    DEMO_LOG_TICK();
    if (!bDirty || (LastPublishedAt >= 0.f && Now - LastPublishedAt < .25f)) return;
    Snapshot = ActivePings; bDirty = false; LastPublishedAt = Now;
    GetOwner()->ForceNetUpdate(); OnRep_Snapshot(); // Listen房主与远端使用相同表现通知，默认4Hz上限。
}

void UDemoTeamPingComponent::OnCombatEnded()
{
    DEMO_LOG_CALL();
    if (bStopping) return;
    ActivePings.Reset(); Session.Reset(); bDirty = !Snapshot.IsEmpty(); // 不把上一场标记带到复用同地图的新关。
    PublishIfDue(GetWorld()->GetTimeSeconds()); EnsureMaintenance();
}

const TArray<FDemoTeamPing>& UDemoTeamPingComponent::GetSnapshot() const
{ DEMO_LOG_TICK(); return Snapshot; }

bool UDemoTeamPingComponent::ResolveDisplayLocation(const FDemoTeamPing& Ping, FVector& OutLocation) const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State = Cast<ADemoGameState>(GetOwner()); // 客户端仅基于服务器快照与同步时钟显示。
    if (!State || State->Phase != EDemoPhase::Combat || State->LevelNumber != Ping.LevelNumber || !IsValid(Ping.Sender)
        || Ping.Serial <= 0 || State->GetServerWorldTimeSeconds() >= Ping.ExpiresAtServerTime) return false;
    OutLocation = IsValid(Ping.Target) ? Ping.Target->GetActorLocation() + FVector(Ping.TargetOffset) : FVector(Ping.Location);
    return !OutLocation.ContainsNaN(); // Actor暂未映射时显示确认地点，不在客户端自行补射线。
}

void UDemoTeamPingComponent::OnRep_Snapshot()
{ DEMO_LOG_CALL(); OnChanged.Broadcast(); }

void UDemoTeamPingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UDemoTeamPingComponent, Snapshot);
}

void UDemoTeamPingComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL(); bStopping = true;
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MaintenanceTimer);
    ActivePings.Reset(); Snapshot.Reset(); NextAllowedAt.Reset(); Session.Reset(); OnChanged.Clear();
    Super::EndPlay(Reason);
}

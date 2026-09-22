#include "Spawning/DemoEnemySpawnComponent.h"
#include "Game/DemoEncounterRules.h"
#include "AI/DemoEnemy.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

UDemoEnemySpawnComponent::UDemoEnemySpawnComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
int32 UDemoEnemySpawnComponent::GetRemaining() const { DEMO_LOG_TICK(); return Alive.Num() + PendingMinions + (bPendingBoss ? 1 : 0); }
int32 UDemoEnemySpawnComponent::GetAliveCount() const { DEMO_LOG_TICK(); return Alive.Num(); }
bool UDemoEnemySpawnComponent::IsEncounterComplete() const { DEMO_LOG_TICK(); return bComplete && GetRemaining() == 0; }
bool UDemoEnemySpawnComponent::StartEncounter(const FDemoSpawnPlan& Plan, const FTransform& AreaTransform, const FDemoSpawnGeometry& Geometry, FString& Error)
{
    DEMO_LOG_CALL();
    if (!DemoSpawnPlan::ValidateSettings(Settings, Error)) return false;
    if (!GetOwner() || !GetOwner()->HasAuthority() || bRunning || !Alive.IsEmpty() || Plan.Level < 1 || Plan.RunId.IsEmpty()
        || Plan.MinionCount < 1 || Plan.MinionCount > 1000000 || Plan.MaxAlive < 1 || Plan.MaxAlive > 64 || AreaTransform.ContainsNaN()
        || !Plan.Minions.Contains(EDemoEnemyRole::Melee) || !Plan.Minions.Contains(EDemoEnemyRole::Charger) || !Plan.Minions.Contains(EDemoEnemyRole::Ranged)
        || (Plan.Level % DemoEncounterRules::BossInterval == 0) != Plan.bBoss
        || (Plan.Level % DemoEncounterRules::RangedInterval == 0 && (Plan.MinionCount < 3 || Plan.MaxAlive < (Plan.bBoss ? 4 : 3))))
    { Error = TEXT("刷怪启动被拒绝：权限、旧计划、数量或兵种快照非法"); UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_START %s"), *Error); return false; }
    ActivePlan = Plan; FrozenSettings = Settings; SpawnTransform = AreaTransform; SpawnGeometry = Geometry;
    PendingMinions = Plan.MinionCount; bPendingBoss = Plan.bBoss; NextMinion = BlockedPasses = 0; bComplete = false; bRunning = true;
    UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_START run=%s level=%d count=%d boss=%d maxAlive=%d"), *Plan.RunId, Plan.Level, Plan.MinionCount, Plan.bBoss, Plan.MaxAlive);
    // 先安装时钟再首次填充；填充耗尽队列时能够真正撤销它，避免遗留空转Timer。
    GetWorld()->GetTimerManager().SetTimer(FillTimer, this, &UDemoEnemySpawnComponent::Fill, FrozenSettings.Interval, true);
    OnRemainingChanged.Broadcast(GetRemaining());
    Fill();
    return true;
}
ADemoEnemy* UDemoEnemySpawnComponent::TrySpawn(bool bBoss, int32 MinionIndex)
{
    DEMO_LOG_CALL();
    FDemoEnemySpawnStats Stats = bBoss ? ActivePlan.Boss : ActivePlan.Minions.FindChecked(EDemoEnemyRole::Melee); // 先用共用规则决定角色，再选择该角色专属数值。
    if (!bBoss)
    {
        if (!DemoEncounterRules::AssignMinionRole(ActivePlan.Level, MinionIndex, GetTypeHash(ActivePlan.RunId), Stats)) return nullptr;
        Stats = ActivePlan.Minions.FindChecked(Stats.Tactics.Role);
    }
    Stats.FormationSlot = bBoss ? ActivePlan.MinionCount : MinionIndex; // 保留普通关小怪原阵型编号，Boss不打乱随机池。
    TSubclassOf<ADemoEnemy> EnemyClass = ADemoEnemy::StaticClass(); // 未覆盖时沿用原生实例；冻结UPROPERTY保障类生命周期。
    if (bBoss && FrozenSettings.BossClass) EnemyClass = FrozenSettings.BossClass;
    else if (!bBoss)
        if (const TSubclassOf<ADemoEnemy>* Override = FrozenSettings.MinionClasses.Find(Stats.Tactics.Role)) EnemyClass = *Override; // 只在调用内借用映射项。
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0); // 当前帧玩家位置，随其移动重新判定安全距离。
    for (int32 Attempt = 0; Attempt < FrozenSettings.CandidateAttempts; ++Attempt) // 有界候选，不阻塞整个游戏线程。
    {
        const float Angle = 2.f * PI * (float(MinionIndex) / (ActivePlan.bReinforcementLayout ? 24.f : FMath::Max(1, ActivePlan.MinionCount)) + float(Attempt) / FrozenSettings.CandidateAttempts); // 第一个点保留旧布局，阻挡才沿环重试。
        const FVector2D Radius = ActivePlan.bReinforcementLayout || bBoss ? SpawnGeometry.ReinforcementRadius : SpawnGeometry.CampaignRadius; // Boss备用候选使用外围，避免专用点堵塞导致永久失败。
        const FVector Offset = ActivePlan.bReinforcementLayout || bBoss ? FVector(0, 0, bBoss ? SpawnGeometry.BossOffset.Z : SpawnGeometry.ReinforcementHeight) : SpawnGeometry.CampaignOffset; // 局部高度以区域原点为地面。
        const FVector Local = bBoss && Attempt == 0 ? SpawnGeometry.BossOffset : Offset + FVector(Radius.X * FMath::Cos(Angle), Radius.Y * FMath::Sin(Angle), 0); // Boss优先专用点。
        const FVector Location = SpawnTransform.TransformPosition(Local); // 值变换不持有场景引用。
        if (Player && FVector::Dist2D(Location, Player->GetActorLocation()) < FrozenSettings.MinPlayerDistance) continue;
        // Boss在Configure后球半径增至115cm，因此生成前用最终尺寸检查，避免只按默认48cm误入墙体。
        if (GetWorld()->OverlapBlockingTestByChannel(Location, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(bBoss ? 115.f : 48.f))) continue;
        FActorSpawnParameters Spawn; // 不强制穿透、不自动移动到安全距离以外；下一个候选负责恢复。
        Spawn.Owner = GetOwner(); // 标识生成来源，不改变World所有权；独立测试/玩法实例可以准确区分自己的敌人。
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
        ADemoEnemy* Enemy = GetWorld()->SpawnActor<ADemoEnemy>(EnemyClass, Location, SpawnTransform.Rotator(), Spawn); // World拥有，Blueprint BeginPlay可能主动销毁，需再次检查。
        if (!IsValid(Enemy)) continue;
        if (Player && FVector::Dist2D(Enemy->GetActorLocation(), Player->GetActorLocation()) < FrozenSettings.MinPlayerDistance) { Enemy->Destroy(); continue; }
        Alive.Add(Enemy);
        Enemy->OnDefeated.AddUObject(this, &UDemoEnemySpawnComponent::NotifyEnemyDefeated);
        Enemy->OnEndPlay.AddDynamic(this, &UDemoEnemySpawnComponent::HandleEnemyEndPlay);
        // 先消费队列再配置，初始化回调看到的剩余数不会重复包含这只怪。
        if (bBoss) bPendingBoss = false; else { --PendingMinions; ++NextMinion; }
        Enemy->Configure(Stats);
        UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_CREATED level=%d index=%d boss=%d role=%d actor=%s"), ActivePlan.Level, MinionIndex, bBoss, static_cast<int32>(Stats.Tactics.Role), *Enemy->GetName());
        return Enemy;
    }
    return nullptr;
}
void UDemoEnemySpawnComponent::Fill()
{
    DEMO_LOG_CALL();
    if (!bRunning) { UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_FILL ignored cancelled/completed")); return; }
    for (int32 Generated = 0; bRunning && Generated < FrozenSettings.BatchLimit && Alive.Num() < ActivePlan.MaxAlive && (PendingMinions > 0 || bPendingBoss); ++Generated) // 每帧有界工作量，队列总数不影响帧开销。
    {
        if (!TrySpawn(bPendingBoss, NextMinion))
        {
            ++BlockedPasses; // 重试计数必须独立于日志求值，包体裁掉详细日志也继续正确超限。
            UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_BLOCKED level=%d pass=%d pending=%d"), ActivePlan.Level, BlockedPasses, PendingMinions + (bPendingBoss ? 1 : 0));
            if (BlockedPasses == 1) UE_LOG(LogFPSDemo, Warning, TEXT("SPAWN_BLOCKED begin level=%d; retries summarized"), ActivePlan.Level);
            if (BlockedPasses >= FrozenSettings.BlockedPassLimit) Fail(TEXT("出生点持续被阻挡，请检查刷怪区域或重试"));
            return;
        }
        if (BlockedPasses > 0) UE_LOG(LogFPSDemo, Display, TEXT("SPAWN_RECOVERED level=%d retries=%d"), ActivePlan.Level, BlockedPasses);
        BlockedPasses = 0;
    }
    if (!bRunning) return;
    OnRemainingChanged.Broadcast(GetRemaining());
    if (PendingMinions == 0 && !bPendingBoss) GetWorld()->GetTimerManager().ClearTimer(FillTimer);
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
    UnbindEnemy(Enemy); // 先移除再发事件，重复死亡或尸体销毁不能重复奖励。
    OnRemainingChanged.Broadcast(GetRemaining());
    OnEnemyDefeated.Broadcast(Enemy);
    if (bRunning && GetRemaining() == 0) CompleteTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UDemoEnemySpawnComponent::Complete);
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
    if (!bRunning || GetRemaining() != 0) { UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_CLEAR ignored stale/nonempty")); return; }
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
    bRunning = bComplete = false; PendingMinions = 0; bPendingBoss = false;
    GetWorld()->GetTimerManager().ClearTimer(FillTimer); GetWorld()->GetTimerManager().ClearTimer(CompleteTimer);
    const TArray<TWeakObjectPtr<ADemoEnemy>> Previous = Alive.Array(); // 弱引用值快照；先解绑所有对象再允许Destroy回调。
    for (const TWeakObjectPtr<ADemoEnemy>& Entry : Previous) if (ADemoEnemy* Enemy = Entry.Get()) UnbindEnemy(Enemy); // 仅当前同步循环借用。
    Alive.Empty();
    if (bDestroyEnemies) for (const TWeakObjectPtr<ADemoEnemy>& Entry : Previous) if (ADemoEnemy* Enemy = Entry.Get()) Enemy->Destroy(); // 不发布击杀/清场事件。
    UE_LOG(LogFPSDemo, Log, TEXT("SPAWN_CANCEL destroy=%d actors=%d"), bDestroyEnemies, Previous.Num());
}
void UDemoEnemySpawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); CancelEncounter(false); Super::EndPlay(EndPlayReason);
}

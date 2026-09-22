#include "Tests/DemoEnemyAttackTest.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Economy/DemoRewardComponent.h"
#include "Economy/DemoShopComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "AI/DemoEnemy.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"

void ADemoEnemyAttackTest::OnTestSpawnDefeated(ADemoEnemy* Enemy)
{
    DEMO_LOG_CALL(); ++SpawnKills;
    if (!Check(Enemy && !Enemy->IsAlive(), TEXT("spawn event carries real dead enemy"))) return;
    TestSpawner->NotifyEnemyDefeated(Enemy); // 同栈重入重复通知必须被注册表拒绝。
}
void ADemoEnemyAttackTest::OnTestSpawnCleared() { DEMO_LOG_CALL(); ++SpawnClears; }
void ADemoEnemyAttackTest::OnTestSpawnFailed(const FString& Reason) { DEMO_LOG_CALL(); ++SpawnFailures; UE_LOG(LogFPSDemo, Log, TEXT("EXPECTED_SPAWN_FAILURE %s"), *Reason); }
void ADemoEnemyAttackTest::ProcessTestSpawnEnemies(bool bKill, int32 Count)
{
    DEMO_LOG_CALL();
    for (TActorIterator<ADemoEnemy> It(GetWorld()); It && Count > 0; ++It) // Owner过滤隔离夹具，不能杀正式关卡怪来伪造完成。
        if (It->GetOwner() == this && It->IsAlive())
        {
            It->SetActorTickEnabled(false);
            if (bKill) { DemoEffects::Apply(It->GetAbilitySystemComponent(), It->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -1000000.f); --Count; } // 真实负向Health GE触发死亡，不手工广播完成。
        }
}
void ADemoEnemyAttackTest::TickSpawnSystemTest()
{
    DEMO_LOG_TICK();
    if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds() < 30.f, TEXT("spawn systems bounded runtime"))) return;
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 每步同步借用权威服务，测试不旅行。
    FDemoLevelRow Level; FDemoDifficultyRow Difficulty; FString Error; // 真实配置解析和错误文本。
    if (!Check(Mode && Mode->GetLevelConfig(1, Level, Difficulty, Error), TEXT("real first-level config available"))) return;
    Level.EnemyCount = Step == 0 ? 6 : 2; // 第一组专门制造队列，后续用小计划检查完成/取消/阻挡。
    const UDataTable* Enemies = Mode->EnemyTableAsset.LoadSynchronous(); // 使用真实已验证模板，不复制一套假GAS初始化。
    FDemoSpawnPlan Plan; // 每步独立输入计划，运行组件持有自己的值快照。
    FTransform AreaTransform(FVector(40000, 0, 10000)); FDemoSpawnGeometry Geometry; // 空中独立场地，与白模战役和安全区分离。
    FDemoSpawnSettings Settings; Settings.Interval = .1f; Settings.BatchLimit = 4; // 快速真实World时钟，保持生产最小并存预算。
    if (!Check(DemoSpawnPlan::Build(Level, Difficulty, Enemies, Settings, TEXT("SpawnRegression"), true, 4, Plan, Error), TEXT("build unified plan"))) return;
    switch (Step)
    {
    case 0:
    {
        if (!Check(Mode->StartRun() && Mode->GetSpawnSystem() && Mode->GetRewardSystem() && Mode->GetShopSystem(), TEXT("three owned systems initialized"))) return;
        if (!CheckComponentLifecycles()) return; // 实际服务已启动，验证重复通知和终端会话而不另造业务实现。
        if (!Check(!Mode->GetRewardSystem()->GrantVictory(TEXT("Invalid"), Difficulty) && !Mode->GetRewardSystem()->GrantAbilityChoice(TEXT("Invalid"), 0)
            && !Mode->GetShopSystem()->PurchaseUpgrade(0, nullptr), TEXT("reward and shop reject invalid phase/purchaser"))) return;
        ADemoEnemySpawnArea* Area = GetWorld()->SpawnActor<ADemoEnemySpawnArea>(AreaTransform.GetLocation(), FRotator(0, 30, 0)); // 实际场景区域，验证配置优先于fallback。
        ADemoEnemySpawnArea* Duplicate = GetWorld()->SpawnActor<ADemoEnemySpawnArea>(AreaTransform.GetLocation(), FRotator::ZeroRotator); // 人为冲突应明确拒绝。
        if (!Check(Area && Duplicate, TEXT("create authored area fixtures"))) return;
        if (!Check(!ADemoEnemySpawnArea::Resolve(GetWorld(), 0, FVector::ZeroVector, AreaTransform, Geometry, Error), TEXT("duplicate areas rejected"))) return;
        Duplicate->Destroy();
        if (!Check(ADemoEnemySpawnArea::Resolve(GetWorld(), 0, FVector::ZeroVector, AreaTransform, Geometry, Error)
            && AreaTransform.GetLocation().Equals(Area->GetActorLocation()), TEXT("authored area selected"))) return;
        Area->Destroy();
        TestSpawner = NewObject<UDemoEnemySpawnComponent>(this); AddInstanceComponent(TestSpawner); TestSpawner->RegisterComponent();
        TestSpawner->Settings = Settings;
        TestSpawner->OnEnemyDefeated.AddUObject(this, &ADemoEnemyAttackTest::OnTestSpawnDefeated);
        TestSpawner->OnCleared.AddUObject(this, &ADemoEnemyAttackTest::OnTestSpawnCleared);
        TestSpawner->OnFailed.AddUObject(this, &ADemoEnemyAttackTest::OnTestSpawnFailed);
        if (!Check(TestSpawner->StartEncounter(Plan, AreaTransform, Geometry, Error) && TestSpawner->GetAliveCount() == 4 && TestSpawner->GetRemaining() == 6, TEXT("shared scheduler respects alive budget and pending count"))) return;
        if (!Check(!TestSpawner->StartEncounter(Plan, AreaTransform, Geometry, Error), TEXT("running encounter cannot be overwritten"))) return;
        ProcessTestSpawnEnemies(false); ProcessTestSpawnEnemies(true, 1);
        if (!Check(SpawnKills == 1 && SpawnClears == 0 && TestSpawner->GetRemaining() == 5, TEXT("duplicate death gives exactly one event, queue prevents clear"))) return;
        Advance(.25f); break;
    }
    case 1:
        if (!Check(TestSpawner->GetAliveCount() == 4 && TestSpawner->GetRemaining() == 5, TEXT("timer refills after death"))) return;
        TestSpawner->CancelEncounter(true); Advance(.3f); break;
    case 2:
        if (!Check(TestSpawner->GetRemaining() == 0 && SpawnKills == 1 && SpawnClears == 0 && SpawnFailures == 0, TEXT("cancel stops refill and destroys without rewards or false clear"))) return;
        if (!Check(TestSpawner->StartEncounter(Plan, AreaTransform, Geometry, Error), TEXT("start completion cancellation fixture"))) return;
        ProcessTestSpawnEnemies(true);
        TestSpawner->CancelEncounter(true); // 最后一只死亡已排下一帧完成；取消必须撤销它。
        Advance(.2f); break;
    case 3:
        if (!Check(SpawnClears == 0 && !TestSpawner->IsEncounterComplete(), TEXT("cancel removes pending next-tick completion"))) return;
        if (!Check(TestSpawner->StartEncounter(Plan, AreaTransform, Geometry, Error), TEXT("start natural completion fixture"))) return;
        ProcessTestSpawnEnemies(true); Advance(.2f); break;
    case 4:
    {
        if (!Check(SpawnClears == 1 && TestSpawner->IsEncounterComplete(), TEXT("natural clear fires once"))) return;
        AStaticMeshActor* Blocker = GetWorld()->SpawnActor<AStaticMeshActor>(AreaTransform.GetLocation() + FVector(0, 0, 100), FRotator::ZeroRotator); // 大实体覆盖全部候选点，验证真实碰撞重试。
        if (!Check(Blocker != nullptr, TEXT("spawn blocker fixture"))) return;
        Blocker->SetMobility(EComponentMobility::Movable); Blocker->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
        Blocker->SetActorScale3D(FVector(50, 50, 10)); Blocker->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Wall = Blocker;
        TestSpawner->Settings.BlockedPassLimit = 10;
        if (!Check(TestSpawner->StartEncounter(Plan, AreaTransform, Geometry, Error) && TestSpawner->GetAliveCount() == 0 && TestSpawner->GetRemaining() == 2, TEXT("blocked candidates retain queue"))) return;
        Advance(.2f); break;
    }
    case 5:
        if (!Check(SpawnFailures == 0 && SpawnClears == 1, TEXT("temporary blocking does not fail or clear"))) return;
        Wall->SetActorEnableCollision(false); Advance(.2f); break;
    case 6:
        if (!Check(TestSpawner->GetAliveCount() == 2, TEXT("unblocked queue recovers"))) return;
        ProcessTestSpawnEnemies(true); Advance(.2f); break;
    case 7:
        if (!Check(SpawnClears == 2, TEXT("recovered plan clears once"))) return;
        Wall->SetActorEnableCollision(true); TestSpawner->Settings.BlockedPassLimit = 2;
        if (!Check(TestSpawner->StartEncounter(Plan, AreaTransform, Geometry, Error), TEXT("start bounded blocking failure"))) return;
        Advance(.3f); break;
    case 8:
        if (!Check(SpawnFailures == 1 && SpawnClears == 2 && TestSpawner->GetRemaining() == 0, TEXT("persistent blocker fails and cancels exactly once"))) return;
        Wall->Destroy();
        if (!Check(TestSpawner->StartEncounter(Plan, AreaTransform, Geometry, Error), TEXT("start lost enemy fixture"))) return;
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) if (It->GetOwner() == this && It->IsAlive()) { It->Destroy(); break; } // 真实异常EndPlay应失败而不是发银币。
        Advance(.2f); break;
    case 9:
        if (!Check(SpawnFailures == 2 && SpawnClears == 2 && TestSpawner->GetRemaining() == 0, TEXT("unexpected live destruction fails without clear"))) return;
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) if (It->GetOwner() == this) It->Destroy(); // 已取消并解绑，最终清理不增加事件。
        if (!CheckComponentShutdown()) return; // 放在所有场景末尾，验证停止不可逆且不会创建新交互。
        UE_LOG(LogFPSDemo, Display, TEXT("DEMO_SPAWN_SYSTEM_SUCCESS: region, budget, refill, duplicate, cancel, retry, failure, reward/shop guards"));
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 0); break;
    }
}

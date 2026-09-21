#include "Tests/DemoEnemyAttackTest.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyProjectile.h"
#include "Game/FPSDemoGameMode.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoGameplayAbility.h"
#include "GAS/DemoTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

ADemoEnemyAttackTest::ADemoEnemyAttackTest()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = .02f;
}
bool ADemoEnemyAttackTest::Check(bool Condition, const TCHAR* Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Display, TEXT("ATTACK_TEST %s: %s"), Condition ? TEXT("PASS") : TEXT("FAIL"), Message);
    if (!Condition) { bFailed = true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false, 1); }
    return Condition;
}
void ADemoEnemyAttackTest::Advance(float Delay)
{
    DEMO_LOG_CALL();
    ++Step;
    NextTime = GetWorld()->GetTimeSeconds() + Delay;
}
ADemoEnemy* ADemoEnemyAttackTest::SpawnEnemy(const FVector& Location, bool bBoss)
{
    DEMO_LOG_CALL();
    // Stats是本测试的固定快照，10伤害、快速测试周期；仍走生产Configure和频率校验。
    FDemoEnemySpawnStats Stats;
    Stats.Health = 100.f;
    Stats.AttackPower = 10.f;
    Stats.bBoss = bBoss;
    Stats.Attack.ProjectileInterval = .6f;
    Stats.Attack.ProjectileSpeed = 2000.f;
    Stats.Attack.GlobalInterval = 3.f;
    Stats.Attack.SlowDuration = .8f;
    // Spawn允许在测试选定的空中区域生成，避免天然地形影响弹道用例。
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADemoEnemy* Enemy = GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(), Location, FRotator::ZeroRotator, Spawn);
    if (Enemy) { Enemy->Configure(Stats); Enemy->SetActorTickEnabled(false); }
    return Enemy;
}
void ADemoEnemyAttackTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    // 复用已隔离的EnemyAttack测试入口；专项不同时驱动原攻击状态机。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoBossDiveTest"))) { TickDiveTest(); return; } // 同一测试隔离/禁云入口，不注册普通游戏任务。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoEnemyTacticsTest"))) { TickTacticsTest(); return; } // 战术专项沿用本测试隔离档和禁云规则。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoEnemyNavigationTest"))) { TickNavigationTest(); return; }
    if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds() < 55.f, TEXT("bounded runtime"))) return;
    // 当前步重新取得World对象，测试不跨关卡存储裸指针。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>();
    ADemoCharacter* Player = Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
    if (!Check(Mode && State && Player && Player->GetDemoASC(), TEXT("runtime initialized"))) return;
    UDemoAbilitySystemComponent* ASC = Player->GetDemoASC(); // 本步借用PS拥有的ASC。
    const UDemoAttributeSet* Attributes = Player->GetDemoAttributes(); // 本步只读属性视图。
    switch (Step)
    {
    case 0:
        if (!Check(Mode->StartRun(), TEXT("start safe hub"))) return;
        Mode->StartNextLevel();
        if (!Check(State->Phase == EDemoPhase::Combat, TEXT("enter combat"))) return;
        // 冻结真实战役怪，保留注册表以避免清场；测试专用怪在空中与它们隔离。
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); }
        Player->SetActorLocation(Mode->GetAreaCenter(0) + FVector(0,0,1500));
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Player->GetCharacterMovement()->StopMovementImmediately();
        DemoEffects::Apply(ASC, ASC, UDemoHealthEffect::StaticClass(), -50.f);
        Shooter = SpawnEnemy(Player->GetActorLocation() + FVector(800,0,0), false);
        if (!Check(Shooter.IsValid(), TEXT("spawn minion test source"))) return;
        // 验证频率校验不能被JSON负值绕过，合法短间隔也必须保留。
        {
            FDemoEnemyAttackSettings Invalid; // 仅测试副本，不更改正式模板。
            Invalid.ProjectileInterval = 0.f;
            if (!Check(!Invalid.IsValid(), TEXT("zero attack interval rejected"))) return;
        }
        Advance(2.1f);
        break;
    case 1:
        if (!Check(Shooter->TryFireProjectile(Player) && !Shooter->TryFireProjectile(Player), TEXT("minion projectile fires once then configured cooldown rejects"))) return;
        Advance(.12f);
        break;
    case 2:
        if (!Check(Attributes->GetHealth() == 50.f && TActorIterator<ADemoEnemyProjectile>(GetWorld()), TEXT("projectile has travel time, no instant damage"))) return;
        // 可选真实RHI截图用于确认弹体可见；不在nullrhi数值测试中请求截图。
        if (FParse::Param(FCommandLine::Get(), TEXT("DemoAttackCapture")))
        {
            // It/Components为本帧借用，用于将可见性诊断与截图关联，不改变弹道。
            for (TActorIterator<ADemoEnemyProjectile> It(GetWorld()); It; ++It)
            {
                TArray<UStaticMeshComponent*> Components;
                It->GetComponents(Components);
                for (const UStaticMeshComponent* Mesh : Components) // Mesh由投射物拥有，仅同步读取。
                    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_VISUAL actor=%s mesh=%s scale=%s visible=%d hidden=%d material=%s"),
                        *It->GetActorLocation().ToString(), *Mesh->GetComponentLocation().ToString(), *Mesh->GetComponentScale().ToString(),
                        Mesh->IsVisible(), It->IsHidden(), *GetNameSafe(Mesh->GetMaterial(0)));
            }
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/EnemyAttacks/Projectile.png"), false, false);
        }
        Advance(.55f);
        break;
    case 3:
        if (!Check(Attributes->GetHealth() == 40.f && !TActorIterator<ADemoEnemyProjectile>(GetWorld()), TEXT("swept minion impact applies exactly one GAS hit"))) return;
        if (!Check(Shooter->TryFireProjectile(Player), TEXT("configured 0.6s interval permits next shot"))) return;
        {
            // WallActor在发射后出现在弹道中，验证真实飞行碰撞而不只验证发射视线。
            AStaticMeshActor* WallActor = GetWorld()->SpawnActor<AStaticMeshActor>(Player->GetActorLocation()+FVector(400,0,0), FRotator::ZeroRotator);
            Wall = WallActor;
            WallActor->SetMobility(EComponentMobility::Movable);
            WallActor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
            WallActor->SetActorScale3D(FVector(.2f,4.f,4.f));
            WallActor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        }
        Advance(.7f);
        break;
    case 4:
        if (!Check(Attributes->GetHealth() == 40.f && !TActorIterator<ADemoEnemyProjectile>(GetWorld()) && !Shooter->TryFireProjectile(Player),
            TEXT("thin wall absorbs in-flight shot and blocks firing sight"))) return;
        Wall->Destroy();
        Shooter->SetActorEnableCollision(false);
        Boss = SpawnEnemy(Player->GetActorLocation()+FVector(1100,0,0), true);
        if (!Check(Boss.IsValid(), TEXT("spawn boss test source"))) return;
        Advance(2.1f);
        break;
    case 5:
        if (!Check(Boss->TryFireProjectile(Player), TEXT("boss also fires traveling projectiles"))) return;
        Advance(1.f);
        break;
    case 6:
        if (!Check(Attributes->GetHealth() == 30.f && Boss->TryStartGlobalAttack(), TEXT("boss projectile damage and full-map cast start"))) return;
        BossStart = Boss->GetActorLocation();
        Boss->SetActorTickEnabled(true);
        Advance(1.7f);
        break;
    case 7:
        if (!Check(Boss->IsGlobalAttackWindingUp() && Boss->GetActorLocation().Equals(BossStart,.01f) && Attributes->GetHealth() == 30.f,
            TEXT("boss remains stationary and deals no global damage during 2s windup"))) return;
        // 当前仍有0.3秒前摇，下一渲染帧能可靠捕捉红光状态，不改变定时器时序。
        if (FParse::Param(FCommandLine::Get(), TEXT("DemoAttackCapture")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/EnemyAttacks/BossWindup.png"), false, false);
        Boss->SetActorTickEnabled(false); // 保留真实Timer，只冻结自动开始其他攻击，隔离结算检查。
        Advance(.4f);
        break;
    case 8:
        if (!Check(!Boss->IsGlobalAttackWindingUp() && Attributes->GetHealth() == 20.f && ASC->HasMatchingGameplayTag(DemoTags::Slowed)
            && FMath::IsNearlyEqual(Player->GetCharacterMovement()->MaxWalkSpeed,325.f), TEXT("global hit applies GAS damage and 50 percent slow"))) return;
        Advance(1.f);
        break;
    case 9:
        if (!Check(!ASC->HasMatchingGameplayTag(DemoTags::Slowed) && FMath::IsNearlyEqual(Player->GetCharacterMovement()->MaxWalkSpeed,650.f), TEXT("slow GE expiry restores baseline speed"))) return;
        if (!Check(Boss->TryStartGlobalAttack(), TEXT("configured global interval allows second cast"))) return;
        Advance(1.7f);
        break;
    case 10:
        if (!Check(ASC->ActivateDemoAbility(UDemoDashAbility::StaticClass()) && Player->IsDashEvading(), TEXT("committed dash opens distinct 0.45s evade window"))) return;
        Advance(.4f);
        break;
    case 11:
        Player->GetCharacterMovement()->StopMovementImmediately();
        if (!Check(Attributes->GetHealth() == 25.f && !ASC->HasMatchingGameplayTag(DemoTags::Slowed) && !Boss->TryStartGlobalAttack(),
            TEXT("timed dash evades global damage and slow, heals exactly five, cooldown blocks duplicate cast"))) return;
        Advance(1.f);
        break;
    case 12:
        {
            // 只在测试中移除Dash冷却来覆盖早冲刺用例；生产技能仍保留4秒冷却。
            FGameplayTagContainer CooldownTags;
            CooldownTags.AddTag(DemoTags::DashCooldown);
            ASC->RemoveActiveEffectsWithGrantedTags(CooldownTags);
        }
        if (!Check(Boss->TryStartGlobalAttack() && ASC->ActivateDemoAbility(UDemoDashAbility::StaticClass()), TEXT("start third cast with deliberately early dash"))) return;
        Advance(.8f);
        break;
    case 13:
        Player->GetCharacterMovement()->StopMovementImmediately();
        if (!Check(!Player->IsDashEvading() && Boss->IsGlobalAttackWindingUp(), TEXT("early dash expires before global release"))) return;
        // 超过普通索敌距离仍应被全图结算，不能用距离绕过攻击。
        Player->SetActorLocation(Boss->GetActorLocation()+FVector(6000,0,0));
        Advance(1.4f);
        break;
    case 14:
        if (!Check(Attributes->GetHealth() == 15.f && ASC->HasMatchingGameplayTag(DemoTags::Slowed), TEXT("early dash fails; global hits beyond normal detection range"))) return;
        Advance(1.f);
        break;
    case 15:
        if (!Check(Boss->TryStartGlobalAttack(), TEXT("begin cancel-on-death case"))) return;
        DemoEffects::Apply(ASC, Boss->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -10000.f);
        Advance(2.2f);
        break;
    case 16:
        if (!Check(!Boss.IsValid() && Attributes->GetHealth() == 15.f && !ASC->HasMatchingGameplayTag(DemoTags::Slowed), TEXT("dead boss cancels timer without damage or dodge reward"))) return;
        Boss = SpawnEnemy(Player->GetActorLocation()+FVector(1000,0,0), true);
        Advance(3.1f);
        break;
    case 17:
        if (!Check(Boss.IsValid() && Boss->TryStartGlobalAttack() && Boss->TryFireProjectile(Player) == false, TEXT("global windup blocks concurrent projectile"))) return;
        DemoEffects::ApplySlow(Boss->GetAbilitySystemComponent(), ASC, .5f, 3.f);
        Mode->NotifyPlayerDied(); // 仅测试触发正式终局入口以检查同帧撤销；不更改健康用于检测残留伤害。
        if (!Check(State->Phase == EDemoPhase::Defeat && !Boss->IsGlobalAttackWindingUp() && !ASC->HasMatchingGameplayTag(DemoTags::Slowed)
            && FMath::IsNearlyEqual(Player->GetCharacterMovement()->MaxWalkSpeed,650.f), TEXT("leaving combat immediately clears cast and slow"))) return;
        Advance(2.2f);
        break;
    case 18:
        if (!Check(Attributes->GetHealth() == 15.f && !Boss->TryFireProjectile(Player) && !Boss->TryStartGlobalAttack(), TEXT("no delayed damage or healing after combat ends"))) return;
        UE_LOG(LogFPSDemo, Display, TEXT("DEMO_ENEMY_ATTACK_TEST_SUCCESS"));
        SetActorTickEnabled(false);
        FPlatformMisc::RequestExitWithStatus(false, 0);
        break;
    }
}

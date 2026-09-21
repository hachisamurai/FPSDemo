#include "Tests/DemoEnemyAttackTest.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyNavigation.h"
#include "Characters/DemoCharacter.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/DemoPlayerController.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Debug/DemoLog.h"

void ADemoEnemyAttackTest::TickNavigationTest()
{
    DEMO_LOG_TICK();
    if (bFailed || GetWorld()->GetTimeSeconds() < NextTime) return;
    if (NavigationDeadline > 0 && FPlatformTime::Seconds() > NavigationDeadline)
    {
        UE_LOG(LogFPSDemo,Error,TEXT("NAV_TEST timeout step=%d shooter=%s boss=%s"),Step,Shooter.IsValid()?*Shooter->GetActorLocation().ToString():TEXT("none"),Boss.IsValid()?*Boss->GetActorLocation().ToString():TEXT("none"));
        Check(false,TEXT("navigation must finish within bounded time")); return;
    }
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 本World权威流程，测试只在独立进程创建。
    ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 用真实阶段约束导航和施法。
    ADemoPlayerController* PC = Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本帧借用，不跨旅行保留。
    ADemoCharacter* Player = PC ? Cast<ADemoCharacter>(PC->GetPawn()) : nullptr; // 真实碰撞和Visibility目标。
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); // 真实运行时Recast，不使用mock路线。
    if (!Mode || !State || !Player || !Nav) { Check(false,TEXT("navigation runtime dependencies")); return; }
    const FVector Center = Mode->GetAreaCenter(0); // 当前白模地面中心，保持测试在本关导航范围。
    if (Step == 0)
    {
        if (!Check(Mode->StartRun(),TEXT("navigation test starts isolated campaign"))) return;
        Mode->StartNextLevel(); PC->OnRunReady();
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 冻结注册怪，防测试绕行期间清关/受伤。
        Player->SetActorLocation(Center+FVector(450,0,100));
        Player->GetCharacterMovement()->StopMovementImmediately();
        AStaticMeshActor* Obstacle = GetWorld()->SpawnActor<AStaticMeshActor>(); // 独立测试掩体，只存在本World，不写地图。
        Wall = Obstacle;
        Obstacle->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        Obstacle->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Obstacle->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
        Obstacle->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
        Obstacle->SetActorScale3D(FVector(1,7,3));
        Obstacle->SetActorLocation(Center+FVector(0,0,150));
        NavigationDeadline = FPlatformTime::Seconds()+40;
        Advance(2.f); return; // 等待真实动态导航更新，不在运行时强制同步Build。
    }
    if (Step == 1)
    {
        if (Nav->IsNavigationBuildInProgress()) return;
        FDemoEnemySpawnStats Stats; // 测试只放宽生存时间，不更改正式数据表；300cm/s便于有界验证。
        Stats.Health=10000; Stats.AttackPower=0; Stats.MoveSpeed=300; Stats.Attack.GlobalInterval=120;
        Shooter = GetWorld()->SpawnActor<ADemoEnemy>(Center+FVector(-450,0,110),FRotator::ZeroRotator);
        if (!Check(Shooter.IsValid(),TEXT("spawn navigation minion"))) return;
        Shooter->Configure(Stats);
        if (!Check(!Shooter->FindComponentByClass<UDemoEnemyNavigation>()->CanSee(Player),TEXT("initial cover blocks sight"))) return;
        Advance(.1f); return;
    }
    if (Step == 2)
    {
        if (!Shooter.IsValid() || FVector::Dist2D(Shooter->GetActorLocation(),Player->GetActorLocation()) > 175.f) return;
        if (!Check(Shooter->FindComponentByClass<UDemoEnemyNavigation>()->CanSee(Player),TEXT("minion follows complete route around solid cover"))) return;
        Shooter->Destroy();
        Player->SetActorLocation(Center+FVector(250,0,100));
        FDemoEnemySpawnStats Stats; // Boss以实际115cm碰撞验证保守代理，不放宽穿墙碰撞。
        Stats.Health=10000; Stats.AttackPower=0; Stats.MoveSpeed=300; Stats.bBoss=true; Stats.Attack.GlobalInterval=120;
        Boss = GetWorld()->SpawnActor<ADemoEnemy>(Center+FVector(-250,0,130),FRotator::ZeroRotator);
        if (!Check(Boss.IsValid(),TEXT("spawn large boss inside stop distance but behind cover"))) return;
        Boss->Configure(Stats);
        BossStart = Boss->GetActorLocation();
        if (!Check(!Boss->FindComponentByClass<UDemoEnemyNavigation>()->CanSee(Player),TEXT("nearby boss sight initially blocked"))) return;
        NavigationDeadline = FPlatformTime::Seconds()+25;
        Advance(.1f); return;
    }
    if (Step == 3)
    {
        if (!Boss->FindComponentByClass<UDemoEnemyNavigation>()->CanSee(Player)) return;
        if (!Check(FVector::Dist2D(BossStart,Boss->GetActorLocation()) > 100.f,TEXT("boss does not stop behind wall just because distance is sufficient"))) return;
        Boss->CancelPendingAttacks();
        State->Phase = EDemoPhase::Hub; // 测试阶段守卫，不调用返回安全区销毁测试Actor。
        BossStart = Boss->GetActorLocation();
        Advance(.8f); return;
    }
    if (Step == 4)
    {
        if (!Check(Boss->GetActorLocation().Equals(BossStart,1.f),TEXT("noncombat phase cannot continue cached route"))) return;
        State->Phase = EDemoPhase::Combat;
        Boss->Destroy(); Wall->Destroy();
        // 最后验证三张战斗区域存在可用数据；不能只修好第一关。
        for (int32 Arena=0; Arena<3; ++Arena)
        {
            FNavLocation Point; // 当前区域地面投影值，不持有Nav多边形生命周期。
            if (!Check(Nav->ProjectPointToNavigation(Mode->GetAreaCenter(Arena)+FVector(600,0,50),Point,FVector(150,150,180)),TEXT("all three arenas contain generated navigation"))) return;
        }
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_ENEMY_NAVIGATION_SUCCESS"));
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

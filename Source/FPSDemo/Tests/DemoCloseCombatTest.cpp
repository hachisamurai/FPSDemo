#include "Tests/DemoEnemyAttackTest.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoCloseCombat.h"
#include "AI/DemoEnemyTactics.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoGameplayAbility.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"

void ADemoEnemyAttackTest::TickCloseCombatTest()
{
    DEMO_LOG_TICK(); if (bFailed||GetWorld()->GetTimeSeconds()<NextTime) return;
    if (GetWorld()->GetTimeSeconds()>120) { Check(false,TEXT("bounded close combat runtime")); return; }
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前权威流程，不跨地图保留。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 真实阶段守卫。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本地玩家输入上下文。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 本帧目标，测试不强持有。
    if (!Mode||!State||!Player) { Check(false,TEXT("close test dependencies")); return; }
    const FVector Center=Mode->GetAreaCenter(0); // 实际NavMesh平面战斗区。
    UDemoCloseCombat* Close=Shooter.IsValid()?Shooter->FindComponentByClass<UDemoCloseCombat>():nullptr; // 实际生产组件，不手工推进回调。
    if (Step==0)
    {
        if (!Check(Mode->StartRun(),TEXT("start isolated close combat run"))) return;
        Mode->StartNextLevel(); PC->OnRunReady();
        int32 Roles[6]={0,0,0,0,0,0}; // 计数真实首关两类近身怪，确保没有任何可射击角色混入。
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It)
        { ++Roles[static_cast<int32>(It->FindComponentByClass<UDemoEnemyTactics>()->GetRole())]; It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 注册怪保留，避免专项误清关。
        if (!Check(Roles[4]>0&&Roles[5]>0&&Roles[1]==0&&Roles[2]==0&&Roles[3]==0,TEXT("production first level only includes melee and charger"))) return;
        FDemoCloseCombatSettings Invalid; Invalid.ChargeDistance=200; // 值副本，不能把最大起手距离配置为超过冲刺行程。
        if (!Check(!Invalid.IsValid(),TEXT("invalid charge distance rejected"))) return;
        FDemoEnemyTacticsSettings BadMix; BadMix.MixedRoles={EDemoEnemyRole::Mixed}; // 拒绝递归混编。
        if (!Check(!BadMix.IsValid(),TEXT("recursive mixed role rejected"))) return;
        Advance(.1f); return;
    }
    if (Step==1)
    {
        State->Phase=EDemoPhase::Combat;
        Player->SetActorLocation(Center+FVector(0,0,100)); Player->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator(0,180,0));
        DemoEffects::Apply(Player->GetDemoASC(),Player->GetDemoASC(),UDemoHealthEffect::StaticClass(),1000);
        CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth();
        FDemoEnemySpawnStats Stats; // 10攻击力，精确验证近战10/冲刺15，模板伤害仍经真实GAS。
        Stats.Health=1000; Stats.AttackPower=10; Stats.bUseTactics=true;
        Stats.Tactics.Role=CloseScenario<3?EDemoEnemyRole::Melee:EDemoEnemyRole::Charger;
        Shooter=GetWorld()->SpawnActor<ADemoEnemy>(Center+FVector(CloseScenario<3?-155.f:-700.f,0,110),FRotator::ZeroRotator);
        if (!Check(Shooter.IsValid(),TEXT("spawn close role"))) return;
        Shooter->Configure(Stats);
        if (!Check(!Shooter->TryFireProjectile(Player),TEXT("pure melee and charger never fire projectile"))) return;
        CloseStartTime=GetWorld()->GetTimeSeconds(); Advance(.01f); return;
    }
    if (Step==2)
    {
        if (Close->GetPhase()==EDemoClosePhase::Idle) return;
        if (!Check(Close->GetPhase()==(CloseScenario<3?EDemoClosePhase::MeleeWindup:EDemoClosePhase::ChargeWindup),TEXT("configured archetype selects correct attack"))) return;
        if (!Check(GetWorld()->GetTimeSeconds()-CloseStartTime>=1.95f&&FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),CloseBeforeHealth),TEXT("spawn buffer and windup cause no instant damage"))) return;
        CloseStartTime=GetWorld()->GetTimeSeconds(); CloseLockedDirection=Close->GetLockedDirection();
        if (CloseScenario==1) Player->SetActorLocation(Center+FVector(0,450,100)); // 近战前摇期间真实出圈。
        if (CloseScenario==2) Shooter->GetAbilitySystemComponent()->AddLooseGameplayTag(DemoAmmoTags::Frozen); // 守卫夹具，等价有效Frozen GE标签。
        if (CloseScenario==4) Player->SetActorLocation(Center+FVector(0,500,100)); // 玩家侧闪，突进不得改变锁定方向。
        if (CloseScenario==5)
        {
            AStaticMeshActor* Obstacle=GetWorld()->SpawnActor<AStaticMeshActor>(); // 起手后出现真实碰撞墙，禁止高速穿越。
            Wall=Obstacle; Obstacle->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Obstacle->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Obstacle->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Obstacle->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
            Obstacle->SetActorScale3D(FVector(1,4,4)); Obstacle->SetActorLocation(Center+FVector(-300,0,150));
        }
        if (CloseScenario==6) State->Phase=EDemoPhase::Hub; // 通过Enemy真实Tick阶段守卫取消。
        if (CloseScenario==8) DemoEffects::Apply(Player->GetDemoASC(),Shooter->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-2000); // 死亡委托立即取消，延迟销毁。
        if (CloseScenario==3&&FParse::Param(FCommandLine::Get(),TEXT("DemoCloseCapture")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/CloseCombat/ChargeWindup.png"),false,false); // 真实渲染预警，不用绘图替代资产。
        Advance(.15f); return;
    }
    if (Step==3)
    {
        if (!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),CloseBeforeHealth),TEXT("readable windup gives reaction time"))) return;
        Advance(.01f); return;
    }
    if (Step==4)
    {
        if (CloseScenario==2||CloseScenario==6||CloseScenario==8)
        {
            if (GetWorld()->GetTimeSeconds()-CloseStartTime<1.5f) return;
            if (!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),CloseBeforeHealth)&&(!Close||Close->GetPhase()==EDemoClosePhase::Idle),TEXT("frozen phase exit or death cancels pending damage"))) return;
        }
        else
        {
            if (CloseScenario==7&&Close->GetPhase()==EDemoClosePhase::Charging&&FVector::Dist2D(Shooter->GetActorLocation(),Player->GetActorLocation())<280&&!bDiveDashSent)
            {
                if (!Check(Player->GetDemoASC()->TryActivateAbilityByClass(UDemoDashAbility::StaticClass()),TEXT("real dash activates against charger"))) return;
                Player->GetCharacterMovement()->StopMovementImmediately(); bDiveDashSent=true; // 留在碰撞线上，只验证窗口免伤，不依赖冲刺位移。
            }
            if (Close->GetPhase()!=EDemoClosePhase::Recovery) return;
            const float Expected=CloseBeforeHealth-(CloseScenario==0?10.f:CloseScenario==3?15.f:0.f); // 其他组都应落空/受阻/成功躲避。
            if (!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),Expected,.01f),TEXT("one correctly scaled hit or successful dodge"))) return;
            if (CloseScenario>=3&&!Check(Close->GetLockedDirection().Equals(CloseLockedDirection,.001f),TEXT("charge direction never tracks moving player"))) return;
            if (CloseScenario==5&&!Check(Shooter->GetActorLocation().X<Center.X-340,TEXT("swept charge stops before wall"))) return;
            if (CloseScenario==7&&!Check(bDiveDashSent,TEXT("dash test actually committed skill"))) return;
        }
        BossStart=Shooter.IsValid()?Shooter->GetActorLocation():FVector::ZeroVector;
        Advance(.3f); return;
    }
    if (Step==5)
    {
        if (Shooter.IsValid())
        {
            if (!Check(Shooter->GetActorLocation().Equals(BossStart,1),TEXT("recovery freeze or cancellation prevents immediate movement"))) return;
            Shooter->Destroy();
        }
        if (Wall.IsValid()) { Wall->Destroy(); Wall.Reset(); }
        if (++CloseScenario<=8) { Step=1; NextTime=GetWorld()->GetTimeSeconds()+.2f; return; }
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_CLOSE_COMBAT_SUCCESS")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

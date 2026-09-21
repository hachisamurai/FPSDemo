#include "Tests/DemoEnemyAttackTest.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyTactics.h"
#include "AI/DemoEnemyNavigation.h"
#include "AI/DemoEnemyProjectile.h"
#include "Characters/DemoCharacter.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/DemoPlayerController.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"

void ADemoEnemyAttackTest::TickTacticsTest()
{
    DEMO_LOG_TICK();
    if (bFailed || GetWorld()->GetTimeSeconds()<NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds()<65.f,TEXT("bounded tactics runtime"))) return;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前World拥有的流程，测试不旅行。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 阶段真值，用于清理后验证。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 单人测试控制器。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 本帧玩家，不在组件内保存裸指针。
    if (!Mode || !State || !Player) { Check(false,TEXT("tactics dependencies")); return; }
    const FVector Center=Mode->GetAreaCenter(0); // 真实导航区域中心。
    if (Step==0)
    {
        if (!Check(Mode->StartRun(),TEXT("start isolated tactics campaign"))) return;
        Mode->StartNextLevel(); PC->OnRunReady();
        int32 Roles[6]={0,0,0,0,0,0}; // 追加近战/冲刺角色后扩容，避免真实生产混编越界写入。
        for (TActorIterator<ADemoEnemy> It(GetWorld()); It; ++It) // 冻结真实波次，保留GM注册表避免专项清场。
        {
            ++Roles[static_cast<int32>(It->FindComponentByClass<UDemoEnemyTactics>()->GetRole())];
            It->SetActorTickEnabled(false); It->SetActorEnableCollision(false);
        }
        if (!Check(Roles[4]>0 && Roles[5]>0 && Roles[1]==0 && Roles[2]==0 && Roles[3]==0,TEXT("production first level only spawns melee and charger"))) return; // 后续测试直接生成旧战术角色，战役池不再包括追击/侧翼。
        FDemoEnemyTacticsSettings Invalid; // 只修改测试值，不污染表；乱序范围必须拒绝。
        Invalid.MinimumRange=Invalid.MaximumRange;
        if (!Check(!Invalid.IsValid(),TEXT("invalid tactical distance ordering rejected"))) return;
        Player->SetActorLocation(Center+FVector(0,0,100)); Player->GetCharacterMovement()->StopMovementImmediately();
        FDemoEnemySpawnStats Stats; // 实际战术敌人，仅伤害归零防自动化等待时杀死玩家。
        Stats.Health=100; Stats.AttackPower=0; Stats.bUseTactics=true; Stats.Tactics.Role=EDemoEnemyRole::Ranged;
        Stats.FormationSlot=1; Stats.MoveSpeed=300; Stats.Tactics.RepositionInterval=6;
        Shooter=GetWorld()->SpawnActor<ADemoEnemy>(Center+FVector(-350,0,110),FRotator::ZeroRotator);
        Shooter->Configure(Stats); BossStart=Shooter->GetActorLocation();
        Advance(1.2f); return;
    }
    if (Step==1)
    {
        if (!Check(FVector::Dist2D(Shooter->GetActorLocation(),Player->GetActorLocation())>500,TEXT("ranged enemy retreats through real navigation"))) return;
        Shooter->GetAbilitySystemComponent()->AddLooseGameplayTag(DemoAmmoTags::Frozen); // 隔离移动守卫，不修改正式冰冻GE持续时间。
        BossStart=Shooter->GetActorLocation(); Advance(.5f); return;
    }
    if (Step==2)
    {
        if (!Check(Shooter->GetActorLocation().Equals(BossStart,1.f),TEXT("frozen prevents tactical retreat movement"))) return;
        Shooter->GetAbilitySystemComponent()->RemoveLooseGameplayTag(DemoAmmoTags::Frozen);
        Shooter->Destroy();
        FDemoEnemySpawnStats Stats; // 侧翼使用实际寻路，初始距离700cm可见，不用瞬移完成目标。
        Stats.Health=100; Stats.AttackPower=0; Stats.bUseTactics=true; Stats.Tactics.Role=EDemoEnemyRole::Flanker;
        Stats.MoveSpeed=300; Stats.FormationSlot=2; Stats.Tactics.RepositionInterval=6;
        Shooter=GetWorld()->SpawnActor<ADemoEnemy>(Center+FVector(-700,0,110),FRotator::ZeroRotator);
        Shooter->Configure(Stats); BossStart=Shooter->GetActorLocation(); Advance(1.5f); return;
    }
    if (Step==3)
    {
        if (!Check(FMath::Abs(Shooter->GetActorLocation().Y-BossStart.Y)>100,TEXT("flanker moves laterally rather than joining straight chase"))) return;
        Shooter->SetActorTickEnabled(false);
        Player->GetCharacterMovement()->Velocity=FVector(0,600,0); // 发射瞬间水平速度，下一帧清除，验证实际弹体方向。
        const FVector Aim=Shooter->FindComponentByClass<UDemoEnemyTactics>()->AimPoint(Player,1000); // 本帧预测结果，只读接口。
        if (!Check(Aim.Y>Player->GetActorLocation().Y && FVector::Dist(Aim,Player->GetActorLocation())<=180.1f,TEXT("prediction leads movement with bounded 180cm offset"))) return;
        Player->GetCharacterMovement()->StopMovementImmediately(); // 限定为瞄准快照，避免等待期间玩家位移干扰下一步视线。
        // 首发错峰要等待满2.34秒；不绕过生产冷却。
        Advance(1.f); return;
    }
    if (Step==4)
    {
        Player->GetCharacterMovement()->Velocity=FVector(0,600,0); // 实际发射入口读取的速度，不人工指定弹体方向。
        const FVector ExpectedDirection=(Shooter->FindComponentByClass<UDemoEnemyTactics>()->AimPoint(Player,1000)-Shooter->GetActorLocation()).GetSafeNormal(); // 同帧期望弹道，随后检查生成物。
        if (!Check(Shooter->TryFireProjectile(Player),TEXT("tactical projectile respects opening delay then fires"))) return;
        bool bFoundPredictedProjectile=false; // 仅接受当前射手生成的真实ProjectileMovement速度。
        for (TActorIterator<ADemoEnemyProjectile> It(GetWorld()); It; ++It)
            if (It->GetOwner()==Shooter.Get()) bFoundPredictedProjectile |= FVector::DotProduct(It->FindComponentByClass<UProjectileMovementComponent>()->Velocity.GetSafeNormal(),ExpectedDirection)>.999f;
        if (!Check(bFoundPredictedProjectile,TEXT("actual spawned projectile uses predicted direction"))) return;
        Player->GetCharacterMovement()->StopMovementImmediately();
        Shooter->Destroy();
        FDemoEnemySpawnStats Stats; // Boss二阶段测试只缩短首次等待，保留固定2秒前摇。
        Stats.Health=100; Stats.AttackPower=0; Stats.bBoss=true; Stats.bUseTactics=true;
        Stats.Tactics.Role=EDemoEnemyRole::Ranged; Stats.Attack.GlobalInterval=3; Stats.Attack.ProjectileInterval=60;
        Boss=GetWorld()->SpawnActor<ADemoEnemy>(Center+FVector(-900,0,130),FRotator::ZeroRotator);
        Boss->Configure(Stats); Boss->SetActorTickEnabled(false);
        DemoEffects::Apply(Player->GetDemoASC(),Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-51);
        if (!Check(Boss->FindComponentByClass<UDemoEnemyTactics>()->IsEnraged(),TEXT("actual GAS damage triggers boss phase two"))) return;
        if (!Check(FMath::IsNearlyEqual(Boss->FindComponentByClass<UDemoEnemyTactics>()->AttackInterval(10,3),7.5f),TEXT("phase two shortens future intervals"))) return;
        DemoEffects::Apply(Player->GetDemoASC(),Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),20);
        if (!Check(Boss->FindComponentByClass<UDemoEnemyTactics>()->IsEnraged(),TEXT("healing cannot oscillate boss phase"))) return;
        Advance(3.1f); return;
    }
    if (Step==5)
    {
        if (!Check(Boss->TryStartGlobalAttack(),TEXT("phase two global attack starts normally"))) return;
        Boss->SetActorTickEnabled(true); BossStart=Boss->GetActorLocation(); Advance(1.f); return;
    }
    if (Step==6)
    {
        if (!Check(Boss->IsGlobalAttackWindingUp() && Boss->GetActorLocation().Equals(BossStart,1),TEXT("enraged boss remains stationary during fixed windup"))) return;
        Advance(1.1f); return;
    }
    if (Step==7)
    {
        if (!Check(!Boss->IsGlobalAttackWindingUp(),TEXT("phase two still resolves global at two seconds"))) return;
        State->Phase=EDemoPhase::Hub; BossStart=Boss->GetActorLocation(); Advance(.4f); return;
    }
    if (Step==8)
    {
        if (!Check(Boss->GetActorLocation().Equals(BossStart,1),TEXT("noncombat prevents tactics after phase two"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_ENEMY_TACTICS_SUCCESS"));
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

// 职责：Boss俯冲真实World回归；使用-DemoEnemyAttackTest -DemoBossDiveTest运行隔离夹具。
// 依赖第十关生产模板、BossDive GA/GAS、NavMesh与Sweep；不再假定旧第五关包含Boss，避免配置调整导致测试在开场失败。
#include "Tests/DemoEnemyAttackTest.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoBossDiveVFX.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "GAS/Abilities/DemoPlayerAbilityTypes.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Debug/DemoLog.h"

void ADemoEnemyAttackTest::TickDiveTest()
{
    DEMO_LOG_TICK(); if (bFailed||GetWorld()->GetTimeSeconds()<NextTime) return;
    if (GetWorld()->GetTimeSeconds()>150) { Check(false,TEXT("bounded dive runtime")); return; }
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前权威World。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 真正阶段约束，不伪造GA回调。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 真实本地玩家输入上下文。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 单帧借用玩家。
    if (!Mode||!State||!Player) { Check(false,TEXT("dive dependencies")); return; }
    const FVector Center=Mode->GetAreaCenter(0); // 实际白模导航中心。
    UDemoBossDiveAbility* Dive=Boss.IsValid()?Boss->GetDiveAbility():nullptr; // Boss ASC拥有的真实GA实例。
    if (Step==0)
    {
        if (!Check(Mode->StartRun(),TEXT("dive isolated run"))) return;
        Mode->StartNextLevel(); PC->OnRunReady();
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 保留战役注册怪避免误触发清场。
        FDemoEnemySpawnStats Production; // 当前十关战役最终关Boss的生产配置，不借用已移除的第五关Boss引用。
        FString Error; // 本次解析失败原因，由生产接口输出日志；不缓存跨关卡状态。
        if (!Check(Mode->GetSpawnStats(DemoCombatConfig::LevelCount,true,Production,Error)&&Production.bUseDive,TEXT("production final-level boss table enables dive"))) return;
        FDemoBossDiveSettings Invalid; Invalid.Height=-1; // 仅测试副本。
        if (!Check(!Invalid.IsValid(),TEXT("negative rise height rejected"))) return;
        Advance(.1f); return;
    }
    if (Step==1)
    {
        State->Phase=EDemoPhase::Combat;
        Player->SetActorLocation(Center+FVector(0,0,100)); Player->GetCharacterMovement()->StopMovementImmediately();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        DemoEffects::Apply(Player->GetDemoASC(),Player->GetDemoASC(),UDemoHealthEffect::StaticClass(),1000);
        if (DiveScenario==7) DemoEffects::Apply(Player->GetDemoASC(),Player->GetDemoASC(),UDemoHealthEffect::StaticClass(),-60); // 40HP玩家验证落地GE触发死亡时的取消重入。
        FDemoEnemySpawnStats Stats; // 足够HP供反复伤害探针，攻击0便于隔离固定50俯冲伤害。
        Stats.Health=1000; Stats.AttackPower=0; Stats.bBoss=true; Stats.bUseDive=true; Stats.DiveSlam.InitialDelay=3;
        Stats.Attack.GlobalInterval=120; Stats.Attack.ProjectileInterval=60;
        Boss=GetWorld()->SpawnActor<ADemoEnemy>(Center+FVector(-600,0,130),FRotator::ZeroRotator);
        if (!Check(Boss.IsValid(),TEXT("spawn dive test boss"))) return;
        Boss->Configure(Stats); Boss->SetActorTickEnabled(false); BossStart=Boss->GetActorLocation(); bDiveDashSent=false;
        // 先施加旧DOT，验证后来进入无敌时不重置持续时间、不继续扣血。
        if (DiveScenario==0)
        {
            FGameplayEffectSpecHandle Burn=Player->GetDemoASC()->MakeOutgoingSpec(UDemoBurnEffect::StaticClass(),1,Player->GetDemoASC()->MakeEffectContext()); // 测试真实周期GE。
            Burn.Data->SetDuration(20,true); Burn.Data->Period=.2f; Burn.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-1);
            Player->GetDemoASC()->ApplyGameplayEffectSpecToTarget(*Burn.Data.Get(),Boss->GetAbilitySystemComponent());
        }
        if (!Check(!Boss->TryStartDiveAttack(),TEXT("initial dive cooldown respected"))) return;
        Advance(3.1f); return;
    }
    if (Step==2)
    {
        if (!Check(Boss->TryStartDiveAttack(),TEXT("real GAS dive activation"))) return;
        Boss->SetActorTickEnabled(true);
        const float Before=Boss->GetHealth(); // 升空期间不是无敌。
        DemoEffects::Apply(Player->GetDemoASC(),Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10);
        if (!Check(FMath::IsNearlyEqual(Boss->GetHealth(),Before-10),TEXT("rising boss remains damageable"))) return;
        if (DiveScenario==5)
        {
            DemoEffects::Apply(Player->GetDemoASC(),Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-2000);
            if (!Check(!Boss->GetDiveAbility()->IsActive()&&!Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoBossTags::Invulnerable),TEXT("death cancels active dive without leftover immunity"))) return;
            Step=7; NextTime=GetWorld()->GetTimeSeconds()+.2f; return;
        }
        Advance(.65f); return;
    }
    if (Step==3)
    {
        if (!Check(Dive&&Dive->GetPhase()==EDemoDivePhase::Hovering,TEXT("arrives at three-second hover"))) return;
        DiveHoverTime=GetWorld()->GetTimeSeconds(); DiveHealth=Boss->GetHealth();
        if (!Check(Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoBossTags::Invulnerable),TEXT("hover owns GAS invulnerability"))) return;
        if (!Check(!Boss->TryStartGlobalAttack()&&!Boss->TryFireProjectile(Player),TEXT("hover blocks other boss attacks"))) return;
        DemoEffects::Apply(Player->GetDemoASC(),Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-500);
        if (!Check(FMath::IsNearlyEqual(Boss->GetHealth(),DiveHealth),TEXT("direct damage rejected before health change"))) return;
        FGameplayEffectSpecHandle Chill=Player->GetDemoASC()->MakeOutgoingSpec(UDemoChillEffect::StaticClass(),1,Player->GetDemoASC()->MakeEffectContext()); // 绕过弹药组件直接施加，GE也必须拒绝。
        if (!Check(!Player->GetDemoASC()->ApplyGameplayEffectSpecToTarget(*Chill.Data.Get(),Boss->GetAbilitySystemComponent()).IsValid(),TEXT("invulnerability rejects new frost status"))) return;
        FGameplayEffectSpecHandle Burn=Player->GetDemoASC()->MakeOutgoingSpec(UDemoBurnEffect::StaticClass(),1,Player->GetDemoASC()->MakeEffectContext()); // 新灼烧栈不得延长已有DOT。
        Burn.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-10);
        if (!Check(!Player->GetDemoASC()->ApplyGameplayEffectSpecToTarget(*Burn.Data.Get(),Boss->GetAbilitySystemComponent()).IsValid(),TEXT("invulnerability rejects new burn stacks"))) return;
        if (DiveScenario==0&&FParse::Param(FCommandLine::Get(),TEXT("DemoBossDiveCapture")))
        {
            PC->SetControlRotation((Boss->GetActorLocation()-Player->GetPawnViewLocation()).Rotation());
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/BossDive/Hover.png"),false,false);
        }
        Advance(1.2f); return;
    }
    if (Step==4)
    {
        if (!Check(Dive->GetPhase()==EDemoDivePhase::Hovering&&FMath::IsNearlyEqual(Boss->GetHealth(),DiveHealth),TEXT("existing periodic damage blocked throughout hover"))) return;
        if (DiveScenario==0&&FParse::Param(FCommandLine::Get(),TEXT("DemoBossDiveCapture")))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/BossDive/Hover.png"),false,false); // 镜头已稳定一秒，实际RHI画面用于人工验收。
        if (DiveScenario==3||DiveScenario==4)
        {
            if (DiveScenario==3) Boss->CancelPendingAttacks(); // 主动GAS取消。
            else { State->Phase=EDemoPhase::Hub; Boss->Tick(.01f); } // 真实Enemy阶段守卫撤销。
            if (!Check(!Dive->IsActive()&&!Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoBossTags::Invulnerable)
                &&Boss->GetActorLocation().Equals(BossStart,1),TEXT("cancel/phase exit clears immunity and returns along safe ascent route"))) return;
            Step=7; NextTime=GetWorld()->GetTimeSeconds()+.1f; return;
        }
        Advance(.01f); return;
    }
    if (Step==5)
    {
        if (Dive->GetPhase()==EDemoDivePhase::Hovering) return;
        if (!Check(Dive->GetPhase()==EDemoDivePhase::Diving&&GetWorld()->GetTimeSeconds()-DiveHoverTime>=2.9f,TEXT("full hover lasts three seconds before locked dive"))) return;
        if (!Check(!Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoBossTags::Invulnerable),TEXT("dive immediately removes immunity"))) return;
        DiveHealth=Player->GetDemoAttributes()->GetHealth();
        const FVector Ground=Dive->GetLandingPoint()-FVector(0,0,130); // 实际合法落点，测试不假设投影精确等于玩家初始坐标。
        Player->SetActorLocation(Ground+FVector(DiveScenario==2?700.f:0.f,0,100)); // 命中/冲刺组在中心，出圈组移到圈外。
        Player->GetCharacterMovement()->StopMovementImmediately();
        PC->SetControlRotation(FRotator(0,0,0));
        if (DiveScenario==2) PC->SetControlRotation((Ground-Player->GetPawnViewLocation()).Rotation()); // 圈外观察真实预警圈，下一帧采集。
        if (DiveScenario==6)
        {
            AStaticMeshActor* Obstacle=GetWorld()->SpawnActor<AStaticMeshActor>(); // 在锁定后插入动态障碍，必须取消而非穿墙。
            Wall=Obstacle; Obstacle->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
            Obstacle->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
            Obstacle->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Obstacle->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
            Obstacle->SetActorScale3D(FVector(2,5,2)); Obstacle->SetActorLocation(FMath::Lerp(Boss->GetActorLocation(),Dive->GetLandingPoint(),.5f));
        }
        Advance(.01f); return;
    }
    if (Step==6)
    {
        if (DiveScenario==2&&!bDiveDashSent&&FParse::Param(FCommandLine::Get(),TEXT("DemoBossDiveCapture")))
        {
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/BossDive/LandingTarget.png"),false,false);
            bDiveDashSent=true; // 此圈外场景复用一次性采集标志，不会调用冲刺。
        }
        if (DiveScenario==1&&Dive->GetPhase()==EDemoDivePhase::Diving&&!bDiveDashSent&&FVector::Dist(Boss->GetActorLocation(),Dive->GetLandingPoint())<250)
        {
            if (!Check(Player->GetDemoASC()->TryActivateAbilityByClass(UDemoDashAbility::StaticClass()),TEXT("actual dash GA activates during dive"))) return;
            // 保留真实0.45秒GE，但阻止冲刺位移，单独证明圈内窗口免伤而不是出圈导致的假阳性。
            Player->GetCharacterMovement()->StopMovementImmediately(); Player->SetActorLocation(Dive->GetLandingPoint()-FVector(0,0,30)); bDiveDashSent=true;
        }
        if (DiveScenario==7)
        {
            // 致命伤害同步清成长，RefillPlayer还会补血/清阵亡标记；以真实Defeat流转证明40HP玩家被50伤害击败。
            if (State->Phase!=EDemoPhase::Defeat) return;
            if (!Check(!Dive->IsActive()&&!Boss->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoBossTags::Invulnerable),TEXT("lethal impact safely cancels ability during health delegate"))) return;
        }
        else if (DiveScenario==6)
        {
            if (Dive->IsActive()) return;
            if (!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),DiveHealth)&&Boss->GetActorLocation().Equals(BossStart,2),TEXT("dynamic obstruction cancels without damage or wall penetration"))) return;
        }
        else
        {
            if (Dive->GetPhase()!=EDemoDivePhase::Recovery) return;
            if (DiveScenario==0)
            {
                if (!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),DiveHealth-50),TEXT("impact deals exactly 50 independent of zero AttackPower"))) return;
                if (!Check(Player->GetCharacterMovement()->Velocity.Size()>500||Player->GetCharacterMovement()->PendingLaunchVelocity.Size()>500,TEXT("successful impact launches player"))) return;
            }
            else if (!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),DiveHealth)&&(DiveScenario!=1||bDiveDashSent),TEXT("dash window or outside radius avoids both damage and heal"))) return;
        }
        Advance(1.2f); return;
    }
    if (Step==7)
    {
        if (Boss.IsValid())
        {
            Boss->SetActorTickEnabled(false);
            if (!Check(!Boss->GetDiveAbility()->IsActive()&&!Boss->TryStartDiveAttack(),TEXT("skill ends and cooldown prevents immediate reuse"))) return;
            Boss->Destroy();
        }
        if (Wall.IsValid()) { Wall->Destroy(); Wall.Reset(); }
        for (TActorIterator<ADemoBossDiveVFX> It(GetWorld());It;++It) if (!Check(false,TEXT("skill VFX must be destroyed on end"))) return; // 生命周期回归，不接受残留隐藏Actor。
        if (++DiveScenario<=7) { Step=1; NextTime=GetWorld()->GetTimeSeconds()+.2f; return; }
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_BOSS_DIVE_SUCCESS")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0);
    }
}

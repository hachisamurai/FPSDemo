// 职责：扩展兵种/双Boss真实World回归；使用-DemoEnemyAttackTest -DemoEnemyRosterTest，依赖生产DT、GE、碰撞和主Spawner。
#include "Tests/DemoEnemyAttackTest.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyRoster.h"
#include "AI/DemoRosterHazard.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "GAS/DemoEnemyDefenseSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h" // 夹具必须确认真正进入Combat，出发可能等待房间/地面确认。
#include "Progression/Builds/DemoRunBuildComponent.h" // 新战役出发门禁要求先选本轮核心，测试经权威接口选择。
#include "Characters/DemoCharacter.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

namespace
{
    /** World/Owner为隔离测试宿主，Name为真实DT行，Location为世界cm；默认关闭AI Tick供精确时序断言。 */
    ADemoEnemy* SpawnRosterFixture(UWorld* World,AActor* Owner,FName Name,const FVector& Location)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs row=%s"),__FUNCTION__,*Name.ToString());
        UDataTable* Table=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Enemies.DT_Enemies")); // Editor/Cook真实表，不伪造运行配置。
        const FDemoEnemyRow* Row=Table?Table->FindRow<FDemoEnemyRow>(Name,TEXT("RosterFixture"),false):nullptr; // 同步借用。
        if (!Row) return nullptr;
        FDemoDifficultyRow Difficulty; FDemoLevelRow Level; // lvl1 normal隔离倍率；Boss生产lvl10数值另作断言。
        const FDemoEnemySpawnStats Stats=DemoCombatConfig::Resolve(*Row,Difficulty,Level); // 正式解析入口。
        FActorSpawnParameters Spawn; Spawn.Owner=Owner; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 隔离空中不会干扰原关卡。
        ADemoEnemy* Enemy=World->SpawnActor<ADemoEnemy>(Location,FRotator::ZeroRotator,Spawn); // World拥有对象，返回借用。
        if (Enemy) { Enemy->Configure(Stats); Enemy->SetActorTickEnabled(false); } return Enemy;
    }
    /** Player为真实原射手，Enemy为受测盾，Amount为正伤害；使用带HitResult的正常Health GE验证预结算。 */
    void ApplyDirectional(ADemoCharacter* Player,ADemoEnemy* Enemy,float Amount)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        FGameplayEffectContextHandle Context=Player->GetDemoASC()->MakeEffectContext(); // 与实际武器相同来源ASC和HitResult协议。
        FHitResult Hit; Hit.ImpactPoint=Enemy->GetActorLocation(); Context.AddHitResult(Hit,true); // 不伪造护盾结果，只提供命中方向上下文。
        FGameplayEffectSpecHandle Spec=Player->GetDemoASC()->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(),1,Context); // 正常GAS执行。
        Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-Amount); Player->GetDemoASC()->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(),Enemy->GetAbilitySystemComponent());
    }
}
void ADemoEnemyAttackTest::TickRosterTest()
{
    DEMO_LOG_TICK(); if (bFailed||GetWorld()->GetTimeSeconds()<NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds()<160.f,TEXT("roster bounded runtime"))) return;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 当前服务端场景。
    ADemoCharacter* Player=Cast<ADemoCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); // 已登记的本地参战者。
    if (!Check(Mode&&Player,TEXT("roster dependencies"))) return;
    UDemoAbilitySystemComponent* ASC=Player->GetDemoASC(); // 玩家PS拥有ASC，测试同步借用。
    UDemoEnemyRoster* Roster=Shooter.IsValid()?Shooter->FindComponentByClass<UDemoEnemyRoster>():nullptr; // 当前受测角色。
    UDemoEnemyRoster* Other=Boss.IsValid()?Boss->FindComponentByClass<UDemoEnemyRoster>():nullptr; // 修复者或Boss。
    switch (Step)
    {
    case 0:
    {
        if (!Check(Mode->StartRun(),TEXT("roster isolated run"))) return;
        // 2026-11-08起出发门禁要求本轮已选核心（否则TEAM_DEPARTURE_REJECT，永远停在Hub，夹具敌人会话无效）。
        // 只对本次Editor隔离PS经权威API选B01，不跳过生产权限、不写持久档。
        UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Player); // 当前Pawn的PS组件，同步借用。
        if (!Check(Build&&(Build->HasSelectedCore()||Build->TrySelectCore(TEXT("B01"))),TEXT("roster fixture selects real core"))) return;
        Mode->StartNextLevel(); Step=150; NextTime=GetWorld()->GetTimeSeconds()+.1f; break; // 正式入口可能异步等待房间；下一步轮询Combat。
    }
    case 150:
    {
        const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 权威阶段，不伪造Combat枚举。
        if (!Check(State!=nullptr,TEXT("roster authoritative game state"))) return;
        if (State->Phase!=EDemoPhase::Combat)
        {
            UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_FIXTURE_WAIT_COMBAT phase=%d reason=%s"),int32(State->Phase),*State->FailureReason);
            if (!Check(GetWorld()->GetTimeSeconds()<30.f,TEXT("roster run reaches combat"))) return;
            NextTime=GetWorld()->GetTimeSeconds()+.2f; break;
        }
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) { It->SetActorTickEnabled(false); It->SetActorEnableCollision(false); } // 保留战役注册避免误清关。
        Player->SetActorLocation(Mode->GetAreaCenter(0)+FVector(0,0,2500)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately();
        ASC->SetNumericAttributeBase(UDemoAttributeSet::GetMaxHealthAttribute(),1000); ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),1000); // 非致死探针，避免故意死亡重置经济。
        UDataTable* Enemies=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Enemies.DT_Enemies")); // 生产资产。
        UDataTable* Levels=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Levels.DT_Levels"));
        UDataTable* Difficulties=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Difficulties.DT_Difficulties"));
        FString Error; // 生产校验原因，不省略新字段错误。
        if (!Check(DemoCombatConfig::Validate(Enemies,Difficulties,Levels,Error),TEXT("complete roster tables validate"))) return;
        const FDemoLevelRow* Final=Levels->FindRow<FDemoLevelRow>(TEXT("Level10"),TEXT("Test")); // lvl10生产倍率3.6。
        const FDemoDifficultyRow* Normal=Difficulties->FindRow<FDemoDifficultyRow>(TEXT("Normal"),TEXT("Test"));
        const FDemoEnemySpawnStats Warden=DemoCombatConfig::Resolve(*Enemies->FindRow<FDemoEnemyRow>(TEXT("Warden"),TEXT("Test")),*Normal,*Final); // 最终7200。
        const FDemoEnemySpawnStats Weaver=DemoCombatConfig::Resolve(*Enemies->FindRow<FDemoEnemyRow>(TEXT("Weaver"),TEXT("Test")),*Normal,*Final); // 最终9000。
        if (!Check(Warden.Health==7200&&Weaver.Health==9000,TEXT("normal final bosses 7200 and 9000 HP"))) return;
        for (int32 Number=1;Number<=10;++Number) // 所有关卡实际计划，特殊槽不额外扩总量。
        {
            const FDemoLevelRow* Level=Levels->FindRow<FDemoLevelRow>(DemoCombatConfig::LevelName(Number),TEXT("Test")); // 本次只读借用。
            FDemoSpawnPlan Plan,Again; FDemoSpawnSettings Settings; // 两次解析比较稳定抽样，独立对象无共享随机状态。
            if (!Check(DemoSpawnPlan::Build(*Level,*Normal,Enemies,Settings,TEXT("RosterStable"),false,30,Plan,Error)
                &&DemoSpawnPlan::Build(*Level,*Normal,Enemies,Settings,TEXT("RosterStable"),false,30,Again,Error),TEXT("level builds complete plan"))) return;
            if (!Check(Plan.GetTotalCount()==Level->EnemyCount+(Number==10?1:0)&&Plan.Boss.Roster.Kind==Again.Boss.Roster.Kind,TEXT("replacement counts and deterministic boss"))) return;
            TMap<EDemoRosterKind,int32> Counts; // 实际SelectGroup结果，不能只检查源数组。
            for (int32 Ordinal=0;Ordinal<Plan.GetTotalCount();++Ordinal) { int32 Slot=0; /* 此次输出阵型索引。 */ ++Counts.FindOrAdd(Plan.Groups[Plan.SelectGroup(Ordinal,Slot)].Stats.Roster.Kind); }
            if (!Check(Counts.FindRef(EDemoRosterKind::Guard)<=2&&Counts.FindRef(EDemoRosterKind::Bombardier)<=2&&Counts.FindRef(EDemoRosterKind::Mender)<=1&&Counts.FindRef(EDemoRosterKind::Exploder)<=2
                &&Counts.FindRef(EDemoRosterKind::Beacon)<=2,TEXT("special roster caps respected"))) return;
            const int32 ExpectedBeacons=(Number==2||Number==5||Number==7||Number==9)?1:0; // 2026-11-17生产编排：第2/5/7/9关各一只封锁眼，第10关不放。
            if (!Check(Counts.FindRef(EDemoRosterKind::Beacon)==ExpectedBeacons,TEXT("beacon placed only in levels 2/5/7/9"))) return;
        }
        Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Guard"),Player->GetActorLocation()+FVector(500,0,0));
        if (!Check(Shooter.IsValid(),TEXT("guard fixture"))) return;
        Shooter->FindComponentByClass<UDemoEnemyRoster>()->Update(Player,0); // 截图前明确护盾正面方向，不依赖关闭的AI Tick。
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoRosterCapture"))) { Step=100; NextTime=GetWorld()->GetTimeSeconds()+2.f; break; } // 等待材质/TSR历史稳定后截图，不把首帧拖影当外观。
        Step=1; NextTime=GetWorld()->GetTimeSeconds()+.2f; break; // 本分支从150进入，不能用Advance的++Step。
    }
    case 100:
        FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/RosterGuard.png"),false,false);
        Step=1; NextTime=GetWorld()->GetTimeSeconds()+.2f; break;
    case 1:
        Roster->Update(Player,0); ApplyDirectional(Player,Shooter.Get(),100);
        if (!Check(FMath::IsNearlyEqual(Shooter->GetHealth(),205.f)&&Shooter->GetAbilitySystemComponent()->GetNumericAttribute(UDemoEnemyDefenseSet::GetShieldAttribute())==115,TEXT("front shield absorbs 65 percent through GE"))) return;
        BossStart=Player->GetActorLocation(); Player->SetActorLocation(Shooter->GetActorLocation()+FVector(500,0,0)); ApplyDirectional(Player,Shooter.Get(),100);
        if (!Check(Shooter->GetHealth()==105,TEXT("rear damage bypasses shield"))) return;
        Player->SetActorLocation(BossStart); ApplyDirectional(Player,Shooter.Get(),200);
        if (!Check(Shooter->GetHealth()==20&&Roster->IsRecovering()&&Shooter->GetAbilitySystemComponent()->GetNumericAttribute(UDemoEnemyDefenseSet::GetShieldAttribute())==0,TEXT("finite shield breaks with overflow and stagger"))) return;
        Boss=SpawnRosterFixture(GetWorld(),this,TEXT("Mender"),Shooter->GetActorLocation()+FVector(0,350,0)); Advance(2.2f); break;
    case 2:
        Other->Update(Player,0); if (!Check(Other->IsCasting(),TEXT("mender channels wounded minion"))) return;
        DemoEffects::Apply(ASC,Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-18);
        if (!Check(!Other->IsCasting(),TEXT("15 percent channel damage interrupts"))) return; Advance(1.6f); break;
    case 3:
        Other->Update(Player,0); if (!Check(Shooter->GetHealth()==20&&Roster->GetHealCount()==0,TEXT("interrupted heal never resolves late"))) return;
        CloseScenario=0; Advance(3.6f); break;
    case 4:
        Other->Update(Player,0); if (!Check(Other->IsCasting(),TEXT("mender reattempt after cooldown"))) return; Advance(1.6f); break;
    case 5:
        Other->Update(Player,0); ++CloseScenario;
        if (!Check(Roster->GetHealCount()==CloseScenario&&FMath::IsNearlyEqual(Shooter->GetHealth(),20.f+19.2f*CloseScenario,.02f),TEXT("heal is 8 percent max and counted once"))) return;
        if (CloseScenario<3) { Step=4; NextTime=GetWorld()->GetTimeSeconds()+5.1f; } else Advance(5.1f); break;
    case 6:
        Other->Update(Player,0); if (!Check(!Other->IsCasting()&&Roster->GetHealCount()==3,TEXT("fourth repair rejected"))) return;
        Shooter->Destroy(); Boss->Destroy();
        Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Bombardier"),Player->GetActorLocation()+FVector(800,0,0)); CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Advance(2.2f); break;
    case 7:
        Roster->Update(Player,0); if (!Check(Roster->IsCasting(),TEXT("bombardier starts three-circle warning"))) return; Advance(.8f); break;
    case 8:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth,TEXT("mortar warning does not damage"))) return; Advance(.5f); break;
    case 9:
        Roster->Update(Player,0); Advance(1.05f); break;
    case 10:
        if (!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),CloseBeforeHealth-16.f),TEXT("real arcing mortar damages once per salvo"))) return;
        Shooter->Destroy(); Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Exploder"),Player->GetActorLocation()+FVector(180,0,0)); CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Advance(2.2f); break;
    case 11:
        Roster->Update(Player,0); if (!Check(Roster->IsCasting(),TEXT("exploder starts stopped one-second warning"))) return;
        DemoEffects::Apply(ASC,Shooter->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-100); Advance(1.2f); break;
    case 12:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth,TEXT("killed exploder has no death explosion"))) return;
        Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Exploder"),Player->GetActorLocation()+FVector(180,0,0)); Advance(2.2f); break;
    case 13:
        Roster->Update(Player,0); Advance(1.1f); break;
    case 14:
        Roster->Update(Player,0);
        if (!Check(!Shooter->IsAlive()&&Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth-28,TEXT("surviving exploder deals 28 once then consumes itself"))) return;
        // 封锁眼专项（40..47）：锁定读条→预警无伤→冻结线段命中一次→横移躲开→击杀取消；完成后回到原裁决者步骤15。
        Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Beacon"),Player->GetActorLocation()+FVector(800,0,0));
        if (!Check(Shooter.IsValid()&&Shooter->FindComponentByClass<UDemoEnemyRoster>()->GetKind()==EDemoRosterKind::Beacon,TEXT("beacon fixture from production row"))) return;
        CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Step=40; NextTime=GetWorld()->GetTimeSeconds()+2.2f; break; // 首发至少等待2秒。
    case 40:
        Roster->Update(Player,0); if (!Check(Roster->IsCasting(),TEXT("beacon locks visible player and starts 1.5 second beam warning"))) return; Advance(.8f); break;
    case 41:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth,TEXT("beacon beam warning does not damage"))) return; Advance(.9f); break;
    case 42:
        Roster->Update(Player,0); Advance(.3f); break; // 读条到期由仲裁消费；光束危险物在自身预警结束时结算。
    case 43:
        if (!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),CloseBeforeHealth-18.f)&&!Roster->IsCasting(),TEXT("beacon beam hits once along locked line"))) return;
        Shooter->Destroy(); Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Beacon"),Player->GetActorLocation()+FVector(800,0,0));
        CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Step=44; NextTime=GetWorld()->GetTimeSeconds()+2.2f; break;
    case 44:
        Roster->Update(Player,0); if (!Check(Roster->IsCasting(),TEXT("second beacon starts lock"))) return;
        BossStart=Player->GetActorLocation(); Player->SetActorLocation(BossStart+FVector(0,400,0)); Player->GetCharacterMovement()->StopMovementImmediately(); // 横移4m，远超90cm半宽。
        Advance(1.7f); break;
    case 45:
        Roster->Update(Player,0);
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth,TEXT("sidestep escapes frozen beacon beam"))) return;
        Player->SetActorLocation(BossStart); Player->GetCharacterMovement()->StopMovementImmediately();
        Shooter->Destroy(); Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Beacon"),Player->GetActorLocation()+FVector(800,0,0));
        CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Step=46; NextTime=GetWorld()->GetTimeSeconds()+2.2f; break;
    case 46:
        Roster->Update(Player,0); if (!Check(Roster->IsCasting(),TEXT("third beacon starts lock"))) return;
        DemoEffects::Apply(ASC,Shooter->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-Shooter->GetMaxHealth()); // 读条中击杀。
        Advance(1.7f); break;
    case 47:
        // 1.7秒后死亡流程可能已销毁Actor，弱引用失效同样代表已死亡；不能直接解引用。
        if (!Check((!Shooter.IsValid()||!Shooter->IsAlive())&&Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth,TEXT("killed beacon cancels pending beam"))) return;
        Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Warden"),Player->GetActorLocation()+FVector(700,0,0)); Step=15; NextTime=GetWorld()->GetTimeSeconds()+8.2f; break;
    case 15:
        if (!Check(Roster->GetPhase()==1&&!Shooter->CanStartDiveAttack(),TEXT("warden phase one cannot dive even after initial cooldown"))) return;
        DemoEffects::Apply(ASC,Shooter->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-650);
        if (!Check(Roster->GetPhase()==2&&Shooter->CanStartDiveAttack(),TEXT("70 percent unlocks dive without HP gate"))) return;
        DemoEffects::Apply(ASC,Shooter->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-700);
        if (!Check(Roster->GetPhase()==3,TEXT("35 percent locks third phase"))) return;
        DemoEffects::Apply(ASC,Shooter->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),1000);
        if (!Check(Roster->GetPhase()==3,TEXT("boss healing cannot regress phase"))) return;
        Shooter->Destroy(); Advance(.1f); break;
    case 16:
    {
        // 真主Spawner测试召唤：四个名额先占满，70%只排队；击杀腾位后才允许出现。
        UDataTable* Enemies=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Enemies.DT_Enemies")); // 生产模板。
        FDemoLevelRow Level; Level.MonsterLevel=10; Level.EnemyCount=3; Level.BossRow=TEXT("Warden"); // 缩小数量但保留三类基础兵种+Boss并发约束。
        FDemoDifficultyRow Difficulty; FDemoSpawnSettings Settings; FDemoSpawnPlan Plan; FString Error; // 独立测试计划，不修改任何资产。
        if (!Check(DemoSpawnPlan::Build(Level,Difficulty,Enemies,Settings,TEXT("SupportFixture"),false,4,Plan,Error),TEXT("support production plan"))) return;
        TestSpawner=NewObject<UDemoEnemySpawnComponent>(this); TestSpawner->RegisterComponent(); TestSpawner->Settings.MinPlayerDistance=0;
        FDemoSpawnGeometry Geometry; // 原生产布局，在空中平移以隔离地形/原战役注册怪。
        if (!Check(TestSpawner->StartEncounter(Plan,FTransform(Player->GetActorLocation()+FVector(4000,0,0)),Geometry,Error),TEXT("support start encounter"))) return;
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) if (It->GetOwner()==this&&It->IsAlive()) { It->SetActorTickEnabled(false); if (It->IsBoss()) Boss=*It; }
        if (!Check(Boss.IsValid()&&TestSpawner->GetAliveCount()==4,TEXT("initial boss plus three minions"))) return;
        DemoEffects::Apply(ASC,Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-Boss->GetMaxHealth()*.31f);
        if (!Check(TestSpawner->GetAliveCount()==4&&TestSpawner->GetRemaining()==6,TEXT("summons queued without exceeding cap"))) return;
        int32 Killed=0; // 只击杀本测试两个原小怪，保留一个验证Boss死亡不误清原怪。
        for (TActorIterator<ADemoEnemy> It(GetWorld());It&&Killed<2;++It) if (It->GetOwner()==this&&It->IsAlive()&&!It->IsBoss()) { ++Killed; DemoEffects::Apply(ASC,It->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-It->GetMaxHealth()); }
        Advance(1.3f); break;
    }
    case 17:
    {
        if (!Check(TestSpawner->GetAliveCount()==4&&TestSpawner->GetRemaining()==4,TEXT("two summons fill only freed slots"))) return;
        int32 SupportCount=0; // 子单位真实零银币，而非只在UI隐藏奖励。
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) if (It->GetOwner()==this&&It->IsAlive()&&!It->IsBoss())
        { It->SetActorTickEnabled(false); if (It->FindComponentByClass<UDemoEnemyRoster>()->GetKind()!=EDemoRosterKind::None) { ++SupportCount; if (!Check(It->GetCoinReward()==0,TEXT("summon zero silver"))) return; } }
        if (!Check(SupportCount==2,TEXT("guard and bombardier summoned"))) return;
        DemoEffects::Apply(ASC,Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-Boss->GetMaxHealth());
        if (!Check(TestSpawner->GetAliveCount()==1&&TestSpawner->GetRemaining()==1,TEXT("boss death clears owned summons but keeps original minion"))) return;
        TestSpawner->CancelEncounter(true); Advance(.1f); break;
    }
    case 18:
    {
        UDataTable* Enemies=LoadObject<UDataTable>(nullptr,TEXT("/Game/Data/DT_Enemies.DT_Enemies")); // 第二Boss沿用相同正式计划/预算接口。
        FDemoLevelRow Level; Level.MonsterLevel=10; Level.EnemyCount=3; Level.BossRow=TEXT("Weaver"); // 强制本专项覆盖另一个Boss，正常游戏仍稳定抽选。
        FDemoDifficultyRow Difficulty; FDemoSpawnSettings Settings; FDemoSpawnPlan Plan; FString Error; FDemoSpawnGeometry Geometry; // 值夹具，不写共享资产。
        if (!Check(DemoSpawnPlan::Build(Level,Difficulty,Enemies,Settings,TEXT("WeaverFixture"),false,4,Plan,Error)
            &&TestSpawner->StartEncounter(Plan,FTransform(Player->GetActorLocation()+FVector(4000,0,0)),Geometry,Error),TEXT("weaver production encounter"))) return;
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) if (It->GetOwner()==this&&It->IsAlive()) { It->SetActorTickEnabled(false); if (It->IsBoss()) Boss=*It; }
        DemoEffects::Apply(ASC,Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-Boss->GetMaxHealth()*.31f);
        int32 Killed=0; // 释放两个锚点名额，已有Boss和一个原小怪继续占位。
        for (TActorIterator<ADemoEnemy> It(GetWorld());It&&Killed<2;++It) if (It->GetOwner()==this&&It->IsAlive()&&!It->IsBoss()) { ++Killed; DemoEffects::Apply(ASC,It->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-It->GetMaxHealth()); }
        Advance(1.3f); break;
    }
    case 19:
    {
        const float Before=Boss->GetHealth(); // 锚点存在时Boss依然可被伤害，不存在隐形锁血。
        DemoEffects::Apply(ASC,Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10);
        if (!Check(Boss->GetHealth()==Before-10&&TestSpawner->GetAliveCount()==4,TEXT("weaver damageable with two live anchors"))) return;
        int32 Anchors=0; // 验证真正生成的装置数，而非仅排队请求数。
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) if (It->GetOwner()==this&&It->IsAlive()&&It->FindComponentByClass<UDemoEnemyRoster>()->GetKind()==EDemoRosterKind::Anchor)
        { ++Anchors; DemoEffects::Apply(ASC,It->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-It->GetMaxHealth()); }
        if (!Check(Anchors==2&&Other->IsRecovering(),TEXT("destroying both GAS anchors staggers weaver"))) return;
        DemoEffects::Apply(ASC,Boss->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-Boss->GetMaxHealth()*.35f);
        Player->SetActorLocation(Boss->GetActorLocation()+FVector(700,0,0)); Player->GetCharacterMovement()->StopMovementImmediately();
        CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Advance(2.1f); break;
    }
    case 20:
        Other->Update(Player,0); if (!Check(Other->IsCasting(),TEXT("weaver starts locked lightning warning"))) return;
        if (FParse::Param(FCommandLine::Get(),TEXT("DemoRosterCapture")))
        {
            Player->GetController()->SetControlRotation((Boss->GetActorLocation()-Player->GetActorLocation()).Rotation()); // 图像验收只调整真实玩家视角，不改Boss几何/伤害。
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Screenshots/RosterWeaver.png"),false,false);
        }
        Advance(.8f); break;
    case 21:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth,TEXT("lightning cannot hit during 1.2 second warning"))) return; Advance(.5f); break;
    case 22:
        Other->Update(Player,0); Advance(.75f); break;
    case 23:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth-22,TEXT("three locked lightning circles settle one salvo hit"))) return;
        CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Advance(3.4f); break;
    case 24:
        Other->Update(Player,0); if (!Check(Other->IsCasting(),TEXT("phase three alternates expanding ring with 1.5 second warning"))) return; Advance(1.6f); break;
    case 25:
        Other->Update(Player,0); Advance(.6f); break;
    case 26:
        // 700cm环约0.84秒穿过；真实冲刺窗口GE覆盖穿环时刻，测试不移动Pawn离开命中区。
        DemoEffects::Apply(ASC,ASC,UDemoDashEvadeEffect::StaticClass(),0); Advance(.5f); break;
    case 27:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth,TEXT("dash window evades expanding ring without healing"))) return;
        TestSpawner->CancelEncounter(true); Advance(.1f); break;
    case 28:
        Shooter=SpawnRosterFixture(GetWorld(),this,TEXT("Warden"),Player->GetActorLocation()+FVector(250,0,0));
        CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Advance(2.2f); break;
    case 29:
        Roster->Update(Player,0); if (!Check(Roster->IsCasting(),TEXT("warden first rotation starts .7 second fan"))) return; Advance(.8f); break;
    case 30:
        Roster->Update(Player,0); Advance(.3f); break;
    case 31:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth-22,TEXT("close-range overlapping fan projectiles damage only once"))) return;
        Shooter->Destroy(); Shooter=SpawnEnemy(Player->GetActorLocation()+FVector(800,0,0),false);
        CloseBeforeHealth=Player->GetDemoAttributes()->GetHealth(); Advance(.1f); break;
    case 32:
    {
        AStaticMeshActor* Cover=GetWorld()->SpawnActor<AStaticMeshActor>(Player->GetActorLocation()+FVector(400,0,0),FRotator::ZeroRotator); // 真World碰撞墙，不用Mock命中。
        Cover->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable); // 运行期先允许赋Mesh，否则Static组件拒绝设置会产生无碰撞的假墙。
        Cover->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
        Cover->SetActorScale3D(FVector(.25f,4,4)); Cover->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); Wall=Cover;
        FDemoHazardVisual Shot; Shot.Shape=EDemoHazardShape::Shot; Shot.Origin=Shooter->GetActorLocation(); Shot.Target=Player->GetActorLocation(); Shot.Warning=0; // 正常实体扫掠应先遇到25cm薄墙。
        ADemoRosterHazard* Hazard=GetWorld()->SpawnActor<ADemoRosterHazard>(); // 测试World持有，使用生产初始化/清理接口。
        Hazard->Initialize(Shooter.Get(),Shot,NewObject<UDemoRosterSalvo>(this),10); Advance(.8f); break;
    }
    case 33:
    {
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth,TEXT("new projectile swept collision respects cover"))) return;
        Wall->Destroy(); Wall.Reset();
        FDemoHazardVisual Ring; Ring.Shape=EDemoHazardShape::Ring; Ring.Origin=Ring.Target=Shooter->GetActorLocation(); Ring.Warning=1.5f; Ring.Radius=1000; // 800cm距离在传播2.4秒穿过，玩家不冲刺。
        ADemoRosterHazard* Hazard=GetWorld()->SpawnActor<ADemoRosterHazard>(); // 同一生产危险物，无客户端伤害捷径。
        Hazard->Initialize(Shooter.Get(),Ring,NewObject<UDemoRosterSalvo>(this),10); Advance(4.1f); break;
    }
    case 34:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth-10,TEXT("ring hits a non-dashing player exactly once"))) return; Advance(.6f); break;
    case 35:
        if (!Check(Player->GetDemoAttributes()->GetHealth()==CloseBeforeHealth-10,TEXT("ring passage cannot repeatedly damage"))) return;
        Mode->NotifyPlayerDied(); // 正式会话退出，同栈撤销所有剩余新危险物。
        for (TActorIterator<ADemoRosterHazard> It(GetWorld());It;++It) if (!Check(It->IsActorBeingDestroyed(),TEXT("session cleanup removes all roster hazards"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_ENEMY_ROSTER_SUCCESS")); FPlatformMisc::RequestExit(false); SetActorTickEnabled(false); break;
    }
}

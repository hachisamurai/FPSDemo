// 职责：Editor隔离战斗天赋回归。使用：-DemoCombatTalentTest经启动器生成，实际伤害/弹丸/GE时钟为断言，不访问云服务。
#include "Tests/DemoCombatTalentTest.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Tests/DemoProjectileFixtures.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Combat/DemoProjectileDamage.h"
#include "GAS/Gambling/DemoGambleEffects.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h" // 清场撤销窗口的边界必须与自然到期区分。
#include "GAS/Abilities/DemoBossDiveAbility.h" // 用正式免伤Tag验证实际0扣血不会吸血。
#include "AI/DemoEnemy.h"
#include "AbilitySystemComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDeviceNull.h"
#include "EngineUtils.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#endif

ADemoCombatTalentTest::ADemoCombatTalentTest()
{ DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.02f; }
#if WITH_EDITOR
bool ADemoCombatTalentTest::Check(bool Condition,const TCHAR* Message)
{
    DEMO_LOG_CALL(); UE_LOG(LogFPSDemo,Display,TEXT("COMBAT_TALENT_TEST %s step=%d %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Step,Message);
    if(!Condition) { bFailed=true; FPlatformMisc::RequestExitWithStatus(false,1); }
    return Condition;
}
void ADemoCombatTalentTest::Advance(float Delay)
{ DEMO_LOG_CALL(); ++Step; Next=GetWorld()->GetTimeSeconds()+Delay; }
ADemoEnemy* ADemoCombatTalentTest::SpawnTarget(ADemoCharacter* Player,float Distance,float Side)
{
    DEMO_LOG_CALL();
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 非GM注册，死亡不会自动清关。
    ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(FVector(Distance,Side+2000,3500),FRotator::ZeroRotator,Params); // World独立夹具。
    if(!Enemy) return nullptr;
    FDemoEnemySpawnStats Stats; Stats.Health=5000; Stats.AttackPower=0; Enemy->Configure(Stats); // HP覆盖全部直伤，不靠无敌绕过伤害结算。
    USkeletalMeshComponent* Mesh=DemoHitZoneTest::Pose(Enemy,TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0); // 真实骨骼碰撞。
    FVector Point; // 稳定机身表面的模型空间点cm。
    const auto* Profile=Player->GetWeaponComponent()->GetActiveWeapon()->EnemyHitProfile.Get(); // 正式武器部位资产。
    if(!Mesh||!Profile||!DemoHitZoneTest::FindRegion(Mesh,Profile,EDemoEnemyHitRegion::Body,Point)) { Enemy->Destroy(); return nullptr; }
    DemoHitZoneTest::Aim(Enemy,Player,Point,Distance); Enemy->AddActorWorldOffset(FVector(0,Side,0)); Mesh->RefreshBoneTransforms();
    return Enemy;
}
bool ADemoCombatTalentTest::FindHit(ADemoEnemy* Enemy,EDemoEnemyHitRegion Region,FHitResult& OutHit)
{
    DEMO_LOG_CALL();
    USkeletalMeshComponent* Mesh=Enemy?Enemy->FindComponentByClass<USkeletalMeshComponent>():nullptr; // 本次查询借用。
    const auto* Profile=LoadObject<UDemoEnemyHitProfile>(nullptr,TEXT("/Game/Data/Combat/DA_EnemyHitZones.DA_EnemyHitZones")); // 正式已导入配置。
    FVector Point; // 稳定模型空间表面，不预填Hit骨名。
    if(!Mesh||!Profile||!DemoHitZoneTest::FindRegion(Mesh,Profile,Region,Point)) return false;
    const FTransform Transform=Mesh->GetComponentTransform(); // 世界查询必须命中与实际弹相同的通道。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(TalentFixture),false); // 简单PhysicsAsset刚体查询。
    return GetWorld()->LineTraceSingleByChannel(OutHit,Transform.TransformPosition(Point+FVector(200,0,0)),Transform.TransformPosition(Point-FVector(200,0,0)),ECC_GameTraceChannel3,Query)&&OutHit.GetActor()==Enemy;
}
UDemoShotContext* ADemoCombatTalentTest::MakeShot(ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    ADemoWeaponBase* Weapon=Player->GetWeaponComponent()->GetActiveWeapon(); // 来源原枪，精准回收冻结此实例。
    Shot=NewObject<UDemoShotContext>(this);
    return Shot->Initialize(Player,Weapon->GetWeaponId(),Weapon->Config,Weapon->EnemyHitProfile,0,FDemoAmmoEffectSnapshot(),Weapon->GetDamagePerPellet())?Shot.Get():nullptr;
}
bool ADemoCombatTalentTest::LaunchAtTarget(ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    FHitResult Hit; // 与FindHit相同实际表面用作飞行目标，不直接施加伤害。
    if(!FindHit(Target.Get(),EDemoEnemyHitRegion::Body,Hit)||!MakeShot(Player)) return false;
    ADemoProjectileBase* Bullet=GetWorld()->SpawnActor<ADemoProjectileBase>(Hit.ImpactPoint-FVector(500,0,0),FRotator::ZeroRotator); // 只存在于测试World。
    if(!Bullet) return false;
    Bullet->Config.InitialSpeed=1000; // 留出首敌/后敌之间可观察的飞行时间，不修改共享CDO。
    return Bullet->Prepare(Shot,0,FVector::ForwardVector)&&Bullet->ActivateProjectile();
}
#endif
void ADemoCombatTalentTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    if(bFailed||GetWorld()->GetTimeSeconds()<Next) return;
    if(!Check(GetWorld()->GetTimeSeconds()<60,TEXT("bounded isolated runtime"))) return;
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 每步重新解析玩家。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 不跨重生借用裸指针。
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 正式流程入口。
    if(!Check(Player&&Mode,TEXT("runtime dependencies"))) return;
    UDemoWeaponComponent* Equipment=Player->GetWeaponComponent(); // 原生真实双槽组件。
    UAbilitySystemComponent* ASC=Player->GetAbilitySystemComponent(); // PS拥有，所有HP变更走生产GE。
    UDemoPlayerProgressComponent* Progress=Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 正式账本供断言。
    FOutputDeviceNull Output; // 控制台命令的同步输出丢弃，结果由实际GAS/账本断言。
    switch(Step)
    {
    case 0:
    {
        UDemoRunSaves* Saves=GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // Editor每GI独立临时槽，退出自动清理。
        if(!Check(Saves&&Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("isolated editor run"))) return;
        Saves->ConsumePending();
        PC->OnRunReady();
        if(!Check(DemoProjectileFixtures::EquipPrimary(Player,1),TEXT("capture legitimately unlocked shotgun loadout"))) return;
        Equipment->CaptureSnapshot(ShotgunLoadout); // 同步复制已提交的库存值，后续不依赖旧Actor存活。
        if(!Check(DemoProjectileFixtures::EquipPrimary(Player,0),TEXT("real rifle equipped"))) return;
        ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 生产战斗凭据监听阶段，专用空中靶场。
        Mode->StartNextLevel(); // 必须创建真实可撤销CombatSession，不能仅改显示阶段伪造伤害资格。
        if(!Check(State->Phase==EDemoPhase::Combat,TEXT("real combat session initialized"))) return;
        Player->SetActorLocation(FVector(0,0,3000)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying); Player->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation(FRotator::ZeroRotator);
        ADemoWeaponBase* Weapon=Equipment->GetActiveWeapon(); // 只修改本次实例，结束Editor自动销毁。
        Weapon->Config.BaseDamage=20; Weapon->Config.SpreadHalfAngle=0; Weapon->Config.RecoilPitch=0; Weapon->Config.Range=5000; Weapon->Config.FalloffStart=4000;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents overdrive twin siphon"),Output,GetWorld());
        if(!Check(Progress->GetSnapshot().Gambling.Talents.Num()==3&&Player->GetDemoAttributes()->GetFireRateMultiplier()==2&&FMath::IsNearlyEqual(Weapon->GetFireInterval(),30.f/Weapon->Config.RoundsPerMinute),TEXT("debug combination grants actual GAS doubled fire rate"))) return;
        DemoGambleEffects::Sync(ASC,Progress->GetSnapshot().Gambling); // 重建必须幂等，不能再翻倍。
        if(!Check(Player->GetDemoAttributes()->GetFireRateMultiplier()==2,TEXT("rebuild does not stack rate to four"))) return;
        const int32 Before=Weapon->GetAmmo(); // 同一触发成本，不按弹体数扣费。
        DemoHitZoneTest::Fire(Player);
        if(!Check(Weapon->GetAmmo()==Before-Weapon->Config.AmmoPerShot&&DemoProjectileFixtures::CountLive(GetWorld())==2&&FMath::IsNearlyEqual(Weapon->GetDamagePerPellet(),14.f)
            &&FMath::IsNearlyEqual(Weapon->GetFireCooldownRemaining(),Weapon->GetFireInterval(),.002f),TEXT("rifle launches two physical bullets at 70 percent and pays once with actual doubled cooldown rate"))) return;
        ADemoProjectileBase::CancelAll(GetWorld());
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents unknown"),Output,GetWorld());
        if(!Check(Progress->GetSnapshot().Gambling.Talents.Num()==3,TEXT("unknown debug talent rejected atomically"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents overdrive:2 twin:2 siphon:2"),Output,GetWorld());
        if(!Check(Player->GetDemoAttributes()->GetFireRateMultiplier()==2.25f&&FMath::IsNearlyEqual(Weapon->GetDamagePerPellet(),17.5f)&&Weapon->GetProjectilesPerShot()==2,TEXT("rank upgrade changes actual rate and damage without exponential entities"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents overdrive twin siphon"),Output,GetWorld()); // 后续原1级边界仍保持独立期望。
        Target=SpawnTarget(Player,1000);
        if(!Check(Target.IsValid(),TEXT("real skeletal damage target"))) return;
        DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),-50); // 玩家留足回血空间。
        FHitResult Hit; // 实际机身PhysicsAsset命中。
        if(!Check(FindHit(Target.Get(),EDemoEnemyHitRegion::Body,Hit)&&MakeShot(Player),TEXT("valid shot and physical body hit"))) return;
        for(int32 Index=0;Index<3;++Index) // 三次100实际直伤共享同一秒预算，应3+3+2HP。
            if(!Check(DemoProjectileDamage::Apply(Shot,Hit,100,Index),TEXT("direct damage enters production GAS path"))) return;
        if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),58.f,.02f),TEXT("siphon caps combined pellets at eight HP per second"))) return;
        DemoEffects::Apply(ASC,Target->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-100); // DOT/爆炸共用HP效果，未进入直击钩子。
        if(!Check(Player->GetDemoAttributes()->GetHealth()==58,TEXT("non-projectile damage does not siphon"))) return;
        Advance(1.1f); break;
    }
    case 1:
    {
        FHitResult Hit; // 新窗口内实际10HP伤害应仅回血0.3。
        if(!Check(FindHit(Target.Get(),EDemoEnemyHitRegion::Body,Hit)&&DemoProjectileDamage::Apply(Shot,Hit,10,0),TEXT("new-window direct damage"))) return;
        if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),58.3f,.02f),TEXT("siphon window replenishes after one game second"))) return;
        Target->GetAbilitySystemComponent()->AddLooseGameplayTag(DemoBossTags::Invulnerable); // 隔离夹具使用生产免伤守卫，调用后必须移除。
        DemoProjectileDamage::Apply(Shot,Hit,100,1);
        Target->GetAbilitySystemComponent()->RemoveLooseGameplayTag(DemoBossTags::Invulnerable);
        if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),58.3f,.02f),TEXT("immune target cannot produce siphon healing"))) return;
        Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),5); // 残血靶，制造独立于理论伤害的过量伤害边界。
        DemoProjectileDamage::Apply(Shot,Hit,1000,2);
        if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),58.45f,.02f),TEXT("overkill heals only from five actual HP lost"))) return;
        Target=SpawnTarget(Player,1000); // 后续核心去重用新活体，不复活死亡Actor/旧委托。
        if(!Check(Target.IsValid(),TEXT("fresh target after lethal boundary"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents twin precision"),Output,GetWorld());
        ADemoWeaponBase* Weapon=Equipment->GetActiveWeapon(); Weapon->RestoreAmmo(5,20); // 原枪留空间观察返弹。
        if(!Check(Player->GetDemoAttributes()->GetFireRateMultiplier()==1&&MakeShot(Player)&&FindHit(Target.Get(),EDemoEnemyHitRegion::Core,Hit),TEXT("removed overdrive restores rate; real core probe"))) return;
        DemoProjectileDamage::Apply(Shot,Hit,10,0); DemoProjectileDamage::Apply(Shot,Hit,10,1);
        if(!Check(Weapon->GetAmmo()==6,TEXT("two core pellets refund only one round per shot"))) return;
        if(!Check(MakeShot(Player)&&FindHit(Target.Get(),EDemoEnemyHitRegion::Body,Hit),TEXT("new body shot"))) return;
        DemoProjectileDamage::Apply(Shot,Hit,10,0);
        if(!Check(Weapon->GetAmmo()==6,TEXT("body damage cannot trigger precision refund"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents twin precision:3"),Output,GetWorld());
        if(!Check(MakeShot(Player)&&FindHit(Target.Get(),EDemoEnemyHitRegion::Core,Hit),TEXT("rank three precision shot prepared"))) return;
        DemoProjectileDamage::Apply(Shot,Hit,10,0); DemoProjectileDamage::Apply(Shot,Hit,10,1);
        if(!Check(Weapon->GetAmmo()==9,TEXT("rank three refunds exactly three once across two core pellets"))) return;
        ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 无飞行弹时的同步库存恢复夹具，不向生产代码增加战斗中换主枪入口。
        State->Phase=EDemoPhase::Intermission;
        const bool Restored=Equipment->RestoreSnapshot(ShotgunLoadout); // 仅安全阶段允许正式恢复事务；仍使用本测试真实创建的CombatSession。
        State->Phase=EDemoPhase::Combat;
        if(!Check(Restored,TEXT("restore previously unlocked real shotgun in safe-stage fixture"))) return;
        Weapon=Equipment->GetActiveWeapon();
        const int32 Before=Weapon->GetAmmo(); // 真实霰弹组，不能只检查一个返回计数函数。
        DemoHitZoneTest::Fire(Player);
        if(!Check(DemoProjectileFixtures::CountLive(GetWorld())==Weapon->GetPelletCount()*2&&Weapon->GetAmmo()==Before-Weapon->Config.AmmoPerShot,TEXT("shotgun doubles all pellets with one ammo payment"))) return;
        ADemoProjectileBase::CancelAll(GetWorld());
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents dashload"),Output,GetWorld());
        Weapon->Config.bInfiniteReserve=false; Weapon->RestoreAmmo(0,20); DashAmmo=FMath::Max(1,FMath::FloorToInt(Weapon->GetCapacity()*.25f));
        Player->PerformDash(); // 正式施加0.45秒GE与结束订阅；无需人为触发组件到期函数。
        if(!Check(Weapon->GetAmmo()==0,TEXT("dash load waits for GAS evade expiry"))) return;
        Advance(FMath::Max(.6f,Weapon->GetFireCooldownRemaining()+.05f)); break; // 同时等真实霰弹冷却，不能为测试绕过生产开火门控。
    }
    case 2:
    {
        ADemoWeaponBase* Weapon=Equipment->GetActiveWeapon(); // 冲刺期间未换枪。
        if(!Check(Weapon->GetAmmo()==DashAmmo&&Weapon->GetReserveAmmo()==20-DashAmmo,TEXT("dash expiry transfers finite reserve without creating rounds"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents twin"),Output,GetWorld());
        const int32 OriginalPellets=Weapon->Config.PelletCount; // 边界仅改本测试武器实例，检查0..63弹体全部准备成功。
        Weapon->Config.PelletCount=32;
        if(!Check(Weapon->PrepareShot()&&DemoProjectileFixtures::CountLive(GetWorld())==64&&Weapon->IsPreparedShotValid(),TEXT("maximum 32-pellet shotgun prepares all 64 twin projectiles"))) return;
        Weapon->CancelPreparedShot(); Weapon->Config.PelletCount=OriginalPellets;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents dashload"),Output,GetWorld()); // 恢复下一次真实冲刺的资格。
        Weapon->RestoreAmmo(0,20); Player->PerformDash();
        ASC->RemoveActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(DemoTags::DashEvading))); // 模拟清场早于自然到期，不能补弹。
        if(!Check(Weapon->GetAmmo()==0&&Weapon->GetReserveAmmo()==20,TEXT("early removal of evade GE cannot grant dash ammo"))) return;
        Weapon->RestoreAmmo(0,20); Player->PerformDash(); Equipment->EquipSlot(2); // 结束前切换，旧凭据必须失效。
        Advance(.6f); break;
    }
    case 3:
    {
        Equipment->EquipSlot(1);
        if(!Check(Equipment->GetActiveWeapon()->GetAmmo()==0,TEXT("switching during dash rejects deferred load"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents ricochet:2"),Output,GetWorld());
        Equipment->EquipSlot(2); ADemoWeaponBase* Weapon=Equipment->GetActiveWeapon(); // 弹射不依赖当前散弹弹数。
        Weapon->Config.BaseDamage=20; Weapon->Config.Range=5000; Weapon->Config.FalloffStart=4000;
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->SetActorLocation(FVector(0,0,3000)); PC->SetControlRotation(FRotator::ZeroRotator);
        Target->Destroy(); Target=SpawnTarget(Player,1000); Rear=SpawnTarget(Player,1500,200);
        if(!Check(Target.IsValid()&&Rear.IsValid(),TEXT("separated front and off-axis rear targets"))) return;
        FHitResult FrontHit; FHitResult RearHit; // 预测查询只用于独立期望值，实际伤害仍需飞行。
        if(!Check(FindHit(Target.Get(),EDemoEnemyHitRegion::Body,FrontHit),TEXT("ricochet front body probe"))) return;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(TalentRear),false); Query.AddIgnoredActor(Target.Get()); // 同一可见性边界。
        EDemoEnemyHitRegion Region; float Multiplier=0; // 后目标自己的倍率，不能使用前目标核心倍数。
        if(!Check(GetWorld()->LineTraceSingleByChannel(RearHit,FrontHit.ImpactPoint,Rear->FindComponentByClass<USkeletalMeshComponent>()->Bounds.Origin,ECC_GameTraceChannel3,Query)
            &&Weapon->EnemyHitProfile->ResolveHitForChannel(RearHit,Region,Multiplier,ECC_GameTraceChannel3),TEXT("visible rear physical region"))) return;
        ExpectedRearDamage=12.5f*Multiplier; // 2级弹射实际后段为20*0.625，再乘后靶部位倍率。
         RearBefore=Rear->GetHealth(); FrontBefore=Target->GetHealth();
        if(!Check(LaunchAtTarget(Player)&&Rear->GetHealth()==RearBefore,TEXT("ricochet launch never applies search-ray damage"))) return;
        Advance(.56f); break;
    }
    case 4:
        if(!Check(Target->GetHealth()<FrontBefore&&Rear->GetHealth()==RearBefore&&DemoProjectileFixtures::CountLive(GetWorld())==1,TEXT("first hit redirects physical projectile before rear arrival"))) return;
        Advance(.75f); break;
    case 5:
        if(!Check(FMath::IsNearlyEqual(Rear->GetHealth(),RearBefore-ExpectedRearDamage,.05f)&&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("rank two ricochet deals upgraded damage then terminates"))) return;
        RearBefore=Rear->GetHealth(); FrontBefore=Target->GetHealth();
        if(!Check(LaunchAtTarget(Player),TEXT("second actual ricochet flight"))) return;
        Advance(.56f); break;
    case 6:
    {
        if(!Check(Target->GetHealth()<FrontBefore&&Rear->GetHealth()==RearBefore,TEXT("second front hit precedes wall insertion"))) return;
        AActor* Blocker=GetWorld()->SpawnActor<AActor>(); // 飞行中动态墙，排除搜敌时提前施伤实现。
        UBoxComponent* Box=NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Box); Box->SetBoxExtent(FVector(2,350,350)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
        TActorIterator<ADemoProjectileBase> Bullet(GetWorld()); // 当前仅一颗已折向弹，不用会随冲刺移动的玩家相机推测靶场坐标。
        const FVector RearCenter=Rear->FindComponentByClass<USkeletalMeshComponent>()->Bounds.Origin; // 当前后靶中心，与搜敌相同几何定义。
        if(!Check(bool(Bullet),TEXT("redirected bullet exists before wall"))) return;
        Blocker->SetActorLocation((Bullet->GetActorLocation()+RearCenter)*.5); Wall=Blocker;
        FHitResult WallHit; FCollisionQueryParams WallQuery(SCENE_QUERY_STAT(TalentWall),false,*Bullet); // 验证夹具墙确实位于尚未飞过的折线路径。
        if(!Check(GetWorld()->LineTraceSingleByChannel(WallHit,Bullet->GetActorLocation(),RearCenter,ECC_GameTraceChannel3,WallQuery)&&WallHit.GetActor()==Blocker,TEXT("inserted wall physically blocks remaining trajectory"))) return;
        Advance(.8f); break;
    }
    case 7:
    {
        if(!Check(Rear->GetHealth()==RearBefore&&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("wall inserted after redirect blocks physical second hit"))) return;
        Wall->Destroy();
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents siphon:2 dashload:2"),Output,GetWorld());
        ASC->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),50); // 留足等级回血空间，旧时间窗已经过期。
        FHitResult Hit; // 使用真实机身命中，避免直接调用回血组件假通过。
        if(!Check(MakeShot(Player)&&FindHit(Target.Get(),EDemoEnemyHitRegion::Body,Hit),TEXT("rank two siphon shot prepared"))) return;
        DemoProjectileDamage::Apply(Shot,Hit,100,0);
        if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),53.75f,.01f),TEXT("rank two siphon heals 3.75 percent actual damage"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents siphon:4 dashload:2"),Output,GetWorld()); // 在飞旧弹仍冻结2级比例和预算。
        DemoProjectileDamage::Apply(Shot,Hit,100,1); DemoProjectileDamage::Apply(Shot,Hit,100,2);
        if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetHealth(),60.f,.01f),TEXT("old shot freezes ranked ten HP budget after source upgrades"))) return;
        Target->Destroy(); Rear->Destroy();
        ADemoWeaponBase* Weapon=Equipment->GetActiveWeapon(); // 当前手枪，2级冲刺转移40%容量且备用守恒。
        Weapon->Config.bInfiniteReserve=false; Weapon->RestoreAmmo(0,20); DashAmmo=FMath::Max(1,FMath::FloorToInt(Weapon->GetCapacity()*.4f));
        Player->PerformDash(); Advance(.6f); break;
    }
    case 8:
    {
        ADemoWeaponBase* Weapon=Equipment->GetActiveWeapon(); // 等待真实GAS到期之后读取弹量。
        if(!Check(Weapon->GetAmmo()==DashAmmo&&Weapon->GetReserveAmmo()==20-DashAmmo,TEXT("rank two dash transfers forty percent from finite reserve"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents clear"),Output,GetWorld());
        if(!Check(Progress->GetSnapshot().Gambling.Talents.IsEmpty()&&Player->GetDemoAttributes()->GetFireRateMultiplier()==1,TEXT("debug clear removes tags and temporary rate"))) return;
        if(!CheckTalentSystem(Player)) return;
        Advance(2.f); break;
    }
    case 9:
    {
        Target=SpawnTarget(Player,1000);
        if(!Check(Target.IsValid(),TEXT("second independent rhythm target"))) return;
        DemoEffects::Apply(ASC,Target->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10000);
        if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetMoveSpeedMultiplier(),1.25f),TEXT("rhythm two independently timed GE layers"))) return;
        Advance(3.2f); break;
    }
    case 10:
    {
        if(!Check(FMath::IsNearlyEqual(Player->GetDemoAttributes()->GetMoveSpeedMultiplier(),1.125f),TEXT("first rhythm layer expires without refreshing second"))) return;
        Player->ClearAttackStatuses();
        if(!Check(Player->GetDemoAttributes()->GetMoveSpeedMultiplier()==1.f,TEXT("combat cleanup removes residual speed layer"))) return;
        Target=SpawnTarget(Player,1000);
        if(!Check(Target.IsValid(),TEXT("elimination speed cleanup target"))) return;
        DemoEffects::Apply(ASC,Target->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-10000);
        if(!Check(Player->GetDemoAttributes()->GetMoveSpeedMultiplier()>1.f,TEXT("speed active immediately before individual elimination"))) return;
        Player->SetCoopEliminated(true);
        if(!Check(Player->GetDemoAttributes()->GetMoveSpeedMultiplier()==1.f,TEXT("individual elimination clears speed without waiting for team wipe"))) return;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("Demo.Debug.Talents clear"),Output,GetWorld());
        UE_LOG(LogFPSDemo,Display,TEXT("COMBAT_TALENT_TEST SUCCESS: GAS, real damage, real bullets, dash expiry and editor commands"));
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
    }
#endif
}

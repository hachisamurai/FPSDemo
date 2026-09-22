#include "Tests/DemoProjectileTest.h"
#include "Tests/DemoProjectileFixtures.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/Projectiles/DemoShotContext.h" // 身份断言读取真正随弹飞行的上下文，不从当前装备倒推。
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "AI/DemoEnemy.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h" // 只在当前World同步借用实体弹，检查真实发射身份。
#include "Debug/DemoLog.h"

ADemoProjectileTest::ADemoProjectileTest()
{
    DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=.02f;
}
bool ADemoProjectileTest::Check(bool Condition,const TCHAR* Message)
{
    DEMO_LOG_CALL(); UE_LOG(LogFPSDemo,Display,TEXT("PROJECTILE_TEST %s step=%d: %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Step,Message);
    if (!Condition) { bFailed=true; RestoreProjectileDefaults(); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,1); }
    return Condition;
}
void ADemoProjectileTest::Advance(float Delay)
{
    DEMO_LOG_CALL(); ++Step; NextTime=GetWorld()->GetTimeSeconds()+Delay;
}
void ADemoProjectileTest::RestoreProjectileDefaults()
{
    DEMO_LOG_CALL();
    if (ProjectileDefaults&&OriginalSpeed>0.f) { ProjectileDefaults->Config.InitialSpeed=OriginalSpeed; OriginalSpeed=0.f; }
}
void ADemoProjectileTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); RestoreProjectileDefaults(); Super::EndPlay(EndPlayReason);
}
ADemoEnemy* ADemoProjectileTest::SpawnTarget(ADemoCharacter* Player,float Distance)
{
    DEMO_LOG_CALL();
    FActorSpawnParameters Params; // 空中生成不受自然关卡地形影响；World持有敌人。
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector StagingLocation=Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(Distance,1000,400); // 扫描骨骼探针时先离开两靶共同弹道，各靶按目标距离独立暂存，避免初生重叠。
    ADemoEnemy* Enemy=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),StagingLocation,FRotator::ZeroRotator,Params); // 非GM注册靶，不导致自然清关。
    if (!Enemy) return nullptr;
    FDemoEnemySpawnStats Stats; Stats.Health=1000.f; Stats.AttackPower=0.f; // 高HP无攻击，不改变原生技能或奖励接口。
    Enemy->Configure(Stats);
    USkeletalMeshComponent* Mesh=DemoHitZoneTest::Pose(Enemy,TEXT("/Game/Enemies/Breach/QA/A_Breach_Chaser_RigCheck.A_Breach_Chaser_RigCheck"),0.f); // 冻结动画/行为，仍用真实PhysicsAsset。
    const UDemoEnemyHitProfile* Profile=LoadObject<UDemoEnemyHitProfile>(nullptr,TEXT("/Game/Data/Combat/DA_EnemyHitZones.DA_EnemyHitZones")); // 校验Cook后的正式部位数据。
    if (!Mesh||!Profile||!DemoHitZoneTest::FindRegion(Mesh,Profile,EDemoEnemyHitRegion::Body,BodyPoint)) { Enemy->Destroy(); return nullptr; }
    DemoHitZoneTest::Aim(Enemy,Player,BodyPoint,Distance);
    return Enemy;
}
AActor* ADemoProjectileTest::SpawnWall(ADemoCharacter* Player,float Distance)
{
    DEMO_LOG_CALL();
    AActor* Blocker=GetWorld()->SpawnActor<AActor>(); // 独立World夹具，不保存到地图。
    UBoxComponent* Box=NewObject<UBoxComponent>(Blocker); // Actor持有薄墙查询体；2cm厚必须被高速扫掠捕获。
    Blocker->SetRootComponent(Box); Box->SetBoxExtent(FVector(1,100,100)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
    Blocker->SetActorLocation(Player->GetFirstPersonCameraComponent()->GetComponentLocation()+FVector(Distance,0,0));
    return Blocker;
}
void ADemoProjectileTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    if (bFailed||GetWorld()->GetTimeSeconds()<NextTime) return;
    if (!Check(GetWorld()->GetTimeSeconds()<60.f,TEXT("bounded world runtime"))) return;
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 本帧权威规则入口，测试不跨World缓存。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 阶段取消由正式模式接口执行。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 当前本地控制器。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 当前真实Avatar。
    if (!Check(Mode&&State&&Player,TEXT("runtime dependencies ready"))) return;
    UDemoWeaponComponent* Equipment=Player->GetWeaponComponent(); // 玩家真实两槽装备，不创建脱离库存的武器。
    const UDemoAmmoCatalog* AmmoConfig=Player->FindComponentByClass<UDemoAmmoComponent>()->GetCatalog(); // 穿透期望值来自正式目录。
    switch (Step)
    {
    case 0:
        if (!Check(Mode->StartRun(),TEXT("isolated run initialized"))) return;
        PC->OnRunReady();
        if (!Check(DemoProjectileFixtures::EquipPrimary(Player,0)&&Equipment->EquipSlot(2),TEXT("real rifle primary and pistol secondary ready"))) return;
        State->Phase=EDemoPhase::Combat; State->LevelNumber=1;
        Player->SetActorLocation(FVector(0,0,3000)); Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
        Player->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation(FRotator::ZeroRotator);
        Pistol=Equipment->GetActiveWeapon();
        Pistol->Config.BaseDamage=20.f; Pistol->Config.SpreadHalfAngle=0.f; Pistol->Config.RecoilPitch=0.f; Pistol->Config.MuzzleSocket=NAME_None;
        Pistol->Config.Range=3000.f; Pistol->Config.FalloffStart=2500.f; // 命中时序与距离衰减分开验证，避免理论伤害混入衰减。
        if (!Check(Pistol->Config.ProjectileClass!=nullptr,TEXT("weapon references projectile class"))) return;
        ProjectileDefaults=Pistol->Config.ProjectileClass->GetDefaultObject<ADemoProjectileBase>();
        OriginalSpeed=ProjectileDefaults->Config.InitialSpeed; ProjectileDefaults->Config.InitialSpeed=1000.f; // 专项可见飞行窗口，结束必须恢复共享CDO。
        DemoHitZoneTest::Ammo(Player,TEXT("normal")); Target=SpawnTarget(Player,1200.f);
        if (!Check(Target.IsValid(),TEXT("true skeletal body target ready"))) return;
        {
            const TSubclassOf<ADemoProjectileBase> SavedClass=Pistol->Config.ProjectileClass; // 仅测试非法类预检，调用后立即恢复正式类引用。
            const int32 AmmoBefore=Pistol->GetAmmo(); // 非法准备不得扣费，与后续正常射击分别断言。
            const float CooldownBefore=Pistol->GetFireCooldownRemaining(); // 不可生成实体弹时不能启动射速冷却。
            Pistol->Config.ProjectileClass=nullptr; DemoHitZoneTest::Fire(Player); Pistol->Config.ProjectileClass=SavedClass;
            if (!Check(Pistol->GetAmmo()==AmmoBefore&&FMath::IsNearlyEqual(Pistol->GetFireCooldownRemaining(),CooldownBefore,.001f)
                &&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("invalid projectile class rejects before ammo cooldown or actor commitment"))) return;
        }
        Advance(.2f); break;
    case 1:
        Before=Target->GetHealth(); DemoHitZoneTest::Fire(Player);
        if (!Check(Target->GetHealth()==Before&&DemoProjectileFixtures::CountLive(GetWorld())==1,TEXT("aim ray never damages; real bullet survives Fire GA end"))) return;
        for (TActorIterator<ADemoProjectileBase> It(GetWorld());It;++It) // 单颗手枪弹在相同调用栈校验，未开始运动或伤害。
            if (!Check(It->GetShotContext()&&It->GetShotContext()->GetWeaponId()==TEXT("pistol"),TEXT("actual pistol projectile keeps pistol weapon identity"))) return;
        Advance(.15f); break;
    case 2:
        if (!Check(Target->GetHealth()==Before&&DemoProjectileFixtures::CountLive(GetWorld())==1,TEXT("distant target remains unharmed while bullet travels"))) return;
        Advance(1.2f); break;
    case 3:
        if (!Check(FMath::IsNearlyEqual(Target->GetHealth(),Before-20.f,.02f)&&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("body collision applies exactly one GAS damage after arrival"))) return;
        Before=Target->GetHealth(); DemoHitZoneTest::Fire(Player);
        Target->AddActorWorldOffset(FVector(0,400,0)); Target->FindComponentByClass<USkeletalMeshComponent>()->RefreshBoneTransforms(); // 开火后移开真实刚体，不能仍按旧射线目标扣血。
        Advance(3.2f); break;
    case 4:
        if (!Check(Target->GetHealth()==Before&&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("target evades original trajectory and missed bullet expires by range"))) return;
        DemoHitZoneTest::Aim(Target.Get(),Player,BodyPoint,1200.f); DemoHitZoneTest::Fire(Player);
        Wall=SpawnWall(Player,600.f); // 在射线结束后才插入墙，专门验证飞行扫掠而非开火视线查询。
        Advance(.9f); break;
    case 5:
        if (!Check(Target->GetHealth()==Before&&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("thin wall inserted after launch blocks actual flying bullet"))) return;
        Wall->Destroy(); DemoHitZoneTest::Fire(Player);
        Pistol->Config.BaseDamage=200.f; DemoHitZoneTest::Ammo(Player,TEXT("frost")); // 发射后同时改旧枪数值和当前弹药，命中仍需读冻结快照。
        if (!Check(Equipment->EquipSlot(1),TEXT("switch weapon while original pistol projectile is flying"))) return;
        Advance(1.4f); break;
    case 6:
    {
        if (!Check(FMath::IsNearlyEqual(Target->GetHealth(),Before-20.f,.02f)
            &&Target->FindComponentByClass<UDemoAmmoStatus>()->Count(DemoAmmoTags::Chill)==0,TEXT("in-flight shot retains original damage and normal ammo after switch"))) return;
        Pistol->Config.BaseDamage=20.f;
        if (!Check(Equipment->EquipSlot(2),TEXT("restore pistol for penetration timing"))) return;
        DemoHitZoneTest::Ammo(Player,TEXT("piercing")); DemoHitZoneTest::Aim(Target.Get(),Player,BodyPoint,1000.f);
        const FVector PlayerBeforeRearSpawn=Player->GetActorLocation(); // Aim只移动靶，后靶构造不应改变玩家/前靶的几何基准。
        const FVector FrontSurface=Target->FindComponentByClass<USkeletalMeshComponent>()->GetComponentTransform().TransformPosition(BodyPoint); // 使用首靶自己的表面快照，后靶探针可能覆盖BodyPoint。
        RearTarget=SpawnTarget(Player,1600.f);
        if (!Check(RearTarget.IsValid(),TEXT("rear skeletal target ready"))) return;
        {
            const FVector Camera=Player->GetFirstPersonCameraComponent()->GetComponentLocation(); // 冻结当前瞄准原点。
            const FVector RearSurface=RearTarget->FindComponentByClass<USkeletalMeshComponent>()->GetComponentTransform().TransformPosition(BodyPoint); // 验证真实骨骼表面，不能只相信期望距离参数。
            if (!Check(Player->GetActorLocation().Equals(PlayerBeforeRearSpawn,.01f)&&FMath::IsNearlyEqual((FrontSurface-Camera).X,1000.f,.1f)
                &&FMath::IsNearlyEqual((RearSurface-Camera).X,1600.f,.1f),TEXT("front and rear skeletal surfaces remain 1000 and 1600 cm with stationary player"))) return;
            const FVector Direction=(Camera+FVector(1000,0,0)-Pistol->GetMuzzleLocation()).GetSafeNormal(); // 与枪口到首靶表面的实际直线一致。
            RearTarget->AddActorWorldOffset(FVector(0,Direction.Y/Direction.X*600,Direction.Z/Direction.X*600));
        }
        Before=Target->GetHealth(); RearBefore=RearTarget->GetHealth(); DemoHitZoneTest::Fire(Player); Advance(1.05f); break;
    }
    case 7:
        if (!Check(FMath::IsNearlyEqual(Target->GetHealth(),Before-20.f*AmmoConfig->PiercingDamageMultiplier,.02f)
            &&RearTarget->GetHealth()==RearBefore&&DemoProjectileFixtures::CountLive(GetWorld())==1,TEXT("piercing damages front target while rear target awaits physical arrival"))) return;
        Advance(.7f); break;
    case 8:
        if (!Check(FMath::IsNearlyEqual(RearTarget->GetHealth(),RearBefore-20.f*AmmoConfig->PiercingDamageMultiplier*AmmoConfig->SecondaryDamageRatio,.02f)
            &&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("rear target receives configured secondary damage only after flight"))) return;
        Before=Target->GetHealth(); RearBefore=RearTarget->GetHealth(); DemoHitZoneTest::Fire(Player);
        Wall=SpawnWall(Player,1300.f); Advance(1.7f); break;
    case 9:
        if (!Check(Target->GetHealth()<Before&&RearTarget->GetHealth()==RearBefore&&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("wall between enemies terminates real penetration"))) return;
        Wall->Destroy(); RearTarget->Destroy(); DemoHitZoneTest::Ammo(Player,TEXT("normal"));
        if (!Check(Equipment->EquipSlot(1),TEXT("equip existing rifle for independent identity regression"))) return;
        DemoHitZoneTest::Fire(Player);
        if (!Check(DemoProjectileFixtures::CountLive(GetWorld())==1,TEXT("real primary shot creates one rifle projectile"))) return;
        for (TActorIterator<ADemoProjectileBase> It(GetWorld());It;++It) // 原手枪弹都已结束，当前只有真实步枪发射的一颗。
            if (!Check(It->GetShotContext()&&It->GetShotContext()->GetWeaponId()==TEXT("rifle"),TEXT("actual primary projectile keeps rifle identity independent of pistol slot"))) return;
        ADemoProjectileBase::CancelAll(GetWorld()); // 此步骤只测身份，显式撤销未运动的步枪弹，隔离下一步真实阶段取消断言。
        if (!Check(Equipment->EquipSlot(2),TEXT("restore pistol after isolated rifle identity test"))) return;
        Advance(.3f); break;
    case 10:
        ProjectileDefaults->Config.InitialSpeed=60000.f; Before=Target->GetHealth(); // 高速扫掠独立于前面1000cm/s的可观察飞行夹具。
        DemoHitZoneTest::Fire(Player);
        if (!Check(DemoProjectileFixtures::CountLive(GetWorld())==1,TEXT("high-speed real projectile launches before obstacle insertion"))) return;
        Wall=SpawnWall(Player,500.f); // 射线已结束，在首个运动Tick前加入2cm薄墙，必须由连续扫掠阻挡。
        Advance(.15f); break;
    case 11:
        if (!Check(Target->GetHealth()==Before&&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("60000 cm per second sweep cannot tunnel through post-launch 2 cm wall"))) return;
        Wall->Destroy(); ProjectileDefaults->Config.InitialSpeed=1000.f; // 下一步重新留出长飞行窗口验证阶段清理。
        Advance(.3f); break;
    case 12:
        Before=Target->GetHealth(); DemoHitZoneTest::Fire(Player);
        if (!Check(DemoProjectileFixtures::CountLive(GetWorld())==1,TEXT("live bullet exists before phase cancellation"))) return;
        Mode->NotifyPlayerDied(); // 正式离开Combat入口应立即取消在飞弹，不能只等到寿命结束。
        if (!Check(State->Phase==EDemoPhase::Defeat&&DemoProjectileFixtures::CountLive(GetWorld())==0,TEXT("leaving combat immediately cancels all player projectiles"))) return;
        Advance(1.5f); break;
    case 13:
        if (!Check(Target.IsValid()&&Target->GetHealth()==Before,TEXT("cancelled shot has no delayed damage after defeat"))) return;
        RestoreProjectileDefaults(); Target->Destroy();
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_PROJECTILE_TEST_SUCCESS")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
}

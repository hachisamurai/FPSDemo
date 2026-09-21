#include "AI/DemoCloseCombat.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyNavigation.h"
#include "Animation/DemoEnemyPresentation.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Components/StaticMeshComponent.h"
#include "NavigationSystem.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Debug/DemoLog.h"

bool FDemoCloseCombatSettings::IsValid() const
{
    UE_LOG(LogFPSDemo,Log,TEXT("%hs"),__FUNCTION__);
    return FMath::IsFinite(MeleeWindup)&&MeleeWindup>=.2f&&MeleeWindup<=2
        &&FMath::IsFinite(MeleeReach)&&MeleeReach>=150&&MeleeReach<=260
        &&FMath::IsFinite(MeleeHalfAngle)&&MeleeHalfAngle>=20&&MeleeHalfAngle<=90
        &&FMath::IsFinite(Recovery)&&Recovery>=.2f&&Recovery<=2
        &&FMath::IsFinite(ApproachMultiplier)&&ApproachMultiplier>=1&&ApproachMultiplier<=2.5f
        &&FMath::IsFinite(ChargeWindup)&&ChargeWindup>=.3f&&ChargeWindup<=2
        &&FMath::IsFinite(ChargeMinRange)&&ChargeMinRange>=250&&ChargeMinRange<=600
        &&FMath::IsFinite(ChargeMaxRange)&&ChargeMaxRange>ChargeMinRange&&ChargeMaxRange<=1800
        &&FMath::IsFinite(ChargeSpeed)&&ChargeSpeed>=600&&ChargeSpeed<=2500
        &&FMath::IsFinite(ChargeDistance)&&ChargeDistance>=400&&ChargeDistance>=ChargeMaxRange&&ChargeDistance<=2000
        &&FMath::IsFinite(ChargeCooldown)&&ChargeCooldown>=1&&ChargeCooldown<=20
        &&FMath::IsFinite(ChargeDamageMultiplier)&&ChargeDamageMultiplier>=.5f&&ChargeDamageMultiplier<=3;
}
UDemoCloseCombat::UDemoCloseCombat()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere")); // 已有球体只用于预警外壳。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube")); // 100cm立方体压扁形成地面带。
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/EnemyAttacks/M_EnemyGlow")); // Cooker硬引用既有Editor材质。
    SphereAsset=Sphere.Object; CubeAsset=Cube.Object; GlowAsset=Glow.Object;
}
void UDemoCloseCombat::CreateTelegraph()
{
    DEMO_LOG_CALL(); if (Shell) return;
    Shell=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("CloseCombatWindup"));
    Shell->SetupAttachment(GetOwner()->GetRootComponent()); Shell->SetStaticMesh(SphereAsset); Shell->SetMaterial(0,GlowAsset);
    Shell->SetRelativeScale3D(FVector(1.45f)); Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shell->SetCanEverAffectNavigation(false); Shell->CastShadow=false; Shell->SetHiddenInGame(true); Shell->RegisterComponent();
    Lane=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("ChargeLane"));
    Lane->SetupAttachment(GetOwner()->GetRootComponent()); Lane->SetAbsolute(true,true,true); Lane->SetStaticMesh(CubeAsset); Lane->SetMaterial(0,GlowAsset);
    Lane->SetCollisionEnabled(ECollisionEnabled::NoCollision); Lane->SetCanEverAffectNavigation(false); Lane->CastShadow=false; Lane->SetHiddenInGame(true); Lane->RegisterComponent();
}
void UDemoCloseCombat::Configure(const FDemoCloseCombatSettings& Settings,bool bEnabled,bool bCharger,float MeleeInterval,float OpeningDelay)
{
    DEMO_LOG_CALL();
    bActive=bEnabled&&Settings.IsValid(); bChargeRole=bCharger;
    Config=bActive?Settings:FDemoCloseCombatSettings(); MeleeCooldown=FMath::Max(.2f,MeleeInterval);
    NextMelee=NextCharge=GetWorld()->GetTimeSeconds()+2.f+FMath::Max(0.f,OpeningDelay);
    if (bEnabled&&!bActive) UE_LOG(LogFPSDemo,Warning,TEXT("CLOSE_CONFIG rejected invalid settings"));
    if (bActive) CreateTelegraph();
    UE_LOG(LogFPSDemo,Log,TEXT("CLOSE_CONFIG actor=%s active=%d charger=%d"),*GetNameSafe(GetOwner()),bActive,bChargeRole);
}
bool UDemoCloseCombat::IsEnabled() const { DEMO_LOG_TICK(); return bActive; }
EDemoClosePhase UDemoCloseCombat::GetPhase() const { DEMO_LOG_TICK(); return Phase; }
bool UDemoCloseCombat::HasLockedFacing() const { DEMO_LOG_TICK(); return bActive&&Phase!=EDemoClosePhase::Idle; }
FVector UDemoCloseCombat::GetLockedDirection() const { DEMO_LOG_TICK(); return Direction; }
void UDemoCloseCombat::SetPhase(EDemoClosePhase Next)
{
    DEMO_LOG_CALL(); Phase=Next; PhaseStart=GetWorld()->GetTimeSeconds();
    if (Shell) Shell->SetHiddenInGame(Phase!=EDemoClosePhase::MeleeWindup&&Phase!=EDemoClosePhase::ChargeWindup);
    if (Lane) Lane->SetHiddenInGame(Phase!=EDemoClosePhase::ChargeWindup);
    UE_LOG(LogFPSDemo,Log,TEXT("CLOSE_PHASE actor=%s phase=%d"),*GetNameSafe(GetOwner()),static_cast<int32>(Phase));
}
void UDemoCloseCombat::Cancel(FName Reason)
{
    DEMO_LOG_TICK(); if (Phase==EDemoClosePhase::Idle) return;
    UE_LOG(LogFPSDemo,Log,TEXT("CLOSE_CANCEL actor=%s reason=%s"),*GetNameSafe(GetOwner()),*Reason.ToString());
    NextMelee=GetWorld()->GetTimeSeconds()+MeleeCooldown; NextCharge=GetWorld()->GetTimeSeconds()+Config.ChargeCooldown;
    DistanceLeft=0; SetPhase(EDemoClosePhase::Idle);
    if (UDemoEnemyPresentation* Presentation=GetOwner()->FindComponentByClass<UDemoEnemyPresentation>()) Presentation->Cancel(); // 仅撤销表现GA，不影响其他玩法技能。
}
void UDemoCloseCombat::Damage(ADemoCharacter* Player,bool bCharge)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 当前权威敌人，状态已先切Recovery防GE死亡回调重入。
    if (!Enemy||!Player||!Player->GetDemoAttributes()||Player->GetDemoAttributes()->GetHealth()<=0) return;
    if (bCharge&&Player->IsDashEvading()) { UE_LOG(LogFPSDemo,Log,TEXT("CHARGE_EVADED dash window")); return; } // 只给突进碰撞躲避，不把普通近战改为通用免伤。
    const float Amount=Enemy->GetAttackPower()*(bCharge?Config.ChargeDamageMultiplier:1.f); // 攻击基值已含关卡/难度，不能重复乘算。
    DemoEffects::Apply(Enemy->GetAbilitySystemComponent(),Player->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),-Amount);
    UE_LOG(LogFPSDemo,Log,TEXT("CLOSE_HIT charge=%d amount=%.2f"),bCharge,Amount);
}
bool UDemoCloseCombat::Update(ADemoCharacter* Player,float Speed,float DeltaSeconds)
{
    DEMO_LOG_TICK(); if (!bActive) return false;
    ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // Actor拥有本组件，同步借用。
    UDemoEnemyNavigation* Nav=Enemy?Enemy->FindComponentByClass<UDemoEnemyNavigation>():nullptr; // 统一导航，不在组件重复直接追击。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // World阶段是所有攻击的最终约束。
    if (!Enemy||!Enemy->HasAuthority()||!Enemy->IsAlive()||!Nav||!State||State->Phase!=EDemoPhase::Combat||!Player||!Player->GetDemoAttributes()||Player->GetDemoAttributes()->GetHealth()<=0)
    { Cancel(TEXT("InvalidContext")); if (Nav) Nav->Stop(TEXT("CloseInactive")); return true; }
    if (Enemy->GetAbilitySystemComponent()->HasMatchingGameplayTag(DemoAmmoTags::Frozen))
    { Cancel(TEXT("Frozen")); Nav->Stop(TEXT("CloseFrozen")); return true; } // 新攻击可被冻结打断，不留解冻后的旧命中。
    const float Now=GetWorld()->GetTimeSeconds(); // World秒，暂停不会积攒突进距离或完成前摇。
    const FVector ToPlayer=Player->GetActorLocation()-Enemy->GetActorLocation(); // 本帧仅用于距离/可见性，起手方向单独锁定。
    const float Distance=ToPlayer.Size2D(); // 水平起手范围cm。
    if (Phase==EDemoClosePhase::Idle)
    {
        if (Distance>4500) { Nav->Stop(TEXT("OutOfRange")); return true; }
        if (Nav->CanSee(Player)&&ToPlayer.Size()<=Config.MeleeReach&&Now>=NextMelee)
        {
            Direction=ToPlayer.GetSafeNormal2D(); Nav->Stop(TEXT("MeleeWindup")); SetPhase(EDemoClosePhase::MeleeWindup);
            return true;
        }
        if (bChargeRole&&Distance>=Config.ChargeMinRange&&Distance<=Config.ChargeMaxRange&&FMath::Abs(ToPlayer.Z)<150&&Now>=NextCharge&&Nav->CanSee(Player))
        {
            Direction=ToPlayer.GetSafeNormal2D(); DistanceLeft=Config.ChargeDistance; Nav->Stop(TEXT("ChargeWindup"));
            Lane->SetWorldLocation(Enemy->GetActorLocation()+Direction*Config.ChargeDistance*.5f-FVector(0,0,100));
            Lane->SetWorldRotation(Direction.Rotation()); Lane->SetWorldScale3D(FVector(Config.ChargeDistance/100.f,.96f,.04f));
            SetPhase(EDemoClosePhase::ChargeWindup); return true;
        }
        const float ApproachRange=bChargeRole&&Distance>=Config.ChargeMinRange?Config.ChargeMinRange+100.f:130.f; // 冲刺兵保留起跑距离，避免出生缓冲期间走到贴脸而永远只挥击。
        Nav->Follow(Player,ApproachRange,Speed*Config.ApproachMultiplier,DeltaSeconds); return true;
    }
    Nav->Stop(TEXT("CloseAttack")); // 前摇/恢复不移动，Charging由下面唯一Sweep控制。
    if (Phase==EDemoClosePhase::MeleeWindup&&Now-PhaseStart>=Config.MeleeWindup)
    {
        SetPhase(EDemoClosePhase::Recovery); // GE可能导致结束关卡，必须先消费攻击状态。
        if (UDemoEnemyPresentation* Presentation=Enemy->FindComponentByClass<UDemoEnemyPresentation>()) Presentation->Play(TEXT("Melee"),Config.Recovery); // 既有动作为撞击后回收，放在结算时播放而非伪装完整前摇动画。
        if (ToPlayer.Size()<=Config.MeleeReach&&FVector::DotProduct(Direction,ToPlayer.GetSafeNormal2D())>=FMath::Cos(FMath::DegreesToRadians(Config.MeleeHalfAngle))&&Nav->CanSee(Player)) Damage(Player,false);
        else UE_LOG(LogFPSDemo,Log,TEXT("MELEE_MISS distance/arc/sight"));
    }
    else if (Phase==EDemoClosePhase::ChargeWindup&&Now-PhaseStart>=Config.ChargeWindup) SetPhase(EDemoClosePhase::Charging);
    else if (Phase==EDemoClosePhase::Charging)
    {
        const float Slow=Enemy->GetAbilitySystemComponent()->GetNumericAttribute(UDemoAttributeSet::GetMoveSpeedMultiplierAttribute()); // 实时GAS倍率，冰霜不能只影响普通追击。
        const float Travel=FMath::Min(DistanceLeft,Config.ChargeSpeed*Slow*DeltaSeconds); // cm，Sweep防高速穿墙，一次最大行程有界。
        const FVector Destination=Enemy->GetActorLocation()+Direction*Travel; // 不再读玩家位置改变方向。
        UNavigationSystemV1* Navigation=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); // 保守代理仍约束地图边界。
        FNavLocation Ground; // 小范围投影拒绝冲出悬崖/导航边界；不将投影结果当作穿墙许可。
        if (!Navigation||!Navigation->ProjectPointToNavigation(Destination,Ground,FVector(80,80,180))||FVector::Dist2D(Ground.Location,Destination)>80||FMath::Abs(Destination.Z-Ground.Location.Z-100)>80)
        { UE_LOG(LogFPSDemo,Log,TEXT("CHARGE_STOP navigation edge")); SetPhase(EDemoClosePhase::Recovery); return true; }
        FHitResult Hit; // 真实球体根扫掠，墙/其他怪物同样终止，不造成友伤。
        Enemy->AddActorWorldOffset(Direction*Travel,true,&Hit); DistanceLeft-=Travel;
        if (Hit.bBlockingHit)
        {
            SetPhase(EDemoClosePhase::Recovery);
            if (Hit.GetActor()==Player) Damage(Player,true);
            else UE_LOG(LogFPSDemo,Log,TEXT("CHARGE_STOP obstacle=%s"),*GetNameSafe(Hit.GetActor()));
        }
        else if (DistanceLeft<=KINDA_SMALL_NUMBER) SetPhase(EDemoClosePhase::Recovery);
    }
    else if (Phase==EDemoClosePhase::Recovery&&Now-PhaseStart>=Config.Recovery)
    { NextMelee=Now+MeleeCooldown; NextCharge=FMath::Max(NextCharge,Now+Config.ChargeCooldown); SetPhase(EDemoClosePhase::Idle); }
    return true;
}

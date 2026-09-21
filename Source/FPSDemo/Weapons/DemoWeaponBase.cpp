#include "Weapons/DemoWeaponBase.h"
#include "Animation/DemoWeaponAnimationComponent.h"
// 对应头文件先于依赖，保证UE独立编译能检查本类声明自包含。
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
#include "Combat/DemoEnemyHitZones.h"
#include "AbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "Audio/DemoWeaponAudio.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Sound/SoundBase.h"
#include "Particles/ParticleSystem.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

namespace
{
    /** 一枪对一个敌人的聚合结果；只活到当前同步开火返回，不保存跨帧敌人裸指针。 */
    struct FDemoWeaponHitAggregate
    {
        float TotalDamage = 0.f; // 每颗弹已经应用区域倍率后累加的生命点数。
        float RepresentativeDamage = 0.f; // 选取最重一颗的真实Hit供GE Context/命中反馈，平局保留较早弹丸。
        FHitResult RepresentativeHit; // 保留真实BoneName、ImpactPoint及组件弱引用，不伪造统一机身命中。
        EDemoEnemyHitRegion RepresentativeRegion = EDemoEnemyHitRegion::Body; // 对应代表弹丸，用于聚合审计而非重新乘倍率。
        int32 PelletHits = 0; // 当前枪对该敌人真正贡献正伤害的弹丸数，元素叠层仍固定一次。
    };

    /** Profile为本枪已验证配置；Hit为真实查询，BaseDamage为尚未乘区域的HP，Shot/Pellet仅用于审计。
     * bSecondary区分穿透段；Targets为本枪同步聚合容器。有效零倍率仍返回true供穿透继续，但不建立GE项。 */
    bool AccumulateEnemyPellet(const UDemoEnemyHitProfile& Profile, const FHitResult& Hit, float BaseDamage,
        int32 Shot, int32 Pellet, bool bSecondary, TMap<ADemoEnemy*, FDemoWeaponHitAggregate>& Targets)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs shot=%d pellet=%d secondary=%d"), __FUNCTION__, Shot, Pellet, bSecondary);
        ADemoEnemy* Enemy = Cast<ADemoEnemy>(Hit.GetActor()); // 仅本次调用借用；调用者随后统一施加GE才可能触发死亡。
        EDemoEnemyHitRegion Region = EDemoEnemyHitRegion::Body; // ResolveHit成功后才用于日志或聚合，不作为缺骨回退。
        float Multiplier = 0.f; // 精确骨名返回的无单位倍率，失败维持零。
        if (!Enemy || !Enemy->IsAlive() || !FMath::IsFinite(BaseDamage) || BaseDamage < 0.f
            || !Profile.ResolveHit(Hit, Region, Multiplier))
        {
            UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_HIT_REJECT shot=%d pellet=%d target=%s bone=%s base=%.3f secondary=%d"),
                Shot, Pellet, *GetNameSafe(Enemy), *Hit.BoneName.ToString(), BaseDamage, bSecondary);
            return false;
        }
        const float FinalDamage = BaseDamage * Multiplier; // 区域只在这里乘一次；后敌传入基数不携带首敌部位倍率。
        UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_HIT_ZONE shot=%d pellet=%d target=%s bone=%s region=%s base=%.3f multiplier=%.3f final=%.3f secondary=%d"),
            Shot, Pellet, *Enemy->GetName(), *Hit.BoneName.ToString(), DemoEnemyHitZones::RegionName(Region), BaseDamage, Multiplier, FinalDamage, bSecondary);
        if (!FMath::IsFinite(FinalDamage))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("WEAPON_HIT_REJECT non-finite multiplied damage"));
            return false;
        }
        if (FinalDamage <= 0.f) return true; // 明确免疫部位不刷新命中时间、不叠元素、不发零伤害GE。
        FDemoWeaponHitAggregate& Aggregate = Targets.FindOrAdd(Enemy); // 仅当前枪同步有效的聚合记录。
        Aggregate.TotalDamage += FinalDamage;
        ++Aggregate.PelletHits;
        if (FinalDamage > Aggregate.RepresentativeDamage)
        {
            Aggregate.RepresentativeDamage = FinalDamage;
            Aggregate.RepresentativeHit = Hit;
            Aggregate.RepresentativeRegion = Region;
        }
        return true;
    }
}

ADemoWeaponBase::ADemoWeaponBase()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false;
    WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    SetRootComponent(WeaponMesh);
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetOnlyOwnerSee(true);
    WeaponMesh->CastShadow = false;
    // 保留已序列化的 WeaponMesh 根，静态枪共享原有挂接、隐藏与镜内隐藏流程。
    StaticWeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticWeaponMesh"));
    StaticWeaponMesh->SetupAttachment(WeaponMesh);
    StaticWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StaticWeaponMesh->SetOnlyOwnerSee(true);
    StaticWeaponMesh->CastShadow = false;
    // 原生默认资源由CDO保活，四种蓝图可分别替换；不在Tick加载资源。
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Gun(TEXT("/Game/FPWeapon/Mesh/SK_FPGun"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(TEXT("/Game/FirstPersonArms/Animations/FP_Rifle_Idle"));
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Fire(TEXT("/Game/FirstPersonArms/Animations/FP_Rifle_Fire"));
    static ConstructorHelpers::FObjectFinder<USoundBase> Sound(TEXT("/Game/Audio/SFX/Weapons/Sword/Fire/Cues/SC_Sword_Fire"));
    Config.Mesh = Gun.Object;
    Config.IdleAnimation = Idle.Object;
    Config.FireAnimation = Fire.Object;
    Config.FireSound = Sound.Object;
    Config.DisplayName = FText::FromString(TEXT("步枪"));
    // CDO硬引用让打包自动收集配置；缺资源时在开火入口明确日志并使用内置完整骨名白名单。
    static ConstructorHelpers::FObjectFinder<UDemoEnemyHitProfile> HitProfile(TEXT("/Game/Data/Combat/DA_EnemyHitZones.DA_EnemyHitZones"));
    EnemyHitProfile = HitProfile.Object;
}
void ADemoWeaponBase::OnConstruction(const FTransform& Transform)
{
    DEMO_LOG_CALL();
    Super::OnConstruction(Transform);
    ApplyVisualMesh();
}
void ADemoWeaponBase::ApplyVisualMesh()
{
    DEMO_LOG_CALL();
    // 不改共享资产/CDO，只选择当前实例的渲染组件；旧蓝图未配置静态枪时保持原效果。
    WeaponMesh->SetSkeletalMesh(Config.StaticMesh ? nullptr : Config.Mesh.Get());
    // 稳定挂点与枪械零件在OwnerNoSee/开镜/NullRHI恢复后仍需真实新骨姿势；单人四个小网格开销可控。
    WeaponMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    StaticWeaponMesh->SetStaticMesh(Config.StaticMesh);
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_VISUAL %s static=%s skeletal=%s"), *GetName(), *GetNameSafe(Config.StaticMesh), *GetNameSafe(WeaponMesh->GetSkeletalMeshAsset()));
}
void ADemoWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    // 外部销毁武器也必须清理其表现，不能只依赖Pawn正常CancelActions顺序。
    if (Wielder.IsValid() && Wielder->GetWeaponAnimationComponent()) Wielder->GetWeaponAnimationComponent()->EndReloadPresentation(this, ReloadSequence, false);
    GetWorldTimerManager().ClearTimer(PoseTimer);
    Wielder.Reset();
    Super::EndPlay(EndPlayReason);
}
bool ADemoWeaponBase::InitializeForOwner(ADemoCharacter* Character)
{
    DEMO_LOG_CALL();
    FString Error; // 本次配置错误，不缓存BP定义指针到外部。
    if (!HasAuthority() || bInitialized || !Character || !Config.Validate(Error)
        || !Character->GetMesh1P()->DoesSocketExist(Config.AttachSocket))
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_INIT_REJECTED %s config=%s socket=%s"), *GetName(), *Error, *Config.AttachSocket.ToString());
        return false;
    }
    Wielder = Character;
    SetOwner(Character);
    ApplyVisualMesh();
    AttachToComponent(Character->GetMesh1P(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Config.AttachSocket);
    SetActorRelativeTransform(Config.AttachOffset);
    Ammo = FMath::Clamp(Config.InitialAmmo, 0, GetCapacity());
    ReserveAmmo = Config.InitialReserve;
    bInitialized = true;
    SetActorHiddenInGame(true);
    // 枪口必须查当前使用的外观，不能误用隐藏的旧骨骼枪挂点。
    const USceneComponent* Visual = Config.StaticMesh ? static_cast<USceneComponent*>(StaticWeaponMesh) : WeaponMesh.Get();
    if (!Visual->DoesSocketExist(Config.MuzzleSocket)) UE_LOG(LogFPSDemo, Warning, TEXT("Weapon %s uses camera-offset muzzle fallback: missing %s"), *GetName(), *Config.MuzzleSocket.ToString());
    return true;
}
void ADemoWeaponBase::SetEquipped(bool bEquipped)
{
    DEMO_LOG_CALL();
    bIsEquipped = bEquipped;
    SetActorHiddenInGame(!bEquipped);
    if (!bEquipped) { CancelReload(); GetWorldTimerManager().ClearTimer(PoseTimer); bShotPaid = false; }
    else
    {
        RestoreIdle();
        // 只由新装备的武器设置共享手臂表现；隐藏库存不反向修改当前武器的手部状态。
        if (Wielder.IsValid())
        {
            USkeletalMeshComponent* Arms = Wielder->GetMesh1P(); // 当前 Pawn 拥有，仅本次同步借用。
            if (Arms->GetBoneIndex(TEXT("upperarm_l")) != INDEX_NONE)
            {
                if (Config.bHideSupportArm) Arms->HideBoneByName(TEXT("upperarm_l"), PBO_None);
                else Arms->UnHideBoneByName(TEXT("upperarm_l"));
                UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_SUPPORT_ARM hidden=%d"), Config.bHideSupportArm);
            }
            else UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_SUPPORT_ARM missing upperarm_l; keeping current pose"));
        }
        else UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_SUPPORT_ARM no wielder; visual deferred"));
    }
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_EQUIPPED %s active=%d ammo=%d cooldown=%.2f"), *GetName(), bEquipped, Ammo, GetFireCooldownRemaining());
    OnWeaponEquipped(bEquipped); // C++入口已记录，再派发允许蓝图扩展的表现事件。
}
void ADemoWeaponBase::SetScopedVisual(bool bScoped) { DEMO_LOG_CALL(); SetActorHiddenInGame(!bIsEquipped || bScoped); }
int32 ADemoWeaponBase::GetAmmo() const { DEMO_LOG_TICK(); return Ammo; }
int32 ADemoWeaponBase::GetReserveAmmo() const { DEMO_LOG_TICK(); return ReserveAmmo; }
int32 ADemoWeaponBase::GetCapacity() const
{
    DEMO_LOG_TICK();
    // 属性只借用持有角色PS；未初始化时只有武器基础容量。
    const UDemoAttributeSet* Attributes = Wielder.IsValid() ? Wielder->GetDemoAttributes() : nullptr;
    return Config.MagazineCapacity + (Attributes ? FMath::Max(0,FMath::FloorToInt(Attributes->GetMagazineBonus())) : 0);
}
int32 ADemoWeaponBase::GetPelletCount() const { DEMO_LOG_TICK(); return 1; }
float ADemoWeaponBase::GetDamagePerPellet() const
{
    DEMO_LOG_TICK();
    // 玩家加成以整次触发为单位；霰弹均分，避免商店+5变成8颗各+5。
    const UDemoAttributeSet* Attributes = Wielder.IsValid() ? Wielder->GetDemoAttributes() : nullptr;
    return Config.BaseDamage + (Attributes ? Attributes->GetWeaponDamageBonus() / FMath::Max(1,GetPelletCount()) : 0.f);
}
bool ADemoWeaponBase::CanPayShotCost() const
{
    DEMO_LOG_TICK();
    return bInitialized && bIsEquipped && !bReloading && Ammo >= Config.AmmoPerShot && Wielder.IsValid()
        && Wielder->CanUseCombatAbilities() && Wielder->GetDemoAttributes() && Wielder->GetDemoAttributes()->GetHealth() > 0.f;
}
float ADemoWeaponBase::GetFireCooldownRemaining() const { DEMO_LOG_TICK(); return FMath::Max(0.f, NextFireTime - GetWorld()->GetTimeSeconds()); }
void ADemoWeaponBase::PayShotCost()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !CanPayShotCost()) { UE_LOG(LogFPSDemo, Warning, TEXT("Weapon cost rejected")); return; }
    Ammo -= Config.AmmoPerShot;
    bShotPaid = true;
}
void ADemoWeaponBase::CommitFireCooldown()
{
    DEMO_LOG_CALL();
    if (!HasAuthority()) return;
    NextFireTime = GetWorld()->GetTimeSeconds() + 60.f / Config.RoundsPerMinute;
}
void ADemoWeaponBase::ExecuteCommittedShot()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bShotPaid || !Wielder.IsValid() || !bIsEquipped)
    { UE_LOG(LogFPSDemo, Warning, TEXT("Unpaid/stale shot rejected")); return; }
    bShotPaid = false; // 在GE和蓝图事件之前消费许可，重入不能重复伤害。
    // 仅已支付且真实执行的射击参与整轮挑战；先落盘再产生命中，避免最后一枪提前结算。
    if (AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) // 开发武器测试无该GameMode时维持原行为。
        if (!Mode->RegisterWeaponShot(DemoWeaponCatalog::IdAt(Wielder->GetWeaponComponent()->GetActiveSlot()==2?0:Wielder->GetWeaponComponent()->GetPrimaryIndex())))
        { Ammo+=Config.AmmoPerShot; UE_LOG(LogFPSDemo,Warning,TEXT("Shot blocked: challenge checkpoint save failed; ammo refunded")); return; }
    ++ShotSequence;
    PerformBallistics();
    DemoWeaponAudio::PlayAtLocation(this, Config.FireSound, GetMuzzleLocation(), TEXT("Weapon.Fire"));
    if (Config.MuzzleEffect) UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Config.MuzzleEffect, GetMuzzleLocation(), Wielder->GetControlRotation());
    if (Config.WeaponAnimLayerClass && Wielder->GetWeaponAnimationComponent())
    {
        Wielder->GetWeaponAnimationComponent()->PlayFire(this); // 新四枪始终保留主图，不使用PlayAnimation覆盖AnimationMode。
    }
    else if (Config.FireAnimation)
    {
        Wielder->GetMesh1P()->PlayAnimation(Config.FireAnimation, false);
        GetWorldTimerManager().SetTimer(PoseTimer, this, &ADemoWeaponBase::RestoreIdle, FMath::Max(.1f,Config.FireAnimation->GetPlayLength()), false);
    }
    if (Config.RecoilPitch > 0.f) Wielder->AddControllerPitchInput(-Config.RecoilPitch);
    if (Config.bSupportsScope && Config.bUnscopeAfterShot) Wielder->GetWeaponComponent()->StopAim();
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_SHOT weapon=%s sequence=%d pellets=%d ammo=%d"), *GetName(), ShotSequence, GetPelletCount(), Ammo);
    OnWeaponShot(); // 表现事件不承担成本和命中，调用前统一有日志。
}
FVector ADemoWeaponBase::GetMuzzleLocation() const
{
    DEMO_LOG_TICK();
    // 静态枪口与实际画面保持一致；未配置静态资产的旧武器仍读取骨骼挂点。
    const USceneComponent* Visual = Config.StaticMesh ? static_cast<USceneComponent*>(StaticWeaponMesh) : WeaponMesh.Get();
    if (Visual->DoesSocketExist(Config.MuzzleSocket)) return Visual->GetSocketLocation(Config.MuzzleSocket);
    if (!Wielder.IsValid()) return GetActorLocation();
    const UCameraComponent* Camera = Wielder->GetFirstPersonCameraComponent(); // 本次借用相机，不跨任务保存。
    return Camera->GetComponentLocation() + Camera->GetForwardVector()*70.f + Camera->GetRightVector()*15.f - Camera->GetUpVector()*10.f;
}
void ADemoWeaponBase::PerformBallistics()
{
    DEMO_LOG_CALL();
    if (!Wielder.IsValid()) { UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_BALLISTICS_REJECT missing wielder")); return; }
    const UDemoEnemyHitProfile* HitProfile = DemoEnemyHitZones::SelectValidProfile(EnemyHitProfile); // 一枪校验一次，与敌人查询碰撞初始化使用同一回退策略。
    if (!HitProfile) { UE_LOG(LogFPSDemo, Error, TEXT("WEAPON_BALLISTICS_REJECT invalid native hit profile")); return; }
    // 相机、枪口防穿墙和真实弹道共用WeaponTrace；墙体保持Block，敌人移动球忽略而骨骼刚体阻挡。
    const UCameraComponent* Camera = Wielder->GetFirstPersonCameraComponent(); // 持有者拥有的相机，本枪同步借用。
    const FVector Start = Camera->GetComponentLocation(); // 世界厘米；所有距离衰减仍以瞄准起点计算。
    FVector Muzzle = GetMuzzleLocation(); // 当前枪口，可被相机到枪口的墙体阻挡回退。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoWeaponShot), false, Wielder.Get()); // 简单查询使用PhysicsAsset，忽略玩家及自身武器。
    Query.AddIgnoredActor(this);
    FHitResult MuzzleBlock; // 防止摄像机到枪口之间有墙，枪口偏移不能把起点放到墙后。
    if (GetWorld()->LineTraceSingleByChannel(MuzzleBlock, Start, Muzzle, DemoEnemyHitZones::TraceChannel, Query)) Muzzle = MuzzleBlock.ImpactPoint - (Muzzle-Start).GetSafeNormal()*2.f;
    // 按本次射击序号生成独立锥形方向；范围/升级在本次调用快照，不逐弹扣弹。
    FRandomStream Random(ShotSequence * 7919 + GetUniqueID()); // 本枪局部随机流，不消耗全局序列。
    const int32 Pellets = GetPelletCount(); // 已验证武器配置的弹丸数量，本枪只消费一次开火成本。
    const float Spread = Wielder->GetWeaponComponent()->GetScopeLevel() > 0 ? Config.ScopedSpreadHalfAngle : Config.SpreadHalfAngle; // 当前瞄准状态的锥半角，度。
    const UDemoAmmoComponent* AmmoType = Wielder->FindComponentByClass<UDemoAmmoComponent>(); // 一次开火快照，所有pellet共享。
    const UDemoAmmoCatalog* AmmoConfig = AmmoType ? AmmoType->GetCatalog() : GetDefault<UDemoAmmoCatalog>(); // 测试/旧Pawn缺组件仍允许普通射击。
    const int32 Type = AmmoType && AmmoConfig->Validate() ? AmmoType->GetSelected() : 0; // 错误配置回退普通弹，不改变基础射击。
    const float Damage = GetDamagePerPellet() * (Type==3?AmmoConfig->PiercingDamageMultiplier:1.f); // 穿透首段增伤只乘一次。
    TMap<ADemoEnemy*, FDemoWeaponHitAggregate> TargetDamage; // 仅同步借用目标；部位倍率在累加前计算，代表Hit用于GE Context。
    for (int32 Index = 0; Index < Pellets; ++Index) // 每颗弹独立碰撞，可击中不同敌人。
    {
        const FVector Direction = Random.VRandCone(Camera->GetForwardVector(), FMath::DegreesToRadians(Spread)); // 本颗世界单位方向。
        FHitResult AimHit; // 相机最近阻挡点，决定枪口实际射向。
        GetWorld()->LineTraceSingleByChannel(AimHit, Start, Start+Direction*Config.Range, DemoEnemyHitZones::TraceChannel, Query);
        const FVector AimEnd = AimHit.bBlockingHit ? AimHit.ImpactPoint : Start+Direction*Config.Range; // 不允许超出配置射程。
        FHitResult Hit; // 实际枪口到目标阻挡；小量延伸确保表面命中精度。
        GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, AimEnd+(AimEnd-Muzzle).GetSafeNormal()*2.f, DemoEnemyHitZones::TraceChannel, Query);
        ADemoEnemy* Enemy = Cast<ADemoEnemy>(Hit.GetActor()); // 当前弹丸目标，只攻击存活敌人。
        if (Enemy && Enemy->IsAlive())
        {
            const float Distance = FVector::Distance(Start,Hit.ImpactPoint); // 衰减统一采用瞄准起点距离cm。
            const float Alpha = Config.Range > Config.FalloffStart ? FMath::Clamp((Distance-Config.FalloffStart)/(Config.Range-Config.FalloffStart),0.f,1.f) : 0.f; // 原有线性距离衰减系数0..1。
            const float FirstBaseDamage = Damage * FMath::Lerp(1.f,Config.MinimumDamageMultiplier,Alpha); // 首目标理论基数，尚未乘部位；保持原穿透衰减来源。
            const bool bAcceptedHit = AccumulateEnemyPellet(*HitProfile, Hit, FirstBaseDamage, ShotSequence, Index, false, TargetDamage); // 无骨/错误组件拒绝伤害和继续穿透。
            if(Type==3 && bAcceptedHit)
            {
                const FVector Travel=(AimEnd-Muzzle).GetSafeNormal(); // 沿真实枪口轨迹继续，不被相机首个敌人AimEnd截断。
                FCollisionQueryParams ContinueQuery=Query; ContinueQuery.AddIgnoredActor(Enemy); // 忽略整只首敌的所有组件。
                FHitResult Behind; // 后方最近阻挡，墙体也会阻挡，绝不穿墙找敌人。
                GetWorld()->LineTraceSingleByChannel(Behind,Hit.ImpactPoint+Travel*2.f,Muzzle+Travel*Config.Range,DemoEnemyHitZones::TraceChannel,ContinueQuery);
                ADemoEnemy* Secondary=Cast<ADemoEnemy>(Behind.GetActor()); // 只额外命中一个活敌人。
                // 后敌沿用既有首命中距离衰减×穿透比例，但独立使用Behind.BoneName；首敌Core绝不提高后敌Arm伤害。
                if (Secondary && Secondary != Enemy && Secondary->IsAlive()
                    && AccumulateEnemyPellet(*HitProfile, Behind, FirstBaseDamage * AmmoConfig->SecondaryDamageRatio, ShotSequence, Index, true, TargetDamage))
                    UE_LOG(LogFPSDemo, Log, TEXT("AMMO_PIERCE shot=%d target=%s bone=%s"), ShotSequence, *Secondary->GetName(), *Behind.BoneName.ToString());
            }
        }
        DrawDebugLine(GetWorld(), Muzzle, Hit.bBlockingHit ? Hit.ImpactPoint : AimEnd, Enemy ? FColor::Green : FColor::Yellow, false, .07f, 0, 1.f);
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("PELLET shot=%d index=%d target=%s bone=%s"), ShotSequence, Index, *GetNameSafe(Enemy), *Hit.BoneName.ToString());
    }
    for (const TPair<ADemoEnemy*, FDemoWeaponHitAggregate>& Entry : TargetDamage) // 同一敌人一次GE/一次肉体反馈/一次元素叠层。
    {
        const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 最后一只怪死亡可同步切阶段，逐目标重查。
        if (!Entry.Key->IsAlive() || !State || State->Phase != EDemoPhase::Combat)
        {
            UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_DAMAGE_REJECT target=%s dead or no active combat"), *GetNameSafe(Entry.Key));
            continue;
        }
        UAbilitySystemComponent* SourceASC = Wielder->GetAbilitySystemComponent(); // 当前枪源ASC，仅游戏线程借用。
        UAbilitySystemComponent* TargetASC = Entry.Key->GetAbilitySystemComponent(); // 当前敌人ASC，死亡后不会再次叠层。
        if (!SourceASC || !TargetASC || !SourceASC->IsOwnerActorAuthoritative() || !TargetASC->IsOwnerActorAuthoritative()
            || !FMath::IsFinite(Entry.Value.TotalDamage) || Entry.Value.TotalDamage <= 0.f)
        {
            UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_DAMAGE_REJECT target=%s invalid ASC/authority/total"), *Entry.Key->GetName());
            continue;
        }
        const float Before = Entry.Key->GetHealth(); // 实际正伤害才叠层，零伤害/致死不施加新状态。
        FGameplayEffectContextHandle Context = SourceASC->MakeEffectContext(); // 每枪每敌的独立上下文保留代表性命中，不修改共享GE模板。
        Context.AddHitResult(Entry.Value.RepresentativeHit, true);
        FGameplayEffectSpecHandle Spec = SourceASC->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(), 1.f, Context); // 使用原Health GE和Magnitude协议扣生命。
        if (!Spec.IsValid()) { UE_LOG(LogFPSDemo, Error, TEXT("WEAPON_DAMAGE_REJECT missing health GE spec")); continue; }
        Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude, -Entry.Value.TotalDamage);
        SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
        if(Entry.Key->IsAlive()&&Entry.Key->GetHealth()<Before)
        {
            if (Type == 1 || Type == 2)
            {
                UDemoAmmoStatus* Status = Entry.Key->FindComponentByClass<UDemoAmmoStatus>(); // 敌人拥有的元素处理器，本枪同步借用。
                if (Status) Status->Apply(SourceASC, Type, AmmoConfig); // 聚合后每敌人每枪只叠一层；DOT/爆炸数值不乘部位倍率。
                else UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_AMMO_STATUS missing target=%s"), *Entry.Key->GetName());
            }
            if (Type == 3)
            {
                FGameplayCueParameters Cue; // 本地穿透表现保留最重弹丸的位置/法线及Context，供后续命中反馈使用。
                Cue.EffectContext = Context;
                Cue.Location = Entry.Value.RepresentativeHit.ImpactPoint;
                Cue.Normal = Entry.Value.RepresentativeHit.ImpactNormal;
                Cue.RawMagnitude = Entry.Value.TotalDamage;
                TargetASC->ExecuteGameplayCue(DemoAmmoTags::PiercingCue, Cue);
            }
        }
        Wielder->LastHitTime = GetWorld()->GetTimeSeconds();
        UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_DAMAGE target=%s total=%.2f pellets=%d representativeBone=%s representativeRegion=%s"),
            *Entry.Key->GetName(), Entry.Value.TotalDamage, Entry.Value.PelletHits, *Entry.Value.RepresentativeHit.BoneName.ToString(),
            DemoEnemyHitZones::RegionName(Entry.Value.RepresentativeRegion));
    }
}
bool ADemoWeaponBase::CanReload() const
{
    DEMO_LOG_TICK();
    return bInitialized && bIsEquipped && !bReloading && Ammo < GetCapacity() && (Config.bInfiniteReserve || ReserveAmmo > 0);
}
bool ADemoWeaponBase::BeginReload()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !CanReload()) { UE_LOG(LogFPSDemo, Log, TEXT("Reload rejected: full/no reserve/already reloading")); return false; }
    bReloading = true;
    ++ReloadSequence;
    ReloadDuration = Config.ReloadSeconds;
    bEmptyReload = Ammo == 0; // AmmoPerShot不足但尚有弹时选普通换弹，不引入隐式膛内弹规则。
    GetWorldTimerManager().ClearTimer(PoseTimer);
    if (!Config.WeaponAnimLayerClass && Wielder.IsValid() && Config.ReloadAnimation) Wielder->GetMesh1P()->PlayAnimation(Config.ReloadAnimation, false);
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_RELOAD_BEGIN %s seq=%d seconds=%.2f empty=%d"), *GetName(), ReloadSequence, ReloadDuration, bEmptyReload);
    return true;
}
bool ADemoWeaponBase::CompleteReload()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bReloading || !bIsEquipped || !Wielder.IsValid() || !Wielder->CanUseCombatAbilities()
        || !Wielder->GetDemoAttributes() || Wielder->GetDemoAttributes()->GetHealth() <= 0.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("Reload completion rejected: stale instance/state")); return false; }
    const int32 Transfer = Config.bInfiniteReserve ? GetCapacity()-Ammo : FMath::Min(GetCapacity()-Ammo, ReserveAmmo); // 唯一弹药转移点。
    Ammo += Transfer;
    if (!Config.bInfiniteReserve) ReserveAmmo -= Transfer;
    bReloading = false;
    if (Wielder.IsValid() && Wielder->GetWeaponAnimationComponent()) Wielder->GetWeaponAnimationComponent()->EndReloadPresentation(this, ReloadSequence, true);
    RestoreIdle();
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_RELOAD_COMPLETE %s seq=%d transferred=%d ammo=%d reserve=%d"), *GetName(), ReloadSequence, Transfer, Ammo, ReserveAmmo);
    return true;
}
void ADemoWeaponBase::CancelReload()
{
    DEMO_LOG_CALL();
    if (bReloading) UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_RELOAD_CANCELLED %s seq=%d unchanged ammo=%d"), *GetName(), ReloadSequence, Ammo);
    bReloading = false;
    if (Wielder.IsValid() && Wielder->GetWeaponAnimationComponent()) Wielder->GetWeaponAnimationComponent()->EndReloadPresentation(this, ReloadSequence, false);
    RestoreIdle();
}
bool ADemoWeaponBase::IsReloading() const { DEMO_LOG_TICK(); return bReloading; }
int32 ADemoWeaponBase::GetReloadSequence() const { DEMO_LOG_TICK(); return ReloadSequence; }
float ADemoWeaponBase::GetReloadDuration() const { DEMO_LOG_TICK(); return ReloadDuration; }
bool ADemoWeaponBase::IsEmptyReload() const { DEMO_LOG_TICK(); return bEmptyReload; }
USkeletalMeshComponent* ADemoWeaponBase::GetWeaponSkeletalMesh() const { DEMO_LOG_TICK(); return WeaponMesh; }
void ADemoWeaponBase::ModifyAmmo(int32 Delta)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bInitialized) return;
    Ammo = static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(Ammo)+Delta, 0, GetCapacity()));
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_AMMO %s delta=%d ammo=%d"), *GetName(), Delta, Ammo);
}
void ADemoWeaponBase::Refill()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bInitialized) return;
    Ammo = GetCapacity();
    ReserveAmmo = Config.InitialReserve;
}
void ADemoWeaponBase::RestoreIdle()
{
    DEMO_LOG_CALL();
    if (!Wielder.IsValid() || !bIsEquipped || bReloading) return;
    if (Config.WeaponAnimLayerClass && Wielder->GetWeaponAnimationComponent())
        Wielder->GetWeaponAnimationComponent()->RestoreWeaponPose(this); // 新图持续评估IdlePose，只恢复临时手臂状态。
    else if (Config.IdleAnimation) Wielder->GetMesh1P()->PlayAnimation(Config.IdleAnimation, true);
}
ADemoShotgunWeapon::ADemoShotgunWeapon()
{
    DEMO_LOG_CALL();
    Config.DisplayName = FText::FromString(TEXT("散弹枪"));
    Config.FireMode = EDemoFireMode::SemiAutomatic;
    Config.RoundsPerMinute = 75.f;
    Config.BaseDamage = 8.f;
    Config.MagazineCapacity = Config.InitialAmmo = 6;
    Config.ReloadSeconds = 2.2f;
    Config.SpreadHalfAngle = 6.f;
    Config.Range = 3000.f;
    Config.FalloffStart = 600.f;
    Config.MinimumDamageMultiplier = .2f;
}
int32 ADemoShotgunWeapon::GetPelletCount() const { DEMO_LOG_TICK(); return Config.PelletCount; }
ADemoSniperWeapon::ADemoSniperWeapon()
{
    DEMO_LOG_CALL();
    Config.DisplayName = FText::FromString(TEXT("狙击枪"));
    Config.FireMode = EDemoFireMode::SemiAutomatic;
    Config.RoundsPerMinute = 45.f;
    Config.BaseDamage = 90.f;
    Config.MagazineCapacity = Config.InitialAmmo = 5;
    Config.ReloadSeconds = 2.5f;
    Config.SpreadHalfAngle = 3.f;
    Config.Range = Config.FalloffStart = 20000.f;
    Config.bSupportsScope = true;
}

void ADemoWeaponBase::RestoreAmmo(int32 SavedAmmo, int32 SavedReserve)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bInitialized) { UE_LOG(LogFPSDemo, Warning, TEXT("RestoreAmmo rejected: initialization/authority")); return; }
    Ammo = FMath::Clamp(SavedAmmo, 0, GetCapacity());
    ReserveAmmo = FMath::Clamp(SavedReserve, 0, 100000);
}

#include "Weapons/DemoWeaponBase.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "GAS/Ammo/DemoAmmoEffectSnapshot.h"
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
    // 原生默认实体子弹支持旧BP；Editor迁移再指定四种数据子类。
    Config.ProjectileClass = ADemoProjectileBase::StaticClass();
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
    CancelPreparedShot(); // 只撤销待发弹，已飞行Actor不归武器销毁。
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
    if (!bEquipped) { CancelPreparedShot(); CancelReload(); GetWorldTimerManager().ClearTimer(PoseTimer); }
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
bool ADemoWeaponBase::IsPreparedShotValid() const
{
    DEMO_LOG_TICK();
    if (!HasAuthority() || !PreparedContext || !PreparedContext->IsAttackValid() || !Wielder.IsValid()
        || !Wielder->CanUseCombatAbilities() || !Wielder->GetWeaponComponent()->IsEquipped(this)
        || !bIsEquipped || bReloading || PreparedProjectiles.Num() != GetPelletCount()) return false;
    for (const ADemoProjectileBase* Projectile : PreparedProjectiles) // World持有Actor；构造回调可能使其中任何一个失效。
        if (!IsValid(Projectile) || !Projectile->IsPrepared()) return false;
    return true;
}
void ADemoWeaponBase::CancelPreparedShot()
{
    DEMO_LOG_CALL();
    // 先清凭据再销毁Actor，EndPlay/蓝图回调重入也不能重复退款。
    const bool bRefund = bShotPaid; // 仅退本次实际支付过的成本，准备失败不凭空增加弹药。
    bShotPaid = false;
    if (bRefund) Ammo = FMath::Clamp(Ammo + PreparedAmmoCost, 0, GetCapacity());
    if (bPreparedCooldownApplied) NextFireTime = PreparedPreviousFireTime;
    bPreparedCooldownApplied = false;
    PreparedAmmoCost = 0;
    PreparedContext = nullptr;
    TArray<TObjectPtr<ADemoProjectileBase>> Cancelled = MoveTemp(PreparedProjectiles); // 本栈列表避免回调修改正在遍历的成员。
    for (ADemoProjectileBase* Projectile : Cancelled) // 取消幂等且不结算伤害；Destroy标记延迟释放。
        if (IsValid(Projectile)) Projectile->CancelProjectile();
    UE_LOG(LogFPSDemo, Log, TEXT("SHOT_PREPARATION_CANCEL weapon=%s refund=%d"), *GetName(), bRefund);
}
bool ADemoWeaponBase::PrepareShot()
{
    DEMO_LOG_CALL();
    // 蓝图构造前锁住入口；构造回调若换枪或换关，末尾校验撤销全组。
    if (bPreparingShot || PreparedContext || !HasAuthority() || !CanPayShotCost()
        || GetFireCooldownRemaining() > KINDA_SMALL_NUMBER || !Wielder->GetWeaponComponent()->IsEquipped(this))
    { UE_LOG(LogFPSDemo, Log, TEXT("SHOT_PREPARE_REJECT ownership/cost/cooldown/reentry")); return false; }
    TGuardValue<bool> PreparingGuard(bPreparingShot, true); // 同步RAII标记，生命周期限本次调用。
    FString Error; // 武器或子弹的非法字段诊断，不回退射线伤害。
    const ADemoProjectileBase* Defaults = Config.ProjectileClass ? Config.ProjectileClass->GetDefaultObject<ADemoProjectileBase>() : nullptr; // CDO只读提供真实碰撞半径。
    const UDemoEnemyHitProfile* HitProfile = DemoEnemyHitZones::SelectValidProfile(EnemyHitProfile); // 部位配置稍后复制进Context。
    if (!Config.Validate(Error) || !Defaults || !Defaults->Config.Validate(Error) || !HitProfile
        || Defaults->Config.InitialSpeed * Defaults->Config.MaxLifeSeconds < Config.Range)
    { UE_LOG(LogFPSDemo, Warning, TEXT("SHOT_PREPARE_REJECT configuration/lifetime %s"), *Error); return false; }
    const UCameraComponent* Camera = Wielder->GetFirstPersonCameraComponent(); // 准备期间借用相机，不跨帧存裸指针。
    const FVector Start = Camera->GetComponentLocation(); // 世界cm，瞄准和衰减原点。
    FVector Muzzle = GetMuzzleLocation(); // 安全短扫掠可以收回枪口，不造成伤害。
    const float Radius = Defaults->Config.CollisionRadius; // cm，必须与实际子弹根球相同。
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoPrepareProjectile), false, Wielder.Get()); // 简单查询实际PhysicsAsset。
    Query.AddIgnoredActor(this);
    FHitResult MuzzleBlock; // 相机到枪口的球体路径，防止从墙后创建子弹。
    if (GetWorld()->SweepSingleByChannel(MuzzleBlock, Start, Muzzle, FQuat::Identity, ECC_GameTraceChannel3, FCollisionShape::MakeSphere(Radius), Query))
    {
        if (MuzzleBlock.bStartPenetrating) { UE_LOG(LogFPSDemo, Log, TEXT("SHOT_PREPARE_REJECT camera penetrating obstacle")); return false; }
        Muzzle = MuzzleBlock.Location + MuzzleBlock.Normal * .2f;
    }
    if (GetWorld()->OverlapBlockingTestByChannel(Muzzle, FQuat::Identity, ECC_GameTraceChannel3, FCollisionShape::MakeSphere(Radius), Query))
    { UE_LOG(LogFPSDemo, Log, TEXT("SHOT_PREPARE_REJECT no safe muzzle")); return false; }
    const UDemoAmmoComponent* AmmoComponent = Wielder->FindComponentByClass<UDemoAmmoComponent>(); // 弹药类型只读一次。
    const UDemoAmmoCatalog* Catalog = AmmoComponent ? AmmoComponent->GetCatalog() : GetDefault<UDemoAmmoCatalog>(); // 兼容旧Pawn缺组件。
    const int32 AmmoType = Catalog && Catalog->Validate() && AmmoComponent ? AmmoComponent->GetSelected() : 0; // 无效目录退普通类型。
    const FName WeaponId = DemoWeaponCatalog::IdAt(Wielder->GetWeaponComponent()->GetActiveSlot() == 2 ? 0 : Wielder->GetWeaponComponent()->GetPrimaryIndex() + 1); // 主武器索引0..2映射目录1..3，手枪固定0；防止步枪误记手枪资格。
    PreparedContext = NewObject<UDemoShotContext>(this);
    if (!PreparedContext->Initialize(Wielder.Get(), WeaponId, Config, HitProfile, AmmoType, FDemoAmmoEffectSnapshot::FromCatalog(Catalog), GetDamagePerPellet()))
    { CancelPreparedShot(); return false; }
    PreparedAmmoCost = Config.AmmoPerShot;
    PreparedPreviousFireTime = NextFireTime;
    const int32 Pellets = GetPelletCount(); // 全组准备成功后才扣一次成本。
    const float Spread = Wielder->GetWeaponComponent()->GetScopeLevel() > 0 ? Config.ScopedSpreadHalfAngle : Config.SpreadHalfAngle; // 本枪锥半角，度。
    FRandomStream Random((ShotSequence + 1) * 7919 + GetUniqueID()); // 失败准备不推进成功枪序号。
    for (int32 Index = 0; Index < Pellets; ++Index) // 独立瞄准点，共享整枪数值和元素/反馈去重。
    {
        const FVector AimDirection = Random.VRandCone(Camera->GetForwardVector(), FMath::DegreesToRadians(Spread)); // 世界单位向量。
        FHitResult AimHit; // 仅决定初始方向，不扣血、不播放肉体音。
        GetWorld()->LineTraceSingleByChannel(AimHit, Start, Start + AimDirection * Config.Range, DemoEnemyHitZones::TraceChannel, Query);
        const FVector AimPoint = AimHit.bBlockingHit ? AimHit.ImpactPoint : Start + AimDirection * Config.Range; // 不锁定目标或提前截断飞行。
        const FVector Direction = (AimPoint - Muzzle).GetSafeNormal(); // 实际枪口方向。
        const FTransform SpawnTransform(Direction.Rotation(), Muzzle); // 单位缩放，外观Transform单独配置。
        ADemoProjectileBase* Projectile = GetWorld()->SpawnActorDeferred<ADemoProjectileBase>(Config.ProjectileClass, SpawnTransform, this, Wielder.Get(), ESpawnActorCollisionHandlingMethod::AlwaysSpawn); // 未激活Actor，先完成蓝图构造。
        if (!Projectile) { UE_LOG(LogFPSDemo, Warning, TEXT("SHOT_PREPARE_REJECT spawn failed pellet=%d"), Index); CancelPreparedShot(); return false; }
        PreparedProjectiles.Add(Projectile);
        Projectile->FinishSpawning(SpawnTransform);
        // 构造脚本只能改表现：实际半径/位置必须仍与安全枪口扫掠一致，不能构造后放大根球或移到墙后。
        if (!IsValid(Projectile) || !PreparedContext || !FMath::IsNearlyEqual(Projectile->Config.CollisionRadius, Radius)
            || !Projectile->GetActorLocation().Equals(Muzzle, .01f) || !Projectile->Prepare(PreparedContext, Index, Direction))
        { UE_LOG(LogFPSDemo, Warning, TEXT("SHOT_PREPARE_REJECT construction invalidated pellet=%d"), Index); CancelPreparedShot(); return false; }
    }
    if (!IsPreparedShotValid()) { UE_LOG(LogFPSDemo, Log, TEXT("SHOT_PREPARE_REJECT ownership/phase changed during construction")); CancelPreparedShot(); return false; }
    return true;
}
void ADemoWeaponBase::PayShotCost()
{
    DEMO_LOG_CALL();
    if (bShotPaid || !IsPreparedShotValid() || !CanPayShotCost() || PreparedAmmoCost != Config.AmmoPerShot)
    { UE_LOG(LogFPSDemo, Warning, TEXT("Weapon cost rejected: missing/stale prepared receipt")); return; }
    Ammo -= PreparedAmmoCost;
    bShotPaid = true;
}
void ADemoWeaponBase::CommitFireCooldown()
{
    DEMO_LOG_CALL();
    if (bPreparedCooldownApplied || !IsPreparedShotValid()) { UE_LOG(LogFPSDemo, Log, TEXT("Weapon cooldown rejected: stale receipt")); return; }
    bPreparedCooldownApplied = true;
    NextFireTime = GetWorld()->GetTimeSeconds() + 60.f / Config.RoundsPerMinute;
}
void ADemoWeaponBase::ExecuteCommittedShot()
{
    DEMO_LOG_CALL();
    if (!bShotPaid || !bPreparedCooldownApplied || !IsPreparedShotValid())
    { UE_LOG(LogFPSDemo, Warning, TEXT("Unpaid/stale prepared shot rejected")); CancelPreparedShot(); return; }
    // 挑战身份在准备时冻结；失败撤销弹丸并恢复本次弹药和射速，不回滚已经持久化的失格事实。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 同步借用本World组合入口。
    if (Mode && !Mode->RegisterWeaponShot(PreparedContext->GetWeaponId()))
    { UE_LOG(LogFPSDemo, Warning, TEXT("Shot blocked: challenge checkpoint failed")); CancelPreparedShot(); return; }
    if (!IsPreparedShotValid()) { CancelPreparedShot(); return; }
    bShotPaid = false;
    bPreparedCooldownApplied = false;
    PreparedAmmoCost = 0;
    ++ShotSequence;
    PerformBallistics(); // 只激活实体子弹，伤害由未来的运动碰撞结算。
    if (!IsValid(this) || !Wielder.IsValid()) return; // 激活表现可能同步销毁武器或Avatar；在飞子弹仍独立持有快照。
    DemoWeaponAudio::PlayAtLocation(this, Config.FireSound, GetMuzzleLocation(), TEXT("Weapon.Fire"));
    if (Config.MuzzleEffect) UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Config.MuzzleEffect, GetMuzzleLocation(), Wielder->GetControlRotation());
    if (!Wielder->GetWeaponComponent()->IsEquipped(this))
    { UE_LOG(LogFPSDemo, Log, TEXT("SHOT_PRESENTATION_SKIP weapon switched during launch")); return; } // 旧枪不能覆盖新枪手臂动作或关闭新枪瞄准。
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
    // 先移出准备列表再激活，蓝图若换枪不能撤销已经提交的这一枪。
    TArray<TObjectPtr<ADemoProjectileBase>> Launching = MoveTemp(PreparedProjectiles); // World持有Actor，各弹强持有Context。
    PreparedContext = nullptr;
    for (ADemoProjectileBase* Projectile : Launching) // 激活重查轮次，阶段失效取消而非退款。
        if (IsValid(Projectile) && !Projectile->ActivateProjectile()) Projectile->CancelProjectile();
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

#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "Game/DemoGameState.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Characters/DemoCharacter.h"
#include "AI/DemoEnemy.h"
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
    Config.Mesh = Gun.Object;
    Config.IdleAnimation = Idle.Object;
    Config.FireAnimation = Fire.Object;
    Config.FireSound = Sound.Object;
    Config.DisplayName = FText::FromString(TEXT("步枪"));
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
    StaticWeaponMesh->SetStaticMesh(Config.StaticMesh);
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_VISUAL %s static=%s skeletal=%s"), *GetName(), *GetNameSafe(Config.StaticMesh), *GetNameSafe(WeaponMesh->GetSkeletalMeshAsset()));
}
void ADemoWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
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
    ++ShotSequence;
    PerformBallistics();
    DemoWeaponAudio::PlayAtLocation(this, Config.FireSound, GetMuzzleLocation(), TEXT("Weapon.Fire"));
    if (Config.MuzzleEffect) UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Config.MuzzleEffect, GetMuzzleLocation(), Wielder->GetControlRotation());
    if (Config.FireAnimation)
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
    if (!Wielder.IsValid()) return;
    // 相机负责瞄准，枪口到目标再做阻挡检测；Query忽略玩家及自身模型。
    const UCameraComponent* Camera = Wielder->GetFirstPersonCameraComponent();
    const FVector Start = Camera->GetComponentLocation();
    FVector Muzzle = GetMuzzleLocation();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoWeaponShot), false, Wielder.Get());
    Query.AddIgnoredActor(this);
    FHitResult MuzzleBlock; // 防止摄像机到枪口之间有墙，枪口偏移不能把起点放到墙后。
    if (GetWorld()->LineTraceSingleByChannel(MuzzleBlock, Start, Muzzle, ECC_Visibility, Query)) Muzzle = MuzzleBlock.ImpactPoint - (Muzzle-Start).GetSafeNormal()*2.f;
    // 按本次射击序号生成独立锥形方向；范围/升级在本次调用快照，不逐弹扣弹。
    FRandomStream Random(ShotSequence * 7919 + GetUniqueID());
    const int32 Pellets = GetPelletCount();
    const float Spread = Wielder->GetWeaponComponent()->GetScopeLevel() > 0 ? Config.ScopedSpreadHalfAngle : Config.SpreadHalfAngle;
    const UDemoAmmoComponent* AmmoType=Wielder->FindComponentByClass<UDemoAmmoComponent>(); // 一次开火快照，所有pellet共享。
    const UDemoAmmoCatalog* AmmoConfig=AmmoType->GetCatalog(); // 只读已加载目录。
    const int32 Type=AmmoConfig->Validate()?AmmoType->GetSelected():0; // 错误配置回退普通弹，不改变基础射击。
    const float Damage = GetDamagePerPellet() * (Type==3?AmmoConfig->PiercingDamageMultiplier:1.f); // 穿透首段增伤只乘一次。
    TMap<ADemoEnemy*, float> TargetDamage; // 仅同步借用目标；敌人死亡为延迟销毁，不跨帧保存。
    for (int32 Index = 0; Index < Pellets; ++Index) // 每颗弹独立碰撞，可击中不同敌人。
    {
        const FVector Direction = Random.VRandCone(Camera->GetForwardVector(), FMath::DegreesToRadians(Spread)); // 本颗世界单位方向。
        FHitResult AimHit; // 相机最近阻挡点，决定枪口实际射向。
        GetWorld()->LineTraceSingleByChannel(AimHit, Start, Start+Direction*Config.Range, ECC_Visibility, Query);
        const FVector AimEnd = AimHit.bBlockingHit ? AimHit.ImpactPoint : Start+Direction*Config.Range; // 不允许超出配置射程。
        FHitResult Hit; // 实际枪口到目标阻挡；小量延伸确保表面命中精度。
        GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, AimEnd+(AimEnd-Muzzle).GetSafeNormal()*2.f, ECC_Visibility, Query);
        ADemoEnemy* Enemy = Cast<ADemoEnemy>(Hit.GetActor()); // 当前弹丸目标，只攻击存活敌人。
        if (Enemy && Enemy->IsAlive())
        {
            const float Distance = FVector::Distance(Start,Hit.ImpactPoint); // 衰减统一采用瞄准起点距离cm。
            const float Alpha = Config.Range > Config.FalloffStart ? FMath::Clamp((Distance-Config.FalloffStart)/(Config.Range-Config.FalloffStart),0.f,1.f) : 0.f;
            const float FirstDamage=Damage * FMath::Lerp(1.f,Config.MinimumDamageMultiplier,Alpha); // 首目标理论值，不依赖实际剩余HP。
            TargetDamage.FindOrAdd(Enemy) += FirstDamage;
            if(Type==3)
            {
                const FVector Travel=(AimEnd-Muzzle).GetSafeNormal(); // 沿真实枪口轨迹继续，不被相机首个敌人AimEnd截断。
                FCollisionQueryParams ContinueQuery=Query; ContinueQuery.AddIgnoredActor(Enemy); // 忽略整只首敌的所有组件。
                FHitResult Behind; // 后方最近阻挡，墙体也会阻挡，绝不穿墙找敌人。
                GetWorld()->LineTraceSingleByChannel(Behind,Hit.ImpactPoint+Travel*2.f,Muzzle+Travel*Config.Range,ECC_Visibility,ContinueQuery);
                ADemoEnemy* Secondary=Cast<ADemoEnemy>(Behind.GetActor()); // 只额外命中一个活敌人。
                if(Secondary&&Secondary!=Enemy&&Secondary->IsAlive()){TargetDamage.FindOrAdd(Secondary)+=FirstDamage*AmmoConfig->SecondaryDamageRatio;UE_LOG(LogFPSDemo,Log,TEXT("AMMO_PIERCE shot=%d target=%s"),ShotSequence,*Secondary->GetName());}
            }
        }
        DrawDebugLine(GetWorld(), Muzzle, Hit.bBlockingHit ? Hit.ImpactPoint : AimEnd, Enemy ? FColor::Green : FColor::Yellow, false, .07f, 0, 1.f);
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("PELLET shot=%d index=%d target=%s"), ShotSequence, Index, *GetNameSafe(Enemy));
    }
    for (const TPair<ADemoEnemy*, float>& Entry : TargetDamage) // 同一敌人一次GE/一次肉体反馈。
    {
        if(!Entry.Key->IsAlive()||GetWorld()->GetGameState<ADemoGameState>()->Phase!=EDemoPhase::Combat)continue; // 最后一只怪死亡可同步切阶段。
        const float Before=Entry.Key->GetHealth(); // 实际正伤害才叠层，零伤害/致死不施加新状态。
        DemoEffects::Apply(Wielder->GetAbilitySystemComponent(), Entry.Key->GetAbilitySystemComponent(), UDemoHealthEffect::StaticClass(), -Entry.Value);
        if(Entry.Key->IsAlive()&&Entry.Key->GetHealth()<Before)
        {
            if(Type==1||Type==2)Entry.Key->FindComponentByClass<UDemoAmmoStatus>()->Apply(Wielder->GetAbilitySystemComponent(),Type,AmmoConfig); // 聚合后每敌人每枪只叠一层。
            if(Type==3)Entry.Key->GetAbilitySystemComponent()->ExecuteGameplayCue(DemoAmmoTags::PiercingCue,FGameplayCueParameters());
        }
        Wielder->LastHitTime = GetWorld()->GetTimeSeconds();
        UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_DAMAGE target=%s total=%.2f"), *Entry.Key->GetName(), Entry.Value);
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
    GetWorldTimerManager().ClearTimer(PoseTimer);
    if (Wielder.IsValid() && Config.ReloadAnimation) Wielder->GetMesh1P()->PlayAnimation(Config.ReloadAnimation, false);
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_RELOAD_BEGIN %s seconds=%.2f"), *GetName(), Config.ReloadSeconds);
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
    RestoreIdle();
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_RELOAD_COMPLETE %s ammo=%d reserve=%d"), *GetName(), Ammo, ReserveAmmo);
    return true;
}
void ADemoWeaponBase::CancelReload()
{
    DEMO_LOG_CALL();
    if (bReloading) UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_RELOAD_CANCELLED %s unchanged ammo=%d"), *GetName(), Ammo);
    bReloading = false;
    RestoreIdle();
}
bool ADemoWeaponBase::IsReloading() const { DEMO_LOG_TICK(); return bReloading; }
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
    if (Wielder.IsValid() && bIsEquipped && !bReloading && Config.IdleAnimation)
        Wielder->GetMesh1P()->PlayAnimation(Config.IdleAnimation, true);
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

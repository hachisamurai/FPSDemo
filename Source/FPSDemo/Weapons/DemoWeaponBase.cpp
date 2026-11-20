// 职责：武器事务及音画；本地F0桥保持原映射，客户端按冻结弹种/接触事实画分段亮条并幂等补闪，绝不运行碰撞/伤害。 本人首段使用发射帧最终可见枪口。
#include "Weapons/DemoWeaponBase.h"
#include "Animation/DemoFPAnimInstance.h" // 权威读取配置层CDO上的机械插入节点，不依赖客户端动画播放。
#include "Engine/HitResult.h" // 非Editor逐文件校验不借共享PCH补全DamageableTarget中的实际命中值。
#include "Engine/StaticMesh.h" // 直接消费CDO网格/日志与当前Cue网格身份，不依赖Editor共享PCH。
#include "Engine/SkeletalMesh.h" // 同上：骨骼资源身份比较须完整类型。
#include "Engine/World.h" // 发射/纯表现World查询直接依赖，非Editor单文件不能靠PCH提供定义。
#include "GAS/Talents/DemoTalentCatalog.h"
// 对应头文件先包含，确保UBT独立编译检查本类头文件自包含；功能逻辑不变。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Weapons/Projectiles/DemoProjectileBase.h"
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Weapons/Projectiles/DemoProjectileTracer.h"
#include "GAS/Ammo/DemoAmmoEffectSnapshot.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Animation/DemoPlayerPresentationComponent.h" // 物理统一TP，独立FP只用于本地声光/弹体接续，不混用伤害来源。
#include "GameFramework/PlayerController.h"
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
#include "Combat/Feedback/DemoCombatFeelSettings.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Scalability.h"
#include "Sound/SoundWave.h"
#include "EngineGlobals.h" // 同帧枪口采样标签，不用于推算飞行或网络时间。
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Sound/SoundBase.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Debug/DemoStaticLog.h" // 纯表现静态工厂必须记录入口，不使用this日志宏。
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

ADemoWeaponBase::ADemoWeaponBase()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true; // 库存由服务器生成，Actor随持有Pawn相关性发送到队友。
    bNetUseOwnerRelevancy = true;
    SetReplicateMovement(false); // 挂接到复制Pawn的网格，不独立争抢运动真值。
    WeaponMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponMesh"));
    SetRootComponent(WeaponMesh);
    WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WeaponMesh->SetOnlyOwnerSee(false);
    WeaponMesh->CastShadow = false;
    // 保留已序列化的 WeaponMesh 根，静态枪共享原有挂接、隐藏与镜内隐藏流程。
    StaticWeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticWeaponMesh"));
    StaticWeaponMesh->SetupAttachment(WeaponMesh);
    StaticWeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    StaticWeaponMesh->SetOnlyOwnerSee(false);
    StaticWeaponMesh->CastShadow = false;
    OpticMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponOptic"));
    OpticMesh->SetupAttachment(WeaponMesh);
    OpticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    OpticMesh->SetOnlyOwnerSee(true); OpticMesh->CastShadow=false;
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
void ADemoWeaponBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL();
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADemoWeaponBase, DefinitionId);
    DOREPLIFETIME_CONDITION(ADemoWeaponBase, Ammo, COND_OwnerOnly);
    DOREPLIFETIME(ADemoWeaponBase, BoltCycleState); // 所有枪TP后坐与狙击FP/TP拉栓使用同一已提交射击时钟，弹药仍OwnerOnly。
    DOREPLIFETIME_CONDITION(ADemoWeaponBase, ReserveAmmo, COND_OwnerOnly);
    DOREPLIFETIME(ADemoWeaponBase, bInitialized);
    DOREPLIFETIME(ADemoWeaponBase, bIsEquipped);
    DOREPLIFETIME(ADemoWeaponBase, bReloading);
    DOREPLIFETIME(ADemoWeaponBase, ReloadSequence);
    DOREPLIFETIME(ADemoWeaponBase, ReloadDuration);
    DOREPLIFETIME(ADemoWeaponBase, bEmptyReload);
    DOREPLIFETIME(ADemoWeaponBase, ReloadServerStartTime);
}
bool ADemoWeaponBase::BindDefinition(const FDemoWeaponDefinition& Definition)
{
    DEMO_LOG_CALL();
    FString Error; // 定义只传身份/资源/策略，不复制BP Config中的伤害或射速。
    if (!HasAuthority() || bInitialized || !DefinitionId.IsNone() || !Definition.Validate(Error))
    { UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_DEFINITION_REJECT id=%s reason=%s"), *Definition.WeaponId.ToString(), *Error); return false; }
    DefinitionId = Definition.WeaponId;
    return true;
}
FName ADemoWeaponBase::GetWeaponId() const
{
    DEMO_LOG_TICK();
    return DefinitionId;
}
void ADemoWeaponBase::OnRep_Owner()
{
    DEMO_LOG_CALL();
    ClearProjectilePresentation(); // Avatar/持有者改变不能接管上一人的FP发射记录。
    Super::OnRep_Owner();
    RefreshReplicatedPresentation();
}
void ADemoWeaponBase::OnRep_WeaponState()
{
    DEMO_LOG_CALL();
    RefreshReplicatedPresentation();
}
void ADemoWeaponBase::RefreshReplicatedPresentation()
{
    DEMO_LOG_CALL();
    if (HasAuthority()) return; // 权威使用CommitEquip事务，复制回调不能再次改变成本/状态。
    ADemoCharacter* ReplicatedWielder=Cast<ADemoCharacter>(GetOwner()); // 复制晚到/换Owner时先撤本枪声光，不借旧Avatar的TP源。
    if (!bIsEquipped || Wielder.Get()!=ReplicatedWielder) ClearMuzzleFlashes();
    Wielder=ReplicatedWielder;
    if (!Wielder.IsValid()) { UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_PRESENTATION deferred until owner replication")); return; }
    ApplyVisualMesh();
    AttachToComponent(Wielder->GetMesh1P(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Config.AttachSocket);
    SetActorRelativeTransform(Config.AttachOffset);
    SetActorHiddenInGame(!bIsEquipped); // Actor隐藏只表达装备状态；Scope使用本地组件可见性，退出不会留下隐藏Actor。
    if (Wielder->IsLocallyControlled()) SetScopedVisual(Wielder->GetWeaponComponent()->GetScopeLevel() > 0);
    if (bIsEquipped && Wielder->IsLocallyControlled() && Wielder->GetWeaponAnimationComponent())
        Wielder->GetWeaponAnimationComponent()->SyncReplicatedWeapon(this);
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
    WeaponMesh->SetOnlyOwnerSee(true); WeaponMesh->SetOwnerNoSee(false); // FP枪保留原射击锚点；队友改看PlayerPresentation的独立全身枪。
    StaticWeaponMesh->SetOnlyOwnerSee(true); StaticWeaponMesh->SetOwnerNoSee(false); // 旧静态枪同样不在队友视角显示相机空间副本。
    WeaponMesh->SetVisibleInRayTracing(false); StaticWeaponMesh->SetVisibleInRayTracing(false); OpticMesh->SetVisibleInRayTracing(false); // 5.4相机空间FP不进入HWRT世界反射；真正TP枪由Presentation独立管理，不移植新版FP渲染器。
    // 稳定挂点与枪械零件在OwnerNoSee/开镜/NullRHI恢复后仍需真实新骨姿势；单人四个小网格开销可控。
    WeaponMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    StaticWeaponMesh->SetStaticMesh(Config.StaticMesh);
    OpticMesh->SetStaticMesh(Config.OpticMesh);
    OpticMesh->AttachToComponent(WeaponMesh,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Config.OpticSocket);
    OpticMesh->SetRelativeTransform(Config.OpticOffset);
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_VISUAL %s static=%s skeletal=%s"), *GetName(), *GetNameSafe(Config.StaticMesh), *GetNameSafe(WeaponMesh->GetSkeletalMeshAsset()));
}
void ADemoWeaponBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    ClearMuzzleFlashes();
    ClearProjectilePresentation();
    // 外部销毁武器也必须清理其表现，不能只依赖Pawn正常CancelActions顺序。
    if (Wielder.IsValid() && Wielder->GetWeaponAnimationComponent()) Wielder->GetWeaponAnimationComponent()->EndReloadPresentation(this, ReloadSequence, false);
    GetWorldTimerManager().ClearTimer(PoseTimer);
    CancelPreparedShot(); // 只撤销待发弹，已飞行Actor不归武器销毁。
    Wielder.Reset();
    Super::EndPlay(EndPlayReason);
}
bool ADemoWeaponBase::InitializeForOwner(ADemoCharacter* Character, bool bPublishReplication)
{
    DEMO_LOG_CALL();
    FString Error; // 本次配置错误，不缓存BP定义指针到外部。
    if (!HasAuthority() || bInitialized || !Character || !Config.Validate(Error)
        || !Character->GetMesh1P()->DoesSocketExist(Config.AttachSocket))
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_INIT_REJECTED %s config=%s socket=%s"), *GetName(), *Error, *Config.AttachSocket.ToString());
        return false;
    }
    if (DefinitionId.IsNone())
    {
        // 兼容旧C++直接生成路径：只接受目录资源类的唯一匹配，绝不按当前装备槽推断身份。
        const FDemoWeaponDefinition* Matched = nullptr; // 同步借用项目定义，正式库存生成会预先Bind而跳过此分支。
        for (const FDemoWeaponDefinition& Definition : DemoWeaponCatalog::GetCatalog()->Definitions)
            if (Definition.WeaponClass.ToSoftObjectPath() == FSoftObjectPath(GetClass()))
            { if (Matched) { UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_INIT ambiguous definition")); return false; } Matched = &Definition; }
        if (!Matched || !BindDefinition(*Matched)) { UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_INIT requires explicit stable definition")); return false; }
    }
    Wielder = Character;
    SetReplicates(bPublishReplication); // 正式库存覆盖旧BP单人设置；商店候选必须等完整本地保存后才发布。
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
    if (!bEquipped) { ClearMuzzleFlashes(); CancelPreparedShot(); CancelReload(); GetWorldTimerManager().ClearTimer(PoseTimer); }
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
void ADemoWeaponBase::SetScopedVisual(bool bScoped)
{
    DEMO_LOG_CALL();
    if (!Wielder.IsValid() || !Wielder->IsLocallyControlled()) return; // 开镜隐藏是本地组件表现，不复制ActorHidden给队友。
    if(bScoped) ClearMuzzleFlashes(); // 镜内本人隐藏枪时清理本人火光，不复制显隐给队友。
    WeaponMesh->SetHiddenInGame(bScoped, false);
    StaticWeaponMesh->SetHiddenInGame(bScoped, false);
    OpticMesh->SetHiddenInGame(bScoped,false);
}
int32 ADemoWeaponBase::GetAmmo() const { DEMO_LOG_TICK(); return Ammo; }
int32 ADemoWeaponBase::GetReserveAmmo() const { DEMO_LOG_TICK(); return ReserveAmmo; }
int32 ADemoWeaponBase::GetCapacity() const
{
    DEMO_LOG_TICK();
    if (Config.bFixedMagazineCapacity) return Config.MagazineCapacity; // 双管固定两膛，不能通过属性升级生成第三膛；存档恢复也统一钳制。
    // 属性只借用持有角色PS；未初始化时只有武器基础容量。
    const UDemoAttributeSet* Attributes = Wielder.IsValid() ? Wielder->GetDemoAttributes() : nullptr;
    const int32 Base=Config.MagazineCapacity + (Attributes ? FMath::Max(0,FMath::FloorToInt(Attributes->GetMagazineBonus())) : 0); // 合计基础容量，不写回永久属性。
    return FMath::Min(11000,FMath::FloorToInt(Base*(DemoGambleEffects::Has(Wielder.IsValid()?Wielder->GetAbilitySystemComponent():nullptr,DemoGamble::Relic(0))?1.f+GetDefault<UDemoGambleSettings>()->CapacityBonus:1.f))); // 维持检查点既有11000发协议上限。
}
int32 ADemoWeaponBase::GetPelletCount() const { DEMO_LOG_TICK(); return 1; }
int32 ADemoWeaponBase::GetBonusPelletCount() const
{
    DEMO_LOG_TICK();
    // 固定物理容量且多弹丸武器（Drake-12双管）无法增加膛数，每2点MagazineBonus折算1颗额外弹丸（单次商店+4=+2丸，上限+16），兼顾Twin天赋后仍不越过64弹体上限。
    if (!Config.bFixedMagazineCapacity || GetPelletCount() <= 1) return 0;
    const UDemoAttributeSet* Attributes = Wielder.IsValid() ? Wielder->GetDemoAttributes() : nullptr;
    return Attributes ? FMath::Clamp(FMath::FloorToInt(FMath::Max(0.f, Attributes->GetMagazineBonus()) * 0.5f), 0, 16) : 0;
}
int32 ADemoWeaponBase::GetProjectilesPerShot() const
{
    DEMO_LOG_TICK();
    const int32 BasePellets = GetPelletCount() + GetBonusPelletCount();
    const int32 TalentScale = GetDefault<UDemoTalentCatalog>()->Evaluate(Wielder.IsValid() ? Wielder->GetAbilitySystemComponent() : nullptr).Projectiles;
    return FMath::Clamp(BasePellets * TalentScale, 1, 64);
}
float ADemoWeaponBase::GetFireInterval() const
{
    DEMO_LOG_TICK();
    const auto* Attributes=Wielder.IsValid()?Wielder->GetDemoAttributes():nullptr; // GAS CurrentValue，未绑定Avatar回退原速。
    return 60.f/(Config.RoundsPerMinute*(Attributes?Attributes->GetFireRateMultiplier():1.f));
}
float ADemoWeaponBase::GetDamagePerPellet() const
{
    DEMO_LOG_TICK();
    // 玩家加成以整次触发为单位；霰弹均分，避免商店+5变成8颗各+5。
    const UDemoAttributeSet* Attributes = Wielder.IsValid() ? Wielder->GetDemoAttributes() : nullptr;
    const float Base=Config.BaseDamage+(Attributes?Attributes->GetWeaponDamageBonus()/FMath::Max(1,GetPelletCount()):0.f); // 按基础弹丸数分摊，双弹不稀释后再重复折扣。
    return Base*GetDefault<UDemoTalentCatalog>()->Evaluate(Wielder.IsValid()?Wielder->GetAbilitySystemComponent():nullptr).PelletDamage; // 重复双重弹头提高每颗伤害，实体数仍固定翻倍。
}
bool ADemoWeaponBase::CanPayShotCost() const
{
    DEMO_LOG_TICK();
    return bInitialized && bIsEquipped && !bReloading && Ammo >= Config.AmmoPerShot && Wielder.IsValid()
        && Wielder->CanUseFirearmActions() && Wielder->GetDemoAttributes() && Wielder->GetDemoAttributes()->GetHealth() > 0.f;
}
float ADemoWeaponBase::GetFireCooldownRemaining() const { DEMO_LOG_TICK(); return FMath::Max(0.f, NextFireTime - GetWorld()->GetTimeSeconds()); }
bool ADemoWeaponBase::IsPreparedShotValid() const
{
    DEMO_LOG_TICK();
    if (!HasAuthority() || !PreparedContext || !PreparedContext->IsFlightValid() || !Wielder.IsValid()
        || !Wielder->CanUseFirearmActions() || !Wielder->GetWeaponComponent()->IsEquipped(this)
        || !bIsEquipped || bReloading || PreparedProjectiles.Num() != GetProjectilesPerShot()) return false; // 天赋变化使未提交整组失效，避免半组发射。
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
    const FVector Start = Wielder->GetSharedViewLocation(); // 世界cm，权威与本机共用稳定眼位，不要求远端Pawn先执行相机渲染。
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
    PreparedMuzzle = Muzzle; // 成功准备后冻结真正安全起点；声光回退不能重查未经防墙的TP Socket。
    const UDemoAmmoComponent* AmmoComponent = Wielder->FindComponentByClass<UDemoAmmoComponent>(); // 弹药类型只读一次。
    const UDemoAmmoCatalog* Catalog = AmmoComponent ? AmmoComponent->GetCatalog() : GetDefault<UDemoAmmoCatalog>(); // 兼容旧Pawn缺组件。
    const int32 AmmoType = Catalog && Catalog->Validate() && AmmoComponent ? AmmoComponent->GetSelected() : 0; // 无效目录退普通类型。
    const FName WeaponId = GetWeaponId(); // 实例身份在生成时绑定并冻结；切槽/蓝图父类不会改变本枪挑战归属。
    PreparedContext = NewObject<UDemoShotContext>(this);
    if (!PreparedContext->Initialize(Wielder.Get(), WeaponId, Config, HitProfile, AmmoType, FDemoAmmoEffectSnapshot::FromCatalog(Catalog), GetDamagePerPellet()))
    { CancelPreparedShot(); return false; }
    PreparedAmmoCost = Config.AmmoPerShot;
    PreparedPreviousFireTime = NextFireTime;
    const int32 Pellets = GetProjectilesPerShot(); // 双弹/散弹全组成功后仅扣一次原成本，失败整组退款。
    const float Spread = FMath::Max(Wielder->GetWeaponComponent()->GetScopeLevel()>0?Config.ScopedSpreadHalfAngle:Config.SpreadHalfAngle,
        GetDefault<UDemoTalentCatalog>()->Evaluate(Wielder->GetAbilitySystemComponent()).Spread)*PreparedContext->GetRunBuildSpreadScale(); // 度；稳定分支只缩本枪冻结锥角，空仓不消费充能。
    FRandomStream Random((ShotSequence + 1) * 7919 + GetUniqueID()); // 失败准备不推进成功枪序号。
    for (int32 Index = 0; Index < Pellets; ++Index) // 独立瞄准点，共享整枪数值和元素/反馈去重。
    {
        const FVector AimDirection = Random.VRandCone(Wielder->GetBaseAimRotation().Vector(), FMath::DegreesToRadians(Spread)); // 世界单位向量。
        const FVector AimPoint=TraceAimPoint(Start,AimDirection); // 保留逐弹丸原始射线、散布及射程。
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
    // 冻结一次射速，动画和权威截止时间不得分别重新读取可能变化的RPM。
    PreparedBoltCycle.ServerStartTime = GetWorld()->GetTimeSeconds();
    PreparedBoltCycle.Duration = GetFireInterval(); // 拉栓表现与权威射速共用一次冻结的GAS修正间隔。
    NextFireTime = PreparedBoltCycle.ServerStartTime + PreparedBoltCycle.Duration;
}
void ADemoWeaponBase::ExecuteCommittedShot()
{
    DEMO_LOG_CALL();
    if (!bShotPaid || !bPreparedCooldownApplied || !IsPreparedShotValid())
    { UE_LOG(LogFPSDemo, Warning, TEXT("Unpaid/stale prepared shot rejected")); CancelPreparedShot(); return; }
    // 挑战身份在准备时冻结；失败撤销弹丸并恢复本次弹药和射速，不回滚已经持久化的失格事实。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 同步借用本World组合入口。
    if (Mode && !Mode->RegisterWeaponShot(PreparedContext->GetWeaponId(), Wielder.Get()))
    { UE_LOG(LogFPSDemo, Warning, TEXT("Shot blocked: challenge checkpoint failed")); CancelPreparedShot(); return; }
    if (!IsPreparedShotValid()) { CancelPreparedShot(); return; }
    PreparedContext->CommitRunBuildShot(); // 最终发射已确认且弹药已扣，整枪只消费一次，任何先前失败都不耗核心层数。
    const float ShotRecoilScale=PreparedContext->GetRunBuildRecoilScale(); // 激活弹丸会移走PreparedContext，先冻结纯表现值。
    const bool bRedesigned = PreparedContext->UsesRedesignedFeedback(); // 候选选择与整枪一起冻结，不让迟到RPC读取新AB配置。
    const FGuid PresentedShotId = PreparedContext->GetShotId(); // PerformBallistics会移走Context，先复制纯身份。
    const bool bScopedAtFire = Wielder->GetWeaponComponent()->GetScopeLevel() > 0; // 发射快照，不受随后的自动退镜重写。
    const float PresentedTime = GetWorld()->GetTimeSeconds(); // 纯表现发射时钟，与本次权威提交同帧。
    const FVector PresentedOrigin = PreparedMuzzle; // BP激活回调可重入准备下一枪，本栈仍发布原枪安全起点。
    const uint32 PresentedPhase=PreparedContext->GetPhaseRevision(); // 声光确认也拒绝跨阶段迟到，不给旧弹新阶段F0。
    const int32 PresentedLevel=PreparedContext->GetLevelNumber();
    CaptureProjectileMuzzle(PresentedShotId,bScopedAtFire); // Host/Standalone也必须在Activate显示Mesh之前冻结F0。
    bShotPaid = false;
    bPreparedCooldownApplied = false;
    PreparedAmmoCost = 0;
    ++ShotSequence;
    // 复用已经提交的射击序号/冷却时钟供所有枪TP读取，不增加第二个玩法时钟或由RPC到达时间重启后坐。
    BoltCycleState = PreparedBoltCycle;
    BoltCycleState.Sequence = ShotSequence; // 仅成功开火发布；空仓/拒绝/事务回滚均不产生后坐或拉栓。
    PerformBallistics(); // 只激活实体子弹，伤害由未来的运动碰撞结算。
    if (!IsValid(this) || !Wielder.IsValid()) return; // 激活表现可能同步销毁武器或Avatar；在飞子弹仍独立持有快照。
    MulticastShotPresentation(PresentedOrigin,PresentedShotId,bScopedAtFire,PresentedTime,PresentedPhase,PresentedLevel,ShotRecoilScale,bRedesigned); // 只传只读发射事实，不提交FP坐标/伤害。
    if (Config.bSupportsScope && Config.bUnscopeAfterShot && Wielder->GetWeaponComponent()->IsEquipped(this)) Wielder->GetWeaponComponent()->StopAim();
    ForceNetUpdate();
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_SHOT weapon=%s id=%s sequence=%d pellets=%d ammo=%d"), *GetName(), *DefinitionId.ToString(), ShotSequence, GetProjectilesPerShot(), Ammo); // 实际弹体数便于Debug核对双弹。
}
void ADemoWeaponBase::MulticastShotPresentation_Implementation(FVector_NetQuantize Muzzle,FGuid ShotId,bool bScopedAtFire,float ServerFireTime,uint32 PhaseRevision,int32 Level,float RecoilScale,bool bRedesigned)
{
    DEMO_LOG_CALL();
    if (GetNetMode() == NM_DedicatedServer) return; // 专用服务端不创建音频/粒子或相机反馈。
    if (!Wielder.IsValid()) Wielder = Cast<ADemoCharacter>(GetOwner());
    const ADemoGameState* ShotState = GetWorld()->GetGameState<ADemoGameState>(); // 时效不明/旧声光不为活弹建立F0凭据。
    if (!ShotState || ShotState->PhaseRevision!=PhaseRevision || ShotState->LevelNumber!=Level || !FMath::IsFinite(ServerFireTime)
        || ShotState->GetServerWorldTimeSeconds()-ServerFireTime>.5f || ShotState->GetServerWorldTimeSeconds()-ServerFireTime<-.1f)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_VISUAL_REJECT reason=shot phase/time")); return; }
    CaptureProjectileMuzzle(ShotId,bScopedAtFire);
    const bool bCurrentWeapon = Wielder.IsValid() && Wielder->GetWeaponComponent() && Wielder->GetWeaponComponent()->IsEquipped(this); // 切枪后的迟到消息不借用新装备挂点。
    const FDemoProjectileMuzzleReceipt* MuzzleReceipt=ProjectileMuzzles.Find(ShotId); // 与F0同次确认的尺寸，非当前装备/退镜后的新尺寸。
    const bool bFrozenFPMuzzle=bCurrentWeapon && MuzzleReceipt && MuzzleReceipt->bAllowed && CanUseIndependentVisualMuzzle(); // Scope-at-fire或当前外视点一律世界降级。
    const FVector VisualMuzzle = bFrozenFPMuzzle ? MuzzleReceipt->Origin : FVector(Muzzle); // 仅声光，物理起点始终为冻结的安全TP位置。
    const bool bLocalCandidate = bRedesigned && bCurrentWeapon && Wielder->IsLocallyControlled(); // 本人只听一份候选近枪声；队友/已切枪迟到消息沿原世界声。
    USoundWave* LocalWave = bLocalCandidate ? GetDefault<UDemoCombatFeelSettings>()->SelectFire(DefinitionId) : nullptr; // CDO预载，不在开火读盘。
    if (LocalWave)
    {
        const UDemoCombatFeelSettings* Feel = GetDefault<UDemoCombatFeelSettings>(); // 同一候选CDO的固定4声部并发策略。
        const float Pitch = FMath::FRandRange(.95f, 1.05f); // 本人原始Wave的单次音高随机；不与世界Cue叠加播放。
        const float Gain = FMath::Pow(10.f, FMath::FRandRange(-1.5f, 1.5f) / 20.f); // 既有±1.5dB需求转线性，仅作用当前声部，不改变武器/资产音量。
        UAudioComponent* Audio = UGameplayStatics::CreateSound2D(this,LocalWave,Feel->GetLocalFireVolume(DefinitionId) * Gain,Pitch,0.f,Feel->LocalFireConcurrency); // 霰弹/狙击取重型本人音量；自动销毁，无跨地图持久化；达到并发上限替换最旧而非无界叠加。
        if (Audio)
        {
            Audio->bIsUISound = false; Audio->Play();
            // 2026-11-17用户报告“没有使用上新的声音”：逐枪记录实际Wave完整路径与音量，供试听日志直接核对资源身份；playing只证明引擎接受，不等于可听。
            UE_LOG(LogFPSDemo, Log, TEXT("FEEL_FIRE_PLAY weapon=%s wave=%s volume=%.3f playing=%d"), *DefinitionId.ToString(), *GetPathNameSafe(LocalWave), Audio->VolumeMultiplier, Audio->IsPlaying());
        }
        else UE_LOG(LogFPSDemo, Log, TEXT("FEEL_FIRE local audio unavailable weapon=%s"), *DefinitionId.ToString());
    }
    else DemoWeaponAudio::PlayAtLocation(this, Config.FireSound, VisualMuzzle, TEXT("Weapon.Fire")); // 缺候选明确降级原Cue；不与近枪完整叠播。
    PlayMuzzleFlash(bScopedAtFire); // 真正附着本观察者可见枪的专用MuzzleFlash；不借F0/Actor旋转/飞行亮条。
    if (!Wielder.IsValid() || !Wielder->IsLocallyControlled() || !Wielder->GetWeaponComponent()->IsEquipped(this)) return;
    if (Config.WeaponAnimLayerClass && Wielder->GetWeaponAnimationComponent()) Wielder->GetWeaponAnimationComponent()->PlayFire(this);
    else if (Config.FireAnimation)
    {
        Wielder->GetMesh1P()->PlayAnimation(Config.FireAnimation, false);
        GetWorldTimerManager().SetTimer(PoseTimer, this, &ADemoWeaponBase::RestoreIdle, FMath::Max(.1f, Config.FireAnimation->GetPlayLength()), false);
    }
    if (Config.RecoilPitch > 0.f) Wielder->AddControllerPitchInput(-Config.RecoilPitch*FMath::Clamp(RecoilScale,.1f,1.f)); // 服务器冻结的核心稳定系数，纯相机后坐，不修改源动画骨骼。
    OnWeaponShot(); // Blueprint表现事件只在当前拥有者执行，不可再次扣弹/伤害。
}
FVector ADemoWeaponBase::GetMuzzleLocation() const
{
    DEMO_LOG_TICK();
    // 所有网络角色使用同一TP世界源；显示策略和Scope不得改变Host/Standalone真实碰撞起点。
    const UDemoPlayerPresentationComponent* Presentation=Wielder.IsValid()?Wielder->GetPlayerPresentation():nullptr; // 当前Pawn保活，只读策略与展示组件。
    const USceneComponent* ThirdPersonVisual = Presentation ? (Config.StaticMesh
        ? static_cast<const USceneComponent*>(Presentation->GetThirdPersonStaticWeaponMesh())
        : static_cast<const USceneComponent*>(Presentation->GetThirdPersonWeaponMesh())) : nullptr; // 均由Body同帧世界锚点驱动。
    const bool bMatchingMesh = Presentation && (Config.StaticMesh
        ? Presentation->GetThirdPersonStaticWeaponMesh() && Presentation->GetThirdPersonStaticWeaponMesh()->GetStaticMesh()==Config.StaticMesh
        : Presentation->GetThirdPersonWeaponMesh() && Presentation->GetThirdPersonWeaponMesh()->GetSkeletalMeshAsset()==Config.Mesh);
    if (bIsEquipped && bMatchingMesh && ThirdPersonVisual && ThirdPersonVisual->DoesSocketExist(Config.MuzzleSocket))
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("MUZZLE_RESOLVE weapon=%s source=TP socket=%s"), *GetName(), *Config.MuzzleSocket.ToString());
        return ThirdPersonVisual->GetSocketLocation(Config.MuzzleSocket);
    }
    // 缺TP资源时保留确定性的稳定世界回退，绝不偷偷使用本人相机空间枪。
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("MUZZLE_FALLBACK weapon=%s socket=%s reason=missing visual socket"), *GetName(), *Config.MuzzleSocket.ToString());
    if (!Wielder.IsValid()) return GetActorLocation();
    const FRotationMatrix AimAxes(Wielder->GetBaseAimRotation()); // 远端摄像机未CalcCamera时仍使用已接收的控制朝向。
    return Wielder->GetSharedViewLocation() + AimAxes.GetUnitAxis(EAxis::X)*70.f + AimAxes.GetUnitAxis(EAxis::Y)*15.f - AimAxes.GetUnitAxis(EAxis::Z)*10.f; // 兼容缺枪口资源的稳定回退，不读取隐藏FP相机旧位置。
}
// 只读中心射线或已经抽样的散布射线；沿用本项目部位/墙通道和忽略名单，不结算命中。
FVector ADemoWeaponBase::TraceAimPoint(const FVector& Eye,const FVector& Direction) const
{
    DEMO_LOG_TICK();
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoWeaponAimPoint),false,Wielder.Get());
    Query.AddIgnoredActor(this);
    FHitResult Hit;
    const FVector End=Eye+Direction*Config.Range;
    return GetWorld()->LineTraceSingleByChannel(Hit,Eye,End,DemoEnemyHitZones::TraceChannel,Query) ? Hit.ImpactPoint : End;
}

FVector ADemoWeaponBase::GetVisualMuzzleLocation() const
{
    DEMO_LOG_TICK();
    USceneComponent* FP=Config.StaticMesh?static_cast<USceneComponent*>(StaticWeaponMesh):WeaponMesh.Get(); // 同一当前独立FP组件，资格仍沿用原门禁。
    if(!CanUseIndependentVisualMuzzle()) return GetMuzzleLocation(); // 非FP/镜内/失效源保持原物理世界降级。
    // 只读机械枪；发射帧稍晚由世界完成相机和动画求值后锁定可见枪口。
    const FName VisualSocket=FP->DoesSocketExist(Config.MuzzleFlashSocket)?Config.MuzzleFlashSocket:Config.MuzzleSocket; // 新四枪共享已校准口心；旧无Flash资产兼容原Muzzle。
    return FP->GetSocketLocation(VisualSocket); // 仅冻结本机声/弹体表现F0，不改变冻结时机、物理起点或离枪后的世界轨迹。
}
UPrimitiveComponent* ADemoWeaponBase::GetMuzzleFlashComponent(bool bScopedAtFire) const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=Wielder.IsValid()?Wielder.Get():Cast<ADemoCharacter>(GetOwner()); // 只借同一Avatar，不能从新Pawn补源。
    const UDemoPlayerPresentationComponent* Presentation=Pawn?Pawn->GetPlayerPresentation():nullptr;
    if(!bIsEquipped||!Pawn||!Presentation||Pawn->IsCoopEliminated()||!Pawn->GetWeaponComponent()||!Pawn->GetWeaponComponent()->IsEquipped(this))
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MUZZLE_FLASH_NO_SOURCE equipment/avatar")); return nullptr; }
    const APlayerController* PC=Cast<APlayerController>(Pawn->GetController()); // 本人仅在真实Pawn视点选择FP；外视点/队友用TP。
    const bool bOwnerView=Pawn->IsLocallyControlled()&&PC&&PC->GetViewTarget()==Pawn;
    if(bOwnerView&&(bScopedAtFire||Pawn->GetWeaponComponent()->GetScopeLevel()>0))
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MUZZLE_FLASH_NO_SOURCE owner scope")); return nullptr; } // 镜内自身枪隐藏，不借隐藏TP枪。
    const bool bFP=bOwnerView&&!Presentation->UsesSharedBodyWeaponDisplay(); // 兼容原独立双臂；附着短闪不改变Projectile桥资格。
    UPrimitiveComponent* Source=Config.StaticMesh
        ?static_cast<UPrimitiveComponent*>(bFP?StaticWeaponMesh.Get():Presentation->GetThirdPersonStaticWeaponMesh())
        :static_cast<UPrimitiveComponent*>(bFP?WeaponMesh.Get():Presentation->GetThirdPersonWeaponMesh());
    const bool bMatching=Config.StaticMesh?Source&&Cast<UStaticMeshComponent>(Source)->GetStaticMesh()==Config.StaticMesh
        :Source&&Cast<USkeletalMeshComponent>(Source)->GetSkeletalMeshAsset()==Config.Mesh;
    if(!bMatching||!Source->IsVisible()||Source->bHiddenInGame||Source->GetOwner()->IsHidden()||!Source->DoesSocketExist(Config.MuzzleFlashSocket))
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MUZZLE_FLASH_NO_SOURCE mesh/visibility/socket")); return nullptr; }
    return Source;
}
void ADemoWeaponBase::PlayMuzzleFlash(bool bScopedAtFire)
{
    DEMO_LOG_CALL();
    if(GetNetMode()==NM_DedicatedServer||!Config.MuzzleEffect)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MUZZLE_FLASH_SKIP dedicated/no asset")); return; }
    UPrimitiveComponent* Source=GetMuzzleFlashComponent(bScopedAtFire); // 唯一可见枪源，不取物理安全起点/F0或Weapon Actor旋转。
    if(!Source)
    { UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_FLASH_SKIP no current visible socket weapon=%s socket=%s"),*GetName(),*Config.MuzzleFlashSocket.ToString()); return; }
    for(int32 Index=MuzzleFlashes.Num()-1;Index>=0;--Index)
        if(!MuzzleFlashes[Index].IsValid()) MuzzleFlashes.RemoveAtSwap(Index); // 自动销毁实例不被弱表保活，按发射边沿清理。
    UParticleSystemComponent* Flash=SpawnAttachedMuzzleFlash(Config.MuzzleEffect,Source,Config.MuzzleFlashSocket);
    if(!Flash) { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SPAWN_FAILED weapon=%s"),*GetName()); return; }
    MuzzleFlashes.Add(Flash);
    // Low仅显示原粒子。微光沿同一可见性/镜内门禁，无VSM、体积散射或间接光贡献。
    if (Scalability::GetQualityLevels().EffectsQuality > 0)
    {
        if (!MuzzleLight)
        {
            MuzzleLight = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
            MuzzleLight->SetMobility(EComponentMobility::Movable);
            MuzzleLight->SetCastShadows(false);
            MuzzleLight->SetVolumetricScatteringIntensity(0.f);
            MuzzleLight->SetIndirectLightingIntensity(0.f);
            MuzzleLight->IntensityUnits = ELightUnits::Lumens;
            MuzzleLight->SetIntensity(45.f);
            MuzzleLight->SetAttenuationRadius(95.f);
            MuzzleLight->SetLightColor(FLinearColor(1.f, .52f, .20f));
            MuzzleLight->SetupAttachment(Source, Config.MuzzleFlashSocket);
            MuzzleLight->RegisterComponent();
            // GT弱UObject委托；仅本枪微光存活期订阅，释放时撤销，不保存跨线程捕获。
            MuzzleLightQualityHandle=Scalability::OnScalabilitySettingsChanged.AddUObject(this,&ADemoWeaponBase::OnMuzzleLightQualityChanged);
            MuzzleLightMonitor=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ADemoWeaponBase::PollMuzzleLightEligibility));
            UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_LIGHT_READY weapon=%s lm=45 radius_cm=95 duration_game_s=0.055 shadows=0 volumetric=0 indirect_scale=0"),*GetName());
        }
        else
        {
            MuzzleLight->AttachToComponent(Source, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Config.MuzzleFlashSocket);
        }
        GetWorldTimerManager().SetTimer(MuzzleLightExpiry, this, &ADemoWeaponBase::ReleaseMuzzleLight, .055f, false);
    }
    else { UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_LIGHT_SKIP weapon=%s reason=EffectsLow"),*GetName()); ReleaseMuzzleLight(); }
    UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_FLASH_ATTACHED weapon=%s source=%s socket=%s relative=identity scale=inherited_once"),
        *GetName(),*Source->GetName(),*Config.MuzzleFlashSocket.ToString());
}
UParticleSystemComponent* ADemoWeaponBase::SpawnAttachedMuzzleFlash(UParticleSystem* Template,UPrimitiveComponent* Source,FName Socket)
{
    DEMO_LOG_STATIC_CALL(Source);
    if(!IsValid(Template)||!IsValid(Source)||!Source->GetWorld()||Source->GetWorld()->GetNetMode()==NM_DedicatedServer||!Source->DoesSocketExist(Socket))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_FACTORY_REJECT template/world/socket")); return nullptr; }
    UParticleSystemComponent* Flash=UGameplayStatics::SpawnEmitterAttached(Template,Source,Socket,
        FVector::ZeroVector,FRotator::ZeroRotator,FVector::OneVector,EAttachLocation::KeepRelativeOffset,true,EPSCPoolMethod::None,false);
    if(!Flash) { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_FACTORY_REJECT engine spawn failed")); return nullptr; }
    Flash->SetOnlyOwnerSee(Source->bOnlyOwnerSee); Flash->SetOwnerNoSee(Source->bOwnerNoSee); // FP/TP各观察者沿可见枪策略，不泄露隐藏FP火光。
    Flash->SetCollisionEnabled(ECollisionEnabled::NoCollision); Flash->SetCastShadow(false);
    Flash->AddTickPrerequisiteComponent(Source); // 机械骨更新先于粒子；局部空间粒子也随随后Camera/父变换更新，不冻结世界点。
    Flash->ActivateSystem(true);
    return Flash;
}
void ADemoWeaponBase::ClearMuzzleFlashes()
{
    DEMO_LOG_CALL();
    GetWorldTimerManager().ClearTimer(MuzzleLightExpiry);
    ReleaseMuzzleLight();
    for(const TWeakObjectPtr<UParticleSystemComponent>& Entry:MuzzleFlashes)
        if(UParticleSystemComponent* Flash=Entry.Get()) { Flash->DeactivateImmediate(); Flash->DestroyComponent(); }
    MuzzleFlashes.Reset(); // 只清本枪创建的短闪；原世界弹/尾迹、队友资产与HP均保持。
}

void ADemoWeaponBase::ReleaseMuzzleLight()
{
    DEMO_LOG_CALL();
    if (GetWorld()) GetWorldTimerManager().ClearTimer(MuzzleLightExpiry);
    if (MuzzleLightQualityHandle.IsValid()) { Scalability::OnScalabilitySettingsChanged.Remove(MuzzleLightQualityHandle); MuzzleLightQualityHandle.Reset(); }
    if (MuzzleLightMonitor.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(MuzzleLightMonitor); MuzzleLightMonitor.Reset(); }
    if (MuzzleLight)
    {
        MuzzleLight->DestroyComponent(); MuzzleLight=nullptr;
        UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_LIGHT_RESTORE weapon=%s reason=ExpiryOrVisibilityOrQualityCleanup"),*GetName());
    }
}

void ADemoWeaponBase::OnMuzzleLightQualityChanged(const Scalability::FQualityLevels& Quality)
{
    DEMO_LOG_CALL();
    if (Quality.EffectsQuality<=0)
    { UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_LIGHT_RESTORE weapon=%s reason=QualityChangedToLow"),*GetName()); ReleaseMuzzleLight(); }
}

bool ADemoWeaponBase::PollMuzzleLightEligibility(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (!MuzzleLight) { MuzzleLightMonitor.Reset(); return false; }
    if (!GetWorld() || GetWorld()->bIsTearingDown || Scalability::GetQualityLevels().EffectsQuality<=0 || !GetMuzzleFlashComponent(false))
    { UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_LIGHT_RESTORE weapon=%s reason=QualityOrVisibleSourceLost"),*GetName()); ReleaseMuzzleLight(); return false; }
    return true; // 55ms仍由原World游戏时间Timer决定；暂停不会伪称真实55ms寿命，但Low/可见性门仍可撤光。
}

bool ADemoWeaponBase::CanUseIndependentVisualMuzzle() const
{
    DEMO_LOG_TICK();
    const UDemoPlayerPresentationComponent* Presentation = Wielder.IsValid() ? Wielder->GetPlayerPresentation() : nullptr; // 只读当前Avatar配置。
    const APlayerController* PC = Wielder.IsValid() ? Cast<APlayerController>(Wielder->GetController()) : nullptr; // 观战/外部相机不能借隐藏本人枪。
    const USceneComponent* FP = Config.StaticMesh ? static_cast<const USceneComponent*>(StaticWeaponMesh) : WeaponMesh.Get();
    const bool bMeshMatches=Config.StaticMesh?StaticWeaponMesh && StaticWeaponMesh->GetStaticMesh()==Config.StaticMesh
        : WeaponMesh && WeaponMesh->GetSkeletalMeshAsset()==Config.Mesh;
    return Presentation && Presentation->UsesIndependentFirstPersonWeapons() && Wielder->IsLocallyControlled()
        && PC && PC->GetViewTarget()==Wielder.Get() && Wielder->GetWeaponComponent()
        && Wielder->GetWeaponComponent()->IsEquipped(this) && Wielder->GetWeaponComponent()->GetScopeLevel()==0
        && !Wielder->IsCoopEliminated() && bMeshMatches && FP && (FP->DoesSocketExist(Config.MuzzleFlashSocket) || FP->DoesSocketExist(Config.MuzzleSocket));
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
    // 最后一发也先完成枪机周期；动画中断或切枪不能绕过这个权威时间门槛。
    return bInitialized && bIsEquipped && !bReloading && GetBoltCycleRemaining() <= KINDA_SMALL_NUMBER
        && Ammo < GetCapacity() && (Config.bInfiniteReserve || ReserveAmmo > 0);
}
bool ADemoWeaponBase::BeginReload()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !CanReload()) { UE_LOG(LogFPSDemo, Log, TEXT("Reload rejected: full/no reserve/already reloading")); return false; }
    bReloading = true;
    ++ReloadSequence;
    bReloadAmmoInserted = false;
    const float BaseDuration=Ammo==0 ? (Config.bReloadOneRoundAtATime?Config.EmptyShellReloadSeconds:Config.EmptyReloadSeconds>0.f?Config.EmptyReloadSeconds:Config.ReloadSeconds) : Config.ReloadSeconds; // 开始时冻结空仓分支；两视角共享本次权威时长。
    ReloadDuration = BaseDuration * (DemoGambleEffects::Has(Wielder.IsValid()?Wielder->GetAbilitySystemComponent():nullptr,DemoGamble::Relic(0))?1.f+GetDefault<UDemoGambleSettings>()->ReloadPenalty:1.f); // 遗物代价仍统一应用，不由动画Notify决定补弹。
    ReloadServerStartTime = GetWorld()->GetTimeSeconds(); // 权威World时钟由GameState同步给拥有者对齐表现。
    ForceNetUpdate();
    bEmptyReload = Ammo == 0; // AmmoPerShot不足但尚有弹时选普通换弹，不引入隐式膛内弹规则。
    GetWorldTimerManager().ClearTimer(PoseTimer);
    if (!Config.WeaponAnimLayerClass && Wielder.IsValid() && Config.ReloadAnimation) Wielder->GetMesh1P()->PlayAnimation(Config.ReloadAnimation, false);
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_RELOAD_BEGIN %s seq=%d seconds=%.2f empty=%d"), *GetName(), ReloadSequence, ReloadDuration, bEmptyReload);
    return true;
}
bool ADemoWeaponBase::CompleteReload()
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bReloading || !bIsEquipped || !Wielder.IsValid() || !Wielder->CanUseFirearmActions()
        || !Wielder->GetDemoAttributes() || Wielder->GetDemoAttributes()->GetHealth() <= 0.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("Reload completion rejected: stale instance/state")); return false; }
    if (!bReloadAmmoInserted)
    { UE_LOG(LogFPSDemo, Log, TEXT("Reload completion rejected: insertion pending")); return false; }
    bReloading = false;
    if (Wielder.IsValid() && Wielder->GetWeaponAnimationComponent()) Wielder->GetWeaponAnimationComponent()->EndReloadPresentation(this, ReloadSequence, true);
    RestoreIdle();
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_RELOAD_COMPLETE %s seq=%d ammo=%d reserve=%d"), *GetName(), ReloadSequence, Ammo, ReserveAmmo);
    return true;
}
bool ADemoWeaponBase::InsertReloadShell(int32 ExpectedSequence)
{
    DEMO_LOG_CALL();
    if (!Config.bReloadOneRoundAtATime) { UE_LOG(LogFPSDemo,Log,TEXT("Shell insertion rejected: not a tube reload")); return false; }
    return CommitReloadInsertion(ExpectedSequence);
}
float ADemoWeaponBase::GetReloadInsertionSeconds() const
{
    DEMO_LOG_TICK();
    if (Config.bReloadOneRoundAtATime) return ReloadDuration * Config.ShellInsertPhase;
    const UDemoWeaponLayerAnimInstance* Layer = Config.WeaponAnimLayerClass.GetDefaultObject(); // 读资产配置而非运行中的 AnimInstance，专服也能结算。
    const float Phase = Layer ? (bEmptyReload ? Layer->AnimSet.EmptyReload : Layer->AnimSet.TacticalReload).MagazineAttachPhase : .75f; // 旧枪没有成对动画时，按四分之三时长插入。
    return ReloadDuration * (FMath::IsFinite(Phase) && Phase > 0.f && Phase <= 1.f ? Phase : .75f);
}
bool ADemoWeaponBase::IsInMagazineRecovery() const
{
    DEMO_LOG_TICK();
    return bReloading && !Config.bReloadOneRoundAtATime && bReloadAmmoInserted;
}
bool ADemoWeaponBase::CommitReloadInsertion(int32 ExpectedSequence)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bReloading || !bIsEquipped || bReloadAmmoInserted
        || ExpectedSequence != ReloadSequence || !Wielder.IsValid() || !Wielder->CanUseFirearmActions()
        || !Wielder->GetDemoAttributes() || Wielder->GetDemoAttributes()->GetHealth() <= 0.f
        || GetWorld()->GetTimeSeconds() + KINDA_SMALL_NUMBER < ReloadServerStartTime + GetReloadInsertionSeconds())
    { UE_LOG(LogFPSDemo, Log, TEXT("RELOAD_INSERT_REJECT weapon=%s seq=%d expected=%d inserted=%d"), *GetName(), ReloadSequence, ExpectedSequence, bReloadAmmoInserted); return false; }
    const int32 Transfer = LoadFromReserve(Config.bReloadOneRoundAtATime ? 1 : GetCapacity()); // 入匣和入膛共用同一份权威弹药。
    if (!Transfer) { UE_LOG(LogFPSDemo,Log,TEXT("RELOAD_INSERT_REJECT no room or reserve")); return false; }
    bReloadAmmoInserted = true;
    UE_LOG(LogFPSDemo, Log, TEXT("RELOAD_INSERT_COMMIT weapon=%s seq=%d loaded=%d ammo=%d reserve=%d"), *GetName(), ReloadSequence, Transfer, Ammo, ReserveAmmo);
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
float ADemoWeaponBase::GetReloadServerStartTime() const { DEMO_LOG_TICK(); return ReloadServerStartTime; }
USkeletalMeshComponent* ADemoWeaponBase::GetWeaponSkeletalMesh() const { DEMO_LOG_TICK(); return WeaponMesh; }
UStaticMeshComponent* ADemoWeaponBase::GetWeaponStaticMesh() const { DEMO_LOG_TICK(); return StaticWeaponMesh; }
UStaticMeshComponent* ADemoWeaponBase::GetWeaponOpticMesh() const { DEMO_LOG_TICK(); return OpticMesh; }
void ADemoWeaponBase::ModifyAmmo(int32 Delta)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bInitialized) return;
    Ammo = static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(Ammo)+Delta, 0, GetCapacity()));
    UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_AMMO %s delta=%d ammo=%d"), *GetName(), Delta, Ammo);
}
int32 ADemoWeaponBase::LoadFromReserve(int32 Rounds)
{
    DEMO_LOG_CALL();
    const int32 Missing = FMath::Clamp(GetCapacity()-Ammo,0,Rounds); // 当前容量包含本轮升级；满仓时不会倒扣。
    const int32 Transfer = Config.bInfiniteReserve ? Missing : FMath::Min(Missing,ReserveAmmo);
    Ammo += Transfer;
    if (!Config.bInfiniteReserve) ReserveAmmo -= Transfer;
    if (Transfer > 0) ForceNetUpdate();
    return Transfer;
}
void ADemoWeaponBase::Refill(bool bFromReserveOnly)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bInitialized) return;
    if (bFromReserveOnly)
    {
        const int32 Loaded = LoadFromReserve(GetCapacity()); // 出发准备不触发手动装填的GA、节律或动画。
        if (Loaded) UE_LOG(LogFPSDemo,Log,TEXT("ENCOUNTER_MAGAZINE_LOADED weapon=%s loaded=%d ammo=%d reserve=%d"),*GetName(),Loaded,Ammo,ReserveAmmo);
        return;
    }
    Ammo = GetCapacity();
    ReserveAmmo = Config.InitialReserve;
}
int32 ADemoWeaponBase::TransferReserveFraction(float Fraction)
{
    DEMO_LOG_CALL();
    if(!HasAuthority()||!bInitialized||!bIsEquipped||bReloading||!FMath::IsFinite(Fraction)||Fraction<=0||Fraction>1)
    { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_DASHLOAD rejected weapon state/fraction")); return 0; }
    const int32 Requested=FMath::Min(FMath::Max(0,GetCapacity()-Ammo),FMath::Max(1,FMath::FloorToInt(GetCapacity()*Fraction))); // 遗物容量临界变化也不能出现负缺口而倒扣弹药。
    const int32 Transfer=Config.bInfiniteReserve?Requested:FMath::Min(Requested,ReserveAmmo); // 沿用武器无限备用配置，有限备用必须守恒。
    Ammo+=Transfer;if(!Config.bInfiniteReserve) ReserveAmmo-=Transfer;
    ForceNetUpdate(); UE_LOG(LogFPSDemo,Log,TEXT("TALENT_DASHLOAD transfer=%d ammo=%d reserve=%d"),Transfer,Ammo,ReserveAmmo);
    return Transfer;
}
int32 ADemoWeaponBase::TransferReserveRounds(int32 Rounds)
{
    DEMO_LOG_CALL();
    if(!HasAuthority()||!bInitialized||Rounds<=0||!Wielder.IsValid()) { UE_LOG(LogFPSDemo,Log,TEXT("BUILD_AMMO_TRANSFER_REJECT authority/weapon/rounds")); return 0; }
    const int32 Transfer=FMath::Min3(Rounds,FMath::Max(0,GetCapacity()-Ammo),FMath::Max(0,ReserveAmmo)); // 即使配置无限备用也从实际记录转移，不凭核心造弹。
    Ammo+=Transfer; ReserveAmmo-=Transfer; ForceNetUpdate(); return Transfer;
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
    Config.BaseDamage = 11.f; // 2026-11-20补强双管近距基础伤害：8×11=88，补偿固定2发与长换弹真空。
    Config.MagazineCapacity = Config.InitialAmmo = 2; // Drake-12 双管两膛；原生类默认与正式蓝图保持一致。
    Config.bFixedMagazineCapacity = true;
    Config.bReloadOneRoundAtATime = false;
    Config.ReloadSeconds = 3.f;
    Config.EmptyReloadSeconds = 3.5f;
    Config.SpreadHalfAngle = 6.f;
    Config.Range = 3000.f;
    Config.FalloffStart = 800.f; // 放宽至8m起衰减，与B08抵近区对齐。
    Config.MinimumDamageMultiplier = .3f; // 远距底线由20%提升至30%，减轻中距断崖。
}
int32 ADemoShotgunWeapon::GetPelletCount() const { DEMO_LOG_TICK(); return Config.PelletCount; }
ADemoSniperWeapon::ADemoSniperWeapon()
{
    DEMO_LOG_CALL();
    Config.DisplayName = FText::FromString(TEXT("狙击枪"));
    Config.FireMode = EDemoFireMode::SemiAutomatic;
    Config.RoundsPerMinute = 45.f;
    Config.BaseDamage = 115.f; // 2026-11-20补强拉栓狙单发基数（弱点230），匹配慢射速高单发定位。
    Config.MagazineCapacity = Config.InitialAmmo = 5;
    Config.ReloadSeconds = 2.5f;
    Config.SpreadHalfAngle = 3.f;
    Config.Range = Config.FalloffStart = 20000.f;
    Config.bSupportsScope = true;
    Config.bCycleBoltAfterShot = true; // 新建狙击BP的默认行为，现有BP由导入工具显式同步。
}

FDemoBoltCycleState ADemoWeaponBase::GetBoltCycleState() const
{
    DEMO_LOG_TICK();
    return BoltCycleState;
}

FDemoBoltCycleState ADemoWeaponBase::GetShotPresentationState() const
{
    DEMO_LOG_TICK();
    return BoltCycleState; // 同一复制记录的语义化只读入口，旧狙击接口继续兼容。
}

float ADemoWeaponBase::GetBoltCycleRemaining() const
{
    DEMO_LOG_TICK();
    if (!Config.bCycleBoltAfterShot || BoltCycleState.Sequence == 0 || !GetWorld()) return 0.f;
    const AGameStateBase* State = GetWorld()->GetGameState(); // 客户端借用同步时钟；权威直接使用自己的World。
    const double Now = !HasAuthority() && State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds(); // 游戏秒，暂停/时间缩放一致。
    return FMath::Max(0.f, static_cast<float>(BoltCycleState.ServerStartTime + BoltCycleState.Duration - Now));
}

void ADemoWeaponBase::OnRep_BoltCycle()
{
    DEMO_LOG_CALL();
    if (!Wielder.IsValid()) Wielder = Cast<ADemoCharacter>(GetOwner());
    if (Config.bCycleBoltAfterShot && Wielder.IsValid() && Wielder->IsLocallyControlled() && Wielder->GetWeaponAnimationComponent())
        Wielder->GetWeaponAnimationComponent()->SyncBoltCycle(this);
}

void ADemoWeaponBase::RestoreAmmo(int32 SavedAmmo, int32 SavedReserve)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || !bInitialized) { UE_LOG(LogFPSDemo, Warning, TEXT("RestoreAmmo rejected: initialization/authority")); return; }
    Ammo = FMath::Clamp(SavedAmmo, 0, GetCapacity());
    ReserveAmmo = FMath::Clamp(SavedReserve, 0, 100000);
}

void ADemoWeaponBase::BroadcastProjectileTravel(const ADemoProjectileBase* Projectile, const FDemoProjectilePresentationSample& Sample)
{
    DEMO_LOG_TICK();
    if (!HasAuthority() || GetNetMode() == NM_Standalone || !IsValid(Projectile) || Projectile->GetOwner() != this
        || !DemoProjectilePresentation::IsValid(Sample)) return;
    const UDemoShotContext* Context = Projectile->GetShotContext(); // 只读取本权威弹冻结的阶段，不能拿当前GS版本给旧弹补签。
    if (!Context || !Context->IsFlightValid() || Context->GetLevelNumber()!=Sample.Launch.Level
        || Context->GetShotId()!=Sample.Launch.ShotId || Projectile->GetPelletIndex()!=Sample.Launch.Pellet) return;
    MulticastProjectileTravel(Projectile->GetClass(),Sample); // 复用25Hz通道，身份和段序号只影响表现，不预测命中。
}
void ADemoWeaponBase::MulticastProjectileTravel_Implementation(TSubclassOf<ADemoProjectileBase> Class, const FDemoProjectilePresentationSample& Sample)
{
    DEMO_LOG_TICK();
    if (HasAuthority()) return; // ListenServer已由权威弹更新本地亮条，不双播。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 客户端只读阶段，绝不由此发起伤害或生成带碰撞实体。
    const ADemoProjectileBase* Defaults = Class ? Class->GetDefaultObject<ADemoProjectileBase>() : nullptr; // 只读已Cook子类的外观资源。
    if (!State || (State->Phase != EDemoPhase::Combat && State->Phase != EDemoPhase::Intermission)
        || !DemoProjectilePresentation::IsValid(Sample) || State->PhaseRevision!=Sample.Launch.Phase
        || State->LevelNumber!=Sample.Launch.Level || !Defaults || State->GetServerWorldTimeSeconds()-Sample.ServerTime>.5f
        || State->GetServerWorldTimeSeconds()-Sample.ServerTime<-.1f)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_VISUAL_REJECT reason=sample structure/phase/time")); return; }
    PruneProjectilePresentation();
    if (!Wielder.IsValid()) Wielder = Cast<ADemoCharacter>(GetOwner()); // Actor Owner可能晚于路径包，缺拥有者时保留权威段。
    const FDemoProjectilePresentationKey Key{Sample.Launch.ShotId,Sample.Launch.Pellet}; // 每弹独立，不能把散弹其他粒当重复包。
    if (!ProjectileCosmetics.Contains(Key) && !ProjectileCosmeticBudget.AllowNew(ProjectileCosmetics.Num()))
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_VISUAL_FALLBACK reason=unknown identity budget latched")); return; }
    if (const FDemoProjectileCosmeticState* Existing=ProjectileCosmetics.Find(Key))
        if (Existing->bTerminal || Sample.Sequence<=Existing->Sequence) return; // 乱序/重复不能回放，也不重置桥接。
    if (!ProjectileCosmetics.Contains(Key) && !Sample.bFirst && ProjectileCosmetics.Num()<4096)
    {
        FDemoProjectileCosmeticState WorldOnly; // 没收到首段、也没有已建立的活弹桥时，只接受后续世界路径。
        WorldOnly.Created=GetWorld()->GetTimeSeconds(); WorldOnly.Phase=Sample.Launch.Phase;
        ProjectileCosmetics.Add(Key,WorldOnly);
    }
    const bool bWorldBoundary=DemoProjectilePresentation::IsWorldBoundary(Sample); // 成功包丢失后，任一后续弹射事实仍必须永久收紧FP桥。
    FVector VisualStart=ResolveProjectileVisual(Sample.Launch,Sample.Start,Sample.StartDistance,Defaults->Config,bWorldBoundary && !(Sample.bFirst && !Sample.bRicochetSegment && Sample.StartDistance==0.f));
    const FVector VisualEnd=ResolveProjectileVisual(Sample.Launch,Sample.End,Sample.EndDistance,Defaults->Config,bWorldBoundary);
    if (FDemoProjectileCosmeticState* Cosmetic=ProjectileCosmetics.Find(Key))
    {
        Cosmetic->Sequence=Sample.Sequence; Cosmetic->bTerminal=Sample.bTerminal;
        if (Cosmetic->Actor.IsValid()) Cosmetic->Actor->ReceivePresentationSample(Sample); // 同Actor/RPC乱序桥，不运行客户端碰撞。
    }
    else if (ProjectileCosmetics.Num()<4096) // Owner/发射确认晚到仍保存终态和序号，只能保守降级，不能随后重开FP。
    {
        FDemoProjectileCosmeticState Fallback;
        Fallback.Created=GetWorld()->GetTimeSeconds(); Fallback.Phase=Sample.Launch.Phase;
        Fallback.Sequence=Sample.Sequence; Fallback.bTerminal=Sample.bTerminal;
        ProjectileCosmetics.Add(Key,Fallback);
    }
    FDemoProjectileCosmeticState* VisualState=ProjectileCosmetics.Find(Key); // 表已按ShotId+Pellet及原时效门收敛，本次调用无异步/跨线程。
    if (!VisualState) return;
    VisualState->ResolveVisualStyle(Sample.Launch); // Shooter空也冻结普通回退，不能飞行中读当前装备变色。
    if (VisualState->ConsumeRicochet(Sample))
    {
        if (VisualState->IncomingBoundaryTracer.IsValid()) VisualState->IncomingBoundaryTracer->Destroy(); // 旧段停在球心，替换为精确表面边界，避免叠亮。
        if (Defaults->Config.bEnableTracer)
        {
            ADemoProjectileTracer* Incoming=GetWorld()->SpawnActor<ADemoProjectileTracer>(Sample.Launch.Origin,FRotator::ZeroRotator); // 成功弹射事实证明这段已实际飞过；首边界包丢失也可补齐，不预测未来。
            if (Incoming)
            {
                if (Incoming->Initialize(Defaults->Config,Sample.Launch.Origin,VisualState->VisualStyle,false))
                {
                    Incoming->UpdateSegment(Sample.Launch.Origin,Sample.RicochetPoint);
                    Incoming->Finish(Defaults->Config.IncomingTracerFadeOnRicochet);
                    VisualState->IncomingBoundaryTracer=Incoming;
                }
                else Incoming->Destroy();
            }
            else UE_LOG(LogFPSDemo,Warning,TEXT("PROJECTILE_RICOCHET_INCOMING spawn failed; gameplay unaffected"));
        }
        ADemoProjectileTracer::SpawnContact(GetWorld(),Defaults->Config,VisualState->VisualStyle,Sample.RicochetPoint,Sample.RicochetDirection,true);
    }
    if (VisualState->ConsumeEnvironmentImpact(Sample))
        ADemoProjectileTracer::SpawnContact(GetWorld(),Defaults->Config,VisualState->VisualStyle,Sample.EnvironmentPoint,Sample.EnvironmentNormal,false);
    if (VisualState->ConsumePiercingExit(Sample))
        ADemoProjectileTracer::SpawnContact(GetWorld(),Defaults->Config,VisualState->VisualStyle,Sample.PiercingExitPoint,Sample.PiercingExitDirection,false);
    if (!Defaults->Config.bEnableTracer) return; // 关Tracer仍消费真实身份/进度和独立接触闪光。
    if (FVector(Sample.End-Sample.Start).IsNearlyZero()) return; // 成功边界零长包只播接触，不制造尚未飞过的出射线。
    if (Sample.bRicochetSegment) VisualStart=Sample.RicochetPoint; // 所有后续段复用真实转折Origin，首包丢失仍能呈现整条出射路径。
    const FVector Delta=VisualEnd-VisualStart; // 原近枪桥或接触点到真实端点，不外推目标。
    if (Delta.IsNearlyZero()) return;
    // 2026-11-19 OWNER-FOLLOW（远端本人客户端）：CS2式构图下，FP桥接期间不再每包生成独立淡出段（会在世界旧位置残留），
    // 改为复用一条随枪口刷新的移动亮条；桥结束/真实边界/终态时在当时端点Finish。弹射段与旧贴枪管模式仍走原逐包路径。
    const UDemoPlayerPresentationComponent* FollowPresentation=Wielder.IsValid()?Wielder->GetPlayerPresentation():nullptr;
    const bool bFollowMode=FollowPresentation && !FollowPresentation->AimsFirstPersonBarrelAtCrosshair() && !Sample.bRicochetSegment;
    ADemoProjectileTracer* Follow=VisualState->FollowTracer.Get();
    if (bFollowMode && (Follow || !VisualStart.Equals(FVector(Sample.Start),.01f)))
    {
        if (!Follow)
        {
            Follow=GetWorld()->SpawnActor<ADemoProjectileTracer>(VisualStart,Delta.Rotation()); // World拥有，CancelAll/阶段清理覆盖。
            if (Follow && !Follow->Initialize(Defaults->Config,VisualStart,VisualState->VisualStyle,false)) { Follow->Destroy(); Follow=nullptr; }
            VisualState->FollowTracer=Follow;
        }
        if (Follow)
        {
            Follow->SetCrossSectionScale(GetProjectileVisualScale(Sample.Launch,Defaults->Config));
            Follow->UpdateSegment(VisualStart,VisualEnd);
            if (!VisualState->bBridge || Sample.bBoundary || Sample.bTerminal)
            {
                Follow->Finish(); VisualState->FollowTracer.Reset();
                if (Sample.bBoundary) VisualState->IncomingBoundaryTracer=Follow; // 成功弹射事实到达时仍可延长/替换入射段。
            }
            return;
        }
        UE_LOG(LogFPSDemo,Warning,TEXT("PROJECTILE_FOLLOW_TRACER spawn failed; per-sample fallback"));
    }
    ADemoProjectileTracer* Cosmetic = GetWorld()->SpawnActor<ADemoProjectileTracer>(VisualStart, Delta.Rotation()); // World拥有的短时纯表现；CancelAll清场统一销毁。
    if (!Cosmetic) return;
    if (!Cosmetic->Initialize(Defaults->Config, VisualStart,VisualState->VisualStyle,Sample.bRicochetSegment)) { Cosmetic->Destroy(); return; }
    Cosmetic->SetCrossSectionScale(GetProjectileVisualScale(Sample.Launch,Defaults->Config)); // 端点已映射，不可再将长轴乘FP比例。
    if (Sample.bFirst && !Sample.bRicochetSegment && Sample.StartDistance==0.f && !VisualStart.Equals(Sample.Start,.01f))
    {
        if (RegisterLaunchTracer(Sample.Launch,Cosmetic,Sample.EndDistance))
        {
            // 已飞出近枪区的部分独立绘制；2026-11-19 OWNER-CONVERGE：沿本人视觉收敛线（VisualStart→VisualEnd）而非视线中心物理段，
            // 只让出约60cm贴枪管短闪；转头时世界固定段仍不会从新枪口连到旧端点。
            const FVector TailStart=VisualStart+(VisualEnd-VisualStart).GetClampedToMaxSize(60.f);
            if (!TailStart.Equals(VisualEnd,.01f))
            {
                ADemoProjectileTracer* Tail=GetWorld()->SpawnActor<ADemoProjectileTracer>(TailStart,Delta.Rotation());
                if (Tail && Tail->Initialize(Defaults->Config,TailStart,VisualState->VisualStyle))
                { Tail->UpdateSegment(TailStart,VisualEnd); Tail->Finish(); }
                else if (Tail) Tail->Destroy();
            }
        }
        else Cosmetic->UpdateSegment(VisualStart,VisualEnd); // CS2式/迟到包：沿已解析的视觉线（无桥时即世界段）绘制首段。
    }
    else Cosmetic->UpdateTravel(VisualEnd,Delta.Size()); // 仅已确认飞行尾迹；枪口火光另挂专用Socket。
    Cosmetic->Finish();
    if (Sample.bBoundary && !Sample.bRicochetSegment && VisualState->LaunchTracer.Get()!=Cosmetic) VisualState->IncomingBoundaryTracer=Cosmetic; // 弱引用不延长寿命，收到成功弹射事实时延长入射淡出。
}

void ADemoWeaponBase::PruneProjectilePresentation()
{
    DEMO_LOG_TICK();
    const float Now=GetWorld()->GetTimeSeconds(); // World暂停冻结缓存寿命，不使用墙钟。
    if (Now<NextPresentationPrune) return;
    NextPresentationPrune=Now+1.f;
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    for (auto It=ProjectileCosmetics.CreateIterator();It;++It)
        if (Now-It.Value().Created>61.f || !State || It.Value().Phase!=State->PhaseRevision) It.RemoveCurrent();
    for (auto It=ProjectileMuzzles.CreateIterator();It;++It)
        if (It.Value().IsExpired(Now)) It.RemoveCurrent(); // F0已复制进单弹状态；短窗清理避免长战斗的256枪表饥饿。
}
FDemoAmmoVisualStyle ADemoWeaponBase::ResolveProjectileStyle(const FDemoProjectilePresentationLaunch& Launch)
{
    DEMO_LOG_TICK();
    const FDemoProjectilePresentationKey Key{Launch.ShotId,Launch.Pellet}; // 与已解析FP资格的同一身份，不重新捕获枪口或当前弹药。
    if (FDemoProjectileCosmeticState* Existing=ProjectileCosmetics.Find(Key)) return Existing->ResolveVisualStyle(Launch);
    if (!DemoProjectilePresentation::IsValid(Launch) || !ProjectileCosmeticBudget.AllowNew(ProjectileCosmetics.Num()))
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("PROJECTILE_STYLE_FALLBACK invalid identity/budget; normal visual"));
        return DemoAmmoVisuals::Default();
    }
    FDemoProjectileCosmeticState WorldOnly; // 仅在ResolveProjectileVisual之后仍无状态时进入，不能授予新的FP桥。
    WorldOnly.Created=GetWorld()->GetTimeSeconds(); WorldOnly.Phase=Launch.Phase;
    return ProjectileCosmetics.Add(Key,WorldOnly).ResolveVisualStyle(Launch);
}
void ADemoWeaponBase::ClearProjectilePresentation()
{
    DEMO_LOG_CALL();
    FWorldDelegates::OnWorldPostActorTick.Remove(MuzzleFrameHandle);
    MuzzleFrameHandle.Reset(); PendingMuzzleShots.Reset();
    ProjectileMuzzles.Reset(); ProjectileCosmetics.Reset(); ProjectileCosmeticBudget=FDemoProjectileCosmeticBudget(); NextPresentationPrune=0.f;
}
bool ADemoWeaponBase::IsProjectilePresentationTerminal(const FDemoProjectilePresentationLaunch& Launch) const
{
    DEMO_LOG_TICK();
    const FDemoProjectileCosmeticState* State=ProjectileCosmetics.Find({Launch.ShotId,Launch.Pellet});
    return State?State->bTerminal:ProjectileCosmeticBudget.bRejectUnknown; // 未知terminal即使满表无法留key，也不会在Actor晚到时重显。
}
void ADemoWeaponBase::RegisterProjectilePresentation(ADemoProjectileBase* Projectile,const FDemoProjectilePresentationLaunch& Launch)
{
    DEMO_LOG_TICK();
    if (!IsValid(Projectile) || Projectile->GetOwner()!=this || !Launch.ShotId.IsValid() || Launch.Pellet<0 || Launch.Pellet>=64) return;
    const FDemoProjectilePresentationKey Key{Launch.ShotId,Launch.Pellet};
    if (FDemoProjectileCosmeticState* State=ProjectileCosmetics.Find(Key)) State->Actor=Projectile;
}
void ADemoWeaponBase::CaptureProjectileMuzzle(const FGuid& ShotId,bool bScopedAtFire)
{
    DEMO_LOG_CALL();
    if (!ShotId.IsValid() || ProjectileMuzzles.Contains(ShotId) || GetNetMode()==NM_DedicatedServer) return;
    PruneProjectilePresentation();
    if (ProjectileMuzzles.Num()>=256) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_VISUAL_FALLBACK muzzle budget")); return; }
    if (!Wielder.IsValid()) Wielder=Cast<ADemoCharacter>(GetOwner());
    FDemoProjectileMuzzleReceipt Receipt; // 不复制本机F0，整枪发出后切枪不会重取Socket。
    Receipt.Created=GetWorld()->GetTimeSeconds();
    Receipt.bAllowed=!bScopedAtFire && CanUseIndependentVisualMuzzle();
    Receipt.Origin=GetVisualMuzzleLocation();
    Receipt.RenderScale=Receipt.bAllowed && Wielder.IsValid() && Wielder->GetPlayerPresentation()
        ?Wielder->GetPlayerPresentation()->GetFirstPersonRenderScale():1.f; // 纯本地F0事实，不进入Launch或RPC。
    if (!FMath::IsFinite(Receipt.RenderScale) || Receipt.RenderScale<=0.f || Receipt.RenderScale>1.f) Receipt.RenderScale=1.f;
    Receipt.CaptureFrame=GFrameCounter;
    ProjectileMuzzles.Add(ShotId,Receipt);
    if (Receipt.bAllowed)
    {
        PendingMuzzleShots.Add(ShotId);
        if (!MuzzleFrameHandle.IsValid())
            MuzzleFrameHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&ADemoWeaponBase::FinalizeMuzzleFrame);
    }
}
bool ADemoWeaponBase::RegisterLaunchTracer(const FDemoProjectilePresentationLaunch& Launch,ADemoProjectileTracer* FirstTracer,float TravelCm)
{
    DEMO_LOG_TICK();
    FDemoProjectileCosmeticState* State=ProjectileCosmetics.Find({Launch.ShotId,Launch.Pellet});
    const FDemoProjectileMuzzleReceipt* Receipt=ProjectileMuzzles.Find(Launch.ShotId);
    UPrimitiveComponent* Gun=GetMuzzleFlashComponent(false);
    // 2026-11-19 OWNER-CONVERGE（CS2式）：枪管不再对准准星时，沿枪管轴贴的短段会与“枪口→命中点”弹道形成折角，改由调用方按视觉收敛线绘制首段。
    const UDemoPlayerPresentationComponent* Presentation=Wielder.IsValid()?Wielder->GetPlayerPresentation():nullptr;
    if (!Presentation || !Presentation->AimsFirstPersonBarrelAtCrosshair())
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_AXIAL_FLASH_SKIP barrel not aimed at crosshair; draw converge segment")); return false; }
    if (!State || !IsValid(FirstTracer) || !Gun || !Receipt || !Receipt->bAllowed || GFrameCounter>Receipt->CaptureFrame+1)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_AXIAL_FLASH_SKIP late/ineligible")); return false; }
    // 限制出口长度，避免近眼的短枪亮条伸过屏幕中心。仅本人凭据有可借用的实际PC视点。
    const APlayerController* PC=Wielder.IsValid() ? Cast<APlayerController>(Wielder->GetController()) : nullptr;
    if (!PC) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_AXIAL_FLASH_SKIP no owner camera")); return false; }
    FVector Eye; FRotator View;
    PC->GetPlayerViewPoint(Eye,View);
    const double Depth=FVector::DotProduct(Gun->GetSocketLocation(Config.MuzzleFlashSocket)-Eye,View.Vector());
    const float Length=FMath::Min3(TravelCm,60.f*Receipt->RenderScale,static_cast<float>(Depth*.65));
    if (!FirstTracer->BindLaunchDisplay(this,Gun,Launch.Origin,State->LaunchRadius,Receipt->RenderScale,Length)) return false;
    State->LaunchTracer=FirstTracer;
    return true;
}
bool ADemoWeaponBase::TryGetLaunchDisplayMuzzle(const FVector& PhysicalOrigin,float Radius,FVector& OutMuzzle) const
{
    DEMO_LOG_TICK();
    if (!CanUseIndependentVisualMuzzle()) return false;
    OutMuzzle=GetVisualMuzzleLocation();
    return FVector::DistSquared(OutMuzzle,PhysicalOrigin)<=FMath::Square(200.f)
        && IsVisualBridgeClear(OutMuzzle,PhysicalOrigin,Radius);
}

void ADemoWeaponBase::FinalizeMuzzleFrame(UWorld* World,ELevelTick TickType,float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (World!=GetWorld()) return; // 全局委托在多PIE也只消费本World。
    FWorldDelegates::OnWorldPostActorTick.Remove(MuzzleFrameHandle);
    MuzzleFrameHandle.Reset();
    TArray<FGuid> Shots=MoveTemp(PendingMuzzleShots); // 仅本次提交；不扫描历史弹或重采上一帧。
    if (!CanUseIndependentVisualMuzzle()) return; // 本帧切枪/开镜保留发射快照。
    const FVector FinalMuzzle=GetVisualMuzzleLocation(); // 与附着MuzzleFlash相同的最终世界Socket。
    const ADemoGameState* GameState=World->GetGameState<ADemoGameState>();
    if (!GameState || FinalMuzzle.ContainsNaN()) return;
    for (const FGuid& Shot : Shots)
    {
        FDemoProjectileMuzzleReceipt* Receipt=ProjectileMuzzles.Find(Shot);
        if (!Receipt || Receipt->CaptureFrame!=GFrameCounter) continue;
        const FVector Previous=Receipt->Origin; // 供结构化实测区分冻结前后的枪姿。
        Receipt->Origin=FinalMuzzle;
        for (int32 Pellet=0; Pellet<64; ++Pellet) // 原生产协议的有界弹丸编号，不假设网络到达连续。
        {
            FDemoProjectileCosmeticState* State=ProjectileCosmetics.Find({Shot,Pellet});
            if (!State || State->Phase!=GameState->PhaseRevision || (!State->bBridge && !State->LaunchTracer.IsValid())) continue;
            const FVector Offset=FinalMuzzle-State->LaunchPhysicalOrigin;
            const bool bClear=Offset.SizeSquared()<=FMath::Square(200.f)
                && IsVisualBridgeClear(FinalMuzzle,State->LaunchPhysicalOrigin,State->LaunchRadius);
            if (!bClear) State->bBridge=false;
            if (State->bBridge) State->Offset=Offset;
            if (ADemoProjectileBase* Bullet=State->Actor.Get())
                if (!State->bTerminal) Bullet->RefreshLaunchVisual();
            if (ADemoProjectileTracer* First=State->LaunchTracer.Get())
            {
                if (!bClear) First->Destroy(); // 同帧新遮挡：撤销专属首段，后续真实世界段仍独立显示。
            }
        }
        UE_LOG(LogFPSDemo,Verbose,TEXT("PROJECTILE_MUZZLE_FRAME shot=%s frame=%llu early=%s final=%s"),
            *Shot.ToString(),GFrameCounter,*Previous.ToString(),*FinalMuzzle.ToString());
    }
}

bool ADemoWeaponBase::IsVisualBridgeClear(const FVector& Visual,const FVector& Physical,float Radius) const
{
    DEMO_LOG_TICK();
    if (!Wielder.IsValid() || Visual.ContainsNaN() || Physical.ContainsNaN() || !FMath::IsFinite(Radius) || Radius<.1f || Radius>20.f) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoProjectileCosmetic),false,Wielder.Get()); // 只查表现路径，不运行Hit/GE。
    Query.AddIgnoredActor(this);
    FHitResult Hit;
    const FCollisionShape Shape=FCollisionShape::MakeSphere(Radius);
    if (GetWorld()->SweepSingleByChannel(Hit,Wielder->GetSharedViewLocation(),Visual,FQuat::Identity,ECC_GameTraceChannel3,Shape,Query)) return false;
    if (GetWorld()->SweepSingleByChannel(Hit,Visual,Physical,FQuat::Identity,ECC_GameTraceChannel3,Shape,Query)
        && (Hit.bStartPenetrating || !Hit.Location.Equals(Physical,Radius+1.f))) return false;
    return true;
}
FVector ADemoWeaponBase::ResolveProjectileVisual(const FDemoProjectilePresentationLaunch& Launch,const FVector& Physical,float Distance,
    const FDemoProjectileConfig& ProjectileConfig,bool bBoundary)
{
    DEMO_LOG_TICK();
    if (GetNetMode()==NM_DedicatedServer || !Launch.ShotId.IsValid() || Launch.Pellet<0 || Launch.Pellet>=64 || Physical.ContainsNaN()
        || !FMath::IsFinite(Distance) || Distance<0.f) return Physical;
    if (!Wielder.IsValid()) Wielder=Cast<ADemoCharacter>(GetOwner());
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>();
    if (!Wielder.IsValid() || Launch.Shooter!=Wielder.Get() || !State || State->PhaseRevision!=Launch.Phase || State->LevelNumber!=Launch.Level) return Physical;
    const FDemoProjectilePresentationKey Key{Launch.ShotId,Launch.Pellet};
    FDemoProjectileCosmeticState* Cosmetic=ProjectileCosmetics.Find(Key);
    if (!Cosmetic)
    {
        PruneProjectilePresentation();
        if (!ProjectileCosmeticBudget.AllowNew(ProjectileCosmetics.Num())) return Physical; // 只降级表现，绝不删除/阻止物理弹。
        FDemoProjectileCosmeticState Initial;
        Initial.LaunchPhysicalOrigin=Launch.Origin;
        Initial.LaunchRadius=ProjectileConfig.CollisionRadius;
        Initial.Created=GetWorld()->GetTimeSeconds(); Initial.Phase=Launch.Phase;
        const FDemoProjectileMuzzleReceipt* Receipt=ProjectileMuzzles.Find(Launch.ShotId); // 未见确认开火/晚到时不伪造枪口首帧。
        const float Age=State->GetServerWorldTimeSeconds()-Launch.ServerTime;
        Initial.bBridge=Receipt && Receipt->bAllowed && !Launch.bScoped && FMath::IsFinite(Age) && Age>=-.1f && Age<=.5f;
        // 2026-11-19 OWNER-CONVERGE：用户实机反馈弹道“从屏幕中心发射”。旧桥只在120cm内回到视线中心射线；改为沿真实弹道一直收敛到准星首个阻挡深度。
        if (Initial.bBridge) Initial.ConvergeDistance=ComputeVisualConvergeDistance(Launch,ProjectileConfig);
        Initial.bBridge=Initial.bBridge && Distance<Initial.ConvergeDistance;
        if (Initial.bBridge)
        {
            Initial.Offset=Receipt->Origin-FVector(Launch.Origin);
            Initial.bBridge=!Initial.Offset.ContainsNaN() && Initial.Offset.SizeSquared()<=FMath::Square(200.f);
            Initial.InitialRenderScale=Receipt->RenderScale;
        }
        Initial.FurthestDistance=Distance;
        Cosmetic=&ProjectileCosmetics.Add(Key,Initial);
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_VISUAL_BEGIN shot=%s pellet=%d bridge=%d offset=%.3f scale=%.4f converge=%.1f"),
            *Launch.ShotId.ToString(),Launch.Pellet,Initial.bBridge,Initial.Offset.Size(),Initial.InitialRenderScale,Initial.ConvergeDistance);
    }
    Cosmetic->FurthestDistance=FMath::Max(Cosmetic->FurthestDistance,Distance); // 根复制与段RPC交错也只向world尺寸收敛。
    const APlayerController* PC=Cast<APlayerController>(Wielder->GetController());
    if (bBoundary || Cosmetic->bTerminal || Distance>=Cosmetic->ConvergeDistance
        || !PC || PC->GetViewTarget()!=Wielder.Get()) Cosmetic->bBridge=false;
    if (!Cosmetic->bBridge) return Physical;
    // 2026-11-19 OWNER-FOLLOW：用户反馈“移动时射击后火光残留在上一帧位置，子弹不同步”。收敛桥每次解析都以当前可见枪口刷新偏移，
    // 近端随枪移动；权重(1-s/D)使越远影响越小，到汇合点仍严格回到物理弹道。切枪/开镜/外视点时停止刷新、保留最后偏移；200cm门不变。
    if (CanUseIndependentVisualMuzzle())
    {
        const FVector Follow=GetVisualMuzzleLocation()-Cosmetic->LaunchPhysicalOrigin; // 当前FP枪口-冻结物理起点，世界cm，仅本机渲染。
        if (!Follow.ContainsNaN() && Follow.SizeSquared()<=FMath::Square(200.f)) Cosmetic->Offset=Follow;
        else UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_VISUAL_FOLLOW_SKIP shot=%s pellet=%d offset out of bound"),*Launch.ShotId.ToString(),Launch.Pellet);
    }
    const FVector Visual=DemoProjectilePresentation::Converge(Physical,Cosmetic->Offset,Distance,Cosmetic->ConvergeDistance);
    if (!IsVisualBridgeClear(Visual,Physical,ProjectileConfig.CollisionRadius))
    {
        Cosmetic->bBridge=false;
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_VISUAL_FALLBACK shot=%s pellet=%d reason=obstruction"),*Launch.ShotId.ToString(),Launch.Pellet);
        return Physical;
    }
    return Visual;
}
float ADemoWeaponBase::GetProjectileVisualScale(const FDemoProjectilePresentationLaunch& Launch,const FDemoProjectileConfig& ProjectileConfig) const
{
    DEMO_LOG_TICK();
    const FDemoProjectileCosmeticState* Cosmetic=ProjectileCosmetics.Find({Launch.ShotId,Launch.Pellet}); // 必须先Resolve建立/收紧资格，不凭当前枪创建新桥。
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr;
    const APlayerController* PC=Wielder.IsValid()?Cast<APlayerController>(Wielder->GetController()):nullptr;
    if (!Cosmetic || !Cosmetic->bBridge || Cosmetic->bTerminal || !Wielder.IsValid() || Launch.Shooter!=Wielder.Get()
        || !PC || !PC->IsLocalController() || PC->GetViewTarget()!=Wielder.Get() || !State
        || State->PhaseRevision!=Launch.Phase || State->LevelNumber!=Launch.Level || Launch.bScoped) return 1.f;
    return DemoProjectilePresentation::RenderScale(Cosmetic->InitialRenderScale,Cosmetic->FurthestDistance,Cosmetic->ConvergeDistance); // 尺寸与位置同一收敛深度归并到世界1，不在1.2m处突变。
}
float ADemoWeaponBase::ComputeVisualConvergeDistance(const FDemoProjectilePresentationLaunch& Launch,const FDemoProjectileConfig& ProjectileConfig) const
{
    DEMO_LOG_CALL();
    const float MinDistance=FMath::Max(10.f,ProjectileConfig.OwnerMuzzleBlendDistance); // 旧近枪归并距离保留为下限，贴脸目标仍至少有一小段从枪口出发。
    const float MaxDistance=FMath::Max(MinDistance,Config.Range); // 本枪射程cm；物理弹不会飞得更远。
    const FVector Direction=FVector(Launch.Direction).GetSafeNormal();
    if (!GetWorld() || Direction.IsNearlyZero() || FVector(Launch.Origin).ContainsNaN())
    { UE_LOG(LogFPSDemo,Warning,TEXT("PROJECTILE_CONVERGE_FALLBACK invalid launch; using minimum")); return MinDistance; }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoProjectileConverge),false,Wielder.Get()); // 与实体弹相同通道/半径的只读预览，不结算命中。
    Query.AddIgnoredActor(this);
    FHitResult Hit;
    const FVector Start=Launch.Origin;
    const bool bHit=GetWorld()->SweepSingleByChannel(Hit,Start,Start+Direction*MaxDistance,FQuat::Identity,ECC_GameTraceChannel3,
        FCollisionShape::MakeSphere(FMath::Clamp(ProjectileConfig.CollisionRadius,.1f,20.f)),Query);
    const float Result=FMath::Clamp(bHit && !Hit.bStartPenetrating ? static_cast<float>(Hit.Distance) : MaxDistance,MinDistance,MaxDistance);
    UE_LOG(LogFPSDemo,Verbose,TEXT("PROJECTILE_CONVERGE shot=%s pellet=%d hit=%d distance=%.1f"),*Launch.ShotId.ToString(),Launch.Pellet,bHit,Result);
    return Result;
}

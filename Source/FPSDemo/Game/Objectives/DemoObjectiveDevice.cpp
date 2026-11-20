// 职责：设施GAS损血、破坏事实与命中反馈。真实损血复用Pawn回执/金属池（按剩余血量选三档命中声，素材V11专业库；摧毁爆炸V8实录），复制HP驱动本地材质短闪，
// 复制破坏边沿各客户端本地播摧毁爆炸；HUD投影标签，不执行敌人奖励或跨会话DOT。
#include "Game/Objectives/DemoObjectiveDevice.h"
#include "AbilitySystemComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Characters/DemoCharacter.h"
#include "Combat/Feedback/DemoHitFeedbackComponent.h"
#include "Combat/Feedback/DemoHitFlash.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/Flow/DemoCombatSession.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "Debug/DemoLog.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Combat/Feedback/DemoCombatFeelSettings.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"

UDemoObjectiveBurnEffect::UDemoObjectiveBurnEffect()
{
    DEMO_LOG_CALL();
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(4.f);
    Period = FScalableFloat(1.f);
    bExecutePeriodicEffectOnApplication = false;
    StackingType = EGameplayEffectStackingType::AggregateBySource;
    StackLimitCount = 32;
    StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
    StackPeriodResetPolicy = EGameplayEffectStackingPeriodPolicy::NeverReset;
    StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;
    FSetByCallerFloat Magnitude; // Spec写入负HP，不复用只能施加于Enemy的烧伤Requirement。
    Magnitude.DataTag = DemoTags::Magnitude;
    FGameplayModifierInfo Modifier; // 不含冻结/减速/爆炸/击杀天赋，设施只有周期Health GE。
    Modifier.Attribute = UDemoAttributeSet::GetHealthAttribute();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
    Modifiers.Add(Modifier);
}

ADemoObjectiveDevice::ADemoObjectiveDevice()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false; // 仅闪光的0.16秒更新；标签在HUD投影，不为朝向常开Actor Tick。
    bReplicates = true;
    bAlwaysRelevant = true; // 场内只有两个必需设施；全队需一致看见其存在/击碎，不依赖近距相关性。
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeviceBody"));
    SetRootComponent(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube")); // Engine合法基础资产，不写二进制。
    if (Cube.Succeeded()) Body->SetStaticMesh(Cube.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> HitMaterial(TEXT("/Game/Enemies/Breach/Materials/M_Enemy_Amber.M_Enemy_Amber")); // 复用正式敌人的EnemyTint发光材质，Cook可追踪，不制作二进制资产。
    if (HitMaterial.Succeeded()) Body->SetMaterial(0, HitMaterial.Object);
    Body->SetRelativeScale3D(FVector(1.2f, 1.2f, 1.8f));
    Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Body->SetCollisionObjectType(ECC_WorldDynamic);
    Body->SetCollisionResponseToAllChannels(ECR_Block);
    Body->SetCanEverAffectNavigation(true);
    Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ObjectiveLabel"));
    Label->SetupAttachment(Body);
    Label->SetAbsolute(false, true, true); // 文字不跟随设施网格缩放，WorldSize直接为cm。
    Label->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetWorldSize(24.f);
    Label->SetTextRenderColor(FColor::Orange);
    Label->SetHiddenInGame(true); // 运行时由屏幕投影文字替代，不让多人共享一个朝向。
    Label->SetVisibility(false);
    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("ObjectiveASC"));
    AbilitySystem->SetIsReplicated(true);
    AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
    Attributes = CreateDefaultSubobject<UDemoAttributeSet>(TEXT("ObjectiveAttributes"));
}

void ADemoObjectiveDevice::BeginPlay()
{
    DEMO_LOG_CALL();
    Super::BeginPlay();
    if (GetNetMode() != NM_DedicatedServer)
    {
        FlashMaterial = Body->CreateDynamicMaterialInstance(0); // 仅此设施拥有；绝不修改敌人共享材质或每次中弹新建实例。
        FLinearColor MaterialTint; // 检查实际材质契约，缺参数保留原外观并记录，不伪造闪光成功。
        if (FlashMaterial && FlashMaterial->GetVectorParameterValue(FMaterialParameterInfo(HitFlashParameter), MaterialTint))
            FlashMaterial->SetVectorParameterValue(HitFlashParameter, IdleTint);
        else
        {
            FlashMaterial = nullptr;
            UE_LOG(LogFPSDemo, Warning, TEXT("OBJECTIVE_FLASH_UNAVAILABLE actor=%s missing material/parameter=%s"), *GetName(), *HitFlashParameter.ToString());
        }
    }
    AbilitySystem->InitAbilityActorInfo(this, this);
    if (HasAuthority()) HealthBinding = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).AddUObject(this, &ADemoObjectiveDevice::HandleHealthChanged);
    OnRep_Presentation();
}

bool ADemoObjectiveDevice::PrepareDevice(UDemoCombatSession* InSession, float MaxHealth)
{
    DEMO_LOG_CALL();
    if (!HasAuthority() || Session.IsValid() || !IsValid(InSession) || !InSession->IsActive() || !FMath::IsFinite(MaxHealth) || MaxHealth <= 0.f)
    { UE_LOG(LogFPSDemo, Warning, TEXT("OBJECTIVE_DEVICE_PREPARE_REJECT authority/session/health")); return false; }
    Session = InSession;
    VisibleMaxHealth = MaxHealth;
    AbilitySystem->SetNumericAttributeBase(UDemoAttributeSet::GetMaxHealthAttribute(), MaxHealth);
    AbilitySystem->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(), MaxHealth);
    VisibleHealth = MaxHealth;
    if (!InSession->RegisterCleanup(this, FSimpleDelegate::CreateUObject(this, &ADemoObjectiveDevice::HandleSessionEnded))) return false;
    OnRep_Presentation();
    return true;
}

void ADemoObjectiveDevice::SetObjectiveActive(bool bActive)
{
    DEMO_LOG_CALL();
    if (!HasAuthority()) { UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_DEVICE_ACTIVE_REJECT client")); return; }
    bObjectiveActive = bActive && Session.IsValid() && Session->IsActive() && !bBroken;
    if (!bObjectiveActive) AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery()); // 设施ASC只持有自己的损血DOT，撤销不能影响玩家。
}

UAbilitySystemComponent* ADemoObjectiveDevice::GetAbilitySystemComponent() const { DEMO_LOG_TICK(); return AbilitySystem; }
bool ADemoObjectiveDevice::IsBroken() const { DEMO_LOG_TICK(); return bBroken; }

FDemoObjectiveDeviceView ADemoObjectiveDevice::GetStatusView() const
{
    DEMO_LOG_TICK();
    FDemoObjectiveDeviceView View; // 当前帧值快照；不缓存玩家/相机，也不暴露ASC可写接口。
    View.Anchor = Body->Bounds.Origin + FVector(0.f, 0.f, Body->Bounds.BoxExtent.Z + 28.f);
    View.Health = VisibleHealth; View.MaxHealth = VisibleMaxHealth; View.bDestroyed = bBroken;
    return View;
}

FDemoTargetDamageResult ADemoObjectiveDevice::ApplyTargetDamage(const FDemoTargetDamageRequest& Request)
{
    DEMO_LOG_CALL();
    FDemoTargetDamageResult Result; // 失败保持零，调用方不能据几何碰撞伪造实际伤害。
    if (!HasAuthority() || !bObjectiveActive || bBroken || !Session.IsValid() || !Session->IsActive() || Request.Session != Session.Get()
        || !IsValid(Request.SourceASC) || !Request.SourceASC->IsOwnerActorAuthoritative() || Request.SourceASC->GetAvatarActor() != Request.SourceAvatar
        || !IsValid(Request.SourceAvatar) || Request.SourceAvatar->GetWorld() != GetWorld() || Request.Hit.GetActor() != this
        || !Request.Hit.bBlockingHit || !Request.AttackId.IsValid() || !FMath::IsFinite(Request.Damage) || Request.Damage <= 0.f)
    { UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_DAMAGE_REJECT actor=%s authority/active/session/source/value"), *GetName()); return Result; }
    const float Before = Attributes->GetHealth(); // 同步GE前冻结HP，致死后仍可返回纯值。
    const uint32 FeedbackTargetId = GetUniqueID(); // 在同步破坏回调前冻结ID，表现无需查找或持有目标。
    const TWeakObjectPtr<ADemoCharacter> FeedbackSource(Cast<ADemoCharacter>(Request.SourceAvatar)); // 原攻击者弱引用，GE后重新检查，不取玩家0。
    FGameplayEffectContextHandle Context = Request.SourceASC->MakeEffectContext(); // 使用原玩家归属，但没有Enemy死亡事件。
    Context.AddHitResult(Request.Hit, true);
    FGameplayEffectSpecHandle Spec = Request.SourceASC->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(), 1.f, Context); // 不直接写Health，不改CDO。
    if (!Spec.IsValid()) { UE_LOG(LogFPSDemo, Warning, TEXT("OBJECTIVE_DAMAGE_REJECT invalid GE")); return Result; }
    Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude, -Request.Damage);
    Request.SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), AbilitySystem);
    Result.bAccepted = true;
    Result.ActualDamage = FMath::Clamp(Before - Attributes->GetHealth(), 0.f, Before);
    Result.bDestroyed = Before > 0.f && bBroken;
    if (Result.ActualDamage > 0.f && FeedbackSource.IsValid())
    {
        if (UDemoHitFeedbackComponent* Feedback = FeedbackSource->FindComponentByClass<UDemoHitFeedbackComponent>()) // 只回执原射手；散弹聚合、金属池与跨阶段末次命中交付复用原机制。
        {
            FDemoHitFeedbackEvent Receipt; // 纯值反馈不授予命中/击杀天赋或银币；DOT不会经过直击接口。
            Receipt.ShotId = Request.AttackId; Receipt.TargetId = FeedbackTargetId;
            Receipt.ImpactPoint = Request.Hit.ImpactPoint; Receipt.ImpactNormal = Request.Hit.ImpactNormal;
            Receipt.Damage = Result.ActualDamage; Receipt.bKilled = Result.bDestroyed;
            Receipt.bMetalTarget = true; Receipt.bMelee = Request.bMelee;
            Receipt.bRedesigned = Request.bRedesignedFeedback && !Request.bMelee;
            Receipt.Surface = EDemoImpactSurface::Device; Receipt.WeaponId = Request.WeaponId; // 仅世界接触，新候选也不把设施摧毁当怪物击杀短音。
            Receipt.DeviceHealthFraction = VisibleMaxHealth > 0.f ? FMath::Clamp(Attributes->GetHealth() / VisibleMaxHealth, 0.f, 1.f) : 0.f; // 扣血后剩余比例，仅服务器选三档命中声，不复制。
            Feedback->Confirm(Receipt);
        }
    }
    if (Result.ActualDamage > 0.f && !bBroken && bObjectiveActive && Session.IsValid() && Session->IsActive()
        && Request.AmmoType == 1 && Request.bApplyElement && Request.Ammo.Validate())
    {
        FGameplayEffectSpecHandle Burn = Request.SourceASC->MakeOutgoingSpec(UDemoObjectiveBurnEffect::StaticClass(), 1.f, Context); // 设施专属周期GE；控制类弹药只留直击。
        if (Burn.IsValid())
        {
            Burn.Data->SetDuration(Request.Ammo.BurnDuration, true);
            Burn.Data->Period = Request.Ammo.BurnPeriod;
            Burn.Data->SetSetByCallerMagnitude(DemoTags::Magnitude, -Request.Ammo.BurnDamagePerStack);
            Request.SourceASC->ApplyGameplayEffectSpecToTarget(*Burn.Data.Get(), AbilitySystem);
        }
    }
    UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_DAMAGE actor=%s attack=%s damage=%.2f broken=%d"), *GetName(), *Request.AttackId.ToString(), Result.ActualDamage, Result.bDestroyed);
    return Result;
}

void ADemoObjectiveDevice::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
    DEMO_LOG_CALL();
    VisibleHealth = FMath::Clamp(Data.NewValue, 0.f, VisibleMaxHealth);
    const bool bNewlyBroken = bObjectiveActive && !bBroken && VisibleHealth <= 0.f; // 准备时初始化属性不算损坏。
    if (bNewlyBroken) { bBroken = true; bObjectiveActive = false; } // 先闩锁，再复制表现/广播，重入不产生第二个完成事实。
    OnRep_Presentation();
    ForceNetUpdate();
    if (bNewlyBroken)
    {
        AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery());
        UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_DEVICE_BROKEN actor=%s"), *GetName());
        OnBroken.Broadcast(this);
    }
}

void ADemoObjectiveDevice::OnRep_Presentation()
{
    DEMO_LOG_CALL();
    if (bHasPresentedHealth && VisibleHealth < LastPresentedHealth && FlashMaterial
        && GetNetMode() != NM_DedicatedServer && FMath::IsFinite(HitFlashSeconds) && HitFlashSeconds > 0.f)
    {
        FlashDuration = FMath::Clamp(HitFlashSeconds, .01f, .4f); FlashRemaining = FlashDuration;
        FlashPeakTint = DemoHitFlash::PeakTint(false, bBroken || VisibleHealth <= 0.f);
        SetActorTickEnabled(true);
        UpdateHitFlash(0.f); // 与敌人相同的即时HDR峰值；只复制HP边沿，各客户端本地淡出。
        UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_HIT_FLASH actor=%s health=%.1f destroyed=%d"), *GetName(), VisibleHealth, bBroken);
    }
    if (bBroken && !bPresentedBroken && bHasPresentedHealth && GetNetMode() != NM_DedicatedServer)
        PlayDestroyExplosion(); // 只在本机已见过未破坏基线后的破坏边沿播放；迟加入首个快照即为已破坏时不补播。
    bPresentedBroken = bBroken;
    LastPresentedHealth = VisibleHealth; bHasPresentedHealth = true;
    Label->SetHiddenInGame(true); Label->SetVisibility(false); // 旧组件保留但不能重新显示背面文字。
    Body->SetCollisionEnabled(bBroken ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    Body->SetVisibility(!bBroken || FlashRemaining > 0.f); // 破坏立即失去碰撞；允许本地短闪结束，绝不延后服务器完成事实。
}

void ADemoObjectiveDevice::PlayDestroyExplosion()
{
    DEMO_LOG_CALL();
    const UDemoCombatFeelSettings* Feel = GetDefault<UDemoCombatFeelSettings>(); // CDO预载爆炸与衰减，摧毁时不读盘。
    USoundWave* Wave = Feel->SelectDeviceExplosion();
    if (!Wave) { UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_EXPLOSION_SKIPPED actor=%s no catalog wave"), *GetName()); return; }
    const FVector Location = Body->Bounds.Origin; // 网格包围盒中心世界cm；不附着，设施隐藏后尾音继续。
    UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(this, Wave, Location, FRotator::ZeroRotator,
        FMath::Clamp(Feel->DeviceExplosionVolume, 0.f, 1.f), FMath::FRandRange(.97f, 1.03f), 0.f, Feel->ExplosionAttenuation.Get());
    UE_LOG(LogFPSDemo, Log, TEXT("OBJECTIVE_EXPLOSION actor=%s wave=%s volume=%.2f point=%s active=%d"),
        *GetName(), *GetPathNameSafe(Wave), Feel->DeviceExplosionVolume, *Location.ToCompactString(), Audio && Audio->IsPlaying()); // active只证明引擎接受，不等于听感验收。
}

void ADemoObjectiveDevice::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    UpdateHitFlash(DeltaSeconds);
}

void ADemoObjectiveDevice::UpdateHitFlash(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    FlashRemaining = FMath::Max(0.f, FlashRemaining - FMath::Max(0.f, DeltaSeconds));
    if (FlashMaterial) FlashMaterial->SetVectorParameterValue(HitFlashParameter, FMath::Lerp(IdleTint, FlashPeakTint, DemoHitFlash::Strength(FlashRemaining, FlashDuration)));
    if (FlashRemaining <= 0.f)
    {
        SetActorTickEnabled(false);
        if (bBroken) Body->SetVisibility(false); // 仅结束残留外观，不调用Destroy/奖励/阶段推进。
    }
}

void ADemoObjectiveDevice::HandleSessionEnded() { DEMO_LOG_CALL(); SetObjectiveActive(false); }

void ADemoObjectiveDevice::EndPlay(const EEndPlayReason::Type Reason)
{
    DEMO_LOG_CALL();
    bObjectiveActive = false;
    UpdateHitFlash(FlashRemaining); // World/Actor卸载时精确恢复材质并停本地Tick。
    FlashMaterial = nullptr;
    AbilitySystem->GetGameplayAttributeValueChangeDelegate(UDemoAttributeSet::GetHealthAttribute()).Remove(HealthBinding);
    if (HasAuthority()) AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery());
    OnBroken.Clear();
    Super::EndPlay(Reason);
}

void ADemoObjectiveDevice::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    DEMO_LOG_CALL();
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADemoObjectiveDevice, VisibleHealth);
    DOREPLIFETIME(ADemoObjectiveDevice, VisibleMaxHealth);
    DOREPLIFETIME(ADemoObjectiveDevice, bBroken);
}

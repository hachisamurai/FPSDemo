#include "Weapons/Projectiles/DemoShotContext.h"
#include "Weapons/DemoWeaponConfig.h"
#include "Characters/DemoCharacter.h"
#include "Camera/CameraComponent.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

bool UDemoShotContext::Initialize(ADemoCharacter* Source, FName WeaponId, const FDemoWeaponConfig& Weapon,
    const UDemoEnemyHitProfile* Profile, int32 AmmoType, const FDemoAmmoEffectSnapshot& Ammo, float DamagePerPellet)
{
    DEMO_LOG_CALL();
    const UWorld* World = IsValid(Source) ? Source->GetWorld() : nullptr; // 仅借用来源World，不使用UObject Outer推测世界。
    const AFPSDemoGameMode* Mode = World ? World->GetAuthGameMode<AFPSDemoGameMode>() : nullptr; // 单人权威组合根。
    const ADemoGameState* State = World ? World->GetGameState<ADemoGameState>() : nullptr; // 当前关号冻结来源。
    FString Error; // Weapon校验输出，不持久化到子弹。
    if (bInitialized || !IsValid(Source) || !Source->HasAuthority() || !Source->GetFirstPersonCameraComponent()
        || !Source->GetAbilitySystemComponent() || !Mode || !Mode->GetRunFlow() || !State
        || State->Phase != EDemoPhase::Combat || State->LevelNumber <= 0 || Mode->GetRunFlow()->GetRunId().IsEmpty()
        || WeaponId.IsNone() || !Weapon.Validate(Error) || !Profile || !Profile->Validate()
        || AmmoType < 0 || AmmoType > 3 || (AmmoType != 0 && !Ammo.Validate())
        || !FMath::IsFinite(DamagePerPellet) || DamagePerPellet < 0.f)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("SHOT_CONTEXT_REJECT source=%s weapon=%s reason=%s"), *GetNameSafe(Source), *WeaponId.ToString(), *Error);
        return false;
    }
    SourcePawn = Source;
    SourceASC = Source->GetAbilitySystemComponent();
    StableWeaponId = WeaponId;
    ShotId = FGuid::NewGuid();
    RunId = Mode->GetRunFlow()->GetRunId();
    LevelNumber = State->LevelNumber;
    SelectedAmmoType = AmmoType;
    AmmoSnapshot = Ammo;
    PerPelletDamage = DamagePerPellet * (AmmoType == 3 ? Ammo.PiercingDamageMultiplier : 1.f);
    Range = Weapon.Range;
    FalloffStart = Weapon.FalloffStart;
    MinimumDamageMultiplier = Weapon.MinimumDamageMultiplier;
    CameraOrigin = Source->GetFirstPersonCameraComponent()->GetComponentLocation();
    // 复制资产规则本身；若飞行中外部修改原资产，已发射弹仍使用发射时的部位倍率。
    HitProfileSnapshot = NewObject<UDemoEnemyHitProfile>(this);
    HitProfileSnapshot->Regions = Profile->Regions;
    HitProfileSnapshot->NonHittableBones = Profile->NonHittableBones;
    bInitialized = FMath::IsFinite(PerPelletDamage) && !CameraOrigin.ContainsNaN();
    UE_LOG(LogFPSDemo, Log, TEXT("SHOT_CONTEXT_PREPARED shot=%s run=%s level=%d weapon=%s damage=%.2f ammo=%d"),
        *ShotId.ToString(), *RunId, LevelNumber, *StableWeaponId.ToString(), PerPelletDamage, SelectedAmmoType);
    return bInitialized && IsAttackValid();
}

bool UDemoShotContext::IsAttackValid() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Source = SourcePawn.Get(); // 弱引用解析仅限本同步调用。
    const UAbilitySystemComponent* ASC = SourceASC.Get(); // 当前Avatar必须仍是发射者，重生不能接管旧弹。
    const UWorld* World = IsValid(Source) ? Source->GetWorld() : nullptr; // 不借用已经卸载的World。
    const AFPSDemoGameMode* Mode = World ? World->GetAuthGameMode<AFPSDemoGameMode>() : nullptr; // 同World轮次真值。
    const ADemoGameState* State = World ? World->GetGameState<ADemoGameState>() : nullptr; // 同World阶段真值。
    return bInitialized && IsValid(Source) && Source->HasAuthority() && IsValid(ASC)
        && ASC->IsOwnerActorAuthoritative() && ASC->GetAvatarActor() == Source && Source->GetAbilitySystemComponent() == ASC
        && Source->GetDemoAttributes() && Source->GetDemoAttributes()->GetHealth() > 0.f
        && State && State->Phase == EDemoPhase::Combat && State->LevelNumber == LevelNumber
        && Mode && Mode->GetRunFlow() && Mode->GetRunFlow()->GetRunId() == RunId;
}

bool UDemoShotContext::TryMarkFeedback(AActor* Target)
{
    DEMO_LOG_CALL();
    if (!IsValid(Target) || FeedbackTargets.Contains(Target)) return false;
    FeedbackTargets.Add(Target); // 先登记，再允许调用方进入同步音效/动画事件。
    return true;
}

bool UDemoShotContext::TryMarkElement(AActor* Target)
{
    DEMO_LOG_CALL();
    if (!IsValid(Target) || ElementTargets.Contains(Target)) return false;
    ElementTargets.Add(Target); // 先登记，GAS首层添加/阈值爆炸可能立即重入。
    return true;
}

FName UDemoShotContext::GetWeaponId() const { DEMO_LOG_TICK(); return StableWeaponId; }
const FGuid& UDemoShotContext::GetShotId() const { DEMO_LOG_TICK(); return ShotId; }
int32 UDemoShotContext::GetLevelNumber() const { DEMO_LOG_TICK(); return LevelNumber; }
float UDemoShotContext::GetRange() const { DEMO_LOG_TICK(); return Range; }
int32 UDemoShotContext::GetAmmoType() const { DEMO_LOG_TICK(); return SelectedAmmoType; }
const FDemoAmmoEffectSnapshot& UDemoShotContext::GetAmmoSnapshot() const { DEMO_LOG_TICK(); return AmmoSnapshot; }
const UDemoEnemyHitProfile* UDemoShotContext::GetHitProfile() const { DEMO_LOG_TICK(); return HitProfileSnapshot; }
ADemoCharacter* UDemoShotContext::GetSourcePawn() const { DEMO_LOG_TICK(); return SourcePawn.Get(); }
UAbilitySystemComponent* UDemoShotContext::GetSourceASC() const { DEMO_LOG_TICK(); return SourceASC.Get(); }

float UDemoShotContext::GetFirstHitBaseDamage(const FVector& ImpactPoint) const
{
    DEMO_LOG_CALL();
    const float Distance = FVector::Distance(CameraOrigin, ImpactPoint); // 厘米；保持原相机起点衰减定义。
    const float Alpha = Range > FalloffStart ? FMath::Clamp((Distance - FalloffStart) / (Range - FalloffStart), 0.f, 1.f) : 0.f; // 线性衰减0..1。
    return PerPelletDamage * FMath::Lerp(1.f, MinimumDamageMultiplier, Alpha);
}

// 职责：一次开火的冻结伤害与来源凭据。使用：实体弹以阶段代数控制飞行，以CombatSession独立控制伤害；备战练枪没有伤害许可，不得跨阶段复用。
#include "Weapons/Projectiles/DemoShotContext.h"
#if WITH_EDITOR
#include "Debug/DemoEditorCombatPrototype.h" // 原型只观察成功发射时序，不改Shot值结构或正式网络协议。
#endif
#include "GAS/Talents/DemoTalentCatalog.h"
// 对应头文件先包含，确保UBT独立编译检查本类头文件自包含；功能逻辑不变。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Weapons/DemoWeaponConfig.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/Gambling/DemoCombatTalentComponent.h"
#include "Characters/DemoCharacter.h"
#include "Camera/CameraComponent.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Game/Flow/DemoCombatSession.h"
#include "GAS/DemoAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "AI/DemoEnemy.h"
#include "Combat/Feedback/DemoCombatFeelSettings.h"

bool UDemoShotContext::Initialize(ADemoCharacter* Source, FName WeaponId, const FDemoWeaponConfig& Weapon,
    const UDemoEnemyHitProfile* Profile, int32 AmmoType, const FDemoAmmoEffectSnapshot& Ammo, float DamagePerPellet)
{
    DEMO_LOG_CALL();
    const UWorld* World = IsValid(Source) ? Source->GetWorld() : nullptr; // 仅借用来源World，不使用UObject Outer推测世界。
    const AFPSDemoGameMode* Mode = World ? World->GetAuthGameMode<AFPSDemoGameMode>() : nullptr; // 单人权威组合根。
    ADemoGameState* State = World ? World->GetGameState<ADemoGameState>() : nullptr; // 当前关号/阶段代数冻结来源，只弱持有World所属对象。
    UDemoCombatSession* Session = Mode && Mode->GetRunFlow() ? Mode->GetRunFlow()->GetCombatSession() : nullptr; // 初始化时组合一次，以后只检查可撤销战斗凭据。
    const bool bCombat = State && State->Phase == EDemoPhase::Combat; // 本次发射伤害资格一经冻结，备战弹不能在下一关提升为Combat弹。
    FString Error; // Weapon校验输出，不持久化到子弹。
    if (bInitialized || !IsValid(Source) || !Source->HasAuthority() || !Source->GetFirstPersonCameraComponent()
        || !Source->GetAbilitySystemComponent() || !Mode || !Mode->GetRunFlow() || !State
        || !Source->CanUseFirearmActions() || State->LevelNumber <= 0 || Mode->GetRunFlow()->GetRunId().IsEmpty()
        || (bCombat && (!Session || !Session->IsActive()))
        || WeaponId.IsNone() || !Weapon.Validate(Error) || !Profile || !Profile->Validate()
        || AmmoType < 0 || AmmoType > 3 || (AmmoType != 0 && !Ammo.Validate())
        || !FMath::IsFinite(DamagePerPellet) || DamagePerPellet < 0.f)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("SHOT_CONTEXT_REJECT source=%s weapon=%s reason=%s"), *GetNameSafe(Source), *WeaponId.ToString(), *Error);
        return false;
    }
    CombatSession = bCombat ? Session : nullptr; // 备战即使碰上下一关预创建会话，也绝不借用或注册它。
    SourceState = State;
    PhaseRevision = State->PhaseRevision;
    bCombatShot = bCombat;
    SourcePawn = Source;
    SourceASC = Source->GetAbilitySystemComponent();
    StableWeaponId = WeaponId;
    bRedesignedFeedback = GetDefault<UDemoCombatFeelSettings>()->UseForWeapon(WeaponId);
    ShotId = FGuid::NewGuid();
    RunId = Mode->GetRunFlow()->GetRunId();
    LevelNumber = State->LevelNumber;
    SelectedAmmoType = AmmoType;
    AmmoSnapshot = Ammo;
    const auto* Gambling=GetDefault<UDemoGambleSettings>(); // 与弹药一起冻结，飞行中切换装备不改变旧弹。
    if(!Gambling->Validate()) { UE_LOG(LogFPSDemo,Warning,TEXT("SHOT_CONTEXT_REJECT invalid talent configuration")); return false; }
    const FDemoTalentModifiers Talents=GetDefault<UDemoTalentCatalog>()->Evaluate(SourceASC.Get()); // 发射冻结聚合策略，飞行中升级不追补。
    RicochetRatio=Talents.RicochetRatio; RicochetRadius=Talents.RicochetRadius;
    SiphonRatio=Talents.SiphonRatio; SiphonBudget=Talents.SiphonBudget; PrecisionRounds=Talents.PrecisionRounds;
    ExecuteThreshold=Talents.ExecuteThreshold; ExecuteMultiplier=Talents.ExecuteMultiplier;
    ADemoWeaponBase* WeaponActor=Source->GetWeaponComponent()->GetActiveWeapon(); // 返弹仅赠给实际发射的原枪。
    if(WeaponActor&&WeaponActor->GetWeaponId()==WeaponId) FiringWeapon=WeaponActor;
    AmmoSnapshot.ExplosionDamage*=Talents.ExplosionMultiplier; FrozenDamageMultiplier=Talents.FrozenMultiplier;
    AmmoSnapshot.SecondaryDamageRatio=1.f-(1.f-FMath::Clamp(AmmoSnapshot.SecondaryDamageRatio+Talents.PierceBonus,0.f,1.f))/Talents.PierceScale;
    if(DemoGambleEffects::Has(SourceASC.Get(),DemoGamble::Relic(1))) { AmmoSnapshot.BurnDuration+=Gambling->BurnExtraSeconds; AmmoSnapshot.ExplosionDamage*=1.f-Gambling->ExplosionPenalty; }
    PerPelletDamage = DamagePerPellet * (AmmoType == 3 ? Ammo.PiercingDamageMultiplier : 1.f);
    Range = Weapon.Range;
    FalloffStart = Weapon.FalloffStart;
    MinimumDamageMultiplier = Weapon.MinimumDamageMultiplier;
    CameraOrigin = Source->GetFirstPersonCameraComponent()->GetComponentLocation();
    // 复制资产规则本身；若飞行中外部修改原资产，已发射弹仍使用发射时的部位倍率。
    HitProfileSnapshot = NewObject<UDemoEnemyHitProfile>(this);
    HitProfileSnapshot->Regions = Profile->Regions;
    HitProfileSnapshot->NonHittableBones = Profile->NonHittableBones;
    RunBuild=UDemoRunBuildComponent::FindForPawn(Source); // 原PS组件借用，准备阶段只冻结不消费充能。
    if(bCombatShot && RunBuild.IsValid()) RunBuildShot=RunBuild->PreviewShot(FiringWeapon.Get()); // 备战不读取/消费战斗核心窗口，回退正常后坐与散布。
    bInitialized = FMath::IsFinite(PerPelletDamage) && !CameraOrigin.ContainsNaN();
    UE_LOG(LogFPSDemo, Log, TEXT("SHOT_CONTEXT_PREPARED shot=%s run=%s level=%d weapon=%s damage=%.2f ammo=%d combat=%d phaseRevision=%u"),
        *ShotId.ToString(), *RunId, LevelNumber, *StableWeaponId.ToString(), PerPelletDamage, SelectedAmmoType, bCombatShot, PhaseRevision);
    return bInitialized && IsFlightValid();
}

bool UDemoShotContext::IsFlightValid() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Source = SourcePawn.Get(); // 弱引用解析仅限本同步调用。
    const UAbilitySystemComponent* ASC = SourceASC.Get(); // 当前Avatar必须仍是发射者，重生不能接管旧弹。
    const ADemoGameState* State = SourceState.Get(); // 原GS持有单调阶段代数；同关Reward/Intermission/Combat往返不能复活旧弹。
    const UDemoCombatSession* Session = CombatSession.Get(); // Combat弹同时服从会话同步撤销，备战不要求存在战斗会话。
    return bInitialized && IsValid(Source) && Source->HasAuthority() && IsValid(ASC)
        && ASC->IsOwnerActorAuthoritative() && ASC->GetAvatarActor() == Source && Source->GetAbilitySystemComponent() == ASC
        && Source->GetDemoAttributes() && Source->GetDemoAttributes()->GetHealth() > 0.f
        && !Source->IsCoopEliminated() && IsValid(State) && State->GetWorld() == Source->GetWorld()
        && State->PhaseRevision == PhaseRevision && State->LevelNumber == LevelNumber
        && State->Phase == (bCombatShot ? EDemoPhase::Combat : EDemoPhase::Intermission)
        && (!bCombatShot || (Session && Session->Matches(RunId, LevelNumber)));
}
bool UDemoShotContext::IsAttackValid() const
{
    DEMO_LOG_TICK();
    const UDemoCombatSession* Session = CombatSession.Get(); // 伤害消费者继续使用旧接口，不能误用纯飞行门禁。
    return bCombatShot && Session && Session->Matches(RunId, LevelNumber) && IsFlightValid();
}
uint32 UDemoShotContext::GetPhaseRevision() const { DEMO_LOG_TICK(); return PhaseRevision; }

bool UDemoShotContext::TryMarkFeedback(AActor* Target)
{
    DEMO_LOG_CALL();
    if (!IsValid(Target) || FeedbackTargets.Contains(Target)) return false;
    FeedbackTargets.Add(Target); // 先登记，再允许调用方进入同步音效/动画事件。
    return true;
}

bool UDemoShotContext::TryUpgradeFeedback(AActor* Target, bool bWeakPoint, bool bFatal)
{
    DEMO_LOG_CALL();
    if (!bRedesignedFeedback || !IsValid(Target)) { UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_GRADE rejected source/target")); return false; }
    const uint8 Grade = bFatal ? 2 : bWeakPoint ? 1 : 0; // 最高优先级只升不降，不触碰HP或死亡状态。
    if (uint8* Existing = FeedbackGrades.Find(Target))
    {
        if (*Existing >= Grade) return false;
        *Existing = Grade; return true;
    }
    if (FeedbackGrades.Num() >= 128) { UE_LOG(LogFPSDemo, Warning, TEXT("FEEL_GRADE target budget exhausted")); return false; }
    FeedbackGrades.Add(Target, Grade); return true;
}
bool UDemoShotContext::UsesRedesignedFeedback() const { DEMO_LOG_TICK(); return bRedesignedFeedback; }

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
float UDemoShotContext::GetFrozenDamageMultiplier() const { DEMO_LOG_TICK(); return FrozenDamageMultiplier; }
float UDemoShotContext::GetRicochetRatio() const { DEMO_LOG_TICK(); return RicochetRatio; }
float UDemoShotContext::GetRicochetRadius() const { DEMO_LOG_TICK(); return RicochetRadius; }
float UDemoShotContext::GetRange() const { DEMO_LOG_TICK(); return Range; }
int32 UDemoShotContext::GetAmmoType() const { DEMO_LOG_TICK(); return SelectedAmmoType; }
const FDemoAmmoEffectSnapshot& UDemoShotContext::GetAmmoSnapshot() const { DEMO_LOG_TICK(); return AmmoSnapshot; }
const UDemoEnemyHitProfile* UDemoShotContext::GetHitProfile() const { DEMO_LOG_TICK(); return HitProfileSnapshot; }
ADemoCharacter* UDemoShotContext::GetSourcePawn() const { DEMO_LOG_TICK(); return SourcePawn.Get(); }
UAbilitySystemComponent* UDemoShotContext::GetSourceASC() const { DEMO_LOG_TICK(); return SourceASC.Get(); }

void UDemoShotContext::NotifyDirectHit(float ActualDamage,bool bCore)
{
    DEMO_LOG_CALL();
    if(!IsAttackValid()||!FMath::IsFinite(ActualDamage)||ActualDamage<=0.f)
    { UE_LOG(LogFPSDemo,Log,TEXT("TALENT_HIT_REJECT invalid shot/actual damage")); return; }
    ADemoCharacter* Pawn=SourcePawn.Get(); // IsAttackValid已经验证本调用中的弱Avatar。
    if(PrecisionRounds>0&&bCore&&!bPrecisionConsumed)
    {
        bPrecisionConsumed=true; // 先闩锁，多个弹丸或弹射核心仍只能回收一次，本次发数由发射时等级决定。
        if(FiringWeapon.IsValid()&&FiringWeapon->GetOwner()==Pawn) FiringWeapon->ModifyAmmo(FMath::Min(PrecisionRounds,FiringWeapon->GetCapacity())); // 先按容量截断避免极高等级整数加法溢出。
    }
    if(SiphonRatio>0.f)
        if(UDemoCombatTalentComponent* Talents=Pawn->FindComponentByClass<UDemoCombatTalentComponent>()) Talents->Siphon(ActualDamage,SiphonRatio,SiphonBudget); // Pawn组件汇总所有武器/弹丸共享预算。
}

float UDemoShotContext::GetFirstHitBaseDamage(const FVector& ImpactPoint) const
{
    DEMO_LOG_CALL();
    const float Distance = FVector::Distance(CameraOrigin, ImpactPoint); // 厘米；保持原相机起点衰减定义。
    const float Alpha = Range > FalloffStart ? FMath::Clamp((Distance - FalloffStart) / (Range - FalloffStart), 0.f, 1.f) : 0.f; // 线性衰减0..1。
    return PerPelletDamage * FMath::Lerp(1.f, MinimumDamageMultiplier, Alpha);
}

float UDemoShotContext::GetConditionalDamageMultiplier(float Health,float MaxHealth) const
{
    DEMO_LOG_TICK();
    return MaxHealth>0.f&&Health>0.f&&Health/MaxHealth<ExecuteThreshold?ExecuteMultiplier:1.f; // 严格低于阈值，命中前判定；DOT不调用。
}
UDemoCombatSession* UDemoShotContext::GetCombatSession() const { DEMO_LOG_TICK(); return CombatSession.Get(); }
float UDemoShotContext::GetRunBuildSpreadScale() const { DEMO_LOG_TICK(); return RunBuildShot.SpreadScale; }
float UDemoShotContext::GetRunBuildRecoilScale() const { DEMO_LOG_TICK(); return RunBuildShot.RecoilScale; }
void UDemoShotContext::CommitRunBuildShot()
{
    DEMO_LOG_CALL(); if(bRunBuildCommitted||!IsAttackValid()) return;
    bRunBuildCommitted=true;
#if WITH_EDITOR
    DemoEditorCombatPrototype::ShotCommitted().Broadcast(SourcePawn.Get(),ShotId); // 成功发射后的唯一整枪通知，近战前已在飞的ShotId不会被候选登记。
#endif
    if(RunBuild.IsValid()) RunBuild->CommitShot(RunBuildShot,ShotId,FiringWeapon.Get());
}
void UDemoShotContext::RegisterProjectile()
{
    DEMO_LOG_TICK(); check(IsInGameThread()); ++PendingProjectiles;
}
void UDemoShotContext::ResolveProjectile()
{
    DEMO_LOG_TICK(); check(IsInGameThread()); if(PendingProjectiles<=0) return;
    --PendingProjectiles;
    TryResolveShot();
}
void UDemoShotContext::BeginImpactResolution() { DEMO_LOG_TICK(); ++ImpactDepth; }
void UDemoShotContext::EndImpactResolution() { DEMO_LOG_TICK(); check(ImpactDepth>0); --ImpactDepth; TryResolveShot(); }
void UDemoShotContext::TryResolveShot()
{
    DEMO_LOG_TICK(); if(PendingProjectiles==0&&ImpactDepth==0&&!bResolutionSent&&bRunBuildCommitted)
    {
        bResolutionSent=true; // 先消费，后派发，GAS同步事件不能再次终结。
        if(IsAttackValid()&&RunBuild.IsValid()) RunBuild->NotifyShotResolved(RunBuildShot,ShotId,bRunBuildFirstEnemyConsumed,bRunBuildAnyWeakPoint,RunBuildTotalPelletsHit,RunBuildTotalDamage,RunBuildFirstEnemy.Get(),RunBuildPrimaryImpact,FiringWeapon.Get());
    }
}
float UDemoShotContext::GetRunBuildDamageMultiplier(ADemoEnemy* Enemy,bool bWeakPoint,const FVector& HitPoint,bool bSecondary)
{
    DEMO_LOG_TICK(); if(!IsAttackValid()||!bRunBuildCommitted||bSecondary||!RunBuild.IsValid()) return 1.f;
    const bool bFirstEnemy=!bRunBuildFirstEnemyConsumed;
    const bool bSameFirstEnemy=bFirstEnemy||(Enemy&&RunBuildFirstEnemy.Get()==Enemy); // 散弹打向同一首敌的后续弱点弹丸仍共享首敌身份，穿透到第二敌则false。
    const float HitDistance=HitPoint.ContainsNaN()?0.f:FVector::Distance(CameraOrigin,HitPoint);
    return RunBuild->GetDirectMultiplier(RunBuildShot,ShotId,Enemy,bWeakPoint,bFirstEnemy,bSameFirstEnemy,HitDistance,bSecondary);
}
void UDemoShotContext::NotifyRunBuildHit(ADemoEnemy* Enemy,float ActualDamage,float OverkillDamage,const FVector& HitPoint,bool bWeakPoint,bool bKilled,bool bSecondary)
{
    DEMO_LOG_TICK(); if(!IsAttackValid()||!bRunBuildCommitted||bSecondary||!RunBuild.IsValid()||!IsValid(Enemy)||!FMath::IsFinite(ActualDamage)||ActualDamage<=0.f) return;
    const bool bFirstEnemy=!bRunBuildFirstEnemyConsumed;
    if(bFirstEnemy) { RunBuildFirstEnemy=Enemy; RunBuildPrimaryImpact=HitPoint; }
    const bool bSameFirstEnemy=bFirstEnemy||(RunBuildFirstEnemy.Get()==Enemy);
    if(bSameFirstEnemy&&!HitPoint.ContainsNaN()) RunBuildPrimaryImpact=HitPoint;
    const bool bFirstTarget=!RunBuildTargets.Contains(Enemy); // 值快照在GAS事件前确定。
    bRunBuildFirstEnemyConsumed=true; RunBuildTargets.Add(Enemy); // 同枪回收/计数闩锁先于同步GAS回调。
    const int32 TargetPellets=++RunBuildTargetPellets.FindOrAdd(Enemy);
    ++RunBuildTotalPelletsHit;
    RunBuildTotalDamage+=ActualDamage;
    if(bWeakPoint) bRunBuildAnyWeakPoint=true;
    const float HitDistance=HitPoint.ContainsNaN()?0.f:FVector::Distance(CameraOrigin,HitPoint);
    RunBuild->NotifyDirectHit(RunBuildShot,ShotId,Enemy,ActualDamage,OverkillDamage,HitPoint,HitDistance,bWeakPoint,bKilled,bFirstEnemy,bSameFirstEnemy,bFirstTarget,TargetPellets,RunBuildTotalPelletsHit,bSecondary,FiringWeapon.Get());
}

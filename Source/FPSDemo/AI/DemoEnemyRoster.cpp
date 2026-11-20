// 职责：扩展兵种和双Boss的权威状态机；依赖GAS、导航、主刷怪器并发预算和会话清理。
#include "AI/DemoEnemyRoster.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyNavigation.h"
#include "AI/DemoEnemyTargeting.h"
#include "Animation/DemoEnemyPresentation.h"
#include "Characters/DemoCharacter.h"
#include "GAS/Abilities/DemoRosterAbility.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "GAS/Ammo/DemoAmmoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEnemyDefenseSet.h"
#include "GAS/DemoEffects.h"
#include "Spawning/DemoEnemySpawnComponent.h"
#include "GameplayEffectExtension.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"
#include "TimerManager.h" // 冻结时撤销已启动旧Boss定时器，不能让新仲裁停Tick却保留迟到攻击。

namespace
{
    // 封锁眼锁定光束的最大开火距离及光束长度cm；超出不起手，避免场外无人机隔图狙击。
    constexpr float BeaconBeamRange=2400.f;
}

UDemoEnemyRoster::UDemoEnemyRoster()
{
    DEMO_LOG_CALL(); SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick=true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube")); // 轮廓附件Cook硬引用。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere")); // 施法脉冲壳。
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/VFX/EnemyAttacks/Roster/M_RosterGlow")); // 已有合法Editor资源。
    CubeMesh=Cube.Object; SphereMesh=Sphere.Object; GlowMaterial=Glow.Object;
}
EDemoRosterKind UDemoEnemyRoster::GetKind() const { DEMO_LOG_TICK(); return Visual.Kind; }
uint8 UDemoEnemyRoster::GetPhase() const { DEMO_LOG_TICK(); return Visual.Phase; }
bool UDemoEnemyRoster::ManagesBoss() const { DEMO_LOG_TICK(); return Config.Kind==EDemoRosterKind::Warden||Config.Kind==EDemoRosterKind::Weaver; }
bool UDemoEnemyRoster::IsCasting() const { DEMO_LOG_TICK(); return Visual.bCasting; }
bool UDemoEnemyRoster::IsRecovering() const { DEMO_LOG_TICK(); return GetWorld()->GetTimeSeconds()<RecoveryUntil; }
int32 UDemoEnemyRoster::GetHealCount() const { DEMO_LOG_TICK(); return HealCount; }
FString UDemoEnemyRoster::GetLabel() const
{
    DEMO_LOG_TICK();
    const TCHAR* Name=TEXT(""); // 静态文字常量，无对象所有权；普通旧敌人保持原HUD。
    switch (Visual.Kind)
    {
    case EDemoRosterKind::Guard: Name=TEXT("护盾卫士"); break;
    case EDemoRosterKind::Bombardier: Name=TEXT("迫击兵"); break;
    case EDemoRosterKind::Mender: Name=TEXT("修复者"); break;
    case EDemoRosterKind::Exploder: Name=TEXT("爆裂虫"); break;
    case EDemoRosterKind::Warden: Name=TEXT("重装裁决者"); break;
    case EDemoRosterKind::Weaver: Name=TEXT("风暴编织者"); break;
    case EDemoRosterKind::Anchor: Name=TEXT("风暴锚点"); break;
    case EDemoRosterKind::Beacon: Name=TEXT("封锁眼"); break;
    default: return TEXT("");
    }
    if (Visual.Kind==EDemoRosterKind::Warden||Visual.Kind==EDemoRosterKind::Weaver) return FString::Printf(TEXT("%s · 阶段%d%s"),Name,Visual.Phase,Visual.bStagger?TEXT(" · 破绽"):TEXT(""));
    return FString(Name)+(Visual.bCasting?TEXT(" · 蓄力"):Visual.bStagger?TEXT(" · 硬直"):TEXT(""));
}
void UDemoEnemyRoster::Configure(const FDemoEnemySpawnStats& Stats)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // Enemy默认子对象，出生同步拥有本组件。
    Config=Stats.Roster; Visual.Kind=Config.Kind;
    if (!Config.IsValid()) { UE_LOG(LogFPSDemo,Error,TEXT("ROSTER_CONFIG invalid; spawn must be rejected")); Config.Kind=Visual.Kind=EDemoRosterKind::None; return; }
    if (Config.Kind==EDemoRosterKind::None) return;
    NextSkill=GetWorld()->GetTimeSeconds()+2.f+Enemy->Tactics->OpeningDelay();
    if (Config.Kind==EDemoRosterKind::Guard)
    { Enemy->AbilitySystem->SetNumericAttributeBase(UDemoEnemyDefenseSet::GetShieldAttribute(),Config.Shield); Visual.bShield=Config.Shield>0; }
    if (Config.Kind!=EDemoRosterKind::Guard&&Config.Kind!=EDemoRosterKind::Anchor)
        SkillHandle=Enemy->AbilitySystem->GiveAbility(FGameplayAbilitySpec(UDemoRosterAbility::StaticClass(),1));
    UE_LOG(LogFPSDemo,Display,TEXT("ROSTER_SPAWN actor=%s kind=%d hp=%.1f shield=%.1f"),*Enemy->GetName(),int32(Config.Kind),Stats.Health,Config.Kind==EDemoRosterKind::Guard?Config.Shield:0.f);
    OnRep_Visual();
}
UDemoRosterAbility* UDemoEnemyRoster::Ability() const
{
    DEMO_LOG_TICK();
    const ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 当前宿主，ASC持有能力实例。
    const FGameplayAbilitySpec* Spec=Enemy?Enemy->GetAbilitySystemComponent()->FindAbilitySpecFromHandle(SkillHandle):nullptr; // 不保留Spec指针跨帧。
    return Spec?Cast<UDemoRosterAbility>(Spec->GetPrimaryInstance()):nullptr;
}
void UDemoEnemyRoster::FilterDamage(FGameplayEffectModCallbackData& Data)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // 同步Health预结算宿主。
    if (Config.Kind!=EDemoRosterKind::Guard||!Visual.bShield||Data.EvaluatedData.ModifierOp!=EGameplayModOp::Additive||Data.EvaluatedData.Magnitude>=0) return;
    const FGameplayEffectContextHandle Context=Data.EffectSpec.GetContext(); // 借用GE上下文，DOT无HitResult时不获得方向盾保护。
    const UAbilitySystemComponent* Source=Context.GetOriginalInstigatorAbilitySystemComponent(); // 原射手ASC，不使用飞行中弹丸已经改变的位置。
    const AActor* Instigator=Source?Source->GetAvatarActor():Context.GetEffectCauser(); // 有效原Avatar优先。
    if (!Context.GetHitResult()||!Instigator||Instigator==Enemy) return;
    const FVector Incoming=(Instigator->GetActorLocation()-Enemy->GetActorLocation()).GetSafeNormal2D(); // 来袭源相对正面方向。
    if (FVector::DotProduct(Incoming,Facing)<FMath::Cos(FMath::DegreesToRadians(Config.ShieldArc*.5f))) return;
    const float Before=Enemy->AbilitySystem->GetNumericAttribute(UDemoEnemyDefenseSet::GetShieldAttribute()); // GAS耐久唯一真值。
    const float Absorbed=FMath::Min(Before,-Data.EvaluatedData.Magnitude*Config.ShieldAbsorb); // 最后一发不足盾量时只吸收剩余耐久。
    Data.EvaluatedData.Magnitude+=Absorbed;
    DemoEffects::Apply(Enemy->AbilitySystem,Enemy->AbilitySystem,UDemoShieldEffect::StaticClass(),-Absorbed);
    UE_LOG(LogFPSDemo,Log,TEXT("GUARD_ABSORB before=%.1f absorbed=%.1f remainingDamage=%.1f"),Before,Absorbed,-Data.EvaluatedData.Magnitude);
    if (Before<=Absorbed+KINDA_SMALL_NUMBER) { Visual.bShield=false; Stagger(Config.Stagger); }
}
void UDemoEnemyRoster::HealthChanged(float Old,float New)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // 属性回调只在服务器初始化完成后调用。
    if (New>=Old||New<=0) return;
    if (Config.Kind==EDemoRosterKind::Mender&&Visual.bCasting)
    {
        ChannelDamage+=Old-New;
        if (ChannelDamage>=Enemy->GetMaxHealth()*Config.InterruptFraction)
        { UE_LOG(LogFPSDemo,Log,TEXT("MENDER_INTERRUPTED damage=%.1f"),ChannelDamage); if (UDemoRosterAbility* Active=Ability()) Active->Complete(true); }
    }
    if (ManagesBoss())
    {
        const uint8 Next=New/Enemy->GetMaxHealth()<=.35f?3:New/Enemy->GetMaxHealth()<=.7f?2:1; // 单次大伤害可跨两阈值，无锁血。
        if (Next>Visual.Phase)
        {
            Visual.Phase=Next; UE_LOG(LogFPSDemo,Display,TEXT("ROSTER_BOSS_PHASE boss=%s phase=%d hp=%.1f"),*Enemy->GetName(),Next,New);
            if (!bSupportRequested) RequestSupport(); // 先锁存再请求；失败不会每次受击重复扩增队列。
            Enemy->ForceNetUpdate();
        }
    }
}
void UDemoEnemyRoster::Stagger(float Seconds)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // 同步停止已有近战/新读条，不撤销战役本身。
    if (UDemoRosterAbility* Active=Ability()) Active->Complete(true);
    Enemy->CloseCombat->Cancel(TEXT("RosterStagger")); Enemy->Navigation->Stop(TEXT("RosterStagger"));
    RecoveryUntil=FMath::Max(RecoveryUntil,GetWorld()->GetTimeSeconds()+Seconds); Visual.bStagger=true;
    if (Enemy->IsBoss()) Enemy->Presentation->Play(TEXT("Recovery"),Seconds); // 小怪没有Boss Recovery槽，靠硬直盾壳表现，避免请求缺失动画。
    Enemy->ForceNetUpdate();
    UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_STAGGER actor=%s seconds=%.1f"),*Enemy->GetName(),Seconds);
}
void UDemoEnemyRoster::LegacyFinished()
{
    DEMO_LOG_CALL(); if (!ManagesBoss()) return;
    RecoveryUntil=FMath::Max(RecoveryUntil,GetWorld()->GetTimeSeconds()+Config.Recovery); Visual.bStagger=true;
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // 恢复窗口保留既有开板姿势，不授予俯冲无敌或改变移动模式。
    if (!bCancelling&&Enemy->IsCombatReady()&&Enemy->IsCombatSessionValid()&&!Enemy->AbilitySystem->HasMatchingGameplayTag(DemoAmmoTags::Frozen))
        Enemy->Presentation->Play(TEXT("Hover"),Config.Recovery); // Hover仅保持护板打开，结束回Idle；受击倍率读取HitProfile，不把胸部旧2倍规则写死，当前正式弱点为head。
}
bool UDemoEnemyRoster::Update(ADemoCharacter* Player,float DeltaSeconds)
{
    DEMO_LOG_TICK(); if (Config.Kind==EDemoRosterKind::None) return false;
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // 本帧仲裁独占，不产生第二个移动Tick。
    const float Now=GetWorld()->GetTimeSeconds(); // 全部CD/硬直采用World时钟。
    if (!Enemy->IsCombatReady()||!Enemy->IsCombatSessionValid()||!DemoEnemyTargeting::IsAliveParticipant(Player)) { Cancel(); return true; }
    if (Enemy->AbilitySystem->HasMatchingGameplayTag(DemoAmmoTags::Frozen))
    {
        if (!bWasFrozen) { bWasFrozen=true; Cancel(false); } // 冰冻边沿撤销已经发出的新弹幕，保留子单位和阶段锁存。
        if (Enemy->GetDiveAbility()&&Enemy->GetDiveAbility()->IsActive()) Enemy->AbilitySystem->CancelAbilityHandle(Enemy->DiveHandle);
        if (Enemy->bGlobalAttackPending||Enemy->bAttackPending)
        {
            Enemy->bGlobalAttackPending=Enemy->bAttackPending=false; Enemy->SetGlobalTelegraph(false);
            GetWorld()->GetTimerManager().ClearTimer(Enemy->GlobalAttackTimer); GetWorld()->GetTimerManager().ClearTimer(Enemy->BossAttackTimer);
            UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_FROZEN cancelled legacy windup"));
        }
        Enemy->Navigation->Stop(TEXT("RosterFrozen")); return true;
    }
    bWasFrozen=false; // 解冻不刷新冷却或重建旧读条。
    Visual.bStagger=Now<RecoveryUntil;
    if (Visual.bStagger) { Enemy->Navigation->Stop(TEXT("RosterRecovery")); return true; }
    if (Config.Kind==EDemoRosterKind::Anchor) { Enemy->Navigation->Stop(TEXT("StormAnchor")); return true; }
    if (!Visual.bCasting)
    {
        Facing=Config.Kind==EDemoRosterKind::Guard&&Enemy->CloseCombat->HasLockedFacing()?Enemy->CloseCombat->GetLockedDirection()
            :(Player->GetActorLocation()-Enemy->GetActorLocation()).GetSafeNormal2D(); // 卫士盾击前摇随近战锁向，玩家可绕侧，盾不能偷偷追踪。
        Visual.Facing=Facing;
    }
    if (Config.Kind==EDemoRosterKind::Guard) return false; // 盾击使用既有可靠近战扫掠和.7秒前摇。
    if (Visual.bCasting)
    {
        Enemy->Navigation->Stop(TEXT("RosterCasting")); Enemy->PublishFacing(Facing);
        if (Config.Kind==EDemoRosterKind::Mender&&HealRecipient.IsValid()) Visual.HealTarget=HealRecipient->GetActorLocation();
        if (Now>=Deadline&&!bExecuting) ExecuteSkill(); return true;
    }
    if (Enemy->GetDiveAbility()&&Enemy->GetDiveAbility()->IsActive()||Enemy->bGlobalAttackPending||Enemy->bAttackPending) return false;
    if (Config.Kind==EDemoRosterKind::Warden)
    {
        if (Now>=NextSkill)
        {
            bool bStarted=false; // 大招轮转只尝试一种，本次CD不就绪下次再换，不同时启动两种。
            const int32 Turn=BossTurn++%3; // 阶段1不能俯冲，轮到俯冲改扇射。
            if (Turn==1) bStarted=Enemy->TryStartGlobalAttack();
            else if (Turn==2&&Visual.Phase>=2) bStarted=Enemy->TryStartDiveAttack();
            else bStarted=TrySkill(Player);
            if (bStarted) { NextSkill=Now+Config.Cooldown*(Visual.Phase==3?.7f:1.f); return true; }
            NextSkill=Now+.5f; // 失败不刷激活日志/忙等，旧远程攻击仍可继续。
        }
        return false;
    }
    if (Config.Kind==EDemoRosterKind::Weaver&&Visual.Phase>=2&&Now>=NextAnchorAttack)
    {
        NextAnchorAttack=Now+5.f; // 锚点电弧有限频率，销毁其中一锚会减少一条线。
        Salvo=NewObject<UDemoRosterSalvo>(this);
        for (const TWeakObjectPtr<ADemoEnemy>& Entry:Supports) if (ADemoEnemy* Anchor=Entry.Get(); Anchor&&Anchor->IsAlive()) // 弱借用每个仍活装置。
        {
            FDemoHazardVisual Lane; // 1.2秒固定目标电弧，角色走开可避免。
            Lane.Shape=EDemoHazardShape::Lane; Lane.Origin=Anchor->GetActorLocation(); Lane.Target=Player->GetActorLocation(); Lane.Warning=1.2f; Lane.Radius=100.f;
            SpawnHazard(Lane,Enemy->GetAttackPower());
        }
    }
    if (Now>=NextSkill&&TrySkill(Player)) return true;
    if (Config.Kind==EDemoRosterKind::Exploder) Enemy->Navigation->Follow(Player,180.f,Enemy->MoveSpeed*Enemy->Attributes->GetMoveSpeedMultiplier(),DeltaSeconds);
    else if (Config.Kind==EDemoRosterKind::Mender)
    {
        // 修复者绕侧翼靠近受伤盟友，但移动目标必须通过同一个Nav组件。
        if (HealRecipient.IsValid()) Enemy->Navigation->MoveTo(HealRecipient->GetActorLocation(),Player,Enemy->MoveSpeed*Enemy->Attributes->GetMoveSpeedMultiplier(),DeltaSeconds);
        else Enemy->Tactics->Move(Player,Enemy->MoveSpeed*Enemy->Attributes->GetMoveSpeedMultiplier(),DeltaSeconds);
    }
    else Enemy->Tactics->Move(Player,Enemy->MoveSpeed*Enemy->Attributes->GetMoveSpeedMultiplier(),DeltaSeconds);
    return true; // 特殊角色不会叠加旧接触伤害或普通子弹。
}
bool UDemoEnemyRoster::TrySkill(ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // 当前仲裁宿主。
    if (Visual.bCasting||!DemoEnemyTargeting::IsAliveParticipant(Player)) return false;
    if (Config.Kind==EDemoRosterKind::Exploder&&(FVector::Dist2D(Player->GetActorLocation(),Enemy->GetActorLocation())>230.f||!Enemy->Navigation->CanSee(Player))) return false;
    if (Config.Kind==EDemoRosterKind::Bombardier&&(!Enemy->Navigation->CanSee(Player)||FVector::Dist2D(Player->GetActorLocation(),Enemy->GetActorLocation())>2200.f)) return false;
    if (Config.Kind==EDemoRosterKind::Beacon&&(!Enemy->Navigation->CanSee(Player)||FVector::Dist(Player->GetActorLocation(),Enemy->GetActorLocation())>BeaconBeamRange))
    { UE_LOG(LogFPSDemo,Verbose,TEXT("BEACON_LOCK rejected no LOS/out of range")); return false; } // 必须看得见才能锁定，掩体是正确的应对。
    if (Config.Kind==EDemoRosterKind::Mender)
    {
        HealRecipient.Reset(); float Best=1.f; // 最低健康比例优先，每次施法锁定一个目标。
        for (TActorIterator<ADemoEnemy> It(GetWorld());It;++It) // 只本World活怪，不跨会话引用。
        {
            ADemoEnemy* Candidate=*It; // 本次遍历借用，选中后保留弱引用。
            UDemoEnemyRoster* Roster=Candidate->FindComponentByClass<UDemoEnemyRoster>(); // 每个Enemy都拥有组件。
            if (Candidate==Enemy||!Candidate->IsCombatReady()||!Candidate->IsCombatSessionValid()||Candidate->IsBoss()||!Roster||Roster->HealCount>=3
                ||Roster->GetKind()==EDemoRosterKind::Mender||Roster->GetKind()==EDemoRosterKind::Anchor) continue;
            const float Fraction=Candidate->GetHealth()/Candidate->GetMaxHealth(); // 当前比例，满血不消耗治疗次数。
            if (Fraction>=Best||FVector::Dist(Candidate->GetActorLocation(),Enemy->GetActorLocation())>Config.HealRange) continue;
            FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MenderLOS),false,Enemy); // 治疗必须可见，骨骼/墙体不透视。
            if (GetWorld()->LineTraceSingleByChannel(Hit,Enemy->GetActorLocation(),Candidate->GetActorLocation(),ECC_Visibility,Query)&&Hit.GetActor()!=Candidate) continue;
            HealRecipient=Candidate; Best=Fraction;
        }
        if (!HealRecipient.IsValid()) { NextSkill=GetWorld()->GetTimeSeconds()+.5f; return false; }
    }
    LockedTarget=Player->GetActorLocation(); Action=Config.Kind==EDemoRosterKind::Warden?2:Config.Kind==EDemoRosterKind::Weaver?(Visual.Phase==3&&BossTurn++%2==1?4:3):1;
    const bool bStarted=Enemy->AbilitySystem->TryActivateAbility(SkillHandle); // 激活失败不得留下可被外部重用的动作意图。
    if (!bStarted) { Action=0; UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_ACTIVATE rejected GAS state")); }
    return bStarted;
}
bool UDemoEnemyRoster::BeginSkill()
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // GAS Avatar必须仍处于本会话。
    if (bCancelling||Action==0||!Enemy->IsCombatReady()||!Enemy->IsCombatSessionValid()
        ||!DemoEnemyTargeting::IsAliveParticipant(Enemy->GetCombatTarget())||(Config.Kind==EDemoRosterKind::Mender&&!HealRecipient.IsValid()))
    { UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_BEGIN rejected stale target/session/intent")); return false; }
    Salvo=NewObject<UDemoRosterSalvo>(this); ChannelDamage=0; bExecuting=false; Visual.bCasting=true;
    const float Windup=Action==2?.7f:Action==4?1.5f:Config.Windup; // Boss技能固定可读前摇不被难度缩短。
    Deadline=GetWorld()->GetTimeSeconds()+Windup; Enemy->Navigation->Stop(TEXT("RosterWindup"));
    if (Config.Kind==EDemoRosterKind::Mender) Visual.HealTarget=HealRecipient->GetActorLocation();
    if (Action==1&&Config.Kind==EDemoRosterKind::Bombardier||Action==3)
    {
        for (int32 Index=0;Index<3;++Index) // 三个固定圈，相隔300cm；锁定后不再跟踪玩家。
        {
            FDemoHazardVisual Mark; // Warning覆盖整个准备，迫击还覆盖飞行0.9秒。
            Mark.Shape=EDemoHazardShape::Marker; Mark.Origin=Mark.Target=LockedTarget+FVector(0,(Index-1)*300.f,0); Mark.Warning=Windup+(Action==3?Index*.3f:0.f); Mark.Radius=Config.Radius;
            SpawnHazard(Mark,0,true);
        }
    }
    if (Config.Kind==EDemoRosterKind::Beacon)
    {
        // 封锁眼：起手瞬间锁定方向（之后不追踪），光束预警覆盖整个读条，读条结束同帧沿冻结线段结算一次。
        // 光束是带伤害的独立危险物而非bWarning：正常完成时FinishSkill不能撤销它；取消/死亡/冰冻经BeaconLane或Cancel()撤销。
        const FVector Origin=Enemy->GetActorLocation(); // 无人机当前位置cm，读条期间Navigation已停。
        FDemoHazardVisual Beam; Beam.Shape=EDemoHazardShape::Lane; Beam.Origin=Origin;
        Beam.Target=Origin+(LockedTarget-Origin).GetSafeNormal()*BeaconBeamRange; // 延长到最大射程，沿光束后退不能躲，必须横移或找掩体。
        Beam.Warning=Windup; Beam.Radius=Config.Radius; Beam.Color=FLinearColor(1,.75f,.2f); // 琥珀=防务公司识别色。
        BeaconLane=SpawnHazard(Beam,Enemy->GetAttackPower());
        UE_LOG(LogFPSDemo,Log,TEXT("BEACON_LOCK actor=%s target=%s halfWidth=%.0f"),*Enemy->GetName(),*LockedTarget.ToCompactString(),Config.Radius);
    }
    if (Config.Kind==EDemoRosterKind::Exploder||Action==4)
    {
        FDemoHazardVisual Mark; // 爆裂半径圈/冲击环前摇；无任何准备期伤害。
        Mark.Shape=EDemoHazardShape::Marker; Mark.Origin=Mark.Target=Enemy->GetActorLocation(); Mark.Radius=Action==4?140.f:Config.Radius; Mark.Warning=Windup;
        Mark.Color=Action==4?FLinearColor(0,.5f,1):FLinearColor(1,.1f,0); SpawnHazard(Mark,0,true);
    }
    if (Enemy->IsBoss()) Enemy->Presentation->Play(TEXT("Area"),Windup); // 现阶段小怪由明确的壳/圈/连线预警，独立模型制作后可配置专属施法Montage。
    Enemy->ForceNetUpdate();
    UE_LOG(LogFPSDemo,Log,TEXT("ROSTER_WINDUP actor=%s action=%d seconds=%.2f"),*Enemy->GetName(),Action,Windup); return true;
}
ADemoRosterHazard* UDemoEnemyRoster::SpawnHazard(const FDemoHazardVisual& InVisual,float Damage,bool bWarning)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // World负责新Actor生命周期，组件仅保留弱清理入口。
    const FTransform Transform(FRotator::ZeroRotator,InVisual.Origin); // 初始位置冻结，Deferred先完成来源登记。
    ADemoRosterHazard* Hazard=GetWorld()->SpawnActorDeferred<ADemoRosterHazard>(ADemoRosterHazard::StaticClass(),Transform,Enemy,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn); // 无碰撞预警独立Actor。
    if (!Hazard) { UE_LOG(LogFPSDemo,Warning,TEXT("ROSTER_HAZARD spawn failed")); return nullptr; }
    Hazard->Initialize(Enemy,InVisual,Salvo,Damage); Hazard->FinishSpawning(Transform);
    Hazards.RemoveAll([](const TWeakObjectPtr<ADemoRosterHazard>& Entry) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] Roster prune hazard")); return !Entry.IsValid(); }); // 无捕获，同步游戏线程剔除失效弱引用，不保存外部对象。
    Hazards.Add(Hazard); if (bWarning) Warnings.Add(Hazard); return Hazard;
}
void UDemoEnemyRoster::ExecuteSkill()
{
    DEMO_LOG_CALL(); if (bExecuting||!Visual.bCasting) return; bExecuting=true;
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // GE可能同栈导致终局，后续每次生成还会验证会话。
    if (Config.Kind==EDemoRosterKind::Mender)
    {
        ADemoEnemy* Recipient=HealRecipient.Get(); // 锁定对象仍存在才允许治疗，绝不临时切换目标。
        UDemoEnemyRoster* Roster=Recipient?Recipient->FindComponentByClass<UDemoEnemyRoster>():nullptr; // 本次同步借用。
        FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(MenderFinishLOS),false,Enemy); // 读条结束再次检查遮挡。
        if (Recipient&&Recipient->IsCombatReady()&&Roster&&Roster->HealCount<3&&FVector::Dist(Recipient->GetActorLocation(),Enemy->GetActorLocation())<=Config.HealRange
            &&(!GetWorld()->LineTraceSingleByChannel(Hit,Enemy->GetActorLocation(),Recipient->GetActorLocation(),ECC_Visibility,Query)||Hit.GetActor()==Recipient)
            &&Recipient->GetHealth()<Recipient->GetMaxHealth())
        { ++Roster->HealCount; DemoEffects::Apply(Enemy->AbilitySystem,Recipient->GetAbilitySystemComponent(),UDemoHealthEffect::StaticClass(),Recipient->GetMaxHealth()*Config.HealFraction);
            UE_LOG(LogFPSDemo,Log,TEXT("MENDER_HEAL target=%s count=%d"),*Recipient->GetName(),Roster->HealCount); }
        else UE_LOG(LogFPSDemo,Log,TEXT("MENDER_HEAL rejected stale/full/cover/range"));
    }
    else if (Action==2)
    {
        for (int32 Index=-2;Index<=2;++Index) // 五条固定射线，共享一份账本，最多一次伤害。
        {
            FDemoHazardVisual Shot; // 方向在准备开始时冻结，发射后无追踪。
            Shot.Shape=EDemoHazardShape::Shot; Shot.Origin=Enemy->GetActorLocation()+Facing*150.f; Shot.Target=Shot.Origin+Facing.RotateAngleAxis(Index*12.f,FVector::UpVector)*3000.f;
            Shot.Warning=0; Shot.Radius=12; Shot.Color=FLinearColor(1,.15f,0); SpawnHazard(Shot,Enemy->GetAttackPower());
        }
    }
    else if (Config.Kind==EDemoRosterKind::Bombardier||Action==3)
    {
        for (int32 Index=0;Index<3;++Index) // 落雷依次间隔.3秒；迫击同发落点预警早已显示1.2秒。
        {
            FDemoHazardVisual Attack; // 统一真实落点，与准备期圈完全一致。
            Attack.Shape=Action==3?EDemoHazardShape::Lightning:EDemoHazardShape::Mortar;
            Attack.Origin=Enemy->GetActorLocation()+Facing*100.f+FVector(0,0,80); Attack.Target=LockedTarget+FVector(0,(Index-1)*300.f,0);
            Attack.Warning=Action==3?Index*.3f:0.f; Attack.Radius=Config.Radius; SpawnHazard(Attack,Enemy->GetAttackPower());
        }
    }
    else if (Action==4||Config.Kind==EDemoRosterKind::Exploder)
    {
        FDemoHazardVisual Attack; // 冲击环由GA完整前摇后生成；爆裂同帧区域结算后消耗自身。
        Attack.Shape=Action==4?EDemoHazardShape::Ring:EDemoHazardShape::Lightning; Attack.Origin=Attack.Target=Enemy->GetActorLocation(); Attack.Warning=0;
        Attack.Radius=Action==4?2500.f:Config.Radius;
        ADemoRosterHazard* Hazard=SpawnHazard(Attack,Enemy->GetAttackPower()); // 爆裂在自身死亡撤销之前同步结算，不遗留死亡炸弹。
        if (Hazard&&Config.Kind==EDemoRosterKind::Exploder) Hazard->Tick(0.f);
    }
    else if (Config.Kind==EDemoRosterKind::Beacon)
        UE_LOG(LogFPSDemo,Log,TEXT("BEACON_FIRE lane=%d"),BeaconLane.IsValid()); // 伤害由起手时生成的光束危险物在预警结束时自行结算。
    if (UDemoRosterAbility* Active=Ability()) Active->Complete(false);
    if (Config.Kind==EDemoRosterKind::Exploder&&Enemy->IsCombatReady())
        DemoEffects::Apply(Enemy->AbilitySystem,Enemy->AbilitySystem,UDemoHealthEffect::StaticClass(),-Enemy->GetMaxHealth()); // 标准死亡事件消耗一次名额；杀死准备中的爆裂虫只取消，不走此分支。
}
void UDemoEnemyRoster::FinishSkill(bool bCancelled)
{
    DEMO_LOG_CALL();
    // 正常迫击读条结束仍保留落点圈覆盖飞行；其他读条表现统一撤销。
    if (bCancelled||Config.Kind!=EDemoRosterKind::Bombardier)
        for (const TWeakObjectPtr<ADemoRosterHazard>& Entry:Warnings) if (ADemoRosterHazard* Hazard=Entry.Get()) Hazard->Cancel();
    if (bCancelled&&BeaconLane.IsValid()) { BeaconLane->Cancel(); UE_LOG(LogFPSDemo,Log,TEXT("BEACON_LOCK cancelled before fire")); } // 读条被打断时光束不得延迟命中。
    BeaconLane.Reset(); Warnings.Reset(); Visual.bCasting=false; Visual.HealTarget=FVector::ZeroVector; Action=0; ChannelDamage=0;
    NextSkill=GetWorld()->GetTimeSeconds()+Config.Cooldown*(Visual.Phase==3?.7f:1.f);
    if (ManagesBoss()&&!bCancelled) LegacyFinished();
    GetOwner()->ForceNetUpdate();
}
void UDemoEnemyRoster::Cancel(bool bClearSupport)
{
    DEMO_LOG_CALL(); if (bCancelling||Config.Kind==EDemoRosterKind::None) return; bCancelling=true;
    if (UDemoRosterAbility* Active=Ability()) Active->Complete(true);
    for (const TWeakObjectPtr<ADemoRosterHazard>& Entry:Hazards) if (ADemoRosterHazard* Hazard=Entry.Get()) Hazard->Cancel(); // 先失效，再让World销毁。
    Hazards.Reset(); Warnings.Reset(); Visual.bCasting=false; Visual.bStagger=false; HealRecipient.Reset();
    if (bClearSupport&&ManagesBoss()) // 普通小怪不请求主Spawner清理子单位，避免无关Remaining广播。
    {
        for (const TWeakObjectPtr<ADemoEnemy>& Entry:Supports) if (ADemoEnemy* Enemy=Entry.Get()) Enemy->OnDefeated.RemoveAll(this); // 撤销不触发双锚破坏奖励。
        if (AActor* Owner=GetOwner()->GetOwner()) if (UDemoEnemySpawnComponent* Spawner=Owner->FindComponentByClass<UDemoEnemySpawnComponent>()) Spawner->CancelSupport(Cast<ADemoEnemy>(GetOwner())); // 主执行器统一移除存活计数。
        Supports.Reset();
    }
    GetOwner()->ForceNetUpdate(); bCancelling=false;
}
void UDemoEnemyRoster::RequestSupport()
{
    DEMO_LOG_CALL(); bSupportRequested=true;
    ADemoEnemy* Enemy=CastChecked<ADemoEnemy>(GetOwner()); // 主Spawner生成的Boss以GameMode为Owner。
    UDemoEnemySpawnComponent* Spawner=Enemy->GetOwner()?Enemy->GetOwner()->FindComponentByClass<UDemoEnemySpawnComponent>():nullptr; // 不创建第二套无限刷怪器。
    if (!Spawner) { UE_LOG(LogFPSDemo,Warning,TEXT("ROSTER_SUPPORT no encounter spawner")); return; }
    if (Config.Kind==EDemoRosterKind::Warden) { Spawner->RequestSupport(Enemy,TEXT("Guard")); Spawner->RequestSupport(Enemy,TEXT("Bombardier")); }
    else { Spawner->RequestSupport(Enemy,TEXT("StormAnchor")); Spawner->RequestSupport(Enemy,TEXT("StormAnchor")); }
    NextAnchorAttack=GetWorld()->GetTimeSeconds()+3.f;
}
void UDemoEnemyRoster::RegisterSupport(ADemoEnemy* Enemy)
{
    DEMO_LOG_CALL(); if (!Enemy||bCancelling) return; Supports.Add(Enemy); Enemy->OnDefeated.AddUObject(this,&UDemoEnemyRoster::OnSupportDefeated);
}
void UDemoEnemyRoster::OnSupportDefeated(ADemoEnemy* Enemy)
{
    DEMO_LOG_CALL();
    if (!Enemy||!Supports.Contains(Enemy)||Config.Kind!=EDemoRosterKind::Weaver||bCancelling) return;
    Supports.Remove(Enemy); ++AnchorsKilled;
    if (AnchorsKilled==2) { UE_LOG(LogFPSDemo,Display,TEXT("WEAVER_ANCHORS_BROKEN")); Stagger(Config.Stagger); }
}
void UDemoEnemyRoster::EndPlay(const EEndPlayReason::Type Reason)
{ DEMO_LOG_CALL(); if (GetOwner()->HasAuthority()) Cancel(); Super::EndPlay(Reason); }
void UDemoEnemyRoster::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ DEMO_LOG_CALL(); Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(UDemoEnemyRoster,Visual); }
void UDemoEnemyRoster::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{ DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,ThisTickFunction); OnRep_Visual(); }
void UDemoEnemyRoster::OnRep_Visual()
{
    DEMO_LOG_TICK();
    const ADemoEnemy* Enemy=Cast<ADemoEnemy>(GetOwner()); // 客户只读Actor身份/位置，不调用权威决策。
    if (!Enemy||!Enemy->HasActorBegunPlay()||Visual.Kind==EDemoRosterKind::None) return;
    if (!Ornament)
    {
        Ornament=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("RosterOrnament")); // Actor拥有运行期组件，不复制组件本身。
        CastShell=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("RosterCastShell"));
        HealBeam=NewObject<UStaticMeshComponent>(GetOwner(),TEXT("RosterHealBeam"));
        for (UStaticMeshComponent* Mesh:{Ornament.Get(),CastShell.Get(),HealBeam.Get()}) // 三个纯表现对象初始化一致。
        { Mesh->SetupAttachment(GetOwner()->GetRootComponent()); Mesh->SetStaticMesh(Mesh==CastShell?SphereMesh:CubeMesh); Mesh->SetMaterial(0,GlowMaterial); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); Mesh->SetCanEverAffectNavigation(false); Mesh->CastShadow=false; Mesh->RegisterComponent(); }
    }
    const bool bAlive=Enemy->IsAlive()&&!Enemy->IsHidden(); // 死亡不保留附件，即使尸体延时销毁。
    const FLinearColor Color=Visual.Kind==EDemoRosterKind::Guard?FLinearColor(.1f,.6f,1):Visual.Kind==EDemoRosterKind::Mender?FLinearColor(.1f,1,.3f)
        :Visual.Kind==EDemoRosterKind::Exploder?FLinearColor(1,.12f,0):Visual.Kind==EDemoRosterKind::Bombardier?FLinearColor(1,.6f,.05f)
        :Visual.Kind==EDemoRosterKind::Beacon?FLinearColor(.95f,.85f,.5f):FLinearColor(.25f,.3f,1); // 各兵种形状与颜色双重区分；封锁眼浅琥珀，区别于迫击橙。
    for (UStaticMeshComponent* Mesh:{Ornament.Get(),CastShell.Get(),HealBeam.Get()}) // MID由各组件保活，只有颜色变化无需重新分配。
        { UMaterialInstanceDynamic* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0)); /* 每组件独立MID，Actor负责回收。 */
          if (!Material) Material=Mesh->CreateAndSetMaterialInstanceDynamic(0);
          if (Material) { Material->SetVectorParameterValue(TEXT("Color"),Visual.bStagger?FLinearColor::Yellow:Color); Material->SetScalarParameterValue(TEXT("Opacity"),Mesh==CastShell?.045f:.25f); } } // 读条壳低透明度，不遮住弱点/模型轮廓。
    Ornament->SetHiddenInGame(!bAlive||(Visual.Kind==EDemoRosterKind::Guard&&!Visual.bShield));
    Ornament->SetWorldLocation(Enemy->GetActorLocation()+Visual.Facing*(Visual.Kind==EDemoRosterKind::Guard?65.f:-35.f)+FVector(0,0,Visual.Kind==EDemoRosterKind::Guard?0:70));
    Ornament->SetWorldRotation(Visual.Facing.Rotation());
    Ornament->SetWorldScale3D(Visual.Kind==EDemoRosterKind::Guard?FVector(.08f,1.5f,1.6f):Visual.Kind==EDemoRosterKind::Anchor?FVector(.5f,.5f,2.2f):FVector(.6f,.8f,.45f));
    CastShell->SetHiddenInGame(!bAlive||!Visual.bCasting&&!Visual.bStagger); CastShell->SetRelativeScale3D(FVector(Enemy->IsBoss()?3.f:1.5f));
    HealBeam->SetHiddenInGame(!bAlive||!Visual.bCasting||Visual.Kind!=EDemoRosterKind::Mender);
    if (Visual.bCasting&&Visual.Kind==EDemoRosterKind::Mender)
    {
        HealBeam->SetWorldLocation((Enemy->GetActorLocation()+Visual.HealTarget)*.5f); HealBeam->SetWorldRotation((Visual.HealTarget-Enemy->GetActorLocation()).Rotation());
        HealBeam->SetWorldScale3D(FVector(FVector::Dist(Visual.HealTarget,Enemy->GetActorLocation())/100.f,.04f,.04f));
    }
}

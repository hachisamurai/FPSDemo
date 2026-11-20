// 职责：核心权威事件执行与独立策略路由。只接受本人原ASC同步事件；目标门禁/击退/Health GE复用原系统。
#include "GAS/Talents/RunBuildAbility.h"
#include "GAS/Talents/RunBuildTargetState.h"
#include "GAS/Talents/Builds/DemoBuildRuntime.h"
#include "GAS/Talents/Builds/DemoRhythmBuildRuntime.h"
#include "GAS/Talents/Builds/DemoCrossfireBuildRuntime.h"
#include "GAS/Talents/Builds/DemoAnchorBuildRuntime.h"
#include "GAS/Talents/Builds/DemoCounterBuildRuntime.h"
#include "GAS/Talents/Builds/DemoCloseQuartersBuildRuntime.h"
#include "GAS/Talents/Builds/DemoMarksmanBuildRuntime.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Builds/DemoRunBuildSettings.h"
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyKnockbackComponent.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Weapons/DemoWeaponBase.h"
#include "GAS/Talents/DemoTalentEvents.h"
#include "GAS/Talents/DemoTalentMoveEffect.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/Abilities/DemoBossDiveAbility.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"

namespace DemoRunBuildTags
{
    UE_DEFINE_GAMEPLAY_TAG(Action,"Demo.Event.RunBuild.Action");
    UE_DEFINE_GAMEPLAY_TAG(Exposed,"Demo.Status.RunBuild.Exposed");
}
UDemoRunBuildAbility::UDemoRunBuildAbility() { DEMO_LOG_CALL(); ListenFor(DemoRunBuildTags::Action); }
void UDemoRunBuildAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData)
{
    DEMO_LOG_TICK();
    if(TriggerEventData) OnTalentEvent(*TriggerEventData,GetAbilityLevel()); // 核心Cue在具体确认行为播放，前置伤害查询不能生成成功反馈。
    EndAbility(Handle,ActorInfo,ActivationInfo,true,false); // 同步事件不保留AbilityTask，不额外授予或刷新冷却。
}
UDemoRunBuildComponent* UDemoRunBuildAbility::GetBuild() const
{
    DEMO_LOG_TICK(); return CurrentActorInfo?Cast<UDemoRunBuildComponent>(GetSourceObject(CurrentSpecHandle,CurrentActorInfo)):nullptr;
}
bool UDemoRunBuildAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,const FGameplayTagContainer* SourceTags,const FGameplayTagContainer* TargetTags,FGameplayTagContainer* OptionalRelevantTags) const
{
    DEMO_LOG_TICK();
    const UDemoRunBuildComponent* Build=ActorInfo?Cast<UDemoRunBuildComponent>(GetSourceObject(Handle,ActorInfo)):nullptr; // 由Talent组件授予的真实SourceObject。
    const ADemoCharacter* Pawn=ActorInfo?Cast<ADemoCharacter>(ActorInfo->AvatarActor.Get()):nullptr; // 当前Avatar需要仍然存活。
    const ADemoGameState* State=Pawn&&Pawn->GetWorld()?Pawn->GetWorld()->GetGameState<ADemoGameState>():nullptr;
    if(!Build||!Build->IsEncounterEnabled()||!Build->GetOwner()->HasAuthority()||!Pawn||Pawn->IsCoopEliminated()||!Pawn->GetDemoAttributes()||Pawn->GetDemoAttributes()->GetHealth()<=0.f||!State||State->Phase!=EDemoPhase::Combat) return false;
    return UGameplayAbility::CanActivateAbility(Handle,ActorInfo,SourceTags,TargetTags,OptionalRelevantTags); // 核心权限不借用原15项随机账本；原GAS阻塞/死亡门禁仍保留。
}
void UDemoRunBuildAbility::RefreshView()
{
    DEMO_LOG_TICK(); UDemoRunBuildComponent* Build=GetBuild(); if(!Build||!GetWorld()) return;
    const float Now=GetWorld()->GetTimeSeconds(); const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>(); // 同World游戏时钟及配置。
    const FDemoRunBuildSnapshot& Selection=Build->GetSnapshot(); // 只借用至本函数，发布视图不修改选择。
    const float ChainSeconds=Selection.Branch3==TEXT("B03-A")?Config->PrecisionSecondsA:Selection.Branch3==TEXT("B03-B")?Config->PrecisionSecondsB:Config->PrecisionSeconds;
    if(ChargedShots>0&&Now>=ChargeEndTime) ChargedShots=0;
    if(PrecisionStacks>0&&Now>=PrecisionLastTime+ChainSeconds) PrecisionStacks=0;
    FDemoRunBuildRuntimeView View; View.PrecisionStacks=PrecisionStacks; View.ChargedShots=ChargedShots; View.ServerEndTime=ChargedShots>0?ChargeEndTime:PrecisionStacks>0?PrecisionLastTime+ChainSeconds:0.f; // 客户端根据截止隐藏过期值，无每帧RPC。
    Build->SetRuntimeView(View);
}
void UDemoRunBuildAbility::OnEncounterStarted(UDemoRunBuildComponent* Build)
{
    DEMO_LOG_CALL();
    if(!Build||!Build->IsEncounterEnabled()) return;
    if(Build->GetSnapshot().CoreId==TEXT("B09")&&!Expansion)
    {
        Expansion=NewObject<UDemoMarksmanBuildRuntime>(this);
        Expansion->Initialize(Build);
    }
}
bool UDemoRunBuildAbility::OnTalentEvent_Implementation(const FGameplayEventData& Event,int32 Rank)
{
    DEMO_LOG_TICK(); UDemoRunBuildEventContext* Context=const_cast<UDemoRunBuildEventContext*>(Cast<UDemoRunBuildEventContext>(Event.OptionalObject)); // 调用者强持有同步参数；只在本栈修改输出。
    UDemoRunBuildComponent* Build=GetBuild(); if(!Context||!Build||!Build->IsEncounterEnabled()) return false;
    const FName Core=Build->GetSnapshot().CoreId; // 新核心独立策略，不覆盖其OwnerOnly状态投影。
    if(Core==TEXT("B04")||Core==TEXT("B05")||Core==TEXT("B06")||Core==TEXT("B07")||Core==TEXT("B08")||Core==TEXT("B09"))
    {
        if(!Expansion)
        {
            if(Core==TEXT("B04")) Expansion=NewObject<UDemoRhythmBuildRuntime>(this);
            else if(Core==TEXT("B05")) Expansion=NewObject<UDemoCrossfireBuildRuntime>(this);
            else if(Core==TEXT("B06")) Expansion=NewObject<UDemoAnchorBuildRuntime>(this);
            else if(Core==TEXT("B07")) Expansion=NewObject<UDemoCounterBuildRuntime>(this);
            else if(Core==TEXT("B08")) Expansion=NewObject<UDemoCloseQuartersBuildRuntime>(this);
            else Expansion=NewObject<UDemoMarksmanBuildRuntime>(this);
            Expansion->Initialize(Build);
        }
        if(Context->Action==EDemoRunBuildAction::BeforeHit||Context->Action==EDemoRunBuildAction::AfterHit) HandleHit(*Context); // 队友B03-D/B08-D/B09-D公开易伤仍适用于所有核心。
        Expansion->Handle(*Context);
        if(Context->Action!=EDemoRunBuildAction::DashStarted) return true;
    }
    else RefreshView();
    switch(Context->Action)
    {
        case EDemoRunBuildAction::PreviewShot: case EDemoRunBuildAction::CommitShot: HandleShot(*Context); break;
        case EDemoRunBuildAction::BeforeHit: case EDemoRunBuildAction::AfterHit: HandleHit(*Context); break;
        case EDemoRunBuildAction::ShotResolved:
        {
            // B03多弹丸整枪弱点判定：整枪只要有弹丸命头即积层，不被边缘蹭身弹丸清零。
            if(Core==TEXT("B03")&&Context->Shot.CoreId==TEXT("B03")&&!Context->Shot.bPrecisionCharged&&Context->Shot.PlannedProjectiles>1&&Context->bHadEnemyHit)
            {
                const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>();
                const float Now=GetWorld()->GetTimeSeconds();
                if(!Context->bShotHadWeakPoint) PrecisionStacks=0;
                else if(ChargedShots==0)
                {
                    ++PrecisionStacks; PrecisionLastTime=Now;
                    if(PrecisionStacks>=Config->PrecisionHits) { PrecisionStacks=0; ChargedShots=1; ChargeEndTime=Now+Config->PrecisionChargeSeconds; ChargeGeneration=ChargeGeneration<MAX_int32?ChargeGeneration+1:MAX_int32; }
                }
                RefreshView();
            }
            break;
        }
        case EDemoRunBuildAction::Melee: HandleMelee(*Context); break;
        case EDemoRunBuildAction::DashStarted:
        {
            if((Core!=TEXT("B02")&&!Expansion)||!Context->Pawn.IsValid()||!FMath::IsFinite(Context->Duration)||Context->Duration<.05f||Context->Duration>2.f) return false;
            DashSession=UDemoCombatSession::FindActive(GetWorld()); if(!DashSession.IsValid()) return false;
            DashPawn=Context->Pawn; DashStart=DashPawn->GetActorLocation();
            GetWorld()->GetTimerManager().SetTimer(DashTimer,this,&UDemoRunBuildAbility::FinishDash,Context->Duration+.005f,false); // UObject弱绑定在游戏线程，重生/离场检查原Avatar/Session。
            break;
        }
        default: break;
    }
    return true;
}
void UDemoRunBuildAbility::HandleShot(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK(); UDemoRunBuildComponent* Build=GetBuild(); if(!Build) return;
    const FDemoRunBuildSnapshot Selection=Build->GetSnapshot(); // 复制选择，不在GAS回调后借用可变成员。
    const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>();
    if(Event.Action==EDemoRunBuildAction::CommitShot)
    {
        if((Event.Shot.bAssaultCharged||Event.Shot.bPrecisionCharged)&&ChargedShots>0&&Event.Shot.ChargeGeneration==ChargeGeneration) --ChargedShots;
        RefreshView(); return;
    }
    Event.Shot=FDemoRunBuildShotSnapshot(); Event.Shot.CoreId=Selection.CoreId; Event.Shot.Branch3=Selection.Branch3; Event.Shot.Branch7=Selection.Branch7; Event.Shot.ChargeGeneration=ChargeGeneration;
    if(ChargedShots<=0) return;
    if(Selection.CoreId==TEXT("B02"))
    {
        Event.Shot.bAssaultCharged=true;
        Event.Shot.DirectMultiplier=1.f+(Selection.Branch3==TEXT("B02-A")?Config->AssaultBonusA:Selection.Branch3==TEXT("B02-B")?Config->AssaultBonusB:Config->AssaultBonus);
        if(Selection.Branch7==TEXT("B02-D")) { Event.Shot.SpreadScale=Config->AssaultSpreadScale; Event.Shot.RecoilScale=Config->AssaultRecoilScale; }
    }
    else if(Selection.CoreId==TEXT("B03"))
    {
        Event.Shot.bPrecisionCharged=true;
        Event.Shot.PrecisionBonus=Selection.Branch3==TEXT("B03-A")?Config->PrecisionBonusA:Selection.Branch3==TEXT("B03-B")?Config->PrecisionBonusB:Config->PrecisionBonus;
    }
}
void UDemoRunBuildAbility::HandleHit(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK(); if(Event.bSecondary) return; // 派生绝不重新享受直击增益或消费原标记。
    UDemoRunBuildComponent* Build=GetBuild(); if(!Build) return;
    ADemoEnemy* Enemy=Event.Enemy.Get(); // 设施为空，只允许突击直伤，不走敌人计数。
    const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>();
    if(Event.Action==EDemoRunBuildAction::BeforeHit)
    {
        Event.Multiplier=Event.Shot.DirectMultiplier;
        const bool bPrecisionTarget=Event.Shot.PlannedProjectiles>1?Event.bSameFirstEnemy:Event.bFirstEnemy; // 散弹强化枪对首敌全部弱点弹丸生效，穿透第二敌仍不享受。
        if(Enemy&&bPrecisionTarget&&Event.bWeakPoint&&Event.Shot.bPrecisionCharged) Event.Multiplier*=1.f+Event.Shot.PrecisionBonus;
        if(UDemoRunBuildTargetState* Target=UDemoRunBuildTargetState::Find(Enemy,false)) Event.Multiplier*=Target->ResolveMarks(Build,Event.ShotId,false);
        return;
    }
    if(!Enemy||Event.Damage<=0.f) return;
    if(UDemoRunBuildTargetState* Target=UDemoRunBuildTargetState::Find(Enemy,false))
    {
        Target->ResolveMarks(Build,Event.ShotId,true); // 只有确认扣血才消耗，无敌/零伤害不消耗。
        if(Event.bKilled) Target->NotifyKilled(Build); // 队友或单人击杀B09-D猎杀标定目标时触发回充。
    }
    const float Now=GetWorld()->GetTimeSeconds(); // HP确认后的服务器游戏时刻。
    if(Event.Shot.CoreId==TEXT("B01")&&!Event.bKilled&&Enemy->IsAlive())
    {
        int32 IceAdd=0;
        if(Event.bFirstTarget) IceAdd=(Event.Shot.FireInterval>=Config->SlowWeaponIntervalSeconds&&Event.bWeakPoint)?2:1; // 慢射速重武器弱点+2，常规单发+1。
        else if(Event.Shot.PlannedProjectiles>1&&(Event.TargetPelletsHit==Config->IcePelletDoubleThreshold||Event.TargetPelletsHit==Config->IcePelletFullThreshold)) IceAdd=1; // 散弹单枪4/6丸累进至2/3层，贴脸可瞬冻。
        if(IceAdd>0)
        {
            for(auto It=IceChains.CreateIterator();It;++It) if(!It.Key().IsValid()||!It.Key()->IsAlive()||Now-It.Value().LastTime>Config->IceChainSeconds) It.RemoveCurrent(); // 当前场目标上限有界，不累积跨关弱键。
            FDemoRunBuildIceChain& Chain=IceChains.FindOrAdd(Enemy); // 本人每目标计数。
            Chain.Count+=IceAdd; Chain.LastTime=Now;
            if(Chain.Count>=Config->IceHits) { Chain.Count=0; if(UDemoRunBuildTargetState* Target=UDemoRunBuildTargetState::Find(Enemy,true)) Target->TryIceTrigger(Build); }
        }
    }
    if(Event.Shot.bAssaultCharged&&Event.Shot.Branch7==TEXT("B02-C")&&Event.bKilled&&!SpeedRewardedGenerations.Contains(Event.Shot.ChargeGeneration))
    {
        SpeedRewardedGenerations.Add(Event.Shot.ChargeGeneration); ApplyAssaultMove();
        for(auto It=SpeedRewardedGenerations.CreateIterator();It;++It) if(*It<ChargeGeneration-64) It.RemoveCurrent(); // 远超飞行寿命的批次修剪，长无尽也不无限增长。
    }
    if(Event.Shot.CoreId!=TEXT("B03")) return;
    if(Event.Shot.bPrecisionCharged)
    {
        const bool bPrecisionTarget=Event.Shot.PlannedProjectiles>1?Event.bSameFirstEnemy:Event.bFirstEnemy;
        if(bPrecisionTarget&&Event.bWeakPoint&&!PrecisionRewardedShots.Contains(Event.ShotId))
        {
            if(PrecisionRewardedShots.Num()>=64) PrecisionRewardedShots.Reset();
            PrecisionRewardedShots.Add(Event.ShotId);
            if(Event.Shot.Branch7==TEXT("B03-C")&&Event.Weapon.IsValid()&&Event.Weapon->GetOwner()==GetAvatarActorFromActorInfo()) Event.Weapon->TransferReserveRounds(1);
            if(Event.Shot.Branch7==TEXT("B03-D")&&!Event.bKilled) if(UDemoRunBuildTargetState* Target=UDemoRunBuildTargetState::Find(Enemy,true)) Target->Expose(Build,Event.ShotId);
        }
        return; // 强化枪无论命中部位都不反过来积层。
    }
    if(Event.Shot.PlannedProjectiles>1||!Event.bFirstEnemy) return; // 多弹丸非强化枪移交ShotResolved整枪判定弱点。
    if(!Event.bWeakPoint) PrecisionStacks=0;
    else if(ChargedShots==0)
    {
        const int32 Gain=Event.Shot.FireInterval>=Config->SlowWeaponIntervalSeconds?2:1; // 慢射速狙击弱点+2层（2枪满层）。
        PrecisionStacks+=Gain; PrecisionLastTime=Now;
        if(PrecisionStacks>=Config->PrecisionHits) { PrecisionStacks=0; ChargedShots=1; ChargeEndTime=Now+Config->PrecisionChargeSeconds; ChargeGeneration=ChargeGeneration<MAX_int32?ChargeGeneration+1:MAX_int32; }
    }
    RefreshView();
}
void UDemoRunBuildAbility::FinishDash()
{
    DEMO_LOG_CALL(); UDemoRunBuildComponent* Build=GetBuild(); ADemoCharacter* Pawn=DashPawn.Get(); // 原Avatar弱引用只在本栈有效。
    if(!Build||!Build->IsEncounterEnabled()||!Pawn||GetAvatarActorFromActorInfo()!=Pawn||Pawn->IsCoopEliminated()||!Pawn->GetDemoAttributes()||Pawn->GetDemoAttributes()->GetHealth()<=0.f||!DashSession.IsValid()||!DashSession->IsActive())
    { UE_LOG(LogFPSDemo,Log,TEXT("BUILD_DASH_REJECT ended/dead/avatar/session")); return; }
    const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>(); // 权威实际位移，不能相信客户端冲刺方向/距离。
    const float Travel=FVector::Dist2D(DashStart,Pawn->GetActorLocation());
    if(Expansion)
    {
        UDemoRunBuildEventContext* Result=NewObject<UDemoRunBuildEventContext>(this); // 仅dash低频一次，同步对象由当前GA Outer保活至GC。
        Result->Action=EDemoRunBuildAction::DashFinished; Result->Pawn=Pawn; Result->Distance=Travel>=Config->DashMinimumDistance?Travel:0.f; Result->bSuccess=Result->Distance>0.f; Expansion->Handle(*Result); DashPawn.Reset(); DashSession.Reset(); return;
    }
    if(Build->GetSnapshot().CoreId!=TEXT("B02")) return;
    if(Travel<Config->DashMinimumDistance) { UE_LOG(LogFPSDemo,Log,TEXT("BUILD_DASH_REJECT distance=%.1f minimum=%.1f"),Travel,Config->DashMinimumDistance); return; }
    const FName Branch=Build->GetSnapshot().Branch3; // 下一次冲刺覆盖窗口不叠加。
    ChargedShots=Branch==TEXT("B02-B")?2:1; ChargeEndTime=GetWorld()->GetTimeSeconds()+(Branch==TEXT("B02-A")?Config->AssaultSecondsA:Config->AssaultSeconds); ChargeGeneration=ChargeGeneration<MAX_int32?ChargeGeneration+1:MAX_int32;
    DashPawn.Reset(); DashSession.Reset(); RefreshView();
    UE_LOG(LogFPSDemo,Log,TEXT("BUILD_ASSAULT_READY shots=%d distance=%.1f"),ChargedShots,Travel);
}
void UDemoRunBuildAbility::ApplyAssaultMove()
{
    DEMO_LOG_CALL(); UAbilitySystemComponent* ASC=GetAbilitySystemComponentFromActorInfo(); if(!ASC) return;
    if(AssaultMove.IsValid()) ASC->RemoveActiveGameplayEffect(AssaultMove); // 单份核心加速，连续冲刺不叠层。
    const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>();
    FGameplayEffectContextHandle Context=ASC->MakeEffectContext(); Context.AddSourceObject(GetDefault<UDemoRunBuildAbility>()); // 生命周期来源明确。
    FGameplayEffectSpecHandle Spec=ASC->MakeOutgoingSpec(UDemoTalentMoveEffect::StaticClass(),1.f,Context);
    if(Spec.IsValid()) { Spec.Data->SetDuration(Config->AssaultMoveSeconds,true); Spec.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,Config->AssaultMoveBonus); Spec.Data->DynamicGrantedTags.AddTag(DemoTalentEvents::CombatTransient); AssaultMove=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()); }
}
void UDemoRunBuildAbility::HandleMelee(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_CALL(); UDemoRunBuildComponent* Build=GetBuild(); ADemoEnemy* Enemy=Event.Enemy.Get(); // 死亡目标仍由World保活至延迟销毁。
    if(!Build||Build->GetSnapshot().CoreId!=TEXT("B01")||!Enemy||Enemy->IsBoss()||!Event.bFrozenBefore||!FMath::IsFinite(Event.Damage)||Event.Damage<=0.f||Event.Direction.ContainsNaN()||GetWorld()->GetTimeSeconds()<NextShatterTime) return;
    UDemoCombatSession* Session=UDemoCombatSession::FindActive(GetWorld()); if(!Session) return;
    const FDemoRunBuildSnapshot Selection=Build->GetSnapshot(); const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>(); // 源端值冻结，不借用首个受害者实际掉血。
    FDemoRunBuildBurst Burst; Burst.Damage=Event.Damage*(Selection.Branch3==TEXT("B01-B")?Config->BurstRatioB:Config->BurstRatio); Burst.Radius=Selection.Branch3==TEXT("B01-B")?Config->BurstRadiusB:Config->BurstRadius; Burst.bSlow=Selection.Branch7==TEXT("B01-C"); Burst.bHeal=Selection.Branch7==TEXT("B01-D"); Burst.Session=Session;
    if(!FMath::IsFinite(Burst.Damage)) return;
    if(Event.bKilled||!Enemy->IsAlive())
    {
        NextShatterTime=GetWorld()->GetTimeSeconds()+(Selection.Branch3==TEXT("B01-A")?Config->ShatterCooldownA:Config->ShatterCooldown); Event.bAccepted=true; Explode(Enemy,Burst); return;
    }
    UDemoEnemyKnockbackComponent* Knockback=Enemy->FindComponentByClass<UDemoEnemyKnockbackComponent>(); // 复用原敌人移动仲裁/复制，禁止第二推力系统。
    if(!Knockback) return;
    // 回调绑定当前GA UObject；只在击退首次终结触发，失效对象自动不执行。替换旧击退先取消其回调，再发布新快照。
    if(Knockback->Apply(Event.Direction,Selection.Branch3==TEXT("B01-A")?Config->PushDistanceA:Config->PushDistance,Config->PushSeconds,FDemoKnockbackFinished::CreateUObject(this,&UDemoRunBuildAbility::OnKnockbackEnded)))
    {
        PendingBursts.Add(Enemy,Burst); NextShatterTime=GetWorld()->GetTimeSeconds()+(Selection.Branch3==TEXT("B01-A")?Config->ShatterCooldownA:Config->ShatterCooldown); Event.bAccepted=true;
    }
}
void UDemoRunBuildAbility::OnKnockbackEnded(ADemoEnemy* Enemy,FName Reason)
{
    DEMO_LOG_CALL(); const FDemoRunBuildBurst* Found=PendingBursts.Find(Enemy); if(!Found) return;
    const FDemoRunBuildBurst Burst=*Found; PendingBursts.Remove(Enemy); // 先消费一次凭据，GAS同步回调不能再次爆裂。
    if(Reason!=TEXT("Completed")&&Reason!=TEXT("Blocked")&&Reason!=TEXT("NoGroundSupport")) return;
    if(!IsValid(Enemy)||!Enemy->IsAlive()||!Burst.Session.IsValid()||!Burst.Session->IsActive()) return;
    Explode(Enemy,Burst);
}
void UDemoRunBuildAbility::Explode(ADemoEnemy* Origin,const FDemoRunBuildBurst& Burst)
{
    DEMO_LOG_CALL(); UDemoRunBuildComponent* Build=GetBuild(); UAbilitySystemComponent* ASC=Build?Build->GetASC():nullptr;
    if(!Origin||!Build||!Build->IsEncounterEnabled()||!ASC||!Burst.Session.IsValid()||!Burst.Session->IsActive()) return;
    const ADemoCharacter* Source=Cast<ADemoCharacter>(ASC->GetAvatarActor()); // 延迟击退结果必须仍属于存活原玩家，不能依靠死亡清理回调的先后顺序。
    if(!Source||Source->IsCoopEliminated()||!Source->GetDemoAttributes()||Source->GetDemoAttributes()->GetHealth()<=0.f) return;
    const FVector Center=Origin->GetActorLocation(); // 固定爆裂位置，后续死亡/回调不能移动查询中心。
    TArray<ADemoEnemy*> Targets; // 仅同步借用，施加每次GE前复核生命/会话。
    for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It)
    {
        ADemoEnemy* Enemy=*It; if(Enemy==Origin||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()||FVector::DistSquared(Center,Enemy->GetActorLocation())>FMath::Square(Burst.Radius)) continue;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(RunBuildBurst),false,Origin); FHitResult Wall; // 真实墙面阻挡，不从纯半径查询穿墙伤害。
        if(GetWorld()->LineTraceSingleByChannel(Wall,Center,Enemy->GetActorLocation(),ECC_Visibility,Query)&&Wall.GetActor()!=Enemy) continue;
        Targets.Add(Enemy);
    }
    // 捕获固定Center值，仅本游戏线程同步排序；相同距离以Actor唯一序号稳定裁决，不持有异步Actor引用。
    Targets.Sort([Center](const ADemoEnemy& A,const ADemoEnemy& B){ UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] RunBuildBurstSort")); const double DA=FVector::DistSquared(Center,A.GetActorLocation()); const double DB=FVector::DistSquared(Center,B.GetActorLocation()); return DA==DB?A.GetUniqueID()<B.GetUniqueID():DA<DB; });
    const UDemoRunBuildSettings* Config=GetDefault<UDemoRunBuildSettings>(); if(Targets.Num()>Config->BurstTargets) Targets.SetNum(Config->BurstTargets,EAllowShrinking::No);
    FGameplayCueParameters Cue; Cue.Location=Center; Cue.RawMagnitude=Burst.Radius; Origin->GetAbilitySystemComponent()->ExecuteGameplayCue(DemoTalentEvents::FrostBurst,Cue); // 复用已存在冰蓝范围反馈，GC绝不扣血。
    bool bDamagedOther=false; // D分支要求至少一个其他目标实际受伤，一次范围只回复一次。
    for(ADemoEnemy* Enemy:Targets)
    {
        if(!Burst.Session.IsValid()||!Burst.Session->IsActive()||!IsValid(Enemy)||!Enemy->IsAlive()||!Enemy->IsCombatSessionValid()) continue;
        UAbilitySystemComponent* Target=Enemy->GetAbilitySystemComponent(); if(!Target||Target->HasMatchingGameplayTag(DemoBossTags::Invulnerable)) continue;
        const float Before=Enemy->GetHealth();
        FGameplayEffectContextHandle Context=ASC->MakeEffectContext(); Context.AddSourceObject(GetDefault<UDemoRunBuildAbility>()); // 继承UDemoTalentAbility的来源让旧冰爆/传染都识别派生并拒绝递归。
        FGameplayEffectSpecHandle Damage=ASC->MakeOutgoingSpec(UDemoHealthEffect::StaticClass(),1.f,Context); if(!Damage.IsValid()) continue;
        Damage.Data->SetSetByCallerMagnitude(DemoTags::Magnitude,-Burst.Damage); ASC->ApplyGameplayEffectSpecToTarget(*Damage.Data.Get(),Target);
        if(IsValid(Enemy)&&Enemy->GetHealth()<Before)
        {
            bDamagedOther=true;
            if(Burst.bSlow&&Enemy->IsAlive()&&!Enemy->IsBoss()) if(UDemoRunBuildTargetState* State=UDemoRunBuildTargetState::Find(Enemy,true)) State->ApplyBurstSlow(Build);
        }
    }
    if(Burst.bHeal&&bDamagedOther&&GetWorld()->GetTimeSeconds()>=NextHealTime&&Burst.Session.IsValid()&&Burst.Session->IsActive())
    {
        NextHealTime=GetWorld()->GetTimeSeconds()+Config->BurstHealCooldown; DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),Config->BurstHeal); // AttributeSet负责最大HP边界，不过量治疗。
    }
    UE_LOG(LogFPSDemo,Log,TEXT("BUILD_ICE_BURST targets=%d damage=%.2f radius=%.1f"),Targets.Num(),Burst.Damage,Burst.Radius);
}
void UDemoRunBuildAbility::ResetRuntime()
{
    DEMO_LOG_CALL(); if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(DashTimer);
    if(Expansion) { Expansion->ResetRuntime(); Expansion=nullptr; } // 先撤新策略Timer/Actor，后清旧字段；下一场重新冻结Avatar/Session。
    if(UAbilitySystemComponent* ASC=GetAbilitySystemComponentFromActorInfo()) if(AssaultMove.IsValid()) ASC->RemoveActiveGameplayEffect(AssaultMove);
    AssaultMove.Invalidate(); DashPawn.Reset(); DashSession.Reset(); IceChains.Reset(); PendingBursts.Reset(); SpeedRewardedGenerations.Reset(); PrecisionRewardedShots.Reset(); PrecisionStacks=0; PrecisionLastTime=0.f; ChargedShots=0; ChargeEndTime=0.f; NextShatterTime=0.f; NextHealTime=0.f;
    if(UDemoRunBuildComponent* Build=GetBuild()) Build->SetRuntimeView(FDemoRunBuildRuntimeView());
}

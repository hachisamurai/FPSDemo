// 职责：B05正式破势标记、共享原子消费与守恒冲击预算。依赖原ASC/Health GE/ShotId/Session；不依赖FPSDemoEditor原型。
#include "GAS/Talents/Builds/DemoCrossfireBuildRuntime.h"
#include "GAS/Talents/Builds/DemoCrossfireTargetState.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Game/DemoGameState.h"
#include "Player/DemoPlayerState.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AI/DemoEnemy.h"
#include "Weapons/DemoWeaponBase.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h" // 静态团队hook没有this，使用显式上下文保留高频埋点。

TArray<TWeakObjectPtr<UDemoCrossfireBuildRuntime>> UDemoCrossfireBuildRuntime::Registered;

void UDemoCrossfireBuildRuntime::Handle(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK();
    if (!Active()) return;
    RegisterRuntime();
    if (Mark.Serial>0 && (Now()>=Mark.EndTime || !Mark.Target.IsValid())) ClearMark(true);
    const bool bReverse=Selection().Branch3==TEXT("B05-B"); // 互斥分支由原RunBuild选择契约保证。
    if (Event.Action==EDemoRunBuildAction::AfterHit && !Event.bSecondary && Event.bFirstTarget && Event.Damage>0.f)
    {
        if (bReverse)
        { if (Event.bFirstEnemy) CreateMark(Event,true); } // 整枪最多一次起手，霰弹/穿透不重置多个标记。
        else if (Selection().Branch7!=TEXT("B05-D")) Consume(Build(),Event,true); // D已由全核心团队桥统一串行处理，避免自身再次抢消费。
    }
    else if (Event.Action==EDemoRunBuildAction::Melee && Event.ActualDamage>0.f)
    {
        if (!bReverse) CreateMark(Event,false);
        else if (Selection().Branch7!=TEXT("B05-D")) Consume(Build(),Event,false);
    }
}
void UDemoCrossfireBuildRuntime::RegisterRuntime()
{
    DEMO_LOG_TICK();
    // 捕获无；仅本次同步清理已销毁/已撤销策略，不持有World，也不跨线程运行。
    Registered.RemoveAll([](const TWeakObjectPtr<UDemoCrossfireBuildRuntime>& Entry)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] CrossfireRegistryPrune")); return !Entry.IsValid() || !Entry->Active(); });
    if (Registered.Contains(this)) return;
    int32 WorldSources=0; // 只约束本World，不把另一PIE World算入同一支队伍。
    for (const TWeakObjectPtr<UDemoCrossfireBuildRuntime>& Entry:Registered) if (Entry.IsValid() && Entry->GetWorld()==GetWorld()) ++WorldSources;
    if (WorldSources>=4) { UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_REGISTRY_REJECT world-source-limit=4")); return; }
    Registered.Add(this);
}
void UDemoCrossfireBuildRuntime::CreateMark(const UDemoRunBuildEventContext& Event,bool bGun)
{
    DEMO_LOG_CALL();
    ADemoEnemy* Enemy=Event.Enemy.Get(); // 当前真实HP受害者，不接受设施、尸体或另一个World目标。
    const int64 Serial=bGun?Event.Shot.CommitSerial:Event.Serial; // 与将来兑现枪/近战同一服务器全序。
    const float Source=Event.Damage; // 枪用真实直击HP；近战用既有未乘目标倍率的源基数，前提为ActualDamage>0。
    if (!Active() || !Enemy || Event.bKilled || !Enemy->IsAlive() || !Enemy->IsCombatSessionValid() || Enemy->GetWorld()!=GetWorld()
        || Serial<=0 || !FMath::IsFinite(Source) || Source<=0.f || (!bGun && Event.ActualDamage<=0.f))
    { UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_MARK_REJECT target/source/serial/life/session")); return; }
    const UDemoExpandedBuildSettings* Settings=GetDefault<UDemoExpandedBuildSettings>(); // 起手冻结集中配置，兑现不读取旧Editor原型值。
    ClearMark(false);
    Mark.Target=Enemy; Mark.Session=UDemoCombatSession::FindActive(GetWorld()); Mark.Gun=bGun?Event.Weapon.Get():nullptr;
    Mark.Serial=Serial; Mark.EndTime=Now()+Settings->CrossfireWindow;
    Mark.Budget=FMath::Min(Settings->CrossfireDamageCap,Source*Settings->CrossfireRatio);
    Mark.Radius=Settings->CrossfireRadius; Mark.Targets=FMath::Clamp(Settings->CrossfireTargets,1,16);
    Mark.Rounds=Selection().Branch7==TEXT("B05-C")?Settings->CrossfireRounds:0;
    Mark.bReverse=bGun; Mark.bArea=Selection().Branch3==TEXT("B05-A"); Mark.bShared=Selection().Branch7==TEXT("B05-D");
    UDemoCrossfireTargetState* PublicState=UDemoCrossfireTargetState::Find(Enemy,true); // 脱敏公开目标方向与期限；私有预算始终留在策略。
    if (!PublicState || !PublicState->SetSource(Build(),Mark.EndTime,Mark.bReverse))
    { ClearMark(false); Publish(TEXT("Crossfire.Idle"),0,1); UE_LOG(LogFPSDemo,Warning,TEXT("CROSSFIRE_MARK_REJECT public target state unavailable")); return; }
    GetWorld()->GetTimerManager().SetTimer(MarkExpiryTimer,this,&UDemoCrossfireBuildRuntime::OnMarkExpired,FMath::Max(.001f,Mark.EndTime-Now()),false);
    Publish(bGun?FName(TEXT("Crossfire.Melee")):FName(TEXT("Crossfire.Gun")),1,1,Mark.EndTime);
    UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_MARK_CREATED reverse=%d shared=%d serial=%lld budget=%.2f target=%s"),Mark.bReverse,Mark.bShared,Mark.Serial,Mark.Budget,*Enemy->GetName());
}
bool UDemoCrossfireBuildRuntime::CanConsume(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event,bool bGun) const
{
    DEMO_LOG_TICK();
    if (!Active() || !ActorBuild || !ActorBuild->IsEncounterEnabled() || ActorBuild->GetWorld()!=GetWorld() || !ActorBuild->GetASC()
        || !Mark.Target.IsValid() || Mark.Target.Get()!=Event.Enemy.Get() || Mark.Serial<=0 || Now()>=Mark.EndTime
        || !Mark.Session.IsValid() || Mark.Session.Get()!=UDemoCombatSession::FindActive(GetWorld()) || Mark.bReverse==bGun) return false;
    const ADemoCharacter* ActorPawn=Cast<ADemoCharacter>(ActorBuild->GetASC()->GetAvatarActor()); // 原出手者ASC当前Avatar，不能代替死亡/观战者消费。
    if (!ActorPawn || ActorPawn->IsCoopEliminated() || !ActorPawn->GetDemoAttributes() || ActorPawn->GetDemoAttributes()->GetHealth()<=0.f) return false;
    if (bGun)
    {
        if (Event.bSecondary || !Event.bFirstTarget || Event.Damage<=0.f || !Event.ShotId.IsValid() || Event.Shot.CommitSerial<=Mark.Serial) return false;
    }
    else if (Event.ActualDamage<=0.f || Event.Serial<=Mark.Serial) return false;
    if (!Mark.bShared) return ActorBuild==Build();
    if (ActorBuild==Build() && HasOtherParticipant()) return false; // 明确的队友优先：另有存活参战者时施加者不可抢，单人回退本人。
    return true;
}
bool UDemoCrossfireBuildRuntime::Consume(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event,bool bGun)
{
    DEMO_LOG_CALL();
    if (!CanConsume(ActorBuild,Event,bGun)) return false;
    const FDemoCrossfireMark Consumed=Mark; // 先冻结值，再清凭据；任何Health GE回调都不可能重复消费同一预算。
    ADemoEnemy* Enemy=Consumed.Target.Get(); // 本次真实受害者；原伤已杀死时仍消费但绝不攻击尸体。
    const FVector Center=Enemy?Enemy->GetActorLocation():FVector::ZeroVector; // GE可能同步清关/销毁，范围中心提前按真实命中目标冻结。
    ClearMark(true);
    float Applied=0.f; // 只用于诊断真实HP结果，不能把请求预算当作已打中。
    if (!Event.bKilled && Enemy && Enemy->IsAlive())
    {
        // A不按敌人数/队员人数放大总预算：50%给主目标，其余50%按最大目标数均分；空缺份额不重新分配。
        const float PrimaryBudget=Consumed.bArea?Consumed.Budget*.5f:Consumed.Budget; // B集中兑现全额，A用范围换单体强度。
        Applied=Damage(Enemy,PrimaryBudget);
        if (Consumed.bArea && Applied>0.f && Active()) Pulse(Center,Consumed.Radius,Consumed.Budget*.5f/FMath::Max(1,Consumed.Targets),Consumed.Targets,Enemy);
    }
    ADemoWeaponBase* OriginalGun=bGun?Event.Weapon.Get():Consumed.Gun.Get(); // 反向路线回起手原枪；不能给当前ActiveWeapon凭空回弹。
    if (Consumed.Rounds>0 && ActorBuild==Build() && Active() && OriginalGun)
    { const int32 Moved=OriginalGun->TransferReserveRounds(Consumed.Rounds); UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_CLOSED_LOOP moved=%d"),Moved); }
    UE_LOG(LogFPSDemo,Log,TEXT("CROSSFIRE_CONSUMED shared=%d reverse=%d budget=%.2f actual-primary=%.2f original-killed=%d"),Consumed.bShared,Consumed.bReverse,Consumed.Budget,Applied,Event.bKilled);
    return true;
}
bool UDemoCrossfireBuildRuntime::HasOtherParticipant() const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 只读当前全队目录，最多4人，不按Tick扫Actor。
    if (!State) return false;
    for (const TObjectPtr<APlayerState>& Entry:State->PlayerArray)
    {
        const ADemoPlayerState* PS=Cast<ADemoPlayerState>(Entry.Get()); // 个人独立ASC/Build；不查询Player0或共享钱包。
        const UDemoRunBuildComponent* Other=PS?PS->GetRunBuildComponent():nullptr;
        const ADemoCharacter* OtherPawn=Other && Other->GetASC()?Cast<ADemoCharacter>(Other->GetASC()->GetAvatarActor()):nullptr;
        if (Other && Other!=Build() && Other->IsEncounterEnabled() && OtherPawn && OtherPawn->GetWorld()==GetWorld()
            && !OtherPawn->IsCoopEliminated() && OtherPawn->GetDemoAttributes() && OtherPawn->GetDemoAttributes()->GetHealth()>0.f) return true;
    }
    return false;
}
void UDemoCrossfireBuildRuntime::NotifyTeamGun(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_STATIC_TICK(ActorBuild);
    NotifyTeam(ActorBuild,Event,true);
}
void UDemoCrossfireBuildRuntime::NotifyTeamMelee(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_STATIC_TICK(ActorBuild);
    NotifyTeam(ActorBuild,Event,false);
}
void UDemoCrossfireBuildRuntime::NotifyTeam(UDemoRunBuildComponent* ActorBuild,const UDemoRunBuildEventContext& Event,bool bGun)
{
    DEMO_LOG_STATIC_TICK(ActorBuild);
    if (!IsInGameThread() || !ActorBuild || !ActorBuild->GetOwner()->HasAuthority()) return;
    UDemoCrossfireBuildRuntime* Candidate=nullptr; // 遍历时只选最早来源，GE开始后不继续访问可能因清关改变的注册表。
    int64 Oldest=MAX_int64; // 不依赖TArray注册先后或网络消息以外的本地渲染顺序。
    for (const TWeakObjectPtr<UDemoCrossfireBuildRuntime>& Entry:Registered)
        if (Entry.IsValid() && Entry->Mark.bShared && Entry->Mark.Serial<Oldest && Entry->CanConsume(ActorBuild,Event,bGun))
        { Candidate=Entry.Get(); Oldest=Entry->Mark.Serial; }
    if (Candidate) Candidate->Consume(ActorBuild,Event,bGun); // 游戏线程原子清标记在先，团队同帧两个请求只有第一个兑现。
}
void UDemoCrossfireBuildRuntime::OnMarkExpired()
{
    DEMO_LOG_CALL();
    if (Mark.Serial>0 && Now()>=Mark.EndTime) ClearMark(Active());
}
void UDemoCrossfireBuildRuntime::ClearMark(bool bPublish)
{
    DEMO_LOG_TICK();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(MarkExpiryTimer);
    if (UDemoCrossfireTargetState* PublicState=UDemoCrossfireTargetState::Find(Mark.Target.Get(),false)) PublicState->RemoveSource(Build()); // 精确撤自己的PS槽，不影响同目标其他来源。
    Mark=FDemoCrossfireMark();
    if (bPublish && Active()) Publish(TEXT("Crossfire.Idle"),0,1);
}
void UDemoCrossfireBuildRuntime::ResetRuntime()
{
    DEMO_LOG_CALL();
    ClearMark(false);
    Registered.Remove(this);
    Super::ResetRuntime();
}

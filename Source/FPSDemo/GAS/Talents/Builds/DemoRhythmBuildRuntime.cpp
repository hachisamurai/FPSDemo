// 职责：B04完整基础与ABCD分支结算。使用：只消费原RunBuild事件；备弹守恒由WeaponBase执行，追加枪仍走原Fire GA。
#include "GAS/Talents/Builds/DemoRhythmBuildRuntime.h"
#include "GAS/Talents/RunBuildAbility.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "Progression/Builds/DemoRunBuildSettings.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"

void UDemoRhythmBuildRuntime::Handle(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK();
    if (!Active()) return;
    if (Credits>0 && Now()>=CreditsEnd) OnCreditsExpired();
    switch (Event.Action)
    {
        case EDemoRunBuildAction::ReloadStarted:
        case EDemoRunBuildAction::ReloadPressed:
        case EDemoRunBuildAction::ReloadFinished: HandleReload(Event); break;
        case EDemoRunBuildAction::PreviewShot:
        case EDemoRunBuildAction::CommitShot:
        case EDemoRunBuildAction::AfterHit:
        case EDemoRunBuildAction::ShotResolved: HandleShot(Event); break;
        case EDemoRunBuildAction::WeaponChanged: HandleWeaponChanged(Event.Weapon.Get()); break;
        case EDemoRunBuildAction::DashFinished:
            if (Selection().Branch7==TEXT("B04-C") && Event.Distance>=GetDefault<UDemoRunBuildSettings>()->DashMinimumDistance)
            { ValidDashUntil=Now()+GetDefault<UDemoExpandedBuildSettings>()->RhythmCarrySeconds; UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_CARRY_DASH_READY")); }
            break;
        default: break;
    }
}
void UDemoRhythmBuildRuntime::HandleReload(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_CALL();
    ADemoWeaponBase* Weapon=Event.Weapon.Get(); // 只借用本次原生装填事实中的武器。
    const UDemoExpandedBuildSettings* Settings=GetDefault<UDemoExpandedBuildSettings>(); // 统一配置只读CDO。
    if (Event.Action==EDemoRunBuildAction::ReloadStarted)
    {
        if (!Weapon || Event.Sequence<=0 || !Pawn() || !Pawn()->GetWeaponComponent()->IsEquipped(Weapon))
        { UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_RELOAD_START_REJECT weapon/session/equipment")); return; }
        ReloadWeapon=Weapon; ReloadSession=Event.Sequence;
        ReloadStarted=Weapon->GetReloadServerStartTime();
        ReloadDuration=FMath::Max(.001f,Weapon->Config.bReloadOneRoundAtATime ? Weapon->GetReloadDuration() : Weapon->GetReloadInsertionSeconds()); // 整匣节律窗口位于提交前，逐颗仍沿用首周期。
        ReloadHits=MagazineHits.FindRef(Weapon); MagazineHits.FindOrAdd(Weapon)=0;
        ++MagazineGenerations.FindOrAdd(Weapon);
        bPressed=bBeatSuccess=bBeatCarry=false;
        Credits=0; CreditWeapon.Reset(); bCarryAvailable=false;
        if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(CreditExpiryTimer);
        PublishState();
        return;
    }
    if (Event.Action==EDemoRunBuildAction::ReloadPressed)
    {
        Event.bAccepted=true; // 包括错过/重复意图：已由节律核心处理，不允许重新启动Reload GA。
        if (!Weapon || ReloadWeapon.Get()!=Weapon || ReloadSession<=0 || bPressed || !Weapon->IsReloading())
        { UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_PRESS_REJECT no-session/duplicate/stale")); return; }
        bPressed=true;
        if (Selection().Branch7==TEXT("B04-D"))
        { UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_PRESS_IGNORED stable-route")); PublishState(); return; }
        const float Tolerance=FMath::Clamp(Settings->RhythmToleranceSeconds,0.f,.15f); // 固定有界服务器接收容错，不接受客户端成功标记或任意回溯时间。
        const float Elapsed=Now()-ReloadStarted; // 游戏秒，不读动画进度或本机墙钟。
        bBeatSuccess=Elapsed>=ReloadDuration*Settings->RhythmWindowStart-Tolerance && Elapsed<=ReloadDuration*Settings->RhythmWindowEnd+Tolerance;
        bBeatCarry=bBeatSuccess && Selection().Branch7==TEXT("B04-C") && Now()<=ValidDashUntil;
        UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_PRESS_RESULT success=%d elapsed=%.3f duration=%.3f carry=%d"),bBeatSuccess,Elapsed,ReloadDuration,bBeatCarry);
        PublishState();
        return;
    }
    if (ReloadSession<=0 || Event.Sequence!=ReloadSession || ReloadWeapon.Get()!=Weapon)
    { UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_RELOAD_END_REJECT stale-session")); return; }
    const bool bStable=Selection().Branch7==TEXT("B04-D"); // 互斥C/D由原选择快照保证。
    const int32 RequiredHits=Weapon?FMath::Min(Settings->RhythmStableHits,FMath::Max(1,Weapon->GetCapacity())):Settings->RhythmStableHits; // 双管等小容量武器按实际弹匣下调，消除不可达死锁。
    const bool bEarned=Event.bSuccess && (bStable ? ReloadHits>=RequiredHits : bBeatSuccess); // 原GA仅最终真实完成才传Success。
    const bool bCarry=bBeatCarry; // 清会话前冻结一次跨枪许可。
    ReloadSession=0; ReloadWeapon.Reset(); bPressed=bBeatSuccess=bBeatCarry=false;
    if (bEarned) Award(Weapon,bStable?Settings->RhythmStableCredits:Settings->RhythmCredits,bCarry);
    else { UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_RELOAD_NO_REWARD success=%d stable=%d hits=%d"),Event.bSuccess,bStable,ReloadHits); PublishState(); }
}
void UDemoRhythmBuildRuntime::HandleShot(UDemoRunBuildEventContext& Event)
{
    DEMO_LOG_TICK();
    ADemoWeaponBase* Weapon=Event.Weapon.Get(); // Commit/AfterHit锁定原枪，不能使用后来换出的ActiveWeapon替代。
    if (Event.Action==EDemoRunBuildAction::PreviewShot)
    {
        ADemoCharacter* Character=Pawn(); // Preview调用可能尚未把Weapon写入事件，查询当前装备只用于冻结准备。
        Weapon=Character && Character->GetWeaponComponent()?Character->GetWeaponComponent()->GetActiveWeapon():nullptr;
        const bool bFollowup=Character && Character->GetWeaponComponent()->IsCoreFollowupExecuting(); // 第二枪不再消费短连凭据，防止递归第三枪。
        Event.Shot.ExpansionGeneration=Generation;
        Event.Shot.bRhythmCharged=Credits>0 && Now()<CreditsEnd && CreditWeapon.Get()==Weapon && !bFollowup;
        if (Event.Shot.bRhythmCharged && Selection().Branch3==TEXT("B04-B") && Event.Shot.bBoltAction)
            Event.Shot.DirectMultiplier*=1.f+GetDefault<UDemoExpandedBuildSettings>()->RhythmBoltActionBonus; // 拉栓武器B04-B不强制退镜自动连射，转为本次直击增伤。
        return;
    }
    if (Event.Action==EDemoRunBuildAction::CommitShot)
    {
        if (!Weapon || !Event.ShotId.IsValid() || PendingShots.Contains(Event.ShotId)) return;
        BoundPendingShots();
        FDemoRhythmPendingShot& Pending=PendingShots.Add(Event.ShotId); // 一枪最多一个记录，整枪终结后移除。
        Pending.Weapon=Weapon; Pending.Serial=Event.Shot.CommitSerial;
        Pending.MagazineGeneration=MagazineGenerations.FindRef(Weapon);
        Pending.CreditGeneration=Event.Shot.ExpansionGeneration;
        if (!Event.Shot.bRhythmCharged || Event.Shot.ExpansionGeneration!=Generation || Credits<=0 || Now()>=CreditsEnd || CreditWeapon.Get()!=Weapon) return;
        --Credits; Pending.bCharged=true;
        if (Selection().Branch3==TEXT("B04-B"))
        {
            if (!Event.Shot.bBoltAction && (!Pawn() || !Pawn()->GetWeaponComponent()->QueueCoreFollowup(Weapon)))
                UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_SEQUENCE_CANCEL input/equipment/action-gate"));
        }
        else Pending.bRefund=true;
        PublishState();
        return;
    }
    FDemoRhythmPendingShot* Pending=PendingShots.Find(Event.ShotId); // 缺失/过期记录不可凭命中伪造供弹。
    if (!Pending) return;
    if (Event.Action==EDemoRunBuildAction::AfterHit)
    {
        if (Event.bSecondary || !Event.bFirstEnemy || !Event.Enemy.IsValid() || Event.Damage<=0.f || Pending->bHitCounted) return;
        Pending->bHitCounted=true;
        ADemoWeaponBase* OriginalWeapon=Pending->Weapon.Get(); // 飞行中切枪仍只能回收至真实原枪。
        if (OriginalWeapon && Pending->MagazineGeneration==MagazineGenerations.FindRef(OriginalWeapon))
            MagazineHits.FindOrAdd(OriginalWeapon)=FMath::Min(100000,MagazineHits.FindRef(OriginalWeapon)+1);
        if (OriginalWeapon && Pending->bRefund)
        { Pending->bRefund=false; const int32 Moved=OriginalWeapon->TransferReserveRounds(1); UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_SUPPLY moved=%d"),Moved); }
        if (Selection().Branch7==TEXT("B04-D") && ReloadSession==0) PublishState();
        return;
    }
    const bool bBreak=Pending->bCharged && !Pending->bHitCounted && Pending->CreditGeneration==Generation && Selection().Branch3==TEXT("B04-A"); // 已实际命中以真实HP事实为准。
    PendingShots.Remove(Event.ShotId);
    if (bBreak)
    { Credits=0; CreditWeapon.Reset(); bCarryAvailable=false; UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_CHAIN_BROKEN miss")); PublishState(); }
}
void UDemoRhythmBuildRuntime::HandleWeaponChanged(ADemoWeaponBase* NewWeapon)
{
    DEMO_LOG_CALL();
    if (Credits<=0 || CreditWeapon.Get()==NewWeapon) return;
    if (bCarryAvailable && NewWeapon && Now()<CreditsEnd)
    { CreditWeapon=NewWeapon; bCarryAvailable=false; UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_CARRY_CONSUMED weapon=%s"),*GetNameSafe(NewWeapon)); }
    else
    { Credits=0; CreditWeapon.Reset(); bCarryAvailable=false; UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_CREDITS_CLEARED weapon-changed")); }
    PublishState();
}
void UDemoRhythmBuildRuntime::Award(ADemoWeaponBase* Weapon,int32 Count,bool bCarry)
{
    DEMO_LOG_CALL();
    if (!Active() || !Weapon || !Pawn() || !Pawn()->GetWeaponComponent()->IsEquipped(Weapon)) return;
    ++Generation; Credits=FMath::Clamp(Count,1,16); CreditWeapon=Weapon; bCarryAvailable=bCarry;
    CreditsEnd=Now()+FMath::Max(.1f,GetDefault<UDemoExpandedBuildSettings>()->RhythmCreditSeconds);
    GetWorld()->GetTimerManager().SetTimer(CreditExpiryTimer,this,&UDemoRhythmBuildRuntime::OnCreditsExpired,FMath::Max(.001f,CreditsEnd-Now()),false);
    UE_LOG(LogFPSDemo,Log,TEXT("RHYTHM_CREDITS_GRANTED count=%d generation=%d carry=%d"),Credits,Generation,bCarryAvailable);
    PublishState();
}
void UDemoRhythmBuildRuntime::OnCreditsExpired()
{
    DEMO_LOG_CALL();
    if (Credits>0 && Now()<CreditsEnd) return; // 同对象后续批次替换后，陈旧Timer不得清新凭据。
    Credits=0; CreditWeapon.Reset(); bCarryAvailable=false;
    if (Active()) PublishState();
}
void UDemoRhythmBuildRuntime::PublishState()
{
    DEMO_LOG_TICK();
    if (!Active()) return;
    const UDemoExpandedBuildSettings* Settings=GetDefault<UDemoExpandedBuildSettings>(); // 发布与结算取同一份集中配置。
    if (ReloadSession>0)
        Publish(bPressed?(bBeatSuccess?FName(TEXT("Rhythm.Ready")):FName(TEXT("Rhythm.Attempted"))):FName(TEXT("Rhythm.Window")),0,1,ReloadStarted+ReloadDuration*Settings->RhythmWindowEnd);
    else if (Credits>0) Publish(TEXT("Rhythm.Credits"),Credits,FMath::Max(Settings->RhythmCredits,Settings->RhythmStableCredits),CreditsEnd);
    else
    {
        ADemoWeaponBase* Current=Pawn() && Pawn()->GetWeaponComponent()?Pawn()->GetWeaponComponent()->GetActiveWeapon():nullptr; // 稳定路线只显示当前实际弹匣进度。
        const int32 Required=Current?FMath::Min(Settings->RhythmStableHits,FMath::Max(1,Current->GetCapacity())):Settings->RhythmStableHits;
        Publish(TEXT("Rhythm.Idle"),Selection().Branch7==TEXT("B04-D")?MagazineHits.FindRef(Current):0,Required);
    }
}
void UDemoRhythmBuildRuntime::BoundPendingShots()
{
    DEMO_LOG_TICK();
    if (PendingShots.Num()<128) return;
    FGuid Oldest; int64 OldestSerial=MAX_int64; // 只回收最旧记录，不随机依赖Map遍历顺序。
    for (const TPair<FGuid,FDemoRhythmPendingShot>& Pair:PendingShots)
        if (Pair.Value.Serial<OldestSerial) { OldestSerial=Pair.Value.Serial; Oldest=Pair.Key; }
    PendingShots.Remove(Oldest);
    UE_LOG(LogFPSDemo,Warning,TEXT("RHYTHM_PENDING_LIMIT oldest shot accounting evicted; limit=128"));
}
void UDemoRhythmBuildRuntime::ResetRuntime()
{
    DEMO_LOG_CALL();
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(CreditExpiryTimer);
    ReloadWeapon.Reset(); CreditWeapon.Reset(); PendingShots.Reset(); MagazineHits.Reset(); MagazineGenerations.Reset();
    ReloadSession=ReloadHits=Credits=Generation=0;
    ReloadStarted=ReloadDuration=CreditsEnd=ValidDashUntil=0.f;
    bPressed=bBeatSuccess=bBeatCarry=bCarryAvailable=false;
    Super::ResetRuntime();
}

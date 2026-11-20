// 职责：近战即时表现、父刀关联单槽队列及可靠收敛；早到实际起刀确认等本地桥末消费，校时不能倒播前摇。预测不提交时间戳、碰撞或伤害。
// 连招与普通尾部缓冲共享一个槽；作者phase、匹配入口、GAS和表现都使用原StartTime/PlaybackKeys时钟。
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Characters/DemoCharacter.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

const FDemoMeleeState& UDemoMeleeComponent::PresentationState() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=Character(); // 本帧借用；占有改变后旁观者不使用旧预测。
    return bUsePrediction && Pawn && !Pawn->HasAuthority() && Pawn->IsLocallyControlled()?PredictedState:State;
}
double UDemoMeleeComponent::PresentationTime() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=Character(); // 拥有者采用本机单调游戏秒，避免网络校时倒放。
    return bUsePrediction && Pawn && !Pawn->HasAuthority() && Pawn->IsLocallyControlled()?GetWorld()->GetTimeSeconds():ServerTime();
}
int32 UDemoMeleeComponent::ResolveInputAttack(int32 Index,int32 Cursor) const
{
    DEMO_LOG_TICK();
    if (!Definition || !Definition->Attacks.IsValidIndex(Index)) return INDEX_NONE;
    if (Index!=0 || Definition->HorizontalAttackSequence.IsEmpty()) return Index;
    const FName Id=Definition->HorizontalAttackSequence[FMath::Max(0,Cursor)%Definition->HorizontalAttackSequence.Num()]; // 以稳定ID解析。
    for (int32 Candidate=0;Candidate<Definition->Attacks.Num();++Candidate)
        if (Definition->Attacks[Candidate].Id==Id) return Candidate;
    UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RESOLVE_REJECT missing=%s"),*Id.ToString()); return INDEX_NONE;
}
void UDemoMeleeComponent::ClearBufferedInput()
{
    DEMO_LOG_CALL();
    BufferedInput=INDEX_NONE; BufferedRequestId=0; bBufferedCombo=false; BufferedSourceSerial=0;
    BufferedParentRequestId=0; BufferedTargetAttack=INDEX_NONE; BufferedExpires=0.;
    BufferedPhase=EDemoPhase::Lobby; BufferedLevel=0;
    PendingConfirmedComboStart=FDemoMeleeState(); bHasPendingConfirmedComboStart=false; PendingConfirmedComboParent=0; // 当前槽撤销即丢弃暂存，取消/换装/死亡后不能迟到续播。
}
bool UDemoMeleeComponent::CanQueueCombo(const FDemoMeleeState& Snapshot,double Now,int32& Target) const
{
    DEMO_LOG_TICK(); Target=INDEX_NONE;
    if (!IsEquipped() || !Snapshot.bPrimarySequence || Snapshot.bComboExitCommitted || !Definition->Attacks.IsValidIndex(Snapshot.Attack)) return false;
    const FDemoMeleeAttack& Attack=Definition->Attacks[Snapshot.Attack]; // 起刀后至Branch接受一次真实新点击，右键不参与。
    if (Attack.ComboTargetId.IsNone() || !Attack.ComboFirstPerson || !Attack.ComboThirdPerson
        || Now<Snapshot.StartTime || Now+1.e-6>=Snapshot.StartTime+Attack.RuntimeAtPhase(Attack.ComboBranchPhase)) return false;
    Target=ResolveInputAttack(0,Snapshot.NextHorizontalCursor);
    return Definition->Attacks.IsValidIndex(Target) && Definition->Attacks[Target].Id==Attack.ComboTargetId;
}
bool UDemoMeleeComponent::QueueAuthorityCombo(uint32 RequestId,uint32 ParentOwnerRequestId)
{
    DEMO_LOG_CALL();
    int32 Target=INDEX_NONE; // 目标与入口只从本机已验证DA读取。
    if (!GetOwner() || !GetOwner()->HasAuthority() || !IsAttackContextValid() || BufferedInput!=INDEX_NONE
        || State.OwnerRequestId!=ParentOwnerRequestId || (RequestId!=0 && RequestId==ParentOwnerRequestId)
        || !CanQueueCombo(State,ServerTime(),Target))
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_QUEUE_REJECT request=%u parent=%u context/occupied/branch"),RequestId,ParentOwnerRequestId); return false; }
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>(); // Context已验证，仍不跨帧保留指针。
    BufferedInput=0; BufferedRequestId=RequestId; bBufferedCombo=true; BufferedSourceSerial=State.Serial;
    BufferedParentRequestId=ParentOwnerRequestId; BufferedTargetAttack=Target;
    BufferedPhase=GameState->Phase; BufferedLevel=GameState->LevelNumber;
    State.bComboExitCommitted=true; NextAttackTime=SegmentEndTime(State); BufferedExpires=NextAttackTime+.1; // 有界长帧容忍，禁止无限待命。
    OnRep_State(); GetOwner()->ForceNetUpdate();
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_QUEUED request=%u parent=%u serial=%u target=%d end=%.6f"),RequestId,ParentOwnerRequestId,State.Serial,Target,NextAttackTime);
    return true;
}
bool UDemoMeleeComponent::TryQueueLocalCombo(int32 Index)
{
    DEMO_LOG_CALL();
    const ADemoCharacter* Pawn=Character(); // 远端普通RPC同步栈不得变为本机提前连招。
    if (Index!=0 || !Pawn || !Pawn->IsLocallyControlled() || CurrentInputRequest!=0) return false;
    if (BufferedInput!=INDEX_NONE)
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_INPUT_IGNORED occupied request=%u"),BufferedRequestId); return true; }
    const FDemoMeleeState& Visual=PresentationState();
    int32 Target=INDEX_NONE;
    if (!CanQueueCombo(Visual,PresentationTime(),Target)) return false;
    if (Pawn->HasAuthority()) return QueueAuthorityCombo(0,State.OwnerRequestId);
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>(); // 消费时再次核阶段/关号。
    if (!GameState || !bUsePrediction || bPredictionCancelled || Visual.OwnerRequestId==0) return false;
    if (++NextRequestId==0) ++NextRequestId;
    BufferedInput=0; BufferedRequestId=NextRequestId; bBufferedCombo=true; BufferedSourceSerial=PredictedState.Serial;
    BufferedParentRequestId=PredictedState.OwnerRequestId; BufferedTargetAttack=Target;
    BufferedPhase=GameState->Phase; BufferedLevel=GameState->LevelNumber;
    LastQueuedComboRequest=BufferedRequestId; LastQueuedComboParent=BufferedParentRequestId;
    PredictedState.bComboExitCommitted=true; LocalRecoveryUntil=SegmentEndTime(PredictedState); BufferedExpires=LocalRecoveryUntil+.1;
    ServerQueueCombo(BufferedRequestId,BufferedParentRequestId);
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_PREDICT_QUEUE request=%u parent=%u target=%d"),BufferedRequestId,BufferedParentRequestId,Target);
    return true;
}
void UDemoMeleeComponent::PredictAttack(int32 Index)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn=Character(); // 只有本机当前拥有者预测此Avatar。
    if (!Pawn || Pawn->HasAuthority() || !Pawn->IsLocallyControlled() || !IsEquipped()) return;
    const double Now=GetWorld()->GetTimeSeconds(); // 本机秒不发往服务器。
    if (bHasPendingConfirmedComboStart && Now+1.e-6>=SegmentEndTime(PredictedState))
        AdvancePredictedCombo(); // 输入Tick可能先于PostUpdate；先消费已发生的子刀事实，再把这次真实点击交给新刀，不抹掉事实重播0秒。
    const FDemoMeleeState& Previous=PresentationState();
    if (Now<LocalRecoveryUntil || IsAttacking())
    {
        if (!TryQueueLocalCombo(Index) && !BufferLocalInput(Index,FMath::Max(LocalRecoveryUntil,SegmentEndTime(Previous))))
            UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_REJECT recovery/branch"));
        return;
    }
    const int32 StartCursor=Index==0 && Definition->bRestartPrimarySequenceAfterRecovery?0:Previous.NextHorizontalCursor; // 同服务器Standalone入口；仅原完整收招门已通过才重选组首，连段推进保持原路径。
    const int32 Attack=ResolveInputAttack(Index,StartCursor);
    if (!Definition->Attacks.IsValidIndex(Attack)) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_REJECT missing sequence attack cursor=%d"),StartCursor); return; }
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>();
    if (!GameState) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_REJECT missing game state")); return; }
    const int32 NextCursor=Index==0 && !Definition->HorizontalAttackSequence.IsEmpty()?(StartCursor+1)%Definition->HorizontalAttackSequence.Num():Previous.NextHorizontalCursor; // 预测仅暂存0→1；真实服务器拒绝时沿原回滚，不推进权威游标。
    ClearBufferedInput(); LastQueuedComboRequest=LastQueuedComboParent=UnconfirmedComboRequest=UnconfirmedComboParent=0;
    if (++NextRequestId==0) ++NextRequestId;
    PredictedState=State; PredictedState.Attack=Attack; PredictedState.StartTime=Now;
    PredictedState.OwnerRequestId=NextRequestId; PredictedState.NextHorizontalCursor=NextCursor; PredictedState.Serial=++LocalPresentationSerial;
    PredictedState.bPrimarySequence=Index==0; PredictedState.bComboExitCommitted=false; PredictedState.bStartedFromCombo=false;
    PredictedState.ComboSourceAttack=INDEX_NONE; PredictedState.EntryPhase=0.f;
    bUsePrediction=true; bPredictionCancelled=false; bSwingPlayed=false;
    LocalRecoveryUntil=SegmentEndTime(PredictedState); PredictedPhase=GameState->Phase; PredictedLevel=GameState->LevelNumber;
    UpdatePresentation();
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_START request=%u attack=%d start=%.3f restart_after_recovery=%d start_cursor=%d next_cursor=%d"),NextRequestId,Attack,Now,Definition->bRestartPrimarySequenceAfterRecovery,StartCursor,NextCursor);
    ServerRequestAttack(Index,NextRequestId);
}
void UDemoMeleeComponent::CancelPredictedPresentation(bool bExplicitCancel)
{
    DEMO_LOG_CALL();
    if(bExplicitCancel) CancelCameraAttack(); // 取消/拒绝清资格；权威AttackNone也可能是正常收刀，不能一概登记撤销。
    else CameraNaturalCompletionTime=GetWorld()->GetTimeSeconds(); // 仅保已确认刀最多0.25秒真实末刀回执，不改变动作/冷却。
    if (bUsePrediction && bHasPendingConfirmedComboStart && PendingConfirmedComboParent==PredictedState.OwnerRequestId && Definition
        && Definition->Attacks.IsValidIndex(PredictedState.Attack) && Definition->Attacks.IsValidIndex(PendingConfirmedComboStart.Attack))
    {
        const FDemoMeleeAttack& Child=Definition->Attacks[PendingConfirmedComboStart.Attack]; // 实际子刀已在服务器开始，即使本机桥未结束，取消也要保其完整恢复。
        const double ChildRecovery=SegmentEndTime(PredictedState)-Child.RuntimeAtPhase(PendingConfirmedComboStart.EntryPhase)+Child.Duration;
        LocalRecoveryUntil=FMath::Max(LocalRecoveryUntil,ChildRecovery);
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_CANCEL_CONFIRMED_RECOVERY request=%u until=%.6f"),PendingConfirmedComboStart.OwnerRequestId,LocalRecoveryUntil);
    }
    ClearBufferedInput(); // 先冻结已知子刀恢复，再删除暂存；未知排队仍只保原父刀规则。
    LastQueuedComboRequest=LastQueuedComboParent=UnconfirmedComboRequest=UnconfirmedComboParent=0;
    if (!bUsePrediction) return;
    if (Definition && Definition->Attacks.IsValidIndex(PredictedState.Attack))
        LocalRecoveryUntil=FMath::Max(LocalRecoveryUntil,PredictedState.StartTime+Definition->Attacks[PredictedState.Attack].Duration); // 与服务器取消一致，提前承诺不成为缩短恢复漏洞。
    PredictedState.Attack=INDEX_NONE; PredictedState.bComboExitCommitted=false; PredictedState.bStartedFromCombo=false;
    bPredictionCancelled=true; bSwingPlayed=true;
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_CANCEL request=%u recovery=%.3f"),PredictedState.OwnerRequestId,LocalRecoveryUntil);
}
bool UDemoMeleeComponent::AdvancePredictedCombo()
{
    DEMO_LOG_CALL();
    const ADemoCharacter* Pawn=Character();
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>();
    const bool bConfirmed=bHasPendingConfirmedComboStart && PendingConfirmedComboParent==PredictedState.OwnerRequestId
        && PendingConfirmedComboStart.OwnerRequestId==BufferedRequestId && PendingConfirmedComboStart.Attack==BufferedTargetAttack; // 已收到真实起刀事实，不能因本机+.1秒槽超时丢失。
    if (!Pawn || Pawn->HasAuthority() || !Pawn->IsLocallyControlled() || !Pawn->CanUsePlayerSkills() || !GameState || bPredictionCancelled
        || !bBufferedCombo || BufferedInput!=0 || BufferedSourceSerial!=PredictedState.Serial || BufferedParentRequestId!=PredictedState.OwnerRequestId
        || !PredictedState.bComboExitCommitted || !Definition->Attacks.IsValidIndex(PredictedState.Attack) || !Definition->Attacks.IsValidIndex(BufferedTargetAttack)
        || GameState->Phase!=BufferedPhase || GameState->LevelNumber!=BufferedLevel || (!bConfirmed && PresentationTime()>BufferedExpires)
        || PresentationTime()+1.e-6<SegmentEndTime(PredictedState) || (!bConfirmed && UnconfirmedComboRequest!=0)) return false;
    const FDemoMeleeState Parent=PredictedState; // 最多一刀已推进未确认，避免高延迟无限推演未知权威链。
    const FDemoMeleeAttack& Source=Definition->Attacks[Parent.Attack];
    if (Source.ComboTargetId!=Definition->Attacks[BufferedTargetAttack].Id || ResolveInputAttack(0,Parent.NextHorizontalCursor)!=BufferedTargetAttack) return false;
    const double Boundary=SegmentEndTime(Parent);
    const uint32 Request=BufferedRequestId;
    const int32 Target=BufferedTargetAttack;
    const FDemoMeleeState Confirmed=bConfirmed?PendingConfirmedComboStart:FDemoMeleeState(); // 清槽前复制实际事实，后面只覆盖本地表现起点/序号，不改服务器时钟。
    ClearBufferedInput();
    if (bConfirmed) PredictedState=Confirmed;
    PredictedState.Attack=Target; PredictedState.OwnerRequestId=Request; PredictedState.Serial=++LocalPresentationSerial;
    PredictedState.StartTime=Boundary-Definition->Attacks[Target].RuntimeAtPhase(Source.ComboEntryPhase);
    PredictedState.bComboExitCommitted=false; PredictedState.bStartedFromCombo=true; PredictedState.ComboSourceAttack=Parent.Attack;
    PredictedState.EntryPhase=Source.ComboEntryPhase;
    PredictedState.NextHorizontalCursor=bConfirmed?Confirmed.NextHorizontalCursor:(Parent.NextHorizontalCursor+1)%Definition->HorizontalAttackSequence.Num();
    UnconfirmedComboRequest=bConfirmed?0:Request; UnconfirmedComboParent=bConfirmed?0:Parent.OwnerRequestId;
    LocalRecoveryUntil=SegmentEndTime(PredictedState); bSwingPlayed=false; UpdatePresentation();
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_PREDICT_STARTED request=%u parent=%u target=%d entry=%.6f confirmed=%d"),Request,Parent.OwnerRequestId,Target,PredictedState.EntryPhase,bConfirmed);
    return true;
}
void UDemoMeleeComponent::RollbackComboPrediction(const FDemoMeleeState& Confirmed,double RecoveryUntil)
{
    DEMO_LOG_CALL();
    const ADemoCharacter* Pawn=Character();
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>();
    if (bPredictionCancelled || !Pawn || !Pawn->CanUsePlayerSkills() || !GameState || GameState->Phase!=PredictedPhase || GameState->LevelNumber!=PredictedLevel)
    { CancelPredictedPresentation(); return; }
    ClearBufferedInput(); LastQueuedComboRequest=LastQueuedComboParent=UnconfirmedComboRequest=UnconfirmedComboParent=0;
    CancelCameraAttack(); // 改写前撤销被拒子刀的反馈身份；不能改写后误撤合法父刀，不改本函数原冷却/时钟。
    PredictedState=Confirmed; PredictedState.StartTime=GetWorld()->GetTimeSeconds()-(ServerTime()-Confirmed.StartTime);
    PredictedState.Serial=++LocalPresentationSerial; PredictedState.bStartedFromCombo=false; // 拒绝恢复使用普通混合，不伪称连续动画。
    LocalRecoveryUntil=GetWorld()->GetTimeSeconds()+FMath::Max(0.,RecoveryUntil-ServerTime()); bSwingPlayed=true;
    UpdatePresentation(); UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_ROLLBACK authority_request=%u attack=%d"),Confirmed.OwnerRequestId,Confirmed.Attack);
}
void UDemoMeleeComponent::ServerQueueCombo_Implementation(uint32 RequestId,uint32 ParentOwnerRequestId)
{
    DEMO_LOG_CALL();
    if (RequestId==0 || ParentOwnerRequestId==0 || RequestId==ParentOwnerRequestId)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_COMBO_RPC_REJECT invalid identity")); return; }
    if (State.OwnerRequestId==RequestId && State.bStartedFromCombo)
    { ClientAttackResult(RequestId,true,State,NextAttackTime); return; } // 重传只重发已发生事实。
    if (bBufferedCombo && BufferedRequestId==RequestId && BufferedParentRequestId==ParentOwnerRequestId)
    { ClientComboQueueResult(RequestId,ParentOwnerRequestId,true,State,NextAttackTime); return; }
    const bool bFresh=LastComboRPCRequest==0 || static_cast<int32>(RequestId-LastComboRPCRequest)>0; // 容许uint32环绕的正常递增。
    if (bFresh) LastComboRPCRequest=RequestId;
    const bool bAccepted=bFresh && QueueAuthorityCombo(RequestId,ParentOwnerRequestId);
    ClientComboQueueResult(RequestId,ParentOwnerRequestId,bAccepted,State,NextAttackTime);
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_RPC_RESULT request=%u parent=%u accepted=%d fresh=%d"),RequestId,ParentOwnerRequestId,bAccepted,bFresh);
}
void UDemoMeleeComponent::ClientComboQueueResult_Implementation(uint32 RequestId,uint32 ParentOwnerRequestId,bool bAccepted,FDemoMeleeState Confirmed,double RecoveryUntil)
{
    DEMO_LOG_CALL();
    if (!bUsePrediction || bPredictionCancelled) return;
    const bool bPending=bBufferedCombo && BufferedRequestId==RequestId && BufferedParentRequestId==ParentOwnerRequestId
        && PredictedState.OwnerRequestId==ParentOwnerRequestId && BufferedSourceSerial==PredictedState.Serial;
    const bool bAdvanced=UnconfirmedComboRequest==RequestId && UnconfirmedComboParent==ParentOwnerRequestId && PredictedState.OwnerRequestId==RequestId;
    if (!bPending && !bAdvanced)
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_QUEUE_STALE request=%u parent=%u"),RequestId,ParentOwnerRequestId); return; }
    if (!bAccepted)
    {
        if (bAdvanced) RollbackComboPrediction(Confirmed,RecoveryUntil);
        else
        {
            ClearBufferedInput(); PredictedState.bComboExitCommitted=false; LocalRecoveryUntil=SegmentEndTime(PredictedState);
            LastQueuedComboRequest=LastQueuedComboParent=0; // 排队失败保留合法父刀，并播放其完整Standalone收招。
        }
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_QUEUE_REJECTED request=%u advanced=%d"),RequestId,bAdvanced); return;
    }
    if (bPending)
    {
        PredictedState.bComboExitCommitted=true; PredictedState.NextHorizontalCursor=Confirmed.NextHorizontalCursor;
        LocalRecoveryUntil=SegmentEndTime(PredictedState); BufferedExpires=LocalRecoveryUntil+.1;
    }
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_QUEUE_CONFIRMED request=%u advanced=%d"),RequestId,bAdvanced);
}
void UDemoMeleeComponent::ServerRequestAttack_Implementation(int32 Index,uint32 RequestId)
{
    DEMO_LOG_CALL();
    if (RequestId==0) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_REQUEST_REJECT zero request id")); return; }
    const double Remaining=NextAttackTime-ServerTime(); // 旧普通尾部缓冲，不把独立起刀RPC解释为提前连招。
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>();
    if (Definition && IsEquipped() && Character() && Character()->CanUsePlayerSkills() && GameState
        && (Index==0 || Index==1) && Definition->Attacks.IsValidIndex(Index) && BufferedInput==INDEX_NONE
        && Definition->InputBufferSeconds>0.f && Remaining>=0. && Remaining<=Definition->InputBufferSeconds
        && (State.Attack!=INDEX_NONE || Remaining>0.))
    {
        ClearBufferedInput(); BufferedInput=Index; BufferedRequestId=RequestId; BufferedExpires=NextAttackTime+.1;
        BufferedPhase=GameState->Phase; BufferedLevel=GameState->LevelNumber;
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_REMOTE_BUFFERED request=%u remaining=%.3f"),RequestId,Remaining); return;
    }
    const uint32 Before=State.Serial; // 只有GA实际起刀才变化。
    CurrentInputRequest=RequestId; RequestAttack(Index); CurrentInputRequest=0;
    const bool bAccepted=State.Serial!=Before && State.OwnerRequestId==RequestId;
    ClientAttackResult(RequestId,bAccepted,State,NextAttackTime);
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_REQUEST_RESULT request=%u accepted=%d serial=%u"),RequestId,bAccepted,State.Serial);
}
void UDemoMeleeComponent::ClientAttackResult_Implementation(uint32 RequestId,bool bAccepted,FDemoMeleeState Confirmed,double RecoveryUntil)
{
    DEMO_LOG_CALL();
    if(bAccepted) RememberCameraAuthority(Confirmed); // 保存服务器真实刀号与请求，不依赖State复制是否先到；不改变预测采用/取消规则。
    if (!bUsePrediction || bPredictionCancelled) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_STALE_RESULT cancelled request=%u"),RequestId); return; }
    bool bDiscontinuousAdoption=false; // 已丢失合法父桥的晚纠正使用普通混合，不伪称已审连续边。
    const bool bQueuedStart=bAccepted && Confirmed.bStartedFromCombo && RequestId==LastQueuedComboRequest
        && PredictedState.OwnerRequestId==LastQueuedComboParent; // 接受实际起刀的晚回包，包括本机队列已过期的情况。
    if (bQueuedStart)
    {
        const ADemoCharacter* Pawn=Character();
        const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>();
        if (!Pawn || !Pawn->CanUsePlayerSkills() || !GameState || GameState->Phase!=PredictedPhase || GameState->LevelNumber!=PredictedLevel)
        { CancelPredictedPresentation(); return; }
        const bool bParentIntact=bBufferedCombo && BufferedRequestId==RequestId && BufferedParentRequestId==PredictedState.OwnerRequestId
            && BufferedSourceSerial==PredictedState.Serial && PredictedState.bComboExitCommitted && Definition->Attacks.IsValidIndex(PredictedState.Attack)
            && Confirmed.ComboSourceAttack==PredictedState.Attack && Confirmed.Attack==BufferedTargetAttack
            && FMath::IsNearlyEqual(Confirmed.EntryPhase,Definition->Attacks[PredictedState.Attack].ComboEntryPhase,KINDA_SMALL_NUMBER);
        if (bParentIntact)
        {
            const bool bAlreadyPending=bHasPendingConfirmedComboStart && PendingConfirmedComboStart.OwnerRequestId==RequestId
                && PendingConfirmedComboParent==PredictedState.OwnerRequestId;
            PendingConfirmedComboStart=Confirmed; PendingConfirmedComboParent=PredictedState.OwnerRequestId; bHasPendingConfirmedComboStart=true;
            const double Now=GetWorld()->GetTimeSeconds(),Boundary=SegmentEndTime(PredictedState); // 使用已有本地父桥出口，不按估计ServerTime提前跳下一刀。
            if (!bAlreadyPending) UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_ADOPT_DEFERRED request=%u parent=%u now=%.6f boundary=%.6f remaining=%.6f"),
                RequestId,PendingConfirmedComboParent,Now,Boundary,FMath::Max(0.,Boundary-Now));
            if (Now+1.e-6<Boundary || AdvancePredictedCombo()) return; // 保持当前桥；边界后沿原连续时钟消费确认，无第二次RPC/Serial重启。
            UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_DEFER_REJECT context/queue")); // 未能安全保留连续边时按普通权威纠正收敛。
        }
        ClearBufferedInput(); PredictedState=Confirmed; PredictedState.Serial=++LocalPresentationSerial;
        const float EntryElapsed=Definition->Attacks.IsValidIndex(Confirmed.Attack)?Definition->Attacks[Confirmed.Attack].RuntimeAtPhase(Confirmed.EntryPhase):0.f;
        const double ServerAge=ServerTime()-Confirmed.StartTime; // 估计同步时钟可能落后，真实已起刀不得在本机采到Entry以前。
        const double Age=FMath::Max(static_cast<double>(EntryElapsed),ServerAge);
        if (ServerAge<EntryElapsed) UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_ADOPT_CLOCK_CLAMP request=%u age=%.6f entry_seconds=%.6f"),RequestId,ServerAge,EntryElapsed);
        PredictedState.StartTime=GetWorld()->GetTimeSeconds()-Age;
        PredictedState.bStartedFromCombo=false; bDiscontinuousAdoption=true; // 取消/过期后丢失桥的回包不能零混合硬接。
        LocalRecoveryUntil=GetWorld()->GetTimeSeconds()+FMath::Max(0.,RecoveryUntil-ServerTime()); bSwingPlayed=false;
        UnconfirmedComboRequest=UnconfirmedComboParent=0; UpdatePresentation();
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_COMBO_ADOPT_STARTED request=%u phase=%.6f"),RequestId,Confirmed.EntryPhase);
    }
    if (RequestId!=PredictedState.OwnerRequestId)
    { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_STALE_RESULT request=%u"),RequestId); return; }
    PredictedState.NextHorizontalCursor=Confirmed.NextHorizontalCursor;
    if (!bAccepted)
    {
        if (UnconfirmedComboRequest==RequestId) RollbackComboPrediction(Confirmed,RecoveryUntil);
        else { CancelPredictedPresentation(); LocalRecoveryUntil=GetWorld()->GetTimeSeconds()+FMath::Max(0.,RecoveryUntil-ServerTime()); }
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_ROLLBACK request=%u"),RequestId); return;
    }
    if (Confirmed.Attack!=PredictedState.Attack)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_CORRECT request=%u from=%d to=%d"),RequestId,PredictedState.Attack,Confirmed.Attack);
        PredictedState.Attack=Confirmed.Attack;
    }
    PredictedState.bPrimarySequence=Confirmed.bPrimarySequence; PredictedState.bStartedFromCombo=Confirmed.bStartedFromCombo && !bDiscontinuousAdoption;
    PredictedState.ComboSourceAttack=Confirmed.ComboSourceAttack; PredictedState.EntryPhase=Confirmed.EntryPhase;
    if (Confirmed.bComboExitCommitted) PredictedState.bComboExitCommitted=true; // 早期起刀确认不能覆盖本人后来已发出的排队。
    if (UnconfirmedComboRequest==RequestId) UnconfirmedComboRequest=UnconfirmedComboParent=0;
    LocalRecoveryUntil=SegmentEndTime(PredictedState); // 普通确认不倒退起点/序号，也不重播挥声。
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_PREDICT_CONFIRMED request=%u response_seconds=%.3f"),RequestId,GetWorld()->GetTimeSeconds()-PredictedState.StartTime);
}
bool UDemoMeleeComponent::BufferLocalInput(int32 Index,double ReadyAt)
{
    DEMO_LOG_CALL();
    const ADemoCharacter* Pawn=Character();
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>();
    const double Remaining=ReadyAt-PresentationTime(); // 原普通完整收招的小窗口，过Branch早点击不会变为连招。
    if (!Pawn || !Pawn->IsLocallyControlled() || CurrentInputRequest!=0 || !GameState || !Definition || Definition->InputBufferSeconds<=0.f
        || Remaining<0. || Remaining>Definition->InputBufferSeconds) return false;
    if (BufferedInput!=INDEX_NONE) { UE_LOG(LogFPSDemo,Log,TEXT("MELEE_INPUT_BUFFER_IGNORED occupied")); return true; }
    ClearBufferedInput(); BufferedInput=Index; BufferedExpires=ReadyAt+.1; BufferedPhase=GameState->Phase; BufferedLevel=GameState->LevelNumber;
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_INPUT_BUFFERED index=%d remaining=%.3f"),Index,Remaining); return true;
}
void UDemoMeleeComponent::ConsumeBufferedInput()
{
    DEMO_LOG_TICK();
    if (BufferedInput==INDEX_NONE) return;
    const ADemoCharacter* Pawn=Character();
    const ADemoGameState* GameState=GetWorld()->GetGameState<ADemoGameState>();
    const double Now=PresentationTime(); // 输入截止采用同端时基，不混绝对服务器秒。
    if (bBufferedCombo && bHasPendingConfirmedComboStart && Now+1.e-6>=SegmentEndTime(PredictedState)
        && AdvancePredictedCombo()) return; // 已确认事实先消费；本机长帧不能把实际起刀当未知过期输入丢掉，函数仍重验死亡/菜单/阶段。
    if (!Pawn || !GameState || !IsEquipped() || !Pawn->CanUsePlayerSkills() || Now>BufferedExpires || GameState->Phase!=BufferedPhase || GameState->LevelNumber!=BufferedLevel)
    {
        const uint32 Request=BufferedRequestId,Parent=BufferedParentRequestId;
        const bool bCombo=bBufferedCombo;
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_INPUT_BUFFER_DISCARD combo=%d expired=%d request=%u"),bCombo,Now>BufferedExpires,Request);
        ClearBufferedInput();
        if (Pawn && !Pawn->HasAuthority() && bCombo && PredictedState.OwnerRequestId==Parent)
        { PredictedState.bComboExitCommitted=false; LocalRecoveryUntil=SegmentEndTime(PredictedState); } // 暂回完整父刀；实际起刀晚回包仍可按LastQueued身份收敛。
        if (Pawn && Pawn->HasAuthority() && Request!=0)
        {
            if (bCombo) ClientComboQueueResult(Request,Parent,false,State,NextAttackTime);
            else ClientAttackResult(Request,false,State,NextAttackTime);
        }
        return;
    }
    if (bBufferedCombo)
    {
        if (!Pawn->HasAuthority() && Now+1.e-6>=SegmentEndTime(PredictedState)) AdvancePredictedCombo(); // 权威只能由同一GA回调推进。
        return;
    }
    if (IsAttacking() || (Pawn->HasAuthority()?(State.Attack!=INDEX_NONE || Now<NextAttackTime):Now<LocalRecoveryUntil)) return;
    const int32 Input=BufferedInput;
    const uint32 Request=BufferedRequestId;
    ClearBufferedInput(); // 同步请求前出队，不复用或堆积旧点击。
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_INPUT_BUFFER_CONSUMED index=%d request=%u"),Input,Request);
    if (Pawn->HasAuthority() && Request!=0) ServerRequestAttack_Implementation(Input,Request); else RequestAttack(Input);
}

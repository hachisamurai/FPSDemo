// 职责：连接当前本人近战装备、挥声相位及真实HP回执到小幅CameraModifier；无新增RPC/伤害/存档权限。
// 使用：Melee表现更新维护唯一实例，取消/菜单/切枪/EndPlay精确清理；定义整个Pawn生命周期只在BeginPlay加载，回执不借其它武器配置。
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Weapons/Melee/DemoMeleeCameraModifier.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h" // 有界等待用本World游戏秒，不开Timer、不用墙钟重播旧回执。
#include "Debug/DemoLog.h"

void UDemoMeleeComponent::ClearCameraFeedback()
{
    DEMO_LOG_CALL();
    if(CameraFeedback.IsValid() && FeedbackCameraManager.IsValid()) FeedbackCameraManager->RemoveCameraModifier(CameraFeedback.Get());
    CameraFeedback.Reset(); FeedbackCameraManager.Reset(); PendingCameraHits.Reset();
}
void UDemoMeleeComponent::RememberCameraAuthority(const FDemoMeleeState& Confirmed)
{
    DEMO_LOG_CALL();
    if(Confirmed.Serial==0 || !Definition || !Definition->Attacks.IsValidIndex(Confirmed.Attack)) return; // Idle属性不能覆盖已知起刀时限，缺事实只等确认。
    for(const FDemoMeleeCameraAuthority& Existing:CameraAuthorities) if(Existing.Serial==Confirmed.Serial) return; // 同一服务器刀号不由迟到属性覆盖已确认关联。
    if(CameraAuthorities.Num()>=16) CameraAuthorities.RemoveAt(0);
    FDemoMeleeCameraAuthority Fact; Fact.Serial=Confirmed.Serial; Fact.Request=Confirmed.OwnerRequestId; Fact.ExpiresAt=SegmentEndTime(Confirmed)+.25; CameraAuthorities.Add(Fact);
}
void UDemoMeleeComponent::CancelCameraAttack()
{
    DEMO_LOG_CALL();
    CameraNaturalCompletionTime=-1.;
    const uint32 Request=PresentationState().OwnerRequestId; // 改写预测之前记录，无确认的本地请求也必须撤销。
    if(Request!=0 && !CancelledCameraRequests.Contains(Request))
    { if(CancelledCameraRequests.Num()>=16) CancelledCameraRequests.RemoveAt(0); CancelledCameraRequests.Add(Request); }
    if(GetOwner() && GetOwner()->HasAuthority() && State.Serial!=0 && !CancelledCameraSerials.Contains(State.Serial))
    { if(CancelledCameraSerials.Num()>=16) CancelledCameraSerials.RemoveAt(0); CancelledCameraSerials.Add(State.Serial); }
    ClearCameraFeedback();
}
void UDemoMeleeComponent::UpdateCameraFeedback()
{
    DEMO_LOG_TICK();
    ADemoCharacter* Pawn=Character(); ADemoPlayerController* PC=Pawn?Cast<ADemoPlayerController>(Pawn->GetController()):nullptr; // 仅GT借用；模拟端没有本人PC便不创建。
    APlayerCameraManager* Manager=PC?PC->PlayerCameraManager.Get():nullptr;
    const bool bAllowed=Pawn && Pawn->IsLocallyControlled() && IsEquipped() && Definition && Definition->CameraFeedback.bEnabled
        && Pawn->CanUsePlayerSkills() && PC && PC->GetViewTarget()==Pawn && Manager;
    if(!bAllowed || (FeedbackCameraManager.IsValid() && FeedbackCameraManager.Get()!=Manager))
    {
        if(CameraFeedback.IsValid() || FeedbackCameraManager.IsValid()) ClearCameraFeedback();
        if(!bAllowed) { PendingCameraHits.Reset(); return; } // 未注册Modifier时收到的pending也不能穿菜单/视角保留。
    }
    if(!CameraFeedback.IsValid())
    {
        UDemoMeleeCameraModifier* Modifier=Cast<UDemoMeleeCameraModifier>(Manager->AddNewCameraModifier(UDemoMeleeCameraModifier::StaticClass())); // Manager为唯一强Owner，不挂GameMode或创建额外Tick。
        if(!Modifier) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_CAMERA_REJECT could not create modifier")); return; }
        Modifier->Initialize(Pawn,Definition->CameraFeedback); CameraFeedback=Modifier; FeedbackCameraManager=Manager;
    }
    const double Now=GetWorld()->GetTimeSeconds(); // 同步有界队列，允许确认晚到；不额外生成HP回执或延长TTL。
    for(int32 Index=PendingCameraHits.Num()-1;Index>=0;--Index)
    {
        const FDemoMeleePendingCameraHit& Pending=PendingCameraHits[Index];
        const uint32 Serial=static_cast<uint32>(Pending.Shot.B); uint32 Request=0; bool bKnown=false; double ExpiresAt=0.;
        for(const FDemoMeleeCameraAuthority& Fact:CameraAuthorities) if(Fact.Serial==Serial) { Request=Fact.Request; ExpiresAt=Fact.ExpiresAt; bKnown=true; break; }
        const bool bExpired=Now-Pending.ReceivedAt>.25 || (bKnown && ServerTime()>ExpiresAt); // 到期先拒绝，不能确认晚到后先播再删。
        const bool bExplicitlyCancelled=bUsePrediction && bPredictionCancelled && (CameraNaturalCompletionTime<0. || Now-CameraNaturalCompletionTime>.25);
        const bool bCancelled=CancelledCameraSerials.Contains(Serial) || (bKnown && CancelledCameraRequests.Contains(Request)) || bExplicitlyCancelled;
        const bool bCurrent=bKnown && (bUsePrediction?PresentationState().OwnerRequestId==Request:State.Serial==Serial); // 房主也只接受当前刀，16条历史不是都当前。
        if(bCurrent && !bCancelled && !bExpired) CameraFeedback->PlayHit(Pending.Shot);
        if(bCurrent || bCancelled || bExpired)
        { if(!bCurrent || bCancelled) UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_CAMERA_HIT_SKIP stale/cancelled/confirmation timeout")); PendingCameraHits.RemoveAt(Index); }
    }
}
void UDemoMeleeComponent::PlaySwingCameraFeedback(int32 AttackIndex)
{
    DEMO_LOG_CALL();
    if(CameraFeedback.IsValid()) CameraFeedback->PlaySwing(GetAttackSerial(),AttackIndex%2==0?1.f:-1.f); // 当前本端Serial保证预测确认不重播，正反手方向分开。
}
void UDemoMeleeComponent::ConfirmCameraHit(const FGuid& ShotId)
{
    DEMO_LOG_CALL();
    if(!ShotId.IsValid() || !IsEquipped() || !GetWorld()
        || (bUsePrediction && bPredictionCancelled && (CameraNaturalCompletionTime<0. || GetWorld()->GetTimeSeconds()-CameraNaturalCompletionTime>.25)))
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_CAMERA_HIT_SKIP cancelled/equipment")); return; }
    for(const FDemoMeleePendingCameraHit& Existing:PendingCameraHits) if(Existing.Shot==ShotId) return;
    if(PendingCameraHits.Num()>=8) PendingCameraHits.RemoveAt(0);
    FDemoMeleePendingCameraHit Pending; Pending.Shot=ShotId; Pending.ReceivedAt=GetWorld()->GetTimeSeconds(); PendingCameraHits.Add(Pending);
    UpdateCameraFeedback();
}

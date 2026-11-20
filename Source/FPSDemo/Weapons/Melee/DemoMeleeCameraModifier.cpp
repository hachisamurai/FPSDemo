// 职责：最终POV的小幅近战Roll包络及与既有受击的合成限额；不操作骨骼、镜头位置或玩法瞄准。
// 使用：本人Melee注册/撤销唯一实例，CameraManager每帧调用；所有对象仅游戏线程借用。
#include "Weapons/Melee/DemoMeleeCameraModifier.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "GAS/DemoAttributeSet.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

UDemoMeleeCameraModifier::UDemoMeleeCameraModifier()
{
    DEMO_LOG_CALL(); Priority=230; bExclusive=false; // 受击Priority220先合成；不把效果再写进Pawn CameraComponent。
}
void UDemoMeleeCameraModifier::Initialize(ADemoCharacter* Pawn,const FDemoMeleeCameraFeedbackSettings& Config)
{
    DEMO_LOG_CALL(); Avatar=Pawn; Settings=Config;
    Settings.SwingRollDegrees=FMath::IsFinite(Config.SwingRollDegrees)?FMath::Clamp(Config.SwingRollDegrees,0.f,.3f):0.f;
    Settings.HitRollDegrees=FMath::IsFinite(Config.HitRollDegrees)?FMath::Clamp(Config.HitRollDegrees,0.f,.4f):0.f;
    Settings.SwingSeconds=FMath::IsFinite(Config.SwingSeconds)?FMath::Clamp(Config.SwingSeconds,.05f,.3f):.14f;
    Settings.HitSeconds=FMath::IsFinite(Config.HitSeconds)?FMath::Clamp(Config.HitSeconds,.05f,.3f):.10f;
    Settings.MaximumRollDegrees=FMath::IsFinite(Config.MaximumRollDegrees)?FMath::Clamp(Config.MaximumRollDegrees,0.f,.4f):0.f;
    SwingStarted=HitStarted=-1.; LastSwingSerial=0; HeardHits.Reset();
}
bool UDemoMeleeCameraModifier::CanPresent() const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=Avatar.Get(); // 同步弱引用，PC观战可能仍保留原Pawn，必须再核ViewTarget。
    const ADemoPlayerController* PC=CameraOwner?Cast<ADemoPlayerController>(CameraOwner->PCOwner.Get()):nullptr;
    const UDemoAttributeSet* Attributes=Pawn?Pawn->GetDemoAttributes():nullptr;
    const UDemoMeleeComponent* Melee=Pawn?Pawn->FindComponentByClass<UDemoMeleeComponent>():nullptr;
    return Settings.bEnabled && Pawn && PC && PC->IsLocalController() && PC->GetPawn()==Pawn && PC->GetViewTarget()==Pawn
        && Melee && Melee->IsEquipped() && !Pawn->IsCoopEliminated() && Attributes && Attributes->GetHealth()>0.f
        && !PC->HasBlockingOverlay() && !PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen() && !GetWorld()->IsPaused();
}
float UDemoMeleeCameraModifier::Envelope(double Now,double Start,float Duration)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoMeleeCameraModifier::Envelope"));
    if(Start<0. || Now<Start || Now>=Start+Duration) return 0.f;
    const double S=FMath::Sin(PI*(Now-Start)/Duration); // 固定有限包络，不累积随机噪声/控制角。
    return static_cast<float>(S*S);
}
void UDemoMeleeCameraModifier::PlaySwing(uint32 Serial,float Direction)
{
    DEMO_LOG_CALL();
    if(!CanPresent() || Serial==0 || Serial==LastSwingSerial) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_CAMERA_SWING_SKIP context/duplicate")); return; }
    LastSwingSerial=Serial; SwingDirection=Direction<0.f?-1.f:1.f; SwingStarted=GetWorld()->GetTimeSeconds();
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_CAMERA_SWING serial=%u direction=%.0f degrees=%.3f"),Serial,SwingDirection,Settings.SwingRollDegrees);
}
void UDemoMeleeCameraModifier::PlayHit(const FGuid& ShotId)
{
    DEMO_LOG_CALL();
    if(!CanPresent() || !ShotId.IsValid() || HeardHits.Contains(ShotId)) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("MELEE_CAMERA_HIT_SKIP context/duplicate")); return; }
    if(HeardHits.Num()>=32) HeardHits.RemoveAt(0); HeardHits.Add(ShotId);
    HitStarted=GetWorld()->GetTimeSeconds();
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_CAMERA_HIT shot=%s degrees=%.3f"),*ShotId.ToString(),Settings.HitRollDegrees);
}
bool UDemoMeleeCameraModifier::ModifyCamera(float DeltaTime,FMinimalViewInfo& InOutPOV)
{
    DEMO_LOG_TICK();
    if(!CanPresent()) { SwingStarted=HitStarted=-1.; return false; } // 阻塞后不在关闭菜单时恢复旧包络。
    const double Now=GetWorld()->GetTimeSeconds(); // 暂停友好，同一个World无墙钟/独立Tick。
    const float Roll=SwingDirection*FMath::Clamp(Settings.SwingRollDegrees*Envelope(Now,SwingStarted,Settings.SwingSeconds)
        +Settings.HitRollDegrees*Envelope(Now,HitStarted,Settings.HitSeconds),0.f,Settings.MaximumRollDegrees);
    const double Previous=FRotator::NormalizeAxis(InOutPOV.Rotation.Roll); // 既有受击等已合成，不能分别夹限后冒称整体有限。
    if(FMath::Abs(Previous)<=.65) InOutPOV.Rotation.Roll=FMath::Clamp(Previous+Roll,-.65,.65); // 当前受击±.4与本功能共同受限；其它大幅特殊模式不加本反馈。
    return false;
}
#if WITH_EDITOR
uint32 UDemoMeleeCameraModifier::GetHitFeedbackCountForAudit() const
{
    DEMO_LOG_TICK(); return static_cast<uint32>(HeardHits.Num()); // 实际PlayHit消费表，不把待核HP队列或计划命中当接受数。
}
#endif

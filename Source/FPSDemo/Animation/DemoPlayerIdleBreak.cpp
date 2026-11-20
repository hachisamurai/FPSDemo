// 职责：协调原生随机待机与当前枪械表现；NativeUpdate在游戏线程发布原生状态资格/代理意图，不改Lyra计时器或Gameplay。Ready层既有CanPlayIdleBreak同时消费进入与提前退出资格。
#include "Animation/DemoLyraAnimInstance.h"
#include "Animation/DemoPlayerAnimProxy.h"
#include "Animation/DemoPlayerAimSettings.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Characters/DemoCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Debug/DemoLog.h"

void UDemoLyraAnimInstance::UpdateIdleBreakMotion(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    const UDemoPlayerAimSettings* Settings=GetDefault<UDemoPlayerAimSettings>(); // 本进程只读配置，任务线程不查询CDO。
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(TryGetPawnOwner()); // 当前Avatar仅本回调借用。
    const TSubclassOf<UAnimInstance>* Configured=WeaponLayers.Find(GetUpperWeaponId()); // 不重新链接或加载资产。
    UAnimInstance* Layer=GetLinkedAnimLayerInstanceByClass(Configured?*Configured:DefaultLayer); // 当前已运行子层，读取上一求值完成后的真实权重。
    NativeIdleBreakWeight=0.f;
    if (Layer)
    {
        const FBakedAnimationStateMachine* Description=Layer->GetStateMachineInstanceDesc(TEXT("IdleSM")); // 只读烘焙描述，不硬编码状态索引。
        const FAnimNode_StateMachine* Machine=Layer->GetStateMachineInstanceFromName(TEXT("IdleSM"));
        if (Description && Machine) for (int32 I=0;I<Description->States.Num();++I)
            if (Description->States[I].StateName==TEXT("IdleBreak")) { NativeIdleBreakWeight=Machine->GetStateWeight(I); break; }
    }
    const float Step=FMath::Max(0.f,DeltaSeconds); // 游戏秒，暂停不推进重入/混合。
    const FQuat Aim=Pawn?Pawn->GetBaseAimRotation().Quaternion():FQuat::Identity; // 不读观战CameraManager，远端用已有Pawn视线。
    const bool bLookChanged=bHasIdleAim && FMath::RadiansToDegrees(PreviousIdleAim.AngularDistance(Aim))>.1f; // 超过0.1度的实际视线变化中断，量化微扰不抖动开关。
    PreviousIdleAim=Aim; bHasIdleAim=true;
    const bool bChangedWeapon=IdleBreakWeapon!=GetUpperWeaponId();
    const FRotator View=Pawn?Pawn->GetBaseAimRotation():FRotator::ZeroRotator; // Pawn实际控制视线，不从完成骨姿态反推造成反馈。
    const float RelativeViewYaw=Pawn?FMath::FindDeltaAngleDegrees(static_cast<float>(Pawn->GetActorRotation().Yaw),static_cast<float>(View.Yaw)):0.f; // 左右相对身体最短角，度。
    const bool bNeutralView=FMath::Abs(FRotator::NormalizeAxis(View.Pitch))<=Settings->IdleBreakMaxViewPitchDegrees && FMath::Abs(RelativeViewYaw)<=Settings->IdleBreakMaxRelativeYawDegrees; // 大幅原生闲置只在近中性稳定视线播放；普通Hold/呼吸不受限。
    const bool bRest=Pawn && !Pawn->IsCoopEliminated() && Pawn->GetCharacterMovement()->IsMovingOnGround() && Speed<3.f && !bSprinting && UpperBodyAction==TEXT("Hold") && Pawn->GetWeaponComponent() && Pawn->GetWeaponComponent()->GetScopeLevel()==0 && bNeutralView; // 非移动/射击/换弹/开镜且近中性视线。
    if (bChangedWeapon || bLookChanged || !bRest)
    { IdleBreakRearmRemaining=Settings->IdleBreakRearmSeconds; bIdleBreakWaitForNeutral=true; }
    else IdleBreakRearmRemaining=FMath::Max(0.f,IdleBreakRearmRemaining-Step);
    IdleBreakWeapon=GetUpperWeaponId();
    if (NativeIdleBreakWeight<.001f && IdleBreakRearmRemaining<=0.f) bIdleBreakWaitForNeutral=false;
    bool bEnabled=Settings->bEnableIdleBreakMotion && Pawn && AuthoredAimProfile && bCalibratedBoreAim && GetUpperWeaponId()!=TEXT("melee"); // 缺参考/近战沿原通道，不读取上一枪数据。
#if WITH_EDITOR
    if (MotionIsolationMode==TEXT("Native") || MotionIsolationMode==TEXT("NoAim")) bEnabled=false; // 保持既有诊断隔离含义，不让修复通道干扰对照。
#endif
    const bool bManagedGun=Pawn && AuthoredAimProfile && bCalibratedBoreAim && GetUpperWeaponId()!=TEXT("melee"); // 仅已标定枪械接管原生大幅IdleBreak，近战/无标定沿原图。
    const bool bAllow=bManagedGun?bEnabled && bRest && !bIdleBreakWaitForNeutral && IdleBreakRearmRemaining<=0.f:true; // 与手部差量统一资格，防原生Base腰背在差量为0时继续抬枪出画。
    bool bNextAllow=bAllow; // Editor原生隔离仅绕过新资格，以保留旧AB含义；正常游戏不存在该旁路。
#if WITH_EDITOR
    if (MotionIsolationMode==TEXT("Native") || MotionIsolationMode==TEXT("NoAim")) bNextAllow=true;
#endif
    if (bAllowNativeIdleBreak!=bNextAllow) UE_LOG(LogFPSDemo,Log,TEXT("NATIVE_IDLE_GATE allow=%d weapon=%s rest=%d neutral=%d lookChanged=%d waitNeutral=%d rearm=%.3f native=%.3f"),bNextAllow,*GetUpperWeaponId().ToString(),bRest,bNeutralView,bLookChanged,bIdleBreakWaitForNeutral,IdleBreakRearmRemaining,NativeIdleBreakWeight);
    bAllowNativeIdleBreak=bNextAllow; // 本帧任务开始前写；原CanPlayIdleBreak的进入与!CanPlay提前退出均读此值。
    const float Target=bEnabled && bRest && !bIdleBreakWaitForNeutral && IdleBreakRearmRemaining<=0.f?NativeIdleBreakWeight:0.f; // 被打断轮次即使计时结束也不能中途恢复。
    const float Previous=IdleBreakMotionWeight; // 只记录启停边沿，细值走Tick埋点/诊断。
    IdleBreakMotionWeight=FMath::FInterpConstantTo(IdleBreakMotionWeight,Target,Step,1.f/Settings->IdleBreakBlendSeconds);
    if (bChangedWeapon || !bEnabled) IdleBreakMotionWeight=0.f; // 不跨枪/Avatar混合不兼容握姿。
    if ((Previous<.001f)!=(IdleBreakMotionWeight<.001f)) UE_LOG(LogFPSDemo,Log,TEXT("IDLE_MOTION active=%d weapon=%s native=%.3f rest=%d look=%d"),IdleBreakMotionWeight>=.001f,*GetUpperWeaponId().ToString(),NativeIdleBreakWeight,bRest,bLookChanged);
    FDemoIdleMotionFrame& Frame=GetProxyOnGameThread<FDemoPlayerAnimProxy>().IdleMotion; // 引擎派发本帧任务前写，任务只顺序读/写其Pose缓冲。
    Frame.Weapon=bEnabled?GetUpperWeaponId():NAME_None; Frame.Weight=IdleBreakMotionWeight; Frame.bPoseValid=false;
    Frame.bCaptureReference=bEnabled && bRest && !bLookChanged && NativeIdleBreakWeight<.001f && IdleBreakMotionWeight<.001f; // 只从原生中立取参考，不把被打断的半段动作当Hold。
}

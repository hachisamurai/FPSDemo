// 职责：加载并验证项目视角配置；CDO初始化调用，非法整组参数回退并记录，不访问World或动画工作线程。
#include "Animation/DemoPlayerAimSettings.h"
#include "Debug/DemoLog.h"

void UDemoPlayerAimSettings::PostInitProperties()
{
    DEMO_LOG_CALL(); Super::PostInitProperties();
    if (!FMath::IsFinite(LookDownDegrees) || LookDownDegrees<1.f || LookDownDegrees>75.f
        || !FMath::IsFinite(LookUpDegrees) || LookUpDegrees<1.f || LookUpDegrees>75.f
        || !FMath::IsFinite(HeadYawLimit) || HeadYawLimit<1.f || HeadYawLimit>60.f
        || !FMath::IsFinite(BodyTurnFollowRate) || BodyTurnFollowRate<1.f || BodyTurnFollowRate>60.f
        || !FMath::IsFinite(TorsoFraction) || TorsoFraction<0.f || TorsoFraction>.35f
        || !FMath::IsFinite(RemoteFollowRate) || RemoteFollowRate<1.f || RemoteFollowRate>60.f
        || !FMath::IsFinite(PistolLowAimOffset) || PistolLowAimOffset<0.f || PistolLowAimOffset>8.f)
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_AIM_CONFIG_INVALID down=%g up=%g headYaw=%g torso=%g rate=%g pistolOffset=%g; defaults restored"),LookDownDegrees,LookUpDegrees,HeadYawLimit,TorsoFraction,RemoteFollowRate,PistolLowAimOffset);
        LookDownDegrees=60.f; LookUpDegrees=55.f; HeadYawLimit=45.f; BodyTurnFollowRate=14.f; TorsoFraction=.18f; RemoteFollowRate=24.f; PistolLowAimOffset=4.f; // 整组回退同一默认配置，避免静止永不收敛。
    }
    UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_AIM_CONFIG down=%.1f up=%.1f headYaw=%.1f torso=%.2f rate=%.1f pistolOffset=%.1f"),LookDownDegrees,LookUpDegrees,HeadYawLimit,TorsoFraction,RemoteFollowRate,PistolLowAimOffset);
    UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_BODY_TURN_CONFIG followRate=%.2f"),BodyTurnFollowRate); // 配置只在加载时记录，逐帧仍用限时TraceCalls。
    if (!FMath::IsFinite(ReloadAimRestoreExponent) || ReloadAimRestoreExponent<1.f || ReloadAimRestoreExponent>4.f)
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("RELOAD_AIM_CONFIG_INVALID exponent=%g; shared source blend restored"),ReloadAimRestoreExponent);
        ReloadAimRestoreExponent=1.f; // 单独回退本项，不覆盖已有视角/追焦配置。
    }
    UE_LOG(LogFPSDemo,Log,TEXT("RELOAD_AIM_CONFIG exponent=%.3f source_duration_unchanged=1 shotgun_serial_recovery_preserved=1"),ReloadAimRestoreExponent); // 同进程AB可核真正消费值，不能只凭启动参数猜策略。
    if (!FMath::IsFinite(MuzzleFocusMinDistance) || MuzzleFocusMinDistance<50.f || MuzzleFocusMinDistance>1000.f
        || !FMath::IsFinite(MuzzleFocusMaxDegrees) || MuzzleFocusMaxDegrees<1.f || MuzzleFocusMaxDegrees>20.f
        || !FMath::IsFinite(MuzzleFocusDistanceFollowRate) || MuzzleFocusDistanceFollowRate<1.f || MuzzleFocusDistanceFollowRate>60.f)
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FOCUS_CONFIG_INVALID min=%g maxAngle=%g rate=%g; focus defaults restored"),MuzzleFocusMinDistance,MuzzleFocusMaxDegrees,MuzzleFocusDistanceFollowRate);
        MuzzleFocusMinDistance=200.f; MuzzleFocusMaxDegrees=15.f; MuzzleFocusDistanceFollowRate=20.f; // 只回退新功能，保持既有相机/作者瞄准配置。
    }
    UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_FOCUS_CONFIG enabled=%d minCm=%.1f maxDeg=%.1f distanceRate=%.1f"),bEnableMuzzleFocus,MuzzleFocusMinDistance,MuzzleFocusMaxDegrees,MuzzleFocusDistanceFollowRate);
    if (!FMath::IsFinite(IdleBreakBlendSeconds) || IdleBreakBlendSeconds<.04f || IdleBreakBlendSeconds>.5f || !FMath::IsFinite(IdleBreakRearmSeconds) || IdleBreakRearmSeconds<.2f || IdleBreakRearmSeconds>5.f)
    { UE_LOG(LogFPSDemo,Warning,TEXT("IDLE_MOTION_CONFIG_INVALID; defaults restored")); IdleBreakBlendSeconds=.15f; IdleBreakRearmSeconds=1.5f; }
    UE_LOG(LogFPSDemo,Log,TEXT("IDLE_MOTION_CONFIG enabled=%d blend=%.3f rearm=%.3f"),bEnableIdleBreakMotion,IdleBreakBlendSeconds,IdleBreakRearmSeconds);
    if (!FMath::IsFinite(IdleBreakMaxViewPitchDegrees) || IdleBreakMaxViewPitchDegrees<1.f || IdleBreakMaxViewPitchDegrees>45.f
        || !FMath::IsFinite(IdleBreakMaxRelativeYawDegrees) || IdleBreakMaxRelativeYawDegrees<1.f || IdleBreakMaxRelativeYawDegrees>45.f)
    { UE_LOG(LogFPSDemo,Warning,TEXT("NATIVE_IDLE_GATE_CONFIG_INVALID; neutral-view defaults restored")); IdleBreakMaxViewPitchDegrees=IdleBreakMaxRelativeYawDegrees=20.f; }
    UE_LOG(LogFPSDemo,Log,TEXT("NATIVE_IDLE_GATE_CONFIG maxPitchDegrees=%.3f maxRelativeYawDegrees=%.3f source_state_gate=1"),IdleBreakMaxViewPitchDegrees,IdleBreakMaxRelativeYawDegrees);
}

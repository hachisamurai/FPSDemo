// 职责：敌人与可破坏设施共用的命中闪光配色和衰减。使用：本地表现消费已确认损血；仅返回值，不持有材质、不Tick、不参与伤害。
#pragma once
#include "CoreMinimal.h"
#include "Debug/DemoLog.h"

namespace DemoHitFlash
{
    /** bWeakPoint/bFatal为已确认结果；返回普通冷白、弱点金色、致死暖红的HDR峰值，不能据颜色推导伤害权限。 */
    inline FLinearColor PeakTint(bool bWeakPoint, bool bFatal)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
        return (bFatal ? FLinearColor(1.f, .22f, .12f) : bWeakPoint ? FLinearColor(1.f, .72f, .22f) : FLinearColor(.85f, .94f, 1.f)) * 4.f;
    }

    /** Remaining/Duration为同次脉冲剩余/总秒数；快速起亮、二次衰减，非法或过期值返回0以恢复基色。 */
    inline float Strength(float Remaining, float Duration)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
        return FMath::IsFinite(Remaining) && FMath::IsFinite(Duration) && Duration > 0.f
            ? FMath::Square(FMath::Clamp(Remaining / Duration, 0.f, 1.f)) : 0.f;
    }
}

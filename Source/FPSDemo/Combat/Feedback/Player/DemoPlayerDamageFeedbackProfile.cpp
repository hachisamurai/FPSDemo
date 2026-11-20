// 职责：玩家表现参数预检；音频缺失仅降级，非法数值回退原生默认，不修改玩法属性。
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackProfile.h"
#include "Debug/DemoLog.h"
UDemoPlayerDamageFeedbackProfile::UDemoPlayerDamageFeedbackProfile() { DEMO_LOG_CALL(); }
bool UDemoPlayerDamageFeedbackProfile::Validate() const
{
    DEMO_LOG_CALL(); const float Values[]={IndicatorSeconds,EdgeSeconds,TrailHoldSeconds,TrailFadeSeconds,LightFraction,HeavyFraction,LightEdge,MediumEdge,HeavyEdge,LightRoll,HeavyRoll,RollSeconds,LowEnter,LowExit,CriticalEnter,CriticalExit,ContactVolume,VoiceVolume,PainChance,LowHealthVolume}; // 小配置集中拒NaN/Inf。
    for(float Value:Values) if(!FMath::IsFinite(Value)) return false;
    return IndicatorSeconds>0.f&&IndicatorSeconds<=2.f&&EdgeSeconds>0.f&&EdgeSeconds<=1.f&&TrailHoldSeconds>=0.f&&TrailFadeSeconds>0.f&&TrailFadeSeconds<=2.f&&LightFraction>0.f&&LightFraction<HeavyFraction&&HeavyFraction<=1.f&&LightEdge>=0.f&&LightEdge<=.25f&&MediumEdge>=0.f&&MediumEdge<=.25f&&HeavyEdge>=0.f&&HeavyEdge<=.25f&&LightRoll>=0.f&&LightRoll<=.4f&&HeavyRoll>=0.f&&HeavyRoll<=.4f&&RollSeconds>0.f&&RollSeconds<=.4f&&CriticalEnter>0.f&&CriticalEnter<CriticalExit&&CriticalExit<=LowEnter&&LowEnter<LowExit&&LowExit<=1.f&&ContactVolume>=0.f&&ContactVolume<=1.f&&VoiceVolume>=0.f&&VoiceVolume<=1.f&&PainChance>=0.f&&PainChance<=1.f&&LowHealthVolume>=0.f&&LowHealthVolume<=1.f;
}

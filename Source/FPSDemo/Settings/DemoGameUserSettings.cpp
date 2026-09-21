#include "Settings/DemoGameUserSettings.h"
#include "Debug/DemoLog.h"
UDemoGameUserSettings::UDemoGameUserSettings() { DEMO_LOG_CALL(); }
void UDemoGameUserSettings::LoadSettings(bool bForceReload)
{
    DEMO_LOG_CALL();
    Super::LoadSettings(bForceReload);
    SetMouseSensitivity(MouseSensitivity); // 手工编辑INI不能注入NaN/无限输入。
}
float UDemoGameUserSettings::GetMouseSensitivity() const { DEMO_LOG_TICK(); return MouseSensitivity; }
void UDemoGameUserSettings::SetMouseSensitivity(float Value)
{
    DEMO_LOG_CALL();
    MouseSensitivity = FMath::IsFinite(Value) ? FMath::Clamp(Value,.1f,3.f) : 1.f;
}

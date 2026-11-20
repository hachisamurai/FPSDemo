// 职责：设备级设置与持久偏好。使用：菜单调用带日志的读写接口，保存到GameUserSettings.ini，不进入战役或云存档。
// 职责：本机显示、输入和受击/UI舒适度设备设置；调用者为本地设置页与视口，独立保存INI，不复制或写战役/云。
#include "Settings/DemoGameUserSettings.h"
#include "Debug/DemoLog.h"
#include "HAL/IConsoleManager.h" // 同步游戏RHI和编辑器Slate各自使用的呈现开关。
UDemoGameUserSettings::UDemoGameUserSettings() { DEMO_LOG_CALL(); }
void UDemoGameUserSettings::SetToDefaults()
{
    DEMO_LOG_CALL(); Super::SetToDefaults();
    bSkipGambleAnimation=false; // 恢复设备默认时恢复动画，与战役数据分离。
    bReduceUIMotion=false; bEnableUISound=true; // 恢复设备默认的表现开关，仍不改变新手提示已读记录。
    DamageShakeScale=DamageFlashScale=1.f; bHurtVoiceEnabled=bLowHealthAudioEnabled=true; // 受击舒适度与UI动效独立，恢复默认时开启。
    // 新手提示已读属于使用记录，不随画质恢复默认或战役重开重置；首次构造仍由成员默认false初始化。
    FrameSyncSettingsVersion=0; SetVSyncEnabled(true); // 新设备ValidateSettings会随后再次LoadSettings，需重新执行缺省迁移。
}
void UDemoGameUserSettings::LoadSettings(bool bForceReload)
{
    DEMO_LOG_CALL();
    Super::LoadSettings(bForceReload);
    if(FrameSyncSettingsVersion<1) { SetVSyncEnabled(true); FrameSyncSettingsVersion=1; } // 首次修正旧隐式关闭值；之后关闭选择仍可持久化。
    SetMouseSensitivity(MouseSensitivity); // 手工编辑INI不能注入NaN/无限输入。
    DamageShakeScale=FMath::IsFinite(DamageShakeScale)?FMath::Clamp(DamageShakeScale,0.f,1.f):1.f; // 读盘只规范内存，避免LoadSettings内重复写盘。
    DamageFlashScale=FMath::IsFinite(DamageFlashScale)?FMath::Clamp(DamageFlashScale,0.f,1.f):1.f;
}
void UDemoGameUserSettings::ApplyNonResolutionSettings()
{
    DEMO_LOG_CALL();
    Super::ApplyNonResolutionSettings();
    ApplyFrameSync();
}
void UDemoGameUserSettings::ApplyFrameSync()
{
    DEMO_LOG_CALL();
    // UE5.4的PIE由Slate呈现，单改r.VSync对Editor窗口无效；普通游戏只使用r.VSync。
    for(const TCHAR* Name : {TEXT("r.VSync"),TEXT("r.VSyncEditor")}) // 当前调用同步借用注册CVar，不缓存跨引擎关闭的指针。
    {
#if !WITH_EDITOR
        if(FCString::Strcmp(Name,TEXT("r.VSyncEditor"))==0) continue; // 包体不修改编辑器设置。
#endif
        if(IConsoleVariable* Variable=IConsoleManager::Get().FindConsoleVariable(Name)) // 引擎拥有，仅本栈有效。
        {
            const EConsoleVariableFlags Priority=FCString::Strcmp(Name,TEXT("r.VSyncEditor"))==0?ECVF_SetByProjectSetting:ECVF_SetByGameSetting; // EditorPerformanceSettings以ProjectSetting写默认0，需同级覆盖；仍低于显式控制台/命令行设置。
            Variable->Set(IsVSyncEnabled()?1:0,Priority);
            UE_LOG(LogFPSDemo,Log,TEXT("FRAME_SYNC name=%s requested=%d actual=%d"),Name,IsVSyncEnabled(),Variable->GetInt());
        }
    }
}
float UDemoGameUserSettings::GetMouseSensitivity() const { DEMO_LOG_TICK(); return MouseSensitivity; }
void UDemoGameUserSettings::SetMouseSensitivity(float Value)
{
    DEMO_LOG_CALL();
    MouseSensitivity = FMath::IsFinite(Value) ? FMath::Clamp(Value,.1f,3.f) : 1.f;
}

bool UDemoGameUserSettings::IsGambleAnimationSkipped() const { DEMO_LOG_TICK(); return bSkipGambleAnimation; }
void UDemoGameUserSettings::SetGambleAnimationSkipped(bool bSkip)
{ DEMO_LOG_CALL(); bSkipGambleAnimation=bSkip; SaveSettings(); }

bool UDemoGameUserSettings::HasDismissedFirstRunGuide() const
{ DEMO_LOG_TICK(); return bFirstRunGuideDismissed; }
bool UDemoGameUserSettings::IsUIMotionReduced() const
{ DEMO_LOG_TICK(); return bReduceUIMotion; }
void UDemoGameUserSettings::SetUIMotionReduced(bool bReduced)
{ DEMO_LOG_CALL(); bReduceUIMotion=bReduced; SaveSettings(); }
bool UDemoGameUserSettings::IsUISoundEnabled() const
{ DEMO_LOG_TICK(); return bEnableUISound; }
void UDemoGameUserSettings::SetUISoundEnabled(bool bEnabled)
{ DEMO_LOG_CALL(); bEnableUISound=bEnabled; SaveSettings(); }
float UDemoGameUserSettings::GetDamageShakeScale() const
{ DEMO_LOG_TICK(); return DamageShakeScale; }
void UDemoGameUserSettings::SetDamageShakeScale(float Value)
{ DEMO_LOG_CALL(); DamageShakeScale=FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):1.f; SaveSettings(); }
float UDemoGameUserSettings::GetDamageFlashScale() const
{ DEMO_LOG_TICK(); return DamageFlashScale; }
void UDemoGameUserSettings::SetDamageFlashScale(float Value)
{ DEMO_LOG_CALL(); DamageFlashScale=FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):1.f; SaveSettings(); }
bool UDemoGameUserSettings::IsHurtVoiceEnabled() const
{ DEMO_LOG_TICK(); return bHurtVoiceEnabled; }
void UDemoGameUserSettings::SetHurtVoiceEnabled(bool bEnabled)
{ DEMO_LOG_CALL(); bHurtVoiceEnabled=bEnabled; SaveSettings(); }
bool UDemoGameUserSettings::IsLowHealthAudioEnabled() const
{ DEMO_LOG_TICK(); return bLowHealthAudioEnabled; }
void UDemoGameUserSettings::SetLowHealthAudioEnabled(bool bEnabled)
{ DEMO_LOG_CALL(); bLowHealthAudioEnabled=bEnabled; SaveSettings(); }
void UDemoGameUserSettings::DismissFirstRunGuide()
{
    DEMO_LOG_CALL();
    if(bFirstRunGuideDismissed) { UE_LOG(LogFPSDemo,Log,TEXT("FIRST_RUN_GUIDE already dismissed")); return; }
    bFirstRunGuideDismissed=true;
    SaveSettings(); // 只在明确关闭时写一次GameUserSettings.ini；HUD绘制不写盘，PIE存档清理不处理此文件。
    UE_LOG(LogFPSDemo,Log,TEXT("FIRST_RUN_GUIDE dismissal stored in device settings"));
}

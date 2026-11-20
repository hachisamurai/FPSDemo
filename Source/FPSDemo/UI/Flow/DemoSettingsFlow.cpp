#include "UI/Flow/DemoSettingsFlow.h"
#include "Settings/DemoGameUserSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "Debug/DemoLog.h"
void UDemoSettingsFlow::Begin()
{
    DEMO_LOG_CALL();
    UDemoGameUserSettings* Settings = Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // Engine拥有的本地设置。
    if (!Settings) { UE_LOG(LogFPSDemo, Error, TEXT("DemoGameUserSettings class missing in config")); return; }
    Resolutions = { FIntPoint(1024,768), FIntPoint(1280,720), FIntPoint(1600,900), FIntPoint(1920,1080), FIntPoint(2560,1440), FIntPoint(3840,2160) };
    Resolutions.AddUnique(Settings->GetScreenResolution());
    ResolutionChoice = Resolutions.IndexOfByKey(Settings->GetScreenResolution());
    QualityChoice = Settings->GetOverallScalabilityLevel();
    WindowChoice = static_cast<int32>(Settings->GetFullscreenMode());
    SensitivityChoice = Settings->GetMouseSensitivity();
    bVSyncChoice=Settings->IsVSyncEnabled(); // 打开时重读，取消编辑不改设备设置。
    Message.Empty();
}
void UDemoSettingsFlow::Cycle(int32 Setting, int32 Direction)
{
    DEMO_LOG_CALL();
    if (DisplayConfirmDeadline > 0 || (Direction != 1 && Direction != -1)) { UE_LOG(LogFPSDemo, Warning, TEXT("Setting edit rejected: page/confirmation/direction")); return; }
    if (Setting == 0 && !Resolutions.IsEmpty()) ResolutionChoice = (ResolutionChoice + Direction + Resolutions.Num()) % Resolutions.Num();
    else if (Setting == 1) QualityChoice = (FMath::Max(0, QualityChoice) + Direction + 4) % 4;
    else if (Setting == 2) SensitivityChoice = FMath::Clamp(SensitivityChoice + Direction*.1f, .1f, 3.f);
    else if (Setting == 3) WindowChoice = (WindowChoice + Direction + 3) % 3;
    else if (Setting == 4) bVSyncChoice=!bVSyncChoice; // 两个方向都切换布尔选项。
    else UE_LOG(LogFPSDemo, Warning, TEXT("Setting edit rejected: unknown setting %d"), Setting);
}
FString UDemoSettingsFlow::GetText(int32 Setting) const
{
    DEMO_LOG_TICK();
    if (Setting == 0 && Resolutions.IsValidIndex(ResolutionChoice)) return FString::Printf(TEXT("%d × %d"), Resolutions[ResolutionChoice].X, Resolutions[ResolutionChoice].Y);
    if (Setting == 1) return QualityChoice == 0 ? TEXT("低") : QualityChoice == 1 ? TEXT("中") : QualityChoice == 2 ? TEXT("高") : QualityChoice == 3 ? TEXT("极高") : TEXT("自定义");
    if (Setting == 2) return FString::Printf(TEXT("%.1f ×"), SensitivityChoice);
    if (Setting == 3) return WindowChoice == 0 ? TEXT("全屏") : WindowChoice == 1 ? TEXT("无边框全屏") : TEXT("窗口化");
    if (Setting == 4) return bVSyncChoice?TEXT("开启（减少撕裂）"):TEXT("关闭");
    return TEXT("不可用");
}
void UDemoSettingsFlow::Apply()
{
    DEMO_LOG_CALL();
    if (DisplayConfirmDeadline > 0 || !Resolutions.IsValidIndex(ResolutionChoice)) { UE_LOG(LogFPSDemo, Warning, TEXT("Apply settings rejected: page/pending display/invalid resolution")); return; }
    UDemoGameUserSettings* Settings = Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 单次提交到Engine配置对象。
    if (!Settings) { UE_LOG(LogFPSDemo, Error, TEXT("Apply settings failed: missing configured settings class")); return; }
    PreviousResolution = Settings->GetScreenResolution();
    PreviousWindowMode = static_cast<int32>(Settings->GetFullscreenMode());
    Settings->SetScreenResolution(Resolutions[ResolutionChoice]);
    Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(WindowChoice));
    if (QualityChoice >= 0) Settings->SetOverallScalabilityLevel(QualityChoice);
    Settings->SetMouseSensitivity(SensitivityChoice);
    Settings->SetVSyncEnabled(bVSyncChoice); // 与其他设备项同一次显式应用/持久化，不属于存档进度。
    Settings->ApplySettings(false);
    if (PreviousResolution != Resolutions[ResolutionChoice] || PreviousWindowMode != WindowChoice) DisplayConfirmDeadline = FPlatformTime::Seconds() + 15.0;
    else { Settings->ConfirmVideoMode(); Settings->SaveSettings(); }
    Message = TEXT("设置已应用");
    UE_LOG(LogFPSDemo, Log, TEXT("SETTINGS_APPLIED resolution=%dx%d mode=%d quality=%d sensitivity=%.1f pending=%d"), Resolutions[ResolutionChoice].X, Resolutions[ResolutionChoice].Y, WindowChoice, QualityChoice, SensitivityChoice, DisplayConfirmDeadline > 0);
}
void UDemoSettingsFlow::Confirm(bool bKeep)
{
    DEMO_LOG_CALL();
    if (DisplayConfirmDeadline <= 0) { UE_LOG(LogFPSDemo, Log, TEXT("Display confirmation ignored: no pending change")); return; }
    UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings(); // Engine拥有，仅此调用借用。
    if (!Settings) { UE_LOG(LogFPSDemo,Warning,TEXT("SETTINGS_CONFIRM missing Engine settings")); return; }
    DisplayConfirmDeadline = 0;
    if (!bKeep) { Settings->SetScreenResolution(PreviousResolution); Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(PreviousWindowMode)); Settings->ApplySettings(false); }
    Settings->ConfirmVideoMode(); Settings->SaveSettings();
    Begin();
    Message = bKeep ? TEXT("显示模式已保存") : TEXT("显示模式已恢复");
    UE_LOG(LogFPSDemo, Log, TEXT("DISPLAY_CONFIRM keep=%d"), bKeep);
}
float UDemoSettingsFlow::GetRemainingSeconds() const
{ DEMO_LOG_TICK(); return DisplayConfirmDeadline > 0 ? FMath::Max(.001f, static_cast<float>(DisplayConfirmDeadline-FPlatformTime::Seconds())) : 0.f; }

bool UDemoSettingsFlow::HasPendingDisplay() const { DEMO_LOG_TICK(); return DisplayConfirmDeadline>0; }
bool UDemoSettingsFlow::HasExpired() const { DEMO_LOG_TICK(); return DisplayConfirmDeadline>0&&FPlatformTime::Seconds()>=DisplayConfirmDeadline; }
const FString& UDemoSettingsFlow::GetMessage() const { DEMO_LOG_TICK(); return Message; }

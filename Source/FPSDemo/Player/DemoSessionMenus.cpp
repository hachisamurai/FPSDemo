#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Settings/DemoGameUserSettings.h"
#include "Game/FPSDemoGameMode.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"

EDemoMenuPage ADemoPlayerController::GetMenuPage() const { DEMO_LOG_TICK(); return MenuPage; }
bool ADemoPlayerController::HasBlockingOverlay() const { DEMO_LOG_TICK(); return MenuPage != EDemoMenuPage::None; }
bool ADemoPlayerController::IsPauseMenuOpen() const { DEMO_LOG_TICK(); return bPauseMenuActive; }
void ADemoPlayerController::RefreshMenuInput()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前World只读阶段。
    UE_LOG(LogFPSDemo, Log, TEXT("SESSION_MENU page=%d paused=%d underlyingUpgrade=%d underlyingDeparture=%d"), static_cast<int32>(MenuPage), bPauseMenuActive, bMenuOpen, bNextLevelConfirmationOpen);
    SetMenuInput(HasBlockingOverlay() || bMenuOpen || bNextLevelConfirmationOpen || !State || State->Phase == EDemoPhase::Lobby || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat);
}
void ADemoPlayerController::EscapePressed()
{
    DEMO_LOG_CALL();
    if (HasBlockingOverlay()) { CloseOverlay(); return; }
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 大厅不必暂停，局内任意阶段保留下层UI。
    if (!State || State->Phase == EDemoPhase::Lobby) { UE_LOG(LogFPSDemo, Log, TEXT("Pause ignored outside run")); return; }
    if (!SetPause(true)) { UE_LOG(LogFPSDemo, Warning, TEXT("Pause rejected by engine")); return; }
    bPauseMenuActive = true;
    MenuPage = EDemoMenuPage::Pause;
    RefreshMenuInput();
}
void ADemoPlayerController::CloseOverlay()
{
    DEMO_LOG_CALL();
    // Esc只取消退出意图；恢复原页面及暂停所有权，不让保存页穿透到游戏。
    if (MenuPage == EDemoMenuPage::QuitSaving) { CancelQuitSave(); return; }
    if (DisplayConfirmDeadline > 0) { ConfirmDisplaySettings(false); return; }
    if (MenuPage == EDemoMenuPage::Settings) MenuPage = SettingsBackPage;
    else if (MenuPage == EDemoMenuPage::ReturnHub) MenuPage = EDemoMenuPage::Pause;
    else if (MenuPage == EDemoMenuPage::CreateSave) { PendingSaveSlot = INDEX_NONE; MenuPage = EDemoMenuPage::Saves; }
    else
    {
        MenuPage = EDemoMenuPage::None;
        if (bPauseMenuActive) { SetPause(false); bPauseMenuActive = false; }
    }
    RefreshMenuInput();
}
void ADemoPlayerController::SelectSaveSlot(int32 Index)
{
    DEMO_LOG_CALL();
    if (MenuPage != EDemoMenuPage::Saves || Index < 0 || Index > 2) { UE_LOG(LogFPSDemo, Warning, TEXT("SelectSaveSlot rejected: page/index")); return; }
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI持有的文件缓存，不在HUD里读盘。
    if (!Saves->SlotExists(Index)) { PendingSaveSlot = Index; MenuPage = EDemoMenuPage::CreateSave; RefreshMenuInput(); return; }
    if (Saves->SelectSlot(Index)) GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->TravelToRun(true);
    else MenuMessage = Saves->GetStatus();
}
void ADemoPlayerController::ConfirmCreateSave(bool Confirm)
{
    DEMO_LOG_CALL();
    if (MenuPage != EDemoMenuPage::CreateSave) { UE_LOG(LogFPSDemo, Warning, TEXT("Create confirmation rejected: wrong page")); return; }
    if (!Confirm) { CloseOverlay(); return; }
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 同步保存成功才旅行。
    if (Saves->CreateSlot(PendingSaveSlot)) GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->TravelToRun(true);
    else MenuMessage = Saves->GetStatus();
}
void ADemoPlayerController::OnRunReady()
{
    DEMO_LOG_CALL();
    MenuPage = EDemoMenuPage::None; bPauseMenuActive = false; PendingSaveSlot = INDEX_NONE;
    bMenuOpen = bRewardMenu = bNextLevelConfirmationOpen = false;
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 恢复UI必须基于实际检查点阶段。
    if (State && State->Phase == EDemoPhase::Reward) OpenUpgradeMenu(true);
    else if (State && State->Phase == EDemoPhase::Victory) ShowEndScreen();
    else RefreshMenuInput();
}
void ADemoPlayerController::ReturnHubPressed()
{
    DEMO_LOG_CALL();
    if (MenuPage != EDemoMenuPage::Pause) { UE_LOG(LogFPSDemo, Warning, TEXT("Return hub rejected: not pause menu")); return; }
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前阶段决定关闭暂停、通关续玩或放弃挑战，不以角色坐标推断安全区。
    // 已在Hub时只移除Esc覆盖层；复用恢复输入/解除暂停流程，保留下层终端、当前库存及全部成长，避免重置或地图旅行。
    if (State && State->Phase == EDemoPhase::Hub)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("Return hub: already in Hub; closing pause overlay only"));
        CloseOverlay();
        return;
    }
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 非Hub才执行权威成长检测/返回流程；胜利仍走统一续玩入口。
    if (State && State->Phase == EDemoPhase::Victory) { Mode->ContinueAfterVictory(); return; }
    if (Mode->HasRunUpgrades()) { MenuPage = EDemoMenuPage::ReturnHub; RefreshMenuInput(); }
    else if (!Mode->ReturnToSafeHub(false)) MenuMessage = GetGameInstance()->GetSubsystem<UDemoRunSaves>()->GetStatus();
}
void ADemoPlayerController::ConfirmReturnHub(bool Confirm)
{
    DEMO_LOG_CALL();
    if (MenuPage != EDemoMenuPage::ReturnHub) { UE_LOG(LogFPSDemo, Warning, TEXT("Return hub confirmation rejected: wrong page")); return; }
    if (!Confirm) { CloseOverlay(); return; }
    if (!GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()->ReturnToSafeHub(true)) MenuMessage = GetGameInstance()->GetSubsystem<UDemoRunSaves>()->GetStatus();
}
void ADemoPlayerController::ReturnLobbyPressed()
{
    DEMO_LOG_CALL();
    if (MenuPage != EDemoMenuPage::Pause) { UE_LOG(LogFPSDemo, Warning, TEXT("Return lobby rejected: not pause menu")); return; }
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 安全阶段保存当前改动，Combat保留进入前快照。
    if (!Mode->SaveCheckpoint()) { MenuMessage = GetGameInstance()->GetSubsystem<UDemoRunSaves>()->GetStatus(); return; }
    Mode->TravelToRun(false);
}
void ADemoPlayerController::SettingsPressed()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 设置仅从主大厅/暂停入口打开。
    if (!State || (MenuPage != EDemoMenuPage::Pause && (State->Phase != EDemoPhase::Lobby || HasBlockingOverlay()))) { UE_LOG(LogFPSDemo, Warning, TEXT("Settings rejected: wrong page/phase")); return; }
    SettingsBackPage = MenuPage;
    BeginSettings();
    MenuPage = EDemoMenuPage::Settings;
    RefreshMenuInput();
}
bool ADemoPlayerController::IsSettingsOpen() const { DEMO_LOG_TICK(); return MenuPage == EDemoMenuPage::Settings; }
void ADemoPlayerController::BeginSettings()
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
    MenuMessage.Empty();
}
void ADemoPlayerController::CycleSetting(int32 Setting, int32 Direction)
{
    DEMO_LOG_CALL();
    if (!IsSettingsOpen() || DisplayConfirmDeadline > 0 || (Direction != 1 && Direction != -1)) { UE_LOG(LogFPSDemo, Warning, TEXT("Setting edit rejected: page/confirmation/direction")); return; }
    if (Setting == 0 && !Resolutions.IsEmpty()) ResolutionChoice = (ResolutionChoice + Direction + Resolutions.Num()) % Resolutions.Num();
    else if (Setting == 1) QualityChoice = (FMath::Max(0, QualityChoice) + Direction + 4) % 4;
    else if (Setting == 2) SensitivityChoice = FMath::Clamp(SensitivityChoice + Direction*.1f, .1f, 3.f);
    else if (Setting == 3) WindowChoice = (WindowChoice + Direction + 3) % 3;
    else UE_LOG(LogFPSDemo, Warning, TEXT("Setting edit rejected: unknown setting %d"), Setting);
}
FString ADemoPlayerController::GetSettingText(int32 Setting) const
{
    DEMO_LOG_TICK();
    if (Setting == 0 && Resolutions.IsValidIndex(ResolutionChoice)) return FString::Printf(TEXT("%d × %d"), Resolutions[ResolutionChoice].X, Resolutions[ResolutionChoice].Y);
    if (Setting == 1) return QualityChoice == 0 ? TEXT("低") : QualityChoice == 1 ? TEXT("中") : QualityChoice == 2 ? TEXT("高") : QualityChoice == 3 ? TEXT("极高") : TEXT("自定义");
    if (Setting == 2) return FString::Printf(TEXT("%.1f ×"), SensitivityChoice);
    if (Setting == 3) return WindowChoice == 0 ? TEXT("全屏") : WindowChoice == 1 ? TEXT("无边框全屏") : TEXT("窗口化");
    return TEXT("不可用");
}
void ADemoPlayerController::ApplyUserSettings()
{
    DEMO_LOG_CALL();
    if (!IsSettingsOpen() || DisplayConfirmDeadline > 0 || !Resolutions.IsValidIndex(ResolutionChoice)) { UE_LOG(LogFPSDemo, Warning, TEXT("Apply settings rejected: page/pending display/invalid resolution")); return; }
    UDemoGameUserSettings* Settings = Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 单次提交到Engine配置对象。
    if (!Settings) { UE_LOG(LogFPSDemo, Error, TEXT("Apply settings failed: missing configured settings class")); return; }
    PreviousResolution = Settings->GetScreenResolution();
    PreviousWindowMode = static_cast<int32>(Settings->GetFullscreenMode());
    Settings->SetScreenResolution(Resolutions[ResolutionChoice]);
    Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(WindowChoice));
    if (QualityChoice >= 0) Settings->SetOverallScalabilityLevel(QualityChoice);
    Settings->SetMouseSensitivity(SensitivityChoice);
    Settings->ApplySettings(false);
    if (PreviousResolution != Resolutions[ResolutionChoice] || PreviousWindowMode != WindowChoice) DisplayConfirmDeadline = FPlatformTime::Seconds() + 15.0;
    else { Settings->ConfirmVideoMode(); Settings->SaveSettings(); }
    MenuMessage = TEXT("设置已应用");
    UE_LOG(LogFPSDemo, Log, TEXT("SETTINGS_APPLIED resolution=%dx%d mode=%d quality=%d sensitivity=%.1f pending=%d"), Resolutions[ResolutionChoice].X, Resolutions[ResolutionChoice].Y, WindowChoice, QualityChoice, SensitivityChoice, DisplayConfirmDeadline > 0);
}
void ADemoPlayerController::ConfirmDisplaySettings(bool Confirm)
{
    DEMO_LOG_CALL();
    if (DisplayConfirmDeadline <= 0) { UE_LOG(LogFPSDemo, Log, TEXT("Display confirmation ignored: no pending change")); return; }
    DisplayConfirmDeadline = 0;
    UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings(); // 显示回退只影响显示，已应用画质/鼠标仍保留。
    if (!Confirm) { Settings->SetScreenResolution(PreviousResolution); Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(PreviousWindowMode)); Settings->ApplySettings(false); }
    Settings->ConfirmVideoMode(); Settings->SaveSettings();
    BeginSettings();
    MenuMessage = Confirm ? TEXT("显示模式已保存") : TEXT("显示模式已恢复");
    UE_LOG(LogFPSDemo, Log, TEXT("DISPLAY_CONFIRM keep=%d"), Confirm);
}
float ADemoPlayerController::GetDisplayConfirmSeconds() const { DEMO_LOG_TICK(); return DisplayConfirmDeadline > 0 ? FMath::Max(.001f, static_cast<float>(DisplayConfirmDeadline-FPlatformTime::Seconds())) : 0.f; }
void ADemoPlayerController::PlayerTick(float DeltaTime)
{
    DEMO_LOG_TICK();
    Super::PlayerTick(DeltaTime);
    UpdateQuitSave(); // bTickEvenWhenPaused保证退出窗口等待期间仍能推进HTTP结果与真实时间。
    if (DisplayConfirmDeadline > 0 && FPlatformTime::Seconds() >= DisplayConfirmDeadline) ConfirmDisplaySettings(false);
}

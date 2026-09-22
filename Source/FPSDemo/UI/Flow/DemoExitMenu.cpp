#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/InputComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interaction/DemoInteractable.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Player/DemoCloudSync.h"
#include "Player/DemoPlayerProfile.h"
#include "Save/DemoRunSave.h"
#include "Settings/DemoGameUserSettings.h"
#include "UI/DemoHUD.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/DemoWeaponComponent.h"
EDemoQuitState UDemoMenuFlowComponent::GetQuitState() const
{ DEMO_LOG_TICK(); return QuitState; }
bool UDemoMenuFlowComponent::IsQuitLocalSaved() const
{ DEMO_LOG_TICK(); return bQuitLocalSaved; }
void UDemoMenuFlowComponent::QuitPressed()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 借用当前阶段，只接受大厅或暂停按钮。
    if (!State || (MenuPage != EDemoMenuPage::Pause && (State->Phase != EDemoPhase::Lobby || HasBlockingOverlay())))
    { UE_LOG(LogFPSDemo, Warning, TEXT("Quit rejected: wrong page/phase")); return; }
    // 从Esc页进入时World已暂停；重复SetPause会返回false，不能把已有暂停误判为失败。
    if (!GetWorld()->IsPaused() && !GetController()->SetPause(true)) { MenuMessage = TEXT("暂停失败，暂时无法安全退出"); UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE pause failed")); return; }
    QuitBackPage = MenuPage;
    MenuPage = EDemoMenuPage::QuitSaving;
    bPauseMenuActive = true;
    bQuitLocalSaved = false;
    QuitState = EDemoQuitState::SavingLocal;
    QuitNextActionTime = FPlatformTime::Seconds() + .15;
    MenuMessage = TEXT("正在保存本地检查点与永久进度……");
    RefreshMenuInput();
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE started"));
}
bool UDemoMenuFlowComponent::SaveBeforeQuit()
{
    DEMO_LOG_CALL();
    UDemoPlayerProfile* Profile = GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // GI持有永久档案，退出前显式写盘，不依赖Deinitialize。
    AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 单人权威负责捕获可恢复检查点。
    bQuitLocalSaved = false;
    if (!Profile || !Mode) MenuMessage = TEXT("保存系统尚未就绪，请取消后重试");
    else if (!Profile->SaveProfile()) MenuMessage = Profile->GetStatus();
    else if (!Mode->SaveCheckpoint()) MenuMessage = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>()->GetStatus();
    else bQuitLocalSaved = true;
    if (!bQuitLocalSaved)
    {
        QuitState = EDemoQuitState::Failed;
        UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE local failed: %s"), *MenuMessage);
    }
    else UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE local complete"));
    return bQuitLocalSaved;
}
void UDemoMenuFlowComponent::UpdateQuitSave()
{
    DEMO_LOG_TICK();
    if (MenuPage != EDemoMenuPage::QuitSaving) return;
    if (QuitState == EDemoQuitState::SavingLocal && FPlatformTime::Seconds() >= QuitNextActionTime)
    {
        if (!SaveBeforeQuit()) return;
        UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>(); // Editor永远为空，不创建HTTP或设置联网豁免。
        if (Cloud) { Cloud->BeginExitSync(); QuitState = EDemoQuitState::WaitingCloud; }
        else
        {
            QuitState = EDemoQuitState::Saved;
            MenuMessage = TEXT("本地保存完成，即将退出");
#if WITH_EDITOR
            MenuMessage = TEXT("试玩检查点已保存；结束试玩将清空本次数据");
#endif
            QuitNextActionTime = FPlatformTime::Seconds() + 1.0;
        }
    }
    if (QuitState == EDemoQuitState::WaitingCloud)
    {
        UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>(); // GI比控制器长寿；仅此页轮询，回调从不捕获控制器。
        const EDemoCloudExitState Result = Cloud ? Cloud->PollExitSync() : EDemoCloudExitState::Failed; // 子系统意外消失不能当作云端成功。
        MenuMessage = Cloud ? Cloud->GetStatus() : TEXT("云同步系统不可用，本地进度已保留");
        if (Result == EDemoCloudExitState::Complete || Result == EDemoCloudExitState::LocalOnly)
        {
            QuitState = EDemoQuitState::Saved;
            MenuMessage = Result == EDemoCloudExitState::Complete ? TEXT("本地与云端均已保存，即将退出") : TEXT("本地保存完成（云同步已关闭），即将退出");
            QuitNextActionTime = FPlatformTime::Seconds() + 1.0;
            UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE ready cloudResult=%d"), static_cast<int32>(Result));
        }
        else if (Result == EDemoCloudExitState::Failed || Result == EDemoCloudExitState::Idle)
        {
            QuitState = EDemoQuitState::Failed;
            UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE cloud failed: %s"), *MenuMessage);
        }
    }
    if (QuitState == EDemoQuitState::Saved && FPlatformTime::Seconds() >= QuitNextActionTime) FinishQuit();
}
void UDemoMenuFlowComponent::RetryQuitSave()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::QuitSaving || QuitState != EDemoQuitState::Failed)
    { UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE retry rejected: page/state")); return; }
    QuitState = EDemoQuitState::SavingLocal;
    bQuitLocalSaved = false;
    QuitNextActionTime = FPlatformTime::Seconds() + .15;
    MenuMessage = TEXT("正在重新保存本地进度……");
}
void UDemoMenuFlowComponent::QuitWithLocalSave()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::QuitSaving || QuitState != EDemoQuitState::Failed || !bQuitLocalSaved)
    { UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE local-only rejected: page/state/local failure")); return; }
    if (UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->CancelExitSync(); // 保留可能已提交但未收到回执的同一请求。
    if (!SaveBeforeQuit()) return;
    QuitState = EDemoQuitState::Saved;
    QuitNextActionTime = FPlatformTime::Seconds() + 1.0;
    MenuMessage = TEXT("本地保存完成；下次联网时继续同步，即将退出");
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE explicit local-only exit"));
}
void UDemoMenuFlowComponent::CancelQuitSave()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::QuitSaving) { UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE cancel ignored: wrong page")); return; }
    if (UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->CancelExitSync(); // 弱UObject HTTP仍由GI管理，不依赖页面生命周期。
    QuitState = EDemoQuitState::Idle;
    MenuPage = QuitBackPage;
    bPauseMenuActive = MenuPage == EDemoMenuPage::Pause;
    if (!bPauseMenuActive) GetController()->SetPause(false);
    MenuMessage = TEXT("已取消退出");
    RefreshMenuInput();
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE cancelled"));
}
void UDemoMenuFlowComponent::FinishQuit()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::QuitSaving || QuitState != EDemoQuitState::Saved)
    { UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE finish rejected: not saved")); return; }
    QuitState = EDemoQuitState::Idle; // 即使平台忽略Quit也不能每帧重复请求。
    GetController()->SetPause(false);
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE quit requested after save feedback"));
    // 标准QuitGame在PIE仅停止试玩，Standalone结束当前进程，保护编辑器资产。
    UKismetSystemLibrary::QuitGame(this, GetController(), EQuitPreference::Quit, false);
}

#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Flow/DemoExitSaveFlow.h"
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
{ DEMO_LOG_TICK(); return ExitFlow->GetState(); }
bool UDemoMenuFlowComponent::IsQuitLocalSaved() const
{ DEMO_LOG_TICK(); return ExitFlow->IsLocalSaved(); }
void UDemoMenuFlowComponent::QuitPressed()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 借用当前阶段，只接受大厅或暂停按钮。
    const bool bFailedConnection=GetWorld()->GetNetMode()==NM_Client && !GetController()->IsCoopRegistered(); // GS晚到/握手失败仍可通过本地保存界面退出。
    if ((!State && !bFailedConnection) || (MenuPage != EDemoMenuPage::Pause && (!State || State->Phase != EDemoPhase::Lobby || HasBlockingOverlay())))
    { UE_LOG(LogFPSDemo, Warning, TEXT("Quit rejected: wrong page/phase")); return; }
    // 从Esc页进入时World已暂停；重复SetPause会返回false，不能把已有暂停误判为失败。
    if (!GetWorld()->IsPaused() && !GetController()->SetLocalMenuPaused(true)) { MenuMessage = TEXT("暂停失败，暂时无法安全退出"); UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE pause failed")); return; }
    QuitBackPage = MenuPage;
    MenuPage = EDemoMenuPage::QuitSaving;
    bPauseMenuActive = true;
    ExitFlow->Begin(); MenuMessage=ExitFlow->GetMessage(); // 子流程管理延迟与保存结果，页面仍由本协调器持有。
    RefreshMenuInput();
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE started"));
}
bool UDemoMenuFlowComponent::SaveBeforeQuit()
{
    DEMO_LOG_CALL();
    const bool bLocalSaved=GetController()->SaveOwnedProgressBeforeExit(MenuMessage); // PC区分房主检查点与客户端本机档案，UI只接收保存结果。
    ExitFlow->CompleteLocal(bLocalSaved,MenuMessage);
    if (!bLocalSaved)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE local failed: %s"), *MenuMessage);
    }
    else UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE local complete"));
    return ExitFlow->IsLocalSaved();
}
void UDemoMenuFlowComponent::UpdateQuitSave()
{
    DEMO_LOG_TICK();
    if (MenuPage != EDemoMenuPage::QuitSaving) return;
    if (ExitFlow->IsLocalDue())
    {
        if (!SaveBeforeQuit()) return;
        UDemoCloudSync* Cloud=GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>(); // 本机GI拥有；Editor永不创建。
        if(Cloud){ Cloud->BeginExitSync(); ExitFlow->WaitForCloud(); }
        else
        {
#if WITH_EDITOR
            ExitFlow->MarkSaved(TEXT("试玩检查点已保存；结束试玩将清空本次数据"));
#else
            ExitFlow->MarkSaved(TEXT("本地保存完成，即将退出"));
#endif
        }
    }
    if(ExitFlow->GetState()==EDemoQuitState::WaitingCloud)
    {
        UDemoCloudSync* Cloud=GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>(); // 只在当前退出意图仍存在时读取屏障。
        ExitFlow->ObserveCloud(Cloud?Cloud->PollExitSync():EDemoCloudExitState::Failed,Cloud?Cloud->GetStatus():TEXT("云同步系统不可用，本地进度已保留"));
    }
    MenuMessage=ExitFlow->GetMessage();
    if(ExitFlow->CanFinish())FinishQuit();
}
void UDemoMenuFlowComponent::RetryQuitSave()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::QuitSaving || ExitFlow->GetState() != EDemoQuitState::Failed)
    { UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE retry rejected: page/state")); return; }
    ExitFlow->Begin(true); MenuMessage=ExitFlow->GetMessage();
}
void UDemoMenuFlowComponent::QuitWithLocalSave()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::QuitSaving || ExitFlow->GetState() != EDemoQuitState::Failed || !ExitFlow->IsLocalSaved())
    { UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE local-only rejected: page/state/local failure")); return; }
    if (UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->CancelExitSync(); // 保留可能已提交但未收到回执的同一请求。
    if (!SaveBeforeQuit()) return;
    ExitFlow->MarkSaved(TEXT("本地保存完成；下次联网时继续同步，即将退出")); MenuMessage=ExitFlow->GetMessage();
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE explicit local-only exit"));
}
void UDemoMenuFlowComponent::CancelQuitSave()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::QuitSaving) { UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE cancel ignored: wrong page")); return; }
    if (UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->CancelExitSync(); // 弱UObject HTTP仍由GI管理，不依赖页面生命周期。
    ExitFlow->Cancel();
    MenuPage = QuitBackPage;
    bPauseMenuActive = MenuPage == EDemoMenuPage::Pause;
    if (!bPauseMenuActive) GetController()->SetLocalMenuPaused(false);
    MenuMessage = TEXT("已取消退出");
    RefreshMenuInput();
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE cancelled"));
}
void UDemoMenuFlowComponent::FinishQuit()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::QuitSaving || ExitFlow->GetState() != EDemoQuitState::Saved)
    { UE_LOG(LogFPSDemo, Warning, TEXT("EXIT_SAVE finish rejected: not saved")); return; }
    ExitFlow->Cancel(); // 即使平台忽略Quit也不能每帧重复请求。
    GetController()->SetLocalMenuPaused(false);
    UE_LOG(LogFPSDemo, Log, TEXT("EXIT_SAVE quit requested after save feedback"));
    // 标准QuitGame在PIE仅停止试玩，Standalone结束当前进程，保护编辑器资产。
    UKismetSystemLibrary::QuitGame(this, GetController(), EQuitPreference::Quit, false);
}

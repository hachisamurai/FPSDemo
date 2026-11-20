// 职责：大厅/暂停/难度等本地页面意图；多人档位与出发分离，权威规则经PC命令执行。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Flow/DemoSettingsFlow.h"
#include "Player/Coop/DemoRoomSubsystem.h"
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
void UDemoMenuFlowComponent::ShowLobby()
{
	DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    MenuTerminal.Reset(); MenuTerminalGeneration = 0; NextTerminalGeneration = 0;
    ResetGamblePresentation(); // 返回大厅不重播已保存的抽取，也不接受迟到展示回执。
	// 大厅切换清理本地待确认引用；不携带上一局入口或进度。
	TerminalPage = EDemoTerminalPage::Stats; PendingAmmoPrice=INDEX_NONE; PendingGamblePrice=INDEX_NONE; // 页面变化撤销旧赌博确认。 // 新大厅不携带武器页或详情窗口状态。
	InspectedWeapon = INDEX_NONE;
	bNextLevelConfirmationOpen = false;
	PendingNextLevelTerminal.Reset();
	PendingCompletedLevel = INDEX_NONE;
	bMenuOpen = false;
	bRewardMenu = false;
	MenuPage = EDemoMenuPage::None;
	bPauseMenuActive = false;
	MenuMessage.Empty();
	// Pawn 仍供 ASC 初始化使用；大厅背景图覆盖场景、菜单半透明且停用重力，等待 StartRun 放到安全区。
	if (ADemoCharacter* DemoPawn = Cast<ADemoCharacter>(GetController()->GetPawn()))
	{
		DemoPawn->StopFiring();
        const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 远端加入时可能已经是Hub，不能在客户端把复制角色永久改成MOVE_None。
        if (DemoPawn->HasAuthority() && State && State->Phase==EDemoPhase::Lobby) DemoPawn->GetCharacterMovement()->DisableMovement();
	}
	SetMenuInput(true);
}
void UDemoMenuFlowComponent::StartGamePressed()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 只显示选择页，不直接开始World。
    if (!State || State->Phase != EDemoPhase::Lobby || HasBlockingOverlay() || !GetController()->HasAuthority()) { UE_LOG(LogFPSDemo, Log, TEXT("Start menu rejected: phase/overlay")); return; }
    GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>()->RefreshSlots();
    MenuPage = EDemoMenuPage::Saves;
    bChoosingHostSave = false; // 单机开始入口不能继承取消过的开房意图。
    MenuMessage.Empty();
    RefreshMenuInput();
}
void UDemoMenuFlowComponent::DifficultyPressed(EDemoDifficulty Difficulty)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 难度只来自安全区出发弹窗。
    if (!State || State->Phase != EDemoPhase::Hub || !bNextLevelConfirmationOpen || HasBlockingOverlay()) { UE_LOG(LogFPSDemo, Log, TEXT("Difficulty input rejected: phase/departure/overlay")); return; }
    if(!GetController()->GetMenuSnapshot().bCanLeadSession) { MenuMessage=TEXT("只有房主可以选择难度"); return; }
    FDemoGameplayCommandRequest Request; // 多人先设置档位，随后全员明确准备；单人仍点击难度直接出发。
    Request.Command=State->TeamReady.bRequired?EDemoGameplayCommand::SelectDeparture:EDemoGameplayCommand::StartLevel; Request.Difficulty=Difficulty;
    Request.ReadyRevision=State->TeamReady.Revision; Request.ExpectedLevel=State->LevelNumber;
    Request.Terminal=PendingNextLevelTerminal.Get(); Request.TerminalGeneration=NextTerminalGeneration;
    GetController()->SubmitGameplayCommand(Request);
}
void UDemoMenuFlowComponent::EndlessPressed()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 只接受安全区当前终端弹窗。
    if (!State || State->Phase!=EDemoPhase::Hub || !bNextLevelConfirmationOpen || HasBlockingOverlay())
    { UE_LOG(LogFPSDemo,Warning,TEXT("Endless input rejected: phase/menu")); return; }
    if(!GetController()->GetMenuSnapshot().bCanLeadSession) { MenuMessage=TEXT("只有房主可以选择模式"); return; }
    FDemoGameplayCommandRequest Request; // 无尽解锁由本人服务器PS复核，多人只改档位，不立即出发。
    Request.Command=State->TeamReady.bRequired?EDemoGameplayCommand::SelectDeparture:EDemoGameplayCommand::StartLevel; Request.bEndless=true;
    Request.ReadyRevision=State->TeamReady.Revision; Request.ExpectedLevel=State->LevelNumber;
    Request.Terminal=PendingNextLevelTerminal.Get(); Request.TerminalGeneration=NextTerminalGeneration;
    GetController()->SubmitGameplayCommand(Request);
}
void UDemoMenuFlowComponent::ShowEndlessUnlockTip()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 防止其他难度或半场战斗显示虚假解锁。
    if (!State || State->Phase!=EDemoPhase::Victory || State->Difficulty!=EDemoDifficulty::Hell)
    { UE_LOG(LogFPSDemo,Warning,TEXT("Endless tip rejected outside Hell victory")); return; }
    MenuPage=EDemoMenuPage::EndlessUnlock;
    MenuMessage.Empty(); RefreshMenuInput();
}
EDemoMenuPage UDemoMenuFlowComponent::GetMenuPage() const
{ DEMO_LOG_TICK(); return MenuPage; }
bool UDemoMenuFlowComponent::HasBlockingOverlay() const
{ DEMO_LOG_TICK(); return MenuPage != EDemoMenuPage::None; }
bool UDemoMenuFlowComponent::IsPauseMenuOpen() const
{ DEMO_LOG_TICK(); return bPauseMenuActive; }
void UDemoMenuFlowComponent::RefreshMenuInput()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前World只读阶段。
    UE_LOG(LogFPSDemo, Log, TEXT("SESSION_MENU page=%d paused=%d underlyingUpgrade=%d underlyingDeparture=%d"), static_cast<int32>(MenuPage), bPauseMenuActive, bMenuOpen, bNextLevelConfirmationOpen);
    SetMenuInput(HasBlockingOverlay() || bMenuOpen || bNextLevelConfirmationOpen || !State || State->Phase == EDemoPhase::Lobby || State->Phase == EDemoPhase::Victory || State->Phase == EDemoPhase::Defeat);
}
void UDemoMenuFlowComponent::EscapePressed()
{
    DEMO_LOG_CALL();
    if(IsCasinoBetInputOpen()) { CloseCasinoBetInput(); return; } // 编辑金额时Esc仅取消本次输入。
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (HasBlockingOverlay()) { CloseOverlay(); return; }
    if(bNextLevelConfirmationOpen) CancelNextLevelConfirmation(); // 打开暂停前可靠地结束本人终端会话，撤销Ready；返回后需重新与终端交互。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 大厅不必暂停，局内任意阶段保留下层UI。
    const bool bUnregisteredClient=GetWorld()->GetNetMode()==NM_Client && !GetController()->IsCoopRegistered(); // 失败连接即使没有GS也必须可重新打开离开菜单。
    if ((!State || State->Phase == EDemoPhase::Lobby) && !bUnregisteredClient) { UE_LOG(LogFPSDemo, Log, TEXT("Pause ignored outside run")); return; }
    if (!GetController()->SetLocalMenuPaused(true)) { UE_LOG(LogFPSDemo, Warning, TEXT("Pause rejected by engine")); return; }
    bPauseMenuActive = true;
    MenuPage = EDemoMenuPage::Pause;
    RefreshMenuInput();
}
void UDemoMenuFlowComponent::CloseOverlay()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if(MenuPage==EDemoMenuPage::FirstRunGuide) { CloseFirstRunGuide(); return; } // 明确关闭才写已读；显示/旅行/阶段变化不伪造用户确认。
    // Esc只取消退出意图；恢复原页面及暂停所有权，不让保存页穿透到游戏。
    if (MenuPage == EDemoMenuPage::QuitSaving) { CancelQuitSave(); return; }
    if (bChoosingHostSave && GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->IsBusy())
    { GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->Leave(); return; } // 开房旅行尚未完成时取消必须撤销GI连接意图。
    if (MenuPage == EDemoMenuPage::Multiplayer)
    {
        UDemoRoomSubsystem* Rooms = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // 关闭查询时撤销委托；连接中明确离开，避免隐藏窗口后迟到旅行。
        if (Rooms->IsBusy() && Rooms->GetState() != EDemoRoomState::Searching) { Rooms->Leave(); return; }
        Rooms->CancelSearch(); MenuPage = EDemoMenuPage::None; RefreshMenuInput(); return;
    }
    if (MenuPage == EDemoMenuPage::Saves && bChoosingHostSave)
    { bChoosingHostSave = false; MenuPage = EDemoMenuPage::Multiplayer; RefreshMenuInput(); return; }
    if (SettingsFlow->HasPendingDisplay()) { ConfirmDisplaySettings(false); return; }
    if (MenuPage == EDemoMenuPage::Settings) MenuPage = SettingsBackPage;
    else if (MenuPage == EDemoMenuPage::ReturnHub) MenuPage = EDemoMenuPage::Pause;
    else if (MenuPage == EDemoMenuPage::CreateSave) { PendingSaveSlot = INDEX_NONE; MenuPage = EDemoMenuPage::Saves; }
    else
    {
        MenuPage = EDemoMenuPage::None;
        if (bPauseMenuActive) { GetController()->SetLocalMenuPaused(false); bPauseMenuActive = false; }
    }
    RefreshMenuInput();
}
void UDemoMenuFlowComponent::SelectSaveSlot(int32 Index)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::Saves || Index < 0 || Index > 2) { UE_LOG(LogFPSDemo, Warning, TEXT("SelectSaveSlot rejected: page/index")); return; }
    UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI持有的文件缓存，不在HUD里读盘。
    if (!Saves->SlotExists(Index)) { PendingSaveSlot = Index; MenuPage = EDemoMenuPage::CreateSave; RefreshMenuInput(); return; }
    if (bChoosingHostSave)
    {
        UDemoRoomSubsystem* Rooms = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // 开房只接受安全区槽，失败仍停留选槽界面。
        Rooms->HostSlot(Index, false); MenuMessage = Rooms->GetStatus(); return;
    }
    FDemoGameplayCommandRequest Request; // 仅房主在自己的持久槽上执行选择及旅行，UI缓存不承担写盘。
    Request.Command=EDemoGameplayCommand::SelectSaveSlot; Request.Choice=Index;
    GetController()->SubmitGameplayCommand(Request);
}
void UDemoMenuFlowComponent::ConfirmCreateSave(bool Confirm)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::CreateSave) { UE_LOG(LogFPSDemo, Warning, TEXT("Create confirmation rejected: wrong page")); return; }
    if (!Confirm) { CloseOverlay(); return; }
    if (bChoosingHostSave)
    {
        UDemoRoomSubsystem* Rooms = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // 用户已明确确认创建空栏，服务先落盘再启动Listen旅行。
        Rooms->HostSlot(PendingSaveSlot, true); MenuMessage = Rooms->GetStatus(); return;
    }
    FDemoGameplayCommandRequest Request; // 主机创建槽并持久化成功后才旅行；远端客户端不能选择房主磁盘槽。
    Request.Command=EDemoGameplayCommand::CreateSave; Request.Choice=PendingSaveSlot;
    GetController()->SubmitGameplayCommand(Request);
}
void UDemoMenuFlowComponent::OnRunReady()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (bPauseMenuActive) GetController()->SetLocalMenuPaused(false); // 同World返回安全区也释放Standalone暂停；联机只是本机页面清理。
    MenuTerminal.Reset(); MenuTerminalGeneration = 0; NextTerminalGeneration = 0;
    MenuPage = EDemoMenuPage::None; bPauseMenuActive = false; PendingSaveSlot = INDEX_NONE;
    bMenuOpen = bRewardMenu = bNextLevelConfirmationOpen = false;
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 恢复UI必须基于实际检查点阶段。
    if (State && State->Phase == EDemoPhase::Reward) OpenUpgradeMenu(true);
    else if (State && State->Phase == EDemoPhase::Victory) ShowEndScreen();
    else RefreshMenuInput();
}
void UDemoMenuFlowComponent::ReturnHubPressed()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::Pause) { UE_LOG(LogFPSDemo, Warning, TEXT("Return hub rejected: not pause menu")); return; }
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 当前阶段决定关闭暂停、通关续玩或放弃挑战，不以角色坐标推断安全区。
    // 已在Hub时只移除Esc覆盖层；复用恢复输入/解除暂停流程，保留下层终端、当前库存及全部成长，避免重置或地图旅行。
    if (State && State->Phase == EDemoPhase::Hub)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("Return hub: already in Hub; closing pause overlay only"));
        CloseOverlay();
        return;
    }
    const FDemoPlayerMenuSnapshot& View=GetController()->GetMenuSnapshot(); // 房主权限和临时成长提示来自服务器快照。
    if (!View.bCanLeadSession) { MenuMessage=TEXT("只有房主可以带全队返回安全区"); return; }
    if (View.bHasRunUpgrades && State && State->Phase!=EDemoPhase::Victory) { MenuPage=EDemoMenuPage::ReturnHub; RefreshMenuInput(); return; }
    FDemoGameplayCommandRequest Request; // 最终状态、全队成长及确认需求仍由服务器复核。
    Request.Command=EDemoGameplayCommand::ReturnHub; GetController()->SubmitGameplayCommand(Request);

}
void UDemoMenuFlowComponent::ConfirmReturnHub(bool Confirm)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::ReturnHub) { UE_LOG(LogFPSDemo, Warning, TEXT("Return hub confirmation rejected: wrong page")); return; }
    if (!Confirm) { CloseOverlay(); return; }
    FDemoGameplayCommandRequest Request; // 确认仅表达清空临时成长意图，不提供客户端计算结果。
    Request.Command=EDemoGameplayCommand::ReturnHub; Request.bConfirmed=true; GetController()->SubmitGameplayCommand(Request);
}
void UDemoMenuFlowComponent::ReturnLobbyPressed()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    if (MenuPage != EDemoMenuPage::Pause) { UE_LOG(LogFPSDemo, Warning, TEXT("Return lobby rejected: not pause menu")); return; }
    if (GetWorld()->GetNetMode()==NM_Client) { GetController()->LeaveCoopToLobby(); return; } // 远端退出只断开本人，房主战局继续。
    FDemoGameplayCommandRequest Request; // 主机保存全队检查点成功后执行全队旅行。
    Request.Command=EDemoGameplayCommand::ReturnLobby; GetController()->SubmitGameplayCommand(Request);

}
void UDemoMenuFlowComponent::SettingsPressed()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) { UE_LOG(LogFPSDemo, Log, TEXT("MENU command rejected after shutdown/nonlocal")); return; } // 拒绝旧PC上的迟到输入/回调。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 设置仅从主大厅/暂停入口打开。
    if (!State || (MenuPage != EDemoMenuPage::Pause && (State->Phase != EDemoPhase::Lobby || HasBlockingOverlay()))) { UE_LOG(LogFPSDemo, Warning, TEXT("Settings rejected: wrong page/phase")); return; }
    SettingsBackPage = MenuPage;
    BeginSettings();
    MenuPage = EDemoMenuPage::Settings;
    RefreshMenuInput();
}
bool UDemoMenuFlowComponent::IsSettingsOpen() const
{ DEMO_LOG_TICK(); return MenuPage == EDemoMenuPage::Settings; }
void UDemoMenuFlowComponent::BeginSettings()
{ DEMO_LOG_CALL(); SettingsFlow->Begin(); MenuMessage=SettingsFlow->GetMessage(); }
void UDemoMenuFlowComponent::CycleSetting(int32 Setting,int32 Direction)
{
    DEMO_LOG_CALL();
    if(bStopped||!GetController()->IsLocalController()||!IsSettingsOpen()){UE_LOG(LogFPSDemo,Log,TEXT("SETTINGS_EDIT rejected page/lifecycle"));return;}
    SettingsFlow->Cycle(Setting,Direction);
}
FString UDemoMenuFlowComponent::GetSettingText(int32 Setting) const
{ DEMO_LOG_TICK(); return SettingsFlow->GetText(Setting); }
void UDemoMenuFlowComponent::ApplyUserSettings()
{
    DEMO_LOG_CALL();
    if(bStopped||!GetController()->IsLocalController()||!IsSettingsOpen()){UE_LOG(LogFPSDemo,Log,TEXT("SETTINGS_APPLY rejected page/lifecycle"));return;}
    SettingsFlow->Apply(); MenuMessage=SettingsFlow->GetMessage();
}
void UDemoMenuFlowComponent::ConfirmDisplaySettings(bool Confirm)
{
    DEMO_LOG_CALL();
    if(bStopped||!GetController()->IsLocalController())return;
    SettingsFlow->Confirm(Confirm); MenuMessage=SettingsFlow->GetMessage();
}
float UDemoMenuFlowComponent::GetDisplayConfirmSeconds() const
{ DEMO_LOG_TICK(); return SettingsFlow->GetRemainingSeconds(); }

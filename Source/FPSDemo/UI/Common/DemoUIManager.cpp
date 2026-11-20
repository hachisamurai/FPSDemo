// 职责：CommonUI栈投影、受控意图及Canvas输入交接。使用：本地PC每帧同步，专服/远端不创建界面。
#include "UI/Common/DemoUIManager.h"
#include "UI/Common/DemoUIRoot.h"
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Screens/DemoCasinoScreen.h"
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Screens/DemoConfirmScreen.h"
#include "UI/Screens/DemoLobbyScreen.h"
#include "UI/Screens/DemoSessionScreen.h"
#include "UI/Screens/DemoSettingsScreen.h"
#include "UI/Screens/DemoPlayerStatusScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIActions.h"
#include "UI/DemoUIPresentation.h" // 设置与按键反馈仅本机，不路由到权威玩法意图。
#include "Settings/DemoGameUserSettings.h" // 受击舒适度只写本机INI，不进入游戏命令、存档或复制。
#include "Player/DemoPlayerController.h"
#include "Game/DemoGameState.h"
#include "Progression/Builds/DemoRunBuildTypes.h" // 核心预览动作数复用可信目录，不另维护七项业务表。
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/UIActionBindingHandle.h"
#include "CommonInputModeTypes.h"
#include "Engine/LocalPlayer.h"
#include "Debug/DemoLog.h"

UDemoUIManager::UDemoUIManager()
{
    DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false;
    RootClass=UDemoUIRoot::StaticClass(); TerminalClass=UDemoTerminalScreen::StaticClass(); CasinoClass=UDemoCasinoScreen::StaticClass(); ModalClass=UDemoConfirmScreen::StaticClass();
    LobbyClass=UDemoLobbyScreen::StaticClass(); SessionClass=UDemoSessionScreen::StaticClass(); SettingsClass=UDemoSettingsScreen::StaticClass(); StatusClass=UDemoPlayerStatusScreen::StaticClass();
}
void UDemoUIManager::ResolvePageClasses()
{
    DEMO_LOG_CALL();
    LoadedRootClass=RootClass.LoadSynchronous();
    LoadedTerminalClass=TerminalClass.LoadSynchronous();
    LoadedCasinoClass=CasinoClass.LoadSynchronous();
    LoadedModalClass=ModalClass.LoadSynchronous();
    LoadedLobbyClass=LobbyClass.LoadSynchronous(); LoadedSessionClass=SessionClass.LoadSynchronous(); LoadedSettingsClass=SettingsClass.LoadSynchronous(); LoadedStatusClass=StatusClass.LoadSynchronous();
    if(!LoadedLobbyClass||!LoadedLobbyClass->IsChildOf(UDemoLobbyScreen::StaticClass())) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMON_UI invalid lobby class; native fallback")); LoadedLobbyClass=UDemoLobbyScreen::StaticClass(); }
    if(!LoadedSessionClass||!LoadedSessionClass->IsChildOf(UDemoSessionScreen::StaticClass())) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMON_UI invalid session class; native fallback")); LoadedSessionClass=UDemoSessionScreen::StaticClass(); }
    if(!LoadedSettingsClass||!LoadedSettingsClass->IsChildOf(UDemoSettingsScreen::StaticClass())) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMON_UI invalid settings class; native fallback")); LoadedSettingsClass=UDemoSettingsScreen::StaticClass(); }
    if(!LoadedStatusClass||!LoadedStatusClass->IsChildOf(UDemoPlayerStatusScreen::StaticClass())) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMON_UI invalid status class; native fallback")); LoadedStatusClass=UDemoPlayerStatusScreen::StaticClass(); }
    if(!LoadedRootClass) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMON_UI missing root asset; using native layout")); LoadedRootClass=UDemoUIRoot::StaticClass(); }
    if(!LoadedTerminalClass||!LoadedTerminalClass->IsChildOf(UDemoTerminalScreen::StaticClass())) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMON_UI invalid terminal class; using native layout")); LoadedTerminalClass=UDemoTerminalScreen::StaticClass(); }
    if(!LoadedCasinoClass||!LoadedCasinoClass->IsChildOf(UDemoCasinoScreen::StaticClass())) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMON_UI invalid casino class; using native layout")); LoadedCasinoClass=UDemoCasinoScreen::StaticClass(); }
    if(!LoadedModalClass||!LoadedModalClass->IsChildOf(UDemoConfirmScreen::StaticClass())) { UE_LOG(LogFPSDemo,Warning,TEXT("COMMON_UI invalid modal class; using native layout")); LoadedModalClass=UDemoConfirmScreen::StaticClass(); }
}
int32 UDemoUIManager::DesiredModal() const
{
    DEMO_LOG_TICK(); const auto* PC=Cast<ADemoPlayerController>(GetOwner()); const auto* F=PC?PC->GetMenuFlow():nullptr; // 当前PC本地意图。
    if(!F||bStopped) return 0;
    if(F->GetMenuPage()==EDemoMenuPage::CreateSave) return 5;
    if(F->GetMenuPage()==EDemoMenuPage::ReturnHub) return 6;
    if(F->GetMenuPage()==EDemoMenuPage::EndlessUnlock) return 8;
    if(F->GetMenuPage()==EDemoMenuPage::Settings&&F->GetDisplayConfirmSeconds()>0) return 7;
    if(F->HasBlockingOverlay()||!F->IsUpgradeMenuOpen()) return 0;
    if(F->IsCasinoBetInputOpen()) return 2;
    if(F->IsGambleConfirmOpen()) return 1;
    if(F->IsAmmoMenuOpen()&&F->GetPendingAmmoPrice()!=INDEX_NONE) return 3;
    // 原Kind4武器详情页已按用户要求删除；目录/装配不再创建第二层CommonUI模态。
    return 0;
}
bool UDemoUIManager::UsesWidgetMenu() const
{
    DEMO_LOG_TICK(); const auto* PC=Cast<ADemoPlayerController>(GetOwner()); const auto* F=PC?PC->GetMenuFlow():nullptr;
    const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 大厅不需要Avatar就绪。
    return !bStopped&&PC&&PC->IsLocalController()&&F&&(F->HasBlockingOverlay()||(State&&State->Phase==EDemoPhase::Lobby)||F->IsUpgradeMenuOpen()||F->IsNextLevelConfirmationOpen());
}
bool UDemoUIManager::OwnsInput() const { DEMO_LOG_TICK(); return bOwnedInput&&!bStopped; }
void UDemoUIManager::Sync()
{
    DEMO_LOG_TICK(); auto* PC=Cast<ADemoPlayerController>(GetOwner()); // 不缓存跨旅行PC/World。
    if(bStopped||bSyncing||!PC||!PC->IsLocalController()||!PC->GetLocalPlayer()||!PC->GetLocalPlayer()->ViewportClient) return;
    TGuardValue<bool> Guard(bSyncing,true); // 同步构建生命周期回调不得重入栈变更。
    if(!UsesWidgetMenu())
    {
        if(Root) { Root->ModalStack->ClearWidgets(); Root->MenuStack->ClearWidgets(); Root->SetVisibility(ESlateVisibility::Collapsed); ActiveModal=0; }
        if(bOwnedInput) { bOwnedInput=false; RestoreLegacyInput(); }
        return;
    }
    if(!Root)
    {
        ResolvePageClasses();
        Root=CreateWidget<UDemoUIRoot>(PC,LoadedRootClass); // 每LocalPlayer独立，非全局Viewport添加。
        if(!Root||!Root->AddToPlayerScreen(50)) { UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI root creation failed")); Root=nullptr; return; }
        Root->TakeWidget(); // 初始化真实栈，后续可访问具名控件。
        UE_LOG(LogFPSDemo,Log,TEXT("COMMON_UI root created owner=%s"),*PC->GetName());
    }
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible); bOwnedInput=true;
    const bool RefreshNow=FPlatformTime::Seconds()>=NextModelRefresh; // 数据比较不随渲染频率反复导出整个快照。
    if(RefreshNow) NextModelRefresh=FPlatformTime::Seconds()+.1;
    auto* F=PC->GetMenuFlow(); const int32 Modal=DesiredModal(); // 模态只读Flow，不保存第二份费用。
    const auto MenuPage=F->GetMenuPage(); const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // Flow仍是唯一页面选择真值。
    const bool HasMenu=MenuPage!=EDemoMenuPage::EndlessUnlock; // 独立提示只有模态；其余确认保留原父页。
    TSubclassOf<UDemoMenuScreen> PageClass=LoadedSessionClass; // 所有全局覆盖层默认会话页。
    if(MenuPage==EDemoMenuPage::Settings) PageClass=LoadedSettingsClass;
    else if(MenuPage==EDemoMenuPage::PlayerStatus) PageClass=LoadedStatusClass;
    else if(MenuPage==EDemoMenuPage::None)
    {
        if(F->IsUpgradeMenuOpen()||F->IsNextLevelConfirmationOpen()) PageClass=F->IsGambleMenuOpen()&&DemoCasino::ModeForChoice(F->GetInspectedGamble())>=0?LoadedCasinoClass:LoadedTerminalClass;
        else if(State&&State->Phase==EDemoPhase::Lobby) PageClass=LoadedLobbyClass;
    }
    auto* Page=Cast<UDemoMenuScreen>(Root->MenuStack->GetActiveWidget());
    if(!HasMenu) Root->MenuStack->ClearWidgets();
    else
    {
        const bool NewPage=!Page||Page->GetClass()!=PageClass;
        if(NewPage) { Root->MenuStack->ClearWidgets(); Page=Root->MenuStack->AddWidget<UDemoMenuScreen>(PageClass); UE_LOG(LogFPSDemo,Log,TEXT("COMMON_UI menu=%s"),*GetNameSafe(PageClass)); }
        if(Page&&(NewPage||RefreshNow)) { Page->TakeWidget(); Page->RefreshScreen(); }
    }
    Root->MenuStack->SetIsEnabled(Modal==0); // 撤销模态前启用底层，CommonUI才能恢复其有效焦点。
    Root->MenuStack->SetRenderOpacity(Modal==0?1.f:0.f); // 只隐藏下层视觉以免透字；不能切Hidden重排CommonUI输入树，否则复用弹窗可能失去焦点。交互仍由Enabled及当前顶层守卫阻塞。
    if(Modal==0) { Root->ModalStack->ClearWidgets(); Root->ModalStack->SetVisibility(ESlateVisibility::Collapsed); ActiveModal=0; }
    else
    {
        Root->ModalStack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        auto* Dialog=Cast<UDemoConfirmScreen>(Root->ModalStack->GetActiveWidget()); // 只允许项目确认派生类。
        const bool NewDialog=!Dialog||ActiveModal!=Modal;
        if(NewDialog)
        {
            Root->ModalStack->ClearWidgets(); ActiveModal=Modal;
            // 捕获Modal仅本次同步初始化，在栈激活前配置类型，不持跨帧引用。
            Dialog=Root->ModalStack->AddWidget<UDemoConfirmScreen>(LoadedModalClass,[Modal](UDemoConfirmScreen& Widget){ UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonModal.Initialize")); Widget.SetKind(Modal); });
            UE_LOG(LogFPSDemo,Log,TEXT("COMMON_UI modal=%d"),Modal);
        }
        if(Dialog&&(NewDialog||RefreshNow)) { Dialog->TakeWidget(); Dialog->RefreshScreen(); }
    }
    const auto* VisualPage=GetActiveScreen(); // 使用真实栈顶页型，禁止只凭残留Flow标志给其他页套R02。
    Root->SetR02Presentation(VisualPage&&VisualPage->IsUsingR02Presentation());
    Root->SetFeedback(GetFeedbackText(),IsFeedbackWarning()); // 先确定当前变体再画真实回执，切页不沿上一页颜色；无输入或业务副作用。
}
UDemoMenuScreen* UDemoUIManager::GetActiveScreen() const
{ DEMO_LOG_TICK(); if(!Root||!UsesWidgetMenu()) return nullptr; return Cast<UDemoMenuScreen>(Root->ModalStack->GetActiveWidget()?Root->ModalStack->GetActiveWidget():Root->MenuStack->GetActiveWidget()); }
bool UDemoUIManager::IsTopScreen(const UDemoMenuScreen* Screen) const
{ DEMO_LOG_TICK(); return Screen&&Screen==GetActiveScreen()&&Screen->IsActivated(); }
FString UDemoUIManager::GetFeedbackText() const
{
    DEMO_LOG_TICK();
    if(bFeedbackAwaitedPreparation)
        if(const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr)
        {
            const bool bPastPreparation=State->Phase==EDemoPhase::Combat||State->Phase==EDemoPhase::Reward||State->Phase==EDemoPhase::Intermission||State->Phase==EDemoPhase::Victory||State->Phase==EDemoPhase::Defeat; // 快速真实清关可能在下一次菜单刷新前越过Combat；后续阶段同样证明已离开该准备门。
            if(bPastPreparation&&!State->Preparation.bActive) return FString(); // 仅抑制原准备中短回执；Hub/Lobby的取消/失败及其他交易消息仍沿原期限显示。
        }
    return FPlatformTime::Seconds()<FeedbackUntil?FeedbackText:FString();
}
bool UDemoUIManager::IsFeedbackWarning() const
{ DEMO_LOG_TICK(); return bFeedbackWarning; }
void UDemoUIManager::PresentCommandResult(EDemoGameplayCommand Command,const FDemoCommandResult& Result)
{
    DEMO_LOG_CALL();
    auto* PC=Cast<ADemoPlayerController>(GetOwner()); // 当前本机可靠回执拥有者，不接触远端UI。
    if(bStopped||!PC||!PC->IsLocalController()) return;
    const bool bTransaction=Command==EDemoGameplayCommand::UpgradeStat||Command==EDemoGameplayCommand::ChooseReward||Command==EDemoGameplayCommand::EquipWeapon||Command==EDemoGameplayCommand::EquipAmmo||Command==EDemoGameplayCommand::PurchaseAmmo||Command==EDemoGameplayCommand::SelectCore||Command==EDemoGameplayCommand::SetReady; // 娱乐结果由已有揭示界面表达，不在提交时提前播胜利音。
    if(!bTransaction&&Result.Status!=EDemoCommandStatus::Rejected) return;
    if(Result.Message.IsEmpty()) return;
    FeedbackText=Result.Message.Left(160); bFeedbackWarning=Result.Status!=EDemoCommandStatus::Committed;
    const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 只读本机GS公开准备状态，不根据文案猜测结果。
    bFeedbackAwaitedPreparation=Command==EDemoGameplayCommand::StartLevel&&State&&State->Preparation.bActive;
    FeedbackUntil=FPlatformTime::Seconds()+(bFeedbackWarning?5.:3.);
    DemoUI::PlayFeedback(PC,bFeedbackWarning?EDemoUISound::Reject:EDemoUISound::Success); // 只有Committed播成功；AppliedPendingSave仍明确等待。
}
int32 UDemoUIManager::GetMenuStackCount() const { DEMO_LOG_TICK(); return Root?Root->MenuStack->GetNumWidgets():0; }
int32 UDemoUIManager::GetModalStackCount() const { DEMO_LOG_TICK(); return Root?Root->ModalStack->GetNumWidgets():0; }
void UDemoUIManager::SubmitAction(FName Action)
{
    DEMO_LOG_CALL(); auto* PC=Cast<ADemoPlayerController>(GetOwner()); auto* Screen=GetActiveScreen();
    if(!PC||!Screen||!IsTopScreen(Screen)||!Screen->FindAction(Action)) { UE_LOG(LogFPSDemo,Log,TEXT("COMMON_UI submit rejected missing action")); return; }
    auto* F=PC->GetMenuFlow(); NextModelRefresh=0; // 本地操作下帧立即刷新；不在点击回调内销毁Widget。
    const bool bDamagePreference=Action==TEXT("UI.DamageShakeMinus")||Action==TEXT("UI.DamageShakePlus")||Action==TEXT("UI.DamageFlashMinus")||Action==TEXT("UI.DamageFlashPlus")||Action==TEXT("UI.ToggleHurtVoice")||Action==TEXT("UI.ToggleLowHealthAudio"); // 固定白名单，不把任意UI字符串变成设备写入。
    if(bDamagePreference)
    {
        auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 引擎拥有本机设备对象，只在当前合法设置页写入。
        if(!PC->IsLocalController()||!Cast<UDemoSettingsScreen>(Screen)||!Settings) { UE_LOG(LogFPSDemo,Log,TEXT("DAMAGE preference rejected unavailable local settings")); return; }
        if(Action==TEXT("UI.DamageShakeMinus")) Settings->SetDamageShakeScale(Settings->GetDamageShakeScale()-.1f);
        else if(Action==TEXT("UI.DamageShakePlus")) Settings->SetDamageShakeScale(Settings->GetDamageShakeScale()+.1f);
        else if(Action==TEXT("UI.DamageFlashMinus")) Settings->SetDamageFlashScale(Settings->GetDamageFlashScale()-.1f);
        else if(Action==TEXT("UI.DamageFlashPlus")) Settings->SetDamageFlashScale(Settings->GetDamageFlashScale()+.1f);
        else if(Action==TEXT("UI.ToggleHurtVoice")) Settings->SetHurtVoiceEnabled(!Settings->IsHurtVoiceEnabled());
        else Settings->SetLowHealthAudioEnabled(!Settings->IsLowHealthAudioEnabled());
        return; // 下一次模型刷新重建控件；不触碰原五项草稿、显示确认或网络命令。
    }
    if(Action==TEXT("UI.ToggleReducedMotion")||Action==TEXT("UI.ToggleSound"))
    {
        if(!Cast<UDemoSettingsScreen>(Screen)) { UE_LOG(LogFPSDemo,Log,TEXT("UI preference rejected outside settings")); return; }
        if(Action==TEXT("UI.ToggleReducedMotion")) DemoUI::SetReducedMotion(!DemoUI::ReducedMotion());
        else DemoUI::SetUISoundEnabled(!DemoUI::UISoundEnabled());
        return; // Pref已进入Settings RefreshKey；下一次Sync更新文案，不在按钮调用栈销毁控件，不改变原5项草稿。
    }
    if(Action==TEXT("UI.Back")) { Back(); return; }
    if(Cast<UDemoLobbyScreen>(Screen))
        for(int32 SlotIndex=0;SlotIndex<3;++SlotIndex)
            if(Action==FName(*FString::Printf(TEXT("UI.LobbySlot%d"),SlotIndex)))
            {
                const auto* Button=Screen->FindAction(Action); // 只接收当前可操作的真实大厅槽按钮，不接受保护槽旧对象。
                if(!Button||!Button->GetIsEnabled()||!F) { UE_LOG(LogFPSDemo,Log,TEXT("R02 lobby slot rejected disabled/unavailable slot=%d"),SlotIndex); return; }
                const auto* LobbyState=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 拒绝首次点击后、栈尚未同步时的大厅旧按钮，不让原入口拒绝后沿Saves继续落入第二次选择。
                if(!LobbyState||LobbyState->Phase!=EDemoPhase::Lobby||F->HasBlockingOverlay()) { UE_LOG(LogFPSDemo,Log,TEXT("R02 lobby slot rejected stale phase/overlay slot=%d"),SlotIndex); return; }
                F->StartGamePressed(); // 原入口重读缓存并核Lobby/本机/Authority/Overlay，不由UI直接创建/读档。
                if(F->GetMenuPage()!=EDemoMenuPage::Saves||F->IsChoosingHostSave()) { UE_LOG(LogFPSDemo,Log,TEXT("R02 lobby slot rejected original start guard slot=%d"),SlotIndex); return; }
                UE_LOG(LogFPSDemo,Log,TEXT("R02 lobby slot routed original save selection slot=%d"),SlotIndex);
                F->DispatchUIAction(DemoUIActions::Decode(FName(*FString::Printf(TEXT("Save%d"),SlotIndex)))); // 同次原Flow先选页再选槽；空槽仍走Create确认，保护/旅行/云仍由原服务裁决。
                return;
            }
    if(auto* Terminal=Cast<UDemoTerminalScreen>(Screen))
    {
        for(int32 Index=0;Index<DemoRunBuild::CoreCount();++Index)
            if(Action==FName(*FString::Printf(TEXT("UI.CorePreview%d"),Index)))
            {
                if(Terminal->IsUsingR02Presentation()&&(F->IsCoreMenuOpen()||F->IsDepartureCorePickerOpen())) Terminal->NoteCoreInspection(Index); // 顶层真实按钮白名单已验证；核心页或Hub出发页只切本地详情，CoreN仍是唯一选择意图。
                else UE_LOG(LogFPSDemo,Log,TEXT("R02 core preview rejected outside current core page index=%d"),Index);
                return;
            }
        if(Action==TEXT("UI.WeaponEquip"))
        {
            const UDemoMenuButton* Equip=Screen->FindAction(Action); // 当前顶层真实按钮，锁定/手枪/终端拒绝不接受直接调用绕过禁用状态。
            if(!Equip||!Equip->GetIsEnabled()) { UE_LOG(LogFPSDemo,Log,TEXT("Weapon catalogue equip rejected disabled action")); return; }
            const int32 Index=Terminal->GetPresentationWeaponIndex(); // 仅本地目录高亮，实际权限/装备仍由原Flow及服务器验证。
            if(Index<1||Index>=4||!F->IsWeaponMenuOpen()) { UE_LOG(LogFPSDemo,Log,TEXT("Weapon catalogue equip rejected invalid main selection/page index=%d"),Index); return; }
            F->EquipCatalogWeapon(Index); return; // 顶层实际启用按钮已验证；明确动作直接提交原权威装备请求，无InspectWeapon/隐藏模态状态。
        }
        for(int32 Index=0;Index<4;++Index)
            if(Action==FName(*FString::Printf(TEXT("UI.JumpAmmo%d"),Index)))
            {
                if(F->IsWeaponMenuOpen())
                {
                    F->DispatchUIAction(DemoUIActions::Decode(TEXT("TerminalAmmo")));
                    F->DispatchUIAction(DemoUIActions::Decode(FName(*FString::Printf(TEXT("Ammo%d"),Index))));
                }
                return;
            }
        for(int32 Index=0;Index<4;++Index)
            if(Action==FName(*FString::Printf(TEXT("Weapon%d"),Index)))
            {
                Terminal->NoteWeaponInspection(Index);
                UE_LOG(LogFPSDemo,Log,TEXT("Weapon catalogue preview=%d; explicit catalogue equip required"),Index); return; // 所有UMG目录只预览；明确装配按钮才发请求。
            }
    }
    if(Action==TEXT("Casino.Help")) { if(auto* Casino=Cast<UDemoCasinoScreen>(Screen)) Casino->ToggleHelp(); return; }
    const auto Decoded=DemoUIActions::Decode(Action); // 房间按钮必须使用实际显示版本，不借新快照替换旧行。
    if(Decoded>=EDemoUIAction::Room0&&Decoded<=EDemoUIAction::Room4) { if(auto* Session=Cast<UDemoSessionScreen>(Screen)) Session->JoinDisplayedRoom(int32(Decoded)-int32(EDemoUIAction::Room0)); return; }
    if(Action==TEXT("Room.DirectJoin")) { if(auto* Session=Cast<UDemoSessionScreen>(Screen)) Session->SubmitDirectJoin(); return; }
    if(Action==TEXT("Casino.BetAccept")) { if(auto* Dialog=Cast<UDemoConfirmScreen>(Screen)) Dialog->AcceptBet(); return; }
    if(Action==TEXT("Casino.BetCancel")) { F->CloseCasinoBetInput(); return; }
    if(F->HandleCasinoClick(Action)) return;
    F->DispatchUIAction(DemoUIActions::Decode(Action));
}
void UDemoUIManager::Back()
{
    DEMO_LOG_CALL(); auto* PC=Cast<ADemoPlayerController>(GetOwner()); if(!PC||!UsesWidgetMenu()) return;
    auto* F=PC->GetMenuFlow(); NextModelRefresh=0; DemoUI::PlayFeedback(PC,EDemoUISound::Back);
    if(auto* Casino=Cast<UDemoCasinoScreen>(GetActiveScreen())) if(Casino->IsHelpOpen()) { Casino->ToggleHelp(); return; } // 纯说明层先返回本玩法，不丢草稿/未结局面。
    if(F->IsCasinoBetInputOpen()) F->CloseCasinoBetInput();
    else if(F->HasBlockingOverlay()) F->CloseOverlay();
    else if(F->IsGambleConfirmOpen()) F->ConfirmGamble(false);
    else if(F->IsAmmoMenuOpen()&&F->GetPendingAmmoPrice()!=INDEX_NONE) F->ConfirmAmmoPurchase(false);
    else if(F->IsWeaponMenuOpen()&&F->GetInspectedWeapon()!=INDEX_NONE) F->CloseWeaponTip();
    else if(F->IsNextLevelConfirmationOpen()) F->CancelNextLevelConfirmation();
    else if(F->IsGambleMenuOpen()&&DemoCasino::ModeForChoice(F->GetInspectedGamble())>=0&&!F->IsGambleWaiting()&&!F->IsCasinoRolling()) F->HandleCasinoClick(TEXT("Casino.Back"));
    else if(F->IsUpgradeMenuOpen()&&!F->IsRewardMenu()&&(F->IsWeaponMenuOpen()||F->IsAmmoMenuOpen()||F->IsCoreMenuOpen()||F->IsGambleMenuOpen())) F->SetTerminalWeaponPage(false);
    else F->CloseUpgradeMenu();
}
void UDemoUIManager::RestoreLegacyInput()
{
    DEMO_LOG_CALL(); auto* PC=Cast<ADemoPlayerController>(GetOwner()); if(!PC||!PC->GetLocalPlayer()) return;
    const auto* F=PC->GetMenuFlow(); const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前旧页面/游戏态，不能假设总回GameOnly。
    const bool Menu=F->HasBlockingOverlay()||F->IsUpgradeMenuOpen()||F->IsNextLevelConfirmationOpen()||!State||State->Phase==EDemoPhase::Lobby||State->Phase==EDemoPhase::Victory||State->Phase==EDemoPhase::Defeat;
    if(auto* Router=PC->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>()) Router->SetActiveUIInputConfig(FUIInputConfig(Menu?ECommonInputMode::All:ECommonInputMode::Game,Menu?EMouseCaptureMode::NoCapture:EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown,false),this);
    PC->bInputPolicyApplied=false; PC->SetMenuInput(Menu); // 边沿强制一次重建门禁/光标；不在每帧争夺焦点。
}
void UDemoUIManager::Shutdown()
{ DEMO_LOG_CALL(); if(bStopped) return; bStopped=true; if(Root) { Root->ModalStack->ClearWidgets(); Root->MenuStack->ClearWidgets(); Root->RemoveFromParent(); Root=nullptr; } bOwnedInput=false; ActiveModal=0; }
void UDemoUIManager::EndPlay(const EEndPlayReason::Type Reason) { DEMO_LOG_CALL(); Shutdown(); Super::EndPlay(Reason); }
#if WITH_EDITOR
bool UDemoUIManager::PressActionForTest(FName Action)
{ DEMO_LOG_CALL(); NextModelRefresh=0; Sync(); auto* Screen=GetActiveScreen(); auto* Button=Screen?Screen->FindAction(Action):nullptr; return Button&&IsTopScreen(Screen)&&Button->PressForTest(); }
#endif

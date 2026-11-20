// 职责：大厅/存档旅行/设置/暂停/Tab与层级返回的实际Widget回归。使用：独立Editor进程及隔离GameUserSettingsINI，不读写用户正式档。
#include "Tests/DemoSessionUITest.h"
#include "Tests/DemoUILayoutChecks.h" // 截图同步核对真实几何；WITH_EDITOR范围内使用。
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Settings/DemoGameUserSettings.h"
#include "Save/DemoRunSave.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoMenuButton.h" // 安全默认焦点检查真实按钮及内部Slate子控件，不由夹具强制聚焦。
#include "UI/DemoUIPresentation.h" // 两项即时设备偏好必须独立于原0..4设置草稿。
#include "UI/Screens/DemoLobbyScreen.h"
#include "UI/Screens/DemoSessionScreen.h"
#include "UI/Screens/DemoSettingsScreen.h"
#include "UI/Screens/DemoPlayerStatusScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Interaction/DemoInteractable.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/ScrollBox.h" // Status首屏截图后分帧滚主正文至尾，补账本/装备下段真实绘制证据。
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "CommonInputSubsystem.h" // 过滤测试进程外部XInput事件，避免真实手柄改变本次键鼠焦点。
#include "UnrealClient.h"
namespace
{
    int32 Step=0,Passed=0; // 仅该显式测试进程，跨OpenLevel保留步骤和断言数。
    double Deadline=0,Started=0,PausedTime=0; // 单调截止/超时与实际暂停World秒。
    float OriginalSensitivity=1; FIntPoint OriginalResolution; // 隔离设备INI的应用/回退对照。
    FDateTime CreatedAt; // 原槽创建时间，旅行后必须保持。
    bool Failed=false; // 首个失败后停止所有测试动作。
    bool OriginalReducedMotion=false,OriginalUISound=true; // 两项设备偏好只在显式隔离INI测试；关闭重开后经真实按钮还原。
    TArray<FString> UnappliedDraft; // 原0..4待应用草稿的可读值，用于验证即时表现按钮不覆盖草稿或误应用。
    TWeakObjectPtr<ULocalPlayer> FilteredLocalPlayer; // 每次OpenLevel重新核查本地所有者，过滤仅该测试进程生命周期。
    /** 同步读取真实TextBlock串供数据断言；不从预期模型拼接页面内容。 */
    FString ReadText(UDemoMenuScreen* Screen)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] SessionUITest.ReadText")); FString Result; TArray<UWidget*> Widgets; // 仅当前树拥有的控件。
        if(Screen&&Screen->WidgetTree) Screen->WidgetTree->GetAllWidgets(Widgets);
        for(auto* Widget:Widgets) if(const auto* Text=Cast<UTextBlock>(Widget)) Result+=TEXT("|")+Text->GetText().ToString();
        return Result;
    }
    /** 读取当前顶层真实Action焦点；禁止先SetUserFocus再声称默认取消焦点通过。 */
    bool ActionHasFocus(UDemoMenuScreen* Screen,FName Action,APlayerController* Owner)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] SessionUITest.ActionHasFocus"));
        const UDemoMenuButton* Button=Screen?Screen->FindAction(Action):nullptr; // 本次构建树拥有，调用结束不保留。
        return Button&&Button->GetIsEnabled()&&(Button->HasUserFocus(Owner)||Button->HasUserFocusedDescendants(Owner));
    }
}
#endif
ADemoSessionUITest::ADemoSessionUITest()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
#endif
}
#if WITH_EDITOR
bool ADemoSessionUITest::Check(bool Value,const TCHAR* Reason)
{
    DEMO_LOG_CALL(); UE_LOG(LogFPSDemo,Display,TEXT("SESSION_UI_TEST %s stage=%d %s"),Value?TEXT("PASS"):TEXT("FAIL"),Step,Reason);
    if(Value) ++Passed; else { Failed=true; FPlatformMisc::RequestExitWithStatus(false,1); } return Value;
}
bool ADemoSessionUITest::Press(FName Action)
{
    DEMO_LOG_CALL(); auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); auto* UI=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 正式本地组件。
    const bool Result=UI&&UI->PressActionForTest(Action); if(UI) UI->Sync(); return Check(Result,*FString::Printf(TEXT("actual button %s"),*Action.ToString()));
}
void ADemoSessionUITest::Key(FKey Input)
{
    DEMO_LOG_CALL(); FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Input,FModifierKeysState(),0,false,0,0)); FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Input,FModifierKeysState(),0,false,0,0));
}
void ADemoSessionUITest::Next(int32 Stage,double Delay) { DEMO_LOG_CALL(); Step=Stage; Deadline=FPlatformTime::Seconds()+Delay; }
void ADemoSessionUITest::Capture(const TCHAR* Name)
{
    DEMO_LOG_CALL();
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); auto* UI=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 当前World拥有者，旅行不缓存。
    DemoUILayoutChecks::Queue(UI?UI->GetActiveScreen():nullptr); // 统一分帧关闭/切页，指南也检查截图这一帧的真实几何。
    FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(FString(TEXT("SessionUI-"))+Name+TEXT(".png")),true,false);
}
#endif
void ADemoSessionUITest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    FString LayoutError; const int32 LayoutResult=DemoUILayoutChecks::ProcessPending(LayoutError); // 截图后真实渲染一帧再采样，不在重建帧读旧缓存。
    if(LayoutResult&&!Check(LayoutResult>0,*FString::Printf(TEXT("rendered layout %s"),*LayoutError))) return;
    if(Failed) return; const double Now=FPlatformTime::Seconds(); // 独立进程最长150秒，暂停不阻塞测试。
    if(!Started) Started=Now; if(Now-Started>150) { Check(false,TEXT("real UI state machine timeout")); return; } if(Now<Deadline) return;
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); auto* State=GetWorld()->GetGameState<ADemoGameState>(); auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 每World重新借用。
    ULocalPlayer* Local=PC?PC->GetLocalPlayer():nullptr; // 本进程本地设备；过滤不写入INI且不改变生产手柄输入规则。
    if(Local&&FilteredLocalPlayer.Get()!=Local)
    {
        if(auto* Input=Local->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("SessionUIAutomation"),true);
        FilteredLocalPlayer=Local;
    }
    if(!PC||!State||!Mode||GetWorld()->GetTimeSeconds()<.5) return;
    auto* UI=PC->FindComponentByClass<UDemoUIManager>(); auto* Flow=PC->GetMenuFlow(); auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 正式生命周期所有者。
    auto* Player=Cast<ADemoCharacter>(PC->GetPawn()); if(!UI||!Flow||!Settings||!Saves||!Player) return; UI->Sync();
    switch(Step)
    {
    case 0:
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { Capture(TEXT("Guide")); Next(100); break; }
        else Settings->DismissFirstRunGuide(); // 隔离INI已读时不等待不存在的提示。
        Next(1); break;
    case 100: if(!Press(TEXT("OverlayBack"))) return; Next(1); break; // 保留已绘制指南一帧，再经真实按钮关闭。
    case 1:
        if(!Check(Cast<UDemoLobbyScreen>(UI->GetActiveScreen())&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0,TEXT("actual lobby root active"))) return;
        Capture(TEXT("Lobby")); Next(2); break;
    case 2: if(!Press(TEXT("StartGame"))) return; Next(3); break;
    case 3:
        if(!Check(Cast<UDemoSessionScreen>(UI->GetActiveScreen())&&Flow->GetMenuPage()==EDemoMenuPage::Saves,TEXT("start opens saves without starting combat"))) return;
        Capture(TEXT("Saves")); Next(4); break;
    case 4: if(!Press(TEXT("Save0"))) return; Next(5); break;
    case 5:
        if(!Check(UI->GetModalStackCount()==1&&UI->GetMenuStackCount()==1&&!UI->PressActionForTest(TEXT("Save1")),TEXT("create modal blocks underlying slots"))) return;
        if(!Check(ActionHasFocus(UI->GetActiveScreen(),TEXT("CreateNo"),PC),TEXT("new-save confirmation defaults to actual Cancel without forced focus"))) return;
        Capture(TEXT("Create")); Next(105); break;
    case 105: if(!Press(TEXT("UI.Back"))) return; Next(6); break;
    case 6:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Saves&&!Saves->SlotExists(0),TEXT("cancel creation leaves empty slot"))) return;
        if(!Press(TEXT("Save0"))) return; Next(7); break;
    case 7: Next(8,1.); if(!Press(TEXT("CreateYes"))) return; break; // 正式OpenLevel前推进静态步骤。
    case 8:
        if(State->Phase!=EDemoPhase::Hub) return;
        if(!Check(Saves->GetActiveSlot()==0&&Saves->GetSlot(0)&&!Flow->HasBlockingOverlay(),TEXT("new save travel reaches playable hub"))) return;
        CreatedAt=Saves->GetSlot(0)->CreatedLocal; Flow->EscapePressed(); Next(9); break;
    case 9:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Pause&&UGameplayStatics::IsGamePaused(this),TEXT("single-player pause uses actual CommonUI"))) return;
        PausedTime=GetWorld()->GetTimeSeconds(); Capture(TEXT("Pause")); Next(10); break;
    case 10:
        if(!Check(FMath::IsNearlyEqual(PausedTime,GetWorld()->GetTimeSeconds()),TEXT("World remains paused across rendered UI frames"))) return;
        if(!Press(TEXT("PauseSettings"))) return; OriginalSensitivity=Settings->GetMouseSensitivity(); OriginalResolution=Settings->GetScreenResolution(); Next(11); break;
    case 11:
        if(!Check(Cast<UDemoSettingsScreen>(UI->GetActiveScreen())!=nullptr,TEXT("settings is actual dedicated widget"))) return;
        Capture(TEXT("Settings")); Next(111); break;
    case 111:
    {
        if(!Press(TEXT("Setting2Plus"))) return;
        UnappliedDraft.Reset(); for(int32 Index=0;Index<5;++Index) UnappliedDraft.Add(Flow->GetSettingText(Index)); // 记录真实Flow草稿，不能用测试预期伪造控件值。
        OriginalReducedMotion=DemoUI::ReducedMotion(); OriginalUISound=DemoUI::UISoundEnabled();
        if(!Press(TEXT("UI.ToggleReducedMotion"))||!Press(TEXT("UI.ToggleSound"))) return;
        bool DraftPreserved=true; for(int32 Index=0;Index<5;++Index) DraftPreserved&=Flow->GetSettingText(Index)==UnappliedDraft[Index]; // 两个即时按钮都不得触碰任一原设置项。
        if(!Check(DemoUI::ReducedMotion()!=OriginalReducedMotion&&DemoUI::UISoundEnabled()!=OriginalUISound&&DraftPreserved&&Settings->GetMouseSensitivity()==OriginalSensitivity,
            TEXT("real presentation preference buttons persist immediately without applying or replacing settings draft 0..4"))) return;
        Next(211); break;
    }
    case 211: Capture(TEXT("SettingsImmediatePreferences")); Next(212); break; // 先绘制实际切换状态，再关闭页面。
    case 212:
        if(!Press(TEXT("OverlayBack"))||!Press(TEXT("PauseSettings"))) return;
        Next(213); break;
    case 213:
        if(!Check(DemoUI::ReducedMotion()!=OriginalReducedMotion&&DemoUI::UISoundEnabled()!=OriginalUISound&&Settings->GetMouseSensitivity()==OriginalSensitivity,
            TEXT("closing and reopening settings preserves immediate UI preferences and discards only unapplied sensitivity"))) return;
        if(!Check(ReadText(UI->GetActiveScreen()).Contains(TEXT("即时保存")),TEXT("actual settings describes immediate device preference saving"))) return;
        if(!Press(TEXT("UI.ToggleReducedMotion"))||!Press(TEXT("UI.ToggleSound"))||!Press(TEXT("OverlayBack"))) return;
        if(!Check(DemoUI::ReducedMotion()==OriginalReducedMotion&&DemoUI::UISoundEnabled()==OriginalUISound,TEXT("real preference buttons restore isolated device configuration"))) return;
        Next(12); break;
    case 12:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Pause&&Settings->GetMouseSensitivity()==OriginalSensitivity,TEXT("settings back discards draft and restores pause"))) return;
        if(!Press(TEXT("PauseSettings"))||!Press(TEXT("Setting2Plus"))||!Press(TEXT("SettingsApply"))) return; Next(13); break;
    case 13:
        if(Flow->GetDisplayConfirmSeconds()>0) { if(!Press(TEXT("DisplayNo"))) return; }
        if(!Check(Settings->GetMouseSensitivity()>OriginalSensitivity,TEXT("apply changes real device sensitivity"))) return;
        OriginalResolution=Settings->GetScreenResolution(); if(!Press(TEXT("Setting0Minus"))||!Press(TEXT("SettingsApply"))) return; Next(14); break;
    case 14:
        if(!Check(UI->GetModalStackCount()==1&&Flow->GetDisplayConfirmSeconds()>0&&ReadText(UI->GetActiveScreen()).Contains(TEXT("秒后")),TEXT("display confirmation shows real countdown"))) return;
        if(!Check(ActionHasFocus(UI->GetActiveScreen(),TEXT("DisplayNo"),PC),TEXT("display confirmation defaults to actual Revert without forced focus"))) return;
        Capture(TEXT("Display")); Next(214); break; // 先保留默认安全焦点已绘制截图，再模拟用户移动焦点。
    case 214:
    {
        UDemoMenuButton* Keep=UI->GetActiveScreen()->FindAction(TEXT("DisplayYes")); // 正式当前模态按钮，不在默认焦点断言前强制设置焦点。
        if(!Check(Keep&&Keep->GetIsEnabled(),TEXT("actual display Keep button can receive deliberate user navigation focus"))) return;
        Keep->SetUserFocus(PC); // 仅模拟用户从安全默认动作移到保留；不调用点击、不提交显示确认。
        Next(215,1.2); break; // 实时跨越一次倒计时文本/控件刷新，再观察原动作焦点是否恢复。
    }
    case 215:
        if(!Check(Flow->GetDisplayConfirmSeconds()>0&&ActionHasFocus(UI->GetActiveScreen(),TEXT("DisplayYes"),PC),TEXT("display countdown rebuild preserves deliberate Keep focus after 1.2 real seconds"))) return;
        Capture(TEXT("DisplayFocusPreserved")); Next(15); break; // 仍接回原Escape/恢复分辨率路径，不点击保留改变原交易断言。
    case 15:
        Key(EKeys::Escape); Next(16); break;
    case 16:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Settings&&Flow->GetDisplayConfirmSeconds()==0&&Settings->GetScreenResolution()==OriginalResolution,TEXT("actual Escape restores display and returns to settings"))) return;
        if(!Press(TEXT("Setting0Minus"))||!Press(TEXT("SettingsApply"))) return; Next(116); break;
    case 116:
        if(!Check(ActionHasFocus(UI->GetActiveScreen(),TEXT("DisplayNo"),PC),TEXT("reopened display confirmation resets pooled dialog to safe Revert focus"))) return;
        Next(17,16.); break;
    case 17:
        if(!Check(UI->GetModalStackCount()==0&&Flow->GetDisplayConfirmSeconds()==0&&Settings->GetScreenResolution()==OriginalResolution,TEXT("real-time timeout reverts display while World paused"))) return;
        if(!Press(TEXT("OverlayBack"))||!Press(TEXT("OverlayBack"))) return; Flow->TabPressed(); Next(18); break;
    case 18:
        if(!Check(Cast<UDemoPlayerStatusScreen>(UI->GetActiveScreen())&&ReadText(UI->GetActiveScreen()).Contains(TEXT("生命"))&&ReadText(UI->GetActiveScreen()).Contains(TEXT("冲刺")),TEXT("Tab shows actual GAS and skill widgets"))) return;
        Player->GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetHealthAttribute(),73.f); Next(19); break;
    case 19:
        if(!Check(ReadText(UI->GetActiveScreen()).Contains(TEXT("生命"))&&ReadText(UI->GetActiveScreen()).Contains(TEXT("73 /")),TEXT("existing status label and dedicated numeric text update from real GAS health"))) return;
        Capture(TEXT("Status")); Next(219); break;
    case 219:
    {
        UScrollBox* MainScroll=Cast<UScrollBox>(UI->GetActiveScreen()->GetWidgetFromName(TEXT("PageScroll"))); // 首屏已绘制/截图后重新借用实际主区，不改变右侧核心/天赋滚动位置。
        if(!Check(MainScroll!=nullptr,TEXT("actual player-status main body exposes its production ScrollBox"))) return;
        MainScroll->ScrollToEnd(); Next(220); break; // UE在下一次布局处理ScrollToEnd，不在同帧截图旧位置。
    }
    case 220:
        Capture(TEXT("StatusBottom")); Next(20); break; // 补正文尾部真实截图与原几何检查后，继续原Tab输入恢复/旅行/退出断言。
    case 20: Key(EKeys::Tab); Next(21); break;
    case 21:
        if(!Check(!Flow->HasBlockingOverlay()&&!UGameplayStatics::IsGamePaused(this)&&!PC->IsMoveInputIgnored()&&!PC->IsLookInputIgnored(),TEXT("actual Tab restores gameplay input"))) return;
        Flow->EscapePressed(); Next(22); break;
    case 22: Next(23,1.); if(!Press(TEXT("PauseLobby"))) return; break;
    case 23:
        if(State->Phase!=EDemoPhase::Lobby) return;
        if(!Check(Cast<UDemoLobbyScreen>(UI->GetActiveScreen())!=nullptr,TEXT("return-lobby travel rebuilds one CommonUI root"))) return;
        if(!Press(TEXT("StartGame"))) return; Next(24); break;
    case 24:
        if(!Check(Saves->GetSlot(0)&&Saves->GetSlot(0)->CreatedLocal==CreatedAt,TEXT("existing save retains original local start time"))) return;
        Capture(TEXT("ExistingSave")); Next(25); break;
    case 25: Next(26,1.); if(!Press(TEXT("Save0"))) return; break;
    case 26:
        if(State->Phase!=EDemoPhase::Hub) return;
        if(!Check(!Flow->HasBlockingOverlay()&&Player->CanUsePlayerSkills()&&!PC->IsMoveInputIgnored(),TEXT("existing checkpoint resumes playable hub"))) return;
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); Mode->GetShopTerminal()->Interact(Player); Next(27); break; // Capsule中心保持地面上110cm，避免解穿透移出终端会话距离。
    case 27: if(!Press(TEXT("TerminalAmmo"))) return; Next(28); break;
    case 28: if(!Press(TEXT("UI.Back"))) return; Next(29); break;
    case 29:
        if(!Check(Flow->IsUpgradeMenuOpen()&&!Flow->IsAmmoMenuOpen(),TEXT("terminal child back returns home without ending session"))) return;
        if(!Press(TEXT("UI.Back"))) return; Next(30); break;
    case 30:
        if(!Check(!Flow->IsUpgradeMenuOpen()&&!UI->UsesWidgetMenu(),TEXT("terminal home back returns game"))) return;
        Flow->EscapePressed(); Next(31); break;
    case 31: if(!Press(TEXT("PauseQuit"))) return; Next(32,.05); break;
    case 32:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::QuitSaving&&Cast<UDemoSessionScreen>(UI->GetActiveScreen()),TEXT("exit saving is actual widget"))) return;
        Capture(TEXT("ExitSaving")); Next(132,.05); break;
    case 132: if(!Press(TEXT("QuitCancel"))) return; Next(33); break;
    default:
        if(!Check(Flow->GetMenuPage()==EDemoMenuPage::Pause,TEXT("cancel exit restores previous pause"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_SESSION_UI_SUCCESS assertions=%d"),Passed); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
#endif
}

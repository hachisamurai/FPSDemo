// 职责：本地菜单状态与输入门禁。使用：PC持有组件，HUD只读投影，经济操作通过拥有者请求网关。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Flow/DemoSettingsFlow.h"
#include "Player/Coop/DemoRoomSubsystem.h"
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
UDemoMenuFlowComponent::UDemoMenuFlowComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; SettingsFlow=CreateDefaultSubobject<UDemoSettingsFlow>(TEXT("SettingsFlow")); ExitFlow=CreateDefaultSubobject<UDemoExitSaveFlow>(TEXT("ExitFlow")); }
ADemoPlayerController* UDemoMenuFlowComponent::GetController() const { DEMO_LOG_TICK(); return CastChecked<ADemoPlayerController>(GetOwner()); }
void UDemoMenuFlowComponent::SetMenuInput(bool bEnabled) { DEMO_LOG_CALL(); if (!bStopped && GetController()->IsLocalController()) GetController()->SetMenuInput(bEnabled); }
ADemoInteractable* UDemoMenuFlowComponent::GetMenuTerminal() const { DEMO_LOG_TICK(); return MenuTerminal.Get(); }
uint64 UDemoMenuFlowComponent::GetMenuTerminalGeneration() const { DEMO_LOG_TICK(); return MenuTerminalGeneration; }
void UDemoMenuFlowComponent::UpdateRealtime(float DeltaTime)
{
    DEMO_LOG_TICK();
    if (bStopped || !GetController()->IsLocalController()) return;
    if(IsCasinoBetInputOpen()&&!CanEditCasinoBet()) CloseCasinoBetInput(); // 阶段/模态变化及时撤销文本层，不让旧编辑遮挡新界面。
    if(GambleReel.Update(FPlatformTime::Seconds())) MenuMessage=TEXT("抽取完成"); // UI真实秒推进到停稳才显示完成；具体奖励保留在Reel.ResultMessage，不再次提交事务。
    if(MenuPage==EDemoMenuPage::PlayerStatus)
    {
        const ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetController()->GetPawn()); // 每帧只借用，联机阵亡/重生不能保留旧面板。
        if(!Pawn||Pawn->IsCoopEliminated()) CloseOverlay();
    }
    // 连接失败的恢复只在新大厅就绪且无覆盖层时消费；不开新查询覆盖原失败原因。
    if (!HasBlockingOverlay() && GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->ConsumeBrowserReturn()) MultiplayerPressed(false);
    UpdateQuitSave(); // 无退出页面立即返回，不在HTTP回调中持有PC。
    if (SettingsFlow->HasExpired()) ConfirmDisplaySettings(false);
    if (bMenuOpen && !bRewardMenu && !MenuTerminal.IsValid() && (!bCasinoDevicePage || !GetController()->GetMenuSnapshot().CasinoDeviceToken.IsValid()))
    {
        bMenuOpen = false; bCasinoDevicePage=false; MenuTerminalGeneration = 0; PendingAmmoPrice = INDEX_NONE; InspectedWeapon = INDEX_NONE;
        ResetGamblePresentation(); // 终端销毁撤销本地动画，已保存奖励不回滚。
        MenuMessage = TEXT("终端交互已结束"); RefreshMenuInput(); // 仅清下层终端，保留暂停/退出等顶层意图。
    }
    TryShowFirstRunGuide(); // 等GS与本地视口可用，且只占用空闲大厅/Hub页面；不抢设置、连接错误或强制奖励的焦点。
}
void UDemoMenuFlowComponent::Shutdown()
{
    DEMO_LOG_CALL();
    if (bStopped) return;
    if (MenuPage == EDemoMenuPage::QuitSaving)
        if (UDemoCloudSync* Cloud = GetWorld()->GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Cloud->CancelExitSync(); // 取消UI等待，不删除耐久发件箱。
    if (SettingsFlow->HasPendingDisplay()) ConfirmDisplaySettings(false); // 旅行/关闭PC时也恢复未确认显示模式。
    bStopped = true; bApprovedInteractionPending=false; ExitFlow->Cancel(); MenuTerminal.Reset(); PendingNextLevelTerminal.Reset();
    ResetGamblePresentation(); // 无跨World计时器/回调残留。
    MenuTerminalGeneration = NextTerminalGeneration = 0;
}
void UDemoMenuFlowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) { DEMO_LOG_CALL(); Shutdown(); Super::EndPlay(EndPlayReason); }

void UDemoMenuFlowComponent::OpenApprovedInteraction(const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    bCasinoDevicePage=false; // 新旧终端批准撤销设备页身份，不能借旧Token跨页操作。
    if (bStopped || !GetController()->IsLocalController() || HasBlockingOverlay() || !IsValid(Snapshot.Terminal)) { bApprovedInteractionPending=false; return; }
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // Actor/RPC通道与GS属性无跨通道先后保证。
    if (!State || (State->Phase!=EDemoPhase::Hub && State->Phase!=EDemoPhase::Intermission))
    { bApprovedInteractionPending=true; UE_LOG(LogFPSDemo,Log,TEXT("MENU interaction waits for replicated run phase")); return; }
    bApprovedInteractionPending=false;
    RefreshFromReplicatedState(); // 先消费已经到达的阶段变化，再绑定本次批准终端；否则同一回执末尾刷新会误关刚打开的新页面。
    MenuTerminal=Snapshot.Terminal; MenuTerminalGeneration=Snapshot.TerminalGeneration;
    if (Snapshot.bNextLevel) OpenNextLevelConfirmation(Snapshot.Terminal);
    else OpenUpgradeMenu(false);
}
void UDemoMenuFlowComponent::ApplyCommandResult(EDemoGameplayCommand Command,const FDemoCommandResult& Result)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) return;
    if(Command==EDemoGameplayCommand::Gamble||Command==EDemoGameplayCommand::Casino) { ReceiveGambleResult(Result); RefreshFromReplicatedState(); return; } // 保存结果与展示状态分离，通用文案不能提前覆盖揭晓提示。
    MenuMessage=Result.Message;
    if (!Result.IsApplied()) { UE_LOG(LogFPSDemo,Log,TEXT("MENU command rejected code=%s"),*Result.Code.ToString()); return; }
    if (Command==EDemoGameplayCommand::ChooseReward && bRewardMenu)
    { bRewardMenu=false; bMenuOpen=false; RefreshMenuInput(); }
    if (Command==EDemoGameplayCommand::StartLevel && bNextLevelConfirmationOpen) CancelNextLevelConfirmation();
    if (Command==EDemoGameplayCommand::ReturnHub || Command==EDemoGameplayCommand::RestartRun) OnRunReady();
    RefreshFromReplicatedState();
}
void UDemoMenuFlowComponent::RefreshFromReplicatedState()
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) return;
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 公开阶段可比拥有者私有投影先后抵达，始终容忍未就绪。
    if (!State) return;
    const bool bPhaseChanged=LastObservedPhase!=static_cast<int32>(State->Phase)||LastObservedLevel!=State->LevelNumber; // 仅状态事件驱动底层页面转换。
    LastObservedPhase=static_cast<int32>(State->Phase); LastObservedLevel=State->LevelNumber;
    if (bPhaseChanged)
    {
        if(MenuPage==EDemoMenuPage::PlayerStatus) CloseOverlay(); // 阶段变化关闭只读页，恢复输入/单人暂停，不遮住强制奖励。
        ResetGamblePresentation(); // 队友推进/死亡会使当前终端失效；关闭动画不影响已落盘事务。
        bMenuOpen=bRewardMenu=bNextLevelConfirmationOpen=false; bCasinoDevicePage=false;
        MenuTerminal.Reset(); PendingNextLevelTerminal.Reset(); MenuTerminalGeneration=NextTerminalGeneration=0;
        PendingAmmoPrice=INDEX_NONE; InspectedWeapon=INDEX_NONE;
    }
    if (State->Phase==EDemoPhase::Reward && GetController()->GetMenuSnapshot().bCanChooseReward)
    { bMenuOpen=true; bRewardMenu=true; TerminalPage=EDemoTerminalPage::Stats; }
    else if (bRewardMenu) { bMenuOpen=false; bRewardMenu=false; }
    if (bApprovedInteractionPending && (State->Phase==EDemoPhase::Hub || State->Phase==EDemoPhase::Intermission))
        OpenApprovedInteraction(GetController()->GetMenuSnapshot()); // 仅最新仍有效会话可补开，服务器已撤销则自动丢弃。
    RefreshMenuInput(); // 覆盖层仍由MenuPage唯一管理，服务器阶段变化不关闭设置或退出屏障。
}

void UDemoMenuFlowComponent::ShowConnectionFailure(const FString& Reason)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController()) return;
    MenuMessage=Reason;
    if (MenuPage!=EDemoMenuPage::QuitSaving) { MenuPage=EDemoMenuPage::Pause; bPauseMenuActive=true; }
    RefreshMenuInput(); // 本机菜单独立于GS就绪，可靠提供离开路径，网络World不暂停。
}

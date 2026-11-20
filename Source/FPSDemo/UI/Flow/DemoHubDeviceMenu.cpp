// 职责：实体设备批准后的既有Casino页面直达。使用：只接受Owner服务器投影，金额/概率/结算继续原MenuFlow和Shop事务，不生成总列表或第二局面。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Debug/DemoLog.h"
bool UDemoMenuFlowComponent::IsCasinoDevicePage() const { DEMO_LOG_TICK(); return bCasinoDevicePage&&bMenuOpen; }
void UDemoMenuFlowComponent::OpenCasinoDevicePage(const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController() || HasBlockingOverlay() || !Snapshot.CasinoDeviceToken.IsValid() || Snapshot.CasinoDeviceMode<0 || Snapshot.CasinoDeviceMode>6)
    { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_PAGE rejected owner/modal/projection")); return; }
    RefreshFromReplicatedState(); // 先消费旧Phase变化，不能在打开后被晚到的Hub刷新撤销。
    bMenuOpen=true; bRewardMenu=false; bCasinoDevicePage=true; bNextLevelConfirmationOpen=false;
    MenuTerminal.Reset(); MenuTerminalGeneration=0; ResetGamblePresentation();
    TerminalPage=EDemoTerminalPage::Gambling; InspectedWeapon=INDEX_NONE; InspectedGamble=INDEX_NONE;
    PendingAmmoPrice=INDEX_NONE; MenuMessage.Empty();
    InspectGamble(DemoCasino::ChoiceForMode(Snapshot.CasinoDeviceMode)); // 原唯一Mode→Choice与草稿初始化，不复制赔率或金额。
    SetMenuInput(true);
}

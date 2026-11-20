// 职责：安全终端核心选择页。用法：Canvas白名单转入本地菜单，拥有者PC提交意图；服务器决定选择和存档。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Game/DemoGameState.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Debug/DemoLog.h"
bool UDemoMenuFlowComponent::IsCoreMenuOpen() const
{ DEMO_LOG_TICK(); return bMenuOpen&&!bRewardMenu&&TerminalPage==EDemoTerminalPage::Core; }
bool UDemoMenuFlowComponent::IsDepartureCorePickerOpen() const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 本地只读阶段；锁定/无尽等由Offer.BlockReason与服务器裁决。
    return bNextLevelConfirmationOpen&&State&&State->Phase==EDemoPhase::Hub&&!State->bEndless&&PendingNextLevelTerminal.IsValid();
}
void UDemoMenuFlowComponent::OpenCoreMenu()
{
    DEMO_LOG_CALL();
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 本地只读阶段，提交仍重验。
    if(bStopped||!GetController()->IsLocalController()||HasBlockingOverlay()||!bMenuOpen||bRewardMenu||!State||State->Phase!=EDemoPhase::Hub
        || !MenuTerminal.IsValid()||!GetController()->GetMenuSnapshot().TerminalBlockReason.IsEmpty())
    { UE_LOG(LogFPSDemo,Log,TEXT("CORE_MENU rejected state/terminal")); return; }
    ResetGamblePresentation(); TerminalPage=EDemoTerminalPage::Core; InspectedWeapon=INDEX_NONE;
    PendingAmmoPrice=INDEX_NONE; MenuMessage.Empty();
}
void UDemoMenuFlowComponent::SelectCore(int32 Choice)
{
    DEMO_LOG_CALL();
    const bool bDeparture=IsDepartureCorePickerOpen(); // 2026-11-20：Hub出发终端页也可选核心，使用该页已签发的出发终端会话。
    if(bStopped||HasBlockingOverlay()||!(IsCoreMenuOpen()||bDeparture)||Choice<0||Choice>=DemoRunBuild::CoreCount())
    { UE_LOG(LogFPSDemo,Log,TEXT("CORE_INTENT rejected page/index departure=%d"),bDeparture); return; }
    FDemoGameplayCommandRequest Request; // 不传属性数值或天赋类路径。
    Request.Command=EDemoGameplayCommand::SelectCore; Request.Choice=Choice;
    Request.Terminal=bDeparture?PendingNextLevelTerminal.Get():MenuTerminal.Get(); Request.TerminalGeneration=bDeparture?NextTerminalGeneration:MenuTerminalGeneration; // 服务器按同一会话重验距离/阶段/代数。
    UE_LOG(LogFPSDemo,Log,TEXT("CORE_INTENT submit choice=%d source=%s"),Choice,bDeparture?TEXT("departure"):TEXT("upgrade"));
    Request.BuildRevision=GetController()->GetMenuSnapshot().Build.Revision;
    Request.ExpectedLevel=0; MenuMessage=TEXT("等待服务器确认核心与检查点");
    GetController()->SubmitGameplayCommand(Request);
}

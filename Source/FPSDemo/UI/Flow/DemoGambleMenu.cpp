// 职责：终端抽取交互与本机展示状态。使用：HUD意图经MenuFlow提交正式交易，已提交回执才可滚动或跳过。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Debug/DemoLog.h"
#include "Settings/DemoGameUserSettings.h"
bool UDemoMenuFlowComponent::IsGambleMenuOpen() const
{ DEMO_LOG_TICK(); return bMenuOpen&&!bRewardMenu&&TerminalPage==EDemoTerminalPage::Gambling; }
int32 UDemoMenuFlowComponent::GetInspectedGamble() const { DEMO_LOG_TICK(); return InspectedGamble; }
int32 UDemoMenuFlowComponent::GetGambleRewardPage() const
{
    DEMO_LOG_TICK();
    const auto& Offers=GetController()->GetMenuSnapshot().GambleOffers; // 拥有者最新权威投影，只读。
    return Offers.IsValidIndex(3)?FMath::Clamp(GambleRewardPage,0,FMath::Max(0,Offers[3].Rewards.Num()-1)):0;
}
void UDemoMenuFlowComponent::ChangeGambleRewardPage(int32 Direction)
{
    DEMO_LOG_CALL();
    if((Direction!=-1&&Direction!=1)||bStopped||!IsGambleMenuOpen()||HasBlockingOverlay()||IsGambleConfirmOpen()
        ||bGambleWaiting||!GambleReel.Cards.IsEmpty()||InspectedGamble!=3)
    { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_POOL_PAGE rejected stale/modal/direction")); return; }
    GambleRewardPage=GetGambleRewardPage()+Direction; GambleRewardPage=GetGambleRewardPage();
}
bool UDemoMenuFlowComponent::IsGambleConfirmOpen() const { DEMO_LOG_TICK(); return PendingGamblePrice!=INDEX_NONE; }
bool UDemoMenuFlowComponent::IsGambleWaiting() const { DEMO_LOG_TICK(); return bGambleWaiting; }
const FDemoGambleReel& UDemoMenuFlowComponent::GetGambleReel() const { DEMO_LOG_TICK(); return GambleReel; }
void UDemoMenuFlowComponent::ResetGamblePresentation()
{
    DEMO_LOG_CALL();
    CloseCasinoBetInput(); // 页面关闭或旅行时移除原生输入层，未确认文本不进入草稿。
    CasinoReveal=FDemoCasinoResult(); CasinoRevealStarted=0; PendingGamblePrice=INDEX_NONE; WaitingGambleChoice=INDEX_NONE; bGambleWaiting=false; GambleReel.Reset();
}
void UDemoMenuFlowComponent::DismissGambleReveal()
{
    DEMO_LOG_CALL();
    if(bStopped||!IsGambleMenuOpen()||HasBlockingOverlay()||bGambleWaiting||GambleReel.bRolling)
    { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REEL dismiss rejected state")); return; }
    GambleReel.Reset(); // 返回已刷新报价的列表；只清展示，不执行奖励/保存。
}
FString UDemoMenuFlowComponent::GetContinueGambleBlockReason() const
{
    DEMO_LOG_TICK();
    if(bStopped||!IsGambleMenuOpen()||HasBlockingOverlay()||IsGambleConfirmOpen()||GambleReel.Cards.IsEmpty()) return TEXT("当前无法继续抽取");
    if(bGambleWaiting||GambleReel.bRolling) return TEXT("请等待本次揭晓结束");
    const auto& View=GetController()->GetMenuSnapshot(); // 本帧拥有者权威投影，不能复用上一轮冻结的奖池或费用。
    if(!View.TerminalBlockReason.IsEmpty()) return View.TerminalBlockReason;
    if(InspectedGamble<0||InspectedGamble>=7||!View.GambleOffers.IsValidIndex(InspectedGamble)) return TEXT("抽取项目已失效");
    if(View.Progress.Gambling.Serial<=PendingGambleSerial) return TEXT("正在更新抽取报价…"); // 回执与投影到达次序不应导致重用上一笔序号。
    return View.GambleOffers[InspectedGamble].BlockReason;
}
void UDemoMenuFlowComponent::ContinueGamble()
{
    DEMO_LOG_CALL();
    const FString BlockReason=GetContinueGambleBlockReason(); // 同步检查后才清展示；失败保留已揭晓奖励与返回按钮。
    if(!BlockReason.IsEmpty())
    { MenuMessage=BlockReason; UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_CONTINUE rejected %s"),*BlockReason); return; }
    GambleReel.Reset(); // 仅清上一轮UI；下面复用正式确认链，重新冻结当前费用/序号，服务仍复核并保存。
    RequestGamble();
    if(IsGambleConfirmOpen()) ConfirmGamble(true); // 带费用的“继续抽取”是本次明确下注，不额外弹确认；等待标志在RPC前设置。
}
void UDemoMenuFlowComponent::ReceiveGambleResult(const FDemoCommandResult& Result)
{
    DEMO_LOG_CALL();
    if(!bGambleWaiting||(!IsGambleMenuOpen()&&!IsCasinoDeviceOperating())) { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REEL late reply ignored after close")); return; } // 独立Operate不是隐形全屏页面，但仍须拥有当前合法上下文和等待相关。
    bGambleWaiting=false; MenuMessage=Result.Message;
    if(WaitingGambleChoice==15&&(PendingCasino.Operation==0||PendingCasino.Operation==3)&&Result.IsApplied()&&Result.Code!=TEXT("RangeUnavailable"))
    { CloseUpgradeMenu(); return; } // 进入真实射击训练，恢复Enhanced Input；成绩仍在服务器计时保存。
    if(DemoCasino::ModeForChoice(WaitingGambleChoice)>=0)
    {
        if(Result.Status==EDemoCommandStatus::Committed&&Result.CasinoResult.Mode==DemoCasino::ModeForChoice(WaitingGambleChoice)&&Result.CasinoResult.Serial==PendingGambleSerial+1)
        { CasinoReveal=Result.CasinoResult; CasinoRevealStarted=FPlatformTime::Seconds(); }
        else UE_LOG(LogFPSDemo,Log,TEXT("CASINO_UI rejected/uncorrelated reply"));
        return;
    }
    if(Result.Status!=EDemoCommandStatus::Committed||WaitingGambleChoice>=7) return; // 失败/装配不播放抽取。
    if(Result.GambleReveal.Serial!=PendingGambleSerial+1||Result.GambleReveal.Choice!=WaitingGambleChoice)
    { UE_LOG(LogFPSDemo,Warning,TEXT("GAMBLE_REEL reply correlation failed; committed message retained")); return; }
    if(GambleReel.Begin(Result.GambleReveal,Result.Message,FPlatformTime::Seconds()))
    {
        if(IsGambleAnimationSkipped()) GambleReel.Finish(); // 跳过只作用于通过序号与奖池验证的已提交回执。
        MenuMessage=GambleReel.bRolling?TEXT("正在抽取"):TEXT("抽取完成");
    }
}
void UDemoMenuFlowComponent::OpenGambleMenu()
{
    DEMO_LOG_CALL();
    if(bCasinoDevicePage) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_PAGE stays on approved mode")); return; }
    if(bStopped||bGambleWaiting||GambleReel.bRolling||!GetController()->IsLocalController()||!bMenuOpen||bRewardMenu||HasBlockingOverlay()||InspectedWeapon!=INDEX_NONE||PendingAmmoPrice!=INDEX_NONE
        ||!GetController()->GetMenuSnapshot().TerminalBlockReason.IsEmpty()) { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_MENU rejected context")); return; }
    ResetGamblePresentation(); TerminalPage=EDemoTerminalPage::Gambling; InspectedGamble=INDEX_NONE; MenuMessage.Empty(); // 新页面不重播历史结果。
}
void UDemoMenuFlowComponent::InspectGamble(int32 Choice)
{
    DEMO_LOG_CALL();
    if(bCasinoDevicePage&&DemoCasino::ModeForChoice(Choice)!=GetController()->GetMenuSnapshot().CasinoDeviceMode)
    { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_PAGE rejected different mode")); return; }
    if(bStopped||!IsGambleMenuOpen()||HasBlockingOverlay()||IsGambleConfirmOpen()||bGambleWaiting||!GambleReel.Cards.IsEmpty()||!GetController()->GetMenuSnapshot().GambleOffers.IsValidIndex(Choice))
    { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_INSPECT rejected page/modal/wait/index")); return; }
    CasinoReveal=FDemoCasinoResult();
    if(DemoCasino::ModeForChoice(Choice)>=0)
    {
        const auto& Rules=GetController()->GetMenuSnapshot().CasinoRules; // 仅满足最低下注，切玩法不压低已有金额；雷数读配置。
        CasinoDraft.Mode=DemoCasino::ModeForChoice(Choice); CasinoDraft.Operation=0; CasinoDraft.Selection=0; CasinoDraft.KeepMask=0;
        CasinoDraft.Stake=FMath::Max(CasinoDraft.Stake,Rules.MinBet);
        if(!Rules.BombChoices.Contains(CasinoDraft.Bombs)&&!Rules.BombChoices.IsEmpty()) CasinoDraft.Bombs=Rules.BombChoices[0];
    }
    InspectedGamble=Choice; GambleRewardPage=0; MenuMessage.Empty(); // 每次新查看从第一项开始，旧池页号不越界。
}
void UDemoMenuFlowComponent::RequestGamble()
{
    DEMO_LOG_CALL();
    if(DemoCasino::ModeForChoice(InspectedGamble)>=0&&CasinoReveal.Mode>=0)
    { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_UI ignored stale fixed wager action during result")); return; } // 结果页只有专用续抽意图可开启下一笔。
    if(bStopped||!IsGambleMenuOpen()||HasBlockingOverlay()||IsGambleConfirmOpen()||bGambleWaiting||!GambleReel.Cards.IsEmpty())
    { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REQUEST rejected page/modal/wait")); return; }
    if(DemoCasino::ModeForChoice(InspectedGamble)>=0) { RequestCasino(); return; }
    const auto& View=GetController()->GetMenuSnapshot(); // 只用服务器本人报价。
    if(!View.GambleOffers.IsValidIndex(InspectedGamble)) { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REQUEST missing inspected offer")); return; }
    const auto& Offer=View.GambleOffers[InspectedGamble]; // 此栈借用，不跨异步持有。
    if(!Offer.BlockReason.IsEmpty()) { MenuMessage=Offer.BlockReason; UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_REQUEST rejected %s"),*MenuMessage); return; }
    PendingGamblePrice=Offer.Cost; PendingGambleSerial=View.Progress.Gambling.Serial; // 确认冻结报价，不能随刷新偷偷变价。
}
void UDemoMenuFlowComponent::ConfirmGamble(bool bConfirm)
{
    DEMO_LOG_CALL();
    if(bStopped||!IsGambleMenuOpen()||HasBlockingOverlay()||!IsGambleConfirmOpen()||bGambleWaiting)
    { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_CONFIRM rejected stale/modal/wait")); return; }
    if(DemoCasino::ModeForChoice(InspectedGamble)>=0)
    {
        ConfirmCasinoWager(bConfirm); return; // 原页面与物理明确拉杆共用冻结确认/提交，页面前置守卫保持不变。
    }
    FDemoGameplayCommandRequest Request; // 无玩家ID、随机数或奖励，由权威重新决定。
    Request.Command=EDemoGameplayCommand::Gamble; Request.Choice=InspectedGamble; Request.ExpectedPrice=PendingGamblePrice; Request.GambleSerial=PendingGambleSerial;
    Request.Terminal=MenuTerminal.Get(); Request.TerminalGeneration=MenuTerminalGeneration;
    PendingGamblePrice=INDEX_NONE;
    if(!bConfirm) { MenuMessage=TEXT("已取消，没有扣币"); return; }
    WaitingGambleChoice=InspectedGamble; bGambleWaiting=true; MenuMessage=InspectedGamble<7?TEXT("正在抽取"):TEXT("正在结算并保存…"); // 等待抽取回执时也使用统一文案；装配继续保留保存提示，且必须先于同步Listen回执冻结项目。
    GetController()->SubmitGameplayCommand(Request); // 先设等待再提交，Listen本机可能同步回执。
}

bool UDemoMenuFlowComponent::IsGambleAnimationSkipped() const
{
    DEMO_LOG_TICK();
    const auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 引擎拥有的本机配置，不借用服务端状态。
    return Settings&&Settings->IsGambleAnimationSkipped();
}
void UDemoMenuFlowComponent::ToggleGambleAnimation()
{
    DEMO_LOG_CALL();
    if(bStopped||!IsGambleMenuOpen()||HasBlockingOverlay()||IsGambleConfirmOpen()||bGambleWaiting)
    { UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_SKIP rejected stale/modal/wait")); return; }
    auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 仅本地点击生命周期内借用。
    if(!Settings) { UE_LOG(LogFPSDemo,Warning,TEXT("GAMBLE_SKIP missing device settings")); return; }
    Settings->SetGambleAnimationSkipped(!Settings->IsGambleAnimationSkipped());
    if(Settings->IsGambleAnimationSkipped()&&GambleReel.Finish()) MenuMessage=TEXT("抽取完成");
}

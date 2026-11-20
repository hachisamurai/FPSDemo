// 职责：UMG下注输入意图与草稿验证。使用：CommonUI模态消费此状态，Flow不持有Widget或直接切输入模式；正式Casino事务仍校验余额和保存。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Game/DemoGameState.h"
#include "Engine/LocalPlayer.h"
#include "Debug/DemoLog.h"

bool UDemoMenuFlowComponent::CanEditCasinoBet() const
{
    DEMO_LOG_TICK();
    const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 当前World阶段，编辑界面不授予交互权限。
    if(bStopped||!State||State->Phase!=EDemoPhase::Hub||!GetController()->IsLocalController()||!IsGambleMenuOpen()
        ||HasBlockingOverlay()||IsGambleConfirmOpen()||bGambleWaiting||DemoCasino::ModeForChoice(InspectedGamble)<0||CasinoReveal.Mode>=0) return false;
    const auto& View=GetController()->GetMenuSnapshot(); // 同步公开快照，不能编辑已经扣款的货箱。
    if(!MenuTerminal.IsValid()&&(!bCasinoDevicePage||!View.CasinoDeviceToken.IsValid()||View.CasinoDeviceMode!=DemoCasino::ModeForChoice(InspectedGamble))) return false; // 设备独立会话仍允许原十进制输入，提交时服务器重验租约。
    return View.Progress.Gambling.Mine.Stake==0&&View.Progress.Gambling.Arcade.Mode<0&&View.CasinoConfigError.IsEmpty()&&View.TerminalBlockReason.IsEmpty();
}
bool UDemoMenuFlowComponent::IsCasinoBetInputOpen() const { DEMO_LOG_TICK(); return bCasinoBetInputOpen; }
void UDemoMenuFlowComponent::OpenCasinoBetInput()
{
    DEMO_LOG_CALL();
    auto* Local=GetController()->GetLocalPlayer(); // 只挂拥有者Viewport，专服没有Slate。
    if(!CanEditCasinoBet()||bCasinoBetInputOpen||!Local||!Local->ViewportClient)
    { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_INPUT rejected context/viewport")); return; }
    bCasinoBetInputOpen=true; // CommonUI下一次同步建立模态；这里只改变输入意图，不争夺Slate焦点。
}
void UDemoMenuFlowComponent::CloseCasinoBetInput()
{
    DEMO_LOG_CALL();
    bCasinoBetInputOpen=false; // 幂等撤销；CommonUI出栈并恢复底层有效焦点。
}
bool UDemoMenuFlowComponent::CommitCasinoBetText(const FString& Text,FString& Error)
{
    DEMO_LOG_CALL();
    if(!bCasinoBetInputOpen||!CanEditCasinoBet())
    { Error=TEXT("当前页面无法修改下注金额"); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_INPUT rejected stale commit")); return false; }
    if(!CommitCasinoDraftText(Text,Error)) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_INPUT rejected parse/minimum")); return false; } // 页面保原模态门禁，只与实体键盘共用整数解析/草稿赋值。
    CloseCasinoBetInput(); return true;
}

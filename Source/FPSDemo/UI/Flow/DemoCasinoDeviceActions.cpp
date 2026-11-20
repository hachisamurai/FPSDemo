// 职责：实体按钮对原Casino草稿/确认/等待/结果的窄适配；首数字替换预填金额，不伪造全屏菜单、复制概率或直改钱包，只接受实际控件同步输入栈。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "UI/Common/DemoUIManager.h" // 本地非法金额使用原Owner短提示，不靠默认TextRender字体渲染中文。
#include "Debug/DemoLog.h"

bool UDemoMenuFlowComponent::IsCasinoDeviceOperating() const
{
    DEMO_LOG_TICK();
    const UDemoCasinoDeviceInputComponent* Input=GetController()->FindComponentByClass<UDemoCasinoDeviceInputComponent>(); return !bStopped && Input && Input->IsOperating();
}
const FString& UDemoMenuFlowComponent::GetCasinoDeviceAmountText() const
{ DEMO_LOG_TICK(); return CasinoDeviceAmountText; }
void UDemoMenuFlowComponent::BeginCasinoDeviceOperation(const FDemoPlayerMenuSnapshot& Snapshot)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController() || Snapshot.CasinoDeviceMode!=1 || !Snapshot.CasinoControlsRevision) return;
    bMenuOpen=false; bRewardMenu=false; bCasinoDevicePage=false; MenuTerminal.Reset(); MenuTerminalGeneration=0; ResetGamblePresentation(); // 没有隐形CommonUI页，原终端授权已由服务器撤销。
    TerminalPage=EDemoTerminalPage::Gambling; InspectedGamble=DemoCasino::ChoiceForMode(Snapshot.CasinoDeviceMode); InspectedWeapon=INDEX_NONE; PendingAmmoPrice=INDEX_NONE;
    CasinoDraft.Mode=1; CasinoDraft.Operation=0; CasinoDraft.KeepMask=0; CasinoDraft.Stake=FMath::Max(Snapshot.CasinoRules.MinBet,CasinoDraft.Stake);
    CasinoDeviceAmountText=FString::FromInt(CasinoDraft.Stake); bCasinoFirstDigitReplacesAmount=true; MenuMessage=TEXT("左键点击数字，SET BET 确认后左键拉杆下注；Esc 离开设备"); SetMenuInput(false);
}
void UDemoMenuFlowComponent::EndCasinoDeviceOperation()
{ DEMO_LOG_CALL(); ResetGamblePresentation(); CasinoDeviceAmountText.Empty(); bCasinoFirstDigitReplacesAmount=true; InspectedGamble=INDEX_NONE; MenuMessage.Empty(); }
bool UDemoMenuFlowComponent::CommitCasinoDraftText(const FString& Text,FString& Error)
{
    DEMO_LOG_CALL(); int32 Parsed=0; // 解析/最低值通过前不触碰旧草稿，整数溢出/小数均由原ParseStake拒绝。
    if (!DemoCasino::ParseStake(Text,Parsed,Error)) return false;
    const int32 Minimum=GetController()->GetMenuSnapshot().CasinoRules.MinBet;
    if (Parsed<Minimum) { Error=FString::Printf(TEXT("最低下注 %d 金币"),Minimum); return false; }
    CasinoDraft.Stake=Parsed; MenuMessage.Empty(); return true;
}
void UDemoMenuFlowComponent::ConfirmCasinoWager(bool bConfirm)
{
    DEMO_LOG_CALL(); PendingGamblePrice=INDEX_NONE;
    if (!bConfirm) { MenuMessage=TEXT("已取消，没有扣币"); return; }
    SubmitCasino(PendingCasino); // 原字段/等待/序号共享；物理路径由SubmitCasino交PC组件再做可信控件授权。
}
void UDemoMenuFlowComponent::SubmitDeviceAction(const FDemoCasinoControlBinding& Binding)
{
    DEMO_LOG_CALL();
    if (!IsCasinoDeviceOperating() || HasBlockingOverlay() || bMenuOpen || bGambleWaiting) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_ACTION_REJECT context/wait")); return; }
    if (Binding.Action==EDemoCasinoControlAction::Exit)
    { GetController()->FindComponentByClass<UDemoCasinoDeviceInputComponent>()->ExitOperation(); return; }
    if (IsCasinoRolling()) { MenuMessage=TEXT("请等待本次已保存结果揭晓"); return; }
    if (Binding.Action==EDemoCasinoControlAction::AmountDigit)
    {
        if (Binding.Index<0 || Binding.Index>9) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_DIGIT_REJECT range")); return; }
        if (bCasinoFirstDigitReplacesAmount) { CasinoDeviceAmountText.Empty(); bCasinoFirstDigitReplacesAmount=false; } // 不把默认1或上次已确认金额拼成15/旧值+新数字。
        if (CasinoDeviceAmountText.Len()>=10)
        {
            MenuMessage=TEXT("金额最多输入10位，确认时会校验整数范围"); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_DIGIT_REJECT length"));
            if (UDemoUIManager* UI=GetController()->FindComponentByClass<UDemoUIManager>()) UI->PresentCommandResult(EDemoGameplayCommand::Casino,FDemoCommandResult::Reject(TEXT("CasinoAmountTooLong"),MenuMessage));
            return;
        } // 最多int32十位文本，真正数值溢出仍由SET BET原ParseStake拒绝，不增加经济下注上限。
        if (CasinoDeviceAmountText==TEXT("0")) CasinoDeviceAmountText.Empty(); CasinoDeviceAmountText+=FString::FromInt(Binding.Index); CasinoReveal=FDemoCasinoResult(); return;
    }
    if (Binding.Action==EDemoCasinoControlAction::AmountClear) { CasinoDeviceAmountText.Empty(); bCasinoFirstDigitReplacesAmount=false; CasinoReveal=FDemoCasinoResult(); return; }
    if (Binding.Action==EDemoCasinoControlAction::AmountBackspace) { if (!CasinoDeviceAmountText.IsEmpty()) CasinoDeviceAmountText.LeftChopInline(1); bCasinoFirstDigitReplacesAmount=false; CasinoReveal=FDemoCasinoResult(); return; }
    if (Binding.Action==EDemoCasinoControlAction::AmountCommit)
    {
        FString Error; if (!CommitCasinoDraftText(CasinoDeviceAmountText,Error)) { MenuMessage=Error; UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_AMOUNT_REJECT %s"),*Error); if (UDemoUIManager* UI=GetController()->FindComponentByClass<UDemoUIManager>()) UI->PresentCommandResult(EDemoGameplayCommand::Casino,FDemoCommandResult::Reject(TEXT("InvalidCasinoAmount"),Error)); }
        else { CasinoDeviceAmountText=FString::FromInt(CasinoDraft.Stake); bCasinoFirstDigitReplacesAmount=true; MenuMessage=TEXT("金额已设置，对准拉杆左键下注一次"); } return;
    }
    if (Binding.Action==EDemoCasinoControlAction::PullSlots)
    {
        if (CasinoDeviceAmountText!=FString::FromInt(CasinoDraft.Stake))
        {
            MenuMessage=TEXT("请先按SET BET确认当前整数金额"); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_PULL_REJECT amount draft not confirmed"));
            if (UDemoUIManager* UI=GetController()->FindComponentByClass<UDemoUIManager>()) UI->PresentCommandResult(EDemoGameplayCommand::Casino,FDemoCommandResult::Reject(TEXT("CasinoDraftUnconfirmed"),MenuMessage)); return;
        }
        CasinoReveal=FDemoCasinoResult(); CasinoDraft.Mode=1; CasinoDraft.Operation=0;
        RequestCasino(); // 原报价/Serial/RulesKey冻结；不复用有IsGambleMenuOpen前置的字符串热区。
        if (IsGambleConfirmOpen()) ConfirmCasinoWager(true); // 拉杆本次就是明确确认，无需第二次拉杆或第二份费用。
        else if (!MenuMessage.IsEmpty()) if (UDemoUIManager* UI=GetController()->FindComponentByClass<UDemoUIManager>()) UI->PresentCommandResult(EDemoGameplayCommand::Casino,FDemoCommandResult::Reject(TEXT("CasinoQuote"),MenuMessage)); // 未通过本地报价不发RPC，原因沿现Owner短提示。
        return;
    }
    UE_LOG(LogFPSDemo,Log,TEXT("CASINO_DEVICE_ACTION_REJECT unsupported type=%d"),int32(Binding.Action));
}
void UDemoMenuFlowComponent::ReceiveCasinoDeviceResult(const FDemoCommandResult& Result)
{ DEMO_LOG_CALL(); ReceiveGambleResult(Result); } // 原接收函数仍核Waiting/Mode/Serial，失败只文案不揭晓。

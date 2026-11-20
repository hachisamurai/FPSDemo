// 职责：金币娱乐UI草稿、确认和已保存结果揭晓。使用：HUD调用严格热区处理；业务始终通过拥有者PC命令。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h" // 物理Operate沿本人复制组件，服务器再验真实控件，不能走旧无Control提交旁路。

const FDemoCasinoIntent& UDemoMenuFlowComponent::GetCasinoDraft() const { DEMO_LOG_TICK(); return CasinoDraft; }
const FDemoCasinoResult& UDemoMenuFlowComponent::GetCasinoResult() const { DEMO_LOG_TICK(); return CasinoReveal; }
double UDemoMenuFlowComponent::GetCasinoRevealTime() const
{ DEMO_LOG_TICK(); return IsGambleAnimationSkipped()?10.:FPlatformTime::Seconds()-CasinoRevealStarted; }
FDemoCasinoQuote UDemoMenuFlowComponent::GetCasinoQuote() const
{
    DEMO_LOG_TICK();
    const auto& View=GetController()->GetMenuSnapshot(); // 使用拥有者权威公开配置，绝不读本机不同CDO。
    FDemoCasinoQuote Quote; // 非法配置不能先参与数组/除法预览，已有货箱仍直接读取冻结金额。
    if(!View.CasinoConfigError.IsEmpty()) { Quote.Error=View.CasinoConfigError; return Quote; }
    Quote=DemoCasino::Quote(View.CasinoRules,CasinoDraft); // 本机预览；提交后会重新算。
    if(!View.TerminalBlockReason.IsEmpty()) Quote.Error=View.TerminalBlockReason;
    else if((View.Progress.Gambling.Mine.Stake>0||View.Progress.Gambling.Arcade.Mode>=0)) Quote.Error=TEXT("请先完成当前未结的娱乐项目");
    // 钱包最终以服务端为准，Editor无限币也走正式命令，因此本地不擅自禁用无余额测试。
    return Quote;
}
void UDemoMenuFlowComponent::RequestCasino()
{
    DEMO_LOG_CALL();
    const auto Quote=GetCasinoQuote(); // 调用者已检查菜单/模态/等待。
    if(!Quote.Error.IsEmpty()) { MenuMessage=Quote.Error; UE_LOG(LogFPSDemo,Log,TEXT("CASINO_UI rejected %s"),*MenuMessage); return; }
    const auto& View=GetController()->GetMenuSnapshot(); // 同帧冻结参数和规则版本。
    PendingCasino=CasinoDraft; PendingCasino.RulesKey=View.CasinoRules.Fingerprint();
    PendingGamblePrice=CasinoDraft.Stake; PendingGambleSerial=View.Progress.Gambling.Serial;
}
void UDemoMenuFlowComponent::SubmitCasino(const FDemoCasinoIntent& Intent)
{
    DEMO_LOG_CALL();
    PendingCasino=Intent; // 回执判断是否进入训练，仍不缓存服务器结果。
    FDemoGameplayCommandRequest Request; // 客户端不能填写金币收益或随机种子。
    Request.Command=EDemoGameplayCommand::Casino; Request.Casino=Intent; Request.GambleSerial=PendingGambleSerial;
    Request.Terminal=MenuTerminal.Get(); Request.TerminalGeneration=MenuTerminalGeneration;
    if (bCasinoDevicePage) { const FDemoPlayerMenuSnapshot& View=GetController()->GetMenuSnapshot(); /* 只带回Owner批准租约，不改变原金额/Serial/Rules。 */ Request.CasinoDeviceId=View.CasinoDeviceId; Request.CasinoDeviceToken=View.CasinoDeviceToken; Request.HubWorldEpoch=View.HubWorldEpoch; }
    WaitingGambleChoice=DemoCasino::ChoiceForMode(Intent.Mode); bGambleWaiting=true; CasinoReveal=FDemoCasinoResult(); MenuMessage=Intent.Operation==0?TEXT("正在抽取"):TEXT("正在保存货箱…");
    if (IsCasinoDeviceOperating())
    {
        UDemoCasinoDeviceInputComponent* Input=GetController()->FindComponentByClass<UDemoCasinoDeviceInputComponent>();
        if (!Input || !Input->SubmitCasinoControl(Intent,PendingGambleSerial)) { bGambleWaiting=false; MenuMessage=TEXT("请重新对准当前设备操作件"); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_CONTROL_SUBMIT_REJECT local context")); }
        return;
    }
    GetController()->SubmitGameplayCommand(Request);
}
bool UDemoMenuFlowComponent::IsCasinoRolling() const
{ DEMO_LOG_TICK(); const auto& R=GetController()->GetMenuSnapshot().Progress.Gambling; return CasinoReveal.Mode>=0&&R.Mine.Stake==0&&R.Arcade.Mode<0&&GetCasinoRevealTime()<3.; }
bool UDemoMenuFlowComponent::HandleCasinoClick(FName HitBox)
{
    DEMO_LOG_CALL();
    const FString Name=HitBox.ToString(); // 有界本地热区字符串，不当作任意函数名或业务权限。
    if(!Name.StartsWith(TEXT("Casino."))) return false;
    if(IsCasinoBetInputOpen()) { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_UI rejected while editing amount")); return true; }
    const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 当前阶段，旧帧按钮不能在战斗触发。
    if(bStopped||!State||State->Phase!=EDemoPhase::Hub||!GetController()->IsLocalController()||!IsGambleMenuOpen()||HasBlockingOverlay()||IsGambleConfirmOpen()||bGambleWaiting)
    { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_UI rejected context/wait/modal")); return true; }
    const auto& View=GetController()->GetMenuSnapshot(); // 此栈只借用公开账本。
    if(Name.StartsWith(TEXT("Casino.Open.")))
    {
        const FString Tail=Name.Mid(12); // 严格单字符模式，不把空串转为0。
        if(!IsCasinoRolling()&&Tail.Len()==1&&Tail[0]>=TEXT('0')&&Tail[0]<=TEXT('6')) InspectGamble(DemoCasino::ChoiceForMode(Tail[0]-TEXT('0')));
        return true;
    }
    if(Name==TEXT("Casino.List")) { if(bCasinoDevicePage) CloseUpgradeMenu(); else InspectedGamble=INDEX_NONE; return true; }
    if(DemoCasino::ModeForChoice(InspectedGamble)<0) return true;
    const auto& Rules=View.CasinoRules; // 指针不跨提交生命周期。
    if(IsCasinoRolling())
    { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_UI rejected during animation")); return true; }
    if(Name==TEXT("Casino.Back")) { if(bCasinoDevicePage) CloseUpgradeMenu(); else { CasinoReveal=FDemoCasinoResult(); InspectedGamble=INDEX_NONE; } return true; }
    if(Name==TEXT("Casino.Again")) { CasinoReveal=FDemoCasinoResult(); MenuMessage.Empty(); RequestCasino(); return true; }
    if(Name==TEXT("Casino.ClearResult")) { CasinoReveal=FDemoCasinoResult(); MenuMessage.Empty(); return true; }
    // 已结结果面板必须显式返回/继续，过期底层金额和棋盘热区不得修改草稿。
    if(CasinoReveal.Mode>=0&&View.Progress.Gambling.Mine.Stake==0&&View.Progress.Gambling.Arcade.Mode<0) return true;
    if(Name==TEXT("Casino.Place")) { RequestGamble(); return true; }
    if(Name==TEXT("Casino.Cash"))
    {
        if(View.Progress.Gambling.Mine.Stake==0||View.Progress.Gambling.Mine.OpenCount()==0) return true;
        FDemoCasinoIntent Intent=CasinoDraft; Intent.Mode=2; Intent.Operation=2; Intent.Stake=View.Progress.Gambling.Mine.Stake; // 已开箱收益使用冻结值。
        PendingGambleSerial=View.Progress.Gambling.Serial; SubmitCasino(Intent); return true;
    }
    if(Name.StartsWith(TEXT("Casino.Tile.")))
    {
        const FString Suffix=Name.Mid(12); // 仅0..24十进制编号；空/非法后缀拒绝，不默认解释0。
        if(Suffix.IsEmpty()||!Suffix.IsNumeric()||Suffix.Len()>2||View.Progress.Gambling.Mine.Stake==0) return true;
        const int32 Tile=FCString::Atoi(*Suffix); // 解析后范围/重复二次校验。
        if(Tile<0||Tile>24||(View.Progress.Gambling.Mine.OpenMask&(1<<Tile))) return true;
        FDemoCasinoIntent Intent=CasinoDraft; Intent.Mode=2; Intent.Operation=1; Intent.Tile=Tile; Intent.Stake=View.Progress.Gambling.Mine.Stake;
        PendingGambleSerial=View.Progress.Gambling.Serial; SubmitCasino(Intent); return true;
    }
    const auto& Arcade=View.Progress.Gambling.Arcade; // 唯一持久局面，仅本模式允许继续。
    if(Arcade.Mode>=0&&Arcade.Mode==CasinoDraft.Mode)
    {
        if(Arcade.Mode==4&&Name.StartsWith(TEXT("Casino.Keep.")))
        {
            const FString Tail=Name.Mid(12); // 5个骰位，仅切换本地保留意图。
            if(Tail.Len()==1&&Tail[0]>=TEXT('0')&&Tail[0]<=TEXT('4')) CasinoDraft.KeepMask^=1<<(Tail[0]-TEXT('0'));
            return true;
        }
        if(Name==TEXT("Casino.Advance")||Name==TEXT("Casino.Collect")||Name==TEXT("Casino.Range"))
        {
            FDemoCasinoIntent Intent=CasinoDraft; // 不接受前端骰面、积分或概率。
            Intent.Stake=Arcade.Stake; Intent.Operation=Name==TEXT("Casino.Advance")?1:Name==TEXT("Casino.Range")?3:2;
            PendingGambleSerial=View.Progress.Gambling.Serial; SubmitCasino(Intent); return true;
        }
    }
    if(Name==TEXT("Casino.Side.0")||Name==TEXT("Casino.Side.1")) { if(Arcade.Mode<0) CasinoDraft.Selection=Name.EndsWith(TEXT("0"))?0:1; return true; }
    if((View.Progress.Gambling.Mine.Stake>0||Arcade.Mode>=0)) return true; // 待结箱不可改价或难度，也不能另开局。
    if(!View.CasinoConfigError.IsEmpty()) { MenuMessage=View.CasinoConfigError; return true; } // 非法步长或上下限不能参与本机整数运算。
    if(Name==TEXT("Casino.BetInput")) { OpenCasinoBetInput(); return true; }
    int64 Amount=CasinoDraft.Stake; // 中间计算宽于存储类型；极大金额/配置步长不得有符号溢出。
    if(Name==TEXT("Casino.BetDown")) Amount-=Rules.BetStep;
    else if(Name==TEXT("Casino.BetUp")) Amount+=Rules.BetStep;
    else if(Name==TEXT("Casino.BetDown10")) Amount-=10ll*Rules.BetStep;
    else if(Name==TEXT("Casino.BetUp10")) Amount+=10ll*Rules.BetStep;
    else if(Name==TEXT("Casino.Small")) { CasinoDraft.Selection=0; CasinoDraft.Low=0; CasinoDraft.High=49; }
    else if(Name==TEXT("Casino.Big")) { CasinoDraft.Selection=0; CasinoDraft.Low=50; CasinoDraft.High=99; }
    else if(Name==TEXT("Casino.Odd")) CasinoDraft.Selection=1;
    else if(Name==TEXT("Casino.Even")) CasinoDraft.Selection=2;
    else if(Name==TEXT("Casino.LowDown")) { CasinoDraft.Selection=0; CasinoDraft.Low=FMath::Max(FMath::Max(0,CasinoDraft.High-49),CasinoDraft.Low-1); }
    else if(Name==TEXT("Casino.LowUp")) { CasinoDraft.Selection=0; CasinoDraft.Low=FMath::Min(CasinoDraft.High,CasinoDraft.Low+1); }
    else if(Name==TEXT("Casino.HighDown")) { CasinoDraft.Selection=0; CasinoDraft.High=FMath::Max(CasinoDraft.Low,CasinoDraft.High-1); }
    else if(Name==TEXT("Casino.HighUp")) { CasinoDraft.Selection=0; CasinoDraft.High=FMath::Min(FMath::Min(99,CasinoDraft.Low+49),CasinoDraft.High+1); }
    else
    {
        // 捕获Name仅本同步调用；Prefix限定UI白名单，返回-1拒绝非法/缺失编号，无异步生命周期。
        const auto IndexFor=[&Name](const TCHAR* Prefix,int32 Count)->int32
        { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CasinoUI.IndexFor")); const FString Start(Prefix),Tail=Name.Mid(Start.Len()); /* 当前前缀/有界后缀。 */ if(!Name.StartsWith(Start)||Tail.IsEmpty()||!Tail.IsNumeric()||Tail.Len()>2) return INDEX_NONE; const int32 Value=FCString::Atoi(*Tail); /* 本地索引。 */ return Value>=0&&Value<Count?Value:INDEX_NONE; };
        int32 Index=IndexFor(TEXT("Casino.Preset."),Rules.Presets.Num()); // 只接受真实数组范围。
        if(Index!=INDEX_NONE) Amount=Rules.Presets[Index];
        else if((Index=IndexFor(TEXT("Casino.Number."),100))!=INDEX_NONE) { CasinoDraft.Selection=0; CasinoDraft.Low=CasinoDraft.High=Index; }
        else if((Index=IndexFor(TEXT("Casino.Bombs."),Rules.BombChoices.Num()))!=INDEX_NONE) CasinoDraft.Bombs=Rules.BombChoices[Index];
        else { UE_LOG(LogFPSDemo,Log,TEXT("CASINO_UI unknown action")); return true; }
    }
    // 仅保护存储范围；切到单号时保留原金额，不存在玩法上限。
    CasinoDraft.Stake=int32(FMath::Clamp<int64>(Amount,Rules.MinBet,MAX_int32)); MenuMessage.Empty(); return true;
}

// 职责：终端抽取Canvas界面。使用：从MenuFlow和服务器投影绘制报价、勾选偏好、结果及续抽热区，不推算中奖。
#include "UI/DemoHUD.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "UI/DemoUIStyle.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "Engine/Canvas.h" // 实际UI剪裁，不创建世界Actor或转动奖励模型。
#include "CanvasTypes.h"
void ADemoHUD::DrawGambleText(const FString& Text,float X,float Y,int32 Lines)
{
    DEMO_LOG_TICK();
    TArray<FString> Paragraphs,Wrapped; // 本帧临时段落/行；金币报价按明确换行分开金额与情报，长天赋说明仍自动换行。
    Text.ParseIntoArrayLines(Paragraphs,false);
    for(const FString& Paragraph:Paragraphs) // 22字符上限仍匹配350px列，不能把换行符直接交给单行Label。
        for(int32 Offset=0;Offset<Paragraph.Len();Offset+=22) Wrapped.Add(Paragraph.Mid(Offset,22));
    for(int32 Row=0;Row<Lines&&Row<Wrapped.Num();++Row) // 超出可见高度才省略最后一行。
        Label(Wrapped[Row]+(Row==Lines-1&&Wrapped.Num()>Lines?TEXT("…"):TEXT("")),X,Y+Row*19,14,DemoUI::Muted);
}
void ADemoHUD::DrawGambleMenu(ADemoPlayerController* PC,const ADemoGameState* State)
{
    DEMO_LOG_TICK();
    const auto& View=PC->GetMenuSnapshot(); // 本人权威报价/已拥有奖励，不读本地随机数。
    const auto* Flow=PC->GetMenuFlow(); // PC拥有的本地页面，只借用当前帧。
    const float X=ViewWidth/2-480,Y=ViewHeight/2-240; // 延续终端960x480设计坐标。
    const bool Hub=State->Phase==EDemoPhase::Hub,Confirm=Flow->IsGambleConfirmOpen(),Waiting=Flow->IsGambleWaiting(); // UI只限制热区，服务器另校验。
    if(Hub&&DemoCasino::ModeForChoice(Flow->GetInspectedGamble())>=0) { DrawCasinoMenu(PC); return; }
    Panel(X,Y,960,480,GetPanelSurface(),12);
    if(!Flow->GetGambleReel().Cards.IsEmpty()) { DrawGambleReel(PC,X,Y); return; } // 动画/结果页不绘制底层购买热区。
    if(Confirm)
    {
        Label(TEXT("确认本次操作"),X+480,Y+75,28,DemoUI::Ink,true);
        const int32 Choice=Flow->GetInspectedGamble(); // 确认时锁定查看项，页签无热区。
        if(View.GambleOffers.IsValidIndex(Choice))
        {
            const auto& Offer=View.GambleOffers[Choice]; // 冻结序号仍由服务器提交重验。
            Label(Offer.Title,X+480,Y+132,24,DemoUI::Gold,true);
            Label(FString::Printf(TEXT("费用 %d %s · 成功率 %.1f%%"),Offer.Cost,Hub?TEXT("金币"):TEXT("银币"),Offer.Chance*100),X+480,Y+180,18,DemoUI::Ink,true);
            if(Choice>=4&&Choice<7) DrawGambleText(Offer.Detail,X+310,Y+214,5); // 金币确认明确展示含本金到账/失败返还，不用泛化的全损提示。
            else Label(Choice>=7?(Offer.Cost>0?TEXT("确认后永久购买并装配，保存失败会全额退回。"):TEXT("更换遗物不会重置本轮已消耗的救援次数。")):TEXT("成功获得奖励；失败仅扣本次费用，已有成长不受影响。"),X+480,Y+223,16,DemoUI::Muted,true);
        }
        Button(TEXT("GambleYes"),TEXT("确认"),X+260,Y+320,200,44);
        Button(TEXT("GambleNo"),TEXT("取消"),X+500,Y+320,200,44,true,true); return;
    }
    Label(Hub?TEXT("GOLD ARCADE / 金币娱乐"):TEXT("RISK UPGRADE / 冒险强化"),X+32,Y+25,13,DemoUI::Accent);
    Label(Hub?TEXT("金币娱乐与遗物商店"):TEXT("本局冒险强化"),X+32,Y+53,28,DemoUI::Ink);
    Button(TEXT("TerminalStats"),TEXT("属性升级"),X+430,Y+25,120,36,!Waiting,true);
    Button(TEXT("TerminalWeapons"),TEXT("武器"),X+560,Y+25,100,36,!Waiting,true);
    if(Hub) Button(TEXT("TerminalAmmo"),TEXT("弹药类型"),X+670,Y+25,130,36,!Waiting,true);
    Button(TEXT("Close"),TEXT("× 关闭"),X+822,Y+25,106,36,!Waiting,true);
    // 次数不限；金币入口进入独立下注页，不能将列表入口费用误作固定下注额。
    Label(FString::Printf(TEXT("%s %d  ·  %s"),Hub?TEXT("金币"):TEXT("银币"),Hub?View.Progress.Coins:View.Progress.SilverCoins,Hub?TEXT("七种玩法 / 明示赔率 / 不限次数"):TEXT("当轮有效 / 不限次数 / 重复升阶")),X+32,Y+95,14,DemoUI::Gold);
    Button(TEXT("GambleSkipAnimation"),Flow->IsGambleAnimationSkipped()?TEXT("[√] 跳过动画"):TEXT("[ ] 跳过动画"),X+735,Y+83,193,30,!Waiting,true); // 单独本地勾选热区，不与余额行重叠。
    const int32 First=0,Count=Hub?12:4; // 安全区娱乐3项+卸下+四遗物购买/装配；关间四项。
    for(int32 Row=0;Row<Count;++Row) // 固定最多8行，页内不滚动。
    {
        const int32 Choices[]={4,5,6,12,13,14,15,7,8,9,10,11}; // 左七玩法、右五遗物，保留旧业务ID。
        const int32 Choice=Hub?Choices[Row]:Row; // 和服务白名单一一对应。
        if(!View.GambleOffers.IsValidIndex(Choice)) continue;
        const auto& Offer=View.GambleOffers[Choice]; // 行标题包含真实费用和概率。
        FString Text=Choice<4?FString::Printf(TEXT("%s  ·  %d  ·  %.1f%%"),*Offer.Title,Offer.Cost,Offer.Chance*100):Offer.Title; // 列表显示实际概率，2.5个百分点情报不被整数取整掩盖。
        if(Choice>=8&&Choice<=11) Text+=!View.Progress.Gambling.Relics.Contains(DemoGamble::Relic(Choice-8))?FString::Printf(TEXT("  [%d金币购买]"),Offer.Cost):View.Progress.Gambling.EquippedRelic==DemoGamble::Relic(Choice-8)?FString(TEXT("  [已装配]")):FString(TEXT("  [免费装配]"));
        if(Hub&&Row>=7&&Flow->GetInspectedGamble()>=0) continue;
        const FName Hit=DemoCasino::ModeForChoice(Choice)>=0?FName(*FString::Printf(TEXT("Casino.Open.%d"),DemoCasino::ModeForChoice(Choice))):FName(*FString::Printf(TEXT("Gamble%d"),Choice)); // 新入口不扩展固定UI动作枚举。
        Button(Hit,Text,X+(Hub&&Row>=7?545:32),Y+122+(Hub&&Row>=7?Row-7:Row)*37,Hub&&Row>=7?350:470,32,!Waiting,Flow->GetInspectedGamble()!=Choice);
    }
    const int32 Selected=Flow->GetInspectedGamble(); // 详情和按钮基于同一项。
    if(View.GambleOffers.IsValidIndex(Selected))
    {
        const auto& Offer=View.GambleOffers[Selected]; // 远端已裁定的效果与奖池。
        Label(Offer.Title,X+545,Y+130,23,DemoUI::Ink);
        if(Hub) Button(TEXT("Casino.List"),TEXT("遗物列表"),X+770,Y+95,125,30,!Waiting,true);
        if(Selected==3&&!Offer.Rewards.IsEmpty()) // 扩展奖池逐项显示完整说明，避免长串截断隐藏候选收益。
        {
            const int32 Page=Flow->GetGambleRewardPage(); // Flow已按最新权威奖池钳制。
            const FDemoGambleReward& Reward=Offer.Rewards[Page]; // 当前页本帧借用，不替代滚动带的完整池。
            Label(FString::Printf(TEXT("奖池预览 %d / %d"),Page+1,Offer.Rewards.Num()),X+545,Y+172,14,DemoUI::Muted);
            Label(Reward.Title,X+545,Y+200,20,DemoUI::Gold);
            DrawGambleText(Reward.Id==TEXT("no_reward")?TEXT("本次未获得奖励，仅消耗本次费用。"):Reward.Description,X+545,Y+231,3);
            Button(TEXT("GamblePoolPrevious"),TEXT("上一项"),X+545,Y+292,160,30,!Waiting&&Page>0,true);
            Button(TEXT("GamblePoolNext"),TEXT("下一项"),X+735,Y+292,160,30,!Waiting&&Page+1<Offer.Rewards.Num(),true);
        }
        else DrawGambleText(Offer.Detail,X+545,Y+173,8);
        DrawGambleText(Offer.BlockReason,X+545,Y+330,2);
        Button(TEXT("GambleAction"),Selected<7?(Hub?TEXT("投入金币并确认"):TEXT("下注并确认")):(Offer.Cost>0?TEXT("购买并装配"):TEXT("装配并保存")),X+545,Y+380,350,36,!Waiting&&Offer.BlockReason.IsEmpty());
    }
    else if(!Hub)
    {
        DrawGambleText(Hub?TEXT("选择数字竞猜、军械老虎机或拆弹货箱，自定金币下注。命中获得金币，倍率包含本金；未结货箱关闭后可继续。遗物明码直购，弹药在上方页签购买。"):TEXT("先选择属性强化或天赋抽取。费用和概率在确认前展示。天赋奖池匹配当前弹药类型。"),X+545,Y+145,8);
        FString Owned=TEXT("当前天赋："); // 本人已拥有列表，重连后仍可见。
        for(FName Id:View.Progress.Gambling.Talents) Owned+=FString::Printf(TEXT("%s Lv.%d "),*DemoGamble::Name(Id),View.Progress.Gambling.Rank(Id)); // 列表摘要带等级；Tab分页可看全部效果。
        Owned+=TEXT("；遗物：")+(View.Progress.Gambling.EquippedRelic.IsNone()?FString(TEXT("无")):DemoGamble::Name(View.Progress.Gambling.EquippedRelic));
        DrawGambleText(Owned,X+545,Y+300,5);
    }
    const FString Message=PC->GetMenuMessage().IsEmpty()?View.Progress.Gambling.LastResult:PC->GetMenuMessage(); // 成功回执仅在持久化后出现。
    Label(Message.Left(60),X+32,Y+440,14,DemoUI::Gold);
}

void ADemoHUD::DrawGambleReel(ADemoPlayerController* PC,float X,float Y)
{
    DEMO_LOG_TICK();
    const FDemoGambleReel& Reel=PC->GetMenuFlow()->GetGambleReel(); // 本帧只读值，不参与结果计算。
    Button(TEXT("GambleSkipAnimation"),PC->GetMenuFlow()->IsGambleAnimationSkipped()?TEXT("[√] 跳过动画"):TEXT("[ ] 跳过动画"),X+618,Y+25,190,36,true,true); // 勾选可立即结束当前已提交动画。
    Label(TEXT("REWARD REVEAL / 抽取结果"),X+32,Y+25,13,DemoUI::Accent);
    // 状态标题只表达玩家看到的抽取阶段；成功/未中奖细节仍在下方结果区展示。
    Label(Reel.bRolling?TEXT("正在抽取"):TEXT("抽取完成"),X+480,Y+68,28,DemoUI::Ink,true);
    Label(TEXT("以下为本次全部可能结果 · 卡片展示频次不代表中奖概率"),X+480,Y+118,14,DemoUI::Muted,true);
    const float Left=X+32,Right=X+928,Center=X+480,Top=Y+167,Pitch=184.f; // 设计像素，滚动位移按统一UIScale变换。
    const int32 Middle=FMath::FloorToInt(Reel.Position); // 附近7张足以覆盖896px窗口；不为整条序列创建Widget。
    Canvas->Canvas->Flush_GameThread(); // 分批边界确保剪裁只影响本条奖励带，之后还原默认无剪裁。
    Canvas->Canvas->SetRenderTargetScissorRect(FIntRect(FMath::RoundToInt(Left*UIScale),FMath::RoundToInt(Top*UIScale),FMath::RoundToInt(Right*UIScale),FMath::RoundToInt((Top+150)*UIScale)));
    for(int32 Slot=Middle-3;Slot<=Middle+3;++Slot) // 槽位可以为负，循环索引归一化后只访问服务器奖池。
    {
        const int32 Index=(Slot%Reel.Cards.Num()+Reel.Cards.Num())%Reel.Cards.Num(); // 纯展示取模，不随机抽选。
        const auto& Card=Reel.Cards[Index]; // 本帧借用冻结卡片。
        const float CardX=Center+(Slot-Reel.Position)*Pitch-86; // 172px卡片+12px间隔。
        const bool Selected=!Reel.bRolling&&Slot==Reel.Target; // 只在停稳后高亮唯一中心卡。
        const FLinearColor Accent=Card.Id==TEXT("no_reward")?DemoUI::Muted:DemoUI::Gold; // 失败卡保持中性，无伪装奖励。
        Panel(CardX,Top+8,172,134,Selected?DemoUI::Selected:DemoUI::Raised,8);
        Panel(CardX+12,Top+18,148,3,Accent,0);
        Label(Card.Id==TEXT("no_reward")?TEXT("—"):TEXT("◆"),CardX+86,Top+37,28,Accent,true);
        Label(Card.Title,CardX+86,Top+94,16,Selected?DemoUI::Ink:Accent,true);
    }
    Canvas->Canvas->Flush_GameThread();
    Canvas->Canvas->SetRenderTargetScissorRect(FIntRect()); // 不泄漏到其他HUD、热区或下一帧。
    Panel(Center-1,Top-10,2,170,DemoUI::Gold,0);
    Label(TEXT("▼"),Center,Top-23,17,DemoUI::Gold,true);
    Label(Reel.bRolling?TEXT("正在抽取"):Reel.ResultMessage,Center,Y+350,18,DemoUI::Gold,true); // 滚动时不提前提示完成；停稳后保留真实奖励或失败详情。
    if(Reel.bRolling) Button(TEXT("GambleNo"),TEXT("正在抽取"),Center-110,Y+397,220,42,false); // 禁用按钮沿用同一阶段文案，避免出现“正在揭晓”。
    else
    {
        const auto* Flow=PC->GetMenuFlow(); // 只借用当前帧的本地页面，业务仍由服务器验证。
        const auto& View=PC->GetMenuSnapshot(); // 下一次报价，不是上一次结果携带的冻结奖池。
        const int32 Choice=Flow->GetInspectedGamble(); // 保持当前抽取方向；返回列表才可换项目。
        const FString BlockReason=Flow->GetContinueGambleBlockReason(); // 禁用时保留结果并说明余额/奖池限制。
        const FString ContinueText=View.GambleOffers.IsValidIndex(Choice)
            ?FString::Printf(TEXT("继续抽取 · %d %s"),View.GambleOffers[Choice].Cost,Choice<4?TEXT("银币"):TEXT("金币")):FString(TEXT("继续抽取")); // 单次明确付费按钮，点击即可提交新交易。
        if(!BlockReason.IsEmpty()) Label(BlockReason.Left(48),Center,Y+376,14,DemoUI::Muted,true);
        Button(TEXT("GambleContinue"),ContinueText,Center-270,Y+403,320,42,BlockReason.IsEmpty());
        Button(TEXT("GambleNo"),TEXT("返回列表"),Center+70,Y+403,200,42,true,true);
    }
    Button(TEXT("Close"),TEXT("× 关闭"),X+822,Y+25,106,36,true,true); // 允许离开，已保存的奖励不会撤销或重新抽取。
}

// 职责：七种金币娱乐的R04分步说明与交易/存档边界。使用：Casino.Help打开，UI.Back只退出本地说明，不改变草稿、报价或局面。
#include "UI/Screens/DemoCasinoScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIStyle.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Debug/DemoLog.h"
void UDemoCasinoScreen::ToggleHelp() { DEMO_LOG_CALL(); bHelpOpen=!bHelpOpen; }
bool UDemoCasinoScreen::IsHelpOpen() const { DEMO_LOG_TICK(); return bHelpOpen; }
void UDemoCasinoScreen::BuildHelp()
{
    DEMO_LOG_CALL();
    SetHeading(TEXT("玩法指南"),DemoCasino::ModeName(Flow()->GetCasinoDraft().Mode)+TEXT(" / 步骤、风险与返回规则"));
    AddBack(TEXT("UI.Back"),TEXT("← 返回本玩法"));
    AddText(PageBody,TEXT("HOW TO PLAY / STEP BY STEP"),16,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(16,TEXT("Medium")));
    FString Guide; // 当前模式的完整规则，下面按既有换行拆为编号步骤，不删减胜负或恢复边界。
    switch(Flow()->GetCasinoDraft().Mode)
    {
    case 0: Guide=TEXT("① 选择小(00–49)、大(50–99)、奇数、偶数，或用起点/终点调整闭区间；点击下方单个数字则只猜这个数。\n② 输入金额，查看右侧命中概率、倍率、到账与净收益，然后确认下注。\n③ 系统按配置权重抽出00–99中的一个数，落在你选中的高亮集合内即中奖，否则到账0。数字并不一定等概率，悬停可查单号权重。\n④ 结果只结算一次；滚动动画不影响结果。结束后可继续同方案下注，或返回列表换玩法。"); break;
    case 1: Guide=TEXT("① 设置金额并确认。三个转轴分别独立抽取一个符号。\n② 三个相同：按该符号三连倍率结算；恰好两个相同：按一对倍率结算；三个不同：到账0。只结算符合的一个组合，不累加。\n③ 左侧表中的概率是单个转轴出现该符号的概率，不是三连中奖率。不同符号按配置权重出现。\n④ 子弹图标仅代表转轴符号，中奖只给金币，不解锁弹药。结束后可继续抽取，没有次数限制。"); break;
    case 2: Guide=TEXT("① 选择雷数与金额，确认后生成一箱25格的货箱。雷位在开箱时确定并保存，客户端看不到未揭雷位。\n② 点击未打开的格子：安全则增加可领取金额，并可继续翻下一格；踩雷则本箱结束，未领取收益归零。\n③ 至少翻开一个安全格后，可以按‘领取金币并结束’，将当前可领金额转入钱包。下一格的安全率和成功后金额显示在报价栏。\n④ 关闭页面只是暂时离开；重新打开或读取检查点仍是同一箱，投入、雷位与已开格不会重抽。未结箱时不能再开其他项目。"); break;
    case 3: Guide=TEXT("① 设置金额并确认开局，初始尚未充能，没有可领取奖金。\n② 每按一次‘继续充能’只尝试下一级，使用列表显示的该级成功率；它是本次条件概率，不是从起点连续闯到本级的概率。\n③ 成功后可以领取当前金额并结束，或继续冒险。失败(过载)立即结束，当前未领取金额归零。通过最后一级会自动到账。\n④ 各级成功率及可领金额在开局时冻结；关闭页面和读档不会重置进度。没有额外的续充费用。"); break;
    case 4: Guide=TEXT("① 设置金额并确认，得到五枚1–6点战术骰。\n② 点击想留下的骰子，使其标记为‘保留’；其余骰子在唯一一次重掷时重新投掷，保留的点数不变。\n③ 可以直接领取当前组合；也可以重掷未保留骰，重掷后自动按最终组合结算，没有第二次机会。\n④ 顺子是1、2、3、4、5或2、3、4、5、6；葫芦是三枚同点+另一对；四同/五同分别是四/五枚相同。一对、两对、三同按相同点数分组。只结算一个最高组合，具体到账见冻结表。\n⑤ 关页不会重掷；骰面、重掷机会和金额已保存。"); break;
    case 5: Guide=TEXT("① 比较A、B两方卡片中的胜率、倍率和金额，选择支持的一方后下注。\n② 服务器按公开战斗模型随机决定先手，再交替攻击直至一方生命归零；生命、伤害与权重由配置决定。\n③ 支持方获胜则按其报价到账，否则到账0。屏幕上的生命条和动作是已完成战斗的回放，点击/跳过动画不会改变胜者。\n④ 胜率由实际有限生命战斗模型计算，历史输赢不会暗中改变本次概率。每次再次下注都是新的一场。"); break;
    case 6: Guide=TEXT("① 确认下注后立即开始计时，并传送到与安全区隔离的专用靶场；不用切换地图，队友仍留在原场景。\n② 在射击线后用鼠标瞄准、左键单发。每次射击消耗1发训练子弹，命中身体得1分，命中红色头部得2分；命中后换下一个靶，最多10靶、20分。脱靶不加分，也消耗次数。\n③ 击倒10靶、训练弹耗尽或真实时间到期都会结算。取你达到的最高积分档，只支付该档金额，不叠加所有档。规则表可查看每档倍率/冻结到账。\n④ 不消耗武器弹匣，枪械/弹药天赋不改变训练计分。单人暂停菜单也不暂停真实时限。保存成功后展示结果3秒，再自动传送回入场位置。\n⑤ 退出或读档不会重置截止时间和成绩；未完成挑战可在终端继续。保存失败保留当前局面并重试，不提前发奖。"); break;
    default: Guide=TEXT("请选择有效玩法。"); UE_LOG(LogFPSDemo,Log,TEXT("CASINO_HELP invalid selected mode")); break;
    }
    TArray<FString> Steps; Guide.ParseIntoArrayLines(Steps,false); // 只拆作者换行，不截断长中文规则。
    for(int32 Index=0;Index<Steps.Num();++Index)
    {
        UVerticalBox* Card=AddCard(PageBody); UHorizontalBox* Row=AddRow(Card); // 每步使用独立留白，规则长时由主正文滚动而不是裁掉。
        UTextBlock* Number=AddText(Row,FString::Printf(TEXT("%02d"),Index+1),32,DemoUI::Gold); Number->SetFont(DemoUI::NumberFont(32));
        if(auto* NumberSlot=Cast<UHorizontalBoxSlot>(Number->Slot)) NumberSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); // 序号不与规则平分宽度。
        FString Text=Steps[Index]; // 移除已有圈号，避免视觉序号重复；规则正文原样保留。
        if(!Text.IsEmpty()&&Text[0]>=0x2460&&Text[0]<=0x2473) Text.RightChopInline(1); Text.TrimStartInline();
        AddText(Row,Text,18,DemoUI::Ink);
    }
    UVerticalBox* Rules=ShowAside(); // 固定侧栏持续提供价格、风险与保存限制，正文步骤滚动时仍能核对。
    AddText(Rules,TEXT("BEFORE YOU COMMIT"),14,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(14,TEXT("Medium")));
    AddText(Rules,TEXT("投入与到账"),23,DemoUI::Ink); AddCasinoRule(Rules);
    AddText(Rules,TEXT("在安全区使用金币。输入正整数金额，查看实际报价，再确认下注。总到账包含本金，按投入×倍率向下取整；净收益=到账−实际投入。"),17,DemoUI::Muted);
    AddText(Rules,TEXT("没有普通下注或奖金玩法上限；余额不足、整数溢出与非法配置仍会拒绝操作。概率和金额以当前公开报价或已保存合同为准。"),16,DemoUI::Muted);
    AddCasinoRule(Rules); AddText(Rules,TEXT("保存与离开"),21,DemoUI::Ink);
    AddText(Rules,TEXT("服务器先保存结果，再显示揭示动画。保存失败撤销本次操作并提供重试。返回先退出说明或确认，再回玩法列表。"),16,DemoUI::Muted);
    AddText(Rules,TEXT("关闭持续局面不等于领取，也不会退还投入；重新打开或读档沿用同一局面。跳过动画只影响表现，不改变结果。"),16,DemoUI::Muted);
    AddText(PageFooter?static_cast<UPanelWidget*>(PageFooter.Get()):PageBody.Get(),TEXT("Esc / Tab 返回本玩法 · 此页不提交下注"),16,DemoUI::Muted);
}

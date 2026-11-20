// 职责：UMG金币娱乐/遗物入口、银币冒险报价与奖励带。使用：只展示已保存回执，按钮沿用原Flow白名单。
// 动画边界：七个真实UMG卡片循环展示完整冻结奖池；位移读取FDemoGambleReel，不生成随机数、不扣费、不发奖。
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIStyle.h"
#include "UI/DemoUIPresentation.h" // 本机静态表现与原赌场Skip独立。
#include "UI/Common/DemoMenuButton.h"
#include "Player/DemoPlayerController.h"
#include "Game/DemoGameState.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Debug/DemoLog.h"

void UDemoTerminalScreen::BuildGamble(const ADemoGameState* State)
{
    DEMO_LOG_CALL();
    if(UsesR02Presentation()) { BuildR02Gamble(State); return; } // 2026-11-20与武器/弹药/核心页统一R02编辑式外观；下方R04布局仅作非R02兼容。
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 仅本人收到的公开商品/赔率，隐藏雷位不参与任何展示。
    const UDemoMenuFlowComponent* Menu=Flow(); // 选择及等待状态属于原Flow。
    const bool Hub=State->Phase==EDemoPhase::Hub,Waiting=Menu->IsGambleWaiting(); // 币种/按钮表现不改变服务端阶段授权。
    const int32 Selected=Menu->GetInspectedGamble(); // 查看项不自动购买、装备或下注。
    SetHeading(Hub?TEXT("安全区娱乐"):TEXT("本轮冒险强化"),Hub?TEXT("七种金币玩法 · 本人独立结算 · 遗物明码直购"):TEXT("使用银币 · 本轮成长 · 同种天赋重复获得会升阶"));
    AddHeaderAction(TEXT("GambleSkipAnimation"),Menu->IsGambleAnimationSkipped()?TEXT("✓ 跳过揭示"):TEXT("跳过揭示"),!Waiting,150.f); // 与返回并排，目录不单占导航行；释放正文高度而不缩字号或裁掉第七项目描述。
    UVerticalBox* Details=ShowAside(); // 固定右栏只展示详情/遗物，不随七玩法目录滚走。
    AddText(PageBody,Hub?TEXT("GOLD ARCADE / CHOOSE AN EXPERIENCE"):TEXT("FIELD UPGRADES / SILVER COINS"),16,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(16,TEXT("Medium")));
    const int32 Choices[]={4,5,6,12,13,14,15}; // 既有协议映射：旧遗物7..11仍保留。
    const TCHAR* Descriptions[]={TEXT("数字区间 / 奇偶 / 单号"),TEXT("三轴符号 / 公开组合赔率"),TEXT("揭开安全格 / 随时领取"),TEXT("逐级充能 / 权衡领取"),TEXT("保留骰面 / 一次重掷"),TEXT("选择支持方 / 观看回放"),TEXT("真实瞄准 / 十靶挑战")}; // 机制标签，不含本地伪造的奖励数值。
    if(Hub)
    {
        UUniformGridPanel* Directory=WidgetTree->ConstructWidget<UUniformGridPanel>(); Directory->SetSlotPadding(FMargin(7.f)); PageBody->AddChild(Directory); // 2列×4行，七个入口全部保留真实CommonUI按钮。
        for(int32 Row=0;Row<UE_ARRAY_COUNT(Choices);++Row)
        {
            const int32 Choice=Choices[Row]; // 同一索引贯穿权威Offer、显示与原Mode白名单。
            if(!View.GambleOffers.IsValidIndex(Choice)) continue;
            const auto& Offer=View.GambleOffers[Choice];
            UDemoMenuButton* Entry=AddAction(Directory,FName(*FString::Printf(TEXT("Casino.Open.%d"),DemoCasino::ModeForChoice(Choice))),FString::Printf(TEXT("%02d  %s\n%s"),Row+1,*Offer.Title,Descriptions[Row]),!Waiting,Selected==Choice);
            Entry->SetVisualRole(EDemoButtonVisualRole::Catalog); Entry->SetMinDimensions(250.f,76.f);
            if(auto* GridSlot=Cast<UUniformGridSlot>(Entry->Slot)) { GridSlot->SetRow(Row/2); GridSlot->SetColumn(Row%2); }
        }
        if(View.Progress.Gambling.Mine.Stake>0||View.Progress.Gambling.Arcade.Mode>=0)
            AddText(PageBody,TEXT("有已保存的未结项目。打开对应玩法即可继续；关闭页面不会重抽或退还投入。"),16,DemoUI::Gold);
        else AddText(PageBody,TEXT("选择玩法后设置金额与选项，确认前核对实际概率和到账；历史结果不改变下一局概率。"),16,DemoUI::Muted);
    }
    else
    {
        UVerticalBox* Directions=AddProgressionCard(PageBody,TEXT("CHOOSE A DIRECTION")); // 银币强化与金币玩法视觉区分，仍使用原四个方向。
        for(int32 Choice=0;Choice<4;++Choice)
        {
            if(!View.GambleOffers.IsValidIndex(Choice)) continue;
            const auto& Offer=View.GambleOffers[Choice]; // 每次保存后服务端生成的新费用/概率。
            AddAction(Directions,FName(*FString::Printf(TEXT("Gamble%d"),Choice)),FString::Printf(TEXT("%s  /  %d 银币  /  %.1f%%"),*Offer.Title,Offer.Cost,Offer.Chance*100),!Waiting,Selected==Choice)->SetVisualRole(EDemoButtonVisualRole::Catalog);
        }
        AddText(PageBody,TEXT("强化与天赋属于本轮成长；通关、团灭或主动放弃后结束。金币与永久解锁不受影响。"),16,DemoUI::Muted);
    }
    if(Hub&&Selected<0)
    {
        AddText(Details,TEXT("PERMANENT RELICS"),14,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(14,TEXT("Medium")));
        AddText(Details,TEXT("永久遗物"),25,DemoUI::Ink); AddProgressionRule(Details);
        for(int32 Choice=7;Choice<=11;++Choice)
        {
            if(!View.GambleOffers.IsValidIndex(Choice)) continue;
            const auto& Offer=View.GambleOffers[Choice]; // 点击只查看：真实购买/装配依旧需要原确认。
            const FName Relic=Choice>=8?DemoGamble::Relic(Choice-8):NAME_None;
            const bool Equipped=Choice>=8&&View.Progress.Gambling.EquippedRelic==Relic;
            AddAction(Details,FName(*FString::Printf(TEXT("Gamble%d"),Choice)),Offer.Title,!Waiting,Equipped)->SetVisualRole(EDemoButtonVisualRole::Catalog);
            if(Choice>=8) AddText(Details,!View.Progress.Gambling.Relics.Contains(Relic)?FString::Printf(TEXT("%d 金币 / 永久购买"),Offer.Cost):Equipped?TEXT("✓ 当前已装配"):TEXT("已拥有 / 免费装配"),14,Equipped?DemoUI::Accent:DemoUI::Muted);
        }
        AddText(Details,TEXT("特殊弹药在「弹药类型」页购买；遗物不改变唯一弹药装配规则。"),15,DemoUI::Muted);
    }
    else if(View.GambleOffers.IsValidIndex(Selected))
    {
        const auto& Offer=View.GambleOffers[Selected]; // 当前操作与规则使用同一商品，未获得的效果不提前显示成功。
        AddText(Details,Selected<4?TEXT("THIS RUN / TEMPORARY"):TEXT("RELIC / PERMANENT"),14,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(14,TEXT("Medium")));
        AddText(Details,Offer.Title,24,DemoUI::Ink); AddProgressionRule(Details);
        if(Hub) AddAction(Details,TEXT("Casino.List"),TEXT("← 返回遗物列表"),!Waiting);
        AddText(Details,Offer.Detail,17,DemoUI::Muted);
        if(Selected==3&&!Offer.Rewards.IsEmpty())
        {
            const int32 Page=FMath::Clamp(Menu->GetGambleRewardPage(),0,Offer.Rewards.Num()-1); // 更新与构建同帧也验证索引。
            const auto& Reward=Offer.Rewards[Page];
            AddProgressionRule(Details); AddText(Details,FString::Printf(TEXT("奖池预览 %d / %d"),Page+1,Offer.Rewards.Num()),15,DemoUI::Muted);
            AddText(Details,Reward.Title,20,DemoUI::Accent);
            AddText(Details,Reward.Id==TEXT("no_reward")?TEXT("本次未获得奖励，仅消耗本次费用。"):Reward.Description,16,DemoUI::Muted);
            UHorizontalBox* Pager=AddRow(Details); // 仅本地查看，不发送经济操作。
            AddCompactAction(Pager,TEXT("GamblePoolPrevious"),TEXT("上一项"),!Waiting&&Page>0,90.f);
            AddCompactAction(Pager,TEXT("GamblePoolNext"),TEXT("下一项"),!Waiting&&Page+1<Offer.Rewards.Num(),90.f);
        }
        if(!Offer.BlockReason.IsEmpty()) AddText(Details,Offer.BlockReason,16,DemoUI::Gold);
        AddFooterAction(TEXT("GambleAction"),Waiting?TEXT("等待服务器确认…"):Selected<7?TEXT("确认本次投入  →"):Selected==7?TEXT("卸下遗物并保存"):Offer.Cost>0?TEXT("购买并装配  →"):TEXT("装配并保存  →"),!Waiting&&Offer.BlockReason.IsEmpty(),220.f); // 主操作固定在正文外，滚动规则不移走提交入口。
    }
    else if(!Hub)
    {
        AddText(Details,TEXT("CURRENT BUILD"),14,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(14,TEXT("Medium")));
        AddText(Details,TEXT("当前天赋"),24,DemoUI::Ink); AddProgressionRule(Details);
        if(View.OwnedTalentViews.IsEmpty()) AddText(Details,TEXT("尚无天赋。先选择左侧方向，查看本次实际费用与概率。"),16,DemoUI::Muted);
        for(const auto& Talent:View.OwnedTalentViews) { AddText(Details,Talent.Title,18,DemoUI::Accent); AddText(Details,Talent.Description,16,DemoUI::Muted); }
        AddText(Details,TEXT("当前遗物：")+(View.Progress.Gambling.EquippedRelic.IsNone()?FString(TEXT("无")):DemoGamble::Name(View.Progress.Gambling.EquippedRelic)),16,DemoUI::Gold);
    }
    if(Waiting) AddText(PageBody,TEXT("正在等待服务器确认。已提交的操作不会因关闭页面重复结算。"),16,DemoUI::Gold);
    else if(Controller()->GetMenuMessage().IsEmpty()&&!View.Progress.Gambling.LastResult.IsEmpty()) AddText(PageBody,View.Progress.Gambling.LastResult,16,DemoUI::Gold);
}

void UDemoTerminalScreen::BuildGambleReel()
{
    DEMO_LOG_CALL();
    if(UsesR02Presentation()) { BuildR02GambleReel(); return; } // R02奖励带只换主题色/字体，位移与回执逻辑同AnimateScreen。
    ReelIdleColor=DemoUI::Raised; ReelSelectedColor=DemoUI::Selected; ReelTitleColor=DemoUI::Gold; ReelMutedColor=DemoUI::Muted; // R04兼容色。
    const UDemoMenuFlowComponent* Menu=Flow(); // 仅读取已保存回执的动画状态。
    const FDemoGambleReel& Reel=Menu->GetGambleReel(); // Cards由Flow冻结，动画不得重抽或重排概率。
    SetHeading(Reel.bRolling&&!DemoUI::ReducedMotion()?TEXT("正在揭示本次强化"):TEXT("本次强化结果"),TEXT("本次回执已保存 · 奖池卡片频次不代表中奖概率"));
    UHorizontalBox* Actions=PageNavigation?PageNavigation.Get():AddRow(PageBody); // 固定导航上的跳过不重复提交，正文只保留奖励带。
    AddCompactAction(Actions,TEXT("GambleSkipAnimation"),Menu->IsGambleAnimationSkipped()?TEXT("✓ 跳过揭示"):TEXT("跳过揭示"),true,150.f);
    AddFooterAction(TEXT("Close"),TEXT("返回游戏"))->SetVisualRole(EDemoButtonVisualRole::Standard);
    AddText(PageBody,TEXT("CONFIRMED REWARD / THIS RUN"),16,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(16,TEXT("Medium")));
    UTextBlock* Pointer=AddText(PageBody,TEXT("▼"),22,DemoUI::Gold); // 中央指针与下方居中锚点使用同一容器宽度。
    Pointer->SetJustification(ETextJustify::Center);
    USizeBox* Window=WidgetTree->ConstructWidget<USizeBox>(); // 固定高度，宽度由父容器决定，适配DPI和窄屏。
    Window->SetHeightOverride(174.f); PageBody->AddChild(Window);
    ReelCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); // 只裁剪本条奖励带，不影响其他页面和按钮。
    ReelCanvas->SetClipping(EWidgetClipping::ClipToBoundsAlways); Window->AddChild(ReelCanvas);
    for(int32 Index=0;Index<7;++Index)
    {
        UBorder* Card=WidgetTree->ConstructWidget<UBorder>(); // 七个槽循环映射完整奖池，不随次数增加对象数量。
        Card->SetPadding(FMargin(10.f)); Card->SetBrushColor(DemoUI::Raised);
        UCanvasPanelSlot* CardSlot=ReelCanvas->AddChildToCanvas(Card); // 中心锚定使指针不依赖固定分辨率；不遮蔽UWidget::Slot。
        CardSlot->SetAnchors(FAnchors(.5f,.5f)); CardSlot->SetSize(FVector2D(172.f,140.f));
        UVerticalBox* Content=AddColumn(Card); // 卡片内容随父树生命周期释放。
        AddText(Content,TEXT("◆"),24,DemoUI::Gold)->SetJustification(ETextJustify::Center);
        UTextBlock* Title=AddText(Content,TEXT(""),18,DemoUI::Ink); // 当前池标题由AnimateScreen按槽索引更新。
        Title->SetJustification(ETextJustify::Center);
        ReelCards.Add(Card); ReelTitles.Add(Title); ReelCardIndices.Add(INDEX_NONE);
    }
    UBorder* CenterLine=WidgetTree->ConstructWidget<UBorder>(); // 不参与命中/业务的视觉停靠线。
    CenterLine->SetBrushColor(DemoUI::Gold); CenterLine->SetVisibility(ESlateVisibility::HitTestInvisible);
    UCanvasPanelSlot* LineSlot=ReelCanvas->AddChildToCanvas(CenterLine); // 全高线覆盖带，无法拦截页外点击。
    LineSlot->SetAnchors(FAnchors(.5f,0.f)); LineSlot->SetPosition(FVector2D(-1.f,0.f)); LineSlot->SetSize(FVector2D(2.f,174.f)); LineSlot->SetZOrder(3);
    AnimateScreen(0.f); // 第一帧已经具备卡片标题与位置，不等待下一Tick才出现。
    AddText(PageBody,Reel.bRolling&&!DemoUI::ReducedMotion()?TEXT("正在揭示…"):Reel.ResultMessage,22,DemoUI::Gold);
    AddText(PageBody,TEXT("本轮强化与天赋在通关、团灭或主动放弃后清除；本页动画不改变已提交结果。"),16,DemoUI::Muted);
    if(Reel.bRolling) AddAction(PageBody,TEXT("GambleNo"),DemoUI::ReducedMotion()?TEXT("结果已确认，稍候可继续"):TEXT("揭示中…"),false);
    else
    {
        const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 续抽使用最新报价；原结果没有价格授权。
        const int32 Choice=Menu->GetInspectedGamble(); // 沿用当前抽取方向，返回列表后才可更改。
        const FString Reason=Menu->GetContinueGambleBlockReason(); // 最新余额/奖池状态，只展示不代替服务端重验。
        if(!Reason.IsEmpty()) AddText(PageBody,Reason,15,DemoUI::Muted);
        UHorizontalBox* ResultActions=PageFooter?PageFooter.Get():AddRow(PageBody); // 续抽与返回均是实际CommonUI按钮。
        const FString ContinueCaption=View.GambleOffers.IsValidIndex(Choice)?FString::Printf(TEXT("继续抽取 · %d %s"),View.GambleOffers[Choice].Cost,Choice<4?TEXT("银币"):TEXT("金币")):TEXT("继续抽取");
        AddCompactAction(ResultActions,TEXT("GambleContinue"),ContinueCaption,Reason.IsEmpty(),220.f)->SetVisualRole(EDemoButtonVisualRole::Primary);
        AddCompactAction(ResultActions,TEXT("GambleNo"),TEXT("返回列表"));
    }
}

void UDemoTerminalScreen::AnimateScreen(float DeltaTime)
{
    DEMO_LOG_TICK();
    const UDemoMenuFlowComponent* Menu=Flow(); // 持续滚动时间由原Flow更新，DeltaTime不参与重新抽样。
    if(!ReelCanvas||!Menu) return;
    const FDemoGambleReel& Reel=Menu->GetGambleReel(); // 只借用当前帧的冻结奖池与连续位移。
    if(Reel.Cards.IsEmpty()) return;
    const bool Reduced=DemoUI::ReducedMotion(); // 本机静态偏好不改原Flow的滚动结束、回执和赌场Skip选项。
    const float Position=Reduced?static_cast<float>(Reel.Target):Reel.Position; // 已确认结果可静态停靠，原续抽门禁仍待原揭示阶段结束。
    const int32 Middle=FMath::FloorToInt(Position); // 当前中心附近七槽，无限滚动只更新位置。
    for(int32 Index=0;Index<ReelCards.Num();++Index)
    {
        const int32 AbsoluteSlot=Middle+Index-3; // 相对中心的展示序号可以为负，不代表业务抽取次数。
        const int32 PoolIndex=(AbsoluteSlot%Reel.Cards.Num()+Reel.Cards.Num())%Reel.Cards.Num(); // 正规化到完整冻结池。
        if(UCanvasPanelSlot* CardSlot=Cast<UCanvasPanelSlot>(ReelCards[Index]->Slot)) CardSlot->SetPosition(FVector2D((AbsoluteSlot-Position)*184.f-86.f,-70.f)); // 仅修改该卡UMG槽的位置。
        if(ReelCardIndices[Index]!=PoolIndex)
        {
            ReelCardIndices[Index]=PoolIndex;
            ReelTitles[Index]->SetText(FText::FromString(Reel.Cards[PoolIndex].Title)); // 换卡才变文字，不逐帧分配新Widget。
        }
        const bool Selected=(!Reel.bRolling||Reduced)&&AbsoluteSlot==Reel.Target; // 静态模式仍只高亮服务器已提交的唯一结果。
        ReelCards[Index]->SetBrushColor(Selected?ReelSelectedColor:ReelIdleColor); // 颜色由当前页型构建时写入，R02/R04共用位移逻辑。
        ReelCards[Index]->SetRenderOpacity(FMath::Clamp(1.f-FMath::Abs(AbsoluteSlot-Position)*.14f,.3f,1.f));
        ReelTitles[Index]->SetColorAndOpacity(FSlateColor(Reel.Cards[PoolIndex].Id==TEXT("no_reward")?ReelMutedColor:ReelTitleColor));
    }
}

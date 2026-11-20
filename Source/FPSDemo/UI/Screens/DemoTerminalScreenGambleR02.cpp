// 职责：R02编辑式金币娱乐/永久遗物（安全区）与银币冒险强化（关间）终端页。与武器/弹药/核心页共用主题、细线、无框文字按钮与整卡高亮。
// 使用：BuildGamble在UsesR02Presentation时分派到此；只读拥有者GambleOffers快照，按钮沿用原Casino.Open.N/GambleN/GambleAction/GamblePool*白名单。
// 依赖：S0 DemoUITheme、DemoR02EditorialButton、DemoCasinoRules映射；本页不扣币、不随机、不保存，所有交易仍经原Flow确认与服务器重验。
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/R02/DemoR02EditorialButton.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/DemoUIPresentation.h" // 本机减少动效只决定奖励带是否静态停靠。
#include "Player/DemoPlayerController.h"
#include "Game/DemoGameState.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Debug/DemoLog.h"

namespace
{
    /** 七个金币玩法在GambleOffers中的固定协议索引；旧遗物7..11保留，不重排。 */
    const int32 GoldArcadeChoices[]={4,5,6,12,13,14,15};
    /** 与上表同序的玩法机制短标签；只描述操作方式，不含本地推算的赔率/奖励数值。 */
    const TCHAR* GoldArcadeTags[]={TEXT("区间 · 奇偶 · 单号"),TEXT("三轴符号组合"),TEXT("揭格 · 随时领取"),TEXT("逐级充能"),TEXT("保留骰面重掷"),TEXT("押注支持方"),TEXT("十靶射击")};
    static_assert(UE_ARRAY_COUNT(GoldArcadeChoices)==UE_ARRAY_COUNT(GoldArcadeTags),"Gold arcade tags must match choices");
}

UTextBlock* UDemoTerminalScreen::AddR02GambleLabel(UPanelWidget* Parent,const FString& Text,int32 Size,bool Serif,bool Muted,float WrapWidth)
{
    DEMO_LOG_CALL();
    const UDemoUITheme* Theme=R02Theme(); // 基类强持有的只读主题，本助手不加载资源。
    if(!Theme) Theme=GetDefault<UDemoUITheme>(); // 缺资产保可读回退，不当作字库验收通过。
    UTextBlock* Label=AddText(Parent,Text,Size,Muted?Theme->Muted:Theme->Ink); // 关键价格/拒绝默认Ink，Muted只用于辅助标签。
    Label->SetFont(Serif?Theme->GetSerifFont(Size,TEXT("Regular"),true):Theme->GetLabelFont(Size));
    if(WrapWidth>0.f) { Label->SetAutoWrapText(false); Label->SetWrapTextAt(WrapWidth); } // 已知列宽首帧即正确分配多行高度。
    return Label;
}

void UDemoTerminalScreen::BuildR02Gamble(const ADemoGameState* State)
{
    DEMO_LOG_CALL();
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 本人脱敏报价；隐藏雷位等私有数据不进入本页。
    const UDemoMenuFlowComponent* Menu=Flow(); // 选择/等待真值来自原Flow。
    UDemoUITheme* Theme=R02Theme(); if(!Theme) Theme=const_cast<UDemoUITheme*>(GetDefault<UDemoUITheme>()); // 编辑式按钮需非const引用，只读使用。
    const bool Hub=State->Phase==EDemoPhase::Hub,Waiting=Menu->IsGambleWaiting(); // 币种与表现，服务端仍按阶段授权。
    const int32 Selected=Menu->GetInspectedGamble(); // 查看项，不自动购买/下注。
    int32 ViewportWidth=0,ViewportHeight=0; Controller()->GetViewportSize(ViewportWidth,ViewportHeight); // 本机物理视口，仅用于重排。
    const float Scale=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(this)); // 当前UMG DPI。
    const float LayoutWidth=FMath::Clamp(static_cast<float>(ViewportWidth)/Scale-48.f,1000.f,1600.f); // 与核心/奖励页同口径。
    ConfigureLayout(LayoutWidth,960.f);
    if(UWidget* Eyebrow=WidgetTree->FindWidget(TEXT("PageSectionLabel"))) Eyebrow->SetVisibility(ESlateVisibility::Collapsed); // 去掉重复英文眉题，标题已表达页型。
    UE_LOG(LogFPSDemo,Log,TEXT("R02_GAMBLE layout hub=%d viewport=%dx%d dpi=%.3f width=%.1f selected=%d waiting=%d"),Hub,ViewportWidth,ViewportHeight,Scale,LayoutWidth,Selected,Waiting);
    AddHeaderAction(TEXT("GambleSkipAnimation"),Menu->IsGambleAnimationSkipped()?TEXT("✓ 跳过揭示"):TEXT("跳过揭示"),!Waiting,150.f); // 本机偏好，与原入口一致。

    if(!Hub)
    {
        // ---- 关间银币冒险强化：四张整卡只给名称/费用/概率，选中后才展开一行规则与主操作 ----
        SetHeading(TEXT("冒险强化"),TEXT("银币 · 本轮有效"));
        const float ContentWidth=FMath::Min(1320.f,LayoutWidth-72.f); // 与奖励页一致的阅读宽度。
        const float CardWidth=ContentWidth/4.f-32.f; // 四列，每列两侧16间距。
        UHorizontalBox* Cards=AddRow(PageBody);
        for(int32 Choice=0;Choice<4;++Choice)
        {
            if(!View.GambleOffers.IsValidIndex(Choice)) continue;
            const auto& Offer=View.GambleOffers[Choice]; // 服务端最新费用/概率，UI不重算。
            const bool IsSelected=Selected==Choice;
            UOverlay* Card=WidgetTree->ConstructWidget<UOverlay>(); // 底层整卡按钮接点击/焦点，上层内容不可命中。
            if(auto* CardSlot=Cards->AddChildToHorizontalBox(Card)) { CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); CardSlot->SetPadding(FMargin(16,4)); }
            UDemoMenuButton* Pick=AddAction(Card,FName(*FString::Printf(TEXT("Gamble%d"),Choice)),TEXT(""),!Waiting,IsSelected); // 原InspectGamble，只查看不扣费。
            if(auto* HitSlot=Cast<UOverlaySlot>(Pick->Slot)) { HitSlot->SetHorizontalAlignment(HAlign_Fill); HitSlot->SetVerticalAlignment(VAlign_Fill); }
            if(auto* Editorial=Cast<UDemoR02EditorialButton>(Pick)) { Editorial->SetR02Appearance(Theme,IsSelected,22,true); Editorial->SetR02CardMode(true); }
            Pick->SetMinDimensions(CardWidth,250.f); Pick->UseSingleLineLabel();
            UVerticalBox* Content=WidgetTree->ConstructWidget<UVerticalBox>(); Content->SetVisibility(ESlateVisibility::HitTestInvisible);
            if(auto* ContentSlot=Card->AddChildToOverlay(Content)) { ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Fill); ContentSlot->SetPadding(FMargin(14,12)); }
            UTextBlock* Number=AddR02GambleLabel(Content,FString::Printf(TEXT("%02d"),Choice+1),56,true,!IsSelected); // 阅读序号，不是等级。
            Number->SetFont(Theme->GetSerifFont(56,TEXT("Regular"),false)); Number->SetAutoWrapText(false);
            AddR02GambleLabel(Content,Offer.Title,28,true,false,CardWidth-28.f);
            AddGap(Content,8.f,true); // 价格贴底，四卡对齐基线。
            AddR02Rule(Content,8.f);
            UHorizontalBox* PriceLine=AddRow(Content);
            UTextBlock* Cost=AddR02GambleLabel(PriceLine,FText::AsNumber(Offer.Cost).ToString(),34,true,false); // 实际报价。
            Cost->SetFont(Theme->GetSerifFont(34,TEXT("Regular"),false)); Cost->SetAutoWrapText(false);
            if(auto* CostSlot=Cast<UHorizontalBoxSlot>(Cost->Slot)) { CostSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); CostSlot->SetPadding(FMargin(0,0,8,0)); CostSlot->SetVerticalAlignment(VAlign_Bottom); }
            UTextBlock* Unit=AddR02GambleLabel(PriceLine,TEXT("银币"),16,false,true);
            if(auto* UnitSlot=Cast<UHorizontalBoxSlot>(Unit->Slot)) UnitSlot->SetVerticalAlignment(VAlign_Bottom);
            AddR02GambleLabel(Content,FString::Printf(TEXT("成功率 %.0f%%"),Offer.Chance*100.f),17,false,true);
            if(!Offer.BlockReason.IsEmpty()) AddR02GambleLabel(Content,Offer.BlockReason,15,false,false,CardWidth-28.f); // 拒绝原因必须可读，不只靠禁用色。
        }
        AddR02Rule(PageBody,14.f);
        if(View.GambleOffers.IsValidIndex(Selected)&&Selected<4)
        {
            const auto& Offer=View.GambleOffers[Selected]; // 主操作与说明冻结同一报价。
            if(Selected==3&&!Offer.Rewards.IsEmpty())
            {
                const int32 Page=FMath::Clamp(Menu->GetGambleRewardPage(),0,Offer.Rewards.Num()-1); // 同帧也做边界保护。
                const auto& Reward=Offer.Rewards[Page];
                UHorizontalBox* Pool=AddRow(PageBody); // 奖池逐项查看：一次只显示一条，替代整段长奖池说明。
                AddCompactAction(Pool,TEXT("GamblePoolPrevious"),TEXT("‹"),!Waiting&&Page>0,48.f);
                UTextBlock* Index=AddR02GambleLabel(Pool,FString::Printf(TEXT("奖池 %d/%d"),Page+1,Offer.Rewards.Num()),16,false,true);
                if(auto* IndexSlot=Cast<UHorizontalBoxSlot>(Index->Slot)) IndexSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                UTextBlock* RewardTitle=AddR02GambleLabel(Pool,Reward.Title,22,true,false);
                if(auto* TitleSlot=Cast<UHorizontalBoxSlot>(RewardTitle->Slot)) TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
                AddR02GambleLabel(Pool,Reward.Id==TEXT("no_reward")?TEXT("仅消耗费用"):Reward.Description,16,false,true);
                AddCompactAction(Pool,TEXT("GamblePoolNext"),TEXT("›"),!Waiting&&Page+1<Offer.Rewards.Num(),48.f);
            }
            else AddR02GambleLabel(PageBody,Offer.Detail,18,false,false,ContentWidth-16.f); // 属性三项本就是一行规则。
            AddFooterAction(TEXT("GambleAction"),Waiting?TEXT("等待服务器确认…"):FString::Printf(TEXT("投入 %s 银币  →"),*FText::AsNumber(Offer.Cost).ToString()),!Waiting&&Offer.BlockReason.IsEmpty(),220.f); // 原确认链。
        }
        else AddR02GambleLabel(PageBody,TEXT("选择一项查看规则"),17,false,true);
        FString Owned; // 只拼已有天赋名称，完整效果在玩家状态页查看。
        for(const auto& Talent:View.OwnedTalentViews) Owned+=(Owned.IsEmpty()?TEXT(""):TEXT("  ·  "))+Talent.Title;
        AddR02GambleLabel(PageBody,FString::Printf(TEXT("天赋  %s"),Owned.IsEmpty()?TEXT("—"):*Owned),16,false,true,ContentWidth-16.f);
    }
    else
    {
        // ---- 安全区：左七玩法目录，右永久遗物列表/当前遗物详情 ----
        SetHeading(TEXT("安全区娱乐"),TEXT("金币玩法 · 永久遗物"));
        const float AsideWidth=440.f,CatalogWidth=FMath::Max(520.f,LayoutWidth-64.f-AsideWidth-64.f); // 右侧固定遗物栏，左侧余宽给目录。
        UHorizontalBox* Columns=AddRow(PageBody);
        UVerticalBox* Catalog=AddColumn(Columns);
        if(auto* CatalogSlot=Cast<UHorizontalBoxSlot>(Catalog->Slot)) CatalogSlot->SetPadding(FMargin(0,0,64,0));
        AddR02GambleLabel(Catalog,TEXT("金币玩法"),18,false,true);
        UUniformGridPanel* Grid=WidgetTree->ConstructWidget<UUniformGridPanel>(); Grid->SetSlotPadding(FMargin(0,0,32,10)); Catalog->AddChild(Grid); // 2列×4行。
        for(int32 Row=0;Row<UE_ARRAY_COUNT(GoldArcadeChoices);++Row)
        {
            const int32 Choice=GoldArcadeChoices[Row]; // 同一索引贯穿Offer/显示/Mode白名单。
            if(!View.GambleOffers.IsValidIndex(Choice)) continue;
            const auto& Offer=View.GambleOffers[Choice];
            UVerticalBox* Cell=WidgetTree->ConstructWidget<UVerticalBox>();
            if(UUniformGridSlot* GridSlot=Grid->AddChildToUniformGrid(Cell,Row/2,Row%2)) GridSlot->SetHorizontalAlignment(HAlign_Fill);
            UDemoMenuButton* Entry=AddAction(Cell,FName(*FString::Printf(TEXT("Casino.Open.%d"),DemoCasino::ModeForChoice(Choice))),FString::Printf(TEXT("%02d  %s"),Row+1,*Offer.Title),!Waiting,Selected==Choice); // 打开原独立玩法页。
            if(auto* Editorial=Cast<UDemoR02EditorialButton>(Entry)) Editorial->SetR02Appearance(Theme,Selected==Choice,26,true);
            Entry->SetMinDimensions(FMath::Max(220.f,CatalogWidth/2.f-48.f),44.f); Entry->UseSingleLineLabel();
            if(auto* EntrySlot=Cast<UVerticalBoxSlot>(Entry->Slot)) EntrySlot->SetPadding(FMargin(0));
            UTextBlock* Tag=AddR02GambleLabel(Cell,GoldArcadeTags[Row],15,false,true);
            if(auto* TagSlot=Cast<UVerticalBoxSlot>(Tag->Slot)) TagSlot->SetPadding(FMargin(24,0,0,0));
        }
        AddR02Rule(Catalog,12.f);
        if(View.Progress.Gambling.Mine.Stake>0||View.Progress.Gambling.Arcade.Mode>=0) AddR02GambleLabel(Catalog,TEXT("有未结项目 · 打开对应玩法继续"),16,false,false);
        else if(!Waiting&&Controller()->GetMenuMessage().IsEmpty()&&!View.Progress.Gambling.LastResult.IsEmpty()) AddR02GambleLabel(Catalog,View.Progress.Gambling.LastResult,16,false,false,CatalogWidth-16.f);

        USizeBox* RelicBounds=WidgetTree->ConstructWidget<USizeBox>(); RelicBounds->SetWidthOverride(AsideWidth); // 局部固定宽遗物栏，不复用基类独立滚动侧栏。
        if(auto* AsideSlot=Columns->AddChildToHorizontalBox(RelicBounds)) AsideSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        UVerticalBox* Relics=WidgetTree->ConstructWidget<UVerticalBox>(); RelicBounds->SetContent(Relics);
        const bool RelicSelected=Selected>=7&&Selected<=11&&View.GambleOffers.IsValidIndex(Selected);
        if(!RelicSelected)
        {
            AddR02GambleLabel(Relics,TEXT("永久遗物"),18,false,true);
            for(int32 Choice=8;Choice<=11;++Choice)
            {
                if(!View.GambleOffers.IsValidIndex(Choice)) continue;
                const auto& Offer=View.GambleOffers[Choice]; // 点击只查看；购买/装配需右侧主操作。
                const FName Relic=DemoGamble::Relic(Choice-8);
                const bool Owned=View.Progress.Gambling.Relics.Contains(Relic),Equipped=View.Progress.Gambling.EquippedRelic==Relic;
                UHorizontalBox* Line=AddRow(Relics);
                UDemoMenuButton* Entry=AddAction(Line,FName(*FString::Printf(TEXT("Gamble%d"),Choice)),Offer.Title,!Waiting,Equipped);
                if(auto* Editorial=Cast<UDemoR02EditorialButton>(Entry)) Editorial->SetR02Appearance(Theme,Equipped,24,true);
                Entry->UseSingleLineLabel();
                UTextBlock* Status=AddR02GambleLabel(Line,Equipped?TEXT("已装配"):Owned?TEXT("已拥有"):FString::Printf(TEXT("%s 金币"),*FText::AsNumber(Offer.Cost).ToString()),16,false,!Equipped);
                if(auto* StatusSlot=Cast<UHorizontalBoxSlot>(Status->Slot)) { StatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); StatusSlot->SetHorizontalAlignment(HAlign_Right); }
                Status->SetAutoWrapText(false);
                AddR02Rule(Relics,2.f);
            }
            if(!View.Progress.Gambling.EquippedRelic.IsNone()&&View.GambleOffers.IsValidIndex(7))
                AddCompactAction(Relics,TEXT("Gamble7"),View.GambleOffers[7].Title,!Waiting,160.f); // 只有已装配时才给卸下入口，减少常驻无效项。
        }
        else
        {
            const auto& Offer=View.GambleOffers[Selected]; // 详情与主操作同一报价。
            const FName Relic=Selected>=8?DemoGamble::Relic(Selected-8):NAME_None;
            const bool Owned=Selected>=8&&View.Progress.Gambling.Relics.Contains(Relic),Equipped=Selected>=8&&View.Progress.Gambling.EquippedRelic==Relic;
            AddCompactAction(Relics,TEXT("Casino.List"),TEXT("← 遗物列表"),!Waiting,140.f);
            AddR02GambleLabel(Relics,Offer.Title,36,true,false,AsideWidth-16.f);
            AddR02GambleLabel(Relics,Selected==7?TEXT("卸下当前遗物"):Equipped?TEXT("已装配"):Owned?TEXT("已拥有 · 免费装配"):FString::Printf(TEXT("%s 金币 · 永久"),*FText::AsNumber(Offer.Cost).ToString()),18,false,true);
            AddR02Rule(Relics,10.f);
            AddR02GambleLabel(Relics,Offer.Detail,18,false,false,AsideWidth-16.f);
            if(!Offer.BlockReason.IsEmpty()) AddR02GambleLabel(Relics,Offer.BlockReason,16,false,false,AsideWidth-16.f);
            AddFooterAction(TEXT("GambleAction"),Waiting?TEXT("等待服务器确认…"):Selected==7?TEXT("卸下并保存"):Offer.Cost>0&&!Owned?TEXT("购买并装配  →"):TEXT("装配并保存  →"),!Waiting&&Offer.BlockReason.IsEmpty(),220.f);
        }
    }
    if(Waiting) AddR02GambleLabel(PageBody,TEXT("等待服务器确认…"),16,false,false); // 关闭页面不会重复结算。
}

void UDemoTerminalScreen::BuildR02GambleReel()
{
    DEMO_LOG_CALL();
    const UDemoMenuFlowComponent* Menu=Flow(); // 只读已保存回执的动画状态。
    const FDemoGambleReel& Reel=Menu->GetGambleReel(); // 冻结奖池，不重抽。
    const UDemoUITheme* Theme=R02Theme(); if(!Theme) Theme=GetDefault<UDemoUITheme>();
    ReelIdleColor=Theme->Ink.CopyWithNewOpacity(.05f); ReelSelectedColor=Theme->Ember.CopyWithNewOpacity(.32f); // 编辑式淡底，中奖卡用Ember而非R04蓝灰。
    ReelTitleColor=Theme->Ink; ReelMutedColor=Theme->Muted;
    if(UWidget* Eyebrow=WidgetTree->FindWidget(TEXT("PageSectionLabel"))) Eyebrow->SetVisibility(ESlateVisibility::Collapsed);
    const bool Rolling=Reel.bRolling&&!DemoUI::ReducedMotion(); // 本机减少动效直接静态停靠。
    SetHeading(Rolling?TEXT("正在揭示"):TEXT("本次结果"),TEXT("回执已保存"));
    UHorizontalBox* Actions=PageNavigation?PageNavigation.Get():AddRow(PageBody);
    AddCompactAction(Actions,TEXT("GambleSkipAnimation"),Menu->IsGambleAnimationSkipped()?TEXT("✓ 跳过揭示"):TEXT("跳过揭示"),true,150.f);
    AddFooterAction(TEXT("Close"),TEXT("返回游戏"))->SetVisualRole(EDemoButtonVisualRole::Standard);
    UTextBlock* Pointer=AddText(PageBody,TEXT("▼"),20,Theme->Ember); Pointer->SetJustification(ETextJustify::Center);
    USizeBox* Window=WidgetTree->ConstructWidget<USizeBox>(); Window->SetHeightOverride(174.f); PageBody->AddChild(Window); // 固定高度奖励带。
    ReelCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); ReelCanvas->SetClipping(EWidgetClipping::ClipToBoundsAlways); Window->AddChild(ReelCanvas);
    for(int32 Index=0;Index<7;++Index)
    {
        UBorder* Card=WidgetTree->ConstructWidget<UBorder>(); Card->SetPadding(FMargin(12.f)); Card->SetBrushColor(ReelIdleColor); // 七槽循环映射完整奖池。
        UCanvasPanelSlot* CardSlot=ReelCanvas->AddChildToCanvas(Card); CardSlot->SetAnchors(FAnchors(.5f,.5f)); CardSlot->SetSize(FVector2D(172.f,140.f));
        UVerticalBox* Content=AddColumn(Card);
        AddGap(Content,8.f,true);
        UTextBlock* Title=AddText(Content,TEXT(""),22,ReelTitleColor); Title->SetFont(Theme->GetSerifFont(22,TEXT("Regular"),true)); Title->SetJustification(ETextJustify::Center); // AnimateScreen换槽时更新。
        AddGap(Content,8.f,true);
        ReelCards.Add(Card); ReelTitles.Add(Title); ReelCardIndices.Add(INDEX_NONE);
    }
    UBorder* CenterLine=WidgetTree->ConstructWidget<UBorder>(); CenterLine->SetBrushColor(Theme->Ember); CenterLine->SetVisibility(ESlateVisibility::HitTestInvisible);
    UCanvasPanelSlot* LineSlot=ReelCanvas->AddChildToCanvas(CenterLine); LineSlot->SetAnchors(FAnchors(.5f,0.f)); LineSlot->SetPosition(FVector2D(-1.f,0.f)); LineSlot->SetSize(FVector2D(2.f,174.f)); LineSlot->SetZOrder(3);
    AnimateScreen(0.f); // 首帧即有标题与位置。
    UTextBlock* Result=AddText(PageBody,Rolling?TEXT("…"):Reel.ResultMessage,30,Theme->Ink); Result->SetFont(Theme->GetSerifFont(30,TEXT("Regular"),true)); Result->SetJustification(ETextJustify::Center);
    if(Reel.bRolling) { AddFooterAction(TEXT("GambleNo"),DemoUI::ReducedMotion()?TEXT("稍候可继续"):TEXT("揭示中…"),false); return; }
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 续抽使用最新报价。
    const int32 Choice=Menu->GetInspectedGamble();
    const FString Reason=Menu->GetContinueGambleBlockReason(); // 最新余额/奖池门禁，仅展示。
    if(!Reason.IsEmpty()) { UTextBlock* Block=AddText(PageBody,Reason,16,Theme->Muted); Block->SetJustification(ETextJustify::Center); }
    UHorizontalBox* ResultActions=PageFooter?PageFooter.Get():AddRow(PageBody);
    AddCompactAction(ResultActions,TEXT("GambleNo"),TEXT("返回列表"));
    const FString ContinueCaption=View.GambleOffers.IsValidIndex(Choice)?FString::Printf(TEXT("再来一次 · %s %s"),*FText::AsNumber(View.GambleOffers[Choice].Cost).ToString(),Choice<4?TEXT("银币"):TEXT("金币")):TEXT("再来一次");
    AddCompactAction(ResultActions,TEXT("GambleContinue"),ContinueCaption,Reason.IsEmpty(),220.f)->SetVisualRole(EDemoButtonVisualRole::Primary);
}

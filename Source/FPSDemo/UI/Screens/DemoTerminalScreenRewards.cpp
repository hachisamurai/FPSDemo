// 职责：C03 R02清关奖励/核心分支的无框编号布局；非R02保留旧清关页。只读真实OwnerOffer，不采用概念能力/图标/数值。
// 使用：终端BuildBody仅在原Flow奖励页调用；依赖拥有者RewardOffers和现有Upgrade0..2意图，不决定奖励或切关。
// 边界：本轮成长说明区分个人阵亡后的随队复活保留与整队失败重开清除；永久成长沿既有规则保留。
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/R02/DemoR02EditorialButton.h" // 复用真实CommonUI按钮/独立焦点与已选细线，不另造点击层。
#include "Player/DemoPlayerController.h"
#include "Game/DemoGameState.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h" // 真实物理视口/DPI只用于重排，不改设备设置。
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Debug/DemoLog.h"

void UDemoTerminalScreen::BuildRewards()
{
    DEMO_LOG_CALL();
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 同次构建借用的本人权威报价；不复制到客户端奖励缓存。
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 只标出已清关编号，不能由编号猜奖励类型。
    const bool Branch=View.bBranchReward; // 实际二选一由服务器投影决定，兼容旧战役/无尽普通奖励。
    if(UsesR02Presentation())
    {
        const UDemoUITheme* Theme=R02Theme(); // 基类强持有的只读主题，构建不加载能力图标或外部素材。
        if(!Theme) Theme=GetDefault<UDemoUITheme>(); // 资源失败保持原生Token/已打包字体可读，不冒充衬线加载已验。
        int32 ViewportWidth=0,ViewportHeight=0; Controller()->GetViewportSize(ViewportWidth,ViewportHeight); // 当前拥有者真实物理视口。
        const float Scale=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(this)); // 当前UMG DPI倍率，非玩法时钟/属性倍率。
        const float LayoutWidth=FMath::Clamp(static_cast<float>(ViewportWidth)/Scale-48.f,1000.f,1600.f); // 外壳总48边距，窄屏不强行扩大到1600再额外缩小。
        const int32 ExpectedCount=Branch?2:3,OfferCount=FMath::Min(ExpectedCount,View.RewardOffers.Num()); // 实际分支标记决定二/三列；只注册真实存在的OwnerOffer。
        const float ContentWidth=FMath::Min(1320.f,LayoutWidth-72.f),ColumnWidth=ContentWidth/FMath::Max(1,OfferCount)-48.f; // 两侧留白与每列24px间距，完整正文按本次真实列宽排版。
        ConfigureLayout(LayoutWidth,960.f);
        UE_LOG(LogFPSDemo,Log,TEXT("R02_REWARDS layout viewport=%dx%d dpi=%.3f width=%.1f branch=%d offers=%d"),ViewportWidth,ViewportHeight,Scale,LayoutWidth,Branch,OfferCount);
        SetHeading(Branch?TEXT("选择本轮流派分支"):TEXT("选择一项能力")); // 分支/普通奖励仍由同一Owner真值区分，不复制图中的虚构技能。
        if(PageHeading) { PageHeading->SetFont(Theme->GetSerifFont(56,TEXT("Regular"),true)); PageHeading->SetJustification(ETextJustify::Center); }
        if(PageSubtitle) PageSubtitle->SetVisibility(ESlateVisibility::Collapsed); // C03只保完成关数眉题与主标题，不叠重复教学。
        if(auto* Header=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("PageHeaderBounds")))) Header->SetHeightOverride(176.f); // 大衬线标题和16号眉题分配真实高度，后续页型由Base重置。
        if(auto* Eyebrow=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PageSectionLabel"))))
        {
            Eyebrow->SetText(FText::FromString(State?TEXT("LEVEL ")+DemoUIR02::ToRoman(State->LevelNumber)+TEXT(" CLEARED"):TEXT("REWARD SELECTION"))); // 真实已清关编号，不固定第III关。
            FSlateFontInfo EyebrowFont=Theme->GetLabelFont(16,TEXT("Regular"),true); EyebrowFont.LetterSpacing=140; // 拉丁辅助字距，中文标题不加密集宽字距。
            Eyebrow->SetFont(EyebrowFont); Eyebrow->SetColorAndOpacity(Theme->Muted); Eyebrow->SetJustification(ETextJustify::Center);
        }
        if(UWidget* Wallet=WidgetTree->FindWidget(TEXT("PageWallet"))) Wallet->SetVisibility(ESlateVisibility::Collapsed); // 免费奖励不常驻双币账本。
        USizeBox* RewardBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardCardsBounds")); // 无底板，只控制最大阅读宽/同行操作基线。
        RewardBounds->SetWidthOverride(ContentWidth); RewardBounds->SetMinDesiredHeight(520.f);
        PageBody->AddChildToVerticalBox(RewardBounds)->SetHorizontalAlignment(HAlign_Center);
        UHorizontalBox* Columns=AddRow(RewardBounds); // 三选一或分支二选一，完整说明自然增高并由原ScrollBox滚动。
        for(int32 Index=0;Index<OfferCount;++Index)
        {
            const FDemoShopOfferSnapshot& Offer=View.RewardOffers[Index]; // 同一次Owner报价的名称/当前值/增量/完整规则与拒绝，UI不计算权限。
            UOverlay* CardOverlay=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass()); // 整卡可点击容器：底层全尺寸按钮接收点击/焦点，顶层展示内容HitTestInvisible。
            if(auto* CardSlot=Columns->AddChildToHorizontalBox(CardOverlay))
            {
                CardSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
                CardSlot->SetPadding(FMargin(16,8));
            }
            UDemoMenuButton* Choose=AddAction(CardOverlay,FName(*FString::Printf(TEXT("Upgrade%d"),Index)),TEXT(""),Offer.BlockReason.IsEmpty(),Offer.bSelected); // 原立即领取请求，不新增确认、跳过或选项缓存。
            if(auto* HitSlot=Cast<UOverlaySlot>(Choose->Slot)) { HitSlot->SetHorizontalAlignment(HAlign_Fill); HitSlot->SetVerticalAlignment(VAlign_Fill); }
            if(auto* Editorial=Cast<UDemoR02EditorialButton>(Choose)) { Editorial->SetR02Appearance(const_cast<UDemoUITheme*>(Theme),Offer.bSelected,24,true); Editorial->SetR02CardMode(true); } // 启用整卡高亮，悬停/焦点整卡提亮并点亮余烬红底线。
            Choose->SetMinDimensions(ColumnWidth,420.f); Choose->UseSingleLineLabel();
            UVerticalBox* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass()); // 每列透明，内容设为HitTestInvisible让整卡任意位置点击直达Choose。
            Column->SetVisibility(ESlateVisibility::HitTestInvisible);
            if(auto* ContentSlot=CardOverlay->AddChildToOverlay(Column))
            {
                ContentSlot->SetHorizontalAlignment(HAlign_Fill);
                ContentSlot->SetVerticalAlignment(VAlign_Fill);
                ContentSlot->SetPadding(FMargin(12,12));
            }
            UTextBlock* Number=AddText(Column,FString::Printf(TEXT("%02d"),Index+1),96,Theme->Ink); // C03巨大序号是阅读索引，不是能力等级/键位。
            Number->SetFont(Theme->GetSerifFont(96,TEXT("Regular"),false)); Number->SetJustification(ETextJustify::Center); Number->SetAutoWrapText(false);
            AddGap(Column,22.f);
            UTextBlock* Title=AddText(Column,Offer.Title,30,Theme->Ink); // 真实能力/分支标题，完整长名允许换行。
            Title->SetFont(Theme->GetSerifFont(30,TEXT("Regular"),true)); Title->SetJustification(ETextJustify::Center); Title->SetAutoWrapText(false); Title->SetWrapTextAt(ColumnWidth);
            if(!Branch)
            {
                UTextBlock* Gain=AddText(Column,FString::Printf(TEXT("%+g"),Offer.Gain),32,Theme->Ink); // 原始实际Gain可为0，不按示例改收益或单位。
                Gain->SetFont(Theme->GetSerifFont(32,TEXT("Regular"),false)); Gain->SetJustification(ETextJustify::Center);
                UTextBlock* Current=AddText(Column,FString::Printf(TEXT("当前 %g  →  %g"),Offer.CurrentValue,Offer.CurrentValue+Offer.Gain),18,Theme->Ink); // 沿原同口径预览，不混入永久/队友值。
                Current->SetJustification(ETextJustify::Center); Current->SetAutoWrapText(false); Current->SetWrapTextAt(ColumnWidth);
            }
            AddR02Rule(Column,10.f);
            UTextBlock* Detail=AddText(Column,Offer.Detail,18,Theme->Ink); // 完整机制/限制保留，关键规则不降为低对比小字。
            Detail->SetAutoWrapText(false); Detail->SetWrapTextAt(ColumnWidth); Detail->SetJustification(Offer.Detail.Len()<=40?ETextJustify::Center:ETextJustify::Left);
            if(!Offer.BlockReason.IsEmpty())
            {
                UTextBlock* Reason=AddText(Column,Offer.BlockReason,18,Theme->Ink); // 真实拒绝完整可读；禁用颜色/鼠标悬停不能替代原因。
                Reason->SetAutoWrapText(false); Reason->SetWrapTextAt(ColumnWidth); Reason->SetJustification(ETextJustify::Center);
            }
            AddGap(Column,24.f,true);
            UTextBlock* ActionHint=AddText(Column,Branch?TEXT("选择此分支"):TEXT("选择此能力"),22,Offer.BlockReason.IsEmpty()?Theme->Ink:Theme->Muted); // 展示文字提示，整卡任意处点击均触发Choose。
            ActionHint->SetFont(Theme->GetSerifFont(22,TEXT("Regular"),true)); ActionHint->SetJustification(ETextJustify::Center);
            if(auto* HintSlot=Cast<UVerticalBoxSlot>(ActionHint->Slot)) HintSlot->SetPadding(FMargin(0,10,0,12));
        }
        if(OfferCount==0)
        {
            UE_LOG(LogFPSDemo,Log,TEXT("R02_REWARDS waiting for owner offers branch=%d"),Branch);
            AddText(PageBody,TEXT("正在等待服务器奖励列表…"),18,Theme->Ink);
        }
        UPanelWidget* RuleRegion=PageFooter?static_cast<UPanelWidget*>(PageFooter.Get()):PageBody.Get(); // 原固定底栏只保寿命/不可跳过，不添加返回动作。
        if(PageFooter&&PageFooter->GetChildrenCount()==1)
            if(auto* SpacerSlot=Cast<UHorizontalBoxSlot>(PageFooter->GetChildAt(0)->Slot)) SpacerSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); // 既有占位改Auto，规则文本不被推到右半屏；保Base两子项可见性契约。
        UTextBlock* Rule=AddText(RuleRegion,TEXT("本轮有效 · 不可跳过"),18,Theme->Ink); Rule->SetJustification(ETextJustify::Center); // 寿命/不可跳过为关键规则，保高对比；真实Owner详情仍完整可滚动。
        if(!Controller()->GetMenuMessage().IsEmpty()) AddText(PageBody,Controller()->GetMenuMessage(),18,Theme->Ink); // 必要原拒绝/等待/保存事实照常显示，UI不推断成功。
        return;
    }
    SetHeading(Branch?TEXT("选择本轮流派分支"):TEXT("选择一项能力强化"),
        State?FString::Printf(TEXT("第 %02d 关已完成  /  %s"),State->LevelNumber,Branch?TEXT("核心进化 · 二选一"):TEXT("战地强化 · 三选一")):
        Branch?TEXT("核心进化 · 二选一"):TEXT("战地强化 · 三选一"));
    AddText(PageBody,Branch?TEXT("选择一条方向，继续塑造本轮打法。此项替代本关普通强化。"):
        TEXT("每位参战者独立领取一项完整强化；领取后留在本关，继续整备。"),18,DemoUI::Muted);
    AddGap(PageBody,16.f);
    USizeBox* Bounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("RewardCardsBounds")); // 本树布局约束；保持普通三卡与分支双卡共同操作基线。
    Bounds->SetMinDesiredHeight(408.f); PageBody->AddChildToVerticalBox(Bounds);
    UHorizontalBox* Cards=AddRow(Bounds); // 同组并列，长文自然增高并由原PageScroll提供滚动，不截断描述。
    for(int32 Index=0;Index<FMath::Min(Branch?2:3,View.RewardOffers.Num());++Index)
    {
        const FDemoShopOfferSnapshot& Offer=View.RewardOffers[Index]; // 序号、收益、拒绝原因均来自同一实际Offer，绝不填入原型示例数值。
        UVerticalBox* Card=AddProgressionCard(Cards,FString::Printf(TEXT("%s / %02d"),Branch?TEXT("CORE EVOLUTION"):TEXT("FIELD UPGRADE"),Index+1)); // 与属性/核心页共用轻底、组织线与窄体编号；不另建领取真值。
        AddText(Card,Offer.Title,26,DemoUI::Ink)->SetFont(DemoUI::TextFont(26,TEXT("Medium")));
        if(!Branch)
        {
            UTextBlock* Gain=AddText(Card,FString::Printf(TEXT("%+g"),Offer.Gain),54,DemoUI::Gold); // Gain可能为0，仍显示实际服务端收益，不能伪造提升。
            Gain->SetFont(DemoUI::NumberFont(54));
            AddText(Card,FString::Printf(TEXT("当前 %g  →  领取后 %g"),Offer.CurrentValue,Offer.CurrentValue+Offer.Gain),16,DemoUI::Muted);
        }
        else AddText(Card,TEXT("本轮核心分支"),17,DemoUI::Accent);
        AddProgressionRule(Card);
        UTextBlock* Detail=AddText(Card,Offer.Detail,17,DemoUI::Muted); // 服务器生成的实际机制说明完整保留；显式换行避免首帧按单行高度分配。
        Detail->SetAutoWrapText(false); Detail->SetWrapTextAt(Branch?630.f:395.f);
        if(!Offer.BlockReason.IsEmpty())
        {
            UTextBlock* Reason=AddText(Card,Offer.BlockReason,16,DemoUI::Gold); // 拒绝原因使用同一卡内宽度，不藏进悬停Tips。
            Reason->SetAutoWrapText(false); Reason->SetWrapTextAt(Branch?630.f:395.f);
        }
        AddGap(Card,18.f,true); // 并列卡片共享本行高度，把实际确认按钮推到相同底部。
        UDemoMenuButton* Choose=AddAction(Card,FName(*FString::Printf(TEXT("Upgrade%d"),Index)),
            Branch?TEXT("选择此分支"):TEXT("领取此强化"),Offer.BlockReason.IsEmpty()); // 沿用原立即领取契约，不新增未实现的二次确认。
        Choose->SetVisualRole(EDemoButtonVisualRole::Primary); Choose->SetMinDimensions(220.f,52.f);
    }
    if(View.RewardOffers.IsEmpty())
    {
        UE_LOG(LogFPSDemo,Log,TEXT("R04 reward page waiting for owner offers branch=%d"),Branch);
        AddText(PageBody,TEXT("正在等待服务器奖励列表…"),18,DemoUI::Muted);
    }
    AddGap(PageBody,14.f);
    // 显式分为本轮/永久两行，避免“死亡”把个人阵亡观战误写成整轮成长结束；不修改任何清理或保存逻辑。
    AddText(PageBody,TEXT("本轮成长  /  个人阵亡后随队复活保留；整轮通关、全员阵亡重开或主动放弃后清除。\n永久成长  /  金币成长与永久解锁保留。"),16,DemoUI::Muted);
    AddText(PageFooter?static_cast<UPanelWidget*>(PageFooter.Get()):PageBody.Get(),TEXT("选择一项后继续  ·  奖励不可跳过"),16,DemoUI::Accent); // 固定底栏只说明原规则，不增加绕过奖励的返回动作。
    if(!Controller()->GetMenuMessage().IsEmpty()) AddText(PageBody,Controller()->GetMenuMessage(),16,DemoUI::Gold); // 真实拒绝/保存失败持续可读，不播放成功提示代替回执。
}

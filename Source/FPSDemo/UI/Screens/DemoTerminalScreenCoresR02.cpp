// 职责：C03/B05 R02七核心的无框目录、只读本地预览与当前一项完整OwnerOffer。使用：Terminal Core页构建，不更改阶段/服务/保存合同。
// 依赖：S0 Theme/EditorialButton、中央CoreCount目录及PC私有快照；UI.CorePreviewN只查看，原CoreN才明确提交，不伪造图标或分支。
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/R02/DemoR02EditorialButton.h"
#include "UI/R02/DemoUITheme.h"
#include "Player/DemoPlayerController.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Debug/DemoLog.h"

void UDemoTerminalScreen::NoteCoreInspection(int32 Index)
{
    DEMO_LOG_CALL();
    if(Index<0||Index>=DemoRunBuild::CoreCount())
    { UE_LOG(LogFPSDemo,Warning,TEXT("R02_CORES rejected local preview index=%d"),Index); return; }
    SelectedCoreIndex=Index;
    UE_LOG(LogFPSDemo,Log,TEXT("R02_CORES preview=%d; authoritative core/revision/save unchanged"),Index); // 只改显示索引，不触Build组件或交易。
}

int32 UDemoTerminalScreen::GetPresentationCoreIndex() const
{ DEMO_LOG_TICK(); return SelectedCoreIndex; }

void UDemoTerminalScreen::BuildR02Cores()
{
    DEMO_LOG_CALL();
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 当前拥有者真实核心/分支/锁定与完整Offer，不借房主或队友数据。
    const UDemoUITheme* Theme=R02Theme(); // 基类持有的只读资产，不在目录绘制中加载任何外部图片。
    if(!Theme) Theme=GetDefault<UDemoUITheme>(); // 缺资源保可读Token/字体回退，不能当成新字库成功验收。
    const int32 OfferCount=FMath::Min(DemoRunBuild::CoreCount(),View.CoreOffers.Num()); // 中央目录与实际Owner列表同口径，拒绝虚构额外可点击项。
    int32 ViewportWidth=0,ViewportHeight=0; Controller()->GetViewportSize(ViewportWidth,ViewportHeight); // 本机实际物理视口。
    const float Scale=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(this)); // 本次UMG DPI，仅用于显示重排，不修改设备设置。
    const float LayoutWidth=FMath::Clamp(static_cast<float>(ViewportWidth)/Scale-48.f,1000.f,1600.f); // 外壳总48边距，不用固定1600塞入窄屏。
    const float CatalogWidth=300.f,DetailWidth=FMath::Max(360.f,LayoutWidth-64.f-CatalogWidth-48.f); // 目录稳定宽度，右侧余宽留给完整机制/分支预览。
    ConfigureLayout(LayoutWidth,960.f);
    if(UWidget* Wallet=WidgetTree->FindWidget(TEXT("PageWallet"))) Wallet->SetVisibility(ESlateVisibility::Collapsed); // 免费核心不常驻双币账本；切页由Base恢复通用可见性。
    if(UWidget* Eyebrow=WidgetTree->FindWidget(TEXT("PageSectionLabel"))) Eyebrow->SetVisibility(ESlateVisibility::Collapsed); // 移除重复TACTICAL眉题，保留真实核心标题与副题。
    if(auto* Header=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("PageHeaderBounds")))) Header->SetHeightOverride(156.f); // 42号衬线/副题留足预算，不贴分类行；后续页型由Base重置。
    UE_LOG(LogFPSDemo,Log,TEXT("R02_CORES layout viewport=%dx%d dpi=%.3f width=%.1f offers=%d"),ViewportWidth,ViewportHeight,Scale,LayoutWidth,OfferCount);
    SetHeading(TEXT("选择流派核心"),View.Build.CoreId.IsNone()?TEXT("本轮选择一种 · 出发后锁定"):
        FString::Printf(TEXT("当前 %s%s"),*DemoRunBuild::Title(View.Build.CoreId).ToString(),View.Build.bLocked?TEXT(" · 已锁定"):TEXT(" · 安全区可更换"))); // 简述当前真实选择，不把正在预览项冒充已选。
    if(OfferCount==0)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("R02_CORES awaiting owner offers"));
        AddText(PageBody,TEXT("等待服务器核心列表"),20,Theme->Ink); return;
    }
    if(SelectedCoreIndex<0||SelectedCoreIndex>=OfferCount)
    {
        SelectedCoreIndex=0;
        for(int32 Index=0;Index<OfferCount;++Index) if(View.CoreOffers[Index].bSelected) { SelectedCoreIndex=Index; break; } // 同步只读Owner已选标记，不写回或重新选择。
        UE_LOG(LogFPSDemo,Log,TEXT("R02_CORES initial preview=%d from owner selection"),SelectedCoreIndex);
    }
    UHorizontalBox* Columns=AddRow(PageBody); // 左七项简目录，右只显示当前一份完整说明，减少同屏七份长文。
    USizeBox* CatalogBounds=WidgetTree->ConstructWidget<USizeBox>(); CatalogBounds->SetWidthOverride(CatalogWidth); // 目录标题不会被右边长机制挤成极窄列。
    auto* CatalogSlot=Columns->AddChildToHorizontalBox(CatalogBounds); CatalogSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); CatalogSlot->SetPadding(FMargin(0,0,48,0));
    UVerticalBox* Catalog=WidgetTree->ConstructWidget<UVerticalBox>(); CatalogBounds->SetContent(Catalog);
    UTextBlock* DirectoryTitle=AddText(Catalog,TEXT("流派目录"),18,Theme->Muted); // 辅助阅读标签，不新增玩法身份。
    if(auto* TitleSlot=Cast<UVerticalBoxSlot>(DirectoryTitle->Slot)) TitleSlot->SetPadding(FMargin(0,0,0,10));
    for(int32 Index=0;Index<OfferCount;++Index)
    {
        const FDemoShopOfferSnapshot& Offer=View.CoreOffers[Index]; // 名称与已选标记同属本人服务器快照。
        UDemoMenuButton* Preview=AddAction(Catalog,FName(*FString::Printf(TEXT("UI.CorePreview%d"),Index)),FString::Printf(TEXT("%02d  %s"),Index+1,*Offer.Title),true,Index==SelectedCoreIndex); // 被锁定也可读机制；只预览，不提交CoreN。
        if(auto* Editorial=Cast<UDemoR02EditorialButton>(Preview)) Editorial->SetR02Appearance(const_cast<UDemoUITheme*>(Theme),Index==SelectedCoreIndex,24,true);
        Preview->SetMinDimensions(280.f,42.f); Preview->UseSingleLineLabel();
        if(auto* PreviewSlot=Cast<UVerticalBoxSlot>(Preview->Slot)) PreviewSlot->SetPadding(FMargin(0));
        if(Offer.bSelected)
        {
            UTextBlock* Selected=AddText(Catalog,TEXT("本轮已选择"),16,Theme->Ink); // 真选择独立明文；Ember线仍仅代表当前本地目录预览。
            if(auto* SelectedSlot=Cast<UVerticalBoxSlot>(Selected->Slot)) SelectedSlot->SetPadding(FMargin(24,0,0,0));
        }
        AddR02Rule(Catalog,4.f);
    }
    UVerticalBox* Detail=AddColumn(Columns); // 当前一项全文由原PageScroll滚动，不截断完整机制/分支。
    if(auto* DetailSlot=Cast<UHorizontalBoxSlot>(Detail->Slot)) DetailSlot->SetPadding(FMargin(0));
    const FDemoShopOfferSnapshot& Offer=View.CoreOffers[SelectedCoreIndex]; // 详情与明确CoreN动作冻结同一索引/同一OwnerOffer。
    UHorizontalBox* Heading=AddRow(Detail);
    UTextBlock* Number=AddText(Heading,FString::Printf(TEXT("%02d"),SelectedCoreIndex+1),76,Theme->Ink); Number->SetFont(Theme->GetSerifFont(76,TEXT("Regular"),false)); // 大编号是当前预览序号，不是等级/分支阶段。
    if(auto* NumberSlot=Cast<UHorizontalBoxSlot>(Number->Slot)) { NumberSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); NumberSlot->SetPadding(FMargin(0,0,28,0)); }
    UVerticalBox* HeadingText=AddColumn(Heading); // 标题与真已选/锁定状态分层，不让七核心标题堆在同一页。
    if(auto* HeadingTextSlot=Cast<UHorizontalBoxSlot>(HeadingText->Slot)) HeadingTextSlot->SetPadding(FMargin(0));
    UTextBlock* Title=AddText(HeadingText,Offer.Title,38,Theme->Ink); Title->SetFont(Theme->GetSerifFont(38,TEXT("Regular"),true)); Title->SetAutoWrapText(false); Title->SetWrapTextAt(FMath::Max(160.f,DetailWidth-150.f));
    AddText(HeadingText,Offer.bSelected?(View.Build.bLocked?TEXT("本轮已选择 · 已锁定"):TEXT("本轮已选择")):TEXT("正在预览 · 尚未选择"),18,Theme->Ink); // 本地预览与权威选择始终有文字区别。
    AddR02Rule(Detail,12.f);
    const FString Trigger=CoreTriggerLine(Offer.Id); // 一行操作链先于完整说明，便于扫读。
    if(!Trigger.IsEmpty()) { UTextBlock* Chain=AddText(Detail,Trigger,26,Theme->Ink); Chain->SetFont(Theme->GetSerifFont(26,TEXT("Regular"),true)); }
    UTextBlock* Description=AddText(Detail,Offer.Detail,18,Theme->Ink); Description->SetAutoWrapText(false); Description->SetWrapTextAt(DetailWidth-16.f); // 保留原Owner核心机制中的次数/窗口/限制；该报价不含未来分支预览，不用本地臆造补齐。
    if(!Offer.BlockReason.IsEmpty())
    {
        UTextBlock* Reason=AddText(Detail,Offer.BlockReason,18,Theme->Ink); Reason->SetAutoWrapText(false); Reason->SetWrapTextAt(DetailWidth-16.f); // 真拒绝完整显示，禁用视觉不代替原因。
    }
    UPanelWidget* ActionRegion=PageFooter?static_cast<UPanelWidget*>(PageFooter.Get()):Detail; // 明确选择固定页脚，长规则滚动后仍可操作；返回/关闭继续原入口。
    UDemoMenuButton* Confirm=AddCompactAction(ActionRegion,FName(*FString::Printf(TEXT("Core%d"),SelectedCoreIndex)),Offer.bSelected?TEXT("本轮已选择"):TEXT("选择此核心"),Offer.BlockReason.IsEmpty(),220.f); // 原CoreN/BuildRevision/阶段/保存合同，不把预览点击当提交。
    if(auto* Editorial=Cast<UDemoR02EditorialButton>(Confirm)) Editorial->SetR02Appearance(const_cast<UDemoUITheme*>(Theme),Offer.bSelected,24,true);
}

FString UDemoTerminalScreen::CoreTriggerLine(FName CoreId)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] UDemoTerminalScreen::CoreTriggerLine")); // 静态函数无this，按DEMO_LOG_TICK同级别手写埋点。
    // 七核心基础操作链教学文案；按稳定ID匹配，不按数组下标猜测未知核心。数值/限制以服务器Describe为准。
    if(CoreId==TEXT("B01")) return TEXT("枪击短冻  →  近战冰裂");
    if(CoreId==TEXT("B02")) return TEXT("冲刺位移  →  强化枪击");
    if(CoreId==TEXT("B03")) return TEXT("连续弱点  →  锁定射击");
    if(CoreId==TEXT("B04")) return TEXT("装填按R踩准  →  命中供弹");
    if(CoreId==TEXT("B05")) return TEXT("近战破势  →  切枪兑现");
    if(CoreId==TEXT("B06")) return TEXT("G部署锚点  →  充满撤离");
    if(CoreId==TEXT("B07")) return TEXT("受伤开窗  →  反击回收");
    if(CoreId==TEXT("B08")) return TEXT("抵近多丸满压  →  爆膛震波");
    if(CoreId==TEXT("B09")) return TEXT("停火/开镜蓄势  →  重击溢伤");
    return FString();
}

void UDemoTerminalScreen::BuildDepartureCorePicker(UPanelWidget* Parent,float Width)
{
    DEMO_LOG_CALL();
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 本人核心报价/已选/锁定，不读队友。
    UDemoUITheme* Theme=R02Theme(); if(!Theme) Theme=const_cast<UDemoUITheme*>(GetDefault<UDemoUITheme>()); // 编辑式按钮需非const引用，只读使用。
    const int32 OfferCount=FMath::Min(DemoRunBuild::CoreCount(),View.CoreOffers.Num()); // 中央目录与Owner列表同口径。
    UTextBlock* Label=AddText(Parent,View.Build.CoreId.IsNone()?TEXT("① 流派核心 · 出发前必选"):TEXT("① 流派核心"),20,Theme->Ink);
    Label->SetFont(Theme->GetSerifFont(22,TEXT("Regular"),true));
    if(OfferCount==0) { UE_LOG(LogFPSDemo,Log,TEXT("DEPART_CORE awaiting owner offers")); AddText(Parent,TEXT("等待服务器核心列表"),16,Theme->Muted); return; }
    if(SelectedCoreIndex<0||SelectedCoreIndex>=OfferCount)
    {
        SelectedCoreIndex=0;
        for(int32 Index=0;Index<OfferCount;++Index) if(View.CoreOffers[Index].bSelected) { SelectedCoreIndex=Index; break; } // 初始预览=Owner真选择，不写回。
        UE_LOG(LogFPSDemo,Log,TEXT("DEPART_CORE initial preview=%d"),SelectedCoreIndex);
    }
    UHorizontalBox* Row=nullptr; // 每行4项，七项两行；纯预览按钮，不提交CoreN。
    for(int32 Index=0;Index<OfferCount;++Index)
    {
        if(Index%4==0) Row=AddRow(Parent);
        const FDemoShopOfferSnapshot& Offer=View.CoreOffers[Index];
        UDemoMenuButton* Preview=AddAction(Row,FName(*FString::Printf(TEXT("UI.CorePreview%d"),Index)),Offer.bSelected?TEXT("✓ ")+Offer.Title:Offer.Title,true,Index==SelectedCoreIndex);
        if(auto* Editorial=Cast<UDemoR02EditorialButton>(Preview)) Editorial->SetR02Appearance(Theme,Index==SelectedCoreIndex,20,true);
        Preview->SetMinDimensions(FMath::Max(96.f,Width/4.f-16.f),40.f); Preview->UseSingleLineLabel();
    }
    if(Row&&OfferCount%4!=0) for(int32 Empty=OfferCount%4;Empty<4;++Empty) AddColumn(Row); // 空列保持等宽，无按钮/权限。
    const FDemoShopOfferSnapshot& Offer=View.CoreOffers[SelectedCoreIndex]; // 明确选择与预览冻结同一索引。
    UHorizontalBox* Pick=AddRow(Parent);
    UVerticalBox* Info=AddColumn(Pick);
    const FString Trigger=CoreTriggerLine(Offer.Id);
    UTextBlock* Chain=AddText(Info,Trigger.IsEmpty()?Offer.Title:Trigger,18,Theme->Ink); Chain->SetAutoWrapText(false); Chain->SetWrapTextAt(FMath::Max(160.f,Width-260.f));
    if(!Offer.BlockReason.IsEmpty()) { UTextBlock* Reason=AddText(Info,Offer.BlockReason,15,Theme->Muted); Reason->SetAutoWrapText(false); Reason->SetWrapTextAt(FMath::Max(160.f,Width-260.f)); }
    UDemoMenuButton* Confirm=AddCompactAction(Pick,FName(*FString::Printf(TEXT("Core%d"),SelectedCoreIndex)),Offer.bSelected?TEXT("本轮已选择"):TEXT("选择此核心"),Offer.BlockReason.IsEmpty()&&!Offer.bSelected,200.f); // 原CoreN/Revision/保存合同，Flow按出发会话签发终端。
    if(auto* Editorial=Cast<UDemoR02EditorialButton>(Confirm)) Editorial->SetR02Appearance(Theme,Offer.bSelected,22,true);
    UE_LOG(LogFPSDemo,Log,TEXT("DEPART_CORE preview=%d selected=%s locked=%d"),SelectedCoreIndex,*View.Build.CoreId.ToString(),View.Build.bLocked);
}

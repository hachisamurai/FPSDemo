// 职责：菜单布局分区与尺寸策略。使用：DemoMenuScreen在RebuildWidget接入规范WBP壳层，页面只声明导航、内容和主要操作。
// 依赖：UMG布局及DemoMenuButton；仅调整运行时WidgetTree，不编辑uasset、不触碰MenuFlow业务或CommonUI输入栈。
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateNoResource.h"
#include "Debug/DemoLog.h"

void UDemoMenuScreen::BuildLayoutRegions()
{
    DEMO_LOG_CALL();
    if(PageNavigation) return; // 同一实例重建Slate时不可再包一层或更换焦点控件。
    auto* Column=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("PageColumn"))); // 既有资产和原生回退共用的规范容器。
    auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("PageScroll"))); // 原正文滚动区；具名PageBody保持原父关系。
    if(!Column||!Scroll||Scroll->GetParent()!=Column)
    { UE_LOG(LogFPSDemo,Warning,TEXT("UI_LAYOUT custom shell missing PageColumn/PageScroll; keeping designer tree")); return; }
    StyleScrollBox(Scroll);
    Scroll->RemoveFromParent(); // 此时Slate尚未构建；只重排实例树，避免运行中InsertChildAt不刷新Slate的问题。
    if(PageHeading&&PageSubtitle&&PageHeading->GetParent()==Column&&PageSubtitle->GetParent()==Column)
    {
        PageHeading->RemoveFromParent(); PageSubtitle->RemoveFromParent();
        auto* Header=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("PageHeaderBounds")); Header->SetHeightOverride(132.f); // 跨武器/弹药固定高度，长副题不移动分类导航。
        struct FPreservedColumnSlot // 函数内同步保存Designer装饰的原顺序/槽设置；UWidget仍由WidgetTree保活，不跨帧借用。
        {
            UWidget* Content; FSlateChildSize Size; FMargin Padding; EHorizontalAlignment Horizontal; EVerticalAlignment Vertical;
        };
        TArray<FPreservedColumnSlot> Decorations; // UE的InsertChildAt仅WITH_EDITOR，本路径必须在非Editor同样可用。
        for(UWidget* Child:Column->GetAllChildren())
            if(const auto* ExistingSlot=Cast<UVerticalBoxSlot>(Child->Slot)) Decorations.Add({Child,ExistingSlot->GetSize(),ExistingSlot->GetPadding(),ExistingSlot->GetHorizontalAlignment(),ExistingSlot->GetVerticalAlignment()});
        Column->ClearChildren(); Column->AddChildToVerticalBox(Header); // Slate尚未生成；用运行时Add/Remove API建立相同顺序。
        for(const FPreservedColumnSlot& Decoration:Decorations)
        {
            UVerticalBoxSlot* Restored=Column->AddChildToVerticalBox(Decoration.Content); // 只重挂当前实例中的原装饰，不重新创建或覆盖WBP。
            Restored->SetSize(Decoration.Size); Restored->SetPadding(Decoration.Padding); Restored->SetHorizontalAlignment(Decoration.Horizontal); Restored->SetVerticalAlignment(Decoration.Vertical);
        }
        auto* HeaderRow=WidgetTree->ConstructWidget<UHorizontalBox>(); Header->SetContent(HeaderRow); // 左标题/右本人钱包分开布局。
        auto* TitleColumn=WidgetTree->ConstructWidget<UVerticalBox>(); HeaderRow->AddChildToHorizontalBox(TitleColumn)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        PageSectionLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("PageSectionLabel"));
        FSlateFontInfo SectionFont=DemoUI::DisplayFont(16,TEXT("Medium")); SectionFont.LetterSpacing=140; // UE5.4字距属于FontInfo，单位为1/1000 em。
        PageSectionLabel->SetFont(SectionFont); PageSectionLabel->SetColorAndOpacity(DemoUI::Gold);
        TitleColumn->AddChildToVerticalBox(PageSectionLabel)->SetPadding(FMargin(0,0,0,8));
        TitleColumn->AddChildToVerticalBox(PageHeading)->SetPadding(FMargin(0,0,0,8));
        TitleColumn->AddChildToVerticalBox(PageSubtitle);
        auto* HeaderRight=WidgetTree->ConstructWidget<UVerticalBox>(); HeaderRow->AddChildToHorizontalBox(HeaderRight)->SetPadding(FMargin(24,0,0,0)); // 钱包与返回独立成右列，不再占正文前一整行。
        PageWallet=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("PageWallet"));
        PageWallet->SetFont(DemoUI::DisplayFont(22,TEXT("Medium"))); PageWallet->SetColorAndOpacity(DemoUI::Muted);
        HeaderRight->AddChildToVerticalBox(PageWallet)->SetHorizontalAlignment(HAlign_Right);
        PageHeaderActions=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("PageHeaderActions"));
        auto* HeaderActionSlot=HeaderRight->AddChildToVerticalBox(PageHeaderActions); HeaderActionSlot->SetHorizontalAlignment(HAlign_Right); HeaderActionSlot->SetPadding(FMargin(0,16,0,0));
        PageWallet->SetVisibility(bIsModal?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    }
    PageNavigation=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("PageNavigation"));
    Column->AddChildToVerticalBox(PageNavigation)->SetPadding(FMargin(0,4,0,12));
    PageCategories=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("PageCategories"));
    Column->AddChildToVerticalBox(PageCategories)->SetPadding(FMargin(0,0,0,16));
    auto* Content=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("PageContent")); // 两个独立滚动区；右栏不受左栏滚动影响。
    auto* ContentSlot=Column->AddChildToVerticalBox(Content); ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    Content->AddChildToHorizontalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    AsideBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("AsideBounds")); AsideBounds->SetWidthOverride(300.f);
    Content->AddChildToHorizontalBox(AsideBounds)->SetPadding(FMargin(24,0,0,0));
    auto* Surface=WidgetTree->ConstructWidget<UBorder>(); Surface->SetPadding(FMargin(12,0,0,0)); Surface->SetBrushColor(FLinearColor::Transparent); AsideBounds->SetContent(Surface); // 右栏靠留白组织，不再重复铺实体灰框。
    auto* AsideScroll=WidgetTree->ConstructWidget<UScrollBox>(); AsideScroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always); StyleScrollBox(AsideScroll); Surface->SetContent(AsideScroll); // 极长错误提示仍有滚动出口。
    PageAside=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PageAside")); AsideScroll->AddChild(PageAside);
    AsideBounds->SetVisibility(ESlateVisibility::Collapsed);
    PageFooter=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("PageFooter"));
    Column->AddChildToVerticalBox(PageFooter)->SetPadding(FMargin(0,16,0,0));
}

void UDemoMenuScreen::StyleScrollBox(UScrollBox* Scroll)
{
    DEMO_LOG_CALL();
    if(!Scroll) { UE_LOG(LogFPSDemo,Warning,TEXT("R04 scroll style skipped null widget")); return; }
    FScrollBarStyle Bar=FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>(TEXT("ScrollBar")); // 复制引擎样式，不修改全局或旧资产。
    Bar.NormalThumbImage.TintColor=FSlateColor(DemoUI::Muted.CopyWithNewOpacity(.22f));
    Bar.HoveredThumbImage.TintColor=FSlateColor(DemoUI::Ink.CopyWithNewOpacity(.45f));
    Bar.DraggedThumbImage.TintColor=FSlateColor(DemoUI::Ink.CopyWithNewOpacity(.7f));
    Bar.SetHorizontalBackgroundImage(FSlateNoResource()).SetVerticalBackgroundImage(FSlateNoResource());
    Scroll->SetWidgetBarStyle(Bar); Scroll->SetScrollbarThickness(FVector2D(3.f,3.f)); Scroll->SetScrollbarPadding(FMargin(4,0)); Scroll->SetAlwaysShowScrollbar(false); // 细灰条仍可拖动，不侵入内容或改变权限。
}

void UDemoMenuScreen::ConfigureLayout(float Width,float Height,bool LeftAligned)
{
    DEMO_LOG_CALL();
    if(PageWallet) PageWallet->SetVisibility(bIsModal||Width<1000.f?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible); // 窄暂停/设置确认页让出标题宽度；钱包不挤压固定导航。
    if(auto* Bounds=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("PanelBounds"))))
    {
        Bounds->SetWidthOverride(Width); Bounds->SetHeightOverride(Height);
        if(auto* ScaleSlot=Cast<UScaleBoxSlot>(Bounds->Slot)) ScaleSlot->SetHorizontalAlignment(LeftAligned?HAlign_Left:HAlign_Center);
    }
}

UDemoMenuButton* UDemoMenuScreen::AddCompactAction(UPanelWidget* Parent,FName Action,const FString& Caption,bool Enabled,float Width)
{
    DEMO_LOG_CALL();
    auto* Button=AddAction(Parent,Action,Caption,Enabled); // 保留同一按钮注册及白名单路径，只改变槽尺寸。
    Button->SetMinDimensions(Width,46.f); Button->UseSingleLineLabel(); // Auto按真实文本宽度增长，长中文返回标签不被最小宽度硬折行。
    if(auto* LayoutSlot=Cast<UHorizontalBoxSlot>(Button->Slot)) LayoutSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    return Button;
}

void UDemoMenuScreen::AddBack(FName Action,const FString& Caption,bool Enabled)
{
    DEMO_LOG_CALL();
    AddHeaderAction(Action,Caption,Enabled);
}
UDemoMenuButton* UDemoMenuScreen::AddHeaderAction(FName Action,const FString& Caption,bool Enabled,float Width)
{
    DEMO_LOG_CALL();
    const auto* Bounds=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("PanelBounds"))); // 当前页面已配置宽度，不按上一页或视口外猜尺寸。
    UPanelWidget* Region=PageHeaderActions&&!bIsModal&&Bounds&&Bounds->GetWidthOverride()>=1000.f?static_cast<UPanelWidget*>(PageHeaderActions):PageNavigation?static_cast<UPanelWidget*>(PageNavigation):PageBody; // 仅宽页移到标题右侧；窄页保留完整标题宽。
    return AddCompactAction(Region,Action,Caption,Enabled,Width);
}

UDemoMenuButton* UDemoMenuScreen::AddFooterAction(FName Action,const FString& Caption,bool Enabled,float Width)
{
    DEMO_LOG_CALL();
    UDemoMenuButton* Button=AddCompactAction(PageFooter?static_cast<UPanelWidget*>(PageFooter):PageBody,Action,Caption,Enabled,Width); // 真实CommonUI按钮沿原注册路径。
    Button->SetVisualRole(Action==TEXT("Close")||Action==TEXT("PauseQuit")?EDemoButtonVisualRole::Standard:EDemoButtonVisualRole::Primary); return Button; // 返回/退出为次级操作，避免与真实购买/应用争夺主CTA。
}

UVerticalBox* UDemoMenuScreen::AddCard(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    auto* Card=WidgetTree->ConstructWidget<UBorder>(); Card->SetPadding(FMargin(20,16)); Card->SetBrushColor(DemoUI::Raised); // R04轻透明分组，交互始终是内部CommonUI按钮。
    auto* LayoutSlot=Parent->AddChild(Card); // 由父容器拥有，不保留业务引用。
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(LayoutSlot)) { Horizontal->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Horizontal->SetPadding(FMargin(8,8)); }
    if(auto* Vertical=Cast<UVerticalBoxSlot>(LayoutSlot)) Vertical->SetPadding(FMargin(4,8));
    auto* Contents=WidgetTree->ConstructWidget<UVerticalBox>(); Card->SetContent(Contents);
    return Contents;
}

void UDemoMenuScreen::AddGap(UPanelWidget* Parent,float Height,bool Stretch)
{
    DEMO_LOG_CALL();
    auto* Space=WidgetTree->ConstructWidget<USpacer>(); Space->SetSize(FVector2D(1,Height)); // 不参与焦点与命中。
    auto* LayoutSlot=Parent->AddChild(Space);
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(LayoutSlot)) Horizontal->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    if(Stretch) if(auto* Vertical=Cast<UVerticalBoxSlot>(LayoutSlot)) Vertical->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

UVerticalBox* UDemoMenuScreen::ShowAside()
{
    DEMO_LOG_CALL();
    if(AsideBounds) AsideBounds->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    return PageAside?PageAside.Get():PageBody.Get(); // 自定义Designer壳层无分区时保留原内容可访问性。
}

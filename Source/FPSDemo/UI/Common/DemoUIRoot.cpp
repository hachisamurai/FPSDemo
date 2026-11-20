// 职责：构造真实CommonUI菜单/模态栈，不复制Flow页面或经济状态。使用：每个本地PlayerScreen一个根。
#include "UI/Common/DemoUIRoot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "UI/DemoUIStyle.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Debug/DemoLog.h"
TSharedRef<SWidget> UDemoUIRoot::RebuildWidget()
{
    DEMO_LOG_CALL(); if(!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    if(!MenuStack) MenuStack=Cast<UCommonActivatableWidgetStack>(WidgetTree->FindWidget(TEXT("MenuStack")));
    if(!ModalStack) ModalStack=Cast<UCommonActivatableWidgetStack>(WidgetTree->FindWidget(TEXT("ModalStack")));
    if(!MenuStack||!ModalStack)
    {
        auto* Layers=WidgetTree->ConstructWidget<UOverlay>(); // 层序即模态优先级，无全局共享静态Widget。
        MenuStack=WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(),TEXT("MenuStack"));
        ModalStack=WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(),TEXT("ModalStack"));
        for(auto* Stack:{MenuStack.Get(),ModalStack.Get()}) { auto* LayerSlot=Layers->AddChildToOverlay(Stack); LayerSlot->SetHorizontalAlignment(HAlign_Fill); LayerSlot->SetVerticalAlignment(VAlign_Fill); Stack->SetTransitionDuration(0); }
        WidgetTree->RootWidget=Layers;
    }
    if(auto* Layers=Cast<UOverlay>(WidgetTree->RootWidget))
    {
        if(!FeedbackSurface)
        {
            auto* Bounds=WidgetTree->ConstructWidget<USizeBox>(); Bounds->SetMaxDesiredWidth(620.f); // 顶部消息独立于标题/钱包，不拉伸成整屏遮挡。
            auto* FeedbackOverlaySlot=Layers->AddChildToOverlay(Bounds); FeedbackOverlaySlot->SetHorizontalAlignment(HAlign_Center); FeedbackOverlaySlot->SetVerticalAlignment(VAlign_Top); FeedbackOverlaySlot->SetPadding(FMargin(24,24,24,0)); // 避免遮蔽UWidget::Slot。
            FeedbackSurface=WidgetTree->ConstructWidget<UBorder>(); FeedbackSurface->SetBrushColor(DemoUI::Surface.CopyWithNewOpacity(.96f)); FeedbackSurface->SetPadding(FMargin(20,10)); Bounds->SetContent(FeedbackSurface);
            FeedbackLabel=WidgetTree->ConstructWidget<UTextBlock>(); FeedbackLabel->SetFont(DemoUI::TextFont(17,TEXT("Medium"))); FeedbackLabel->SetAutoWrapText(false); FeedbackLabel->SetWrapTextAt(580.f); FeedbackSurface->SetContent(FeedbackLabel);
            Bounds->SetVisibility(ESlateVisibility::HitTestInvisible); FeedbackSurface->SetVisibility(ESlateVisibility::Collapsed);
        }
        if(!WidgetTree->FindWidget(TEXT("R04Brand")))
        {
            auto* Brand=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("R04Brand")); // 本地根持有的纯装饰，不注册输入/业务事件。
            FSlateFontInfo BrandFont=DemoUI::DisplayFont(22,TEXT("SemiBold")); BrandFont.LetterSpacing=150; // 仅品牌字距，不改页面DPI或输入几何。
            Brand->SetText(FText::FromString(TEXT("◇  BREACH"))); Brand->SetFont(BrandFont); Brand->SetColorAndOpacity(DemoUI::Ink); Brand->SetVisibility(ESlateVisibility::HitTestInvisible);
            auto* BrandSlot=Layers->AddChildToOverlay(Brand); BrandSlot->SetHorizontalAlignment(HAlign_Left); BrandSlot->SetVerticalAlignment(VAlign_Top); BrandSlot->SetPadding(FMargin(64,32,0,0)); // 留在面板上方，不挤压滚动/页签。
        }
    }
    return Super::RebuildWidget();
}
void UDemoUIRoot::SetFeedback(const FString& Message,bool Warning)
{
    DEMO_LOG_TICK(); if(!FeedbackSurface||!FeedbackLabel) return;
    FeedbackLabel->SetText(FText::FromString(Message)); FeedbackLabel->SetColorAndOpacity(bR02Presentation?FLinearColor(FColor(233,227,215)):(Warning?DemoUI::Gold:DemoUI::Accent));
    FeedbackSurface->SetVisibility(Message.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
void UDemoUIRoot::SetR02Presentation(bool Enabled)
{
    DEMO_LOG_TICK(); if(bR02Presentation==Enabled) return;
    bR02Presentation=Enabled;
    UE_LOG(LogFPSDemo,Log,TEXT("R02 root presentation=%d"),Enabled);
    if(UWidget* Brand=WidgetTree?WidgetTree->FindWidget(TEXT("R04Brand")):nullptr) Brand->SetVisibility(Enabled?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(FeedbackSurface) FeedbackSurface->SetBrushColor(Enabled?FLinearColor(FColor(13,12,11)):DemoUI::Surface.CopyWithNewOpacity(.96f));
    if(Enabled&&FeedbackLabel) FeedbackLabel->SetColorAndOpacity(FLinearColor(FColor(233,227,215)));
}

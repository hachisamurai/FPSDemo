// 职责：S1/S2菜单壳层与物理像素细线的R02适配。使用：Screen按明确页型选择；依赖规范WBP具名节点、共享主题和引擎Slate。
// 只修改运行时实例字体/颜色/留白，不改uasset、菜单路由、交易、存档或其他页默认外观。
#include "UI/Common/DemoMenuScreen.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/DemoUIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"
#include "Components/NativeWidgetHost.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Debug/DemoLog.h"

namespace
{
    /** 当前WidgetTree的Host持有本Slate装饰；节点只复制颜色，不引用Theme/Actor或异步对象。 */
    class SDemoR02PageRule final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SDemoR02PageRule) : _Color(FLinearColor::White) {}
            SLATE_ARGUMENT(FLinearColor,Color) // 只读含Alpha线色，不代表业务或输入状态。
        SLATE_END_ARGS()
        /** 同步构建复制颜色，Arguments不跨帧持有。 */
        void Construct(const FArguments& Arguments)
        { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] SDemoR02PageRule.Construct")); RuleColor=Arguments._Color; SetVisibility(EVisibility::HitTestInvisible); }
        /** 预排版只预留局部空间；真正厚度在Paint由完整累计缩放换算。 */
        virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
        { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] SDemoR02PageRule.ComputeDesiredSize")); return FVector2D(1.f,2.f); }
        /** 引擎Paint借用几何/绘制列表；两端绝对像素对齐，线厚严格1物理像素。 */
        virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle& Style,bool ParentEnabled) const override
        {
            UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] SDemoR02PageRule.OnPaint"));
            const float Scale=FMath::Max(.01f,Geometry.GetAccumulatedLayoutTransform().GetScale()); // 已含DPI/ScaleBox，本组件不支持旋转。
            const FVector2D Start=Geometry.LocalToAbsolute(FVector2D::ZeroVector),End=Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize().X,0)); // 只借本次真实几何。
            const FVector2D LocalStart=Geometry.AbsoluteToLocal(FVector2D(FMath::RoundToFloat(Start.X),FMath::RoundToFloat(Start.Y)));
            const float Width=FMath::Max(0.f,(FMath::RoundToFloat(End.X)-FMath::RoundToFloat(Start.X))/Scale);
            FSlateDrawElement::MakeBox(Elements,LayerId,Geometry.ToPaintGeometry(FVector2D(Width,DemoUIR02::PhysicalHairlineThickness(Scale)),FSlateLayoutTransform(LocalStart)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,RuleColor*Style.GetColorAndOpacityTint());
            return LayerId;
        }
    private:
        FLinearColor RuleColor=FLinearColor::White; // 节点拥有的显示副本，随Host销毁释放。
    };
}

void UDemoMenuScreen::AddR02Rule(UPanelWidget* Parent,float RulePadding)
{
    DEMO_LOG_CALL();
    if(!Parent||!WidgetTree) { UE_LOG(LogFPSDemo,Warning,TEXT("R02 rule rejected missing parent/tree")); return; }
    const auto* Theme=R02Theme(); // 页面CDO/实例强持有，只借当前构建。
    if(!Theme) Theme=GetDefault<UDemoUITheme>();
    auto* Rule=WidgetTree->ConstructWidget<UNativeWidgetHost>(); Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
    Rule->SetContent(SNew(SDemoR02PageRule).Color(Theme->Ink.CopyWithNewOpacity(Theme->HairlineOpacity)));
    if(auto* RuleSlot=Cast<UVerticalBoxSlot>(Parent->AddChild(Rule))) RuleSlot->SetPadding(FMargin(0,FMath::Clamp(RulePadding,0.f,64.f)));
}

bool UDemoMenuScreen::UsesR02Presentation() const
{ DEMO_LOG_TICK(); return false; }
bool UDemoMenuScreen::IsUsingR02Presentation() const
{ DEMO_LOG_TICK(); return UsesR02Presentation(); }
UDemoUITheme* UDemoMenuScreen::R02Theme() const
{
    DEMO_LOG_CALL();
    if(!bR02ThemeResolved)
    {
        bR02ThemeResolved=true;
        LoadedR02Theme=LoadObject<UDemoUITheme>(nullptr,TEXT("/Game/UI/RedesignR02/Theme/DA_R02CinemaTheme_S0R02.DA_R02CinemaTheme_S0R02"));
        if(!LoadedR02Theme) UE_LOG(LogFPSDemo,Warning,TEXT("R02 menu theme missing; using native fallback"));
    }
    return LoadedR02Theme;
}
void UDemoMenuScreen::ApplyPagePresentation()
{
    DEMO_LOG_CALL();
    const bool bR02=UsesR02Presentation(); // Terminal池复用时按当前页型重设，B08～B11不继承R02。
    const auto* Theme=bR02?R02Theme():nullptr;
    if(auto* Backdrop=Cast<UBorder>(WidgetTree->RootWidget))
    { Backdrop->SetBrushColor(Theme?Theme->Base:(bIsModal?DemoUI::Overlay:DemoUI::Surface.CopyWithNewOpacity(.92f))); Backdrop->SetPadding(FMargin(24.f)); }
    if(auto* Header=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("PageHeaderBounds")))) { Header->SetHeightOverride(132.f); Header->SetVisibility(ESlateVisibility::SelfHitTestInvisible); } // Session池在Saves/Pause/Guide等页型间复用，先恢复共用壳层。
    if(PageHeading) { PageHeading->SetVisibility(ESlateVisibility::HitTestInvisible); PageHeading->SetJustification(ETextJustify::Left); }
    if(PageSubtitle) { PageSubtitle->SetVisibility(ESlateVisibility::HitTestInvisible); PageSubtitle->SetJustification(ETextJustify::Left); }
    if(PageSectionLabel) { PageSectionLabel->SetVisibility(ESlateVisibility::HitTestInvisible); PageSectionLabel->SetJustification(ETextJustify::Left); } // 奖励居中/大厅折叠不泄漏给同池下一页。
    if(auto* Surface=Cast<UBorder>(WidgetTree->FindWidget(TEXT("PanelSurface"))))
    {
        Surface->SetBrushColor(bR02?FLinearColor::Transparent:(bIsModal?DemoUI::Surface.CopyWithNewOpacity(.96f):DemoUI::Surface.CopyWithNewOpacity(.32f)));
        Surface->SetPadding(FMargin(bR02?32.f:(bIsModal?36.f:40.f),28.f));
    }
    if(PageHeading) { PageHeading->SetFont(Theme?Theme->GetSerifFont(bIsModal?32:42,TEXT("Regular"),true):DemoUI::TextFont(bIsModal?28:34,TEXT("Medium"))); PageHeading->SetColorAndOpacity(Theme?Theme->Ink:DemoUI::Ink); }
    if(PageSubtitle) { PageSubtitle->SetFont(Theme?Theme->GetLabelFont(18):DemoUI::TextFont(16)); PageSubtitle->SetColorAndOpacity(Theme?Theme->Ink:DemoUI::Muted); }
    if(PageSectionLabel) { FSlateFontInfo SectionFont=Theme?Theme->GetLabelFont(16,TEXT("Regular"),true):DemoUI::DisplayFont(16,TEXT("Medium")); SectionFont.LetterSpacing=140; PageSectionLabel->SetFont(SectionFont); PageSectionLabel->SetColorAndOpacity(Theme?Theme->Muted:DemoUI::Gold); }
    if(PageWallet) { PageWallet->SetFont(Theme?Theme->GetLabelFont(20):DemoUI::DisplayFont(22,TEXT("Medium"))); PageWallet->SetColorAndOpacity(Theme?Theme->Ink:DemoUI::Muted); }
    // 规范旧壳可能包含纯装饰色条；只在S1变体隐藏，切回旧页恢复，不删除作者节点。
    for(const FName Name:{FName(TEXT("HeadingRule")),FName(TEXT("AccentLine"))})
        if(UWidget* Decoration=WidgetTree->FindWidget(Name)) Decoration->SetVisibility(bR02?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}

// 职责：构造R02文字按钮的真实UMG/CommonUI内容，按独立已选/焦点状态绘制细线，不触碰旧R04资源。
// 边界：仅本实例持有Theme和WidgetTree引用；未提供Screen的S0展示实例不能经原白名单执行交易，正式Screen仍沿父类NativeOnClicked。
#include "UI/R02/DemoR02EditorialButton.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Debug/DemoLog.h"
#include "UI/R02/DemoUITheme.h"
#include "Widgets/WidgetPixelSnapping.h"

UDemoR02EditorialButton::UDemoR02EditorialButton(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    DEMO_LOG_CALL();
    bUseNativeStateColors = false;
    bUseNativeTypography = false;
}

bool UDemoR02EditorialButton::Initialize()
{
    DEMO_LOG_CALL();
    const bool bBlueprintWidget = Cast<UWidgetBlueprintGeneratedClass>(GetClass()) != nullptr; // 蓝图Designer树只能由其生成类复制。
    if (!bBlueprintWidget && !HasAnyFlags(RF_ClassDefaultObject))
    {
        if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
        if (!WidgetTree->RootWidget)
        {
            UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("R02ButtonVisualRoot")); // 单一内容根交CommonUI包实际SButton，不另造点击层。
            R02Surface = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ButtonSurface"));
            R02Surface->SetBrushColor(FLinearColor::Transparent);
            R02Surface->SetPadding(FMargin(24.f, 6.f, 8.f, 6.f)); // 左侧给焦点刻度留空；隐形热区另由MinDimensions保证。
            R02Caption = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Caption"));
            R02Caption->SetVisibility(ESlateVisibility::HitTestInvisible);
            R02Caption->SetAutoWrapText(false);
            R02Caption->SetJustification(ETextJustify::Left);
            R02Surface->SetContent(R02Caption);
            UOverlaySlot* SurfaceSlot = Root->AddChildToOverlay(R02Surface); // 树拥有子节点，装饰和文字覆盖相同真实按钮区域。
            SurfaceSlot->SetHorizontalAlignment(HAlign_Fill);
            SurfaceSlot->SetVerticalAlignment(VAlign_Fill);

            SelectionLineBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("R02SelectionBounds"));
            SelectionLineBounds->SetHeightOverride(1.f);
            SelectionLineBounds->SetVisibility(ESlateVisibility::HitTestInvisible);
            SelectionLine = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("R02SelectionLine"));
            SelectionLine->SetPadding(FMargin(0.f));
            SelectionLine->SetPixelSnapping(EWidgetPixelSnapping::SnapToPixel);
            SelectionLineBounds->SetContent(SelectionLine);
            UOverlaySlot* SelectionSlot = Root->AddChildToOverlay(SelectionLineBounds); // 底线持续表示展示已选，与真实焦点刻度分离。
            SelectionSlot->SetHorizontalAlignment(HAlign_Fill);
            SelectionSlot->SetVerticalAlignment(VAlign_Bottom);
            SelectionSlot->SetPadding(FMargin(24.f, 0.f, 8.f, 0.f));

            FocusTickBounds = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("R02FocusTickBounds"));
            FocusTickBounds->SetWidthOverride(10.f);
            FocusTickBounds->SetHeightOverride(1.f);
            FocusTickBounds->SetVisibility(ESlateVisibility::HitTestInvisible);
            FocusTick = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("R02FocusTick"));
            FocusTick->SetPadding(FMargin(0.f));
            FocusTick->SetPixelSnapping(EWidgetPixelSnapping::SnapToPixel);
            FocusTickBounds->SetContent(FocusTick);
            UOverlaySlot* TickSlot = Root->AddChildToOverlay(FocusTickBounds); // 左侧短刻度只代表CommonUI真实焦点，不使用整框。
            TickSlot->SetHorizontalAlignment(HAlign_Left);
            TickSlot->SetVerticalAlignment(VAlign_Center);
            TickSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
            WidgetTree->RootWidget = Root;
        }
    }
    const bool bInitializedThisCall = Super::Initialize(); // 父类保持CommonUI内部委托/默认确认键生命周期，局部名不遮蔽UUserWidget状态。
    if (bInitializedThisCall)
    {
        ResolveR02Widgets();
        SetIsFocusable(true);
        SetMinDimensions(180.f, 42.f);
        RefreshR02Visuals();
    }
    return bInitializedThisCall;
}

TSharedRef<SWidget> UDemoR02EditorialButton::RebuildWidget()
{
    DEMO_LOG_CALL();
    TSharedRef<SWidget> Result = Super::RebuildWidget();
    ResolveR02Widgets();
    RefreshR02Visuals();
    return Result;
}

void UDemoR02EditorialButton::SetR02Appearance(UDemoUITheme* Theme, bool Selected, int32 Size, bool bChinese)
{
    DEMO_LOG_CALL();
    R02Theme = Theme;
    bR02Selected = Selected;
    R02FontSize = FMath::Clamp(Size, 1, 256);
    bPreferChineseFont = bChinese;
    if (!Theme) UE_LOG(LogFPSDemo, Warning, TEXT("R02_BUTTON missing theme; using local default colors and packaged Sans"));
    RefreshR02Visuals();
}

void UDemoR02EditorialButton::SetR02CardMode(bool bInCardMode)
{
    DEMO_LOG_CALL();
    bR02CardMode = bInCardMode;
    RefreshR02Visuals();
}

bool UDemoR02EditorialButton::IsR02Selected() const { DEMO_LOG_TICK(); return bR02Selected; }
bool UDemoR02EditorialButton::HasR02FocusHighlight() const { DEMO_LOG_TICK(); return bR02Focus; }

void UDemoR02EditorialButton::ResolveR02Widgets()
{
    DEMO_LOG_CALL();
    R02Caption = WidgetTree ? Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Caption"))) : nullptr;
    R02Surface = WidgetTree ? Cast<UBorder>(WidgetTree->FindWidget(TEXT("ButtonSurface"))) : nullptr;
    SelectionLine = WidgetTree ? Cast<UBorder>(WidgetTree->FindWidget(TEXT("R02SelectionLine"))) : nullptr;
    SelectionLineBounds = WidgetTree ? Cast<USizeBox>(WidgetTree->FindWidget(TEXT("R02SelectionBounds"))) : nullptr;
    FocusTick = WidgetTree ? Cast<UBorder>(WidgetTree->FindWidget(TEXT("R02FocusTick"))) : nullptr;
    FocusTickBounds = WidgetTree ? Cast<USizeBox>(WidgetTree->FindWidget(TEXT("R02FocusTickBounds"))) : nullptr;
    if (!R02Caption || !R02Surface) UE_LOG(LogFPSDemo, Warning, TEXT("R02_BUTTON missing named caption/surface; retaining designer tree"));
}

void UDemoR02EditorialButton::RefreshR02Visuals()
{
    DEMO_LOG_CALL();
    const UDemoUITheme* Theme = R02Theme ? R02Theme.Get() : GetDefault<UDemoUITheme>(); // CDO只供缺主题回退，不自动切换任何全局主题。
    const bool bEnabled = GetIsEnabled(); // 真实权限只读；禁用理由由所属页面给完整文字。
    const bool bHighlight = bEnabled && (bR02Focus || bR02Hover || bR02Selected); // 卡片模式下悬停/焦点/已选均触发清晰高亮。
    if (R02Surface)
    {
        if (bR02CardMode)
        {
            const float CardAlpha = !bEnabled ? .015f : bR02Pressed ? .18f : (bR02Focus || bR02Hover) ? .11f : bR02Selected ? .08f : .03f; // 整卡区域在悬停/选中时明显提亮，常态保微弱轮廓。
            R02Surface->SetBrushColor(Theme->Ink.CopyWithNewOpacity(CardAlpha));
        }
        else R02Surface->SetBrushColor(FLinearColor::Transparent);
    }
    if (R02Caption)
    {
        R02Caption->SetFont(Theme->GetSerifFont(R02FontSize, bR02Focus ? FName(TEXT("SemiBold")) : FName(TEXT("Regular")), bPreferChineseFont));
        R02Caption->SetJustification(ETextJustify::Left);
        R02Caption->SetAutoWrapText(false);
        R02Caption->SetColorAndOpacity(bEnabled ? Theme->Ink.CopyWithNewOpacity(bR02Focus || bR02Hover ? 1.f : .88f) : Theme->Disabled);
        R02Caption->SetRenderTranslation(FVector2D::ZeroVector); // R02不沿R04焦点位移，避免细线/字体重新亚像素模糊。
    }
    if (SelectionLine)
    {
        const bool bShowLine = bHighlight; // 悬停/焦点/已选均点亮底部余烬红线，使菜单切换选项时红线同步跟随。
        SelectionLine->SetBrushColor(Theme->Ember.CopyWithNewOpacity(bShowLine ? (bR02Pressed ? .7f : 1.f) : 0.f));
    }
    if (FocusTick) FocusTick->SetBrushColor(Theme->Ink.CopyWithNewOpacity(bEnabled && bR02Focus ? 1.f : 0.f));
    UpdateLineScale(LastEffectiveScale);
}

void UDemoR02EditorialButton::UpdateLineScale(float EffectiveScale)
{
    DEMO_LOG_TICK();
    LastEffectiveScale = EffectiveScale;
    const float Thickness = DemoUIR02::PhysicalHairlineThickness(EffectiveScale); // 完整几何缩放，不假定只有UI DPI。
    if (SelectionLineBounds) SelectionLineBounds->SetHeightOverride(bR02CardMode && (bR02Focus || bR02Hover || bR02Selected) ? Thickness * 2.f : Thickness);
    if (FocusTickBounds) FocusTickBounds->SetHeightOverride(Thickness);
}

void UDemoR02EditorialButton::HandleFocusReceived()
{
    DEMO_LOG_CALL(); bR02Focus = true; Super::HandleFocusReceived(); RefreshR02Visuals();
}
void UDemoR02EditorialButton::HandleFocusLost()
{
    DEMO_LOG_CALL(); bR02Focus = false; bR02Hover = false; bR02Pressed = false; Super::HandleFocusLost(); RefreshR02Visuals();
}
void UDemoR02EditorialButton::NativeOnHovered()
{
    DEMO_LOG_CALL(); bR02Hover = true; Super::NativeOnHovered();
    if (GetIsEnabled() && IsInteractionEnabled() && !IsDesignTime())
    {
        if (APlayerController* PC = GetOwningPlayer())
        {
            if (!HasUserFocus(PC) && !HasUserFocusedDescendants(PC)) SetUserFocus(PC); // 悬停同步转移真实焦点，使上一初始焦点项红线立即熄灭。
        }
    }
    RefreshR02Visuals();
}
void UDemoR02EditorialButton::NativeOnUnhovered()
{
    DEMO_LOG_CALL(); bR02Hover = false; Super::NativeOnUnhovered(); RefreshR02Visuals();
}
void UDemoR02EditorialButton::NativeOnPressed()
{
    DEMO_LOG_CALL(); bR02Pressed = true; Super::NativeOnPressed(); RefreshR02Visuals();
}
void UDemoR02EditorialButton::NativeOnReleased()
{
    DEMO_LOG_CALL(); bR02Pressed = false; Super::NativeOnReleased(); RefreshR02Visuals();
}
void UDemoR02EditorialButton::NativeOnEnabled()
{
    DEMO_LOG_CALL(); Super::NativeOnEnabled(); RefreshR02Visuals();
}
void UDemoR02EditorialButton::NativeOnDisabled()
{
    DEMO_LOG_CALL(); bR02Pressed = false; Super::NativeOnDisabled(); RefreshR02Visuals();
}
void UDemoR02EditorialButton::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    DEMO_LOG_TICK();
    Super::NativeTick(MyGeometry, InDeltaTime);
    const float EffectiveScale = MyGeometry.GetAccumulatedLayoutTransform().GetScale(); // 最终父级布局缩放已含DPI与ScaleBox，暂无R02 RenderTransform位移。
    if (!FMath::IsNearlyEqual(EffectiveScale, LastEffectiveScale)) UpdateLineScale(EffectiveScale);
}
void UDemoR02EditorialButton::NativeDestruct()
{
    DEMO_LOG_CALL(); bR02Focus = false; bR02Hover = false; bR02Pressed = false; RefreshR02Visuals(); Super::NativeDestruct();
}

// 职责：真实CommonUI按钮的UMG内容、状态视觉与动作转发。使用：原生构建或蓝图Designer树均由CommonButtonBase包裹绑定。
// 边界：R04角色只更新具名控件/不可命中装饰；原生树应用主题字体，Designer树与输入仍由CommonUI持有。
// 状态反馈无需Tick/定时器，焦点确认经原动作绑定，失焦即撤销，不改变白名单、页面门禁或交易。
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIPresentation.h" // 本机限频短音；按下只反馈操作，不伪造业务成功。
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoUIManager.h"
#include "Player/DemoPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "CommonInputSettings.h"
#include "ICommonInputModule.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Debug/DemoLog.h"

UDemoMenuButtonStyle::UDemoMenuButtonStyle()
{
    DEMO_LOG_CALL();
    bSingleMaterial=false;
    NormalBase=FSlateNoResource(); NormalHovered=FSlateNoResource(); NormalPressed=FSlateNoResource();
    SelectedBase=FSlateNoResource(); SelectedHovered=FSlateNoResource(); SelectedPressed=FSlateNoResource();
    Disabled=FSlateNoResource(); SingleMaterialBrush=FSlateNoResource();
    ButtonPadding=FMargin(0.f); CustomPadding=FMargin(0.f); MinWidth=30; MinHeight=30; // UE取max(Style,实例)：普通实例仍为40×42，数字棋盘才允许30px下限。
}
UDemoMenuButton::UDemoMenuButton(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    DEMO_LOG_CALL();
    Style=UDemoMenuButtonStyle::StaticClass(); // 仅指定默认样式；Initialize之后CommonUI自己BuildStyles，不访问尚未创建的RootButton。
}

bool UDemoMenuButton::Initialize()
{
    DEMO_LOG_CALL();
    const bool bBlueprintWidget=Cast<UWidgetBlueprintGeneratedClass>(GetClass())!=nullptr; // 检测真实生成类，不能用WidgetTree是否为空来猜来源。
    if(!bBlueprintWidget&&!HasAnyFlags(RF_ClassDefaultObject))
    {
        // CommonButtonBase::Initialize只在Super之后已有RootWidget时绑定内部点击/焦点，因此原生内容必须先建立。
        if(!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
        if(!WidgetTree->RootWidget)
        {
            auto* VisualRoot=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("ButtonVisualRoot")); // 仅原生树添加装饰，CommonUI仍只包一个真实SButton。
            auto* Surface=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ButtonSurface")); // 实例控件树拥有，UMG而非Canvas热区。
            Surface->SetPadding(FMargin(10,8)); Surface->SetBrushColor(NormalSurfaceColor);
            CaptionWidget=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("Caption"));
            CaptionWidget->SetFont(DemoUI::TextFont(18,TEXT("Medium"))); CaptionWidget->SetJustification(ETextJustify::Center);
            CaptionWidget->SetAutoWrapText(true); CaptionWidget->SetColorAndOpacity(FSlateColor(DemoUI::Ink));
            CaptionWidget->SetVisibility(ESlateVisibility::HitTestInvisible); Surface->SetContent(CaptionWidget);
            auto* SurfaceSlot=VisualRoot->AddChildToOverlay(Surface); // 主题底板与文字填满同一CommonUI内容区，未增加另一层点击。
            SurfaceSlot->SetHorizontalAlignment(HAlign_Fill); SurfaceSlot->SetVerticalAlignment(VAlign_Fill);
            StateRailBounds=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ButtonStateRailBounds")); // 原生左线或底线，既不参与焦点也不截获鼠标。
            StateRailWidget=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ButtonStateRail")); StateRailWidget->SetPadding(FMargin(0));
            StateRailWidget->SetBrushColor(DemoUI::Gold); StateRailBounds->SetContent(StateRailWidget); StateRailBounds->SetVisibility(ESlateVisibility::HitTestInvisible);
            VisualRoot->AddChildToOverlay(StateRailBounds);
            FocusFrameWidget=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ButtonFocusFrame")); // 持续焦点框与Selected状态线独立，避免只用色相表示焦点。
            FocusFrameWidget->SetPadding(FMargin(0)); FocusFrameWidget->SetVisibility(ESlateVisibility::HitTestInvisible); FocusFrameWidget->SetBrush(FSlateNoResource()); // 未聚焦时不提交任何轮廓绘制，不能用透明度隐藏独立Outline。
            auto* FocusSlot=VisualRoot->AddChildToOverlay(FocusFrameWidget); // 最上层装饰只绘制，命中仍归CommonUI根。
            FocusSlot->SetHorizontalAlignment(HAlign_Fill); FocusSlot->SetVerticalAlignment(VAlign_Fill); FocusSlot->SetPadding(FMargin(2));
            bNativeContentBuilt=true; WidgetTree->RootWidget=VisualRoot;
        }
    }
    // 蓝图分支不提前创建WidgetTree：UUserWidget::Initialize会复制Designer树，随后CommonUI包裹实际Root。
    const bool bInitializedThisCall=Super::Initialize(); // 保留引擎重复初始化/CDO返回false的契约，不重复构造或绑定。
    if(bInitializedThisCall) { ResolveVisualWidgets(); RefreshVisualState(); }
    else UE_LOG(LogFPSDemo,Log,TEXT("UI_BUTTON Initialize skipped: already initialized or class default"));
    return bInitializedThisCall;
}
TSharedRef<SWidget> UDemoMenuButton::RebuildWidget()
{
    DEMO_LOG_CALL();
    TSharedRef<SWidget> Result=Super::RebuildWidget(); // CommonUI持有实际Slate按钮，本类不另加可点击层。
    ResolveVisualWidgets(); RefreshVisualState();
    return Result;
}
void UDemoMenuButton::ResolveVisualWidgets()
{
    DEMO_LOG_CALL();
    CaptionWidget=WidgetTree?Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Caption"))):nullptr;
    ButtonSurfaceWidget=WidgetTree?Cast<UBorder>(WidgetTree->FindWidget(TEXT("ButtonSurface"))):nullptr;
    FocusFrameWidget=WidgetTree?Cast<UBorder>(WidgetTree->FindWidget(TEXT("ButtonFocusFrame"))):nullptr;
    StateRailWidget=WidgetTree?Cast<UBorder>(WidgetTree->FindWidget(TEXT("ButtonStateRail"))):nullptr;
    StateRailBounds=WidgetTree?Cast<USizeBox>(WidgetTree->FindWidget(TEXT("ButtonStateRailBounds"))):nullptr;
    if(CaptionWidget&&bHasConfiguredCaption) CaptionWidget->SetText(FText::FromString(Label));
    ApplyTypographyAndLayout();
    if(!CaptionWidget||!ButtonSurfaceWidget) UE_LOG(LogFPSDemo,Log,TEXT("UI_BUTTON optional named visuals missing: caption=%d surface=%d; designer tree preserved"),CaptionWidget!=nullptr,ButtonSurfaceWidget!=nullptr);
}
void UDemoMenuButton::RefreshVisualState()
{
    DEMO_LOG_CALL();
    if(!bUseNativeStateColors) return; // 蓝图已明确采用自己的视觉动画时，仍保留全部真实输入回调。
    const bool bVisualEnabled=GetIsEnabled(); // 只读取现有按钮权限，不把视觉选中或焦点反向写入权限。
    const bool bPrimary=VisualRole==EDemoButtonVisualRole::Primary; // 主要提交角色，不从Selected反推真实业务意义。
    const bool bQuiet=VisualRole==EDemoButtonVisualRole::Tab||VisualRole==EDemoButtonVisualRole::Catalog||VisualRole==EDemoButtonVisualRole::Menu; // 导航与目录减少重复矩形底板。
    FLinearColor SurfaceColor=!bVisualEnabled?DisabledSurfaceColor:bFocusHighlighted?FocusSurfaceColor:bPointerHighlighted?HoverSurfaceColor:bMarkedSelected?SelectedSurfaceColor:NormalSurfaceColor; // 禁用>焦点>悬停>选中>普通。
    FLinearColor TextColor=!bVisualEnabled?DemoUI::Muted.CopyWithNewOpacity(.48f):bMarkedSelected||bFocusHighlighted||bPointerHighlighted?DemoUI::Ink:DemoUI::Ink.CopyWithNewOpacity(.88f); // 不再让选中变高饱和金/青，状态通过线、文字和明度共同表达。
    if(bQuiet)
    {
        const float Opacity=!bVisualEnabled?.015f:bPressHighlighted?.10f:bFocusHighlighted?.07f:bPointerHighlighted?.055f:bMarkedSelected?.04f:VisualRole==EDemoButtonVisualRole::Menu?.012f:0.f; // 轻填充保留背景，数字只是纯表现Alpha。
        SurfaceColor=DemoUI::Ink.CopyWithNewOpacity(Opacity);
        if(bVisualEnabled&&!bMarkedSelected&&!bFocusHighlighted&&!bPointerHighlighted) TextColor=DemoUI::Muted;
    }
    else if(bPrimary)
    {
        SurfaceColor=!bVisualEnabled?DemoUI::Primary.CopyWithNewOpacity(.16f):bPressHighlighted?DemoUI::Primary*FLinearColor(.82f,.82f,.82f,1.f):bPointerHighlighted||bFocusHighlighted?DemoUI::PrimaryHovered:DemoUI::Primary;
        TextColor=!bVisualEnabled?DemoUI::Muted.CopyWithNewOpacity(.55f):DemoUI::PrimaryInk;
    }
    else if(bVisualEnabled&&bPressHighlighted) SurfaceColor=DemoUI::Ink.CopyWithNewOpacity(.18f);
    if(ButtonSurfaceWidget) ButtonSurfaceWidget->SetBrushColor(SurfaceColor); // 保留Designer的Brush、边距和布局，只修改其颜色。
    if(CaptionWidget)
    {
        CaptionWidget->SetColorAndOpacity(FSlateColor(TextColor));
        CaptionWidget->SetRenderTranslation(bVisualEnabled?FVector2D(VisualRole==EDemoButtonVisualRole::Menu&&(bPointerHighlighted||bFocusHighlighted)?4.f:0.f,bPressHighlighted?1.f:0.f):FVector2D::ZeroVector); // 只移动不可命中文字，真实按钮热区与页签锚点保持固定。
    }
    if(FocusFrameWidget)
    {
        // UE5.4 RoundedBox的Outline默认不继承InTint.A；SetRenderOpacity(0)仍可能提交不透明轮廓。
        // 非焦点使用NoResource让SBorder跳过绘制，保留Overlay槽与命中区域，不靠改Alpha掩盖问题。
        if(bVisualEnabled&&bFocusHighlighted)
        {
            const FLinearColor Outline=bPrimary?DemoUI::Surface.CopyWithNewOpacity(.8f):DemoUI::Ink.CopyWithNewOpacity(.92f); // 白CTA上的深轮廓与其他按钮的骨白轮廓均有明暗差。
            FocusFrameWidget->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent,0.f,Outline,1.5f));
        }
        else FocusFrameWidget->SetBrush(FSlateNoResource());
        FocusFrameWidget->SetRenderOpacity(1.f); // 开关由Brush决定，禁止再以RenderOpacity隐藏独立Outline。
    }
    if(StateRailWidget)
    {
        StateRailWidget->SetBrushColor(DemoUI::Gold.CopyWithNewOpacity(bMarkedSelected?1.f:.6f));
        StateRailWidget->SetRenderOpacity(bVisualEnabled&&bQuiet&&(bMarkedSelected||bPointerHighlighted||bFocusHighlighted)?1.f:0.f);
    }
}
void UDemoMenuButton::SetVisualRole(EDemoButtonVisualRole Role)
{
    DEMO_LOG_CALL();
    if(static_cast<uint8>(Role)>static_cast<uint8>(EDemoButtonVisualRole::Menu))
    { UE_LOG(LogFPSDemo,Warning,TEXT("UI_BUTTON invalid visual role=%d; retaining current role"),static_cast<uint8>(Role)); return; }
    VisualRole=Role; ApplyTypographyAndLayout(); RefreshVisualState();
}
EDemoButtonVisualRole UDemoMenuButton::GetVisualRole() const { DEMO_LOG_TICK(); return VisualRole; }
void UDemoMenuButton::ApplyTypographyAndLayout()
{
    DEMO_LOG_CALL();
    const bool bLeftAligned=VisualRole==EDemoButtonVisualRole::Catalog||VisualRole==EDemoButtonVisualRole::Menu; // 左对齐仅组织内容，不移动真实按钮或改变最小宽高。
    if(CaptionWidget&&bUseNativeTypography)
    {
        CaptionWidget->SetFont(DemoUI::TextFont(18,TEXT("Medium")));
        CaptionWidget->SetJustification(bLeftAligned?ETextJustify::Left:ETextJustify::Center);
        CaptionWidget->SetAutoWrapText(!bGridDensity&&!bSingleLineLabel); CaptionWidget->SetWrapTextAt(0.f);
    }
    if(ButtonSurfaceWidget&&bNativeContentBuilt) ButtonSurfaceWidget->SetPadding(bGridDensity?FMargin(4,2):VisualRole==EDemoButtonVisualRole::Tab?FMargin(8,10):bLeftAligned?FMargin(18,10):FMargin(14,10)); // 原生角色内边距；保留Designer树自身的Padding。
    if(StateRailBounds)
    {
        const bool bBottomRail=VisualRole==EDemoButtonVisualRole::Tab; // 页签底线，目录/大厅侧线。
        if(bBottomRail) { StateRailBounds->ClearWidthOverride(); StateRailBounds->SetHeightOverride(2.f); }
        else { StateRailBounds->SetWidthOverride(2.f); StateRailBounds->ClearHeightOverride(); }
        if(auto* RailSlot=Cast<UOverlaySlot>(StateRailBounds->Slot))
        {
            RailSlot->SetHorizontalAlignment(bBottomRail?HAlign_Fill:HAlign_Left); RailSlot->SetVerticalAlignment(bBottomRail?VAlign_Bottom:VAlign_Fill);
            RailSlot->SetPadding(bBottomRail?FMargin(6,0,6,0):FMargin(0,10,0,10));
        }
    }
}
void UDemoMenuButton::Configure(UDemoMenuScreen* Screen,FName Action,const FString& Caption,bool Enabled,bool Selected)
{
    DEMO_LOG_CALL(); ScreenOwner=Screen; Intent=Action; bMarkedSelected=Selected; SetCaption(Caption); SetIsFocusable(true); SetMinDimensions(40,42); SetIsEnabled(Enabled);
    RefreshVisualState();
}
void UDemoMenuButton::RefreshFocusAcceptBinding()
{
    DEMO_LOG_CALL();
    const bool bNeedsDefaultAccept=bFocusHighlighted&&GetIsEnabled()&&IsInteractionEnabled()&&!IsDesignTime()&&GetOwningLocalPlayer(); // 只有实际有焦点且可操作的运行时按钮借用确认键。
    if(!bNeedsDefaultAccept)
    {
        if(!FocusAcceptAction.IsNull())
        {
            const bool bStillOwned=TriggeringInputAction==FocusAcceptAction; // 蓝图若已经显式换过快捷键，不撤销其新绑定。
            FocusAcceptAction=FDataTableRowHandle(); // 先撤销本地所有权，再触发CommonUI解绑/蓝图回调，避免重入借用旧值。
            if(bStillOwned) SetTriggeringInputAction(FDataTableRowHandle());
            UE_LOG(LogFPSDemo,Log,TEXT("UI_BUTTON focus accept unbound action=%s"),*Intent.ToString());
        }
        return;
    }
    if(!FocusAcceptAction.IsNull()||!TriggeringInputAction.IsNull()||!TriggeredInputAction.IsNull()||TriggeringEnhancedInputAction) return; // 保留Designer显式快捷键及其优先级。
    const FDataTableRowHandle DefaultAccept=ICommonInputModule::GetSettings().GetDefaultClickAction(); // 当前项目配置Enter/A，不硬编码或伪造键事件。
    if(DefaultAccept.IsNull())
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("UI_BUTTON missing CommonInput default click action; keyboard confirm unavailable")); return;
    }
    FocusAcceptAction=DefaultAccept; // 注册前标记所有权，SetTriggeringInputAction内的蓝图通知允许同步重入。
    // UE5.4 SCommonButton::OnKeyDown/OnKeyUp对Enter明确Unhandled；默认ClickAction只负责图标，不自动创建动作绑定。
    // 原生CommonUI路由随后执行HandleTriggeringActionCommited→HandleButtonClicked→NativeOnClicked，仍经过原页面/权限验证。
    SetTriggeringInputAction(DefaultAccept);
    UE_LOG(LogFPSDemo,Log,TEXT("UI_BUTTON focus accept bound action=%s row=%s"),*Intent.ToString(),*DefaultAccept.RowName.ToString());
}
FName UDemoMenuButton::GetAction() const { DEMO_LOG_TICK(); return Intent; }
void UDemoMenuButton::UseGridDensity()
{
    DEMO_LOG_CALL();
    bGridDensity=true; SetMinDimensions(30.f,30.f); ApplyTypographyAndLayout();
}
void UDemoMenuButton::UseSingleLineLabel()
{
    DEMO_LOG_CALL();
    bSingleLineLabel=true; ApplyTypographyAndLayout();
}
void UDemoMenuButton::SetCaption(const FString& Caption) { DEMO_LOG_TICK(); bHasConfiguredCaption=true; Label=Caption; if(CaptionWidget) CaptionWidget->SetText(FText::FromString(Label)); }
void UDemoMenuButton::NativeOnHovered()
{
    DEMO_LOG_CALL(); bPointerHighlighted=true; Super::NativeOnHovered(); RefreshVisualState();
    if(ScreenOwner.IsValid()&&GetIsEnabled()) DemoUI::PlayFeedback(ScreenOwner->Controller(),EDemoUISound::Hover);
}
void UDemoMenuButton::NativeOnUnhovered()
{
    DEMO_LOG_CALL(); bPointerHighlighted=false; Super::NativeOnUnhovered(); RefreshVisualState();
}
void UDemoMenuButton::NativeOnPressed()
{
    DEMO_LOG_CALL(); bPressHighlighted=true; Super::NativeOnPressed(); RefreshVisualState();
}
void UDemoMenuButton::NativeOnReleased()
{
    DEMO_LOG_CALL(); bPressHighlighted=false; Super::NativeOnReleased(); RefreshVisualState();
}
void UDemoMenuButton::HandleFocusReceived()
{
    DEMO_LOG_CALL(); bFocusHighlighted=true; Super::HandleFocusReceived(); RefreshVisualState(); RefreshFocusAcceptBinding();
    if(ScreenOwner.IsValid()&&GetIsEnabled()) DemoUI::PlayFeedback(ScreenOwner->Controller(),EDemoUISound::Hover);
}
void UDemoMenuButton::HandleFocusLost()
{
    DEMO_LOG_CALL(); bFocusHighlighted=false; bPressHighlighted=false; Super::HandleFocusLost(); RefreshVisualState(); RefreshFocusAcceptBinding();
}
void UDemoMenuButton::NativeOnEnabled()
{
    DEMO_LOG_CALL(); Super::NativeOnEnabled(); RefreshVisualState(); RefreshFocusAcceptBinding();
}
void UDemoMenuButton::NativeOnDisabled()
{
    DEMO_LOG_CALL(); bPressHighlighted=false; Super::NativeOnDisabled(); RefreshVisualState(); RefreshFocusAcceptBinding();
}
void UDemoMenuButton::NativeDestruct()
{
    DEMO_LOG_CALL(); bPointerHighlighted=false; bFocusHighlighted=false; bPressHighlighted=false; RefreshVisualState(); RefreshFocusAcceptBinding(); Super::NativeDestruct();
}
void UDemoMenuButton::NativeOnClicked()
{
    DEMO_LOG_CALL();
    const auto* PC=ScreenOwner.IsValid()?ScreenOwner->Controller():nullptr; const auto* UI=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 当前页面/输入所有者，不缓存。
    if(!IsInteractionEnabled()||!GetIsEnabled()||!UI||!UI->IsTopScreen(ScreenOwner.Get())||ScreenOwner->FindAction(Intent)!=this) { UE_LOG(LogFPSDemo,Log,TEXT("UI_BUTTON rejected disabled/expired/underlying")); return; }
    const bool Returning=Intent==TEXT("UI.Back")||Intent==TEXT("OverlayBack")||Intent==TEXT("Close")||Intent==TEXT("GambleNo")||Intent==TEXT("AmmoBuyNo")||Intent==TEXT("WeaponTipClose")||Intent==TEXT("Casino.BetCancel")||Intent==TEXT("DisplayNo")||Intent==TEXT("CreateNo")||Intent==TEXT("HubNo"); // 返回按钮与键盘Back使用同一语义去重，避免同帧叠两声。
    DemoUI::PlayFeedback(ScreenOwner->Controller(),Returning?EDemoUISound::Back:EDemoUISound::Press);
    ScreenOwner->SubmitAction(Intent); Super::NativeOnClicked();
}
#if WITH_EDITOR
bool UDemoMenuButton::PressForTest()
{
    DEMO_LOG_CALL();
    const auto* PC=ScreenOwner.IsValid()?ScreenOwner->Controller():nullptr; const auto* UI=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 与真实点击相同的当前层检查。
    if(!IsInteractionEnabled()||!GetIsEnabled()||!UI||!UI->IsTopScreen(ScreenOwner.Get())||ScreenOwner->FindAction(Intent)!=this) return false;
    NativeOnClicked(); return true;
}
#endif

// 职责：可复用UMG页面布局、焦点和安全意图适配。使用：CommonUI栈激活子页面；UIManager与MenuFlow保有生命周期/业务边界。
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/R02/DemoR02EditorialButton.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/DemoUIStyle.h" // R04统一主题与本地复合字体；业务状态仍属于Flow/OwnerSnapshot。
#include "UI/DemoUIPresentation.h" // 本机减少动效，不影响菜单栈切换或服务器等待。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Input/UIActionBindingHandle.h"
#include "CommonInputModeTypes.h"
#include "Styling/CoreStyle.h"
#include "Debug/DemoLog.h"
#include "HAL/PlatformTime.h" // 入场使用本机单调秒，单人暂停仍平稳完成。
#include "UObject/ConstructorHelpers.h"

UDemoMenuScreen::UDemoMenuScreen(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    DEMO_LOG_CALL(); bIsBackHandler=true; bAutoRestoreFocus=true; SetIsFocusable(true);
    static ConstructorHelpers::FObjectFinder<UDemoUITheme> Theme(TEXT("/Game/UI/RedesignR02/Theme/DA_R02CinemaTheme_S0R02.DA_R02CinemaTheme_S0R02")); // CDO硬引用当前候选字体/主题，运行期不依赖外部文件。
    LoadedR02Theme=Theme.Object; bR02ThemeResolved=LoadedR02Theme!=nullptr;
    if(!LoadedR02Theme) UE_LOG(LogFPSDemo,Warning,TEXT("R02 menu CDO theme missing; fallback remains available"));
}
ADemoPlayerController* UDemoMenuScreen::Controller() const { DEMO_LOG_TICK(); return Cast<ADemoPlayerController>(GetOwningPlayer()); }
UDemoMenuFlowComponent* UDemoMenuScreen::Flow() const { DEMO_LOG_TICK(); const auto* PC=Controller(); return PC?PC->GetMenuFlow():nullptr; }
TSharedRef<SWidget> UDemoMenuScreen::RebuildWidget()
{
    DEMO_LOG_CALL();
    if(!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    if(!PageBody) PageBody=Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("PageBody")));
    if(!PageBody)
    {
        auto* Backdrop=WidgetTree->ConstructWidget<UBorder>(); // 全屏模态遮罩捕获指针，阻止点击世界或Canvas。
        Backdrop->SetBrushColor(bIsModal?DemoUI::Overlay:FLinearColor::Transparent); Backdrop->SetPadding(FMargin(24));
        auto* Scale=WidgetTree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit); Scale->SetStretchDirection(EStretchDirection::DownOnly); Backdrop->SetContent(Scale);
        auto* Size=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("PanelBounds")); Size->SetWidthOverride(PanelWidth); Size->SetHeightOverride(820.f); Scale->SetContent(Size);
        auto* Surface=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("PanelSurface")); Surface->SetBrushColor(SurfaceColor); Surface->SetPadding(FMargin(24)); Size->SetContent(Surface); // 与Designer规范外壳共用具名主题入口。
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PageColumn")); Surface->SetContent(Column);
        PageHeading=AddText(Column,TEXT(""),28,FLinearColor(.96f,.96f,.94f)); PageSubtitle=AddText(Column,TEXT(""),16,FLinearColor(1.f,.76f,.28f));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("PageScroll")); Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
        auto* ScrollSlot=Column->AddChildToVerticalBox(Scroll); ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); ScrollSlot->SetPadding(FMargin(0,12,0,0));
        PageBody=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PageBody")); Scroll->AddChild(PageBody);
        WidgetTree->RootWidget=Backdrop;
    }
    if(!PageHeading) PageHeading=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PageHeading")));
    if(!PageSubtitle) PageSubtitle=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PageSubtitle")));
    if(auto* Backdrop=Cast<UBorder>(WidgetTree->RootWidget)) Backdrop->SetBrushColor(bIsModal?DemoUI::Overlay:DemoUI::Surface.CopyWithNewOpacity(.92f)); // R04深钛灰遮罩隔开高亮白模，避免场景标签穿透；大厅在BuildBody显式恢复背景取景。
    if(auto* Surface=Cast<UBorder>(WidgetTree->FindWidget(TEXT("PanelSurface"))))
    {
        Surface->SetBrushColor(bIsModal?DemoUI::Surface.CopyWithNewOpacity(.96f):DemoUI::Surface.CopyWithNewOpacity(.32f));
        Surface->SetPadding(FMargin(bIsModal?36.f:40.f,28.f)); // 独立面板透明度不作用于文字/按钮；保留世界可见。
    }
    if(PageHeading) { PageHeading->SetFont(DemoUI::TextFont(bIsModal?28:34,TEXT("Medium"))); PageHeading->SetColorAndOpacity(DemoUI::Ink); }
    if(PageSubtitle) { PageSubtitle->SetFont(DemoUI::TextFont(16)); PageSubtitle->SetColorAndOpacity(DemoUI::Muted); }
    BuildLayoutRegions(); // 规范壳层只补充运行时区域，原有WBP与具名正文保持有效。
    return Super::RebuildWidget();
}
void UDemoMenuScreen::RefreshScreen(bool Force)
{
    DEMO_LOG_TICK(); if(!PageBody||!Flow()) return;
    const FString Key=RefreshKey(); const bool FirstBuild=LastRefreshKey.IsEmpty(); // 栈激活先于首次内容构建，构建完成后补一次焦点请求。
    if(!Force&&LastRefreshKey==Key) return;
    if(FirstBuild) FocusAction=NAME_None; // 复用池重新打开页面采用安全初始焦点；同一次打开的倒计时刷新继续恢复用户选择。
    bool HadFocus=false; // 只恢复本页已有焦点，不抢上层模态焦点。
    for(const auto& Pair:ActionButtons) if(Pair.Value&&(Pair.Value->HasUserFocus(GetOwningPlayer())||Pair.Value->HasAnyUserFocus()||Pair.Value->HasUserFocusedDescendants(GetOwningPlayer()))) { FocusAction=Pair.Key; HadFocus=true; break; }
    if(CaptureExtraFocusBeforeRebuild()) HadFocus=true; // 允许包含输入框的子页在后台刷新重建前保存输入焦点。
    const FString PreviousHeading=PageHeading?PageHeading->GetText().ToString():FString(); // 数值/钱包更新不能重复播放整页入场。
    LastRefreshKey=Key; ActionButtons.Reset(); ActionOrder.Reset(); PageBody->ClearChildren();
    ApplyPagePresentation(); // 页型可在同一Terminal实例切换，必须在新内容构建前更新壳层。
    if(PageNavigation) PageNavigation->ClearChildren();
    if(PageHeaderActions) PageHeaderActions->ClearChildren();
    if(PageCategories) PageCategories->ClearChildren();
    if(PageFooter) { PageFooter->ClearChildren(); AddGap(PageFooter); }
    if(PageAside) PageAside->ClearChildren();
    if(AsideBounds) AsideBounds->SetVisibility(ESlateVisibility::Collapsed);
    ConfigureLayout(bIsModal?PanelWidth:1600.f,bIsModal?590.f:835.f); BuildBody(); // 共用R04工作区；具体窄页可显式覆盖，正文仍独立滚动。
    if(FirstBuild||(PageHeading&&PageHeading->GetText().ToString()!=PreviousHeading)) PageTransitionStart=DemoUI::ReducedMotion()?-1.:FPlatformTime::Seconds();
    if(PageNavigation) PageNavigation->SetVisibility(PageNavigation->GetChildrenCount()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(PageCategories) PageCategories->SetVisibility(PageCategories->GetChildrenCount()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(PageFooter) PageFooter->SetVisibility(PageFooter->GetChildrenCount()>1?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    UpdatePageTransition();
    if((HadFocus||FirstBuild)&&IsActivated()) RequestRefreshFocus();
}
void UDemoMenuScreen::SetHeading(const FString& Heading,const FString& Subtitle)
{
    DEMO_LOG_CALL();
    if(PageHeading) PageHeading->SetText(FText::FromString(Heading));
    if(PageSubtitle) PageSubtitle->SetText(FText::FromString(Subtitle));
    if(PageSectionLabel)
    {
        const TCHAR* Section=Heading.Contains(TEXT("武器"))?TEXT("FIELD ARMORY"):Heading.Contains(TEXT("弹药"))?TEXT("AMMUNITION"):Heading.Contains(TEXT("强化"))||Heading.Contains(TEXT("升级"))?TEXT("PERSONAL UPGRADES"):Heading.Contains(TEXT("存档"))?TEXT("OPERATION ARCHIVE"):Heading.Contains(TEXT("设置"))?TEXT("SYSTEM PREFERENCES"):Heading.Contains(TEXT("确认"))?TEXT("FIELD CONFIRMATION"):TEXT("TACTICAL OPERATIONS"); // 静态英文仅作区域身份，不作为业务判断。
        PageSectionLabel->SetText(FText::FromString(Section));
    }
    if(PageWallet&&Controller())
    {
        const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 只读本人权威投影，不创建第二钱包。
        PageWallet->SetText(FText::FromString(FString::Printf(TEXT("金币  %s     银币  %s"),*FText::AsNumber(View.Progress.Coins).ToString(),*FText::AsNumber(View.Progress.SilverCoins).ToString())));
    }
}
UTextBlock* UDemoMenuScreen::AddText(UPanelWidget* Parent,const FString& Text,int32 Size,FLinearColor Color)
{
    DEMO_LOG_CALL(); auto* Label=WidgetTree->ConstructWidget<UTextBlock>(); Label->SetText(FText::FromString(Text));
    const auto* Theme=UsesR02Presentation()?R02Theme():nullptr; // 只影响当前S1页，其他页继续原字体/颜色。
    Label->SetFont(Theme?(Size>=26?Theme->GetSerifFont(Size,TEXT("Regular"),true):Theme->GetLabelFont(Size)):DemoUI::TextFont(Size));
    const bool bLegacyEmphasis=Color==DemoUI::Gold||Color==DemoUI::Accent||Color==FLinearColor(FColor(240,237,230)); // 只转换旧语义Token，子页明确传入的R02色保持原值。
    Label->SetColorAndOpacity(FSlateColor(Theme?(Color==DemoUI::Muted?Theme->Muted:(bLegacyEmphasis?Theme->Ink:Color)):Color)); Label->SetAutoWrapText(true);
    if(Parent==PageAside.Get()&&AsideBounds)
    {
        // 固定侧栏在首次Prepass就知道宽度；AutoWrap依赖上次Paint宽度，会短暂按一行分配高度而绘制多行。
        // 预留两侧18px内边距、16px滚动条、文本8px边距，共60px；不靠延迟一帧或截断正文掩盖重叠。
        Label->SetAutoWrapText(false); Label->SetWrapTextAt(FMath::Max(80.f,AsideBounds->GetWidthOverride()-60.f));
    }
    auto* LayoutSlot=Parent->AddChild(Label); // 槽布局由父容器决定，避免固定像素坐标。
    if(auto* Vertical=Cast<UVerticalBoxSlot>(LayoutSlot)) Vertical->SetPadding(FMargin(4,5));
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(LayoutSlot)) { Horizontal->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Horizontal->SetPadding(FMargin(8,6)); Horizontal->SetVerticalAlignment(VAlign_Center); }
    return Label;
}
UDemoMenuButton* UDemoMenuScreen::AddAction(UPanelWidget* Parent,FName Action,const FString& Caption,bool Enabled,bool Selected)
{
    DEMO_LOG_CALL();
    const bool bR02=UsesR02Presentation(); // 仅当前S1页面创建新外观；WidgetTree Outer让CommonUI正确找到本页输入根。
    UDemoMenuButton* Button=bR02?CreateWidget<UDemoR02EditorialButton>(this):CreateWidget<UDemoMenuButton>(GetOwningPlayer(),UDemoMenuButton::StaticClass());
    if(auto* Editorial=Cast<UDemoR02EditorialButton>(Button)) Editorial->SetR02Appearance(R02Theme(),Selected,22,true);
    Button->Configure(this,Action,Caption,Enabled,Selected); ActionButtons.Add(Action,Button); ActionOrder.Add(Action);
    if(Parent==PageCategories.Get()) Button->SetVisualRole(EDemoButtonVisualRole::Tab); // 固定页签只改变表现，不扩大权限。
    auto* LayoutSlot=Parent->AddChild(Button); // 页面拥有，控件树释放后不保留旧操作。
    if(auto* Vertical=Cast<UVerticalBoxSlot>(LayoutSlot)) Vertical->SetPadding(FMargin(4,7));
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(LayoutSlot)) { Horizontal->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Horizontal->SetPadding(FMargin(6,4)); Horizontal->SetVerticalAlignment(VAlign_Center); }
    return Button;
}
UHorizontalBox* UDemoMenuScreen::AddRow(UPanelWidget* Parent)
{ DEMO_LOG_CALL(); auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(); Parent->AddChild(Row); return Row; }
UVerticalBox* UDemoMenuScreen::AddColumn(UPanelWidget* Parent)
{
    DEMO_LOG_CALL(); auto* Column=WidgetTree->ConstructWidget<UVerticalBox>(); auto* LayoutSlot=Parent->AddChild(Column); // 父布局拥有新列的槽。
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(LayoutSlot)) { Horizontal->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Horizontal->SetPadding(FMargin(8)); }
    return Column;
}
UImage* UDemoMenuScreen::AddIcon(UPanelWidget* Parent,UTexture2D* Texture,float Size)
{
    DEMO_LOG_CALL();
    auto* Image=WidgetTree->ConstructWidget<UImage>(); // 当前页面树拥有；仅复用已加载图标，不同步加载资产。
    Image->SetBrushFromTexture(Texture);
    const FVector2D SourceSize=Texture?FVector2D(FMath::Max(1,Texture->GetSizeX()),FMath::Max(1,Texture->GetSizeY())):FVector2D(1.f); // 缺图仍保留可预测的方形占位。
    const float Scale=FMath::Max(1.f,Size)/FMath::Max(SourceSize.X,SourceSize.Y); // DPI前最大边长，保持纹理原始宽高比。
    Image->SetDesiredSizeOverride(SourceSize*Scale);
    auto* LayoutSlot=Parent->AddChild(Image); // DesiredSize不是布局上限，父槽Fill会再次拉伸纹理，必须同时约束对齐。
    if(auto* Vertical=Cast<UVerticalBoxSlot>(LayoutSlot)) { Vertical->SetHorizontalAlignment(HAlign_Left); Vertical->SetVerticalAlignment(VAlign_Center); }
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(LayoutSlot)) { Horizontal->SetHorizontalAlignment(HAlign_Left); Horizontal->SetVerticalAlignment(VAlign_Center); }
    return Image;
}
UDemoMenuButton* UDemoMenuScreen::FindAction(FName Action) const { DEMO_LOG_TICK(); const auto* Found=ActionButtons.Find(Action); return Found?Found->Get():nullptr; }
void UDemoMenuScreen::SubmitAction(FName Action)
{
    DEMO_LOG_CALL(); auto* PC=Controller(); auto* Manager=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr;
    if(!Manager||!Manager->IsTopScreen(this)||!FindAction(Action)||!FindAction(Action)->GetIsEnabled()) { UE_LOG(LogFPSDemo,Log,TEXT("COMMON_UI rejected hidden/disabled action=%s"),*Action.ToString()); return; }
    Manager->SubmitAction(Action); // 只转意图；刷新在下一次协调器同步执行，不在点击回调中销毁控件。
}
TOptional<FUIInputConfig> UDemoMenuScreen::GetDesiredInputConfig() const
{
    DEMO_LOG_CALL();
    FUIInputConfig Config(ECommonInputMode::Menu,EMouseCaptureMode::NoCapture,EMouseLockMode::DoNotLock,false); // 与PC菜单门禁一致，首次路由不得抵消移动锁。
    Config.bIgnoreMoveInput=true; Config.bIgnoreLookInput=true;
    return Config;
}
bool UDemoMenuScreen::NativeOnHandleBackAction()
{
    DEMO_LOG_CALL(); if(auto* PC=Controller()) if(auto* Manager=PC->FindComponentByClass<UDemoUIManager>()) Manager->Back(); return true;
}
FReply UDemoMenuScreen::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    DEMO_LOG_CALL(); if(Event.GetKey()==EKeys::Tab||Event.GetKey()==EKeys::Escape) { if(!Event.IsRepeat()) NativeOnHandleBackAction(); return FReply::Handled(); }
    return Super::NativeOnPreviewKeyDown(Geometry,Event);
}
UWidget* UDemoMenuScreen::NativeGetDesiredFocusTarget() const
{
    DEMO_LOG_TICK(); if(auto* Previous=GetRestoredFocusTarget()) return Previous;
    if(UsesR02Presentation()) for(const FName Action:ActionOrder) if(auto* Button=FindAction(Action)) if(Button->GetIsEnabled()) return Button;
    for(const auto& Pair:ActionButtons) if(Pair.Value&&Pair.Value->GetIsEnabled()) return Pair.Value;
    return const_cast<UDemoMenuScreen*>(this);
}
UWidget* UDemoMenuScreen::GetRestoredFocusTarget() const
{ DEMO_LOG_TICK(); if(auto* Button=FindAction(FocusAction)) if(Button->GetIsEnabled()) return Button; return nullptr; }
bool UDemoMenuScreen::CaptureExtraFocusBeforeRebuild() { DEMO_LOG_TICK(); return false; }
void UDemoMenuScreen::NativeTick(const FGeometry& Geometry,float DeltaTime) { DEMO_LOG_TICK(); Super::NativeTick(Geometry,DeltaTime); if(IsActivated()) { UpdatePageTransition(); AnimateScreen(DeltaTime); } }
void UDemoMenuScreen::UpdatePageTransition()
{
    DEMO_LOG_TICK(); if(!PageBody) return;
    const float Alpha=PageTransitionStart<0.||DemoUI::ReducedMotion()?1.f:FMath::Clamp(static_cast<float>((FPlatformTime::Seconds()-PageTransitionStart)/.16),0.f,1.f); // 无Timer/延迟回调，关页无需等待动画。
    const float Eased=1.f-FMath::Pow(1.f-Alpha,3.f); // 微小5px位移＋90%至100%正文透明度，保留输入/焦点/控件结构。
    PageBody->SetRenderTranslation(FVector2D(0,UsesR02Presentation()?0.f:5.f*(1.f-Eased))); PageBody->SetRenderOpacity(.90f+.10f*Eased);
    if(Alpha>=1.f) PageTransitionStart=-1.;
}
void UDemoMenuScreen::AnimateScreen(float DeltaTime) { DEMO_LOG_TICK(); }
FString UDemoMenuScreen::RefreshKey() const { DEMO_LOG_TICK(); return TEXT("base"); }
void UDemoMenuScreen::BuildBody() { DEMO_LOG_CALL(); }
void UDemoMenuScreen::NativeDestruct() { DEMO_LOG_CALL(); ActionButtons.Reset(); ActionOrder.Reset(); FocusAction=NAME_None; LastRefreshKey.Empty(); PageTransitionStart=-1.; Super::NativeDestruct(); }

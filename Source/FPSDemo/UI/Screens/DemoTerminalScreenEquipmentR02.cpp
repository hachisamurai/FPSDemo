// 职责：S1 R02武器/弹药正式页的无框目录、真产品图、基础参数与本人报价。使用：Terminal仅在原Weapon/Ammo Flow分支调用。
// 依赖：S0持久化Theme、CommonUI原动作、只读Owner快照/CDO与原异步纹理缓存；无背景图、经济/装配/存档写入或新网络字段。
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/R02/DemoR02EditorialButton.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoCloudSync.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/NativeWidgetHost.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h" // 实际视口/DPI只用于本次排版，不改变设备设置或窗口模式。
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Debug/DemoLog.h"

namespace
{
    /** 本页纯装饰线/比例条；Host拥有Slate生命周期，Paint只使用传入几何/颜色，不保存UObject或业务引用。 */
    class SDemoR02EquipmentRule final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SDemoR02EquipmentRule) : _Color(FLinearColor::White), _FillColor(FLinearColor::White), _Vertical(false), _FillFraction(0.f) {}
            SLATE_ARGUMENT(FLinearColor,Color) // 只读线色，含主题透明度，不代表交易/焦点状态。
            SLATE_ARGUMENT(FLinearColor,FillColor) // 前景占比线色，仅FillFraction>0时绘制。
            SLATE_ARGUMENT(bool,Vertical) // true时画竖向1px分栏线。
            SLATE_ARGUMENT(float,FillFraction) // >0时作为C04发丝比例条绘制前景骨白线段。
        SLATE_END_ARGS()

        /** Slate创建时复制线色；Arguments只在同步Construct期间借用，无异步回调。 */
        void Construct(const FArguments& Arguments)
        {
            UE_LOG(LogFPSDemo,Log,TEXT("[CALL] SDemoR02EquipmentRule::Construct"));
            RuleColor=Arguments._Color;
            FillColor=Arguments._FillColor;
            bVertical=Arguments._Vertical;
            FillFraction=FMath::Clamp(Arguments._FillFraction,0.f,1.f);
            SetVisibility(EVisibility::HitTestInvisible);
        }

        /** LayoutScaleMultiplier为Slate预排版输入；预留两局部单位容纳720p的1物理像素，不将其当实际绘制厚度。 */
        virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
        {
            UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] SDemoR02EquipmentRule::ComputeDesiredSize"));
            return bVertical?FVector2D(2.f,1.f):FVector2D(1.f,2.f);
        }

        /** Slate常规绘制入口：按Geometry累计缩放与绝对起点对齐线，Elements/LayerId仅借本次Paint，不创建资源。 */
        virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle& Style,bool ParentEnabled) const override
        {
            UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] SDemoR02EquipmentRule::OnPaint"));
            const float EffectiveScale=FMath::Max(.01f,Geometry.GetAccumulatedLayoutTransform().GetScale()); // 当前树无旋转/RenderScale；包含外壳ScaleBox与DPI，入场仅平移。
            const float Thickness=DemoUIR02::PhysicalHairlineThickness(EffectiveScale);
            if(bVertical)
            {
                const FVector2D AbsoluteStart=Geometry.LocalToAbsolute(FVector2D::ZeroVector);
                const FVector2D AbsoluteEnd=Geometry.LocalToAbsolute(FVector2D(0,Geometry.GetLocalSize().Y));
                const FVector2D LocalStart=Geometry.AbsoluteToLocal(FVector2D(FMath::RoundToFloat(AbsoluteStart.X),FMath::RoundToFloat(AbsoluteStart.Y)));
                const float LocalHeight=FMath::Max(0.f,(FMath::RoundToFloat(AbsoluteEnd.Y)-FMath::RoundToFloat(AbsoluteStart.Y))/EffectiveScale);
                FSlateDrawElement::MakeBox(Elements,LayerId,Geometry.ToPaintGeometry(FVector2D(Thickness,LocalHeight),FSlateLayoutTransform(LocalStart)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,RuleColor*Style.GetColorAndOpacityTint());
                return LayerId;
            }
            const FVector2D AbsoluteStart=Geometry.LocalToAbsolute(FVector2D::ZeroVector); // 包含当前入场平移，避免只按DPI计算导致半像素起点。
            const FVector2D AbsoluteEnd=Geometry.LocalToAbsolute(FVector2D(Geometry.GetLocalSize().X,0)); // 实际可用行宽，不猜测1600设计宽。
            const FVector2D LocalStart=Geometry.AbsoluteToLocal(FVector2D(FMath::RoundToFloat(AbsoluteStart.X),FMath::RoundToFloat(AbsoluteStart.Y))); // 绝对像素起点反解到本地。
            const float LocalWidth=FMath::Max(0.f,(FMath::RoundToFloat(AbsoluteEnd.X)-FMath::RoundToFloat(AbsoluteStart.X))/EffectiveScale); // 两端均对齐，防止模糊尾端。
            FSlateDrawElement::MakeBox(Elements,LayerId,Geometry.ToPaintGeometry(FVector2D(LocalWidth,Thickness),FSlateLayoutTransform(LocalStart)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,RuleColor*Style.GetColorAndOpacityTint());
            if(FillFraction>KINDA_SMALL_NUMBER)
                FSlateDrawElement::MakeBox(Elements,LayerId+1,Geometry.ToPaintGeometry(FVector2D(LocalWidth*FillFraction,Thickness*1.5f),FSlateLayoutTransform(LocalStart)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,FillColor*Style.GetColorAndOpacityTint());
            return LayerId+1;
        }
    private:
        FLinearColor RuleColor=FLinearColor::White; // 本Slate节点拥有的显示值，释放节点即清理，不复制/持久化。
        FLinearColor FillColor=FLinearColor::White; // 前景骨白占比色。
        bool bVertical=false; // 是否竖向分栏线。
        float FillFraction=0.f; // 占比[0,1]。
    };
}

bool UDemoTerminalScreen::UsesR02Presentation() const
{
    DEMO_LOG_TICK();
    const UDemoMenuFlowComponent* Menu=Flow(); // 只借用当前本机Flow；2026-11-20起金币娱乐/遗物、冒险强化与奖励带也统一R02（独立玩法页DemoCasinoScreen不受影响）。
    return Menu!=nullptr;
}

UTextBlock* UDemoTerminalScreen::AddR02EquipmentLabel(UPanelWidget* Parent,const FString& Text,int32 Size,bool Serif,bool Muted,bool Latin)
{
    DEMO_LOG_CALL();
    const UDemoUITheme* Theme=R02Theme(); // 基类强持有的S0主题，本助手不加载路径/修改资产。
    if(!Theme) Theme=GetDefault<UDemoUITheme>(); // 缺资产沿原字体可读回退；原生主题默认对象不持World，不把回退冒充衬线加载通过。
    UTextBlock* Label=AddText(Parent,Text,Size,Muted?Theme->Muted:Theme->Ink); // 关键规则/拒绝/保存事实默认Ink，Muted仅辅助编号/单位。
    Label->SetFont(Serif?Theme->GetSerifFont(Size,TEXT("Regular"),!Latin):Theme->GetLabelFont(Size,TEXT("Regular"),Latin));
    if(auto* Vertical=Cast<UVerticalBoxSlot>(Label->Slot)) Vertical->SetPadding(FMargin(0,3));
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(Label->Slot)) Horizontal->SetPadding(FMargin(0,3));
    return Label;
}

void UDemoTerminalScreen::AddR02EquipmentRule(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    UNativeWidgetHost* Rule=WidgetTree->ConstructWidget<UNativeWidgetHost>(); // 当前WidgetTree拥有Host，Host持有纯Slate线，不接收输入。
    Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
    const UDemoUITheme* Theme=R02Theme(); // 与文字相同的只读主题，缺资产时只用原生Token回退，不发生第二次加载。
    if(!Theme) Theme=GetDefault<UDemoUITheme>();
    Rule->SetContent(SNew(SDemoR02EquipmentRule).Color(Theme->Ink.CopyWithNewOpacity(Theme->HairlineOpacity)));
    if(auto* Vertical=Cast<UVerticalBoxSlot>(Parent->AddChild(Rule))) Vertical->SetPadding(FMargin(0,5));
}

void UDemoTerminalScreen::AddR02EquipmentVerticalRule(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    UNativeWidgetHost* Rule=WidgetTree->ConstructWidget<UNativeWidgetHost>();
    Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
    const UDemoUITheme* Theme=R02Theme()?R02Theme():GetDefault<UDemoUITheme>();
    Rule->SetContent(SNew(SDemoR02EquipmentRule).Color(Theme->Ink.CopyWithNewOpacity(Theme->HairlineOpacity)).Vertical(true));
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(Parent->AddChild(Rule)))
    {
        Horizontal->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        Horizontal->SetVerticalAlignment(VAlign_Fill);
        Horizontal->SetPadding(FMargin(20,0));
    }
}

void UDemoTerminalScreen::AddR02EquipmentMeter(UPanelWidget* Parent,const FString& Title,const FString& Value,float Fraction)
{
    DEMO_LOG_CALL();
    const UDemoUITheme* Theme=R02Theme()?R02Theme():GetDefault<UDemoUITheme>();
    UHorizontalBox* Row=AddRow(Parent);
    UTextBlock* Caption=AddR02EquipmentLabel(Row,Title,17,false,true);
    UTextBlock* Number=AddR02EquipmentLabel(Row,Value,24,true,false,true);
    Caption->SetAutoWrapText(false); Number->SetAutoWrapText(false); Number->SetJustification(ETextJustify::Right);
    UNativeWidgetHost* Meter=WidgetTree->ConstructWidget<UNativeWidgetHost>();
    Meter->SetVisibility(ESlateVisibility::HitTestInvisible);
    Meter->SetContent(SNew(SDemoR02EquipmentRule).Color(Theme->Ink.CopyWithNewOpacity(Theme->HairlineOpacity)).FillColor(Theme->Ink).FillFraction(Fraction));
    if(auto* Vertical=Cast<UVerticalBoxSlot>(Parent->AddChild(Meter))) Vertical->SetPadding(FMargin(0,2,0,14));
}

UVerticalBox* UDemoTerminalScreen::BuildR02EquipmentColumns(UVerticalBox*& OutCatalog)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Content=AddRow(PageBody); // 左窄目录、竖分栏线、中展台共用同一水平容器。
    USizeBox* CatalogBounds=WidgetTree->ConstructWidget<USizeBox>(); // C04左栏收窄至196px，突出中间大图舞台。
    CatalogBounds->SetWidthOverride(196.f);
    UHorizontalBoxSlot* CatalogSlot=Content->AddChildToHorizontalBox(CatalogBounds);
    CatalogSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); CatalogSlot->SetPadding(FMargin(0));
    OutCatalog=WidgetTree->ConstructWidget<UVerticalBox>(); CatalogBounds->SetContent(OutCatalog);
    AddR02EquipmentVerticalRule(Content);
    UVerticalBox* Hero=AddColumn(Content); // 中间展台占据最大面积。
    if(auto* HeroSlot=Cast<UHorizontalBoxSlot>(Hero->Slot)) { HeroSlot->SetPadding(FMargin(8,0)); HeroSlot->SetVerticalAlignment(VAlign_Center); }
    return Hero;
}

void UDemoTerminalScreen::AddR02EquipmentStat(UPanelWidget* Parent,const FString& Title,const FString& Value)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Row=AddRow(Parent); // 同行单位/真实数值；不用条形图推断武器优劣。
    UTextBlock* Caption=AddR02EquipmentLabel(Row,Title,16,false,true);
    UTextBlock* Number=AddR02EquipmentLabel(Row,Value,30,true,false,true); // 只读静态读数，衬线比例数字不会改变任何报价精度。
    Caption->SetAutoWrapText(false); Number->SetAutoWrapText(false); Number->SetJustification(ETextJustify::Right);
    AddR02EquipmentRule(Parent);
}

void UDemoTerminalScreen::AddR02EquipmentComparison(UPanelWidget* Parent,const ADemoWeaponBase* Selected,const ADemoWeaponBase* Baseline,bool EmptyPrimary)
{
    DEMO_LOG_CALL();
    if(!Selected||!Baseline)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("R02_B04 comparison awaiting real catalog CDO"));
        AddR02EquipmentLabel(Parent,TEXT("等待武器基础配置，暂不显示对比。")); return;
    }
    const FString BaselineName=Baseline->Config.DisplayName.ToString(); // 由本人快照指定的主枪CDO；空主槽明确借固定手枪，不读取手中增益实例。
    AddR02EquipmentLabel(Parent,EmptyPrimary?FString::Printf(TEXT("未装主武器 · 基准为固定手枪「%s」"),*BaselineName):FString::Printf(TEXT("对比当前主武器「%s」"),*BaselineName),18);
    const TCHAR* Headings[]={TEXT("基础参数"),TEXT("基准"),TEXT("当前预览"),TEXT("差值")}; // 同一CDO口径，差值不是性能评级。
    UHorizontalBox* Header=AddRow(Parent); // 四等宽列，数值始终对齐同一位置。
    for(int32 Column=0;Column<4;++Column)
    {
        UTextBlock* Cell=AddR02EquipmentLabel(Header,Headings[Column],16,false,true); // 小表头只辅助实际读数。
        Cell->SetAutoWrapText(false);
        if(Column>0) Cell->SetJustification(ETextJustify::Right);
    }
    AddR02EquipmentRule(Parent);
    const TCHAR* Labels[]={TEXT("伤害 / 弹丸"),TEXT("射速 / RPM"),TEXT("弹匣 / 发"),TEXT("装填 / 秒")}; // 伤害按单弹丸；完整每发弹丸数在表后明示。
    const float Reference[]={Baseline->Config.BaseDamage,Baseline->Config.RoundsPerMinute,static_cast<float>(Baseline->Config.MagazineCapacity),Baseline->Config.ReloadSeconds}; // 基准原始四字段。
    const float Preview[]={Selected->Config.BaseDamage,Selected->Config.RoundsPerMinute,static_cast<float>(Selected->Config.MagazineCapacity),Selected->Config.ReloadSeconds}; // 当前预览原始四字段，不包括GAS或实例。
    for(int32 Index=0;Index<4;++Index)
    {
        const float Difference=Preview[Index]-Reference[Index]; // 同名字段相减，保留正负，不把装填更长误标成更优。
        const bool Changed=!FMath::IsNearlyZero(Difference,.01f); // 与旧页相同显示精度，只过滤浮点噪声。
        const FString ReferenceValue=Index==3?FString::Printf(TEXT("%.1f"),Reference[Index]):FString::Printf(TEXT("%.0f"),Reference[Index]); // 秒一位，其余与原参数面板一致。
        const FString PreviewValue=Index==3?FString::Printf(TEXT("%.1f"),Preview[Index]):FString::Printf(TEXT("%.0f"),Preview[Index]); // 不以字体改变值/精度。
        const FString Delta=Changed?(Index==3?FString::Printf(TEXT("%+.1f"),Difference):FString::Printf(TEXT("%+.0f"),Difference)):FString(TEXT("—")); // 未变化明确短横，正负差值由文本表达。
        const FString Values[]={Labels[Index],ReferenceValue,PreviewValue,Delta}; // 当前同步构建行的四个短值。
        UHorizontalBox* Row=AddRow(Parent);
        for(int32 Column=0;Column<4;++Column)
        {
            UTextBlock* Cell=AddR02EquipmentLabel(Row,Values[Column],18,false,false,Column>0); // 真实参数读数使用高对比Sans，720p不依赖细衬线读取小数字。
            Cell->SetAutoWrapText(false); Cell->SetWrapTextAt(0.f);
            if(Column>0) Cell->SetJustification(ETextJustify::Right);
            if(auto* CellSlot=Cast<UHorizontalBoxSlot>(Cell->Slot)) CellSlot->SetPadding(FMargin(0,1));
        }
    }
    AddR02EquipmentLabel(Parent,FString::Printf(TEXT("不含个人增益 · 弹丸数：基准 %d / 预览 %d"),Baseline->GetPelletCount(),Selected->GetPelletCount()),16,false,true);
}

void UDemoTerminalScreen::BuildR02Weapons(const ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    int32 ViewportWidth=0,ViewportHeight=0; Controller()->GetViewportSize(ViewportWidth,ViewportHeight);
    const float ViewportScale=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(this));
    const float AvailableWidth=static_cast<float>(ViewportWidth)/ViewportScale-48.f;
    const float LayoutWidth=FMath::Clamp(AvailableWidth,1000.f,1600.f);
    const bool Compact=AvailableWidth<1400.f;
    ConfigureLayout(LayoutWidth,960.f);
    UE_LOG(LogFPSDemo,Log,TEXT("R02_B04 C04 weapon layout viewport=%dx%d dpi=%.3f available=%.1f width=%.1f compact=%d"),ViewportWidth,ViewportHeight,ViewportScale,AvailableWidth,LayoutWidth,Compact);
    const UDemoUITheme* Theme=R02Theme()?R02Theme():GetDefault<UDemoUITheme>();
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot();
    const UDemoWeaponComponent* Equipment=Player?Player->GetWeaponComponent():nullptr;
    PrepareWeaponIcons(Equipment);
    SetHeading(TEXT("武器库"),TEXT(""));
    if(PageSubtitle) PageSubtitle->SetVisibility(ESlateVisibility::Collapsed); // C04极简杂志头，只留导航与右上钱包。
    if(SelectedWeaponIndex<0||SelectedWeaponIndex>3)
    {
        const int32 EquippedIndex=DemoWeaponCatalog::IndexOf(View.PrimaryWeaponId);
        SelectedWeaponIndex=EquippedIndex>=1&&EquippedIndex<=3?EquippedIndex:0;
    }
    UVerticalBox* Catalog=nullptr;
    UVerticalBox* Hero=BuildR02EquipmentColumns(Catalog);
    // 左窄栏：C04 极简四枪目录（选中枪带余烬红底线）
    AddR02EquipmentLabel(Catalog,TEXT("COLLECTION"),14,false,true,true);
    AddR02EquipmentRule(Catalog);
    for(int32 Index=0;Index<4;++Index)
    {
        const ADemoWeaponBase* Definition=Equipment?Equipment->GetCatalogWeapon(Index):nullptr;
        const FName Id=DemoWeaponCatalog::IdAt(Index);
        const bool Unlocked=View.Progress.UnlockedWeaponIds.Contains(Id);
        const bool Equipped=Index==0||View.PrimaryWeaponId==Id;
        const FString Name=Definition?Definition->Config.DisplayName.ToString():TEXT("资源未就绪");
        UDemoMenuButton* Entry=AddAction(Catalog,FName(*FString::Printf(TEXT("Weapon%d"),Index)),FString::Printf(TEXT("%02d  %s"),Index+1,*Name),true,Index==SelectedWeaponIndex);
        if(auto* Editorial=Cast<UDemoR02EditorialButton>(Entry)) Editorial->SetR02Appearance(const_cast<UDemoUITheme*>(Theme),Index==SelectedWeaponIndex,22,true);
        Entry->SetMinDimensions(170.f,42.f); Entry->UseSingleLineLabel();
        if(auto* EntrySlot=Cast<UVerticalBoxSlot>(Entry->Slot)) EntrySlot->SetPadding(FMargin(0,6,0,2));
        UTextBlock* Status=AddR02EquipmentLabel(Catalog,!Unlocked?TEXT("未购买 · 枪墙获取"):Equipped?(Index==0?TEXT("固定副武器"):TEXT("当前已装配")):TEXT("已购买 · 可装配"),14,false,true);
        Status->SetAutoWrapText(false);
        AddR02EquipmentRule(Catalog);
    }
    AddGap(Catalog,12.f);
    AddR02EquipmentLabel(Catalog,TEXT("主武器仅在安全区枪墙购买"),14,false,true);

    // 中主区：C04 超大武器展台 + 下方4个极简方形弹药色块（点击直接跳转弹药页对应项）
    const ADemoWeaponBase* Selected=Equipment?Equipment->GetCatalogWeapon(SelectedWeaponIndex):nullptr;
    const bool Owned=View.Progress.UnlockedWeaponIds.Contains(DemoWeaponCatalog::IdAt(SelectedWeaponIndex));
    if(Selected)
    {
        UTexture2D* Product=WeaponIcons.IsValidIndex(SelectedWeaponIndex+4)?WeaponIcons[SelectedWeaponIndex+4].Get():nullptr;
        UTexture2D* Icon=WeaponIcons.IsValidIndex(SelectedWeaponIndex)?WeaponIcons[SelectedWeaponIndex].Get():nullptr;
        AddEquipmentArtwork(Hero,Product?Product:Icon,SelectedWeaponIndex,Product!=nullptr,!Owned);
    }
    else AddR02EquipmentLabel(Hero,TEXT("等待武器定义"),24,true);

    const UDemoAmmoComponent* AmmoComp=Player?Player->FindComponentByClass<UDemoAmmoComponent>():nullptr;
    const UDemoAmmoCatalog* AmmoData=AmmoComp?AmmoComp->GetCatalog():nullptr;
    if(AmmoData&&AmmoData->Entries.Num()==4&&View.AmmoOffers.Num()==4)
    {
        AddGap(Hero,26.f);
        UHorizontalBox* AmmoRow=AddRow(Hero);
        if(auto* RowSlot=Cast<UVerticalBoxSlot>(AmmoRow->Slot)) RowSlot->SetHorizontalAlignment(HAlign_Center);
        for(int32 AmmoIdx=0;AmmoIdx<4;++AmmoIdx)
        {
            const FDemoAmmoEntry& AmmoEntry=AmmoData->Entries[AmmoIdx];
            const FDemoShopOfferSnapshot& AmmoOffer=View.AmmoOffers[AmmoIdx];
            UVerticalBox* SwatchCol=WidgetTree->ConstructWidget<UVerticalBox>();
            if(auto* ColSlot=AmmoRow->AddChildToHorizontalBox(SwatchCol)) { ColSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); ColSlot->SetPadding(FMargin(14,0)); }
            UOverlay* BoxOverlay=WidgetTree->ConstructWidget<UOverlay>();
            USizeBox* SquareBounds=WidgetTree->ConstructWidget<USizeBox>(); SquareBounds->SetWidthOverride(78.f); SquareBounds->SetHeightOverride(78.f);
            SquareBounds->SetContent(BoxOverlay); SwatchCol->AddChildToVerticalBox(SquareBounds)->SetHorizontalAlignment(HAlign_Center);
            UDemoMenuButton* JumpBtn=AddAction(BoxOverlay,FName(*FString::Printf(TEXT("UI.JumpAmmo%d"),AmmoIdx)),TEXT(""),true,AmmoOffer.bSelected);
            if(auto* HitSlot=Cast<UOverlaySlot>(JumpBtn->Slot)) { HitSlot->SetHorizontalAlignment(HAlign_Fill); HitSlot->SetVerticalAlignment(VAlign_Fill); }
            if(auto* Editorial=Cast<UDemoR02EditorialButton>(JumpBtn)) { Editorial->SetR02Appearance(const_cast<UDemoUITheme*>(Theme),AmmoOffer.bSelected,16,true); Editorial->SetR02CardMode(true); }
            JumpBtn->SetMinDimensions(78.f,78.f);
            // 嵌套1px空心线框（外框Hairline，内填Base暖炭黑）还原C04方形弹药色块
            UBorder* OuterEdge=WidgetTree->ConstructWidget<UBorder>();
            OuterEdge->SetVisibility(ESlateVisibility::HitTestInvisible);
            OuterEdge->SetPadding(FMargin(1.f));
            OuterEdge->SetBrushColor(Theme->Ink.CopyWithNewOpacity(AmmoOffer.bSelected?.55f:AmmoOffer.bUnlocked?.28f:.15f));
            if(auto* EdgeSlot=BoxOverlay->AddChildToOverlay(OuterEdge)) { EdgeSlot->SetHorizontalAlignment(HAlign_Fill); EdgeSlot->SetVerticalAlignment(VAlign_Fill); }
            UBorder* InnerFill=WidgetTree->ConstructWidget<UBorder>();
            InnerFill->SetVisibility(ESlateVisibility::HitTestInvisible);
            InnerFill->SetBrushColor(AmmoOffer.bUnlocked?Theme->Base.CopyWithNewOpacity(.88f):Theme->Ink.CopyWithNewOpacity(.08f));
            InnerFill->SetPadding(FMargin(10.f)); InnerFill->SetHorizontalAlignment(HAlign_Center); InnerFill->SetVerticalAlignment(VAlign_Center);
            OuterEdge->SetContent(InnerFill);
            if(AmmoEntry.Icon)
            {
                UImage* AmmoIcon=AddIcon(InnerFill,AmmoEntry.Icon,42.f);
                AmmoIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
                if(!AmmoOffer.bUnlocked) AmmoIcon->SetColorAndOpacity(FLinearColor(.55f,.55f,.55f,.55f));
            }
            if(AmmoOffer.bSelected) // C04 已装配弹药左上角小余烬红方点
            {
                USizeBox* DotSize=WidgetTree->ConstructWidget<USizeBox>(); DotSize->SetWidthOverride(6.f); DotSize->SetHeightOverride(6.f);
                UBorder* Dot=WidgetTree->ConstructWidget<UBorder>(); Dot->SetBrushColor(Theme->Ember); DotSize->SetContent(Dot);
                DotSize->SetVisibility(ESlateVisibility::HitTestInvisible);
                if(auto* DotSlot=BoxOverlay->AddChildToOverlay(DotSize)) { DotSlot->SetHorizontalAlignment(HAlign_Left); DotSlot->SetVerticalAlignment(VAlign_Top); DotSlot->SetPadding(FMargin(6,6,0,0)); }
            }
            UTextBlock* AmmoName=AddR02EquipmentLabel(SwatchCol,AmmoEntry.Name.ToString(),16,true,!AmmoOffer.bUnlocked);
            AmmoName->SetJustification(ETextJustify::Center);
            if(!AmmoOffer.bUnlocked)
            {
                UTextBlock* Price=AddR02EquipmentLabel(SwatchCol,FString::Printf(TEXT("%d 金"),AmmoOffer.Cost),13,false,true);
                Price->SetJustification(ETextJustify::Center);
            }
        }
    }
    if(!View.TerminalBlockReason.IsEmpty()) AddR02EquipmentLabel(Hero,View.TerminalBlockReason,16);

    // 右信息栏：02 / 04 索引 + 衬线武器名 + 4 条真实占比发丝条 + 下划线“装配”主操作
    UVerticalBox* Details=nullptr;
    if(Compact) { AddGap(PageBody,20.f); AddR02EquipmentRule(PageBody); Details=AddColumn(PageBody); }
    else Details=ShowAside();

    AddR02EquipmentLabel(Details,FString::Printf(TEXT("%02d / 04"),SelectedWeaponIndex+1),16,true,true,true);
    if(Selected)
    {
        UTextBlock* WeaponTitle=AddR02EquipmentLabel(Details,Selected->Config.DisplayName.ToString(),36,true);
        WeaponTitle->SetAutoWrapText(false); WeaponTitle->SetWrapTextAt(320.f);
        AddGap(Details,14.f);
        float MaxDmg=1.f,MaxRpm=1.f,MaxMag=1.f,MinReload=Selected->Config.ReloadSeconds; // 从真实4枪CDO求比例标尺，不写死数值。
        for(int32 i=0;i<4;++i) if(const ADemoWeaponBase* W=Equipment?Equipment->GetCatalogWeapon(i):nullptr)
        {
            MaxDmg=FMath::Max(MaxDmg,W->Config.BaseDamage*float(FMath::Max(1,W->GetPelletCount())));
            MaxRpm=FMath::Max(MaxRpm,W->Config.RoundsPerMinute);
            MaxMag=FMath::Max(MaxMag,float(W->Config.MagazineCapacity));
            MinReload=FMath::Min(MinReload,FMath::Max(.1f,W->Config.ReloadSeconds));
        }
        const float CurDmg=Selected->Config.BaseDamage*float(FMath::Max(1,Selected->GetPelletCount()));
        AddR02EquipmentMeter(Details,TEXT("伤害"),Selected->GetPelletCount()>1?FString::Printf(TEXT("%.0f × %d"),Selected->Config.BaseDamage,Selected->GetPelletCount()):FString::Printf(TEXT("%.0f"),Selected->Config.BaseDamage),CurDmg/MaxDmg);
        AddR02EquipmentMeter(Details,TEXT("射速"),FString::Printf(TEXT("%.0f"),Selected->Config.RoundsPerMinute),Selected->Config.RoundsPerMinute/MaxRpm);
        AddR02EquipmentMeter(Details,TEXT("弹容"),FString::FromInt(Selected->Config.MagazineCapacity),float(Selected->Config.MagazineCapacity)/MaxMag);
        AddR02EquipmentMeter(Details,TEXT("装填"),FString::Printf(TEXT("%.1fs"),Selected->Config.ReloadSeconds),FMath::Clamp(MinReload/FMath::Max(.1f,Selected->Config.ReloadSeconds),.15f,1.f));
    }
    AddGap(Details,8.f);
    AddR02EquipmentLabel(Details,Owned?(SelectedWeaponIndex==0?TEXT("固定副武器 [2]"):View.PrimaryWeaponId==DemoWeaponCatalog::IdAt(SelectedWeaponIndex)?TEXT("当前已装配为主武器 [1]"):TEXT("可装配为主武器 [1]")):TEXT("未购买 · 前往安全区枪墙购买"),16,false,!Owned); // 有效装配许可可来自Editor临时权限，不冒称全部已永久购买。

    const bool bCanEquip=Selected&&Owned&&SelectedWeaponIndex>0&&View.TerminalBlockReason.IsEmpty(); // 本人报价与当前终端限制只作启用投影；服务器仍重核权限/距离/代次/临时装备。
    const FString EquipCaption=SelectedWeaponIndex==0?TEXT("固定副武器"):!Owned?TEXT("尚未购买"):View.PrimaryWeaponId==DemoWeaponCatalog::IdAt(SelectedWeaponIndex)?TEXT("已装配"):TEXT("装配");
    UDemoMenuButton* Equip=AddAction(Details,TEXT("UI.WeaponEquip"),EquipCaption,bCanEquip,Owned&&SelectedWeaponIndex>0); // 明确装配直接发原请求；手枪与未拥有项只显示页内说明，不弹详情。
    if(auto* Editorial=Cast<UDemoR02EditorialButton>(Equip)) Editorial->SetR02Appearance(const_cast<UDemoUITheme*>(Theme),Owned&&SelectedWeaponIndex>0,28,true);
    Equip->SetMinDimensions(160.f,48.f); Equip->UseSingleLineLabel();
    if(auto* EquipLayout=Cast<UVerticalBoxSlot>(Equip->Slot)) { EquipLayout->SetHorizontalAlignment(HAlign_Center); EquipLayout->SetPadding(FMargin(0,18,0,6)); }

    if(GetGameInstance())
    {
        if(const UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) AddR02EquipmentLabel(Details,Profile->GetStatus(),14,false,true);
        if(const UDemoCloudSync* Cloud=GetGameInstance()->GetSubsystem<UDemoCloudSync>()) AddR02EquipmentLabel(Details,Cloud->GetStatus(),14,false,true);
    }
    AddCompactAction(PageFooter?static_cast<UPanelWidget*>(PageFooter):PageBody,TEXT("ProfileSave"),TEXT("保存重试"));
}

void UDemoTerminalScreen::BuildR02Ammo(const ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    int32 ViewportWidth=0,ViewportHeight=0; Controller()->GetViewportSize(ViewportWidth,ViewportHeight); // 当前本机物理视口；与武器页采用同一计算口径。
    const float ViewportScale=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(this)); // 本次UMG全局DPI倍率，只影响显示尺寸。
    const float AvailableWidth=static_cast<float>(ViewportWidth)/ViewportScale-48.f; // 逻辑可用宽扣外壳总48边距。
    const float LayoutWidth=FMath::Clamp(AvailableWidth,1000.f,1600.f); // 与武器相同的支持下限/上限，不以1600整页额外缩放替代重排。
    const bool Compact=AvailableWidth<1450.f; // 窄屏正文下接完整详情，主操作固定在页脚。
    ConfigureLayout(LayoutWidth,960.f); // 与武器页同高，A02实图后补正文空间；不压缩真实弹药产品图。
    UE_LOG(LogFPSDemo,Log,TEXT("R02_B04 ammo layout viewport=%dx%d dpi=%.3f available=%.1f width=%.1f compact=%d"),ViewportWidth,ViewportHeight,ViewportScale,AvailableWidth,LayoutWidth,Compact);
    if(AvailableWidth<1000.f) UE_LOG(LogFPSDemo,Warning,TEXT("R02_B04 ammo viewport below 1000 logical minimum; additional outer fit remains unverified"));
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 四项真实Owner报价、解锁/装配及拒绝理由。
    const UDemoAmmoComponent* Ammo=Player?Player->FindComponentByClass<UDemoAmmoComponent>():nullptr; // 本次同步借用角色组件。
    const UDemoAmmoCatalog* Data=Ammo?Ammo->GetCatalog():nullptr; // 原已加载目录；本页不新增IO。
    SetHeading(TEXT("弹药配置"),TEXT("购买解锁 → 明确装配  /  主副武器通用  /  同时装配一种"));
    if(!Data||Data->Entries.Num()!=4||View.AmmoOffers.Num()!=4)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("R02_B04 awaiting ammo catalog and four Owner offers"));
        AddR02EquipmentLabel(PageBody,TEXT("等待弹药目录与服务器报价"),20); return;
    }
    UVerticalBox* Catalog=nullptr; // 当前树目录列，不跨重建缓存。
    UVerticalBox* Hero=BuildR02EquipmentColumns(Catalog); // 中央机制说明与图像不铺大卡片。
    UVerticalBox* Details=nullptr; // 当前树拥有：宽屏为独立侧栏，窄屏追加正文下方以保完整规则可滚动。
    if(Compact)
    {
        AddGap(PageBody,20.f); AddR02EquipmentRule(PageBody); Details=AddColumn(PageBody);
    }
    else Details=ShowAside();
    const int32 Selected=Flow()->GetInspectedAmmo(); // 查看项与已装配Offer分别读取，绝不本地预测装配成功。
    for(int32 Index=0;Index<4;++Index)
    {
        const FDemoAmmoEntry& Entry=Data->Entries[Index]; // 原目录本地化名称与图标。
        const FDemoShopOfferSnapshot& Offer=View.AmmoOffers[Index]; // 对应本人的服务端报价。
        UHorizontalBox* Row=AddRow(Catalog); // 与武器页同编号/发丝线节奏。
        UImage* Icon=AddIcon(Row,Entry.Icon,48.f); // 只读真实弹药图。
        if(auto* IconSlot=Cast<UHorizontalBoxSlot>(Icon->Slot)) { IconSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); IconSlot->SetPadding(FMargin(0,0,12,0)); }
        UVerticalBox* Description=AddColumn(Row); // 目录当前树文字列。
        if(auto* DescriptionSlot=Cast<UHorizontalBoxSlot>(Description->Slot)) DescriptionSlot->SetPadding(FMargin(0));
        UDemoMenuButton* EntryButton=AddAction(Description,FName(*FString::Printf(TEXT("Ammo%d"),Index)),FString::Printf(TEXT("%02d  %s"),Index+1,*Entry.Name.ToString()),true,Index==Selected); // 只查看，不解锁/不装配。
        EntryButton->SetMinDimensions(150.f,42.f); EntryButton->UseSingleLineLabel();
        if(auto* EntrySlot=Cast<UVerticalBoxSlot>(EntryButton->Slot)) EntrySlot->SetPadding(FMargin(0,0,0,2));
        UTextBlock* Status=AddR02EquipmentLabel(Description,Offer.bSelected?TEXT("当前已装配"):Offer.bUnlocked?TEXT("已解锁 · 可装配"):FString::Printf(TEXT("%d 金币解锁"),Offer.Cost),16,false,true); // 价格始终来自本项Offer，不硬编码。
        Status->SetAutoWrapText(false);
        AddR02EquipmentRule(Catalog);
    }
    AddR02EquipmentLabel(Catalog,TEXT("主副武器共用，每人一种。\n仅安全区可购买和更换。"),18);
    if(!Data->Entries.IsValidIndex(Selected))
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("R02_B04 invalid ammo preview index=%d"),Selected);
        AddR02EquipmentLabel(Hero,TEXT("请选择有效弹药类型"),20); return;
    }
    const FDemoShopOfferSnapshot& Offer=View.AmmoOffers[Selected]; // 当前预览机制和原AmmoAction使用同一报价项。
    AddR02EquipmentLabel(Hero,FString::Printf(TEXT("AMMUNITION  /  %02d — 04"),Selected+1),16,false,true,true);
    AddR02EquipmentLabel(Hero,Data->Entries[Selected].Name.ToString(),40,true);
    AddEquipmentArtwork(Hero,Data->Entries[Selected].Icon,INDEX_NONE,false);
    AddR02EquipmentRule(Hero);
    switch(Selected)
    {
        case 0:
            AddR02EquipmentLabel(Hero,TEXT("原始武器伤害，无附加状态。已默认解锁，原射速、弹匣和装填规则保持不变。"),20);
            break;
        case 1:
            AddR02EquipmentLabel(Hero,FString::Printf(TEXT("命中叠加灼烧：每层每 %.1f 秒造成 %.1f HP，持续 %.1f 秒。"),Data->BurnPeriod,Data->BurnDamagePerStack,Data->BurnDuration),20);
            AddR02EquipmentLabel(Hero,FString::Printf(TEXT("达到 %d 层触发单体爆炸，额外造成 %.1f 伤害并清空层数。"),Data->BurnThreshold,Data->ExplosionDamage),18);
            AddR02EquipmentLabel(Hero,TEXT("同次开火对同一敌人只叠一层，散弹多颗命中也不重复叠层。"),18);
            break;
        case 2:
            AddR02EquipmentLabel(Hero,FString::Printf(TEXT("每层减速 %.0f%%，上限 %.0f%%，持续 %.1f 秒。"),100*Data->SlowPerStack,100*Data->MaxSlow,Data->ChillDuration),20);
            AddR02EquipmentLabel(Hero,FString::Printf(TEXT("达到 %d 层冻结 %.1f 秒，解冻后免疫冰冻 %.1f 秒。"),Data->FreezeThreshold,Data->FreezeDuration,Data->PostThawImmunityDuration),18);
            AddR02EquipmentLabel(Hero,TEXT("冻结限制移动，不阻止攻击；免疫期间仍承受子弹直接伤害。"),18);
            break;
        case 3:
            AddR02EquipmentLabel(Hero,FString::Printf(TEXT("首个敌人承受 %.0f%% 伤害；沿原轨迹额外击中一个后方敌人，承受首目标伤害的 %.0f%%。"),100*Data->PiercingDamageMultiplier,100*Data->SecondaryDamageRatio),20);
            AddR02EquipmentLabel(Hero,TEXT("无法穿墙；散弹每颗弹丸独立检测穿透。"),18);
            break;
        default: break;
    }
    AddR02EquipmentLabel(Details,TEXT("CURRENT LOADOUT"),16,false,true,true);
    AddR02EquipmentRule(Details);
    int32 EquippedIndex=INDEX_NONE; // 没有已确认选项时明确等待，不把当前查看项当实际装配。
    for(int32 Index=0;Index<View.AmmoOffers.Num();++Index) if(View.AmmoOffers[Index].bSelected) { EquippedIndex=Index; break; } // 同步只读扫描，避免临时Lambda留用快照引用。
    AddR02EquipmentLabel(Details,Data->Entries.IsValidIndex(EquippedIndex)?Data->Entries[EquippedIndex].Name.ToString():TEXT("等待装配状态"),28,true);
    AddR02EquipmentLabel(Details,Offer.bUnlocked?TEXT("本项已解锁"):TEXT("本项尚未解锁"),20);
    AddR02EquipmentRule(Details);
    if(!Offer.bUnlocked)
    {
        AddR02EquipmentStat(Details,TEXT("解锁价格 / 金币"),FString::FromInt(Offer.Cost));
        AddR02EquipmentLabel(Details,TEXT("购买仅解锁，不替换当前弹药。"),18); // 先明示交易结果，再提供原确认入口。
    }
    else AddR02EquipmentLabel(Details,TEXT("已解锁弹药可免费装配。"),18);
    UPanelWidget* ActionRegion=PageFooter?static_cast<UPanelWidget*>(PageFooter):PageBody.Get(); // 宽/窄主动作均固定页脚；完整报价/规则继续在相应详情区域滚动。
    const FString ActionCaption=Offer.bSelected?TEXT("当前已装配"):Offer.bUnlocked?TEXT("装配此弹药"):TEXT("查看解锁确认"); // 仍按同一Owner报价和装配状态命名，不把购买改成自动装配。
    const bool ActionEnabled=Offer.BlockReason.IsEmpty()&&!Offer.bSelected; // 沿原真实报价许可，不能靠移动按钮解除服务端拒绝。
    UDemoMenuButton* Primary=AddCompactAction(ActionRegion,TEXT("AmmoAction"),ActionCaption,ActionEnabled,200.f); // 原AmmoAction仍负责冻结报价/购买确认/装配门禁；本页不扣币，主动作不随侧栏滚走。
    Primary->UseSingleLineLabel();
    if(auto* PrimarySlot=Cast<UVerticalBoxSlot>(Primary->Slot)) PrimarySlot->SetPadding(FMargin(0,12,0,4));
    if(!Offer.BlockReason.IsEmpty()) AddR02EquipmentLabel(Details,Offer.BlockReason,18); // 禁用原因用Ink完整显示，禁用字色不承担解释任务。
    AddR02EquipmentLabel(Hero,TEXT("永久解锁保留在此存档。解锁成功后，还需明确装配；购买本身不会替换当前弹药。"),18);
    if(!View.TerminalBlockReason.IsEmpty()) AddR02EquipmentLabel(Hero,View.TerminalBlockReason,18); // 保留拥有者原终端拒绝事实。
}

// 职责：S2 C02真实大厅的纯暖黑、左品牌/动作与底部横向三槽简行。使用：仅DemoLobbyScreen构建，动作继续原Flow和顶层白名单。
// 依赖：S0 Theme/EditorialButton、GI RunSaves缓存和已有云子系统；不读取磁盘、不创建存档、不伪造关数/钱包，不使用背景图。
#include "UI/Screens/DemoLobbyScreen.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/R02/DemoR02EditorialButton.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Flow/DemoMenuFlowComponent.h" // 只读既有拒绝/状态文案，不构造交易结果。
#include "Player/DemoPlayerController.h"
#include "Player/DemoCloudSync.h"
#include "Save/DemoRunSave.h"
#include "Game/DemoCombatConfig.h"
#include "Game/DemoGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h" // 本地大厅诊断查询，不依赖Unity源文件顺序。
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/NativeWidgetHost.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Debug/DemoLog.h"

namespace
{
    /** 当前Host拥有的纯装饰线；本地颜色/方向不引用World/UObject，绝不捕获指针或注册业务。 */
    class SDemoR02LobbyRule final : public SLeafWidget
    {
    public:
        SLATE_BEGIN_ARGS(SDemoR02LobbyRule) : _Color(FLinearColor::White),_Vertical(false) {}
            SLATE_ARGUMENT(FLinearColor,Color) // 本节点复制的线色，含主题装饰Alpha。
            SLATE_ARGUMENT(bool,Vertical) // true竖分栏，false横分组；不影响父布局内容或点击区域。
        SLATE_END_ARGS()
        /** Slate创建时同步复制参数，不保留FArguments或绑定异步回调。 */
        void Construct(const FArguments& Arguments)
        { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] SDemoR02LobbyRule::Construct")); RuleColor=Arguments._Color; bVertical=Arguments._Vertical; SetVisibility(EVisibility::HitTestInvisible); }
        /** LayoutScaleMultiplier仅预排版输入；留两局部单位容纳缩小后的一物理像素，不以请求厚度冒充实际厚度。 */
        virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
        { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] SDemoR02LobbyRule::ComputeDesiredSize")); return FVector2D(2.f,2.f); }
        /** 真实Paint：Geometry为当帧完整布局，Elements/Style只同步借用；起点和末端在绝对像素对齐，不生成资源。 */
        virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 LayerId,const FWidgetStyle& Style,bool ParentEnabled) const override
        {
            UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] SDemoR02LobbyRule::OnPaint"));
            const float Scale=FMath::Max(.01f,Geometry.GetAccumulatedLayoutTransform().GetScale()); // 含DPI/外壳ScaleBox；本树不施旋转或RenderScale。
            const FVector2D Start=Geometry.LocalToAbsolute(FVector2D::ZeroVector),End=Geometry.LocalToAbsolute(Geometry.GetLocalSize()); // 本次实际分配端点，不能用1600设计宽自证。
            const FVector2D LocalStart=Geometry.AbsoluteToLocal(FVector2D(FMath::RoundToFloat(Start.X),FMath::RoundToFloat(Start.Y))); // 反解物理像素起点。
            const float Thickness=DemoUIR02::PhysicalHairlineThickness(Scale); // 1物理px换成本地厚度，横/竖共用。
            const FVector2D Size=bVertical?FVector2D(Thickness,FMath::Max(0.f,(FMath::RoundToFloat(End.Y)-FMath::RoundToFloat(Start.Y))/Scale))
                :FVector2D(FMath::Max(0.f,(FMath::RoundToFloat(End.X)-FMath::RoundToFloat(Start.X))/Scale),Thickness); // 沿当前真正可用分栏绘制。
            FSlateDrawElement::MakeBox(Elements,LayerId,Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(LocalStart)),FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")),ESlateDrawEffect::None,RuleColor*Style.GetColorAndOpacityTint());
            return LayerId;
        }
    private:
        FLinearColor RuleColor=FLinearColor::White; // 本Slate节点拥有的只读显示值，随Host释放。
        bool bVertical=false; // 仅线方向，不保存用户选择/权限。
    };
}

bool UDemoLobbyScreen::UsesR02Presentation() const
{ DEMO_LOG_TICK(); return true; }

UTextBlock* UDemoLobbyScreen::AddLobbyLabel(UPanelWidget* Parent,const FString& Text,int32 Size,bool Serif,bool Muted,bool Latin,float WrapWidth)
{
    DEMO_LOG_CALL();
    const UDemoUITheme* Theme=R02Theme(); // 基类持有的共享只读主题，不在本助手创建资产或读路径。
    if(!Theme) Theme=GetDefault<UDemoUITheme>(); // 资源失败保持可读；不把本地回退记作字体加载通过。
    UTextBlock* Label=AddText(Parent,Text,Size,Muted?Theme->Muted:Theme->Ink); // 关键状态/保护理由使用Ink；Muted仅辅助日期/眉题。
    Label->SetFont(Serif?Theme->GetSerifFont(Size,TEXT("Regular"),!Latin):Theme->GetLabelFont(Size,TEXT("Regular"),Latin));
    Label->SetAutoWrapText(false); Label->SetWrapTextAt(WrapWidth>0.f?WrapWidth:0.f); // 真实列宽已知时首帧即完整分配长理由，不依赖上一帧Paint宽。
    if(auto* Vertical=Cast<UVerticalBoxSlot>(Label->Slot)) Vertical->SetPadding(FMargin(0,4));
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(Label->Slot)) Horizontal->SetPadding(FMargin(0,4));
    return Label;
}

void UDemoLobbyScreen::AddLobbyRule(UPanelWidget* Parent,bool Vertical)
{
    DEMO_LOG_CALL();
    const UDemoUITheme* Theme=R02Theme(); // 不持额外资源；Theme缺失时仅用原生Token降级。
    if(!Theme) Theme=GetDefault<UDemoUITheme>();
    UNativeWidgetHost* Rule=WidgetTree->ConstructWidget<UNativeWidgetHost>(); // 当前WidgetTree拥有纯Slate装饰，无点击/焦点。
    Rule->SetVisibility(ESlateVisibility::HitTestInvisible); Rule->SetContent(SNew(SDemoR02LobbyRule).Color(Theme->Ink.CopyWithNewOpacity(Theme->HairlineOpacity)).Vertical(Vertical));
    UPanelSlot* LayoutSlot=Parent->AddChild(Rule); // 父容器负责实际长度；竖线填充同一行高度。
    if(auto* ColumnSlot=Cast<UVerticalBoxSlot>(LayoutSlot)) ColumnSlot->SetPadding(FMargin(0,12));
    if(auto* RowSlot=Cast<UHorizontalBoxSlot>(LayoutSlot)) { RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); RowSlot->SetPadding(FMargin(12,0)); RowSlot->SetVerticalAlignment(VAlign_Fill); } // 底部三简行之间只留紧凑间距，不沿用旧大分栏的64px留白。
}

void UDemoLobbyScreen::BuildR02Lobby()
{
    DEMO_LOG_CALL();
    const UDemoUITheme* Theme=R02Theme(); // 本页只读S0主题，缺失时采用同一原生色值/已打包字体回退。
    if(!Theme) Theme=GetDefault<UDemoUITheme>();
    ADemoPlayerController* PC=Controller(); // 本次构建的实际本机拥有者，不能直接调用GameMode或磁盘。
    if(!PC||!PageBody||!GetGameInstance()) { UE_LOG(LogFPSDemo,Warning,TEXT("R02_LOBBY waiting for owner/body/GI")); return; }
    int32 ViewportWidth=0,ViewportHeight=0; PC->GetViewportSize(ViewportWidth,ViewportHeight); // 物理像素，由本机真实窗口提供。
    const float ViewportScale=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(this)); // 本次UMG逻辑缩放，仅影响排版。
    const float LayoutWidth=FMath::Clamp(static_cast<float>(ViewportWidth)/ViewportScale-48.f,1000.f,1600.f); // 两侧共48外边，窄屏不硬套1600逻辑宽。
    const float LeftWidth=FMath::Min(LayoutWidth-64.f,960.f); // BREACH真实大字宽于旧46%列，提供足够测量空间避免H被裁；动作仍左对齐、右区留白。
    ConfigureLayout(LayoutWidth,960.f);
    UE_LOG(LogFPSDemo,Log,TEXT("R02_LOBBY horizontal-record layout viewport=%dx%d dpi=%.3f width=%.1f left=%.1f"),ViewportWidth,ViewportHeight,ViewportScale,LayoutWidth,LeftWidth);
    SetHeading(TEXT("BREACH")); // 保留基类模型/过渡身份，画面品牌由左列独立排版。
    if(auto* Backdrop=Cast<UBorder>(WidgetTree->RootWidget)) { Backdrop->SetPadding(FMargin(24.f)); Backdrop->SetBrushColor(Theme->Base); } // 不透出旧HUD大厅场景/背景图。
    if(auto* Surface=Cast<UBorder>(WidgetTree->FindWidget(TEXT("PanelSurface")))) { Surface->SetBrushColor(FLinearColor::Transparent); Surface->SetPadding(FMargin(32,28)); }
    for(const FName Name:{FName(TEXT("PageHeaderBounds")),FName(TEXT("PageWallet")),FName(TEXT("PageSectionLabel"))})
        if(UWidget* Widget=WidgetTree->FindWidget(Name)) Widget->SetVisibility(ESlateVisibility::Collapsed); // 折叠旧共用页头，避免重复BREACH/钱包占用正文。
    if(PageHeading) PageHeading->SetVisibility(ESlateVisibility::Collapsed);
    if(PageSubtitle) PageSubtitle->SetVisibility(ESlateVisibility::Collapsed); // 自定义壳缺HeaderBounds时也不叠第二组品牌；基类每次刷新负责恢复通用可见性。
    AddGap(PageBody,64.f);
    UHorizontalBox* Columns=AddRow(PageBody); // 同一真实正文树只放左品牌/原动作，右侧留白，不增加额外输入层。
    USizeBox* BrandBounds=WidgetTree->ConstructWidget<USizeBox>(); BrandBounds->SetWidthOverride(LeftWidth); // 品牌列稳定，不被长时间/保护文案挤压。
    Columns->AddChildToHorizontalBox(BrandBounds)->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    UVerticalBox* Brand=WidgetTree->ConstructWidget<UVerticalBox>(); BrandBounds->SetContent(Brand);
    AddLobbyLabel(Brand,TEXT("BREACH"),LayoutWidth>=1400.f?136:112,true,false,true);
    AddLobbyLabel(Brand,TEXT("每一次突破，都有新的打法。"),26,true,false,false,LeftWidth-12.f); // 沿项目已有标语，不复制概念图中的未实现故事。
    AddGap(Brand,88.f);
    const bool bCanChooseLocalSave=PC->HasAuthority(); // 与原StartGame入口一致；客户端不能因新槽按钮误以为可替房主选择档案，服务器仍重核。
    const UDemoRunSaves* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 只借Initialize已准备的GI缓存，不调用RefreshSlots/SelectSlot/CreateSlot。
    bool bHasStoredRecord=false; // 仅区分首次全空档的开始标签；受保护文件也算已有记录，不能误称空档。
    if(Saves) for(int32 Index=0;Index<3;++Index) bHasStoredRecord|=Saves->SlotExists(Index);
    const TPair<FName,const TCHAR*> Actions[]={{TEXT("StartGame"),TEXT("继续")},{TEXT("Multiplayer"),TEXT("合作")},{TEXT("LobbySettings"),TEXT("设置")},{TEXT("QuitGame"),TEXT("退出")}}; // 原Flow白名单；继续仍打开原存档选择，合作/设置/退出保持原服务守卫。
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Actions);++Index)
    {
        const FString Caption=Index==0&&Saves&&!bHasStoredRecord?FString(TEXT("开始行动")):FString(Actions[Index].Value); // 全空新档给准确入口名称；StartGame ID及选档/创建确认均不变。
        UDemoMenuButton* Button=AddAction(Brand,Actions[Index].Key,Caption,Index!=0||bCanChooseLocalSave,false); // 当前页注册真实CommonUI意图，初始高亮由首项焦点驱动而非写死Selected。
        if(auto* Editorial=Cast<UDemoR02EditorialButton>(Button)) Editorial->SetR02Appearance(const_cast<UDemoUITheme*>(Theme),false,LayoutWidth>=1400.f?36:32,true);
        Button->SetMinDimensions(300.f,58.f); Button->UseSingleLineLabel();
        if(auto* ButtonSlot=Cast<UVerticalBoxSlot>(Button->Slot)) { ButtonSlot->SetHorizontalAlignment(HAlign_Left); ButtonSlot->SetPadding(FMargin(0,3,0,5)); }
    }
    const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 本World诊断，不以大厅布局裁定流程阶段。
    if(State&&!State->FailureReason.IsEmpty()) AddLobbyLabel(Brand,State->FailureReason,18,false,false,false,LeftWidth-24.f);
    if(!PC->GetConnectionStatus().IsEmpty()) AddLobbyLabel(Brand,PC->GetConnectionStatus(),18,false,false,false,LeftWidth-24.f);
    if(Flow()&&!Flow()->GetMenuMessage().IsEmpty()) AddLobbyLabel(Brand,Flow()->GetMenuMessage(),18,false,false,false,LeftWidth-24.f); // 原文只在有内容时显示，关键原因用Ink，不由UI推断成功/失败。
    if(!bCanChooseLocalSave) AddLobbyLabel(Brand,TEXT("由房主选择行动记录"),18,false,false,false,LeftWidth-24.f); // 明示原权限原因，不用仅变灰代替说明。
    UHorizontalBox* Records=PageFooter?PageFooter.Get():AddRow(PageBody); // 底部三个横向简行沿固定页脚显示；自定义缺页脚壳才回退正文。
    for(int32 Index=0;Index<3;++Index)
    {
        const UDemoRunSave* Data=Saves?Saves->GetSlot(Index):nullptr; // 只借用本次构建缓存对象，不保存指针到成员或回调。
        const bool Protected=Saves&&Saves->SlotExists(Index)&&!Data; // 坏档/未来内容仍占用，禁止当空槽覆盖。
        const bool Active=Data&&Saves&&Saves->GetActiveSlot()==Index; // 当前GI已选择档案的显示值，不创建新选择。
        FString Summary; // 底部只投影真实关数/难度/阶段；日期、钱包与完整档案说明仍留原Saves页。
        if(Data)
        {
            const TCHAR* Phase=Data->Phase==EDemoPhase::Hub?TEXT("安全区"):Data->Phase==EDemoPhase::Victory?TEXT("已通关"):Data->Phase==EDemoPhase::Reward?TEXT("待领奖"):TEXT("备战"); // 紧凑阶段摘要仍只翻译真实保存阶段，不据物理位置猜Hub。
            const TCHAR* Difficulty=Data->Difficulty==EDemoDifficulty::Easy?TEXT("简单"):Data->Difficulty==EDemoDifficulty::Hard?TEXT("困难"):Data->Difficulty==EDemoDifficulty::Hell?TEXT("地狱"):TEXT("普通"); // 枚举中文映射，与原选档口径一致。
            Summary=Data->bEndless?FString::Printf(TEXT("%02d 无尽 %d · %s · %s"),Index+1,Data->CompletedLevel,Difficulty,Phase)
                :FString::Printf(TEXT("%02d %s/%s · %s · %s"),Index+1,*DemoUIR02::ToRoman(Data->CompletedLevel),*DemoUIR02::ToRoman(DemoCombatConfig::LevelCount),Difficulty,Phase); // 0或超十关按真实数返回阿拉伯，不把示例IV/10固定给玩家。
        }
        else Summary=FString::Printf(TEXT("%02d %s"),Index+1,!Saves?TEXT("等待存档服务"):Protected?TEXT("受保护"):TEXT("空记录 · 新建"));
        if(Index>0) AddLobbyRule(Records,true); // 三个简行之间仅短竖发丝线，不恢复右侧大分栏或卡底。
        UDemoMenuButton* Select=AddCompactAction(Records,FName(*FString::Printf(TEXT("UI.LobbySlot%d"),Index)),Summary,Saves&&!Protected&&bCanChooseLocalSave,220.f); // 原槽意图及根桥接保持，客户端/受保护项禁用。
        if(auto* Editorial=Cast<UDemoR02EditorialButton>(Select)) Editorial->SetR02Appearance(const_cast<UDemoUITheme*>(Theme),Active,LayoutWidth>=1400.f?20:18,true); // 窄屏仍保一行真摘要，不恢复日期/钱包形成大卡。
        Select->UseSingleLineLabel();
        if(auto* SelectSlot=Cast<UHorizontalBoxSlot>(Select->Slot)) SelectSlot->SetPadding(FMargin(0,4));
        if(Protected) AddLobbyLabel(Brand,FString::Printf(TEXT("记录 %02d 无法安全读取，原文件已保留。"),Index+1),18,false,false,false,LeftWidth-24.f); // 短保护原因仍可见，长恢复信息在原Saves页，不把保护文件当空槽。
    }
    if(const auto* Cloud=GetGameInstance()->GetSubsystem<UDemoCloudSync>()) // Editor合法缺失；不会创建联网服务。
    {
        AddLobbyLabel(Brand,Cloud->GetStatus(),18,false,false,false,LeftWidth-24.f);
        UHorizontalBox* CloudActions=AddRow(Brand); // 必要云决策仍留左菜单下，右侧保持空白；不取云数据替换本机缓存。
        if(Cloud->HasConflict()) { AddAction(CloudActions,TEXT("CloudLocal"),TEXT("保留本机检查点")); AddAction(CloudActions,TEXT("CloudRemote"),TEXT("使用云端检查点")); }
        else AddAction(CloudActions,TEXT("CloudRetry"),TEXT("重试同步"));
    }
}

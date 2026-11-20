// 职责：R04合作房间浏览：固定网络页签、域名/IP直连入口、真实列表与连接状态侧栏。使用：仅读RoomSubsystem缓存，版本化加入与直连均交MenuFlow。
#include "UI/Screens/DemoSessionScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h"
#include "Player/DemoPlayerController.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"

void UDemoSessionScreen::BuildRooms()
{
    DEMO_LOG_CALL();
    ConfigureLayout(1600.f,835.f);
    SetHeading(TEXT("组建突破小队"),TEXT("2～4人合作 · 房主保存检查点 · 安全区开放加入与重连"));
    AddBack(TEXT("OverlayBack"),TEXT("← 返回大厅 / 取消连接"));
    const auto* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // 借用服务当前结果，绝不捕获到异步回调。
    if(!Rooms) { AddText(PageBody,TEXT("房间服务不可用")); return; }
    const bool Busy=Rooms->IsBusy(),Ready=Rooms->IsReady(); const auto& Items=Rooms->GetRooms(); // 业务仍再次验证。
    const bool bSearching=Rooms->GetState()==EDemoRoomState::Searching; // LAN刚进页会自动搜索；直连允许同步取消搜索并立即连接。
    const bool bCanDirectJoin=!Rooms->IsPublic()&&Ready&&(!Busy||bSearching);
    RoomRevision=Rooms->GetRevision(); RoomOffset=Flow()->GetRoomPage()*5;
    if(Rooms->IsPublic()) { DirectErrorMessage.Empty(); bRestoreDirectFocus=false; } // 公网EOS禁止IP直连回退。
    UHorizontalBox* Network=PageCategories?PageCategories.Get():AddRow(PageBody); // 固定页签，不随五条房间记录滚动。
    AddAction(Network,TEXT("RoomLAN"),TEXT("局域网 / 直连"),!Busy,!Rooms->IsPublic()); AddAction(Network,TEXT("RoomPublic"),TEXT("公网 / ONLINE"),!Busy,Rooms->IsPublic());
    UVerticalBox* Connection=ShowAside(); // 连接/账号状态独立滚动，不能被某个长房间名挤出视口。
    AddSessionLabel(Connection,TEXT("CONNECTION STATUS"));
    AddText(Connection,Busy?TEXT("正在处理请求"):Ready?TEXT("服务已就绪"):TEXT("等待连接服务"),24,Busy?DemoUI::Gold:Ready?DemoUI::Accent:DemoUI::Ink);
    AddText(Connection,Rooms->IsPublic()?TEXT("公网合作"):TEXT("本地网络 / 域名直连"),18,DemoUI::Muted);
    AddSessionRule(Connection);
    AddText(Connection,Rooms->GetStatus(),17,DemoUI::Gold); // 原登录/搜索/加入失败原因完整显示，不只保留一枚红点。
    if(!Controller()->GetConnectionStatus().IsEmpty()) AddText(Connection,Controller()->GetConnectionStatus(),17,DemoUI::Gold);
    if(Rooms->IsPublic()) AddAction(Connection,TEXT("RoomAccount"),Ready?TEXT("退出账号"):TEXT("登录账号"),!Busy); // LAN不摆放永远禁用的账号操作。
    AddSessionRule(Connection);
    AddText(Connection,TEXT("加入规则"),19,DemoUI::Ink);
    AddText(Connection,TEXT("仅合法安全阶段允许加入或重连。进入战斗后队伍锁定，各玩家经济与解锁独立。"),17,DemoUI::Muted);
    AddText(Connection,TEXT("可取消连接返回大厅；服务器状态变化时，原服务会重新核验房间。"),16,DemoUI::Muted);
    if(!Rooms->IsPublic())
    {
        // 非公网模式提供域名/IP直连入口，适用于公网IP或VPS UDP转发（默认端口7777），底层与JoinCoop共用受限旅行。
        UVerticalBox* DirectCard=AddSessionCard(PageBody);
        AddSessionLabel(DirectCard,TEXT("DIRECT CONNECT / 域名或 IP 直连"));
        UHorizontalBox* DirectRow=AddRow(DirectCard);
        const UDemoUITheme* Theme=UsesR02Presentation()?R02Theme():nullptr; // 复用R02只读字体与色调，不引入外部系统字体。
        UBorder* InputFrame=WidgetTree->ConstructWidget<UBorder>();
        InputFrame->SetPadding(FMargin(1.f));
        InputFrame->SetBrushColor(Theme?Theme->Ink.CopyWithNewOpacity(Theme->HairlineOpacity):DemoUI::Edge);
        DirectAddressEntry=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("DirectAddressEntry"));
        DirectAddressEntry->WidgetStyle.SetFont(Theme?Theme->GetLabelFont(20):DemoUI::TextFont(20));
        const FSlateRoundedBoxBrush EntryBrush(FLinearColor(.03f,.03f,.035f,.94f),3.f);
        DirectAddressEntry->WidgetStyle.SetBackgroundImageNormal(EntryBrush).SetBackgroundImageHovered(EntryBrush).SetBackgroundImageFocused(EntryBrush);
        DirectAddressEntry->WidgetStyle.SetForegroundColor(FSlateColor(FLinearColor(.95f,.94f,.91f))).SetFocusedForegroundColor(FSlateColor(FLinearColor::White));
        DirectAddressEntry->WidgetStyle.SetPadding(FMargin(14.f,10.f));
        DirectAddressEntry->SetHintText(FText::FromString(TEXT("输入房主域名或 IP，如 play.example.com:7777（留空端口默认 7777）")));
        DirectAddressEntry->SetText(FText::FromString(Rooms->GetDirectAddressDraft()));
        DirectAddressEntry->SetIsReadOnly(!bCanDirectJoin);
        DirectAddressEntry->SetSelectAllTextWhenFocused(false); // 避免后台LAN搜索结束重建恢复焦点时全选覆盖已输入的字符。
        DirectAddressEntry->SetClearKeyboardFocusOnCommit(false);
        DirectAddressEntry->SetRevertTextOnEscape(false);
        DirectAddressEntry->OnTextChanged.AddDynamic(this,&UDemoSessionScreen::OnDirectAddressChanged);
        DirectAddressEntry->OnTextCommitted.AddDynamic(this,&UDemoSessionScreen::OnDirectAddressCommitted);
        InputFrame->SetContent(DirectAddressEntry);
        if(auto* FrameSlot=Cast<UHorizontalBoxSlot>(DirectRow->AddChild(InputFrame)))
        {
            FrameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            FrameSlot->SetVerticalAlignment(VAlign_Center);
            FrameSlot->SetPadding(FMargin(4.f,4.f,12.f,4.f));
        }
        UDemoMenuButton* DirectJoin=AddCompactAction(DirectRow,TEXT("Room.DirectJoin"),TEXT("直连加入  →"),bCanDirectJoin,170.f);
        DirectJoin->SetVisualRole(EDemoButtonVisualRole::Primary); DirectJoin->UseSingleLineLabel();
        AddText(DirectCard,TEXT("适用于具备公网 IP 或 VPS UDP 转发（默认 UDP 7777）的房主；需两端版本一致且房主位于安全区。"),15,DemoUI::Muted);
        DirectErrorText=AddText(DirectCard,DirectErrorMessage,16,DemoUI::Danger);
        if(DirectErrorText) DirectErrorText->SetVisibility(DirectErrorMessage.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    }
    AddSessionLabel(PageBody,FString::Printf(TEXT("AVAILABLE SESSIONS / %02d"),Items.Num()));
    for(int32 Row=0;Row<5&&Items.IsValidIndex(RoomOffset+Row);++Row)
    {
        const auto& Item=Items[RoomOffset+Row]; // 本行不保存地址，服务依据目录版本解析。
        UHorizontalBox* Line=AddRow(PageBody); UVerticalBox* Identity=AddColumn(Line); // 轻量表格式记录，五条默认房间初屏可见；长名称仍可增加行高/滚动。
        UTextBlock* Name=AddText(Identity,Item.Name,21,DemoUI::Ink); Name->SetAutoWrapText(false); Name->SetWrapTextAt(670.f); // 完整名称可换行，不把地址/身份令牌加入界面。
        if(auto* NameSlot=Cast<UVerticalBoxSlot>(Name->Slot)) NameSlot->SetPadding(FMargin(4,1));
        UTextBlock* Stage=AddText(Identity,Item.Stage,16,DemoUI::Muted); if(auto* StageSlot=Cast<UVerticalBoxSlot>(Stage->Slot)) StageSlot->SetPadding(FMargin(4,1)); // 副行减小重复边距，不降低字号。
        UVerticalBox* Detail=AddColumn(Line); // 小列只描述公开人数与实测延迟，未测不假写0ms。
        if(auto* DetailSlot=Cast<UHorizontalBoxSlot>(Detail->Slot)) { FSlateChildSize DetailSize(ESlateSizeRule::Fill); DetailSize.Value=.30f; DetailSlot->SetSize(DetailSize); } // UE5.4只提供SizeRule构造，比例通过Value设置。
        UTextBlock* Population=AddText(Detail,FString::Printf(TEXT("%d / 4 人"),Item.Players),18,DemoUI::Ink); if(auto* NumberSlot=Cast<UVerticalBoxSlot>(Population->Slot)) NumberSlot->SetPadding(FMargin(4,1));
        UTextBlock* Ping=AddText(Detail,Item.PingMs==INDEX_NONE?TEXT("延迟未测"):FString::Printf(TEXT("%d ms"),Item.PingMs),16,DemoUI::Muted); if(auto* PingSlot=Cast<UVerticalBoxSlot>(Ping->Slot)) PingSlot->SetPadding(FMargin(4,1));
        UDemoMenuButton* Join=AddCompactAction(Line,FName(*FString::Printf(TEXT("Room%d"),Row)),Item.bJoinable?TEXT("加入  →"):TEXT("暂不可加入"),!Busy&&Ready&&Item.bJoinable,130.f); Join->SetVisualRole(EDemoButtonVisualRole::Primary); // 使用本页行号/修订，不能直接缓存地址。
        AddSessionRule(PageBody);
    }
    if(Items.IsEmpty())
    {
        UVerticalBox* Empty=AddSessionCard(PageBody); AddSessionLabel(Empty,Busy?TEXT("SEARCHING FOR SQUAD"):TEXT("NO OPEN SESSIONS"));
        AddText(Empty,Busy?TEXT("正在寻找同伴…"):TEXT("暂未发现可加入的房间"),29,DemoUI::Ink);
        AddText(Empty,Busy?TEXT("等待搜索结果。处理完成前不能重复提交请求，可以返回取消。"):Ready?TEXT("刷新当前列表，创建自己的房间，或在上方输入房主域名/IP直连。请确认使用同一网络模式与兼容版本。"):TEXT("先处理右侧连接或账号提示。服务就绪后再刷新或创建房间。"),19,DemoUI::Muted);
    }
    UHorizontalBox* Footer=PageFooter?PageFooter.Get():AddRow(PageBody); // 翻页、刷新、创建都固定在正文外，不因空列表或长错误跳动。
    AddCompactAction(Footer,TEXT("RoomPrevious"),TEXT("上一页"),!Busy&&RoomOffset>0,90.f);
    AddText(Footer,FString::Printf(TEXT("%d / %d"),Flow()->GetRoomPage()+1,FMath::Max(1,(Items.Num()+4)/5)),17,DemoUI::Muted);
    AddCompactAction(Footer,TEXT("RoomNext"),TEXT("下一页"),!Busy&&RoomOffset+5<Items.Num(),90.f);
    AddCompactAction(Footer,TEXT("RoomRefresh"),TEXT("刷新列表"),!Busy&&Ready,140.f);
    AddCompactAction(Footer,TEXT("RoomQuick"),TEXT("快速加入"),!Busy&&Ready,140.f);
    AddCompactAction(Footer,TEXT("RoomCreate"),TEXT("创建房间  +"),!Busy&&Ready,180.f)->SetVisualRole(EDemoButtonVisualRole::Primary);
}
void UDemoSessionScreen::JoinDisplayedRoom(int32 Row)
{
    DEMO_LOG_CALL();
    if(!Flow()||Flow()->GetMenuPage()!=EDemoMenuPage::Multiplayer||Row<0||Row>=5) { UE_LOG(LogFPSDemo,Log,TEXT("ROOM_UI rejected stale page/row")); return; }
    Flow()->JoinRoom(RoomOffset+Row,RoomRevision);
}
void UDemoSessionScreen::SubmitDirectJoin()
{
    DEMO_LOG_CALL();
    if(!Flow()||Flow()->GetMenuPage()!=EDemoMenuPage::Multiplayer||!DirectAddressEntry)
    { UE_LOG(LogFPSDemo,Log,TEXT("ROOM_DIRECT_UI rejected stale page/entry")); return; }
    FString Error; // 规则校验与旅行状态机统一由MenuFlow/RoomSubsystem裁决，UI不自行ClientTravel。
    if(!Flow()->JoinDirectRoom(DirectAddressEntry->GetText().ToString(),Error))
    {
        DirectErrorMessage=Error;
        if(DirectErrorText)
        {
            DirectErrorText->SetText(FText::FromString(DirectErrorMessage));
            DirectErrorText->SetVisibility(DirectErrorMessage.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
        }
        DirectAddressEntry->SetUserFocus(GetOwningPlayer());
        return;
    }
    DirectErrorMessage.Empty(); bRestoreDirectFocus=false;
    if(DirectErrorText) DirectErrorText->SetVisibility(ESlateVisibility::Collapsed);
}
void UDemoSessionScreen::OnDirectAddressChanged(const FText& Text)
{
    DEMO_LOG_TICK();
    if(auto* Rooms=GetGameInstance()?GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>():nullptr) Rooms->SetDirectAddressDraft(Text.ToString());
    if(!DirectErrorMessage.IsEmpty())
    {
        DirectErrorMessage.Empty();
        if(DirectErrorText) { DirectErrorText->SetText(FText::GetEmpty()); DirectErrorText->SetVisibility(ESlateVisibility::Collapsed); }
    }
}
void UDemoSessionScreen::OnDirectAddressCommitted(const FText& Text,ETextCommit::Type Method)
{
    DEMO_LOG_CALL();
    if(Method==ETextCommit::OnEnter) SubmitDirectJoin(); // 失焦不自动发起网络连接，防止点击返回或切页误触直连。
}
bool UDemoSessionScreen::CaptureExtraFocusBeforeRebuild()
{
    DEMO_LOG_TICK();
    bRestoreDirectFocus=false;
    if(!DirectAddressEntry) return false;
    if(auto* Rooms=GetGameInstance()?GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>():nullptr) Rooms->SetDirectAddressDraft(DirectAddressEntry->GetText().ToString());
    bRestoreDirectFocus=DirectAddressEntry->HasKeyboardFocus()||DirectAddressEntry->HasUserFocus(GetOwningPlayer())||DirectAddressEntry->HasAnyUserFocus();
    return bRestoreDirectFocus;
}
UWidget* UDemoSessionScreen::NativeGetDesiredFocusTarget() const
{
    DEMO_LOG_TICK();
    if(bRestoreDirectFocus&&DirectAddressEntry&&!DirectAddressEntry->GetIsReadOnly()) return DirectAddressEntry.Get();
    return Super::NativeGetDesiredFocusTarget();
}
void UDemoSessionScreen::NativeDestruct()
{
    DEMO_LOG_CALL();
    DirectAddressEntry=nullptr; DirectErrorText=nullptr; DirectErrorMessage.Empty(); bRestoreDirectFocus=false;
    Super::NativeDestruct();
}

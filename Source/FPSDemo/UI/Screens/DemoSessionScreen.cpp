// 职责：R02竖向三槽档案/无框暂停菜单；其他会话页保留旧主题。只读当前拥有者/服务缓存，所有动作仍交原MenuFlow。
#include "UI/Screens/DemoSessionScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/R02/DemoR02EditorialButton.h" // 仅设置本次动作的衬线字号，不改变CommonUI输入或白名单。
#include "Player/DemoPlayerController.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Save/DemoRunSave.h"
#include "Game/DemoGameState.h" // 暂停简报只读当前公开阶段/关数/难度，不依赖Unity包含顺序。
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h" // 只按真实本机Viewport/DPI排版，缩放或改窗口后刷新，不修改设备分辨率。
#include "Debug/DemoLog.h"

bool UDemoSessionScreen::UsesR02Presentation() const
{
    DEMO_LOG_TICK();
    const UDemoMenuFlowComponent* Menu=Flow(); // 当前拥有者意图只借用，不缓存或创建另一套页面状态。
    if(!Menu) return false;
    const EDemoMenuPage Page=Menu->GetMenuPage(); // 全会话/大厅/合作房间/指南/暂停/退出页面统一启用R02暗黑电影感变体。
    return Page==EDemoMenuPage::Saves||Page==EDemoMenuPage::CreateSave||Page==EDemoMenuPage::Pause||Page==EDemoMenuPage::ReturnHub||Page==EDemoMenuPage::Multiplayer||Page==EDemoMenuPage::QuitSaving||Page==EDemoMenuPage::FirstRunGuide||Page==EDemoMenuPage::EndlessUnlock;
}

FString UDemoSessionScreen::RefreshKey() const
{
    DEMO_LOG_TICK();
    FString Key=FString::Printf(TEXT("%d/%d/%d/%d/%d|%s|%s"),int32(Flow()->GetMenuPage()),int32(Flow()->GetQuitState()),Flow()->IsQuitLocalSaved(),Flow()->IsChoosingHostSave(),Flow()->GetRoomPage(),*Flow()->GetMenuMessage(),*Controller()->GetConnectionStatus()); // 本地状态键。
    if(const auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>())
    {
        Key+=Saves->GetStatus();
        for(int32 Index=0;Index<3;++Index) // 缓存三栏，不扫描磁盘。
        {
            const auto* Data=Saves->GetSlot(Index); // 本帧借用缓存UObject。
            Key+=FString::Printf(TEXT("|%d/%d"),Index,Saves->SlotExists(Index));
            if(Data) Key+=FString::Printf(TEXT("/%s/%d/%d/%d/%d/%d/%d/%d"),*Data->CreatedLocal.ToString(),Data->CompletedLevel,Data->Coins,Data->SilverCoins,int32(Data->Phase),Data->bEndless,Data->BestEndlessLevel,int32(Data->Difficulty)); // 档案的真实难度变化也刷新，不能沿用同金额旧文本。
        }
    }
    if(const auto* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()) Key+=FString::Printf(TEXT("|%u/%d/%d/%d/%d|%s"),Rooms->GetRevision(),int32(Rooms->GetState()),Rooms->IsBusy(),Rooms->IsReady(),Rooms->IsPublic(),*Rooms->GetStatus());
    const auto& View=Controller()->GetMenuSnapshot(); // 暂停页的钱包/构筑来自本人RPC；联机菜单不暂停时仍能刷新到账。
    Key+=FString::Printf(TEXT("|owner=%d:%d:%d:%d"),View.Progress.Coins,View.Progress.SilverCoins,View.Progress.Kills,View.Build.Revision);
    Key+=FString::Printf(TEXT("|pause=%d/net=%d"),Controller()->IsPaused(),int32(GetWorld()->GetNetMode())); // 实际暂停事实变化也刷新标题，不能仅按旧构建时的单人假设显示。
    int32 ViewportWidth=0,ViewportHeight=0; Controller()->GetViewportSize(ViewportWidth,ViewportHeight); // 本机物理视口，仅参与布局键，不触发应用设置。
    Key+=FString::Printf(TEXT("|viewport=%dx%d:%.3f"),ViewportWidth,ViewportHeight,UWidgetLayoutLibrary::GetViewportScale(this));
    if(const auto* State=GetWorld()->GetGameState<ADemoGameState>()) Key+=FString::Printf(TEXT("|run=%d:%d:%d"),int32(State->Phase),State->LevelNumber,int32(State->Difficulty)); // 公开阶段变化不能保留旧暂停文案。
    return Key;
}
void UDemoSessionScreen::BuildBody()
{
    DEMO_LOG_CALL();
    DirectAddressEntry=nullptr; DirectErrorText=nullptr; // 每次正文重建前释放旧树临时引用，避免切页后悬空。
    if(!Flow()||Flow()->GetMenuPage()!=EDemoMenuPage::Multiplayer) { DirectErrorMessage.Empty(); bRestoreDirectFocus=false; }
    switch(Flow()->GetMenuPage())
    {
    case EDemoMenuPage::Saves: case EDemoMenuPage::CreateSave: BuildSaves(); break;
    case EDemoMenuPage::Pause: case EDemoMenuPage::ReturnHub: BuildPause(); break;
    case EDemoMenuPage::QuitSaving: BuildExit(); break;
    case EDemoMenuPage::Multiplayer: BuildRooms(); break;
    case EDemoMenuPage::FirstRunGuide: BuildGuide(); break;
    default: SetHeading(TEXT("返回游戏")); AddBack(TEXT("OverlayBack"),TEXT("返回上一层")); break;
    }
    if(!Flow()->GetMenuMessage().IsEmpty()) AddText(PageBody,Flow()->GetMenuMessage(),17,UsesR02Presentation()&&R02Theme()?R02Theme()->Ember:DemoUI::Gold); // 业务错误完整可读，不用装饰成功提示覆盖。
}
void UDemoSessionScreen::BuildSaves()
{
    DEMO_LOG_CALL();
    ConfigureLayout(1440.f,835.f,true);
    SetHeading(Flow()->IsChoosingHostSave()?TEXT("房主档案"):TEXT("战役档案"),TEXT("CAMPAIGN RECORDS / 三个独立检查点"));
    for(const FName Name:{FName(TEXT("PageWallet")),FName(TEXT("PageSectionLabel"))})
        if(UWidget* Widget=WidgetTree->FindWidget(Name)) Widget->SetVisibility(ESlateVisibility::Collapsed); // 尚未选槽时PS钱包不代表任一真实档案；三槽自己的币值仍在各记录内。
    AddBack(TEXT("OverlayBack"),Flow()->IsChoosingHostSave()?TEXT("← 返回多人游戏"):TEXT("← 返回大厅"));
    const auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // GI缓存，无磁盘副作用。
    if(!Saves) { AddText(PageBody,TEXT("正在等待存档服务…")); return; }
    const UDemoUITheme* Theme=R02Theme(); // 页面已持有的只读主题，无独立背景或运行时图片。
    if(auto* HeaderRegion=Cast<USizeBox>(GetWidgetFromName(TEXT("PageHeaderBounds")))) HeaderRegion->SetHeightOverride(144.f); // 每次重建显式给C02页头预算，不继承暂停页大标题高度。
    if(Theme&&PageHeading) { PageHeading->SetFont(Theme->GetSerifFont(56,TEXT("Regular"),true)); PageHeading->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
    if(Theme&&PageSubtitle) { PageSubtitle->SetFont(Theme->GetLabelFont(16)); PageSubtitle->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
    AddText(PageBody,TEXT("选择一份行动记录"),22,Theme?Theme->Muted:DemoUI::Muted);
    AddGap(PageBody,14.f);
    for(int32 Index=0;Index<3;++Index)
    {
        const auto* Data=Saves->GetSlot(Index); // 槽数据只借用到构建结束。
        const bool Protected=Saves->SlotExists(Index)&&!Data; // 坏档/未来版本保留原文件，不能当作空栏创建。
        AddSessionRule(PageBody);
        UHorizontalBox* Record=AddRow(PageBody); // 三条竖向真实记录，顺序固定；目录展示不选槽或创建存档。
        if(auto* RecordLayout=Cast<UVerticalBoxSlot>(Record->Slot)) RecordLayout->SetPadding(FMargin(0,8,0,18));
        USizeBox* NumberBounds=WidgetTree->ConstructWidget<USizeBox>(); NumberBounds->SetWidthOverride(90.f); Record->AddChild(NumberBounds); // 大序号只读，不产生第二个点击入口。
        if(auto* NumberLayout=Cast<UHorizontalBoxSlot>(NumberBounds->Slot)) NumberLayout->SetVerticalAlignment(VAlign_Center);
        UVerticalBox* NumberColumn=WidgetTree->ConstructWidget<UVerticalBox>(); NumberBounds->SetContent(NumberColumn);
        UTextBlock* Number=AddText(NumberColumn,FString::Printf(TEXT("%02d"),Index+1),48,Data?(Theme?Theme->Ink:DemoUI::Ink):(Theme?Theme->Muted:DemoUI::Muted));
        if(Theme) Number->SetFont(Theme->GetSerifFont(48,TEXT("Regular"),false));
        UVerticalBox* Detail=AddColumn(Record); // 只有这列横向Fill，操作保持右侧Auto宽度；长错误可以完整换行。
        if(auto* DetailLayout=Cast<UHorizontalBoxSlot>(Detail->Slot)) { DetailLayout->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); DetailLayout->SetPadding(FMargin(18,0,24,0)); }
        AddText(Detail,Data?TEXT("继续行动"):Protected?TEXT("档案受到保护"):TEXT("新的突破"),30,Theme?Theme->Ink:DemoUI::Ink);
        if(Data)
        {
            AddText(Detail,TEXT("开始于 ")+Data->CreatedLocal.ToString(TEXT("%Y-%m-%d %H:%M:%S")),15,Theme?Theme->Muted:DemoUI::Muted); // 创建本地时间不冒充最近保存。
            const TCHAR* Phase=Data->Phase==EDemoPhase::Hub?TEXT("安全区"):Data->Phase==EDemoPhase::Victory?TEXT("已通关"):Data->Phase==EDemoPhase::Reward?TEXT("待领取奖励"):TEXT("关间检查点"); // 只翻译合法已保存阶段。
            const TCHAR* Difficulty=Data->Difficulty==EDemoDifficulty::Easy?TEXT("简单"):Data->Difficulty==EDemoDifficulty::Hard?TEXT("困难"):Data->Difficulty==EDemoDifficulty::Hell?TEXT("地狱"):TEXT("普通"); // 不把DT行键当中文显示名。
            AddText(Detail,Data->bEndless?FString::Printf(TEXT("无尽 · 已完成 %d 关 / %s难度"),Data->CompletedLevel,Difficulty)
                :FString::Printf(TEXT("%s · 已完成 %02d / 10 / %s难度"),Phase,Data->CompletedLevel,Difficulty),18,Theme?Theme->Ink:DemoUI::Ink);
            AddText(Detail,FString::Printf(TEXT("金币 %d   银币 %d"),Data->Coins,Data->SilverCoins),15,Theme?Theme->Muted:DemoUI::Muted);
        }
        else
        {
            AddText(Detail,Protected?TEXT("无法安全读取。原文件已保留，当前不会覆盖或新建。"):TEXT("从安全区开始，整备装备并选择本轮核心。"),17,Protected?(Theme?Theme->Ember:DemoUI::Gold):(Theme?Theme->Muted:DemoUI::Muted)); // 损坏保护真值不改变，空槽不会直接开局。
        }
        UDemoMenuButton* Action=AddCompactAction(Record,FName(*FString::Printf(TEXT("Save%d"),Index)),Data?TEXT("继续  →"):Protected?TEXT("已保护"):TEXT("创建  +"),!Protected,180.f); // 原Save0..2及创建/Host守卫，不新增选择状态。
        Action->SetMinDimensions(180.f,54.f); Action->UseSingleLineLabel();
        if(auto* ActionLayout=Cast<UHorizontalBoxSlot>(Action->Slot)) ActionLayout->SetVerticalAlignment(VAlign_Center);
    }
    AddSessionRule(PageBody);
    AddText(PageBody,Flow()->IsChoosingHostSave()?TEXT("房主档案保存固定队伍检查点；各玩家经济与解锁独立。"):TEXT("未完成的战斗从最近检查点重新开始。"),16,Theme?Theme->Muted:DemoUI::Muted);
    if(!Saves->GetStatus().IsEmpty()) AddText(PageBody,Saves->GetStatus(),16,Theme?Theme->Ember:DemoUI::Gold); // 保留迁移/云/读档错误原文，不伪造保存成功。
}
void UDemoSessionScreen::BuildPause()
{
    DEMO_LOG_CALL();
    int32 ViewportWidth=0,ViewportHeight=0; Controller()->GetViewportSize(ViewportWidth,ViewportHeight); // 当前物理窗口，只读，用于720p/1080p一致的内容比例。
    const float ViewportScale=FMath::Max(.01f,UWidgetLayoutLibrary::GetViewportScale(this)); // 先转UMG逻辑单位，避免720p时把1400逻辑宽误当1400物理px。
    const float LogicalWidth=ViewportWidth>0?static_cast<float>(ViewportWidth)/ViewportScale:1920.f;
    const float LogicalHeight=ViewportHeight>0?static_cast<float>(ViewportHeight)/ViewportScale:1080.f;
    const float LayoutWidth=FMath::Clamp(LogicalWidth-256.f,1100.f,1700.f),LayoutHeight=FMath::Clamp(LogicalHeight-240.f,760.f,900.f); // 居中并留约C05比例的两侧空白，窄窗口由既有ScaleBox保可达。
    ConfigureLayout(LayoutWidth,LayoutHeight,false);
    UE_LOG(LogFPSDemo,Log,TEXT("R02_PAUSE layout viewport=%dx%d dpi=%.3f content=%.1fx%.1f"),ViewportWidth,ViewportHeight,ViewportScale,LayoutWidth,LayoutHeight);
    if(auto* Surface=Cast<UBorder>(WidgetTree->FindWidget(TEXT("PanelSurface")))) Surface->SetPadding(FMargin(32.f,72.f,32.f,32.f)); // 大标题上留C05呼吸区；旧页由Base逐次恢复，不带入Rooms/Guide。
    for(const FName Name:{FName(TEXT("PageWallet")),FName(TEXT("PageSectionLabel"))})
        if(UWidget* Widget=WidgetTree->FindWidget(Name)) Widget->SetVisibility(ESlateVisibility::Collapsed); // 本页只保右侧实际简报，移除重复TACTICAL OPERATIONS和顶栏PS钱包。
    const bool Solo=GetWorld()->GetNetMode()==NM_Standalone; // 显示语义只读，不由Widget暂停World。
    const bool Paused=Solo&&Controller()->IsPaused(); // 单人也必须核真实暂停事实，不能只靠打开菜单推断。
    const UDemoUITheme* Theme=R02Theme(); // 本页共享只读主题，无图片/场景截图或独立动态背景。
    SetHeading(Paused?TEXT("暂停"):TEXT("菜单"),!Solo?TEXT("世界继续运行"):Paused?TEXT("GAME PAUSED / Esc 返回行动"):TEXT("Esc 返回行动"));
    if(auto* HeaderRegion=Cast<USizeBox>(GetWidgetFromName(TEXT("PageHeaderBounds")))) HeaderRegion->SetHeightOverride(240.f); // 标题/副题独占明确预算，不与01行共享最后一段高度。
    if(Theme&&PageHeading) PageHeading->SetFont(Theme->GetSerifFont(96,TEXT("Regular"),true)); // C05大衬线标题，保持真正字体栅格化，不拉伸旧文字。
    if(Theme&&PageSubtitle) { PageSubtitle->SetFont(Theme->GetLabelFont(16)); PageSubtitle->SetColorAndOpacity(Theme->Muted); }
    UHorizontalBox* Columns=AddRow(PageBody); // 左菜单与右简报均无底板，保留足够负空间。
    UVerticalBox* Actions=AddColumn(Columns); UVerticalBox* Brief=AddColumn(Columns);
    if(auto* ActionsLayout=Cast<UHorizontalBoxSlot>(Actions->Slot)) { ActionsLayout->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); ActionsLayout->SetPadding(FMargin(0,0,64,0)); }
    if(auto* BriefLayout=Cast<UHorizontalBoxSlot>(Brief->Slot)) { BriefLayout->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); BriefLayout->SetPadding(FMargin(120,0,0,0)); BriefLayout->SetVerticalAlignment(VAlign_Center); } // 右简报居菜单中部并靠右对齐，保留中间负空间。
    const FName ActionIds[]={TEXT("OverlayBack"),TEXT("PauseHub"),TEXT("PauseSettings"),TEXT("PauseLobby"),TEXT("PauseQuit")}; // 原五个白名单，不自行处理旅行/设置/保存。
    const TCHAR* Captions[]={TEXT("继续行动"),TEXT("返回安全区"),TEXT("设置"),TEXT("返回大厅"),TEXT("保存并退出")}; // 用户看到的编号不成为业务ID。
    for(int32 Index=0;Index<5;++Index)
    {
        UDemoMenuButton* Action=AddAction(Actions,ActionIds[Index],FString::Printf(TEXT("%02d   %s"),Index+1,Captions[Index]),true,false);
        Action->SetVisualRole(EDemoButtonVisualRole::Menu); Action->SetMinDimensions(340.f,58.f); Action->UseSingleLineLabel();
        if(auto* Editorial=Cast<UDemoR02EditorialButton>(Action)) Editorial->SetR02Appearance(R02Theme(),false,32,true); // 五行沿同一真实CommonUI动作，初始红线随首项焦点点亮并随选中项跳转。
        if(auto* ActionLayout=Cast<UVerticalBoxSlot>(Action->Slot)) ActionLayout->SetPadding(FMargin(0,0,0,2));
        AddR02Rule(Actions,4.f); // 每行共享1物理px分隔；选中Ember标记仍由原真实按钮维护，分隔不命中。
    }
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 当前公开阶段；不按物理位置猜测安全区。
    const FString Phase=!State?TEXT("行动状态同步中"):State->Phase==EDemoPhase::Hub?TEXT("安全区"):State->Phase==EDemoPhase::Intermission?TEXT("原地备战"):State->Phase==EDemoPhase::Combat?TEXT("交战中"):State->Phase==EDemoPhase::Reward?TEXT("待领奖励"):TEXT("正在处理行动结果");
    AddText(Brief,Phase,27,Theme?Theme->Ink:DemoUI::Ink)->SetJustification(ETextJustify::Right);
    if(State)
    {
        const TCHAR* Difficulty=State->Difficulty==EDemoDifficulty::Easy?TEXT("简单"):State->Difficulty==EDemoDifficulty::Hard?TEXT("困难"):State->Difficulty==EDemoDifficulty::Hell?TEXT("地狱"):TEXT("普通"); // 实际公开难度，不写死普通或根据菜单选择猜测。
        const FString Progress=State->Phase==EDemoPhase::Hub?FString(Difficulty)+TEXT("难度")
            :State->bEndless?FString::Printf(TEXT("无尽 · 第 %d 关 · %s"),State->LevelNumber,Difficulty)
            :FString::Printf(TEXT("%s / X · %s"),*DemoUIR02::ToRoman(State->LevelNumber),Difficulty); // 第0关Hub不伪装已完成00/10，战斗/关间仍显示真实当前关数。
        AddText(Brief,Progress,20,Theme?Theme->Ink:DemoUI::Ink)->SetJustification(ETextJustify::Right);
    }
    const auto& View=Controller()->GetMenuSnapshot(); // 本人可靠拥有者投影，不读队友经济或旧GS钱包字段。
    AddR02Rule(Brief,10.f);
    AddText(Brief,FString::Printf(TEXT("银币 %d"),View.Progress.SilverCoins),28,Theme?Theme->Ink:DemoUI::Ink)->SetJustification(ETextJustify::Right); // 一行真实本轮银币，不拆成大0与重复标签。
    AddGap(Brief,12.f);
    if(State&&State->Phase!=EDemoPhase::Hub) AddText(Brief,TEXT("返回安全区将清空本轮银币与临时成长。"),15,Theme?Theme->Ember:DemoUI::Gold)->SetJustification(ETextJustify::Right); // Hub返回仅关闭菜单，没有损失警告；清空确认仍由原Flow决定。
    AddText(Brief,TEXT("大厅/退出保留最近检查点；未完成战斗重开。"),15,Theme?Theme->Muted:DemoUI::Muted)->SetJustification(ETextJustify::Right);
    if(!Controller()->GetConnectionStatus().IsEmpty()) AddText(Brief,Controller()->GetConnectionStatus(),15,Theme?Theme->Ember:DemoUI::Gold)->SetJustification(ETextJustify::Right); // 真实连接错误保留。
}
void UDemoSessionScreen::BuildExit()
{
    DEMO_LOG_CALL();
    const auto State=Flow()->GetQuitState(); const bool Failed=State==EDemoQuitState::Failed; // 只有明确失败才开放重试/仅本地退出。
    ConfigureLayout(1240.f,760.f);
    SetHeading(Failed?TEXT("进度保存需要处理"):State==EDemoQuitState::Saved?TEXT("进度已保存"):TEXT("正在保存行动记录"),TEXT("以实际保存回执为准 · 可取消退出返回上一层"));
    AddBack(TEXT("QuitCancel"),TEXT("← 取消退出，返回上一层"));
    UHorizontalBox* Columns=AddRow(PageBody); UVerticalBox* Local=AddSessionCard(Columns); UVerticalBox* Cloud=AddSessionCard(Columns); // 分清本机和云端，不用单一‘成功’掩盖部分失败。
    AddSessionLabel(Local,TEXT("01 / LOCAL CHECKPOINT"));
    AddText(Local,Flow()->IsQuitLocalSaved()?TEXT("本地记录已保存"):TEXT("本地记录待完成"),25,Flow()->IsQuitLocalSaved()?DemoUI::Accent:DemoUI::Gold);
    AddText(Local,GetWorld()->GetNetMode()==NM_Client?TEXT("本机永久档案与战局检查点分开保存，战局检查点由房主维护。"):TEXT("检查点与永久进度由原存档服务写入；未完成战斗不会记录为已通关。"),18,DemoUI::Muted);
    AddSessionLabel(Cloud,TEXT("02 / SYNC STATUS"));
#if WITH_EDITOR
    AddText(Cloud,TEXT("本次为本地试玩"),25,DemoUI::Ink);
    AddText(Cloud,TEXT("编辑器试玩不连接云端。结束试玩后，本次试玩进度和解锁清空；设备设置保留。"),18,DemoUI::Muted);
#else
    AddText(Cloud,State==EDemoQuitState::WaitingCloud?TEXT("正在等待云端确认"):State==EDemoQuitState::Saved?TEXT("保存流程已完成"):Failed?TEXT("同步需要重试"):TEXT("等待保存服务"),25,Failed?DemoUI::Gold:DemoUI::Ink);
    AddText(Cloud,State==EDemoQuitState::WaitingCloud?TEXT("最长等待30秒。以原服务回执为准，取消退出不会伪造上传成功。"):TEXT("具体云端状态与错误见下方提示。尚未收到确认时，不显示上传成功。"),18,DemoUI::Muted);
#endif
    if(Failed) { AddFooterAction(TEXT("QuitRetry"),TEXT("重试保存")); AddFooterAction(TEXT("QuitLocal"),TEXT("仅本地保存并退出"),Flow()->IsQuitLocalSaved(),240.f)->SetVisualRole(EDemoButtonVisualRole::Standard); } // 失败才暴露原合法恢复入口。
}
void UDemoSessionScreen::BuildGuide()
{
    DEMO_LOG_CALL();
    ConfigureLayout(1600.f,835.f);
    SetHeading(TEXT("准备好开始行动。"),TEXT("操作速览 · 关闭后，本机不再自动显示此提示"));
    const TCHAR* Titles[]={TEXT("移动与观察"),TEXT("武器与装填"),TEXT("技能与整备"),TEXT("信息与协作")}; // 与当前实际输入绑定一致，不新建重绑定选项。
    const TCHAR* Keys[]={TEXT("WASD / MOUSE / SPACE"),TEXT("LMB / RMB / R / 1 · 2 · 3"),TEXT("SHIFT / Q / E"),TEXT("TAB / ESC / G / T")};
    const TCHAR* Copy[]={TEXT("WASD移动，鼠标观察，Space跳跃。\n保持机动，留意目标方向与敌人推进。"),TEXT("左键攻击；右键瞄准或副攻击，依当前装备而定。R装填。\n1主武器、2手枪、3近战；主武器须在终端装备。"),TEXT("Shift冲刺，Q治疗，E与附近终端交互。\n出发前选择一种流派核心，整备后进入下一关。"),TEXT("Tab查看构筑，Esc打开菜单，G战术标记。\n按住T通话，松开停止。合作菜单不会暂停战斗。")};
    for(int32 RowIndex=0;RowIndex<2;++RowIndex)
    {
        UHorizontalBox* Row=AddRow(PageBody); // 两行两卡，键位与说明分层，完整页面仍可正文滚动。
        for(int32 Column=0;Column<2;++Column)
        {
            const int32 Index=RowIndex*2+Column; UVerticalBox* Card=AddSessionCard(Row); // 稳定顺序不会与焦点操作混用。
            AddSessionLabel(Card,FString::Printf(TEXT("FIELD GUIDE / 0%d"),Index+1)); AddText(Card,Titles[Index],23,DemoUI::Ink);
            AddSessionLabel(Card,Keys[Index],19);
            UTextBlock* Detail=AddText(Card,Copy[Index],18,DemoUI::Muted); Detail->SetAutoWrapText(false); Detail->SetWrapTextAt(625.f);
        }
    }
    AddFooterAction(TEXT("OverlayBack"),TEXT("开始行动  →")); // 唯一关闭动作沿原设备已读记录链，不新增业务意图。
}

UVerticalBox* UDemoSessionScreen::AddSessionCard(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    UBorder* Surface=WidgetTree->ConstructWidget<UBorder>(); Surface->SetPadding(FMargin(22,18)); Surface->SetBrushColor(DemoUI::Surface.CopyWithNewOpacity(.20f)); // 保留R04灰半透明，不重复铺厚重蓝色面板。
    UPanelSlot* LayoutSlot=Parent->AddChild(Surface); // 当前树持有装饰和内容，不创建业务缓存。
    if(auto* Horizontal=Cast<UHorizontalBoxSlot>(LayoutSlot)) { Horizontal->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Horizontal->SetPadding(FMargin(8,8)); }
    if(auto* Vertical=Cast<UVerticalBoxSlot>(LayoutSlot)) Vertical->SetPadding(FMargin(4,7));
    UVerticalBox* Content=WidgetTree->ConstructWidget<UVerticalBox>(); Surface->SetContent(Content); AddSessionRule(Content);
    return Content;
}
void UDemoSessionScreen::AddSessionRule(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    if(UsesR02Presentation()) { AddR02Rule(Parent,12.f); return; } // Base统一换算真实物理1px；旧页保留原细线与布局。
    USizeBox* Size=WidgetTree->ConstructWidget<USizeBox>(); Size->SetHeightOverride(1.f); // 细线不接受输入或抢焦点。
    UBorder* Line=WidgetTree->ConstructWidget<UBorder>(); Line->SetPadding(FMargin(0));
    Line->SetBrushColor(DemoUI::Edge); // R02已走上方统一物理线helper；这里只保留旧Exit/Guide等原色。
    Line->SetVisibility(ESlateVisibility::HitTestInvisible); Size->SetContent(Line);
    UPanelSlot* LayoutSlot=Parent->AddChild(Size); if(auto* Vertical=Cast<UVerticalBoxSlot>(LayoutSlot)) Vertical->SetPadding(FMargin(4,0,4,12)); // 保持统一阅读间距。
}
UTextBlock* UDemoSessionScreen::AddSessionLabel(UPanelWidget* Parent,const FString& Text,int32 LabelSize)
{
    DEMO_LOG_CALL();
    const UDemoUITheme* Theme=UsesR02Presentation()?R02Theme():nullptr; // 复用同一Session实例切回Rooms/Exit/Guide时不污染旧字体。
    UTextBlock* Label=AddText(Parent,Text,LabelSize,Theme?Theme->Muted:DemoUI::Gold); Label->SetFont(Theme?Theme->GetLabelFont(LabelSize):DemoUI::DisplayFont(LabelSize,TEXT("Medium")));
    return Label;
}

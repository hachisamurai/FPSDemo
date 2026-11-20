// 职责：UMG终端页模型比较、生命周期、固定页签与R04属性/核心布局。
// 使用：由CommonUI协调器RefreshScreen；只读拥有者快照/GAS及房主GI缓存的检查点反馈；核心按R02页型分派，奖励、装备、抽取与出发分文件。
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIStyle.h"
#include "UI/DemoUIPresentation.h" // 本机减少动效仅进入显示Key，不修改奖励或原跳过设置。
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoCloudSync.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "Game/DemoGameState.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Save/DemoRunSave.h" // 只读取本机权威房主已有的检查点状态；UI不保存、不创建服务或联网。
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h" // S1只在真实视口/DPI改变时重排，避免窗口改窄仍沿上次宽屏布局。
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StreamableManager.h"
#include "UObject/Class.h"
#include "Debug/DemoLog.h"

FString UDemoTerminalScreen::RefreshKey() const
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* PC=Controller(); // 当前拥有者，仅同步借用；旅行后不复用。
    const UDemoMenuFlowComponent* Menu=Flow(); // 菜单选择的唯一真值，不在本页另存子页枚举。
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 公开阶段可能比交互回执晚抵达。
    if(!PC||!Menu||!State) return TEXT("terminal-waiting");
    const FDemoPlayerMenuSnapshot& View=PC->GetMenuSnapshot(); // 拥有者脱敏快照，没有未揭晓雷位。
    FString Key; // 仅用于相等比较，不写日志、文件、云或复制；变化才重建UMG。
    FDemoPlayerMenuSnapshot::StaticStruct()->ExportText(Key,&View,nullptr,nullptr,PPF_None,nullptr);
    Key+=FString::Printf(TEXT("|phase=%d|level=%d|difficulty=%d|endless=%d|next=%d|reward=%d|core=%d|weapon=%d|ammo=%d|gamble=%d|item=%d|choice=%d|pool=%d|wait=%d|skip=%d|icon=%u"),
        int32(State->Phase),State->LevelNumber,int32(State->Difficulty),State->bEndless,Menu->IsNextLevelConfirmationOpen(),Menu->IsRewardMenu(),
        Menu->IsCoreMenuOpen(),Menu->IsWeaponMenuOpen(),Menu->IsAmmoMenuOpen(),Menu->IsGambleMenuOpen(),Menu->GetInspectedAmmo(),
        Menu->GetInspectedGamble(),Menu->GetGambleRewardPage(),Menu->IsGambleWaiting(),Menu->IsGambleAnimationSkipped(),WeaponIconRevision);
    Key+=PC->GetMenuMessage();
    Key+=FString::Printf(TEXT("|reduced=%d"),DemoUI::ReducedMotion()); // 奖励带标题/等待说明跟随本机偏好，非每帧时间。
    Key+=FString::Printf(TEXT("|presentationWeapon=%d"),GetPresentationWeaponIndex()); // R04只读目录选择变化重建展示；不作为装备/解锁真值。
    Key+=FString::Printf(TEXT("|presentationCore=%d"),GetPresentationCoreIndex()); // 目录仅预览索引改变才重建，不伪造核心选择/修订或保存序列。
    if(UsesR02Presentation())
    {
        const FVector2D Viewport=UWidgetLayoutLibrary::GetViewportSize(GetWorld()); // UE5.4此接口需要非const WorldContext；只读当前本机像素，不移除Screen自身const限定。
        Key+=FString::Printf(TEXT("|R02viewport=%d:%d|dpi=%d"),FMath::RoundToInt(Viewport.X),FMath::RoundToInt(Viewport.Y),FMath::RoundToInt(UWidgetLayoutLibrary::GetViewportScale(this)*1000.f)); // 只把尺寸变化加入10Hz模型比较，不逐帧重建。
    }
    if(Menu->IsNextLevelConfirmationOpen()) FDemoTeamReadySnapshot::StaticStruct()->ExportText(Key,&State->TeamReady,nullptr,nullptr,PPF_None,nullptr);
    if(Menu->IsNextLevelConfirmationOpen()) FDemoEncounterPreparationView::StaticStruct()->ExportText(Key,&State->Preparation,nullptr,nullptr,PPF_None,nullptr); // 握手人数/失败变化才刷新，不把逐秒时钟加入布局Key。
    if(Menu->IsNextLevelConfirmationOpen()&&State->Preparation.bActive&&State->Preparation.bReadyToCommit&&PC->HasAuthority()&&View.bCanLeadSession&&GetGameInstance())
        if(const UDemoRunSaves* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>()) Key+=TEXT("|departureCheckpoint=")+Saves->GetStatus(); // 异步出发失败不一定有新命令回执；同一准备状态下，本机缓存反馈变化也要重建。远端队员不读取自己的档案冒充房主保存结果。
    const FDemoGambleReel& Reel=Menu->GetGambleReel(); // Position/StartSeconds故意不加入，动画不会逐帧重建树。
    Key+=FString::Printf(TEXT("|reel=%d:%d:%d:%s:%s"),Reel.Cards.Num(),Reel.Target,Reel.bRolling,*Reel.ResultId.ToString(),*Reel.ResultMessage);
    for(const FDemoGambleReward& Card:Reel.Cards) Key+=Card.Id.ToString()+Card.Title+Card.Description;
    const ADemoCharacter* Player=Cast<ADemoCharacter>(PC->GetPawn()); // GAS属性由本人ASC复制，仅展示全局加成。
    if(const UDemoAttributeSet* Attributes=Player?Player->GetDemoAttributes():nullptr)
        Key+=FString::Printf(TEXT("|gas=%g:%g:%g"),Attributes->GetWeaponDamageBonus(),Attributes->GetMaxHealth(),Attributes->GetMagazineBonus());
    if(Menu->IsWeaponMenuOpen()&&GetGameInstance())
    {
        if(const UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) Key+=Profile->GetStatus(); // 读取已存在的状态，不创建交易或IO。
        if(const UDemoCloudSync* Cloud=GetGameInstance()->GetSubsystem<UDemoCloudSync>()) Key+=Cloud->GetStatus(); // Editor不创建该子系统，缺失合法。
    }
    return Key;
}

void UDemoTerminalScreen::BuildBody()
{
    DEMO_LOG_CALL();
    ReelCanvas=nullptr; ReelCards.Reset(); ReelTitles.Reset(); ReelCardIndices.Reset(); // 前一树的动画引用不得保留到其他子页。
    ADemoPlayerController* PC=Controller(); // 只借用到本次同步构建完成。
    UDemoMenuFlowComponent* Menu=Flow(); // 页面真值始终来自既有Flow。
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 公开状态未就绪时只呈现等待。
    if(!PC||!Menu||!State||!PageBody)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("UMG_TERMINAL waiting for owner, flow, state or body"));
        SetHeading(TEXT("备战终端"),TEXT("等待服务器状态"));
        return;
    }
    if(Menu->IsNextLevelConfirmationOpen()) { BuildDeparture(State); return; }
    if(Menu->IsRewardMenu()) { BuildRewards(); return; }
    AddBack(TEXT("UI.Back"),Menu->IsWeaponMenuOpen()||Menu->IsAmmoMenuOpen()||Menu->IsCoreMenuOpen()||Menu->IsGambleMenuOpen()?TEXT("← 返回终端首页"):TEXT("← 返回游戏"),!Menu->IsGambleWaiting());
    if(Menu->IsGambleMenuOpen()&&!Menu->GetGambleReel().Cards.IsEmpty()) { BuildGambleReel(); return; }
    BuildTabs(State);
    const ADemoCharacter* Player=Cast<ADemoCharacter>(PC->GetPawn()); // 角色可能正在重生；各子页允许安全缺省。
    if(Menu->IsCoreMenuOpen()) BuildCores();
    else if(Menu->IsGambleMenuOpen()) BuildGamble(State);
    else if(Menu->IsWeaponMenuOpen()) BuildWeapons(Player);
    else if(Menu->IsAmmoMenuOpen()) BuildAmmo(Player);
    else BuildStats(State,Player);
    if(!PC->GetMenuMessage().IsEmpty()) AddText(PageBody,PC->GetMenuMessage(),16,DemoUI::Gold);
}

void UDemoTerminalScreen::BuildTabs(const ADemoGameState* State)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Tabs=PageCategories?PageCategories.Get():AddRow(PageBody); // 固定分类导航；商品滚动不改变其位置与原白名单。
    const UDemoMenuFlowComponent* Menu=Flow(); // 本次构建读取，不跨帧保存。
    const bool Enabled=!Menu->IsGambleWaiting(); // 等待可靠回执期间阻止页签重复请求；服务端仍重新验证。
    AddAction(Tabs,TEXT("TerminalStats"),TEXT("属性升级"),Enabled,!Menu->IsWeaponMenuOpen()&&!Menu->IsAmmoMenuOpen()&&!Menu->IsCoreMenuOpen()&&!Menu->IsGambleMenuOpen());
    AddAction(Tabs,TEXT("TerminalWeapons"),TEXT("武器"),Enabled,Menu->IsWeaponMenuOpen());
    if(State->Phase==EDemoPhase::Hub)
    {
        AddAction(Tabs,TEXT("TerminalAmmo"),TEXT("弹药类型"),Enabled,Menu->IsAmmoMenuOpen());
        AddAction(Tabs,TEXT("TerminalCore"),TEXT("流派核心"),Enabled,Menu->IsCoreMenuOpen());
    }
    AddAction(Tabs,TEXT("TerminalGamble"),State->Phase==EDemoPhase::Hub?TEXT("金币娱乐 / 遗物"):TEXT("冒险强化"),Enabled,Menu->IsGambleMenuOpen());
    AddFooterAction(TEXT("Close"),TEXT("返回游戏")); // 等待中仍可离开，已提交事务不因此取消。
}

void UDemoTerminalScreen::BuildStats(const ADemoGameState* State,const ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 价格/增量/可操作状态来自权威快照。
    const bool Gold=State->Phase==EDemoPhase::Hub; // 仅币种文案，不能据此允许跨币支付。
    const TCHAR* Currency=Gold?TEXT("金币"):TEXT("银币"); // 静态文案生命周期为进程。
    SetHeading(Gold?TEXT("永久强化"):TEXT("本轮强化"),Gold?TEXT("安全区使用金币 · 永久保留 · 每项独立计价"):TEXT("关间使用银币 · 本轮有效 · 每项独立计价"));
    UHorizontalBox* Cards=AddRow(PageBody); // R04三张等宽强化卡；固定导航/钱包/返回不随卡片说明滚动。
    for(int32 Index=0;Index<FMath::Min(3,View.UpgradeOffers.Num());++Index)
    {
        const FDemoShopOfferSnapshot& Offer=View.UpgradeOffers[Index]; // 每项独立涨价；客户端不重算Cost。
        UVerticalBox* Card=AddProgressionCard(Cards,FString::Printf(TEXT("UPGRADE / %02d"),Index+1)); // 编号只表达阅读顺序，动作索引始终使用同一Offer。
        UTextBlock* Title=AddText(Card,Offer.Title,26,DemoUI::Ink); // 中文标题使用实际Medium字重，不把等级写成虚构稀有度。
        Title->SetFont(DemoUI::TextFont(26,TEXT("Medium")));
        UTextBlock* Gain=AddText(Card,FString::Printf(TEXT("%+g"),Offer.Gain),58,Gold?DemoUI::Gold:DemoUI::Ink); // 大读数明确是本次实际增量，0收益不会显示假加成。
        Gain->SetFont(DemoUI::NumberFont(58));
        AddText(Card,FString::Printf(TEXT("当前 %g  →  升级后 %g"),Offer.CurrentValue,Offer.CurrentValue+Offer.Gain),17,DemoUI::Muted);
        AddProgressionRule(Card);
        UTextBlock* Detail=AddText(Card,Offer.Detail,17,DemoUI::Muted); // 显式安全换行宽度避免首帧按单行分配而绘制多行；完整文字仍保留滚动出口。
        Detail->SetAutoWrapText(false); Detail->SetWrapTextAt(395.f);
        if(!Offer.BlockReason.IsEmpty())
        {
            UTextBlock* Reason=AddText(Card,Offer.BlockReason,16,DemoUI::Gold); // 禁用原因不藏进Hover或只通过颜色表达。
            Reason->SetAutoWrapText(false); Reason->SetWrapTextAt(395.f);
        }
        AddGap(Card,16.f,true); // 各卡的真实CommonUI购买按钮保持相同下沿，长说明仍能在正文滚动。
        UDemoMenuButton* Purchase=AddAction(Card,FName(*FString::Printf(TEXT("Upgrade%d"),Index)),FString::Printf(TEXT("购买强化  ·  %d %s"),Offer.Cost,Currency),Offer.BlockReason.IsEmpty()); // 点击仅提交原UpgradeStat请求，不做本地扣费。
        Purchase->SetVisualRole(EDemoButtonVisualRole::Primary);
    }
    if(View.UpgradeOffers.IsEmpty()) AddText(PageBody,TEXT("等待服务器商品报价"),16,DemoUI::Muted);
    AddGap(PageBody,12.f);
    AddText(PageBody,FString::Printf(TEXT("%s已购买 %d 次  ·  仅上涨本次所购属性的价格"),Currency,View.Progress.UpgradeProgress.Total(Gold)),16,DemoUI::Muted);
    if(const UDemoAttributeSet* Attributes=Player?Player->GetDemoAttributes():nullptr)
        AddText(PageBody,FString::Printf(TEXT("实际配置  /  伤害加成 %g  ·  生命上限 %g  ·  弹匣加成 %g"),Attributes->GetWeaponDamageBonus(),Attributes->GetMaxHealth(),Attributes->GetMagazineBonus()),16,DemoUI::Accent);
    // 合作中个人阵亡会观战并随队复活；只有整轮结束才清理本轮来源，文案不把两者混为“死亡”。
    AddText(PageBody,Gold?TEXT("金币成长与金币价格随当前存档永久保留。银币不能支付本页商品。"):
        TEXT("个人阵亡后随队复活保留银币与本轮成长。\n整轮通关、全员阵亡重开或主动放弃时清空银币、本轮成长及银币价格；金币成长保留。"),15,DemoUI::Muted);
}

void UDemoTerminalScreen::BuildCores()
{
    DEMO_LOG_CALL();
    if(UsesR02Presentation()) { BuildR02Cores(); return; } // C03/B05授权覆盖真实七核心，预览与原明确选择分离；非R02兼容旧卡布局。
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 核心选择仍由原服务器服务授权并保存。
    SetHeading(TEXT("选择流派核心"),TEXT("七种战斗思路选一 · 向下滚动查看全部 · 出发后锁定 · 第3 / 7关选择分支"));
    UHorizontalBox* Cards=nullptr; // 沿用PageBody的原ScrollBox，每行最多三卡，不把七卡压成一行或另加滚动状态。
    const int32 OfferCount=FMath::Min(DemoRunBuild::CoreCount(),View.CoreOffers.Num()); // 拒绝异常投影超出本地可信动作目录。
    for(int32 Index=0;Index<OfferCount;++Index)
    {
        if(Index%3==0) Cards=AddRow(PageBody);
        const FDemoShopOfferSnapshot& Offer=View.CoreOffers[Index]; // 完整说明使用UMG换行，不再截断为六行Canvas文本。
        UVerticalBox* Card=AddProgressionCard(Cards,FString::Printf(TEXT("BUILD FOUNDATION / %02d"),Index+1),Offer.bSelected); // 持久选择使用权威bSelected，不能用焦点替代。
        const TCHAR* Signature=Offer.Id==TEXT("B01")?TEXT("◇"):Offer.Id==TEXT("B02")?TEXT("↗"):Offer.Id==TEXT("B03")?TEXT("⊙"):TEXT("+"); // 纯符号身份，不按数组下标猜测未知核心机制。
        UTextBlock* Title=AddText(Card,FString::Printf(TEXT("%s  %s"),Signature,*Offer.Title),27,DemoUI::Ink); // 符号与中文标题同一基线，给完整玩法条件及选择操作保留初屏高度。
        Title->SetFont(DemoUI::TextFont(27,TEXT("Medium")));
        const FString Trigger=CoreTriggerLine(Offer.Id); // 2026-11-20抽为共享助手，出发页核心选择同口径；完整分支和限制仍以权威说明为准。
        AddText(Card,Trigger.IsEmpty()?FString(TEXT("以服务器核心说明为准")):Trigger,17,DemoUI::Gold);
        AddProgressionRule(Card);
        UTextBlock* Detail=AddText(Card,Offer.Detail,17,DemoUI::Muted); // 实际配置增量/次数/窗口均来自拥有者服务器投影。
        Detail->SetAutoWrapText(false); Detail->SetWrapTextAt(395.f);
        if(!Offer.BlockReason.IsEmpty())
        {
            UTextBlock* Reason=AddText(Card,Offer.BlockReason,16,DemoUI::Gold); // 出发锁定/阶段拒绝仍可读，焦点不被允许状态假替换。
            Reason->SetAutoWrapText(false); Reason->SetWrapTextAt(395.f);
        }
        AddGap(Card,16.f,true); // 同行选择按钮统一下沿，不因不同字数上下跳动。
        UDemoMenuButton* Select=AddAction(Card,FName(*FString::Printf(TEXT("Core%d"),Index)),Offer.bSelected?TEXT("✓  本轮已选择"):TEXT("选择此核心  →"),Offer.BlockReason.IsEmpty(),Offer.bSelected); // 选择继续原SelectCore/Revision/保存流程。
        Select->SetVisualRole(Offer.bSelected?EDemoButtonVisualRole::Catalog:EDemoButtonVisualRole::Primary);
    }
    if(Cards&&OfferCount%3!=0) for(int32 Empty=OfferCount%3;Empty<3;++Empty) AddColumn(Cards); // 最后一行留空列保持卡宽，空列无按钮/业务权限。
    if(View.CoreOffers.IsEmpty()) AddText(PageBody,TEXT("等待服务器核心列表"),16,DemoUI::Muted);
    AddGap(PageBody,12.f);
    AddText(PageBody,View.Build.CoreId.IsNone()?TEXT("当前尚未选择核心；在安全区选择后出发。"):FString::Printf(TEXT("当前核心  /  %s%s"),*DemoRunBuild::Title(View.Build.CoreId).ToString(),View.Build.bLocked?TEXT("  ·  已锁定"):TEXT("  ·  安全区可更换")),17,DemoUI::Accent);
    AddText(PageBody,TEXT("01 选择核心   →   03 第一分支   →   07 第二分支  ·  复活保留，整轮通关、死亡重开或放弃后清除。"),15,DemoUI::Muted);
}

UVerticalBox* UDemoTerminalScreen::AddProgressionCard(UPanelWidget* Parent,const FString& Code,bool Selected)
{
    DEMO_LOG_CALL();
    UVerticalBox* Card=AddCard(Parent); // 只包装当前实例树；沿用水平Fill槽实现等宽/等高，不增加另一个可点击框。
    if(UBorder* Surface=Cast<UBorder>(Card->GetParent()))
    { Surface->SetPadding(FMargin(24,18)); Surface->SetBrushColor(Selected?DemoUI::Selected.CopyWithNewOpacity(.20f):DemoUI::Raised.CopyWithNewOpacity(.14f)); }
    USizeBox* AccentBounds=WidgetTree->ConstructWidget<USizeBox>(); // 2px顶线只给当前核心明确身份，不用持续发光表达。
    AccentBounds->SetHeightOverride(2.f);
    UBorder* Accent=WidgetTree->ConstructWidget<UBorder>(); // 本次树拥有，纯装饰，不捕获鼠标。
    Accent->SetPadding(FMargin(0)); Accent->SetBrushColor(Selected?DemoUI::Gold:DemoUI::Edge); Accent->SetVisibility(ESlateVisibility::HitTestInvisible);
    AccentBounds->SetContent(Accent);
    Card->AddChildToVerticalBox(AccentBounds)->SetPadding(FMargin(4,0,4,16));
    UTextBlock* Number=AddText(Card,Code,16,Selected?DemoUI::Gold:DemoUI::Muted); // 编号英文使用批准的窄体，数值不改原型全局字距。
    FSlateFontInfo CodeFont=DemoUI::DisplayFont(16,TEXT("Medium")); CodeFont.LetterSpacing=120; // UE字距单位为1/1000 em。
    Number->SetFont(CodeFont);
    return Card;
}

void UDemoTerminalScreen::AddProgressionRule(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    USizeBox* Bounds=WidgetTree->ConstructWidget<USizeBox>(); // 1px组织线，不带业务状态/数值。
    Bounds->SetHeightOverride(1.f);
    UBorder* Rule=WidgetTree->ConstructWidget<UBorder>(); // 随当前WidgetTree销毁，不保存或复制。
    Rule->SetPadding(FMargin(0)); Rule->SetBrushColor(DemoUI::Edge); Rule->SetVisibility(ESlateVisibility::HitTestInvisible); Bounds->SetContent(Rule);
    auto* LayoutSlot=Parent->AddChild(Bounds); // 泛用父容器槽仅用于局部内边距。
    if(UVerticalBoxSlot* Vertical=Cast<UVerticalBoxSlot>(LayoutSlot)) Vertical->SetPadding(FMargin(4,9,4,12));
}

void UDemoTerminalScreen::NativeDestruct()
{
    DEMO_LOG_CALL();
    if(WeaponIconLoadHandle) { WeaponIconLoadHandle->CancelHandle(); WeaponIconLoadHandle.Reset(); }
    WeaponIconPaths.Reset(); WeaponIcons.Reset();
    ReelCanvas=nullptr; ReelCards.Reset(); ReelTitles.Reset(); ReelCardIndices.Reset();
    SelectedCoreIndex=INDEX_NONE; // 离开页面不继承上次本地目录预览；下一次按本人已确认核心初始化。
    Super::NativeDestruct();
}

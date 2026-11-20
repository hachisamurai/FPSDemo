// 职责：七玩法UMG选项、报价、持续局面及已保存结果的局部动画。使用：由CommonUI菜单栈管理，所有操作转原Flow；不随机、不扣币、不保存。
#include "UI/Screens/DemoCasinoScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIStyle.h" // R04中性钛灰、骨白及香槟金，所有玩法共用已批准token。
#include "UI/DemoUIPresentation.h" // 本机减少动效只改变已保存结果的表现，不修改赌场Skip或交易状态。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Debug/DemoLog.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h" // 转轴固定符号格让内部纵向布局填满宽度，不依赖自动换行文本的期望宽度。
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"

namespace
{
    const FLinearColor Gold=DemoUI::Gold; // 金币/收益小面积强调，使用同一批准的香槟金。
    const FLinearColor Muted=DemoUI::Muted; // 规则与辅助信息，权限仍由原快照决定。
    const FLinearColor Cyan=DemoUI::Accent; // 历史局部名称保留；现为低饱和青灰，不再使用旧霓虹蓝。
}

FString UDemoCasinoScreen::RefreshKey() const
{
    DEMO_LOG_TICK();
    const auto* Menu=Flow(); const auto* PC=Controller(); // 只借用当前本地拥有者，不跨旅行缓存。
    if(!Menu||!PC) return TEXT("unavailable");
    const auto& D=Menu->GetCasinoDraft(); const auto& V=PC->GetMenuSnapshot(); // 草稿和公开业务快照。
    const auto& G=V.Progress.Gambling; const auto& R=Menu->GetCasinoResult(); // 序号覆盖已保存的持续局面变化。
    FString Key=FString::Printf(TEXT("%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%u"),
        D.Mode,D.Stake,D.Selection,D.Low,D.High,D.Bombs,D.KeepMask,G.Serial,G.Mine.OpenMask,G.Arcade.Mode,G.Arcade.Step,G.Arcade.Shots,
        V.Progress.Coins,R.Serial,R.Mode,Menu->IsGambleWaiting(),Menu->IsGambleAnimationSkipped(),Menu->IsCasinoRolling(),V.CasinoRules.Fingerprint());
    Key+=TEXT("|")+Menu->GetMenuMessage()+TEXT("|")+V.TerminalBlockReason+TEXT("|")+V.CasinoConfigError;
    Key+=bHelpOpen?TEXT("|help"):TEXT("|play");
    Key+=DemoUI::ReducedMotion()?TEXT("|reduced"):TEXT("|motion"); // 偏好变化立即更新静态表现，不改赌场草稿或Skip设置。
    return Key; // 动画进行中只更新现有控件；滚动结束这个结构边沿才重新启用继续按钮。
}

void UDemoCasinoScreen::BuildBody()
{
    DEMO_LOG_CALL(); ConfigureLayout(1600.f,900.f); // 与R04终端同宽，密集棋盘单独保留纵向空间；正文/报价独立滚动。
    RevealStatus=nullptr; RevealValue=nullptr; RevealMessage=nullptr; RevealPayout=nullptr; DiceFaces.Reset(); ReelCells.Reset(); ReelIcons.Reset(); ReelLabels.Reset();
    ReelStates.Reset(); ReelSymbolIndices.Reset(); ArenaFigures.Reset(); ArenaHealthBars.Reset(); ArenaHealthText.Reset(); ExplosionPieces.Reset(); AnimatedMode=INDEX_NONE;
    const auto* Menu=Flow(); const auto* PC=Controller(); // 在当前树构建期间借用，异步不捕获这些指针。
    if(!Menu||!PC||!PageBody) { UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_UMG build rejected: missing owner/body")); return; }
    const auto& V=PC->GetMenuSnapshot(); const auto& D=Menu->GetCasinoDraft(); // 所有价格和概率来自拥有者投影。
    const bool Enabled=!Menu->IsGambleWaiting(); // 模态栈与Flow另行阻断底层点击，不将UI状态作为交易授权。
    SetHeading(DemoCasino::ModeName(D.Mode),TEXT("金币娱乐 / 本人独立结算 · 到账倍率包含本金")); // 钱包已在固定页头，不在副题重复堆叠金额。
    if(bHelpOpen) { BuildHelp(); return; }
    AddBack(TEXT("Casino.Back"),Menu->IsCasinoDevicePage()?TEXT("← 离开设备"):TEXT("← 玩法列表"),Enabled&&!Menu->IsCasinoRolling()); // 实体入口不引导回总列表，仍复用原玩法正文/事务。
    AddHeaderAction(TEXT("GambleSkipAnimation"),Menu->IsGambleAnimationSkipped()?TEXT("✓ 跳过揭示"):TEXT("跳过揭示"),Enabled,150.f); // 与返回共用页头助手；空导航行自动Collapsed，给100格初屏释放一整行。
    AddFooterAction(TEXT("Close"),TEXT("返回游戏"),Enabled);
    AddHeaderAction(TEXT("Casino.Help"),TEXT("玩法说明"),Enabled&&!Menu->IsCasinoRolling(),120.f); // 维持原动作/门禁与字号，不能以缩字或隐藏规则修复棋盘高度。
    if(Menu->GetCasinoResult().Mode>=0&&V.Progress.Gambling.Mine.Stake==0&&V.Progress.Gambling.Arcade.Mode<0)
    { BuildResult(); return; }
    UVerticalBox* Options=PageBody; UVerticalBox* Stake=ShowAside(); // 左侧独立滚动选项，右侧固定宽度报价；各自拥有滚动出口。
    const bool Pending=V.Progress.Gambling.Mine.Stake>0||V.Progress.Gambling.Arcade.Mode>=0; // 只读已保存未结局面。
    if(!V.CasinoConfigError.IsEmpty()) AddCasinoNotice(Options,TEXT("暂时无法开启新项目"),V.CasinoConfigError,true); // 未结合同仍可按冻结数据恢复，不能以坏新配置阻断旧局面。
    if(!V.TerminalBlockReason.IsEmpty()) AddCasinoNotice(Options,TEXT("终端状态"),V.TerminalBlockReason,true);
    if(D.Mode==0) BuildNumbers(Options,Enabled&&!Pending);
    else if(D.Mode==1) BuildSlots(Options);
    else if(D.Mode==2) BuildMine(Options,Enabled);
    else if(D.Mode>=3&&D.Mode<=6) BuildArcade(Options,Enabled);
    else AddText(Options,TEXT("请选择有效的娱乐项目。"),20);
    BuildStakePanel(Stake,Enabled);
    if(!Menu->GetMenuMessage().IsEmpty()) AddCasinoNotice(PageBody,Menu->IsGambleWaiting()?TEXT("请求处理中"):TEXT("操作反馈"),Menu->GetMenuMessage());
}

UVerticalBox* UDemoCasinoScreen::AddCasinoSection(UPanelWidget* Parent,const FString& Code,const FString& Title,bool Selected)
{
    DEMO_LOG_CALL();
    UVerticalBox* Card=AddCard(Parent); // 当前树拥有轻透明卡，不建立点击或业务嵌套。
    if(auto* Surface=Cast<UBorder>(Card->GetParent())) { Surface->SetPadding(FMargin(18,12)); Surface->SetBrushColor((Selected?DemoUI::Selected:DemoUI::Raised).CopyWithNewOpacity(Selected?.24f:.12f)); }
    UTextBlock* Eyebrow=AddText(Card,Code,14,Selected?Gold:Muted); // 编号/英文身份与正文分层。
    FSlateFontInfo Font=DemoUI::DisplayFont(14,TEXT("Medium")); Font.LetterSpacing=100; Eyebrow->SetFont(Font);
    AddText(Card,Title,24,DemoUI::Ink); AddCasinoRule(Card);
    return Card;
}

void UDemoCasinoScreen::AddCasinoRule(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    USizeBox* Bounds=WidgetTree->ConstructWidget<USizeBox>(); Bounds->SetHeightOverride(1.f); // 本次树的组织线，不参与命中。
    UBorder* Line=WidgetTree->ConstructWidget<UBorder>(); Line->SetPadding(FMargin(0)); Line->SetBrushColor(DemoUI::Edge); Line->SetVisibility(ESlateVisibility::HitTestInvisible); Bounds->SetContent(Line);
    if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Parent->AddChild(Bounds))) LayoutSlot->SetPadding(FMargin(4,8,4,12));
}

void UDemoCasinoScreen::AddCasinoValue(UPanelWidget* Parent,const FString& Label,const FString& Value,FLinearColor Color)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Row=AddRow(Parent); // 同一行的真实标签和值，长值保留换行而不遮住下一条。
    UTextBlock* Name=AddText(Row,Label,16,Muted); UTextBlock* Reading=AddText(Row,Value,18,Color); Reading->SetJustification(ETextJustify::Right);
    FSlateChildSize NameSize(ESlateSizeRule::Fill),ValueSize(ESlateSizeRule::Fill); NameSize.Value=.42f; ValueSize.Value=.58f; // UE5.4权重写Value，构造器只接受Auto/Fill枚举。
    if(auto* NameSlot=Cast<UHorizontalBoxSlot>(Name->Slot)) NameSlot->SetSize(NameSize);
    if(auto* ValueSlot=Cast<UHorizontalBoxSlot>(Reading->Slot)) ValueSlot->SetSize(ValueSize);
}

void UDemoCasinoScreen::AddCasinoNotice(UPanelWidget* Parent,const FString& State,const FString& Description,bool Error)
{
    DEMO_LOG_CALL();
    UVerticalBox* Box=AddCard(Parent); // 静态消息不随每帧重建，完整原因保留在正文滚动区。
    if(auto* Surface=Cast<UBorder>(Box->GetParent())) { Surface->SetPadding(FMargin(16,10)); Surface->SetBrushColor(DemoUI::Surface.CopyWithNewOpacity(.22f)); }
    AddText(Box,State,16,Error?DemoUI::Danger:Gold); AddText(Box,Description,16,Muted);
}

UUniformGridPanel* UDemoCasinoScreen::AddGrid(UPanelWidget* Parent,int32 ColumnCount)
{
    DEMO_LOG_CALL();
    UUniformGridPanel* Grid=WidgetTree->ConstructWidget<UUniformGridPanel>(); // 当前树唯一拥有，不建立业务引用。
    Grid->SetSlotPadding(FMargin(2.f)); Grid->SetMinDesiredSlotWidth(ColumnCount>=10?38.f:70.f); Grid->SetMinDesiredSlotHeight(ColumnCount>=10?34.f:40.f); // 100格使用紧凑密度，25格和赔率表保留较高触控行。
    Parent->AddChild(Grid); return Grid;
}

void UDemoCasinoScreen::PlaceGridButton(UDemoMenuButton* Button,int32 Row,int32 Column)
{
    DEMO_LOG_CALL();
    if(!Button) { UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_UMG missing grid button")); return; }
    Button->SetMinDimensions(30,30);
    if(auto* GridSlot=Cast<UUniformGridSlot>(Button->Slot)) { GridSlot->SetRow(Row); GridSlot->SetColumn(Column); GridSlot->SetHorizontalAlignment(HAlign_Fill); GridSlot->SetVerticalAlignment(VAlign_Fill); }
}

void UDemoCasinoScreen::BuildNumbers(UPanelWidget* Parent,bool Enabled)
{
    DEMO_LOG_CALL();
    const auto& D=Flow()->GetCasinoDraft(); const auto& Rules=Controller()->GetMenuSnapshot().CasinoRules; // 本地选择与服务器公开配置。
    AddText(Parent,TEXT("NUMBER SELECTION  /  选择区间、奇偶或一个数字"),17,DemoUI::Ink);
    UHorizontalBox* Quick=AddRow(Parent); // 固定白名单快捷操作，不自行修改草稿。
    AddAction(Quick,TEXT("Casino.Small"),TEXT("小 00–49"),Enabled,D.Selection==0&&D.Low==0&&D.High==49);
    AddAction(Quick,TEXT("Casino.Big"),TEXT("大 50–99"),Enabled,D.Selection==0&&D.Low==50&&D.High==99);
    AddAction(Quick,TEXT("Casino.Odd"),TEXT("奇数"),Enabled,D.Selection==1);
    AddAction(Quick,TEXT("Casino.Even"),TEXT("偶数"),Enabled,D.Selection==2);
    for(UWidget* Child:Quick->GetAllChildren()) if(auto* Button=Cast<UDemoMenuButton>(Child)) { Button->SetVisualRole(EDemoButtonVisualRole::Tab); Button->SetMinDimensions(80.f,38.f); } // 四个真实快捷按钮共同基线，不增加第二套选择状态。
    UHorizontalBox* Low=AddRow(Parent); // 有界连续区间，最终约束沿用Flow和服务器。
    AddCompactAction(Low,TEXT("Casino.LowDown"),TEXT("起点 −"),Enabled);
    AddText(Low,FString::Printf(TEXT("%02d ～ %02d"),D.Low,D.High),20,Gold)->SetFont(DemoUI::DisplayFont(20,TEXT("Medium")));
    AddCompactAction(Low,TEXT("Casino.LowUp"),TEXT("起点 +"),Enabled);
    UHorizontalBox* High=Low; // 共用一行端点调节：四个紧凑按钮与区间值，释放棋盘垂直空间。
    AddCompactAction(High,TEXT("Casino.HighDown"),TEXT("终点 −"),Enabled);
    AddCompactAction(High,TEXT("Casino.HighUp"),TEXT("终点 +"),Enabled);
    double WeightSum=0.; for(double Weight:Rules.NumberWeights) WeightSum+=Weight; // 展示归一化概率，不产生新结果。
    UUniformGridPanel* Grid=AddGrid(Parent,10); // 恰好100个真实可导航CommonUI按钮。
    for(int32 Number=0;Number<100;++Number)
    {
        UDemoMenuButton* Button=AddAction(Grid,FName(*FString::Printf(TEXT("Casino.Number.%d"),Number)),FString::Printf(TEXT("%02d"),Number),Enabled,DemoCasino::Matches(D,Number)); // 当前格受真实输入焦点管理。
        PlaceGridButton(Button,Number/10,Number%10); Button->UseGridDensity(); // 双位数字不换行，减少100格棋盘的无效内边距。
        const double Chance=WeightSum>0&&Rules.NumberWeights.IsValidIndex(Number)?Rules.NumberWeights[Number]/WeightSum:0.; // 不假定数字均匀。
        Button->SetToolTipText(FText::FromString(FString::Printf(TEXT("数字 %02d · 权重 %.4g · 单号概率 %.3f%%"),Number,Rules.NumberWeights.IsValidIndex(Number)?Rules.NumberWeights[Number]:0.,Chance*100.)));
    }
    AddText(Parent,TEXT("高亮为当前覆盖数字；悬停查看单号权重与概率。右侧报价以当前配置为准。"),14,Muted);
}

UTexture2D* UDemoCasinoScreen::SymbolIcon(int32 Index) const
{
    DEMO_LOG_TICK();
    const auto* PC=Controller(); const auto* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 旅行后不复用原Pawn。
    const auto* Ammo=Pawn?Pawn->FindComponentByClass<UDemoAmmoComponent>():nullptr; const auto* Catalog=Ammo?Ammo->GetCatalog():nullptr; // 已加载强引用目录。
    return Index>=0&&Index<4&&Catalog&&Catalog->Entries.IsValidIndex(Index)?Catalog->Entries[Index].Icon.Get():nullptr;
}

void UDemoCasinoScreen::BuildSlots(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    const auto& Rules=Controller()->GetMenuSnapshot().CasinoRules; // 公共配置，UI不拥有随机流。
    AddText(Parent,TEXT("SYMBOL DIRECTORY  /  三个转轴独立抽取"),17,DemoUI::Ink);
    double Total=0.; for(double Weight:Rules.SymbolWeights) Total+=Weight; // 以实际权重求单轴概率。
    for(int32 Group=0;Group<2;++Group)
    {
        UHorizontalBox* Row=AddRow(Parent); // 三列目录，不把真实概率画成虚假的强度条。
        for(int32 Column=0;Column<3;++Column)
        {
            const int32 Index=Group*3+Column; // 同一符号索引同时用于名称、Icon及公开倍率。
            if(Index>=5||!Rules.SymbolWeights.IsValidIndex(Index)||!Rules.TripleMultipliers.IsValidIndex(Index)) { AddColumn(Row); continue; }
            UVerticalBox* Card=AddCasinoSection(Row,FString::Printf(TEXT("SYMBOL / %02d"),Index+1),DemoCasino::SymbolName(Index));
            if(UTexture2D* Texture=SymbolIcon(Index)) AddIcon(Card,Texture,38.f); else AddText(Card,Index==4?TEXT("G"):TEXT("◇"),30,Gold)->SetFont(DemoUI::DisplayFont(30,TEXT("Medium")));
            AddCasinoValue(Card,TEXT("单轴概率"),FString::Printf(TEXT("%.2f%%"),Total>0?Rules.SymbolWeights[Index]/Total*100.:0.),DemoUI::Ink);
            AddCasinoValue(Card,TEXT("三连到账"),FString::Printf(TEXT("%.4g 倍"),Rules.TripleMultipliers[Index]),Gold);
            AddText(Card,FString::Printf(TEXT("配置权重 %.3g"),Rules.SymbolWeights[Index]),13,Muted);
        }
    }
    AddText(Parent,FString::Printf(TEXT("恰好一对 %.4g 倍 / 三个不同 0 倍；三连优先，组合不累加。符号仅结算金币，不解锁弹药。"),Rules.PairMultiplier),16,Muted);
}

void UDemoCasinoScreen::BuildMine(UPanelWidget* Parent,bool Enabled)
{
    DEMO_LOG_CALL();
    const auto& V=Controller()->GetMenuSnapshot(); const auto& Mine=V.Progress.Gambling.Mine; const auto& D=Flow()->GetCasinoDraft(); // 公开副本不含隐藏棋盘。
    const bool Pending=Mine.Stake>0,OtherPending=V.Progress.Gambling.Arcade.Mode>=0; // 其他未结项目不允许开新箱。
    AddText(Parent,Pending?FString::Printf(TEXT("SAVED CRATE / %d 枚雷 · 已开 %d 格"),Mine.Bombs,Mine.OpenCount()):TEXT("MINE CRATE / 25 格 · 选择风险等级"),17,DemoUI::Ink);
    if(!Pending)
    {
        UHorizontalBox* Choices=AddRow(Parent); // 难度索引与服务器配置一一对应。
        for(int32 Index=0;Index<V.CasinoRules.BombChoices.Num();++Index)
            AddAction(Choices,FName(*FString::Printf(TEXT("Casino.Bombs.%d"),Index)),FString::Printf(TEXT("%d 枚雷"),V.CasinoRules.BombChoices[Index]),Enabled&&!OtherPending,D.Bombs==V.CasinoRules.BombChoices[Index])->SetVisualRole(EDemoButtonVisualRole::Tab);
    }
    UUniformGridPanel* Grid=AddGrid(Parent,5); // 无私有雷位判断，关闭重开不会重建经济棋盘。
    Grid->SetMinDesiredSlotHeight(56.f);
    for(int32 Tile=0;Tile<25;++Tile)
    {
        const bool Open=(Mine.OpenMask&(1<<Tile))!=0; // 只检查已经公开的安全位。
        UDemoMenuButton* Button=AddAction(Grid,FName(*FString::Printf(TEXT("Casino.Tile.%d"),Tile)),Open?TEXT("安全"):FString::Printf(TEXT("%02d"),Tile+1),Enabled&&Pending&&!Open,Open); // 禁止重复翻格。
        PlaceGridButton(Button,Tile/5,Tile%5); Button->SetMinDimensions(54.f,54.f); // 25格不是数字板的30px小密度，保留明确翻格触感。
    }
    AddText(Parent,TEXT("安全格提高可领取金额。踩雷失去本箱未领取收益；关闭页面或读档仍是同一箱。"),16,Muted);
}

void UDemoCasinoScreen::BuildArcade(UPanelWidget* Parent,bool Enabled)
{
    DEMO_LOG_CALL();
    const auto& V=Controller()->GetMenuSnapshot(); const auto& R=V.CasinoRules; const auto& D=Flow()->GetCasinoDraft(); const auto& Round=V.Progress.Gambling.Arcade; // 四种玩法仅消费公开数据。
    const bool Pending=Round.Mode==D.Mode,AnyPending=Round.Mode>=0||V.Progress.Gambling.Mine.Stake>0; // 跨玩法待结时只展示规则。
    if(D.Mode==3)
    {
        AddText(Parent,TEXT("REACTOR / 逐级充能 · 决定何时领取"),17,DemoUI::Ink);
        const auto& Chances=Pending?Round.Chances:R.ReactorChances; double Survival=1.; // 已开局使用冻结概率和金额。
        UHorizontalBox* Stages=nullptr; // 每行最多三阶段，超长合法配置仍可正文滚动，不把十阶段横向挤压。
        for(int32 Level=0;Level<Chances.Num();++Level)
        {
            Survival*=Chances[Level]; const int32 Paid=Pending?(Round.Payouts.IsValidIndex(Level)?Round.Payouts[Level]:INDEX_NONE):DemoCasino::Pay(D.Stake,Survival>0?R.ReactorReturn/Survival:0.); // 有界预览，不执行结算。
            if(Level%3==0) Stages=AddRow(Parent);
            const bool Completed=Pending&&Round.Step>Level,Next=Pending&&Round.Step==Level; // 真实冻结进度，不因悬停或视觉动画误标完成。
            UVerticalBox* Stage=AddCasinoSection(Stages,FString::Printf(TEXT("STAGE / %02d"),Level+1),Completed?TEXT("✓ 已完成"):Next?TEXT("下一次充能"):TEXT("待充能"),Completed||Next);
            AddText(Stage,FString::Printf(TEXT("本次成功率 %.1f%%"),Chances[Level]*100.),16,Completed?Cyan:Muted);
            AddText(Stage,Paid<0?TEXT("金额超出存储范围"):FString::Printf(TEXT("安全通过可领 %d 金币"),Paid),18,Gold);
        }
        AddText(Parent,TEXT("每次充能独立判定。成功后可领或继续；过载损失投入，最后一级成功后自动结算。"),16);
        if(Pending)
        {
            UHorizontalBox* Actions=AddRow(Parent); // 续充和领取都由原子保存事务处理。
            AddAction(Actions,TEXT("Casino.Advance"),TEXT("继续充能"),Enabled)->SetVisualRole(EDemoButtonVisualRole::Primary);
            AddAction(Actions,TEXT("Casino.Collect"),TEXT("立即领取"),Enabled&&Round.Step>0);
        }
    }
    else if(D.Mode==4)
    {
        AddText(Parent,TEXT("TACTICAL DICE / 选择保留，或冒险重掷"),17,DemoUI::Ink);
        UHorizontalBox* Dice=AddRow(Parent); // 点击真实骰按钮只修改KeepMask。
        for(int32 Index=0;Index<5;++Index)
        {
            const bool Kept=(D.KeepMask&(1<<Index))!=0; // 五位本地保留草稿。
            UDemoMenuButton* Face=AddAction(Dice,FName(*FString::Printf(TEXT("Casino.Keep.%d"),Index)),Pending&&Round.Values.IsValidIndex(Index)?FString::Printf(TEXT("%d\n%s"),Round.Values[Index],Kept?TEXT("✓ 保留"):TEXT("将重掷")):TEXT("?\n等待开局"),Enabled&&Pending,Kept);
            Face->SetVisualRole(EDemoButtonVisualRole::Catalog); Face->SetMinDimensions(80.f,78.f); // 保留与焦点各自可辨，不伪造骰面图标。
        }
        UUniformGridPanel* PayTable=AddGrid(Parent,2); // 八档互斥组合，便于比较。
        for(int32 Tier=0;Tier<8;++Tier)
        {
            UTextBlock* Text=AddText(PayTable,Pending&&Round.Payouts.IsValidIndex(Tier)?FString::Printf(TEXT("%s：%d金币"),*DemoCasino::DiceTierName(Tier),Round.Payouts[Tier]):FString::Printf(TEXT("%s：%.4g倍"),*DemoCasino::DiceTierName(Tier),R.DiceMultipliers.IsValidIndex(Tier)?R.DiceMultipliers[Tier]:0.),17,Gold); // 冻结金额高于当前INI。
            if(auto* GridSlot=Cast<UUniformGridSlot>(Text->Slot)) { GridSlot->SetRow(Tier/2); GridSlot->SetColumn(Tier%2); }
        }
        const auto& Weights=Pending?Round.Chances:R.DiceWeights; double Sum=0.; for(double Weight:Weights) Sum+=Weight; // 公布真实骰面权重。
        FString Chances=TEXT("各面概率："); // 单段可换行文本，不隐去非均匀骰配置。
        for(int32 Face=0;Face<Weights.Num();++Face) Chances+=FString::Printf(TEXT("%d面 %.2f%%  "),Face+1,Sum>0?100.*Weights[Face]/Sum:0.);
        AddText(Parent,Chances,14,Muted); AddText(Parent,TEXT("点击骰子选择保留；只结算最高组合。关闭和读档不会重投首轮骰。"),16);
        if(Pending)
        {
            UHorizontalBox* Actions=AddRow(Parent); // 一次重掷后终局，不能在UI增加重掷次数。
            AddAction(Actions,TEXT("Casino.Advance"),TEXT("重掷未保留骰并结算"),Enabled)->SetVisualRole(EDemoButtonVisualRole::Primary);
            AddAction(Actions,TEXT("Casino.Collect"),TEXT("直接结算"),Enabled);
        }
    }
    else if(D.Mode==5)
    {
        AddText(Parent,TEXT("MECHANICAL ARENA / 比较双方，选择支持方"),17,DemoUI::Ink);
        if(R.ArenaHealth.Num()!=2||R.ArenaDamage.Num()!=2||R.ArenaHit.Num()!=2) { AddText(Parent,TEXT("战斗配置尚不可用"),18); UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_UMG invalid public arena arrays")); return; }
        UHorizontalBox* Sides=AddRow(Parent); // 两方赔率由现有确定性精确模型提供。
        for(int32 Side=0;Side<2;++Side)
        {
            UVerticalBox* Card=AddCasinoSection(Sides,Side==0?TEXT("CONTENDER / A"):TEXT("CONTENDER / B"),Side==0?TEXT("磁轨兽"):TEXT("装甲蛛"),D.Selection==Side); FDemoCasinoIntent Intent=D; Intent.Selection=Side; // 此局部意图仅用于报价，不提交。
            const FDemoCasinoQuote Quote=V.CasinoConfigError.IsEmpty()?DemoCasino::QuoteArcade(R,Intent):FDemoCasinoQuote(); // 错误配置不参与递推运算。
            AddCasinoValue(Card,TEXT("生命"),FString::FromInt(R.ArenaHealth[Side]),DemoUI::Ink);
            AddCasinoValue(Card,TEXT("伤害"),FString::FromInt(R.ArenaDamage[Side]),DemoUI::Ink);
            AddCasinoValue(Card,TEXT("单次命中率"),FString::Printf(TEXT("%.1f%%"),R.ArenaHit[Side]*100.),Muted);
            AddCasinoValue(Card,TEXT("实际模型胜率"),FString::Printf(TEXT("%.2f%%"),Quote.Chance*100.),Cyan);
            AddCasinoValue(Card,TEXT("总到账倍率"),FString::Printf(TEXT("%.4g 倍"),Quote.Multiplier),Gold);
            AddAction(Card,FName(*FString::Printf(TEXT("Casino.Side.%d"),Side)),D.Selection==Side?TEXT("✓ 当前支持方"):TEXT("支持此方"),Enabled&&!AnyPending,D.Selection==Side)->SetVisualRole(EDemoButtonVisualRole::Catalog);
        }
        AddText(Parent,TEXT("双方随机先手、交替攻击。概率来自实际战斗模型；历史输赢不改变胜率。"),16,Muted);
    }
    else if(D.Mode==6)
    {
        AddText(Parent,TEXT("LIVE FIRE RANGE / 十靶限时挑战"),17,DemoUI::Ink);
        UVerticalBox* Brief=AddCasinoSection(Parent,TEXT("TRAINING BRIEF"),TEXT("真实瞄准，成绩决定到账"));
        AddCasinoValue(Brief,TEXT("新局时限"),FString::Printf(TEXT("%d 秒"),R.RangeSeconds),Gold);
        AddCasinoValue(Brief,TEXT("训练弹药"),FString::Printf(TEXT("%d 发"),Pending&&!Round.Values.IsEmpty()?Round.Values[0]:R.RangeShots),DemoUI::Ink);
        AddText(Brief,TEXT("鼠标瞄准，左键单发；身体 1 分，头部 2 分。确认后立即计时并进入独立靶场，不消耗持有武器弹药，结算后自动返回。"),17,Muted);
        AddText(Parent,TEXT("积分与到账 / 只领取达到的最高一档"),18,DemoUI::Ink);
        for(int32 Tier=0;Tier<5;++Tier)
        {
            if(Pending&&Round.Values.IsValidIndex(Tier+1)&&Round.Payouts.IsValidIndex(Tier)) AddCasinoValue(Parent,FString::Printf(TEXT("至少 %d 分"),Round.Values[Tier+1]),FString::Printf(TEXT("%d 金币"),Round.Payouts[Tier]),Gold);
            else if(R.RangeScores.IsValidIndex(Tier)&&R.RangeMultipliers.IsValidIndex(Tier)) AddCasinoValue(Parent,FString::Printf(TEXT("至少 %d 分"),R.RangeScores[Tier]),FString::Printf(TEXT("%.4g 倍"),R.RangeMultipliers[Tier]),Gold);
        }
        if(Pending)
        {
            AddText(Parent,FString::Printf(TEXT("已用%d发 / 当前%d分 / 命中%d靶"),Round.Shots,Round.Step+Round.Heads,Round.Step),17,Cyan);
            AddAction(Parent,TEXT("Casino.Collect"),TEXT("提前结束并领取当前成绩"),Enabled);
        }
        AddText(Parent,TEXT("已有挑战沿用原截止时间；关闭页面、单人暂停或读档不会重置成绩和时限。"),16,Muted);
    }
}

void UDemoCasinoScreen::BuildStakePanel(UPanelWidget* Parent,bool Enabled)
{
    DEMO_LOG_CALL();
    const auto& V=Controller()->GetMenuSnapshot(); const auto& R=V.CasinoRules; const auto& D=Flow()->GetCasinoDraft(); const auto& Mine=V.Progress.Gambling.Mine; const auto& Round=V.Progress.Gambling.Arcade; // 当前拥有者公开合同，不查询隐藏雷位。
    AddText(Parent,TEXT("TRANSACTION / PERSONAL"),14,Muted)->SetFont(DemoUI::DisplayFont(14,TEXT("Medium")));
    AddText(Parent,Mine.Stake>0||Round.Mode>=0?TEXT("继续已保存局面"):TEXT("本次投入"),24,DemoUI::Ink);
    if(Round.Mode>=0)
    {
        AddText(Parent,DemoCasino::ModeName(Round.Mode),21,Cyan);
        AddCasinoRule(Parent);
        AddCasinoValue(Parent,TEXT("原始下注"),FString::FromInt(Round.Stake),Gold);
        AddCasinoValue(Parent,TEXT("实际投入"),FString::FromInt(Round.Debited),DemoUI::Ink);
        AddText(Parent,TEXT("金币 / 金额与进度已经保存。关闭页面后可继续，不能同时开启另一项目。"),16,Muted);
        if(Round.Mode!=D.Mode) AddAction(Parent,FName(*FString::Printf(TEXT("Casino.Open.%d"),Round.Mode)),TEXT("继续未结项目"),Enabled)->SetVisualRole(EDemoButtonVisualRole::Primary);
        else if(Round.Mode==6) AddAction(Parent,TEXT("Casino.Range"),TEXT("返回靶场 · 左键射击"),Enabled)->SetVisualRole(EDemoButtonVisualRole::Primary);
        else if(Round.Mode==3&&Round.Step>0&&Round.Payouts.IsValidIndex(Round.Step-1))
        {
            AddCasinoRule(Parent); AddText(Parent,TEXT("当前可领取"),16,Muted);
            AddText(Parent,FString::FromInt(Round.Payouts[Round.Step-1]),34,Gold)->SetFont(DemoUI::NumberFont(34));
        }
        return;
    }
    if(Mine.Stake>0)
    {
        const int32 Count=Mine.OpenCount(); // 只计算已公开安全格。
        AddText(Parent,FString::FromInt(Mine.Available()),34,Gold)->SetFont(DemoUI::NumberFont(34));
        AddText(Parent,TEXT("金币 / 当前可领取"),16,Muted); AddCasinoRule(Parent);
        AddCasinoValue(Parent,TEXT("实际投入"),FString::FromInt(Mine.Debited),DemoUI::Ink);
        AddCasinoValue(Parent,TEXT("已开安全格"),FString::Printf(TEXT("%d / 25"),Count),DemoUI::Ink);
        if(Mine.Payouts.IsValidIndex(Count)) AddCasinoValue(Parent,TEXT("下一安全格"),FString::FromInt(Mine.Payouts[Count]),Gold);
        if(Count<25) AddCasinoValue(Parent,TEXT("下一格安全率"),FString::Printf(TEXT("%.2f%%"),100.*(25-Mine.Bombs-Count)/(25-Count)),Cyan);
        AddCasinoValue(Parent,TEXT("当前净收益"),FString::Printf(TEXT("%+d"),Mine.Available()-Mine.Debited),Gold);
        AddCasinoRule(Parent); AddText(Parent,TEXT("踩雷失去本箱未领金币。领取才会结束货箱；保存失败撤销本次操作。"),16,Muted);
        if(D.Mode==2) AddAction(Parent,TEXT("Casino.Cash"),TEXT("领取金币并结束"),Enabled&&Count>0)->SetVisualRole(EDemoButtonVisualRole::Primary);
        else AddAction(Parent,TEXT("Casino.Open.2"),TEXT("继续未结货箱"),Enabled)->SetVisualRole(EDemoButtonVisualRole::Primary);
        return;
    }
    AddText(Parent,FString::FromInt(D.Stake),34,Gold)->SetFont(DemoUI::NumberFont(34));
    AddAction(Parent,TEXT("Casino.BetInput"),TEXT("金币 · 输入金额"),Enabled)->SetVisualRole(EDemoButtonVisualRole::Catalog);
    UUniformGridPanel* Presets=AddGrid(Parent,2); // 四个快捷金额改为二乘二，固定右栏不再挤四列。
    for(int32 Index=0;Index<R.Presets.Num()&&Index<4;++Index)
    {
        UDemoMenuButton* Preset=AddAction(Presets,FName(*FString::Printf(TEXT("Casino.Preset.%d"),Index)),FString::FromInt(R.Presets[Index]),Enabled,D.Stake==R.Presets[Index]);
        PlaceGridButton(Preset,Index/2,Index%2); Preset->UseGridDensity();
    }
    UHorizontalBox* Adjust=AddRow(Parent); // 宽整数加减与存储保护仍由Flow处理，无普通/单号玩法上限。
    AddAction(Adjust,TEXT("Casino.BetDown10"),TEXT("−10"),Enabled)->UseGridDensity(); AddAction(Adjust,TEXT("Casino.BetDown"),TEXT("−"),Enabled)->UseGridDensity();
    AddAction(Adjust,TEXT("Casino.BetUp"),TEXT("+"),Enabled)->UseGridDensity(); AddAction(Adjust,TEXT("Casino.BetUp10"),TEXT("+10"),Enabled)->UseGridDensity();
    AddText(Parent,FString::Printf(TEXT("最低 %d 金币 · 余额与整数保护保留"),R.MinBet),14,Muted);
    AddCasinoRule(Parent);
    const FDemoCasinoQuote Quote=Flow()->GetCasinoQuote(); // 只展示已校验公开报价，确认后服务器仍重新验证。
    if(D.Mode==0||D.Mode==5)
    {
        AddCasinoValue(Parent,D.Mode==0?TEXT("命中概率"):TEXT("所选胜率"),FString::Printf(TEXT("%.3f%%"),Quote.Chance*100.),Cyan);
        AddCasinoValue(Parent,TEXT("总到账倍率"),FString::Printf(TEXT("%.5g 倍"),Quote.Multiplier),DemoUI::Ink);
        AddCasinoValue(Parent,TEXT("命中到账"),FString::FromInt(Quote.Payout),Gold);
        if(D.Mode==0) AddCasinoValue(Parent,TEXT("命中净收益"),FString::Printf(TEXT("%+d"),Quote.Payout-D.Stake),Gold);
    }
    else if(D.Mode==1)
    {
        AddCasinoValue(Parent,TEXT("中奖概率"),FString::Printf(TEXT("%.3f%%"),Quote.Chance*100.),Cyan);
        AddCasinoValue(Parent,TEXT("最高到账"),FString::FromInt(Quote.Payout),Gold);
        AddText(Parent,TEXT("全部组合赔率见左侧符号目录。"),15,Muted);
    }
    else if(D.Mode==2)
    {
        AddCasinoValue(Parent,TEXT("首格安全率"),FString::Printf(TEXT("%.3f%%"),Quote.Chance*100.),Cyan);
        AddCasinoValue(Parent,TEXT("首格安全可领"),FString::FromInt(Quote.Payout),Gold);
        AddCasinoValue(Parent,TEXT("返还系数"),FString::Printf(TEXT("%.3f%%"),R.MineReturn*100.),DemoUI::Ink);
    }
    else
    {
        AddCasinoValue(Parent,TEXT("最高总到账"),FString::FromInt(Quote.MaximumPayout),Gold);
        AddText(Parent,TEXT("按实际组合或成绩结算，不累加所有档位。"),15,Muted);
    }
    AddText(Parent,TEXT("到账包含本金；未中损失本次投入。"),15,Muted);
    if(!Quote.Error.IsEmpty()) AddText(Parent,Quote.Error,16,DemoUI::Danger);
    AddFooterAction(TEXT("Casino.Place"),Enabled?(D.Mode==2?TEXT("投入金币 · 开启货箱"):TEXT("确认本次下注  →")):TEXT("等待服务器确认…"),Enabled&&Quote.Error.IsEmpty()&&V.TerminalBlockReason.IsEmpty(),220.f);
}

void UDemoCasinoScreen::BuildSlotReels(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Reels=AddRow(Parent); // 三个真正裁切的UMG容器，不使用Canvas整页绘制。
    for(int32 Reel=0;Reel<3;++Reel)
    {
        UVerticalBox* Column=AddColumn(Reels); // 单轴当前树根，图标变更不重建树。
        AddText(Column,TEXT("▼"),20,Gold)->SetJustification(ETextJustify::Center);
        USizeBox* Viewport=WidgetTree->ConstructWidget<USizeBox>(); Viewport->SetHeightOverride(180.f); Viewport->SetClipping(EWidgetClipping::ClipToBounds); Column->AddChild(Viewport); // 有限窗口隔离轴外符号。
        UOverlay* Overlay=WidgetTree->ConstructWidget<UOverlay>(); Viewport->SetContent(Overlay); // 四个复用图标围绕中心连续位移。
        for(int32 Cell=0;Cell<4;++Cell)
        {
            USizeBox* CellFrame=WidgetTree->ConstructWidget<USizeBox>(); CellFrame->SetWidthOverride(150.f); CellFrame->SetHeightOverride(100.f); // 与100像素轴间距一致；固定宽度打破AutoWrap期望宽度与居中Overlay之间的收缩循环。
            UOverlaySlot* CellSlot=Overlay->AddChildToOverlay(CellFrame); CellSlot->SetHorizontalAlignment(HAlign_Center); CellSlot->SetVerticalAlignment(VAlign_Top); // 当前槽只布局，无点击。
            UVerticalBox* Root=WidgetTree->ConstructWidget<UVerticalBox>(); CellFrame->SetContent(Root); // 仍对原Root施加动画位移，保留三轴的所有停靠数值。
            if(auto* ContentSlot=Cast<USizeBoxSlot>(Root->Slot)) { ContentSlot->SetHorizontalAlignment(HAlign_Fill); ContentSlot->SetVerticalAlignment(VAlign_Fill); } // 明确填满150宽，不能退回窄图标的期望宽度。
            UImage* Icon=AddIcon(Root,SymbolIcon(Cell),56.f); // 允许金币/缺图为空，文字仍可识别。
            if(auto* IconSlot=Cast<UVerticalBoxSlot>(Icon->Slot)) IconSlot->SetHorizontalAlignment(HAlign_Center); // 正方形图标不随符号名称长度拉伸。
            UTextBlock* Label=AddText(Root,DemoCasino::SymbolName(Cell),16,Gold); Label->SetJustification(ETextJustify::Center); Label->SetAutoWrapText(false); Label->SetWrapTextAt(0.f); // 五种短名称保持单行，其他玩法继续沿用通用自动换行。
            ReelCells.Add(Root); ReelIcons.Add(Icon); ReelLabels.Add(Label); ReelSymbolIndices.Add(INDEX_NONE);
        }
        UTextBlock* State=AddText(Column,TEXT("正在抽取"),14,Muted); State->SetJustification(ETextJustify::Center); ReelStates.Add(State); // 只更新停轴表现。
    }
}

void UDemoCasinoScreen::BuildExplosion(UPanelWidget* Parent)
{
    DEMO_LOG_CALL();
    USizeBox* Frame=WidgetTree->ConstructWidget<USizeBox>(); Frame->SetHeightOverride(190.f); Frame->SetWidthOverride(600.f); Parent->AddChild(Frame); // 爆炸不占满屏幕，避免强闪。
    UCanvasPanel* Layer=WidgetTree->ConstructWidget<UCanvasPanel>(); Frame->SetContent(Layer); Layer->SetVisibility(ESlateVisibility::HitTestInvisible); Layer->SetClipping(EWidgetClipping::ClipToBounds); // 局部真实UMG布局层，不代理整页绘制。
    for(int32 Index=0;Index<24;++Index)
    {
        UImage* Piece=WidgetTree->ConstructWidget<UImage>(); UCanvasPanelSlot* PieceSlot=Layer->AddChildToCanvas(Piece); // 核心0/环1..3/火星4..17/烟18..23。
        const float Size=Index<4?100.f:Index<18?6.f:36.f; // 固定初始像素，后续仅变换。
        PieceSlot->SetAnchors(FAnchors(.5f,.5f)); PieceSlot->SetAlignment(FVector2D(.5f,.5f)); PieceSlot->SetPosition(FVector2D::ZeroVector); PieceSlot->SetSize(FVector2D(Size,Size)); Piece->SetRenderTransformPivot(FVector2D(.5f,.5f));
        if(Index>=1&&Index<=3) Piece->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent,FLinearColor(1.f,.45f,.06f),2.5f,FVector2D(Size,Size)));
        else Piece->SetBrush(FSlateRoundedBoxBrush(Index==0?FLinearColor(1.f,.7f,.12f):Index<18?FLinearColor(1.f,.4f,.04f):FLinearColor(.2f,.16f,.12f),Size*.5f,FVector2D(Size,Size)));
        ExplosionPieces.Add(Piece);
    }
}

void UDemoCasinoScreen::BuildMechanicalFigure(UPanelWidget* Parent,int32 Side)
{
    DEMO_LOG_CALL();
    USizeBox* Frame=WidgetTree->ConstructWidget<USizeBox>(); Frame->SetHeightOverride(110.f); Parent->AddChild(Frame); // 固定卡片内的表现高度。
    UCanvasPanel* Figure=WidgetTree->ConstructWidget<UCanvasPanel>(); Frame->SetContent(Figure); Figure->SetVisibility(ESlateVisibility::HitTestInvisible); // 局部图形控件，没有交互坐标或世界Actor。
    const FVector2D PositionsA[]={FVector2D(-60,22),FVector2D(60,22),FVector2D(0,0),FVector2D(0,-28),FVector2D(-18,-28),FVector2D(18,-28)}; // 磁轨兽：履带、机身、炮塔、传感器。
    const FVector2D SizesA[]={FVector2D(24,48),FVector2D(24,48),FVector2D(130,55),FVector2D(82,35),FVector2D(10,9),FVector2D(10,9)}; // DPI前矩形尺寸。
    const FVector2D PositionsB[]={FVector2D(-72,18),FVector2D(72,18),FVector2D(0,0),FVector2D(0,-30),FVector2D(-18,-30),FVector2D(18,-30)}; // 装甲蛛：外置足架、装甲舱与复眼。
    const FVector2D SizesB[]={FVector2D(38,26),FVector2D(38,26),FVector2D(108,66),FVector2D(76,32),FVector2D(12,12),FVector2D(12,12)}; // 两方造型区别只作用视觉。
    for(int32 Part=0;Part<6;++Part)
    {
        const FVector2D Position=Side==0?PositionsA[Part]:PositionsB[Part],Size=Side==0?SizesA[Part]:SizesB[Part]; // 形状来自本地固定轮廓。
        UImage* Shape=WidgetTree->ConstructWidget<UImage>(); UCanvasPanelSlot* ShapeSlot=Figure->AddChildToCanvas(Shape); // 当前控件树拥有。
        ShapeSlot->SetAnchors(FAnchors(.5f,.5f)); ShapeSlot->SetAlignment(FVector2D(.5f,.5f)); ShapeSlot->SetPosition(Position); ShapeSlot->SetSize(Size);
        const FLinearColor Tint=Part>=4?FLinearColor(.85f,.96f,1.f):Part==3?FLinearColor(.12f,.18f,.22f):Part<2?Muted:Side==0?Cyan:Gold; // 传感器/炮塔/支撑/装甲分层。
        Shape->SetBrush(FSlateRoundedBoxBrush(Tint,4.f,Size));
    }
}

void UDemoCasinoScreen::BuildResult()
{
    DEMO_LOG_CALL();
    const auto* Menu=Flow(); const auto& Result=Menu->GetCasinoResult(); const auto& Rules=Controller()->GetMenuSnapshot().CasinoRules; // 已保存回执只有可公开结果。
    const bool Rolling=Menu->IsCasinoRolling(); AnimatedMode=Result.Mode; // 结构状态只在开始/停靠时变化。
    AddText(PageBody,TEXT("RESULT REVEAL / CONFIRMED RECEIPT"),16,Gold)->SetFont(DemoUI::DisplayFont(16,TEXT("Medium")));
    RevealStatus=AddText(PageBody,Rolling?TEXT("正在揭示本次结果"):TEXT("本次结果已确认"),26,DemoUI::Ink);
    RevealStatus->SetJustification(ETextJustify::Center);
    if(Result.Mode==1)
    {
        AddText(PageBody,TEXT("转轴停止后显示结果；各符号中奖概率见玩法规则。"),15,Muted)->SetJustification(ETextJustify::Center);
        BuildSlotReels(PageBody);
    }
    else if(Result.Mode==4)
    {
        UHorizontalBox* Faces=AddRow(PageBody); // 五个文字控件复用到最终骰面。
        for(int32 Index=0;Index<5;++Index) { UTextBlock* Face=AddText(Faces,TEXT("1"),56,Gold); Face->SetJustification(ETextJustify::Center); DiceFaces.Add(Face); }
    }
    else if(Result.Mode==5&&Rules.ArenaHealth.Num()==2)
    {
        UHorizontalBox* Sides=AddRow(PageBody); // 独立UMG卡片、机械轮廓和生命条。
        for(int32 Side=0;Side<2;++Side)
        {
            UVerticalBox* Figure=AddColumn(Sides); ArenaFigures.Add(Figure); // 进攻动画局部平移，不影响按钮布局。
            AddText(Figure,Side==0?TEXT("磁轨兽"):TEXT("装甲蛛"),25,Side==0?Cyan:Gold)->SetJustification(ETextJustify::Center);
            BuildMechanicalFigure(Figure,Side);
            UProgressBar* Health=WidgetTree->ConstructWidget<UProgressBar>(); Figure->AddChild(Health); Health->SetFillColorAndOpacity(Side==0?Cyan:Gold); Health->SetPercent(1.f); ArenaHealthBars.Add(Health); // 生命回放不发Gameplay事件。
            UTextBlock* HealthText=AddText(Figure,FString::FromInt(Rules.ArenaHealth[Side]),17); HealthText->SetJustification(ETextJustify::Center); ArenaHealthText.Add(HealthText);
        }
    }
    else if((Result.Mode==2||Result.Mode==3)&&Result.bBomb) BuildExplosion(PageBody);
    RevealValue=AddText(PageBody,TEXT(""),Result.Mode==0?66:22,Gold); RevealValue->SetJustification(ETextJustify::Center); // 时间更新，不重新构建。
    if(Result.Mode==0) RevealValue->SetFont(DemoUI::NumberFont(66)); // 两位数字开奖使用项目窄体等宽读数，不把混排文案压成数字格。
    RevealMessage=AddText(PageBody,Rolling?TEXT(""):Menu->GetMenuMessage(),18); RevealMessage->SetJustification(ETextJustify::Center);
    UVerticalBox* Receipt=ShowAside(); // 结果与后续操作分区，原金额/骰面/机械回放仍在左侧实际控件更新。
    AddText(Receipt,TEXT("PERSONAL RECEIPT"),14,Muted)->SetFont(DemoUI::DisplayFont(14,TEXT("Medium")));
    AddText(Receipt,TEXT("本次实际到账"),23,DemoUI::Ink); AddCasinoRule(Receipt);
    RevealPayout=AddText(Receipt,TEXT("等待揭示"),24,Gold);
    AddText(Receipt,TEXT("金币 / 金额已包含本金"),16,Muted);
    AddText(Receipt,TEXT("结果来自服务器已保存的回执，不会因重复打开、跳过动画或关闭页面再次发放。"),16,Muted);
    AddCasinoRule(Receipt); AddText(Receipt,Rolling?TEXT("揭示结束后可继续相同配置，或调整选项。"):TEXT("可继续相同配置，或先调整金额与选项。"),16,Muted);
    UHorizontalBox* Actions=PageFooter?PageFooter.Get():AddRow(PageBody); // 继续抽取与调整选项固定在页脚，动画仍由原Flow解锁。
    AddCompactAction(Actions,TEXT("Casino.Again"),FString::Printf(TEXT("继续抽取 · %d金币"),Menu->GetCasinoDraft().Stake),!Rolling&&!Menu->IsGambleWaiting(),220.f)->SetVisualRole(EDemoButtonVisualRole::Primary);
    AddCompactAction(Actions,TEXT("Casino.ClearResult"),TEXT("调整选项"),!Rolling&&!Menu->IsGambleWaiting());
    AnimateScreen(0.f); // 首帧即反映当前真实时间；跳过时不会闪现未停靠符号。
}

void UDemoCasinoScreen::AnimateExplosion(double Time)
{
    DEMO_LOG_TICK();
    const float T=FMath::Clamp(float(Time),0.f,3.f),Fade=FMath::Clamp(1.f-T/2.6f,0.f,1.f); // 真实秒，结束自然消隐。
    for(int32 Index=0;Index<ExplosionPieces.Num();++Index)
    {
        UImage* Piece=ExplosionPieces[Index]; if(!Piece) continue; // 页面重建前引用由UPROPERTY保活。
        if(Index==0) { Piece->SetRenderScale(FVector2D(1.f+T*2.f,.65f+T)); Piece->SetRenderOpacity(FMath::Max(0.f,1.f-T/.55f)); }
        else if(Index<=3) { const float Radius=.35f+FMath::Max(0.f,T-(Index-1)*.16f)*2.1f; Piece->SetRenderScale(FVector2D(Radius,Radius*.55f)); Piece->SetRenderOpacity(Fade*.8f); } // 环只扩散，不修改Brush。
        else if(Index<18)
        {
            const float Angle=Index*2.39996f,Speed=55.f+(Index%5)*28.f; // 固定视觉序列，不占用随机流。
            Piece->SetRenderTranslation(FVector2D(FMath::Cos(Angle)*Speed*T,FMath::Sin(Angle)*Speed*T*.5f+T*T*18.f)); Piece->SetRenderOpacity(Fade);
        }
        else
        {
            Piece->SetRenderTranslation(FVector2D((Index-20.5f)*18.f*T,-T*(20.f+(Index-18)*3.f))); Piece->SetRenderScale(FVector2D(1.f+T)); Piece->SetRenderOpacity(Fade*.32f); // 烟随时间上浮。
        }
    }
}

void UDemoCasinoScreen::AnimateScreen(float DeltaTime)
{
    DEMO_LOG_TICK();
    const auto* Menu=Flow(); const auto* PC=Controller(); // 每帧借用本地页面，不缓存原始业务对象。
    if(!Menu||!PC||AnimatedMode<0||!RevealValue) return;
    const auto& R=Menu->GetCasinoResult(); const auto& Rules=PC->GetMenuSnapshot().CasinoRules; // 回执中的结果唯一确定停靠和战斗轨迹。
    if(R.Mode!=AnimatedMode) return; // 模型变更交给下一次RefreshScreen构建，旧树不混用新模式数组。
    const bool Reduced=DemoUI::ReducedMotion(),BusinessRolling=Menu->IsCasinoRolling(); // 本机偏好只作用于表现，原解锁/续抽时序继续由Flow决定。
    const double Time=Reduced?4.:Menu->GetCasinoRevealTime(); const bool Rolling=BusinessRolling&&!Reduced; // 减少动效静态呈现已保存终点，不在本页调用Skip/Finish或修改原赌场设置。
    RevealStatus->SetText(FText::FromString(BusinessRolling?(Reduced?TEXT("结果已确认，稍候可继续"):TEXT("正在揭示本次结果")):TEXT("本次结果已确认")));
    RevealMessage->SetText(FText::FromString(Rolling?TEXT(""):Menu->GetMenuMessage()));
    if(RevealPayout)
    {
        const FString Paid=FString::FromInt(R.Paid); // 唯一已保存的实际到账，不由本机赔率推算。
        RevealPayout->SetText(FText::FromString(Rolling?TEXT("等待揭示"):Paid));
        RevealPayout->SetFont(Rolling?DemoUI::TextFont(24):DemoUI::NumberFont(FMath::FloorToInt(FMath::Min(46.f,230.f/FMath::Max(1.f,Paid.Len()*.64f))))); // 十位余额仍限制在侧栏内，不截断金额。
    }
    if(R.Mode==0) RevealValue->SetText(FText::FromString(FString::Printf(TEXT("%02d"),Rolling?int32(Time*53.)%100:R.Number)));
    else if(R.Mode==1&&R.Symbols.Num()==3&&ReelCells.Num()==12)
    {
        for(int32 Reel=0;Reel<3;++Reel)
        {
            const double Stop=1.7+Reel*.55,Progress=FMath::Clamp(Time/Stop,0.,1.),Position=(30.+R.Symbols[Reel])*(1.-FMath::Pow(1.-Progress,3.)); const int32 Middle=FMath::FloorToInt(Position); // 确定性减速到回执目标。
            for(int32 Cell=0;Cell<4;++Cell)
            {
                const int32 SymbolSlot=Middle+Cell-1,Symbol=(SymbolSlot%5+5)%5,Index=Reel*4+Cell; // 循环中只复用相邻4格。
                ReelCells[Index]->SetRenderTranslation(FVector2D(0.f,48.f+float(SymbolSlot-Position)*100.f));
                if(ReelSymbolIndices[Index]!=Symbol)
                {
                    ReelSymbolIndices[Index]=Symbol; UTexture2D* Texture=SymbolIcon(Symbol); // 仅索引改变时刷新已加载纹理。
                    if(ReelIcons[Index]) { ReelIcons[Index]->SetBrushFromTexture(Texture); ReelIcons[Index]->SetRenderOpacity(Texture?1.f:0.f); }
                    ReelLabels[Index]->SetText(FText::FromString(DemoCasino::SymbolName(Symbol)));
                }
            }
            ReelStates[Reel]->SetText(FText::FromString(Time>=Stop?TEXT("已停靠"):TEXT("正在抽取")));
        }
    }
    else if(R.Mode==4)
    {
        const int32 KeepMask=Menu->GetCasinoDraft().KeepMask; // 本次已提交保留标志仍属于同一草稿，结果期Flow禁止改动；只约束视觉，不参与结算。
        for(int32 Index=0;Index<DiceFaces.Num()&&R.Symbols.IsValidIndex(Index);++Index)
        {
            const bool Kept=(KeepMask&(1<<Index))!=0; // 保留骰从第一帧展示权威回执，只有未保留骰使用本地滚动字符。
            DiceFaces[Index]->SetText(FText::AsNumber(!Kept&&Time<2.?1+(int32(Time*20.)+Index)%6:R.Symbols[Index]));
        }
        RevealValue->SetText(FText::FromString(Rolling?TEXT("保留骰已锁定 · 正在抽取"):DemoCasino::DiceTierName(R.Winner)));
    }
    else if(R.Mode==5&&ArenaHealthBars.Num()==2&&Rules.ArenaHealth.Num()==2)
    {
        int32 Health[2]={Rules.ArenaHealth[0],Rules.ArenaHealth[1]}; // 从回执的实际命中序列还原生命。
        const int32 Count=FMath::Min(R.Battle.Num(),FMath::FloorToInt(FMath::Clamp(Time/2.8,0.,1.)*R.Battle.Num())); // 完整播放每个已保存事件。
        for(int32 Index=0;Index<Count;++Index) { const int32 Event=R.Battle[Index],Target=Event>0?1:0; Health[Target]=FMath::Max(0,Health[Target]-(FMath::Abs(Event)-1)); }
        for(int32 Side=0;Side<2;++Side)
        {
            const bool Attack=Count>0&&Time<2.8&&(R.Battle[Count-1]>0?0:1)==Side; const float Lunge=Attack?FMath::Abs(FMath::Sin(float(Time)*35.f))*18.f*(Side==0?1.f:-1.f):0.f; // 卡片的视觉进攻，不改变排列槽。
            ArenaFigures[Side]->SetRenderTranslation(FVector2D(Lunge,0)); ArenaHealthBars[Side]->SetPercent(Rules.ArenaHealth[Side]>0?float(Health[Side])/Rules.ArenaHealth[Side]:0.f);
            ArenaHealthText[Side]->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"),Health[Side],Rules.ArenaHealth[Side])));
        }
        FString EventText=TEXT("机械兽入场"); // 初始时没有命中事件可播。
        if(!Rolling) EventText=R.Winner==0?TEXT("磁轨兽获胜"):TEXT("装甲蛛获胜");
        else if(Count>0) EventText=FMath::Abs(R.Battle[Count-1])==1?TEXT("闪避"):FString::Printf(TEXT("命中 −%d"),FMath::Abs(R.Battle[Count-1])-1);
        RevealValue->SetText(FText::FromString(EventText));
    }
    else if(R.Mode==2||R.Mode==3)
    {
        if(R.bBomb) { if(Reduced) { for(UImage* Piece:ExplosionPieces) if(Piece) Piece->SetRenderOpacity(0.f); } else AnimateExplosion(Time); RevealValue->SetText(FText::FromString(R.Mode==2?TEXT("货箱爆炸"):TEXT("过载 · 能量已消散"))); } // 减少动效关闭爆炸环/碎片，保留明确的真实损失文案。
        else RevealValue->SetText(FText::FromString(R.Mode==2?TEXT("货箱金币已领取"):TEXT("充能收益已领取")));
    }
    else if(R.Mode==6) RevealValue->SetText(FText::FromString(TEXT("射击挑战结算完成")));
}

#if WITH_EDITOR
FDemoCasinoAnimationProbe UDemoCasinoScreen::ReadAnimationForTest() const
{
    DEMO_LOG_CALL();
    FDemoCasinoAnimationProbe Probe; // 值快照由测试调用栈拥有，不保活Widget或暴露任何修改入口。
    if(RevealValue) { Probe.ContentId=RevealValue->GetUniqueID(); Probe.Value=RevealValue->GetText().ToString(); }
    for(const auto& Cell:ReelCells) if(Cell)
    {
        Probe.ReelTranslations.Add(Cell->GetRenderTransform().Translation);
        Probe.ReelCellSizes.Add(Cell->GetCachedGeometry().GetLocalSize());
    }
    for(const auto& Label:ReelLabels) if(Label)
    {
        Probe.ReelLabels.Add(Label->GetText().ToString());
        Probe.ReelLabelSizes.Add(Label->GetCachedGeometry().GetLocalSize());
        Probe.ReelLabelWrap.Add(Label->GetAutoWrapText()||Label->GetWrapTextAt()>0.f);
    }
    for(const auto& Face:DiceFaces) if(Face) Probe.DiceValues.Add(Face->GetText().ToString());
    for(const auto& Piece:ExplosionPieces) if(Piece)
    {
        Probe.ExplosionScales.Add(Piece->GetRenderTransform().Scale);
        Probe.ExplosionTranslations.Add(Piece->GetRenderTransform().Translation);
        Probe.ExplosionOpacities.Add(Piece->GetRenderOpacity());
    }
    for(const auto& Health:ArenaHealthBars) if(Health) Probe.ArenaHealth.Add(Health->GetPercent());
    return Probe;
}
#endif

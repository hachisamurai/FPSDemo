// 职责：确认/金额模态内容及输入生命周期。使用：读取MenuFlow冻结参数，确认调用原业务；失焦不消费金额。
#include "UI/Screens/DemoConfirmScreen.h"
#include "UI/DemoUIStyle.h" // R04金额字体与本项目颜色token，不依赖本机系统字体。
#include "UI/R02/DemoUITheme.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Engine/Texture2D.h" // 详情只借已加载的Config.Icon软资源，TSoftObjectPtr取值需要完整纹理类型。
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h" // 金额框使用中性灰底与清晰前景，避免默认浅底浅字。
#include "Debug/DemoLog.h"
UDemoConfirmScreen::UDemoConfirmScreen(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{ DEMO_LOG_CALL(); bIsModal=true; PanelWidth=720.f; }
void UDemoConfirmScreen::SetKind(int32 InKind) { DEMO_LOG_CALL(); Kind=InKind; }
bool UDemoConfirmScreen::UsesR02Presentation() const
{
    DEMO_LOG_TICK();
    if(Kind==3) return true; // 弹药解锁保留原确认；原Kind4武器详情已删除，不复用其编号。
    const UDemoMenuFlowComponent* Menu=Flow(); // 只看正式来源页意图，不缓存另一份菜单状态。
    if(!Menu) return false;
    const EDemoMenuPage Page=Menu->GetMenuPage();
    if(Kind==1&&DemoCasino::ModeForChoice(Menu->GetInspectedGamble())<0) return true; // 2026-11-20终端冒险强化/遗物确认随来源页统一R02；独立金币玩法下注确认保持原样。
    return (Kind==5&&(Page==EDemoMenuPage::Saves||Page==EDemoMenuPage::CreateSave))
        ||(Kind==6&&(Page==EDemoMenuPage::Pause||Page==EDemoMenuPage::ReturnHub));
}
FString UDemoConfirmScreen::RefreshKey() const
{
    DEMO_LOG_TICK(); if(!Flow()||!Controller()) return TEXT("missing");
    if(Kind==7) return FString::Printf(TEXT("display/%d"),FMath::CeilToInt(Flow()->GetDisplayConfirmSeconds())); // 每秒倒计时，保留同名按钮焦点。
    if(Kind==2) return FString::Printf(TEXT("bet/%d"),Kind); // 编辑过程不受余额刷新影响，保留光标与错误文本。
    const auto& View=Controller()->GetMenuSnapshot();
    return FString::Printf(TEXT("%d/%d/%d/%d/%d/%d/%d/%d/%s"),Kind,int32(Flow()->GetMenuPage()),UsesR02Presentation(),Flow()->GetInspectedGamble(),Flow()->GetPendingAmmoPrice(),Flow()->GetInspectedWeapon(),View.Progress.Coins,Flow()->GetCasinoDraft().Stake,*Flow()->GetMenuMessage()); // 同Kind复用也按真实来源页恢复主题。
}
void UDemoConfirmScreen::BuildBody()
{
    DEMO_LOG_CALL(); BetEntry=nullptr; ErrorText=nullptr; ConfigureLayout(800.f,660.f);
    auto* F=Flow(); const auto& View=Controller()->GetMenuSnapshot(); // 当前拥有者快照，绝不借其他玩家经济。
    SetHeading(TEXT("确认操作"),TEXT("返回：Esc / Tab"));
    AddBack(TEXT("UI.Back"),TEXT("← 返回上一层"),!F->IsGambleWaiting()); // 与Esc共用模态撤销路径，不触发确认。
    if(Kind==2)
    {
        SetHeading(TEXT("输入下注金额"),TEXT("输入正整数；确定仅修改草稿，正式下注仍需确认"));
        BetEntry=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("BetEntry"));
        AddText(PageBody,TEXT("BET AMOUNT / 金币"),15,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(15,TEXT("Medium")));
        BetEntry->WidgetStyle.SetFont(DemoUI::NumberFont(36)); // R04纯ASCII金额读数，UE5.4在Slate构建前配置；不影响输入/解析/冻结报价。
        const FSlateRoundedBoxBrush EntryBrush(FLinearColor(.02f,.02f,.02f,.88f),3.f); // 原生金额框由当前Widget持有样式副本，DPI前3px圆角。
        BetEntry->WidgetStyle.SetBackgroundImageNormal(EntryBrush).SetBackgroundImageHovered(EntryBrush).SetBackgroundImageFocused(EntryBrush);
        BetEntry->WidgetStyle.SetForegroundColor(FSlateColor(FLinearColor(.94f,.94f,.94f))).SetFocusedForegroundColor(FSlateColor(FLinearColor::White));
        BetEntry->SetText(FText::AsNumber(F->GetCasinoDraft().Stake,&FNumberFormattingOptions::DefaultNoGrouping()));
        BetEntry->SetSelectAllTextWhenFocused(true); BetEntry->SetClearKeyboardFocusOnCommit(false); BetEntry->SetRevertTextOnEscape(false);
        BetEntry->OnTextCommitted.AddDynamic(this,&UDemoConfirmScreen::OnBetCommitted); PageBody->AddChild(BetEntry);
        AddText(PageBody,FString::Printf(TEXT("可用余额  %d 金币"),View.Progress.Coins),17,DemoUI::Muted);
        AddText(PageBody,TEXT("支持正整数输入；超过余额仍可编辑，正式下注由服务器复核。"),16,DemoUI::Muted);
        ErrorText=AddText(PageBody,TEXT(""),18,DemoUI::Danger);
        auto* Row=PageFooter?PageFooter.Get():AddRow(PageBody); AddDialogAction(Row,TEXT("Casino.BetAccept"),TEXT("确定金额")); AddDialogAction(Row,TEXT("Casino.BetCancel"),TEXT("取消")); return;
    }
    if(Kind==1)
    {
        const int32 Choice=F->GetInspectedGamble();
        if(DemoCasino::ModeForChoice(Choice)>=0)
        {
            const auto& Draft=F->GetCasinoDraft(); const auto Quote=F->GetCasinoQuote(); // 与冻结时相同的草稿，弹窗期禁止底层修改。
            SetHeading(TEXT("确认下注"),DemoCasino::ModeName(Draft.Mode));
            AddText(PageBody,TEXT("本次投入 / 金币"),16,DemoUI::Muted);
            AddText(PageBody,FString::FromInt(Draft.Stake),48,DemoUI::Gold)->SetFont(DemoUI::NumberFont(48));
            if(Draft.Mode==0) AddText(PageBody,Draft.Selection==0?FString::Printf(TEXT("选择 %02d～%02d"),Draft.Low,Draft.High):Draft.Selection==1?TEXT("选择奇数"):TEXT("选择偶数"));
            if(Draft.Mode==0||Draft.Mode==5) AddText(PageBody,FString::Printf(TEXT("命中概率 %.2f%% · 总到账 %d · 净收益 %+d"),Quote.Chance*100,Quote.Payout,Quote.Payout-Draft.Stake));
            else AddText(PageBody,FString::Printf(TEXT("最高总到账 %d 金币，按实际结果结算"),Quote.MaximumPayout));
            AddText(PageBody,TEXT("倍率包含本金。失败损失本次投入；保存失败撤销本次操作。"));
        }
        else if(View.GambleOffers.IsValidIndex(Choice))
        {
            const auto& Offer=View.GambleOffers[Choice]; SetHeading(Choice<4?TEXT("确认投入"):TEXT("确认遗物"),Offer.Title);
            // 冒险强化只保留价格/概率一行与失败后果一行；遗物保留完整效果说明。真实拒绝仍由服务器回执展示。
            AddText(PageBody,Choice<4?FString::Printf(TEXT("%d 银币 · 成功率 %.0f%%"),Offer.Cost,Offer.Chance*100):FString::Printf(TEXT("%d 金币"),Offer.Cost),30);
            if(Choice>=7) AddText(PageBody,Offer.Detail);
            AddText(PageBody,Choice>=7?TEXT("成功后保存；保存失败撤销。"):TEXT("失败仅扣本次费用。"),17,DemoUI::Muted);
        }
        auto* Row=PageFooter?PageFooter.Get():AddRow(PageBody); AddDialogAction(Row,TEXT("GambleYes"),TEXT("确认"),!F->IsGambleWaiting()); AddDialogAction(Row,TEXT("GambleNo"),TEXT("取消"),!F->IsGambleWaiting()); return;
    }
    if(Kind==3)
    {
        SetHeading(TEXT("确认解锁弹药"));
        auto* Pawn=Cast<ADemoCharacter>(Controller()->GetPawn()); const auto* Ammo=Pawn?Pawn->FindComponentByClass<UDemoAmmoComponent>():nullptr;
        const auto* Catalog=Ammo?Ammo->GetCatalog():nullptr; const int32 Index=F->GetInspectedAmmo(); // 已加载的图标/说明，不在UI读磁盘。
        if(Catalog&&Catalog->Entries.IsValidIndex(Index)) { AddIcon(PageBody,Catalog->Entries[Index].Icon,72); AddText(PageBody,Catalog->Entries[Index].Name.ToString(),26); }
        AddText(PageBody,FString::FromInt(F->GetPendingAmmoPrice()),44,R02Theme()?R02Theme()->Ink:DemoUI::Ink);
        AddText(PageBody,FString::Printf(TEXT("本次花费 / 金币　·　当前余额 %d"),View.Progress.Coins),18,DemoUI::Muted);
        AddText(PageBody,TEXT("永久解锁后免费装配；购买不会自动替换当前弹药。"));
        auto* Row=PageFooter?PageFooter.Get():AddRow(PageBody); AddDialogAction(Row,TEXT("AmmoBuyYes"),TEXT("确认解锁")); AddDialogAction(Row,TEXT("AmmoBuyNo"),TEXT("取消")); return;
    }
    auto* Row=PageFooter?PageFooter.Get():AddRow(PageBody); // 旧全局确认沿用原白名单和Overlay守卫。
    if(Kind==5)
    {
        SetHeading(TEXT("是否创建新档案？"),TEXT("新行动从安全区开始"));
        AddText(PageBody,TEXT("建立一份独立的行动记录。"),26,DemoUI::Ink);
        AddText(PageBody,TEXT("默认携带手枪与近战。\n在安全区整备装备，选择核心与难度后出发。"),18,DemoUI::Muted);
        AddText(PageBody,TEXT("每个存档栏独立记录进度。金币、永久强化和武器解锁会保留；银币与本局成长在本轮结束时清空。"),16,DemoUI::Muted);
        if(F->IsChoosingHostSave()) AddText(PageBody,TEXT("房主档案保存固定队伍检查点；各玩家经济与解锁独立。"),16,DemoUI::Muted); // 真实房主来源保留，不假造队伍或绕过后续创建守卫。
        AddDialogAction(Row,TEXT("CreateYes"),TEXT("创建档案")); AddDialogAction(Row,TEXT("CreateNo"),TEXT("取消"));
    }
    if(Kind==6)
    {
        SetHeading(TEXT("返回安全区？"),TEXT("结束当前这一轮行动"));
        AddText(PageBody,TEXT("将清除"),17,UsesR02Presentation()&&R02Theme()?R02Theme()->Ember:DemoUI::Gold);
        AddText(PageBody,TEXT("银币余额、银币成长、免费强化及本轮核心分支。"),20,DemoUI::Ink); AddGap(PageBody,12);
        AddText(PageBody,TEXT("继续保留"),17,DemoUI::Muted);
        AddText(PageBody,TEXT("金币余额、金币永久成长、已解锁武器与弹药。"),20,DemoUI::Ink);
        AddDialogAction(Row,TEXT("HubYes"),TEXT("确认返回")); AddDialogAction(Row,TEXT("HubNo"),TEXT("取消，返回菜单")); // 默认焦点仍为HubNo，R02不把安全取消换成确认。
    }
    if(Kind==7) { SetHeading(TEXT("保留当前显示设置？"),FString::Printf(TEXT("%d秒后自动恢复旧显示模式"),FMath::CeilToInt(F->GetDisplayConfirmSeconds()))); AddText(PageBody,FString::FromInt(FMath::CeilToInt(F->GetDisplayConfirmSeconds())),64,DemoUI::Gold)->SetFont(DemoUI::NumberFont(64)); AddText(PageBody,TEXT("确认画面可见后保留设置。按 Esc、返回或等待倒计时结束，均恢复原显示模式。"),18,DemoUI::Muted); AddDialogAction(Row,TEXT("DisplayYes"),TEXT("保留")); AddDialogAction(Row,TEXT("DisplayNo"),TEXT("恢复并返回设置")); }
    if(Kind==8) { SetHeading(TEXT("无尽模式已解锁"),TEXT("可在安全区出发终端选择")); AddDialogAction(Row,TEXT("OverlayBack"),TEXT("知道了")); }
}
void UDemoConfirmScreen::AcceptBet()
{
    DEMO_LOG_CALL(); if(Kind!=2||!BetEntry||!Flow()) return;
    FString Error; // 原Flow负责精确解析、最低金额和有效页面验证；UI不推算钱款。
    if(!Flow()->CommitCasinoBetText(BetEntry->GetText().ToString(),Error)) { if(ErrorText) ErrorText->SetText(FText::FromString(Error)); BetEntry->SetUserFocus(GetOwningPlayer()); }
}
void UDemoConfirmScreen::OnBetCommitted(const FText& Text,ETextCommit::Type Method)
{ DEMO_LOG_CALL(); if(Method==ETextCommit::OnEnter) AcceptBet(); }
UWidget* UDemoConfirmScreen::NativeGetDesiredFocusTarget() const
{
    DEMO_LOG_TICK(); if(Kind==2&&BetEntry) return BetEntry.Get();
    if(UWidget* Previous=GetRestoredFocusTarget()) return Previous; // 倒计时/回执重建保留已移动的焦点；只在首次构建给取消安全默认。
    const FName SafeAction=Kind==1?TEXT("GambleNo"):Kind==3?TEXT("AmmoBuyNo"):Kind==5?TEXT("CreateNo"):Kind==6?TEXT("HubNo"):Kind==7?TEXT("DisplayNo"):TEXT("OverlayBack"); // 默认焦点优先取消/返回；原武器详情Kind4已退役。
    if(auto* Safe=FindAction(SafeAction)) if(Safe->GetIsEnabled()) return Safe;
    return Super::NativeGetDesiredFocusTarget();
}
UDemoMenuButton* UDemoConfirmScreen::AddDialogAction(UPanelWidget* Parent,FName Action,const FString& Caption,bool Enabled)
{
    DEMO_LOG_CALL();
    auto* Button=AddCompactAction(Parent,Action,Caption,Enabled,180.f); // 操作区固定，正文滚动时确认/取消不消失。
    const bool Primary=Action==TEXT("Casino.BetAccept")||Action==TEXT("GambleYes")||Action==TEXT("AmmoBuyYes")||Action==TEXT("WeaponEquip")||Action==TEXT("CreateYes")||Action==TEXT("HubYes")||Action==TEXT("DisplayYes"); // 仅视觉主次，不改变业务许可。
    Button->SetVisualRole(Primary?EDemoButtonVisualRole::Primary:EDemoButtonVisualRole::Standard); return Button;
}

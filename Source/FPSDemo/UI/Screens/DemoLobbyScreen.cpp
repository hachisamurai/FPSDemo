// 职责：大厅根页及云状态展示。使用：只投影当前GI服务；业务授权与旅行仍由MenuFlow执行。
#include "UI/Screens/DemoLobbyScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIStyle.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoCloudSync.h"
#include "Save/DemoRunSave.h" // 仅当前GI已初始化缓存；不调用RefreshSlots或任何I/O。
#include "Game/DemoGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h" // 本地GameState查询需完整World类型，不依赖Unity传递包含。
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Debug/DemoLog.h"

UDemoLobbyScreen::UDemoLobbyScreen(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{ DEMO_LOG_CALL(); WorldDimColor=FLinearColor(0,0,0,.15f); }

FString UDemoLobbyScreen::RefreshKey() const
{
    DEMO_LOG_TICK();
    const auto* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 本帧借用当前World诊断。
    const auto* Cloud=GetGameInstance()?GetGameInstance()->GetSubsystem<UDemoCloudSync>():nullptr; // 只取已存在服务；Editor不创建云。
    const auto* PC=Controller(); // 当前本机拥有者，旅行时允许暂缺。
    FString Key=(State?State->FailureReason:FString())+TEXT("|")+(PC?PC->GetConnectionStatus():FString())+TEXT("|")+(Cloud?Cloud->GetStatus():FString())+TEXT("|")+(Cloud&&Cloud->HasConflict()?TEXT("conflict"):TEXT("ready")); // 只在内存比较，不记录私有文件内容。
    Key+=TEXT("|flow=")+(Flow()?Flow()->GetMenuMessage():FString()); // 原Flow拒绝/状态变化也刷新大厅，不在UI构造新的业务结果。
    if(const auto* Saves=GetGameInstance()?GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr)
    {
        Key+=FString::Printf(TEXT("|serial=%llu|active=%d|%s"),static_cast<unsigned long long>(Saves->GetChangeSerial()),Saves->GetActiveSlot(),*Saves->GetStatus()); // 成功写入/导入由服务维护序列，UI不推进。
        for(int32 Index=0;Index<3;++Index)
        {
            const auto* Data=Saves->GetSlot(Index); // 只读GI缓存，不因重建读取磁盘。
            Key+=FString::Printf(TEXT("|slot=%d:%d:%d"),Index,Saves->SlotExists(Index),Data!=nullptr);
            if(Data) Key+=FString::Printf(TEXT(":%s:%d:%d:%d:%d"),*Data->CreatedLocal.ToString(),Data->CompletedLevel,int32(Data->Phase),int32(Data->Difficulty),Data->bEndless); // 仅画面实际使用字段，不导出账号/队伍私有正文。
        }
    }
    int32 Width=0,Height=0; if(PC) PC->GetViewportSize(Width,Height); // 本次真实物理视口；窗口变更会重排而不读盘。
    Key+=FString::Printf(TEXT("|viewport=%dx%d:%.3f"),Width,Height,UWidgetLayoutLibrary::GetViewportScale(this));
    return Key;
}
void UDemoLobbyScreen::BuildBody()
{
    DEMO_LOG_CALL();
    if(UsesR02Presentation()) { BuildR02Lobby(); return; } // 授权C02大厅改版；历史布局仅保兼容，不改变原动作/云流程。
    ConfigureLayout(740.f,790.f,true); // 左侧阅读区由ScaleBox在小屏缩小，菜单仍为实际CommonUI控件。
    SetHeading(TEXT("BREACH"),TEXT("每一次突破，都有新的打法。"));
    if(auto* Backdrop=Cast<UBorder>(WidgetTree->RootWidget)) { Backdrop->SetPadding(FMargin(100,110,60,20)); Backdrop->SetBrushColor(FLinearColor(0.f,0.f,0.f,.12f)); } // 背景由HUD独立全屏绘制，菜单锚在左中偏上。
    if(auto* Surface=Cast<UBorder>(WidgetTree->FindWidget(TEXT("PanelSurface")))) { Surface->SetBrushColor(FLinearColor::Transparent); Surface->SetPadding(FMargin(24,12)); }
    if(auto* Header=Cast<USizeBox>(WidgetTree->FindWidget(TEXT("PageHeaderBounds")))) Header->SetHeightOverride(270.f);
    if(auto* Wallet=WidgetTree->FindWidget(TEXT("PageWallet"))) Wallet->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* Eyebrow=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("PageSectionLabel")))) { FSlateFontInfo EyebrowFont=DemoUI::DisplayFont(14,TEXT("Medium")); EyebrowFont.LetterSpacing=180; Eyebrow->SetText(FText::FromString(TEXT("TACTICAL CO-OP / ROGUELITE"))); Eyebrow->SetFont(EyebrowFont); } // 原生眉题FontInfo由当前控件持有。
    if(PageHeading) { FSlateFontInfo TitleFont=DemoUI::DisplayFont(116,TEXT("SemiBold")); TitleFont.LetterSpacing=55; PageHeading->SetFont(TitleFont); } // Barlow真字重/字距，无水平拉伸。
    if(PageSubtitle) { PageSubtitle->SetFont(DemoUI::TextFont(18)); PageSubtitle->SetColorAndOpacity(DemoUI::Muted); }
    const TPair<FName,const TCHAR*> MenuActions[]={{TEXT("StartGame"),TEXT("01    开始行动                       ↗")},{TEXT("Multiplayer"),TEXT("02    多人游戏                  →")},{TEXT("LobbySettings"),TEXT("03    设置                         →")},{TEXT("QuitGame"),TEXT("04    退出游戏                  →")}}; // 原白名单仅变更排版标签，不改变保存/退出流程。
    for(int32 Index=0;Index<UE_ARRAY_COUNT(MenuActions);++Index)
    {
        UDemoMenuButton* Button=AddAction(PageBody,MenuActions[Index].Key,MenuActions[Index].Value,true,Index==0); // 页面拥有真实动作按钮。
        Button->SetVisualRole(EDemoButtonVisualRole::Menu); Button->SetMinDimensions(Index==0?440.f:320.f,58.f);
        if(auto* ButtonSlot=Cast<UVerticalBoxSlot>(Button->Slot)) { ButtonSlot->SetHorizontalAlignment(HAlign_Left); ButtonSlot->SetPadding(FMargin(0,Index==0?0.f:4.f,0,Index==0?14.f:0.f)); }
    }
    AddGap(PageBody,26.f);
    AddText(PageBody,TEXT("从安全区整备，在十关挑战中构筑你的流派。"),15,DemoUI::Muted);
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 错误完整换行，不能仅依赖日志定位。
    if(State&&!State->FailureReason.IsEmpty()) AddText(PageBody,State->FailureReason,17,DemoUI::Danger);
    if(!Controller()->GetConnectionStatus().IsEmpty()) AddText(PageBody,Controller()->GetConnectionStatus(),17);
    if(const auto* Cloud=GetGameInstance()->GetSubsystem<UDemoCloudSync>())
    {
        AddText(PageBody,Cloud->GetStatus(),16);
        auto* Actions=AddRow(PageBody); // 冲突决策仍由原归档/同步流程处理。
        if(Cloud->HasConflict()) { AddAction(Actions,TEXT("CloudLocal"),TEXT("保留本机检查点")); AddAction(Actions,TEXT("CloudRemote"),TEXT("使用云端检查点")); }
        else AddAction(Actions,TEXT("CloudRetry"),TEXT("重试同步"));
    }
}

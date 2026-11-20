// 职责：R04图像/操作设置与本机UI/受击舒适度偏好；设置项采用固定控件列和独立留白，避免文本长度改变按钮对齐。
// 使用/依赖：原Flow持有0..4草稿/15秒显示回退，表现开关由本机设备服务即时保存；UMG负责等宽数值框，不缓存设备值。
#include "UI/Screens/DemoSettingsScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIStyle.h"
#include "UI/DemoUIPresentation.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/Common/DemoMenuButton.h"
#include "Settings/DemoGameUserSettings.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Debug/DemoLog.h"
namespace
{
    constexpr float StepperButtonSize=52.f; // DPI缩放前的方形加减按钮边长；五项设置共用。
    constexpr float SettingValueWidth=228.f; // 固定数值区宽度，包含最长的VSync说明，不随当前选项伸缩。
    constexpr float StepperInnerGap=14.f; // 加减按钮与数值框之间的净空；不依赖通用按钮默认Padding叠加。
    constexpr float SettingRowGap=16.f; // 各设置行之间的净空；数值框底边不与下一行相贴。
}
bool UDemoSettingsScreen::UsesR02Presentation() const
{
    DEMO_LOG_TICK();
    return true;
}
FString UDemoSettingsScreen::RefreshKey() const
{
    DEMO_LOG_TICK(); FString Key=Flow()->GetMenuMessage(); // 模型键，无设备写入。
    for(int32 Index=0;Index<5;++Index) Key+=TEXT("|")+Flow()->GetSettingText(Index);
    Key+=FString::Printf(TEXT("|presentation=%d:%d"),DemoUI::ReducedMotion(),DemoUI::UISoundEnabled()); // 仅本机已保存偏好，不混入战役/云进度。
    if(const auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())) // 借用引擎本机设备对象，页面不缓存或拥有。
        Key+=FString::Printf(TEXT("|damage=%.3f:%.3f:%d:%d"),Settings->GetDamageShakeScale(),Settings->GetDamageFlashScale(),Settings->IsHurtVoiceEnabled(),Settings->IsLowHealthAudioEnabled());
    return Key;
}
void UDemoSettingsScreen::BuildBody()
{
    DEMO_LOG_CALL(); SetHeading(TEXT("系统设置"),TEXT("图像与操作调整 · 应用后生效，界面表现偏好即时保存"));
    ConfigureLayout(1600.f,835.f);
    AddBack(TEXT("OverlayBack"),TEXT("← 返回上一层"));
    UHorizontalBox* Columns=AddRow(PageBody); UVerticalBox* Display=AddCard(Columns); UVerticalBox* Controls=AddCard(Columns); // 图像与操作按任务分栏，避免七行设置挤成长表。
    // 只重设当前两个卡片槽：中间净空32设计单位，左右仍各保留8；不改全项目的AddCard间距。
    if(auto* DisplaySlot=Cast<UHorizontalBoxSlot>(Display->GetParent()->Slot)) DisplaySlot->SetPadding(FMargin(8,8,16,8));
    if(auto* ControlsSlot=Cast<UHorizontalBoxSlot>(Controls->GetParent()->Slot)) ControlsSlot->SetPadding(FMargin(16,8,8,8));
    const UDemoUITheme* Theme=R02Theme()?R02Theme():GetDefault<UDemoUITheme>();
    AddText(Display,TEXT("DISPLAY / GRAPHICS"),14,Theme->Muted);
    AddText(Display,TEXT("图像与显示"),26,Theme->Ink)->SetFont(Theme->GetSerifFont(26,TEXT("Regular"),true));
    AddR02Rule(Display,10.f);
    AddSettingRow(Display,0,TEXT("游戏分辨率")); AddSettingRow(Display,1,TEXT("画质")); AddSettingRow(Display,3,TEXT("显示模式")); AddSettingRow(Display,4,TEXT("垂直同步")); // 原编号不可随视觉重排而改变。
    AddText(Display,TEXT("分辨率或窗口模式改变后，需在15秒内确认。超时或返回会恢复旧显示模式。"),16,Theme->Muted);
    AddText(Controls,TEXT("CONTROLS / INTERFACE"),14,Theme->Muted);
    AddText(Controls,TEXT("操作与界面"),26,Theme->Ink)->SetFont(Theme->GetSerifFont(26,TEXT("Regular"),true));
    AddR02Rule(Controls,10.f);
    AddSettingRow(Controls,2,TEXT("鼠标移动速度"));
    AddPresentationRow(Controls,TEXT("UI.ToggleReducedMotion"),TEXT("减少界面动效"),DemoUI::ReducedMotion(),TEXT("减弱入场位移与装饰动效，保留操作和真实结果。"));
    AddPresentationRow(Controls,TEXT("UI.ToggleSound"),TEXT("界面交互音效"),DemoUI::UISoundEnabled(),TEXT("控制按钮/焦点反馈声音，不改变枪械或世界音效。"));
    AddText(Controls,TEXT("上述两项界面偏好即时保存到本机设备设置；图像与鼠标设置仍需点击应用。"),16,Theme->Muted);
    UVerticalBox* Comfort=AddCard(PageBody); // 独立全宽卡片由既有PageScroll承载，不挤压原五项设置与显示确认。
    AddText(Comfort,TEXT("DAMAGE FEEDBACK / COMFORT"),14,Theme->Muted);
    AddText(Comfort,TEXT("受击反馈与舒适度"),26,Theme->Ink)->SetFont(Theme->GetSerifFont(26,TEXT("Regular"),true));
    AddR02Rule(Comfort,10.f);
    const auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 本次构建只读，不改变草稿或服务器状态。
    AddFeedbackScaleRow(Comfort,TEXT("UI.DamageShakeMinus"),TEXT("UI.DamageShakePlus"),TEXT("受击镜头倾斜强度"),Settings?Settings->GetDamageShakeScale():1.f);
    AddFeedbackScaleRow(Comfort,TEXT("UI.DamageFlashMinus"),TEXT("UI.DamageFlashPlus"),TEXT("受击边缘闪光强度"),Settings?Settings->GetDamageFlashScale():1.f);
    AddPresentationRow(Comfort,TEXT("UI.ToggleHurtVoice"),TEXT("本人受伤人声"),!Settings||Settings->IsHurtVoiceEnabled(),TEXT("控制本人受伤短人声，保留攻击接触音与队伍语音。"));
    AddPresentationRow(Comfort,TEXT("UI.ToggleLowHealthAudio"),TEXT("低血呼吸与警示音"),!Settings||Settings->IsLowHealthAudioEnabled(),TEXT("仅控制本人低血声音，低血视觉警示与生命信息仍显示。"));
    AddText(Comfort,TEXT("上述四项即时保存到本机；强度0%关闭该效果。方向提示、真实生命条与原界面动效设置独立。"),17,DemoUI::Muted);
    AddFooterAction(TEXT("SettingsApply"),TEXT("应用设置"),true,220.f);
    AddText(PageBody,TEXT("返回会放弃未应用的图像/操作草稿；已即时保存的界面与受击偏好保留。"),17,DemoUI::Muted);
    if(!Flow()->GetMenuMessage().IsEmpty()) AddText(PageBody,Flow()->GetMenuMessage(),17,DemoUI::Gold);
}
void UDemoSettingsScreen::AddSettingRow(UPanelWidget* Parent,int32 Index,const FString& Name)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Row=AddRow(Parent); // 当前树拥有本行；仅标签伸展，右侧步进器总宽固定360设计单位。
    if(auto* RowSlot=Cast<UVerticalBoxSlot>(Row->Slot)) RowSlot->SetPadding(FMargin(4,0,4,SettingRowGap));
    UTextBlock* Label=AddText(Row,Name,18,DemoUI::Ink); // 标签不参与数值列宽分配，五项使用相同起点。
    if(auto* LabelSlot=Cast<UHorizontalBoxSlot>(Label->Slot)) LabelSlot->SetPadding(FMargin(0,0,20,0));
    UHorizontalBox* Stepper=AddRow(Row); // 自动尺寸由两个52按钮、228数值及两段14净空组成，右边缘固定。
    if(auto* StepperSlot=Cast<UHorizontalBoxSlot>(Stepper->Slot)) StepperSlot->SetVerticalAlignment(VAlign_Center);
    UDemoMenuButton* Minus=AddCompactAction(Stepper,FName(*FString::Printf(TEXT("Setting%dMinus"),Index)),TEXT("−"),true,StepperButtonSize); // 仍使用原ActionID/注册与CommonUI焦点。
    Minus->SetMinDimensions(StepperButtonSize,StepperButtonSize);
    if(auto* MinusSlot=Cast<UHorizontalBoxSlot>(Minus->Slot)) MinusSlot->SetPadding(FMargin(0,0,StepperInnerGap,0));
    USizeBox* ValueBounds=WidgetTree->ConstructWidget<USizeBox>(); // 页面拥有的固定数值格；字体和文本原样保留。
    ValueBounds->SetWidthOverride(SettingValueWidth); ValueBounds->SetHeightOverride(StepperButtonSize); Stepper->AddChildToHorizontalBox(ValueBounds);
    UBorder* ValueSurface=WidgetTree->ConstructWidget<UBorder>(); // R02去底板：透明衬底与细衬线数值，保持不可命中。
    const UDemoUITheme* Theme=R02Theme()?R02Theme():GetDefault<UDemoUITheme>();
    ValueSurface->SetPadding(FMargin(10,0)); ValueSurface->SetBrushColor(FLinearColor::Transparent); ValueSurface->SetHorizontalAlignment(HAlign_Center); ValueSurface->SetVerticalAlignment(VAlign_Center);
    ValueSurface->SetVisibility(ESlateVisibility::HitTestInvisible); ValueBounds->SetContent(ValueSurface);
    UTextBlock* Value=AddText(ValueSurface,Flow()->GetSettingText(Index),18,Theme->Ink); // 每次重建读取当前真实草稿，不保留另一份值。
    Value->SetFont(Theme->GetSerifFont(18,TEXT("Regular"),false));
    Value->SetAutoWrapText(false); Value->SetJustification(ETextJustify::Center);
    UDemoMenuButton* Plus=AddCompactAction(Stepper,FName(*FString::Printf(TEXT("Setting%dPlus"),Index)),TEXT("+"),true,StepperButtonSize);
    Plus->SetMinDimensions(StepperButtonSize,StepperButtonSize);
    if(auto* PlusSlot=Cast<UHorizontalBoxSlot>(Plus->Slot)) PlusSlot->SetPadding(FMargin(StepperInnerGap,0,0,0));
}
void UDemoSettingsScreen::AddPresentationRow(UPanelWidget* Parent,FName Action,const FString& Name,bool EnabledValue,const FString& Detail)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Row=AddRow(Parent); // 根UI只接受Settings当前顶层动作，按钮不自己保存或写战役。
    if(auto* RowSlot=Cast<UVerticalBoxSlot>(Row->Slot)) RowSlot->SetPadding(FMargin(4,0,4,8));
    UTextBlock* Label=AddText(Row,Name,18,DemoUI::Ink); // 与数值行标签同起点，开关右边缘与加号列相同。
    if(auto* LabelSlot=Cast<UHorizontalBoxSlot>(Label->Slot)) LabelSlot->SetPadding(FMargin(0,0,20,0));
    UDemoMenuButton* Toggle=AddCompactAction(Row,Action,EnabledValue?TEXT("已开启  ✓"):TEXT("已关闭"),true,142.f); // 只更改表现尺寸，不改变即时保存行为。
    Toggle->SetMinDimensions(142.f,StepperButtonSize); Toggle->SetVisualRole(EDemoButtonVisualRole::Standard);
    if(auto* ToggleSlot=Cast<UHorizontalBoxSlot>(Toggle->Slot)) ToggleSlot->SetPadding(FMargin(0));
    UTextBlock* Note=AddText(Parent,Detail,16,DemoUI::Muted); Note->SetAutoWrapText(false); Note->SetWrapTextAt(625.f); // 显式宽度避免第一帧自动换行低估高度。
    if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Note->Slot)) LayoutSlot->SetPadding(FMargin(4,0,4,SettingRowGap));
}

void UDemoSettingsScreen::AddFeedbackScaleRow(UPanelWidget* Parent,FName MinusAction,FName PlusAction,const FString& Name,float Value)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Row=AddRow(Parent); // 本次Widget树持有；只有右侧步进器接受意图，页面不保存另一份设置值。
    if(auto* RowSlot=Cast<UVerticalBoxSlot>(Row->Slot)) RowSlot->SetPadding(FMargin(4,0,4,SettingRowGap));
    UTextBlock* Label=AddText(Row,Name,18,DemoUI::Ink); // 标签伸展，保持与其他设置右侧控件相同基线。
    if(auto* LabelSlot=Cast<UHorizontalBoxSlot>(Label->Slot)) LabelSlot->SetPadding(FMargin(0,0,20,0));
    UHorizontalBox* Stepper=AddRow(Row); // 总宽仍为固定360设计单位；0..100%每次10%。
    const bool bAvailable=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())!=nullptr; // 引擎未建立设备对象时不提供无效操作。
    UDemoMenuButton* Minus=AddCompactAction(Stepper,MinusAction,TEXT("−"),bAvailable&&Value>KINDA_SMALL_NUMBER,StepperButtonSize);
    Minus->SetMinDimensions(StepperButtonSize,StepperButtonSize);
    if(auto* MinusSlot=Cast<UHorizontalBoxSlot>(Minus->Slot)) MinusSlot->SetPadding(FMargin(0,0,StepperInnerGap,0));
    USizeBox* ValueBounds=WidgetTree->ConstructWidget<USizeBox>(); // 本次树拥有的固定只读数值框，不注册额外输入。
    ValueBounds->SetWidthOverride(SettingValueWidth); ValueBounds->SetHeightOverride(StepperButtonSize); Stepper->AddChildToHorizontalBox(ValueBounds);
    UBorder* ValueSurface=WidgetTree->ConstructWidget<UBorder>(); // R02去底板，保持百分比居中可读。
    const UDemoUITheme* Theme=R02Theme()?R02Theme():GetDefault<UDemoUITheme>();
    ValueSurface->SetPadding(FMargin(10,0)); ValueSurface->SetBrushColor(FLinearColor::Transparent); ValueSurface->SetHorizontalAlignment(HAlign_Center); ValueSurface->SetVerticalAlignment(VAlign_Center);
    ValueSurface->SetVisibility(ESlateVisibility::HitTestInvisible); ValueBounds->SetContent(ValueSurface);
    UTextBlock* Percentage=AddText(ValueSurface,FString::Printf(TEXT("%d%%"),FMath::RoundToInt(FMath::Clamp(Value,0.f,1.f)*100.f)),18,Theme->Ink); // 值由设备服务读取；浮点误差不泄露到百分比文本。
    Percentage->SetFont(Theme->GetSerifFont(18,TEXT("Regular"),false));
    Percentage->SetAutoWrapText(false); Percentage->SetJustification(ETextJustify::Center);
    UDemoMenuButton* Plus=AddCompactAction(Stepper,PlusAction,TEXT("+"),bAvailable&&Value<1.f-KINDA_SMALL_NUMBER,StepperButtonSize);
    Plus->SetMinDimensions(StepperButtonSize,StepperButtonSize);
    if(auto* PlusSlot=Cast<UHorizontalBoxSlot>(Plus->Slot)) PlusSlot->SetPadding(FMargin(StepperInnerGap,0,0,0));
}

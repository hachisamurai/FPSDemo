// 职责：本机枪墙三行浮签的轻量UMG布局，使用项目中文字体。所有业务数据由拥有者Focus提供，不在Widget查询/扣款/保存。
#include "UI/DemoShopFocusCard.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "UI/DemoUIFont.h"
#include "Debug/DemoLog.h"
TSharedRef<SWidget> UDemoShopFocusCard::RebuildWidget()
{
    DEMO_LOG_CALL();
    if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this);
    Sizing = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); WidgetTree->RootWidget = Sizing;
    UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass()); // 当前WidgetTree持有，无跨界捕获。
    Border->SetBrushColor(FLinearColor(.025f,.035f,.04f,.92f)); Border->SetPadding(FMargin(14,10)); Sizing->AddChild(Border);
    UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass()); Border->AddChild(Stack);
    for (int32 Index = 0; Index < 3; ++Index)
    {
        Lines[Index] = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        Lines[Index]->SetFont(DemoUI::TextFont(Index == 0 ? 16 : 14)); Lines[Index]->SetAutoWrapText(true);
        Lines[Index]->SetColorAndOpacity(Index == 2 ? FLinearColor(.25f,.9f,.75f,1) : FLinearColor(.94f,.94f,.9f,1)); Stack->AddChild(Lines[Index]);
    }
    SetVisibility(ESlateVisibility::HitTestInvisible);
    return Super::RebuildWidget();
}
void UDemoShopFocusCard::SetLines(const FString& First, const FString& Second, const FString& Third, const FVector2D& Size)
{
    DEMO_LOG_TICK();
    if (!Sizing) return;
    Sizing->SetWidthOverride(Size.X); Sizing->SetHeightOverride(Size.Y);
    const FString Texts[3] = {First, Second, Third}; // 本帧只读三行；不持有报价引用到下一帧。
    for (int32 Index = 0; Index < 3; ++Index) if (Lines[Index]) Lines[Index]->SetText(FText::FromString(Texts[Index]));
}

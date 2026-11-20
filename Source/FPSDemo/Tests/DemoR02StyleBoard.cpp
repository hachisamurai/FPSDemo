// 职责：R02 S0原生UMG/Slate样式板，记录真实字体/几何/焦点和两分辨率独立进程PNG。
// 边界：样式数据不是交易/战斗快照；不改正式WBP、UIManager、存档/经济/协议或角色资源。
#include "Tests/DemoR02StyleBoard.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "UI/R02/DemoUITheme.h"
#include "UI/R02/DemoR02EditorialButton.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "CommonInputModeTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Font.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h" // 显式声明真实字体Measure服务，不能依赖其他头的传递包含。
#include "Fonts/FontMeasure.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#endif

bool UDemoR02StyleBoardWidget::InitializeBoard(UDemoUITheme* InTheme)
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    Theme=InTheme;
    return Theme&&Theme->SerifLatinFont&&Theme->SerifChineseFont&&Theme->AtmosphereMaterial;
#else
    return false;
#endif
}
UDemoR02EditorialButton* UDemoR02StyleBoardWidget::GetFocusSample() const
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    return Buttons.IsValidIndex(1)?Buttons[1].Get():nullptr;
#else
    return nullptr;
#endif
}
TSharedRef<SWidget> UDemoR02StyleBoardWidget::RebuildWidget()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if(!WidgetTree) WidgetTree=NewObject<UWidgetTree>(this,TEXT("WidgetTree"));
    if(!BoardCanvas)
    {
        BoardCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=BoardCanvas;
        BackgroundPlate=WidgetTree->ConstructWidget<UBorder>();
        BackgroundPlate->SetPadding(FMargin(0.f)); BackgroundPlate->SetBrushColor(Theme->Base);
        BackgroundPlate->SetVisibility(ESlateVisibility::HitTestInvisible);
        BoardCanvas->AddChildToCanvas(BackgroundPlate); // 纯色，不读取场景/角色截图或颗粒材质，遵用户最新要求。
        const TCHAR* Captions[]={TEXT("01  继续行动"),TEXT("02  选择此流派"),TEXT("03  返回上一层"),TEXT("04  等待资源就绪")}; // 样式示例，无Screen/交易动作所有者。
        for(int32 Index=0;Index<4;++Index)
        {
            auto* Button=CreateWidget<UDemoR02EditorialButton>(this); // Outer为本Board.WidgetTree，CommonUI才能找到真实父Activatable；PlayerContext由本Board继承。
            Button->SetR02Appearance(Theme,Index==1,24,true); Button->SetCaption(Captions[Index]);
            Button->SetIsFocusable(true); Button->SetIsEnabled(Index!=3);
            BoardCanvas->AddChildToCanvas(Button); Buttons.Add(Button);
        }
    }
#endif
    return Super::RebuildWidget();
}
TOptional<FUIInputConfig> UDemoR02StyleBoardWidget::GetDesiredInputConfig() const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    return FUIInputConfig(ECommonInputMode::Menu,EMouseCaptureMode::NoCapture);
#else
    return TOptional<FUIInputConfig>();
#endif
}
void UDemoR02StyleBoardWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::NativeTick(Geometry,DeltaSeconds);
#if WITH_EDITOR
    const FVector2D Size=Geometry.GetLocalSize();
    if(Size.Equals(LastLocalSize,.1f)||!BoardCanvas) return;
    LastLocalSize=Size;
    const float S=FMath::Min(Size.X/1280.f,Size.Y/720.f); // 板工作区缩放；真实DPI仍由Viewport分配，本值不再乘到物理像素线厚。
    if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(BackgroundPlate->Slot)) { CanvasSlot->SetPosition(FVector2D::ZeroVector); CanvasSlot->SetSize(Size); }
    for(int32 Index=0;Index<Buttons.Num();++Index) if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Buttons[Index]->Slot))
    {
        CanvasSlot->SetPosition(FVector2D(62.f,350.f+Index*52.f)*S); CanvasSlot->SetSize(FVector2D(360.f,44.f)*S);
        Buttons[Index]->SetR02Appearance(Theme,Index==1,FMath::RoundToInt(22.f*S),true);
    }
#endif
}
int32 UDemoR02StyleBoardWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    DEMO_LOG_TICK(); const int32 BaseLayer=Super::NativePaint(Args,Geometry,CullRect,Elements,Layer,Style,ParentEnabled);
#if WITH_EDITOR
    if(!Theme) return BaseLayer;
    const FVector2D Size=Geometry.GetLocalSize(); const float S=FMath::Min(Size.X/1280.f,Size.Y/720.f);
    const float EffectiveScale=Geometry.GetAccumulatedLayoutTransform().GetScale(); // 最终Slate布局缩放；厚度量化不假设只有DPI。
    const FSlateBrush* Solid=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
    const int32 PaintLayer=BaseLayer+1;
    // 捕获均为本次NativePaint同步借用的ElementList/Geometry/Theme，无异步或跨帧生命周期。
    const auto Rect=[&](float X,float Y,float W,float H,const FLinearColor& Color)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] R02StyleBoard.Rect"));
        FSlateDrawElement::MakeBox(Elements,PaintLayer,Geometry.ToPaintGeometry(FVector2D(W,H)*S,FSlateLayoutTransform(FVector2D(X,Y)*S)),Solid,ESlateDrawEffect::None,Color);
    };
    // 目标1物理像素；在局部空间反算厚度并将坐标最终量化，字体/按钮不受此逻辑改动。
    const auto Rule=[&](float X,float Y,float W,float Alpha=0.25f)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] R02StyleBoard.Rule"));
        const float PhysicalY=FMath::RoundToFloat(Y*S*EffectiveScale);
        const float LocalY=PhysicalY/FMath::Max(.01f,EffectiveScale);
        FSlateDrawElement::MakeBox(Elements,PaintLayer,Geometry.ToPaintGeometry(FVector2D(W*S,DemoUIR02::PhysicalHairlineThickness(EffectiveScale)),FSlateLayoutTransform(FVector2D(X*S,LocalY))),Solid,ESlateDrawEffect::None,Theme->Ink.CopyWithNewOpacity(Alpha));
    };
    // 文本为真实Slate UFont；字号在1280工作区缩放，测量与绘制使用同一FontInfo，不烘焙进图。
    const auto Text=[&](const FString& Value,float X,float Y,int32 FontSize,bool Serif,bool Chinese,const FLinearColor& Color,FName Weight=TEXT("Regular"))
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] R02StyleBoard.Text"));
        const int32 Points=FMath::Max(1,FMath::RoundToInt(FontSize*S*.75f));
        const FSlateFontInfo Font=Serif?Theme->GetSerifFont(Points,Weight,Chinese):Theme->GetLabelFont(Points,Weight);
        FSlateDrawElement::MakeText(Elements,PaintLayer,Geometry.ToPaintGeometry(FVector2D(1100.f,120.f)*S,FSlateLayoutTransform(FVector2D(X,Y)*S)),Value,Font,ESlateDrawEffect::None,Color);
    };
    // 逐位定宽样本：每格以该实际字库最宽数字测量，单位/分隔符另排；无固定Barlow比例常数。
    const auto Digits=[&](const FString& Value,float X,float Y,int32 FontSize)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] R02StyleBoard.Digits"));
        const FSlateFontInfo Font=Theme->GetSerifFont(FMath::RoundToInt(FontSize*S*.75f));
        const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const float Cell=DemoUIR02::MeasureDigitCellWidth(Font);
        for(int32 Index=0;Index<Value.Len();++Index)
        {
            const FString Glyph=Value.Mid(Index,1); const float Width=Measure->Measure(Glyph,Font).X;
            FSlateDrawElement::MakeText(Elements,PaintLayer,Geometry.ToPaintGeometry(FVector2D(Cell,110.f*S),FSlateLayoutTransform(FVector2D(X*S+Index*Cell+(Cell-Width)*.5f,Y*S))),Glyph,Font,ESlateDrawEffect::None,Theme->Ink);
        }
    };
    // 纯色底板在最底层，这里不叠全屏遮罩，避免把真实按钮和焦点一起压暗。
    Text(TEXT("B R E A C H   /   R 0 2"),62,42,15,false,false,Theme->Ink);
    Text(TEXT("S0  /  真实 UE 样式板"),956,42,15,false,true,Theme->Ink);
    Rule(62,76,1156);
    Text(TEXT("静候下一次突破"),60,96,50,true,true,Theme->Ink);
    Text(TEXT("低饱和。衬线。留白。"),64,165,18,false,true,Theme->Ink);
    Text(TEXT("01 / ACTION & FOCUS"),62,246,13,false,false,Theme->Ink);
    Text(TEXT("动作与焦点"),62,274,28,true,true,Theme->Ink);
    Text(TEXT("余烬红：已选    骨白刻度：真实焦点"),62,313,16,false,true,Theme->Ink);
    Text(TEXT("禁用原因：资源尚未齐备"),62,554,16,false,true,Theme->Ink); // Disabled文字之外保留清楚原因，不能只用低Alpha灰字。
    Text(TEXT("仅示例状态 · 未接入购买或战斗流程"),62,600,16,false,true,Theme->Ink);
    Rule(488,247,330);
    Text(TEXT("02 / TYPE & NUMERALS"),488,214,13,false,false,Theme->Ink);
    Text(DemoUIR02::ToRoman(3),488,262,66,true,false,Theme->Ink);
    Text(TEXT("第三关 · 清除区域敌人"),490,343,20,false,true,Theme->Ink);
    Text(TEXT("正在保存，请等待真实回执。"),490,384,18,false,true,Theme->Ink);
    Text(TEXT("购买不会自动替换当前装配。"),490,416,18,false,true,Theme->Ink);
    Rule(488,463,330);
    Digits(TEXT("100"),488,482,76); Text(TEXT("生命"),650,533,17,false,true,Theme->Ink);
    Digits(TEXT("12"),488,580,54); Text(TEXT("/ 72  备用"),565,608,18,false,true,Theme->Ink);
    Text(TEXT("03 / COLOR & DETAILS"),902,214,13,false,false,Theme->Ink);
    Rule(902,247,316);
    Text(TEXT("Warm black"),902,277,29,true,false,Theme->Ink);
    Text(TEXT("暖炭黑 / 骨白 / 余烬红"),902,327,18,false,true,Theme->Ink);
    Rect(904,375,22,22,Theme->Base); Rect(940,375,22,22,Theme->Ink); Rect(976,375,22,22,Theme->Ember);
    Text(TEXT("1 物理像素 · 发丝分隔线"),902,429,17,false,true,Theme->Ink); Rule(904,467,310,.28f);
    Text(TEXT("纯色底 · 无背景图"),902,505,18,false,true,Theme->Ink);
    Text(TEXT("无闪烁 · 无位移"),902,537,16,false,true,Theme->Ink);
    Text(TEXT("非关键标签 / MUTED"),902,584,18,false,true,Theme->Muted);
    Rule(62,661,1156);
    Text(TEXT("Noto Serif SC  /  EB Garamond    ·    S0 字体与组件候选"),62,682,14,false,true,Theme->Ink);
    Text(TEXT("720p / 1080p  独立原生帧"),989,682,14,false,true,Theme->Ink);
    return PaintLayer;
#else
    return BaseLayer;
#endif
}
bool UDemoR02StyleBoardWidget::ValidateRendered(FString& Reason) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if(!Theme||!Theme->SerifLatinFont||!Theme->SerifChineseFont||Buttons.Num()!=4) { Reason=TEXT("missing real theme/font/button"); return false; }
    const auto* Focus=GetFocusSample(); const FVector2D Size=GetCachedGeometry().GetLocalSize();
    if(Size.X<100.f||Size.Y<100.f||!Focus||!Focus->GetIsEnabled()||!Focus->HasR02FocusHighlight()
        ||!(Focus->HasAnyUserFocus()||Focus->HasUserFocusedDescendants(GetOwningPlayer()))) { Reason=TEXT("no painted geometry or actual CommonUI focus"); return false; }
    if(Buttons[3]->GetIsEnabled()) { Reason=TEXT("disabled sample was interactive"); return false; }
    for(const auto& Button:Buttons)
    {
        const FGeometry& Geo=Button->GetCachedGeometry();
        if(Geo.GetLocalSize().GetMin()<=0.f) { Reason=TEXT("button has no painted geometry"); return false; }
    }
    Reason=TEXT("real UMG geometry/fonts/CommonUI focus; S0 presentation only, no gameplay/network acceptance"); return true;
#else
    Reason=TEXT("Editor-only fixture"); return false;
#endif
}

bool UDemoR02StyleBoardCapture::ShouldCreateSubsystem(UObject* Outer) const
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    const auto* World=Cast<UWorld>(Outer);
    return World&&World->WorldType==EWorldType::Game&&World->GetNetMode()==NM_Standalone&&FParse::Param(FCommandLine::Get(),TEXT("game"))&&FParse::Param(FCommandLine::Get(),TEXT("DemoR02StyleBoard"));
#else
    return false;
#endif
}
bool UDemoR02StyleBoardCapture::DoesSupportWorldType(EWorldType::Type Type) const { DEMO_LOG_CALL(); return WITH_EDITOR&&Type==EWorldType::Game; }
void UDemoR02StyleBoardCapture::OnWorldBeginPlay(UWorld& World)
{
    DEMO_LOG_CALL(); Super::OnWorldBeginPlay(World);
#if WITH_EDITOR
    StartedAt=FPlatformTime::Seconds();
    if(!FParse::Value(FCommandLine::Get(),TEXT("DemoR02Output="),OutputDirectory)||OutputDirectory.IsEmpty()) { Finish(false,TEXT("explicit evidence directory required")); return; }
    OutputDirectory=FPaths::ConvertRelativePathToFull(OutputDirectory);
    IFileManager::Get().MakeDirectory(*OutputDirectory,true);
    PNGPath=OutputDirectory/TEXT("R02-S0-StyleBoard.png");
    TickHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&UDemoR02StyleBoardCapture::OnWorldTick); // 本World GT；不持捕获、不跨线程访问UObject。
#endif
}
void UDemoR02StyleBoardCapture::Deinitialize()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    if(TickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle); TickHandle.Reset();
#endif
    if(Board) Board->RemoveFromParent(); Board=nullptr; Super::Deinitialize();
}
#if WITH_EDITOR
void UDemoR02StyleBoardCapture::OnWorldTick(UWorld* World,ELevelTick TickType,float DeltaSeconds)
{
    DEMO_LOG_TICK(); if(bFinished||World!=GetWorld()) return;
    const double Now=FPlatformTime::Seconds();
    if(Now-StartedAt>90.) { Finish(false,TEXT("bounded S0 capture timed out")); return; }
    if(Now<Deadline||!GEngine||!GEngine->GameViewport||!GEngine->GameViewport->Viewport) return;
    auto* PC=UGameplayStatics::GetPlayerController(World,0); if(!PC||!PC->GetLocalPlayer()) return;
    if(Step==0)
    {
        auto* Theme=LoadObject<UDemoUITheme>(nullptr,TEXT("/Game/UI/RedesignR02/Theme/DA_R02CinemaTheme_S0R02.DA_R02CinemaTheme_S0R02"));
        Board=CreateWidget<UDemoR02StyleBoardWidget>(PC);
        if(!Board||!Board->InitializeBoard(Theme)) { Finish(false,TEXT("S0 assets absent or wrong class")); return; }
        Board->AddToViewport(999); Board->TakeWidget(); Board->ActivateWidget();
        PC->SetInputMode(FInputModeUIOnly()); PC->bShowMouseCursor=false;
        Step=1; Deadline=Now+2.; return;
    }
    if(Step==1)
    {
        if(auto* Button=Board->GetFocusSample()) Button->SetUserFocus(PC);
        Step=2; Deadline=Now+1.; return;
    }
    if(Step==2)
    {
        if((GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())||FAssetCompilingManager::Get().GetNumRemainingAssets()>0)
        { Deadline=Now+1.; return; } // 不隐藏准备提示冒充稳定，待真实Shader/网格派生资源完成后再截。
        FString Reason; if(!Board->ValidateRendered(Reason)) { Finish(false,Reason); return; }
        FScreenshotRequest::RequestScreenshot(PNGPath,true,false); // 正常含Slate PNG，不注册大数组截图委托。
        UE_LOG(LogFPSDemo,Display,TEXT("R02_S0 screenshot requested viewport=%dx%d path=%s"),GEngine->GameViewport->Viewport->GetSizeXY().X,GEngine->GameViewport->Viewport->GetSizeXY().Y,*PNGPath);
        Step=3; Deadline=Now+1.; return;
    }
    if(Step==3&&IFileManager::Get().FileSize(*PNGPath)>1024) Finish(true,TEXT("S0 real UE style board captured; user visual review pending, S1 not started"));
}
void UDemoR02StyleBoardCapture::Finish(bool Success,const FString& Reason)
{
    DEMO_LOG_CALL(); if(bFinished) return; bFinished=true;
    if(TickHandle.IsValid()) FWorldDelegates::OnWorldPostActorTick.Remove(TickHandle); TickHandle.Reset();
    const FIntPoint Size=GEngine&&GEngine->GameViewport&&GEngine->GameViewport->Viewport?GEngine->GameViewport->Viewport->GetSizeXY():FIntPoint::ZeroValue;
    auto Data=MakeShared<FJsonObject>(); Data->SetBoolField(TEXT("success"),Success); Data->SetStringField(TEXT("scope"),TEXT("R02 S0 actual UMG/Slate style board; example data, no gameplay/network/Cook acceptance"));
    Data->SetStringField(TEXT("reason"),Reason); Data->SetNumberField(TEXT("width"),Size.X); Data->SetNumberField(TEXT("height"),Size.Y); Data->SetStringField(TEXT("screenshot"),PNGPath);
    Data->SetBoolField(TEXT("s1_started"),false); Data->SetBoolField(TEXT("plain_background"),true); Data->SetBoolField(TEXT("background_image_enabled"),false);
    FString JSON; const auto Writer=TJsonWriterFactory<>::Create(&JSON); FJsonSerializer::Serialize(Data,Writer);
    if(!OutputDirectory.IsEmpty()) FFileHelper::SaveStringToFile(JSON,*(OutputDirectory/TEXT("report.json")));
    UE_LOG(LogFPSDemo,Display,TEXT("R02_S0 %s: %s"),Success?TEXT("PASS"):TEXT("FAIL"),*Reason);
    FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
}
#endif

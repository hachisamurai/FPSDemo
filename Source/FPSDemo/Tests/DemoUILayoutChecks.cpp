// 职责：读取Slate已绘制几何验证菜单拥挤回归。使用：会话/七玩法专项截图前调用；仅Editor，不能作为多人或真实鼠标验收。
#include "Tests/DemoUILayoutChecks.h"
#if WITH_EDITOR
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Layout/ArrangedChildren.h" // 零缓存离屏文本需从真实父Slate当前分配证明裁切，不能把0几何一律当成合法。
#include "Widgets/SWidget.h" // 只读ArrangeChildren沿用当前Slate槽/字体度量，不在测试复制纵向布局公式。
#include "Debug/DemoLog.h"

namespace
{
    TWeakObjectPtr<UDemoMenuScreen> PendingScreen; // 截图帧末采样对象；弱引用不延长页面寿命。
    FDelegateHandle FrameHook; // 一次性游戏线程帧末委托；消费后立即移除，不跨测试持续回调。
    int32 ReadyResult=0; // 0尚未产生结果，1成功，-1失败；下一测试Tick读取并清零。
    FString ReadyError; // 本次帧末实测失败原因，不包含玩家存档或隐私。
    /** 引擎Slate TimeAndWidgets之后的OnEndFrame采样；静态委托无捕获，UObject只通过弱引用取得。 */
    void CaptureRenderedLayout()
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UILayoutChecks.CaptureRenderedLayout"));
        FCoreDelegates::OnEndFrame.Remove(FrameHook); FrameHook.Reset(); // 从当前广播移除自身，避免以后帧采样旧页。
        ReadyResult=DemoUILayoutChecks::Validate(PendingScreen.Get(),ReadyError)?1:-1;
        PendingScreen.Reset();
    }
    /** 当前渲染帧的绝对边界；不使用控件请求尺寸替代最终排版结果。 */
    FSlateRect Bounds(const UWidget* Widget)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] UILayoutChecks.Bounds")); // 自由函数没有this，等价静态入口埋点。
        const FGeometry& Geometry=Widget->GetCachedGeometry(); // 同步借用，不能跨帧存储Geometry引用。
        const FVector2D Start=Geometry.LocalToAbsolute(FVector2D::ZeroVector),End=Geometry.LocalToAbsolute(Geometry.GetLocalSize()); // 本专项不旋转控件。
        return FSlateRect(Start.X,Start.Y,End.X,End.Y);
    }
    /** Widget/Screen仅本次采样同步借用；Rect与祖先Scroll/root相交，规则与真实按钮裁切相同。 */
    bool ClipToVisibleAncestors(const UWidget* Widget,const UDemoMenuScreen* Screen,FSlateRect& Rect)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UILayoutChecks.ClipToVisibleAncestors"));
        for(const UWidget* Ancestor=Widget;Ancestor;Ancestor=Ancestor->GetParent())
        {
            if(Ancestor->GetVisibility()==ESlateVisibility::Hidden||Ancestor->GetVisibility()==ESlateVisibility::Collapsed) return false;
            if(Cast<UScrollBox>(Ancestor)||Ancestor==Screen->WidgetTree->RootWidget)
            {
                const FSlateRect Clip=Bounds(Ancestor); // 使用已经绘制的祖先视口，不使用请求尺寸。
                Rect.Left=FMath::Max(Rect.Left,Clip.Left); Rect.Top=FMath::Max(Rect.Top,Clip.Top);
                Rect.Right=FMath::Min(Rect.Right,Clip.Right); Rect.Bottom=FMath::Min(Rect.Bottom,Clip.Bottom);
            }
        }
        return Rect.Right-Rect.Left>=1.f&&Rect.Bottom-Rect.Top>=1.f;
    }
    /** Widget为PageAside直系文本；真实父Slate在已绘制几何中重列当前子槽，只证明离屏位置，不代替已绘制文本高度验收。 */
    bool ArrangeCurrentChildBounds(const UWidget* Widget,FSlateRect& Rect)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UILayoutChecks.ArrangeCurrentChildBounds"));
        const UWidget* Parent=Widget?Widget->GetParent():nullptr; // 不缓存父/子Slate或几何到下一帧。
        const TSharedPtr<SWidget> ParentSlate=Parent?Parent->GetCachedWidget():nullptr,ChildSlate=Widget?Widget->GetCachedWidget():nullptr; // 沿用TakeWidget生成的实际对象，禁止新建替身。
        if(!ParentSlate||!ChildSlate||Parent->GetCachedGeometry().GetLocalSize().X<=0.f||Parent->GetCachedGeometry().GetLocalSize().Y<=0.f) return false;
        FArrangedChildren Children(EVisibility::Visible); ParentSlate->ArrangeChildren(Parent->GetCachedGeometry(),Children); // UE5.4 SPanel同一布局入口；不执行Paint/Tick、不推进动画。
        for(int32 Index=0;Index<Children.Num();++Index)
        {
            const FArrangedWidget& Child=Children[Index]; // 只同步读取引擎当前槽结果，身份匹配排除其他行。
            if(&Child.Widget.Get()!=ChildSlate.Get()) continue;
            const FVector2D Size=Child.Geometry.GetLocalSize(); if(Size.X<=0.f||Size.Y<=0.f) return false; // 不让实际零分配借“离屏”逃过检查。
            const FVector2D Start=Child.Geometry.LocalToAbsolute(FVector2D::ZeroVector),End=Child.Geometry.LocalToAbsolute(Size);
            Rect=FSlateRect(Start.X,Start.Y,End.X,End.Y); return true;
        }
        return false;
    }
}

FString DemoUILayoutChecks::ScreenshotPath(const FString& Name)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UILayoutChecks.ScreenshotPath"));
    FString Directory; // 只读取显式测试命令行，普通游戏无此Editor接口，不改用户截图设置。
    if(!FParse::Value(FCommandLine::Get(),TEXT("DemoUIScreenshotDir="),Directory)) return Name;
    Directory=FPaths::ConvertRelativePathToFull(Directory); FPaths::NormalizeDirectoryName(Directory); FPaths::CollapseRelativeDirectories(Directory);
    const FString ArtRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Art/UI")); // 用户当前UI任务产物范围。
    const FString SavedRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()); // 隔离测试目录备选。
    if(!Directory.StartsWith(ArtRoot+TEXT("/"))&&!Directory.StartsWith(SavedRoot))
    { UE_LOG(LogFPSDemo,Error,TEXT("UI_LAYOUT rejected out-of-project screenshot directory")); Directory=SavedRoot/TEXT("Tests/RejectedUIScreenShots"); }
    IFileManager::Get().MakeDirectory(*Directory,true);
    return Directory/FPaths::GetCleanFilename(Name); // 名称不能把调用方目录附加到目标外。
}

void DemoUILayoutChecks::Queue(UDemoMenuScreen* Screen)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UILayoutChecks.Queue"));
    if(FrameHook.IsValid()||ReadyResult) { UE_LOG(LogFPSDemo,Error,TEXT("UI_LAYOUT duplicate sample before previous result consumed")); ReadyResult=-1; ReadyError=TEXT("duplicate frame sample"); return; }
    PendingScreen=Screen; FrameHook=FCoreDelegates::OnEndFrame.AddStatic(&CaptureRenderedLayout);
}

int32 DemoUILayoutChecks::ProcessPending(FString& Error)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[TICK] UILayoutChecks.ProcessPending"));
    const int32 Result=ReadyResult; Error=ReadyError; // 取的是上次已绘制帧，而非PC本帧可能重建过的Widget缓存。
    ReadyResult=0; ReadyError.Reset(); return Result;
}

bool DemoUILayoutChecks::Validate(UDemoMenuScreen* Screen,FString& Error)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UILayoutChecks.Validate")); Error.Reset(); // 自由函数不使用依赖this的宏。
    if(!Screen||!Screen->WidgetTree) { Error=TEXT("missing active widget tree"); return false; }
    auto* Scroll=Screen->WidgetTree->FindWidget(TEXT("PageScroll")); // 所有正式页面必须保留具名规范壳层。
    if(!Scroll||Scroll->GetCachedGeometry().GetLocalSize().X<=0) { Error=TEXT("body has no rendered layout"); return false; }
    const FSlateRect Body=Bounds(Scroll); // 实测正文屏幕区域。
    for(const TCHAR* Name:{TEXT("PageNavigation"),TEXT("PageCategories"),TEXT("PageFooter")})
    {
        auto* Region=Screen->WidgetTree->FindWidget(Name); // 实际固定区域，不通过预期指针自证。
        if(!Region) { Error=FString::Printf(TEXT("missing fixed region %s"),Name); return false; }
        if(Region->GetVisibility()==ESlateVisibility::Collapsed) continue;
        const FSlateRect Rect=Bounds(Region); // 减1像素容忍浮点/DPI边界。
        if(Rect.Left<Body.Right&&Rect.Right>Body.Left&&Rect.Top<Body.Bottom-1&&Rect.Bottom>Body.Top+1)
        { Error=FString::Printf(TEXT("fixed region overlaps scrolling body: %s"),Name); return false; }
    }
    if(auto* Aside=Screen->WidgetTree->FindWidget(TEXT("AsideBounds")))
        if(Aside->GetVisibility()!=ESlateVisibility::Collapsed&&Bounds(Aside).Left<Body.Right+8)
        { Error=TEXT("side panel overlaps body or loses gutter"); return false; }
    TArray<UWidget*> Widgets; Screen->WidgetTree->GetAllWidgets(Widgets); // 当前可达树，不含已经销毁的按钮。
    TArray<FSlateRect> VisibleRects; TArray<FName> Actions; // 相同索引记录裁切后的有效点击边界及稳定意图名。
    for(auto* Widget:Widgets)
    {
        if(auto* Label=Cast<UTextBlock>(Widget))
            if(Label->GetParent()&&Label->GetParent()->GetFName()==TEXT("PageAside")&&!Label->GetText().IsEmpty())
            {
                bool Hidden=false; // 显式隐藏是可证的不可见原因，不能与零尺寸挤压混为一谈。
                for(const UWidget* Ancestor=Label;Ancestor;Ancestor=Ancestor->GetParent())
                    if(Ancestor->GetVisibility()==ESlateVisibility::Hidden||Ancestor->GetVisibility()==ESlateVisibility::Collapsed) { Hidden=true; break; }
                if(Hidden) { UE_LOG(LogFPSDemo,Log,TEXT("UI_LAYOUT_TEXT_CULLED reason=hidden-ancestor text='%s'"),*Label->GetText().ToString().Left(48)); continue; }
                const FVector2D Size=Label->GetCachedGeometry().GetLocalSize(); FSlateRect Rect=Bounds(Label); // 非零使用真正已绘制分配，不能用DesiredSize自证。
                if(Size.X<=0.f||Size.Y<=0.f)
                {
                    FSlateRect CurrentRect; // SPanel在Paint前剔除离屏子项，DesktopGeometry未被写入；须由真实ArrangeChildren证明其在裁切外。
                    if(ArrangeCurrentChildBounds(Label,CurrentRect)&&!ClipToVisibleAncestors(Label,Screen,CurrentRect))
                    { UE_LOG(LogFPSDemo,Log,TEXT("UI_LAYOUT_TEXT_CULLED reason=unpainted-outside-scroll text='%s' actual=%.1fx%.1f"),*Label->GetText().ToString().Left(48),Size.X,Size.Y); continue; }
                    Error=FString::Printf(TEXT("side-panel text lacks rendered allocation without offscreen proof text='%s' desired=%.1fx%.1f actual=%.1fx%.1f"),*Label->GetText().ToString().Left(48),Label->GetDesiredSize().X,Label->GetDesiredSize().Y,Size.X,Size.Y); return false;
                }
                if(!ClipToVisibleAncestors(Label,Screen,Rect)) { UE_LOG(LogFPSDemo,Log,TEXT("UI_LAYOUT_TEXT_CULLED reason=outside-scroll text='%s'"),*Label->GetText().ToString().Left(48)); continue; }
                if(Label->GetDesiredSize().Y>Size.Y+1.f)
                { Error=FString::Printf(TEXT("side-panel text exceeds allocation text='%s' desired=%.1fx%.1f actual=%.1fx%.1f wrap=%.1f"),*Label->GetText().ToString().Left(48),Label->GetDesiredSize().X,Label->GetDesiredSize().Y,Size.X,Size.Y,Label->GetWrapTextAt()); return false; } // 可视文本保留原严格高度断言，不能用滚动或首帧延迟掩盖挤压。
            }
        auto* Button=Cast<UDemoMenuButton>(Widget); if(!Button) continue;
        FSlateRect Rect=Bounds(Button); bool Visible=true; // 屏幕与所有ScrollBox交集，不能把滚出视口的缓存几何误判成重叠。
        for(UWidget* Ancestor=Button;Ancestor;Ancestor=Ancestor->GetParent())
        {
            if(Ancestor->GetVisibility()==ESlateVisibility::Hidden||Ancestor->GetVisibility()==ESlateVisibility::Collapsed) { Visible=false; break; }
            if(Cast<UScrollBox>(Ancestor)||Ancestor==Screen->WidgetTree->RootWidget)
            {
                const FSlateRect Clip=Bounds(Ancestor); // 纯轴对齐裁切；当前菜单无旋转布局。
                Rect.Left=FMath::Max(Rect.Left,Clip.Left); Rect.Top=FMath::Max(Rect.Top,Clip.Top);
                Rect.Right=FMath::Min(Rect.Right,Clip.Right); Rect.Bottom=FMath::Min(Rect.Bottom,Clip.Bottom);
            }
        }
        if(!Visible||Rect.Right-Rect.Left<1||Rect.Bottom-Rect.Top<1) continue;
        for(int32 Index=0;Index<VisibleRects.Num();++Index)
        {
            const auto& Other=VisibleRects[Index]; // 不同操作之间不允许实质点击面积重叠。
            if(FMath::Min(Rect.Right,Other.Right)-FMath::Max(Rect.Left,Other.Left)>1&&FMath::Min(Rect.Bottom,Other.Bottom)-FMath::Max(Rect.Top,Other.Top)>1)
            { Error=FString::Printf(TEXT("visible actions overlap: %s / %s"),*Actions[Index].ToString(),*Button->GetAction().ToString()); return false; }
        }
        VisibleRects.Add(Rect); Actions.Add(Button->GetAction());
        if(Button->GetAction().ToString().StartsWith(TEXT("Setting"))&&Button->GetAction()!=TEXT("SettingsApply")&&Button->GetCachedGeometry().GetLocalSize().X>80.f)
        { Error=TEXT("setting stepper stretched beyond compact 80px budget"); return false; }
    }
    if(VisibleRects.IsEmpty()) { Error=TEXT("no actual buttons rendered; cannot validate empty geometry"); return false; }
    if(auto* FirstCore=Screen->FindAction(TEXT("Core0")))
        if(auto* LastCore=Screen->FindAction(TEXT("Core2")))
            if(FMath::Abs(Bounds(FirstCore).Bottom-Bounds(LastCore).Bottom)>1.f)
            { Error=TEXT("parallel core card actions lost their common baseline"); return false; }
    if(auto* LastNumber=Screen->FindAction(TEXT("Casino.Number.99")))
        if(Bounds(LastNumber).Bottom>Body.Bottom+1||Bounds(LastNumber).Top<Body.Top-1)
        { Error=TEXT("100-number board does not fit its initial viewport"); return false; }
    UE_LOG(LogFPSDemo,Log,TEXT("UI_LAYOUT_PASS page=%s visible_actions=%d body=%.0fx%.0f"),*Screen->GetClass()->GetName(),VisibleRects.Num(),Body.Right-Body.Left,Body.Bottom-Body.Top);
    return true;
}
#endif

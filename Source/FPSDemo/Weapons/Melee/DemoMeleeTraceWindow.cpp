// 职责：解析原生AnimNotifyState作者区间供显式取样近战使用。由定义校验/每刀初始化调用，依赖UE5.4公开Notifies数据；不依赖MeleeTrace插件，不在通知对象保存任何玩家状态。
#include "Weapons/Melee/DemoMeleeTraceWindow.h"
#include "Animation/AnimSequenceBase.h"
#include "Debug/DemoLog.h"

UDemoMeleeTraceWindow::UDemoMeleeTraceWindow()
{
    DEMO_LOG_CALL();
#if WITH_EDITORONLY_DATA
    NotifyColor=FColor(255,130,35); // 作者轨道橙色窗口，打击段在Persona内可直接拖动。
    bShouldFireInEditor=false; // 预览只显示窗口；碰撞属于运行时组件/服务器，绝不因编辑器预览造成业务事件。
#endif
}

FString UDemoMeleeTraceWindow::GetNotifyName_Implementation() const
{
    DEMO_LOG_TICK();
    return TEXT("Demo Melee Trace Window");
}

bool UDemoMeleeTraceWindow::ReadWindow(const UAnimSequenceBase* Sequence,FVector2D& OutWindow,FString& Error)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); // 静态读取器无this，保留同级入口埋点而不误用UObject宏。
    OutWindow=FVector2D::ZeroVector; Error.Reset(); // 失败不残留上一武器/上一招的窗口。
    if (!Sequence || !FMath::IsFinite(Sequence->GetPlayLength()) || Sequence->GetPlayLength()<=0.f)
    { Error=TEXT("Trace-window animation is missing or has invalid length"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NOTIFY_WINDOW_REJECT %s"),*Error); return false; }
    const FAnimNotifyEvent* Window=nullptr; // 只在同步读取期间借用；不保留动画数组元素到下一帧。
    for (const FAnimNotifyEvent& Event:Sequence->Notifies)
        if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UDemoMeleeTraceWindow>())
        {
            if (Window)
            { Error=TEXT("Expected exactly one Demo Melee Trace Window per canonical TP attack"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NOTIFY_WINDOW_REJECT %s"),*Error); return false; }
            Window=&Event;
        }
    if (!Window)
    { Error=TEXT("Canonical TP attack has no Demo Melee Trace Window"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NOTIFY_WINDOW_REJECT %s"),*Error); return false; }
    const float Start=Window->GetTime(),Duration=Window->GetDuration(),Length=Sequence->GetPlayLength(); // 作者源秒；不用TriggerTime的浮点触发偏移改写刀刃范围。
    if (!FMath::IsFinite(Start) || !FMath::IsFinite(Duration) || Start<0.f || Duration<=0.f || Start+Duration>Length+KINDA_SMALL_NUMBER)
    { Error=TEXT("Trace-window start/duration is non-finite or outside the canonical animation"); UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_NOTIFY_WINDOW_REJECT %s"),*Error); return false; }
    OutWindow=FVector2D(Start/Length,FMath::Min(1.f,(Start+Duration)/Length));
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_NOTIFY_WINDOW_READ animation=%s source=[%.6f,%.6f] phase=[%.6f,%.6f]"),*GetNameSafe(Sequence),Start,Start+Duration,OutWindow.X,OutWindow.Y);
    return true;
}

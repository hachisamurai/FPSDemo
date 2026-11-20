// 职责：Editor限时调用追踪开关。使用：Demo.Debug.TraceCalls [秒数]，0立即停止；依赖CoreTicker/单调时钟，不持有World、不创建云服务，不进入包体。
#if WITH_EDITOR
#include "Debug/DemoCallTrace.h"
#include "Debug/DemoLog.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "String/LexFromString.h" // UE5.4的LexTryParseString声明位于String目录，使用真实引擎头避免Editor编译失败。

namespace
{
    double TraceEndsAt = 0.0; // 游戏线程单调秒，0表示关闭；不受World暂停/PIE切图影响。
    ELogVerbosity::Type RestoreVerbosity = ELogVerbosity::Log; // 开启前低频级别；已有VeryVerbose也恢复Log，避免一次命令把全量日志永久留下。
    FTSTicker::FDelegateHandle TraceTicker; // 仅追踪窗口持有一个CoreTicker回调，0停止时精确解绑。

    /** 无外部参数；追踪结束/手工停止恢复原低频级别，不写配置文件或修改包体策略。 */
    void FinishTrace()
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoCallTrace::FinishTrace"));
        LogFPSDemo.SetVerbosity(RestoreVerbosity);
        TraceEndsAt = 0.0;
        UE_LOG(LogFPSDemo, Display, TEXT("CALL_TRACE_STOP restored=%d"), static_cast<int32>(RestoreVerbosity));
    }

    /** DeltaSeconds为CoreTicker上下文，不累计其值；游戏线程按单调截止时间结束，返回false让Ticker移除自己。 */
    bool TickTrace(float DeltaSeconds)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] DemoCallTrace::TickTrace"));
        if (FPlatformTime::Seconds() < TraceEndsAt) return true;
        FinishTrace(); TraceTicker.Reset();
        return false;
    }

    /** Args为本次控制台参数，默认5秒、上限10秒、0停止；只允许游戏线程，重复开启先结束原窗口。 */
    void TraceCalls(const TArray<FString>& Args)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoCallTrace::TraceCalls"));
        if (!IsInGameThread()) { UE_LOG(LogFPSDemo, Warning, TEXT("CALL_TRACE_REJECT not game thread")); return; }
        float Seconds = 5.f; // 用户请求的有限时长；非法参数拒绝，避免NaN使追踪永不结束。
        if (Args.Num() > 1 || (Args.Num() == 1 && !LexTryParseString(Seconds, *Args[0])) || !FMath::IsFinite(Seconds) || Seconds < 0.f)
        { UE_LOG(LogFPSDemo, Warning, TEXT("CALL_TRACE_REJECT usage: Demo.Debug.TraceCalls [0..10]")); return; }
        if (TraceTicker.IsValid())
        {
            FTSTicker::GetCoreTicker().RemoveTicker(TraceTicker); TraceTicker.Reset(); FinishTrace();
        }
        if (Seconds == 0.f) return;
        RestoreVerbosity = LogFPSDemo.GetVerbosity() < ELogVerbosity::VeryVerbose ? LogFPSDemo.GetVerbosity() : ELogVerbosity::Log;
        TraceEndsAt = FPlatformTime::Seconds() + FMath::Min(Seconds, 10.f);
        LogFPSDemo.SetVerbosity(ELogVerbosity::VeryVerbose);
        TraceTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickTrace), .1f); // 静态函数无对象捕获；CoreTicker游戏线程执行，关闭时移除。
        UE_LOG(LogFPSDemo, Display, TEXT("CALL_TRACE_START seconds=%.1f; automatic restore enabled"), FMath::Min(Seconds, 10.f));
    }

    FAutoConsoleCommand TraceCommand(TEXT("Demo.Debug.TraceCalls"), TEXT("Editor only: full gameplay call tracing for up to 10 seconds (default 5); 0 stops."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&TraceCalls)); // 随模块注册/析构；无World引用，不进入任何非Editor目标。
}

void DemoCallTrace::Shutdown()
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoCallTrace::Shutdown"));
    if (TraceTicker.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TraceTicker); TraceTicker.Reset(); FinishTrace();
    }
}
#endif

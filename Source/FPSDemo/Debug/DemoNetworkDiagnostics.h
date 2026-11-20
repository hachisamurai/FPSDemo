#pragma once

#include "CoreMinimal.h"

class UWorld;

#if WITH_EDITOR
/** Editor进程专用的被动诊断；不创建OSS、HTTP、Socket或修改系统网络。 */
namespace DemoNetworkDiagnostics
{
    /** 游戏线程查询开关；关闭后不采集也不写独立文件，包体不编译此接口。 */
    FPSDEMO_API bool IsEnabled();

    /** Event为固定事件名；World仅在调用期间借用；Details只能传内部状态/计数，禁止URL、账号、Token和错误正文。
     * 同步追加到本进程独立日志并关闭文件，记录UTC/单调时间/PID/序号；不能从工作线程调用或持有World跨帧。 */
    FPSDEMO_API void Record(const TCHAR* Event, const UWorld* World = nullptr, const FString& Details = FString());
}

// 参数只在Editor求值，避免包体执行额外查询、构造字符串或增加网络诊断日志。
#define DEMO_NET_DIAG(Event, World, Details) DemoNetworkDiagnostics::Record(Event, World, Details)
#else
#define DEMO_NET_DIAG(Event, World, Details) do {} while (false)
#endif

#pragma once

#include "CoreMinimal.h"

// 全模块共用调用日志；对象名 + C++ 函数名用于串联 GAS、输入和波次回调。
// 导出到 FPSDemoEditor，使导入/资产验证与运行时共用日志而不会发生跨 DLL 链接错误。
// 以构建目标而非GIsEditor区分：Editor -game仍完整埋点；所有非Editor包编译裁掉Display以下的调用/逐帧日志。
#if WITH_EDITOR
FPSDEMO_API DECLARE_LOG_CATEGORY_EXTERN(LogFPSDemo, VeryVerbose, All);
#else
FPSDEMO_API DECLARE_LOG_CATEGORY_EXTERN(LogFPSDemo, Display, Display);
#endif

// 所有入口继续保留埋点；Editor默认输出，非Editor由类别编译上限移除。宏不保存this。
#define DEMO_LOG_CALL() UE_LOG(LogFPSDemo, Log, TEXT("[%s] %hs"), *GetNameSafe(this), __FUNCTION__)
// 高频入口保留每次调用埋点；Editor类别默认VeryVerbose，可临时降噪，包体不求值日志参数。
#define DEMO_LOG_TICK() UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[%s] %hs"), *GetNameSafe(this), __FUNCTION__)

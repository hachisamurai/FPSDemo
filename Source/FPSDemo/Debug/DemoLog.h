#pragma once

#include "CoreMinimal.h"

// 全模块共用调用日志；对象名 + C++ 函数名用于串联 GAS、输入和波次回调。
// 导出到 FPSDemoEditor，使导入/资产验证与运行时共用日志而不会发生跨 DLL 链接错误。
FPSDEMO_API DECLARE_LOG_CATEGORY_EXTERN(LogFPSDemo, Log, All);

// 普通入口默认输出；宏仅用于 UObject 成员函数，this 不会被保存。
#define DEMO_LOG_CALL() UE_LOG(LogFPSDemo, Log, TEXT("[%s] %hs"), *GetNameSafe(this), __FUNCTION__)
// 高频入口保留每次调用埋点；DefaultEngine.ini 默认启用 VeryVerbose，可按文档降噪。
#define DEMO_LOG_TICK() UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[%s] %hs"), *GetNameSafe(this), __FUNCTION__)

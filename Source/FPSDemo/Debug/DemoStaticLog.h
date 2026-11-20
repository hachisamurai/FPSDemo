// 职责：给无this的静态工具/查询提供同类别入口日志。使用：显式传入同步借用的UObject上下文；依赖DemoLog的目标级别裁剪，不保存指针。
#pragma once

#include "Debug/DemoLog.h"

// 静态业务函数传入UObject上下文；显式基类指针令nullptr不与FField的GetNameSafe重载歧义，不提高包体日志上限。
#define DEMO_LOG_STATIC_CALL(Context) UE_LOG(LogFPSDemo, Log, TEXT("[%s] %hs"), *GetNameSafe(static_cast<const UObjectBaseUtility*>(Context)), __FUNCTION__)
// 静态高频查询仅VeryVerbose，仍受Editor限时追踪及默认降噪规则控制。
#define DEMO_LOG_STATIC_TICK(Context) UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[%s] %hs"), *GetNameSafe(static_cast<const UObjectBaseUtility*>(Context)), __FUNCTION__)

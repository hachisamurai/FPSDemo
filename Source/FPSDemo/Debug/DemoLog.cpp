// 职责：唯一项目日志类别。使用：所有运行模块引用DemoLog.h；Editor逐帧追踪由限时控制台命令开启，包体编译上限仍为Display。
#include "Debug/DemoLog.h"

// 模块唯一日志类别定义，避免每个子系统使用不同过滤名称。
DEFINE_LOG_CATEGORY(LogFPSDemo);

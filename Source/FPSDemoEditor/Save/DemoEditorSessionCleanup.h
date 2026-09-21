#pragma once
#include "CoreMinimal.h"

/** 仅父Editor使用的临时存档回收；不进入打包游戏，不删除运行中的子进程数据。 */
namespace DemoEditorSessionCleanup
{
    /** 模块启动注册真实Standalone启动委托与1秒轮询；-game子进程不注册。 */
    void Startup();
    /** 模块退出解绑全部回调，回收已退出子进程，关闭监视句柄但不终止进程。 */
    void Shutdown();
    /** ProcessId来自BeginStandaloneLocalPlay或自动化创建的子进程；只监视已打开的真实进程。 */
    void WatchProcess(uint32 ProcessId);
}

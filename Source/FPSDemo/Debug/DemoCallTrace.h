// 职责：限时调用追踪的模块卸载接口。使用：FPSDemo模块Shutdown调用；依赖CoreTicker，Editor以外不声明调试入口。
#pragma once
#if WITH_EDITOR
namespace DemoCallTrace
{
    /** 模块卸载前撤销静态Ticker并恢复日志级别；不依赖World销毁来释放模块函数地址。 */
    void Shutdown();
}
#endif

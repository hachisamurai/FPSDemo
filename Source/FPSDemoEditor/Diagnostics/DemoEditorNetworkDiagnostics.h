#pragma once

/** Editor模块拥有的诊断观察器；只监听生命周期和读取本机状态，不参与游戏网络清理。 */
namespace DemoEditorNetworkDiagnostics
{
    /** PostEngineInit安装游戏线程委托、低频Ticker和手工快照命令；重复调用不重复注册。 */
    void Startup();
    /** 模块卸载前移除所有回调与命令，不销毁游戏会话、不启停网卡、不发起探测。 */
    void Shutdown();
}

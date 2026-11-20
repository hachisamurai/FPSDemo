// 职责：主游戏模块注册和模块级诊断清理。使用：UE加载/卸载FPSDemo模块；依赖默认GameModule及Editor限时追踪，卸载前撤销静态Ticker。

#include "FPSDemo.h"
#include "Modules/ModuleManager.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoCallTrace.h"

class FFPSDemoModule final : public FDefaultGameModuleImpl
{
public:
    /** 引擎卸载模块时先撤销指向本DLL的追踪回调，再执行默认清理；不终止或保存玩家战局。 */
    virtual void ShutdownModule() override
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FFPSDemoModule::ShutdownModule"));
#if WITH_EDITOR
        DemoCallTrace::Shutdown();
#endif
        FDefaultGameModuleImpl::ShutdownModule();
    }
};
// 引擎注册胶水；自定义逻辑仅为卸载回调清理。
IMPLEMENT_PRIMARY_GAME_MODULE( FFPSDemoModule, FPSDemo, "FPSDemo" );
 

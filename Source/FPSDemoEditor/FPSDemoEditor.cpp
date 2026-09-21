#include "Modules/ModuleManager.h"
#include "Save/DemoEditorSessionCleanup.h"
#include "Debug/DemoLog.h"

/** 编辑器作者工具和试玩数据生命周期；Runtime模块不依赖UnrealEd。 */
class FFPSDemoEditorModule final : public IModuleInterface
{
public:
    /** 模块载入后监视Editor产生的Standalone进程，用于强制停止时补做数据清理。 */
    virtual void StartupModule() override
    { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FFPSDemoEditorModule::StartupModule")); DemoEditorSessionCleanup::Startup(); }
    /** 卸载前解绑委托与Ticker，避免回调进入已卸载代码。 */
    virtual void ShutdownModule() override
    { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FFPSDemoEditorModule::ShutdownModule")); DemoEditorSessionCleanup::Shutdown(); }
};
// 引擎模块注册胶水；具体功能回调在上方入口记录。
IMPLEMENT_MODULE(FFPSDemoEditorModule, FPSDemoEditor);

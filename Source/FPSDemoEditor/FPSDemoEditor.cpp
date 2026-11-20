#include "Modules/ModuleManager.h"
#include "Save/DemoEditorSessionCleanup.h"
#include "Diagnostics/DemoEditorNetworkDiagnostics.h" // 独立观察PIE/World/Windows状态，不参与网络业务清理。
#include "Debug/DemoLog.h"

/** 编辑器作者工具和试玩数据生命周期；Runtime模块不依赖UnrealEd。 */
class FFPSDemoEditorModule final : public IModuleInterface
{
public:
    /** 模块载入后先安装被动诊断，再监视Standalone临时存档；两者均不修改系统网络。 */
    virtual void StartupModule() override
    { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FFPSDemoEditorModule::StartupModule")); DemoEditorNetworkDiagnostics::Startup(); DemoEditorSessionCleanup::Startup(); }
    /** 卸载前先完成子进程句柄清理，再解绑诊断委托/Ticker，保证清理尾部仍有时间线。 */
    virtual void ShutdownModule() override
    { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FFPSDemoEditorModule::ShutdownModule")); DemoEditorSessionCleanup::Shutdown(); DemoEditorNetworkDiagnostics::Shutdown(); }
};
// 引擎模块注册胶水；具体功能回调在上方入口记录。
IMPLEMENT_MODULE(FFPSDemoEditorModule, FPSDemoEditor);

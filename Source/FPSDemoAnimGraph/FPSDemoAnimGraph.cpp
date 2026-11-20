#include "Modules/ModuleManager.h"
#include "Debug/DemoLog.h"

/** 仅注册自定义图节点类，未Cook -game也需要；不绑定Editor存档/窗口委托。 */
class FFPSDemoAnimGraphModule final : public IModuleInterface
{
public:
    /** 引擎Default载入图类，以便随后读取未Cook玩家AnimBP。 */
    virtual void StartupModule() override
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FFPSDemoAnimGraphModule::StartupModule")); }
    /** 无跨帧任务或委托，只记录图类模块卸载。 */
    virtual void ShutdownModule() override
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FFPSDemoAnimGraphModule::ShutdownModule")); }
};
IMPLEMENT_MODULE(FFPSDemoAnimGraphModule,FPSDemoAnimGraph);

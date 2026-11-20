#pragma once
#include "CoreMinimal.h"
class UWorld;
/** 开发启动参数适配，不参与正式玩法或系统初始化时序。 */
namespace DemoDevelopmentBootstrap
{
    /** World 为当前已安装全部服务的服务器世界；仅显式命令行参数创建隔离测试 Actor。 */
    void Start(UWorld* World);
}

#pragma once
#include "CoreMinimal.h"
#include "Contracts/DemoGameplayContracts.h"

/** 旧单人测试的PS迁移适配层；Player0仅允许用于这些明确单人夹具，生产系统不得引用。 */
namespace DemoTestProgress
{
    /** Context为本测试World内UObject；返回本人PS快照，缺依赖记录错误并返回只读空值。 */
    const FDemoPlayerProgressSnapshot& Read(const UObject* Context);
    /** Context指定测试World，Amount为合法钱包值；使用权威Commit，不直接写GS兼容字段。 */
    bool SetGold(const UObject* Context, int32 Amount);
    bool SetSilver(const UObject* Context, int32 Amount);
    /** Context本机测试已通过Profile产生通关事实后，将权限安装到本人PS；不用于联网生产授权。 */
    bool ImportLocalPermissions(const UObject* Context);
}

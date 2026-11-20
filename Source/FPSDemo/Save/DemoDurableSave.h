// 职责：Windows本地SaveGame序列化/同目录暂存/Flush/原子替换。使用：小型同步槽保存；不触发HTTP，不保证未实测硬件断电行为。
#pragma once
#include "CoreMinimal.h"
class USaveGame;
namespace DemoDurableSave
{
    /** Snapshot为完整值对象、Slot为项目生成安全名；先暂存刷新再替换正式文件，失败不删除旧正式档。 */
    FPSDEMO_API bool Write(USaveGame* Snapshot, const FString& Slot);
    /** 升级前保存原始槽字节到固定归档名；已存在的归档不覆盖，仍需其为完整可读文件。 */
    FPSDEMO_API bool Archive(const FString& SourceSlot, const FString& ArchiveSlot);
    /** 停止Editor试玩时清理该GI确切槽名的未提交暂存，不枚举其他槽。 */
    FPSDEMO_API void RemoveStaging(const FString& Slot);
#if WITH_EDITOR
    /** 仅Editor纯合同测试注入0正常/1刷新前失败/2替换前失败；测试结束必须复原0，无控制台或非Editor接口。 */
    FPSDEMO_API void SetFailureForTest(int32 Point);
#endif
}

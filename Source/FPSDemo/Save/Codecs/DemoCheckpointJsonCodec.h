// 职责：检查点JSON序列化与严格解码。使用：读写本人稳定ID和等级，目录白名单校验，不从存档加载类型路径。
#pragma once
#include "CoreMinimal.h"

class UDemoRunSave;
class FJsonObject;

/** 独立云检查点协议边界；不读取World/GI、不写盘、不反射整个SaveGame。 */
namespace DemoCheckpointJsonCodec
{
    /** Snapshot为已持久化值；显式V1..V10白名单，V10含个人枪权/槽身份；旧版本不新增字段，未知结构拒绝。 */
    FPSDEMO_API TSharedPtr<FJsonObject> Encode(const UDemoRunSave& Snapshot,FString& Error);
    /** Data为远端协议值，Out为调用者新建的空候选；严格解析全部字段后迁移校验，不直接覆盖任何现有槽。 */
    FPSDEMO_API bool Decode(const TSharedPtr<FJsonObject>& Data,UDemoRunSave& Out,FString& Error);
}

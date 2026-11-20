// 职责：V10拥有权和枪墙收据的显式JSON白名单；使用：根和成员同一转换，不反射额外私有字段。
#pragma once
#include "CoreMinimal.h"
#include "Contracts/DemoWeaponOwnership.h"
class FJsonObject;
namespace DemoOwnershipJson
{
    /** 已校验的完整来源账本转换为独立JSON值，不写盘。 */
    FPSDEMO_API TSharedRef<FJsonObject> Encode(const FDemoWeaponOwnership& Value);
    /** 远端字段严格白名单/类型/长度解析到暂存Out，失败不修改原账本。 */
    FPSDEMO_API bool Decode(const TSharedPtr<FJsonObject>& Json, FDemoWeaponOwnership& Out);
}

// 职责：显式云DTO接口；HTTP3来源能力与已冻结Profile4/V10槽，不把新增UPROPERTY自动上传。
#pragma once
#include "CoreMinimal.h"

struct FDemoPlayerProfileData;
class FJsonObject;

/** 网络槽DTO；只含协议值，Snapshot由存档Codec完整验证，不持有World。 */
struct FDemoCloudSlot
{
    int32 Index = INDEX_NONE; // 固定三槽0..2；禁止用数组位置替代协议槽号。
    FString Revision; // 非负Int64字符串，保留服务器CAS精度。
    TSharedPtr<FJsonObject> Snapshot; // 当前响应共享值，仅在同步导入期间借用。
};

/** 显式网络编解码；不读文件、不访问GI、无HTTP和UObject生命周期副作用。 */
namespace DemoCloudProtocol
{
    /** Json为可空协议对象；只序列化为紧凑JSON，不打印包含认证字段的正文。 */
    FString Encode(const TSharedPtr<FJsonObject>& Json);
    /** Text为响应/耐久发件箱；失败返回空对象，由编排器决定重试。 */
    TSharedPtr<FJsonObject> Decode(const FString& Text);
    /** Snapshot为固定字段顺序的本地DTO；仅用于变化检测，不能作为认证摘要。 */
    FString Fingerprint(const TSharedPtr<FJsonObject>& Snapshot);
    /** Remote为云投影，OutSlots仅成功时有效；先校验全部槽号、版本、重复和快照，再允许任何写盘。 */
    bool ReadSlots(const TSharedPtr<FJsonObject>& Remote,TArray<FDemoCloudSlot>& OutSlots);
    /** Data为Profile4已冻结值；Binding/RequestId为绑定/不可变请求ID；Slots为已冻结变更槽，返回HTTP3与拥有权能力白名单。 */
    TSharedPtr<FJsonObject> BuildSync(const FDemoPlayerProfileData& Data,const FString& Binding,const FString& RequestId,const TArray<FDemoCloudSlot>& Slots);
    /** Data为旧工具调用的值；保留协议1字段形状，不反射新增UPROPERTY到网络。正式同步使用BuildSync。 */
    TSharedPtr<FJsonObject> BuildLegacyProfile(const FDemoPlayerProfileData& Data);
}

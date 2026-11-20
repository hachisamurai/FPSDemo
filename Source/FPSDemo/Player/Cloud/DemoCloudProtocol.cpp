// 职责：HTTP3不可变同步DTO/槽指纹编解码；使用Profile4冻结Legacy与V10槽，兼容旧Outbox重放，不访问World或发送HTTP。
#include "Player/Cloud/DemoCloudProtocol.h"
#include "Player/DemoPlayerProfile.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/SecureHash.h"
#include "Debug/DemoStaticLog.h"

namespace
{
    /** Data为只读通关列表，Limit为当前协议批次上限；每条只导出稳定事实字段。 */
    TArray<TSharedPtr<FJsonValue>> EncodeClears(const FDemoPlayerProfileData& Data,int32 Limit)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        TArray<TSharedPtr<FJsonValue>> Values; // 当前请求独占的数组，发件箱序列化后不再读取原Profile。
        for(int32 Index=0;Index<FMath::Min(Limit,Data.Clears.Num());++Index) // 有界批次，避免一次提交覆盖未携带事实。
        {
            const FDemoClearRecord& Clear=Data.Clears[Index]; // 仅本循环借用原始不可变事实。
            TSharedRef<FJsonObject> Item=MakeShared<FJsonObject>(); // 字段白名单，不导出未来结构新增字段。
            Item->SetStringField(TEXT("runId"),Clear.RunId); Item->SetStringField(TEXT("difficultyId"),Clear.DifficultyId); Item->SetStringField(TEXT("completedUtc"),Clear.CompletedUtc);
            Values.Add(MakeShared<FJsonValueObject>(Item));
        }
        return Values;
    }
}
FString DemoCloudProtocol::Encode(const TSharedPtr<FJsonObject>& Json)
{
    DEMO_LOG_STATIC_TICK(nullptr);
    FString Text; // 返回值持有正文，严禁记录其中的令牌/认证字段。
    if(Json)FJsonSerializer::Serialize(Json.ToSharedRef(),TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
    return Text;
}
TSharedPtr<FJsonObject> DemoCloudProtocol::Decode(const FString& Text)
{
    DEMO_LOG_STATIC_TICK(nullptr);
    TSharedPtr<FJsonObject> Json; // 失败返回空对象，编排器不能把空对象当成功回执。
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json);
    return Json;
}
FString DemoCloudProtocol::Fingerprint(const TSharedPtr<FJsonObject>& Snapshot)
{
    DEMO_LOG_STATIC_TICK(nullptr);
    return Snapshot?FMD5::HashAnsiString(*Encode(Snapshot)):FString();
}
bool DemoCloudProtocol::ReadSlots(const TSharedPtr<FJsonObject>& Remote,TArray<FDemoCloudSlot>& OutSlots)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    OutSlots.Empty();
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr; // 响应拥有数组，只在本次校验内借用。
    if(!Remote||!Remote->TryGetArrayField(TEXT("slots"),Values)||Values->Num()>3)return false;
    TSet<int32> Seen; // 防重复槽号导致同次响应第二次覆写同一文件。
    for(const TSharedPtr<FJsonValue>& Value:*Values) // 先生成完整候选DTO列表，不在验证中写磁盘。
    {
        const TSharedPtr<FJsonObject>* ItemPointer=nullptr; // TryGetObject不对非法网络元素触发AsObject诊断断言。
        if(!Value.IsValid()||!Value->TryGetObject(ItemPointer)||!ItemPointer||!ItemPointer->IsValid())return false;
        const TSharedPtr<FJsonObject>& Item=*ItemPointer; // 已验证对象由响应JSON拥有，仅本次循环借用。
        FDemoCloudSlot Slot; // 当前候选槽的纯值。
        const TSharedPtr<FJsonObject>* Snapshot=nullptr; // 服务器提供快照，具体存档版本由Save Codec验证。
        int64 Revision=0; // 字符串CAS严格拒绝负数和溢出。
        double Index=0; // JSON数字先检查整数性，不能把0.5截断为槽0。
        if(!Item->TryGetNumberField(TEXT("slotIndex"),Index)||!FMath::IsFinite(Index)||Index<0||Index>2||Index!=FMath::FloorToDouble(Index)
            ||!Item->TryGetStringField(TEXT("serverRevision"),Slot.Revision)||!LexTryParseString(Revision,*Slot.Revision)||Revision<0
            ||!Item->TryGetObjectField(TEXT("snapshot"),Snapshot))return false;
        Slot.Index=static_cast<int32>(Index); Slot.Snapshot=*Snapshot;
        if(Seen.Contains(Slot.Index))return false;
        Seen.Add(Slot.Index); OutSlots.Add(MoveTemp(Slot));
    }
    return true;
}
TSharedPtr<FJsonObject> DemoCloudProtocol::BuildSync(const FDemoPlayerProfileData& Data,const FString& Binding,const FString& RequestId,const TArray<FDemoCloudSlot>& Slots)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    TSharedRef<FJsonObject> Body=MakeShared<FJsonObject>(); // 不生成新ID；超时重放继续使用已保存的完整Outbox。
    Body->SetNumberField(TEXT("protocolVersion"),3); Body->SetStringField(TEXT("requestId"),RequestId); Body->SetStringField(TEXT("bindingId"),Binding);
    Body->SetArrayField(TEXT("capabilities"),{MakeShared<FJsonValueString>(TEXT("hub_weapon_ownership_v1"))}); // 旧API明确拒绝新结构；不阻塞本地购物。
    TSharedRef<FJsonObject> Migration=MakeShared<FJsonObject>(); // 账号一次冻结来源，与新通关批次独立。
    Migration->SetStringField(TEXT("migrationId"),Data.LegacyMigrationId); Migration->SetNumberField(TEXT("rulesVersion"),1); Migration->SetNumberField(TEXT("sourceRevision"),Data.LegacySourceRevision);
    TArray<TSharedPtr<FJsonValue>> BaselineIds,BaselineRuns; // 只发送已持久冻结集合，不将后续Clears混入。
    for (const FName Id:Data.LegacyWeaponIds) BaselineIds.Add(MakeShared<FJsonValueString>(Id.ToString().ToLower()));
    for (const FString& Id:Data.LegacyRunIds) BaselineRuns.Add(MakeShared<FJsonValueString>(Id));
    Migration->SetArrayField(TEXT("baselineWeaponIds"),BaselineIds); Migration->SetArrayField(TEXT("baselineRunIds"),BaselineRuns); Body->SetObjectField(TEXT("legacyMigration"),Migration);
    Body->SetNumberField(TEXT("localRevision"),Data.LocalRevision); Body->SetStringField(TEXT("baseServerRevision"),Data.ServerRevision.IsEmpty()?TEXT("0"):Data.ServerRevision);
    Body->SetArrayField(TEXT("clears"),EncodeClears(Data,64));
    const bool bLastBatch=Data.Clears.Num()<=64; // 偏好和槽只能在全部相关解锁进入此批次后上传。
    Body->SetBoolField(TEXT("hasPreferenceChange"),bLastBatch&&Data.PreferenceRevision>Data.SyncedPreferenceRevision);
    Body->SetStringField(TEXT("lastSelectedPrimary"),Data.LastSelectedPrimary.IsNone()?TEXT(""):Data.LastSelectedPrimary.ToString().ToLower());
    TArray<TSharedPtr<FJsonValue>> Changes; // 变更槽DTO与原本地缓存分离。
    if(bLastBatch)for(const FDemoCloudSlot& Slot:Slots) // 三槽已由本地编排器筛选指纹。
    {
        TSharedRef<FJsonObject> Item=MakeShared<FJsonObject>(); // 显式CAS写操作字段。
        Item->SetNumberField(TEXT("slotIndex"),Slot.Index); Item->SetStringField(TEXT("baseSlotRevision"),Slot.Revision); Item->SetObjectField(TEXT("snapshot"),Slot.Snapshot);
        Changes.Add(MakeShared<FJsonValueObject>(Item));
    }
    Body->SetArrayField(TEXT("slots"),Changes);
    return Body;
}
TSharedPtr<FJsonObject> DemoCloudProtocol::BuildLegacyProfile(const FDemoPlayerProfileData& Data)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>(); // 保留兼容工具约定；不作为正式HTTP出口。
    Json->SetNumberField(TEXT("schemaVersion"),Data.SchemaVersion); Json->SetStringField(TEXT("profileId"),Data.ProfileId);
    Json->SetNumberField(TEXT("localRevision"),Data.LocalRevision); Json->SetNumberField(TEXT("syncedRevision"),Data.SyncedRevision);
    Json->SetStringField(TEXT("serverRevision"),Data.ServerRevision); Json->SetStringField(TEXT("updatedUtc"),Data.UpdatedUtc);
    Json->SetArrayField(TEXT("clears"),EncodeClears(Data,MAX_int32));
    TArray<TSharedPtr<FJsonValue>> Difficulties,Runs,Weapons; // 显式字符串列表，不导出UObject信息。
    for(const FString& Value:Data.CloudClearedDifficulties)Difficulties.Add(MakeShared<FJsonValueString>(Value)); // 永久难度事实。
    for(const FString& Value:Data.RecentAcceptedRuns)Runs.Add(MakeShared<FJsonValueString>(Value)); // 已确认幂等记录。
    for(const FName Value:Data.UnlockedWeaponIds)Weapons.Add(MakeShared<FJsonValueString>(Value.ToString())); // 兼容旧接口保留FName文本。
    Json->SetArrayField(TEXT("cloudClearedDifficulties"),Difficulties); Json->SetArrayField(TEXT("recentAcceptedRuns"),Runs); Json->SetArrayField(TEXT("unlockedWeaponIds"),Weapons);
    Json->SetNumberField(TEXT("preferenceRevision"),Data.PreferenceRevision); Json->SetNumberField(TEXT("syncedPreferenceRevision"),Data.SyncedPreferenceRevision);
    Json->SetStringField(TEXT("lastSelectedPrimary"),Data.LastSelectedPrimary.ToString());
    return Json;
}

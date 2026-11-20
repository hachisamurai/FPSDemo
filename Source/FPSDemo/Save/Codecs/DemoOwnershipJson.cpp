// 职责：根/成员枪权使用相同HTTP3值协议；不信客户端资源路径，不触发加载/保存/权益发放。
#include "Save/Codecs/DemoOwnershipJson.h"
#include "Debug/DemoStaticLog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include <initializer_list>
namespace
{
    /** 小型对象键白名单，不接受未来未知字段而丢弃其语义。 */
    bool Keys(const FJsonObject& Object,std::initializer_list<const TCHAR*> Allowed)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        for (const auto& Field:Object.Values) { bool bFound=false; for (const TCHAR* Key:Allowed) if (Field.Key==Key) bFound=true; if (!bFound) return false; } return true;
    }
    /** 必需非负int32，避免double截断或溢出。 */
    bool Integer(const FJsonObject& Object,const TCHAR* Key,int32& Out)
    {
        DEMO_LOG_STATIC_CALL(nullptr); double Number=0; // 先验证后转换。
        if (!Object.HasTypedField<EJson::Number>(Key)||!Object.TryGetNumberField(Key,Number)||!FMath::IsFinite(Number)||Number<0||Number>MAX_int32||Number!=FMath::FloorToDouble(Number)) return false;
        Out=static_cast<int32>(Number); return true;
    }
    /** 必需有界字符串；空值的领域限制由最终Validate检查。 */
    bool String(const FJsonObject& Object,const TCHAR* Key,FString& Out,int32 Limit)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        return Object.HasTypedField<EJson::String>(Key)&&Object.TryGetStringField(Key,Out)&&Out.Len()<=Limit;
    }
}
TSharedRef<FJsonObject> DemoOwnershipJson::Encode(const FDemoWeaponOwnership& Value)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>(); // 独立JSON不借用后续可变账本。
    Json->SetNumberField(TEXT("schema"),Value.Schema); Json->SetNumberField(TEXT("revision"),Value.Revision); Json->SetStringField(TEXT("legacyMigrationId"),Value.LegacyMigrationId);
    Json->SetNumberField(TEXT("legacySourceRevision"),Value.LegacySourceRevision); Json->SetNumberField(TEXT("shopSerial"),Value.ShopSerial);
    TArray<TSharedPtr<FJsonValue>> Entries,Receipts; // 固定小型上限，Validate由外层先执行。
    for (const FDemoWeaponOwnershipEntry& Entry:Value.Entries)
    {
        TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>(); // 一次获得的来源记录。
        Row->SetStringField(TEXT("weaponId"),Entry.WeaponId.ToString().ToLower()); Row->SetStringField(TEXT("acquisitionId"),Entry.AcquisitionId);
        Row->SetStringField(TEXT("source"),Entry.Source==EDemoWeaponAcquisitionSource::GoldPurchase?TEXT("GoldPurchase"):Entry.Source==EDemoWeaponAcquisitionSource::Legacy?TEXT("Legacy"):TEXT("Robbery"));
        Row->SetNumberField(TEXT("pricePaid"),Entry.PricePaid); Row->SetNumberField(TEXT("offerRevision"),Entry.OfferRevision); Row->SetBoolField(TEXT("initialSupplyGranted"),Entry.bInitialSupplyGranted); Entries.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (const FDemoWeaponShopReceipt& Receipt:Value.ShopReceipts)
    {
        TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>(); // 不包含钱包快照或私有云令牌。
        Row->SetStringField(TEXT("requestId"),Receipt.RequestId); Row->SetStringField(TEXT("payloadHash"),Receipt.PayloadHash); Row->SetNumberField(TEXT("commandSerial"),Receipt.CommandSerial);
        Row->SetStringField(TEXT("weaponId"),Receipt.WeaponId.ToString().ToLower()); Row->SetNumberField(TEXT("chargedGold"),Receipt.ChargedGold); Row->SetNumberField(TEXT("ownershipRevision"),Receipt.OwnershipRevision); Row->SetBoolField(TEXT("bought"),Receipt.bBought); Receipts.Add(MakeShared<FJsonValueObject>(Row));
    }
    Json->SetArrayField(TEXT("entries"),Entries); Json->SetArrayField(TEXT("shopReceipts"),Receipts); return Json;
}
bool DemoOwnershipJson::Decode(const TSharedPtr<FJsonObject>& Json,FDemoWeaponOwnership& Out)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    FDemoWeaponOwnership Candidate; // 全部字段和引用关系校验通过后才替换Out。
    const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr; const TArray<TSharedPtr<FJsonValue>>* Receipts=nullptr; // 借用本JSON解析栈。
    if (!Json || !Keys(*Json,{TEXT("schema"),TEXT("revision"),TEXT("legacyMigrationId"),TEXT("legacySourceRevision"),TEXT("entries"),TEXT("shopSerial"),TEXT("shopReceipts")})
        ||!Integer(*Json,TEXT("schema"),Candidate.Schema)||!Integer(*Json,TEXT("revision"),Candidate.Revision)||!String(*Json,TEXT("legacyMigrationId"),Candidate.LegacyMigrationId,36)
        ||!Integer(*Json,TEXT("legacySourceRevision"),Candidate.LegacySourceRevision)||!Integer(*Json,TEXT("shopSerial"),Candidate.ShopSerial)
        ||!Json->TryGetArrayField(TEXT("entries"),Entries)||Entries->Num()>3||!Json->TryGetArrayField(TEXT("shopReceipts"),Receipts)||Receipts->Num()>16) return false;
    for (const TSharedPtr<FJsonValue>& Value:*Entries)
    {
        const TSharedPtr<FJsonObject>* Row=nullptr; FDemoWeaponOwnershipEntry Entry; FString Id,Source; // 未完成一行不提交。
        if (!Value||!Value->TryGetObject(Row)||!Keys(**Row,{TEXT("weaponId"),TEXT("acquisitionId"),TEXT("source"),TEXT("pricePaid"),TEXT("offerRevision"),TEXT("initialSupplyGranted")})
            ||!String(**Row,TEXT("weaponId"),Id,32)||!String(**Row,TEXT("acquisitionId"),Entry.AcquisitionId,36)||!String(**Row,TEXT("source"),Source,16)
            ||!Integer(**Row,TEXT("pricePaid"),Entry.PricePaid)||!Integer(**Row,TEXT("offerRevision"),Entry.OfferRevision)||!(*Row)->HasTypedField<EJson::Boolean>(TEXT("initialSupplyGranted"))||!(*Row)->TryGetBoolField(TEXT("initialSupplyGranted"),Entry.bInitialSupplyGranted)) return false;
        if (Source!=TEXT("Legacy")&&Source!=TEXT("GoldPurchase")&&Source!=TEXT("Robbery")) return false;
        Entry.WeaponId=FName(*Id); Entry.Source=Source==TEXT("Legacy")?EDemoWeaponAcquisitionSource::Legacy:Source==TEXT("GoldPurchase")?EDemoWeaponAcquisitionSource::GoldPurchase:EDemoWeaponAcquisitionSource::Robbery; Candidate.Entries.Add(Entry);
    }
    for (const TSharedPtr<FJsonValue>& Value:*Receipts)
    {
        const TSharedPtr<FJsonObject>* Row=nullptr; FDemoWeaponShopReceipt Receipt; FString Id; // 成功收据字段也使用同一严格整型规则。
        if (!Value||!Value->TryGetObject(Row)||!Keys(**Row,{TEXT("requestId"),TEXT("payloadHash"),TEXT("commandSerial"),TEXT("weaponId"),TEXT("chargedGold"),TEXT("ownershipRevision"),TEXT("bought")})
            ||!String(**Row,TEXT("requestId"),Receipt.RequestId,36)||!String(**Row,TEXT("payloadHash"),Receipt.PayloadHash,45)||!Integer(**Row,TEXT("commandSerial"),Receipt.CommandSerial)
            ||!String(**Row,TEXT("weaponId"),Id,32)||!Integer(**Row,TEXT("chargedGold"),Receipt.ChargedGold)||!Integer(**Row,TEXT("ownershipRevision"),Receipt.OwnershipRevision)
            ||!(*Row)->HasTypedField<EJson::Boolean>(TEXT("bought"))||!(*Row)->TryGetBoolField(TEXT("bought"),Receipt.bBought)) return false;
        Receipt.WeaponId=FName(*Id); Candidate.ShopReceipts.Add(Receipt);
    }
    if (!Candidate.Validate(true)) return false; Out=MoveTemp(Candidate); return true;
}

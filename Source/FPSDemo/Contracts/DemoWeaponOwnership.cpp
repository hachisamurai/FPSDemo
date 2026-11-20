// 职责：验证永久枪权、一次Legacy冻结和暂存购买来源；调用者负责整槽原子保存，不接触World、资产或云服务。
#include "Contracts/DemoWeaponOwnership.h"
#include "Debug/DemoStaticLog.h"
#include "Weapons/DemoWeaponCatalog.h"

namespace
{
    /** 规范GUID检查用于持久身份，不把任意非空字符串当来源凭据。 */
    bool CanonicalGuid(const FString& Value)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        FGuid Id; // 同步解析值，不持有外部对象。
        return Value.Len()==36 && FGuid::Parse(Value, Id) && Id.IsValid() && Id.ToString(EGuidFormats::DigitsWithHyphens).Equals(Value,ESearchCase::IgnoreCase);
    }
}

bool FDemoWeaponOwnership::Validate(bool bRequireFrozen) const
{
    DEMO_LOG_STATIC_CALL(nullptr);
    if (Schema != 1 || Revision < 0 || LegacySourceRevision < 0 || ShopSerial < 0 || Entries.Num() > 3 || ShopReceipts.Num() > 16
        || (LegacyMigrationId.IsEmpty() ? (bRequireFrozen || !Entries.IsEmpty() || ShopSerial != 0 || Revision != 0) : !CanonicalGuid(LegacyMigrationId)))
    { UE_LOG(LogFPSDemo, Warning, TEXT("OWNERSHIP_REJECT schema/migration/revision")); return false; }
    TSet<FName> Seen; TSet<FString> Acquisitions; // 候选值去重，集合不跨调用缓存。
    for (const FDemoWeaponOwnershipEntry& Entry : Entries)
    {
        if (DemoWeaponCatalog::IndexOf(Entry.WeaponId) <= 0 || Seen.Contains(Entry.WeaponId) || !CanonicalGuid(Entry.AcquisitionId)
            || Acquisitions.Contains(Entry.AcquisitionId.ToUpper()) || static_cast<uint8>(Entry.Source) > 2 || Entry.PricePaid < 0 || Entry.OfferRevision < 0
            || (Entry.Source == EDemoWeaponAcquisitionSource::GoldPurchase && (Entry.OfferRevision == 0 || !Entry.bInitialSupplyGranted))
            || (Entry.Source == EDemoWeaponAcquisitionSource::Legacy && (Entry.PricePaid != 0 || Entry.OfferRevision != 0))
            || (Entry.Source == EDemoWeaponAcquisitionSource::Robbery && (Entry.PricePaid != 0 || Entry.OfferRevision == 0 || !Entry.bInitialSupplyGranted)))
        { UE_LOG(LogFPSDemo, Warning, TEXT("OWNERSHIP_REJECT entry source/value")); return false; }
        Seen.Add(Entry.WeaponId); Acquisitions.Add(Entry.AcquisitionId.ToUpper());
    }
    int32 Previous = 0; TSet<FString> Requests; // 收据严格有序并保高水位，不在重试时重新执行已淘汰命令。
    for (const FDemoWeaponShopReceipt& Receipt : ShopReceipts)
    {
        bool bHashValid = Receipt.PayloadHash.Len() == 45 && Receipt.PayloadHash.StartsWith(TEXT("sha1:")); // 固定ASCII十六进制摘要形状。
        for (int32 Index = 5; bHashValid && Index < Receipt.PayloadHash.Len(); ++Index)
            bHashValid = (Receipt.PayloadHash[Index] >= TEXT('0') && Receipt.PayloadHash[Index] <= TEXT('9')) || (Receipt.PayloadHash[Index] >= TEXT('a') && Receipt.PayloadHash[Index] <= TEXT('f'));
        if (!CanonicalGuid(Receipt.RequestId) || Requests.Contains(Receipt.RequestId.ToUpper()) || !bHashValid || Receipt.CommandSerial <= Previous || Receipt.CommandSerial > ShopSerial
            || !Contains(Receipt.WeaponId) || Receipt.ChargedGold < 0 || Receipt.OwnershipRevision < 0 || Receipt.OwnershipRevision > Revision
            || (!Receipt.bBought && Receipt.ChargedGold != 0))
        { UE_LOG(LogFPSDemo, Warning, TEXT("OWNERSHIP_REJECT receipt")); return false; }
        if (Receipt.bBought)
        {
            bool bMatches=false; // 成功购买收据必须与同次来源实付一致，不凭收据反向授新权。
            for (const FDemoWeaponOwnershipEntry& Entry:Entries) if (Entry.WeaponId==Receipt.WeaponId && Entry.Source==EDemoWeaponAcquisitionSource::GoldPurchase && Entry.PricePaid==Receipt.ChargedGold) bMatches=true;
            if (!bMatches) { UE_LOG(LogFPSDemo, Warning, TEXT("OWNERSHIP_REJECT receipt/source mismatch")); return false; }
        }
        Requests.Add(Receipt.RequestId.ToUpper()); Previous = Receipt.CommandSerial;
    }
    if ((ShopSerial == 0) != ShopReceipts.IsEmpty() || (ShopSerial > 0 && Previous != ShopSerial))
    { UE_LOG(LogFPSDemo, Warning, TEXT("OWNERSHIP_REJECT high water")); return false; }
    return true;
}
bool FDemoWeaponOwnership::Contains(FName WeaponId) const
{
    DEMO_LOG_STATIC_TICK(nullptr); // 高频值查询不默认刷屏。
    if (WeaponId == TEXT("pistol")) return true;
    for (const FDemoWeaponOwnershipEntry& Entry : Entries) if (Entry.WeaponId == WeaponId) return true;
    return false;
}
TArray<FName> FDemoWeaponOwnership::EffectiveIds() const
{
    DEMO_LOG_STATIC_CALL(nullptr);
    TArray<FName> Result = {FName(TEXT("pistol"))}; // 规范顺序保持JSON指纹稳定。
    for (int32 Index = 1; Index < DemoWeaponCatalog::Num(); ++Index) if (Contains(DemoWeaponCatalog::IdAt(Index))) Result.Add(DemoWeaponCatalog::IdAt(Index));
    return Result;
}
bool FDemoWeaponOwnership::FreezeLegacy(const TArray<FName>& BaselineIds, const FString& MigrationId, int32 SourceRevision)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    if (!Validate() || !CanonicalGuid(MigrationId) || SourceRevision < 0 || BaselineIds.Num() > DemoWeaponCatalog::Num()) return false; // 不用迁移函数为未绑定非法Paid候选补凭据。
    if (!LegacyMigrationId.IsEmpty()) return LegacyMigrationId == MigrationId && Validate(true); // 不重复求并集，迁移后新通关不能扩大旧权。
    FDemoWeaponOwnership Candidate = *this; // 全候选校验后才写调用者暂存状态。
    Candidate.LegacyMigrationId = MigrationId; Candidate.LegacySourceRevision = SourceRevision;
    TSet<FName> Seen; // 拒绝未知/重复旧权限，缺pistol的旧合法缓存也拒绝。
    for (const FName Id : BaselineIds)
    {
        if (DemoWeaponCatalog::IndexOf(Id) < 0 || Seen.Contains(Id)) return false;
        Seen.Add(Id); if (Id == TEXT("pistol")) continue;
        FDemoWeaponOwnershipEntry Entry; // Legacy不消耗钱包；调用者按旧库存显式冻结供弹消费标记。
        Entry.WeaponId = Id; Entry.AcquisitionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
        Candidate.Entries.Add(Entry);
    }
    if (!Seen.Contains(TEXT("pistol")) || !Candidate.Validate(true)) return false;
    *this = MoveTemp(Candidate); return true;
}
bool FDemoWeaponOwnership::AddPurchased(FName WeaponId, const FString& AcquisitionId, int32 Price, int32 OfferRevision)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    if (!Validate(true) || Contains(WeaponId) || DemoWeaponCatalog::IndexOf(WeaponId) <= 0 || Revision == MAX_int32 || Price < 0 || OfferRevision <= 0 || !CanonicalGuid(AcquisitionId))
    { UE_LOG(LogFPSDemo, Log, TEXT("OWNERSHIP_PURCHASE rejected source/value")); return false; }
    FDemoWeaponOwnershipEntry Entry; // 只有外层扣款/库存/整槽持久完成才发布该候选。
    Entry.WeaponId=WeaponId; Entry.AcquisitionId=AcquisitionId; Entry.Source=EDemoWeaponAcquisitionSource::GoldPurchase; Entry.PricePaid=Price; Entry.OfferRevision=OfferRevision; Entry.bInitialSupplyGranted=true;
    Entries.Add(Entry); ++Revision; return true;
}

#include "Game/DemoUpgradeProgress.h"
#include "Debug/DemoLog.h"
#include "Economy/DemoUpgradeCatalog.h"

bool FDemoUpgradeProgress::Validate() const
{
    UE_LOG(LogFPSDemo, Log, TEXT("FDemoUpgradeProgress::Validate")); // 值类型无UObject，不能使用依赖this对象名的宏。
    if (GoldLevels.Num() != 3 || SilverLevels.Num() != 3) { UE_LOG(LogFPSDemo, Warning, TEXT("UPGRADE_LEDGER invalid array size")); return false; }
    for (int32 Choice = 0; Choice < 3; ++Choice) // 先限制各项，后续求和/定价不会溢出。
        if (GoldLevels[Choice] < 0 || GoldLevels[Choice] > 100000 || SilverLevels[Choice] < 0 || SilverLevels[Choice] > 100000)
        { UE_LOG(LogFPSDemo, Warning, TEXT("UPGRADE_LEDGER invalid count at %d"), Choice); return false; }
    const bool bValid = Total(true) + Total(false) <= 100000 // 与检查点购买统计范围保持一致。
        && FMath::IsFinite(PermanentDamage) && PermanentDamage >= 0 && PermanentDamage <= 10000
        && FMath::IsFinite(PermanentHealth) && PermanentHealth >= 0 && PermanentHealth <= 9999900
        && FMath::IsFinite(PermanentMagazine) && PermanentMagazine >= 0 && PermanentMagazine <= 10000;
    if (!bValid) UE_LOG(LogFPSDemo, Warning, TEXT("UPGRADE_LEDGER invalid totals/permanent values"));
    return bValid;
}
int32 FDemoUpgradeProgress::Total(bool bGold) const
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FDemoUpgradeProgress::Total gold=%d"), bGold);
    const TArray<int32>& Levels = bGold ? GoldLevels : SilverLevels; // 同步只读账本，不持有跨World引用。
    int32 Sum = 0; // Validate已限制持久输入；运行时只接受合法购买。
    for (const int32 Level : Levels) Sum += Level; // 每项购买次数，不依赖GAS合计属性。
    return Sum;
}
int32 FDemoUpgradeProgress::Cost(int32 Choice, bool bGold) const
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FDemoUpgradeProgress::Cost choice=%d gold=%d"), Choice, bGold);
    const TArray<int32>& Levels = bGold ? GoldLevels : SilverLevels; // 固定三项，UI不能自行选择权威钱包。
    if (Choice < 0 || Choice > 2 || !Levels.IsValidIndex(Choice))
    { UE_LOG(LogFPSDemo, Warning, TEXT("UPGRADE_COST rejected invalid choice=%d"), Choice); return INDEX_NONE; }
    const UDemoUpgradeCatalog* Catalog = UDemoUpgradeCatalog::Get(); // 价格配置与账本分离；旧永久值不根据新目录重算。
    const FDemoUpgradeDefinition* Item = Catalog ? Catalog->Find(false, Choice) : nullptr; // 稳定ID映射旧三列。
    const int64 Price = Item ? static_cast<int64>(Item->BasePrice) + static_cast<int64>(Item->PriceStep) * Levels[Choice] : -1; // 防止配置与高等级乘法溢出。
    return Price >= 0 && Price <= 100000000 ? static_cast<int32>(Price) : INDEX_NONE;
}
void FDemoUpgradeProgress::ResetTemporary()
{
    UE_LOG(LogFPSDemo, Log, TEXT("FDemoUpgradeProgress::ResetTemporary"));
    SilverLevels = {0, 0, 0}; // 重建完整三项，重置不得改变金币账本。
}

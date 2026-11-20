// 职责：校验并查询三项商品/奖励配置。自定义资产由原生GC引用器保活；交易与玩家账本不属于共享目录。
#include "Economy/DemoUpgradeCatalog.h"
#include "Debug/DemoLog.h"

namespace
{
    /** Index为旧协议0..2，bReward区分商品与奖励；返回稳定键，非法索引无键。 */
    FName UpgradeId(bool bReward, int32 Index)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
        static const FName ShopIds[] = { TEXT("damage"), TEXT("health"), TEXT("magazine") }; // 兼容旧账本三列，不随展示顺序改变。
        static const FName RewardIds[] = { TEXT("damage"), TEXT("healing"), TEXT("dash") }; // 三种明确能力策略，禁止通过资产注入任意GE。
        return Index >= 0 && Index < 3 ? (bReward ? RewardIds[Index] : ShopIds[Index]) : NAME_None;
    }
}
UDemoUpgradeCatalog::UDemoUpgradeCatalog()
{
    DEMO_LOG_CALL();
    const float ShopAmounts[] = {5.f,25.f,4.f}; // 历史默认值，仅此配置源保留常量。
    const float ShopCaps[] = {10000.f,10000000.f,10000.f}; // 受存档/GAS边界约束。
    const float RewardAmounts[] = {10.f,20.f,350.f}; // 默认免费奖励，不重置技能冷却。
    const float RewardCaps[] = {10000.f,100000.f,100000.f}; // 避免无尽指数增长生成不可保存值。
    const TCHAR* ShopTitles[] = {TEXT("武器调校"),TEXT("生命强化"),TEXT("扩容弹匣")}; // 中文只用于展示。
    const TCHAR* RewardTitles[] = {TEXT("强效弹药"),TEXT("战地医疗"),TEXT("机动强化")}; // 权限与文字解耦。
    for (int32 Index = 0; Index < 3; ++Index) // 生成原生默认条目，无磁盘资产写入。
    {
        FDemoUpgradeDefinition Item; // 初始化同一稳定策略的数值/展示配置。
        Item.Id=UpgradeId(false,Index); Item.Title=FText::FromString(ShopTitles[Index]); Item.Increment=ShopAmounts[Index]; Item.Cap=ShopCaps[Index]; Shop.Add(Item);
        Item.Id=UpgradeId(true,Index); Item.Title=FText::FromString(RewardTitles[Index]); Item.Increment=RewardAmounts[Index]; Item.Cap=RewardCaps[Index]; Rewards.Add(Item);
    }
}
const FDemoUpgradeDefinition* UDemoUpgradeCatalog::Find(bool bReward, int32 Index) const
{
    DEMO_LOG_TICK();
    const FName Id = UpgradeId(bReward,Index); // 业务索引转为稳定ID，不把资产重排误认成玩家账本迁移。
    for (const FDemoUpgradeDefinition& Item : bReward ? Rewards : Shop) if (Item.Id == Id && !Id.IsNone()) return &Item;
    return nullptr;
}
bool UDemoUpgradeCatalog::Validate() const
{
    DEMO_LOG_CALL();
    if (Shop.Num()!=3 || Rewards.Num()!=3) { UE_LOG(LogFPSDemo, Error, TEXT("UPGRADE_CONFIG requires three compatible strategies per catalog")); return false; }
    for (int32 Kind=0; Kind<2; ++Kind) // 0商品/1奖励，检查持久格式与物理单位边界。
        for (int32 Index=0; Index<3; ++Index)
        {
            const FDemoUpgradeDefinition* Item=Find(Kind==1,Index); // 找不到稳定ID（含重复替代）即拒绝。
            const float HardCap=Index==0?10000.f:(Kind==1?100000.f:(Index==1?10000000.f:10000.f)); // 服务器及存档共有硬限制。
            if (!Item || Item->Title.IsEmpty() || !FMath::IsFinite(Item->Increment) || Item->Increment<=0 || !FMath::IsFinite(Item->Cap)
                || Item->Cap<=0 || Item->Cap>HardCap || Item->Increment>Item->Cap || Item->BasePrice<0 || Item->BasePrice>1000000
                || Item->PriceStep<0 || Item->PriceStep>1000 || (Kind==0 && Index==2 && (Item->Increment!=FMath::FloorToFloat(Item->Increment) || Item->Cap!=FMath::FloorToFloat(Item->Cap))))
            { UE_LOG(LogFPSDemo, Error, TEXT("UPGRADE_CONFIG invalid kind=%d index=%d"),Kind,Index); return false; }
        }
    return true;
}
const UDemoUpgradeCatalog* UDemoUpgradeCatalog::Get()
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    UDemoUpgradeSettings* Settings=GetMutableDefault<UDemoUpgradeSettings>(); // 不跨线程修改，运行时由游戏线程首次访问。
    if (!Settings->bResolved)
    {
        Settings->bResolved=true;
        Settings->Loaded.Reset(Settings->CatalogAsset.IsNull()?GetMutableDefault<UDemoUpgradeCatalog>():Settings->CatalogAsset.LoadSynchronous());
        if (!Settings->Loaded.IsValid() || !Settings->Loaded->Validate()) { Settings->Loaded.Reset(); UE_LOG(LogFPSDemo, Error, TEXT("UPGRADE_CONFIG unavailable; purchases/rewards disabled")); }
    }
    return Settings->Loaded.Get();
}

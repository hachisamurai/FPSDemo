// 职责：本槽个人永久枪权来源与枪墙幂等收据。使用：Progress/检查点共享值，免费手枪固定可用；不从库存或新通关反推权利。
#pragma once
#include "CoreMinimal.h"
#include "DemoWeaponOwnership.generated.h"

/** 永久取得来源；Robbery仅预留，P0不提供抢夺授权入口。 */
UENUM()
enum class EDemoWeaponAcquisitionSource : uint8 { GoldPurchase, Legacy, Robbery };

/** 单条已提交永久权，不含Actor/资源路径；AcquisitionId用于来源审计，不是云端签名。 */
USTRUCT()
struct FPSDEMO_API FDemoWeaponOwnershipEntry
{
    GENERATED_BODY()
    UPROPERTY() FName WeaponId; // 固定目录主枪ID，不记录免费pistol。
    UPROPERTY() FString AcquisitionId; // 本次获得的规范GUID，生命周期为本槽此成员。
    UPROPERTY() EDemoWeaponAcquisitionSource Source = EDemoWeaponAcquisitionSource::Legacy; // 冻结来源，不能由UI自选。
    UPROPERTY() int32 PricePaid = 0; // 实付金币；Legacy/Robbery为0，GoldPurchase可因Editor免扣为0，正式报价仍为正。
    UPROPERTY() int32 OfferRevision = 0; // 当时服务端报价版本；Legacy为0。
    UPROPERTY() bool bInitialSupplyGranted = false; // 一次供弹资格；Legacy旧库存已有此枪才消费，首购保存时true。
};

/** 只发本人的持久枪墙成功收据；淘汰旧收据后ShopSerial高水位仍拒绝重放。 */
USTRUCT()
struct FPSDEMO_API FDemoWeaponShopReceipt
{
    GENERATED_BODY()
    UPROPERTY() FString RequestId; // 客户端规范GUID，仅用于幂等，不授予身份。
    UPROPERTY() FString PayloadHash; // sha1:40位小写HEX，冻结完整命令载荷。
    UPROPERTY() int32 CommandSerial = 0; // 本槽个人单调序号，达到int32上限拒绝新交易。
    UPROPERTY() FName WeaponId; // 实际装备目录ID。
    UPROPERTY() int32 ChargedGold = 0; // 本次真实扣款，已拥有装备为0。
    UPROPERTY() int32 OwnershipRevision = 0; // 成功提交时来源账本版本。
    UPROPERTY() bool bBought = false; // true首次购买，false免费装备；不由客户端结果决定。
};

/** 唯一本槽个人枪权来源；UnlockedWeaponIds仅由EffectiveIds派生。 */
USTRUCT()
struct FPSDEMO_API FDemoWeaponOwnership
{
    GENERATED_BODY()
    UPROPERTY() int32 Schema = 1; // 当前来源格式1，未知版本保护原档。
    UPROPERTY() int32 Revision = 0; // 永久来源变化单调版本，钱包变化不增加。
    UPROPERTY() FString LegacyMigrationId; // 冻结旧权的一次GUID；空只允许尚未绑定的空运行态。
    UPROPERTY() int32 LegacySourceRevision = 0; // 冻结时原Profile或成员版本；不按后续Clears推进。
    UPROPERTY() TArray<FDemoWeaponOwnershipEntry> Entries; // 最多3个独立主枪；pistol固定免费。
    UPROPERTY() int32 ShopSerial = 0; // 成功枪墙命令高水位，收据淘汰也不回退。
    UPROPERTY() TArray<FDemoWeaponShopReceipt> ShopReceipts; // 顺序递增最多16条，不把失败交易当永久成功。
    /** 存盘/提交前校验来源和收据；bRequireFrozen为true时要求持久迁移GUID，false仅允许空未绑定默认。 */
    bool Validate(bool bRequireFrozen = false) const;
    /** 高频权限查询，只认Entries与免费手枪，不解析通关或库存。 */
    bool Contains(FName WeaponId) const;
    /** 返回确定目录顺序的有效权缓存，调用者持有值副本。 */
    TArray<FName> EffectiveIds() const;
    /** 仅首次冻结合法旧权限；MigrationId/SourceRevision来自已验证Profile/旧成员水位，新档空基线也要冻结。 */
    bool FreezeLegacy(const TArray<FName>& BaselineIds, const FString& MigrationId, int32 SourceRevision);
    /** 交易暂存值加入首次金币权；不保存/发复制，外层必须整槽耐久成功后发布。 */
    bool AddPurchased(FName WeaponId, const FString& AcquisitionId, int32 Price, int32 OfferRevision);
};

// 职责：商店/关卡奖励内容定义与目录配置。依赖原生GC强引用保活自定义资产，避免配置CDO在切图GC时引用普通运行对象。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/StrongObjectPtr.h" // 原生FGCObject引用器；不使用CDO上的UPROPERTY运行时硬引用。
#include "DemoUpgradeCatalog.generated.h"

/** 商品定义与玩家账本分离。ID固定兼容V5，数值配置可变，已购永久值不重新计算。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoUpgradeDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id; // 稳定业务键，必须为本类别约定的三个ID之一。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Title; // UI展示名，由服务器商品投影发送给拥有者。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0.01")) float Increment = 1.f; // 单次实际属性增量；弹匣必须为整数发数。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) float Cap = 10000.f; // 当前合计属性上限，不能超过存档协议的硬限制。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1000000")) int32 BasePrice = 20; // 两币种各自从此价格起算；奖励条目不收费。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1000")) int32 PriceStep = 10; // 仅此商品、此币种每次购买的涨价额。
};

/** 可由Editor创建DataAsset配置，无资产时用原生兼容默认值，绝不手写uasset。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoUpgradeCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 初始化原有三项商店和三项关卡奖励，保持旧档价格及玩法不变。 */
    UDemoUpgradeCatalog();
    /** 严格校验ID、数值、上限；非法目录阻止交易/奖励，不静默改用另一套报价。 */
    bool Validate() const;
    /** bReward决定目录；Index映射旧0..2协议，不依赖资产数组排列顺序。 */
    const FDemoUpgradeDefinition* Find(bool bReward, int32 Index) const;
    /** 游戏线程首次加载配置并由原生GC强引用保活；失败返回nullptr，目录只读，不持有玩家数据。 */
    static const UDemoUpgradeCatalog* Get();
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FDemoUpgradeDefinition> Shop; // damage/health/magazine，实际成长与账本索引固定对应。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FDemoUpgradeDefinition> Rewards; // damage/healing/dash，允许到上限后领取但增量饱和。
};

UCLASS(Config=Game, DefaultConfig)
class FPSDEMO_API UDemoUpgradeSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere) TSoftObjectPtr<UDemoUpgradeCatalog> CatalogAsset; // 可选目录；部署时将自定义目录所在文件夹纳入Cook。
    TStrongObjectPtr<UDemoUpgradeCatalog> Loaded; // 非反射原生GC强引用，随Settings析构释放；不序列化/复制，绝不保存个人购买数据。
    bool bResolved = false; // 每次试玩进程只解析一次；错误不在HUD高频查询中反复刷日志。
};

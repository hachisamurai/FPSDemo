#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoAmmoCatalog.generated.h"

/** 单项展示与解锁配置，索引0普通/1火焰/2冰冻/3穿透；ID固定保证旧存档兼容。 */
USTRUCT(BlueprintType)
struct FDemoAmmoEntry
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FName Id; // 稳定协议ID，不使用显示名匹配存档。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Name; // 本地化名称，UI不硬编码价格。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<class UTexture2D> Icon; // 目录强引用既有图标，随目录加载并自动Cook。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 UnlockGoldCost = 0; // 一次性金币整数价格，0表示免费领取。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUnlockedByDefault = false; // 新档免购买；普通弹始终可用。
};

/** Editor可编辑的单一目录/平衡数据源，运行时只读；不修改GE的共享CDO。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoAmmoCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 设置示例价格与现成ElementIcons引用，资产创建后编辑该实例即可覆盖数值。 */
    UDemoAmmoCatalog();
    /** 校验四个稳定ID及有限正值/层数范围；失败拒绝特殊弹药。 */
    bool Validate() const;
    /** Index为0..3，返回稳定ID，无效索引返回None。 */
    static FName IdAt(int32 Index);
    /** Id为保存的稳定标识，未知返回INDEX_NONE。 */
    static int32 IndexOf(FName Id);
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FDemoAmmoEntry> Entries; // 四项展示/购买配置，不允许删除或改变ID顺序。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float BurnDuration = 4.f; // 每次新层刷新整组寿命，秒。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float BurnPeriod = 1.f; // DOT结算周期秒，叠层不重置周期。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float BurnDamagePerStack = 3.f; // 每周期每层HP，GAS自动乘栈数。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 BurnThreshold = 5; // 1..32，达到即爆炸并清层。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ExplosionDamage = 40.f; // 满层单体附加HP，不再触发弹药效果。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ChillDuration = 4.f; // 冰霜层共用寿命秒。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SlowPerStack = .1f; // 每层减速比例，0..0.9。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxSlow = .4f; // 总减速上限，0..0.9。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 FreezeThreshold = 5; // 1..32，达到后定身并清层。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float FreezeDuration = 2.f; // 定身秒数，不阻止攻击。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PostThawImmunityDuration = 6.f; // 解冻后冰霜免疫秒数，允许0。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float PiercingDamageMultiplier = 1.25f; // 首目标理论伤害倍率。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float SecondaryDamageRatio = .5f; // 后目标相对首目标理论伤害比例。
};

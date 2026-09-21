#pragma once
#include "CoreMinimal.h"
#include "DemoUpgradeProgress.generated.h"

/** 按存档栏位持有的成长账本；GAS保存合计值，账本只记录永久来源与每项价格，不能反推来源。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoUpgradeProgress
{
    GENERATED_BODY()
    // 固定0伤害/1生命/2弹匣；金币购买次数跨死亡/通关保留，每项仅影响自己的金币价格。
    UPROPERTY(BlueprintReadOnly) TArray<int32> GoldLevels = {0, 0, 0};
    // 同一索引顺序；银币购买次数只在本轮有效，结束挑战清零。
    UPROPERTY(BlueprintReadOnly) TArray<int32> SilverLevels = {0, 0, 0};
    // 永久增量单独存值，未来调价或改增量不能反向改变已经购买的属性。
    UPROPERTY(BlueprintReadOnly) float PermanentDamage = 0.f; // 伤害加值0..10000。
    UPROPERTY(BlueprintReadOnly) float PermanentHealth = 0.f; // 生命上限额外HP，0..9999900，不含初始100。
    UPROPERTY(BlueprintReadOnly) float PermanentMagazine = 0.f; // 额外弹匣发数0..10000。
    /** 只读校验数组长度、次数及永久值；存档读写和重置前调用，失败不能覆盖原数据。 */
    bool Validate() const;
    /** bGold指定钱包；返回三项总次数，仅供统计与存档一致性校验，不参与单项价格。 */
    int32 Total(bool bGold) const;
    /** Choice=0..2，bGold指定币种；非法索引返回INDEX_NONE，调用者必须拒绝交易。 */
    int32 Cost(int32 Choice, bool bGold) const;
    /** 结束一轮挑战时仅清空银币价格，永久金币价格与属性保留。 */
    void ResetTemporary();
};

#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Contracts/DemoGameplayContracts.h"
#include "DemoLoadoutTransaction.generated.h"
class ADemoWeaponBase;

/** 单次同步恢复暂存区：保留原Actor以便整队失败时无需重新生成旧装备。仅库存组件访问。 */
UCLASS()
class UDemoLoadoutTransaction : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY() TArray<TObjectPtr<ADemoWeaponBase>> BeforePrimary; // 原库存强引用，事务提交前禁止销毁。
    UPROPERTY() TObjectPtr<ADemoWeaponBase> BeforeSecondary; // 原副枪，回滚直接恢复相同实例。
    UPROPERTY() TObjectPtr<ADemoWeaponBase> BeforeActive; // 原当前装备及动画目标。
    int32 BeforeSlot = 2; // 原活动槽位。
    int32 BeforeIndex = INDEX_NONE; // 原主枪选择；无主枪为INDEX_NONE。
    UPROPERTY() TArray<TObjectPtr<ADemoWeaponBase>> PreparedPrimary; // 新库存准备成功前不写入复制字段。
    UPROPERTY() TObjectPtr<ADemoWeaponBase> PreparedSecondary; // 新副枪；失败销毁，不影响旧库存。
    FDemoLoadoutSnapshot Target; // 经过校验的目标值副本，避免调用方修改原入参。
    FDemoLoadoutSnapshot BeforeAmmo; // 原弹药类型选择/解锁值，不含临时GE句柄。
    bool bInstalled = false; // 新库存已安装但还未整队确认，需要支持回滚。
};

// 职责：B08抵近爆膛近距膛压、整枪多弹丸满压震爆与ABCD分支结算。由原RunBuildAbility持有，不新增ASC或存档字段。
#pragma once
#include "CoreMinimal.h"
#include "GAS/Talents/Builds/DemoBuildRuntime.h"
#include "DemoCloseQuartersBuildRuntime.generated.h"
class ADemoEnemy;
class ADemoWeaponBase;

/** 本枪在近距区内的弹丸与实伤累计；整枪所有弹丸终结后一次结算并销毁。 */
struct FDemoCloseQuartersShot
{
    int32 ClosePellets=0; // 本枪在CloseRangeCm内实际造成HP损失的弹丸数。
    float CloseDamage=0.f; // 本枪近距弹丸累计真实HP损失。
    int64 Serial=0; // 服务器提交序号，用于有界账本淘汰最旧记录。
    TWeakObjectPtr<ADemoEnemy> PrimaryEnemy; // 近距主受击目标，供冲击中心与Boss集中倍率。
    TWeakObjectPtr<ADemoWeaponBase> Weapon; // 原发射枪，B08-A回弹仅返还原枪。
    FVector PrimaryImpact=FVector::ZeroVector; // 最近一次有效近距碰撞点世界cm。
    FVector AimDirection=FVector::ForwardVector; // 发射冻结瞄准方向，供B08-B前方宽扇面。
    int32 PlannedProjectiles=1; // 发射冻结弹体数，区分散弹多弹丸与单发枪。
};

UCLASS(Transient)
class FPSDEMO_API UDemoCloseQuartersBuildRuntime : public UDemoBuildRuntime
{
    GENERATED_BODY()
public:
    /** 处理发射预览/提交、近距直击累计与整枪终结震爆。 */
    virtual void Handle(UDemoRunBuildEventContext& Event) override;
    /** 离场、个人阵亡或Avatar替换时清膛压、在飞账本、散布窗口与临时移速GE。 */
    virtual void ResetRuntime() override;
private:
    /** 达到满膛压时执行非元素震爆、击退、回弹、护幕治疗与震颤易伤。 */
    void TriggerBurst(const FDemoCloseQuartersShot& Shot,const FGuid& ShotId);
    /** 限制在飞整枪映射不超过128项，防极端高射速无界增长。 */
    void BoundPendingShots();
    /** 发布当前膛压/散布窗口低频状态至拥有者HUD。 */
    void PublishState();
    TMap<FGuid,FDemoCloseQuartersShot> PendingShots; // 仅本场在飞枪，ShotResolved移除。
    int32 PressureStacks=0; // 当前累积膛压0..ClosePressureMax。
    float PressureEndTime=0.f; // 膛压过期World游戏秒。
    float TightSpreadEndTime=0.f; // B08-A散布收束窗口截止World游戏秒。
    float NextReloadTime=0.f; // B08-A自动续压回弹CD截止World秒。
    float NextGuardTime=0.f; // B08-C贴身护幕回复CD截止World秒。
    FActiveGameplayEffectHandle KillMoveHandle; // B08-B冲击击杀临时移速GE句柄。
};

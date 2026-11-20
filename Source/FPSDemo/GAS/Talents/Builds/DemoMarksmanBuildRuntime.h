// 职责：B09蓄势贯杀停火/开镜蓄势、满蓄重击与溢伤转化/贯心裂痕/幽灵步态/猎杀标定策略。由原RunBuildAbility持有，无Tick或额外ASC。
#pragma once
#include "CoreMinimal.h"
#include "GAS/Talents/Builds/DemoBuildRuntime.h"
#include "DemoMarksmanBuildRuntime.generated.h"
class ADemoEnemy;

/** B09-B单目标贯心裂痕层数与超时快照，仅存本场弱键，定期修剪。 */
struct FDemoMarksmanRiftState
{
    int32 Stacks=0; // 1..MarksmanRiftMaxStacksB。
    float ExpireTime=0.f; // 裂痕截止World游戏秒。
};

UCLASS(Transient)
class FPSDEMO_API UDemoMarksmanBuildRuntime : public UDemoBuildRuntime
{
    GENERATED_BODY()
public:
    /** 战斗启动时立即满蓄并开启0.1s维护，让首枪即可触发蓄势贯杀。 */
    virtual void OnInitialized() override;
    /** 处理蓄势查询/重置、满蓄直击倍率、溢伤转化、裂痕叠层、冲刺保蓄与标定击杀回充。 */
    virtual void Handle(UDemoRunBuildEventContext& Event) override;
    /** 清理0.1s维护Timer、裂痕表、移速GE并恢复开镜移速。 */
    virtual void ResetRuntime() override;
private:
    /** 按World游戏秒增量推进蓄势进度0..1，开镜与B09-B快速充能自动加速。 */
    void AdvanceFocus();
    /** 0.1s低频维护：更新HUD蓄势进度与B09-C过半蓄势开镜轻步切换。 */
    void Maintain();
    /** 确保维护Timer仅在本场活跃期间运行一次。 */
    void EnsureMaintenance();
    /** 发布蓄势阶段（0..10档）至拥有者HUD，减少低价值网络刷新。 */
    void PublishFocus(bool bForce=false);
    float Focus=0.f; // 当前蓄势进度0..1；开火归零，开镜翻倍速充能。
    float LastFocusTime=0.f; // 上次积分的World游戏秒。
    bool bNextFastCharge=false; // B09-B满蓄弱点命中后下一次蓄势提速标记。
    bool bLastHalfFocused=false; // 上一次通知Movement的B09-C过半状态，减少无变化刷新。
    int32 LastPublishedTier=-1; // 上次发布的0..10整数档，避免同档重复复制。
    FName LastPublishedAction=NAME_None;
    TSet<FGuid> ConsumedShots; // 同枪多弹丸防重复结算溢伤/裂痕/标定，有界修剪。
    TMap<TWeakObjectPtr<ADemoEnemy>,FDemoMarksmanRiftState> Rifts; // B09-B每目标裂痕层数。
    FTimerHandle MaintenanceTimer; // 0.1s UObject单次/循环回调，ResetRuntime清理。
    FActiveGameplayEffectHandle WeakMoveHandle; // B09-C满蓄弱点移速GE句柄。
};

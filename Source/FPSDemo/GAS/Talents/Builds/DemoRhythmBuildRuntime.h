// 职责：B04节律装填的服务器临时策略。使用：原RunBuild GA拥有/分派；复用原装填会话与武器发射事务，不拥有输入或第二ASC。
#pragma once
#include "CoreMinimal.h"
#include "Engine/TimerHandle.h" // 供弹凭据截止定时器的按值成员需要完整定义。
#include "GAS/Talents/Builds/DemoBuildRuntime.h"
#include "DemoRhythmBuildRuntime.generated.h"
class ADemoWeaponBase;

/** 一枪冻结的节律结算凭据；只保存弱武器与数值，终结/离场删除，最多128个飞行中ShotId。 */
struct FDemoRhythmPendingShot
{
    TWeakObjectPtr<ADemoWeaponBase> Weapon; // 原枪备用弹只转回原枪，不能赠给后来切换的枪。
    int64 Serial=0; // 成功扣弹的服务器全序，用于有界回收最老条目。
    int32 MagazineGeneration=0; // 发射时弹匣代次，旧弹不能贡献下一次装填的稳定命中数。
    int32 CreditGeneration=0; // 发射时凭据批次，旧空枪不能清掉后来重新获得的凭据。
    bool bCharged=false; // 真实提交时已消费凭据，失败准备不创建这个事实。
    bool bRefund=false; // 基础/A路线可在首次实际直击时转移备用弹；B短连明确禁止供弹。
    bool bHitCounted=false; // 只承认整枪首个真实敌人HP损失，霰弹/穿透不能重复供弹。
};

UCLASS(Transient)
class FPSDEMO_API UDemoRhythmBuildRuntime : public UDemoBuildRuntime
{
    GENERATED_BODY()
public:
    /** 只处理当前权威战斗的原生动作事实；R意图由服务器时钟判窗，不能由客户端声明成功。 */
    virtual void Handle(UDemoRunBuildEventContext& Event) override;
    /** 整次战斗/个人阵亡/Avatar替换前清理凭据、整GA装填会话和唯一到期Timer。 */
    virtual void ResetRuntime() override;
private:
    /** 同步处理首次装填、一次R判窗与最终完成；逐颗中间周期不能获得重复奖励。 */
    void HandleReload(UDemoRunBuildEventContext& Event);
    /** Preview只冻结；Commit只在已扣弹时消费；AfterHit/ShotResolved据真实HP结算供弹或空枪。 */
    void HandleShot(UDemoRunBuildEventContext& Event);
    /** 切枪消费一次C许可，其他切枪清剩余凭据；已发出的合法ShotId仍按原枪结算。 */
    void HandleWeaponChanged(ADemoWeaponBase* NewWeapon);
    /** 成功完整装填授予有界凭据；Count为配置数量，Weapon为原完成装填的装备实例。 */
    void Award(ADemoWeaponBase* Weapon,int32 Count,bool bCarry);
    /** 游戏线程单次UObject Timer；只撤销到期凭据，不刷新装填、技能或枪械冷却。 */
    UFUNCTION() void OnCreditsExpired();
    /** 低频确认状态发布；不根据客户端进度条或动画帧判成功。 */
    void PublishState();
    /** 删除最早成功提交记录保持128条硬上限，拒绝无界飞行物/旧弱引用积累。 */
    void BoundPendingShots();
    TWeakObjectPtr<ADemoWeaponBase> ReloadWeapon; // 本次完整GA装填的原武器。
    TWeakObjectPtr<ADemoWeaponBase> CreditWeapon; // 尚未消费凭据绑定的当前枪，C最多迁移一次。
    TMap<TWeakObjectPtr<ADemoWeaponBase>,int32> MagazineHits; // 每把库存枪当前弹匣的不同有效ShotId数。
    TMap<TWeakObjectPtr<ADemoWeaponBase>,int32> MagazineGenerations; // 装填开始递增，阻断旧飞行弹给新弹匣计数。
    TMap<FGuid,FDemoRhythmPendingShot> PendingShots; // 真实成功发射事实，ShotResolved移除。
    int32 ReloadSession=0; // 首周期ReloadSequence作为整个GA会话ID，中途逐颗循环不改变。
    int32 ReloadHits=0; // 开始装填前冻结的当前弹匣有效命中数，供D自动路线使用。
    float ReloadStarted=0.f; // 权威武器首周期起始World秒。
    float ReloadDuration=0.f; // 首周期权威时长秒；逐颗武器只在首周期判一次节拍。
    bool bPressed=false; // 一次完整装填最多判定一次R，无效时机也消费这次尝试。
    bool bBeatSuccess=false; // 仅服务器判窗成功，最终合法完成前不授予任何收益。
    bool bBeatCarry=false; // C路线：判窗时仍在有效冲刺后的短许可窗口。
    int32 Credits=0; // 尚未消费的成功开火凭据数；空枪同样消费。
    int32 Generation=0; // 本场每次授予递增，飞行弹保留自己的代次。
    float CreditsEnd=0.f; // 当前凭据截止World秒，不跨场次。
    float ValidDashUntil=0.f; // C路线允许在此之前完成一次节拍的服务器截止秒。
    bool bCarryAvailable=false; // 本批凭据最多跨一次切枪，成功迁移后立即清除。
    FTimerHandle CreditExpiryTimer; // 一次到期回调归本策略对象，Reset显式撤销。
};

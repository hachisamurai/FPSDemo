// 职责：B07反击回路及四个分支的服务器瞬态策略。由原RunBuildAbility拥有；复用原PS ASC/Health GE，不保存受伤预算、冲刺凭据或队友引用。
#pragma once

#include "CoreMinimal.h"
#include "GAS/Talents/Builds/DemoBuildRuntime.h"
#include "DemoCounterBuildRuntime.generated.h"

/** 合法敌方真实损血开启一次固定回收机会；所有收益由真实命中兑现，不复活、不累计受伤或刷新窗口。 */
UCLASS(Transient)
class FPSDEMO_API UDemoCounterBuildRuntime : public UDemoBuildRuntime
{
    GENERATED_BODY()
public:
    /** Event为原组件同步验证的事实；只处理损血、近战、首次弱点直击与实际冲刺完成，不保留Event引用。 */
    virtual void Handle(UDemoRunBuildEventContext& Event) override;
    /** 离场/死亡/Avatar替换由原GA调用；清全部本地窗口后撤销基类原会话引用，不重置玩家技能CD。 */
    virtual void ResetRuntime() override;

private:
    /** 事件边沿淘汰到期机会；已开始的C分支冲刺仅暂存原预算，完成前不能使用过期窗口。 */
    void RefreshCounter();
    /** Loss为已经实际损失的正HP；仅无窗口且个人CD结束时开启，不受后续伤害叠加或续期。 */
    void OpenCounter(float Loss);
    /** Duration为服务器已提交冲刺的预计秒数；只记录一次有界待确认，不据此授予延长窗口。 */
    void BeginCounterDash(float Duration);
    /** Distance为服务器验证的实际位移厘米，失败为0；仅成功完成原次冲刺才给予一次后续短窗口。 */
    void FinishCounterDash(float Distance);
    /** Enemy为本次实际受害者；允许真实致死后仍在当前会话的Actor，拒绝跨World/旧会话。 */
    bool IsCurrentImpact(const ADemoEnemy* Enemy) const;
    /** ActualDamage为近战/首次有效弱点枪击的真实HP；按固定预算消费一次，GE重入前先清机会。 */
    void ConsumeCounter(float ActualDamage, bool bWeakPointRoute);
    /** Radius厘米；仅在本场注册、存活且缺血的队友中取最低HP比例，同值按Actor序号稳定裁决，不保留指针。 */
    ADemoCharacter* FindShareTarget(float Radius) const;
    /** 只清一次机会的预算/冲刺凭据，保留受伤起算CD；不能调用会撤销Owner/Avatar的基类Reset。 */
    void ClearCounter();
    /** 发布OwnerOnly预算HP取整、窗口和个人CD；HUD自行按服务器时钟隐藏过期值，无Tick或持久化。 */
    void PublishCounter();

    float RecoverableBudget = 0.f; // 当前机会剩余固定HP；开窗为min(本次损失×至多0.5,配置上限)，兑现前清零防重入。
    float CounterEndTime = 0.f; // 原World游戏秒截止；C成功冲刺可延长一次，后续受伤不得刷新。
    float NextCounterTime = 0.f; // 开启机会时起算的个人CD截止秒，消费/到期不提前重置。
    float DashResolveDeadline = 0.f; // C分支等待原冲刺完成的有界截止秒；取消/缺回调不会永久保留预算。
    bool bDashClaimed = false; // 当前机会是否已尝试一次C分支保留，防同一预算反复冲刺续期。
    bool bDashPending = false; // 只暂存预算等待根路由的真实位移结果，不代表反击窗口已经延长。
};

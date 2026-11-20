// 职责：Reload独立玩家GA。使用：由ASC授予，行为在对应CPP；沿用原反射类名，资源无需迁移。
#pragma once
#include "GAS/DemoGameplayAbility.h"
#include "DemoReloadAbility.generated.h"
UCLASS(Blueprintable) class FPSDEMO_API UDemoReloadAbility : public UDemoGameplayAbility
{
    GENERATED_BODY()
public:
    /** Combat/Intermission允许装填，等待武器配置秒数；活动期间持有Reloading标签，离开原阶段撤销任务。 */
    UDemoReloadAbility();
    /** GAS上下文Handle/ActorInfo及可选Tags只借用本次调用；额外检查当前武器确实可装填。 */
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
    /** Handle/ActorInfo/ActivationInfo为本次GAS上下文，TriggerEventData只借用；锁定实例后启动唯一补弹任务。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
    /** 上下文沿用激活；bReplicateEndAbility/bWasCancelled为GAS结束标记，先清凭据再取消对应武器。 */
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
private:
    /** 启动当前锁定武器的一次装填周期并建立权威延时任务；循环不更换GAS实例或Avatar。 */
    bool StartReloadCycle();
    /** 每次任务回调重查Avatar、生命、Combat/Intermission枪械许可、装备和序号，不借用已失效的枪/角色。 */
    bool IsReloadContextValid() const;
    /** 同一周期的权威插入回调。转移整匣或一颗，整匣此时通知流派成功；余下时长用于回握。 */
    UFUNCTION() void OnAmmoInserted();
    /** 游戏线程WaitDelay回调；必须仍是同一Avatar/装备/装填序号才完成，过期回调不补弹。 */
    UFUNCTION() void OnReloadFinished();
    TWeakObjectPtr<class ADemoWeaponBase> ReloadWeapon; // 本次激活锁定的实例，切枪/死亡后不延长旧Pawn寿命。
    int32 ReloadSequence = 0; // 对应武器动作序号，旧任务不能补充下一次装填的弹药。
    TWeakObjectPtr<class ADemoCharacter> ReloadPawn; // 首次激活冻结的Avatar；重生后不向新角色发送旧装填结算。
    int32 ReloadSessionSequence = 0; // 整次GA装填的首周期序号；逐颗循环始终使用同一核心会话ID。
    bool bReloadSessionNotified = false; // Started成功发布后，End仅发布一次Finished，防同步取消重入。
    bool bFinalCycleCompleted = false; // 只有最后合法周期真正完成才可奖励；取消/任务失败/陈旧回调均为false。
};

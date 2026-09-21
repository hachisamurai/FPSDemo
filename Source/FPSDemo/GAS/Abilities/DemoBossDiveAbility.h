#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AI/DemoBossDiveSettings.h"
#include "NativeGameplayTags.h"
#include "DemoBossDiveAbility.generated.h"

namespace DemoBossTags
{
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Invulnerable); // 仅免疫负向生命GE及新攻击Debuff，不免疫治疗。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Casting); // 俯冲全过程持有，旧地面/全图/飞行物入口同样检查。
}
/** 由技能显式移除的无敌GE，只在悬停阶段存在；取消时按Handle精确移除。 */
UCLASS()
class FPSDEMO_API UDemoBossInvulnerableEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    /** 授予Invulnerable标签，生命周期归活动GA，避免定时到期与低帧率状态切换错位。 */
    UDemoBossInvulnerableEffect();
};
/** 技能进度供调试/测试只读，Landing硬直属于Recovery。 */
UENUM()
enum class EDemoDivePhase : uint8 { None, Rising, Hovering, Diving, Recovery };

/** 原生服务器GAS技能：Enemy Tick驱动，不另开定时器/异步Lambda，不依赖行为树。 */
UCLASS()
class FPSDEMO_API UDemoBossDiveAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    /** 配置按Actor实例化与施法标签；所有清理集中EndAbility。 */
    UDemoBossDiveAbility();
    /** GAS激活上下文/可选标签仅同步借用；检查Boss、Combat、冷却、冻结与旧技能互斥。 */
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayTagContainer* SourceTags=nullptr,const FGameplayTagContainer* TargetTags=nullptr,FGameplayTagContainer* OptionalRelevantTags=nullptr) const override;
    /** 提交技能后冻结配置/关卡、检查升空通道；TriggerEventData不跨帧保存。 */
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,const FGameplayEventData* TriggerEventData) override;
    /** GAS结束/取消上下文；精确移除无敌、特效、移动忽略并在需要时沿原路径安全回收。 */
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,const FGameplayAbilityActorInfo* ActorInfo,
        const FGameplayAbilityActivationInfo ActivationInfo,bool bReplicateEndAbility,bool bWasCancelled) override;
    /** DeltaSeconds为World帧秒；Enemy通过技能互斥分支驱动，暂停不会积攒移动。 */
    void Advance(float DeltaSeconds);
    /** 只读状态/落点供测试和调试，不允许外部推进技能。 */
    EDemoDivePhase GetPhase() const;
    FVector GetLandingPoint() const;
private:
    /** Next为下一阶段；重置World时刻并记录转换，悬停时开始无敌与扩散光。 */
    void SetPhase(EDemoDivePhase Next);
    /** 3秒结束时只锁定一次玩家位置，投影合法地面并检查容积/俯冲通道。 */
    bool LockLanding();
    /** Start/End为Boss中心世界cm，Target为本次玩家；球形Sweep禁止飞行穿墙。 */
    bool ClearFlight(const FVector& Start,const FVector& End,const class ADemoCharacter* Target) const;
    /** 一次性落地结算：范围/高度/视线/冲刺窗口，固定50伤害后条件击退。 */
    void Impact();
    /** 中断时按飞行经过的Top/Origin逆向扫掠回收；动态阻挡记录原因，不穿墙瞬移。 */
    void ReturnToGround();
    FDemoBossDiveSettings Settings; // 本次施法配置值快照，不读热变更表。
    EDemoDivePhase Phase=EDemoDivePhase::None; // 由GA拥有，结束后清空。
    FVector Origin=FVector::ZeroVector; // 起飞前中心cm，取消的安全回收端点。
    FVector Top=FVector::ZeroVector; // 起飞结束中心cm，飞行折线的中间点。
    FVector Landing=FVector::ZeroVector; // 已验证落地中心cm，固定130cm悬浮高度。
    float PhaseStart=0.f; // 当前阶段World秒，固定3秒从到达Top开始。
    float FlightSeconds=.6f; // 根据距离/限速得到的实际俯冲秒数。
    int32 CastLevel=0; // 施法时逻辑关卡，防同一竞技场复用后旧技能伤人。
    bool bImpacted=false; // 应用GE前消费，阻断重入或多组件重复伤害。
    bool bHasOrigin=false; // 只有完成启动验证才允许取消回收，避免默认零向量传送。
    FActiveGameplayEffectHandle Immunity; // 仅本次无敌GE句柄，不能误删其他技能状态。
    TWeakObjectPtr<class ADemoCharacter> IgnoredPlayer; // 临时只忽略移动Sweep，玩家仍可射击Boss。
    UPROPERTY() TObjectPtr<class ADemoBossDiveVFX> VFX; // 本GA持有纯表现Actor，结束显式销毁，World卸载兜底。
};

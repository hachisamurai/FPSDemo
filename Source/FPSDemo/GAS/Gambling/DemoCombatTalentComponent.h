// 职责：Avatar寿命的命中预算与冲刺凭据适配。使用：武器提交实际伤害、Dash提交成功窗口，收益值来自天赋系统聚合快照。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DemoCombatTalentComponent.generated.h"
class UAbilitySystemComponent;
class ADemoWeaponBase;
/** Pawn寿命的权威触发状态；账本/Tag仍由PS持有，重生不继承回血预算或冲刺凭据。 */
UCLASS()
class FPSDEMO_API UDemoCombatTalentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick；只响应命中与GAS标签边沿。 */
    UDemoCombatTalentComponent();
    /** 订阅Avatar准备事件，ASC晚于组件初始化时也能绑定。 */
    virtual void BeginPlay() override;
    /** Reason为World/Pawn结束原因；先撤销凭据再解绑PS上的委托。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** ActualDamage为本次直击实际损失HP，Ratio为发射冻结比例；Budget为冻结每秒HP上限，0仅兼容直接调用时使用1级配置。 */
    void Siphon(float ActualDamage,float Ratio,float Budget=0.f);
    /** 成功施加DashEvading后调用；冻结本次持枪，仅到期时且仍同枪可转移备用弹。 */
    void ArmDash();
private:
    /** 初次BeginPlay与AvatarReady同步调用；重复绑定幂等，弱对象委托只在游戏线程执行。 */
    void BindASC();
    /** Tag为DashEvading，Count为新计数；0边沿消费一次凭据，死亡/菜单/切枪拒绝。 */
    void OnDashTagChanged(const FGameplayTag Tag,int32 Count);
    TWeakObjectPtr<UAbilitySystemComponent> BoundASC; // PS拥有，组件只借用并在EndPlay解绑。
    TWeakObjectPtr<ADemoWeaponBase> DashWeapon; // 一次成功冲刺的原武器；不保活或跨Pawn。
    float DashFraction=0.f; // 成功冲刺时冻结等级后的装填比例，0..1；中途升级不追补。
    double DashDeadline=-1.; // 冻结真实躲避GE到期的World秒；阶段切换提前撤销Tag不能发放补弹。
    FDelegateHandle AvatarHandle; // Pawn的同步准备回调，EndPlay撤销。
    FDelegateHandle DashHandle; // ASC标签回调，EndPlay或ASC改变时撤销。
    double HealWindowStart=-1.; // World秒，首次有效回血开始一秒预算窗口。
    float HealingUsed=0.f; // 当前窗口已经提交的HP；提交前递增防止同步GE重入。
};

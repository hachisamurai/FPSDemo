// 职责：扩展核心的瞬态策略基类与共用权威结算。由原RunBuildAbility持有，不授予第二个ASC或能力句柄。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "GameplayEffectTypes.h"
#include "DemoBuildRuntime.generated.h"
class UDemoRunBuildComponent;
class UDemoRunBuildEventContext;
class UDemoCombatSession;
class UAbilitySystemComponent;
class ADemoCharacter;
class ADemoEnemy;

UCLASS(Abstract,Transient)
class FPSDEMO_API UDemoBuildRuntime : public UObject
{
    GENERATED_BODY()
public:
    /** 原GA创建后调用一次，冻结原Avatar与CombatSession；不持久化。 */
    void Initialize(UDemoRunBuildComponent* Component);
    /** 初始化冻结Avatar/Session后由子类按需启动初始状态（如B09开战即蓄势），默认空操作。 */
    virtual void OnInitialized() {}
    /** 同步服务器事实入口；派生实现不可跨调用栈保留Event引用。 */
    virtual void Handle(UDemoRunBuildEventContext& Event);
    /** 离场、销毁或Avatar替换清理；派生先撤Timer/Actor再调用基类。 */
    virtual void ResetRuntime();
    /** 返回原组件World；不回退全局或Player0。 */
    virtual UWorld* GetWorld() const override;
protected:
    /** 以下指针只借用当前同步栈；失效返回空。 */
    UDemoRunBuildComponent* Build() const;
    ADemoCharacter* Pawn() const;
    UAbilitySystemComponent* ASC() const;
    /** 选择值快照，不借用跨GE可变状态。 */
    FDemoRunBuildSnapshot Selection() const;
    /** 统一权威、同Avatar、同会话、Combat和生命门禁。 */
    bool Active() const;
    /** 当前World游戏时间秒，不使用墙钟。 */
    float Now() const;
    /** 对当前合法敌人施加派生GE，返回真实HP损失；来源为原Talent GA防递归。 */
    float Damage(ADemoEnemy* Enemy,float Amount) const;
    /** 对同World活玩家治疗，返回真实HP增量，不能复活。 */
    float Heal(ADemoCharacter* Target,float Amount) const;
    /** 有界、视线阻挡的敌人范围伤害；方向非零时按ConeCos裁锥；返回实际伤敌数。 */
    int32 Pulse(FVector Center,float Radius,float Amount,int32 Limit,ADemoEnemy* Exclude=nullptr,FVector Direction=FVector::ZeroVector,float ConeCos=-1.f);
    /** 有界、视线阻挡的敌人列表采集，按距离/唯一ID稳定排序；供B08击退易伤及B09-A守恒均分复用。 */
    void CollectVisibleEnemies(FVector Center,float Radius,int32 Limit,TArray<ADemoEnemy*>& OutTargets,ADemoEnemy* Exclude=nullptr,FVector Direction=FVector::ZeroVector,float ConeCos=-1.f) const;
    /** 为原Avatar施加或刷新单槽临时移速GE；ResetRuntime由子类先移除句柄，不残留跨场增益。 */
    void ApplyTemporaryMoveSpeed(float Bonus,float Seconds,FActiveGameplayEffectHandle& Handle) const;
    /** 清除先前施加的临时移速GE，安全忽略失效ASC/句柄。 */
    void ClearTemporaryMoveSpeed(FActiveGameplayEffectHandle& Handle) const;
    /** 发布拥有者HUD的低频状态；截止与冷却为服务器World秒。 */
    void Publish(FName Action,int32 Stacks,int32 Capacity,float EndTime=0.f,float Cooldown=0.f);
private:
    TWeakObjectPtr<UDemoRunBuildComponent> OwnerBuild; // 原PS组件弱引用，组件/GA负责保活策略。
    TWeakObjectPtr<ADemoCharacter> OriginalPawn; // 禁止重生Avatar继承窗口。
    TWeakObjectPtr<UDemoCombatSession> Session; // 禁止跨关结算。
};

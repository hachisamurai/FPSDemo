#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GAS/Ammo/DemoAmmoEffectSnapshot.h"
#include "DemoAmmoStatus.generated.h"
class UAbilitySystemComponent;

/** 敌人Debuff协调器；ActiveGE负责层数和计时，本组件只处理阈值及派生移动效果。 */
UCLASS()
class FPSDEMO_API UDemoAmmoStatus : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 绑定目标ASC的添加/移除事件，使用弱UObject委托。 */
    virtual void BeginPlay() override;
    /** EndPlayReason为卸载原因；清理效果、委托及GC派生对象。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** Source为玩家ASC，Type=1/2，Config为兼容旧调用的目录；立即复制数值再转ApplySnapshot，不保留目录引用。 */
    void Apply(UAbilitySystemComponent* Source,int32 Type,const class UDemoAmmoCatalog* Config);
    /** Source为借用的权威源ASC，Type=1火/2冰，Snapshot为开火时值快照；已有同类栈沿用首层参数直到清除。 */
    void ApplySnapshot(UAbilitySystemComponent* Source,int32 Type,const FDemoAmmoEffectSnapshot& Snapshot);
    /** 死亡立即移除所有弹药状态；重复调用安全，不改变其他GAS效果。 */
    void Clear();
    /** Cue/Event/Parameters由GAS分发，只更新本地表现，绝不结算伤害。 */
    void HandleCue(FGameplayTag Cue,EGameplayCueEvent::Type Event,const FGameplayCueParameters& Parameters);
    /** 只读查询Tag对应GE栈；不存在返回0，供HUD/回归。 */
    int32 Count(FGameplayTag Tag) const;
private:
    /** Target/Spec/Handle为ASC添加回调；绑定新栈并检查第一层阈值。 */
    void Added(UAbilitySystemComponent* Target,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle Handle);
    /** Handle/NewCount/OldCount来自GAS栈委托；游戏线程处理满层消费。 */
    void StackChanged(FActiveGameplayEffectHandle Handle,int32 NewCount,int32 OldCount);
    /** Effect为即将移除的只读效果，移除冰霜时撤销移速GE。 */
    void Removed(const FActiveGameplayEffect& Effect);
    /** 根据剩余GE标签刷新提示，GC只读表现与生命逻辑分离。 */
    void RefreshVisual();
    UPROPERTY() TObjectPtr<UAbilitySystemComponent> ASC; // 敌人自身拥有，不跨World。
    TMap<FActiveGameplayEffectHandle,FDemoAmmoEffectSnapshot> EffectSnapshots; // 每个活跃火/冰GE持有首层纯值副本，随Removed清理。
    struct FPendingSnapshot
    {
        int32 Type = 0; // 当前同步Apply的1火/2冰类型，Added只消费同类的准备上下文。
        FDemoAmmoEffectSnapshot Values; // Apply返回前保活的首层参数，覆盖Added先于Apply返回的同步回调。
    };
    TArray<FPendingSnapshot> PendingSnapshots; // 游戏线程同步嵌套Apply栈；无Lambda/异步裸对象捕获。
    UPROPERTY() TObjectPtr<class UPointLightComponent> Glow; // 状态辉光，无碰撞和伤害。
    TMap<FActiveGameplayEffectHandle,FDelegateHandle> StackBindings; // 每组GE最多一个栈委托。
    FDelegateHandle AddedBinding,RemovedBinding; // BeginPlay注册，EndPlay精确解绑。
    FActiveGameplayEffectHandle SlowHandle; // 当前派生减速GE，冰霜消失同时删除。
    bool bConsuming = false; // 防止消费栈过程同步回调重入。
};

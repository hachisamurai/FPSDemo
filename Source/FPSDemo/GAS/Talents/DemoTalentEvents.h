// 职责：通用击杀事件桥接与有限AOE查询。使用：敌人死亡清除Debuff前捕获快照，向击杀者GAS发送一次事件；二次效果标记禁止递归。
#pragma once
#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "GAS/Ammo/DemoAmmoEffectSnapshot.h"
#include "GameplayEffectTypes.h"
#include "DemoTalentEvents.generated.h"
class ADemoEnemy;
class UAbilitySystemComponent;
/** 仅同步事件持有的死亡值快照；原目标的GE随后可安全清除。 */
UCLASS(Transient)
class FPSDEMO_API UDemoTalentDeathContext : public UObject
{
    GENERATED_BODY()
public:
    int32 BurnStacks=0; // 死亡前真实GE栈，非Tag引用计数。
    FDemoAmmoEffectSnapshot BurnSnapshot; // 首层数值副本，不引用可变弹药资产。
    bool bFrozen=false; // 死亡前是否有冻结GE。
    bool bSecondary=false; // 本次伤害为天赋派生；传播/冰爆禁止递归。
    bool bSpreadBurn=false; // 当前燃烧来自传播；即使后续普通子弹补刀也不再传播。
};
namespace DemoTalentEvents
{
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(FrostBurst); // 冰蓝色范围GC，纯表现。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Kill); // 仅真正击杀者收到，来源保留原ASC。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(TeamKill); // 每个参战者在独立奖励提交后收到。
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(CombatTransient); // 离开战斗/死亡清理，不影响基础技能冷却。
    /** Enemy为已标死但尚未清Debuff的目标；Context为本次Health GE上下文，只同步借用。 */
    void NotifyDeath(ADemoEnemy* Enemy,const FGameplayEffectContextHandle& Context);
    /** Origin为已死源目标，Radius厘米，Limit限定1..8；返回World拥有的可见有效靶，调用前需再验存活。 */
    TArray<ADemoEnemy*> Nearby(ADemoEnemy* Origin,float Radius,int32 Limit);
}
